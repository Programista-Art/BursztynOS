#pragma once
#include <stdint.h>

// Straight-alpha source-over. Legacy nonzero RGB pixels are opaque; zero
// denotes an empty layer pixel, and 0x01000000 is the legacy visible black.
inline uint32_t kolor_z_alfa(uint32_t c) {
    if (!c) return 0;
    if (!(c >> 24) || c == 0x01000000U) return 0xff000000U | (c & 0xffffffU);
    return c;
}
inline uint32_t kolor_nad(uint32_t dst, uint32_t src) {
    unsigned a=src>>24;
    if (!a) return dst;
    if (a==255) return src;
    dst=kolor_z_alfa(dst);
    unsigned da=dst>>24;
    if (da==255) {
        uint32_t out=0xff000000U;
        for(unsigned shift=0;shift<24;shift+=8)
            out|=(((src>>shift&255)*a+(dst>>shift&255)*(255-a)+127)/255)<<shift;
        return out;
    }
    unsigned combined=a*255+da*(255-a);
    uint32_t out=((combined+127)/255)<<24;
    for(unsigned shift=0;shift<24;shift+=8) {
        unsigned value=((src>>shift&255)*a*255+(dst>>shift&255)*da*(255-a)+combined/2)/combined;
        out|=value<<shift;
    }
    return out;
}
