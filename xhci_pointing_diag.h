#pragma once
#include <stdint.h>
#include "input_device.h"
#include "pointing_diag.h"
#ifndef BURSZTYNDIAGXHCIPOINTING
#define BURSZTYNDIAGXHCIPOINTING 0
#endif
struct PointingCounters {
    uint32_t ps2_irq{},ps2_bytes{},ps2_packets{},ps2_published{};
    uint32_t owner_transitions{},owner_accepted{},nonowner_ignored{};
    uint32_t xhci_irq{},events{},transfers{},commands{},bad_completions{};
    uint32_t hid_completions{},hid_accepted{},hid_rejected{},proven{};
    uint32_t gui_consumed{},cursor_commits{};
    uint32_t ps2_irq_at_activation{};
    bool xhci_active{},ps2_after_seen{},ps2_drop_seen{};
};
inline PointingCounters pointing_counters{};
inline void pointing_count(uint32_t& counter) {
    uint32_t old=__atomic_load_n(&counter,__ATOMIC_RELAXED);
    while(old!=UINT32_MAX && !__atomic_compare_exchange_n(&counter,&old,old+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
}
inline uint32_t pointing_read(const uint32_t& counter) {
    return __atomic_load_n(&counter,__ATOMIC_RELAXED);
}
// Deferred context only. IRQ handlers only increment counters.
inline void pointing_snapshot(const char* reason) {
#if BURSZTYNDIAGXHCIPOINTING
    static unsigned snapshots=0;
    if(snapshots>=32)return;
    ++snapshots;
    const auto& c=pointing_counters;
    const InputDevice* owner=input_get_pointer_owner();
    int64_t stamp=0;
#ifndef BURSZTYN_VIRTIO_HOST_TEST
    extern uint64_t czas_monotoniczny_ns();
    stamp=int64_t(czas_monotoniczny_ns());
#endif
    const int64_t v[]={stamp,owner?int64_t(owner->device_id):-1,owner?owner->bus_type:0,
        pointing_read(c.ps2_irq),pointing_read(c.ps2_bytes),pointing_read(c.ps2_packets),pointing_read(c.ps2_published),pointing_read(c.owner_transitions),
        pointing_read(c.owner_accepted),pointing_read(c.nonowner_ignored),pointing_read(c.xhci_irq),pointing_read(c.events),pointing_read(c.transfers),pointing_read(c.commands),
        pointing_read(c.bad_completions),pointing_read(c.hid_completions),pointing_read(c.hid_accepted),pointing_read(c.hid_rejected),pointing_read(c.proven),
        pointing_read(c.gui_consumed),pointing_read(c.cursor_commits)};
    pointing_diag(reason,v,sizeof(v)/sizeof(v[0]));
    for(size_t i=0;i<16;++i) {
        const auto* d=input_get_device(i); if(!d)continue;
        const int64_t state[]={d->device_id,d->bus_type,d->pointer_state};
        pointing_diag("[POINTING state] id/bus/state",state,3);
    }
#else
    (void)reason;
#endif
}
