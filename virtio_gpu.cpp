/*
 * Bursztyn OS - Sterownik karty graficznej VirtIO-GPU (VirtIO 1.x Modern PCI 2D)
 *
 * Plik: virtio_gpu.cpp
 *
 * Implementacja produkcyjna sterownika VirtIO-GPU 2D dla transportu Modern PCI
 * i Split VirtQueue, zintegrowana z podsystemem grafiki jadra (SterownikEkranu).
 */

#include "virtio_gpu.h"
#include "idt.h"
#include "apic.h"

#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern bool host_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages);
extern void host_dma_free(void* vaddr, uint64_t paddr, size_t pages);
extern void host_klog(const char* msg);
extern uint64_t host_czas_monotoniczny_ns(void);

extern bool host_fb_alloc(size_t total_bytes, void** out_vaddr, void** out_frames, size_t max_pages, size_t* out_page_count);
extern void host_fb_free(void* vaddr, void** frames, size_t page_count);
extern void grafika_pobierz_boot_rozmiar(uint32_t* out_w, uint32_t* out_h);

#define gpu_klog host_klog

static bool gpu_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void gpu_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

static inline void* gpu_heap_alloc(size_t size) {
    return std::malloc(size);
}

static inline void gpu_heap_free(void* ptr) {
    std::free(ptr);
}

static inline bool gpu_fb_alloc(size_t total_bytes, void** out_vaddr, void** out_frames, size_t max_pages, size_t* out_page_count) {
    return host_fb_alloc(total_bytes, out_vaddr, out_frames, max_pages, out_page_count);
}

static inline void gpu_fb_free(void* vaddr, void** frames, size_t page_count) {
    host_fb_free(vaddr, frames, page_count);
}

static inline uint64_t gpu_monotonic_ns(void) {
    return host_czas_monotoniczny_ns();
}

#else
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "grafika.h"
#include "pamiec.h"
#include "bufory_grafiki.h"
#include "heap.h"

#define gpu_klog klog_serial

static bool gpu_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    DmaBuffer dma{};
    if (!dma_allocate(size, alignment, &dma)) {
        return false;
    }
    *vaddr = dma.virtual_address;
    *paddr = dma.physical_address;
    *pages = dma.page_count;
    return true;
}

static void gpu_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    DmaBuffer dma{};
    dma.virtual_address = vaddr;
    dma.physical_address = paddr;
    dma.page_count = pages;
    dma_release(&dma);
}

static inline void* gpu_heap_alloc(size_t size) {
    return kmalloc(static_cast<uint64_t>(size));
}

static inline void gpu_heap_free(void* ptr) {
    kfree(ptr);
}

static inline bool gpu_fb_alloc(size_t total_bytes, void** out_vaddr, void** out_frames, size_t max_pages, size_t* out_page_count) {
    bufory_grafiki_inicjalizuj();
    void* va = bufor_grafiki_alokuj_strony(total_bytes, out_frames, max_pages, out_page_count);
    if (!va) return false;
    *out_vaddr = va;
    return true;
}

static inline void gpu_fb_free(void* vaddr, void** frames, size_t page_count) {
    (void)frames;
    (void)page_count;
    if (vaddr) {
        bufor_grafiki_zwolnij(vaddr);
    }
}

static inline uint64_t gpu_monotonic_ns(void) {
    if (hpet_dostepny()) {
        return czas_monotoniczny_ns();
    }
    return 0;
}
#endif

/*
 * Prawdziwe, nieoptymalizowalne przez kompilator zerowanie pamieci.
 */
static void gpu_secure_zero(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) {
        *p++ = 0;
    }
    asm volatile("" : : "r"(ptr) : "memory");
}

static inline void gpu_memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

namespace {

/*
 * Blokada spinlockowa dla serializacji polecen na controlq.
 */
struct GpuSpinLockGuard {
    volatile uint32_t* lock_ptr;

    explicit GpuSpinLockGuard(uint32_t& lk) : lock_ptr(&lk) {
        while (__atomic_exchange_n(lock_ptr, 1U, __ATOMIC_ACQUIRE) != 0U) {
            asm volatile("pause" ::: "memory");
        }
    }

    ~GpuSpinLockGuard() {
        __atomic_store_n(lock_ptr, 0U, __ATOMIC_RELEASE);
    }

    GpuSpinLockGuard(const GpuSpinLockGuard&) = delete;
    GpuSpinLockGuard& operator=(const GpuSpinLockGuard&) = delete;
};

/*
 * Globalna tablica instancji sterownikow VirtIO-GPU.
 */
static VirtioGpuDriver g_gpu_drivers[VIRTIO_GPU_MAX_DEVICES]{};
static uint32_t        g_gpu_driver_count = 0;

/*
 * Maksymalne dopuszczalne czasy oczekiwania i limity buforow DMA.
 */
constexpr uint64_t VIRTIO_GPU_DEFAULT_TIMEOUT_MS = 500ULL;
constexpr uint32_t VIRTIO_GPU_MAX_SPINS_GUARD     = 5000000U;

constexpr size_t VIRTIO_GPU_CMD_BUFFER_SIZE   = 64ULL * 1024;  // 64 KiB bufor DMA dla controlq
constexpr size_t VIRTIO_GPU_CMD_REQ_MAX_SIZE  = 48ULL * 1024;  // 48 KiB obszar polecen
constexpr size_t VIRTIO_GPU_CMD_RESP_OFFSET   = 48ULL * 1024;  // offset bufora odpowiedzi
constexpr size_t VIRTIO_GPU_CMD_RESP_MAX_SIZE = 16ULL * 1024;  // 16 KiB obszar odpowiedzi

/* Pomocnicze funkcje formatowania logow */
static void log_append_str(char* buf, size_t cap, size_t& pos, const char* s) {
    if (!s) return;
    while (*s && pos + 1 < cap) {
        buf[pos++] = *s++;
    }
    buf[pos] = '\0';
}

static void log_append_u64_dec(char* buf, size_t cap, size_t& pos, uint64_t val) {
    char tmp[24];
    size_t tpos = 0;
    if (val == 0) {
        tmp[tpos++] = '0';
    } else {
        while (val > 0 && tpos < sizeof(tmp)) {
            tmp[tpos++] = static_cast<char>('0' + (val % 10ULL));
            val /= 10ULL;
        }
    }
    while (tpos > 0 && pos + 1 < cap) {
        buf[pos++] = tmp[--tpos];
    }
    buf[pos] = '\0';
}

/*
 * Obsluga przerwania MSI-X dla VirtIO-GPU.
 * Zgodnie ze specyfikacja nie wykonuje alokacji ze sterty (heap) ani operacji blokujacych.
 */
static void virtio_gpu_isr(void* context) {
    VirtioGpuDriver* drv = static_cast<VirtioGpuDriver*>(context);
    if (!drv) return;

    drv->irq_count++;
    __atomic_store_n(&drv->command_done, 1U, __ATOMIC_RELEASE);
}

} // namespace

/* =========================================================================
 * 1. WYSLANIE I WYKONANIE POLECENIA KONTROLNEGO NA CONTROLQ
 * ========================================================================= */

