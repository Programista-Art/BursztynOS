#include "diagnostyka_pci.h"
/*
 * Bursztyn OS - Sterownik dysku blokowego VirtIO-BLK (VirtIO 1.x Modern PCI)
 *
 * Plik: virtio_blk.cpp
 *
 * Pelna implementacja produkcyjna sterownika blokowego VirtIO-BLK zgodna ze
 * standardem VirtIO 1.x Modern PCI, uzywajaca istniejacej warstwy Split VirtQueue
 * oraz integrujaca sie z podsystemem BlockDevice API (blokowy_zarejestruj_dysk).
 */

#include "virtio_blk.h"
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

#define blk_klog host_klog

static bool blk_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void blk_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

static inline uint64_t blk_monotonic_ns(void) {
    return host_czas_monotoniczny_ns();
}

#else
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "grafika.h"
#include "pamiec.h"
#include "blokowy.h"
#include "storage_format.h"
#include "instalator.h"

#define blk_klog klog_serial

static bool blk_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    DmaBuffer dma{};
    if (!dma_allocate(size, alignment, &dma)) {
        return false;
    }
    *vaddr = dma.virtual_address;
    *paddr = dma.physical_address;
    *pages = dma.page_count;
    return true;
}

static void blk_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    DmaBuffer dma{};
    dma.virtual_address = vaddr;
    dma.physical_address = paddr;
    dma.page_count = pages;
    dma_release(&dma);
}

static inline uint64_t blk_monotonic_ns(void) {
    if (hpet_dostepny()) {
        return czas_monotoniczny_ns();
    }
    return 0;
}
#endif

/*
 * Prawdziwe, nieoptymalizowalne przez kompilator zerowanie pamieci z danymi wrazliwymi.
 */
static void blk_secure_zero(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) {
        *p++ = 0;
    }
    asm volatile("" : : "r"(ptr) : "memory");
}

static inline void blk_memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

static inline int blk_memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = static_cast<const uint8_t*>(s1);
    const uint8_t* p2 = static_cast<const uint8_t*>(s2);
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) {
            return (p1[i] < p2[i]) ? -1 : 1;
        }
    }
    return 0;
}

namespace {

/*
 * Struktura bufora kontrolnego DMA zawierajaca naglowek zadania (16 B) i bajt statusu (1 B).
 * Wyrownana i zapakowana do 64 bajtow dla zachowania bezpiecznego odstepu DMA.
 */
#pragma pack(push, 1)
struct VirtioBlkControl {
    VirtIOBlkReqHeader header;  // offset 0, 16 bajtow
    volatile uint8_t   status;  // offset 16, 1 bajt (zapisywany przez urzadzenie)
    uint8_t            pad[47]; // dopelnienie do 64 bajtow
};
#pragma pack(pop)

static_assert(sizeof(VirtioBlkControl) == 64, "VirtioBlkControl rozmiar != 64 B");

/*
 * Blokada synchronizacyjna per instancja urzadzenia.
 * Przerwania sprzetowe pozostaja wlaczone, by timer monotoniczny (HPET) mogl inkrementowac czas.
 */
struct BlkLockGuard {
    volatile uint32_t* lock_ptr;

    explicit BlkLockGuard(uint32_t& lk) : lock_ptr(&lk) {
        while (__atomic_exchange_n(lock_ptr, 1U, __ATOMIC_ACQUIRE) != 0U) {
            asm volatile("pause" ::: "memory");
        }
    }

    ~BlkLockGuard() {
        __atomic_store_n(lock_ptr, 0U, __ATOMIC_RELEASE);
    }

    BlkLockGuard(const BlkLockGuard&) = delete;
    BlkLockGuard& operator=(const BlkLockGuard&) = delete;
};

// Globalne instancje sterownika dla wykrytych dyskow VirtIO-BLK
static VirtioBlkDriver g_blk_drivers[VIRTIO_BLK_MAX_DEVICES]{};
static bool            g_blk_registered[VIRTIO_BLK_MAX_DEVICES]{};
static uint32_t        g_blk_driver_count = 0;

enum class BlkReqOp {
    Read,
    Write,
    Flush
};

static void virtio_blk_isr(void* context) {
    if (!context) return;
    auto* drv = reinterpret_cast<VirtioBlkDriver*>(context);
    __atomic_add_fetch(&drv->irq_count, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&drv->request_done, true, __ATOMIC_RELEASE);
}

/*
 * Wykonuje reset sprzetowy urzadzenia i zwalnia zasoby (kolejke VirtQueue, bufory DMA).
 * W razie timeoutu resetu stosuje KWARANTANNE DMA (nie zwalnia pamieci, aby hypervisor nie uszkodzil pamieci).
 * Funkcja wywolywana w sekcji krytycznej (pod lockiem sterownika).
 */
static bool virtio_blk_reset_locked(VirtioBlkDriver* drv) {
    if (!drv) return false;
    bool reset_confirmed = false;

    // Bezpieczenstwo teardown: maskujemy i wylaczamy MSI-X przed zwalnianiem kolejek lub DMA
    if (drv->use_msix) {
        if (drv->dev && drv->dev->has_msix) {
            virtio_pci_mask_msix_entry(drv->dev, 0);
            pci_msix_disable(drv->dev->bus, drv->dev->slot, drv->dev->func, &drv->dev->msix_cap);
            drv->dev->msix_enabled = false;
            virtio_pci_select_queue(drv->dev, 0);
            virtio_pci_set_queue_msix_vector(drv->dev, VIRTIO_MSI_NO_VECTOR);
        }
        if (drv->irq_vector >= 0) {
            idt_wyrejestruj_irq(static_cast<uint8_t>(drv->irq_vector));
            idt_zwolnij_wektor_irq(static_cast<uint8_t>(drv->irq_vector));
            drv->irq_vector = -1;
        }
        drv->use_msix = false;
    }

    if (drv->dev != nullptr) {
        // VirtIO 1.x sec 2.4.1: reset urzadzenia przez wpisanie 0 do device_status
        virtio_pci_set_status(drv->dev, VIRTIO_STATUS_RESET);

        const uint64_t start_ns = blk_monotonic_ns();
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
            const uint64_t now_ns = blk_monotonic_ns();
            if (now_ns != 0 && now_ns >= deadline_ns) {
                break;
            }
            asm volatile("pause" ::: "memory");
        }

        if (!reset_confirmed && virtio_pci_get_status(drv->dev) == 0) {
            reset_confirmed = true;
        }

        if (!reset_confirmed) {
            blk_klog("[VIRTIO-BLK] ERROR: device reset timed out (status != 0)");
            virtio_pci_set_status(drv->dev, VIRTIO_STATUS_FAILED);
            drv->has_error = true;
        }
    } else {
        reset_confirmed = true;
    }

