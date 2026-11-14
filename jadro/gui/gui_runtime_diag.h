#pragma once
#include <stdint.h>
#ifndef BURSZTYNDIAGGUIRUNTIME
#define BURSZTYNDIAGGUIRUNTIME 0
#endif
// Global metrics are attributed to PID 0; application requests retain their PID.
enum GuiPerfMetric { GP_CPU_NS,GP_IDLE_NS,GP_WAKE,GP_WAIT,GP_BWS,GP_MOVE,GP_TIMER,GP_DIRTY,
    GP_PRESENT,GP_CURSOR,GP_COPY,GP_PRESENT_NS,GP_PRESENT_MAX_NS,
    GP_LOCK_NS,GP_LOCK_MAX_NS,GP_COMPOSE_NS,GP_COPY_NS,GP_GPU_NS,GP_GPU_MAX_NS,GP_PRECISE,
    GP_BWS_REFRESH,GP_FRAME_PRESENT,GP_FRAME_INTERVAL_NS,GP_FRAME_INTERVAL_MAX_NS,
    GP_INPUT_PRESENT_NS,GP_INPUT_PRESENT_MAX_NS,
    // Timer ticks that could not switch away from a Ring 0 syscall while another
    // process was ready, and the longest uninterrupted run of such ticks.
    GP_DEFERRED_TICKS,GP_DEFERRED_RUN_MAX_NS,
    // Compositions that read a layer whose owner was mid-draw (torn frames).
    GP_TORN_COMPOSE,
    // Compositions served from the layer shadow (a torn frame prevented).
    GP_SHADOW_COMPOSE,
    // Cooperative crypto yield points: HLT yields performed / skipped (interrupts masked).
    GP_YIELD_HLT,GP_YIELD_SKIP,
    // Cooperative yields between bands of a large BWS57 blit.
    GP_SYSCALL_YIELD,GP_METRICS };
