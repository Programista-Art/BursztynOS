#include "../../diagnostyka_systemu.h"
// OHCI 1.0 root-port HID driver. Polled schedules, no IRQ callbacks. DMA
// objects have controller lifetime, including failed enumeration and unplug.
#include "sterowniki/usb/ohci.h"
#include "sterowniki/usb/hid_pointer.h"
#include "sterowniki/usb/usb_hid.h"
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "pci.h"
#include "pamiec.h"
#include "pointing_diag.h"
#include "xhci_pointing_diag.h"

void wypisz_log(const char*);
namespace {
constexpr unsigned MAX_DEVICES=16, MAX_PORTS=15;
constexpr uint64_t MMIO=0x144000000ULL, TIMEOUT=1000000000ULL;
struct alignas(16) ED { volatile uint32_t info,tail,head,next; };
struct alignas(16) TD { volatile uint32_t info,cbp,next,end; };
struct alignas(256) HCCA { volatile uint32_t table[32];volatile uint16_t frame,pad;volatile uint32_t done;uint8_t reserved[120]; };
static_assert(sizeof(HCCA)==256,"HCCA");
struct Device {
    DmaBuffer memory{},data{};
    ED* control{};ED* interrupt{};TD* td{};
    bool retired[6]{},used{},ready{},low{},pending{},keyboard{};
    uint8_t port{},address{},mps{8},tail{},active{},interval{1};
    uint16_t packet{};UsbHidInterface iface{};HidPointerLayout layout{};InputDevice input{};
};
volatile uint32_t* regs{};DmaBuffer hcca_dma{};HCCA* hcca{};
Device devices[MAX_DEVICES]{};unsigned ports{},used{};bool ready{};uint32_t diagnostic_id{};
#ifdef BURSZTYN_USB_HOST_TEST
uint32_t ohci_test_read(unsigned);
void ohci_test_write(unsigned,uint32_t);
uint32_t read(unsigned offset){return ohci_test_read(offset);}
void write(unsigned offset,uint32_t v){ohci_test_write(offset,v);}
#else
uint32_t read(unsigned offset){__atomic_thread_fence(__ATOMIC_SEQ_CST);return regs[offset/4];}
void write(unsigned offset,uint32_t v){__atomic_thread_fence(__ATOMIC_RELEASE);regs[offset/4]=v;__atomic_thread_fence(__ATOMIC_SEQ_CST);}
#endif
uint32_t phys(const Device& d,const void* p){return uint32_t(d.memory.physical_address+ (reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(d.memory.virtual_address)));}
bool allocate32(size_t n,DmaBuffer& b){
    if(!dma_allocate(n,4096,&b))return false;
    if(b.physical_address>UINT32_MAX || b.size-1>UINT32_MAX-b.physical_address){dma_release(&b);return false;}
    auto* p=static_cast<uint8_t*>(b.virtual_address);for(size_t i=0;i<b.size;++i)p[i]=0;return true;
}
bool wait_bits(unsigned offset,uint32_t mask,uint32_t value){
    const uint64_t start=czas_monotoniczny_ns();
    for(unsigned spins=0;spins<10000000;++spins){if((read(offset)&mask)==value)return true;
        if(czas_monotoniczny_ns()-start>=TIMEOUT)return false;
        asm volatile("pause");}
    return false;
}
bool frames(unsigned count){
    const uint16_t start=hcca->frame;const uint64_t time=czas_monotoniczny_ns();
    for(unsigned spins=0;spins<10000000;++spins){if(uint16_t(hcca->frame-start)>=count)return true;
        if(czas_monotoniczny_ns()-time>=TIMEOUT)return false;
        asm volatile("pause");}
    return false;
}
void fail_controller(){
    diag_set_status(diagnostic_id,DIAG_INIT_FAILED,DIAG_CODE_HID_INIT,"Kontroler OHCI zatrzymany; sprawdź log USB");
    ready=false;write(4,0); // reset state stops USB schedules; retain all DMA
    for(auto& d:devices)if(d.used){d.ready=false;hid_pointer_remove(d.input);}
    wypisz_log("[OHCI] Controller failure; DMA retained, PS/2 fallback available.");
}
void done_queue(){
    if(!(read(0x0C)&2))return;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);uint32_t p=hcca->done&~15U;hcca->done=0;
    for(unsigned budget=0;p&&budget<MAX_DEVICES*6;++budget){
        TD* found=nullptr;
        for(auto& d:devices)if(d.used)for(unsigned i=0;i<6;++i)
            if(phys(d,d.td+i)==p){found=d.td+i;d.retired[i]=true;}
        if(!found){fail_controller();return;}p=found->next&~15U;
    }
    if(p){fail_controller();return;}write(0x0C,2);
}
void set_td(Device& d,unsigned i,uint32_t info,uint32_t buffer,uint32_t length,unsigned next){
    TD& t=d.td[i];d.retired[i]=false;t.info=0xF0000000U|info;t.cbp=length?buffer:0;
    t.end=length?buffer+length-1:0;t.next=phys(d,d.td+next);
}
bool control(Device& d,uint8_t request_type,uint8_t request,uint16_t value,uint16_t index,uint16_t length,size_t& actual){
    actual=0;if(length>4096||!ready)return false;
    // Control list is serialized by usb_service. Hardware may retain CurrentED
    // until a frame boundary; skip and disable before rebuilding this ED.
    d.control->info|=1U<<14;write(4,read(4)&~(1U<<4));
    if(!frames(2)){fail_controller();return false;}
    done_queue();if(!ready)return false;
    auto* setup=static_cast<uint8_t*>(d.memory.virtual_address)+256;
    setup[0]=request_type;setup[1]=request;setup[2]=uint8_t(value);setup[3]=uint8_t(value>>8);
    setup[4]=uint8_t(index);setup[5]=uint8_t(index>>8);setup[6]=uint8_t(length);setup[7]=uint8_t(length>>8);
    const uint32_t setup_phys=uint32_t(d.memory.physical_address+256),data_phys=uint32_t(d.data.physical_address);
    set_td(d,0,2U<<24,setup_phys,8,length?1:2);
    if(length)set_td(d,1,(3U<<24)|(1U<<18)|((request_type&0x80)?(2U<<19):(1U<<19)),data_phys,length,2);
    set_td(d,2,(3U<<24)|((length&&(request_type&0x80))?(1U<<19):(2U<<19)),0,0,3);
    d.td[3]={};d.control->tail=phys(d,d.td+3);d.control->head=phys(d,d.td);
    d.control->next=0;d.control->info=d.address|(d.low?1U<<13:0)|(uint32_t(d.mps)<<16);
    write(0x20,phys(d,d.control));write(0x24,0);write(4,read(4)|(1U<<4));write(8,2);
    const uint64_t start=czas_monotoniczny_ns();bool completed=false;
    for(unsigned spins=0;spins<10000000;++spins){
        done_queue();if(!ready)break;
        if(d.retired[2]){completed=true;break;}
        if((d.control->head&1)||!(read(0x54+4*d.port)&1)||czas_monotoniczny_ns()-start>=TIMEOUT)break;
        asm volatile("pause");
    }
    d.control->info|=1U<<14;write(4,read(4)&~(1U<<4));
    if(!frames(2)){fail_controller();return false;}done_queue();
    if(!completed || (d.td[0].info>>28) || (d.td[2].info>>28) || (length&&(d.td[1].info>>28)))return false;
    uint32_t remaining=0;
    if(length&&d.td[1].cbp){const uint32_t cbp=d.td[1].cbp;
        if(cbp<data_phys||cbp>data_phys+length)return false;
        remaining=data_phys+length-cbp;}
    actual=length-remaining;return true;
}
void rebuild_periodic(){
    // Shared suffix tree: longer periods precede shorter ones. All branches use
    // phase zero, which is sufficient for the bounded root-port HID workload.
    write(4,read(4)&~(1U<<2));if(!frames(2)){fail_controller();return;}done_queue();
    Device* sorted[MAX_DEVICES]{};unsigned n=0;
    for(auto& d:devices)if(d.ready){unsigned j=n;while(j&&sorted[j-1]->interval<d.interval){sorted[j]=sorted[j-1];--j;}sorted[j]=&d;++n;}
    for(unsigned i=0;i<n;++i)sorted[i]->interrupt->next=i+1<n?phys(*sorted[i+1],sorted[i+1]->interrupt):0;
    for(unsigned frame=0;frame<32;++frame){hcca->table[frame]=0;
        for(unsigned i=0;i<n;++i)if(frame%sorted[i]->interval==0){hcca->table[frame]=phys(*sorted[i],sorted[i]->interrupt);break;}}
    write(4,read(4)|(1U<<2));
}
void queue_interrupt(Device& d){
    const unsigned current=d.tail,next=current==4?5:4;
    set_td(d,current, (2U<<19)|(1U<<18),uint32_t(d.data.physical_address),d.packet,next);
    d.td[next]={};d.active=uint8_t(current);d.tail=uint8_t(next);d.pending=true;
    __atomic_thread_fence(__ATOMIC_RELEASE);d.interrupt->tail=phys(d,d.td+next);
}
bool enumerate(unsigned port){
    if(used==MAX_DEVICES)return false;
    const unsigned offset=0x54+4*port;uint32_t status=read(offset);if(!(status&1))return false;
    write(offset,1U<<8); // port power
    // Respect root-hub power-on to power-good delay, expressed in 2ms units.
    if(!frames(2U*((read(0x48)>>24)&255U)+2))return false;
    write(offset,1U<<4);
    if(!wait_bits(offset,1U<<4,0)||!(read(offset)&2))return false;
    write(offset,0x1F0000);
    Device& d=devices[used++];
    if(!allocate32(4096,d.memory)||!allocate32(4096,d.data))return false;
    d.used=true;d.port=uint8_t(port);d.low=read(offset)&(1U<<9);
    auto* base=static_cast<uint8_t*>(d.memory.virtual_address);d.control=reinterpret_cast<ED*>(base);
    d.interrupt=reinterpret_cast<ED*>(base+16);d.td=reinterpret_cast<TD*>(base+32);
    size_t got=0;auto* data=static_cast<uint8_t*>(d.data.virtual_address);
    if(!control(d,0x80,6,0x100,0,8,got)||got<8||data[0]!=18||data[1]!=1)return false;
    const uint8_t mps=data[7];if((d.low&&mps!=8)||(!d.low&&mps!=8&&mps!=16&&mps!=32&&mps!=64))return false;
    d.mps=mps;const uint8_t address=uint8_t(used);
    if(!control(d,0,5,address,0,0,got)||!frames(2))return false;
    d.address=address;
    if(!control(d,0x80,6,0x100,0,18,got)||got<18||data[0]!=18||data[1]!=1)return false;
#if BURSZTYNDIAGUSBHID
    const int64_t identity[]={uint16_t(data[8])|(uint16_t(data[9])<<8),uint16_t(data[10])|(uint16_t(data[11])<<8),port};
    pointing_diag("[OHCI HID] VID/PID/root-port",identity,3);
#endif
    if(!control(d,0x80,6,0x200,0,9,got)||got<9||data[0]!=9||data[1]!=2)return false;
    const uint16_t total=uint16_t(data[2])|uint16_t(data[3])<<8;
    if(total<9||total>4096||!control(d,0x80,6,0x200,0,total,got)||!usb_hid_find_interface(data,got,d.iface))return false;
    if(d.iface.packet_size>(d.low?8:64))return false;
    if(!control(d,0,9,d.iface.configuration,0,0,got))return false;
    d.keyboard=d.iface.subclass==1&&d.iface.protocol==1;
    if(d.keyboard){if(!control(d,0x21,0x0B,0,d.iface.interface_number,0,got))return false;}
    else {
        if(!control(d,0x81,6,0x2200,d.iface.interface_number,d.iface.report_length,got)||
           got!=d.iface.report_length||!hid_pointer_compile(data,got,d.layout))return false;
        if(d.iface.subclass==1&&!control(d,0x21,0x0B,1,d.iface.interface_number,0,got))return false;
    }
    for(unsigned i=0;i<d.layout.id_count;++i)
        if((d.layout.bits[i]+7U)/8+(d.layout.numbered?1U:0U)>d.iface.packet_size)return false;
    d.packet=d.iface.packet_size;unsigned interval=1;
    while(interval<32&&interval*2<=d.iface.interval)interval*=2;
    d.interval=uint8_t(interval);
    d.interrupt->info=d.address|((d.iface.endpoint&15U)<<7)|(2U<<11)|(d.low?1U<<13:0)|(uint32_t(d.packet)<<16);
    d.tail=4;d.interrupt->head=phys(d,d.td+4);d.interrupt->tail=d.interrupt->head;
    d.input.usb_controller=INPUT_USB_CONTROLLER_OHCI;
    if(!d.keyboard && !hid_pointer_register(d.input,d.layout,d.address))return false;
    queue_interrupt(d);if(!d.keyboard)input_arm_pointer(&d.input);
    d.ready=true;rebuild_periodic();
    wypisz_log("[OHCI] Root-port HID interrupt IN configured.");return true;
}
}
bool ohci_initialize(){
    if(regs||!hpet_dostepny())return false;
    for(unsigned bus=0;bus<256;++bus)for(uint8_t slot=0;slot<32;++slot)for(uint8_t fn=0;fn<8;++fn){
        if(pci_vendor_id(uint8_t(bus),slot,fn)==0xFFFF)continue;
        const uint32_t cls=pci_odczytaj_dword(uint8_t(bus),slot,fn,8);
        if((cls>>8)!=0x0C0310)continue;
        struct DiagResult {DiagDevice d;~DiagResult(){d.status=ready?DIAG_OK:DIAG_INIT_FAILED;
            if(!ready)d.code=DIAG_CODE_HID_INIT;
            d.ports=ports;diagnostic_id=diag_publish(0,d);}}
            diag{diag_device(DIAG_OHCI,"Kontroler OHCI","ohci",DIAG_INACTIVE,true,"Inicjalizacja kontrolera OHCI")};
        diag.d.parent=diag_pci_parent(uint8_t(bus),slot,fn);
        diag.d.transport=DIAG_TRANSPORT_PCI;diag.d.bus=uint8_t(bus);diag.d.pci_device=slot;
        diag.d.function=fn;diag.d.pci_valid=1;
        const uint32_t ident=pci_odczytaj_dword(uint8_t(bus),slot,fn,0);
        diag.d.vendor=uint16_t(ident);diag.d.device=uint16_t(ident>>16);
        const uint32_t bar=pci_odczytaj_dword(uint8_t(bus),slot,fn,0x10);
        diag.d.bar=bar;
        if((bar&15)||!bar)continue;
        // OHCI registers and at most 15 root ports fit inside one BAR page.
        ZmapujStrone(reinterpret_cast<void*>(MMIO),reinterpret_cast<void*>(uintptr_t(bar&~4095U)),VMM_FLAGA_PRESENT|VMM_FLAGA_ZAPIS|VMM_FLAGA_PCD|VMM_FLAGA_PWT);
        regs=reinterpret_cast<volatile uint32_t*>(MMIO+(bar&4095U));
        if((read(0)&255)!=0x10)return false;
        pci_zapisz_dword(uint8_t(bus),slot,fn,4,uint32_t(pci_odczytaj_word(uint8_t(bus),slot,fn,4))|0x406);
        if(read(4)&(1U<<8)){write(8,1U<<3);if(!wait_bits(4,1U<<8,0))return false;}
        write(0x14,0xFFFFFFFF);write(4,0);write(8,1);if(!wait_bits(8,1,0))return false;
        if(!allocate32(4096,hcca_dma))return false;
        hcca=static_cast<HCCA*>(hcca_dma.virtual_address);
        write(0x18,uint32_t(hcca_dma.physical_address));
        const uint32_t fi=11999,fsmps=(fi-210)*6/7;
        write(0x34,fi|(fsmps<<16));write(0x40,fi*9/10);write(0x44,0x628);
        write(0x0C,0xFFFFFFFF);write(4,(2U<<6)|3U);ready=true;
        ports=read(0x48)&255;if(!ports||ports>MAX_PORTS){fail_controller();return false;}
        if(!frames(10)){fail_controller();return false;}
        wypisz_log("[USB] OHCI 1.0 root-port transport ready.");
        for(unsigned port=0;port<ports&&ready;++port)if(read(0x54+4*port)&1)enumerate(port);
        return ready;
    }
    return false;
}
void ohci_service(){
    if(!ready)return;
    if(read(0x0C)&(1U<<4)){fail_controller();return;}
    done_queue();if(!ready)return;
    for(auto& d:devices)if(d.used&&d.ready){
        if(!(read(0x54+4*d.port)&1)){d.interrupt->info|=1U<<14;d.ready=false;hid_pointer_remove(d.input);continue;}
        if(!d.pending||!d.retired[d.active])continue;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);const TD& t=d.td[d.active];
        d.pending=false;const uint32_t cbp=t.cbp,base=uint32_t(d.data.physical_address);
        if((t.info>>28)!=0|| (cbp&&(cbp<base||cbp>base+d.packet))){
            d.interrupt->info|=1U<<14;d.ready=false;hid_pointer_remove(d.input);continue;
        }
        pointing_count(pointing_counters.hid_completions);
        const size_t actual=cbp?cbp-base:d.packet;
        if(d.keyboard){if(actual>=8)usb_hid_przetworz_raport(static_cast<const uint8_t*>(d.data.virtual_address));}
        else hid_pointer_publish(d.input,d.layout,static_cast<const uint8_t*>(d.data.virtual_address),actual,d.address);
        queue_interrupt(d);
    }
}