    if (reset_confirmed) {
        if (drv->vq) {
            virtqueue_destroy(drv->vq);
            drv->vq = nullptr;
        }
        if (drv->ctrl_vaddr) {
            blk_secure_zero(drv->ctrl_vaddr, sizeof(VirtioBlkControl));
            blk_dma_free(drv->ctrl_vaddr, drv->ctrl_paddr, drv->ctrl_pages);
            drv->ctrl_vaddr = nullptr;
            drv->ctrl_paddr = 0;
            drv->ctrl_pages = 0;
        }
        if (drv->data_vaddr) {
            blk_secure_zero(drv->data_vaddr, drv->data_size);
            blk_dma_free(drv->data_vaddr, drv->data_paddr, drv->data_pages);
            drv->data_vaddr = nullptr;
            drv->data_paddr = 0;
            drv->data_pages = 0;
            drv->data_size = 0;
        }
        drv->dev = nullptr;
        drv->initialized = false;
        drv->has_error = false;
        drv->raw_capacity_512 = 0;
        drv->total_sectors = 0;
        drv->bdev.total_sectors = 0;
        drv->bdev.write_count = 0;
        drv->total_reads = 0;
        drv->total_writes = 0;
        drv->total_flushes = 0;
        drv->total_sectors_read = 0;
        drv->total_sectors_written = 0;
        return true;
    } else {
        // KWARANTANNA DMA:
        // Brak potwierdzenia zatrzymania silnika urzadzenia. Zwalnianie pamieci do puli ogolnej
        // mogloby spowodowac niekontrolowane nadpisanie struktur jadra przez kontroler DMA.
        blk_klog("[VIRTIO-BLK] CRITICAL: DMA buffers quarantined due to unconfirmed device reset");
        drv->initialized = false;
        drv->has_error = true;
        drv->bdev.total_sectors = 0;
        return false;
    }
}

/*
 * Zglasza zadanie do kolejki VirtQueue 0, powiadamia urzadzenie, oczekuje z limitem monotonicznym
 * i weryfikuje zakonczenie (id deskryptora, used.len, status).
 * Funkcja wywolywana w sekcji krytycznej (pod lockiem sterownika).
 */
