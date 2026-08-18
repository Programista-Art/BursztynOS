#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sterowniki/dma.h"
#include "sterowniki/usb/xhci_trb.h"

struct XhciProducerRing {
    DmaBuffer dma;
    XhciTrb* trbs;
    uint16_t trb_count;
    uint16_t producer_index;
    uint16_t outstanding;
    bool producer_cycle;
};

bool xhci_command_ring_create(XhciProducerRing* ring, uint16_t trb_count);
bool xhci_command_ring_enqueue(XhciProducerRing* ring, const XhciTrb& trb,
                               uint64_t* physical_trb);
void xhci_command_ring_complete(XhciProducerRing* ring);
void xhci_ring_complete(XhciProducerRing* ring, uint16_t count);
void xhci_command_ring_destroy(XhciProducerRing* ring);
