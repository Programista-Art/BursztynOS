#include "storage_format.h"
#ifndef BURSZTYN_PSF_HOST_TEST
#include "scheduler.h"
#endif

namespace Storage {
// ABI: b21fe73c-0414-4585-bd07-8ab4980f9c88 (UUIDv4 z systemowego generatora).
const uint8_t BspType[16]={0x3c,0xe7,0x1f,0xb2,0x14,0x04,0x85,0x45,0xbd,0x07,0x8a,0xb4,0x98,0x0f,0x9c,0x88};
const uint8_t EspType[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
static void zero(void* p,size_t n){auto*b=(uint8_t*)p;while(n--)*b++=0;}
static void copy(void* d,const void*s,size_t n){auto*a=(uint8_t*)d;auto*b=(const uint8_t*)s;while(n--)*a++=*b++;}
bool equal(const void*a,const void*b,size_t n){auto*x=(const uint8_t*)a;auto*y=(const uint8_t*)b;while(n--)if(*x++!=*y++)return false;return true;}
static uint16_t u16(const uint8_t*p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
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
static FormatterWorkspace g_workspace;
static uint32_t s_formatter_lock = 0;
static uint32_t s_formatter_depth = 0;
static int s_formatter_owner = -1;

GuardStorageFormatter::GuardStorageFormatter() : aktywny_(false), pierwotny_(false) {
#ifndef BURSZTYN_PSF_HOST_TEST
    int my_id = ::aktualny_pid;
#else
    int my_id = 1;
#endif
    if (s_formatter_owner == my_id && s_formatter_depth > 0) {
        s_formatter_depth++;
        aktywny_ = true;
        pierwotny_ = false;
        return;
    }
    while (__atomic_exchange_n(&s_formatter_lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
#if defined(__x86_64__) || defined(_M_X64)
        asm volatile("pause");
#endif
    }
    s_formatter_owner = my_id;
    s_formatter_depth = 1;
    aktywny_ = true;
    pierwotny_ = true;
}

GuardStorageFormatter::~GuardStorageFormatter() {
    if (!aktywny_) return;
    if (s_formatter_depth > 1) {
        s_formatter_depth--;
        return;
    }
    s_formatter_depth = 0;
    s_formatter_owner = -1;
    __atomic_store_n(&s_formatter_lock, 0U, __ATOMIC_RELEASE);
}

bool write_verify(const Partition&p,uint64_t s,uint32_t n,const void*b){
    if(!valid(p,s,n,b))return false;
    GuardStorageFormatter guard;
    uint8_t* check = g_workspace.write_verify_check;
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
    GuardStorageFormatter guard;
    uint8_t* h = g_workspace.gpt_h;
    uint8_t* b = g_workspace.gpt_b;
    uint8_t* sector = g_workspace.gpt_sector;
    uint8_t* backup = g_workspace.gpt_backup;
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

static void gpt_entries(const Layout&l,const uint8_t uuid[16],uint8_t out[512]){
    zero(out,512);
    auto entry=[&](unsigned off,const uint8_t*type,const Partition&p,const char*name,uint8_t salt){
        auto*e=out+off;copy(e,type,16);copy(e+16,uuid,16);e[16]^=salt;p64(e+32,p.start);p64(e+40,p.start+p.count-1);
        for(unsigned i=0;name[i]&&i<36;++i)p16(e+56+2*i,uint8_t(name[i]));
    };
    entry(0,EspType,l.esp,"EFI System Partition",1);entry(128,BspType,l.bsp,"Bursztyn OS",2);
}
static void gpt_header_sector(const GptInstallState&s,bool primary,uint8_t out[512]){
    zero(out,512);copy(out,"EFI PART",8);p32(out+8,0x10000);p32(out+12,92);
    p64(out+24,primary?1:s.device->sectors-1);p64(out+32,primary?s.device->sectors-1:1);
    p64(out+40,34);p64(out+48,s.device->sectors-34);copy(out+56,s.uuid,16);
    p64(out+72,primary?2:s.device->sectors-33);p32(out+80,128);p32(out+84,128);
    p32(out+88,s.entries_crc);p32(out+16,crc32(out,92));
}
bool gpt_install_start(Device*d,const Layout&l,const uint8_t uuid[16],GptInstallState*s){
    if(!s){
        return false;
    }
    *s={};
    Layout expected{};
    if(!uuid||!plan(d,&expected)||l.esp.device!=d||l.bsp.device!=d||l.esp.start!=expected.esp.start||
       l.esp.count!=expected.esp.count||l.bsp.start!=expected.bsp.start||l.bsp.count!=expected.bsp.count)return false;
    s->device=d;s->layout=l;copy(s->uuid,uuid,16);uint8_t entries[512]{};
    gpt_entries(l,uuid,entries);uint32_t c=~0U;
    for(unsigned i=0;i<16384;++i){c^=i<512?entries[i]:0;for(unsigned bit=0;bit<8;++bit)c=(c>>1)^(0xEDB88320U&(0U-(c&1)));}
    s->entries_crc=~c;s->total_units=105;return true;
}
bool gpt_install_step(GptInstallState*s){
    if(!s||s->failed||s->complete||!s->device)return false;
    Partition all{s->device,0,s->device->sectors};uint8_t b[512]{},expected[512]{};bool ok=false;
    if(s->phase==0){ok=flush(s->layout.bsp);s->phase=1;}
    else if(s->phase==1){GuardStorageFormatter guard;uint8_t* z=g_workspace.gpt_install_z;zero(z,4096);ok=write_verify(s->layout.bsp,0,8,z);s->phase=2;}
    else if(s->phase==2){ok=flush(s->layout.bsp);s->phase=3;}
    else if(s->phase==3){ok=flush(all);s->phase=4;}
    else if(s->phase==4){b[450]=0xEE;b[447]=0;b[448]=2;b[451]=b[452]=b[453]=0xFF;p32(b+454,1);
        p32(b+458,s->device->sectors-1>UINT32_MAX?UINT32_MAX:uint32_t(s->device->sectors-1));b[510]=0x55;b[511]=0xAA;
        ok=write_verify(all,0,1,b);s->phase=5;s->index=0;}
    else if(s->phase==5||s->phase==7){const bool primary=s->phase==7;const uint64_t base=primary?2:s->device->sectors-33;
        if(s->index==0){
            gpt_entries(s->layout,s->uuid,b);
        }
        ok=write_verify(all,base+s->index,1,b);
        if(++s->index==32){s->phase+=1;s->index=0;}}
    else if(s->phase==6||s->phase==8){gpt_header_sector(*s,s->phase==8,b);
        ok=write_verify(all,s->phase==8?1:s->device->sectors-1,1,b);++s->phase;}
    else if(s->phase==9){ok=flush(all);s->phase=10;s->index=0;}
    else if(s->phase==10){const uint64_t a=2+s->index,c=s->device->sectors-33+s->index;
        if(s->index==0)gpt_entries(s->layout,s->uuid,expected);
        ok=read(all,a,1,b)&&equal(b,expected,512)&&read(all,c,1,b)&&equal(b,expected,512);
        if(++s->index==32){s->phase=11;s->index=0;}}
    else if(s->phase==11){uint8_t h[512],backup[512];gpt_header_sector(*s,true,expected);
        ok=read(all,1,1,h)&&equal(h,expected,512);gpt_header_sector(*s,false,expected);
        ok=ok&&read(all,s->device->sectors-1,1,backup)&&equal(backup,expected,512);s->phase=12;}
    else {s->complete=true;return true;}
    if(!ok){s->failed=true;return false;}++s->completed_units;return true;
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

// Installer FAT32 z obsluga slotow A/B: 512 MiB ESP, 4 KiB klastry, dwa FAT-y.
static uint8_t default_grubenv_bytes[1024];
static bool default_grubenv_init = false;

static void inicjalizuj_domyslny_grubenv(){
    if (default_grubenv_init) return;
    for (int i = 0; i < 1024; ++i) default_grubenv_bytes[i] = '#';
    const char tekst[] =
        "# GRUB Environment Block\n"
        "bursztyn_potwierdzony=A\n"
        "bursztyn_oczekujacy=BRAK\n"
        "bursztyn_proby=0\n";
    for (size_t i = 0; tekst[i]; ++i) default_grubenv_bytes[i] = (uint8_t)tekst[i];
    default_grubenv_init = true;
}

bool fat_install_start(const Partition&p,BootFile efi,BootFile kernel,BootFile config,
                       FatInstallState*state){
    if(!state)return false;
    *state={};
    inicjalizuj_domyslny_grubenv();
    if(!p.device||p.device->sector_size!=512||p.count!=EspBytes/512||p.start>UINT32_MAX||
       !efi.data||!efi.size||!kernel.data||!kernel.size||!config.data||!config.size)return false;
    const uint32_t total=uint32_t(p.count),fat=(total/8*4+511)/512;
    const uint32_t data_start=32+2*fat,clusters=(total-data_start)/8;
    BootFile files[5]={efi,kernel,kernel,config,{default_grubenv_bytes,1024}};
    uint32_t start[5],length[5],next=7,total_file_clusters=0;
    for(unsigned i=0;i<5;++i){length[i]=(uint64_t(files[i].size)+4095)/4096;start[i]=next;
        if(length[i]>clusters||next>clusters+2-length[i]||total_file_clusters>UINT32_MAX-length[i])return false;
        total_file_clusters+=length[i];next+=length[i];}
    if(clusters<65525||uint64_t(clusters+2)*4>uint64_t(fat)*512)return false;
    state->partition=p;
    for(unsigned i=0;i<5;++i){
        state->files[i]=files[i];
        state->file_start[i]=start[i];
        state->file_clusters[i]=length[i];
    }
    state->fat_sectors=fat;state->data_start=data_start;state->clusters=clusters;state->next_cluster=next;
    const uint32_t crc_units=(kernel.size+16383U)/16384U;
    state->boot_crc_state=~0U;
    state->total_units=4+crc_units+1+1+(fat+7)/8+5+total_file_clusters+1;
    return true;
}

bool fat_install_step(FatInstallState*s){
    if(!s||s->failed||s->complete)return false;
    GuardStorageFormatter guard;
    const Partition&p=s->partition;const uint32_t fat=s->fat_sectors;
    const uint32_t data_start=s->data_start,clusters=s->clusters,next=s->next_cluster;
    const BootFile*files=s->files;const uint32_t*start=s->file_start;const uint32_t*length=s->file_clusters;
    uint8_t* b = g_workspace.fat_install_b;
    zero(b, 4096);
    bool ok=false;
    if(s->phase==0){
        ok=write_verify(p,uint64_t(s->index)*8,8,b);
        if(++s->index==4){s->phase=1;s->index=0;}
    }else if(s->phase==1){
        const uint32_t left=files[1].size-s->boot_crc_offset;
        const uint32_t n=left>16384U?16384U:left;
        const uint8_t*src=files[1].data+s->boot_crc_offset;
        uint32_t crc=s->boot_crc_state;
        for(uint32_t i=0;i<n;++i){crc^=src[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xEDB88320U&uint32_t(-int32_t(crc&1)));}
        s->boot_crc_state=crc;s->boot_crc_offset+=n;ok=true;
        if(s->boot_crc_offset==files[1].size){s->boot_crc_state=~crc;s->phase=2;}
    }else if(s->phase==2){
        b[0]=0xEB;b[1]=0x58;b[2]=0x90;copy(b+3,"BURSZTYN",8);p16(b+11,512);b[13]=8;p16(b+14,32);b[16]=2;b[21]=0xF8;
        p16(b+24,63);p16(b+26,255);p32(b+28,uint32_t(p.start));p32(b+32,uint32_t(p.count));p32(b+36,fat);p32(b+44,2);
        p16(b+48,1);p16(b+50,6);b[64]=0x80;b[66]=0x29;p32(b+67,s->boot_crc_state);
        copy(b+71,"BURSZTYNESP",11);copy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xAA;
        ok=write_verify(p,0,1,b)&&write_verify(p,6,1,b);s->phase=3;
    }else if(s->phase==3){
        p32(b,0x41615252);p32(b+484,0x61417272);p32(b+488,clusters-(next-2));p32(b+492,next);p32(b+508,0xAA550000);
        ok=write_verify(p,1,1,b)&&write_verify(p,7,1,b);s->phase=4;s->index=0;
    }else if(s->phase==4){
        const uint32_t off=s->index;
        for(unsigned j=0;j<1024;++j){uint32_t cl=off*128+j,v=0;
            if(cl==0)v=0x0FFFFFF8;else if(cl==1|| (cl>=2&&cl<7))v=0x0FFFFFFF;
            else for(unsigned f=0;f<5;++f)if(cl>=start[f]&&cl<start[f]+length[f])v=cl+1==start[f]+length[f]?0x0FFFFFFF:cl+1;
            p32(b+4*j,v);}
        uint32_t n=fat-off<8?fat-off:8;
        ok=write_verify(p,32+off,n,b)&&write_verify(p,32+fat+off,n,b);
        s->index+=n;if(s->index>=fat){s->phase=5;s->index=0;}
    }else if(s->phase==5){
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
                dirent(6,"JADRO_A BIN",start[1],files[1].size,false);
                dirent(7,"JADRO_B BIN",start[2],files[2].size,false);
            }
            if(cl==6){
                dirent(2,"GRUB    CFG",start[3],files[3].size,false);
                dirent(3,"GRUBENV    ",start[4],files[4].size,false);
            }
        }
        ok=write_verify(p,data_start+uint64_t(cl-2)*8,8,b);
        if(++s->index==5){s->phase=6;s->index=0;s->file_index=0;}
    }else if(s->phase==6){
        const unsigned f=s->file_index;const uint32_t i=s->index;
        const uint64_t off=uint64_t(i)*4096;
        const uint32_t n=files[f].size-off>4096?4096:uint32_t(files[f].size-off);
        copy(b,files[f].data+off,n);
        ok=write_verify(p,data_start+uint64_t(start[f]+i-2)*8,8,b);
        if(++s->index==length[f]){s->index=0;if(++s->file_index==5)s->phase=7;}
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

bool esp_odczytaj_uklad(const Partition& esp, EspLayout* out) {
    if (!out || !esp.device || esp.device->sector_size != 512 || esp.count < 65536) return false;
    uint8_t boot[512];
    if (!read(esp, 0, 1, boot)) return false;
    if (!equal(boot + 3, "BURSZTYN", 8) || !equal(boot + 82, "FAT32   ", 8)) return false;

    uint32_t fat = u32(boot + 36);
    uint32_t data_start = 32 + 2 * fat;
    uint32_t spc = boot[13];
    if (spc != 8 || fat == 0) return false;

    GuardStorageFormatter guard;
    uint8_t* boot_dir = g_workspace.esp_boot_dir;
    uint64_t cl5_lba = data_start + uint64_t(5 - 2) * spc;
    if (!read(esp, cl5_lba, 8, boot_dir)) return false;

    uint8_t* grub_dir = g_workspace.esp_grub_dir;
    uint64_t cl6_lba = data_start + uint64_t(6 - 2) * spc;
    if (!read(esp, cl6_lba, 8, grub_dir)) return false;

    uint32_t cl_a = 0, sz_a = 0, off_a = 0;
    uint32_t cl_b = 0, sz_b = 0, off_b = 0;
    bool found_a = false, found_b = false;

    for (unsigned i = 0; i < 128; ++i) {
        const uint8_t* e = boot_dir + 32 * i;
        if (equal(e, "JADRO_A BIN", 11)) {
            cl_a = uint32_t(u16(e + 20)) << 16 | uint32_t(u16(e + 26));
            sz_a = u32(e + 28);
            off_a = 32 * i;
            found_a = true;
        } else if (equal(e, "JADRO_B BIN", 11)) {
            cl_b = uint32_t(u16(e + 20)) << 16 | uint32_t(u16(e + 26));
            sz_b = u32(e + 28);
            off_b = 32 * i;
            found_b = true;
        }
    }

    uint32_t cl_env = 0;
    bool found_env = false;
    for (unsigned i = 0; i < 128; ++i) {
        const uint8_t* e = grub_dir + 32 * i;
        if (equal(e, "GRUBENV    ", 11)) {
            cl_env = uint32_t(u16(e + 20)) << 16 | uint32_t(u16(e + 26));
            found_env = true;
        }
    }

    if (!found_a || !found_b || !found_env) return false;
    if (cl_a < 7 || cl_b <= cl_a || cl_env <= cl_b) return false;

    uint32_t len_a = cl_b - cl_a;
    uint32_t len_b = len_a;

    out->data_start_lba = data_start;
    out->cluster_size_sectors = spc;
    out->jadro_a_lba = data_start + uint64_t(cl_a - 2) * spc;
    out->jadro_a_clusters = len_a;
    out->jadro_a_max_bytes = len_a * 4096;
    out->jadro_a_size = sz_a;
    out->jadro_b_lba = data_start + uint64_t(cl_b - 2) * spc;
    out->jadro_b_clusters = len_b;
    out->jadro_b_max_bytes = len_b * 4096;
    out->jadro_b_size = sz_b;
    out->grubenv_lba = data_start + uint64_t(cl_env - 2) * spc;
    out->dirent_boot_lba = cl5_lba;
    out->dirent_jadro_a_offset = off_a;
    out->dirent_jadro_b_offset = off_b;

    return range(out->jadro_a_lba, uint64_t(len_a) * spc, esp.count) &&
           range(out->jadro_b_lba, uint64_t(len_b) * spc, esp.count) &&
           range(out->grubenv_lba, 2, esp.count);
}

bool esp_zapisz_jadro_slotu(const Partition& esp, uint8_t slot, const uint8_t* dane, uint32_t rozmiar) {
    if (!dane || rozmiar == 0) return false;
    EspLayout l{};
    if (!esp_odczytaj_uklad(esp, &l)) return false;

    uint64_t target_lba = (slot == 0) ? l.jadro_a_lba : l.jadro_b_lba;
    uint32_t max_bytes  = (slot == 0) ? l.jadro_a_max_bytes : l.jadro_b_max_bytes;
    uint32_t dirent_off = (slot == 0) ? l.dirent_jadro_a_offset : l.dirent_jadro_b_offset;

    if (rozmiar > max_bytes) return false;

    GuardStorageFormatter guard;
    uint8_t* blk = g_workspace.esp_slot_blk;
    uint32_t zapisano = 0;
    while (zapisano < rozmiar) {
        uint32_t porcja = (rozmiar - zapisano > 4096) ? 4096 : (rozmiar - zapisano);
        copy(blk, dane + zapisano, porcja);
        if (porcja < 4096) zero(blk + porcja, 4096 - porcja);
        uint32_t sektory = (porcja + 511) / 512;
        uint64_t lba = target_lba + uint64_t(zapisano / 512);
        if (!write_verify(esp, lba, sektory, blk)) return false;
        zapisano += porcja;
    }

    if (!flush(esp)) return false;

    uint8_t* s_boot_dir = g_workspace.esp_slot_boot_dir;
    if (!read(esp, l.dirent_boot_lba, 8, s_boot_dir)) return false;
    p32(s_boot_dir + dirent_off + 28, rozmiar);
    if (!write_verify(esp, l.dirent_boot_lba, 8, s_boot_dir)) return false;
    return flush(esp);
}

bool esp_odczytaj_jadro_slotu(const Partition& esp, uint8_t slot, uint8_t* bufor, uint32_t maks_rozmiar, uint32_t* odczytano) {
    if (!bufor || maks_rozmiar == 0) return false;
    EspLayout l{};
    if (!esp_odczytaj_uklad(esp, &l)) return false;

    uint64_t target_lba = (slot == 0) ? l.jadro_a_lba : l.jadro_b_lba;
    uint32_t target_size = (slot == 0) ? l.jadro_a_size : l.jadro_b_size;
    if (target_size > maks_rozmiar) return false;

    uint8_t sec[512];
    uint32_t przeczytano = 0;
    while (przeczytano < target_size) {
        uint64_t lba = target_lba + (przeczytano / 512);
        if (!read(esp, lba, 1, sec)) return false;
        uint32_t porcja = (target_size - przeczytano > 512) ? 512 : (target_size - przeczytano);
        copy(bufor + przeczytano, sec, porcja);
        przeczytano += porcja;
    }
    if (odczytano) *odczytano = target_size;
    return true;
}

bool esp_zapisz_grubenv(const Partition& esp, const char* potwierdzony, const char* oczekujacy, uint32_t proby) {
    GuardStorageFormatter guard;
    EspLayout l{};
    if (!esp_odczytaj_uklad(esp, &l)) return false;

    uint8_t* env = g_workspace.esp_write_env;
    for (int i = 0; i < 1024; ++i) env[i] = '#';

    const char header[] = "# GRUB Environment Block\n";
    size_t p = 0;
    for (size_t i = 0; header[i]; ++i) env[p++] = (uint8_t)header[i];

    auto dodaj_tekst = [&](const char* k, const char* v) {
        for (size_t i = 0; k[i] && p < 1000; ++i) env[p++] = (uint8_t)k[i];
        env[p++] = '=';
        for (size_t i = 0; v[i] && p < 1000; ++i) env[p++] = (uint8_t)v[i];
        env[p++] = '\n';
    };

    dodaj_tekst("bursztyn_potwierdzony", potwierdzony ? potwierdzony : "A");
    dodaj_tekst("bursztyn_oczekujacy", oczekujacy ? oczekujacy : "BRAK");

    char proby_str[16];
    int pl = 0;
    if (proby == 0) proby_str[pl++] = '0';
    else {
        char rev[16]; int rl = 0;
        uint32_t v = proby;
        while (v > 0) { rev[rl++] = char('0' + v % 10); v /= 10; }
        while (rl > 0) proby_str[pl++] = rev[--rl];
    }
    proby_str[pl] = 0;
    dodaj_tekst("bursztyn_proby", proby_str);

    if (!write_verify(esp, l.grubenv_lba, 2, env)) return false;
    return flush(esp);
}

bool esp_odczytaj_grubenv(const Partition& esp, char* potwierdzony, char* oczekujacy, uint32_t* proby) {
    if (potwierdzony) { potwierdzony[0] = 'A'; potwierdzony[1] = 0; }
    if (oczekujacy) { copy(oczekujacy, "BRAK", 5); }
    if (proby) *proby = 0;

    GuardStorageFormatter guard;
    EspLayout l{};
    if (!esp_odczytaj_uklad(esp, &l)) return false;

    uint8_t* env = g_workspace.esp_read_env;
    if (!read(esp, l.grubenv_lba, 2, env)) return false;
    if (!equal(env, "# GRUB Environment Block\n", 25)) return false;

    size_t i = 25;
    while (i < 1024 && env[i] != '#') {
        size_t start = i;
        while (i < 1024 && env[i] != '\n') ++i;
        size_t line_len = i - start;
        if (i < 1024) ++i;

        size_t eq = 0;
        while (eq < line_len && env[start + eq] != '=') ++eq;
        if (eq >= line_len) continue;

        const uint8_t* k = env + start;
        const uint8_t* v = env + start + eq + 1;
        size_t v_len = line_len - eq - 1;

        if (eq == 21 && equal(k, "bursztyn_potwierdzony", 21)) {
            if (potwierdzony && v_len > 0) {
                potwierdzony[0] = (char)v[0];
                potwierdzony[1] = 0;
            }
        } else if (eq == 19 && equal(k, "bursztyn_oczekujacy", 19)) {
            if (oczekujacy && v_len < 16) {
                for (size_t j = 0; j < v_len; ++j) oczekujacy[j] = (char)v[j];
                oczekujacy[v_len] = 0;
            }
        } else if (eq == 14 && equal(k, "bursztyn_proby", 14)) {
            if (proby) {
                uint32_t num = 0;
                for (size_t j = 0; j < v_len; ++j) {
                    if (v[j] >= '0' && v[j] <= '9') num = num * 10 + (v[j] - '0');
                }
                *proby = num;
            }
        }
    }
    return true;
}

bool rozruch_zapisz_sektory(Device* d, const bursztyn_rozruch_stan* s) {
    if (!d || !d->writable || !s || !rozruch_waliduj_rekord(s)) return false;
    Partition all{d, 0, d->sectors};
    if (d->sectors <= ROZRUCH_SEKTOR_KOPIA_1) return false;

    bursztyn_rozruch_stan k0{}, k1{};
    bool v0 = read(all, ROZRUCH_SEKTOR_KOPIA_0, 1, &k0) && rozruch_waliduj_rekord(&k0);
    bool v1 = read(all, ROZRUCH_SEKTOR_KOPIA_1, 1, &k1) && rozruch_waliduj_rekord(&k1);

    uint64_t pierwsza = ROZRUCH_SEKTOR_KOPIA_0;
    uint64_t druga    = ROZRUCH_SEKTOR_KOPIA_1;
    if (v0 && !v1) {
        /* Kopia 1 jest uszkodzona - nadpisujemy najpierw uszkodzona, chroniac wazna kopie 0 */
        pierwsza = ROZRUCH_SEKTOR_KOPIA_1;
        druga    = ROZRUCH_SEKTOR_KOPIA_0;
    } else if (v0 && v1 && (int64_t)(k0.generacja - k1.generacja) > 0) {
        /* Obie poprawne, kopia 0 jest nowsza - nadpisujemy starsza kopie 1 w pierwszej kolejnosci */
        pierwsza = ROZRUCH_SEKTOR_KOPIA_1;
        druga    = ROZRUCH_SEKTOR_KOPIA_0;
    }

    if (!write_verify(all, pierwsza, 1, s) || !flush(all)) return false;
    if (!write_verify(all, druga, 1, s) || !flush(all)) return false;

    return true;
}

bool rozruch_odczytaj_sektory(Device* d, bursztyn_rozruch_stan* s) {
    if (!d || !s || d->sectors <= ROZRUCH_SEKTOR_KOPIA_1) return false;
    Partition all{d, 0, d->sectors};
    bursztyn_rozruch_stan k0{}, k1{};
    bool r0 = read(all, ROZRUCH_SEKTOR_KOPIA_0, 1, &k0);
    bool r1 = read(all, ROZRUCH_SEKTOR_KOPIA_1, 1, &k1);
    if (!r0 && !r1) return false;

    return rozruch_wybierz_poprawna_kopie(r0 ? &k0 : nullptr, r1 ? &k1 : nullptr, s);
}

bool rozruch_weryfikuj_uklad_dysku(Device* d) {
    if (!d || d->sector_size != 512 || d->sectors <= ROZRUCH_SEKTOR_KOPIA_1) {
        return false;
    }

    GuardStorageFormatter guard;
    Table* table = &g_workspace.rozruch_table;
    *table = {};
    if (!gpt_read(d, table)) {
        return false;
    }

    /* 1. Weryfikacja, ze metadane podstawowego GPT nie nachodza na LBA 2046/2047 */
    Partition all{d, 0, d->sectors};
    uint8_t* h = g_workspace.rozruch_h;
    zero(h, 512);
    if (!read(all, 1, 1, h) || !equal(h, "EFI PART", 8)) {
        return false;
    }
    uint64_t arr_lba = u64(h + 72);
    uint32_t num_entries = u32(h + 80);
    uint32_t entry_size = u32(h + 84);
    if (entry_size != 128 || num_entries == 0 || num_entries > 128) {
        return false;
    }
    uint64_t arr_sectors = (uint64_t(num_entries) * entry_size + 511) / 512;
    uint64_t arr_end_lba = arr_lba + arr_sectors - 1;

    /* Metadane GPT (naglowek i tablica wpisow) musza konczyc sie scisle przed LBA 2046 */
    if (arr_lba <= ROZRUCH_SEKTOR_KOPIA_1 && arr_end_lba >= ROZRUCH_SEKTOR_KOPIA_0) {
        return false;
    }
    if (arr_end_lba >= ROZRUCH_SEKTOR_KOPIA_0) {
        return false;
    }

    /* 2. Weryfikacja partycji: zadna partycja nie moze nachodzic na zarezerwowane sektory LBA 2046/2047 */
    bool esp_znalezione = false;
    bool bsp_znalezione = false;
    uint64_t esp_start = 0;

    for (uint32_t i = 0; i < table->count; ++i) {
        const Entry& e = table->entries[i];
        if (e.count == 0) continue;
        uint64_t p_start = e.start;
        uint64_t p_end = e.start + e.count - 1;

        /* Zadna partycja nie moze nachodzic na zakres [2046, 2047] */
        if (p_start <= ROZRUCH_SEKTOR_KOPIA_1 && p_end >= ROZRUCH_SEKTOR_KOPIA_0) {
            return false;
        }

        if (equal(e.type, EspType, 16)) {
            esp_znalezione = true;
            esp_start = p_start;
        } else if (equal(e.type, BspType, 16)) {
            bsp_znalezione = true;
        }
    }

    /* 3. ESP musi zaczynac sie na lub za LBA 2048 (granica 1 MiB) */
    if (!esp_znalezione || esp_start < EspMinStartSector) {
        return false;
    }

    /* 4. Oczekiwany uklad Bursztyn OS: obecna partycja ESP oraz partycja systemowa BSP */
    if (!bsp_znalezione) {
        return false;
    }

    return true;
}
}

