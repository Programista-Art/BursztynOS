#pragma once
#include <stdint.h>
#include "sterowniki/czas/hpet.h"
#include "input_coexistence_diag.h"

// The same bounded recorder and clock reads run in every build. Only emission
// is diagnostic. No recorded value participates in hardware/state decisions.
namespace BootTiming {
enum Phase : uint8_t { PciScan, UsbBeforeSplash, Discovery, Legacy, Halt, Reset,
    Cnr, Scratchpads, Dcbaa, CommandRing, EventRing, IrqSetup, PortInspection,
    PortReset, EnableSlot, AddressDevice, Control, ConfigParse, HidSetup,
    FirstReceive, Enumeration, Ps2Init, Command, Count };
struct Record { Phase phase; uint32_t detail; uint64_t start, duration; };
inline Record records[768]{};
inline unsigned recorded=0, emitted=0;
inline uint64_t totals[Count]{}, calls[Count]{};
inline uint64_t entry_ticks=0,framebuffer_ticks=0,publication_ticks=0,splash_ticks=0;
inline uint64_t anchor_ticks=0,anchor_ns=0;
inline bool summary_emitted=false;
inline uint64_t ticks() {
#ifdef BURSZTYN_BOOT_TIMING_HOST_TEST
    extern uint64_t host_ticks();return host_ticks();
#else
    uint32_t lo,hi;asm volatile("lfence; rdtsc":"=a"(lo),"=d"(hi)::"memory");
    return (uint64_t(hi)<<32)|lo;
#endif
}
inline uint64_t now() { return hpet_dostepny()?czas_monotoniczny_ns():0; }
inline void entry(){entry_ticks=ticks();}
inline void framebuffer_ready(){framebuffer_ticks=ticks();}
inline void framebuffer_published(){if(!publication_ticks)publication_ticks=ticks();}
inline void clock_ready(){anchor_ticks=ticks();anchor_ns=now();}
inline void splash_started(){if(!splash_ticks)splash_ticks=ticks();}
inline uint64_t estimated_ns(uint64_t delta,uint64_t tick_span,uint64_t ns_span) {
    return tick_span?uint64_t((__uint128_t(delta)*ns_span)/tick_span):0;
}
struct Scope {
    Phase phase;uint32_t detail;uint64_t start;bool enabled;
    explicit Scope(Phase p,uint32_t d=0,bool active=true):phase(p),detail(d),start(active?now():0),enabled(active){}
    ~Scope(){if(!enabled)return;const uint64_t end=now(),duration=end>=start?end-start:0;
        totals[phase]+=duration;++calls[phase];
        if(recorded<768)records[recorded++]={phase,detail,start,duration};}
};
inline const char* name(Phase p){
    const char* const names[]={"PCI scan","USB synchronous before splash","xHCI discovery","BIOS handoff",
        "controller halt","controller reset","CNR wait","scratchpads","DCBAA","command ring","event ring",
        "IRQ setup","root port inspect","root port reset","Enable Slot","Address Device","control transfer",
        "config parse","HID setup","first receive","port enumeration","PS2 initialization","xHCI command"};
    return names[p];
}
inline void dump(){
#if BURSZTYNDIAGINPUTCOEXISTENCE
    if(!summary_emitted && entry_ticks && framebuffer_ticks && publication_ticks && splash_ticks && anchor_ticks) {
        const uint64_t t=ticks(),n=now();
        const uint64_t dt=t>anchor_ticks?t-anchor_ticks:0,dn=n>=anchor_ns?n-anchor_ns:0;
        const int64_t summary[]={int64_t(estimated_ns(framebuffer_ticks-entry_ticks,dt,dn)),
            int64_t(estimated_ns(publication_ticks-entry_ticks,dt,dn)),
            int64_t(estimated_ns(splash_ticks-publication_ticks,dt,dn)),
            int64_t(estimated_ns(splash_ticks-framebuffer_ticks,dt,dn)),
            int64_t(estimated_ns(splash_ticks-entry_ticks,dt,dn)),int64_t(totals[UsbBeforeSplash])};
        pointing_diag("[BOOT PHASE totals ns; pre-HPET TSC calibrated estimate] entry-to-framebuffer-ready/entry-to-framebuffer-publication/publication-to-splash/framebuffer-ready-to-splash/entry-to-splash/USB-before-splash",summary,6);
        summary_emitted=true;
    }
    while(emitted<recorded){const auto& r=records[emitted++];
        const int64_t values[]={r.detail,int64_t(r.start),int64_t(r.duration)};
        pointing_diag(name(r.phase),values,3);}
#endif
}
}
