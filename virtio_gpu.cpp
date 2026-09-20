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

#define gpu_klog host_klog

static bool gpu_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void gpu_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

static inline uint64_t gpu_monotonic_ns(void) {
    return host_czas_monotoniczny_ns();
}

#else
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "grafika.h"
#include "pamiec.h"

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
 * Maksymalne dopuszczalne czasy oczekiwania i limity petli.
 */
constexpr uint64_t VIRTIO_GPU_DEFAULT_TIMEOUT_MS = 500ULL;
constexpr uint32_t VIRTIO_GPU_MAX_SPINS_GUARD     = 5000000U;

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
        req_len > 2048 || resp_len > 2048 || drv->has_error) {
        return false;
    }

    /* Kopiujemy polecenie do obszaru DMA zadania (offset 0) */
    gpu_memcpy(drv->cmd_vaddr, req, req_len);

    /* Zerujemy obszar odpowiedzi DMA (offset 2048) */
    uint8_t* resp_dma_vaddr = static_cast<uint8_t*>(drv->cmd_vaddr) + 2048;
    gpu_secure_zero(resp_dma_vaddr, resp_len);

    const uint64_t req_paddr = drv->cmd_paddr;
    const uint64_t resp_paddr = drv->cmd_paddr + 2048ULL;

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

    while (spin_guard > 0) {
        spin_guard--;

        if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) || virtqueue_has_used(drv->controlq)) {
            void* cookie = nullptr;
            uint32_t used_len = 0;
            int popped = virtqueue_pop_used(drv->controlq, &cookie, &used_len);
            if (popped >= 0) {
                completed = true;
                break;
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
    if (!drv || resource_id == 0 || !entries || nr_entries == 0 || nr_entries > 64) {
        return false;
    }

    GpuSpinLockGuard guard(drv->lock);

    /* Tworzymy bufor zapytania z naglowkiem i tablica mem_entries */
    uint8_t cmd_buf[sizeof(VirtioGpuResourceAttachBacking) + sizeof(VirtioGpuMemEntry) * 64];
    VirtioGpuResourceAttachBacking* req = reinterpret_cast<VirtioGpuResourceAttachBacking*>(cmd_buf);
    req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req->hdr.flags = 0;
    req->hdr.fence_id = 0;
    req->hdr.ctx_id = 0;
    req->hdr.padding = 0;
    req->resource_id = resource_id;
    req->nr_entries = nr_entries;

    VirtioGpuMemEntry* dst_entries = reinterpret_cast<VirtioGpuMemEntry*>(
        cmd_buf + sizeof(VirtioGpuResourceAttachBacking));
    for (uint32_t i = 0; i < nr_entries; ++i) {
        dst_entries[i] = entries[i];
    }

    const size_t total_cmd_len = sizeof(VirtioGpuResourceAttachBacking) +
                                 sizeof(VirtioGpuMemEntry) * nr_entries;

    VirtioGpuCtrlHdr resp{};
    return virtio_gpu_send_command_locked(
        drv, cmd_buf, total_cmd_len, &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
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
    return virtio_gpu_send_command_locked(
        drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
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

bool virtio_gpu_setup_framebuffer(VirtioGpuDriver* drv, uint32_t width, uint32_t height) {
    if (!drv || width == 0 || height == 0 || width > 4096 || height > 4096) {
        return false;
    }

    const uint64_t stride = static_cast<uint64_t>(width) * 4ULL;
    if ((stride & 3U) != 0) return false;

    const uint64_t total_bytes = stride * static_cast<uint64_t>(height);
    if (total_bytes == 0 || total_bytes > 64ULL * 1024 * 1024) return false;

    /*
     * 1. Utworzenie zasobu 2D
     */
    constexpr uint32_t RES_ID = 1;
    constexpr uint32_t FORMAT = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;

    if (!virtio_gpu_cmd_resource_create_2d(drv, RES_ID, FORMAT, width, height)) {
        gpu_klog("[VIRTIO-GPU] BLAD: resource_create_2d failed!");
        return false;
    }
    drv->resource_created = true;

    /*
     * 2. Alokacja pamieci fizycznej dla bufora ramki (DMA)
     */
    void* fb_vaddr = nullptr;
    uint64_t fb_paddr = 0;
    size_t fb_pages = 0;

    if (!gpu_dma_alloc(total_bytes, 4096, &fb_vaddr, &fb_paddr, &fb_pages)) {
        gpu_klog("[VIRTIO-GPU] BLAD: gpu_dma_alloc dla bufora ramki failed!");
        virtio_gpu_cmd_resource_unref(drv, RES_ID);
        drv->resource_created = false;
        return false;
    }

    drv->fb_vaddr = fb_vaddr;
    drv->fb_size = total_bytes;
    drv->fb_pages = fb_pages;
    drv->is_contiguous_dma = true;

    /* Wypelnienie tablicy wpisow scatter-gather */
    static VirtioGpuMemEntry s_entries[1];
    s_entries[0].addr = fb_paddr;
    s_entries[0].length = static_cast<uint32_t>(fb_pages * 4096ULL);
    s_entries[0].padding = 0;

    drv->mem_entries = s_entries;
    drv->nr_entries = 1;

    /*
     * 3. Podpiecie bufora backing
     */
    if (!virtio_gpu_cmd_resource_attach_backing(drv, RES_ID, drv->mem_entries, drv->nr_entries)) {
        gpu_klog("[VIRTIO-GPU] BLAD: resource_attach_backing failed!");
        gpu_dma_free(drv->fb_vaddr, fb_paddr, drv->fb_pages);
        drv->fb_vaddr = nullptr;
        virtio_gpu_cmd_resource_unref(drv, RES_ID);
        drv->resource_created = false;
        return false;
    }
    drv->backing_attached = true;

    /*
     * 4. Powiazanie scanoutu z zasobem
     */
    if (!virtio_gpu_cmd_set_scanout(drv, drv->scanout_id, RES_ID, 0, 0, width, height)) {
        gpu_klog("[VIRTIO-GPU] BLAD: set_scanout failed!");
        virtio_gpu_cmd_resource_detach_backing(drv, RES_ID);
        drv->backing_attached = false;
        gpu_dma_free(drv->fb_vaddr, fb_paddr, drv->fb_pages);
        drv->fb_vaddr = nullptr;
        virtio_gpu_cmd_resource_unref(drv, RES_ID);
        drv->resource_created = false;
        return false;
    }
    drv->scanout_set = true;

    drv->resource_id = RES_ID;
    drv->fb_width = width;
    drv->fb_height = height;
    drv->fb_stride = static_cast<uint32_t>(stride);
    drv->fb_format = FORMAT;

    char msg[128]{};
    size_t mpos = 0;
    log_append_str(msg, sizeof(msg), mpos, "[VIRTIO-GPU] Framebuffer gotowy: ");
    log_append_u64_dec(msg, sizeof(msg), mpos, width);
    log_append_str(msg, sizeof(msg), mpos, "x");
    log_append_u64_dec(msg, sizeof(msg), mpos, height);
    log_append_str(msg, sizeof(msg), mpos, " (stride=");
    log_append_u64_dec(msg, sizeof(msg), mpos, stride);
    log_append_str(msg, sizeof(msg), mpos, " B, format=B8G8R8X8)");
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

    /* Alokacja bufora DMA dla polecen i odpowiedzi (4 KiB) */
    if (!gpu_dma_alloc(4096, 4096, &drv->cmd_vaddr, &drv->cmd_paddr, &drv->cmd_pages)) {
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
    uint32_t target_w = 1280;
    uint32_t target_h = 800;

    if (virtio_gpu_cmd_get_display_info(drv, &dinfo)) {
        if (dinfo.pmodes[0].enabled && dinfo.pmodes[0].r.width > 0 && dinfo.pmodes[0].r.height > 0) {
            target_w = dinfo.pmodes[0].r.width;
            target_h = dinfo.pmodes[0].r.height;
            drv->scanout_enabled = true;
        }
    }

    drv->display_width = target_w;
    drv->display_height = target_h;
    drv->scanout_id = 0;

    /* Utworzenie i podpiecie bufora ramki 2D */
    if (!virtio_gpu_setup_framebuffer(drv, target_w, target_h)) {
        gpu_klog("[VIRTIO-GPU] OSTRZEZENIE: setup_framebuffer nie powiodl sie!");
    }

    gpu_klog("[VIRTIO-GPU] Karta graficzna gotowa do pracy w trybie 2D.");
    return true;
}

bool virtio_gpu_reset(VirtioGpuDriver* drv) {
    if (!drv) return false;

    GpuSpinLockGuard guard(drv->lock);

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
        gpu_dma_free(drv->fb_vaddr, drv->mem_entries ? drv->mem_entries[0].addr : 0, drv->fb_pages);
        drv->fb_vaddr = nullptr;
        drv->fb_size = 0;
        drv->fb_pages = 0;
    }

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