static BlockError virtio_blk_submit_request_locked(VirtioBlkDriver* drv,
                                                   BlkReqOp op,
                                                   uint64_t lba,
                                                   uint32_t count,
                                                   void* buf) {
    if (!drv || !drv->initialized || drv->has_error || !drv->vq || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }

    uint32_t data_bytes = 0;
    uint64_t virtio_sector = 0;

    if (op != BlkReqOp::Flush) {
        if (!buf || count == 0) return BlockError::InvalidArgument;

        const uint64_t total_bytes_u64 = static_cast<uint64_t>(count) * static_cast<uint64_t>(drv->logical_sector_size);
        if (total_bytes_u64 > drv->data_size || total_bytes_u64 > UINT32_MAX) {
            return BlockError::InvalidArgument;
        }
        data_bytes = static_cast<uint32_t>(total_bytes_u64);

        // Zgodnie ze specyfikacja VirtIO 1.x (sec 5.2.6), pole sector jest ZAWSZE wyrazane
        // w jednostkach 512-bajtowych niezaleznie od logicznego rozmiaru sektora.
        const uint32_t ratio_512 = drv->logical_sector_size / 512U;
        if (lba > (UINT64_MAX / ratio_512)) {
            return BlockError::InvalidArgument;
        }
        virtio_sector = lba * static_cast<uint64_t>(ratio_512);
    }

    // Inicjalizacja bufora kontrolnego
    VirtioBlkControl* ctrl = reinterpret_cast<VirtioBlkControl*>(drv->ctrl_vaddr);
    blk_secure_zero(ctrl, sizeof(VirtioBlkControl));

    if (op == BlkReqOp::Read) {
        ctrl->header.type = VIRTIO_BLK_T_IN;
    } else if (op == BlkReqOp::Write) {
        ctrl->header.type = VIRTIO_BLK_T_OUT;
    } else {
        ctrl->header.type = VIRTIO_BLK_T_FLUSH;
    }
    ctrl->header.reserved = 0;
    ctrl->header.sector = virtio_sector;
    ctrl->status = 0xFF; // Wartosc wartownicza: urzadzenie musi ja nadpisac

    // W przypadku zapisu: kopiujemy dane do bufora DMA przed zgloszeniem do kolejki
    if (op == BlkReqOp::Write) {
        blk_memcpy(drv->data_vaddr, buf, data_bytes);
        asm volatile("mfence" ::: "memory");
    }

    // Przygotowanie lancucha deskryptorow scatter-gather
    VirtqBuffer bufs[3];
    uint32_t num_bufs = 0;

    // Deskryptor 0: Naglowek zadania (16 bajtow, czytany przez urzadzenie)
    bufs[0].phys_addr = drv->ctrl_paddr + offsetof(VirtioBlkControl, header);
    bufs[0].len = sizeof(VirtIOBlkReqHeader);
    bufs[0].is_write = false;

    if (op == BlkReqOp::Flush) {
        // FLUSH: 2-elementowy lancuch (naglowek + bajt statusu)
        bufs[1].phys_addr = drv->ctrl_paddr + offsetof(VirtioBlkControl, status);
        bufs[1].len = 1;
        bufs[1].is_write = true;
        num_bufs = 2;
    } else {
        // READ / WRITE: 3-elementowy lancuch
        // Deskryptor 1: Bufor danych DMA
        bufs[1].phys_addr = drv->data_paddr;
        bufs[1].len = data_bytes;
        // Dla READ: urzadzenie zapisuje do bufora (is_write = true)
        // Dla WRITE: urzadzenie czyta z bufora (is_write = false)
        bufs[1].is_write = (op == BlkReqOp::Read);

        // Deskryptor 2: Bajt statusu (1 bajt, zapisywany przez urzadzenie)
        bufs[2].phys_addr = drv->ctrl_paddr + offsetof(VirtioBlkControl, status);
        bufs[2].len = 1;
        bufs[2].is_write = true;
        num_bufs = 3;
    }

    int head = virtqueue_submit(drv->vq, bufs, num_bufs, ctrl);
    if (head < 0) {
        blk_klog("[VIRTIO-BLK] ERROR: virtqueue_submit failed");
        return BlockError::ResourceFailure;
    }

    __atomic_store_n(&drv->request_done, false, __ATOMIC_RELEASE);
    virtqueue_kick(drv->vq, true);

    // Oczekiwanie na zakonczenie z ograniczeniem czasu monotonicznego i wtornym spin guardem
    const uint64_t start_ns = blk_monotonic_ns();
    const uint64_t timeout_ns = VIRTIO_BLK_DEFAULT_TIMEOUT_MS * 1000000ULL;
    const uint64_t deadline_ns = (start_ns > UINT64_MAX - timeout_ns) ? UINT64_MAX : (start_ns + timeout_ns);
    uint32_t spin_guard = VIRTIO_BLK_MAX_SPINS_GUARD;

    bool completed = false;
    void* cookie = nullptr;
    uint32_t completed_len = 0;
    int popped_id = -1;

    if (drv->use_msix) {
        while (spin_guard > 0) {
            spin_guard--;
            if (__atomic_load_n(&drv->request_done, __ATOMIC_ACQUIRE) || virtqueue_has_used(drv->vq)) {
                popped_id = virtqueue_pop_used(drv->vq, &cookie, &completed_len);
                if (popped_id >= 0) {
                    completed = true;
                    break;
                } else if (popped_id != -1) {
                    blk_klog("[VIRTIO-BLK] ERROR: virtqueue_pop_used ring corruption");
                    drv->has_error = true;
                    virtio_blk_reset_locked(drv);
                    if (op == BlkReqOp::Read && buf) blk_secure_zero(buf, data_bytes);
                    return BlockError::IOError;
                }
            }
            const uint64_t now_ns = blk_monotonic_ns();
            if (now_ns != 0 && now_ns >= deadline_ns) {
                break;
            }
            asm volatile("pause" ::: "memory");
        }
        if (!completed) {
            uint32_t poll_guard = 100000U;
            while (!completed && poll_guard > 0) {
                poll_guard--;
                if (virtqueue_has_used(drv->vq)) {
                    popped_id = virtqueue_pop_used(drv->vq, &cookie, &completed_len);
                    if (popped_id >= 0) {
                        completed = true;
                        break;
                    }
                }
                asm volatile("pause" ::: "memory");
            }
        }
    } else {
        while (spin_guard > 0) {
            spin_guard--;
            if (virtqueue_has_used(drv->vq)) {
                popped_id = virtqueue_pop_used(drv->vq, &cookie, &completed_len);
                if (popped_id >= 0) {
                    completed = true;
                    break;
                } else if (popped_id != -1) {
                    blk_klog("[VIRTIO-BLK] ERROR: virtqueue_pop_used ring corruption");
                    drv->has_error = true;
                    virtio_blk_reset_locked(drv);
                    if (op == BlkReqOp::Read && buf) blk_secure_zero(buf, data_bytes);
                    return BlockError::IOError;
                }
            }
            const uint64_t now_ns = blk_monotonic_ns();
            if (now_ns != 0 && now_ns >= deadline_ns) {
                break;
            }
            asm volatile("pause" ::: "memory");
        }
    }

    if (!completed && virtqueue_has_used(drv->vq)) {
        popped_id = virtqueue_pop_used(drv->vq, &cookie, &completed_len);
        if (popped_id >= 0) {
            completed = true;
        }
    }

    if (!completed) {
        blk_klog("[VIRTIO-BLK] ERROR: request timed out");
        drv->has_error = true;
        virtio_blk_reset_locked(drv);
        if (op == BlkReqOp::Read && buf) blk_secure_zero(buf, data_bytes);
        return BlockError::Timeout;
    }

    // Walidacja tozsamosci zakonczonego deskryptora i tokenu
    if (popped_id != head || cookie != ctrl) {
        blk_klog("[VIRTIO-BLK] ERROR: descriptor ID or cookie mismatch in completion");
        drv->has_error = true;
        virtio_blk_reset_locked(drv);
        if (op == BlkReqOp::Read && buf) blk_secure_zero(buf, data_bytes);
        return BlockError::IOError;
    }

    // Weryfikacja dlugosci zapisanej przez urzadzenie:
    // Zgodnie z regula zadania: jesli used.len > submitted buffer length, odrzuc jako korupcje.
    if (op == BlkReqOp::Flush || op == BlkReqOp::Write) {
        if (completed_len != 1U) {
            blk_klog("[VIRTIO-BLK] ERROR: completion used.len invalid for write/flush");
            drv->has_error = true;
            virtio_blk_reset_locked(drv);
            return BlockError::IOError;
        }
    } else { // Read
        const uint32_t expected_max = data_bytes + 1U;
        if (completed_len > expected_max) {
            blk_klog("[VIRTIO-BLK] ERROR: completion used.len > submitted buffer length (corruption)");
            drv->has_error = true;
            virtio_blk_reset_locked(drv);
            if (buf) blk_secure_zero(buf, data_bytes);
            return BlockError::IOError;
        }
        if (completed_len < 1U) {
            blk_klog("[VIRTIO-BLK] ERROR: completion used.len < 1 (status byte missing)");
            drv->has_error = true;
            virtio_blk_reset_locked(drv);
            if (buf) blk_secure_zero(buf, data_bytes);
            return BlockError::IOError;
        }
    }

    // Bariera pamieci przed odczytem bajtu statusu i bufora danych z pamieci DMA
    asm volatile("mfence" ::: "memory");
    const uint8_t status = ctrl->status;

    if (status == VIRTIO_BLK_S_OK) {
        if (op == BlkReqOp::Read) {
            // Urzadzenie zapisalo status oraz dane. used.len zawiera bajt statusu.
            uint32_t data_written = completed_len - 1U;
            if (data_written > data_bytes) data_written = data_bytes;

            blk_memcpy(buf, drv->data_vaddr, data_written);

            // Jesli urzadzenie zwrocilo mniej niz zadano: zerujemy niezapelniona czesc bufora wyjsciowego
            if (data_written < data_bytes) {
                blk_secure_zero(static_cast<uint8_t*>(buf) + data_written, data_bytes - data_written);
            }

            if (drv->total_reads < UINT64_MAX) drv->total_reads++;
            if (drv->total_sectors_read <= UINT64_MAX - count) drv->total_sectors_read += count;
            else drv->total_sectors_read = UINT64_MAX;
        } else if (op == BlkReqOp::Write) {
            if (drv->total_writes < UINT64_MAX) drv->total_writes++;
            if (drv->total_sectors_written <= UINT64_MAX - count) drv->total_sectors_written += count;
            else drv->total_sectors_written = UINT64_MAX;
        } else { // Flush
            if (drv->total_flushes < UINT64_MAX) drv->total_flushes++;
        }
        return BlockError::Ok;
    } else if (status == VIRTIO_BLK_S_UNSUPP) {
        if (op == BlkReqOp::Read && buf) blk_secure_zero(buf, data_bytes);
        return BlockError::Unsupported;
    } else { // VIRTIO_BLK_S_IOERR lub nieznany kod bledu sprzetowego
        if (op == BlkReqOp::Read && buf) blk_secure_zero(buf, data_bytes);
        return BlockError::IOError;
    }
}