#if BURSZTYNDIAGGUIRUNTIME
#include "scheduler.h"
#include "sterowniki/czas/hpet.h"
extern void klog_serial(const char*);
inline uint64_t gui_perf_counts[MAKS_PROCESOW][GP_METRICS]{};
inline int gui_perf_running=-1;
inline uint64_t gui_perf_run_start=0;
inline bool gui_perf_runnable=false;
inline uint64_t gui_perf_idle_start=0;
inline bool gui_perf_idle_active=false;
inline uint64_t gui_perf_last_frame_ns=0;
inline uint64_t gui_perf_last_input_ns=0;
inline uint64_t gui_perf_now(){return hpet_dostepny()?czas_monotoniczny_ns():0;}
inline void gui_perf_add(int pid,GuiPerfMetric m,uint64_t n=1){
    if(pid>=0&&pid<int(MAKS_PROCESOW))__atomic_add_fetch(&gui_perf_counts[pid][m],n,__ATOMIC_RELAXED);
}
inline void gui_perf_max(int pid,GuiPerfMetric m,uint64_t n){
    if(pid<0||pid>=int(MAKS_PROCESOW))return;
    auto* p=&gui_perf_counts[pid][m];auto old=__atomic_load_n(p,__ATOMIC_RELAXED);
    while(n>old&&!__atomic_compare_exchange_n(p,&old,n,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
}
inline void gui_perf_charge(){auto now=gui_perf_now();
    if(gui_perf_runnable&&gui_perf_run_start&&now>=gui_perf_run_start)
        gui_perf_add(gui_perf_running,GP_CPU_NS,now-gui_perf_run_start);
    gui_perf_run_start=now;
}
inline void gui_perf_close_idle(uint64_t now) {
    if (!gui_perf_idle_active) return;
    if (gui_perf_idle_start && now >= gui_perf_idle_start)
        gui_perf_add(0, GP_IDLE_NS, now - gui_perf_idle_start);
    gui_perf_idle_start = 0;
    gui_perf_idle_active = false;
    gui_perf_runnable = false;
    gui_perf_run_start = now;
}
inline uint64_t gui_perf_deferred_start=0;
inline uint64_t gui_perf_deferred_by_nr[256]{};
inline void gui_perf_deferred_tick(bool other_ready,uint64_t syscall_nr=0){
    if(!other_ready){gui_perf_deferred_start=0;return;}
    __atomic_add_fetch(&gui_perf_deferred_by_nr[syscall_nr&255U],1ULL,__ATOMIC_RELAXED);
    const uint64_t now=gui_perf_now();if(!now)return;
    gui_perf_add(0,GP_DEFERRED_TICKS);
    if(!gui_perf_deferred_start)gui_perf_deferred_start=now;
    gui_perf_max(0,GP_DEFERRED_RUN_MAX_NS,now-gui_perf_deferred_start);
}
inline void gui_perf_switch(int pid){
    gui_perf_deferred_start=0;
    const uint64_t now=gui_perf_now();
    gui_perf_close_idle(now);
    gui_perf_charge();
    gui_perf_running=pid;
    gui_perf_runnable=true;
}
inline void gui_perf_block(int pid){if(pid==gui_perf_running){gui_perf_charge();gui_perf_runnable=false;}}
inline void gui_perf_idle_begin(){
    const uint64_t now=gui_perf_now();
    if(!now||gui_perf_idle_active)return;
    gui_perf_charge();
    gui_perf_idle_start=now;
    gui_perf_idle_active=true;
    gui_perf_runnable=false;
}
inline void gui_perf_idle_end(){
    const uint64_t now=gui_perf_now();
    if(!gui_perf_idle_active)return;
    gui_perf_close_idle(now);
    gui_perf_running=0;
    gui_perf_runnable=true;
    gui_perf_run_start=now;
}
inline void gui_perf_mark_input(){
    const uint64_t now=gui_perf_now();
    if(now)__atomic_store_n(&gui_perf_last_input_ns,now,__ATOMIC_RELAXED);
}
inline void gui_perf_frame_presented(){
    const uint64_t now=gui_perf_now();
    if(!now)return;
    const uint64_t previous=__atomic_exchange_n(&gui_perf_last_frame_ns,now,__ATOMIC_RELAXED);
    gui_perf_add(0,GP_FRAME_PRESENT);
    if(previous&&now>=previous){
        const uint64_t interval=now-previous;
        gui_perf_add(0,GP_FRAME_INTERVAL_NS,interval);
        gui_perf_max(0,GP_FRAME_INTERVAL_MAX_NS,interval);
    }
    const uint64_t input=__atomic_exchange_n(&gui_perf_last_input_ns,0,__ATOMIC_RELAXED);
    if(input&&now>=input){
        const uint64_t latency=now-input;
        gui_perf_add(0,GP_INPUT_PRESENT_NS,latency);
        gui_perf_max(0,GP_INPUT_PRESENT_MAX_NS,latency);
    }
}
inline void gui_perf_report(){
    static uint64_t previous=0;auto now=gui_perf_now();
    if(!now)return;
    if(!previous){previous=now;return;}
    if(now-previous<1000000000ULL)return;
    const auto interval=now-previous;previous=now;
    const char* names[]={"cpu_ns","idle_ns","wake","wait","bws_gui","move_requested","timer_requested","dirty",
        "present","cursor_present","copy","present_ns","present_max_ns","lock_wait_ns","lock_wait_max_ns",
        "compose_ns","copy_ns","gpu_ns","gpu_max_ns","precise_requested","bws_refresh","frame_present",
        "frame_interval_ns","frame_interval_max_ns","input_present_ns","input_present_max_ns",
        "deferred_ticks","deferred_run_max_ns","torn_compose","shadow_compose","yield_hlt","yield_skip","syscall_yield"};
    for(unsigned pid=0;pid<MAKS_PROCESOW;++pid){
        uint64_t sample[GP_METRICS]{};bool nonzero=false;
        for(unsigned i=0;i<GP_METRICS;++i){sample[i]=__atomic_exchange_n(&gui_perf_counts[pid][i],0,__ATOMIC_RELAXED);nonzero|=sample[i]!=0;}
        if(!nonzero&&tablica_procesow[pid].stan==PROCES_PUSTY)continue;
        char buf[768]{};unsigned n=0;
        auto text=[&](const char* s){while(*s&&n+1<sizeof(buf))buf[n++]=*s++;};
        auto num=[&](uint64_t v){char tmp[24];unsigned i=0;do{tmp[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(buf))buf[n++]=tmp[--i];};
        text("[GUI PERF] interval_ns=");num(interval);text(" pid=");num(pid);text(" path=");
        for(unsigned i=0;i<sizeof(tablica_procesow[pid].sciezka_pliku)&&tablica_procesow[pid].sciezka_pliku[i];++i){if(n+1<sizeof(buf))buf[n++]=tablica_procesow[pid].sciezka_pliku[i];}
        for(unsigned i=0;i<GP_METRICS;++i){text(" ");text(names[i]);text("=");num(sample[i]);}
        text("\n");klog_serial(buf);
    }
    {   // Which syscall was running when ticks could not preempt it (top 4).
        uint64_t top[4]{},nrs[4]{};
        for(unsigned nr=0;nr<256;++nr){
            uint64_t v=__atomic_exchange_n(&gui_perf_deferred_by_nr[nr],0,__ATOMIC_RELAXED);
            for(unsigned k=0;k<4;++k)if(v>top[k]){for(unsigned j=3;j>k;--j){top[j]=top[j-1];nrs[j]=nrs[j-1];}top[k]=v;nrs[k]=nr;break;}
        }
        if(top[0]){
            char buf[160]{};unsigned n=0;
            auto text=[&](const char* s){while(*s&&n+1<sizeof(buf))buf[n++]=*s++;};
            auto num=[&](uint64_t v){char tmp[24];unsigned i=0;do{tmp[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(buf))buf[n++]=tmp[--i];};
            text("[GUI PERF] deferred_by_syscall");
            for(unsigned k=0;k<4&&top[k];++k){text(" bws");num(nrs[k]);text("=");num(top[k]);}
            text("\n");klog_serial(buf);
        }
    }
}
#else
inline uint64_t gui_perf_now(){return 0;}
inline void gui_perf_add(int,GuiPerfMetric,uint64_t=1){}
inline void gui_perf_max(int,GuiPerfMetric,uint64_t){}
inline void gui_perf_switch(int){}
inline void gui_perf_block(int){}
inline void gui_perf_idle_begin(){}
inline void gui_perf_idle_end(){}
inline void gui_perf_mark_input(){}
inline void gui_perf_deferred_tick(bool,uint64_t=0){}
inline void gui_perf_frame_presented(){}
inline void gui_perf_report(){}
#endif
