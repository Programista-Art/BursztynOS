#define main physical_suite_main
#include "testy/test_xhci_physical_mouse.cpp"
#undef main
namespace BootTiming { uint64_t host_ticks(){return mock_now+1;} }
static unsigned invocations=0;static uint64_t max_service_ns=0;
static void service(uint64_t advance=0){
    mock_now+=advance;progress_clock();const uint64_t before=mock_now;const bool flags=prior_if;
    const auto state=enumeration.step;const unsigned controls=control_requests;
    usb_obsluz();++invocations;CHECK(prior_if==flags);
    const auto elapsed=mock_now-before;max_service_ns=std::max(max_service_ns,elapsed);
    CHECK(elapsed<1000000); // clock_step is 1us, never a hardware deadline wait
    CHECK(control_requests-controls<=1);
    if(state==EnumStep::Enable)CHECK(enumeration.step==EnumStep::EnableWait);
    if(state==EnumStep::Address)CHECK(enumeration.step==EnumStep::AddressWait);
    if(functional_trace)printf("STATE %u %u %u %llu %u %d %d\n",unsigned(enumeration.step),enumeration.port,enumeration.slot,
        (unsigned long long)xhc.wanted_command,control_requests,px,py);
}
static void until(EnumStep desired){for(unsigned n=0;n<1000&&enumeration.step!=desired;++n)service();CHECK(enumeration.step==desired);}
static void command_completion(uint8_t code=1){CHECK(delayed_command_pointer);emit(delayed_command_pointer,uint32_t(code)<<24,
    xhci_trb::type_field(33)|(uint32_t(delayed_command_slot)<<24));delayed_command_pointer=0;}
static void start(unsigned ports=20){fixture();BootTiming::first_visible_splash=false;
    put(registers,4,32U|(4U<<8)|(ports<<24));
    put(registers,0x110,2U|(2U<<24));put(registers,0x118,1U|(ports<<8));
    CHECK(xhci_inicjalizuj_pierwszy());}
static void connect(unsigned port){put(xhc.operational,PORTSC_BASE+(port-1)*16,PORT_CCS|(1U<<10)|(1U<<17));
    emit(uint64_t(port)<<24,0,xhci_trb::type_field(34));}
