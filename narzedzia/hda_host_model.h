#pragma once
// Deterministic register/DMA model. Executes production controller and transaction code.
#include "../sterowniki/dzwiek/hda_transport.h"
#include <array>
#include <map>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

struct HdaHostModel {
    struct Codec {
        uint32_t vendor=0x1af40012, revision=0x00100100;
        std::map<uint32_t,uint32_t> reads;
        uint32_t get(uint8_t nid,uint16_t verb,uint8_t payload) const {
            if(verb==0xf00 && !nid && !payload)return vendor;
            if(verb==0xf00 && !nid && payload==2)return revision;
            const auto it=reads.find((uint32_t(nid)<<20)|(uint32_t(verb)<<8)|payload);
            return it==reads.end()?0:it->second;
        }
        void param(uint8_t nid,uint8_t p,uint32_t r){reads[(uint32_t(nid)<<20)|0xf0000|p]=r;}
        void verb(uint8_t nid,uint16_t v,uint8_t p,uint32_t r){reads[(uint32_t(nid)<<20)|(uint32_t(v)<<8)|p]=r;}
        static Codec qemu(){
            Codec c;c.param(0,4,0x00010001);c.param(1,5,1);c.param(1,4,0x00020002);
            c.param(1,0xa,(1U<<17)|(1U<<6));c.param(1,0xb,1);
            c.param(2,9,0x1);c.param(3,9,0x400101);c.param(3,0xc,0x10);c.param(3,0xe,1);
            c.verb(3,0xf1c,0,0x01014010);c.verb(3,0xf02,0,2);return c;
        }
        // Realtek-LIKE topology with invented identity. This is not Acer evidence.
        static Codec laptop(uint32_t count=64,bool second_afg=false){
            Codec c;c.vendor=0xfacedead;c.param(0,4,0x00010001+(second_afg?1:0));
            const uint8_t start=second_afg?3:2;
            c.param(1,5,1);c.param(1,4,(uint32_t(start)<<16)|count);
            c.param(1,0xa,(1U<<17)|(1U<<19)|(1U<<6)|(1U<<8));c.param(1,0xb,1);
            c.param(1,0xd,0x80002018);c.param(1,0x12,0x80002018);c.param(1,0xf,0xf);
            for(uint32_t j=0;j<count&&start+j<128;++j)c.param(uint8_t(start+j),9,0xf00000);
            c.param(start,9,0x5); // DAC + output amplifier, inherits AFG format/amp caps
            c.param(start+1,9,0x300500); // selector + power + connections
            c.param(start+1,0xe,1);c.param(start+1,0xf,0xf);c.verb(start+1,0xf05,0,0x30);c.verb(start+1,0xf02,0,start);
            c.param(start+2,9,0x400505); // output pin, power and output amplifier
            c.param(start+2,0xc,(1U<<16)|0x10);c.param(start+2,0xe,1);c.param(start+2,0xf,0xf);
            c.verb(start+2,0xf02,0,start+1);c.verb(start+2,0xf1c,0,0x90170110);c.verb(start+2,0xf0c,0,2);
            c.param(start+3,9,0x400101);c.param(start+3,0xc,0x18);c.param(start+3,0xe,1);
            c.verb(start+3,0xf02,0,start);c.verb(start+3,0xf1c,0,0x02214020); // headphone
            c.param(start+4,9,0x400001);c.param(start+4,0xc,0x20);
            c.verb(start+4,0xf1c,0,0x90a60130); // internal microphone
            c.param(start+5,9,0x100103);c.param(start+5,0xe,1);
            c.verb(start+5,0xf02,0,start+4); // ADC input path, enumeration only
            c.param(start+6,9,0x400301);c.param(start+6,0xc,0x90);c.param(start+6,0xe,1);
            c.verb(start+6,0xf02,0,start);c.verb(start+6,0xf1c,0,0x18560040); // HDMI separate
            if(second_afg){c.param(2,5,1);c.param(2,4,0x00700002);c.param(0x70,9,1);c.param(0x71,9,0x400101);
                c.param(0x71,0xc,0x10);c.param(0x71,0xe,1);c.verb(0x71,0xf02,0,0x70);c.verb(0x71,0xf1c,0,0x01014010);}
            return c;
        }
    };
    struct Device {
        uint8_t bus=0, slot=27, fn=0;
        uint16_t vendor=0x8086, device=0x2668;
        uint32_t class_revision=0x04030001, command=0x100, bar_lo=0x10000000, bar_hi=0;
        std::array<uint8_t,0x4000> regs{};
        std::map<uint8_t,Codec> codecs;
        bool map_fails=false, memory_fails=false, master_fails=false, reset_zero_fails=false, reset_one_fails=false;
        bool corb_reset_fails=false, rirb_reset_fails=false, start_fails=false, ic_never_idle=false, no_response=false;
        bool qemu_absent_busy=true;
        uint64_t response_delay_ns=10000, response_due=0, irv_delay_ns=0, irv_due=0;
        uint32_t pending=0, verbs=0, codec_writes=0, mmio_reads=0, mmio_writes=0;
        bool pending_ic=false,pending_valid=false;
        std::vector<DmaBuffer> buffers;
        Device(){set(0,2,0x4401);set(2,1,0);set(3,1,1);set(8,4,1);set(0xe,2,1);set(4,2,60);set(0x4e,1,0x70);set(0x5e,1,0x70);codecs.emplace(0,Codec::qemu());}
        uint32_t get(uint32_t o,uint8_t n)const{uint32_t v=0;std::memcpy(&v,regs.data()+o,n);return v;}
        void set(uint32_t o,uint8_t n,uint32_t v){std::memcpy(regs.data()+o,&v,n);}
    };
    std::vector<Device> devices;
    std::vector<DmaBuffer> allocated;
    uint64_t ns=1000000000ULL, cost_ns=50, next_pa=0x200000;
    bool clock_ok=true, dma_fail=false, force_high32=false;
    uint32_t map_calls=0, dma_calls=0, max_reads=0,max_writes=0;
    std::vector<std::array<uint64_t,3>> mappings;
    ~HdaHostModel(){for(auto& b:allocated)std::free(b.virtual_address);}
    Device& add(uint16_t vendor=0x8086,uint16_t device=0x2668,uint8_t bus=0,uint8_t slot=27,uint8_t fn=0){
        devices.emplace_back();auto& d=devices.back();d.vendor=vendor;d.device=device;d.bus=bus;d.slot=slot;d.fn=fn;
        d.bar_lo=0x10000000+uint32_t(devices.size()-1)*0x10000;return d;
    }
    void* physical(uint64_t pa){for(auto& b:allocated)if(pa>=b.physical_address&&pa-b.physical_address<b.size)return static_cast<uint8_t*>(b.virtual_address)+(pa-b.physical_address);return nullptr;}
    void rirb(uint32_t k,uint32_t response,uint32_t ext){
        auto& d=devices.at(k);const uint32_t code=d.get(0x5e,1)&3,size=code==2?256:code==1?16:2;
        const uint32_t wp=(d.get(0x58,2)+1)%size;
        const uint64_t pa=d.get(0x50,4)|(uint64_t(d.get(0x54,4))<<32);
        auto* p=static_cast<uint32_t*>(physical(pa));if(!p)std::abort();p[wp*2]=response;p[wp*2+1]=ext;d.set(0x58,2,wp);
    }
    void service(uint32_t k){
        auto& d=devices.at(k);
        if(d.irv_due&&ns>=d.irv_due){d.set(0x68,2,d.get(0x68,2)|2);d.irv_due=0;}
        if(!d.pending_valid||ns<d.response_due)return;
        d.pending_valid=false;const uint8_t cad=d.pending>>28;
        const uint8_t nid=(d.pending>>20)&127;const uint16_t verb=(d.pending>>8)&0xfff;const uint8_t payload=d.pending;
        const auto codec=d.codecs.find(cad);
        if(codec==d.codecs.end()||d.no_response){if(d.pending_ic&&!d.qemu_absent_busy)d.set(0x68,2,d.get(0x68,2)&~1U);return;}
        if(verb!=0xf00&&verb!=0xf02&&verb!=0xf05&&verb!=0xf1c&&verb!=0xf01&&verb!=0xf0c)++d.codec_writes;
        const uint32_t r=codec->second.get(nid,verb,payload);
        if(d.pending_ic){d.set(0x64,4,r);d.set(0x68,2,d.irv_delay_ns?0:2);if(d.irv_delay_ns)d.irv_due=ns+d.irv_delay_ns;}
        else if(d.get(0x5c,1)&2)rirb(k,r,cad);
    }
    static uint32_t pci_read(void* ctx,uint8_t b,uint8_t s,uint8_t f,uint8_t o){
        auto& m=*static_cast<HdaHostModel*>(ctx);m.ns+=m.cost_ns;
        for(auto& d:m.devices)if(d.bus==b&&d.slot==s&&d.fn==f){
            switch(o){case 0:return (uint32_t(d.device)<<16)|d.vendor;case 4:return d.command;
            case 8:return d.class_revision;case 0x10:return d.bar_lo;case 0x14:return d.bar_hi;case 0x2c:return 0x11223344;default:return 0;}}
        return UINT32_MAX;
    }
    static void pci_write(void* ctx,uint8_t b,uint8_t s,uint8_t f,uint8_t o,uint32_t v){
        auto& m=*static_cast<HdaHostModel*>(ctx);m.ns+=m.cost_ns;
        for(auto& d:m.devices)if(d.bus==b&&d.slot==s&&d.fn==f&&o==4){
            if(v>>16)std::abort();
            d.command=v;
            if(d.memory_fails)d.command&=~2U;
            if(d.master_fails)d.command&=~4U;
        }
    }
    static bool map(void* ctx,uint32_t k,uint64_t pa,uint32_t n,uint64_t* va){
        auto& m=*static_cast<HdaHostModel*>(ctx);++m.map_calls;m.mappings.push_back({k,pa,n});
        if(m.devices.at(k).map_fails)return false;
        *va=0xffff920000000000ULL+uint64_t(k)*0x10000;return true;
    }
    static uint32_t read(void* ctx,uint32_t k,uint32_t o,uint8_t n){
        auto& m=*static_cast<HdaHostModel*>(ctx);m.ns+=m.cost_ns;m.service(k);auto& d=m.devices.at(k);++d.mmio_reads;
        if(o==0x30)return uint32_t(m.ns*24/1000);
        if(o==0x68&&d.ic_never_idle)return 1;
        if(o>=0x80&&o<0x440&&(o%0x20)==4&&(d.get(o-4,2)&2))d.set(o,4,d.get(o,4)+128);
        return d.get(o,n);
    }
    static void write(void* ctx,uint32_t k,uint32_t o,uint8_t n,uint32_t v){
        auto& m=*static_cast<HdaHostModel*>(ctx);m.ns+=m.cost_ns;auto& d=m.devices.at(k);++d.mmio_writes;
        if(o==0x0e){d.set(o,n,d.get(o,n)&~v);return;}
        if(o==0x4d||o==0x5d||(o>=0x80&&(o%0x20)==3)){d.set(o,n,d.get(o,n)&~v);return;}
        if(o==8){if((!(v&1)&&d.reset_zero_fails)||((v&1)&&d.reset_one_fails))return;
            d.set(o,n,v);if(v&1){uint16_t mask=0;for(auto& entry:d.codecs)mask|=1U<<entry.first;d.set(0xe,2,d.get(0xe,2)|mask);}return;}
        if(o==0x4a&&(v&0x8000)){if(!d.corb_reset_fails)d.set(o,n,0x8000);return;}
        if(o==0x58&&(v&0x8000)){if(!d.rirb_reset_fails)d.set(o,n,0);return;}
        if((o==0x4c||o==0x5c)&&d.start_fails&&(v&2))return;
        if(o==0x68){
            if(v&2)d.set(o,n,d.get(o,n)&~2U);
            else if(v&1){d.set(o,n,1);d.pending=d.get(0x60,4);d.pending_ic=true;d.pending_valid=true;d.response_due=m.ns+d.response_delay_ns;++d.verbs;}
            else{d.set(o,n,0);d.pending_valid=false;}
            return;
        }
        d.set(o,n,v);
        if(o==0x48&&(d.get(0x4c,1)&2)){
            const uint64_t pa=d.get(0x40,4)|(uint64_t(d.get(0x44,4))<<32);
            auto* corb=static_cast<uint32_t*>(m.physical(pa));if(!corb)std::abort();
            d.pending=corb[v];d.pending_ic=false;d.pending_valid=true;d.response_due=m.ns+d.response_delay_ns;++d.verbs;d.set(0x4a,2,v);
        }
    }
    static bool dma(void* ctx,bool dma64,DmaBuffer* out){
        auto& m=*static_cast<HdaHostModel*>(ctx);++m.dma_calls;
        if(m.dma_fail)return false;
        void* mem=std::aligned_alloc(4096,4096);if(!mem)return false;std::memset(mem,0,4096);
        uint64_t pa=m.next_pa;m.next_pa+=4096;
        if(!dma64&&m.force_high32)pa|=1ULL<<32;
        *out={mem,pa,4096,1};m.allocated.push_back(*out);return true;
    }
    static void release(void* ctx,DmaBuffer* b){
        auto& m=*static_cast<HdaHostModel*>(ctx);if(!b->virtual_address)return;
        for(auto it=m.allocated.begin();it!=m.allocated.end();++it)if(it->virtual_address==b->virtual_address){std::free(it->virtual_address);m.allocated.erase(it);break;}
        *b={};
    }
    static uint64_t clock(void* ctx){auto& m=*static_cast<HdaHostModel*>(ctx);m.ns+=m.cost_ns;return m.ns;}
    static bool clock_ready(void* ctx){return static_cast<HdaHostModel*>(ctx)->clock_ok;}
    HdaPlatforma platform(){return {this,pci_read,pci_write,map,read,write,dma,release,clock,clock_ready};}
    void discover(){if(!hda_transport_wykryj_z(platform()))std::abort();}
    void init_all(){
        discover();for(uint32_t k=0;k<hda_transport_liczba();++k){
            for(unsigned step=0;step<2000;++step){if(hda_transport_init_krok(k))break;ns+=100000;}
        }
    }
};
