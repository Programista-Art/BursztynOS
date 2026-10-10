extern "C" void virtio_input_host_early_irq(VirtioInputDriver*);

// Initialization coverage uses separate PCI/BAR/config/MMIO state throughout.
static void init_fixture_clear() {
    init_checkpoint_action=nullptr; init_driver_ok_action=nullptr;
    virtio_input_reset_all(); // Fresh startup: the common late barrier is closed.
    init_activation_barriers=0; init_functions.clear(); input_reset_all();
}
static void init_emit_report(InitFunction& f) {
    auto* q=f.drv.eventq; assert(q && q->avail->idx>=3);
    const VirtioInputEvent events[]={{INPUT_EV_ABS,INPUT_ABS_X,100},
        {INPUT_EV_ABS,INPUT_ABS_Y,200},{INPUT_EV_SYN,INPUT_SYN_REPORT,0}};
    for(unsigned i=0;i<3;++i) {
        const auto id=q->avail->ring[i]; assert(id<q->queue_size);
        auto* dst=static_cast<VirtioInputEvent*>(host_phys_to_virt(q->desc[id].addr));
        assert(dst); *dst=events[i]; q->used->ring[i].id=id; q->used->ring[i].len=8;
    }
    q->used->idx=3;
}
void test_input_init_independent_functions_early_irq() {
    init_fixture_clear();
    InitFunction kbd(6,false), tablet(7,true);
    init_functions={&kbd,&tablet};
    TEST_ASSERT(host_input_prepare_and_activate(&kbd.drv,&kbd.dev),"Keyboard initializes");
    auto keyboard_bar=kbd.bar; uint8_t keyboard_pci[256]; std::memcpy(keyboard_pci,kbd.pci,256);
    unsigned injections=0;
    init_checkpoint_action=+[](InitFunction& f,const char* label) {
        if(!f.tablet) return;
        virtio_input_host_early_irq(&f.drv);
        // Attempt normal polling at every checkpoint, including partially
        // allocated queues: it must stay inert until registration is complete.
        if(f.drv.init_state!=VirtioInputInitState::REGISTERED)
            assert(virtio_input_poll_events(&f.drv,64)==0);
        if(std::strcmp(label,"buffers post done/avail")==0) init_emit_report(f);
        // IRQ context becomes reachable as soon as IDT registration completes.
        for(int v=48;v<256;++v) if(g_mock_idt[v].context==&f.drv) mock_trigger_irq(v);
    };
    TEST_ASSERT(host_input_prepare_and_activate(&tablet.drv,&tablet.dev),"Tablet initializes through early IRQs");
    injections=tablet.drv.irq_count;
    TEST_ASSERT(injections>0,"Early IRQs actually injected");
    TEST_ASSERT(tablet.dev.input_init_stage<256 && kbd.dev.input_init_stage<256,"Complete checkpoints fit diagnostic cap");
    TEST_ASSERT(tablet.drv.total_events_processed==3,"Early report drained only after late readiness");
    TEST_ASSERT(tablet.drv.irq_vector!=kbd.drv.irq_vector,"Different vectors");
    TEST_ASSERT(tablet.dev.msix_table!=kbd.dev.msix_table,"Different entry zero table bases");
    TEST_ASSERT(tablet.drv.eventq->dma_physical!=kbd.drv.eventq->dma_physical,"Independent queue DMA");
    TEST_ASSERT(tablet.drv.dma_phys!=kbd.drv.dma_phys,"Independent event DMA");
    TEST_ASSERT(kbd.bar==keyboard_bar && std::memcmp(kbd.pci,keyboard_pci,256)==0,"Tablet never changes keyboard MMIO/PCI");
    TEST_ASSERT(tablet.statuses==kbd.statuses,"Identical status progression");
    TEST_ASSERT(tablet.statuses==std::vector<uint8_t>({0,1,3,11,15}),"Reset ACK DRIVER FEATURES_OK DRIVER_OK");
    init_checkpoint_action=nullptr;
    TEST_ASSERT(virtio_input_poll_events(&tablet.drv,64)==0,"Late activation consumed report exactly once");
    kbd.sentinels();tablet.sentinels();
    TEST_ASSERT(virtio_input_reset(&tablet.drv),"Tablet resets");
    TEST_ASSERT(kbd.bar==keyboard_bar,"Tablet reset isolated");
    TEST_ASSERT(virtio_input_reset(&kbd.drv),"Keyboard resets");
    init_fixture_clear();
}
void test_input_init_queue_capacity_matrix() {
    init_fixture_clear();
    for(uint16_t size: {0,1,2,8,32,64,128}) {
        InitFunction f(7,true,size);init_functions={&f};
        const auto dma_before=g_host_dma_allocs.size();
        const auto vectors_before=g_mock_idt_allocated_count;
        bool ok=host_input_prepare_and_activate(&f.drv,&f.dev);
        TEST_ASSERT(ok==(size!=0),"Zero queue fails; supported sizes initialize");
        if(ok) {
            const auto count=std::min<unsigned>(size,64);
            TEST_ASSERT(f.drv.num_event_bufs==count && f.drv.eventq->avail->idx==count,"Posting bounded by queue capacity");
            TEST_ASSERT(f.drv.eventq->queue_index==0 && !f.drv.statusq,"Eventq only; no status feedback");
            for(unsigned i=0;i<count;++i) {
                const auto id=f.drv.buffer_head[i];
                TEST_ASSERT(f.drv.eventq->desc[id].len==8 && f.drv.eventq->desc[id].flags==VRING_DESC_F_WRITE,"Writable eight-byte event");
                TEST_ASSERT(host_phys_to_virt(f.drv.eventq->desc[id].addr)==f.drv.dma_events+i,"DMA address resolves to owned buffer");
            }
            TEST_ASSERT(virtio_input_reset(&f.drv),"Reset");
        } else TEST_ASSERT(f.drv.init_state==VirtioInputInitState::FAILED,"Failed state");
        f.sentinels();
        TEST_ASSERT(g_host_dma_allocs.size()==dma_before && g_mock_idt_allocated_count==vectors_before,"No failure leaks");
    }
    init_fixture_clear();
}
void test_input_init_capability_lengths_and_wide_access() {
    init_fixture_clear();
    for(uint32_t len:{0,1,2,7,8,27,28,42,43,136,256}) {
        InitFunction f(7,true,64,len);init_functions={&f};
        VirtioInputAbsInfo out{};uint8_t sz=99;
        bool ok=virtio_input_query_config(&f.dev,VIRTIO_INPUT_CFG_ABS_INFO,INPUT_ABS_X,&out,&sz,sizeof(out));
        TEST_ASSERT(ok==(len>=28),"ABSINFO respects exact payload capability length");
        if(ok) TEST_ASSERT(sz==20 && out.max==32767,"ABSINFO copied correctly");
        const auto before=f.accesses;
        if(len<2) {virtio_pci_read_device_config16(&f.dev,0);virtio_pci_write_device_config16(&f.dev,0,123);}
        if(len<4) {virtio_pci_read_device_config32(&f.dev,0);virtio_pci_write_device_config32(&f.dev,0,123);}
        if(len<8) virtio_pci_read_device_config64(&f.dev,0);
        TEST_ASSERT(f.accesses==before,"Short capability cannot underflow wide-access bounds");
        // Last-byte wide access must never reach the next MMIO capability.
        const auto before_end=f.accesses;
        virtio_pci_read_device_config64(&f.dev,len?len-1:0);
        virtio_pci_write_device_config32(&f.dev,len?len-1:0,123);
        TEST_ASSERT(f.accesses==before_end,"End-of-capability access rejected");
        f.sentinels();
    }
    init_fixture_clear();
}
void test_input_init_absinfo_failure_matrix() {
    init_fixture_clear();
    for(unsigned variant=0;variant<9;++variant) {
        InitFunction f(7,true);init_functions={&f};
        if(variant==0) f.abs_size=0;
        if(variant==1) f.abs_size=19;
        if(variant==2) f.abs_max=f.abs_min;
        if(variant==3) f.abs_max=-1;
        if(variant==4) {f.abs_min=INT32_MIN;f.abs_max=INT32_MAX;}
        if(variant==5) {f.abs_min=-1000;f.abs_max=1000;}
        if(variant==7) f.abs_y_size=0;
        if(variant==8) f.abs_y_size=19;
        bool ok=host_input_prepare_and_activate(&f.drv,&f.dev);
        TEST_ASSERT(ok==(variant>=4 && variant<=6),"Missing/truncated/invalid range fails safely");
        if(ok) TEST_ASSERT(virtio_input_reset(&f.drv),"Reset");
        else {
            TEST_ASSERT(f.drv.init_state==VirtioInputInitState::FAILED && !f.drv.input_dev.registered,"Invalid tablet never registers");
            TEST_ASSERT(!f.drv.dma_virt && !f.drv.eventq,"Failure cleanup");
        }
        f.sentinels();
    }
    init_fixture_clear();
}
void test_input_init_feature_vector_reset_failures() {
    init_fixture_clear();
    for(unsigned variant=0;variant<5;++variant) {
        InitFunction kbd(6,false),f(7,true);init_functions={&kbd,&f};
        TEST_ASSERT(host_input_prepare_and_activate(&kbd.drv,&kbd.dev),"Keyboard");
        auto keyboard=kbd.bar;
        if(variant==0) f.accept_features=false;
        if(variant==1) f.accept_vector=false;
        if(variant==2) {f.reset_completes=false;f.dev.common_cfg->device_status=3;}
        if(variant==3) f.dev.common_cfg->num_queues=0;
        if(variant==4) f.allow_driver_ok=false;
        bool ok=host_input_prepare_and_activate(&f.drv,&f.dev);
        TEST_ASSERT(ok==(variant==1),"Failed reset/features/queues abort; rejected MSI-X uses bounded polling");
        if(ok) {
            TEST_ASSERT(!f.drv.use_msix && f.drv.irq_vector==-1,"NOVECTOR never reported as MSI-X success");
            uint16_t command=0;std::memcpy(&command,f.pci+PCI_OFFSET_COMMAND_STATUS,2);
            TEST_ASSERT((command&PCI_COMMAND_INT_DISABLE)!=0,"NOVECTOR fallback suppresses unhandled legacy IRQ");
            TEST_ASSERT(virtio_input_reset(&f.drv),"Reset");
        } else TEST_ASSERT(f.drv.init_state==VirtioInputInitState::FAILED,"Failed state");
        TEST_ASSERT(kbd.bar==keyboard && kbd.drv.initialized,"Keyboard survives tablet failure");
        f.reset_completes=true;TEST_ASSERT(virtio_input_reset(&f.drv),"Quarantined state can be reset");
        TEST_ASSERT(virtio_input_reset(&kbd.drv),"Reset keyboard");
        kbd.sentinels(); f.sentinels();
    }
    init_fixture_clear();
}

