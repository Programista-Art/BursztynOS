#pragma once
#include <stdint.h>
#include "pointing_diag.h"
// Same bounded recorder in both builds. Emission alone is compile-time gated;
// no MMIO, ISR acknowledgement, queue work or scheduling in this recorder.
namespace VirtioInputQemuDiag {
struct Record {const char* label;uint32_t bdf;uint64_t a,b,c;};
struct Source {uint32_t bdf;uint32_t seen;};
inline Record records[512]{};inline unsigned count=0,emitted=0;
inline Source sources[8]{};inline unsigned source_count=0;
inline char names[8][160]{};inline unsigned name_count=0;
inline void record(uint32_t bdf,const char* label,uint64_t a=0,uint64_t b=0,uint64_t c=0){
    if(count<512)records[count++]={label,bdf,a,b,c};
}
inline void name(uint32_t bdf,const char* value){
    if(name_count==8)return;
    auto* out=names[name_count++];unsigned i=0;
    for(const char* p="[VINPUT name] ";*p;++p)out[i++]=*p;
    for(unsigned j=0;value[j]&&i<159;++j){out[i++]=value[j];}
    out[i]=0;
    record(bdf,out);
}
inline void once(uint32_t bdf,unsigned bit,const char* label,uint64_t a=0,uint64_t b=0,uint64_t c=0){
    unsigned s=0;for(;s<source_count;++s)if(sources[s].bdf==bdf)break;
    if(s==source_count){if(source_count==8)return;sources[source_count++]={bdf,0};}
    if(sources[s].seen&(1U<<bit))return;
    sources[s].seen|=1U<<bit;record(bdf,label,a,b,c);
}
inline void dump(){
#if BURSZTYNDIAGVIRTIOINPUTQEMU
    for(unsigned budget=2;budget&&emitted<count;--budget){const auto& r=records[emitted++];
        const int64_t v[]={r.bdf,int64_t(r.a),int64_t(r.b),int64_t(r.c)};pointing_diag(r.label,v,4);}
#endif
}
}
