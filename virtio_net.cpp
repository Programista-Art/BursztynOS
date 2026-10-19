#include "diagnostyka_pci.h"
/*
 * Bursztyn OS - Sterownik karty sieciowej VirtIO-NET (VirtIO 1.x Modern PCI)
 *
 * Plik: virtio_net.cpp
 *
 * Implementacja produkcyjna sterownika VirtIO-NET dla transportu Modern PCI
 * i Split VirtQueue, zintegrowana z NetworkDevice API (net_register_device).
 */

#include "virtio_net.h"
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

#define net_klog host_klog

static bool net_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void net_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

static inline uint64_t net_monotonic_ns(void) {
    return host_czas_monotoniczny_ns();
}

#else
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "grafika.h"
#include "pamiec.h"

#define net_klog klog_serial

static bool net_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    DmaBuffer dma{};
    if (!dma_allocate(size, alignment, &dma)) {
        return false;
    }
    *vaddr = dma.virtual_address;
    *paddr = dma.physical_address;
    *pages = dma.page_count;
    return true;
}

static void net_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    DmaBuffer dma{};
    dma.virtual_address = vaddr;
    dma.physical_address = paddr;
    dma.page_count = pages;
    dma_release(&dma);
}

static inline uint64_t net_monotonic_ns(void) {
    if (hpet_dostepny()) {
        return czas_monotoniczny_ns();
    }
    return 0;
}
#endif

/*
 * Prawdziwe, nieoptymalizowalne przez kompilator zerowanie pamieci z danymi wrazliwymi.
 */
static void net_secure_zero(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) {
        *p++ = 0;
    }
    asm volatile("" : : "r"(ptr) : "memory");
}

static inline void net_memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

namespace {

/*
 * Blokada spinlockowa.
 */
struct NetSpinLockGuard {
    volatile uint32_t* lock_ptr;

    explicit NetSpinLockGuard(uint32_t& lk) : lock_ptr(&lk) {
        while (__atomic_exchange_n(lock_ptr, 1U, __ATOMIC_ACQUIRE) != 0U) {
            asm volatile("pause" ::: "memory");
        }
    }

    ~NetSpinLockGuard() {
        __atomic_store_n(lock_ptr, 0U, __ATOMIC_RELEASE);
    }

    NetSpinLockGuard(const NetSpinLockGuard&) = delete;
    NetSpinLockGuard& operator=(const NetSpinLockGuard&) = delete;
};

/*
 * Blokada try-lock dla sciezki odbioru (RX).
 * Chroni przed zagniezdzeniem i zakleszczeniem, gdy parser pakietow
 * wywoluje wewnetrzne sprawdzanie kolejki sieciowej.
 */
struct NetTryLockGuard {
    volatile uint32_t* lock_ptr;
    bool acquired;

    explicit NetTryLockGuard(uint32_t& lk) : lock_ptr(&lk), acquired(false) {
        acquired = (__atomic_exchange_n(lock_ptr, 1U, __ATOMIC_ACQUIRE) == 0U);
    }

    ~NetTryLockGuard() {
        if (acquired) {
            __atomic_store_n(lock_ptr, 0U, __ATOMIC_RELEASE);
        }
    }

    bool is_locked() const { return acquired; }

    NetTryLockGuard(const NetTryLockGuard&) = delete;
    NetTryLockGuard& operator=(const NetTryLockGuard&) = delete;
};

/*
 * Globalna tablica instancji sterownikow VirtIO-NET
 */
static VirtioNetDriver g_net_drivers[VIRTIO_NET_MAX_DEVICES]{};
static uint32_t        g_net_driver_count = 0;

static void log_append_str(char* buf, size_t cap, size_t& pos, const char* s) {
    if (!s) return;
    while (*s && pos + 1 < cap) {
        buf[pos++] = *s++;
    }
    buf[pos] = '\0';
}

static void log_append_u64_dec(char* buf, size_t cap, size_t& pos, uint64_t val) {
    char tmp[24];
    size_t t = 0;
    if (val == 0) {
        tmp[t++] = '0';
    } else {
        while (val > 0) {
            tmp[t++] = static_cast<char>('0' + (val % 10));
            val /= 10;
        }
    }
    while (t > 0 && pos + 1 < cap) {
        buf[pos++] = tmp[--t];
    }
    buf[pos] = '\0';
}

/*
 * Minimalne procedury obslugi przerwan (ISR) - zapisuja flage i licznik telemetrii.
 * Przetwarzanie ramek odbywa sie wylacznie poza twardym kontekstem przerwan.
 */
static void virtio_net_rx_isr(void* context) {
    if (!context) return;
    auto* drv = reinterpret_cast<VirtioNetDriver*>(context);
    const uint64_t cnt = __atomic_add_fetch(&drv->rx_irq_count, 1ULL, __ATOMIC_RELAXED);
    __atomic_store_n(&drv->rx_interrupt_pending, true, __ATOMIC_RELEASE);
    if (cnt <= 5 || (cnt % 64) == 0) {
        char msg[64];
        size_t pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO-NET] RX MSI-X count=");
        log_append_u64_dec(msg, sizeof(msg), pos, cnt);
        net_klog(msg);
    }
}

static void virtio_net_tx_isr(void* context) {
    if (!context) return;
    auto* drv = reinterpret_cast<VirtioNetDriver*>(context);
    const uint64_t cnt = __atomic_add_fetch(&drv->tx_irq_count, 1ULL, __ATOMIC_RELAXED);
    __atomic_store_n(&drv->tx_interrupt_pending, true, __ATOMIC_RELEASE);
    if (cnt <= 5 || (cnt % 64) == 0) {
        char msg[64];
        size_t pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO-NET] TX MSI-X count=");
        log_append_u64_dec(msg, sizeof(msg), pos, cnt);
        net_klog(msg);
    }
}