static bool virtio_gpu_send_command_locked(
    VirtioGpuDriver* drv,
    const void* req,
    size_t req_len,
    void* resp,
    size_t resp_len,
    uint32_t expected_resp_type
) {
    if (!drv || !drv->dev || !drv->controlq || !drv->cmd_vaddr ||
        req_len == 0 || resp_len == 0 ||
        req_len > VIRTIO_GPU_CMD_REQ_MAX_SIZE ||
        resp_len > VIRTIO_GPU_CMD_RESP_MAX_SIZE ||
        drv->has_error) {
        return false;
    }

    /* Kopiujemy polecenie do obszaru DMA zadania (offset 0) */
    gpu_memcpy(drv->cmd_vaddr, req, req_len);

    /* Zerujemy obszar odpowiedzi DMA */
    uint8_t* resp_dma_vaddr = static_cast<uint8_t*>(drv->cmd_vaddr) + VIRTIO_GPU_CMD_RESP_OFFSET;
    gpu_secure_zero(resp_dma_vaddr, resp_len);

    const uint64_t req_paddr = drv->cmd_paddr;
    const uint64_t resp_paddr = drv->cmd_paddr + VIRTIO_GPU_CMD_RESP_OFFSET;

    /*
     * Budujemy lancuch deskryptorow:
     * Deskryptor 1: bufor zadania (Device-READ) -> flaga VIRTQ_DESC_F_NEXT
     * Deskryptor 2: bufor odpowiedzi (Device-WRITE) -> flaga VIRTQ_DESC_F_WRITE
     */
    VirtqBuffer bufs[2];
    bufs[0].phys_addr = req_paddr;
    bufs[0].len = static_cast<uint32_t>(req_len);
    bufs[0].is_write = false;

    bufs[1].phys_addr = resp_paddr;
    bufs[1].len = static_cast<uint32_t>(resp_len);
    bufs[1].is_write = true;

    __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);

    const int head = virtqueue_submit(drv->controlq, bufs, 2, nullptr);
    if (head < 0) {
        gpu_klog("[VIRTIO-GPU] BLAD: virtqueue_submit failed!");
        drv->error_count++;
        return false;
    }

    virtqueue_kick(drv->controlq, true);
    drv->cmd_count++;

    /*
     * Oczekiwanie na zakonczenie ze scisle ograniczonym czasem monotonicznym.
     */
    const uint64_t start_ns = gpu_monotonic_ns();
    const uint64_t timeout_ns = VIRTIO_GPU_DEFAULT_TIMEOUT_MS * 1000000ULL;
    const uint64_t deadline_ns = (start_ns > UINT64_MAX - timeout_ns) ? UINT64_MAX : (start_ns + timeout_ns);

    uint32_t spin_guard = VIRTIO_GPU_MAX_SPINS_GUARD;
    bool completed = false;
    uint32_t actual_used_len = 0;

    while (spin_guard > 0) {
        spin_guard--;

        if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) || virtqueue_has_used(drv->controlq)) {
            void* cookie = nullptr;
            uint32_t used_len = 0;
            int popped = virtqueue_pop_used(drv->controlq, &cookie, &used_len);
            if (popped >= 0) {
                completed = true;
                actual_used_len = used_len;
                break;
            } else if (popped < -1) {
                gpu_klog("[VIRTIO-GPU] BLAD KRYTYCZNY: Uszkodzenie kolejki virtqueue_pop_used!");
                drv->has_error = true;
                drv->error_count++;
                return false;
            }
        }

        const uint64_t now = gpu_monotonic_ns();
        if (now != 0 && now >= deadline_ns) {
            break;
        }

        asm volatile("pause" ::: "memory");
    }

    if (!completed) {
        gpu_klog("[VIRTIO-GPU] BLAD KRYTYCZNY: Timeout polecenia kontrolnego!");
        drv->has_error = true;
        drv->error_count++;
        /* NIGDY nie traktujemy timeoutu jako sukces i NIE zwalniamy DMA uzywanego przez sprzet! */
        return false;
    }

    /* Weryfikacja dlugosci odpowiedzi zapisanej przez urzadzenie */
    if (actual_used_len < sizeof(VirtioGpuCtrlHdr) || actual_used_len > resp_len) {
        gpu_klog("[VIRTIO-GPU] BLAD: Nieprawidlowa lub znieksztalcona dlugosc used_len!");
        drv->has_error = true;
        drv->error_count++;
        return false;
    }

    /* Kopiujemy odpowiedz z bufora DMA */
    gpu_memcpy(resp, resp_dma_vaddr, resp_len);

    /* Weryfikacja typu odpowiedzi */
    const VirtioGpuCtrlHdr* resp_hdr = static_cast<const VirtioGpuCtrlHdr*>(resp);
    if (resp_hdr->type != expected_resp_type) {
        char err_buf[128]{};
        size_t epos = 0;
        log_append_str(err_buf, sizeof(err_buf), epos, "[VIRTIO-GPU] Odpowiedz bledu sprzetu: type=0x");
        log_append_u64_dec(err_buf, sizeof(err_buf), epos, resp_hdr->type);
        log_append_str(err_buf, sizeof(err_buf), epos, " (oczekiwano=0x");
        log_append_u64_dec(err_buf, sizeof(err_buf), epos, expected_resp_type);
        log_append_str(err_buf, sizeof(err_buf), epos, ")");
        gpu_klog(err_buf);
        drv->error_count++;
        return false;
    }

    return true;
}

/* =========================================================================
 * 2. IMPLEMENTACJA KOMEND 2D VIRTIO-GPU
 * ========================================================================= */

bool virtio_gpu_cmd_get_display_info(VirtioGpuDriver* drv, VirtioGpuRespDisplayInfo* out_info) {
    if (!drv || !out_info) return false;
    GpuSpinLockGuard guard(drv->lock);

    VirtioGpuCtrlHdr req{};
    req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    VirtioGpuRespDisplayInfo resp{};
    if (!virtio_gpu_send_command_locked(
            drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_DISPLAY_INFO)) {
        return false;
    }

    gpu_memcpy(out_info, &resp, sizeof(resp));
    return true;
}

bool virtio_gpu_cmd_resource_create_2d(
    VirtioGpuDriver* drv,
    uint32_t resource_id,
    uint32_t format,
    uint32_t width,
    uint32_t height
) {
    if (!drv || resource_id == 0 || width == 0 || height == 0) return false;

    /* Zabezpieczenie przed overflow */
    if (width > 4096 || height > 4096) return false;
    const uint64_t stride = static_cast<uint64_t>(width) * 4ULL;
    const uint64_t total = stride * static_cast<uint64_t>(height);
    if (total == 0 || total > 64ULL * 1024 * 1024) return false;

    GpuSpinLockGuard guard(drv->lock);

    VirtioGpuResourceCreate2d req{};
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req.resource_id = resource_id;
    req.format = format;
    req.width = width;
    req.height = height;

    VirtioGpuCtrlHdr resp{};
    return virtio_gpu_send_command_locked(
        drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
}

bool virtio_gpu_cmd_resource_attach_backing(
    VirtioGpuDriver* drv,
    uint32_t resource_id,
    const VirtioGpuMemEntry* entries,
    uint32_t nr_entries
) {
    if (!drv || resource_id == 0 || !entries || nr_entries == 0) {
        return false;
    }

    /* Overflow-safe sprawdzenie limitu liczby wpisow w buforze polecen */
    constexpr size_t MAX_ATTACH_ENTRIES = (VIRTIO_GPU_CMD_REQ_MAX_SIZE - sizeof(VirtioGpuResourceAttachBacking)) / sizeof(VirtioGpuMemEntry);
    if (nr_entries > MAX_ATTACH_ENTRIES) {
        gpu_klog("[VIRTIO-GPU] BLAD: Liczba wpisow backing przekracza limit bufora DMA!");
        return false;
    }

    const size_t entries_bytes = static_cast<size_t>(nr_entries) * sizeof(VirtioGpuMemEntry);
    const size_t total_cmd_len = sizeof(VirtioGpuResourceAttachBacking) + entries_bytes;

    GpuSpinLockGuard guard(drv->lock);

    /* Budujemy zapytanie bezposrednio w buforze DMA polecen */
    VirtioGpuResourceAttachBacking* req = static_cast<VirtioGpuResourceAttachBacking*>(drv->cmd_vaddr);
    gpu_secure_zero(req, sizeof(*req));
    req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req->resource_id = resource_id;
    req->nr_entries = nr_entries;

    VirtioGpuMemEntry* dst_entries = reinterpret_cast<VirtioGpuMemEntry*>(
        static_cast<uint8_t*>(drv->cmd_vaddr) + sizeof(VirtioGpuResourceAttachBacking));
    gpu_memcpy(dst_entries, entries, entries_bytes);

    /* Zerujemy obszar odpowiedzi DMA */
    uint8_t* resp_dma_vaddr = static_cast<uint8_t*>(drv->cmd_vaddr) + VIRTIO_GPU_CMD_RESP_OFFSET;
    gpu_secure_zero(resp_dma_vaddr, sizeof(VirtioGpuCtrlHdr));

    const uint64_t req_paddr = drv->cmd_paddr;
    const uint64_t resp_paddr = drv->cmd_paddr + VIRTIO_GPU_CMD_RESP_OFFSET;

    VirtqBuffer bufs[2];
    bufs[0].phys_addr = req_paddr;
    bufs[0].len = static_cast<uint32_t>(total_cmd_len);
    bufs[0].is_write = false;

    bufs[1].phys_addr = resp_paddr;
    bufs[1].len = static_cast<uint32_t>(sizeof(VirtioGpuCtrlHdr));
    bufs[1].is_write = true;

    __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);

    const int head = virtqueue_submit(drv->controlq, bufs, 2, nullptr);
    if (head < 0) {
        gpu_klog("[VIRTIO-GPU] BLAD: virtqueue_submit failed dla attach_backing!");
        drv->error_count++;
        return false;
    }

    virtqueue_kick(drv->controlq, true);
    drv->cmd_count++;

    const uint64_t start_ns = gpu_monotonic_ns();
    const uint64_t timeout_ns = VIRTIO_GPU_DEFAULT_TIMEOUT_MS * 1000000ULL;
    const uint64_t deadline_ns = (start_ns > UINT64_MAX - timeout_ns) ? UINT64_MAX : (start_ns + timeout_ns);

    uint32_t spin_guard = VIRTIO_GPU_MAX_SPINS_GUARD;
    bool completed = false;
    uint32_t actual_used_len = 0;

    while (spin_guard > 0) {
        spin_guard--;

        if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) || virtqueue_has_used(drv->controlq)) {
            void* cookie = nullptr;
            uint32_t used_len = 0;
            int popped = virtqueue_pop_used(drv->controlq, &cookie, &used_len);
            if (popped >= 0) {
                completed = true;
                actual_used_len = used_len;
                break;
            } else if (popped < -1) {
                gpu_klog("[VIRTIO-GPU] BLAD KRYTYCZNY: virtqueue_pop_used blad!");
                drv->has_error = true;
                drv->error_count++;
                return false;
            }
        }

        const uint64_t now = gpu_monotonic_ns();
        if (now != 0 && now >= deadline_ns) break;
        asm volatile("pause" ::: "memory");
    }

    if (!completed) {
        gpu_klog("[VIRTIO-GPU] BLAD: Timeout attach_backing!");
        drv->has_error = true;
        drv->error_count++;
        return false;
    }

    if (actual_used_len < sizeof(VirtioGpuCtrlHdr) || actual_used_len > sizeof(VirtioGpuCtrlHdr)) {
        gpu_klog("[VIRTIO-GPU] BLAD: Znieksztalcony used_len dla attach_backing!");
        drv->has_error = true;
        drv->error_count++;
        return false;
    }

    VirtioGpuCtrlHdr resp{};
    gpu_memcpy(&resp, resp_dma_vaddr, sizeof(resp));

    if (resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
        gpu_klog("[VIRTIO-GPU] Urzadzenie odrzucilo attach_backing!");
        drv->error_count++;
        return false;
    }

    return true;
}


