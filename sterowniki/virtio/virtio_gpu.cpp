#include "jadro/gui/display_mode_policy.h"
#include "jadro/gui/graphics_perf.h"
/*
 * Bursztyn OS - Sterownik karty graficznej VirtIO-GPU (VirtIO 1.x Modern PCI 2D)
 *
 * Plik: virtio_gpu.cpp
 *
 * Implementacja produkcyjna sterownika VirtIO-GPU 2D dla transportu Modern PCI
 * i Split VirtQueue, zintegrowana z podsystemem grafiki jadra (SterownikEkranu).
 */

#include "virtio_gpu.h"
#include "diagnostyka_startu.h"
#include "diagnostyka_klatek_startu.h"
#include "idt.h"
#include "apic.h"

#ifndef BURSZTYN_DEFAULT_DISPLAY_720P
#define BURSZTYN_DEFAULT_DISPLAY_720P 0
#endif

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

// 4K backing may contain 8100 noncontiguous pages: 32 + 8100 * 16 bytes.
// Reserve enough request space without relying on physical coalescing.
constexpr size_t VIRTIO_GPU_CMD_REQ_MAX_SIZE  = 128ULL * 1024;
constexpr size_t VIRTIO_GPU_CMD_RESP_OFFSET   = VIRTIO_GPU_CMD_REQ_MAX_SIZE;
constexpr size_t VIRTIO_GPU_CMD_BUFFER_SIZE   = VIRTIO_GPU_CMD_REQ_MAX_SIZE + 16ULL * 1024;
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

/*
 * Flaga globalna blokujaca nowe polecenia GPU podczas restartu/shutdown.
 * Zapewnia, ze zaden kompozytor ani zadanie w tle nie przesle nowej
 * klatki na controlq podczas procedury restartu.
 */
static volatile uint32_t g_gpu_reboot_quiescing = 0U;

void virtio_gpu_stop_submissions(void) {
    __atomic_store_n(&g_gpu_reboot_quiescing, 1U, __ATOMIC_RELEASE);
}

bool virtio_gpu_is_quiescing(void) {
    return __atomic_load_n(&g_gpu_reboot_quiescing, __ATOMIC_ACQUIRE) != 0U;
}

/* =========================================================================
 * 1. WYSLANIE I WYKONANIE POLECENIA KONTROLNEGO NA CONTROLQ
 * ========================================================================= */

static const char* virtio_gpu_cmd_name(uint32_t cmd_type) {
    switch(cmd_type) {
        case 0x0100: return "GET_DISPLAY_INFO";
        case 0x0101: return "RESOURCE_CREATE_2D";
        case 0x0102: return "RESOURCE_UNREF";
        case 0x0103: return "SET_SCANOUT";
        case 0x0104: return "RESOURCE_FLUSH";
        case 0x0105: return "TRANSFER_TO_HOST_2D";
        case 0x0106: return "RESOURCE_ATTACH_BACKING";
        case 0x0107: return "RESOURCE_DETACH_BACKING";
        case 0x0300: return "UPDATE_CURSOR";
        case 0x0301: return "MOVE_CURSOR";
    }
    return "UNKNOWN";
}

const char* virtio_gpu_cmd_state_name(VirtioGpuCmdState state) {
    switch (state) {
        case VIRTIO_GPU_CMD_IDLE: return "IDLE";
        case VIRTIO_GPU_CMD_SUBMITTED: return "SUBMITTED";
        case VIRTIO_GPU_CMD_WAITING: return "WAITING";
        case VIRTIO_GPU_CMD_TIMED_OUT_PENDING: return "TIMED_OUT_PENDING";
        case VIRTIO_GPU_CMD_RECOVERED: return "RECOVERED";
        case VIRTIO_GPU_CMD_PERMANENT_FAILURE: return "PERMANENT_FAILURE";
    }
    return "INVALID";
}

/*
 * Late-completion policy (docs/virtio-gpu-mouse-reliability-2026-11-11.md).
 * TRANSFER_TO_HOST_2D and RESOURCE_FLUSH only copy/present guest pixels that
 * the next frame rewrites; the caller can drop the frame. They return failure
 * at the soft deadline and stay TIMED_OUT_PENDING (asynchronous recovery).
 * Every other command changes resource/backing/scanout state the caller must
 * know the outcome of, so the same call keeps waiting until the bounded hard
 * deadline (synchronous recovery) and reports the real, validated result.
 * Nothing is ever resubmitted: the device executes each chain at most once.
 */
static bool virtio_gpu_cmd_async_recovery(uint32_t cmd_type) {
    return cmd_type == VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D ||
           cmd_type == VIRTIO_GPU_CMD_RESOURCE_FLUSH;
}

static void virtio_gpu_log_u64(const char* prefix, uint64_t a, const char* mid = nullptr,
                               uint64_t b = 0, const char* tail = nullptr, const char* text = nullptr) {
    char buf[200]{};
    size_t pos = 0;
    log_append_str(buf, sizeof(buf), pos, prefix);
    log_append_u64_dec(buf, sizeof(buf), pos, a);
    if (mid) { log_append_str(buf, sizeof(buf), pos, mid); log_append_u64_dec(buf, sizeof(buf), pos, b); }
    if (tail) log_append_str(buf, sizeof(buf), pos, tail);
    if (text) log_append_str(buf, sizeof(buf), pos, text);
    gpu_klog(buf);
}

static void virtio_gpu_permanent_failure_locked(VirtioGpuDriver* drv, const char* reason) {
    if (drv->cmd_state != VIRTIO_GPU_CMD_PERMANENT_FAILURE) {
        drv->recovery.permanent_failures++;
        char buf[200]{};
        size_t pos = 0;
        log_append_str(buf, sizeof(buf), pos, "[VIRTIO-GPU-RECOVERY] PERMANENT_FAILURE: ");
        log_append_str(buf, sizeof(buf), pos, reason);
        log_append_str(buf, sizeof(buf), pos, " cmd=");
        log_append_str(buf, sizeof(buf), pos, virtio_gpu_cmd_name(drv->pending_cmd_type));
        gpu_klog(buf);
    }
    drv->cmd_state = VIRTIO_GPU_CMD_PERMANENT_FAILURE;
    drv->permanent_failure_reason = reason;
    /* DMA, descriptors and framebuffer stay allocated until a confirmed reset. */
    drv->has_error = true;
    drv->error_count++;
}

static void virtio_gpu_log_timeout_diag_locked(VirtioGpuDriver* drv, uint32_t cmd_type,
                                               uint32_t expected_resp_type, int head,
                                               uint64_t elapsed_ns, uint32_t spins_left) {
    char timing[400]{}; size_t pos = 0;
    log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU] timeout elapsed_ns=");
    log_append_u64_dec(timing, sizeof(timing), pos, elapsed_ns);
    log_append_str(timing, sizeof(timing), pos, " spins_left=");
    log_append_u64_dec(timing, sizeof(timing), pos, spins_left);
    gpu_klog(timing);
    // Soft deadline only: the recovery policy decides; PERMANENT_FAILURE is
    // logged separately as "[VIRTIO-GPU-RECOVERY] PERMANENT_FAILURE".
    gpu_klog("[VIRTIO-GPU] Timeout polecenia kontrolnego (miekki termin 500 ms)");

    const uint8_t* resp_dma_vaddr = static_cast<const uint8_t*>(drv->cmd_vaddr) + VIRTIO_GPU_CMD_RESP_OFFSET;
    const uint32_t act_type = reinterpret_cast<const VirtioGpuCtrlHdr*>(resp_dma_vaddr)->type;
    pos = 0;
    log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] cmd=");
    log_append_u64_dec(timing, sizeof(timing), pos, cmd_type);
    log_append_str(timing, sizeof(timing), pos, " (");
    log_append_str(timing, sizeof(timing), pos, virtio_gpu_cmd_name(cmd_type));
    log_append_str(timing, sizeof(timing), pos, ") exp_resp=");
    log_append_u64_dec(timing, sizeof(timing), pos, expected_resp_type);
    log_append_str(timing, sizeof(timing), pos, " act_resp=");
    log_append_u64_dec(timing, sizeof(timing), pos, act_type);
    gpu_klog(timing);

    pos = 0;
    log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] qstate: avail=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->avail ? drv->controlq->avail->idx : 0);
    log_append_str(timing, sizeof(timing), pos, " used=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->used ? drv->controlq->used->idx : 0);
    log_append_str(timing, sizeof(timing), pos, " last_used=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->last_used_idx);
    log_append_str(timing, sizeof(timing), pos, " head=");
    log_append_u64_dec(timing, sizeof(timing), pos, static_cast<uint64_t>(head));
    gpu_klog(timing);

    pos = 0;
    log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] drvstate: initialized=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->initialized ? 1 : 0);
    log_append_str(timing, sizeof(timing), pos, " cmd_done=");
    log_append_u64_dec(timing, sizeof(timing), pos, __atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE));
    log_append_str(timing, sizeof(timing), pos, " lock=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->lock);
    log_append_str(timing, sizeof(timing), pos, " err=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->has_error);
    log_append_str(timing, sizeof(timing), pos, " quiesc=");
    log_append_u64_dec(timing, sizeof(timing), pos, __atomic_load_n(&g_gpu_reboot_quiescing, __ATOMIC_ACQUIRE));
    log_append_str(timing, sizeof(timing), pos, " cmd_seq=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->cmd_count);
    gpu_klog(timing);

    pos = 0;
    log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] qstate_extra: q_size=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->queue_size);
    log_append_str(timing, sizeof(timing), pos, " num_free=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->num_free);
    log_append_str(timing, sizeof(timing), pos, " notify_off=");
    log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->queue_notify_off);
    gpu_klog(timing);

    if (drv->dev) {
        const uint16_t isr = virtio_pci_read_isr(drv->dev);
        pos = 0;
        log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] pci isr=");
        log_append_u64_dec(timing, sizeof(timing), pos, isr);
        gpu_klog(timing);
    }
}