/* =========================================================================
 * IMPLEMENTACJA OPERACJI BLOKOWYCH (BlockOperations)
 * ========================================================================= */

static BlockError virtio_blk_op_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buf) {
    if (!dev || !dev->driver_data || !buf || count == 0) {
        return BlockError::InvalidArgument;
    }
    VirtioBlkDriver* drv = static_cast<VirtioBlkDriver*>(dev->driver_data);
    if (!drv->initialized || drv->has_error || !drv->vq || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }
    if (!block_range_valid(lba, count, drv->total_sectors)) {
        return BlockError::OutOfRange;
    }
    if (count > drv->max_transfer_sectors) {
        return BlockError::InvalidArgument;
    }

    BlkLockGuard guard(drv->lock);

    if (!drv->initialized || drv->has_error || !drv->vq || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }

    return virtio_blk_submit_request_locked(drv, BlkReqOp::Read, lba, count, buf);
}

static BlockError virtio_blk_op_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buf) {
    if (!dev || !dev->driver_data || !buf || count == 0) {
        return BlockError::InvalidArgument;
    }
    VirtioBlkDriver* drv = static_cast<VirtioBlkDriver*>(dev->driver_data);
    if (!drv->initialized || drv->has_error || !drv->vq || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }
    if (drv->read_only) {
        return BlockError::Unsupported;
    }
    if (!block_range_valid(lba, count, drv->total_sectors)) {
        return BlockError::OutOfRange;
    }
    if (count > drv->max_transfer_sectors) {
        return BlockError::InvalidArgument;
    }

    BlkLockGuard guard(drv->lock);

    if (!drv->initialized || drv->has_error || !drv->vq || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }
    if (drv->read_only) {
        return BlockError::Unsupported;
    }

    return virtio_blk_submit_request_locked(drv, BlkReqOp::Write, lba, count, const_cast<void*>(buf));
}

static BlockError virtio_blk_op_flush(BlockDevice* dev) {
    if (!dev || !dev->driver_data) {
        return BlockError::InvalidArgument;
    }
    VirtioBlkDriver* drv = static_cast<VirtioBlkDriver*>(dev->driver_data);
    if (!drv->initialized || drv->has_error || !drv->vq || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }
    if (!drv->has_flush) {
        return BlockError::Unsupported;
    }

    BlkLockGuard guard(drv->lock);

    if (!drv->initialized || drv->has_error || !drv->vq || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }
    if (!drv->has_flush) {
        return BlockError::Unsupported;
    }

    return virtio_blk_submit_request_locked(drv, BlkReqOp::Flush, 0, 0, nullptr);
}

static BlockError virtio_blk_op_revalidate(BlockDevice* dev) {
    if (!dev || !dev->driver_data) {
        return BlockError::InvalidArgument;
    }
    VirtioBlkDriver* drv = static_cast<VirtioBlkDriver*>(dev->driver_data);
    if (!drv->initialized || drv->has_error || !drv->dev) {
        return BlockError::DeviceUnavailable;
    }

    uint8_t st = virtio_pci_get_status(drv->dev);
    if (st == 0xFF ||
        (st & (VIRTIO_STATUS_DEVICE_NEEDS_RESET | VIRTIO_STATUS_FAILED)) != 0 ||
        (st & VIRTIO_STATUS_DRIVER_OK) == 0) {
        return BlockError::DeviceUnavailable;
    }

    uint64_t cur_cap = 0;
    if (!virtio_pci_read_device_config_consistent(drv->dev, 0, &cur_cap, sizeof(cur_cap))) {
        return BlockError::DeviceUnavailable;
    }
    if (cur_cap != drv->raw_capacity_512) {
        return BlockError::DeviceUnavailable;
    }

    return BlockError::Ok;
}

static const BlockOperations virtio_blk_operations = {
    virtio_blk_op_read,
    virtio_blk_op_write,
    virtio_blk_op_flush,
    virtio_blk_op_revalidate
};

} // namespace

/* =========================================================================
 * PUBLICZNE API STEROWNIKA VIRTIO-BLK
 * ========================================================================= */

