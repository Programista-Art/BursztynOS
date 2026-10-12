// Simulated Intel-style register semantics; production PCI/xHCI/HID/InputDevice code.
#include "narzedzia/pointing_host_stubs.h"
#include "sterowniki/usb/hid_pointer.h"
#include "narzedzia/pointing_test_descriptors.h"
#ifdef BURSZTYN_XHCI_SOURCE
#include BURSZTYN_XHCI_SOURCE
#else
#include "sterowniki/usb/xhci.cpp"
#endif
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
static bool functional_trace=false;
static uint64_t mock_now=0,clock_step=1000,control_delay=0,pending_due=0;
static unsigned pending_control=0;
static bool delayed_commands=false;static uint64_t delayed_command_pointer=0;
static uint8_t delayed_command_slot=0,command_error=1,enable_slot_next=2;
static uint64_t ownership_delay=0,ownership_due=0;
static void progress_clock();
static void trace_write(const char* kind,uint64_t offset,uint64_t value){
    if(functional_trace)printf("WRITE %s %llu %llu\n",kind,(unsigned long long)offset,(unsigned long long)value);
}
static uint64_t next_dma=0x100000;
static unsigned allocated,released,publication_checks;
static std::vector<DmaBuffer> dma_map;
bool dma_allocate(size_t n,size_t alignment,DmaBuffer* b){
    size_t bytes=(n+4095)&~size_t(4095);void* p=std::aligned_alloc(std::max(alignment,size_t(4096)),bytes);
    if(!p)return false;
    std::memset(p,0,bytes);*b={p,next_dma,n,bytes/4096};trace_write("DMA",next_dma,n);next_dma+=bytes;++allocated;dma_map.push_back(*b);return true;
}
void dma_release(DmaBuffer* b){if(b->virtual_address){trace_write("FREE",b->physical_address,b->size);std::free(b->virtual_address);++released;}*b={};}
extern "C" void usb_hid_przetworz_raport(const uint8_t*){}
void ohci_service(){}
void ZmapujStrone(void*,void*,unsigned int){CHECK(false);}
static uint8_t cfg[256];static uint32_t bar_mask[6];static bool probing[6];
static bool prior_if=true;
unsigned long host_idt_irq_save(){unsigned long f=prior_if?512:0;prior_if=false;return f;}
void host_idt_irq_restore(unsigned long f){prior_if=f&512;}
extern "C" uint8_t apic_pobierz_lapic_id(){return 0;}
bool hpet_dostepny(){return true;}
uint64_t czas_monotoniczny_ns(){mock_now+=clock_step;progress_clock();return mock_now;}
uint32_t host_pci_read(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t off){
    if(bus!=0||slot!=20||fn!=0)return UINT32_MAX;
    if(off>=0x10&&off<0x28&&probing[(off-0x10)/4])return bar_mask[(off-0x10)/4];
    uint32_t v;std::memcpy(&v,cfg+(off&252),4);return v;
}
void host_pci_write(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t off,uint32_t v){
    CHECK(bus==0&&slot==20&&fn==0);trace_write("PCI",off,v);
    if(off==4){CHECK(!(v&0xffff0000U));uint32_t old;std::memcpy(&old,cfg+4,4);v=(old&0xffff0000U)|(v&65535);}
    if(off>=0x10&&off<0x28){unsigned i=(off-0x10)/4;probing[i]=v==UINT32_MAX;if(probing[i])return;}
    std::memcpy(cfg+(off&252),&v,4);
}
alignas(4096) static uint8_t registers[65536],irq_bar[4096];
static bool model=true,firmware_stuck,reset_stuck,halt_stuck,cnr_stuck,port_reset_stuck,stall_protocol,conventional_wheel_descriptor;
static uint16_t event_producer;static bool event_cycle=true;
static unsigned port_writes,control_requests,boot_requests;
static uint8_t control_error,command_slot_override;static uint32_t control_short;
static std::vector<uint32_t> port_values;
static const uint8_t configuration[]={
    9,2,50,0,2,1,0,0x80,50,
    9,4,0,0,1,0xff,0,0,0, 7,5,0x02,2,64,0,0,
    9,4,3,0,1,3,1,2,0, 9,0x21,0x11,1,0,1,0x22,52,0,
    7,5,0x85,3,8,0,10};
