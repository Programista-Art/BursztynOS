#include "sterowniki/usb/xhci_ring.h"

bool xhci_command_ring_create(XhciProducerRing* ring, uint16_t trb_count) {
    if (!ring || trb_count < 16U || trb_count > 256U) return false;
    *ring = {};
    const size_t bytes = static_cast<size_t>(trb_count) * sizeof(XhciTrb);
    if (!dma_allocate(bytes, 64U, &ring->dma)) return false;
    ring->trbs = static_cast<XhciTrb*>(ring->dma.virtual_address);
    ring->trb_count = trb_count;
    ring->producer_cycle = true;
    XhciTrb& link = ring->trbs[trb_count - 1U];
    link.dword0 = static_cast<uint32_t>(ring->dma.physical_address);
    link.dword1 = static_cast<uint32_t>(ring->dma.physical_address >> 32);
    link.dword2 = 0;
    link.dword3 = xhci_trb::type_field(xhci_trb::TYPE_LINK) |
                  xhci_trb::TOGGLE_CYCLE | xhci_trb::CYCLE;
    return true;
}

bool xhci_command_ring_enqueue(XhciProducerRing* ring, const XhciTrb& source,
                               uint64_t* physical_trb, bool publish) {
    if (!ring || !ring->trbs || !physical_trb || ring->trb_count<16U || ring->trb_count>256U ||
        ring->producer_index>=ring->trb_count-1U || ring->dma.size<size_t(ring->trb_count)*sizeof(XhciTrb) ||
        ring->outstanding >= static_cast<uint16_t>(ring->trb_count - 2U)) return false;
    XhciTrb trb = source;
    trb.dword3 = (trb.dword3 & ~xhci_trb::CYCLE) |
                 (ring->producer_cycle ? xhci_trb::CYCLE : 0U);
    const uint16_t index = ring->producer_index;
    ring->trbs[index].dword0 = trb.dword0;
    ring->trbs[index].dword1 = trb.dword1;
    ring->trbs[index].dword2 = trb.dword2;
    const bool wraps=index==ring->trb_count-2U;
    if(wraps) {
        // Publish the Link with this traversal's cycle before the preceding TRB
        // becomes visible. Hardware may follow the Link immediately.
        XhciTrb& link=ring->trbs[ring->trb_count-1U];
        link.dword3=xhci_trb::type_field(xhci_trb::TYPE_LINK) |
            xhci_trb::TOGGLE_CYCLE | (ring->producer_cycle?xhci_trb::CYCLE:0U);
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
#ifdef BURSZTYN_XHCI_HOST_TEST
    extern void xhci_host_before_publish(const XhciProducerRing*,uint16_t,const XhciTrb&);
    xhci_host_before_publish(ring,index,trb);
#endif
    // A multi-stage control request publishes its first TRB only after Status.
    if(publish)__atomic_store_n(&ring->trbs[index].dword3,trb.dword3,__ATOMIC_RELEASE);
    *physical_trb = ring->dma.physical_address +
                    static_cast<uint64_t>(index) * sizeof(XhciTrb);
    ++ring->producer_index;
    ++ring->outstanding;
    if(wraps) {
        ring->producer_index=0;
        ring->producer_cycle=!ring->producer_cycle;
    }
    return true;
}

void xhci_command_ring_complete(XhciProducerRing* ring) {
    if (ring && ring->outstanding != 0) --ring->outstanding;
}

void xhci_ring_complete(XhciProducerRing* ring, uint16_t count) {
    if (!ring) return;
    ring->outstanding = count >= ring->outstanding
        ? 0U : static_cast<uint16_t>(ring->outstanding - count);
}

void xhci_command_ring_destroy(XhciProducerRing* ring) {
    if (!ring) return;
    dma_release(&ring->dma);
    *ring = {};
}
