// Included inside the controller namespace. One serialized EP0/command lane;
// independent port lifecycle and live HID rings continue alongside it.
enum class EnumStep : uint8_t {
    Idle, Reset, ResetWait, Enable, EnableWait, Address, AddressWait,
    FirstDescriptor, FirstDescriptorDone, Evaluate, EvaluateWait,
    DeviceDescriptor, DeviceDescriptorDone, ConfigHeader, ConfigHeaderDone,
    ConfigBody, ConfigBodyDone, Configure, ConfigureWait, SetConfig, SetConfigDone,
    Protocol, ProtocolDone, ReportDescriptor, ReportDescriptorDone,
    ReportProtocol, ReportProtocolDone, IdleRequest, IdleDone, Activate,
    ControlWait, RecoverReset, RecoverResetWait, RecoverDequeue, RecoverDequeueWait,
    Retire, RetireWait
};
struct Enumeration {
    EnumStep step=EnumStep::Idle,next=EnumStep::Idle;
    uint8_t port=0,slot=0,code=0,command_type=0;uint16_t length=0,mps=0;
    uint32_t actual=0,reset_change=0,generation=0;uint64_t due=0;
    bool ok=false; DmaBuffer data{};hid_wynik_konfiguracji hid{};
};
Enumeration enumeration{};