static const uint8_t device_descriptor[]={18,1,0,2,0,0,0,64,0x34,0x12,0x78,0x56,0,1,0,0,0,1};
static std::vector<uint8_t> report_descriptor(){
    if(!conventional_wheel_descriptor)return tablet_descriptor(true,5,-127,127,true,8);
    return {5,1,9,2,0xa1,1,9,1,0xa1,0,5,9,0x19,1,0x29,3,0x15,0,0x25,1,0x95,3,0x75,1,0x81,2,
        0x95,1,0x75,5,0x81,1,5,1,9,0x30,9,0x31,9,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0,0xc0};
}
namespace {
volatile uint8_t* xhci_host_map_bar(uint8_t bir,uint64_t physical,uint64_t size){
    if(bir==0){CHECK(physical==0xf0000000 && size==65536);return registers;}
    CHECK(bir==2&&physical==0xf1000000&&size==4096);return irq_bar;
}
static void put(volatile uint8_t* base,uint32_t off,uint32_t v){*reinterpret_cast<volatile uint32_t*>(base+off)=v;}
uint32_t xhci_host_read(const volatile uint8_t* b,uint32_t off){return *reinterpret_cast<const volatile uint32_t*>(b+off);}
static void emit(uint64_t ptr,uint32_t status,uint32_t flags){
    auto* events=static_cast<XhciTrb*>(xhc.event_segment.virtual_address);CHECK(events);
    events[event_producer]={uint32_t(ptr),uint32_t(ptr>>32),status,flags|(event_cycle?1U:0U)};
    if(++event_producer==EVENT_TRBS){event_producer=0;event_cycle=!event_cycle;}
}
static void control_complete(XhciDevice& d){
    ++control_requests;
    const auto* setup=reinterpret_cast<const uint8_t*>(&d.ep0.trbs[(xhc.wanted_setup-d.ep0.dma.physical_address)/16]);
    CHECK(!(d.ep0.trbs[(xhc.wanted_setup-d.ep0.dma.physical_address)/16].dword3&(1U<<4)));
    const uint16_t value=uint16_t(setup[2]|setup[3]<<8),length=uint16_t(setup[6]|setup[7]<<8);
    uint8_t code=control_error?control_error:1;uint32_t residual=control_short;
    if(setup[1]==0x0b){++boot_requests;if(stall_protocol)code=6;}
    if(length && code==1){
        const uint8_t* source=nullptr;size_t size=0;
        if(value==0x100){source=device_descriptor;size=sizeof(device_descriptor);}
        if(value==0x200){static std::vector<uint8_t> config_copy(configuration,configuration+sizeof(configuration));
            const auto descriptor=report_descriptor();
            config_copy[41]=uint8_t(descriptor.size());config_copy[42]=uint8_t(descriptor.size()>>8);
            source=config_copy.data();size=config_copy.size();}
        if(value==0x2200){static std::vector<uint8_t> report;report=report_descriptor();source=report.data();size=report.size();}
        // Locate DMA by TRB address through the fixture's tracked allocations.
        extern void* dma_virtual(uint64_t);
        const auto& trb=d.ep0.trbs[(xhc.wanted_data-d.ep0.dma.physical_address)/16];
        void* target=dma_virtual((uint64_t(trb.dword1)<<32)|trb.dword0);
        if(source){std::memcpy(target,source,std::min(size,size_t(length)));if(size<length)residual=length-uint32_t(size);}
        if(residual)emit(xhc.wanted_data,(13U<<24)|residual,xhci_trb::type_field(32)|(uint32_t(d.slot_id)<<24)|(1U<<16));
    }
    emit(code==1?xhc.wanted_transfer:xhc.wanted_setup,(uint32_t(code)<<24),xhci_trb::type_field(32)|(uint32_t(d.slot_id)<<24)|(1U<<16));
}
void xhci_host_write(volatile uint8_t* b,uint32_t off,uint32_t v){
    if(functional_trace){
        const uintptr_t address=reinterpret_cast<uintptr_t>(b)+off;
        const uintptr_t first=reinterpret_cast<uintptr_t>(registers),irq=reinterpret_cast<uintptr_t>(irq_bar);
        if(address>=first&&address<first+sizeof(registers))trace_write("MMIO",address-first,v);
        else if(address>=irq&&address<irq+sizeof(irq_bar))trace_write("MSIX",address-irq,v);
        else CHECK(false);
    }
    if(!model){put(b,off,v);return;}
    if(b==registers && off==0x100){
        if((v&(1U<<24))&&!firmware_stuck){
            if(ownership_delay && (v&(1U<<16)))ownership_due=mock_now+ownership_delay;
            else v&=~(1U<<16);
        }
        put(b,off,v);return;
    }
    if(b==xhc.operational && off==USBCMD){
        if(v&CMD_HCRST){CHECK(!(host_pci_read(0,20,0,4)&4U));put(b,off,reset_stuck?v:v&~CMD_HCRST);put(b,USBSTS,STS_HCH|(cnr_stuck?STS_CNR:0));}
        else {if(v&CMD_RS)CHECK((host_pci_read(0,20,0,4)&6U)==6U);put(b,off,v);put(b,USBSTS,v&CMD_RS?0:halt_stuck?0:STS_HCH);}return;
    }
    if(b==xhc.operational && off>=PORTSC_BASE && off<PORTSC_BASE+255*16 && !(off&15)){
        ++port_writes;port_values.push_back(v);
        uint32_t old=xhci_host_read(b,off);uint32_t value=(old&~(v&PORT_RW1C));
        value=(value&~((1U<<9)|(3U<<14)|(7U<<25)))|(v&((1U<<9)|(3U<<14)|(7U<<25)));
        if(v&PORT_PR)value|=port_reset_stuck?PORT_PR:PORT_PED|PORT_PRC;
        if(v&PORT_WPR)value|=port_reset_stuck?PORT_WPR:PORT_PED|(1U<<19);
        CHECK(!(v&PORT_PED));CHECK(!(v&PORT_CCS));put(b,off,value);return;
    }
    if(b==xhc.operational && off==USBSTS){put(b,off,xhci_host_read(b,off)&~v);return;}
    if(b==xhc.runtime+0x20 && off==0){uint32_t old=xhci_host_read(b,off);put(b,off,(v&2)|((old&1)&~(v&1)));return;}
    put(b,off,v);
    if(b==xhc.doorbells && off==0 && xhc.wanted_command){
        const auto& trb=xhc.command.trbs[(xhc.wanted_command-xhc.command.dma.physical_address)/16];
        const uint32_t type=xhci_trb::type(trb.dword3);const unsigned slot=type==9?enable_slot_next++:trb.dword3>>24;
        if(type==11){auto& d=xhc.devices[slot];std::memcpy(d.output_context.virtual_address,
            static_cast<uint8_t*>(d.input_context.virtual_address)+xhc.context_size,32*xhc.context_size);context(d.output_context,0)[3]=5;}
        if(delayed_commands){delayed_command_pointer=xhc.wanted_command;delayed_command_slot=uint8_t(command_slot_override?command_slot_override:slot);}
        else emit(xhc.wanted_command,uint32_t(command_error)<<24,xhci_trb::type_field(33)|(uint32_t(command_slot_override?command_slot_override:slot)<<24));
    }else if(b==xhc.doorbells && off && v==1 && xhc.wanted_transfer){if(control_delay){pending_control=off/4;pending_due=mock_now+control_delay;}else control_complete(xhc.devices[off/4]);}
}

}
static void progress_clock(){
    if(ownership_due && mock_now>=ownership_due){ownership_due=0;put(registers,0x100,xhci_host_read(registers,0x100)&~(1U<<16));}
    if(pending_control && mock_now>=pending_due){const unsigned slot=pending_control;pending_control=0;control_complete(xhc.devices[slot]);}}