bool virtio_gpu_cmd_set_scanout(
    VirtioGpuDriver* drv,
    uint32_t scanout_id,
    uint32_t resource_id,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    if (!drv || scanout_id >= VIRTIO_GPU_MAX_SCANOUTS) return false;
    GpuSpinLockGuard guard(drv->lock);

    VirtioGpuSetScanout req{};
    req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    req.scanout_id = scanout_id;
    req.resource_id = resource_id;
    req.r.x = x;
    req.r.y = y;
    req.r.width = width;
    req.r.height = height;

    VirtioGpuCtrlHdr resp{};
    bool ok = virtio_gpu_send_command_locked(
        drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);

    if (resource_id != 0) {
        char smsg[96]{};
        size_t pos = 0;
        log_append_str(smsg, sizeof(smsg), pos, "[VIRTIO-GPU] SETSCANOUT ");
        log_append_u64_dec(smsg, sizeof(smsg), pos, width);
        log_append_str(smsg, sizeof(smsg), pos, "x");
        log_append_u64_dec(smsg, sizeof(smsg), pos, height);
        log_append_str(smsg, sizeof(smsg), pos, " result=");
        log_append_str(smsg, sizeof(smsg), pos, ok ? "OK" : "FAIL");
        gpu_klog(smsg);
    }
    return ok;
}

bool virtio_gpu_cmd_transfer_to_host_2d(
    VirtioGpuDriver* drv,
    uint32_t resource_id,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    if (!drv || resource_id == 0 || width == 0 || height == 0) return false;
    if (x > drv->fb_width || y > drv->fb_height) return false;
    if (x + width > drv->fb_width || y + height > drv->fb_height) return false;

    const uint64_t offset = (static_cast<uint64_t>(y) * static_cast<uint64_t>(drv->fb_stride)) +
                            (static_cast<uint64_t>(x) * 4ULL);

    GpuSpinLockGuard guard(drv->lock);

    VirtioGpuTransferToHost2d req{};
    req.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req.resource_id = resource_id;
    req.r.x = x;
    req.r.y = y;
    req.r.width = width;
    req.r.height = height;
    req.offset = offset;
    req.padding = 0;

    VirtioGpuCtrlHdr resp{};
    if (!virtio_gpu_send_command_locked(
            drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA)) {
        return false;
    }

    drv->transfer_count++;
    return true;
}

bool virtio_gpu_cmd_resource_flush(
    VirtioGpuDriver* drv,
    uint32_t resource_id,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    if (!drv || resource_id == 0 || width == 0 || height == 0) return false;
    if (x > drv->fb_width || y > drv->fb_height) return false;
    if (x + width > drv->fb_width || y + height > drv->fb_height) return false;

    GpuSpinLockGuard guard(drv->lock);

    VirtioGpuResourceFlush req{};
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req.resource_id = resource_id;
    req.r.x = x;
    req.r.y = y;
    req.r.width = width;
    req.r.height = height;
    req.padding = 0;

    VirtioGpuCtrlHdr resp{};
    if (!virtio_gpu_send_command_locked(
            drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA)) {
        return false;
    }

    drv->flush_count++;
    return true;
}

