#pragma once
#include <stddef.h>
#include <stdint.h>
// Call only from controlled initialization or deferred input service, never IRQ.
inline void pointing_diag(const char* tag,const int64_t* values,size_t count) {
    extern void klog_serial(const char*);
    char buffer[240]{};size_t n=0;
    auto append=[&](char c){if(n+1<sizeof(buffer))buffer[n++]=c;};
    while(*tag)append(*tag++);
    for(size_t i=0;i<count;++i){append(' ');int64_t v=values[i];
        uint64_t magnitude=v<0?uint64_t(-(v+1))+1:uint64_t(v);if(v<0)append('-');
        char digits[24];size_t d=0;do{digits[d++]=char('0'+magnitude%10);magnitude/=10;}while(magnitude);
        while(d)append(digits[--d]);}
    buffer[n]=0;klog_serial(buffer);
}