bool virtio_blk_init(VirtioPciDevice* dev) {
    if (!dev || dev->virtio_id != VIRTIO_ID_BLOCK) {
        return false;
    }

    DiagPciAttempt diagnostic(DIAG_VIRTIO_BLOCK,"Kontroler virtio-blk","virtio-blk",
        dev->bus,dev->slot,dev->func,dev->pci_vendor_id,dev->pci_device_id);

    // Znajdz istniejacy slot lub przydziel nowy dla tego urzadzenia
    VirtioBlkDriver* drv = nullptr;
    uint32_t slot_idx = 0;

    for (uint32_t i = 0; i < g_blk_driver_count; ++i) {
        if (g_blk_drivers[i].dev == dev) {
            drv = &g_blk_drivers[i];
            slot_idx = i;
            break;
        }
    }
    if (!drv) {
        for (uint32_t i = 0; i < g_blk_driver_count; ++i) {
            if (!g_blk_drivers[i].initialized) {
                drv = &g_blk_drivers[i];
                slot_idx = i;
                break;
            }
        }
    }
    if (!drv) {
        if (g_blk_driver_count >= VIRTIO_BLK_MAX_DEVICES) {
            blk_klog("[VIRTIO-BLK] Reached maximum supported VirtIO-BLK devices");
            return false;
        }
        slot_idx = g_blk_driver_count++;
        drv = &g_blk_drivers[slot_idx];
    }

    BlkLockGuard guard(drv->lock);

    // Krok 1: Wymuszenie czystej sekwencji inicjalizacji VirtIO 1.x (VirtIO 1.0 sec 3.1.1):
    // Reset urzadzenia jezeli nie jest w stanie czystym, nastepnie ACKNOWLEDGE oraz DRIVER
    uint8_t cur_status = virtio_pci_get_status(dev);
    if ((cur_status & (VIRTIO_STATUS_DRIVER_OK | VIRTIO_STATUS_FAILED | VIRTIO_STATUS_DEVICE_NEEDS_RESET)) != 0 ||
        (cur_status & VIRTIO_STATUS_FEATURES_OK) != 0) {
        virtio_pci_set_status(dev, VIRTIO_STATUS_RESET);
        if (!virtio_pci_reset(dev)) {
            blk_klog("[VIRTIO-BLK] Device reset failed before init");
            return false;
        }
        cur_status = 0;
    }

    // Negocjacja cech urzadzenia (VirtIO 1.x)
    // Wymagamy bezwzglednie VIRTIO_F_VERSION_1, negocjujemy tylko cechy faktycznie obslugiwane.
    const uint64_t requested_features = VIRTIO_F_VERSION_1 |
                                        VIRTIO_BLK_F_SIZE_MAX |
                                        VIRTIO_BLK_F_SEG_MAX |
                                        VIRTIO_BLK_F_RO |
                                        VIRTIO_BLK_F_BLK_SIZE |
                                        VIRTIO_BLK_F_FLUSH |
                                        VIRTIO_BLK_F_TOPOLOGY;

    if ((cur_status & VIRTIO_STATUS_FEATURES_OK) == 0) {
        virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);
        virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
        if (!virtio_pci_negotiate_features(dev, requested_features)) {
            blk_klog("[VIRTIO-BLK] Feature negotiation failed or device rejected VIRTIO_F_VERSION_1");
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
    }

    const uint64_t neg_features = dev->driver_features;
    const bool has_blk_size = (neg_features & VIRTIO_BLK_F_BLK_SIZE) != 0;
    const bool has_topology = (neg_features & VIRTIO_BLK_F_TOPOLOGY) != 0;
    const bool has_flush    = (neg_features & VIRTIO_BLK_F_FLUSH) != 0;
    const bool is_ro        = (neg_features & VIRTIO_BLK_F_RO) != 0;
    const bool has_size_max = (neg_features & VIRTIO_BLK_F_SIZE_MAX) != 0;

    // Krok 2: Bezpieczny odczyt konfiguracji urzadzenia (capacity, blk_size, topology)
    if (!dev->has_device_cfg) {
        blk_klog("[VIRTIO-BLK] Device missing device_cfg capability");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    uint64_t raw_capacity_512 = 0;
    if (!virtio_pci_read_device_config_consistent(dev, 0, &raw_capacity_512, sizeof(raw_capacity_512))) {
        blk_klog("[VIRTIO-BLK] Failed to read capacity consistently");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    if (raw_capacity_512 == 0 || raw_capacity_512 > (UINT64_MAX / 512ULL)) {
        blk_klog("[VIRTIO-BLK] Invalid or overflowing device capacity");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    uint32_t logical_sector_size = 512;
    if (has_blk_size) {
        uint32_t blk_sz = 0;
        if (virtio_pci_read_device_config_consistent(dev, 20, &blk_sz, sizeof(blk_sz))) {
            if (blk_sz >= 512 && blk_sz <= 65536 && (blk_sz & (blk_sz - 1)) == 0) {
                logical_sector_size = blk_sz;
            }
        }
    }

    uint32_t physical_sector_size = logical_sector_size;
    if (has_topology) {
        struct {
            uint8_t physical_block_exp;
            uint8_t alignment_offset;
            uint16_t min_io_size;
            uint32_t opt_io_size;
        } topo{};
        if (virtio_pci_read_device_config_consistent(dev, 24, &topo, sizeof(topo))) {
            if (topo.physical_block_exp <= 16) {
                const uint64_t phys_sz = static_cast<uint64_t>(logical_sector_size) << topo.physical_block_exp;
                if (phys_sz <= 65536) {
                    physical_sector_size = static_cast<uint32_t>(phys_sz);
                }
            }
        }
    }

    const uint64_t total_bytes = raw_capacity_512 * 512ULL;
    const uint64_t total_sectors = total_bytes / logical_sector_size;
    if (total_sectors == 0) {
        blk_klog("[VIRTIO-BLK] Calculated total_sectors is 0");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    size_t bounce_buf_limit = VIRTIO_BLK_BOUNCE_BUFFER_SIZE;
    if (has_size_max) {
        uint32_t sm = 0;
        if (virtio_pci_read_device_config_consistent(dev, 8, &sm, sizeof(sm)) && sm > 0) {
            if (sm < bounce_buf_limit) bounce_buf_limit = sm;
        }
    }

    const uint32_t max_transfer_sectors = static_cast<uint32_t>(bounce_buf_limit / logical_sector_size);
    if (max_transfer_sectors == 0) {
        blk_klog("[VIRTIO-BLK] max_transfer_sectors evaluated to 0");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    // Krok 3: Alokacja buforow DMA
    if (!drv->ctrl_vaddr) {
        if (!blk_dma_alloc(sizeof(VirtioBlkControl), 64, &drv->ctrl_vaddr, &drv->ctrl_paddr, &drv->ctrl_pages)) {
            blk_klog("[VIRTIO-BLK] DMA alloc failed for ctrl buffer");
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
    }
    blk_secure_zero(drv->ctrl_vaddr, sizeof(VirtioBlkControl));

    if (!drv->data_vaddr) {
        drv->data_size = VIRTIO_BLK_BOUNCE_BUFFER_SIZE;
        if (!blk_dma_alloc(drv->data_size, 4096, &drv->data_vaddr, &drv->data_paddr, &drv->data_pages)) {
            blk_klog("[VIRTIO-BLK] DMA alloc failed for data bounce buffer");
            blk_dma_free(drv->ctrl_vaddr, drv->ctrl_paddr, drv->ctrl_pages);
            drv->ctrl_vaddr = nullptr;
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
    }
    blk_secure_zero(drv->data_vaddr, drv->data_size);

    // Krok 4: Konfiguracja kolejki VirtQueue 0
    if (drv->vq) {
        virtqueue_destroy(drv->vq);
        drv->vq = nullptr;
    }
    drv->vq = virtqueue_create(dev, 0);
    if (!drv->vq) {
        blk_klog("[VIRTIO-BLK] virtqueue_create failed for queue 0");
        blk_dma_free(drv->data_vaddr, drv->data_paddr, drv->data_pages);
        drv->data_vaddr = nullptr;
        blk_dma_free(drv->ctrl_vaddr, drv->ctrl_paddr, drv->ctrl_pages);
        drv->ctrl_vaddr = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    // Konfiguracja MSI-X dla dysku (jesli urzadzenie obsluguje MSI-X)
    if (dev->has_msix) {
        int vec = idt_alokuj_wektor_irq();
        if (vec >= 0) {
            uint8_t lapic_id = apic_pobierz_lapic_id();
            if (idt_zarejestruj_irq(static_cast<uint8_t>(vec), virtio_blk_isr, drv)) {
                if (virtio_pci_program_msix_entry(dev, 0, lapic_id, static_cast<uint8_t>(vec), false)) {
                    if (virtio_pci_set_queue_msix_vector_checked(dev, 0, 0)) {
                        if (pci_msix_enable(dev->bus, dev->slot, dev->func, &dev->msix_cap)) {
                            dev->msix_enabled = true;
                            drv->use_msix = true;
                            drv->irq_vector = vec;
                            blk_klog("[VIRTIO-BLK] MSI-X configured successfully (entry 0)");
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

    // Krok 5: Ustawienie DRIVER_OK wylacznie po pelnej gotowosci kolejki i struktur
    virtio_pci_set_status(dev, virtio_pci_get_status(dev) | VIRTIO_STATUS_DRIVER_OK);
    if ((virtio_pci_get_status(dev) & VIRTIO_STATUS_DRIVER_OK) == 0) {
        blk_klog("[VIRTIO-BLK] Device rejected DRIVER_OK");
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
        virtqueue_destroy(drv->vq);
        drv->vq = nullptr;
        blk_dma_free(drv->data_vaddr, drv->data_paddr, drv->data_pages);
        drv->data_vaddr = nullptr;
        blk_dma_free(drv->ctrl_vaddr, drv->ctrl_paddr, drv->ctrl_pages);
        drv->ctrl_vaddr = nullptr;
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

    // Krok 6: Inicjalizacja struktury sterownika i integracja z BlockDevice API
    drv->dev = dev;
    drv->raw_capacity_512 = raw_capacity_512;
    drv->total_sectors = total_sectors;
    drv->logical_sector_size = logical_sector_size;
    drv->physical_sector_size = physical_sector_size;
    drv->max_transfer_sectors = max_transfer_sectors;
    drv->has_flush = has_flush;
    drv->read_only = is_ro;
    drv->has_error = false;
    drv->initialized = true;
    drv->total_reads = 0;
    drv->total_writes = 0;
    drv->total_flushes = 0;
    drv->total_sectors_read = 0;
    drv->total_sectors_written = 0;

    drv->bdev = BlockDevice{};
    drv->bdev.driver_data = drv;
    drv->bdev.ops = &virtio_blk_operations;
    drv->bdev.total_sectors = total_sectors;
    drv->bdev.logical_sector_size = logical_sector_size;
    drv->bdev.physical_sector_size = physical_sector_size;
    drv->bdev.max_transfer_sectors = max_transfer_sectors;
    drv->bdev.read_only = is_ro;
    drv->bdev.removable = false;
    drv->bdev.device_type = BlockDeviceType::VirtIO;
    drv->bdev.write_count = 0;

    const char* default_model = "VirtIO Block Disk";
    for (size_t i = 0; i < sizeof(drv->bdev.model) - 1 && default_model[i]; ++i) {
        drv->bdev.model[i] = default_model[i];
    }
    drv->bdev.model[sizeof(drv->bdev.model) - 1] = '\0';

    const char* default_serial = "VIRTIO-BLK";
    for (size_t i = 0; i < sizeof(drv->bdev.serial) - 1 && default_serial[i]; ++i) {
        drv->bdev.serial[i] = default_serial[i];
    }
    drv->bdev.serial[sizeof(drv->bdev.serial) - 1] = '\0';

    drv->bdev.sync_state();

    // Rejestracja w podsystemie blokowym jadra
    if (!g_blk_registered[slot_idx]) {
#ifndef BURSZTYN_VIRTIO_HOST_TEST
        if (!blokowy_zarejestruj_dysk(&drv->bdev)) {
            blk_klog("[VIRTIO-BLK] blokowy_zarejestruj_dysk failed");
            virtio_blk_reset_locked(drv);
            return false;
        }
#endif
        g_blk_registered[slot_idx] = true;
    }

    blk_klog("[VIRTIO-BLK] Successfully initialized and registered VirtIO-BLK device");
    diagnostic.ready();
    return true;
}

uint32_t virtio_blk_probe_and_init(void) {
    const uint32_t total_pci = virtio_pci_liczba_urzadzen();
    uint32_t registered = 0;

    for (uint32_t i = 0; i < total_pci; ++i) {
        VirtioPciDevice* dev = virtio_pci_pobierz_urzadzenie(i);
        if (dev && dev->virtio_id == VIRTIO_ID_BLOCK) {
            if (virtio_blk_init(dev)) {
                registered++;
            }
        }
    }
    return registered;
}

bool virtio_blk_reset(VirtioBlkDriver* drv) {
    if (!drv) return false;
    BlkLockGuard guard(drv->lock);
    return virtio_blk_reset_locked(drv);
}

VirtioBlkDriver* virtio_blk_get_driver(uint32_t index) {
    if (index >= g_blk_driver_count) return nullptr;
    return &g_blk_drivers[index];
}

uint32_t virtio_blk_get_count(void) {
    return g_blk_driver_count;
}

void virtio_blk_reset_all(void) {
    for (uint32_t i = 0; i < g_blk_driver_count; ++i) {
        virtio_blk_reset(&g_blk_drivers[i]);
    }
    g_blk_driver_count = 0;
}

static bool virtio_blk_validate_scratch_disk(VirtioBlkDriver* drv, uint64_t* out_scratch_lba) {
    if (!drv || !drv->initialized || !drv->vq || !drv->dev) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: driver not initialized.");
        return false;
    }
    if (drv->read_only) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: device is read-only.");
        return false;
    }
    if (drv->total_sectors < 4) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: capacity too small for test scratch disk (< 4 sectors).");
        return false;
    }

#ifndef BURSZTYN_VIRTIO_HOST_TEST
    // 1. Walidacja czy urzadzenie to nosnik ISO9660
    if (Storage::source_iso(&drv->bdev)) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: target device is ISO9660 media.");
        return false;
    }

    // 2. Walidacja trybu instalatora lub wyznaczonego celu instalacji
    if (instalator_tryb() || blokowy_zapis_instalatora()) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: installer mode or installer write target active.");
        return false;
    }

    // 3. Walidacja czy urzadzenie jest glownym zarejestrowanym dyskiem systemowym
    if (&drv->bdev == blokowy_dysk_systemowy()) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: device is registered system disk.");
        return false;
    }
#endif

    // 4. Odczyt znacznika testowego z LBA 1
    VirtioBlkTestMarker marker{};
    BlockError err = drv->bdev.read(VIRTIO_BLK_TEST_MARKER_LBA, 1, &marker);
    if (err != BlockError::Ok) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: failed to read test-disk marker from LBA 1.");
        return false;
    }

    // 5. Walidacja pol znacznika
    if (blk_memcmp(marker.magic, VIRTIO_BLK_TEST_MARKER_MAGIC, sizeof(VIRTIO_BLK_TEST_MARKER_MAGIC) - 1) != 0) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: missing or invalid test-disk magic.");
        return false;
    }
    if (marker.signature != VIRTIO_BLK_TEST_SIGNATURE) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: invalid test-disk signature.");
        return false;
    }
    if (marker.version != VIRTIO_BLK_TEST_VERSION) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: unsupported test-disk marker version.");
        return false;
    }
    if ((marker.flags & VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE) == 0) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: test-disk marker flags do not permit destructive writes.");
        return false;
    }

    const uint32_t expected_crc = rozruch_oblicz_crc32(&marker, offsetof(VirtioBlkTestMarker, crc32));
    if (marker.crc32 != expected_crc) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: test-disk marker CRC32 checksum mismatch.");
        return false;
    }

    // 6. Wyznaczenie sektora testowego
    uint64_t test_lba = (marker.scratch_lba != 0) ? marker.scratch_lba : (drv->total_sectors - 1);
    if (test_lba == 0) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: test sector cannot be LBA 0.");
        return false;
    }
    if (test_lba == VIRTIO_BLK_TEST_MARKER_LBA) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: test sector cannot overwrite marker LBA 1.");
        return false;
    }
    if (test_lba == ROZRUCH_SEKTOR_KOPIA_0 || test_lba == ROZRUCH_SEKTOR_KOPIA_1) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: test sector collides with A/B boot-state sectors 2046/2047.");
        return false;
    }
    if (test_lba >= drv->total_sectors) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: test sector is out of bounds.");
        return false;
    }