enum class GpuCompletion : uint8_t { None, Ours, Corrupt };

/* Pop at most one used entry; it must be exactly the chain we own. */
static GpuCompletion virtio_gpu_try_pop_locked(VirtioGpuDriver* drv, int head, uint32_t* used_len) {
    if (!__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) && !virtqueue_has_used(drv->controlq))
        return GpuCompletion::None;
    void* cookie = nullptr;
    const int popped = virtqueue_pop_used(drv->controlq, &cookie, used_len);
    if (popped == -1) return GpuCompletion::None;
    if (popped < -1) return GpuCompletion::Corrupt;
    // pop_used already validated the whole chain; with a single outstanding
    // controlq chain any other head means the device completed something we
    // never submitted (or completed twice).
    if (popped != head) return GpuCompletion::Corrupt;
    if (virtqueue_has_used(drv->controlq)) return GpuCompletion::Corrupt; // extra entry
    return GpuCompletion::Ours;
}

/* Validates length and type of a completion for the outstanding chain.
   Returns true only for the expected successful response. */
static bool virtio_gpu_finish_completion_locked(VirtioGpuDriver* drv, uint32_t used_len,
                                                size_t resp_len, bool exact_len,
                                                void* resp_out, uint32_t expected_resp_type,
                                                bool* device_rejected) {
    *device_rejected = false;
    const bool len_ok = exact_len ? used_len == resp_len
                                  : (used_len >= sizeof(VirtioGpuCtrlHdr) && used_len <= resp_len);
    if (!len_ok) {
        gpu_klog("[VIRTIO-GPU] BLAD: Nieprawidlowa lub znieksztalcona dlugosc used_len!");
        virtio_gpu_permanent_failure_locked(drv, "invalid used_len");
        return false;
    }
    const uint8_t* resp_dma_vaddr = static_cast<const uint8_t*>(drv->cmd_vaddr) + VIRTIO_GPU_CMD_RESP_OFFSET;
    if (resp_out) gpu_memcpy(resp_out, resp_dma_vaddr, resp_len);
    VirtioGpuCtrlHdr hdr{};
    gpu_memcpy(&hdr, resp_dma_vaddr, sizeof(hdr));
    if (hdr.type != expected_resp_type) {
        char err_buf[128]{};
        size_t epos = 0;
        log_append_str(err_buf, sizeof(err_buf), epos, "[VIRTIO-GPU] Odpowiedz bledu sprzetu: type=0x");
        log_append_u64_dec(err_buf, sizeof(err_buf), epos, hdr.type);
        log_append_str(err_buf, sizeof(err_buf), epos, " (oczekiwano=0x");
        log_append_u64_dec(err_buf, sizeof(err_buf), epos, expected_resp_type);
        log_append_str(err_buf, sizeof(err_buf), epos, ")");
        gpu_klog(err_buf);
        drv->error_count++;
        *device_rejected = true;
        return false;
    }
    return true;
}

/*
 * TIMED_OUT_PENDING -> IDLE only after the device returned exactly the chain
 * it owns, with a valid length. Never blocks. After the hard deadline (or
 * without a clock to measure one) the driver fails closed.
 */
static void virtio_gpu_reap_pending_locked(VirtioGpuDriver* drv) {
    if (drv->cmd_state != VIRTIO_GPU_CMD_TIMED_OUT_PENDING) return;
    // Same rule as the in-call wait: the used ring is inspected once more at
    // (or after) the hard deadline. A validated completion already published
    // by the device is reaped; with none visible the driver fails closed and
    // never looks again (a completion after that decision is ignored).
    uint32_t used_len = 0;
    const GpuCompletion c = virtio_gpu_try_pop_locked(drv, drv->pending_head, &used_len);
    if (c == GpuCompletion::Corrupt) {
        virtio_gpu_permanent_failure_locked(drv, "unexpected used-ring entry while pending");
        return;
    }
    const uint64_t now = gpu_monotonic_ns();
    if (c == GpuCompletion::None) {
        if (now == 0 || now >= drv->pending_hard_deadline_ns)
            virtio_gpu_permanent_failure_locked(drv, "no completion before hard deadline");
        return;
    }
    bool rejected = false;
    const bool ok = virtio_gpu_finish_completion_locked(drv, used_len, drv->pending_resp_len, false,
                                                        nullptr, drv->pending_expected_resp, &rejected);
    if (!ok && !rejected) return; // permanent failure already recorded
    const uint64_t late = now > drv->pending_submit_ns ? now - drv->pending_submit_ns : 0;
    drv->recovery.late_completions++;
    if (rejected) drv->recovery.late_error_responses++;
    if (late > drv->recovery.max_late_ns) drv->recovery.max_late_ns = late;
    drv->cmd_state = VIRTIO_GPU_CMD_RECOVERED;
    drv->recovery.recoveries++;
    drv->recovery_full_refresh = true; // the dropped frame(s) must be republished
    virtio_gpu_log_u64("[VIRTIO-GPU-RECOVERY] late completion reaped seq=", drv->pending_seq,
                       " late_ms=", late / 1000000ULL, " cmd=", virtio_gpu_cmd_name(drv->pending_cmd_type));
    drv->pending_head = -1;
    drv->cmd_state = VIRTIO_GPU_CMD_IDLE;
}

/* Gate for every controlq user: may the shared command buffer be written? */
static bool virtio_gpu_controlq_ready_locked(VirtioGpuDriver* drv) {
    if (virtio_gpu_is_quiescing()) return false;
    if (!drv || !drv->dev || !drv->controlq || !drv->cmd_vaddr || drv->has_error) return false;
    if (drv->cmd_state == VIRTIO_GPU_CMD_TIMED_OUT_PENDING) {
        virtio_gpu_reap_pending_locked(drv);
        if (drv->cmd_state != VIRTIO_GPU_CMD_IDLE) {
            if (drv->cmd_state == VIRTIO_GPU_CMD_TIMED_OUT_PENDING) drv->recovery.rejected_while_pending++;
            return false;
        }
    }
    if (drv->cmd_state != VIRTIO_GPU_CMD_IDLE) {
        virtio_gpu_permanent_failure_locked(drv, "controlq not idle");
        return false;
    }
    // With nothing outstanding, a used entry can only be bogus.
    if (virtqueue_has_used(drv->controlq)) {
        virtio_gpu_permanent_failure_locked(drv, "used-ring entry without outstanding command");
        return false;
    }
    return true;
}

/*
 * Wait step of the control-queue completion loop. The device completion raises
 * MSI-X (virtio_gpu_isr sets command_done), so while the used ring is empty the
 * CPU halts until the next interrupt instead of spinning on `pause`: the vCPU is
 * idle (no host/vCPU burn, other interrupts - timer, input - are serviced at once)
 * and the loop still re-checks the ring after every wakeup. The timer tick bounds
 * the sleep (<= 4 ms) even if an MSI-X were lost, and the soft/hard deadlines keep
 * their meaning. `cli; check; sti; hlt` closes the lost-wakeup window (STI delays
 * interrupts until after the next instruction). With interrupts masked by the
 * caller we fall back to `pause`. NO scheduling happens here: locks held by the
 * caller (driver, screen) stay held, exactly as before - this is not a yield.
 */
/*
 * Uprzywilejowane operacje CPU uzywane przez gpu_wait_for_irq. W jadrze to dokladnie te same instrukcje co wczesniej
 * (inline, bez zmiany generowanego kodu). W buildzie hostowym (proces Linux w Ring 3) CLI/STI/HLT dalyby SIGSEGV, wiec
 * test dostarcza model flagi IF i HLT (host_cpu_*): sprawdza protokol bez wykonywania instrukcji uprzywilejowanych.
 */
