#pragma once
#ifndef BURSZTYNDIAGGUIRUNTIME
#define BURSZTYNDIAGGUIRUNTIME 0
#endif
struct ShellPerf {uint64_t waits=0,events=0,move=0,precise=0,timer=0,full=0,row=0,caret=0,scrollbar=0,publish=0;};
#if BURSZTYNDIAGGUIRUNTIME
static ShellPerf shell_perf{};
#define SHELL_PERF(field) (++shell_perf.field)
static void shell_perf_report(){
    static uint64_t previous=0;auto now=gui_czas_monotoniczny_ns();
    if(!previous){previous=now;return;}if(now-previous<1000000000ULL)return;
    char buf[512]{};unsigned n=0;
    auto text=[&](const char* s){while(*s&&n+1<sizeof(buf))buf[n++]=*s++;};
    auto num=[&](uint64_t v){char t[24];unsigned i=0;do{t[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(buf))buf[n++]=t[--i];};
    auto field=[&](const char* key,uint64_t value){text(key);num(value);text(" ");};
    text("[SHELL PERF] ");field("interval_ns=",now-previous);field("waits=",shell_perf.waits);
    field("events=",shell_perf.events);field("move=",shell_perf.move);field("precise=",shell_perf.precise);field("timer=",shell_perf.timer);
    field("full=",shell_perf.full);field("row=",shell_perf.row);field("caret=",shell_perf.caret);
    field("scrollbar=",shell_perf.scrollbar);field("publish=",shell_perf.publish);text("\n");
    bws_wywolaj(1,reinterpret_cast<uint64_t>(buf),0,0,0);
    shell_perf={};previous=now;
}
#else
#define SHELL_PERF(field) ((void)0)
inline void shell_perf_report(){}
#endif
