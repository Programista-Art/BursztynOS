#include "fpu.h"
#include "pzb.h"

// Uniprocessor scheduler. An SMP scheduler must move hardware ownership per CPU.
static FpuState states[PZB_MAKS_PROCESOW];
static bool ready;
bool fpu_initialize() {
    uint32_t a,b,c,d;
    asm volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0));
    if(a<1)return false;
    asm volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));
    constexpr uint32_t required=(1U<<0)|(1U<<24)|(1U<<25)|(1U<<26);
    if((d&required)!=required)return false;
    uint64_t cr0,cr4;
    asm volatile("mov %%cr0,%0":"=r"(cr0));
    cr0=(cr0| (1ULL<<1) | (1ULL<<5)) & ~((1ULL<<2)|(1ULL<<3)); // MP, NE; !EM,!TS
    asm volatile("mov %0,%%cr0"::"r"(cr0):"memory");
    asm volatile("mov %%cr4,%0":"=r"(cr4));
    // Deliberately FXSAVE-only: no OSXSAVE, no XSETBV, no enabled AVX state.
    cr4=(cr4|(1ULL<<9)|(1ULL<<10)) & ~(1ULL<<18);
    asm volatile("mov %0,%%cr4"::"r"(cr4):"memory");
    for(unsigned i=0;i<PZB_MAKS_PROCESOW;++i)fpu_defaults(states[i]);
    fpu_restore(states[0]);ready=true;return true;
}
void fpu_reset_process(int pid) {
    if(pid>=0 && pid<int(PZB_MAKS_PROCESOW))fpu_defaults(states[pid]);
}
void fpu_switch_process(int outgoing,int incoming) {
    // Caller holds IRQs disabled, after validating both scheduler slots.
    if(!ready || outgoing==incoming)return;
    fpu_save(states[outgoing]);fpu_restore(states[incoming]);
}
