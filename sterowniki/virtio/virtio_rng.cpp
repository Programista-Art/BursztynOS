/*
 * Bursztyn OS - Sterownik sprzetowego generatora liczb losowych VirtIO-RNG (VirtIO 1.x)
 *
 * Plik: virtio_rng.cpp
 */

#include "virtio_rng.h"
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

#define klog_impl host_klog

static bool rng_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void rng_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

static inline uint64_t rng_monotonic_ns(void) {
    return host_czas_monotoniczny_ns();
}

#else
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "grafika.h"
#include "pamiec.h"

#define klog_impl klog_serial

static bool rng_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    DmaBuffer dma{};
    if (!dma_allocate(size, alignment, &dma)) {
        return false;
    }
    *vaddr = dma.virtual_address;
    *paddr = dma.physical_address;
    *pages = dma.page_count;
    return true;
}

static void rng_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    DmaBuffer dma{};
    dma.virtual_address = vaddr;
    dma.physical_address = paddr;
    dma.page_count = pages;
    dma_release(&dma);
}

static inline uint64_t rng_monotonic_ns(void) {
    if (hpet_dostepny()) {
        return czas_monotoniczny_ns();
    }
    return 0;
}
#endif

/*
 * Prawdziwe, nieoptymalizowalne przez kompilator zerowanie pamieci z danymi wrazliwymi.
 * Zapis przez volatile uint8_t* gwarantuje, ze kompilator nie usunie zapisu jako dead store.
 * Bariera pamieci z referencja do bufora ptr uniemozliwia przestawienie operacji.
 */
void virtio_rng_secure_zero(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) {
        *p++ = 0;
    }
    asm volatile("" : : "r"(ptr) : "memory");
}