#ifndef BURSZTYN_VIRTIO_HOST_TEST
    // 7. Weryfikacja ze LBA 2046/2047 nie zawieraja autorytatywnego stanu rozruchu
    if (drv->total_sectors > ROZRUCH_SEKTOR_KOPIA_1) {
        uint8_t boot_sec[512]{};
        if (drv->bdev.read(ROZRUCH_SEKTOR_KOPIA_0, 1, boot_sec) == BlockError::Ok) {
            auto* s = reinterpret_cast<const struct bursztyn_rozruch_stan*>(boot_sec);
            if (s->magic == ROZRUCH_MAGIC) {
                blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: disk contains active A/B boot-state metadata.");
                return false;
            }
        }
    }

    // 8. Weryfikacja ze dysk nie zawiera aktywnej tablicy GPT ani partycji BSP2/ESP
    Storage::Table tbl{};
    if (Storage::gpt_read(&drv->bdev, &tbl)) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: disk contains valid GPT partition table.");
        return false;
    }

    // 9. Weryfikacja ze LBA 0 nie zawiera naglowka systemu plikow BSP2
    uint8_t sec0[512]{};
    if (drv->bdev.read(0, 1, sec0) == BlockError::Ok) {
        if (sec0[0] == 'B' && sec0[1] == 'S' && sec0[2] == 'P' && sec0[3] == '2') {
            blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: sector 0 contains BSP2 superblock.");
            return false;
        }
    }