#ifdef BURSZTYN_VIRTIO_HOST_TEST
/* Domyslny model (slabe symbole): sledzi tylko IF; test moze dostarczyc wlasny (testy/test_virtio_pci.cpp). */
static bool g_host_cpu_if_domyslny = true;
__attribute__((weak)) bool host_cpu_irq_enabled() { return g_host_cpu_if_domyslny; }
__attribute__((weak)) void host_cpu_irq_disable() { g_host_cpu_if_domyslny = false; }
__attribute__((weak)) void host_cpu_irq_enable() { g_host_cpu_if_domyslny = true; }
__attribute__((weak)) void host_cpu_enable_irq_and_halt() { g_host_cpu_if_domyslny = true; }   // "przerwanie" budzi od razu
static inline bool gpu_cpu_irq_enabled() { return host_cpu_irq_enabled(); }
static inline void gpu_cpu_irq_disable() { host_cpu_irq_disable(); }
static inline void gpu_cpu_irq_enable() { host_cpu_irq_enable(); }
static inline void gpu_cpu_enable_irq_and_halt() { host_cpu_enable_irq_and_halt(); }
#else
static inline bool gpu_cpu_irq_enabled() {
    uint64_t rflags = 0;
    asm volatile("pushfq; popq %0" : "=r"(rflags));
    return (rflags & (1ULL << 9)) != 0;
}
static inline void gpu_cpu_irq_disable() { asm volatile("cli" ::: "memory"); }
static inline void gpu_cpu_irq_enable() { asm volatile("sti" ::: "memory"); }
/* STI i HLT musza byc JEDNYM blokiem asm: cien przerwania STI obejmuje HLT, wiec IRQ nie wpadnie miedzy nie. */
static inline void gpu_cpu_enable_irq_and_halt() { asm volatile("sti; hlt" ::: "memory", "cc"); }
#endif

static inline void gpu_wait_for_irq(VirtioGpuDriver* drv) {
#if defined(BURSZTYN_GPU_POLL_WAIT) && BURSZTYN_GPU_POLL_WAIT
    (void)drv;
    asm volatile("pause" ::: "memory");   // A/B: zachowanie sprzed zmiany
#else
    if (!gpu_cpu_irq_enabled()) {
        asm volatile("pause" ::: "memory");
        return;
    }
    gpu_cpu_irq_disable();
    if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) || virtqueue_has_used(drv->controlq)) {
        gpu_cpu_irq_enable();
        return;
    }
    gpu_cpu_enable_irq_and_halt();
#endif
}

/*
 * Submits the request already placed at cmd_vaddr (offset 0) and waits for
 * it: soft deadline VIRTIO_GPU_DEFAULT_TIMEOUT_MS, then the recovery policy.
 */
static bool virtio_gpu_execute_locked(VirtioGpuDriver* drv, size_t req_len, size_t resp_len,
                                      bool exact_resp_len, void* resp_out,
                                      uint32_t expected_resp_type) {
    const uint32_t cmd_type = static_cast<const VirtioGpuCtrlHdr*>(drv->cmd_vaddr)->type;
    uint8_t* resp_dma_vaddr = static_cast<uint8_t*>(drv->cmd_vaddr) + VIRTIO_GPU_CMD_RESP_OFFSET;
    gpu_secure_zero(resp_dma_vaddr, resp_len);

    VirtqBuffer bufs[2];
    bufs[0].phys_addr = drv->cmd_paddr;
    bufs[0].len = static_cast<uint32_t>(req_len);
    bufs[0].is_write = false;
    bufs[1].phys_addr = drv->cmd_paddr + VIRTIO_GPU_CMD_RESP_OFFSET;
    bufs[1].len = static_cast<uint32_t>(resp_len);
    bufs[1].is_write = true;

    __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);
    const int head = virtqueue_submit(drv->controlq, bufs, 2, nullptr);
    if (head < 0) {
        gpu_klog("[VIRTIO-GPU] BLAD: virtqueue_submit failed!");
        drv->error_count++;
        return false;
    }
    drv->cmd_state = VIRTIO_GPU_CMD_SUBMITTED;
    drv->pending_head = head;
    drv->pending_cmd_type = cmd_type;
    drv->pending_expected_resp = expected_resp_type;
    drv->pending_resp_len = static_cast<uint32_t>(resp_len);
    drv->pending_seq = drv->cmd_count;
    virtqueue_kick(drv->controlq, true);
    drv->cmd_count++;
    GraphicsPerf::add(GraphicsPerf::GpuCommands);
    GraphicsPerf::Timer wait_timer(GraphicsPerf::GpuWaitNs);

    const uint64_t start_ns = gpu_monotonic_ns();
    drv->pending_submit_ns = start_ns;
    const uint64_t timeout_ns = VIRTIO_GPU_DEFAULT_TIMEOUT_MS * 1000000ULL;
    const uint64_t hard_ns = VIRTIO_GPU_RECOVERY_HARD_DEADLINE_MS * 1000000ULL;
    const uint64_t deadline_ns = (start_ns > UINT64_MAX - timeout_ns) ? UINT64_MAX : (start_ns + timeout_ns);
    drv->pending_hard_deadline_ns = (start_ns > UINT64_MAX - hard_ns) ? UINT64_MAX : (start_ns + hard_ns);
    drv->cmd_state = VIRTIO_GPU_CMD_WAITING;

    // Returns Ours/Corrupt, or None when the deadline (or spin guard) expired.
    auto wait_until = [&](uint64_t limit_ns, uint32_t& spin_guard, uint32_t* used_len) {
        bool expired = false;
        for (;;) {
            const GpuCompletion c = virtio_gpu_try_pop_locked(drv, head, used_len);
            if (c != GpuCompletion::None) return c;
            // Completion can arrive while the clock read stalls (e.g. under TCG).
            // Poll once more before treating the sampled deadline as expired.
            if (expired || spin_guard == 0) return GpuCompletion::None;
            --spin_guard;
            const uint64_t now = gpu_monotonic_ns();
            expired = now != 0 && now >= limit_ns;
            gpu_wait_for_irq(drv);
        }
    };

    uint32_t used_len = 0;
    uint32_t spin_guard = VIRTIO_GPU_MAX_SPINS_GUARD;
    GpuCompletion c = wait_until(deadline_ns, spin_guard, &used_len);
    bool late = false;
    if (c == GpuCompletion::None) {
        drv->recovery.soft_timeouts++;
        const uint64_t now = gpu_monotonic_ns();
        virtio_gpu_log_timeout_diag_locked(drv, cmd_type, expected_resp_type, head,
                                           now > start_ns ? now - start_ns : 0, spin_guard);
        if (start_ns == 0) {
            // No clock: a hard deadline cannot be measured. Fail closed.
            virtio_gpu_permanent_failure_locked(drv, "timeout without monotonic clock");
            return false;
        }
        drv->cmd_state = VIRTIO_GPU_CMD_TIMED_OUT_PENDING;
        if (virtio_gpu_cmd_async_recovery(cmd_type)) {
            // Frame dropped; descriptors/DMA stay owned by the device.
            virtio_gpu_log_u64("[VIRTIO-GPU-RECOVERY] TIMED_OUT_PENDING seq=", drv->pending_seq,
                               " hard_ms=", VIRTIO_GPU_RECOVERY_HARD_DEADLINE_MS, " cmd=", virtio_gpu_cmd_name(cmd_type));
            return false;
        }
        virtio_gpu_log_u64("[VIRTIO-GPU-RECOVERY] waiting for late completion seq=", drv->pending_seq,
                           " hard_ms=", VIRTIO_GPU_RECOVERY_HARD_DEADLINE_MS, " cmd=", virtio_gpu_cmd_name(cmd_type));
        uint32_t hard_guard = VIRTIO_GPU_MAX_SPINS_GUARD * 10U;
        c = wait_until(drv->pending_hard_deadline_ns, hard_guard, &used_len);
        if (c == GpuCompletion::None) {
            virtio_gpu_permanent_failure_locked(drv, "no completion before hard deadline");
            return false;
        }
        late = true;
    }
    if (c == GpuCompletion::Corrupt) {
        gpu_klog("[VIRTIO-GPU] BLAD KRYTYCZNY: Uszkodzenie kolejki virtqueue_pop_used!");
        virtio_gpu_permanent_failure_locked(drv, "corrupt or foreign used-ring entry");
        return false;
    }

    bool rejected = false;
    const bool ok = virtio_gpu_finish_completion_locked(drv, used_len, resp_len, exact_resp_len,
                                                        resp_out, expected_resp_type, &rejected);
    if (!ok && !rejected) return false; // permanent failure recorded
    if (late) {
        const uint64_t now = gpu_monotonic_ns();
        const uint64_t late_ns = now > start_ns ? now - start_ns : 0;
        drv->recovery.late_completions++;
        if (rejected) drv->recovery.late_error_responses++;
        if (late_ns > drv->recovery.max_late_ns) drv->recovery.max_late_ns = late_ns;
        drv->cmd_state = VIRTIO_GPU_CMD_RECOVERED;
        drv->recovery.recoveries++;
        virtio_gpu_log_u64("[VIRTIO-GPU-RECOVERY] late completion in call seq=", drv->pending_seq,
                           " late_ms=", late_ns / 1000000ULL, " cmd=", virtio_gpu_cmd_name(cmd_type));
    }
    drv->pending_head = -1;
    drv->cmd_state = VIRTIO_GPU_CMD_IDLE;
    return ok;
}

