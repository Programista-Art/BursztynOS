#pragma once
#include "skladacz_obrazu.h"
#ifndef BURSZTYNDIAGCURSORPERF
#define BURSZTYNDIAGCURSORPERF 0
#endif
#if BURSZTYNDIAGCURSORPERF
#include "sterowniki/czas/hpet.h"
extern void klog_serial(const char*);
struct KursorDiagSample {
    uint64_t input=0,logical=0,wm=0,queued=0,begin=0,restore=0,draw=0,present=0,end=0;
    uint64_t coalesced=0,area=0,present_area=0;
    uint32_t count=0,presents=0;
    bool full=false,decor=false,scene=false,overwritten=false,composing=false;
};
inline KursorDiagSample kursor_diag_producer{},kursor_diag_pending{},kursor_diag_active{};
inline uint64_t kursor_diag_total_coalesced=0,kursor_diag_total_dropped=0;
inline void kursor_diag_drop(){++kursor_diag_total_dropped;}
inline void kursor_diag_retry(){kursor_diag_pending=kursor_diag_active;kursor_diag_active={};}
inline uint64_t kursor_diag_now(){return hpet_dostepny()?czas_monotoniczny_ns():0;}
inline void kursor_diag_input(){if(!kursor_diag_producer.input)kursor_diag_producer.input=kursor_diag_now();}
inline void kursor_diag_wm(){kursor_diag_producer.wm=kursor_diag_now();}
inline void kursor_diag_logical(){kursor_diag_producer.logical=kursor_diag_now();}
// These queue/consume calls execute inside compositor IRQ exclusion.
inline void kursor_diag_queue(bool overwritten){
    if(!kursor_diag_producer.wm)kursor_diag_producer.wm=kursor_diag_now();
    kursor_diag_producer.queued=kursor_diag_now();
    if(overwritten)++kursor_diag_total_coalesced;
    kursor_diag_producer.coalesced=kursor_diag_total_coalesced;
    kursor_diag_producer.overwritten=overwritten;
    if(!kursor_diag_producer.input&&overwritten){
        kursor_diag_producer.input=kursor_diag_pending.input;
        kursor_diag_producer.logical=kursor_diag_pending.logical;
    }
    kursor_diag_pending=kursor_diag_producer;kursor_diag_producer={};
}
inline void kursor_diag_consume(const GuiDirtyRect* r,uint32_t count){
    kursor_diag_active=kursor_diag_pending;kursor_diag_pending={};
    auto& d=kursor_diag_active;d.begin=kursor_diag_now();d.count=count;d.scene=count!=0;
    extern int grafika_pobierz_szerokosc();extern int grafika_pobierz_wysokosc();
    for(uint32_t i=0;i<count;++i){d.area+=uint64_t(r[i].width)*r[i].height;
        if(r[i].x==0&&r[i].y==0&&r[i].width==grafika_pobierz_szerokosc()&&r[i].height==grafika_pobierz_wysokosc())d.full=true;}
}
inline void kursor_diag_compose(){kursor_diag_active.composing=true;}
inline void kursor_diag_decor(){kursor_diag_active.decor=true;}
inline void kursor_diag_stage(int s){auto& d=kursor_diag_active;auto now=kursor_diag_now();
    if(s==0)d.restore=now;else if(s==1)d.draw=now;else if(s==2)d.present=now;else d.end=now;}
inline void kursor_diag_present(GuiDirtyRect r){++kursor_diag_active.presents;kursor_diag_active.present_area+=uint64_t(r.width)*r.height;}
inline void kursor_diag_finish(){
    const auto d=kursor_diag_active;kursor_diag_active={};
    if(!d.input||d.end<d.input||d.end-d.input<25000000ULL)return;
    // At most one anomaly per second. No packet logging, bounded serial cost.
    static uint64_t last=0;if(last&&d.end-last<1000000000ULL)return;last=d.end;
    char buf[768]{};unsigned n=0;
    auto text=[&](const char* s){while(*s&&n+1<sizeof(buf))buf[n++]=*s++;};
    auto num=[&](uint64_t v){char tmp[24];unsigned i=0;do{tmp[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(buf))buf[n++]=tmp[--i];};
    auto field=[&](const char* key,uint64_t v){text(key);num(v);text(" ");};
    text("[CURSOR PERF] ns ");field("input=",d.input);field("logical=",d.logical);
    field("wm_enqueued=",d.wm);field("queued=",d.queued);field("begin=",d.begin);
    field("restore=",d.restore);field("draw=",d.draw);field("present=",d.present);field("end=",d.end);
    field("latency_us=",(d.end-d.input)/1000);field("rects=",d.count);field("area=",d.area);
    field("full_pending=",d.full);field("composing=",d.composing);field("decor=",d.decor);field("scene=",d.scene);
    field("overwritten=",d.overwritten);field("coalesced_total=",d.coalesced);field("rejected_total=",kursor_diag_total_dropped);field("presents=",d.presents);field("present_area=",d.present_area);
    text("\n");klog_serial(buf);
}
#else
inline void kursor_diag_drop(){}
inline void kursor_diag_retry(){}
inline void kursor_diag_input(){}
inline void kursor_diag_logical(){}
inline void kursor_diag_wm(){}
inline void kursor_diag_queue(bool){}
inline void kursor_diag_consume(const GuiDirtyRect*,uint32_t){}
inline void kursor_diag_compose(){}
inline void kursor_diag_decor(){}
inline void kursor_diag_stage(int){}
inline void kursor_diag_present(GuiDirtyRect){}
inline void kursor_diag_finish(){}
#endif
