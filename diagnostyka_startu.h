#pragma once
#ifndef BURSZTYNDIAGSPLASHTIMING
#define BURSZTYNDIAGSPLASHTIMING 0
#endif
#ifndef BURSZTYNDIAGSTARTUPREBOOT
#define BURSZTYNDIAGSTARTUPREBOOT 0
#endif
inline void diag_start_reboot(const char* tekst) {
#if BURSZTYNDIAGSTARTUPREBOOT
    extern void klog_serial(const char*);
    klog_serial(tekst);
#else
    (void)tekst;
#endif
}

#ifndef BURSZTYNDIAGSPLASHFIRSTFRAME
#define BURSZTYNDIAGSPLASHFIRSTFRAME 0
#endif
#include <stdint.h>
#if BURSZTYNDIAGSPLASHFIRSTFRAME
extern void klog_serial(const char*);
#endif
namespace SplashFirstDiag {
struct Id { uint64_t generation=0, frame=0; bool title=false, message=false; unsigned message_index=0; };
inline Id& id() { static Id value; return value; }
inline void event(const char* event,uint64_t now,uint32_t resource=0) {
#if BURSZTYNDIAGSPLASHFIRSTFRAME
    char msg[224];unsigned n=0;
    auto txt=[&](const char* s){while(*s&&n<sizeof(msg)-1)msg[n++]=*s++;};
    auto num=[&](uint64_t v){char d[24];unsigned k=0;do{d[k++]=char('0'+v%10);v/=10;}while(v);while(k&&n<sizeof(msg)-1)msg[n++]=d[--k];};
    txt("[SPLASH FIRST] ");txt(event);txt(" now_us=");num(now);
    txt(" generation=");num(id().generation);txt(" frame=");num(id().frame);
    txt(" resource=");num(resource);txt(" title_pixels=");num(id().title);
    txt(" message_pixels=");num(id().message);txt(" message_index=");num(id().message_index);msg[n]=0;
    ::klog_serial(msg);
#else
    (void)event;(void)now;(void)resource;
#endif
}
}