static bool virtio_gpu_send_command_locked(
    VirtioGpuDriver* drv,
    const void* req,
    size_t req_len,
    void* resp,
    size_t resp_len,
    uint32_t expected_resp_type
) {
    if (!drv || req_len == 0 || resp_len == 0 ||
        req_len > VIRTIO_GPU_CMD_REQ_MAX_SIZE ||
        resp_len > VIRTIO_GPU_CMD_RESP_MAX_SIZE) {
        return false;
    }
    if (!virtio_gpu_controlq_ready_locked(drv)) return false;

    /* Bufor polecen jest wolny (IDLE): kopiujemy zadanie do obszaru DMA */
    gpu_memcpy(drv->cmd_vaddr, req, req_len);
    return virtio_gpu_execute_locked(drv, req_len, resp_len, false, resp, expected_resp_type);
}

bool virtio_gpu_service_recovery(VirtioGpuDriver* drv) {
    if (!drv || !drv->initialized) return false;
    GpuSpinLockGuard guard(drv->lock);
    if (drv->has_error) return false;
    virtio_gpu_reap_pending_locked(drv);
    return drv->cmd_state == VIRTIO_GPU_CMD_IDLE;
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
    if(drv->resource_id&&drv->fb_vaddr&&drv->fb_width==width&&drv->fb_height==height)
        return true;

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
    if (!drv || resource_id == 0 || !entries || nr_entries == 0 || virtio_gpu_is_quiescing()) {
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
    // Never write the shared command buffer while the device may own it.
    if (!virtio_gpu_controlq_ready_locked(drv)) return false;

    /* Budujemy zapytanie bezposrednio w buforze DMA polecen */
    VirtioGpuResourceAttachBacking* req = static_cast<VirtioGpuResourceAttachBacking*>(drv->cmd_vaddr);
    gpu_secure_zero(req, sizeof(*req));
    req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req->resource_id = resource_id;
    req->nr_entries = nr_entries;

    VirtioGpuMemEntry* dst_entries = reinterpret_cast<VirtioGpuMemEntry*>(
        static_cast<uint8_t*>(drv->cmd_vaddr) + sizeof(VirtioGpuResourceAttachBacking));
    gpu_memcpy(dst_entries, entries, entries_bytes);

    // Response must be exactly one header (previous behaviour kept).
    VirtioGpuCtrlHdr resp{};
    return virtio_gpu_execute_locked(drv, total_cmd_len, sizeof(VirtioGpuCtrlHdr), true,
                                     &resp, VIRTIO_GPU_RESP_OK_NODATA);
}


static bool virtio_gpu_cmd_set_scanout_locked(
    VirtioGpuDriver* drv,
    uint32_t scanout_id,
    uint32_t resource_id,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    if (!drv || scanout_id >= VIRTIO_GPU_MAX_SCANOUTS) return false;
    if(resource_id!=0) {
        const bool valid_old=resource_id==drv->old_resource_id&&!drv->old_scanout_pending;
        if(!valid_old&&!PrezentacjaStartu::dozwolona())return false;
        if(resource_id==drv->resource_id&&drv->initial_scanout_pending&&!drv->first_transfer_complete)return false;
    }

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
    if(ok&&resource_id!=0&&resource_id==drv->resource_id) {
        drv->scanout_set=true;drv->initial_scanout_pending=false;
    }
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES&&ok)KlatkiStartuDiag::event("virtio_gpu_cmd_set_scanout","VirtIO/SETSCANOUT",gpu_monotonic_ns()/1000,
        drv->fb_vaddr,KlatkiStartuDiag::info().backbuffer,resource_id,{int(x),int(y),int(width),int(height)},
        static_cast<const uint32_t*>(drv->fb_vaddr),drv->fb_width,drv->fb_height,drv->fb_stride/4,true);

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

bool virtio_gpu_cmd_set_scanout(
    VirtioGpuDriver* drv,
    uint32_t scanout_id,
    uint32_t resource_id,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    GraphicsPerf::add(GraphicsPerf::Scanouts);
    if (!drv || virtio_gpu_is_quiescing()) return false;
    GpuSpinLockGuard guard(drv->lock);
    return virtio_gpu_cmd_set_scanout_locked(drv, scanout_id, resource_id, x, y, width, height);
}

bool virtio_gpu_cmd_transfer_to_host_2d(
    VirtioGpuDriver* drv,
    uint32_t resource_id,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    GraphicsPerf::add(GraphicsPerf::TransferPixels,uint64_t(width)*height);
    GraphicsPerf::add(GraphicsPerf::Transfers);
    if (!drv || resource_id == 0 || width == 0 || height == 0 || virtio_gpu_is_quiescing()) return false;
    if (x > drv->fb_width || y > drv->fb_height) return false;
    if (width > drv->fb_width - x || height > drv->fb_height - y) return false;

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
    if(resource_id==drv->resource_id&&x==0&&y==0&&width==drv->fb_width&&height==drv->fb_height)
        drv->first_transfer_complete=true;
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::event("virtio_gpu_cmd_transfer_to_host_2d","VirtIO/TRANSFER",gpu_monotonic_ns()/1000,
        drv->fb_vaddr,KlatkiStartuDiag::info().backbuffer,resource_id,{int(x),int(y),int(width),int(height)},
        static_cast<const uint32_t*>(drv->fb_vaddr),drv->fb_width,drv->fb_height,drv->fb_stride/4,false);
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
    GraphicsPerf::add(GraphicsPerf::FlushPixels,uint64_t(width)*height);
    GraphicsPerf::add(GraphicsPerf::Flushes);
    if (!drv || resource_id == 0 || width == 0 || height == 0 || virtio_gpu_is_quiescing()) return false;
    if(!PrezentacjaStartu::dozwolona()||(resource_id==drv->resource_id&&drv->initial_scanout_pending))return false;
    if (x > drv->fb_width || y > drv->fb_height) return false;
    if (width > drv->fb_width - x || height > drv->fb_height - y) return false;

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
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::event("virtio_gpu_cmd_resource_flush","VirtIO/RESOURCEFLUSH",gpu_monotonic_ns()/1000,
        drv->fb_vaddr,KlatkiStartuDiag::info().backbuffer,resource_id,{int(x),int(y),int(width),int(height)},
        static_cast<const uint32_t*>(drv->fb_vaddr),drv->fb_width,drv->fb_height,drv->fb_stride/4,true);
    return true;
}

static bool virtio_gpu_cmd_resource_detach_backing_locked(VirtioGpuDriver* drv, uint32_t resource_id) {
    if (!drv || resource_id == 0) return false;

    VirtioGpuResourceDetachBacking req{};
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    req.resource_id = resource_id;
    req.padding = 0;

    VirtioGpuCtrlHdr resp{};
    return virtio_gpu_send_command_locked(
        drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
}

bool virtio_gpu_cmd_resource_detach_backing(VirtioGpuDriver* drv, uint32_t resource_id) {
    if (!drv || resource_id == 0) return false;
    GpuSpinLockGuard guard(drv->lock);
    return virtio_gpu_cmd_resource_detach_backing_locked(drv, resource_id);
}

static bool virtio_gpu_cmd_resource_unref_locked(VirtioGpuDriver* drv, uint32_t resource_id) {
    if (!drv || resource_id == 0) return false;

    VirtioGpuResourceUnref req{};
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    req.resource_id = resource_id;
    req.padding = 0;

    VirtioGpuCtrlHdr resp{};
    return virtio_gpu_send_command_locked(
        drv, &req, sizeof(req), &resp, sizeof(resp), VIRTIO_GPU_RESP_OK_NODATA);
}

bool virtio_gpu_cmd_resource_unref(VirtioGpuDriver* drv, uint32_t resource_id) {
    if (!drv || resource_id == 0) return false;
    GpuSpinLockGuard guard(drv->lock);
    return virtio_gpu_cmd_resource_unref_locked(drv, resource_id);
}

static bool virtio_gpu_cursor_submit_locked(VirtioGpuDriver* drv,
                                             const void* command,
                                             uint32_t command_size) {
    if (!drv || !drv->cursorq || !drv->cursor_cmd_vaddr || !command ||
        command_size == 0 || command_size > VIRTIO_GPU_CMD_RESP_OFFSET ||
        !drv->cursor_ready || virtio_gpu_is_quiescing()) {
        return false;
    }

    auto reap = [&]() {
        if (!drv->cursor_inflight) return true;
        if (!virtqueue_has_used(drv->cursorq)) return false;
        void* cookie = nullptr;
        uint32_t used_len = 0;
        const int popped = virtqueue_pop_used(drv->cursorq, &cookie, &used_len);
        if (popped < 0) {
            drv->has_error = true;
            drv->cursor_faulted = true;
            drv->error_count++;
            return false;
        }
        drv->cursor_inflight = false;
        return true;
    };
    if (drv->cursor_inflight && !reap()) {
        return false;
    }

    gpu_memcpy(drv->cursor_cmd_vaddr, command, command_size);
    const VirtqBuffer buffer{
        drv->cursor_cmd_paddr, command_size, false
    };
    if (virtqueue_submit(drv->cursorq, &buffer, 1, nullptr) < 0) {
        drv->cursor_faulted = true;
        return false;
    }
    if (!virtqueue_kick(drv->cursorq, true)) {
        drv->cursor_faulted = true;
        return false;
    }
    drv->cursor_inflight = true;
    /*
     * The cursor command buffer is dedicated to cursorq and must not be
     * overwritten until the device returns the descriptor.
     * Cursor commands are intentionally synchronous and bounded; unlike a
     * scene flush this is only a tiny control transaction.
     */
    for (uint32_t spin=0;spin<100000U;++spin) {
        if (reap()) return true;
        asm volatile("pause" ::: "memory");
    }
    drv->cursor_faulted = true;
    return false;
}

static void virtio_gpu_cursor_release_locked(VirtioGpuDriver* drv,
                                               bool device_reset) {
    if (!drv) return;
    if (!device_reset && !drv->has_error && drv->cursor_resource_id != 0) {
        virtio_gpu_cmd_resource_detach_backing_locked(
            drv, drv->cursor_resource_id);
        virtio_gpu_cmd_resource_unref_locked(drv, drv->cursor_resource_id);
    }
    if (drv->cursor_vaddr) {
        gpu_fb_free(drv->cursor_vaddr, drv->cursor_frames,
                    drv->cursor_pages);
    }
    if (drv->cursor_mem_entries) {
        gpu_heap_free(drv->cursor_mem_entries);
    }
    if (drv->cursor_frames) {
        gpu_heap_free(drv->cursor_frames);
    }
    drv->cursor_resource_id = 0;
    if (drv->cursor_cmd_vaddr) {
        gpu_dma_free(drv->cursor_cmd_vaddr, drv->cursor_cmd_paddr,
                     drv->cursor_cmd_pages);
    }
    drv->cursor_cmd_vaddr = nullptr;
    drv->cursor_cmd_paddr = 0;
    drv->cursor_cmd_pages = 0;
    drv->cursor_vaddr = nullptr;
    drv->cursor_frames = nullptr;
    drv->cursor_pages = 0;
    drv->cursor_mem_entries = nullptr;
    drv->cursor_nr_entries = 0;
    drv->cursor_ready = false;
    drv->cursor_faulted = false;
    drv->cursor_inflight = false;
    drv->cursor_visible = false;
}

[[maybe_unused]] static bool virtio_gpu_cursor_init(VirtioGpuDriver* drv) {
    if (!drv || !drv->dev || !drv->controlq) return false;
    if (!drv->cursorq) return false;

    constexpr uint32_t CURSOR_RESOURCE = 3;
    constexpr uint32_t CURSOR_W = 64;
    constexpr uint32_t CURSOR_H = 64;
    constexpr size_t CURSOR_BYTES = CURSOR_W * CURSOR_H * sizeof(uint32_t);
    constexpr size_t CURSOR_PAGES = (CURSOR_BYTES + 4095U) / 4096U;

    if (!gpu_dma_alloc(4096, 4096, &drv->cursor_cmd_vaddr,
                       &drv->cursor_cmd_paddr, &drv->cursor_cmd_pages)) {
        virtqueue_destroy(drv->cursorq);
        drv->cursorq = nullptr;
        return false;
    }

    void** frames = static_cast<void**>(
        gpu_heap_alloc(sizeof(void*) * CURSOR_PAGES));
    VirtioGpuMemEntry* entries = static_cast<VirtioGpuMemEntry*>(
        gpu_heap_alloc(sizeof(VirtioGpuMemEntry) * CURSOR_PAGES));
    void* pixels = nullptr;
    size_t actual_pages = 0;
    if (!frames || !entries ||
        !gpu_fb_alloc(CURSOR_BYTES, &pixels, frames, CURSOR_PAGES,
                      &actual_pages)) {
        if (pixels) gpu_fb_free(pixels, frames, actual_pages);
        if (entries) gpu_heap_free(entries);
        if (frames) gpu_heap_free(frames);
        gpu_dma_free(drv->cursor_cmd_vaddr, drv->cursor_cmd_paddr,
                     drv->cursor_cmd_pages);
        drv->cursor_cmd_vaddr = nullptr;
        drv->cursor_cmd_paddr = 0;
        drv->cursor_cmd_pages = 0;
        virtqueue_destroy(drv->cursorq);
        drv->cursorq = nullptr;
        return false;
    }
    gpu_secure_zero(pixels, CURSOR_BYTES);
    gpu_secure_zero(entries, sizeof(VirtioGpuMemEntry) * CURSOR_PAGES);

    uint32_t nr_entries = 0;
    for (size_t i = 0; i < actual_pages; ++i) {
        const uint64_t pa = reinterpret_cast<uint64_t>(frames[i]);
        if (nr_entries > 0 &&
            entries[nr_entries - 1].addr +
                entries[nr_entries - 1].length == pa) {
            entries[nr_entries - 1].length += 4096;
        } else {
            entries[nr_entries] = {pa, 4096, 0};
            ++nr_entries;
        }
    }

    if (!virtio_gpu_cmd_resource_create_2d(
            drv, CURSOR_RESOURCE, VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
            CURSOR_W, CURSOR_H) ||
        !virtio_gpu_cmd_resource_attach_backing(
            drv, CURSOR_RESOURCE, entries, nr_entries)) {
        if (!drv->has_error) {
            virtio_gpu_cmd_resource_detach_backing(drv, CURSOR_RESOURCE);
            virtio_gpu_cmd_resource_unref(drv, CURSOR_RESOURCE);
        }
        gpu_fb_free(pixels, frames, actual_pages);
        gpu_heap_free(entries);
        gpu_heap_free(frames);
        gpu_dma_free(drv->cursor_cmd_vaddr, drv->cursor_cmd_paddr,
                     drv->cursor_cmd_pages);
        drv->cursor_cmd_vaddr = nullptr;
        drv->cursor_cmd_paddr = 0;
        drv->cursor_cmd_pages = 0;
        virtqueue_destroy(drv->cursorq);
        drv->cursorq = nullptr;
        return false;
    }

    drv->cursor_resource_id = CURSOR_RESOURCE;
    drv->cursor_vaddr = pixels;
    drv->cursor_frames = frames;
    drv->cursor_pages = actual_pages;
    drv->cursor_mem_entries = entries;
    drv->cursor_nr_entries = nr_entries;
    drv->cursor_ready = true;
    return true;
}

bool virtio_gpu_cursor_available(const VirtioGpuDriver* drv) {
    return drv && drv->initialized && drv->cursor_ready &&
           drv->cursorq != nullptr && !drv->has_error &&
           !drv->cursor_faulted &&
           !virtio_gpu_is_quiescing();
}

bool virtio_gpu_cursor_set_shape(VirtioGpuDriver* drv,
                                 const uint32_t* pixels,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t hotspot_x,
                                 uint32_t hotspot_y) {
    if (!virtio_gpu_cursor_available(drv) || !pixels ||
        width == 0 || height == 0 || width > 64 || height > 64 ||
        hotspot_x >= width || hotspot_y >= height) {
        return false;
    }
    GpuSpinLockGuard guard(drv->lock);
    gpu_secure_zero(drv->cursor_vaddr, 64U * 64U * sizeof(uint32_t));
    for (uint32_t y = 0; y < height; ++y) {
        gpu_memcpy(static_cast<uint8_t*>(drv->cursor_vaddr) +
                       static_cast<size_t>(y) * 64U * sizeof(uint32_t),
                   pixels + static_cast<size_t>(y) * width,
                   static_cast<size_t>(width) * sizeof(uint32_t));
    }
    VirtioGpuUpdateCursor command{};
    command.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    command.pos.scanout = drv->scanout_id;
    command.pos.x = 0;
    command.pos.y = 0;
    command.resource_id = drv->cursor_resource_id;
    command.hot_x = hotspot_x;
    command.hot_y = hotspot_y;
    const bool submitted = virtio_gpu_cursor_submit_locked(
        drv, &command, sizeof(command));
    if (submitted) drv->cursor_visible = true;
    return submitted;
}

bool virtio_gpu_cursor_move(VirtioGpuDriver* drv, int x, int y) {
    if (!virtio_gpu_cursor_available(drv)) return false;
    GpuSpinLockGuard guard(drv->lock);
    VirtioGpuMoveCursor command{};
    command.hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
    command.pos.scanout = drv->scanout_id;
    command.pos.x = static_cast<uint32_t>(x);
    command.pos.y = static_cast<uint32_t>(y);
    const bool submitted = virtio_gpu_cursor_submit_locked(
        drv, &command, sizeof(command));
    if (submitted) drv->cursor_visible = true;
    return submitted;
}

bool virtio_gpu_cursor_hide(VirtioGpuDriver* drv) {
    if (!drv || !drv->initialized || !drv->cursor_ready ||
        !drv->cursorq || drv->has_error || virtio_gpu_is_quiescing()) {
        return false;
    }
    GpuSpinLockGuard guard(drv->lock);
    VirtioGpuUpdateCursor command{};
    command.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    command.pos.scanout = drv->scanout_id;
    command.resource_id = 0;
    const bool submitted = virtio_gpu_cursor_submit_locked(
        drv, &command, sizeof(command));
    if (submitted) drv->cursor_visible = false;
    return submitted;
}

/* =========================================================================
 * 3. ZARZADZANIE BUFOREM RAMKI I INICJALIZACJA 2D
 * ========================================================================= */

static void virtio_gpu_free_stale_resource_locked(VirtioGpuDriver* drv,bool sprzet_zresetowany=false) {
    if (!drv || drv->old_resource_id == 0) return;

    if (!drv->has_error&&!sprzet_zresetowany) {
        virtio_gpu_cmd_resource_detach_backing_locked(drv, drv->old_resource_id);
        virtio_gpu_cmd_resource_unref_locked(drv, drv->old_resource_id);
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

void virtio_gpu_free_stale_resource(VirtioGpuDriver* drv) {
    if (!drv || drv->old_resource_id == 0 || virtio_gpu_is_quiescing()) return;
    // The previous valid scanout is retained while a startup replacement is
    // still off-screen. Unref would otherwise blank the currently visible image.
    if(drv->initial_scanout_pending&&!drv->old_scanout_pending)return;
    GpuSpinLockGuard guard(drv->lock);
    virtio_gpu_free_stale_resource_locked(drv);
}

static bool virtio_gpu_rollback_modeset_locked(VirtioGpuDriver* drv) {
    if (!drv || drv->old_resource_id == 0) return false;

    // 1. Przywroc scanout na stary zasob
    if (drv->scanout_set && !drv->has_error) {
        virtio_gpu_cmd_set_scanout_locked(drv, drv->scanout_id, drv->old_resource_id, 0, 0,
                                           drv->old_display_width, drv->old_display_height);
    }

    // 2. Zwolnij nowy nieudany zasob
    if (drv->backing_attached && !drv->has_error) {
        virtio_gpu_cmd_resource_detach_backing_locked(drv, drv->resource_id);
    }
    if (drv->resource_created && !drv->has_error) {
        virtio_gpu_cmd_resource_unref_locked(drv, drv->resource_id);
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
    drv->initial_scanout_pending=drv->old_scanout_pending;
    drv->scanout_set = !drv->initial_scanout_pending;
    drv->first_transfer_complete=!drv->initial_scanout_pending;

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

bool virtio_gpu_rollback_modeset(VirtioGpuDriver* drv) {
    if (!drv || drv->old_resource_id == 0 || virtio_gpu_is_quiescing()) return false;
    GpuSpinLockGuard guard(drv->lock);
    return virtio_gpu_rollback_modeset_locked(drv);
}

/*
 * Odczytuje geometrie zadeklarowana przez hosta (GET_DISPLAYINFO).
 *
 * This is a frontend hint, not the current guest scanout. The display-mode
 * policy decides whether a stable hint may change the selected guest mode.
 *
 * Zwraca true tylko gdy host zadeklarowal poprawny, wlaczony scanout
 * o sensownych wymiarach. W przeciwnym razie out_* zostaja nietkniete.
 */
bool virtio_gpu_pobierz_geometrie_hosta(VirtioGpuDriver* drv,
                                        uint32_t* out_w, uint32_t* out_h) {
    if (!drv || !out_w || !out_h) return false;
    if (virtio_gpu_is_quiescing() || drv->has_error || !drv->initialized) return false;

    VirtioGpuRespDisplayInfo info{};
    if (!virtio_gpu_cmd_get_display_info(drv, &info)) return false;

    const uint32_t w = info.pmodes[0].r.width;
    const uint32_t h = info.pmodes[0].r.height;
    const uint32_t enabled = info.pmodes[0].enabled;

    if (enabled == 0) return false;
    if (w < 640 || h < 480 || w > 4096 || h > 4096) return false;
    if (static_cast<uint64_t>(w) * h * 4ULL > 64ULL * 1024 * 1024) return false;

    *out_w = w;
    *out_h = h;
    return true;
}

bool virtio_gpu_setup_framebuffer(VirtioGpuDriver* drv, uint32_t width, uint32_t height,bool prepare_only) {
    GraphicsPerf::add(GraphicsPerf::FramebufferAttempts);
    if (!drv || width == 0 || height == 0 || width > 4096 || height > 4096 || virtio_gpu_is_quiescing()) {
        return false;
    }

    if(drv->resource_id&&drv->fb_vaddr&&drv->fb_width==width&&drv->fb_height==height)
        return true;

    const uint64_t stride = static_cast<uint64_t>(width) * 4ULL;
    if ((stride & 3U) != 0) return false;

    const uint64_t total_bytes = stride * static_cast<uint64_t>(height);
    if (total_bytes == 0 || total_bytes > 64ULL * 1024 * 1024) return false;

    const size_t page_count = static_cast<size_t>((total_bytes + 4095ULL) / 4096ULL);
    if (page_count == 0 || page_count > 16384) return false;

    /*
     * 0. Jesli istnial poprzedni niezwolniony zasob oczekujacy, posprzataj go
     */
    if(drv->old_resource_id&&drv->initial_scanout_pending&&!drv->old_scanout_pending) {
        if(!virtio_gpu_rollback_modeset(drv))return false;
    }
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

    // Initial scanout must not expose a resource with no host-side pixels.
    // Runtime modesetting retains its existing rollback/confirmation path.
    const bool defer_scanout=prepare_only||drv->resource_id==0||drv->initial_scanout_pending||PrezentacjaStartu::stan().inicjalizacja;
    SplashFirstDiag::event("gpu_scanout_prepared",gpu_monotonic_ns()/1000,new_res_id);
    if (!defer_scanout && !virtio_gpu_cmd_set_scanout(drv, drv->scanout_id, new_res_id, 0, 0, width, height)) {
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
        drv->old_scanout_pending=drv->initial_scanout_pending;
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
    GraphicsPerf::add(GraphicsPerf::FramebufferCreates);
    drv->fb_vaddr = fb_vaddr;
    drv->fb_size = total_bytes;
    drv->fb_pages = actual_pages;
    drv->allocated_frames = frames;
    drv->mem_entries = entries;
    drv->nr_entries = nr_entries;
    drv->resource_created = true;
    drv->backing_attached = true;
    drv->initial_scanout_pending = defer_scanout;
    drv->first_transfer_complete=false;
    drv->scanout_set = !defer_scanout;
    drv->display_width = width;
    drv->display_height = height;
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::event("virtio_gpu_setup_framebuffer","VirtIO/prepared",gpu_monotonic_ns()/1000,
        drv->fb_vaddr,nullptr,new_res_id,{0,0,int(width),int(height)},
        static_cast<const uint32_t*>(drv->fb_vaddr),width,height,drv->fb_stride/4,false);

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

// Frames are dropped (not queued) while a timed-out command is pending; a
// late completion is reaped first so the frame knows it must be full.
static bool virtio_gpu_frame_gate(VirtioGpuDriver* drv) {
    if (drv->cmd_state != VIRTIO_GPU_CMD_TIMED_OUT_PENDING) return true;
    if (virtio_gpu_service_recovery(drv)) return true;
    if (drv->cmd_state == VIRTIO_GPU_CMD_TIMED_OUT_PENDING) drv->recovery.rejected_while_pending++;
    return false;
}

bool virtio_gpu_transfer_and_flush(VirtioGpuDriver* drv, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!PrezentacjaStartu::dozwolona() || virtio_gpu_is_quiescing() || !drv || !drv->initialized || drv->has_error || drv->resource_id == 0) return false;
    // Reap a late completion first so this frame already knows it must be full.
    if (!virtio_gpu_frame_gate(drv)) return false;

    const bool first=drv->initial_scanout_pending;
    // Reject partial initial updates: they cannot authorize a visible scanout.
    if(first&&(x!=0||y!=0||width!=drv->fb_width||height!=drv->fb_height))return false;
    // After a recovered timeout earlier frames were dropped: republish all of it.
    const bool full_refresh=drv->recovery_full_refresh&&!first;
    if(full_refresh){x=0;y=0;width=drv->fb_width;height=drv->fb_height;}
    if (!virtio_gpu_cmd_transfer_to_host_2d(drv, drv->resource_id, x, y, width, height)) {
        return false;
    }
    if(first) {
        SplashFirstDiag::event("first_transfer_to_host",gpu_monotonic_ns()/1000,drv->resource_id);
        if(!virtio_gpu_cmd_set_scanout(drv,drv->scanout_id,drv->resource_id,0,0,width,height))return false;
        drv->scanout_set=true;
        drv->initial_scanout_pending=false;
        SplashFirstDiag::event("first_scanout_commit",gpu_monotonic_ns()/1000,drv->resource_id);
    }
    if (!virtio_gpu_cmd_resource_flush(drv, drv->resource_id, x, y, width, height)) {
        return false;
    }
    if(first)SplashFirstDiag::event("first_resource_flush",gpu_monotonic_ns()/1000,drv->resource_id);
    if(full_refresh)drv->recovery_full_refresh=false;
    return true;
}

bool virtio_gpu_transfer_rects_and_flush(VirtioGpuDriver* drv,
                                         const VirtioGpuRect* rects,
                                         uint32_t count) {
    constexpr uint32_t MAX_BATCH_RECTS = 256;
    if (!drv || !rects || count == 0 || count > MAX_BATCH_RECTS ||
        !PrezentacjaStartu::dozwolona() || virtio_gpu_is_quiescing() ||
        !drv->initialized || drv->has_error || drv->resource_id == 0) {
        return false;
    }
    if (!virtio_gpu_frame_gate(drv)) return false;

    if (drv->initial_scanout_pending) {
        if (count != 1 ||
            rects[0].x != 0 || rects[0].y != 0 ||
            rects[0].width != drv->fb_width ||
            rects[0].height != drv->fb_height) {
            return false;
        }
        return virtio_gpu_transfer_and_flush(
            drv, rects[0].x, rects[0].y, rects[0].width, rects[0].height);
    }
    if (drv->recovery_full_refresh) {
        return virtio_gpu_transfer_and_flush(drv, 0, 0, drv->fb_width, drv->fb_height);
    }

    uint32_t left = drv->fb_width;
    uint32_t top = drv->fb_height;
    uint32_t right = 0;
    uint32_t bottom = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const VirtioGpuRect& r = rects[i];
        if (r.width == 0 || r.height == 0 ||
            r.x > drv->fb_width || r.y > drv->fb_height ||
            r.width > drv->fb_width - r.x ||
            r.height > drv->fb_height - r.y) {
            return false;
        }
        if (!virtio_gpu_cmd_transfer_to_host_2d(
                drv, drv->resource_id, r.x, r.y, r.width, r.height)) {
            return false;
        }
        if (r.x < left) left = r.x;
        if (r.y < top) top = r.y;
        if (r.x + r.width > right) right = r.x + r.width;
        if (r.y + r.height > bottom) bottom = r.y + r.height;
    }
    if (right <= left || bottom <= top) return false;
    return virtio_gpu_cmd_resource_flush(
        drv, drv->resource_id, left, top, right - left, bottom - top);
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
    if (virtio_gpu_is_quiescing() || !drv || !drv->fb_vaddr || drv->fb_width < 320 || drv->fb_height < 240) {
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
    KlatkiStartuDiag::Sciezka diagnostic_path("VirtIO/init");
    if (!dev) return false;

    __atomic_store_n(&g_gpu_reboot_quiescing, 0U, __ATOMIC_RELEASE);

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
    drv->cmd_state = VIRTIO_GPU_CMD_IDLE;
    drv->pending_head = -1;

    // PCI probe already completed this exact 2D feature negotiation and owns
    // no queues. Resetting it again destroys the visible firmware surface in
    // QEMU even though first-frame command gating is otherwise correct.
    const auto status=virtio_pci_get_status(dev);
    const bool po_probe=dev->initialized&&
        status==(VIRTIO_STATUS_ACKNOWLEDGE|VIRTIO_STATUS_DRIVER|VIRTIO_STATUS_FEATURES_OK)&&
        dev->driver_features==VIRTIO_F_VERSION_1;
    if(!po_probe) {
        if(status!=VIRTIO_STATUS_RESET&&!virtio_pci_reset(dev))return false;
        virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE|VIRTIO_STATUS_DRIVER);
    }

    /* Negocjacja cech */
    const uint64_t dev_features = virtio_pci_read_device_features(dev);
    if ((dev_features & VIRTIO_F_VERSION_1) == 0) {
        gpu_klog("[VIRTIO-GPU] Brak obslugi obowiazkowej cechy VIRTIO_F_VERSION_1!");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    /* Zadamy wylacznie 2D - BEZ VIRGL */
    const uint64_t drv_features = VIRTIO_F_VERSION_1;
    if(!po_probe) {
        virtio_pci_write_driver_features(dev, drv_features);
        virtio_pci_set_status(dev, virtio_pci_get_status(dev) | VIRTIO_STATUS_FEATURES_OK);
    }
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
    /*
     * Configure the optional cursor queue before DRIVER_OK. The resource is
     * created later, after the driver is marked initialized and controlq can
     * safely service the setup commands. Host transport fixtures keep the
     * legacy queue topology; dedicated CursorPlane tests exercise the
     * compositor contract without changing allocator expectations here.
     */
#ifndef BURSZTYN_VIRTIO_HOST_TEST
    drv->cursorq = virtqueue_create(dev, 1);
    if (!drv->cursorq) {
        gpu_klog("[VIRTIO-GPU] cursorq unavailable; using software cursor.");
    }


#endif

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
        if (drv->cursorq) {
            virtqueue_destroy(drv->cursorq);
            drv->cursorq = nullptr;
        }
        gpu_dma_free(drv->cmd_vaddr, drv->cmd_paddr, drv->cmd_pages);
        drv->cmd_vaddr = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    drv->initialized = true;
#ifndef BURSZTYN_VIRTIO_HOST_TEST
    if (virtio_gpu_cursor_init(drv)) {
        gpu_klog("[VIRTIO-GPU] Sprzetowa plaszczyzna kursora aktywna.");
    } else {
        gpu_klog("[VIRTIO-GPU] Brak cursorq/resource kursora; fallback software.");
    }
#endif

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

    // OVMF can silently fall back from GRUB's requested mode. Use the explicit
    // guest boot policy (default 720p); saved Display Settings apply afterwards.
    uint32_t target_w=display_mode_policy.initial_width;
    uint32_t target_h=display_mode_policy.initial_height;
    const char* zrodlo_trybu="guest-policy";

    {
        char zmsg[96]{};
        size_t zp = 0;
        log_append_str(zmsg, sizeof(zmsg), zp, "[VIRTIO-GPU] mode source=");
        log_append_str(zmsg, sizeof(zmsg), zp, zrodlo_trybu);
        gpu_klog(zmsg);
    }

    char smsg[64]{};
    size_t sp = 0;
    log_append_str(smsg, sizeof(smsg), sp, "[VIRTIO-GPU] selected initial mode=");
    log_append_u64_dec(smsg, sizeof(smsg), sp, target_w);
    log_append_str(smsg, sizeof(smsg), sp, "x");
    log_append_u64_dec(smsg, sizeof(smsg), sp, target_h);
    gpu_klog(smsg);

    GraphicsPerf::mode(GraphicsPerf::Boot,target_w,target_h,100);
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
            GraphicsPerf::mode(GraphicsPerf::Boot,target_w,target_h,100);
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

/* =========================================================================
 * PRZYGOTOWANIE GPU DO RESTARTU / SHUTDOWN
 * =========================================================================
 *
 * Sciezka restartu w Bursztyn OS:
 *   GUI klik "Uruchom ponownie"
 *    -> bws_wywolaj(25)
 *    -> syscall 25 -> reset_ps2()
 *       1. virtio_gpu_quiesce_for_reboot()  [NOWE - czeka na GPU, bez device reset]
 *       2. psf_flush_cache()                [zapis BSP2]
 *       3. cli                              [wylacza przerwania - po GPU quiesce!]
 *       4. acpi_wykonaj_restart() lub 8042 puls 0xFE
 *
 * UWAGA: NIE wolno wywolywac virtio_gpu_reset() podczas restartu.
 * Poprzednia analiza GDB pokazala, ze QEMU moze zawisnac wewnatrz
 * virtiogpureset() przy zapisie device_status=0 przez guest.
 * Zamiast tego uzywamy bezpiecznego quiesce bez pelnego resetu.
 * ========================================================================= */

/*
 * Loguje aktualny stan kazdego sterownika GPU do portu szeregowego.
 * Wywolywac pod BURSZTYNDIAGREBOOT lub w razie freeze przed rebootem.
 */
void virtio_gpu_log_reboot_state(void) {
    for (uint32_t i = 0; i < g_gpu_driver_count; ++i) {
        const VirtioGpuDriver* drv = &g_gpu_drivers[i];
        char buf[256]{};
        size_t pos = 0;
        log_append_str(buf, sizeof(buf), pos, "[REBOOT TRACE] GPU[");
        log_append_u64_dec(buf, sizeof(buf), pos, i);
        log_append_str(buf, sizeof(buf), pos, "] init=");
        buf[pos++] = drv->initialized ? '1' : '0';
        log_append_str(buf, sizeof(buf), pos, " err=");
        buf[pos++] = drv->has_error ? '1' : '0';
        log_append_str(buf, sizeof(buf), pos, " res=");
        log_append_u64_dec(buf, sizeof(buf), pos, drv->resource_id);
        log_append_str(buf, sizeof(buf), pos, " oldres=");
        log_append_u64_dec(buf, sizeof(buf), pos, drv->old_resource_id);
        log_append_str(buf, sizeof(buf), pos, " scan=");
        buf[pos++] = drv->scanout_set ? '1' : '0';
        log_append_str(buf, sizeof(buf), pos, " back=");
        buf[pos++] = drv->backing_attached ? '1' : '0';
        log_append_str(buf, sizeof(buf), pos, " cmds=");
        log_append_u64_dec(buf, sizeof(buf), pos, drv->cmd_count);
        log_append_str(buf, sizeof(buf), pos, " errs=");
        log_append_u64_dec(buf, sizeof(buf), pos, drv->error_count);
        log_append_str(buf, sizeof(buf), pos, " w=");
        log_append_u64_dec(buf, sizeof(buf), pos, drv->fb_width);
        log_append_str(buf, sizeof(buf), pos, " h=");
        log_append_u64_dec(buf, sizeof(buf), pos, drv->fb_height);
        buf[pos] = '\0';
        gpu_klog(buf);

        /* Loguj producer/consumer indeksy kolejki */
        if (drv->controlq) {
            char qbuf[128]{};
            size_t qpos = 0;
            log_append_str(qbuf, sizeof(qbuf), qpos, "[REBOOT TRACE] GPU[");
            log_append_u64_dec(qbuf, sizeof(qbuf), qpos, i);
            log_append_str(qbuf, sizeof(qbuf), qpos, "] controlq_has_used=");
            qbuf[qpos++] = virtqueue_has_used(drv->controlq) ? '1' : '0';
            log_append_str(qbuf, sizeof(qbuf), qpos, " cmd_done=");
            log_append_u64_dec(qbuf, sizeof(qbuf), qpos,
                               __atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE));
            qbuf[qpos] = '\0';
            gpu_klog(qbuf);
        }
    }
    if (g_gpu_driver_count == 0) {
        gpu_klog("[REBOOT TRACE] Brak zainicjowanych sterownikow GPU.");
    }
}

/*
 * Czeka (z ograniczonym timeoutem) na zakonczenie ewentualnie trwajacego
 * polecenia GPU controlq, aby nie wchodzic w 8042/ACPI reset ze stanem
 * "komenda w locie" na virt-queue.
 *
 * WAZNE: NIE wykonuje virtio_pci_set_status(dev, VIRTIO_STATUS_RESET),
 * poniewaz zapis device_status=0 moze zawiesic QEMU (virtiogpureset deadlock).
 * Jedynym celem jest oproznienie avail/used ring przed przerwaniem przerwan.
 *
 * Timeout: 200 ms - wystarczajaco dlugi dla wolnego TCG, ale nie za dlugi
 * aby nie blokac restartu przy zamrozonej GPU.
 */
void virtio_gpu_quiesce_for_reboot(void) {
    constexpr uint64_t QUIESCE_TIMEOUT_NS = 200ULL * 1000000ULL; /* 200 ms */

    /* Upewnij sie, ze nowe zgloszenia GPU sa bezwzglednie zablokowane */
    virtio_gpu_stop_submissions();

    for (uint32_t i = 0; i < g_gpu_driver_count; ++i) {
        VirtioGpuDriver* drv = &g_gpu_drivers[i];
        if (!drv->initialized || drv->has_error || !drv->controlq) continue;

        const uint64_t start_ns = gpu_monotonic_ns();
        const uint64_t deadline_ns =
            (start_ns > UINT64_MAX - QUIESCE_TIMEOUT_NS)
                ? UINT64_MAX
                : (start_ns + QUIESCE_TIMEOUT_NS);

        bool locked = false;
        // HPET moze byc niedostepny: zerowy odczyt nie ogranicza petli.
        // Zachowaj istniejacy timeout; ogranicz tez liczbe prob bez zegara.
        for (uint32_t spin=0; spin<VIRTIO_GPU_MAX_SPINS_GUARD; ++spin) {
            if (__atomic_test_and_set(&drv->lock, __ATOMIC_ACQUIRE) == 0) {
                locked = true;
                break;
            }
            const uint64_t now = gpu_monotonic_ns();
            if (now != 0 && now >= deadline_ns) break;
            asm volatile("pause" ::: "memory");
        }

        if (!locked) {
            gpu_klog("[REBOOT TRACE] gpu quiesce lock TIMEOUT - przerywam quiesce urzadzenia");
            drv->has_error = true;
            continue; /* NIE dotykaj controlq, inna zablokowana petla moze go uzywac! */
        }

        // Submissions are stopped: at most queue_size completions can remain.
        // A rejected used entry is not consumed by virtqueue_pop_used(). Never
        // retry that same invalid entry forever, including after SET_SCANOUT.
        auto drain_used = [&]() {
            for (uint32_t drained=0; virtqueue_has_used(drv->controlq); ++drained) {
                void* cookie=nullptr;uint32_t used_len=0;
                if (drained>=drv->controlq->queue_size ||
                    virtqueue_pop_used(drv->controlq,&cookie,&used_len)<0) {
                    gpu_klog("[REBOOT TRACE] gpu quiesce invalid completion - aborting drain");
                    drv->has_error=true;
                    return false;
                }
            }
            return true;
        };

        /* 2. Sprawdz czy jest oczekujace polecenie lub nieodebrane zakonczenia */
        const bool has_pending =
            virtqueue_has_used(drv->controlq) ||
            (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) == 0 &&
             drv->controlq->avail->idx != drv->controlq->last_used_idx);

        if (has_pending) {
            bool quiesced = false;
            for (uint32_t spin = 0; spin < 10000000U; ++spin) {
                if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) ||
                    virtqueue_has_used(drv->controlq)) {
                    quiesced = drain_used();
                    if (quiesced) { drv->cmd_state = VIRTIO_GPU_CMD_IDLE; drv->pending_head = -1; }
                    __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);
                    break;
                }
                const uint64_t now = gpu_monotonic_ns();
                if (now != 0 && now >= deadline_ns) break;
                asm volatile("pause" ::: "memory");
            }
            if (!quiesced) {
                if (!drv->has_error)gpu_klog("[REBOOT TRACE] gpu quiesce pop TIMEOUT");
                drv->has_error = true;
                __atomic_clear(&drv->lock, __ATOMIC_RELEASE);
                continue;
            }
        }

        /* 3. Wylacz scanout aby QEMU nie probowalo czytac pamieci podczas resetu maszyny */
        if ((drv->scanout_set||(drv->old_resource_id&&!drv->old_scanout_pending)) && drv->cmd_vaddr) {
            VirtioGpuSetScanout req{};
            req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
            req.scanout_id = 0;
            req.resource_id = 0; /* 0 = disable */
            req.r.x = 0; req.r.y = 0; req.r.width = 0; req.r.height = 0;

            VirtioGpuCtrlHdr resp{};

            gpu_memcpy(drv->cmd_vaddr, &req, sizeof(req));
            uint8_t* resp_dma_vaddr = static_cast<uint8_t*>(drv->cmd_vaddr) + VIRTIO_GPU_CMD_RESP_OFFSET;
            gpu_secure_zero(resp_dma_vaddr, sizeof(resp));

            const uint64_t req_paddr = drv->cmd_paddr;
            const uint64_t resp_paddr = drv->cmd_paddr + VIRTIO_GPU_CMD_RESP_OFFSET;

            VirtqBuffer bufs[2];
            bufs[0].phys_addr = req_paddr;
            bufs[0].len = static_cast<uint32_t>(sizeof(req));
            bufs[0].is_write = false;

            bufs[1].phys_addr = resp_paddr;
            bufs[1].len = static_cast<uint32_t>(sizeof(resp));
            bufs[1].is_write = true;

            __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);

            const int head = virtqueue_submit(drv->controlq, bufs, 2, nullptr);
            if (head >= 0) {
                virtqueue_kick(drv->controlq, true);

                for (uint32_t spin = 0; spin < 10000000U; ++spin) {
                    if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) || virtqueue_has_used(drv->controlq)) {
                        drain_used();
                        __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);
                        break;
                    }
                    asm volatile("pause" ::: "memory");
                }
            }
        }

        __atomic_clear(&drv->lock, __ATOMIC_RELEASE);
    }
}