void test_input_init_irqs_at_each_readiness_boundary() {
    init_fixture_clear();
    const char* phases[]={"queue select done", "queue enable store done",
        "IDT register done", "buffers post done/avail", "PCI MSI-X enable done",
        "status store done", "InputDevice register begin", "InputDevice register done"};
    static const char* phase;
    static unsigned hits;
    static bool report;
    for(const char* selected:phases) {
        InitFunction f(7,true); init_functions={&f}; phase=selected; hits=0;report=false;
        init_checkpoint_action=+[](InitFunction& f,const char* label) {
            if(std::strcmp(label,phase)) return;
            ++hits;
            if(f.drv.eventq && f.drv.eventq->avail->idx>=3 && !report) {
                init_emit_report(f);report=true;
            }
            virtio_input_host_early_irq(&f.drv);
            if(!f.drv.initialized) assert(virtio_input_poll_events(&f.drv,64)==0);
        };
        TEST_ASSERT(host_input_prepare_and_activate(&f.drv,&f.dev),"Initialization completes across boundary IRQ");
        TEST_ASSERT(hits>0,"Selected boundary reached");
        TEST_ASSERT(f.drv.total_events_processed==(report?3U:0U),"Boundary report dispatched only after software readiness");
        init_checkpoint_action=nullptr;
        if(report) TEST_ASSERT(virtio_input_poll_events(&f.drv,64)==0,"Late activation already delivered pending report");
        TEST_ASSERT(virtio_input_reset(&f.drv),"Reset");f.sentinels();
    }
    init_fixture_clear();
}
void test_input_init_registry_failure_cleanup() {
    init_fixture_clear();
    InputDevice devices[64]{};unsigned registered=0;
    for(auto& dev:devices) { if(input_register_device(&dev)) ++registered; else break; }
    TEST_ASSERT(registered>0,"Registry filled");
    InitFunction f(7,true);init_functions={&f};
    const auto allocations=g_host_dma_allocs.size();const auto vectors=g_mock_idt_allocated_count;
    TEST_ASSERT(!host_input_prepare_and_activate(&f.drv,&f.dev),"Full registry rejects optional tablet");
    TEST_ASSERT(f.drv.init_state==VirtioInputInitState::FAILED,"Failure state retained");
    TEST_ASSERT(!f.drv.eventq && !f.drv.dma_virt && !f.drv.input_dev.registered,"Reset before DMA cleanup");
    TEST_ASSERT(g_host_dma_allocs.size()==allocations && g_mock_idt_allocated_count==vectors,"DMA and IRQ resources recovered");
    TEST_ASSERT((f.dev.msix_table[0].vector_ctrl & PCI_MSIX_VECTOR_CTRL_MASK)!=0,"Failure masks table entry");
    f.sentinels();init_fixture_clear();
}

