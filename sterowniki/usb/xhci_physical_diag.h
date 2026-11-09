#pragma once
#include "pointing_diag.h"
#ifndef BURSZTYNDIAGXHCIPHYSICALMOUSE
#define BURSZTYNDIAGXHCIPHYSICALMOUSE 0
#endif
inline bool xhci_physical_log_allowed() {
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    static unsigned lines=0;
    return lines<1024 ? (++lines,true) : false;
#else
    return false;
#endif
}
inline void xhci_physical_diag(const char* stage,const int64_t* values,size_t n) {
#if BURSZTYNDIAGXHCIPHYSICALMOUSE
    if(xhci_physical_log_allowed())pointing_diag(stage,values,n);
#else
    (void)stage;(void)values;(void)n;
#endif
}