namespace {

static inline void rng_memcpy(void* dst, const volatile void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const volatile uint8_t* s = static_cast<const volatile uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

#ifndef BURSZTYN_VIRTIO_HOST_TEST
static void append_u64(char* buf, size_t cap, size_t& pos, uint64_t val) {
    char tmp[24];
    size_t tlen = 0;
    if (val == 0) {
        tmp[tlen++] = '0';
    } else {
        while (val > 0) {
            tmp[tlen++] = static_cast<char>('0' + (val % 10));
            val /= 10;
        }
    }
    for (size_t i = 0; i < tlen && pos + 1 < cap; ++i) {
        buf[pos++] = tmp[tlen - 1 - i];
    }
    buf[pos] = '\0';
}

static void append_str(char* buf, size_t cap, size_t& pos, const char* s) {
    if (!s) return;
    while (*s && pos + 1 < cap) {
        buf[pos++] = *s++;
    }
    buf[pos] = '\0';
}
#endif

#if defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT && !defined(BURSZTYN_VIRTIO_HOST_TEST)
static void rng_boot_trace(const char* phase, const VirtioPciDevice* dev = nullptr,
                           const VirtQueue* vq = nullptr) {
    char msg[192]{};
    size_t pos = 0;
    append_str(msg, sizeof(msg), pos, "[VIRTIO-RNG TRACE] ");
    append_str(msg, sizeof(msg), pos, phase ? phase : "?");
    if (dev) {
        append_str(msg, sizeof(msg), pos, " status=");
        append_u64(msg, sizeof(msg), pos, virtio_pci_get_status(dev));
    }
    if (vq) {
        append_str(msg, sizeof(msg), pos, " qsize=");
        append_u64(msg, sizeof(msg), pos, vq->queue_size);
        append_str(msg, sizeof(msg), pos, " avail=");
        append_u64(msg, sizeof(msg), pos, vq->avail ? vq->avail->idx : 0);
        append_str(msg, sizeof(msg), pos, " used=");
        append_u64(msg, sizeof(msg), pos, vq->used ? vq->used->idx : 0);
    }
    klog_impl(msg);
}
#else
static inline void rng_boot_trace(const char*, const VirtioPciDevice* = nullptr,
                                  const VirtQueue* = nullptr) {}
#endif

// Glowny stan sterownika
static VirtioRngDriver g_virtio_rng{};

// Blokada synchronizacji sterownika (ochrona przed wspolbieznym uzyciem bufora DMA)
static uint32_t g_rng_lock = 0;

struct RngLockGuard {
    RngLockGuard() {
        while (__atomic_exchange_n(&g_rng_lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
            asm volatile("pause" ::: "memory");
        }
    }

    ~RngLockGuard() {
        __atomic_store_n(&g_rng_lock, 0U, __ATOMIC_RELEASE);
    }

    RngLockGuard(const RngLockGuard&) = delete;
    RngLockGuard& operator=(const RngLockGuard&) = delete;
};

static bool virtio_rng_is_ready_locked(void) {
    if (!g_virtio_rng.initialized ||
        g_virtio_rng.has_error ||
        g_virtio_rng.vq == nullptr ||
        g_virtio_rng.dev == nullptr) {
        return false;
    }
    uint8_t dev_status = virtio_pci_get_status(g_virtio_rng.dev);
    if (dev_status == 0xFF ||
        (dev_status & (VIRTIO_STATUS_DEVICE_NEEDS_RESET | VIRTIO_STATUS_FAILED)) != 0 ||
        (dev_status & VIRTIO_STATUS_DRIVER_OK) == 0) {
        return false;
    }
    return true;
}

static void virtio_rng_isr(void* context) {
    if (!context) return;
    auto* drv = reinterpret_cast<VirtioRngDriver*>(context);
    __atomic_add_fetch(&drv->irq_count, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&drv->request_done, true, __ATOMIC_RELEASE);
}

/*
 * Wykonuje reset sprzetowy urzadzenia i czysci zasoby sterownika.
 * Funkcja wywolywana pod zalozona blokada g_rng_lock.
 * Zgodnie ze specyfikacja VirtIO 1.x: wpisuje 0 do device_status i oczekuje, az urzadzenie
 * potwierdzi status == 0. Jezeli reset przekroczy deadline, pamiec DMA i VirtQueue
 * NIE sa zwalniane (kwarantanna sprzetowa), aby uniknac korupcji pamieci przez hypervisor.
 */
static bool virtio_rng_reset_locked(void) {
    bool reset_confirmed = false;

    // Bezpieczenstwo teardown: maskujemy i wylaczamy MSI-X przed zwalnianiem kolejek lub DMA
    if (g_virtio_rng.use_msix) {
        if (g_virtio_rng.dev && g_virtio_rng.dev->has_msix) {
            virtio_pci_mask_msix_entry(g_virtio_rng.dev, 0);
            pci_msix_disable(g_virtio_rng.dev->bus, g_virtio_rng.dev->slot, g_virtio_rng.dev->func, &g_virtio_rng.dev->msix_cap);
            g_virtio_rng.dev->msix_enabled = false;
            virtio_pci_select_queue(g_virtio_rng.dev, 0);
            virtio_pci_set_queue_msix_vector(g_virtio_rng.dev, VIRTIO_MSI_NO_VECTOR);
        }
        if (g_virtio_rng.irq_vector >= 0) {
            idt_wyrejestruj_irq(static_cast<uint8_t>(g_virtio_rng.irq_vector));
            idt_zwolnij_wektor_irq(static_cast<uint8_t>(g_virtio_rng.irq_vector));
            g_virtio_rng.irq_vector = -1;
        }
        g_virtio_rng.use_msix = false;
    }

    if (g_virtio_rng.dev != nullptr) {
        // VirtIO 1.x sec 2.4.1: reset rozpoczyna sie od wpisania 0 do device_status
        virtio_pci_set_status(g_virtio_rng.dev, VIRTIO_STATUS_RESET);

        // Oczekiwanie z ograniczeniem monotonicznym (deadline 100 ms) oraz wtornym spin guardem
        const uint64_t start_ns = rng_monotonic_ns();
        const uint64_t deadline_ns = (start_ns > UINT64_MAX - 100000000ULL)
                                         ? UINT64_MAX
                                         : (start_ns + 100000000ULL);
        uint32_t spin_guard = 1000000U;

        while (spin_guard > 0) {
            spin_guard--;
            if (virtio_pci_get_status(g_virtio_rng.dev) == 0) {
                reset_confirmed = true;
                break;
            }
            const uint64_t now_ns = rng_monotonic_ns();
            if (now_ns != 0 && now_ns >= deadline_ns) {
                break;
            }
            asm volatile("pause" ::: "memory");
        }

        if (!reset_confirmed && virtio_pci_get_status(g_virtio_rng.dev) == 0) {
            reset_confirmed = true;
        }

        if (!reset_confirmed) {
            klog_impl("[VIRTIO-RNG] ERROR: device reset timed out (status != 0)");
            virtio_pci_set_status(g_virtio_rng.dev, VIRTIO_STATUS_FAILED);
            g_virtio_rng.has_error = true;
        }
    } else {
        reset_confirmed = true;
    }

    if (reset_confirmed) {
        // Bezpieczne zwolnienie struktur kolejki po potwierdzeniu zatrzymania urzadzenia
        if (g_virtio_rng.vq) {
            virtqueue_destroy(g_virtio_rng.vq);
            g_virtio_rng.vq = nullptr;
        }
        // Bezpieczne wyczyszczenie i zwolnienie bufora DMA
        if (g_virtio_rng.dma_virtual) {
            virtio_rng_secure_zero(g_virtio_rng.dma_virtual, g_virtio_rng.buffer_size);
            rng_dma_free(g_virtio_rng.dma_virtual, g_virtio_rng.dma_physical, g_virtio_rng.dma_pages);
            g_virtio_rng.dma_virtual = nullptr;
            g_virtio_rng.dma_physical = 0;
            g_virtio_rng.dma_pages = 0;
        }
        g_virtio_rng.dev = nullptr;
        g_virtio_rng.buffer_size = 0;
        g_virtio_rng.initialized = false;
        g_virtio_rng.has_error = false;
        g_virtio_rng.total_bytes_read = 0;
        g_virtio_rng.total_requests = 0;
        return true;
    } else {
        // KWARANTANNA DMA:
        // Jezeli urzadzenie nie zresetowalo sie do statusu 0, pamiec DMA NIE MOZE
        // zostac zwolniona do puli ogolnej pamieci, poniewaz kontroler moglby nadal
        // wykonywac niekontrolowany zapis.
        klog_impl("[VIRTIO-RNG] CRITICAL: DMA buffer quarantined due to unconfirmed device reset");
        g_virtio_rng.initialized = false;
        g_virtio_rng.has_error = true;
        return false;
    }
}

} // namespace

VirtioRngDriver* virtio_rng_get_driver(void) {
    return &g_virtio_rng;
}

bool virtio_rng_is_ready(void) {
    RngLockGuard guard;
    return virtio_rng_is_ready_locked();
}

bool virtio_rng_reset(void) {
    RngLockGuard guard;
    return virtio_rng_reset_locked();
}

bool virtio_rng_init(VirtioPciDevice* dev) {
    if (!dev || dev->virtio_id != VIRTIO_ID_RNG) {
        return false;
    }

    rng_boot_trace("probe enter", dev);
    RngLockGuard guard;

    // Pelny reset i audyt stanu urzadzenia przed konfiguracja
    rng_boot_trace("transport init enter", dev);
    if (!virtio_rng_reset_locked()) {
        klog_impl("[VIRTIO-RNG] ERROR: failed to reset device before init");
        return false;
    }

    uint8_t status = virtio_pci_get_status(dev);
    if ((status & (VIRTIO_STATUS_DRIVER_OK | VIRTIO_STATUS_FAILED | VIRTIO_STATUS_DEVICE_NEEDS_RESET)) != 0) {
        virtio_pci_set_status(dev, VIRTIO_STATUS_RESET);
        if (virtio_pci_get_status(dev) != 0) {
            klog_impl("[VIRTIO-RNG] ERROR: device reset failed before init");
            return false;
        }
        status = virtio_pci_get_status(dev);
    }

    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0) {
        virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);
        virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
        if (!virtio_pci_negotiate_features(dev, 0)) {
            klog_impl("[VIRTIO-RNG] ERROR: feature negotiation failed");
            return false;
        }
    }
    rng_boot_trace("transport init leave", dev);

