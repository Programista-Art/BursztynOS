// Included inside the controller namespace. One serialized EP0/command lane;
// independent port lifecycle and live HID rings continue alongside it.

XhciTrb context_command(uint32_t type,const XhciDevice& d) {
    return {uint32_t(d.input_context.physical_address),uint32_t(d.input_context.physical_address>>32),0,
        xhci_trb::type_field(type)|(uint32_t(d.slot_id)<<24)};
}
bool submit_command(Controller& xhc,XhciTrb trb,EnumStep wait) {
    if(xhc.wanted_command)return false;
    uint64_t pa=0;if(!xhci_command_ring_enqueue(&xhc.command,trb,&pa))return false;
    xhc.wanted_command=pa;xhc.command_result={};xhc.enumeration.due=deadline();xhc.enumeration.step=wait;
    xhc.enumeration.command_type=uint8_t(xhci_trb::type(trb.dword3));
    const char* label=xhc.enumeration.command_type==9?"Enable Slot submit":xhc.enumeration.command_type==11?"Address Device submit":xhc.enumeration.command_type==12?"Configure Endpoint submit":"command submit";
    BootTiming::mark(label,xhc.enumeration.command_type);
    __atomic_thread_fence(__ATOMIC_RELEASE);write32(xhc.doorbells,0,0);return true;
}
// 0 pending, 1 successful identity/code, -1 completed failure/timeout.
int command_ready(Controller& xhc,uint8_t expected_slot=0) {
    if(xhc.command_result.valid){const auto r=xhc.command_result;xhc.wanted_command=0;
        BootTiming::mark(xhc.enumeration.command_type==9?"Enable Slot complete":xhc.enumeration.command_type==11?"Address Device complete":xhc.enumeration.command_type==12?"Configure Endpoint complete":"command complete",(uint32_t(r.slot)<<16)|(uint32_t(r.code)<<8));
        return r.code==1&&(!expected_slot||r.slot==expected_slot)?1:-1;}
    if(czas_monotoniczny_ns()>=xhc.enumeration.due){xhc.wanted_command=0;BootTiming::mark("command timeout",xhc.enumeration.slot);
        xhc.enumeration.diagnostic_status=DIAG_TIMEOUT;log_controller(xhc,"command timeout");log_values("[USB command timeout] port=",xhc.enumeration.port," type=",xhc.enumeration.command_type);return -2;}
    return 0;
}
void enumeration_idle(Controller& xhc,bool safe_to_free) {
    if(safe_to_free)dma_release(&xhc.enumeration.data);
    else xhc.enumeration.data={}; // quarantine pending DMA until a confirmed Disable Slot
    xhc.wanted_transfer=xhc.wanted_setup=xhc.wanted_data=0;xhc.control_slot=0;
    xhc.enumeration=Enumeration{};
}
void enumeration_fail(Controller& xhc) {
    BootTiming::mark("enumeration failed",xhc.enumeration.port);
    log_controller(xhc,"enumeration failed");
    log_values("[USB enumeration failure] port=",xhc.enumeration.port," step=",uint8_t(xhc.enumeration.step));
    log_values("[USB enumeration failure] slot=",xhc.enumeration.slot," completion=",xhc.enumeration.code);
    if(xhc.enumeration.slot)diag_usb(xhc,xhc.devices[xhc.enumeration.slot],xhc.enumeration.diagnostic_status,xhc.enumeration.diagnostic_status==DIAG_DETECTED_UNSUPPORTED?"Urządzenie USB wykryte; brak obsługiwanych interfejsów HID":"Błąd enumeracji USB; sprawdź log uruchamiania");
    if(xhc.enumeration.slot){auto& failed=xhc.devices[xhc.enumeration.slot];
        for(size_t i=0;i<failed.hid_count;++i)diag_hid(xhc,failed,hid_at(failed,i),xhc.enumeration.diagnostic_status,"Urządzenie USB wycofane po błędzie enumeracji");}
    auto& p=xhc.ports[xhc.enumeration.port];p.state=PortState::Failed;
    xhc.wanted_transfer=0;
    if(xhc.enumeration.slot && xhc.devices[xhc.enumeration.slot].enabled){
        auto& d=xhc.devices[xhc.enumeration.slot];d.hid_gotowy=false;d.hid_result={};
        d.retiring=true;release_mouse_buttons(d);xhc.enumeration.step=EnumStep::Retire;
    } else enumeration_idle(xhc,true);
}
bool enum_control(Controller& xhc,UsbSetupPacket setup,EnumStep next) {
    auto& d=xhc.devices[xhc.enumeration.slot];
    xhc.enumeration.length=setup.wLength;xhc.enumeration.actual=0;xhc.enumeration.code=0;
    xhc.enumeration.ok=false;xhc.enumeration.next=next;
    if(!submit_control(xhc,d,setup,setup.wLength?&xhc.enumeration.data:nullptr,setup.wLength)){enumeration_fail(xhc);return false;}
    xhc.enumeration.due=deadline();xhc.enumeration.step=EnumStep::ControlWait;return true;
}
bool enum_buffer(Controller& xhc,size_t bytes) {
    dma_release(&xhc.enumeration.data);
    if(!bytes||bytes>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI||!allocate_dma(xhc,bytes,64,&xhc.enumeration.data)){
        enumeration_fail(xhc);return false;}
    return true;
}
void inspect_one_port(Controller& xhc) {
    if(!xhc.max_ports)return;
    if(xhc.discovery_port>xhc.max_ports)xhc.discovery_port=1;
    uint8_t port=uint8_t(xhc.discovery_port++);
    if(xhc.discovery_port>xhc.max_ports)xhc.discovery_pending=false;
    for(uint32_t candidate=1;candidate<=xhc.max_ports;++candidate)if(xhc.ports[candidate].scan_pending){port=uint8_t(candidate);break;}
    auto& p=xhc.ports[port];p.scan_pending=false;
    BootTiming::Scope timing(BootTiming::PortInspection,port);
    const uint32_t status=read32(xhc.operational,PORTSC_BASE+(port-1U)*16U);
    const bool connected=status&PORT_CCS,csc=status&(1U<<17);
    if(csc||connected!=p.connection_seen){++p.generation;
        log_values("[USB root port] controller=",xhc.index," port=",port);
        log_values(connected?"[USB root port] connected PORTSC=":"[USB root port] disconnected PORTSC=",status,nullptr,0,true);
    }
    // Leave PRC/WRC to the outstanding reset operation. Its own change event
    // must never create another connection or erase completion evidence.
    const bool resetting=xhc.enumeration.port==port&&xhc.enumeration.step==EnumStep::ResetWait;
    port_clear_changes(xhc,port,status&PORT_RW1C&~(resetting?xhc.enumeration.reset_change:0U));
    if((!connected||csc)&&p.slot){
        auto& d=xhc.devices[p.slot];d.retiring=true;d.hid_gotowy=false;d.hid_result={};
        release_mouse_buttons(d);p.state=PortState::Retiring;p.reconnect=connected;
    }
    if(!connected){p.connection_seen=false;if(!p.slot)p.state=PortState::Empty;}
    else if(!p.slot&&(!p.connection_seen||csc)&&p.protocol){
        p.connection_seen=true;p.state=PortState::Queued;p.reconnect=false;
    }
}
void enumeration_step(Controller& xhc) {
    auto& e=xhc.enumeration;
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
        if(xhc.wanted_command){const int r=command_ready(xhc,e.slot);if(!r)return;
            if(!e.slot && r>0){e.slot=xhc.command_result.slot;
                if(e.slot&&e.slot<=xhc.enabled_slots&&!xhc.devices[e.slot].enabled){auto& slot=xhc.devices[e.slot];slot.enabled=true;slot.slot_id=e.slot;slot.root_port=e.port;p.slot=e.slot;}
                else e.slot=0;}}
        const bool replacement=(status&PORT_CCS)&&e.generation!=p.generation;
        enumeration_fail(xhc);
        if(replacement){if(p.slot)p.reconnect=true;else p.state=PortState::Queued;}
        return;
    }
    const auto data=[&](){return static_cast<const uint8_t*>(e.data.virtual_address);};
    auto& hi=hid_at(d,e.interface_index);
    switch(e.step){
    case EnumStep::Reset: {
        if(!(status&PORT_CCS)){enumeration_fail(xhc);return;}
        auto* proto=p.protocol;if(!proto||(!proto->usb2&&!proto->usb3)){enumeration_fail(xhc);return;}
        if(proto->usb3&&(status&PORT_PED)&&!(status&(PORT_PR|PORT_WPR))&&((status>>PORT_PLS_SHIFT)&PORT_PLS_MASK)==0){
            e.step=EnumStep::Enable;return;}
        const unsigned link=(status>>PORT_PLS_SHIFT)&PORT_PLS_MASK;
        const bool warm=proto->usb3&&(link==6||link==10||link==8);
        e.reset_change=warm?(1U<<19):PORT_PRC;
        port_clear_changes(xhc,e.port,status&PORT_RW1C);e.due=deadline();e.step=EnumStep::ResetWait;
        BootTiming::mark("port reset submit",e.port);port_set(xhc,e.port,warm?PORT_WPR:PORT_PR);return;
    }
    case EnumStep::ResetWait:
        if(!(status&(PORT_PR|PORT_WPR))&&(status&e.reset_change)){
            port_clear_changes(xhc,e.port,e.reset_change);BootTiming::mark("port reset complete",e.port);
            if(!(status&PORT_PED)||xhci_pobierz_predkosc_portu(xhc,e.port).logical==USB_SPEED_UNKNOWN){enumeration_fail(xhc);return;}
            e.step=EnumStep::Enable;
        } else if(czas_monotoniczny_ns()>=e.due){enumeration_fail(xhc);}
        return;
    case EnumStep::Enable: {
        XhciTrb t{};t.dword3=xhci_trb::type_field(9)|(uint32_t(p.protocol->slot_type)<<16);
        if(!submit_command(xhc,t,EnumStep::EnableWait)){enumeration_fail(xhc);}
        return;
    }
    case EnumStep::EnableWait: {
        const int r=command_ready(xhc);if(!r)return;
        const auto slot=xhc.command_result.slot;
        if(r<0||!slot||slot>xhc.enabled_slots||xhc.devices[slot].enabled){enumeration_fail(xhc);return;}
        e.slot=slot;p.slot=slot;
        if(!prepare_device(xhc,xhc.devices[slot],slot,e.port,xhci_pobierz_predkosc_portu(xhc,e.port))){enumeration_fail(xhc);return;}
        e.step=EnumStep::Address;return;
    }
    case EnumStep::Address:
        if(!submit_command(xhc,context_command(11,d),EnumStep::AddressWait)){enumeration_fail(xhc);}
        return;
    case EnumStep::AddressWait: {
        const int r=command_ready(xhc,e.slot);if(!r)return;if(r<0){enumeration_fail(xhc);return;}
        __atomic_thread_fence(__ATOMIC_ACQUIRE);d.addressed=true;d.device_address=uint8_t(context(xhc,d.output_context,0)[3]);
        e.step=EnumStep::FirstDescriptor;return;
    }
    case EnumStep::FirstDescriptor:
        if(enum_buffer(xhc,18)){enum_control(xhc,{0x80,6,0x100,0,8},EnumStep::FirstDescriptorDone);}
        return;
    case EnumStep::FirstDescriptorDone:
        if(!e.ok||e.actual<8||data()[0]!=18||data()[1]!=1||!valid_mps(d,data()[7],&e.mps)){enumeration_fail(xhc);return;}
        e.step=e.mps==d.ep0_mps?EnumStep::DeviceDescriptor:EnumStep::Evaluate;return;
    case EnumStep::Evaluate: {
        zero_dma(d.input_context);context(xhc,d.input_context,0)[1]=1U<<1;
        auto* ep=context(xhc,d.input_context,2);const auto* old=context(xhc,d.output_context,1);
        for(unsigned i=0;i<xhc.context_size/4;++i)ep[i]=old[i];
        ep[1]=(ep[1]&0xffffU)|(uint32_t(e.mps)<<16);
        if(!submit_command(xhc,context_command(13,d),EnumStep::EvaluateWait)){enumeration_fail(xhc);}
        return;
    }
    case EnumStep::EvaluateWait: {
        const int r=command_ready(xhc,e.slot);if(!r)return;if(r<0){enumeration_fail(xhc);return;}
        d.ep0_mps=e.mps;e.step=EnumStep::DeviceDescriptor;return;
    }
    case EnumStep::DeviceDescriptor:
        zero_dma(e.data);enum_control(xhc,{0x80,6,0x100,0,18},EnumStep::DeviceDescriptorDone);return;
    case EnumStep::DeviceDescriptorDone:
        if(!e.ok||e.actual<18||data()[0]!=18||data()[1]!=1){enumeration_fail(xhc);return;}
        d.device_class=data()[4];d.vendor_id=le16(data()+8);d.product_id=le16(data()+10);e.step=EnumStep::ConfigHeader;return;
    case EnumStep::ConfigHeader:
        if(enum_buffer(xhc,9)){enum_control(xhc,{0x80,6,0x200,0,9},EnumStep::ConfigHeaderDone);}
        return;
    case EnumStep::ConfigHeaderDone:
        if(!e.ok||e.actual<9||data()[0]<9||data()[1]!=2||le16(data()+2)<9||le16(data()+2)>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI){enumeration_fail(xhc);return;}
        e.length=le16(data()+2);e.step=EnumStep::ConfigBody;return;
    case EnumStep::ConfigBody: {
        const auto length=e.length;if(enum_buffer(xhc,length))enum_control(xhc,{0x80,6,0x200,0,length},EnumStep::ConfigBodyDone);return;
    }
    case EnumStep::ConfigBodyDone:
        if(!e.ok||e.actual<9||!usb_hid_parse_configuration(data(),e.actual,e.configuration)){enumeration_fail(xhc);return;}
        log_usb_configuration(xhc,d,e.configuration,data(),e.actual);
        if(!e.configuration.count){wypisz_log("[USB] no usable HID interrupt IN interfaces");e.diagnostic_status=DIAG_DETECTED_UNSUPPORTED;enumeration_fail(xhc);return;}
        if(e.configuration.count>1){
            if(!allocate_dma(xhc,(e.configuration.count-1)*sizeof(XhciHid),64,&d.interface_storage)){enumeration_fail(xhc);return;}
            d.extra_hid=new(d.interface_storage.virtual_address) XhciHid[uint8_t(e.configuration.count-1)]{};
        }
        d.hid_count=uint8_t(e.configuration.count);e.interface_index=0;
        e.step=EnumStep::Configure;return;
    case EnumStep::Configure: {
        hi.descriptor=e.configuration.interfaces[e.interface_index];
        hid_configuration(hi.descriptor,&e.hid);
        diag_hid(xhc,d,hi,DIAG_INACTIVE,"Trwa konfiguracja interfejsu HID");
        XhciTrb t{};
        if(!prepare_hid_endpoint(xhc,d,e.hid,t,&hi)){hi.configuration_failed=true;log_hid(xhc,d,hi,"endpoint rejected",DIAG_INIT_FAILED);e.step=EnumStep::ConfigureWait;e.ok=false;return;}
        e.ok=true;
        if(!submit_command(xhc,t,EnumStep::ConfigureWait)){enumeration_fail(xhc);}
        return;
    }
    case EnumStep::ConfigureWait: {
        if(e.ok){const int r=command_ready(xhc,e.slot);if(!r)return;
            if(r==-2){enumeration_fail(xhc);return;}
            if(r<0){hi.configuration_failed=true;log_hid(xhc,d,hi,"Configure Endpoint failed",DIAG_INIT_FAILED);hi.hid_ring.outstanding=0;hi.type=USB_DEVICE_UNKNOWN;}
            else log_hid(xhc,d,hi,"endpoint configured");}
        if(++e.interface_index<d.hid_count){e.step=EnumStep::Configure;return;}
        e.interface_index=0;hid_configuration(e.configuration.interfaces[0],&e.hid);
        e.step=EnumStep::SetConfig;return;
    }
    case EnumStep::SetConfig:
        enum_control(xhc,{0,9,e.hid.konfiguracja,0,0},EnumStep::SetConfigDone);return;
    case EnumStep::SetConfigDone:
        if(!e.ok){enumeration_fail(xhc);return;}d.konfiguracja=e.hid.konfiguracja;d.active_configuration=e.hid.konfiguracja;e.step=EnumStep::Protocol;return;
    case EnumStep::Protocol:
        hi.descriptor=e.configuration.interfaces[e.interface_index];
        hid_configuration(hi.descriptor,&e.hid);
        if(hi.type==USB_DEVICE_UNKNOWN||!hi.hid_ring.trbs){e.step=EnumStep::Activate;return;}
        if(hi.type==USB_HID_BOOT_KEYBOARD||(e.hid.subclass==1&&e.hid.protocol==2))
            enum_control(xhc,{0x21,0x0b,0,e.hid.interfejs,0},EnumStep::ProtocolDone);
        else {e.step=EnumStep::ReportDescriptor;}
        return;
    case EnumStep::ProtocolDone:
        if(hi.type==USB_HID_BOOT_KEYBOARD){if(!e.ok){e.step=EnumStep::Activate;return;}e.step=EnumStep::IdleRequest;return;}
        hi.boot_mouse=e.ok;if(hi.boot_mouse)hi.hid_gotowy=hid_boot_mouse_layout(hi.pointer_layout);
        e.step=EnumStep::ReportDescriptor;return;
    case EnumStep::ReportDescriptor:
        if(!e.hid.raport_dlugosc||e.hid.raport_dlugosc>USB_MAKSYMALNY_ROZMIAR_KONFIGURACJI){e.step=EnumStep::Activate;return;}
        dma_release(&e.data);
        if(!allocate_dma(xhc,e.hid.raport_dlugosc,64,&e.data)){
            e.step=EnumStep::Activate;return;
        }
        enum_control(xhc,{0x81,6,0x2200,e.hid.interfejs,e.hid.raport_dlugosc},EnumStep::ReportDescriptorDone);return;
    case EnumStep::ReportDescriptorDone: {
        if(e.ok)hi.report_roles=hid_report_roles(data(),e.actual);
        HidPointerLayout compiled{};
        const bool valid=e.ok&&e.actual==e.hid.raport_dlugosc&&hid_pointer_compile(data(),e.actual,compiled);
        if(hi.boot_mouse){hi.boot_wheel=valid&&hid_boot_mouse_has_wheel(compiled);e.step=EnumStep::Activate;return;}
        if(!valid){log_hid(xhc,d,hi,"unsupported report descriptor");e.step=EnumStep::Activate;return;}hi.pointer_layout=compiled;
        for(unsigned i=0;i<compiled.id_count;++i)if((compiled.bits[i]+7U)/8+(compiled.numbered?1U:0U)>hi.hid_dma_length){e.step=EnumStep::Activate;return;}
        hi.hid_gotowy=true;e.step=e.hid.subclass==1&&e.hid.protocol!=2?EnumStep::ReportProtocol:EnumStep::Activate;return;
    }
    case EnumStep::ReportProtocol:
        enum_control(xhc,{0x21,0x0b,1,e.hid.interfejs,0},EnumStep::ReportProtocolDone);return;
    case EnumStep::ReportProtocolDone:
        if(!e.ok)hi.hid_gotowy=false;
        e.step=EnumStep::Activate;return;
    case EnumStep::IdleRequest:
        enum_control(xhc,{0x21,0x0a,0,e.hid.interfejs,0},EnumStep::IdleDone);return;
    case EnumStep::IdleDone:
        hi.hid_gotowy=e.ok||e.code==6;e.step=EnumStep::Activate;return;
    case EnumStep::Activate: {
        bool usable=hi.hid_gotowy&&!d.control_failed;
        if(usable&&hi.type==USB_HID_BOOT_KEYBOARD){
            hi.keyboard_device.usb_controller=INPUT_USB_CONTROLLER_XHCI;
            hi.keyboard_device.device_id=(uint32_t(xhc.index)<<24)|(uint32_t(hi.hid_interfejs)<<16)|d.slot_id;
            usable=usb_hid_keyboard_register(hi.keyboard_device);
        }
        if(usable&&hi.type==USB_HID_POINTER){hi.pointer_device.usb_controller=INPUT_USB_CONTROLLER_XHCI;
            usable=hid_pointer_register(hi.pointer_device,hi.pointer_layout,(uint32_t(xhc.index)<<24)|(uint32_t(hi.hid_interfejs)<<16)|d.slot_id);}
        if(usable)usable=xhci_zakolejkuj_transfer_interrupt_in(xhc,d,&hi);
        if(!usable){hi.hid_gotowy=false;remove_hid(hi);log_hid(xhc,d,hi,"interface unavailable; continuing",hi.configuration_failed||hi.type==USB_HID_BOOT_KEYBOARD||hi.type==USB_HID_POINTER?DIAG_INIT_FAILED:DIAG_UNSUPPORTED);}
        else {log_hid(xhc,d,hi,"interrupt IN armed",DIAG_OK);BootTiming::mark("first interrupt IN submit",e.slot);}
        if(++e.interface_index<d.hid_count){e.step=EnumStep::Protocol;return;}
        bool active=false;for(size_t i=0;i<d.hid_count;++i)if(hid_at(d,i).hid_gotowy)active=true;
        diag_usb(xhc,d,active?DIAG_OK:DIAG_UNSUPPORTED,active?"Interfejsy HID uruchomione":"Brak obsługi interfejsów tego urządzenia");
        BootTiming::mark("xHCI device armed",e.slot);p.state=PortState::Active;enumeration_idle(xhc,true);return;
    }
    case EnumStep::ControlWait: {
        if(!xhc.transfer_result.valid){if(czas_monotoniczny_ns()>=e.due){d.control_failed=true;BootTiming::mark("control timeout",e.slot);enumeration_fail(xhc);}return;}
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
        if(!submit_command(xhc,t,EnumStep::RecoverResetWait)){enumeration_fail(xhc);}
        return;
    }
    case EnumStep::RecoverResetWait: {
        const int r=command_ready(xhc,e.slot);if(!r)return;if(r<0){d.control_failed=true;enumeration_fail(xhc);return;}
        e.step=EnumStep::RecoverDequeue;return;
    }
    case EnumStep::RecoverDequeue: {
        const uint64_t next=d.ep0.dma.physical_address+uint64_t(d.ep0.producer_index)*16;
        XhciTrb t{uint32_t(next)|(d.ep0.producer_cycle?1U:0U),uint32_t(next>>32),0,xhci_trb::type_field(16)|(1U<<16)|(uint32_t(e.slot)<<24)};
        if(!submit_command(xhc,t,EnumStep::RecoverDequeueWait)){enumeration_fail(xhc);}
        return;
    }
    case EnumStep::RecoverDequeueWait: {
        const int r=command_ready(xhc,e.slot);if(!r)return;if(r<0){d.control_failed=true;enumeration_fail(xhc);return;}
        e.step=e.next;return;
    }
    case EnumStep::Retire: {
        if(!e.slot||!d.enabled){enumeration_idle(xhc,true);return;}
        XhciTrb t{};t.dword3=xhci_trb::type_field(10)|(uint32_t(e.slot)<<24);
        if(!submit_command(xhc,t,EnumStep::RetireWait)){p.state=PortState::Failed;enumeration_idle(xhc,false);}return;
    }
    case EnumStep::RetireWait: {
        const int r=command_ready(xhc,e.slot);if(!r)return;
        if(r>0){d.enabled=false;free_device(xhc,d);p.slot=0;
            p.state=p.reconnect?PortState::Queued:p.connection_seen?PortState::Failed:PortState::Empty;p.reconnect=false;}
        else p.state=PortState::Failed; // retain slot and all DMA on failed Disable
        enumeration_idle(xhc,r>0);return;
    }
    case EnumStep::Idle:return;
    }
}
