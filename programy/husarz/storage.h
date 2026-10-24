#pragma once
#include "origin.h"
namespace Husarz {
constexpr unsigned StorageOrigins=4,StorageEntries=16,StorageKey=128,StorageValue=1024,StorageQuota=16*1024,StorageDiskCapacity=20*1024,StorageSessions=24;
struct StorageEntry {char key[StorageKey+1]{},value[StorageValue+1]{};uint16_t key_size{},value_size{};};
struct StorageNamespace {
    Origin origin{};StorageEntry entries[StorageEntries]{};unsigned count{},bytes{},revision{1},saved{};bool loaded{},loading{},saving{},failed{};uint32_t tab{};
    int find(const char *key,unsigned size)const{for(unsigned i=0;i<count;++i)if(entries[i].key_size==size){bool same=true;for(unsigned j=0;j<size;++j)if(entries[i].key[j]!=key[j]){same=false;break;}if(same)return int(i);}return -1;}
    bool set(const char *key,unsigned ks,const char *value,unsigned vs){
        if(ks>StorageKey||vs>StorageValue||revision==UINT32_MAX)return false;int index=find(key,ks);unsigned previous=index<0?0:entries[index].key_size+entries[index].value_size;
        if(bytes-previous+ks+vs>StorageQuota||(index<0&&count==StorageEntries))return false;
        unsigned slot=index<0?count++:unsigned(index);auto &e=entries[slot];for(unsigned i=0;i<ks;++i)e.key[i]=key[i];e.key[ks]=0;for(unsigned i=0;i<vs;++i)e.value[i]=value[i];e.value[vs]=0;e.key_size=uint16_t(ks);e.value_size=uint16_t(vs);bytes=bytes-previous+ks+vs;++revision;return true;
    }
    void remove(unsigned i){if(i>=count||revision==UINT32_MAX)return;bytes-=entries[i].key_size+entries[i].value_size;for(unsigned j=i+1;j<count;++j)entries[j-1]=entries[j];entries[--count]={};++revision;}
};
inline unsigned origin_key(const Origin &o,char *out){unsigned n=0;const char *scheme=o.secure?"https://":"http://";for(unsigned i=0;scheme[i];++i)out[n++]=scheme[i];for(unsigned i=0;o.host[i];++i)out[n++]=o.host[i];out[n++]=':';char rev[6];unsigned k=0,p=o.port;do{rev[k++]=char('0'+p%10);p/=10;}while(p);while(k)out[n++]=rev[--k];out[n]=0;return n;}
inline uint32_t storage_checksum(const uint8_t *data,unsigned n){uint32_t hash=2166136261U;for(unsigned i=0;i<n;++i){hash^=data[i];hash*=16777619U;}return hash;}
inline void storage_u32(uint8_t *out,uint32_t value){for(unsigned i=0;i<4;++i)out[i]=uint8_t(value>>(i*8));}
inline uint32_t storage_read_u32(const uint8_t *in){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(in[i])<<(i*8);return v;}
inline unsigned storage_encode(const StorageNamespace &s,uint8_t *out,unsigned cap){
    if(cap<StorageDiskCapacity||!s.origin.valid||s.count>StorageEntries)return 0;char origin[280];unsigned os=origin_key(s.origin,origin),pos=24;
    for(unsigned i=0;i<os;++i)out[pos++]=uint8_t(origin[i]);
    for(unsigned i=0;i<s.count;++i){auto &e=s.entries[i];if(e.key_size>StorageKey||e.value_size>StorageValue||pos+8+e.key_size+e.value_size>cap)return 0;storage_u32(out+pos,e.key_size);pos+=4;storage_u32(out+pos,e.value_size);pos+=4;for(unsigned j=0;j<e.key_size;++j)out[pos++]=uint8_t(e.key[j]);for(unsigned j=0;j<e.value_size;++j)out[pos++]=uint8_t(e.value[j]);}
    storage_u32(out,0x31545348);storage_u32(out+4,pos);storage_u32(out+8,s.revision);storage_u32(out+12,s.count);storage_u32(out+16,os);storage_u32(out+20,storage_checksum(out+24,pos-24)^storage_checksum(out,20));return pos;
}
inline bool storage_utf8(const char *s,unsigned n){for(unsigned i=0;i<n;){size_t used=0;uint32_t cp=utf8(s+i,n-i,used);if(!used||!s[i]||(cp==0xfffd&&(used!=3||uint8_t(s[i])!=0xef)))return false;i+=unsigned(used);}return true;}
inline bool storage_conflicts(const uint8_t *data,unsigned n,const Origin &expected){
    if(n<24||n>StorageDiskCapacity||storage_read_u32(data)!=0x31545348||storage_read_u32(data+4)!=n)return false;
    unsigned os=storage_read_u32(data+16);if(os>279||os>n-24)return false;
    if(storage_read_u32(data+20)!=(storage_checksum(data+24,n-24)^storage_checksum(data,20)))return false;
    char key[280];unsigned size=origin_key(expected,key);if(size!=os)return true;
    for(unsigned i=0;i<size;++i)if(data[24+i]!=uint8_t(key[i]))return true;
    return false;
}
// Zero denotes an invalid/missing generation. Mutation revisions can skip when
// writes are coalesced: never choose the destination from revision parity.
inline unsigned storage_write_slot(const uint32_t committed[2]){
    return committed[0]>committed[1]?1U:0U;
}
inline bool storage_decode(StorageNamespace &out,const uint8_t *data,unsigned n,const Origin &expected){
    if(n<24||n>StorageDiskCapacity||storage_read_u32(data)!=0x31545348||storage_read_u32(data+4)!=n||!storage_read_u32(data+8)||storage_read_u32(data+12)>StorageEntries||storage_read_u32(data+16)>279||storage_read_u32(data+16)>n-24)return false;
    if(storage_read_u32(data+20)!=(storage_checksum(data+24,n-24)^storage_checksum(data,20)))return false;
    char origin[280];unsigned os=origin_key(expected,origin);if(os!=storage_read_u32(data+16))return false;for(unsigned i=0;i<os;++i)if(uint8_t(origin[i])!=data[24+i])return false;
    // Validate the entire record before publishing anything into the live namespace.
    unsigned pos=24+os,total=0,count=storage_read_u32(data+12);
    for(unsigned i=0;i<count;++i){if(pos>n||n-pos<8)return false;unsigned ks=storage_read_u32(data+pos),vs=storage_read_u32(data+pos+4);pos+=8;if(ks>StorageKey||vs>StorageValue||ks+vs>n-pos||total+ks+vs>StorageQuota)return false;if(!storage_utf8(reinterpret_cast<const char*>(data+pos),ks)||!storage_utf8(reinterpret_cast<const char*>(data+pos+ks),vs))return false;pos+=ks+vs;total+=ks+vs;}
    if(pos!=n)return false;
    out.origin=expected;out.count=count;out.bytes=total;out.revision=out.saved=storage_read_u32(data+8);out.loaded=true;pos=24+os;
    for(unsigned i=0;i<count;++i){auto &e=out.entries[i];e.key_size=uint16_t(storage_read_u32(data+pos));e.value_size=uint16_t(storage_read_u32(data+pos+4));pos+=8;for(unsigned j=0;j<e.key_size;++j)e.key[j]=char(data[pos++]);e.key[e.key_size]=0;for(unsigned j=0;j<e.value_size;++j)e.value[j]=char(data[pos++]);e.value[e.value_size]=0;}
    return true;
}
inline void storage_path(const Origin &origin,unsigned slot,char *path){
    char key[280];unsigned size=origin_key(origin,key);uint64_t hash=14695981039346656037ULL;for(unsigned i=0;i<size;++i){hash^=uint8_t(key[i]);hash*=1099511628211ULL;}
    const char *prefix="/uzytkownicy/husarz/";unsigned n=0;for(;prefix[n];++n)path[n]=prefix[n];const char *hex="0123456789abcdef";for(unsigned i=0;i<16;++i)path[n++]=hex[(hash>>((15-i)*4))&15];path[n++]='.';path[n++]=slot?'b':'a';path[n]=0;
}
struct BrowserStorage {
    StorageNamespace *local[StorageOrigins]{},*sessions[StorageSessions]{};
    void clear(){for(auto *&p:local){delete p;p=nullptr;}for(auto *&p:sessions){delete p;p=nullptr;}}
    StorageNamespace *get(const Origin &origin,uint32_t tab=0){
        if(!origin.valid)return nullptr;auto **slots=tab?sessions:local;unsigned limit=tab?StorageSessions:StorageOrigins;
        for(unsigned i=0;i<limit;++i)if(slots[i]&&slots[i]->tab==tab&&slots[i]->origin.same(origin))return slots[i];
        for(unsigned i=0;i<limit;++i)if(!slots[i]){auto *s=new StorageNamespace;if(!s)return nullptr;s->origin=origin;s->tab=tab;s->loaded=tab!=0;s->saved=1;slots[i]=s;return s;}return nullptr;
    }
    void close(uint32_t tab){if(tab)for(auto *&s:sessions)if(s&&s->tab==tab){delete s;s=nullptr;}}
};
inline BrowserStorage browser_storage;
}
