#include "narzedzia/pointing_host_stubs.h"
#include "sterowniki/usb/hid_pointer.h"
#include "narzedzia/pointing_test_descriptors.h"
#include "narzedzia/virtualbox_tablet_descriptor.h"
#include "narzedzia/pointer_transport_stream.h"
#include "sterowniki/usb/xhci.cpp"
#include <cstring>
static uint64_t dma_next=0x100000;
bool dma_allocate(size_t n,size_t alignment,DmaBuffer* b){n=(n+4095)&~size_t(4095);void* p=std::aligned_alloc(alignment,n);if(!p)return false;std::memset(p,0,n);*b={p,dma_next,n,n/4096};dma_next+=n;return true;}
void dma_release(DmaBuffer* b){std::free(b->virtual_address);*b={};}
extern "C" void usb_hid_przetworz_raport(const uint8_t*){}
void ohci_service(){}
void ZmapujStrone(void*,void*,unsigned int){CHECK(false);}
static uint8_t config[256]{};
static bool have_pci{},clock_ready{},reject_msi{},reject_msix{};
static unsigned pci_writes{};
uint32_t host_pci_read(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t offset){
    if(!have_pci || bus!=2 || slot!=3 || fn!=1)return UINT32_MAX;
    uint32_t v;std::memcpy(&v,config+(offset&252),4);return v;
}
void host_pci_write(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t offset,uint32_t value){
    CHECK(bus==2 && slot==3 && fn==1);++pci_writes;
    if(offset==4) { uint32_t old;std::memcpy(&old,config+4,4);
        value=(value&65535)|((old&0xFFFF0000U)&~(value&0xF9000000U)); }
    if(offset==0x40 && reject_msi)value&=~(1U<<16);
    if(offset==0x40 && reject_msix)value&=~(1U<<31);
    std::memcpy(config+(offset&252),&value,4);
}
static bool irq_enabled;
static unsigned irq_saves,irq_restores;
unsigned long host_idt_irq_save(){++irq_saves;unsigned long f=irq_enabled?512:0;irq_enabled=false;return f;}
void host_idt_irq_restore(unsigned long f){++irq_restores;irq_enabled=(f&512)!=0;}
extern "C" uint8_t apic_pobierz_lapic_id(){return 2;}
bool hpet_dostepny(){return clock_ready;}
uint64_t czas_monotoniczny_ns(){static uint64_t n;return ++n;}
void irq_tests(){
    const unsigned start=checks;
    alignas(64) uint8_t mmio[4096]{};
    for(bool prior:{false,true}) {
        irq_enabled=prior;xhc=Controller{};
        xhc.pci={2,3,1,0x8086,0x1e31};xhc.mmio=mmio;xhc.runtime=mmio+256;
        xhc.bar_size=sizeof(mmio);xhc.operational=mmio+512;
        have_pci=true;std::memset(config,0,sizeof(config));config[6]=16;
        CHECK(!setup_interrupt(xhc));CHECK(irq_enabled==prior);CHECK(idt_zaalokowane_wektory_liczba()==0);
        config[0x34]=0x40;config[0x40]=5;config[0x42]=0x80;
        CHECK(setup_interrupt(xhc));CHECK(xhc.irq_vector>=48&&xhc.irq_vector!=44);
        CHECK(xhc.irq_mode==1);CHECK(irq_enabled==prior);
        CHECK(idt_zaalokowane_wektory_liczba()==1);
        const int vector=xhc.irq_vector;xhc.ready=true;
        CHECK(xhci_inicjalizuj_pierwszy());CHECK(xhc.irq_vector==vector);
        CHECK(idt_zaalokowane_wektory_liczba()==1);
        xhci_irq(&xhc);CHECK(xhc.irq_pending==1);CHECK(read32(xhc.runtime+0x20,0)==1);
        release_interrupt(xhc);CHECK(idt_zaalokowane_wektory_liczba()==0);CHECK(irq_enabled==prior);
        reject_msi=true;CHECK(!setup_interrupt(xhc));reject_msi=false;
        CHECK(idt_zaalokowane_wektory_liczba()==0);CHECK(irq_enabled==prior);
        // MSI payload overlapping the next capability must be rejected.
        config[0x41]=0x48;config[0x48]=0x09;config[0x49]=0;
        PciMsiCapability msi{};CHECK(!pci_find_msi(2,3,1,&msi));
        CHECK(!setup_interrupt(xhc));CHECK(idt_zaalokowane_wektory_liczba()==0);
        // MSI-X table/PBA wholly in this controller BAR.
        std::memset(config,0,sizeof(config));config[6]=16;config[0x34]=0x40;config[0x40]=0x11;
        uint32_t offset=0x800;std::memcpy(config+0x44,&offset,4);offset=0x900;std::memcpy(config+0x48,&offset,4);
        CHECK(setup_interrupt(xhc));CHECK(xhc.irq_mode==2);CHECK(xhc.msix_entry->msg_data==uint32_t(xhc.irq_vector));
        release_interrupt(xhc);CHECK(idt_zaalokowane_wektory_liczba()==0);CHECK(irq_enabled==prior);
        // MSI-X enable readback failure falls back to the same allocated MSI vector.
        config[0x41]=0x60;config[0x60]=5;config[0x62]=0x80;
        reject_msix=true;CHECK(setup_interrupt(xhc));CHECK(xhc.irq_mode==1);
        reject_msix=false;release_interrupt(xhc);CHECK(idt_zaalokowane_wektory_liczba()==0);
        config[0x41]=0;
        offset=0x1000;std::memcpy(config+0x44,&offset,4);CHECK(!setup_interrupt(xhc));
        CHECK(idt_zaalokowane_wektory_liczba()==0);CHECK(irq_saves==irq_restores);
        xhc=Controller{};clock_ready=false;CHECK(!xhci_inicjalizuj_pierwszy());CHECK(irq_enabled==prior);
        CHECK(!xhci_inicjalizuj_pierwszy());CHECK(idt_zaalokowane_wektory_liczba()==0);
    }
    CHECK(pci_writes>0);xhc=Controller{};have_pci=false;
    printf("xHCI production interrupt setup/BDF/capability/failure/IF: %u checks PASS\n",checks-start);
}
void producer_tests(){
    const unsigned start=checks;
    dma_next=UINT64_C(0x100100000); // exercise full-width physical addresses
    for(uint32_t type:{xhci_trb::TYPE_NORMAL,xhci_trb::TYPE_ENABLE_SLOT}) {
        XhciProducerRing ring{};CHECK(xhci_command_ring_create(&ring,16));
        CHECK(ring.trbs[15].dword1==uint32_t(ring.dma.physical_address>>32));
        for(unsigned i=0;i<120;++i){XhciTrb t{};t.dword3=xhci_trb::type_field(type);uint64_t ptr;
            const unsigned index=ring.producer_index;const bool cycle=ring.producer_cycle;
            CHECK(xhci_command_ring_enqueue(&ring,t,&ptr));CHECK(ptr==ring.dma.physical_address+index*16);
            CHECK(bool(ring.trbs[index].dword3&1)==cycle);
            if(index==14){CHECK(ring.producer_index==0);CHECK(ring.producer_cycle!=cycle);
                CHECK(bool(ring.trbs[15].dword3&1)==cycle);CHECK(ring.trbs[15].dword3&xhci_trb::TOGGLE_CYCLE);}
            xhci_ring_complete(&ring,1);CHECK(ring.outstanding==0);
        }
        xhci_command_ring_destroy(&ring);CHECK(!ring.trbs);
    }
    printf("Command/transfer producer wrap and Link cycle: %u checks PASS\n",checks-start);
}
void context_tests(){
    const unsigned start=checks;
    alignas(64) uint8_t mmio[4096]{},runtime[256]{},bells[1024]{};
    XhciTrb events[EVENT_TRBS]{};xhc=Controller{};
    xhc.mmio=mmio;xhc.bar_size=sizeof(mmio);xhc.max_ports=1;
    write32(mmio,0x100,2U|(2U<<24));write32(mmio,0x104,0x20425355);
    write32(mmio,0x108,1U|(1U<<8)|(1U<<28));write32(mmio,0x10C,0);
    write32(mmio,0x110,1U|(2U<<4)|(12U<<16));
    CHECK(parse_protocol(xhc,0x100,8));CHECK(xhc.protocols[0].psi_count==1);
    CHECK(xhc.protocols[0].psi[0].mantissa==12);CHECK(xhc.ports[1].protocol==&xhc.protocols[0]);
    xhc.context_size=32;xhc.runtime=runtime;xhc.doorbells=bells;xhc.ready=true;xhc.enabled_slots=1;
    xhc.event_segment={events,0x200000,sizeof(events),1};
    CHECK(xhci_command_ring_create(&xhc.command,16));
    XhciDevice& d=xhc.devices[1];d.slot_id=1;d.enabled=true;d.logical_speed=USB_FULL_SPEED;
    CHECK(dma_allocate(33*32,64,&d.input_context));CHECK(dma_allocate(32*32,64,&d.output_context));
    auto completion=[&](){auto& e=events[xhc.event_index];const uint64_t ptr=xhc.command.dma.physical_address+uint64_t(xhc.command.producer_index)*16;
        e={uint32_t(ptr),uint32_t(ptr>>32),1U<<24,xhci_trb::type_field(xhci_trb::TYPE_COMMAND_COMPLETION)|(1U<<24)|(xhc.event_cycle?1U:0U)};};
    hid_wynik_konfiguracji h{};h.znaleziono=true;h.dci=3;h.endpoint=0x81;h.maksymalny_rozmiar=8;h.interwal=10;h.type=USB_HID_POINTER;
    completion();CHECK(xhci_skonfiguruj_endpoint(d,h));
    uint32_t* ep=xhci_context_ptr(d.input_context,3);
    CHECK((ep[4]>>16)==8);CHECK((ep[4]&65535)==8);CHECK(((ep[1]>>3)&7)==7);
    CHECK(((ep[0]>>16)&255)==6);CHECK(ep[3]==uint32_t(d.hid_ring.dma.physical_address>>32));
    xhci_command_ring_destroy(&d.hid_ring);dma_release(&d.hid_raport);
    // High-speed bInterval=1 legitimately encodes interval zero.
    d.logical_speed=USB_HIGH_SPEED;h.interwal=1;completion();CHECK(xhci_skonfiguruj_endpoint(d,h));
    CHECK(d.hid_interwal==0);
    d.enabled=false;free_device(d);xhci_command_ring_destroy(&xhc.command);xhc=Controller{};
    printf("xHCI Supported Protocol/endpoint context: %u checks PASS\n",checks-start);
}