static void virtio_net_combined_isr(void* context) {
    if (!context) return;
    auto* drv = reinterpret_cast<VirtioNetDriver*>(context);
    const uint64_t rxc = __atomic_add_fetch(&drv->rx_irq_count, 1ULL, __ATOMIC_RELAXED);
    const uint64_t txc = __atomic_add_fetch(&drv->tx_irq_count, 1ULL, __ATOMIC_RELAXED);
    __atomic_store_n(&drv->rx_interrupt_pending, true, __ATOMIC_RELEASE);
    __atomic_store_n(&drv->tx_interrupt_pending, true, __ATOMIC_RELEASE);
    if (rxc <= 5 || (rxc % 64) == 0) {
        char msg[64];
        size_t pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO-NET] Combined MSI-X rx_cnt=");
        log_append_u64_dec(msg, sizeof(msg), pos, rxc);
        log_append_str(msg, sizeof(msg), pos, " tx_cnt=");
        log_append_u64_dec(msg, sizeof(msg), pos, txc);
        net_klog(msg);
    }
}

/*
 * Zwalnia zakonczone deskryptory nadawcze (TX).
 * Wywolywana pod tx_lock.
 */
static void virtio_net_reclaim_tx_descriptors_locked(VirtioNetDriver* drv) {
    if (!drv || !drv->tx_vq) return;

    while (virtqueue_has_used(drv->tx_vq)) {
        void* cookie = nullptr;
        uint32_t used_len = 0;
        int desc_id = virtqueue_pop_used(drv->tx_vq, &cookie, &used_len);
        if (desc_id < 0) {
            break;
        }
        const uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cookie));
        if (slot < drv->tx_count) {
            drv->tx_slot_busy[slot] = false;
        }
    }
}

/*
 * Reset sprzetowy i czyszczenie zasobow urzadzenia.
 * W przypadku braku potwierdzenia resetu stosuje kwarantanne DMA.
 * Wywolywana pod reset_lock, rx_lock i tx_lock.
 */
static bool virtio_net_reset_locked(VirtioNetDriver* drv) {
    if (!drv) return false;
    bool reset_confirmed = false;

    // Krok 1: Wylaczenie przerwan MSI-X przed zwalnianiem kolejek lub DMA
    if (drv->use_msix) {
        if (drv->dev && drv->dev->has_msix) {
            virtio_pci_mask_msix_entry(drv->dev, 0);
            virtio_pci_mask_msix_entry(drv->dev, 1);
            pci_msix_disable(drv->dev->bus, drv->dev->slot, drv->dev->func, &drv->dev->msix_cap);
            drv->dev->msix_enabled = false;

            virtio_pci_select_queue(drv->dev, VIRTIO_NET_RX_QUEUE_INDEX);
            virtio_pci_set_queue_msix_vector(drv->dev, VIRTIO_MSI_NO_VECTOR);
            virtio_pci_select_queue(drv->dev, VIRTIO_NET_TX_QUEUE_INDEX);
            virtio_pci_set_queue_msix_vector(drv->dev, VIRTIO_MSI_NO_VECTOR);
        }

        if (drv->rx_irq_vector >= 0) {
            idt_wyrejestruj_irq(static_cast<uint8_t>(drv->rx_irq_vector));
            idt_zwolnij_wektor_irq(static_cast<uint8_t>(drv->rx_irq_vector));
            if (drv->tx_irq_vector == drv->rx_irq_vector) {
                drv->tx_irq_vector = -1;
            }
            drv->rx_irq_vector = -1;
        }
        if (drv->tx_irq_vector >= 0) {
            idt_wyrejestruj_irq(static_cast<uint8_t>(drv->tx_irq_vector));
            idt_zwolnij_wektor_irq(static_cast<uint8_t>(drv->tx_irq_vector));
            drv->tx_irq_vector = -1;
        }
        drv->use_msix = false;
    }

    // Krok 2: Wyrejestrowanie z rejestru NetworkDevice
    net_unregister_device(&drv->net_dev);

    // Krok 3: Reset sprzetowy kontrolera VirtIO
    if (drv->dev != nullptr) {
        virtio_pci_set_status(drv->dev, VIRTIO_STATUS_RESET);

        const uint64_t start_ns = net_monotonic_ns();
        const uint64_t deadline_ns = (start_ns > UINT64_MAX - 100000000ULL)
                                         ? UINT64_MAX
                                         : (start_ns + 100000000ULL);
        uint32_t spin_guard = 1000000U;

        while (spin_guard > 0) {
            spin_guard--;
            if (virtio_pci_get_status(drv->dev) == 0) {
                reset_confirmed = true;
                break;
            }
            const uint64_t now_ns = net_monotonic_ns();
            if (now_ns != 0 && now_ns >= deadline_ns) {
                break;
            }
            asm volatile("pause" ::: "memory");
        }

        if (!reset_confirmed && virtio_pci_get_status(drv->dev) == 0) {
            reset_confirmed = true;
        }

        if (!reset_confirmed) {
            net_klog("[VIRTIO-NET] ERROR: device reset timed out (status != 0)");
            virtio_pci_set_status(drv->dev, VIRTIO_STATUS_FAILED);
            drv->has_error = true;
        }
    } else {
        reset_confirmed = true;
    }

    // Krok 4: Zwolnienie lub kwarantanna pamieci DMA
    if (reset_confirmed) {
        if (drv->rx_vq) {
            virtqueue_destroy(drv->rx_vq);
            drv->rx_vq = nullptr;
        }
        if (drv->tx_vq) {
            virtqueue_destroy(drv->tx_vq);
            drv->tx_vq = nullptr;
        }
        if (drv->rx_dma_vaddr) {
            net_secure_zero(drv->rx_dma_vaddr, drv->rx_dma_size);
            net_dma_free(drv->rx_dma_vaddr, drv->rx_dma_paddr, drv->rx_dma_pages);
            drv->rx_dma_vaddr = nullptr;
            drv->rx_dma_paddr = 0;
            drv->rx_dma_pages = 0;
            drv->rx_dma_size = 0;
            drv->rx_count = 0;
        }
        if (drv->tx_dma_vaddr) {
            net_secure_zero(drv->tx_dma_vaddr, drv->tx_dma_size);
            net_dma_free(drv->tx_dma_vaddr, drv->tx_dma_paddr, drv->tx_dma_pages);
            drv->tx_dma_vaddr = nullptr;
            drv->tx_dma_paddr = 0;
            drv->tx_dma_pages = 0;
            drv->tx_dma_size = 0;
            drv->tx_count = 0;
        }
        for (uint32_t i = 0; i < VIRTIO_NET_MAX_TX_BUFFERS; ++i) {
            drv->tx_slot_busy[i] = false;
        }
        drv->dev = nullptr;
        drv->initialized = false;
        drv->has_error = false;
        drv->link_up = false;
        drv->net_dev = NetworkDevice{};
        return true;
    } else {
        // KWARANTANNA DMA: Brak potwierdzenia zatrzymania silnika urzadzenia.
        // Pamiec DMA nie moze wrocic do ogolnej puli.
        net_klog("[VIRTIO-NET] CRITICAL: DMA buffers quarantined due to unconfirmed device reset");
        drv->initialized = false;
        drv->has_error = true;
        drv->net_dev.active = false;
        return false;
    }
}