XhciTrb context_command(uint32_t type,const XhciDevice& d) {
    return {uint32_t(d.input_context.physical_address),uint32_t(d.input_context.physical_address>>32),0,
        xhci_trb::type_field(type)|(uint32_t(d.slot_id)<<24)};
}
bool submit_command(XhciTrb trb,EnumStep wait) {
    if(xhc.wanted_command)return false;
    uint64_t pa=0;if(!xhci_command_ring_enqueue(&xhc.command,trb,&pa))return false;
    xhc.wanted_command=pa;xhc.command_result={};enumeration.due=deadline();enumeration.step=wait;
    enumeration.command_type=uint8_t(xhci_trb::type(trb.dword3));
    const char* label=enumeration.command_type==9?"Enable Slot submit":enumeration.command_type==11?"Address Device submit":enumeration.command_type==12?"Configure Endpoint submit":"command submit";
    BootTiming::mark(label,enumeration.command_type);
    __atomic_thread_fence(__ATOMIC_RELEASE);write32(xhc.doorbells,0,0);return true;
}
// 0 pending, 1 successful identity/code, -1 completed failure/timeout.
int command_ready(uint8_t expected_slot=0) {
    if(xhc.command_result.valid){const auto r=xhc.command_result;xhc.wanted_command=0;
        BootTiming::mark(enumeration.command_type==9?"Enable Slot complete":enumeration.command_type==11?"Address Device complete":enumeration.command_type==12?"Configure Endpoint complete":"command complete",(uint32_t(r.slot)<<16)|(uint32_t(r.code)<<8));
        return r.code==1&&(!expected_slot||r.slot==expected_slot)?1:-1;}
    if(czas_monotoniczny_ns()>=enumeration.due){xhc.wanted_command=0;BootTiming::mark("command timeout",enumeration.slot);return -1;}
    return 0;
}
void enumeration_idle(bool safe_to_free) {
    if(safe_to_free)dma_release(&enumeration.data);
    else enumeration.data={}; // quarantine pending DMA until a confirmed Disable Slot
    xhc.wanted_transfer=xhc.wanted_setup=xhc.wanted_data=0;xhc.control_slot=0;
    enumeration=Enumeration{};
}
void enumeration_fail() {
    BootTiming::mark("enumeration failed",enumeration.port);
    auto& p=xhc.ports[enumeration.port];p.state=PortState::Failed;
    xhc.wanted_transfer=0;
    if(enumeration.slot && xhc.devices[enumeration.slot].enabled){
        auto& d=xhc.devices[enumeration.slot];d.hid_gotowy=false;d.hid_result={};
        d.retiring=true;release_mouse_buttons(d);enumeration.step=EnumStep::Retire;
    } else enumeration_idle(true);
}
bool enum_control(UsbSetupPacket setup,EnumStep next) {
    auto& d=xhc.devices[enumeration.slot];
    enumeration.length=setup.wLength;enumeration.actual=0;enumeration.code=0;
    enumeration.ok=false;enumeration.next=next;
    if(!submit_control(d,setup,setup.wLength?&enumeration.data:nullptr,setup.wLength)){enumeration_fail();return false;}
    enumeration.due=deadline();enumeration.step=EnumStep::ControlWait;return true;
}
bool enum_buffer(size_t bytes) {
    dma_release(&enumeration.data);
    if(!bytes||bytes>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI||!allocate_dma(bytes,64,&enumeration.data)){
        enumeration_fail();return false;}
    return true;
}
void inspect_one_port() {
    if(!xhc.max_ports)return;
    if(xhc.discovery_port>xhc.max_ports)xhc.discovery_port=1;
    uint8_t port=uint8_t(xhc.discovery_port++);
    if(xhc.discovery_port>xhc.max_ports)xhc.discovery_pending=false;
    for(uint32_t candidate=1;candidate<=xhc.max_ports;++candidate)if(xhc.ports[candidate].scan_pending){port=uint8_t(candidate);break;}
    auto& p=xhc.ports[port];p.scan_pending=false;
    BootTiming::Scope timing(BootTiming::PortInspection,port);
    const uint32_t status=read32(xhc.operational,PORTSC_BASE+(port-1U)*16U);
    const bool connected=status&PORT_CCS,csc=status&(1U<<17);
    if(csc||connected!=p.connection_seen)++p.generation;
    // Leave PRC/WRC to the outstanding reset operation. Its own change event
    // must never create another connection or erase completion evidence.
    const bool resetting=enumeration.port==port&&enumeration.step==EnumStep::ResetWait;
    port_clear_changes(port,status&PORT_RW1C&~(resetting?enumeration.reset_change:0U));
    if((!connected||csc)&&p.slot){
        auto& d=xhc.devices[p.slot];d.retiring=true;d.hid_gotowy=false;d.hid_result={};
        release_mouse_buttons(d);p.state=PortState::Retiring;p.reconnect=connected;
    }
    if(!connected){p.connection_seen=false;if(!p.slot)p.state=PortState::Empty;}
    else if(!p.slot&&(!p.connection_seen||csc)&&p.protocol){
        p.connection_seen=true;p.state=PortState::Queued;p.reconnect=false;
    }
}
void enumeration_step() {
    auto& e=enumeration;
    if(e.step==EnumStep::Idle){
        // Selection is bounded by the hardware's 255 root ports, with no MMIO,
        // command submission or waits in this scan. Retirement takes priority.
        for(uint32_t port=1;port<=xhc.max_ports;++port)if(xhc.ports[port].state==PortState::Retiring){
            e.port=uint8_t(port);e.slot=xhc.ports[port].slot;e.step=EnumStep::Retire;return;}
        for(uint32_t port=1;port<=xhc.max_ports;++port)if(xhc.ports[port].state==PortState::Queued){
            e.port=uint8_t(port);e.generation=xhc.ports[port].generation;xhc.ports[port].state=PortState::Enumerating;e.step=EnumStep::Reset;
            BootTiming::mark("connected port selected",port);return;}
        return;
    }
    auto& p=xhc.ports[e.port];auto& d=xhc.devices[e.slot];
    // Check this device every invocation, independently of the root-port scan.
    // Pending command identity must be resolved before reusing the shared lane.
    const uint32_t status=read32(xhc.operational,PORTSC_BASE+(e.port-1U)*16U);
    if((!(status&PORT_CCS)||e.generation!=p.generation||(e.slot&&d.retiring)) && e.step!=EnumStep::Retire&&e.step!=EnumStep::RetireWait){
        if(xhc.wanted_command){const int r=command_ready(e.slot);if(!r)return;
            if(!e.slot && r>0){e.slot=xhc.command_result.slot;
                if(e.slot&&e.slot<=xhc.enabled_slots&&!xhc.devices[e.slot].enabled){auto& slot=xhc.devices[e.slot];slot.enabled=true;slot.slot_id=e.slot;slot.root_port=e.port;p.slot=e.slot;}
                else e.slot=0;}}
        const bool replacement=(status&PORT_CCS)&&e.generation!=p.generation;
        enumeration_fail();
        if(replacement){if(p.slot)p.reconnect=true;else p.state=PortState::Queued;}
        return;
    }
    const auto data=[&](){return static_cast<const uint8_t*>(e.data.virtual_address);};
    switch(e.step){
    case EnumStep::Reset: {
        if(!(status&PORT_CCS)){enumeration_fail();return;}
        auto* proto=p.protocol;if(!proto||(!proto->usb2&&!proto->usb3)){enumeration_fail();return;}
        if(proto->usb3&&(status&PORT_PED)&&!(status&(PORT_PR|PORT_WPR))&&((status>>PORT_PLS_SHIFT)&PORT_PLS_MASK)==0){
            e.step=EnumStep::Enable;return;}
        const unsigned link=(status>>PORT_PLS_SHIFT)&PORT_PLS_MASK;
        const bool warm=proto->usb3&&(link==6||link==10||link==8);
        e.reset_change=warm?(1U<<19):PORT_PRC;
        port_clear_changes(e.port,status&PORT_RW1C);e.due=deadline();e.step=EnumStep::ResetWait;
        BootTiming::mark("port reset submit",e.port);port_set(e.port,warm?PORT_WPR:PORT_PR);return;
    }
    case EnumStep::ResetWait:
        if(!(status&(PORT_PR|PORT_WPR))&&(status&e.reset_change)){
            port_clear_changes(e.port,e.reset_change);BootTiming::mark("port reset complete",e.port);
            if(!(status&PORT_PED)||xhci_pobierz_predkosc_portu(e.port).logical==USB_SPEED_UNKNOWN){enumeration_fail();return;}
            e.step=EnumStep::Enable;
        } else if(czas_monotoniczny_ns()>=e.due){enumeration_fail();}
        return;
    case EnumStep::Enable: {
        XhciTrb t{};t.dword3=xhci_trb::type_field(9)|(uint32_t(p.protocol->slot_type)<<16);
        if(!submit_command(t,EnumStep::EnableWait)){enumeration_fail();}
        return;
    }
    case EnumStep::EnableWait: {
        const int r=command_ready();if(!r)return;
        const auto slot=xhc.command_result.slot;
        if(r<0||!slot||slot>xhc.enabled_slots||xhc.devices[slot].enabled){enumeration_fail();return;}
        e.slot=slot;p.slot=slot;
        if(!prepare_device(xhc.devices[slot],slot,e.port,xhci_pobierz_predkosc_portu(e.port))){enumeration_fail();return;}
        e.step=EnumStep::Address;return;
    }
    case EnumStep::Address:
        if(!submit_command(context_command(11,d),EnumStep::AddressWait)){enumeration_fail();}
        return;
    case EnumStep::AddressWait: {
        const int r=command_ready(e.slot);if(!r)return;if(r<0){enumeration_fail();return;}
        __atomic_thread_fence(__ATOMIC_ACQUIRE);d.addressed=true;d.device_address=uint8_t(context(d.output_context,0)[3]);
        e.step=EnumStep::FirstDescriptor;return;
    }
    case EnumStep::FirstDescriptor:
        if(enum_buffer(18)){enum_control({0x80,6,0x100,0,8},EnumStep::FirstDescriptorDone);}
        return;
    case EnumStep::FirstDescriptorDone:
        if(!e.ok||e.actual<8||data()[0]!=18||data()[1]!=1||!valid_mps(d,data()[7],&e.mps)){enumeration_fail();return;}
        e.step=e.mps==d.ep0_mps?EnumStep::DeviceDescriptor:EnumStep::Evaluate;return;
    case EnumStep::Evaluate: {
        zero_dma(d.input_context);context(d.input_context,0)[1]=1U<<1;
        auto* ep=context(d.input_context,2);const auto* old=context(d.output_context,1);
        for(unsigned i=0;i<xhc.context_size/4;++i)ep[i]=old[i];
        ep[1]=(ep[1]&0xffffU)|(uint32_t(e.mps)<<16);
        if(!submit_command(context_command(13,d),EnumStep::EvaluateWait)){enumeration_fail();}
        return;
    }
    case EnumStep::EvaluateWait: {
        const int r=command_ready(e.slot);if(!r)return;if(r<0){enumeration_fail();return;}
        d.ep0_mps=e.mps;e.step=EnumStep::DeviceDescriptor;return;
    }
    case EnumStep::DeviceDescriptor:
        zero_dma(e.data);enum_control({0x80,6,0x100,0,18},EnumStep::DeviceDescriptorDone);return;
    case EnumStep::DeviceDescriptorDone:
        if(!e.ok||e.actual<18||data()[0]!=18||data()[1]!=1){enumeration_fail();return;}
        d.vendor_id=le16(data()+8);d.product_id=le16(data()+10);e.step=EnumStep::ConfigHeader;return;
    case EnumStep::ConfigHeader:
        if(enum_buffer(9)){enum_control({0x80,6,0x200,0,9},EnumStep::ConfigHeaderDone);}
        return;
    case EnumStep::ConfigHeaderDone:
        if(!e.ok||e.actual<9||data()[0]<9||data()[1]!=2||le16(data()+2)<9||le16(data()+2)>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI){enumeration_fail();return;}
        e.length=le16(data()+2);e.step=EnumStep::ConfigBody;return;
    case EnumStep::ConfigBody: {
        const auto length=e.length;if(enum_buffer(length))enum_control({0x80,6,0x200,0,length},EnumStep::ConfigBodyDone);return;
    }
    case EnumStep::ConfigBodyDone:
        if(!e.ok||e.actual<9||!usb_sprawdz_deskryptor_konfiguracji(data(),e.actual,&e.hid)){enumeration_fail();return;}
        e.step=EnumStep::Configure;return;
    case EnumStep::Configure: {
        XhciTrb t{};if(!prepare_hid_endpoint(d,e.hid,t)||!submit_command(t,EnumStep::ConfigureWait)){enumeration_fail();}
        return;
    }
    case EnumStep::ConfigureWait: {
        const int r=command_ready(e.slot);if(!r)return;if(r<0){enumeration_fail();return;}
        e.step=EnumStep::SetConfig;return;
    }
    case EnumStep::SetConfig:
        enum_control({0,9,e.hid.konfiguracja,0,0},EnumStep::SetConfigDone);return;
    case EnumStep::SetConfigDone:
        if(!e.ok){enumeration_fail();return;}d.konfiguracja=e.hid.konfiguracja;e.step=EnumStep::Protocol;return;
    case EnumStep::Protocol:
        if(d.type==USB_HID_BOOT_KEYBOARD||(e.hid.subclass==1&&e.hid.protocol==2))
            enum_control({0x21,0x0b,0,e.hid.interfejs,0},EnumStep::ProtocolDone);
        else {e.step=EnumStep::ReportDescriptor;}
        return;
    case EnumStep::ProtocolDone:
        if(d.type==USB_HID_BOOT_KEYBOARD){if(!e.ok){enumeration_fail();return;}e.step=EnumStep::IdleRequest;return;}
        d.boot_mouse=e.ok;if(d.boot_mouse)d.hid_gotowy=hid_boot_mouse_layout(d.pointer_layout);
        e.step=EnumStep::ReportDescriptor;return;
    case EnumStep::ReportDescriptor:
        if(!e.hid.raport_dlugosc||e.hid.raport_dlugosc>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI){e.step=EnumStep::Activate;return;}
        dma_release(&e.data);
        if(!allocate_dma(e.hid.raport_dlugosc,64,&e.data)){
            if(d.boot_mouse)e.step=EnumStep::Activate;else enumeration_fail();return;
        }
        enum_control({0x81,6,0x2200,e.hid.interfejs,e.hid.raport_dlugosc},EnumStep::ReportDescriptorDone);return;
    case EnumStep::ReportDescriptorDone: {
        HidPointerLayout compiled{};
        const bool valid=e.ok&&e.actual==e.hid.raport_dlugosc&&hid_pointer_compile(data(),e.actual,compiled);
        if(d.boot_mouse){d.boot_wheel=valid&&hid_boot_mouse_has_wheel(compiled);e.step=EnumStep::Activate;return;}
        if(!valid){enumeration_fail();return;}d.pointer_layout=compiled;
        for(unsigned i=0;i<compiled.id_count;++i)if((compiled.bits[i]+7U)/8+(compiled.numbered?1U:0U)>d.hid_dma_length){enumeration_fail();return;}
        d.hid_gotowy=true;e.step=e.hid.subclass==1&&e.hid.protocol!=2?EnumStep::ReportProtocol:EnumStep::Activate;return;
    }
    case EnumStep::ReportProtocol:
        enum_control({0x21,0x0b,1,e.hid.interfejs,0},EnumStep::ReportProtocolDone);return;
    case EnumStep::ReportProtocolDone:
        if(!e.ok){enumeration_fail();return;}e.step=EnumStep::Activate;return;
    case EnumStep::IdleRequest:
        enum_control({0x21,0x0a,0,e.hid.interfejs,0},EnumStep::IdleDone);return;
    case EnumStep::IdleDone:
        d.hid_gotowy=e.ok||e.code==6;e.step=EnumStep::Activate;return;
    case EnumStep::Activate:
        if(!d.hid_gotowy||d.control_failed){enumeration_fail();return;}
        if(d.type==USB_HID_POINTER){d.pointer_device.usb_controller=INPUT_USB_CONTROLLER_XHCI;
            if(!hid_pointer_register(d.pointer_device,d.pointer_layout,d.slot_id)){enumeration_fail();return;}}
        if(!xhci_zakolejkuj_transfer_interrupt_in(d)){enumeration_fail();return;}
        BootTiming::mark("first interrupt IN submit",e.slot);BootTiming::mark("xHCI device armed",e.slot);
        p.state=PortState::Active;enumeration_idle(true);return;
    case EnumStep::ControlWait: {
        if(!xhc.transfer_result.valid){if(czas_monotoniczny_ns()>=e.due){d.control_failed=true;BootTiming::mark("control timeout",e.slot);enumeration_fail();}return;}
        const auto r=xhc.transfer_result;xhc.wanted_transfer=0;
        xhci_ring_complete(&d.ep0,e.length?3:2);e.code=r.code;
        e.ok=r.slot==e.slot&&r.endpoint==1&&r.residual<=e.length&&(r.code==1||r.code==13);
        e.actual=e.ok?e.length-r.residual:0;
        BootTiming::mark("control complete",(uint32_t(r.code)<<24)|e.actual);
        if(r.code==6)e.step=EnumStep::RecoverReset;
        else {e.step=e.next;}
        return;
    }
    case EnumStep::RecoverReset: {
        XhciTrb t{};t.dword3=xhci_trb::type_field(14)|(1U<<16)|(uint32_t(e.slot)<<24);
        if(!submit_command(t,EnumStep::RecoverResetWait)){enumeration_fail();}
        return;
    }
    case EnumStep::RecoverResetWait: {
        const int r=command_ready(e.slot);if(!r)return;if(r<0){d.control_failed=true;enumeration_fail();return;}
        e.step=EnumStep::RecoverDequeue;return;
    }
    case EnumStep::RecoverDequeue: {
        const uint64_t next=d.ep0.dma.physical_address+uint64_t(d.ep0.producer_index)*16;
        XhciTrb t{uint32_t(next)|(d.ep0.producer_cycle?1U:0U),uint32_t(next>>32),0,xhci_trb::type_field(16)|(1U<<16)|(uint32_t(e.slot)<<24)};
        if(!submit_command(t,EnumStep::RecoverDequeueWait)){enumeration_fail();}
        return;
    }
    case EnumStep::RecoverDequeueWait: {
        const int r=command_ready(e.slot);if(!r)return;if(r<0){d.control_failed=true;enumeration_fail();return;}
        e.step=e.next;return;
    }
    case EnumStep::Retire: {
        if(!e.slot||!d.enabled){enumeration_idle(true);return;}
        XhciTrb t{};t.dword3=xhci_trb::type_field(10)|(uint32_t(e.slot)<<24);
        if(!submit_command(t,EnumStep::RetireWait)){p.state=PortState::Failed;enumeration_idle(false);}return;
    }
    case EnumStep::RetireWait: {
        const int r=command_ready(e.slot);if(!r)return;
        if(r>0){d.enabled=false;free_device(d);p.slot=0;
            p.state=p.reconnect?PortState::Queued:p.connection_seen?PortState::Failed:PortState::Empty;p.reconnect=false;}
        else p.state=PortState::Failed; // retain slot and all DMA on failed Disable
        enumeration_idle(r>0);return;
    }
    case EnumStep::Idle:return;
    }
}
