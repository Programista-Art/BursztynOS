#include "storage_format.h"

namespace Storage {
// ABI: b21fe73c-0414-4585-bd07-8ab4980f9c88 (UUIDv4 z systemowego generatora).
const uint8_t BspType[16]={0x3c,0xe7,0x1f,0xb2,0x14,0x04,0x85,0x45,0xbd,0x07,0x8a,0xb4,0x98,0x0f,0x9c,0x88};
const uint8_t EspType[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
static void zero(void* p,size_t n){auto*b=(uint8_t*)p;while(n--)*b++=0;}
static void copy(void* d,const void*s,size_t n){auto*a=(uint8_t*)d;auto*b=(const uint8_t*)s;while(n--)*a++=*b++;}
bool equal(const void*a,const void*b,size_t n){auto*x=(const uint8_t*)a;auto*y=(const uint8_t*)b;while(n--)if(*x++!=*y++)return false;return true;}
static uint32_t u32(const uint8_t*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static uint64_t u64(const uint8_t*p){return u32(p)|(uint64_t(u32(p+4))<<32);}
static void p16(uint8_t*p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void p32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=v>>(8*i);}
static void p64(uint8_t*p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=v>>(8*i);}
uint32_t crc32(const void*p,size_t n){uint32_t c=~0U;auto*b=(const uint8_t*)p;while(n--){c^=*b++;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xEDB88320U & (0U-(c&1)));}return ~c;}
bool range(uint64_t s,uint64_t n,uint64_t cap){return n && s<cap && n<=cap-s;}
static bool valid(const Partition&p,uint64_t s,uint32_t n,const void*b){return b&&p.device&&p.device->sector_size==512&&range(p.start,p.count,p.device->sectors)&&range(s,n,p.count);}
bool read(const Partition&p,uint64_t s,uint32_t n,void*b){
    if(!valid(p,s,n,b)||!p.device->read)return false;
    for(uint32_t i=0;i<n;){uint32_t k=n-i>32?32:n-i;if(!p.device->read(p.device->ctx,p.start+s+i,k,(uint8_t*)b+size_t(i)*512))return false;i+=k;}return true;
}
bool write(const Partition&p,uint64_t s,uint32_t n,const void*b){
    if(!valid(p,s,n,b)||!p.device->writable||!p.device->write)return false;
    for(uint32_t i=0;i<n;){uint32_t k=n-i>32?32:n-i;if(!p.device->write(p.device->ctx,p.start+s+i,k,(const uint8_t*)b+size_t(i)*512))return false;i+=k;}return true;
}
bool flush(const Partition&p){return p.device&&p.device->flush&&p.device->flush(p.device->ctx);}
bool write_verify(const Partition&p,uint64_t s,uint32_t n,const void*b){
    if(!valid(p,s,n,b))return false;
    uint8_t check[4096];
    for(uint32_t i=0;i<n;){uint32_t k=n-i>8?8:n-i;const uint8_t*src=(const uint8_t*)b+size_t(i)*512;
        if(!write(p,s+i,k,src)||!read(p,s+i,k,check)||!equal(src,check,k*512))return false;
        i+=k;}
    return true;
}
bool plan(Device*d,Layout*out){
    if(!d||!out||!d->writable||d->sector_size!=512||d->sectors>(1ULL<<48))return false;
    const uint64_t a=MiB/d->sector_size,esp=EspBytes/d->sector_size,start=a+esp;
    if(d->sectors<=start+33)return false;
    uint64_t available=(d->sectors-33-start)/a*a;
    if(available<128*MiB/d->sector_size)return false;
    uint64_t n=available<BspMaxBytes/d->sector_size?available:BspMaxBytes/d->sector_size;
    *out={{d,a,esp},{d,start,n},available-n};return true;
}
static bool header(Device*d,uint64_t where,uint8_t*h){
    Partition all{d,0,d->sectors};
    if(!read(all,where,1,h)||!equal(h,"EFI PART",8)||u32(h+8)!=0x10000||u32(h+12)!=92||u32(h+20))return false;
    uint32_t c=u32(h+16);p32(h+16,0);bool ok=crc32(h,92)==c;p32(h+16,c);
    uint64_t first=u64(h+40),last=u64(h+48),arr=u64(h+72);
    uint32_t n=u32(h+80),sz=u32(h+84);
    return ok&&u64(h+24)==where&&u64(h+32)==(where==1?d->sectors-1:1)&&
        first>=34&&last>=first&&last<d->sectors-33&&n&&n<=128&&sz==128&&
        range(arr,(uint64_t(n)*128+511)/512,d->sectors)&&
        (where==1?(arr>=2&&arr+(uint64_t(n)*128+511)/512<=first):(arr>last&&arr+(uint64_t(n)*128+511)/512<=where));
}
bool gpt_read(Device*d,Table*out){
    if(!d||!out||d->sector_size!=512||d->sectors<68)return false;
    uint8_t h[512],b[512],sector[512],backup[512];
    if(!header(d,1,h)||!header(d,d->sectors-1,b)||!equal(h+40,b+40,32)||!equal(h+80,b+80,12))return false;
    Partition all{d,0,d->sectors};
    uint32_t entries=u32(h+80),c=~0U;out->count=0;copy(out->uuid,h+56,16);
    for(uint32_t i=0;i<entries;i+=4){
        if(!read(all,u64(h+72)+i/4,1,sector)||!read(all,u64(b+72)+i/4,1,backup))return false;
        uint32_t bytes=(entries-i<4?entries-i:4)*128;
        if(!equal(sector,backup,bytes))return false;
        for(uint32_t j=0;j<bytes;++j){c^=sector[j];for(unsigned k=0;k<8;++k)c=(c>>1)^(0xEDB88320U & (0U-(c&1)));}
        for(uint32_t j=0;j<bytes;j+=128){
            auto*p=sector+j;uint8_t empty[16]{};if(equal(p,empty,16))continue;
            uint64_t s=u64(p+32),end=u64(p+40);
            if(s<u64(h+40)||end<s||end>u64(h+48)||equal(p+16,empty,16))return false;
            for(uint32_t k=0;k<out->count;++k){const auto&e=out->entries[k];if((s<e.start+e.count&&e.start<=end)||equal(e.uuid,p+16,16))return false;}
            auto&e=out->entries[out->count++];copy(e.type,p,16);copy(e.uuid,p+16,16);e.start=s;e.count=end-s+1;
        }
    }
    return ~c==u32(h+88);
}
bool gpt_write(Device*d,const Layout&l,const uint8_t uuid[16]){
    Layout expected{};
    if(!uuid||!plan(d,&expected)||l.esp.device!=d||l.bsp.device!=d||l.esp.start!=expected.esp.start||
       l.esp.count!=expected.esp.count||l.bsp.start!=expected.bsp.start||l.bsp.count!=expected.bsp.count)return false;
    // Bounded scratch on stack (kernel stack is 16 KiB).
    uint8_t entries[512]{},h[512]{},zeroes[512]{},mbr[512]{};
    auto entry=[&](unsigned off,const uint8_t*type,const Partition&p,const char*name,uint8_t salt){
        auto*e=entries+off;copy(e,type,16);copy(e+16,uuid,16);e[16]^=salt;p64(e+32,p.start);p64(e+40,p.start+p.count-1);
        for(unsigned i=0;name[i]&&i<36;++i)p16(e+56+2*i,uint8_t(name[i]));
    };
    entry(0,EspType,l.esp,"EFI System Partition",1);entry(128,BspType,l.bsp,"Bursztyn OS",2);
    uint32_t c=~0U;for(unsigned i=0;i<16384;++i){c^=i<512?entries[i]:0;for(unsigned k=0;k<8;++k)c=(c>>1)^(0xEDB88320U & (0U-(c&1)));}c=~c;
    Partition all{d,0,d->sectors};
    mbr[446+4]=0xEE;mbr[447]=0;mbr[448]=2;mbr[449]=0;mbr[451]=mbr[452]=mbr[453]=0xFF;
    p32(mbr+454,1);p32(mbr+458,d->sectors-1>UINT32_MAX?UINT32_MAX:uint32_t(d->sectors-1));mbr[510]=0x55;mbr[511]=0xAA;
    if(!flush(all)||!write_verify(all,0,1,mbr))return false;
    // Backup first: primary header is published only after both arrays exist.
    for(unsigned pass=0;pass<2;++pass){uint64_t arr=pass?2:d->sectors-33;
        if(!write_verify(all,arr,1,entries))return false;
        for(unsigned i=1;i<32;++i)if(!write_verify(all,arr+i,1,zeroes))return false;
        zero(h,512);copy(h,"EFI PART",8);p32(h+8,0x10000);p32(h+12,92);
        p64(h+24,pass?1:d->sectors-1);p64(h+32,pass?d->sectors-1:1);p64(h+40,34);p64(h+48,d->sectors-34);
        copy(h+56,uuid,16);p64(h+72,arr);p32(h+80,128);p32(h+84,128);p32(h+88,c);p32(h+16,crc32(h,92));
        if(!write_verify(all,pass?1:d->sectors-1,1,h))return false;
    }
    return flush(all);
}
bool source_iso(Device*d){uint8_t b[512];Partition p{d,0,d?d->sectors:0};return read(p,64,1,b)&&b[0]==1&&equal(b+1,"CD001",5);}

bool marker(const Partition&p,const uint8_t uuid[16],uint64_t bytes,bool done){
    uint8_t b[512]{};if(!uuid||p.count<10||!bytes||bytes>(p.count-8)*512)return false;
    copy(b,"BSPINST1",8);p32(b+8,1);p32(b+12,done?2:1);copy(b+16,uuid,16);p64(b+32,bytes);p32(b+508,crc32(b,508));
    return write_verify(p,p.count-2,1,b)&&flush(p)&&write_verify(p,p.count-1,1,b)&&flush(p);
}
bool complete(const Partition&p,const uint8_t uuid[16]){
    uint8_t a[512],b[512],sb[512];
    return uuid&&p.count>=10&&read(p,p.count-2,1,a)&&read(p,p.count-1,1,b)&&read(p,0,1,sb)&&equal(a,b,512)&&equal(a,"BSPINST1",8)&&
        u32(a+8)==1&&u32(a+12)==2&&equal(a+16,uuid,16)&&crc32(a,508)==u32(a+508)&&
        equal(sb,"BSP2",4)&&u64(sb+4)==u64(a+32)&&u64(a+32)<=(p.count-8)*512;
}

// Installer-only FAT32: 512 MiB ESP, 4 KiB clusters, two FATs, bounded tree.
// No runtime FAT mount, no allocation from untrusted existing FAT metadata.
bool fat_install_start(const Partition&p,BootFile efi,BootFile kernel,BootFile config,
                       FatInstallState*state){
    if(!state)return false;
    *state={};
    if(!p.device||p.device->sector_size!=512||p.count!=EspBytes/512||p.start>UINT32_MAX||
       !efi.data||!efi.size||!kernel.data||!kernel.size||!config.data||!config.size)return false;
    const uint32_t total=uint32_t(p.count),fat=(total/8*4+511)/512;
    const uint32_t data_start=32+2*fat,clusters=(total-data_start)/8;
    BootFile files[]={efi,kernel,config};uint32_t start[3],length[3],next=7,total_file_clusters=0;
    for(unsigned i=0;i<3;++i){length[i]=(uint64_t(files[i].size)+4095)/4096;start[i]=next;
        if(length[i]>clusters||next>clusters+2-length[i]||total_file_clusters>UINT32_MAX-length[i])return false;
        total_file_clusters+=length[i];next+=length[i];}
    if(clusters<65525||uint64_t(clusters+2)*4>uint64_t(fat)*512)return false;
    state->partition=p;state->files[0]=efi;state->files[1]=kernel;state->files[2]=config;
    state->fat_sectors=fat;state->data_start=data_start;state->clusters=clusters;state->next_cluster=next;
    for(unsigned i=0;i<3;++i){state->file_start[i]=start[i];state->file_clusters[i]=length[i];}
    state->total_units=4+1+1+(fat+7)/8+5+total_file_clusters+1;
    return true;
}

bool fat_install_step(FatInstallState*s){
    if(!s||s->failed||s->complete)return false;
    const Partition&p=s->partition;const uint32_t fat=s->fat_sectors;
    const uint32_t data_start=s->data_start,clusters=s->clusters,next=s->next_cluster;
    const BootFile*files=s->files;const uint32_t*start=s->file_start;const uint32_t*length=s->file_clusters;
    uint8_t b[4096]{};
    bool ok=false;
    if(s->phase==0){
        ok=write_verify(p,uint64_t(s->index)*8,8,b);
        if(++s->index==4){s->phase=1;s->index=0;}
    }else if(s->phase==1){
        b[0]=0xEB;b[1]=0x58;b[2]=0x90;copy(b+3,"BURSZTYN",8);p16(b+11,512);b[13]=8;p16(b+14,32);b[16]=2;b[21]=0xF8;
    p16(b+24,63);p16(b+26,255);p32(b+28,uint32_t(p.start));p32(b+32,uint32_t(p.count));p32(b+36,fat);p32(b+44,2);
        p16(b+48,1);p16(b+50,6);b[64]=0x80;b[66]=0x29;p32(b+67,crc32(files[1].data,files[1].size));
    copy(b+71,"BURSZTYNESP",11);copy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xAA;
        ok=write_verify(p,0,1,b)&&write_verify(p,6,1,b);s->phase=2;
    }else if(s->phase==2){
        p32(b,0x41615252);p32(b+484,0x61417272);p32(b+488,clusters-(next-2));p32(b+492,next);p32(b+508,0xAA550000);
        ok=write_verify(p,1,1,b)&&write_verify(p,7,1,b);s->phase=3;s->index=0;
    }else if(s->phase==3){
        const uint32_t off=s->index;
        for(unsigned j=0;j<1024;++j){uint32_t cl=off*128+j,v=0;
            if(cl==0)v=0x0FFFFFF8;else if(cl==1|| (cl>=2&&cl<7))v=0x0FFFFFFF;
            else for(unsigned f=0;f<3;++f)if(cl>=start[f]&&cl<start[f]+length[f])v=cl+1==start[f]+length[f]?0x0FFFFFFF:cl+1;
            p32(b+4*j,v);}
        uint32_t n=fat-off<8?fat-off:8;
        ok=write_verify(p,32+off,n,b)&&write_verify(p,32+fat+off,n,b);
        s->index+=n;if(s->index>=fat){s->phase=4;s->index=0;}
    }else if(s->phase==4){
    auto dirent=[&](unsigned index,const char*name,uint32_t cl,uint32_t size,bool dir){auto*e=b+32*index;
        copy(e,name,11);e[11]=dir?0x10:0x20;p16(e+20,cl>>16);p16(e+26,cl);p32(e+28,size);p16(e+24,0x5821);};
        const uint32_t cl=2+s->index;
        if(cl==2){dirent(0,"EFI        ",3,0,true);dirent(1,"BOOT       ",5,0,true);}
        else {
            dirent(0,".          ",cl,0,true);dirent(1,"..         ",cl==4?3:(cl==6?5:0),0,true);
            if(cl==3)dirent(2,"BOOT       ",4,0,true);
            if(cl==4)dirent(2,"BOOTX64 EFI",start[0],files[0].size,false);
            if(cl==5){
                dirent(2,"GRUB       ",6,0,true);
                const char*name="system_operacyjny.bin";const char*alias="SYSTEM~1BIN";
                uint8_t sum=0;for(unsigned i=0;i<11;++i)sum=uint8_t(((sum&1)?128:0)+(sum>>1)+uint8_t(alias[i]));
                constexpr unsigned positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
                for(unsigned n=0;n<2;++n){auto*e=b+(3+n)*32;unsigned seq=2-n;e[0]=seq|(seq==2?0x40:0);e[11]=0x0F;e[13]=sum;
                    for(unsigned j=0;j<13;++j){unsigned pos=(seq-1)*13+j;p16(e+positions[j],pos<21?uint8_t(name[pos]):(pos==21?0:0xFFFF));}}
                dirent(5,alias,start[1],files[1].size,false);
            }
            if(cl==6)dirent(2,"GRUB    CFG",start[2],files[2].size,false);
        }
        ok=write_verify(p,data_start+uint64_t(cl-2)*8,8,b);
        if(++s->index==5){s->phase=5;s->index=0;s->file_index=0;}
    }else if(s->phase==5){
        const unsigned f=s->file_index;const uint32_t i=s->index;
        const uint64_t off=uint64_t(i)*4096;
        const uint32_t n=files[f].size-off>4096?4096:uint32_t(files[f].size-off);
        copy(b,files[f].data+off,n);
        ok=write_verify(p,data_start+uint64_t(start[f]+i-2)*8,8,b);
        if(++s->index==length[f]){s->index=0;if(++s->file_index==3)s->phase=6;}
    }else{
        ok=flush(p);s->complete=ok;
    }
    if(!ok){s->failed=true;return false;}
    ++s->completed_units;
    return true;
}

bool fat_install(const Partition&p,BootFile efi,BootFile kernel,BootFile config){
    FatInstallState state{};
    if(!fat_install_start(p,efi,kernel,config,&state))return false;
    while(!state.complete)if(!fat_install_step(&state))return false;
    return true;
}
}