void first_submission_failure_tests(){
    const unsigned start=checks;input_reset_all();InputDevice fallback{};
    fallback.bus_type=INPUT_BUS_PS2;fallback.kinds=INPUT_DEVICE_KIND_MOUSE;CHECK(input_register_device(&fallback));
    xhc=Controller{};auto& d=xhc.devices[1];d.enabled=true;d.hid_gotowy=true;d.slot_id=1;d.hid_dci=3;
    d.type=USB_HID_POINTER;d.hid_dma_length=8;CHECK(dma_allocate(8,64,&d.hid_raport));
    CHECK(xhci_command_ring_create(&d.hid_ring,16));auto desc=tablet_descriptor();
    CHECK(hid_pointer_compile(desc.data(),desc.size(),d.pointer_layout));
    CHECK(hid_pointer_register(d.pointer_device,d.pointer_layout,1));
    for(unsigned i=0;i<14;++i){XhciTrb t{};uint64_t ptr;CHECK(xhci_command_ring_enqueue(&d.hid_ring,t,&ptr));}
    CHECK(!xhci_zakolejkuj_transfer_interrupt_in(d));CHECK(!d.hid_transfer_oczekiwany);
    CHECK(d.pointer_device.pointer_state==INPUT_POINTER_REGISTERED_INACTIVE);
    CHECK(input_get_pointer_owner()==&fallback);
    hid_pointer_remove(d.pointer_device);CHECK(input_get_pointer_owner()==&fallback);
    d.enabled=false;free_device(d);input_reset_all();xhc=Controller{};
    printf("xHCI failed first submission preserves PS/2: %u checks PASS\n",checks-start);
}
int main(){
    irq_tests();producer_tests();context_tests();first_submission_failure_tests();const unsigned runtime_start=checks;
    CHECK(xhci_oblicz_interwal(USB_FULL_SPEED,1)==3);
    CHECK(xhci_oblicz_interwal(USB_FULL_SPEED,10)==6);
    CHECK(xhci_oblicz_interwal(USB_LOW_SPEED,255)==10);
    CHECK(xhci_oblicz_interwal(USB_HIGH_SPEED,16)==15);
    CHECK(xhci_oblicz_interwal(USB_HIGH_SPEED,17)==0);
    CHECK(xhci_oblicz_interwal(USB_FULL_SPEED,0)==0);
    alignas(64) uint8_t doorbells[1024]{},runtime[256]{},operational[8192]{};XhciTrb events[EVENT_TRBS]{};
    xhc.operational=operational;xhc.doorbells=doorbells;xhc.runtime=runtime;xhc.ready=true;xhc.enabled_slots=1;
    xhc.event_segment={events,0x200000,sizeof(events),1};xhc.event_cycle=true;
    XhciDevice& d=xhc.devices[1];d.enabled=true;d.slot_id=1;d.hid_dci=3;d.type=USB_HID_POINTER;d.hid_gotowy=true;d.hid_dma_length=8;
    CHECK(xhci_command_ring_create(&d.hid_ring,64));CHECK(dma_allocate(8,64,&d.hid_raport));CHECK(xhci_command_ring_create(&d.ep0,64));
    std::vector<uint8_t> descriptor(virtualbox_tablet_descriptor,virtualbox_tablet_descriptor+sizeof(virtualbox_tablet_descriptor));CHECK(hid_pointer_compile(descriptor.data(),descriptor.size(),d.pointer_layout));
    CHECK(xhci_zakolejkuj_transfer_interrupt_in(d));const uint64_t first=d.hid_transfer_pointer;
    CHECK(!xhci_zakolejkuj_transfer_interrupt_in(d));CHECK(d.hid_ring.outstanding==1);
    auto event=[&](uint64_t ptr,uint8_t ep,uint8_t code,uint32_t residual,uint8_t slot=1){
        XhciTrb& t=events[xhc.event_index];t={};t.dword0=uint32_t(ptr);t.dword1=uint32_t(ptr>>32);t.dword2=(uint32_t(code)<<24)|residual;
        t.dword3=xhci_trb::type_field(xhci_trb::TYPE_TRANSFER_EVENT)|(uint32_t(slot)<<24)|(uint32_t(ep)<<16)|(xhc.event_cycle?1U:0U);
        CHECK(xhci_poll_events(1)==1);
    };
    InputDevice fallback{};fallback.bus_type=INPUT_BUS_PS2;fallback.kinds=INPUT_DEVICE_KIND_MOUSE;
    CHECK(input_register_device(&fallback));CHECK(hid_pointer_register(d.pointer_device,d.pointer_layout,1));
    CHECK(input_arm_pointer(&d.pointer_device));CHECK(input_get_pointer_owner()==&fallback);
    event(first,3,1,0,2);CHECK(!d.hid_result.valid);
    event(first,5,1,0);CHECK(!d.hid_result.valid);
    event(first+1,3,1,0);CHECK(!d.hid_result.valid);
    event(first,3,13,8);xhci_obsluz_hid(d);CHECK(input_get_pointer_owner()==&fallback);
    // Explicit transport failure before readiness leaves the existing owner.
    event(d.hid_transfer_pointer,3,6,0);xhci_obsluz_hid(d);
    CHECK(input_get_pointer_owner()==&fallback);CHECK(!d.pointer_device.registered);
    d.hid_gotowy=true;CHECK(hid_pointer_register(d.pointer_device,d.pointer_layout,1));
    CHECK(xhci_zakolejkuj_transfer_interrupt_in(d));
    uint64_t valid=d.hid_transfer_pointer;
    event(valid,3,13,7);xhci_obsluz_hid(d);CHECK(input_get_pointer_owner()==&fallback);
    valid=d.hid_transfer_pointer;
    px=0;py=0; // stationary first complete report must prove readiness
    pointer_transport_stream(d.pointer_layout,d.pointer_device,[&](const std::vector<uint8_t>& r){
        std::memcpy(d.hid_raport.virtual_address,r.data(),r.size());
        event(d.hid_transfer_pointer,3,1,uint32_t(8-r.size()));xhci_obsluz_hid(d);
        CHECK(d.hid_transfer_oczekiwany);CHECK(d.hid_ring.outstanding==1);
    });
    valid=d.hid_transfer_pointer;
    auto report=tablet_report(d.pointer_layout,32767,32767,7);std::memcpy(d.hid_raport.virtual_address,report.data(),report.size());
    event(valid,3,13,uint32_t(8-report.size()));CHECK(d.hid_result.valid);xhci_obsluz_hid(d);CHECK(px==1279&&py==719&&last_buttons==7);CHECK(d.pointer_device.registered);
    CHECK(d.hid_transfer_pointer!=first);const unsigned moved=motions;
    event(first,3,13,0);CHECK(!d.hid_result.valid);xhci_obsluz_hid(d);CHECK(motions==moved);CHECK(d.hid_ring.outstanding==1);
    // Repeated completions wrap both event and transfer rings; each requeues once.
    for(unsigned i=0;i<EVENT_TRBS*3;++i) {
        const uint64_t ptr=d.hid_transfer_pointer;
        event(ptr,3,13,uint32_t(8-report.size()));
        CHECK(d.hid_result.valid);event(ptr,3,13,0); // duplicate before deferred service
        xhci_obsluz_hid(d);CHECK(d.hid_transfer_oczekiwany);CHECK(d.hid_ring.outstanding==1);
        CHECK(read64(runtime+0x20,0x18)==(0x200000+uint64_t(xhc.event_index)*16+8));
    }
    CHECK(xhci_poll_events(1)==0); // old-cycle entries must never replay
    // A real IRQ notification drives deferred consumption and one requeue.
    auto& notified=events[xhc.event_index];const uint64_t queued=d.hid_transfer_pointer;
    notified={uint32_t(queued),uint32_t(queued>>32),(13U<<24)|uint32_t(8-report.size()),
        xhci_trb::type_field(xhci_trb::TYPE_TRANSFER_EVENT)|(1U<<24)|(3U<<16)|(xhc.event_cycle?1U:0U)};
    xhci_irq(&xhc);usb_obsluz();CHECK(!xhc.irq_pending);CHECK(d.hid_ring.outstanding==1);
    CHECK(d.hid_transfer_pointer!=queued);CHECK(read32(runtime+0x20,0)==2);
    // A short control data-stage completion must be retained until status stage.
    xhc.wanted_transfer=d.ep0.dma.physical_address+32;xhc.wanted_data=d.ep0.dma.physical_address+16;xhc.wanted_setup=d.ep0.dma.physical_address;xhc.control_slot=1;
    event(xhc.wanted_data,1,13,20);CHECK(!xhc.transfer_result.valid);CHECK(xhc.control_residual==20);
    event(xhc.wanted_transfer,1,1,0);CHECK(xhc.transfer_result.valid&&xhc.transfer_result.residual==20);
    // Endpoint error removes pointer readiness and does not requeue a halted EP.
    InputDevice& ps2=fallback;
    event(d.hid_transfer_pointer,3,6,0);xhci_obsluz_hid(d);CHECK(!d.hid_gotowy);CHECK(input_get_pointer_owner()==&ps2);CHECK(!d.hid_transfer_oczekiwany);
    CHECK(d.hid_raport.virtual_address!=nullptr); // retained until slot quiescence
    CHECK(xhc.ready); // endpoint failure leaves the controller serviceable
    CHECK(xhci_command_ring_create(&xhc.command,16));XhciTrb command{};
    command.dword3=xhci_trb::type_field(xhci_trb::TYPE_ENABLE_SLOT);uint64_t command_ptr;
    CHECK(xhci_command_ring_enqueue(&xhc.command,command,&command_ptr));xhc.wanted_command=command_ptr;
    events[xhc.event_index]={uint32_t(command_ptr),uint32_t(command_ptr>>32),1U<<24,
        xhci_trb::type_field(xhci_trb::TYPE_COMMAND_COMPLETION)|(1U<<24)|(xhc.event_cycle?1U:0U)};
    CHECK(xhci_poll_events(1)==1);CHECK(xhc.command_result.valid&&xhc.command_result.code==1);
    CHECK(xhc.command.outstanding==0);xhci_command_ring_destroy(&xhc.command);xhc.wanted_command=0;
    // A removal quarantines pending DMA until the slot is confirmed disabled.
    d.enabled=true;d.hid_gotowy=true;CHECK(xhci_zakolejkuj_transfer_interrupt_in(d));
    const uint64_t removed=d.hid_transfer_pointer;release_mouse_buttons(d);d.hid_gotowy=false;
    free_device(d);CHECK(d.hid_raport.virtual_address); // still enabled, cannot free
    event(removed,3,1,0);xhci_obsluz_hid(d);CHECK(!d.pointer_device.registered);
    d.enabled=false;free_device(d);CHECK(!d.hid_raport.virtual_address);free_device(d);
    event(removed,3,1,0);CHECK(!d.hid_result.valid); // late event after free
    // Controller-fatal status removes an owner and retains controller DMA.
    CHECK(hid_pointer_publish(d.pointer_device,d.pointer_layout,report.data(),report.size(),1)==false); // layout cleared on free
    CHECK(hid_pointer_compile(descriptor.data(),descriptor.size(),d.pointer_layout));
    CHECK(hid_pointer_publish(d.pointer_device,d.pointer_layout,report.data(),report.size(),1));
    CHECK(input_get_pointer_owner()==&d.pointer_device);
    write32(operational,USBSTS,STS_HSE);usb_obsluz();
    CHECK(!xhc.ready);CHECK(input_get_pointer_owner()==&fallback);CHECK(!d.pointer_device.registered);
    input_reset_all();xhci_command_ring_destroy(&d.hid_ring);xhci_command_ring_destroy(&d.ep0);dma_release(&d.hid_raport);
    printf("xHCI production pointer completions/short control/stale events: %u checks PASS\n",checks-runtime_start);
}