#endif

    if (out_scratch_lba) {
        *out_scratch_lba = test_lba;
    }
    return true;
}

bool virtio_blk_stress_test(VirtioBlkDriver* drv, bool allow_destructive) {
    if (!drv || !drv->initialized || !drv->vq || !drv->dev) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: driver not initialized");
        return false;
    }

    blk_klog("[VIRTIO-BLK-STRESS] Starting stress test: raw read, write, flush, wraparound, reset...");

    // 1. Raw Read test LBA 0
    blk_klog("[VIRTIO-BLK-STRESS] Step 1: raw read LBA 0");
    uint8_t sec_buf[512]{};
    BlockError err = drv->bdev.read(0, 1, sec_buf);
    if (err != BlockError::Ok) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: initial read LBA 0 failed");
        return false;
    }

    // 2. Walidacja odrzucania zakresow poza pojemnoscia nosnika
    blk_klog("[VIRTIO-BLK-STRESS] Step 2: out-of-bounds LBA validation");
    uint8_t dummy[512]{};
    if (drv->bdev.read(drv->total_sectors, 1, dummy) != BlockError::OutOfRange) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: expected OutOfRange on capacity read");
        return false;
    }
    if (drv->bdev.read(drv->total_sectors + 10, 1, dummy) != BlockError::OutOfRange) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: expected OutOfRange on past-capacity read");
        return false;
    }

    // 3. Raw Write and Flush test na dedykowanym sektorze nosnika testowego
    bool destructive_tested = false;
    if (!allow_destructive) {
        blk_klog("[VIRTIO-BLK-STRESS] Step 3 skipped: destructive disk test flag not set (requires bursztyn.test.diskdestructive=1).");
    } else {
        uint64_t test_lba = 0;
        if (virtio_blk_validate_scratch_disk(drv, &test_lba)) {
            blk_klog("[VIRTIO-BLK-STRESS] Test-disk identity verified (dedicated scratch disk). Executing write, flush, readback...");
            blk_klog("[VIRTIO-BLK-STRESS] Step 3: write, flush, readback verification");
            uint8_t write_pat[512];
            uint8_t read_pat[512]{};
            for (size_t i = 0; i < 512; ++i) {
                write_pat[i] = static_cast<uint8_t>((i * 7 + 0xA5) & 0xFF);
            }

            err = drv->bdev.write(test_lba, 1, write_pat);
            if (err != BlockError::Ok) {
                blk_klog("[VIRTIO-BLK-STRESS] ERROR: write to test sector failed");
                return false;
            }

            err = drv->bdev.flush();
            if (err != BlockError::Ok && err != BlockError::Unsupported) {
                blk_klog("[VIRTIO-BLK-STRESS] ERROR: flush failed");
                return false;
            }

            err = drv->bdev.read(test_lba, 1, read_pat);
            if (err != BlockError::Ok) {
                blk_klog("[VIRTIO-BLK-STRESS] ERROR: read-back of test sector failed");
                return false;
            }

            if (blk_memcmp(write_pat, read_pat, 512) != 0) {
                blk_klog("[VIRTIO-BLK-STRESS] ERROR: read-back pattern mismatch");
                return false;
            }
            destructive_tested = true;
        }
    }

    // 4. Ring wraparound i recykling deskryptorow (1024 transakcje)
    blk_klog("[VIRTIO-BLK-STRESS] Step 4: ring wraparound 1024 transactions");
    const uint16_t initial_free = virtqueue_get_num_free(drv->vq);
    const uint32_t NUM_TX = 1024;
    for (uint32_t i = 0; i < NUM_TX; ++i) {
        err = drv->bdev.read(0, 1, sec_buf);
        if (err != BlockError::Ok) {
            blk_klog("[VIRTIO-BLK-STRESS] ERROR: transaction failed during wraparound loop");
            return false;
        }
        if (virtqueue_get_num_free(drv->vq) != initial_free) {
            blk_klog("[VIRTIO-BLK-STRESS] ERROR: descriptor leak detected during wraparound");
            return false;
        }
    }

    // 5. Test zatrzymania, resetu sprzetowego i czystego zwolnienia z PMM delta = 0
    blk_klog("[VIRTIO-BLK-STRESS] Step 5: hardware reset and PMM delta verification");
    VirtioPciDevice* dev = drv->dev;
    if (!virtio_blk_reset(drv)) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: reset failed during stress test");
        return false;
    }

