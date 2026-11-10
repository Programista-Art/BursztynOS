#pragma once
#include <stddef.h>
#include <stdint.h>

// Ordinary RAM only, never volatile firmware/MMIO. Constant-size builtins
// inline into general-register loads/stores even with kernel SIMD prohibited.
// Unlike variable memcpy (the freestanding byte loop) or REP under TCG, each
// loop iteration copies an entire 64-byte block without a per-element REP exit.
inline void graphics_copy_pixels(uint32_t* dst,const uint32_t* src,size_t count){
    while(count>=16){__builtin_memcpy(dst,src,64);dst+=16;src+=16;count-=16;}
    if(count>=8){__builtin_memcpy(dst,src,32);dst+=8;src+=8;count-=8;}
    if(count>=4){__builtin_memcpy(dst,src,16);dst+=4;src+=4;count-=4;}
    if(count>=2){__builtin_memcpy(dst,src,8);dst+=2;src+=2;count-=2;}
    if(count)*dst=*src;
}
