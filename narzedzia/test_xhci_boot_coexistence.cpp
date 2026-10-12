// The real controller state machine on deterministic registers and monotonic time.
#define main existing_physical_fixture_main
#include "narzedzia/test_xhci_physical_mouse.cpp"
#undef main
#include "boot_phase_timing.h"
namespace BootTiming { uint64_t host_ticks(){return mock_now+1;} }
static void reset_timing(){
    BootTiming::recorded=BootTiming::emitted=0;BootTiming::summary_emitted=false;
    for(auto& t:BootTiming::totals)t=0;
    for(auto& c:BootTiming::calls)c=0;
    BootTiming::entry_ticks=BootTiming::framebuffer_ticks=BootTiming::splash_ticks=0;
}
static void timing_benchmark(){
    fixture();reset_timing();clock_step=100000;control_delay=400000000;
    put(registers,0x80+PORTSC_BASE+8*16,PORT_CCS|(1U<<10)|(1U<<17));
    const unsigned controls_before=control_requests;
    const uint64_t begin=mock_now;
    CHECK(xhci_inicjalizuj_pierwszy());
    const uint64_t synchronous=mock_now-begin;
#ifdef BURSZTYN_XHCI_BASELINE
    CHECK(xhc.devices[2].hid_gotowy);CHECK(synchronous>=2000000000ULL);
    printf("BOOT baseline USB-before-splash=%llu ns control-requests=%u\n",(unsigned long long)synchronous,control_requests-controls_before);
#else
    CHECK(!xhc.devices[2].hid_gotowy);CHECK(control_requests==controls_before);
    CHECK(synchronous<10000000ULL);CHECK(BootTiming::calls[BootTiming::PortReset]==0);
    CHECK(BootTiming::calls[BootTiming::Control]==0);CHECK(xhc.discovery_pending);
    BootTiming::first_visible_splash=true;const uint64_t visible=mock_now; // Display is free to publish before deferred service.
    for(unsigned i=0;i<xhc.max_ports;++i)usb_obsluz();
    for(unsigned call=0;call<2000&&!xhc.devices[2].hid_transfer_oczekiwany;++call){mock_now+=10000000;usb_obsluz();}
    CHECK(xhc.devices[2].hid_transfer_oczekiwany);CHECK(mock_now-visible>=2000000000ULL);
    printf("BOOT repaired USB-before-splash=%llu ns USB-after-publication=%llu ns control-requests=%u\n",
        (unsigned long long)synchronous,(unsigned long long)(mock_now-visible),control_requests-controls_before);
    for(unsigned i=0;i<BootTiming::Count;++i)if(BootTiming::calls[i])
        printf("PHASE %s calls=%llu ns=%llu\n",BootTiming::name(BootTiming::Phase(i)),
            (unsigned long long)BootTiming::calls[i],(unsigned long long)BootTiming::totals[i]);
#endif
    clean();
}
#ifndef BURSZTYN_XHCI_BASELINE
static void startup_tests(){unsigned start=checks;
    for(unsigned count:{1U,20U,255U}){
        fixture();reset_timing();put(registers,4,32U|(4U<<8)|(count<<24));
        // One Supported Protocol covering every USB2 port, no second overlap.
        put(registers,0x110,2U|(2U<<24));put(registers,0x118,1U|(count<<8));
        CHECK(xhci_inicjalizuj_pierwszy());const unsigned writes=port_writes,controls=control_requests;
        const uint64_t begin=mock_now;
        for(unsigned i=0;i<count;++i)usb_obsluz();
        CHECK(!xhc.discovery_pending);CHECK(BootTiming::calls[BootTiming::PortInspection]==count);
        CHECK(BootTiming::calls[BootTiming::PortReset]==0);CHECK(BootTiming::calls[BootTiming::EnableSlot]==0);
        CHECK(control_requests==controls);CHECK(port_writes-writes<=count);CHECK(mock_now-begin<10000000);
        for(unsigned i=1;i<=count;++i){const unsigned w=port_writes;const uint64_t t=mock_now;
            CHECK(!reset_port(uint8_t(i)));CHECK(w==port_writes);CHECK(mock_now-t<100000);}
        printf("EMPTY ports=%u ns=%llu resets=0 controls=0\n",count,(unsigned long long)(mock_now-begin));clean();
    }
    for(bool bios_owned:{false,true})for(uint64_t delay:{UINT64_C(0),UINT64_C(100000000)}){
        if(!bios_owned && delay)continue;
        fixture();reset_timing();xhc.mmio=registers;xhc.bar_size=65536;xhc.max_ports=20;
        ownership_delay=delay;if(delay)clock_step=100000;
        put(registers,0x100,1U|(4U<<8)|(bios_owned?1U<<16:1U<<24));
        const uint64_t begin=mock_now;CHECK(ext_capabilities(xhc,0x40U<<16));
        CHECK(delay?(mock_now-begin>=delay&&mock_now-begin<delay+1000000):mock_now-begin<100000);CHECK(read32(registers,0x100)&(1U<<24));
        CHECK(!(read32(registers,0x100)&(1U<<16)));CHECK(!(read32(registers,0x104)&0x1ff0e011U));clean();
    }
    // Independent failures must consume one controller timeout, never per port.
    for(unsigned failure=0;failure<4;++failure)for(bool entry_if:{false,true}){
        fixture();reset_timing();clock_step=1000000;prior_if=entry_if;
        if(failure==0)firmware_stuck=true;
        if(failure==1){halt_stuck=true;put(registers,0x80+USBSTS,0);}
        if(failure==2)reset_stuck=true;
        if(failure==3)cnr_stuck=true;
        const uint64_t begin=mock_now;CHECK(!xhci_inicjalizuj_pierwszy());
        const uint64_t spent=mock_now-begin;CHECK(spent>=1000000000ULL&&spent<1100000000ULL);
        CHECK(prior_if==entry_if);CHECK(!xhc.ready);CHECK(!idt_zaalokowane_wektory_liczba());
        CHECK(BootTiming::calls[BootTiming::PortReset]==0);CHECK(BootTiming::calls[BootTiming::Control]==0);
        printf("TIMEOUT stage=%u IF=%u ns=%llu\n",failure,entry_if,(unsigned long long)spent);clean();
    }
    fixture();reset_timing();CHECK(xhci_inicjalizuj_pierwszy());clock_step=1000000;port_reset_stuck=true;
    put(xhc.operational,PORTSC_BASE,PORT_CCS|(1U<<10));
    const unsigned writes=port_writes;const uint64_t begin=mock_now;usb_obsluz();
    CHECK(mock_now-begin<10000000ULL);
    usb_obsluz();CHECK(enumeration.step==EnumStep::ResetWait);
    mock_now+=WAIT_NS;usb_obsluz();CHECK(enumeration.step==EnumStep::Idle);
    CHECK(xhc.ports[1].state==PortState::Failed);CHECK(port_writes>writes);
    const auto resets=std::count_if(port_values.begin(),port_values.end(),[](uint32_t v){return v&(PORT_PR|PORT_WPR);});
    for(unsigned i=0;i<3;++i){xhc.port_event=true;usb_obsluz();}
    CHECK(std::count_if(port_values.begin(),port_values.end(),[](uint32_t v){return v&(PORT_PR|PORT_WPR);})==resets); // Own reset changes do not restart a failed connection.
    clean();prior_if=true;
    boot_and_insert(true);
    const InputEvent ps2_first[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(-5)},
        {INPUT_EV_REL,INPUT_REL_Y,6},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&touchpad,ps2_first,3);CHECK(px==635&&py==366);
    report_complete({0,2,3});CHECK(px==637&&py==369);
    input_dispatch_events(&touchpad,ps2_first,3);CHECK(px==632&&py==375);
    CHECK(input_pointer_is_active(&touchpad)&&input_pointer_is_active(&xhc.devices[2].pointer_device));
    const int saved_x=px,saved_y=py;
    put(xhc.operational,PORTSC_BASE+8*16,PORT_CCS|(1U<<10)|(1U<<17)|(1U<<9));
    emit(uint64_t(9)<<24,0,xhci_trb::type_field(34));xhci_irq(&xhc);
    for(unsigned call=0;call<500&&!xhc.devices[3].hid_transfer_oczekiwany;++call)usb_obsluz();
    CHECK(xhc.devices[3].hid_transfer_oczekiwany);CHECK(!xhc.devices[2].enabled);
    CHECK(px==saved_x&&py==saved_y);CHECK(input_pointer_is_active(&touchpad));
    CHECK(!input_pointer_is_active(&xhc.devices[3].pointer_device));
    auto& replacement=xhc.devices[3];static_cast<uint8_t*>(replacement.hid_raport.virtual_address)[1]=0xff;
    emit(replacement.hid_transfer_pointer,(13U<<24)|uint32_t(replacement.hid_dma_length-3),
        xhci_trb::type_field(32)|(3U<<24)|(uint32_t(replacement.hid_dci)<<16));
    xhci_irq(&xhc);usb_obsluz();CHECK(px==saved_x-1&&py==saved_y);
    CHECK(input_pointer_is_active(&touchpad)&&input_pointer_is_active(&replacement.pointer_device));
    clean();section("Deferred startup/empty ports/handoff/controller and port timeout bounds",start);
}
#endif
int main(){
#ifdef BURSZTYN_XHCI_BASELINE
    timing_benchmark();
#else
    functional_trace=true;existing_physical_fixture_main();startup_tests();timing_benchmark();
#endif
    printf("Boot/coexistence fixture total: %u checks PASS\n",checks);
}
