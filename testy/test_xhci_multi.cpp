#define main physical_suite_main
#include "testy/test_xhci_physical_mouse.cpp"
#undef main
namespace BootTiming { uint64_t host_ticks(){return mock_now+1;} }
static void second_controller(){
    second_present=true;std::memcpy(second_cfg,cfg,sizeof(cfg));std::memcpy(second_registers,registers,sizeof(registers));
    std::memcpy(second_bar_mask,bar_mask,sizeof(bar_mask));std::memset(second_probing,0,sizeof(second_probing));
    const uint32_t id=0x16391022,bar=0xf2000000;std::memcpy(second_cfg,&id,4);std::memcpy(second_cfg+0x10,&bar,4);
    second_cfg[0x50]=5;second_cfg[0x51]=0;second_cfg[0x52]=0;second_cfg[0x53]=0;
}
static void service_many(unsigned count=500){for(unsigned i=0;i<count;++i)usb_obsluz();}
static void connect_controller(Controller& c,unsigned port){
    put(c.operational,PORTSC_BASE+(port-1)*16,PORT_CCS|(1U<<10)|(1U<<17));
    model_controller=&c;emit(uint64_t(port)<<24,0,xhci_trb::type_field(34));model_controller=&controllers[0];
}
static void independent_controllers(){
    fixture();second_controller();CHECK(xhci_inicjalizuj_wszystkie());CHECK(controller_count==2);
    auto& a=controllers[0];auto& b=controllers[1];CHECK(a.ready&&b.ready);
#ifdef BURSZTYN_DIAG_STORE_TEST
    DiagSummary summary{};diag_get_summary(&summary);CHECK(summary.devices==2);
    DiagDevice diagnostic{};CHECK(diag_get_device(0,&diagnostic));CHECK(diagnostic.category==DIAG_XHCI&&diagnostic.status==DIAG_OK);
    CHECK(diag_get_device(1,&diagnostic));CHECK(diagnostic.vendor==0x1022&&diagnostic.device==0x1639);
#endif
CHECK(a.mmio!=b.mmio);
    CHECK(a.command.dma.physical_address!=b.command.dma.physical_address);
    CHECK(a.event_segment.physical_address!=b.event_segment.physical_address);CHECK(a.irq_vector!=b.irq_vector);
    xhci_irq(&b);CHECK(b.irq_pending==1&&a.irq_pending==0);
    BootTiming::first_visible_splash=true;connect_controller(a,1);connect_controller(b,1);service_many();
    CHECK(a.ports[1].state==PortState::Active&&b.ports[1].state==PortState::Active);
    auto& da=a.devices[a.ports[1].slot];auto& db=b.devices[b.ports[1].slot];
    CHECK(da.slot_id==db.slot_id);CHECK(da.hid_transfer_oczekiwany&&db.hid_transfer_oczekiwany);
    // Inject an alien ring pointer into controller B: it must never complete A.
    model_controller=&b;emit(da.hid_transfer_pointer,1U<<24,xhci_trb::type_field(32)|(uint32_t(db.slot_id)<<24)|(uint32_t(db.hid_dci)<<16));
    model_controller=&a;service_many(1);CHECK(!db.pierwszy_raport_zalogowany&&!da.pierwszy_raport_zalogowany);
    put(a.operational,USBSTS,STS_HCE);usb_obsluz();CHECK(!a.ready&&b.ready);service_many();CHECK(db.hid_transfer_oczekiwany);clean();
    // Failure of the first detected controller cannot block a later AMD one.
    fixture();second_controller();put(registers,4,0);CHECK(xhci_inicjalizuj_wszystkie());
    CHECK(controller_count==2&&!controllers[0].ready&&controllers[1].ready);clean();
}
static void composite_device(){
    fixture();
    composite_configuration={9,2,0,0,3,1,0,0x80,50,
        9,4,4,0,1,3,0,0,0,9,0x21,0x11,1,0,1,0x22,1,0,7,5,0x84,3,16,0,10,
        9,4,2,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,63,0,7,5,0x82,3,8,0,10,
        9,4,7,0,1,3,1,2,0,9,0x21,0x11,1,0,1,0x22,52,0,7,5,0x85,3,8,0,10};
    composite_configuration[2]=uint8_t(composite_configuration.size());
    CHECK(xhci_inicjalizuj_wszystkie());BootTiming::first_visible_splash=true;connect_controller(xhc,1);service_many();
    auto& d=xhc.devices[xhc.ports[1].slot];CHECK(d.hid_count==3);
    auto& unsupported=hid_at(d,0);auto& keyboard=hid_at(d,1);auto& mouse=hid_at(d,2);
    CHECK(!unsupported.hid_gotowy);CHECK(keyboard.hid_gotowy&&mouse.hid_gotowy);
    CHECK(keyboard.hid_interfejs==2&&mouse.hid_interfejs==7);
    CHECK(keyboard.hid_ring.dma.physical_address!=mouse.hid_ring.dma.physical_address);
    CHECK(keyboard.hid_raport.physical_address!=mouse.hid_raport.physical_address);
    CHECK(keyboard.hid_transfer_oczekiwany&&mouse.hid_transfer_oczekiwany);
#ifdef BURSZTYN_DIAG_STORE_TEST
    DiagSummary summary{};diag_get_summary(&summary);CHECK(summary.keyboard&&summary.pointer);
    unsigned hid=0,usable=0;DiagDevice diagnostic{};
    for(uint32_t i=0;i<summary.devices;++i)if(diag_get_device(i,&diagnostic)&&diagnostic.category==DIAG_HID){++hid;if(diagnostic.status==DIAG_OK)++usable;CHECK(diagnostic.parent==d.diag_id);CHECK(diagnostic.configuration==1);CHECK(diagnostic.interval==10&&diagnostic.packet_size&&diagnostic.endpoint>0x80);}
    CHECK(hid==3&&usable==2);CHECK(!summary.serious);
#endif

    const uint8_t k[8]={0,0,4,0,0,0,0,0};std::memcpy(keyboard.hid_raport.virtual_address,k,8);
    emit(keyboard.hid_transfer_pointer,1U<<24,xhci_trb::type_field(32)|(uint32_t(d.slot_id)<<24)|(uint32_t(keyboard.hid_dci)<<16));
    service_many(1);CHECK(keyboard.poprzedni_raport[2]==4);CHECK(mouse.hid_transfer_oczekiwany);
    const uint8_t m[3]={0,1,1};std::memcpy(mouse.hid_raport.virtual_address,m,3);
    emit(mouse.hid_transfer_pointer,(13U<<24)|5,xhci_trb::type_field(32)|(uint32_t(d.slot_id)<<24)|(uint32_t(mouse.hid_dci)<<16));
    service_many(1);CHECK(input_pointer_is_active(&mouse.pointer_device));CHECK(keyboard.poprzedni_raport[2]==4);
#ifdef BURSZTYN_DIAG_STORE_TEST
    // Routine completions must never downgrade an armed interface to inactive.
    diag_get_summary(&summary);usable=0;
    for(uint32_t i=0;i<summary.devices;++i)if(diag_get_device(i,&diagnostic)&&diagnostic.category==DIAG_HID&&diagnostic.status==DIAG_OK)++usable;
    CHECK(usable==2&&summary.keyboard&&summary.pointer);
#endif

    put(xhc.operational,PORTSC_BASE,1U<<17);service_many();CHECK(!d.enabled);clean();
}
static void interface_fairness(){
    fixture();composite_configuration={9,2,0,0,15,1,0,0x80,50};
    for(uint8_t i=0;i<15;++i){const uint8_t descriptors[]={9,4,i,0,1,3,1,2,0,
        9,0x21,0x11,1,0,1,0x22,52,0,7,5,uint8_t(0x81+i),3,8,0,10};
        composite_configuration.insert(composite_configuration.end(),descriptors,descriptors+sizeof(descriptors));}
    composite_configuration[2]=uint8_t(composite_configuration.size());composite_configuration[3]=uint8_t(composite_configuration.size()>>8);
    CHECK(xhci_inicjalizuj_wszystkie());BootTiming::first_visible_splash=true;connect_controller(xhc,1);service_many(2000);
    auto& d=xhc.devices[xhc.ports[1].slot];CHECK(d.hid_count==15);
    auto complete=[&](size_t i){auto& hi=hid_at(d,i);CHECK(hi.hid_transfer_oczekiwany);
        const uint8_t report[3]={0,1,1};std::memcpy(hi.hid_raport.virtual_address,report,3);
        emit(hi.hid_transfer_pointer,(13U<<24)|5,xhci_trb::type_field(32)|(uint32_t(d.slot_id)<<24)|(uint32_t(hi.hid_dci)<<16));};
    for(size_t i=0;i<15;++i)complete(i);
    service_many(1);CHECK(xhc.mouse_reports_processed==8);
    for(size_t i=0;i<8;++i)complete(i);
    service_many(1);CHECK(xhc.mouse_reports_processed==16);
    for(size_t i=8;i<15;++i)CHECK(hid_at(d,i).pierwszy_raport_zalogowany);
    clean();
    XhciProducerRing ring{};CHECK(create_ring(&ring,16));XhciTrb trb{0,0,0,xhci_trb::type_field(1)};uint64_t pointer=0;
    ring.producer_index=15;CHECK(!xhci_command_ring_enqueue(&ring,trb,&pointer));ring.producer_index=0;
    ring.trb_count=0;CHECK(!xhci_command_ring_enqueue(&ring,trb,&pointer));ring.trb_count=16;
    xhci_command_ring_destroy(&ring);clean();
}
#ifndef BURSZTYN_DIAG_STORE_TEST
int main(){independent_controllers();composite_device();interface_fairness();printf("Multiple xHCI/composite endpoint isolation: %u checks PASS\n",checks);}

#endif
