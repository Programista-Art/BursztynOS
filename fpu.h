#pragma once
#include <stdint.h>

// Kernel-owned, never mapped user-writable. No variable extended state / AVX.
struct alignas(16) FpuState { uint8_t bytes[512]; };
static_assert(sizeof(FpuState)==512 && alignof(FpuState)==16, "FXSAVE ABI");
inline void fpu_defaults(FpuState &s) {
    for (unsigned i=0;i<sizeof(s.bytes);++i) s.bytes[i]=0;
    s.bytes[0]=0x7f;s.bytes[1]=0x03; // x87: masked, nearest, extended precision
    s.bytes[24]=0x80;s.bytes[25]=0x1f; // MXCSR: masked, nearest, no FTZ/DAZ
}
inline void fpu_save(FpuState &s) { asm volatile("fxsave64 %0" : "=m"(s) :: "memory"); }
inline void fpu_restore(const FpuState &s) { asm volatile("fxrstor64 %0" :: "m"(s) : "memory"); }
bool fpu_initialize();
void fpu_reset_process(int pid);
void fpu_switch_process(int outgoing,int incoming);