/*
 * Niskopoziomowa tabela operacji NetworkDevice dla VirtIO-NET
 */
static const NetDeviceOps g_virtio_net_ops = {
    virtio_net_transmit,
    virtio_net_poll_rx,
    virtio_net_get_link_state,
    virtio_net_get_stats
};

} // namespace

/* =========================================================================
 * IMPLEMENTACJA OPERACJI NETWORKDEVICE
 * ========================================================================= */

NetError virtio_net_transmit(NetworkDevice* dev, const void* frame, size_t length) {
    if (!dev || !dev->driver_data || !frame) {
        return NetError::InvalidArgument;
    }
    if (length < NET_MIN_ETH_FRAME) {
        return NetError::BufferTooSmall;
    }
    if (dev->mtu > 0 && length > static_cast<size_t>(dev->mtu) + NET_MIN_ETH_FRAME) {
        return NetError::BufferTooLarge;
    }

    auto* drv = static_cast<VirtioNetDriver*>(dev->driver_data);
    if (!drv->initialized || drv->has_error || !drv->dev || !drv->tx_vq) {
        return NetError::NotReady;
    }
    if (!drv->link_up) {
        return NetError::LinkDown;
    }

    NetSpinLockGuard guard(drv->tx_lock);

    if (!drv->initialized || drv->has_error || !drv->dev || !drv->tx_vq) {
        return NetError::NotReady;
    }

    // Odzyskaj juz zakonczone deskryptory TX
    virtio_net_reclaim_tx_descriptors_locked(drv);

    // Sprawdz czy kolejka ma wolne deskryptory
    if (virtqueue_get_num_free(drv->tx_vq) == 0) {
        __atomic_add_fetch(&dev->stats.tx_ring_full, 1ULL, __ATOMIC_RELAXED);
        __atomic_add_fetch(&dev->stats.tx_dropped, 1ULL, __ATOMIC_RELAXED);
        return NetError::QueueFull;
    }

    // Znajdz wolny slot w puli buforow TX
    uint32_t slot = drv->tx_count;
    for (uint32_t i = 0; i < drv->tx_count; ++i) {
        if (!drv->tx_slot_busy[i]) {
            slot = i;
            break;
        }
    }

    if (slot >= drv->tx_count) {
        __atomic_add_fetch(&dev->stats.tx_ring_full, 1ULL, __ATOMIC_RELAXED);
        __atomic_add_fetch(&dev->stats.tx_dropped, 1ULL, __ATOMIC_RELAXED);
        return NetError::QueueFull;
    }

    // Przygotuj bufor DMA z naglowkiem VirtioNetHdr (10 bajtow) + ramka Ethernet
    uint8_t* slot_vaddr = static_cast<uint8_t*>(drv->tx_dma_vaddr) + slot * VIRTIO_NET_BUFFER_SIZE;
    const uint64_t slot_paddr = drv->tx_dma_paddr + slot * VIRTIO_NET_BUFFER_SIZE;

    auto* hdr = reinterpret_cast<VirtioNetHdr*>(slot_vaddr);
    net_secure_zero(hdr, sizeof(VirtioNetHdr));

    uint8_t* payload = slot_vaddr + sizeof(VirtioNetHdr);
    net_memcpy(payload, frame, length);

    // Bariera DMA przed powiadomieniem urzadzenia
    asm volatile("mfence" ::: "memory");

    VirtqBuffer buf{};
    buf.phys_addr = slot_paddr;
    buf.len = static_cast<uint32_t>(sizeof(VirtioNetHdr) + length);
    buf.is_write = false;

    const int head = virtqueue_submit(drv->tx_vq, &buf, 1, reinterpret_cast<void*>(uintptr_t(slot)));
    if (head < 0) {
        __atomic_add_fetch(&dev->stats.tx_errors, 1ULL, __ATOMIC_RELAXED);
        __atomic_add_fetch(&dev->stats.tx_dropped, 1ULL, __ATOMIC_RELAXED);
        return NetError::IOError;
    }

    drv->tx_slot_busy[slot] = true;
    __atomic_store_n(&drv->tx_interrupt_pending, false, __ATOMIC_RELEASE);

    char tx_sub[96];
    size_t tspos = 0;
    log_append_str(tx_sub, sizeof(tx_sub), tspos, "[VIRTIO-NET] TX submit head=");
    log_append_u64_dec(tx_sub, sizeof(tx_sub), tspos, static_cast<uint64_t>(head));
    log_append_str(tx_sub, sizeof(tx_sub), tspos, " avail_idx=");
    log_append_u64_dec(tx_sub, sizeof(tx_sub), tspos, drv->tx_vq->avail->idx);
    log_append_str(tx_sub, sizeof(tx_sub), tspos, " used_idx=");
    log_append_u64_dec(tx_sub, sizeof(tx_sub), tspos, drv->tx_vq->used->idx);
    net_klog(tx_sub);

    // Powiadom kontroler (kick)
    virtqueue_kick(drv->tx_vq, false);

    // Oczekiwanie z limitem monotonicznym na zakonczenie transmisji tego slotu
    const uint64_t start_ns = net_monotonic_ns();
    const uint64_t deadline_ns = (start_ns > UINT64_MAX - 500000000ULL)
                                     ? UINT64_MAX
                                     : (start_ns + 500000000ULL); // 500 ms limit
    uint32_t spin_guard = 5000000U;
    bool completed = false;

    while (spin_guard > 0) {
        spin_guard--;
        virtio_net_reclaim_tx_descriptors_locked(drv);
        if (!drv->tx_slot_busy[slot]) {
            completed = true;
            break;
        }
        const uint64_t now_ns = net_monotonic_ns();
        if (now_ns != 0 && now_ns >= deadline_ns) {
            break;
        }
        asm volatile("pause" ::: "memory");
    }

    if (completed) {
        char tx_cmp[64];
        size_t tcpos = 0;
        log_append_str(tx_cmp, sizeof(tx_cmp), tcpos, "[VIRTIO-NET] TX completion head=");
        log_append_u64_dec(tx_cmp, sizeof(tx_cmp), tcpos, static_cast<uint64_t>(head));
        log_append_str(tx_cmp, sizeof(tx_cmp), tcpos, " used_idx=");
        log_append_u64_dec(tx_cmp, sizeof(tx_cmp), tcpos, drv->tx_vq->used->idx);
        net_klog(tx_cmp);

        __atomic_add_fetch(&dev->stats.tx_packets, 1ULL, __ATOMIC_RELAXED);
        __atomic_add_fetch(&dev->stats.tx_bytes, static_cast<uint64_t>(length), __ATOMIC_RELAXED);
        return NetError::Ok;
    } else {
        char to_msg[96];
        size_t topos = 0;
        log_append_str(to_msg, sizeof(to_msg), topos, "[VIRTIO-NET] TX timeout slot=");
        log_append_u64_dec(to_msg, sizeof(to_msg), topos, slot);
        log_append_str(to_msg, sizeof(to_msg), topos, " used_idx=");
        log_append_u64_dec(to_msg, sizeof(to_msg), topos, drv->tx_vq->used->idx);
        net_klog(to_msg);

        // Przekroczono limit czasu, ale slot pozostaje zalogowany jako zajety;
        // kolejne wywolania transmit/poll odzyskaja go po zakonczeniu bez wyciekow
        __atomic_add_fetch(&dev->stats.tx_timeouts, 1ULL, __ATOMIC_RELAXED);
        __atomic_add_fetch(&dev->stats.tx_errors, 1ULL, __ATOMIC_RELAXED);
        return NetError::Timeout;
    }
}