// Independent mapping of virtual allocations for control DMA.
namespace { void* dma_virtual(uint64_t physical){for(auto& b:dma_map)if(b.physical_address==physical)return b.virtual_address;CHECK(false);return nullptr;} }
void xhci_host_before_publish(const XhciProducerRing* ring,uint16_t index,const XhciTrb& expected) {
    ++publication_checks;
    if(functional_trace)printf("TRB %llu %u %u %u %u %u\n",(unsigned long long)ring->dma.physical_address,index,
        expected.dword0,expected.dword1,expected.dword2,expected.dword3);
    static const XhciTrb* held_setup=nullptr;static bool setup_cycle=false;
    const auto type=xhci_trb::type(expected.dword3);
    if(type==2){held_setup=&ring->trbs[index];setup_cycle=ring->producer_cycle;}
    if(type==3 || type==4){CHECK(held_setup);CHECK(bool(held_setup->dword3&1)!=setup_cycle);if(type==4)held_setup=nullptr;}
    // Observe hardware-visible memory immediately before ownership publication.
    CHECK(ring->trbs[index].dword0==expected.dword0);CHECK(ring->trbs[index].dword1==expected.dword1);
    CHECK(ring->trbs[index].dword2==expected.dword2);CHECK(bool(ring->trbs[index].dword3&1)!=ring->producer_cycle);
    if(index==ring->trb_count-2){CHECK(bool(ring->trbs[index+1].dword3&1)==ring->producer_cycle);CHECK(ring->trbs[index+1].dword3&2);}
}
static void ring_full_test(){const unsigned start=checks;XhciProducerRing ring{};CHECK(create_ring(&ring,16));
    XhciTrb trb{0x12345678,0x101,64,xhci_trb::type_field(1)};uint64_t physical=0;
    for(unsigned wrap=0;wrap<20;++wrap){for(unsigned i=0;i<14;++i)CHECK(xhci_command_ring_enqueue(&ring,trb,&physical));
        const auto producer=ring.producer_index;CHECK(!xhci_command_ring_enqueue(&ring,trb,&physical));CHECK(producer==ring.producer_index);
        xhci_ring_complete(&ring,14);CHECK(!ring.outstanding);}
    xhci_command_ring_destroy(&ring);
    printf("Ring full/wrap/Link/pre-publication observation: %u checks PASS\n",checks-start);
}
static void section(const char* name,unsigned start){printf("%s: %u checks PASS\n",name,checks-start);}
static void clean(){
    for(auto& d:xhc.devices){d.enabled=false;free_device(d);}release_interrupt(xhc);release_structures(xhc);
    dma_release(&enumeration.data);enumeration=Enumeration{};
    pending_control=0;ownership_due=0;input_reset_all();xhc=Controller{};dma_map.clear();CHECK(allocated==released);
}
static void fixture(bool csz64=true){
    clean();model=true;firmware_stuck=reset_stuck=halt_stuck=cnr_stuck=port_reset_stuck=stall_protocol=conventional_wheel_descriptor=false;
    delayed_commands=false;delayed_command_pointer=0;command_error=1;enable_slot_next=2;
    clock_step=1000;control_delay=0;pending_control=0;ownership_delay=ownership_due=0;control_error=command_slot_override=0;control_short=0;
    event_producer=0;event_cycle=true;port_values.clear();next_dma=0x100000;
    std::memset(registers,0,sizeof(registers));std::memset(irq_bar,0,sizeof(irq_bar));std::memset(cfg,0,sizeof(cfg));
    std::memset(probing,0,sizeof(probing));std::memset(bar_mask,0,sizeof(bar_mask));
    auto config_put=[](unsigned off,uint32_t v){std::memcpy(cfg+off,&v,4);};
    config_put(0,0xa36d8086);config_put(4,0xf9000000U|(1U<<20)|6U);config_put(8,0x0c033010);
    config_put(0x10,0xf0000004);config_put(0x14,0);bar_mask[0]=0xffff0004;bar_mask[1]=UINT32_MAX;
    config_put(0x18,0xf1000000);bar_mask[2]=0xfffff000;
    cfg[0x34]=0x50;cfg[0x50]=0x11;config_put(0x54,0x802);config_put(0x58,0x902);
    put(registers,0,0x01100080);put(registers,4,32U|(4U<<8)|(20U<<24));put(registers,8,4U<<27);
    put(registers,0x10,(0x40U<<16)|1U|8U|(csz64?4U:0U));put(registers,0x14,0x2000);put(registers,0x18,0x3000);
    put(registers,0x80+PAGESIZE,1);put(registers,0x80+USBSTS,STS_HCH);
    put(registers,0x100,1U|(4U<<8)|(1U<<16));put(registers,0x104,0xffffffffU);
    put(registers,0x110,2U|(4U<<8)|(2U<<24));put(registers,0x114,0x20425355);
    put(registers,0x118,1U|(14U<<8));
    put(registers,0x120,2U|(3U<<24));put(registers,0x124,0x20425355);put(registers,0x128,15U|(6U<<8));
    // Firmware may leave CSC set even on an empty port. Must acknowledge it.
    put(registers,0x80+PORTSC_BASE+8*16,1U<<17);
    BootTiming::first_visible_splash=true;
    width=1280;height=720;px=640;py=360;input_set_pointer_ready(true);
}
static void caps_tests(){unsigned start=checks;
    CHECK(xhci_scratchpad_count(0)==0);CHECK(xhci_scratchpad_count(4U<<27)==4);
    CHECK(xhci_scratchpad_count((1U<<21)|(3U<<27))==35);CHECK(xhci_scratchpad_count(0xffffffffU)==1023);
    CHECK(xhci_context_bytes(0x00a0f045)==64);CHECK(xhci_context_bytes(0x00a0f041)==32);
    for(uint32_t original:{0U,UINT32_MAX,0xa5296a9bU,0x00010001U}){
        CHECK((xhci_legacy_os_owned(original)^original)==((original&(1U<<24))?0U:1U<<24));
        const uint32_t result=xhci_legacy_disable_smi(original);
        CHECK((result&0x000e1feeU)==(original&0x000e1feeU));CHECK(!(result&0x1ff0e011U));CHECK((result&0xe0000000)==0xe0000000);
    }
    fixture();xhc.mmio=registers;xhc.bar_size=65536;xhc.max_ports=20;
    CHECK(ext_capabilities(xhc,0x40U<<16));CHECK(read32(registers,0x100)&(1U<<24));CHECK(!(read32(registers,0x100)&(1U<<16)));
    CHECK(xhc.ports[1].protocol==xhc.ports[14].protocol);CHECK(xhc.ports[15].protocol==xhc.ports[20].protocol);
    CHECK(xhc.ports[1].protocol->usb2);CHECK(xhc.ports[20].protocol->usb3);
    // A truncated legacy capability must cause no write (OS Owned stays zero).
    xhc=Controller{};xhc.mmio=registers;xhc.bar_size=0x104;put(registers,0x100,1U|(1U<<16));
    CHECK(!ext_capabilities(xhc,0x40U<<16));CHECK(!(read32(registers,0x100)&(1U<<24)));
    fixture();xhc.mmio=registers;xhc.bar_size=65536;xhc.max_ports=20;firmware_stuck=true;
    CHECK(!ext_capabilities(xhc,0x40U<<16));firmware_stuck=false;
    DmaBuffer b{};next_dma=UINT64_C(0x100000000);xhc.ac64=false;CHECK(!allocate_dma(4096,64,&b));CHECK(!b.virtual_address);
    xhc.ac64=true;CHECK(allocate_dma(4096,64,&b));CHECK(b.physical_address>UINT32_MAX);dma_release(&b);
    clean();section("Intel capabilities/legacy handoff/address width",start);
}
static void control_trb_tests(){unsigned start=checks;
    const uint8_t get[]={0x80,6,0,1,0,0,18,0},config[]={0,9,1,0,0,0,0,0},protocol[]={0x21,11,0,0,3,0,0,0};
    for(const uint8_t* setup:{get,config,protocol}){XhciTrb s{},d{},st{};const uint16_t n=setup==get?18:0;
        xhci_control_trbs(setup,UINT64_C(0x123400001000),n,s,d,st);
        CHECK(s.dword2==8);CHECK(!(s.dword3&(1U<<4)));CHECK(s.dword3&(1U<<6));
        CHECK((s.dword3>>16)==(n?3U:0U));CHECK(xhci_trb::type(st.dword3)==4);
        CHECK(st.dword3&(1U<<5));CHECK(bool(st.dword3&(1U<<16))==!n);
        CHECK(s.dword0==(uint32_t(setup[0])|uint32_t(setup[1])<<8|uint32_t(setup[2])<<16|uint32_t(setup[3])<<24));
        if(n){CHECK(d.dword0==0x1000&&d.dword1==0x1234);CHECK(d.dword3&(1U<<16));CHECK(!(d.dword3&(1U<<4)));}
        else CHECK(d.dword3==0);
    }
    const uint8_t out[]={0,7,0,0,0,0,8,0};XhciTrb s{},d{},st{};xhci_control_trbs(out,0x100000,8,s,d,st);
    CHECK((s.dword3>>16)==2);CHECK(!(d.dword3&(1U<<16)));CHECK(st.dword3&(1U<<16));
    section("Control TRB requests/directions/reserved fields",start);
}
static void descriptor_tests(){unsigned start=checks;
    UsbHidInterface h{};CHECK(usb_hid_find_interface(configuration,sizeof(configuration),h));
    CHECK(h.interface_number==3&&h.subclass==1&&h.protocol==2);CHECK(h.endpoint==0x85&&h.packet_size==8&&h.interval==10);
    std::vector<uint8_t> ss(configuration,configuration+sizeof(configuration));ss.insert(ss.end(),{6,0x30,1,0,16,0});ss[2]=uint8_t(ss.size());
    CHECK(usb_hid_find_interface(ss.data(),ss.size(),h));CHECK(h.companion&&h.max_burst==1&&h.bytes_per_interval==16);
    ss.back()=1;ss[ss.size()-3]=1;CHECK(!usb_hid_find_interface(ss.data(),ss.size(),h));
    for(unsigned endpoint=1;endpoint<=15;++endpoint){CHECK(xhci_oblicz_dci(endpoint,true)==2*endpoint+1);CHECK(xhci_oblicz_dci(endpoint,false)==2*endpoint);}
    CHECK(xhci_oblicz_dci(0,true)==0);CHECK(xhci_oblicz_dci(16,false)==0);
    for(auto speed:{USB_LOW_SPEED,USB_FULL_SPEED})for(auto pair:{std::pair<unsigned,unsigned>{1,3},{2,4},{10,6},{255,10}})CHECK(xhci_oblicz_interwal(speed,pair.first)==pair.second);
    for(auto speed:{USB_HIGH_SPEED,USB_SUPER_SPEED,USB_SUPER_SPEED_PLUS}){CHECK(xhci_oblicz_interwal(speed,1)==0);CHECK(xhci_oblicz_interwal(speed,16)==15);}
    HidPointerLayout boot{};CHECK(hid_boot_mouse_layout(boot));CHECK(!boot.absolute);CHECK(!hid_boot_mouse_has_wheel(boot));
    InputEvent e[HID_POINTER_FIELDS_MAX+1];size_t n;
    for(int axis=0;axis<2;++axis)for(int delta=-127;delta<=127;++delta){uint8_t b[]={0,0,0};b[1+axis]=uint8_t(delta);
        CHECK(hid_pointer_decode(boot,b,3,e,HID_POINTER_FIELDS_MAX+1,n));bool seen=false;
        for(size_t i=0;i<n;++i)if(e[i].type==INPUT_EV_REL&&e[i].code==axis){CHECK(input_signed_value(e[i].value)==delta);seen=true;}
        CHECK(seen);
    }
    uint8_t b[]={7,0x81,127,0xff};CHECK(!hid_pointer_decode(boot,b,2,e,HID_POINTER_FIELDS_MAX+1,n));
    CHECK(hid_boot_mouse_layout(boot,true));CHECK(hid_boot_mouse_has_wheel(boot));CHECK(hid_pointer_decode(boot,b,4,e,HID_POINTER_FIELDS_MAX+1,n));
    CHECK(input_signed_value(e[3].value)==-127);CHECK(input_signed_value(e[4].value)==127);CHECK(input_signed_value(e[5].value)==-1);
    CHECK(e[0].value&&e[1].value&&e[2].value);
    // Numbered, packed 12-bit signed axes on a real report descriptor path.
    auto desc=tablet_descriptor(true,6,-2047,2047,true,12);HidPointerLayout packed{};
    CHECK(hid_pointer_compile(desc.data(),desc.size(),packed));CHECK(!hid_boot_mouse_has_wheel(packed));auto report=tablet_report(packed,-17,29);
    CHECK(hid_pointer_decode(packed,report.data(),report.size(),e,HID_POINTER_FIELDS_MAX+1,n));
    bool x=false,y=false;for(size_t i=0;i<n;++i){if(e[i].type==INPUT_EV_REL&&e[i].code==INPUT_REL_X)x=input_signed_value(e[i].value)==-17;
        if(e[i].type==INPUT_EV_REL&&e[i].code==INPUT_REL_Y)y=input_signed_value(e[i].value)==29;}CHECK(x&&y);
    section("Multi-interface/companion/interval/DCI/Boot Mouse/packed report",start);
}
static InputDevice touchpad;
static void boot_and_insert(bool csz64,bool boot_stalls=false,bool wheel=false){
    fixture(csz64);stall_protocol=boot_stalls;conventional_wheel_descriptor=wheel;touchpad={};touchpad.bus_type=INPUT_BUS_PS2;touchpad.kinds=INPUT_DEVICE_KIND_MOUSE;touchpad.pointer_mode=INPUT_POINTER_RELATIVE;
    CHECK(input_register_device(&touchpad));CHECK(input_get_pointer_owner()==&touchpad);
    CHECK(xhci_inicjalizuj_pierwszy());CHECK(xhc.context_size==(csz64?64:32));CHECK(xhc.scratchpads==4);
    CHECK(xhc.irq_mode==2&&xhc.irq_vector>=48&&xhc.irq_vector!=44);CHECK(prior_if);
    CHECK(static_cast<uint64_t*>(xhc.dcbaa.virtual_address)[0]==xhc.scratchpad_array.physical_address);
    for(unsigned i=0;i<4;++i)CHECK(static_cast<uint64_t*>(xhc.scratchpad_array.virtual_address)[i]==xhc.scratchpad_buffers[i].physical_address);
    for(unsigned i=0;i<xhc.max_ports;++i)usb_obsluz();
    CHECK(!(read32(xhc.operational,PORTSC_BASE+8*16)&PORT_RW1C));
    const uint32_t port=PORTSC_BASE+8*16;put(xhc.operational,port,(1U<<9)|PORT_CCS|(1U<<10)|(1U<<17));
    emit(uint64_t(9)<<24,0,xhci_trb::type_field(34));xhci_irq(&xhc);usb_obsluz();
    for(unsigned call=0;call<400&&!xhc.devices[2].hid_transfer_oczekiwany;++call)usb_obsluz();
    auto& d=xhc.devices[2];CHECK(d.enabled&&d.hid_gotowy&&d.boot_mouse==!boot_stalls);CHECK(d.root_port==9&&d.speed_id==1);
    CHECK(d.hid_dci==11&&d.hid_endpoint==0x85);CHECK(d.ep0_mps==64);CHECK(d.hid_ring.outstanding==1);
    CHECK(d.pointer_device.pointer_mode==INPUT_POINTER_RELATIVE);CHECK(d.pointer_device.pointer_state==INPUT_POINTER_ARMED);
    CHECK(input_get_pointer_owner()==&touchpad);CHECK(px==640&&py==360);CHECK(d.hid_transfer_oczekiwany);
    CHECK(!(read32(xhc.operational,port)&PORT_RW1C));
}
static void report_complete(const std::vector<uint8_t>& report,uint8_t code=13,uint32_t residual=UINT32_MAX,
                            int slot=2,int endpoint=11,uint64_t pointer=0){
    auto& d=xhc.devices[2];if(!pointer)pointer=d.hid_transfer_pointer;
    if(residual==UINT32_MAX)residual=uint32_t(d.hid_dma_length-report.size());
    if(!report.empty())std::memcpy(d.hid_raport.virtual_address,report.data(),report.size());
    emit(pointer,(uint32_t(code)<<24)|residual,xhci_trb::type_field(32)|(uint32_t(slot)<<24)|(uint32_t(endpoint)<<16));
    xhci_irq(&xhc);usb_obsluz();
    if(functional_trace)printf("REPORT %d %d %u %u %u %u %u %u %u %u\n",px,py,last_buttons,
        d.enabled,d.hid_gotowy,d.pointer_device.registered,d.pointer_device.pointer_state,
        d.hid_ring.producer_index,d.hid_ring.outstanding,d.hid_transfer_oczekiwany);
}
static void runtime_tests(){unsigned start=checks;
    for(bool csz64:{false,true}){
        boot_and_insert(csz64);auto& d=xhc.devices[2];uint64_t first=d.hid_transfer_pointer;
        for(auto invalid: {0,1,2,3,4}){
            report_complete({},13,0,invalid==0?3:2,invalid==1?9:11,invalid==2?first+1:invalid==3?first+16:first+4096);
            CHECK(d.hid_transfer_pointer==first);CHECK(d.hid_ring.outstanding==1);CHECK(input_get_pointer_owner()==&touchpad);
        }
        report_complete({},13,8);CHECK(d.pointer_device.pointer_state==INPUT_POINTER_ARMED);CHECK(px==640&&py==360);
        report_complete({0,0},13,6);CHECK(input_get_pointer_owner()==&touchpad);
        report_complete({0,0,0});CHECK(d.pointer_device.pointer_state==INPUT_POINTER_PROVEN_ACTIVE);
        CHECK(input_get_pointer_owner()==&d.pointer_device);CHECK(px==640&&py==360);
        width=height=0;report_complete({0,0xff,0xff});CHECK(px==640&&py==360);
        width=1280;height=720;input_service_all();CHECK(px==639&&py==359);
        report_complete({7,10,10,1});CHECK(px==649&&py==369);CHECK(last_buttons==7);
        report_complete({0,1,0,0,0,0,0,0},1,0);CHECK(px==650&&py==369);CHECK(d.hid_ring.outstanding==1);
        report_complete({0,0xff,0});CHECK(px==649&&py==369);
        const auto reports_before=xhc.mouse_reports_processed;
        for(unsigned i=0;i<800;++i){report_complete({0,0xff,1});CHECK(d.hid_ring.outstanding==1);CHECK(d.hid_transfer_oczekiwany);}
        CHECK(xhc.mouse_reports_processed==reports_before+800);CHECK(px==0&&py==719);
        const unsigned logged=logs;for(unsigned i=0;i<40;++i)report_complete({0,0,0});CHECK(logs==logged);
        const uint64_t current=d.hid_transfer_pointer;report_complete({},13,0,2,11,first);CHECK(d.hid_transfer_pointer==current);
        const int before_x=px,before_y=py;
        put(xhc.operational,PORTSC_BASE+8*16,(1U<<9)|(1U<<17));emit(uint64_t(9)<<24,0,xhci_trb::type_field(34));xhci_irq(&xhc);usb_obsluz();
        for(unsigned call=0;call<100&&d.enabled;++call)usb_obsluz();
        CHECK(!d.enabled&&!d.pointer_device.registered);CHECK(input_get_pointer_owner()==&touchpad);CHECK(px==before_x&&py==before_y);
        clean();
    }
    for(uint8_t code:{uint8_t(2),uint8_t(3),uint8_t(4),uint8_t(5),uint8_t(6)}){
        boot_and_insert(false);auto& d=xhc.devices[2];report_complete({},code,0);
        CHECK(!d.hid_gotowy&&!d.pointer_device.registered);CHECK(!d.hid_transfer_oczekiwany);CHECK(xhc.ready);
        CHECK(input_get_pointer_owner()==&touchpad);CHECK(d.hid_raport.virtual_address);clean();
    }
    boot_and_insert(true);report_complete({},13,9);CHECK(!xhc.devices[2].hid_gotowy);CHECK(input_get_pointer_owner()==&touchpad);clean();
    boot_and_insert(true,true);
    auto& fallback=xhc.devices[2];CHECK(fallback.pointer_layout.numbered&&!fallback.pointer_layout.absolute);
    report_complete(tablet_report(fallback.pointer_layout,-17,29,1));CHECK(px==623&&py==389);CHECK(last_buttons==1);
    CHECK(fallback.pointer_device.pointer_state==INPUT_POINTER_PROVEN_ACTIVE);clean();
    boot_and_insert(true,false,true);CHECK(xhc.devices[2].boot_wheel);
    report_complete({0,0xff,1,0xff});CHECK(px==639&&py==361);CHECK(xhc.mouse_reports_processed==1);clean();
    section("Production Intel-style boot/hotplug/EP0/IRQ/mouse/requeue/removal",start);
}
static void control_and_ports_tests(){unsigned start=checks;
    boot_and_insert(true);auto& d=xhc.devices[2];DmaBuffer data{};CHECK(allocate_dma(18,64,&data));
    UsbSetupPacket get{0x80,6,0x100,0,18};uint32_t actual=0;uint8_t code=0;
    control_short=10;CHECK(usb_control_transfer(d,get,&data,18,&actual,&code));CHECK(actual==8&&code==1);control_short=0;
    // Explicitly recover EP0 after an optional class request stalls, then reuse it.
    stall_protocol=true;UsbSetupPacket protocol{0x21,11,0,3,0};CHECK(!usb_control_transfer(d,protocol,nullptr,0,&actual,&code));
    CHECK(code==6&&!d.control_failed);stall_protocol=false;CHECK(usb_control_transfer(d,get,&data,18,&actual,&code));CHECK(actual==18);
    for(unsigned i=0;i<50;++i){CHECK(usb_control_transfer(d,get,&data,18,&actual,&code));CHECK(actual==18);}
    dma_release(&data);
    command_slot_override=7;CHECK(!disable_slot(d));CHECK(d.enabled);command_slot_override=0;
    XhciDevice& super=xhc.devices[3];CHECK(prepare_device(super,3,15,{USB_SUPER_SPEED,5000000000ULL,4}));
    hid_wynik_konfiguracji h{};h.znaleziono=true;h.endpoint=0x83;h.dci=7;h.maksymalny_rozmiar=64;h.interwal=1;
    h.companion=true;h.burst=1;h.bytes_per_interval=96;h.type=USB_HID_POINTER;
    CHECK(xhci_skonfiguruj_endpoint(super,h));auto* ep=xhci_context_ptr(super.input_context,7);
    CHECK((ep[4]>>16)==96&&(ep[4]&65535)==96);CHECK(((ep[1]>>8)&255)==1);CHECK(super.hid_interwal==0);CHECK(super.hid_dma_length==96);
    super.enabled=false;free_device(super);
    XhciDevice& high=xhc.devices[3];CHECK(prepare_device(high,3,9,{USB_HIGH_SPEED,480000000ULL,3}));
    h.companion=false;h.burst=0;h.transactions=2;h.bytes_per_interval=0;
    CHECK(xhci_skonfiguruj_endpoint(high,h));ep=xhci_context_ptr(high.input_context,7);
    CHECK((ep[4]>>16)==192);CHECK(high.hid_dma_length==192);CHECK(((ep[1]>>8)&255)==2);high.enabled=false;free_device(high);
    const uint32_t port=PORTSC_BASE+14*16;
    put(xhc.operational,port,PORT_CCS|PORT_PED|(4U<<10)|(1U<<9)|(1U<<17)|(1U<<22));
    unsigned before=port_writes;CHECK(reset_port(15));CHECK(port_writes==before+1); // USB3 U0: only acknowledge changes
    CHECK(!(read32(xhc.operational,port)&PORT_RW1C));CHECK(read32(xhc.operational,port)&PORT_PED);
    put(xhc.operational,port,PORT_CCS|(4U<<10)|(1U<<9)|(6U<<5));CHECK(reset_port(15));CHECK(port_values.back()&(1U<<19));
    // No cached read-only, PED(RW1C), PR, WPR or unselected change bits leak into writes.
    for(uint32_t v:port_values)CHECK(!(v&(PORT_CCS|PORT_PED|(15U<<10))));
    clean();fixture();reset_stuck=true;CHECK(!xhci_inicjalizuj_pierwszy());CHECK(prior_if);CHECK(idt_zaalokowane_wektory_liczba()==0);clean();
    for(bool entry_if:{false,true}) {
        fixture();prior_if=entry_if;put(registers,0x80+PAGESIZE,2);CHECK(!xhci_inicjalizuj_pierwszy());CHECK(prior_if==entry_if);clean();
        fixture();prior_if=entry_if;firmware_stuck=true;CHECK(!xhci_inicjalizuj_pierwszy());CHECK(prior_if==entry_if);clean();
        fixture();prior_if=entry_if;cfg[0x34]=0;CHECK(!xhci_inicjalizuj_pierwszy());CHECK(prior_if==entry_if);CHECK(idt_zaalokowane_wektory_liczba()==0);clean();
    }
    prior_if=true;
    section("Control short/Stall recovery/USB2-USB3 PORTSC/reset failure",start);
}
static void coldplug_test(){unsigned start=checks;fixture();touchpad={};touchpad.bus_type=INPUT_BUS_PS2;
    touchpad.kinds=INPUT_DEVICE_KIND_MOUSE;touchpad.pointer_mode=INPUT_POINTER_RELATIVE;CHECK(input_register_device(&touchpad));
    put(registers,0x80+PORTSC_BASE+8*16,PORT_CCS|(1U<<10)|(1U<<17));
    CHECK(xhci_inicjalizuj_pierwszy());CHECK(!xhc.devices[2].hid_gotowy);
    for(unsigned i=0;i<400&&!xhc.devices[2].hid_transfer_oczekiwany;++i)usb_obsluz();
    CHECK(xhc.devices[2].hid_gotowy);CHECK(input_get_pointer_owner()==&touchpad);
    report_complete({0,0xf9,13});CHECK(px==633&&py==373);CHECK(xhc.devices[2].pointer_device.pointer_state==INPUT_POINTER_PROVEN_ACTIVE);
    clean();section("Connected-at-boot mouse/first live report",start);
}
int main(){caps_tests();control_trb_tests();descriptor_tests();ring_full_test();runtime_tests();control_and_ports_tests();coldplug_test();
    printf("Physical xHCI mouse total: %u checks PASS; allocations %u releases %u\n",checks,allocated,released);return 0;}
