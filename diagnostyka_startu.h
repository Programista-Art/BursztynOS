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