    // Tworzenie kolejki Split VirtQueue 0
    rng_boot_trace("queue init enter", dev);
    VirtQueue* vq = virtqueue_create(dev, 0);
    if (!vq) {
        klog_impl("[VIRTIO-RNG] ERROR: virtqueue_create failed for queue 0");
        return false;
    }
    rng_boot_trace("queue init leave", dev, vq);

    // Alokacja bufora DMA dla transakcji entropii
    void* dma_v = nullptr;
    uint64_t dma_p = 0;
    size_t dma_pages = 0;
    if (!rng_dma_alloc(VIRTIO_RNG_BUFFER_SIZE, 64, &dma_v, &dma_p, &dma_pages)) {
        klog_impl("[VIRTIO-RNG] ERROR: DMA allocation failed for entropy buffer");
        virtqueue_destroy(vq);
        return false;
    }
    virtio_rng_secure_zero(dma_v, VIRTIO_RNG_BUFFER_SIZE);

    // Konfiguracja MSI-X jesli urzadzenie obsluguje MSI-X
    if (dev->has_msix) {
        int vec = idt_alokuj_wektor_irq();
        if (vec >= 0) {
            uint8_t lapic_id = apic_pobierz_lapic_id();
            if (idt_zarejestruj_irq(static_cast<uint8_t>(vec), virtio_rng_isr, &g_virtio_rng)) {
                if (virtio_pci_program_msix_entry(dev, 0, lapic_id, static_cast<uint8_t>(vec), false)) {
                    if (virtio_pci_set_queue_msix_vector_checked(dev, 0, 0)) {
                        if (pci_msix_enable(dev->bus, dev->slot, dev->func, &dev->msix_cap)) {
                            dev->msix_enabled = true;
                            g_virtio_rng.use_msix = true;
                            g_virtio_rng.irq_vector = vec;
                            klog_impl("[VIRTIO-RNG] MSI-X configured successfully for queue 0");
                        }
                    }
                }
            }
            if (!g_virtio_rng.use_msix) {
                idt_wyrejestruj_irq(static_cast<uint8_t>(vec));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(vec));
                g_virtio_rng.irq_vector = -1;
            }
        }
    }

    // VirtIO 1.0 sec 3.1.1 krok 8:
    // Ustawienie bitu DRIVER_OK dopiero po pelnym przygotowaniu kolejki i buforow sterownika
    status = virtio_pci_get_status(dev);
    virtio_pci_set_status(dev, static_cast<uint8_t>(status | VIRTIO_STATUS_DRIVER_OK));
    if ((virtio_pci_get_status(dev) & VIRTIO_STATUS_DRIVER_OK) == 0) {
        klog_impl("[VIRTIO-RNG] ERROR: failed to set DRIVER_OK");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        virtio_pci_set_status(dev, VIRTIO_STATUS_RESET);
        rng_dma_free(dma_v, dma_p, dma_pages);
        virtqueue_destroy(vq);
        return false;
    }

    g_virtio_rng.dev = dev;
    g_virtio_rng.vq = vq;
    g_virtio_rng.dma_virtual = dma_v;
    g_virtio_rng.dma_physical = dma_p;
    g_virtio_rng.dma_pages = dma_pages;
    g_virtio_rng.buffer_size = VIRTIO_RNG_BUFFER_SIZE;
    g_virtio_rng.initialized = true;
    g_virtio_rng.has_error = false;
    g_virtio_rng.total_bytes_read = 0;
    g_virtio_rng.total_requests = 0;

    rng_boot_trace("probe leave", dev, vq);
    klog_impl("[VIRTIO-RNG] Device initialized successfully (queue 0, DRIVER_OK)");
    return true;
}

