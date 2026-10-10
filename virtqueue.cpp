/*
 * Bursztyn OS - Wspolna warstwa Split VirtQueue (VirtIO 1.x)
 *
 * Plik: virtqueue.cpp
 */

#include "virtqueue.h"

#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <cstdlib>
#include <cstring>

extern bool host_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages);
extern void host_dma_free(void* vaddr, uint64_t paddr, size_t pages);
extern void host_virtqueue_mb_hook();
extern void host_virtqueue_rmb_hook();
extern void host_virtqueue_wmb_hook();

static void* kernel_kmalloc(size_t size) {
    return std::malloc(size);
}

static void kernel_kfree(void* ptr) {
    std::free(ptr);
}

static bool virtq_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void virtq_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

#else
#include "sterowniki/dma.h"
#include "heap.h"
#include "grafika.h"

static void* kernel_kmalloc(size_t size) {
    return kmalloc(static_cast<uint64_t>(size));
}

static void kernel_kfree(void* ptr) {
    kfree(ptr);
}

static bool virtq_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    DmaBuffer dma{};
    if (!dma_allocate(size, alignment, &dma)) {
        return false;
    }
    *vaddr = dma.virtual_address;
    *paddr = dma.physical_address;
    *pages = dma.page_count;
    return true;
}

static void virtq_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    DmaBuffer dma{};
    dma.virtual_address = vaddr;
    dma.physical_address = paddr;
    dma.page_count = pages;
    dma_release(&dma);
}
#endif

namespace {

/* =========================================================================
 * BARIERY PAMIECIOWE DLA SPLIT VIRTQUEUE (x86-64)
 * ========================================================================= */

static inline void virtqueue_mb() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    host_virtqueue_mb_hook();
#endif
    // Store-Load barrier dla x86-64 (mfence gwarantuje pelne uporzadkowanie z pamiecia urzadzenia)
    asm volatile("mfence" ::: "memory");
}

static inline void virtqueue_rmb() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    host_virtqueue_rmb_hook();
#endif
    // Load-Load: na x86-64 odczyty sa sprzetowo uporzadkowane, bariera kompilatora zapobiega reorderingowi
    asm volatile("" ::: "memory");
}

static inline void virtqueue_wmb() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    host_virtqueue_wmb_hook();
#endif
    // Store-Store: na x86-64 zapisy sa sprzetowo uporzadkowane, bariera kompilatora zapobiega reorderingowi
    asm volatile("" ::: "memory");
}

} // namespace

/* =========================================================================
 * IMPLEMENTACJA PUBLICZNEGO API VIRTQUEUE
 * ========================================================================= */