#ifndef BURSZTYN_VIRTIO_HOST_TEST
    const uint64_t pmm_initial = pmm_pobierz_wolne_ramki();
#endif

    // Cykl re-init i ponownego zwolnienia zasobow
    blk_klog("[VIRTIO-BLK-STRESS] Step 6: first re-init");
    if (!virtio_blk_init(dev)) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: re-init failed after reset");
        return false;
    }

    drv = virtio_blk_get_driver(0);
    if (!drv || !drv->initialized) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: driver slot 0 invalid after re-init");
        return false;
    }

    blk_klog("[VIRTIO-BLK-STRESS] Step 7: read after re-init");
    err = drv->bdev.read(0, 1, sec_buf);
    if (err != BlockError::Ok) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: read failed after re-init");
        return false;
    }

    blk_klog("[VIRTIO-BLK-STRESS] Step 8: second reset");
    if (!virtio_blk_reset(drv)) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: second reset failed");
        return false;
    }

#ifndef BURSZTYN_VIRTIO_HOST_TEST
    const uint64_t pmm_after = pmm_pobierz_wolne_ramki();
    if (pmm_after != pmm_initial) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: PMM delta != 0 across reset cycle");
        return false;
    }
#endif

    // Ostateczna ponowna inicjalizacja urzadzenia dla dalszej pracy systemu
    blk_klog("[VIRTIO-BLK-STRESS] Step 9: final re-init");
    if (!virtio_blk_init(dev)) {
        blk_klog("[VIRTIO-BLK-STRESS] ERROR: final re-init failed");
        return false;
    }

    if (destructive_tested) {
        blk_klog("[VIRTIO-BLK-STRESS] SUCCESS: raw I/O, flush, ring wraparound, reset and PMM delta=0 verified.");
    } else {
        blk_klog("[VIRTIO-BLK-STRESS] SUCCESS: read I/O, ring wraparound, reset and PMM delta=0 verified (destructive write skipped).");
    }
    return true;
}