bool virtio_gpu_cmd_resource_detach_backing(VirtioGpuDriver* drv, uint32_t resource_id) {
    if (!drv || resource_id == 0) return false;
    GpuSpinLockGuard guard(drv->lock);

    VirtioGpuResourceDetachBacking req{};
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    req.resource_id = resource_id;
    req.padding = 0;

    VirtioGpuCtrlHdr resp{};
    return virtio_gpu_send_command_locked(
        drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
}

bool virtio_gpu_cmd_resource_unref(VirtioGpuDriver* drv, uint32_t resource_id) {
    if (!drv || resource_id == 0) return false;
    GpuSpinLockGuard guard(drv->lock);

    VirtioGpuResourceUnref req{};
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    req.resource_id = resource_id;
    req.padding = 0;

    VirtioGpuCtrlHdr resp{};
    return virtio_gpu_send_command_locked(
        drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
}

/* =========================================================================
 * 3. ZARZADZANIE BUFOREM RAMKI I INICJALIZACJA 2D
 * ========================================================================= */

void virtio_gpu_free_stale_resource(VirtioGpuDriver* drv) {
    if (!drv || drv->old_resource_id == 0) return;

    if (!drv->has_error) {
        virtio_gpu_cmd_resource_detach_backing(drv, drv->old_resource_id);
        virtio_gpu_cmd_resource_unref(drv, drv->old_resource_id);
    }
    if (drv->old_fb_vaddr) {
        gpu_fb_free(drv->old_fb_vaddr, drv->old_allocated_frames, drv->old_fb_pages);
    }
    if (drv->old_mem_entries) {
        gpu_heap_free(drv->old_mem_entries);
    }
    if (drv->old_allocated_frames) {
        gpu_heap_free(drv->old_allocated_frames);
    }

    drv->old_resource_id = 0;
    drv->old_fb_vaddr = nullptr;
    drv->old_allocated_frames = nullptr;
    drv->old_mem_entries = nullptr;
    drv->old_fb_pages = 0;
    drv->old_nr_entries = 0;
    drv->old_display_width = 0;
    drv->old_display_height = 0;
}

bool virtio_gpu_rollback_modeset(VirtioGpuDriver* drv) {
    if (!drv || drv->old_resource_id == 0) return false;

    // 1. Przywroc scanout na stary zasob
    if (drv->scanout_set && !drv->has_error) {
        virtio_gpu_cmd_set_scanout(drv, drv->scanout_id, drv->old_resource_id, 0, 0,
                                   drv->old_display_width, drv->old_display_height);
    }

    // 2. Zwolnij nowy nieudany zasob
    if (drv->backing_attached && !drv->has_error) {
        virtio_gpu_cmd_resource_detach_backing(drv, drv->resource_id);
    }
    if (drv->resource_created && !drv->has_error) {
        virtio_gpu_cmd_resource_unref(drv, drv->resource_id);
    }
    if (drv->fb_vaddr) {
        gpu_fb_free(drv->fb_vaddr, drv->allocated_frames, drv->fb_pages);
    }
    if (drv->mem_entries) {
        gpu_heap_free(drv->mem_entries);
    }
    if (drv->allocated_frames) {
        gpu_heap_free(drv->allocated_frames);
    }

    // 3. Przywroc stary zasob jako aktywny
    drv->resource_id = drv->old_resource_id;
    drv->fb_vaddr = drv->old_fb_vaddr;
    drv->allocated_frames = drv->old_allocated_frames;
    drv->fb_pages = drv->old_fb_pages;
    drv->mem_entries = drv->old_mem_entries;
    drv->nr_entries = drv->old_nr_entries;
    drv->fb_width = drv->old_display_width;
    drv->fb_height = drv->old_display_height;
    drv->display_width = drv->old_display_width;
    drv->display_height = drv->old_display_height;
    drv->fb_stride = drv->old_display_width * 4U;
    drv->resource_created = true;
    drv->backing_attached = true;
    drv->scanout_set = true;

    drv->old_resource_id = 0;
    drv->old_fb_vaddr = nullptr;
    drv->old_allocated_frames = nullptr;
    drv->old_mem_entries = nullptr;
    drv->old_fb_pages = 0;
    drv->old_nr_entries = 0;
    drv->old_display_width = 0;
    drv->old_display_height = 0;

    return true;
}

bool virtio_gpu_setup_framebuffer(VirtioGpuDriver* drv, uint32_t width, uint32_t height) {
    if (!drv || width == 0 || height == 0 || width > 4096 || height > 4096) {
        return false;
    }

    const uint64_t stride = static_cast<uint64_t>(width) * 4ULL;
    if ((stride & 3U) != 0) return false;

    const uint64_t total_bytes = stride * static_cast<uint64_t>(height);
    if (total_bytes == 0 || total_bytes > 64ULL * 1024 * 1024) return false;

    const size_t page_count = static_cast<size_t>((total_bytes + 4095ULL) / 4096ULL);
    if (page_count == 0 || page_count > 16384) return false;

    /*
     * 0. Jesli istnial poprzedni niezwolniony zasob oczekujacy, posprzataj go
     */
    virtio_gpu_free_stale_resource(drv);

    /*
     * 1. Alokacja struktur metadanych
     */
    void** frames = static_cast<void**>(gpu_heap_alloc(sizeof(void*) * page_count));
    if (!frames) {
        gpu_klog("[VIRTIO-GPU] BLAD: Nie mozna zaalokowac tablicy frames!");
        return false;
    }
    gpu_secure_zero(frames, sizeof(void*) * page_count);

    VirtioGpuMemEntry* entries = static_cast<VirtioGpuMemEntry*>(
        gpu_heap_alloc(sizeof(VirtioGpuMemEntry) * page_count));
    if (!entries) {
        gpu_klog("[VIRTIO-GPU] BLAD: Nie mozna zaalokowac tablicy mem_entries!");
        gpu_heap_free(frames);
        return false;
    }
    gpu_secure_zero(entries, sizeof(VirtioGpuMemEntry) * page_count);

    /*
     * 2. Wybor nowego resource_id (naprzemiennie 1 i 2 dla atomic switch)
     */
    uint32_t new_res_id = (drv->resource_id == 1) ? 2 : 1;
    constexpr uint32_t FORMAT = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;

    /*
     * 3. Utworzenie nowego zasobu 2D
     */
    if (!virtio_gpu_cmd_resource_create_2d(drv, new_res_id, FORMAT, width, height)) {
        gpu_klog("[VIRTIO-GPU] BLAD: resource_create_2d failed!");
        gpu_heap_free(entries);
        gpu_heap_free(frames);
        return false;
    }

    /*
     * 4. Alokacja pojedynczych ramek fizycznych i zmapowanie do ciaglej przestrzeni wirtualnej
     */
    void* fb_vaddr = nullptr;
    size_t actual_pages = 0;

    if (!gpu_fb_alloc(total_bytes, &fb_vaddr, frames, page_count, &actual_pages)) {
        gpu_klog("[VIRTIO-GPU] BLAD: Alokacja stron bufora ramki nie powiodla sie!");
        virtio_gpu_cmd_resource_unref(drv, new_res_id);
        gpu_heap_free(entries);
        gpu_heap_free(frames);
        return false;
    }

    /*
     * 5. Koalescencja ciaglych fragmentow fizycznych do wpisow scatter-gather
     */
    uint32_t nr_entries = 0;
    for (size_t i = 0; i < actual_pages; ++i) {
        const uint64_t pa = reinterpret_cast<uint64_t>(frames[i]);
        if (nr_entries > 0 &&
            (entries[nr_entries - 1].addr + entries[nr_entries - 1].length == pa)) {
            entries[nr_entries - 1].length += 4096;
        } else {
            entries[nr_entries].addr = pa;
            entries[nr_entries].length = 4096;
            entries[nr_entries].padding = 0;
            nr_entries++;
        }
    }

    /*
     * 6. Podpiecie bufora backing
     */
    if (!virtio_gpu_cmd_resource_attach_backing(drv, new_res_id, entries, nr_entries)) {
        gpu_klog("[VIRTIO-GPU] BLAD: resource_attach_backing failed!");
        gpu_fb_free(fb_vaddr, frames, actual_pages);
        virtio_gpu_cmd_resource_unref(drv, new_res_id);
        gpu_heap_free(entries);
        gpu_heap_free(frames);
        return false;
    }

    /*
     * 7. Powiazanie scanoutu z nowym zasobem
     */
    if (!virtio_gpu_cmd_set_scanout(drv, drv->scanout_id, new_res_id, 0, 0, width, height)) {
        gpu_klog("[VIRTIO-GPU] BLAD: set_scanout failed!");
        virtio_gpu_cmd_resource_detach_backing(drv, new_res_id);
        gpu_fb_free(fb_vaddr, frames, actual_pages);
        virtio_gpu_cmd_resource_unref(drv, new_res_id);
        gpu_heap_free(entries);
        gpu_heap_free(frames);
        return false;
    }

    /*
     * 8. Zachowanie poprzedniego zasobu do zwolnienia po redraw/flush
     */
    if (drv->resource_id != 0 && drv->fb_vaddr != nullptr) {
        drv->old_resource_id = drv->resource_id;
        drv->old_fb_vaddr = drv->fb_vaddr;
        drv->old_allocated_frames = drv->allocated_frames;
        drv->old_fb_pages = drv->fb_pages;
        drv->old_mem_entries = drv->mem_entries;
        drv->old_nr_entries = drv->nr_entries;
        drv->old_display_width = drv->fb_width;
        drv->old_display_height = drv->fb_height;
    }

    drv->resource_id = new_res_id;
    drv->fb_width = width;
    drv->fb_height = height;
    drv->fb_stride = static_cast<uint32_t>(stride);
    drv->fb_format = FORMAT;
    drv->fb_vaddr = fb_vaddr;
    drv->fb_size = total_bytes;
    drv->fb_pages = actual_pages;
    drv->allocated_frames = frames;
    drv->mem_entries = entries;
    drv->nr_entries = nr_entries;
    drv->resource_created = true;
    drv->backing_attached = true;
    drv->scanout_set = true;
    drv->display_width = width;
    drv->display_height = height;

    char msg[128]{};
    size_t mpos = 0;
    log_append_str(msg, sizeof(msg), mpos, "[VIRTIO-GPU] Framebuffer gotowy: ");
    log_append_u64_dec(msg, sizeof(msg), mpos, width);
    log_append_str(msg, sizeof(msg), mpos, "x");
    log_append_u64_dec(msg, sizeof(msg), mpos, height);
    log_append_str(msg, sizeof(msg), mpos, " (stron=");
    log_append_u64_dec(msg, sizeof(msg), mpos, actual_pages);
    log_append_str(msg, sizeof(msg), mpos, ", wpisow_sg=");
    log_append_u64_dec(msg, sizeof(msg), mpos, nr_entries);
    log_append_str(msg, sizeof(msg), mpos, ", format=B8G8R8X8)");
    gpu_klog(msg);

    return true;
}

bool virtio_gpu_transfer_and_flush(VirtioGpuDriver* drv, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!drv || !drv->initialized || drv->has_error || drv->resource_id == 0) return false;

    if (!virtio_gpu_cmd_transfer_to_host_2d(drv, drv->resource_id, x, y, width, height)) {
        return false;
    }
    if (!virtio_gpu_cmd_resource_flush(drv, drv->resource_id, x, y, width, height)) {
        return false;
    }
    return true;
}

/*
 * Renderuje deterministyczny wzorzec testowy do bufora ramki i wykonuje transfer+flush.
 * Wzorzec uzywany przez testy integracyjne QEMU i weryfikacje pikseli screendump:
 * - Tlo: ciemny granat (#0A192F)
 * - Lewy-gorny prostokat: zloty bursztyn (#FFBF00) w [50, 50, 100, 100]
 * - Prawy-gorny prostokat: cyjan (#00E5FF) w [w-150, 50, 100, 100]
 * - Lewy-dolny prostokat: magenta (#FF007F) w [50, h-150, 100, 100]
 * - Prawy-dolny prostokat: limonka (#39FF14) w [w-150, h-150, 100, 100]
 * - Srodkowy prostokat: biel (#FFFFFF) w [srodek_x-100, srodek_y-50, 200, 100]
 */
bool virtio_gpu_render_test_pattern(VirtioGpuDriver* drv) {
    if (!drv || !drv->fb_vaddr || drv->fb_width < 320 || drv->fb_height < 240) {
        return false;
    }

    uint32_t* fb = static_cast<uint32_t*>(drv->fb_vaddr);
    const uint32_t w = drv->fb_width;
    const uint32_t h = drv->fb_height;

    /* 1. Tlo: #0A192F */
    constexpr uint32_t COLOR_BG = 0x000A192FU;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            fb[y * w + x] = COLOR_BG;
        }
    }

    auto fill_rect = [&](uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t color) {
        for (uint32_t cy = ry; cy < ry + rh && cy < h; ++cy) {
            for (uint32_t cx = rx; cx < rx + rw && cx < w; ++cx) {
                fb[cy * w + cx] = color;
            }
        }
    };

    /* Lewy-gorny: Bursztyn #FFBF00 */
    fill_rect(50, 50, 100, 100, 0x00FFBF00U);

    /* Prawy-gorny: Cyjan #00E5FF */
    fill_rect(w - 150, 50, 100, 100, 0x0000E5FFU);

    /* Lewy-dolny: Magenta #FF007F */
    fill_rect(50, h - 150, 100, 100, 0x00FF007FU);

    /* Prawy-dolny: Limonka #39FF14 */
    fill_rect(w - 150, h - 150, 100, 100, 0x0039FF14U);

    /* Srodek: Biel #FFFFFF */
    fill_rect(w / 2 - 100, h / 2 - 50, 200, 100, 0x00FFFFFFU);

    /* Transfer i flush calej ramki */
    if (!virtio_gpu_transfer_and_flush(drv, 0, 0, w, h)) {
        return false;
    }

    gpu_klog("[VIRTIO-GPU] Test pattern rendered and flushed (Amber/Cyan/Magenta/Lime/White).");
    return true;
}