VirtQueue* virtqueue_create(VirtioPciDevice* dev, uint16_t queue_index) {
    if (!dev || !dev->common_cfg) return nullptr;

    // 1. Wybor kolejki w kontrolerze transportu
    virtio_pci_diag_trace_queue(dev, queue_index, "queue discovery");
    virtio_pci_select_queue(dev, queue_index);

    // 2. Weryfikacja, czy kolejka nie jest juz wlaczona
    virtio_pci_input_init_trace(dev, "queue enable read begin");
    const uint16_t enabled = virtio_pci_get_queue_enable(dev);
    virtio_pci_input_init_trace(dev, "queue enable read done", enabled);
    if (enabled != 0) {
        return nullptr;
    }

    // 3. Odczyt i scisla walidacja rozmiaru kolejki
    virtio_pci_input_init_trace(dev, "queue size read begin");
    const uint16_t qsize = virtio_pci_get_queue_size(dev);
    virtio_pci_input_init_trace(dev, "queue size read done", qsize);
    if (qsize == 0 || qsize > VIRTQUEUE_MAX_QUEUE_SIZE || (qsize & (qsize - 1U)) != 0) {
        return nullptr;
    }

    // 4. Obliczenie rozmiaru i przesuniec struktur Split VirtQueue (VirtIO 1.0 sec 2.6)
    // Descriptor Table: 16 * qsize (wyrownanie 16 B)
    const size_t desc_size = sizeof(VirtqDesc) * qsize;
    const size_t avail_offset = desc_size;
    // Available Ring: flags(2) + idx(2) + ring[qsize](2 * qsize) + used_event(2) (wyrownanie 2 B)
    const size_t avail_size = 4 + sizeof(uint16_t) * qsize + 2;
    // Used Ring: flags(2) + idx(2) + ring[qsize](8 * qsize) + avail_event(2) (wyrownanie 4 B / 64 B)
    const size_t used_offset = (avail_offset + avail_size + 63U) & ~63U;
    const size_t used_size = 4 + sizeof(VirtqUsedElem) * qsize + 2;
    const size_t total_dma_size = used_offset + used_size;

    // 5. Alokacja bufora DMA dla struktur pierscienia
    void* dma_virt = nullptr;
    uint64_t dma_phys = 0;
    size_t dma_pages = 0;

    virtio_pci_input_init_trace(dev, "queue DMA allocate begin", total_dma_size);
    if (!virtq_dma_alloc(total_dma_size, 4096, &dma_virt, &dma_phys, &dma_pages)) {
        return nullptr;
    }
    virtio_pci_diag_trace_queue(dev, queue_index, "queue allocation");

    virtio_pci_input_init_trace(dev, "queue DMA allocate done", dma_phys);
    // 6. Alokacja obiektu zarzadzania i tablic sledzenia
    virtio_pci_input_init_trace(dev, "queue heap allocate begin");
    VirtQueue* vq = static_cast<VirtQueue*>(kernel_kmalloc(sizeof(VirtQueue)));
    if (!vq) {
        virtq_dma_free(dma_virt, dma_phys, dma_pages);
        return nullptr;
    }
    *vq = {};

    virtio_pci_input_init_trace(dev, "queue descriptor state allocate begin", qsize);
    vq->desc_state = static_cast<VirtQueueDescState*>(kernel_kmalloc(sizeof(VirtQueueDescState) * qsize));
    virtio_pci_input_init_trace(dev, "queue descriptor state allocate done");
    if (!vq->desc_state) {
        kernel_kfree(vq);
        virtq_dma_free(dma_virt, dma_phys, dma_pages);
        return nullptr;
    }

    virtio_pci_input_init_trace(dev, "queue free list allocate begin", qsize);
    vq->free_desc_next = static_cast<uint16_t*>(kernel_kmalloc(sizeof(uint16_t) * qsize));
    virtio_pci_input_init_trace(dev, "queue free list allocate done");
    if (!vq->free_desc_next) {
        kernel_kfree(vq->desc_state);
        kernel_kfree(vq);
        virtq_dma_free(dma_virt, dma_phys, dma_pages);
        return nullptr;
    }

    virtio_pci_input_init_trace(dev, "queue heap allocate done");
    vq->dev = dev;
    vq->queue_index = queue_index;
    vq->queue_size = qsize;
    vq->dma_virtual = dma_virt;
    vq->dma_physical = dma_phys;
    vq->dma_size = total_dma_size;
    vq->dma_pages = dma_pages;

    // Wskazniki wirtualne do struktur pierscienia
    uint8_t* base_virt = static_cast<uint8_t*>(dma_virt);
    vq->desc = reinterpret_cast<volatile VirtqDesc*>(base_virt);
    vq->avail = reinterpret_cast<volatile VirtqAvail*>(base_virt + avail_offset);
    vq->used = reinterpret_cast<volatile VirtqUsed*>(base_virt + used_offset);

    // Fizyczne 64-bitowe adresy DMA
    vq->desc_phys = dma_phys;
    vq->driver_phys = dma_phys + avail_offset;
    vq->device_phys = dma_phys + used_offset;

    // Inicjalizacja listy wolnych deskryptorow
    vq->free_head = 0;
    vq->num_free = qsize;
    for (uint16_t i = 0; i < qsize; ++i) {
        vq->free_desc_next[i] = static_cast<uint16_t>(i + 1);
        vq->desc_state[i] = {};
    }

    vq->last_used_idx = 0;

    // 7. Weryfikacja adresu powiadomien w warstwie transportowej
    virtio_pci_input_init_trace(dev, "queue notify offset read begin");
    vq->queue_notify_off = virtio_pci_get_queue_notify_off(dev);
    virtio_pci_input_init_trace(dev, "queue notify offset read done", vq->queue_notify_off);
    volatile uint16_t* notify_addr = virtio_pci_notify_address(dev, vq->queue_notify_off);
    virtio_pci_input_init_trace(dev, "queue notify address", reinterpret_cast<uintptr_t>(notify_addr));
    if (!notify_addr) {
        kernel_kfree(vq->free_desc_next);
        kernel_kfree(vq->desc_state);
        kernel_kfree(vq);
        virtq_dma_free(dma_virt, dma_phys, dma_pages);
        return nullptr;
    }

    // 8. Programowanie rejestrow transportu PCI
    virtio_pci_input_init_trace(dev, "queue size store begin", qsize);
    virtio_pci_set_queue_size(dev, qsize);
    virtio_pci_input_init_trace(dev, "queue size store done");
    virtio_pci_diag_trace_queue(dev, queue_index, "queue physical addresses");
    virtio_pci_input_init_trace(dev, "queue descriptor address store begin", vq->desc_phys);
    virtio_pci_set_queue_desc(dev, vq->desc_phys);
    virtio_pci_input_init_trace(dev, "queue descriptor address store done");
    virtio_pci_input_init_trace(dev, "queue available address store begin", vq->driver_phys);
    virtio_pci_set_queue_driver(dev, vq->driver_phys);
    virtio_pci_input_init_trace(dev, "queue available address store done");
    virtio_pci_input_init_trace(dev, "queue used address store begin", vq->device_phys);
    virtio_pci_set_queue_device(dev, vq->device_phys);
    virtio_pci_input_init_trace(dev, "queue used address store done");

    // 9. Wlaczenie kolejki i weryfikacja
    virtio_pci_diag_trace_queue(dev, queue_index, "queue enable");
    virtio_pci_input_init_trace(dev, "queue enable store begin");
    virtio_pci_set_queue_enable(dev, 1);
    virtio_pci_input_init_trace(dev, "queue enable store done");
    virtio_pci_input_init_trace(dev, "queue enable readback begin");
    const uint16_t confirmed_enable = virtio_pci_get_queue_enable(dev);
    virtio_pci_input_init_trace(dev, "queue enable readback done", confirmed_enable);
    if (confirmed_enable != 1) {
        // Rollback w razie odmowy wlaczenia (VirtIO 1.0 sec 4.1.4.3: MUST NOT write 0 to queue_enable)
        virtio_pci_set_queue_desc(dev, 0);
        virtio_pci_set_queue_driver(dev, 0);
        virtio_pci_set_queue_device(dev, 0);
        kernel_kfree(vq->free_desc_next);
        kernel_kfree(vq->desc_state);
        kernel_kfree(vq);
        virtq_dma_free(dma_virt, dma_phys, dma_pages);
        return nullptr;
    }

    vq->initialized = true;
    return vq;
}