static void drain(unsigned n=500,uint64_t advance=0){for(unsigned i=0;i<n;++i)service(advance);}
static void active(unsigned slot=2){for(unsigned n=0;n<1000&&(!xhc.devices[slot].hid_transfer_oczekiwany||enumeration.step!=EnumStep::Idle);++n)service();CHECK(xhc.devices[slot].hid_transfer_oczekiwany);}
static void asynchronous_stages(){const unsigned begin=checks;start();connect(1);delayed_commands=true;
    for(unsigned i=0;i<100;++i)service();CHECK(enumeration.step==EnumStep::Idle);CHECK(!xhc.wanted_command);
    BootTiming::framebuffer_published();drain(10);CHECK(enumeration.step==EnumStep::Idle);
    BootTiming::splash_started();until(EnumStep::EnableWait);CHECK(xhc.wanted_command);CHECK(!xhc.devices[2].enabled);
    drain(3);CHECK(enumeration.step==EnumStep::EnableWait);
    command_completion();service();CHECK(enumeration.step==EnumStep::Address);CHECK(xhc.devices[2].enabled);
    service();CHECK(enumeration.step==EnumStep::AddressWait);drain(3);CHECK(!xhc.devices[2].addressed);
    command_completion();service();CHECK(xhc.devices[2].addressed);
    control_delay=WAIT_NS-100000;until(EnumStep::ControlWait);CHECK(pending_control);CHECK(!xhc.transfer_result.valid);
    service();CHECK(enumeration.step==EnumStep::ControlWait);
    service(WAIT_NS-120000);CHECK(enumeration.step==EnumStep::ControlWait);
    service(50000);CHECK(enumeration.step==EnumStep::FirstDescriptorDone);CHECK(enumeration.actual==8);
    delayed_commands=false;control_delay=0;active();CHECK(xhc.ports[1].state==PortState::Active);
    CHECK(xhc.devices[2].pointer_device.pointer_state==INPUT_POINTER_ARMED);
    report_complete({0,0xff,0xfe});CHECK(px==639&&py==358);CHECK(input_pointer_is_active(&xhc.devices[2].pointer_device));
    clean();section("Incremental submissions/later completion/near deadline/publication barrier",begin);
}
static void topology_cases(){unsigned begin=checks;
    for(unsigned ports:{1U,20U,255U}){start(ports);BootTiming::first_visible_splash=true;
        const unsigned resets=port_writes,controls=control_requests;drain(ports);
        CHECK(!xhc.discovery_pending);CHECK(!xhc.wanted_command);CHECK(control_requests==controls);
        CHECK(port_writes-resets<=ports);CHECK(enumeration.step==EnumStep::Idle);clean();}
    start();connect(1);connect(2);BootTiming::first_visible_splash=true;drain();
    CHECK(xhc.devices[2].hid_transfer_oczekiwany&&xhc.devices[3].hid_transfer_oczekiwany);
    CHECK(xhc.devices[2].root_port==1&&xhc.devices[3].root_port==2);clean();
    start();connect(1);BootTiming::first_visible_splash=true;control_delay=400000000;
    until(EnumStep::ControlWait);for(unsigned n=0;n<10;++n)service(10000000);CHECK(enumeration.step==EnumStep::ControlWait);
    control_delay=0;service(400000000);active();clean();
    section("Disconnected/many disconnected/multiple ports/slow device fairness",begin);
}
static void failures(){unsigned begin=checks;
    for(unsigned failure=0;failure<5;++failure){start();connect(1);BootTiming::first_visible_splash=true;
        if(failure==0){delayed_commands=true;until(EnumStep::EnableWait);service(WAIT_NS+1);CHECK(enumeration.step==EnumStep::Idle);}
        if(failure==1){delayed_commands=true;until(EnumStep::EnableWait);command_completion();service();until(EnumStep::AddressWait);command_completion(5);service();CHECK(enumeration.step==EnumStep::Retire);delayed_commands=false;}
        if(failure==2){control_error=4;until(EnumStep::FirstDescriptorDone);service();CHECK(enumeration.step==EnumStep::Retire);}
        if(failure==3){control_short=4;until(EnumStep::FirstDescriptorDone);CHECK(enumeration.actual==4);service();CHECK(enumeration.step==EnumStep::Retire);}
        if(failure==4){control_delay=WAIT_NS*2;until(EnumStep::ControlWait);service(WAIT_NS+1);CHECK(enumeration.step==EnumStep::Retire);pending_control=0;}
        delayed_commands=false;control_error=0;control_short=0;control_delay=0;drain(100);
        CHECK(xhc.ports[1].state==PortState::Failed);CHECK(enumeration.step==EnumStep::Idle);
        const auto requests=control_requests;drain(100,WAIT_NS);CHECK(control_requests==requests); // no tick retries
        connect(1);active(enable_slot_next);CHECK(xhc.ports[1].state==PortState::Active);clean();
    }
    // Disconnect while EP0 DMA is pending: no free before Disable completion.
    start();connect(1);BootTiming::first_visible_splash=true;control_delay=WAIT_NS*2;
    until(EnumStep::ControlWait);auto* buffer=enumeration.data.virtual_address;CHECK(buffer);
    put(xhc.operational,PORTSC_BASE,1U<<17);service();CHECK(enumeration.step==EnumStep::Retire);
    delayed_commands=true;service();CHECK(enumeration.step==EnumStep::RetireWait);CHECK(enumeration.data.virtual_address==buffer);
    pending_control=0;command_completion();service();CHECK(!enumeration.data.virtual_address);CHECK(!xhc.devices[2].enabled);
    control_delay=0;delayed_commands=false;connect(1);active(3);CHECK(px==640&&py==360);clean();
    // Disconnect/reconnect coalesced CSC retires only this source.
    boot_and_insert(true);report_complete({0,4,5});const int x=px,y=py;connect(9);drain(400);
    CHECK(xhc.devices[3].hid_transfer_oczekiwany);CHECK(!xhc.devices[2].enabled);CHECK(px==x&&py==y);clean();
    // A reconnect while Enable Slot is pending must retire the old generation,
    // then enumerate the replacement without another reset-change retry.
    start();connect(1);BootTiming::first_visible_splash=true;delayed_commands=true;
    until(EnumStep::EnableWait);connect(1);service();command_completion();service();
    CHECK(enumeration.step==EnumStep::Retire);delayed_commands=false;active(3);
    CHECK(!xhc.devices[2].enabled&&xhc.devices[3].root_port==1);clean();
    // A corrupt Enable completion for an existing live slot must not retire it.
    start();connect(1);BootTiming::first_visible_splash=true;active();connect(2);
    delayed_commands=true;until(EnumStep::EnableWait);delayed_command_slot=2;
    put(xhc.operational,PORTSC_BASE+16,1U<<17);command_completion();service();
    CHECK(xhc.devices[2].hid_transfer_oczekiwany&&!xhc.devices[2].retiring);
    CHECK(enumeration.step==EnumStep::Idle);clean();
    section("Command/transfer/short/timeout/disconnect/reconnect safe retirement",begin);
}
int main(){functional_trace=true;asynchronous_stages();topology_cases();failures();
    printf("Incremental enumeration total: %u checks PASS; service calls=%u max simulated service ns=%llu\n",checks,invocations,(unsigned long long)max_service_ns);}