void test_input_init_reset_failure_quarantines_live_dma() {
    init_fixture_clear();
    InitFunction f(7,true);init_functions={&f};
    init_checkpoint_action=+[](InitFunction& f,const char* label) {
        if(!std::strcmp(label,"buffers post done/avail")) {
            f.reset_completes=false;f.allow_driver_ok=false;
        }
    };
    const auto allocations=g_host_dma_allocs.size();const auto vectors=g_mock_idt_allocated_count;
    TEST_ASSERT(!host_input_prepare_and_activate(&f.drv,&f.dev),"Rejected DRIVER_OK and reset timeout terminate");
    init_checkpoint_action=nullptr;
    TEST_ASSERT(f.drv.init_state==VirtioInputInitState::FAILED && !f.drv.initialized,"No failed device service");
    TEST_ASSERT(f.drv.eventq && f.drv.dma_virt && g_host_dma_allocs.size()>allocations,"Live DMA quarantined until reset acknowledgement");
    TEST_ASSERT(g_mock_idt_allocated_count==vectors,"IRQ context detached");
    TEST_ASSERT((f.dev.msix_table[0].vector_ctrl&1)!=0,"Table masked");
    TEST_ASSERT(virtio_input_poll_events(&f.drv,64)==0,"Failed device cannot dispatch");
    f.reset_completes=true;
    TEST_ASSERT(virtio_input_reset(&f.drv),"Confirmed reset releases quarantine");
    TEST_ASSERT(g_host_dma_allocs.size()==allocations,"DMA released after confirmed reset");
    f.sentinels();init_fixture_clear();
}
