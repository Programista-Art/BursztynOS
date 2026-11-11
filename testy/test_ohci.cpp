#include "testy/pointing_host_stubs.h"
#include "sterowniki/usb/hid_pointer.h"
#include "testy/pointing_test_descriptors.h"
#include <vector>
#include <algorithm>
#include "testy/virtualbox_tablet_descriptor.h"
#include "testy/pointer_transport_stream.h"
#define BURSZTYN_USB_HOST_TEST 1
#include "sterowniki/usb/ohci.cpp"

static std::vector<DmaBuffer> allocations;
static uint64_t fake_time{};
static uint32_t hardware[64]{};
static std::vector<uint8_t> descriptor(virtualbox_tablet_descriptor,virtualbox_tablet_descriptor+sizeof(virtualbox_tablet_descriptor));
static bool inject_stall{},suppress_completion{},oversized_descriptor{};
static unsigned transactions{},releases{},keyboard_reports{};
static void* dma_pointer(uint32_t address){
    for(const auto& a:allocations)if(address>=a.physical_address&&address-a.physical_address<a.size)
        return static_cast<uint8_t*>(a.virtual_address)+address-a.physical_address;
    CHECK(false);return nullptr;
}
bool dma_allocate(size_t size,size_t alignment,DmaBuffer* out){
    size=(size+4095)&~size_t(4095);void* ptr=std::aligned_alloc(std::max(alignment,size_t(4096)),size);if(!ptr)return false;
    *out={ptr,0x100000+allocations.size()*0x10000,size,size/4096};allocations.push_back(*out);return true;
}
void dma_release(DmaBuffer* b){if(b->virtual_address){++releases;for(auto& a:allocations)if(a.virtual_address==b->virtual_address){std::free(a.virtual_address);a.virtual_address=nullptr;}}*b={};}
bool hpet_dostepny(){return true;}
uint64_t czas_monotoniczny_ns(){if(hcca)++hcca->frame;fake_time+=1000000;return fake_time;}
void ZmapujStrone(void*,void*,uint32_t){}
extern "C" uint32_t pci_odczytaj_dword(uint8_t b,uint8_t slot,uint8_t fn,uint8_t off){
    if(b!=0||slot!=1||fn!=0)return 0xFFFFFFFF;
    return off==0?0x1234:off==8?0x0C031000:off==0x10?0x10000000:0;
}
extern "C" void pci_zapisz_dword(uint8_t,uint8_t,uint8_t,uint8_t,uint32_t){}
extern "C" void usb_hid_przetworz_raport(const uint8_t*){++keyboard_reports;}
static void retire(Device& d,unsigned first,unsigned last,unsigned actual=0){
    uint32_t done=0;
    for(unsigned i=first;i<=last;++i){TD& t=d.td[i];t.info&=0x0FFFFFFF;
        if(i==1&&t.cbp&&actual)t.cbp+=actual;else t.cbp=0;
        t.next=done;done=phys(d,&t);}
    d.control->head=d.control->tail;hcca->done=done;hardware[0x0C/4]|=2;
}
namespace {
uint32_t ohci_test_read(unsigned off){return hardware[off/4];}
void ohci_test_write(unsigned off,uint32_t v){
    if(off==0x0C){hardware[off/4]&=~v;return;}
    if(off>=0x54&&off<0x54+MAX_PORTS*4){
        if(v&(1U<<8))hardware[off/4]|=1U<<8;
        if(v&(1U<<4))hardware[off/4]|=2|(1U<<20);
        hardware[off/4]&=~(v&0x1F0000);return;
    }
    hardware[off/4]=v;
    if(off==8&&v==1){hardware[off/4]=0;return;}
    if(off!=8||v!=2||suppress_completion)return;
    ++transactions;
    ED* ed=static_cast<ED*>(dma_pointer(hardware[0x20/4]));
    Device* device=nullptr;for(auto& d:devices)if(d.control==ed)device=&d;CHECK(device);Device& d=*device;
    auto* setup=static_cast<uint8_t*>(d.memory.virtual_address)+256;
    uint16_t value=uint16_t(setup[2])|uint16_t(setup[3])<<8,len=uint16_t(setup[6])|uint16_t(setup[7])<<8;
    if(inject_stall){d.td[0].info=4U<<28;ed->head|=1;return;}
    std::vector<uint8_t> response;
    if(setup[1]==6){
        if(value==0x100)response={18,1,0,2,0,0,0,8,0xEE,0x80,0x21,0,0,1,0,0,0,1};
        else if(value==0x200)response=usb_configuration(oversized_descriptor?65535:unsigned(descriptor.size()));
        else if(value==0x2200)response=descriptor;
        else CHECK(false);
    }
    const unsigned actual=std::min(unsigned(len),unsigned(response.size()));
    auto* data=static_cast<uint8_t*>(d.data.virtual_address);
    for(unsigned i=0;i<actual;++i)data[i]=response[i];
    retire(d,0,2,actual);
    if(len&&actual==len)d.td[1].cbp=0;
}
}
static void interrupt(Device& d,const std::vector<uint8_t>& report,unsigned code=0){
    CHECK(d.pending);CHECK(report.size()<=d.packet);
    auto* data=static_cast<uint8_t*>(d.data.virtual_address);std::copy(report.begin(),report.end(),data);
    TD& td=d.td[d.active];td.info=(td.info&0x0FFFFFFF)|(code<<28);
    td.cbp=report.size()==d.packet?0:uint32_t(d.data.physical_address+report.size());
    td.next=0;d.interrupt->head=d.interrupt->tail;hcca->done=phys(d,&td);hardware[0x0C/4]|=2;
}
int main(){
    hardware[0]=0x10;hardware[0x48/4]=2;hardware[0x54/4]=1;
    InputDevice ps2{};ps2.kinds=INPUT_DEVICE_KIND_MOUSE;ps2.bus_type=INPUT_BUS_PS2;CHECK(input_register_device(&ps2));
    CHECK(ohci_initialize());CHECK(used==1&&devices[0].ready);CHECK(transactions>=7);CHECK(input_get_pointer_owner()==&ps2);
    Device& d=devices[0];CHECK(d.address==1&&d.layout.absolute);CHECK(d.packet==8);CHECK(d.interval==8);CHECK(hcca->table[0]==phys(d,d.interrupt));CHECK(hcca->table[1]==0);
    CHECK(d.input.pointer_state==INPUT_POINTER_ARMED);
    interrupt(d,{});ohci_service();CHECK(d.input.pointer_state==INPUT_POINTER_ARMED);CHECK(input_get_pointer_owner()==&ps2);CHECK(d.pending);
    interrupt(d,{0});ohci_service();CHECK(d.input.pointer_state==INPUT_POINTER_ARMED);CHECK(input_get_pointer_owner()==&ps2);CHECK(d.pending);
    px=0;py=0; // the first complete axes report is stationary and still proves readiness
    pointer_transport_stream(d.layout,d.input,[&](const std::vector<uint8_t>& report){
        interrupt(d,report);ohci_service();CHECK(d.pending);CHECK(!d.retired[d.active]);
    });
    auto r=tablet_report(d.layout,32767,32767,5,-1);interrupt(d,r);ohci_service();CHECK(input_get_pointer_owner()==&d.input);CHECK(px==1279&&py==719&&last_buttons==5);CHECK(d.pending);
    for(unsigned i=0;i<100;++i){r=tablet_report(d.layout,int(i),int(i),0,0);interrupt(d,r);ohci_service();CHECK(d.pending);CHECK(d.retired[d.active]==false);}
    const int prevx=px,prevy=py;interrupt(d,{0});ohci_service();CHECK(px==prevx&&py==prevy);CHECK(d.pending);
    // Malformed enumeration never registers a pointer or releases DMA in flight.
    hardware[0x58/4]=1;oversized_descriptor=true;CHECK(!enumerate(1));CHECK(input_get_pointer_owner()==&d.input);CHECK(used==2);CHECK(releases==0);oversized_descriptor=false;
    hardware[0x54/4]=0;ohci_service();CHECK(input_get_pointer_owner()==&ps2);CHECK(!d.ready);CHECK(releases==0);
    // A transfer that never completes terminates by a bounded deadline.
    suppress_completion=true;const uint64_t before=fake_time;size_t actual=0;
    CHECK(!control(devices[1],0x80,6,0x100,0,8,actual));CHECK(fake_time-before<2000000000ULL);suppress_completion=false;
    inject_stall=true;CHECK(!control(devices[1],0x80,6,0x100,0,8,actual));inject_stall=false;
    // Invalid physical done pointers stop the controller, retaining DMA safely.
    hcca->done=0xDEAD0000;hardware[0x0C/4]|=2;ohci_service();CHECK(!ready);CHECK(releases==0);
    input_reset_all();for(auto& a:allocations)std::free(a.virtual_address);
    printf("OHCI production enumeration/transfers/failure: %u checks PASS\n",checks);
}