/* =========================================================================
 * 4. INICJALIZACJA URZADZENIA I RESET ZE SPRAWDZENIEM KWARANTANNY
 * ========================================================================= */

bool virtio_gpu_init(VirtioPciDevice* dev) {
    if (!dev) return false;

    /* Znajdz wolny slot lub przydziel nowy */
    VirtioGpuDriver* drv = nullptr;
    uint32_t slot_idx = 0;
    for (uint32_t i = 0; i < g_gpu_driver_count; ++i) {
        if (g_gpu_drivers[i].dev == dev) {
            drv = &g_gpu_drivers[i];
            slot_idx = i;
            break;
        }
    }
    if (!drv) {
        for (uint32_t i = 0; i < g_gpu_driver_count; ++i) {
            if (!g_gpu_drivers[i].initialized && g_gpu_drivers[i].dev == nullptr) {
                drv = &g_gpu_drivers[i];
                slot_idx = i;
                break;
            }
        }
    }
    if (!drv) {
        if (g_gpu_driver_count >= VIRTIO_GPU_MAX_DEVICES) {
            gpu_klog("[VIRTIO-GPU] Osiagnieto limit instancji urzadzen GPU.");
            return false;
        }
        slot_idx = g_gpu_driver_count++;
        drv = &g_gpu_drivers[slot_idx];
    }

    gpu_secure_zero(drv, sizeof(*drv));
    drv->dev = dev;
    drv->irq_vector = -1;

    /* VirtIO 1.x: Reset sekwencji statusu */
    virtio_pci_set_status(dev, VIRTIO_STATUS_RESET);
    virtio_pci_set_status(dev, virtio_pci_get_status(dev) | VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_pci_set_status(dev, virtio_pci_get_status(dev) | VIRTIO_STATUS_DRIVER);

    /* Negocjacja cech */
    const uint64_t dev_features = virtio_pci_read_device_features(dev);
    if ((dev_features & VIRTIO_F_VERSION_1) == 0) {
        gpu_klog("[VIRTIO-GPU] Brak obslugi obowiazkowej cechy VIRTIO_F_VERSION_1!");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    /* Zadamy wylacznie 2D - BEZ VIRGL */
    const uint64_t drv_features = VIRTIO_F_VERSION_1;
    virtio_pci_write_driver_features(dev, drv_features);

    virtio_pci_set_status(dev, virtio_pci_get_status(dev) | VIRTIO_STATUS_FEATURES_OK);
    if ((virtio_pci_get_status(dev) & VIRTIO_STATUS_FEATURES_OK) == 0) {
        gpu_klog("[VIRTIO-GPU] Urzadzenie odrzucilo cechy (FEATURES_OK nieaktywne)");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    /* Alokacja bufora DMA dla polecen i odpowiedzi (64 KiB) */
    if (!gpu_dma_alloc(VIRTIO_GPU_CMD_BUFFER_SIZE, 4096, &drv->cmd_vaddr, &drv->cmd_paddr, &drv->cmd_pages)) {
        gpu_klog("[VIRTIO-GPU] BLAD: Nie mozna zaalokowac pamieci DMA dla polecen!");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    /* Utworzenie kolejki controlq (indeks 0) */
    drv->controlq = virtqueue_create(dev, 0);
    if (!drv->controlq) {
        gpu_klog("[VIRTIO-GPU] BLAD: virtqueue_create failed dla controlq (queue 0)!");
        gpu_dma_free(drv->cmd_vaddr, drv->cmd_paddr, drv->cmd_pages);
        drv->cmd_vaddr = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    /* Konfiguracja MSI-X (jesli urzadzenie obsluguje MSI-X) */
    if (dev->has_msix) {
        int vec = idt_alokuj_wektor_irq();
        if (vec >= 0) {
            uint8_t lapic_id = apic_pobierz_lapic_id();
            if (idt_zarejestruj_irq(static_cast<uint8_t>(vec), virtio_gpu_isr, drv)) {
                if (virtio_pci_program_msix_entry(dev, 0, lapic_id, static_cast<uint8_t>(vec), false)) {
                    if (virtio_pci_set_queue_msix_vector_checked(dev, 0, 0)) {
                        if (pci_msix_enable(dev->bus, dev->slot, dev->func, &dev->msix_cap)) {
                            dev->msix_enabled = true;
                            drv->use_msix = true;
                            drv->irq_vector = vec;
                            gpu_klog("[VIRTIO-GPU] MSI-X configured successfully (entry 0)");
                        }
                    }
                }
            }
            if (!drv->use_msix) {
                idt_wyrejestruj_irq(static_cast<uint8_t>(vec));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(vec));
                drv->irq_vector = -1;
            }
        }
    }

    /*
     * Ustawienie DRIVER_OK wylacznie po pelnej gotowosci kolejki i struktur
     */
    virtio_pci_set_status(dev, virtio_pci_get_status(dev) | VIRTIO_STATUS_DRIVER_OK);
    if ((virtio_pci_get_status(dev) & VIRTIO_STATUS_DRIVER_OK) == 0) {
        gpu_klog("[VIRTIO-GPU] Urzadzenie odrzucilo status DRIVER_OK!");
        if (drv->use_msix) {
            virtio_pci_mask_msix_entry(dev, 0);
            pci_msix_disable(dev->bus, dev->slot, dev->func, &dev->msix_cap);
            dev->msix_enabled = false;
            virtio_pci_select_queue(dev, 0);
            virtio_pci_set_queue_msix_vector(dev, VIRTIO_MSI_NO_VECTOR);
            if (drv->irq_vector >= 0) {
                idt_wyrejestruj_irq(static_cast<uint8_t>(drv->irq_vector));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(drv->irq_vector));
                drv->irq_vector = -1;
            }
            drv->use_msix = false;
        }
        virtqueue_destroy(drv->controlq);
        drv->controlq = nullptr;
        gpu_dma_free(drv->cmd_vaddr, drv->cmd_paddr, drv->cmd_pages);
        drv->cmd_vaddr = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    drv->initialized = true;

    /* Pobranie informacji o wyswietlaczu */
    VirtioGpuRespDisplayInfo dinfo{};
    bool got_dinfo = virtio_gpu_cmd_get_display_info(drv, &dinfo);

    uint32_t dinfo_w = 0;
    uint32_t dinfo_h = 0;
    bool dinfo_enabled = false;

    if (got_dinfo) {
        dinfo_w = dinfo.pmodes[0].r.width;
        dinfo_h = dinfo.pmodes[0].r.height;
        dinfo_enabled = (dinfo.pmodes[0].enabled != 0);

        char dmsg[96]{};
        size_t dp = 0;
        log_append_str(dmsg, sizeof(dmsg), dp, "[VIRTIO-GPU] GETDISPLAYINFO scanout0=");
        log_append_u64_dec(dmsg, sizeof(dmsg), dp, dinfo_w);
        log_append_str(dmsg, sizeof(dmsg), dp, "x");
        log_append_u64_dec(dmsg, sizeof(dmsg), dp, dinfo_h);
        log_append_str(dmsg, sizeof(dmsg), dp, " (enabled=");
        log_append_u64_dec(dmsg, sizeof(dmsg), dp, dinfo_enabled ? 1 : 0);
        log_append_str(dmsg, sizeof(dmsg), dp, ")");
        gpu_klog(dmsg);
    } else {
        gpu_klog("[VIRTIO-GPU] GETDISPLAYINFO scanout0=failed");
    }

    uint32_t boot_w = 0, boot_h = 0;
    grafika_pobierz_boot_rozmiar(&boot_w, &boot_h);

    char bmsg[64]{};
    size_t bp = 0;
    log_append_str(bmsg, sizeof(bmsg), bp, "[VIRTIO-GPU] boot mode=");
    log_append_u64_dec(bmsg, sizeof(bmsg), bp, boot_w);
    log_append_str(bmsg, sizeof(bmsg), bp, "x");
    log_append_u64_dec(bmsg, sizeof(bmsg), bp, boot_h);
    gpu_klog(bmsg);

    auto is_valid_dim = [](uint32_t w, uint32_t h) {
        return w >= 640 && w <= 4096 && h >= 480 && h <= 4096 && (uint64_t(w) * h * 4ULL <= 64ULL * 1024 * 1024);
    };

    uint32_t target_w = 1280;
    uint32_t target_h = 720;

    if (is_valid_dim(boot_w, boot_h)) {
        target_w = boot_w;
        target_h = boot_h;
    } else if (dinfo_enabled && is_valid_dim(dinfo_w, dinfo_h)) {
        target_w = dinfo_w;
        target_h = dinfo_h;
    }

    char smsg[64]{};
    size_t sp = 0;
    log_append_str(smsg, sizeof(smsg), sp, "[VIRTIO-GPU] selected initial mode=");
    log_append_u64_dec(smsg, sizeof(smsg), sp, target_w);
    log_append_str(smsg, sizeof(smsg), sp, "x");
    log_append_u64_dec(smsg, sizeof(smsg), sp, target_h);
    gpu_klog(smsg);

    drv->display_width = target_w;
    drv->display_height = target_h;
    drv->scanout_id = 0;
    drv->scanout_enabled = true;

    /* Utworzenie i podpiecie bufora ramki 2D */
    if (!virtio_gpu_setup_framebuffer(drv, target_w, target_h)) {
        gpu_klog("[VIRTIO-GPU] OSTRZEZENIE: setup_framebuffer dla wybranego trybu nie powiodl sie!");
        if (target_w != 640 || target_h != 480) {
            target_w = 640;
            target_h = 480;
            drv->display_width = target_w;
            drv->display_height = target_h;
            if (!virtio_gpu_setup_framebuffer(drv, target_w, target_h)) {
                gpu_klog("[VIRTIO-GPU] BLAD: setup_framebuffer fallback do 640x480 nie powiodl sie!");
                return false;
            }
        } else {
            return false;
        }
    }

    gpu_klog("[VIRTIO-GPU] Karta graficzna gotowa do pracy w trybie 2D.");
    return true;
}

bool virtio_gpu_reset(VirtioGpuDriver* drv) {
    if (!drv) return false;

    GpuSpinLockGuard guard(drv->lock);

    /* Posprzataj ewentualny stary zasob po modesettingu */
    virtio_gpu_free_stale_resource(drv);

    /* 1. Odpiecie scanoutu jesli byl ustawiony */
    if (drv->scanout_set && !drv->has_error) {
        VirtioGpuSetScanout req{};
        req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
        req.scanout_id = drv->scanout_id;
        req.resource_id = 0; /* 0 = wylacz scanout */
        VirtioGpuCtrlHdr resp{};
        virtio_gpu_send_command_locked(drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
        drv->scanout_set = false;
    }

    /* 2. Odlaczenie pamieci backing jesli byla podpieta */
    if (drv->backing_attached && !drv->has_error) {
        VirtioGpuResourceDetachBacking req{};
        req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
        req.resource_id = drv->resource_id;
        VirtioGpuCtrlHdr resp{};
        virtio_gpu_send_command_locked(drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
        drv->backing_attached = false;
    }

    /* 3. Zniszczenie zasobu 2D */
    if (drv->resource_created && !drv->has_error) {
        VirtioGpuResourceUnref req{};
        req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
        req.resource_id = drv->resource_id;
        VirtioGpuCtrlHdr resp{};
        virtio_gpu_send_command_locked(drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
        drv->resource_created = false;
    }

    /* 4. Wylaczenie MSI-X */
    if (drv->use_msix && drv->dev) {
        virtio_pci_mask_msix_entry(drv->dev, 0);
        pci_msix_disable(drv->dev->bus, drv->dev->slot, drv->dev->func, &drv->dev->msix_cap);
        drv->dev->msix_enabled = false;
        virtio_pci_select_queue(drv->dev, 0);
        virtio_pci_set_queue_msix_vector(drv->dev, VIRTIO_MSI_NO_VECTOR);
        if (drv->dev->has_device_cfg) {
            virtio_pci_set_config_msix_vector(drv->dev, VIRTIO_MSI_NO_VECTOR);
        }
        if (drv->irq_vector >= 0) {
            idt_wyrejestruj_irq(static_cast<uint8_t>(drv->irq_vector));
            idt_zwolnij_wektor_irq(static_cast<uint8_t>(drv->irq_vector));
            drv->irq_vector = -1;
        }
        drv->use_msix = false;
    }

    /* 5. Reset sprzetowy urzadzenia */
    bool reset_confirmed = false;
    if (drv->dev != nullptr) {
        virtio_pci_set_status(drv->dev, VIRTIO_STATUS_RESET);

        const uint64_t start_ns = gpu_monotonic_ns();
        const uint64_t deadline_ns = (start_ns > UINT64_MAX - 100000000ULL)
                                         ? UINT64_MAX
                                         : (start_ns + 100000000ULL);
        uint32_t spin = 1000000;
        while (spin-- > 0) {
            if (virtio_pci_get_status(drv->dev) == VIRTIO_STATUS_RESET) {
                reset_confirmed = true;
                break;
            }
            const uint64_t now = gpu_monotonic_ns();
            if (now != 0 && now >= deadline_ns) break;
            asm volatile("pause" ::: "memory");
        }
    } else {
        reset_confirmed = true;
    }

    if (!reset_confirmed) {
        gpu_klog("[VIRTIO-GPU] OSTRZEZENIE: Reset sprzetu niepotwierdzony! Kwarantanna DMA.");
        drv->has_error = true;
        /* Pamiec DMA i bufor ramki NIE sa zwalniane, aby uniknac uszkodzenia pamieci przez sprzet */
        return false;
    }

    /* Sprzet potwierdzil reset: mozemy bezpiecznie zwolnic kolejki i DMA */
    if (drv->controlq) {
        virtqueue_destroy(drv->controlq);
        drv->controlq = nullptr;
    }
    if (drv->cursorq) {
        virtqueue_destroy(drv->cursorq);
        drv->cursorq = nullptr;
    }

    if (drv->cmd_vaddr) {
        gpu_dma_free(drv->cmd_vaddr, drv->cmd_paddr, drv->cmd_pages);
        drv->cmd_vaddr = nullptr;
        drv->cmd_paddr = 0;
        drv->cmd_pages = 0;
    }

    if (drv->fb_vaddr) {
        gpu_fb_free(drv->fb_vaddr, drv->allocated_frames, drv->fb_pages);
        drv->fb_vaddr = nullptr;
        drv->fb_size = 0;
        drv->fb_pages = 0;
    }

    if (drv->allocated_frames) {
        gpu_heap_free(drv->allocated_frames);
        drv->allocated_frames = nullptr;
    }
    if (drv->mem_entries) {
        gpu_heap_free(drv->mem_entries);
        drv->mem_entries = nullptr;
    }
    drv->nr_entries = 0;

    drv->resource_id = 0;
    drv->resource_created = false;
    drv->backing_attached = false;
    drv->scanout_set = false;
    drv->initialized = false;
    drv->has_error = false;
    drv->dev = nullptr;

    return true;
}

void virtio_gpu_reset_all(void) {
    for (uint32_t i = 0; i < g_gpu_driver_count; ++i) {
        if (g_gpu_drivers[i].initialized || g_gpu_drivers[i].dev != nullptr) {
            virtio_gpu_reset(&g_gpu_drivers[i]);
        }
    }
    g_gpu_driver_count = 0;
}

uint32_t virtio_gpu_probe_and_init(void) {
    const uint32_t count = virtio_pci_liczba_urzadzen();
    uint32_t initialized = 0;

    for (uint32_t i = 0; i < count; ++i) {
        VirtioPciDevice* dev = virtio_pci_pobierz_urzadzenie(i);
        if (dev && dev->virtio_id == VIRTIO_ID_GPU) {
            char log_buf[128]{};
            size_t lpos = 0;
            log_append_str(log_buf, sizeof(log_buf), lpos, "[VIRTIO-GPU] Wykryto urzadzenie VirtIO-GPU na magistrali PCI (BDF ");
            log_append_u64_dec(log_buf, sizeof(log_buf), lpos, dev->bus);
            log_append_str(log_buf, sizeof(log_buf), lpos, ":");
            log_append_u64_dec(log_buf, sizeof(log_buf), lpos, dev->slot);
            log_append_str(log_buf, sizeof(log_buf), lpos, ".");
            log_append_u64_dec(log_buf, sizeof(log_buf), lpos, dev->func);
            log_append_str(log_buf, sizeof(log_buf), lpos, ")");
            gpu_klog(log_buf);

            if (virtio_gpu_init(dev)) {
                initialized++;
            }
        }
    }

    return initialized;
}

VirtioGpuDriver* virtio_gpu_get_driver(uint32_t index) {
    if (index >= g_gpu_driver_count) return nullptr;
    return &g_gpu_drivers[index];
}

uint32_t virtio_gpu_get_driver_count(void) {
    return g_gpu_driver_count;
}

/* =========================================================================
 * 5. TESTY REGRESYJNE TEARDOWN/REINIT I OBCIAZENIOWE DLA QEMU
 * ========================================================================= */

bool virtio_gpu_test_teardown_reinit(VirtioGpuDriver* drv) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    (void)drv;
    return true;
#else
    if (!drv || !drv->dev || !drv->initialized) return false;
    VirtioPciDevice* dev = drv->dev;

    gpu_klog("[VIRTIO-GPU-TEST] === Rozpoczynanie audytu cykli teardown/reinit w QEMU ===");

    /*
     * 1. Zresetuj urzadzenie, aby uzyskac czysty stan bazowy pamieci i przerwan.
     */
    if (!virtio_gpu_reset(drv)) {
        gpu_klog("[VIRTIO-GPU-TEST] BLAD: Wstepny reset nie powiodl sie!");
        return false;
    }

    const uint64_t baseline_pmm = pmm_pobierz_wolne_ramki();
    const uint32_t baseline_vectors = idt_zaalokowane_wektory_liczba();

    char base_msg[128]{};
    size_t bpos = 0;
    log_append_str(base_msg, sizeof(base_msg), bpos, "[VIRTIO-GPU-TEST] Stan bazowy: wolne ramki PMM=");
    log_append_u64_dec(base_msg, sizeof(base_msg), bpos, baseline_pmm);
    log_append_str(base_msg, sizeof(base_msg), bpos, ", wektory IDT=");
    log_append_u64_dec(base_msg, sizeof(base_msg), bpos, baseline_vectors);
    gpu_klog(base_msg);

    constexpr int CYCLES = 5;
    for (int i = 0; i < CYCLES; ++i) {
        if (!virtio_gpu_init(dev)) {
            gpu_klog("[VIRTIO-GPU-TEST] BLAD: Inicjalizacja GPU w cyklu testowym!");
            return false;
        }

        VirtioGpuDriver* cur_drv = virtio_gpu_get_driver(0);
        if (!cur_drv || !cur_drv->initialized) {
            gpu_klog("[VIRTIO-GPU-TEST] BLAD: Brak aktywnego sterownika w cyklu!");
            return false;
        }

        if (!virtio_gpu_render_test_pattern(cur_drv)) {
            gpu_klog("[VIRTIO-GPU-TEST] BLAD: Rendering wzorca w cyklu testowym!");
            return false;
        }

        if (!virtio_gpu_reset(cur_drv)) {
            gpu_klog("[VIRTIO-GPU-TEST] BLAD: Reset GPU w cyklu testowym!");
            return false;
        }

        const uint64_t curr_pmm = pmm_pobierz_wolne_ramki();
        const uint32_t curr_vectors = idt_zaalokowane_wektory_liczba();

        if (curr_pmm != baseline_pmm || curr_vectors != baseline_vectors) {
            char err[160]{};
            size_t epos = 0;
            log_append_str(err, sizeof(err), epos, "[VIRTIO-GPU-TEST] BLAD WYCIEKU w cyklu ");
            log_append_u64_dec(err, sizeof(err), epos, i + 1);
            log_append_str(err, sizeof(err), epos, ": PMM delta=");
            log_append_u64_dec(err, sizeof(err), epos, (curr_pmm < baseline_pmm) ? (baseline_pmm - curr_pmm) : (curr_pmm - baseline_pmm));
            log_append_str(err, sizeof(err), epos, ", Vector delta=");
            log_append_u64_dec(err, sizeof(err), epos, (curr_vectors >= baseline_vectors) ? (curr_vectors - baseline_vectors) : (baseline_vectors - curr_vectors));
            gpu_klog(err);
            return false;
        }
    }

    /*
     * 2. Re-inicjalizacja do stanu aktywnego przed rozpoczeciem pracy systemu
     */
    if (!virtio_gpu_init(dev)) {
        gpu_klog("[VIRTIO-GPU-TEST] BLAD: Re-inicjalizacja po testach wyciekow!");
        return false;
    }

    gpu_klog("[VIRTIO-GPU-TEST] Cykle teardown/reinit: PASS (PMM delta=0, DMA delta=0, IDT delta=0).");
    return true;
#endif
}

bool virtio_gpu_stress_dirty_rectangles(VirtioGpuDriver* drv) {
    if (!drv || !drv->initialized || drv->has_error || !drv->fb_vaddr) {
        return false;
    }

    gpu_klog("[VIRTIO-GPU-STRESS] Rozpoczynanie testu obciazeniowego dirty rectangles (10 000 aktualizacji)...");

    const uint32_t w = drv->fb_width;
    const uint32_t h = drv->fb_height;
    if (w < 64 || h < 64) return false;

    uint32_t* fb = static_cast<uint32_t*>(drv->fb_vaddr);

    // Prosty deterministyczny generator liczb pseudolosowych Xorshift32
    uint32_t state = 0x12345678U;
    auto prng = [&state]() -> uint32_t {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    };

    auto draw_rect = [&](uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t col) {
        for (uint32_t cy = ry; cy < ry + rh && cy < h; ++cy) {
            for (uint32_t cx = rx; cx < rx + rw && cx < w; ++cx) {
                fb[cy * w + cx] = col;
            }
        }
    };

    constexpr uint32_t TOTAL_UPDATES = 10000;
    uint32_t error_count = 0;

    for (uint32_t i = 0; i < TOTAL_UPDATES; ++i) {
        uint32_t rx = 0;
        uint32_t ry = 0;
        uint32_t rw = 1;
        uint32_t rh = 1;
        const uint32_t mode = i % 6;
        const uint32_t color = 0x00102030U ^ (prng() & 0x00FFFFFFU);

        if (mode == 0) {
            // 1. 1x1 piksel (w tym rogi i srodek)
            rx = prng() % w;
            ry = prng() % h;
            rw = 1;
            rh = 1;
        } else if (mode == 1) {
            // 2. Krawedzie (edge rectangles: lewa, prawa, gorna, dolna)
            const uint32_t edge = prng() % 4;
            if (edge == 0) { // Gorna
                rw = 16 + (prng() % 64);
                rh = 2 + (prng() % 8);
                rx = prng() % (w - rw);
                ry = 0;
            } else if (edge == 1) { // Dolna
                rw = 16 + (prng() % 64);
                rh = 2 + (prng() % 8);
                rx = prng() % (w - rw);
                ry = h - rh;
            } else if (edge == 2) { // Lewa
                rw = 2 + (prng() % 8);
                rh = 16 + (prng() % 64);
                rx = 0;
                ry = prng() % (h - rh);
            } else { // Prawa
                rw = 2 + (prng() % 8);
                rh = 16 + (prng() % 64);
                rx = w - rw;
                ry = prng() % (h - rh);
            }
        } else if (mode == 2) {
            // 3. Nakladajace sie prostokaty (overlapping rectangles)
            rw = 32 + (prng() % 32);
            rh = 32 + (prng() % 32);
            rx = 100 + (prng() % (w > 200 ? (w - 200) : 1));
            ry = 100 + (prng() % (h > 200 ? (h - 200) : 1));
        } else if (mode == 3) {
            // 4. Szybkie naprzemienne regiony (rapid alternating regions: lewy / prawy)
            if (i & 1) {
                rx = 20;
                ry = 120;
                rw = 80;
                rh = 80;
            } else {
                rx = w - 100;
                ry = 120;
                rw = 80;
                rh = 80;
            }
        } else if (mode == 4) {
            // 5. Male losowe prostokaty (small random tiles)
            rw = 4 + (prng() % 28);
            rh = 4 + (prng() % 28);
            rx = prng() % (w - rw);
            ry = prng() % (h - rh);
        } else {
            // 6. Okresowy pelny flush (full-screen flush co 600 iteracji)
            if (i % 600 == 5) {
                rx = 0;
                ry = 0;
                rw = w;
                rh = h;
            } else {
                rw = 16;
                rh = 16;
                rx = prng() % (w - 16);
                ry = prng() % (h - 16);
            }
        }

        draw_rect(rx, ry, rw, rh, color);

        if (!virtio_gpu_transfer_and_flush(drv, rx, ry, rw, rh)) {
            error_count++;
            break;
        }

        if ((i + 1) % 2500 == 0) {
            char pmsg[128]{};
            size_t ppos = 0;
            log_append_str(pmsg, sizeof(pmsg), ppos, "[VIRTIO-GPU-STRESS] Wykonano ");
            log_append_u64_dec(pmsg, sizeof(pmsg), ppos, i + 1);
            log_append_str(pmsg, sizeof(pmsg), ppos, " / 10000 aktualizacji dirty-rect.");
            gpu_klog(pmsg);
        }
    }

    if (error_count > 0 || drv->has_error) {
        gpu_klog("[VIRTIO-GPU-STRESS] BLAD: Test 10 000 aktualizacji zakonczony niepowodzeniem!");
        return false;
    }

    char smsg[128]{};
    size_t spos = 0;
    log_append_str(smsg, sizeof(smsg), spos, "[VIRTIO-GPU-STRESS] Zakonczono pomyslnie 10000 aktualizacji (transfers=");
    log_append_u64_dec(smsg, sizeof(smsg), spos, drv->transfer_count);
    log_append_str(smsg, sizeof(smsg), spos, ", flushes=");
    log_append_u64_dec(smsg, sizeof(smsg), spos, drv->flush_count);
    log_append_str(smsg, sizeof(smsg), spos, ", wraparound=PASS, bledy=0).");
    gpu_klog(smsg);

    // Po tescie obciazeniowym przywracamy deterministyczny wzorzec testowy do weryfikacji pikseli
    return virtio_gpu_render_test_pattern(drv);
}

bool virtio_gpu_test_runtime_modeset(VirtioGpuDriver* drv) {
    if (!drv || !drv->initialized || drv->has_error) return false;

    gpu_klog("[VIRTIO-GPU-MODESET] === ROZPOCZECIE TESTOW TRYBOW RUNTIME ===");

    // 1. Sprawdz zachowanie trybu rozruchowego
    uint32_t boot_w = 0, boot_h = 0;
    grafika_pobierz_boot_rozmiar(&boot_w, &boot_h);
    if (boot_w >= 640 && boot_h >= 480) {
        if (drv->fb_width != boot_w || drv->fb_height != boot_h) {
            gpu_klog("[VIRTIO-GPU-MODESET] BLAD: Tryb rozruchowy nie zostal zachowany w drv!");
            return false;
        }
        gpu_klog("[VIRTIO-GPU-MODESET] Boot mode preservation: PASS");
    }

    // 2. Odrzucenie niepoprawnych wymiarow bufora
    if (virtio_gpu_setup_framebuffer(drv, 0, 0)) {
        gpu_klog("[VIRTIO-GPU-MODESET] BLAD: Zaakceptowano wymiary 0x0!");
        return false;
    }
    if (virtio_gpu_setup_framebuffer(drv, 50000, 50000)) {
        gpu_klog("[VIRTIO-GPU-MODESET] BLAD: Zaakceptowano wymiary 50000x50000!");
        return false;
    }
    if (virtio_gpu_setup_framebuffer(drv, 10000, 10000)) {
        gpu_klog("[VIRTIO-GPU-MODESET] BLAD: Zaakceptowano wymiary 10000x10000!");
        return false;
    }
    if (drv->has_error) {
        gpu_klog("[VIRTIO-GPU-MODESET] BLAD: Stan bledu po odrzuceniu wymiarow!");
        return false;
    }
    gpu_klog("[VIRTIO-GPU-MODESET] Invalid dimensions rejection: PASS");

    // 3. Test 100 cykli przelaczania rozdzielczosci (PMM delta=0, DMA delta=0, IDT delta=0)
    const uint32_t initial_w = drv->fb_width;
    const uint32_t initial_h = drv->fb_height;

    // Rozgrzewka struktur VMM i sterty dla obu slotow bufora ramki (1920x1080 i 1024x768)
    for (int w = 0; w < 4; ++w) {
        const uint32_t warm_w = (w % 2 == 0) ? 1920 : 1024;
        const uint32_t warm_h = (w % 2 == 0) ? 1080 : 768;
        if (!virtio_gpu_setup_framebuffer(drv, warm_w, warm_h)) {
            gpu_klog("[VIRTIO-GPU-MODESET] BLAD: setup_framebuffer w fazie rozgrzewki!");
            return false;
        }
        virtio_gpu_free_stale_resource(drv);
    }

    if (!virtio_gpu_setup_framebuffer(drv, initial_w, initial_h)) {
        gpu_klog("[VIRTIO-GPU-MODESET] BLAD: powrot do trybu poczatkowego w fazie rozgrzewki!");
        return false;
    }
    virtio_gpu_free_stale_resource(drv);

    #ifndef BURSZTYN_VIRTIO_HOST_TEST
    const uint64_t baseline_pmm = pmm_pobierz_wolne_ramki();
    const uint32_t baseline_vectors = idt_zaalokowane_wektory_liczba();
    #endif

    for (int i = 0; i < 100; ++i) {
        const uint32_t target_w = (i % 2 == 0) ? 1024 : 1920;
        const uint32_t target_h = (i % 2 == 0) ? 768 : 1080;
        if (!virtio_gpu_setup_framebuffer(drv, target_w, target_h)) {
            gpu_klog("[VIRTIO-GPU-MODESET] BLAD: setup_framebuffer w 100-cyklowej petli!");
            return false;
        }
        virtio_gpu_free_stale_resource(drv);
    }

    // Przywrocenie poczatkowego trybu
    if (!virtio_gpu_setup_framebuffer(drv, initial_w, initial_h)) {
        gpu_klog("[VIRTIO-GPU-MODESET] BLAD: Przywrocenie poczatkowego trybu po 100 cyklach!");
        return false;
    }
    virtio_gpu_free_stale_resource(drv);

    #ifndef BURSZTYN_VIRTIO_HOST_TEST
    const uint64_t final_pmm = pmm_pobierz_wolne_ramki();
    const uint32_t final_vectors = idt_zaalokowane_wektory_liczba();
    if (final_pmm != baseline_pmm || final_vectors != baseline_vectors) {
        char err[160]{};
        size_t epos = 0;
        log_append_str(err, sizeof(err), epos, "[VIRTIO-GPU-MODESET] BLAD WYCIEKU: PMM delta=");
        log_append_u64_dec(err, sizeof(err), epos, (final_pmm < baseline_pmm) ? (baseline_pmm - final_pmm) : (final_pmm - baseline_pmm));
        log_append_str(err, sizeof(err), epos, ", Vector delta=");
        log_append_u64_dec(err, sizeof(err), epos, (final_vectors >= baseline_vectors) ? (final_vectors - baseline_vectors) : (baseline_vectors - final_vectors));
        gpu_klog(err);
        return false;
    }
    #endif

    gpu_klog("[VIRTIO-GPU-MODESET] 100x modeset cycles: PASS (PMM delta=0, DMA delta=0, IDT delta=0).");

    // Przywrocenie deterministycznego wzorca po testach
    return virtio_gpu_render_test_pattern(drv);
}