void virtio_net_poll_rx(NetworkDevice* dev) {
    if (!dev || !dev->driver_data) return;

    auto* drv = static_cast<VirtioNetDriver*>(dev->driver_data);
    if (!drv->initialized || drv->has_error || !drv->dev || !drv->rx_vq) {
        return;
    }

    // Try-lock: zapobiega blokowaniu i reentrancy gdy obsluga pakietu
    // wywoluje kod sieciowy wykonujacy polling
    NetTryLockGuard rx_guard(drv->rx_lock);
    if (!rx_guard.is_locked()) {
        return;
    }

    __atomic_store_n(&drv->rx_interrupt_pending, false, __ATOMIC_RELEASE);

    // Opcjonalny odzysk zakonczonych nadan (TX) pod osobnym lockiem
    if (__atomic_exchange_n(&drv->tx_lock, 1U, __ATOMIC_ACQUIRE) == 0U) {
        virtio_net_reclaim_tx_descriptors_locked(drv);
        __atomic_store_n(&drv->tx_lock, 0U, __ATOMIC_RELEASE);
    }

    uint32_t budget = 64U;
    uint32_t reposted = 0;

    while (budget > 0 && virtqueue_has_used(drv->rx_vq)) {
        budget--;
        void* cookie = nullptr;
        uint32_t used_len = 0;
        const int head = virtqueue_pop_used(drv->rx_vq, &cookie, &used_len);
        if (head < 0) {
            __atomic_add_fetch(&dev->stats.rx_errors, 1ULL, __ATOMIC_RELAXED);
            break;
        }

        const uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cookie));
        if (slot >= drv->rx_count) {
            __atomic_add_fetch(&dev->stats.rx_errors, 1ULL, __ATOMIC_RELAXED);
            break;
        }

        uint8_t* slot_vaddr = static_cast<uint8_t*>(drv->rx_dma_vaddr) + slot * VIRTIO_NET_BUFFER_SIZE;
        const uint64_t slot_paddr = drv->rx_dma_paddr + slot * VIRTIO_NET_BUFFER_SIZE;

        // Walidacja dlugosci: pakiet musi miec naglowek VirtioNetHdr + min. 14 B ramki
        if (used_len < sizeof(VirtioNetHdr) + NET_MIN_ETH_FRAME || used_len > VIRTIO_NET_BUFFER_SIZE) {
            __atomic_add_fetch(&dev->stats.rx_errors, 1ULL, __ATOMIC_RELAXED);
            __atomic_add_fetch(&dev->stats.rx_dropped, 1ULL, __ATOMIC_RELAXED);
            char rx_err[96];
            size_t rxepos = 0;
            log_append_str(rx_err, sizeof(rx_err), rxepos, "[VIRTIO-NET] RX malformed packet used_len=");
            log_append_u64_dec(rx_err, sizeof(rx_err), rxepos, used_len);
            log_append_str(rx_err, sizeof(rx_err), rxepos, " rx_errors=");
            log_append_u64_dec(rx_err, sizeof(rx_err), rxepos, dev->stats.rx_errors);
            log_append_str(rx_err, sizeof(rx_err), rxepos, " rx_dropped=");
            log_append_u64_dec(rx_err, sizeof(rx_err), rxepos, dev->stats.rx_dropped);
            net_klog(rx_err);
        } else {
            asm volatile("mfence" ::: "memory");
            const uint8_t* frame_ptr = slot_vaddr + sizeof(VirtioNetHdr);
            const size_t frame_len = used_len - sizeof(VirtioNetHdr);

            char rx_log[128];
            size_t rxpos = 0;
            log_append_str(rx_log, sizeof(rx_log), rxpos, "[VIRTIO-NET] RX packet: used.len=");
            log_append_u64_dec(rx_log, sizeof(rx_log), rxpos, used_len);
            log_append_str(rx_log, sizeof(rx_log), rxpos, " frame_len=");
            log_append_u64_dec(rx_log, sizeof(rx_log), rxpos, frame_len);
            log_append_str(rx_log, sizeof(rx_log), rxpos, " rx_avail_idx=");
            log_append_u64_dec(rx_log, sizeof(rx_log), rxpos, drv->rx_vq->avail->idx);
            log_append_str(rx_log, sizeof(rx_log), rxpos, " rx_used_idx=");
            log_append_u64_dec(rx_log, sizeof(rx_log), rxpos, drv->rx_vq->used->idx);
            net_klog(rx_log);

            // Przekazanie do generycznego odbioru NetworkDevice
            net_receive_frame(dev, frame_ptr, frame_len);
        }

        // Ponowne wstawienie bufora odbiorczego do kolejki RX (Queue 0)
        VirtqBuffer replenish_buf{};
        replenish_buf.phys_addr = slot_paddr;
        replenish_buf.len = VIRTIO_NET_BUFFER_SIZE;
        replenish_buf.is_write = true;

        if (virtqueue_submit(drv->rx_vq, &replenish_buf, 1, cookie) >= 0) {
            reposted++;
        } else {
            __atomic_add_fetch(&dev->stats.rx_errors, 1ULL, __ATOMIC_RELAXED);
        }
    }

    if (reposted > 0) {
        virtqueue_kick(drv->rx_vq, false);
    }
}