bool virtio_rng_probe_and_init(void) {
    const uint32_t count = virtio_pci_liczba_urzadzen();
    for (uint32_t i = 0; i < count; ++i) {
        VirtioPciDevice* dev = virtio_pci_pobierz_urzadzenie(i);
        if (dev && dev->virtio_id == VIRTIO_ID_RNG) {
            if (virtio_rng_init(dev)) {
                return true;
            }
        }
    }
    return false;
}

size_t virtio_rng_read_bytes(void* dst, size_t max_len, uint64_t timeout_ms) {
    if (!dst || max_len == 0) return 0;

    // Serializacja wywolan - zapobiega jednoczesnemu uzyciu wspolnego 64-bajtowego bufora DMA
    RngLockGuard guard;

    if (!virtio_rng_is_ready_locked()) {
        virtio_rng_secure_zero(dst, max_len);
        return 0;
    }

    VirtQueue* vq = g_virtio_rng.vq;
    uint8_t* out = static_cast<uint8_t*>(dst);
    size_t total_read = 0;

    // Obliczenie deadline w czasie monotonicznym dla calego zadania
    const uint64_t timeout_ns = (timeout_ms > UINT64_MAX / 1000000ULL)
                                    ? (UINT64_MAX - 1ULL)
                                    : (timeout_ms * 1000000ULL);
    const uint64_t start_ns = rng_monotonic_ns();
    const uint64_t deadline_ns = (start_ns > UINT64_MAX - timeout_ns)
                                     ? UINT64_MAX
                                     : (start_ns + timeout_ns);

    while (total_read < max_len) {
        size_t chunk = max_len - total_read;
        if (chunk > g_virtio_rng.buffer_size) {
            chunk = g_virtio_rng.buffer_size;
        }

        virtio_rng_secure_zero(g_virtio_rng.dma_virtual, chunk);

        VirtqBuffer vbuf{};
        vbuf.phys_addr = g_virtio_rng.dma_physical;
        vbuf.len = static_cast<uint32_t>(chunk);
        vbuf.is_write = true; // bufor przeznaczony do zapisu przez urzadzenie

        if (g_virtio_rng.total_requests == 0) {
            rng_boot_trace("first request enter", g_virtio_rng.dev, vq);
        }
        rng_boot_trace("descriptor publication", g_virtio_rng.dev, vq);
        rng_boot_trace("avail idx before", g_virtio_rng.dev, vq);
        int head = virtqueue_submit(vq, &vbuf, 1, g_virtio_rng.dma_virtual);
        if (head < 0) {
            klog_impl("[VIRTIO-RNG] virtqueue_submit failed");
            g_virtio_rng.has_error = true;
            virtio_rng_reset_locked();
            virtio_rng_secure_zero(dst, max_len);
            return 0;
        }
        rng_boot_trace("avail idx after", g_virtio_rng.dev, vq);

        __atomic_store_n(&g_virtio_rng.request_done, false, __ATOMIC_RELEASE);
        virtqueue_kick(vq, true);
        rng_boot_trace("notify", g_virtio_rng.dev, vq);

        // Oczekiwanie z ograniczeniem czasu monotonicznego: obsluga przerwania MSI-X lub fallback polling
        uint32_t spin_guard = VIRTIO_RNG_MAX_SPINS_GUARD;
        rng_boot_trace("wait enter", g_virtio_rng.dev, vq);
        if (g_virtio_rng.use_msix) {
            while (!__atomic_load_n(&g_virtio_rng.request_done, __ATOMIC_ACQUIRE) && !virtqueue_has_used(vq)) {
                if (spin_guard > 0) {
                    spin_guard--;
                } else {
                    break;
                }
                const uint64_t now_ns = rng_monotonic_ns();
                if (now_ns != 0 && now_ns >= deadline_ns) {
                    break;
                }
                asm volatile("pause" ::: "memory");
            }
            if (!virtqueue_has_used(vq)) {
                uint32_t poll_guard = 100000U;
                while (!virtqueue_has_used(vq) && poll_guard > 0) {
                    poll_guard--;
                    asm volatile("pause" ::: "memory");
                }
            }
        } else {
            while (!virtqueue_has_used(vq)) {
                if (spin_guard > 0) {
                    spin_guard--;
                } else {
                    break;
                }
                const uint64_t now_ns = rng_monotonic_ns();
                if (now_ns != 0 && now_ns >= deadline_ns) {
                    break;
                }
                asm volatile("pause" ::: "memory");
            }
        }

        if (!virtqueue_has_used(vq)) {
            klog_impl("[VIRTIO-RNG] ERROR: timeout waiting for used ring completion");
            g_virtio_rng.has_error = true;
            virtio_rng_reset_locked();
            virtio_rng_secure_zero(dst, max_len);
            return 0;
        }
        rng_boot_trace("used idx observation", g_virtio_rng.dev, vq);

        void* cookie = nullptr;
        uint32_t bytes_written = 0;
        int pop_id = virtqueue_pop_used(vq, &cookie, &bytes_written);
        if (pop_id < 0) {
            klog_impl("[VIRTIO-RNG] ERROR: virtqueue_pop_used failed");
            g_virtio_rng.has_error = true;
            virtio_rng_reset_locked();
            virtio_rng_secure_zero(dst, max_len);
            return 0;
        }
        rng_boot_trace("completion", g_virtio_rng.dev, vq);

        if (pop_id != head) {
            klog_impl("[VIRTIO-RNG] ERROR: descriptor ID mismatch");
            g_virtio_rng.has_error = true;
            virtio_rng_reset_locked();
            virtio_rng_secure_zero(dst, max_len);
            return 0;
        }

        if (cookie != g_virtio_rng.dma_virtual) {
            klog_impl("[VIRTIO-RNG] ERROR: cookie mismatch");
            g_virtio_rng.has_error = true;
            virtio_rng_reset_locked();
            virtio_rng_secure_zero(dst, max_len);
            return 0;
        }

        // Punkt 1: Odrzucenie completion z used.len > submitted buffer length jako korupcji.
        // Nie clampujemy - natychmiastowy fail-closed i reset.
        if (bytes_written > chunk) {
            klog_impl("[VIRTIO-RNG] ERROR: device reported used.len > submitted buffer length (corruption)");
            g_virtio_rng.has_error = true;
            virtio_rng_reset_locked();
            virtio_rng_secure_zero(dst, max_len);
            return 0;
        }

        if (bytes_written == 0) {
            // Urzadzenie zwrocilo 0 bajtow - koniec pobierania
            break;
        }

        // Kopiowanie wylacznie prawidlowo zainicjalizowanych bajtow
        rng_memcpy(out + total_read, g_virtio_rng.dma_virtual, bytes_written);
        virtio_rng_secure_zero(g_virtio_rng.dma_virtual, bytes_written);

        total_read += bytes_written;

        // Saturacja licznikow diagnostycznych uint64_t
        if (g_virtio_rng.total_bytes_read <= UINT64_MAX - bytes_written) {
            g_virtio_rng.total_bytes_read += bytes_written;
        } else {
            g_virtio_rng.total_bytes_read = UINT64_MAX;
        }

        if (g_virtio_rng.total_requests < UINT64_MAX) {
            g_virtio_rng.total_requests++;
        }

        if (bytes_written < chunk) {
            // Czesciowe zakonczenie przez urzadzenie
            break;
        }
    }

    // Punkt 6: Czesciowy odczyt - zerowanie pozostalego niezainicjalizowanego bufora dst
    if (total_read < max_len) {
        virtio_rng_secure_zero(out + total_read, max_len - total_read);
    }

    return total_read;
}

