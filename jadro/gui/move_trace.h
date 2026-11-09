#pragma once
#include <stdint.h>
#ifndef BURSZTYNDIAGMOVE
#define BURSZTYNDIAGMOVE 0
#endif
#if BURSZTYNDIAGMOVE
#include "virtio_gpu.h"
#include "sterowniki/czas/hpet.h"
extern void wypisz_log(const char*);
namespace MoveTrace {
inline bool active=false;
inline uint64_t start=0,dirty=0,desktop=0,explorer=0,other=0,requests=0,passes=0,presents=0,commands=0,flushes=0;
inline uint64_t gpu(bool flush) { auto* d=virtio_gpu_get_driver(0);return d?(flush?d->flush_count:d->cmd_count):0; }
inline void text(const char* stage,const char* value) {
    char out[640]{};unsigned n=0;
    for(const char* p="[MOVE TRACE] ";*p && n+1<sizeof(out);++p)out[n++]=*p;
    for(const char* p=stage;*p && n+1<sizeof(out);++p)out[n++]=*p;
    out[n++]='=';
    for(const char* p=value;*p && n+1<sizeof(out);++p)out[n++]=*p;
    wypisz_log(out);
}
inline void begin(const char* source,const char* destination) {
    text("source",source);text("destination",destination);
    dirty=desktop=explorer=other=requests=passes=presents=0;commands=gpu(false);flushes=gpu(true);
    start=czas_monotoniczny_ns();active=true;
}
inline void invalidate(bool root,bool is_explorer=false) {if(active){++dirty;if(root)++desktop;else if(is_explorer)++explorer;else ++other;}}
inline void flush_request(){if(active)++requests;}
inline void compose(){if(active)++passes;}
inline void present(){if(active)++presents;}
inline void report(bool force=false) {
    if(!active || (!force && czas_monotoniczny_ns()-start<1000000000ULL))return;
    char out[384]{};unsigned n=0;
    auto field=[&](const char* name,uint64_t v) {
        while(*name && n+1<sizeof(out))out[n++]=*name++;
        char num[24];unsigned k=0;do{num[k++]=char('0'+v%10);v/=10;}while(v);
        while(k && n+1<sizeof(out))out[n++]=num[--k];
    };
    field("[MOVE TRACE] desktop_invalidations=",desktop);field(" explorer_invalidations=",explorer);field(" other_app_invalidations=",other);
    field(" total_invalidations=",dirty);field(" compositor_passes=",passes);field(" present_calls=",presents);
    field(" gpu_commands=",gpu(false)-commands);field(" transfer_flush_requests=",requests);field(" resourceflush_completed=",gpu(true)-flushes);
    active=false;wypisz_log(out);
}
}
#else
namespace MoveTrace {
inline void text(const char*,const char*){}
inline void begin(const char*,const char*){}
inline void invalidate(bool,bool=false){}
inline void flush_request(){}
inline void compose(){}
inline void present(){}
inline void report(bool=false){}
}
#endif
