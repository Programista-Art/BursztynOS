#pragma once
#include <stdint.h>
#include "sterowniki/usb/xhci_trb.h"

// xHCI 1.1: HCSPARAMS2, HCCPARAMS1 and USB Legacy Support.
constexpr uint32_t xhci_scratchpad_count(uint32_t hcs2) {
    return (((hcs2 >> 21) & 31U) << 5) | ((hcs2 >> 27) & 31U);
}
constexpr uint8_t xhci_context_bytes(uint32_t hcc) { return hcc & 4U ? 64 : 32; }
constexpr uint32_t xhci_legacy_os_owned(uint32_t original) { return original | (1U << 24); }
constexpr uint32_t xhci_legacy_disable_smi(uint32_t original) {
    // Preserve reserved fields 1:3,5:12,17:19; clear SMI enables,
    // write zero to reserved 21:28, acknowledge only defined RW1C events 29:31.
    return (original & ((7U << 1) | (255U << 5) | (7U << 17))) | (7U << 29);
}
inline void xhci_control_trbs(const uint8_t* setup,uint64_t physical,uint16_t length,
                             XhciTrb& s,XhciTrb& data,XhciTrb& status) {
    s={};data={};status={};
    for(unsigned i=0;i<4;++i){s.dword0|=uint32_t(setup[i])<<(8*i);s.dword1|=uint32_t(setup[i+4])<<(8*i);}
    const bool in=setup[0]&0x80;
    s.dword2=8;
    // Chain is reserved in Setup and Status Stage TRBs. Each control stage is
    // a separate TD; the last (single) Data Stage TRB must not chain to Status.
    s.dword3=xhci_trb::type_field(xhci_trb::TYPE_SETUP_STAGE)|(1U<<6)|((length?(in?3U:2U):0U)<<16);
    if(length){data.dword0=uint32_t(physical);data.dword1=uint32_t(physical>>32);data.dword2=length;
        data.dword3=xhci_trb::type_field(xhci_trb::TYPE_DATA_STAGE)|(in?((1U<<16)|(1U<<2)):0U);}
    status.dword3=xhci_trb::type_field(xhci_trb::TYPE_STATUS_STAGE)|(1U<<5)|((length&&in)?0U:(1U<<16));
}
