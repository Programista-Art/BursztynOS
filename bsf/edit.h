#pragma once
#include "bsf.h"
namespace BSF {
inline size_t cluster_ceil(const char* text,size_t size,size_t offset){
  size_t p=cluster_floor(text,size,offset);
  return p<offset?next_cluster(text,p,size):p;
}
// Byte-preserving truncation: retain only complete source graphemes.
inline size_t cluster_prefix(const char* text,size_t size,size_t capacity){
  return cluster_floor(text,size,capacity<size?capacity:size);
}
// A bounded, reusable byte-boundary index. Rebuild after every text mutation.
// Fixed bitset and prefix counts at the native 65,536-byte capacity; no allocations.
template<size_t Limit=65536> struct BoundaryIndex {
  uint64_t bits[Limit/64+1]{};uint32_t ranks[Limit/64+2]{};size_t size{};bool indexed{};
  static unsigned bit_count(uint64_t value){
    value-= (value>>1)&UINT64_C(0x5555555555555555);
    value=(value&UINT64_C(0x3333333333333333))+((value>>2)&UINT64_C(0x3333333333333333));
    value=(value+(value>>4))&UINT64_C(0x0f0f0f0f0f0f0f0f);
    return unsigned((value*UINT64_C(0x0101010101010101))>>56);
  }
  void rebuild(const char* text,size_t n){
    size=n;indexed=n<=Limit;if(!indexed)return;
    for(auto& word:bits)word=0;
    for(size_t p=0;;p=next_cluster(text,p,n)){
      bits[p/64]|=uint64_t(1)<<(p%64);if(p==n)break;
    }
    ranks[0]=0;for(size_t i=0;i<Limit/64+1;++i)ranks[i+1]=ranks[i]+bit_count(bits[i]);
  }
  size_t rank(const char* text,size_t p)const{
    if(p>size)p=size;
    if(!indexed)return cluster_count(text,p);
    unsigned bit=p%64;uint64_t mask=bit?(~uint64_t(0)>>(64-bit)):0;
    return ranks[p/64]+bit_count(bits[p/64]&mask);
  }
  size_t floor(const char* text,size_t p)const{
    if(p>size)p=size;
    if(!indexed)return cluster_floor(text,size,p);
    size_t word=p/64;uint64_t value=bits[word]&(~uint64_t(0)>>(63-p%64));
    while(!value&&word)value=bits[--word];
    return word*64+63-unsigned(__builtin_clzll(value));
  }
  size_t previous(const char* text,size_t p)const{return p?floor(text,p-1):0;}
};
}