void virtqueue_destroy(VirtQueue* vq) {
    if (!vq) return;

    if (vq->dev && vq->initialized) {
        virtio_pci_select_queue(vq->dev, vq->queue_index);
        // VirtIO 1.0 sec 4.1.4.3: The driver MUST NOT write a 0 to queue_enable.
        // Zerujemy rejestry adresowe kolejki
        virtio_pci_set_queue_desc(vq->dev, 0);
        virtio_pci_set_queue_driver(vq->dev, 0);
        virtio_pci_set_queue_device(vq->dev, 0);
    }

    if (vq->dma_virtual) {
        virtq_dma_free(vq->dma_virtual, vq->dma_physical, vq->dma_pages);
    }

    if (vq->free_desc_next) {
        kernel_kfree(vq->free_desc_next);
    }
    if (vq->desc_state) {
        kernel_kfree(vq->desc_state);
    }
    kernel_kfree(vq);
}

int virtqueue_submit(VirtQueue* vq, const VirtqBuffer* bufs, uint32_t num_bufs, void* cookie) {
    if (!vq || !vq->initialized || !bufs || num_bufs == 0 || num_bufs > vq->queue_size) {
        return -1;
    }

    // Regula VirtIO sec 2.6.4: wszystkie bufory device-writable musza nastepowac PO readable
    bool seen_write = false;
    uint32_t total_len = 0;
    for (uint32_t i = 0; i < num_bufs; ++i) {
        if (bufs[i].is_write) {
            seen_write = true;
        } else if (seen_write) {
            return -1; // Readable po writable jest zabronione
        }
        if (bufs[i].phys_addr == 0 && bufs[i].len > 0) {
            return -3; // Bledny adres fizyczny
        }
        if (bufs[i].len > UINT32_MAX - total_len) {
            return -1; // Przepelnienie arytmetyczne lacznej dlugosci
        }
        total_len += bufs[i].len;
    }

    // Sprawdzenie wyczerpania deskryptorow
    if (vq->num_free < num_bufs) {
        return -2;
    }

    // Validate the entire allocation before changing ownership. A damaged
    // free list must never become an unchecked descriptor-table subscript.
    if (vq->num_free > vq->queue_size || vq->outstanding_heads >= vq->queue_size)
        return -4;
    if (++vq->visit_sequence == 0) {
        for (uint16_t i = 0; i < vq->queue_size; ++i) vq->desc_state[i].visit = 0;
        ++vq->visit_sequence;
    }
    uint16_t check = vq->free_head;
    for (uint32_t i = 0; i < num_bufs; ++i) {
        if (check >= vq->queue_size || vq->desc_state[check].is_allocated ||
            vq->desc_state[check].visit == vq->visit_sequence) return -4;
        vq->desc_state[check].visit = vq->visit_sequence;
        check = vq->free_desc_next[check];
    }
    if ((vq->num_free == num_bufs && check != vq->queue_size) ||
        (vq->num_free > num_bufs && (check >= vq->queue_size ||
         vq->desc_state[check].is_allocated ||
         vq->desc_state[check].visit == vq->visit_sequence))) return -4;

    // Przydzial deskryptorow z listy wolnych
    const uint16_t head = vq->free_head;
    uint16_t curr = head;

    for (uint32_t i = 0; i < num_bufs; ++i) {
        const uint16_t desc_idx = curr;
        const uint16_t next_free = vq->free_desc_next[curr];

        vq->desc_state[desc_idx].is_allocated = true;
        vq->desc_state[desc_idx].is_chain_head = (i == 0);
        vq->desc_state[desc_idx].cookie = (i == 0) ? cookie : nullptr;
        vq->desc_state[desc_idx].chain_len = (i == 0) ? static_cast<uint16_t>(num_bufs) : 0;
        vq->desc_state[desc_idx].total_buf_len = (i == 0) ? total_len : 0;

        uint16_t flags = 0;
        if (bufs[i].is_write) {
            flags |= VRING_DESC_F_WRITE;
        }
        if (i + 1 < num_bufs) {
            flags |= VRING_DESC_F_NEXT;
        }

        vq->desc[desc_idx].addr = bufs[i].phys_addr;
        vq->desc[desc_idx].len = bufs[i].len;
        vq->desc[desc_idx].flags = flags;
        vq->desc[desc_idx].next = (i + 1 < num_bufs) ? next_free : 0;
        vq->desc_state[desc_idx].owner = head;
        vq->desc_state[desc_idx].next = vq->desc[desc_idx].next;
        vq->desc_state[desc_idx].flags = flags;
        vq->desc_state[desc_idx].buffer_addr = bufs[i].phys_addr;
        vq->desc_state[desc_idx].buffer_len = bufs[i].len;

        curr = next_free;
    }

    vq->free_head = curr;
    vq->num_free -= static_cast<uint16_t>(num_bufs);
    ++vq->outstanding_heads;

    // Bariera Store-Store: upewniamy sie, ze tablica deskryptorow jest zapisana przed ringiem
    virtqueue_wmb();

    // Publikacja indeksu w Available Ring
    const uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx & (vq->queue_size - 1U)] = head;

    // Bariera Store-Store: upewniamy sie, ze wpis w ring[] jest widoczny przed zwiekszeniem avail->idx
    virtqueue_wmb();

    vq->avail->idx = static_cast<uint16_t>(avail_idx + 1U);

    return static_cast<int>(head);
}