size_t virtio_rng_read(void* dst, size_t max_len) {
    return virtio_rng_read_bytes(dst, max_len, VIRTIO_RNG_DEFAULT_TIMEOUT_MS);
}

bool virtio_rng_stress_test(VirtioPciDevice* dev) {
    if (!dev || dev->virtio_id != VIRTIO_ID_RNG) return false;

    klog_impl("[VIRTIO-RNG-STRESS] Starting stress test: >= 1000 transactions, 3 cycles, ring wraparound...");

    virtio_rng_reset();

#ifndef BURSZTYN_VIRTIO_HOST_TEST
    const uint64_t pmm_initial = pmm_pobierz_wolne_ramki();
#endif

    const uint32_t NUM_CYCLES = 3;
    const uint32_t TRANSACTIONS_PER_CYCLE = 1024;
    uint32_t total_success = 0;

    for (uint32_t cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        if (!virtio_rng_init(dev)) {
            klog_impl("[VIRTIO-RNG-STRESS] ERROR: virtio_rng_init failed during cycle");
            return false;
        }

        VirtioRngDriver* drv = virtio_rng_get_driver();
        if (!drv || !drv->initialized || !drv->vq) {
            klog_impl("[VIRTIO-RNG-STRESS] ERROR: driver state invalid after init");
            return false;
        }

        const uint16_t initial_num_free = virtqueue_get_num_free(drv->vq);
        uint8_t prev_buf[16]{};
        bool has_prev = false;

        for (uint32_t t = 0; t < TRANSACTIONS_PER_CYCLE; ++t) {
            uint8_t buf[16]{};
            size_t n = virtio_rng_read(buf, sizeof(buf));
            if (n != sizeof(buf)) {
                klog_impl("[VIRTIO-RNG-STRESS] ERROR: unexpected byte count or timeout");
                virtio_rng_secure_zero(buf, sizeof(buf));
                virtio_rng_secure_zero(prev_buf, sizeof(prev_buf));
                return false;
            }

            // Weryfikacja: bufor nie moze skladac sie z samych zer
            bool non_zero = false;
            for (size_t b = 0; b < sizeof(buf); ++b) {
                if (buf[b] != 0) {
                    non_zero = true;
                    break;
                }
            }
            if (!non_zero) {
                klog_impl("[VIRTIO-RNG-STRESS] ERROR: entropy returned all zeroes");
                virtio_rng_secure_zero(buf, sizeof(buf));
                virtio_rng_secure_zero(prev_buf, sizeof(prev_buf));
                return false;
            }

            // Weryfikacja braku identycznych kolejnych blokow losowych
            if (has_prev) {
                bool identical = true;
                for (size_t b = 0; b < sizeof(buf); ++b) {
                    if (buf[b] != prev_buf[b]) {
                        identical = false;
                        break;
                    }
                }
                if (identical) {
                    klog_impl("[VIRTIO-RNG-STRESS] ERROR: consecutive random blocks identical");
                    virtio_rng_secure_zero(buf, sizeof(buf));
                    virtio_rng_secure_zero(prev_buf, sizeof(prev_buf));
                    return false;
                }
            }
            for (size_t b = 0; b < sizeof(buf); ++b) {
                prev_buf[b] = buf[b];
            }
            has_prev = true;

            // Weryfikacja spojnosci kolejki (recykling deskryptora bez wycieku)
            if (virtqueue_get_num_free(drv->vq) != initial_num_free) {
                klog_impl("[VIRTIO-RNG-STRESS] ERROR: descriptor leak detected");
                virtio_rng_secure_zero(buf, sizeof(buf));
                virtio_rng_secure_zero(prev_buf, sizeof(prev_buf));
                return false;
            }

            total_success++;
        }

        virtio_rng_secure_zero(prev_buf, sizeof(prev_buf));
        virtio_rng_reset();
    }

#ifndef BURSZTYN_VIRTIO_HOST_TEST
    const uint64_t pmm_after = pmm_pobierz_wolne_ramki();
    if (pmm_after != pmm_initial) {
        klog_impl("[VIRTIO-RNG-STRESS] ERROR: PMM delta != 0 across cycles");
        return false;
    }
    {
        char msg[160]{};
        size_t pos = 0;
        append_str(msg, sizeof(msg), pos, "[VIRTIO-RNG-STRESS] PMM frames: before=");
        append_u64(msg, sizeof(msg), pos, pmm_initial);
        append_str(msg, sizeof(msg), pos, ", after=");
        append_u64(msg, sizeof(msg), pos, pmm_after);
        append_str(msg, sizeof(msg), pos, ", delta=0");
        klog_impl(msg);
    }
#endif

    // Przywrocenie sterownika do pelnej gotowosci operacyjnej dla jądra
    if (!virtio_rng_init(dev)) {
        klog_impl("[VIRTIO-RNG-STRESS] ERROR: final virtio_rng_init failed");
        return false;
    }

    klog_impl("[VIRTIO-RNG-STRESS] SUCCESS: 3072 transactions across 3 cycles completed. PMM delta=0, no queue corruption.");
    return true;
}