NetLinkState virtio_net_get_link_state(const NetworkDevice* dev) {
    if (!dev || !dev->driver_data) {
        return NetLinkState::Unknown;
    }
    auto* drv = static_cast<VirtioNetDriver*>(dev->driver_data);
    if (!drv->initialized || drv->has_error || !drv->dev) {
        return NetLinkState::Down;
    }

    if ((drv->dev->driver_features & VIRTIO_NET_F_STATUS) != 0) {
        uint16_t raw_status = 0;
        if (virtio_pci_read_device_config_consistent(drv->dev, 6, &raw_status, sizeof(raw_status))) {
            drv->link_up = (raw_status & VIRTIO_NET_S_LINK_UP) != 0;
        }
    }

    return drv->link_up ? NetLinkState::Up : NetLinkState::Down;
}

void virtio_net_get_stats(const NetworkDevice* dev, NetDeviceStats* out_stats) {
    if (!dev || !out_stats) return;
    *out_stats = dev->stats;
}

/* =========================================================================
 * PUBLICZNE API STEROWNIKA VIRTIO-NET
 * ========================================================================= */

bool virtio_net_init(VirtioPciDevice* dev) {
    if (!dev || dev->virtio_id != VIRTIO_ID_NET) {
        return false;
    }

    DiagPciAttempt diagnostic(DIAG_VIRTIO_NET,"Kontroler virtio-net","virtio-net",
        dev->bus,dev->slot,dev->func,dev->pci_vendor_id,dev->pci_device_id);

    // Znajdz istniejacy slot lub przydziel nowy
    VirtioNetDriver* drv = nullptr;
    uint32_t slot_idx = 0;

    for (uint32_t i = 0; i < g_net_driver_count; ++i) {
        if (g_net_drivers[i].dev == dev) {
            drv = &g_net_drivers[i];
            slot_idx = i;
            break;
        }
    }
    if (!drv) {
        for (uint32_t i = 0; i < g_net_driver_count; ++i) {
            if (!g_net_drivers[i].initialized) {
                drv = &g_net_drivers[i];
                slot_idx = i;
                break;
            }
        }
    }
    if (!drv) {
        if (g_net_driver_count >= VIRTIO_NET_MAX_DEVICES) {
            net_klog("[VIRTIO-NET] Osiagnieto limit urzadzen VirtIO-NET");
            return false;
        }
        slot_idx = g_net_driver_count++;
        drv = &g_net_drivers[slot_idx];
    }

    NetSpinLockGuard reset_guard(drv->reset_lock);
    NetSpinLockGuard rx_guard(drv->rx_lock);
    NetSpinLockGuard tx_guard(drv->tx_lock);

    // Krok 1: Wymuszenie czystego stanu urzadzenia (VirtIO 1.x sec 3.1.1)
    uint8_t cur_status = virtio_pci_get_status(dev);
    if ((cur_status & (VIRTIO_STATUS_DRIVER_OK | VIRTIO_STATUS_FAILED | VIRTIO_STATUS_DEVICE_NEEDS_RESET)) != 0 ||
        (cur_status & VIRTIO_STATUS_FEATURES_OK) != 0) {
        virtio_pci_set_status(dev, VIRTIO_STATUS_RESET);
        if (!virtio_pci_reset(dev)) {
            net_klog("[VIRTIO-NET] Device reset failed before init");
            return false;
        }
        cur_status = 0;
    }

    // Negocjacja cech: wymagamy VIRTIO_F_VERSION_1, negocjujemy MAC i STATUS
    const uint64_t requested_features = VIRTIO_F_VERSION_1 |
                                        VIRTIO_NET_F_MAC |
                                        VIRTIO_NET_F_STATUS;

    if ((cur_status & VIRTIO_STATUS_FEATURES_OK) == 0) {
        virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);
        virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
        if (!virtio_pci_negotiate_features(dev, requested_features)) {
            net_klog("[VIRTIO-NET] Feature negotiation failed or device rejected VIRTIO_F_VERSION_1");
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
    }

    const uint64_t neg_features = dev->driver_features;
    const bool has_mac_feat = (neg_features & VIRTIO_NET_F_MAC) != 0;
    const bool has_status_feat = (neg_features & VIRTIO_NET_F_STATUS) != 0;

    // Krok 2: Odczyt adresu MAC
    uint8_t mac_addr[6] = {0};
    bool mac_read_ok = false;

    if (has_mac_feat && dev->has_device_cfg) {
        if (virtio_pci_read_device_config_consistent(dev, 0, mac_addr, 6)) {
            if (net_mac_valid(mac_addr)) {
                mac_read_ok = true;
            }
        }
    }

    if (!mac_read_ok) {
        // Fallback: przypisanie adresu MAC ze strefy prywatnej (locally administered)
        mac_addr[0] = 0x52;
        mac_addr[1] = 0x54;
        mac_addr[2] = 0x00;
        mac_addr[3] = 0x12;
        mac_addr[4] = 0x34;
        mac_addr[5] = static_cast<uint8_t>(0x56 + slot_idx);
    }

    // Stan lacza
    bool link_is_up = true;
    if (has_status_feat && dev->has_device_cfg) {
        uint16_t raw_st = 0;
        if (virtio_pci_read_device_config_consistent(dev, 6, &raw_st, sizeof(raw_st))) {
            link_is_up = (raw_st & VIRTIO_NET_S_LINK_UP) != 0;
        }
    }

    // Krok 3: Utworzenie kolejek Split VirtQueue:
    // Queue 0 = RX, Queue 1 = TX
    if (drv->rx_vq) {
        virtqueue_destroy(drv->rx_vq);
        drv->rx_vq = nullptr;
    }
    if (drv->tx_vq) {
        virtqueue_destroy(drv->tx_vq);
        drv->tx_vq = nullptr;
    }

    drv->rx_vq = virtqueue_create(dev, VIRTIO_NET_RX_QUEUE_INDEX);
    if (!drv->rx_vq) {
        net_klog("[VIRTIO-NET] virtqueue_create failed for RX queue 0");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    drv->tx_vq = virtqueue_create(dev, VIRTIO_NET_TX_QUEUE_INDEX);
    if (!drv->tx_vq) {
        net_klog("[VIRTIO-NET] virtqueue_create failed for TX queue 1");
        virtqueue_destroy(drv->rx_vq);
        drv->rx_vq = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    // Obliczenie liczby buforow
    const uint16_t rx_qsize = virtqueue_get_size(drv->rx_vq);
    const uint16_t tx_qsize = virtqueue_get_size(drv->tx_vq);
    const uint32_t rx_count = (rx_qsize < VIRTIO_NET_MAX_RX_BUFFERS) ? rx_qsize : VIRTIO_NET_MAX_RX_BUFFERS;
    const uint32_t tx_count = (tx_qsize < VIRTIO_NET_MAX_TX_BUFFERS) ? tx_qsize : VIRTIO_NET_MAX_TX_BUFFERS;

    // Krok 4: Alokacja pamieci DMA dla RX i TX
    const size_t rx_dma_size = rx_count * VIRTIO_NET_BUFFER_SIZE;
    if (!drv->rx_dma_vaddr) {
        if (!net_dma_alloc(rx_dma_size, 4096, &drv->rx_dma_vaddr, &drv->rx_dma_paddr, &drv->rx_dma_pages)) {
            net_klog("[VIRTIO-NET] DMA alloc failed for RX buffers");
            virtqueue_destroy(drv->tx_vq);
            virtqueue_destroy(drv->rx_vq);
            drv->tx_vq = nullptr;
            drv->rx_vq = nullptr;
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
        drv->rx_dma_size = rx_dma_size;
        drv->rx_count = rx_count;
    }
    net_secure_zero(drv->rx_dma_vaddr, drv->rx_dma_size);

    const size_t tx_dma_size = tx_count * VIRTIO_NET_BUFFER_SIZE;
    if (!drv->tx_dma_vaddr) {
        if (!net_dma_alloc(tx_dma_size, 4096, &drv->tx_dma_vaddr, &drv->tx_dma_paddr, &drv->tx_dma_pages)) {
            net_klog("[VIRTIO-NET] DMA alloc failed for TX buffers");
            net_dma_free(drv->rx_dma_vaddr, drv->rx_dma_paddr, drv->rx_dma_pages);
            drv->rx_dma_vaddr = nullptr;
            virtqueue_destroy(drv->tx_vq);
            virtqueue_destroy(drv->rx_vq);
            drv->tx_vq = nullptr;
            drv->rx_vq = nullptr;
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
        drv->tx_dma_size = tx_dma_size;
        drv->tx_count = tx_count;
    }
    net_secure_zero(drv->tx_dma_vaddr, drv->tx_dma_size);
    for (uint32_t i = 0; i < VIRTIO_NET_MAX_TX_BUFFERS; ++i) {
        drv->tx_slot_busy[i] = false;
    }

    // Krok 5: Konfiguracja MSI-X (z fallbackiem do pollingu)
    drv->use_msix = false;
    drv->rx_irq_vector = -1;
    drv->tx_irq_vector = -1;

    if (dev->has_msix && dev->msix_cap.table_size >= 2) {
        const int vec_rx = idt_alokuj_wektor_irq();
        const int vec_tx = idt_alokuj_wektor_irq();
        if (vec_rx >= 0 && vec_tx >= 0) {
            const uint8_t lapic = apic_pobierz_lapic_id();
            if (idt_zarejestruj_irq(static_cast<uint8_t>(vec_rx), virtio_net_rx_isr, drv) &&
                idt_zarejestruj_irq(static_cast<uint8_t>(vec_tx), virtio_net_tx_isr, drv)) {
                if (virtio_pci_program_msix_entry(dev, 0, lapic, static_cast<uint8_t>(vec_rx), false) &&
                    virtio_pci_program_msix_entry(dev, 1, lapic, static_cast<uint8_t>(vec_tx), false)) {
                    if (virtio_pci_set_queue_msix_vector_checked(dev, VIRTIO_NET_RX_QUEUE_INDEX, 0) &&
                        virtio_pci_set_queue_msix_vector_checked(dev, VIRTIO_NET_TX_QUEUE_INDEX, 1)) {
                        if (pci_msix_enable(dev->bus, dev->slot, dev->func, &dev->msix_cap)) {
                            dev->msix_enabled = true;
                            drv->use_msix = true;
                            drv->rx_irq_vector = vec_rx;
                            drv->tx_irq_vector = vec_tx;
                            net_klog("[VIRTIO-NET] MSI-X configured successfully (RX=0, TX=1)");
                        }
                    }
                }
            }
        }
        if (!drv->use_msix) {
            if (vec_rx >= 0) {
                idt_wyrejestruj_irq(static_cast<uint8_t>(vec_rx));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(vec_rx));
            }
            if (vec_tx >= 0) {
                idt_wyrejestruj_irq(static_cast<uint8_t>(vec_tx));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(vec_tx));
            }
        }
    } else if (dev->has_msix && dev->msix_cap.table_size == 1) {
        const int vec = idt_alokuj_wektor_irq();
        if (vec >= 0) {
            const uint8_t lapic = apic_pobierz_lapic_id();
            if (idt_zarejestruj_irq(static_cast<uint8_t>(vec), virtio_net_combined_isr, drv)) {
                if (virtio_pci_program_msix_entry(dev, 0, lapic, static_cast<uint8_t>(vec), false)) {
                    if (virtio_pci_set_queue_msix_vector_checked(dev, VIRTIO_NET_RX_QUEUE_INDEX, 0) &&
                        virtio_pci_set_queue_msix_vector_checked(dev, VIRTIO_NET_TX_QUEUE_INDEX, 0)) {
                        if (pci_msix_enable(dev->bus, dev->slot, dev->func, &dev->msix_cap)) {
                            dev->msix_enabled = true;
                            drv->use_msix = true;
                            drv->rx_irq_vector = vec;
                            drv->tx_irq_vector = vec;
                            net_klog("[VIRTIO-NET] MSI-X configured successfully (shared vector 0)");
                        }
                    }
                }
            }
            if (!drv->use_msix) {
                idt_wyrejestruj_irq(static_cast<uint8_t>(vec));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(vec));
            }
        }
    }

    // Krok 6: Pre-postowanie buforow odbiorczych (Queue 0)
    for (uint32_t i = 0; i < rx_count; ++i) {
        const uint64_t slot_paddr = drv->rx_dma_paddr + i * VIRTIO_NET_BUFFER_SIZE;
        VirtqBuffer buf{};
        buf.phys_addr = slot_paddr;
        buf.len = VIRTIO_NET_BUFFER_SIZE;
        buf.is_write = true;

        if (virtqueue_submit(drv->rx_vq, &buf, 1, reinterpret_cast<void*>(uintptr_t(i))) < 0) {
            net_klog("[VIRTIO-NET] Failed to pre-post RX buffer");
            virtio_net_reset_locked(drv);
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
    }
    virtqueue_kick(drv->rx_vq, true);

    char post_msg[96];
    size_t ppos = 0;
    log_append_str(post_msg, sizeof(post_msg), ppos, "[VIRTIO-NET] RX buffers posted: count=");
    log_append_u64_dec(post_msg, sizeof(post_msg), ppos, rx_count);
    log_append_str(post_msg, sizeof(post_msg), ppos, " avail_idx=");
    log_append_u64_dec(post_msg, sizeof(post_msg), ppos, drv->rx_vq && drv->rx_vq->avail ? drv->rx_vq->avail->idx : 0);
    log_append_str(post_msg, sizeof(post_msg), ppos, " used_idx=");
    log_append_u64_dec(post_msg, sizeof(post_msg), ppos, drv->rx_vq && drv->rx_vq->used ? drv->rx_vq->used->idx : 0);
    net_klog(post_msg);

    // Krok 7: Ustawienie DRIVER_OK wylacznie po pelnej gotowosci struktur i kolejek
    virtio_pci_set_status(dev, virtio_pci_get_status(dev) | VIRTIO_STATUS_DRIVER_OK);
    if ((virtio_pci_get_status(dev) & VIRTIO_STATUS_DRIVER_OK) == 0) {
        net_klog("[VIRTIO-NET] Device rejected DRIVER_OK");
        virtio_net_reset_locked(drv);
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    // Krok 8: Inicjalizacja struktury sterownika i integracja z NetworkDevice API
    drv->dev = dev;
    drv->link_up = link_is_up;
    drv->has_error = false;
    drv->initialized = true;
    drv->rx_irq_count = 0;
    drv->tx_irq_count = 0;
    drv->rx_interrupt_pending = false;
    drv->tx_interrupt_pending = false;

    drv->net_dev = NetworkDevice{};
    drv->net_dev.driver_data = drv;
    drv->net_dev.ops = &g_virtio_net_ops;
    drv->net_dev.device_type = NetDeviceType::VirtIO;
    drv->net_dev.mtu = NET_DEFAULT_MTU;
    drv->net_dev.active = true;
    net_memcpy(drv->net_dev.mac, mac_addr, 6);

    const char* prefix = "virtio-net";
    size_t p = 0;
    while (prefix[p] && p < sizeof(drv->net_dev.name) - 3) {
        drv->net_dev.name[p] = prefix[p];
        p++;
    }
    drv->net_dev.name[p++] = static_cast<char>('0' + (slot_idx % 10));
    drv->net_dev.name[p] = '\0';

    if (!net_register_device(&drv->net_dev)) {
        net_klog("[VIRTIO-NET] net_register_device failed");
        virtio_net_reset_locked(drv);
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    char mac_str[64] = "[VIRTIO-NET] MAC: 00:00:00:00:00:00";
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; ++i) {
        mac_str[18 + i * 3]     = hex_chars[(mac_addr[i] >> 4) & 0x0F];
        mac_str[18 + i * 3 + 1] = hex_chars[mac_addr[i] & 0x0F];
    }
    net_klog(mac_str);
    net_klog("[VIRTIO-NET] Karta gotowa; link aktywny.");

    char reg_msg[64] = "[VIRTIO-NET] Zarejestrowano jako NetworkDevice (";
    size_t rp = 48;
    for (size_t i = 0; drv->net_dev.name[i] && rp < sizeof(reg_msg) - 3; ++i) {
        reg_msg[rp++] = drv->net_dev.name[i];
    }
    reg_msg[rp++] = ')';
    reg_msg[rp++] = '.';
    reg_msg[rp] = '\0';
    net_klog(reg_msg);

    diagnostic.ready();
    return true;
}

uint32_t virtio_net_probe_and_init(void) {
    const uint32_t total_pci = virtio_pci_liczba_urzadzen();
    uint32_t registered = 0;

    for (uint32_t i = 0; i < total_pci; ++i) {
        VirtioPciDevice* dev = virtio_pci_pobierz_urzadzenie(i);
        if (dev && dev->virtio_id == VIRTIO_ID_NET) {
            if (virtio_net_init(dev)) {
                registered++;
            }
        }
    }
    return registered;
}

bool virtio_net_reset(VirtioNetDriver* drv) {
    if (!drv) return false;
    NetSpinLockGuard reset_guard(drv->reset_lock);
    NetSpinLockGuard rx_guard(drv->rx_lock);
    NetSpinLockGuard tx_guard(drv->tx_lock);
    return virtio_net_reset_locked(drv);
}

void virtio_net_reset_all(void) {
    for (uint32_t i = 0; i < g_net_driver_count; ++i) {
        if (g_net_drivers[i].initialized || g_net_drivers[i].dev != nullptr) {
            virtio_net_reset(&g_net_drivers[i]);
        }
    }
    g_net_driver_count = 0;
}

VirtioNetDriver* virtio_net_get_driver(uint32_t index) {
    if (index >= g_net_driver_count) return nullptr;
    return &g_net_drivers[index];
}

uint32_t virtio_net_get_count(void) {
    return g_net_driver_count;
}

bool virtio_net_stress_test(VirtioNetDriver* drv) {
    if (!drv || !drv->initialized || drv->has_error || !drv->dev) {
        return false;
    }

    if (!net_mac_valid(drv->net_dev.mac)) {
        return false;
    }

    if (drv->net_dev.mtu < 68 || drv->net_dev.mtu > 1500) {
        return false;
    }

    NetDeviceStats st_before{};
    drv->net_dev.get_stats(&st_before);

    // Przygotowanie testowej ramki Ethernet II (ARP probe)
    uint8_t test_frame[60]{};
    // Dest: broadcast FF:FF:FF:FF:FF:FF
    for (int i = 0; i < 6; ++i) test_frame[i] = 0xFF;
    // Source: MAC karty
    net_memcpy(&test_frame[6], drv->net_dev.mac, 6);
    // EtherType: 0x0806 (ARP)
    test_frame[12] = 0x08;
    test_frame[13] = 0x06;
    // ARP hardware type: Ethernet (1)
    test_frame[14] = 0x00;
    test_frame[15] = 0x01;
    // ARP proto type: IPv4 (0x0800)
    test_frame[16] = 0x08;
    test_frame[17] = 0x00;
    // Hardware size: 6, proto size: 4
    test_frame[18] = 0x06;
    test_frame[19] = 0x04;
    // Opcode: 1 (Request)
    test_frame[20] = 0x00;
    test_frame[21] = 0x01;
    // Sender MAC
    net_memcpy(&test_frame[22], drv->net_dev.mac, 6);

    // Wyslij ramke testowa
    const NetError err = drv->net_dev.transmit(test_frame, sizeof(test_frame));
    if (!net_sukces(err)) {
        return false;
    }

    NetDeviceStats st_after{};
    drv->net_dev.get_stats(&st_after);

    if (st_after.tx_packets <= st_before.tx_packets) {
        return false;
    }
    if (st_after.tx_bytes < st_before.tx_bytes + sizeof(test_frame)) {
        return false;
    }

    return true;
}