bool virtqueue_kick(VirtQueue* vq, bool force) {
    if (!vq || !vq->initialized) return false;

    // Bariera Store-Load: zapis avail->idx musi byc zatwierdzony przed odczytem used->flags
    virtqueue_mb();

    const uint16_t flags = vq->used->flags;
    const bool notify_suppressed = (flags & VRING_USED_F_NO_NOTIFY) != 0;

    if (force || !notify_suppressed) {
        virtio_pci_notify_queue(vq->dev, vq->queue_index, vq->queue_notify_off);
        return true;
    }
    return false;
}

bool virtqueue_has_used(const VirtQueue* vq) {
    if (!vq || !vq->initialized || !vq->used) return false;
    virtqueue_rmb();
    return vq->last_used_idx != vq->used->idx;
}

int virtqueue_pop_used(VirtQueue* vq, void** out_cookie, uint32_t* out_len) {
    if (!vq || !vq->initialized || !vq->used) return -1;

    const uint16_t used_idx = vq->used->idx;
    if (vq->last_used_idx == used_idx) {
        return -1; // Brak nowych zakonczen
    }
    // The acquire barrier belongs AFTER observing the published used index.
    virtqueue_rmb();
    const uint16_t pending = static_cast<uint16_t>(used_idx - vq->last_used_idx);
    if (pending > vq->queue_size || pending > vq->outstanding_heads) return -4;

    const uint16_t ring_idx = vq->last_used_idx & (vq->queue_size - 1U);
    const uint32_t id = vq->used->ring[ring_idx].id;
    const uint32_t len = vq->used->ring[ring_idx].len;

    // Walidacja ID zgloszonego przez kontroler
    if (id >= vq->queue_size) {
        return -2; // Identyfikator poza zakresem kolejki
    }

    // Weryfikacja: deskryptor musi byc zaalokowany i byc glowa lancucha
    if (!vq->desc_state[id].is_allocated || !vq->desc_state[id].is_chain_head) {
        return -3; // Podwojne zakonczenie lub falszywa glowa
    }

    // Weryfikacja dlugosci zapisanej przez urzadzenie
    if (len > vq->desc_state[id].total_buf_len) {
        return -4; // Urzadzenie zapisalo wiecej niz dlugosc bufora
    }

    const uint16_t expected_chain_len = vq->desc_state[id].chain_len;
    if (expected_chain_len == 0 || expected_chain_len > vq->queue_size) {
        return -4; // Uszkodzony stan dlugosci lancucha
    }

    void* const cookie = vq->desc_state[id].cookie;

    // Validate every link before freeing ANY link. Keep trusted chain metadata
    // outside DMA so a malformed completion cannot splice another request.
    uint16_t curr = static_cast<uint16_t>(id);
    if (vq->num_free > vq->queue_size - expected_chain_len) return -4;
    for (uint16_t i = 0; i < expected_chain_len; ++i) {
        if (curr >= vq->queue_size) return -4;
        const auto& state = vq->desc_state[curr];
        const bool next_required = (i + 1U < expected_chain_len);
        if (!state.is_allocated || state.owner != id ||
            state.is_chain_head != (i == 0) ||
            ((state.flags & VRING_DESC_F_NEXT) != 0) != next_required ||
            (state.flags & ~(VRING_DESC_F_NEXT | VRING_DESC_F_WRITE)) != 0 ||
            vq->desc[curr].next != state.next || vq->desc[curr].flags != state.flags ||
            vq->desc[curr].addr != state.buffer_addr || vq->desc[curr].len != state.buffer_len)
            return -4;
        curr = state.next;
    }

    curr = static_cast<uint16_t>(id);
    uint16_t freed_count = 0;

    while (freed_count < expected_chain_len) {
        if (curr >= vq->queue_size || !vq->desc_state[curr].is_allocated) {
            return -4; // Niespojnosc lancucha deskryptorow
        }

        const uint16_t desc_flags = vq->desc[curr].flags;
        const uint16_t next = vq->desc[curr].next;

        // Czyszczenie stanu
        vq->desc_state[curr].is_allocated = false;
        vq->desc_state[curr].is_chain_head = false;
        vq->desc_state[curr].cookie = nullptr;
        vq->desc_state[curr].chain_len = 0;
        vq->desc_state[curr].total_buf_len = 0;

        // Zwrot do puli wolnych
        vq->free_desc_next[curr] = vq->free_head;
        vq->free_head = curr;
        vq->num_free++;
        freed_count++;

        if (desc_flags & VRING_DESC_F_NEXT) {
            curr = next;
        } else {
            break;
        }
    }

    if (freed_count != expected_chain_len) {
        return -4; // Lancuch zakonczyl sie przedwczesnie
    }

    // Bariera spojnosci DMA: upewniamy sie, ze dane bufora zapisane przez DMA sa widoczne dla CPU
    virtqueue_mb();

    vq->last_used_idx = static_cast<uint16_t>(vq->last_used_idx + 1U);
    --vq->outstanding_heads;

    if (out_cookie) *out_cookie = cookie;
    if (out_len) *out_len = len;

    return static_cast<int>(id);
}

uint16_t virtqueue_get_size(const VirtQueue* vq) {
    if (!vq) return 0;
    return vq->queue_size;
}

uint16_t virtqueue_get_num_free(const VirtQueue* vq) {
    if (!vq) return 0;
    return vq->num_free;
}
