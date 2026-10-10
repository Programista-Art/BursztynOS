#include "narzedzia/pointing_host_stubs.h"
#include "sterowniki/usb/hid_pointer.h"
#include "narzedzia/pointing_test_descriptors.h"
#include "sterowniki/usb/xhci.cpp"
#include <cstring>
static uint64_t dma_next=0x100000;
bool dma_allocate(size_t n,size_t alignment,DmaBuffer* b){n=(n+4095)&~size_t(4095);void* p=std::aligned_alloc(alignment,n);if(!p)return false;std::memset(p,0,n);*b={p,dma_next,n,n/4096};dma_next+=n;return true;}
void dma_release(DmaBuffer* b){std::free(b->virtual_address);*b={};}
extern "C" void usb_hid_przetworz_raport(const uint8_t*){}
int main(){
    CHECK(xhci_oblicz_interwal(USB_FULL_SPEED,1)==3);
    CHECK(xhci_oblicz_interwal(USB_FULL_SPEED,10)==6);
    CHECK(xhci_oblicz_interwal(USB_LOW_SPEED,255)==10);
    CHECK(xhci_oblicz_interwal(USB_HIGH_SPEED,16)==15);
    CHECK(xhci_oblicz_interwal(USB_HIGH_SPEED,17)==0);
    CHECK(xhci_oblicz_interwal(USB_FULL_SPEED,0)==0);
    alignas(64) uint8_t doorbells[1024]{},runtime[256]{};XhciTrb events[EVENT_TRBS]{};
    xhc.doorbells=doorbells;xhc.runtime=runtime;xhc.ready=true;xhc.enabled_slots=1;
    xhc.event_segment={events,0x200000,sizeof(events),1};xhc.event_cycle=true;
    XhciDevice& d=xhc.devices[1];d.enabled=true;d.slot_id=1;d.hid_dci=3;d.type=USB_HID_POINTER;d.hid_gotowy=true;d.hid_dma_length=8;
    CHECK(xhci_command_ring_create(&d.hid_ring,64));CHECK(dma_allocate(8,64,&d.hid_raport));CHECK(xhci_command_ring_create(&d.ep0,64));
    auto descriptor=tablet_descriptor();CHECK(hid_pointer_compile(descriptor.data(),descriptor.size(),d.pointer_layout));
    CHECK(xhci_zakolejkuj_transfer_interrupt_in(d));const uint64_t first=d.hid_transfer_pointer;
    CHECK(!xhci_zakolejkuj_transfer_interrupt_in(d));CHECK(d.hid_ring.outstanding==1);
    auto event=[&](uint64_t ptr,uint8_t ep,uint8_t code,uint32_t residual){
        XhciTrb& t=events[xhc.event_index];t={};t.dword0=uint32_t(ptr);t.dword1=uint32_t(ptr>>32);t.dword2=(uint32_t(code)<<24)|residual;
        t.dword3=xhci_trb::type_field(xhci_trb::TYPE_TRANSFER_EVENT)|(1U<<24)|(uint32_t(ep)<<16)|1;
        CHECK(xhci_poll_events(1)==1);
    };
    auto report=tablet_report(d.pointer_layout,32767,32767,7);std::memcpy(d.hid_raport.virtual_address,report.data(),report.size());
    event(first,3,13,uint32_t(8-report.size()));CHECK(d.hid_result.valid);xhci_obsluz_hid(d);CHECK(px==1279&&py==719&&last_buttons==7);CHECK(d.pointer_device.registered);
    CHECK(d.hid_transfer_pointer!=first);const unsigned moved=motions;
    event(first,3,13,0);CHECK(!d.hid_result.valid);xhci_obsluz_hid(d);CHECK(motions==moved);CHECK(d.hid_ring.outstanding==1);
    // A short control data-stage completion must be retained until status stage.
    xhc.wanted_transfer=d.ep0.dma.physical_address+32;xhc.wanted_data=d.ep0.dma.physical_address+16;xhc.wanted_setup=d.ep0.dma.physical_address;xhc.control_slot=1;
    event(xhc.wanted_data,1,13,20);CHECK(!xhc.transfer_result.valid);CHECK(xhc.control_residual==20);
    event(xhc.wanted_transfer,1,1,0);CHECK(xhc.transfer_result.valid&&xhc.transfer_result.residual==20);
    // Endpoint error removes pointer readiness and does not requeue a halted EP.
    InputDevice ps2{};ps2.bus_type=INPUT_BUS_PS2;ps2.kinds=INPUT_DEVICE_KIND_MOUSE;CHECK(input_register_device(&ps2));
    event(d.hid_transfer_pointer,3,6,0);xhci_obsluz_hid(d);CHECK(!d.hid_gotowy);CHECK(input_get_pointer_owner()==&ps2);CHECK(!d.hid_transfer_oczekiwany);
    CHECK(d.hid_raport.virtual_address!=nullptr); // retained until slot quiescence
    input_reset_all();xhci_command_ring_destroy(&d.hid_ring);xhci_command_ring_destroy(&d.ep0);dma_release(&d.hid_raport);
    printf("xHCI production pointer completions/short control/stale events: %u checks PASS\n",checks);
}