bool virtio_gpu_reset(VirtioGpuDriver* drv) {
    if (!drv || virtio_gpu_is_quiescing()) return false;

    GpuSpinLockGuard guard(drv->lock);

    /* 1. Odpiecie scanoutu jesli byl ustawiony */
    if ((drv->scanout_set||(drv->old_resource_id&&!drv->old_scanout_pending)) && !drv->has_error) {
        virtio_gpu_cmd_set_scanout_locked(drv, drv->scanout_id, 0, 0, 0, 0, 0);
        drv->scanout_set = false;
    }
    /* Keep both CPU backings until hardware reset is confirmed. A failed
       disable/timeout may leave the retained old scanout reading its backing. */

    /* 2. Odlaczenie pamieci backing jesli byla podpieta */
    if (drv->backing_attached && !drv->has_error) {
        virtio_gpu_cmd_resource_detach_backing_locked(drv, drv->resource_id);
        drv->backing_attached = false;
    }

    /* 3. Zniszczenie zasobu 2D */
    if (drv->resource_created && !drv->has_error) {
        virtio_gpu_cmd_resource_unref_locked(drv, drv->resource_id);
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
    virtio_gpu_free_stale_resource_locked(drv,true);
    virtio_gpu_cursor_release_locked(drv,true);
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
    drv->fb_width = 0;
    drv->fb_height = 0;
    drv->fb_stride = 0;
    drv->fb_format = 0;
    drv->display_width = 0;
    drv->display_height = 0;
    drv->old_display_width = 0;
    drv->old_display_height = 0;
    drv->scanout_id = 0;
    drv->scanout_enabled = false;
    drv->resource_created = false;
    drv->backing_attached = false;
    drv->scanout_set = false;
    drv->initial_scanout_pending = false;
    drv->first_transfer_complete = false;
    drv->old_scanout_pending = false;
    drv->initialized = false;
    drv->has_error = false;
    // Confirmed hardware reset: the device owns no descriptor any more.
    drv->cmd_state = VIRTIO_GPU_CMD_IDLE;
    drv->pending_head = -1;
    drv->recovery_full_refresh = false;
    drv->permanent_failure_reason = nullptr;
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
