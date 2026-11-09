#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fpu.h"
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FPU FAIL %d: %s\n",__LINE__,#x);std::abort();}} while(0)
int main(){
    FpuState original,a,b,fresh,snapshot;fpu_save(original);fpu_defaults(a);fpu_defaults(b);fpu_defaults(fresh);
    // All sixteen XMM registers, x87 stack and both independent control words.
    for(unsigned i=160;i<416;++i){a.bytes[i]=uint8_t(i*13);b.bytes[i]=uint8_t(i*29+1);}
    a.bytes[25]=0x3f;b.bytes[25]=0x5f; // downward / upward rounding
    a.bytes[1]=0x07;b.bytes[1]=0x0b;
    fpu_restore(a);asm volatile("fld1");fpu_save(a);
    fpu_restore(b);asm volatile("fldpi");fpu_save(b);
    for(unsigned i=0;i<10000;++i){
        fpu_restore(a);fpu_save(snapshot);CHECK(std::memcmp(snapshot.bytes,a.bytes,416)==0);
        fpu_restore(b);fpu_save(snapshot);CHECK(std::memcmp(snapshot.bytes,b.bytes,416)==0);
    }
    fpu_defaults(a);CHECK(std::memcmp(&a,&fresh,sizeof(a))==0);
    fpu_restore(fresh);fpu_save(snapshot);CHECK(snapshot.bytes[4]==0);CHECK(snapshot.bytes[24]==0x80&&snapshot.bytes[25]==0x1f);
    for(unsigned i=32;i<416;++i)CHECK(snapshot.bytes[i]==0);
    fpu_restore(original);std::printf("FPU hardware state roundtrip: PASS (%u checks)\n",checks);
}
