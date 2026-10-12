// Uses production PMM tag parsing, reservation and contiguous-frame allocation.
bool CzyRamkaWolna(uint64_t);
struct InitMultiboot {
    std::vector<uint64_t> words{0};
    void append(const void* bytes, size_t size) {
        const auto old=words.size(); words.resize(old+(size+7)/8,0);
        std::memcpy(words.data()+old,bytes,size);
    }
    void ram(uint64_t start,uint64_t size,uint32_t type=1) {
        struct __attribute__((packed)) {uint32_t type,size,entry_size,version; WpisMapyPamieciMB2 entry;}
            tag{6,40,24,0,{start,size,type,0}};
        append(&tag,sizeof(tag));
    }
    void module(uint32_t start,uint32_t end) {
        const uint32_t tag[]={3,24,start,end,0,0}; append(tag,sizeof(tag));
    }
    void framebuffer(uint64_t start) {
        TagFramebufferMB2 fb{};fb.typ=8;fb.rozmiar=sizeof(fb);
        fb.adres_fizyczny=start;fb.pitch=4096;fb.wysokosc=1;
        append(&fb,sizeof(fb));
    }
    void finish() {
        const uint32_t end[]={0,8};append(end,sizeof(end));
        words[0]=words.size()*8;
        InicjalizujPMM(reinterpret_cast<uintptr_t>(words.data()));
    }
};

static void activation_check_buffers(InitFunction& f) {
    auto* q=f.drv.eventq;
    assert(q && q->queue_size==64 && q->avail->idx==64);
    assert(f.drv.init_state==VirtioInputInitState::ACTIVATING);
    assert(!f.drv.initialized && !f.drv.input_dev.registered);
    assert(!f.drv.service_lock && !f.dev.input_config_lock);
    assert(f.dev.common_cfg->queue_select==0 && f.dev.common_cfg->queue_enable==1);
    assert(f.dev.common_cfg->queue_desc==q->dma_physical);
    assert(f.dev.common_cfg->queue_driver==q->dma_physical+0x400);
    assert(f.dev.common_cfg->queue_device==q->dma_physical+0x4C0);
    assert(q->dma_size==0x6C6 && q->dma_pages==1);
    assert(init_activation_barriers>0);
    uint16_t cmd=0,ctrl=0;std::memcpy(&cmd,f.pci+4,2);std::memcpy(&ctrl,f.pci+0xA2,2);
    assert(cmd & PCI_COMMAND_INT_DISABLE);
    assert(ctrl & PCI_MSIX_CTRL_ENABLE);
    assert(f.dev.msix_table[0].vector_ctrl==1);
    assert(f.dev.common_cfg->queue_msix_vector==0);
    assert(f.dev.common_cfg->config_msix_vector==VIRTIO_MSI_NO_VECTOR);
    assert(q->avail->flags==0 && q->used->flags==0);
    for(unsigned i=0;i<64;++i) {
        const auto id=q->avail->ring[i];assert(id<64);
        assert(q->desc[id].flags==VRING_DESC_F_WRITE && q->desc[id].len==8);
        assert(q->desc[id].addr==f.drv.dma_phys+i*8);
    }
#if BURSZTYNDIAGVIRTIOINPUTINIT
    assert(input_activation_trace.phase==1);
#endif
}

void test_input_synchronous_driver_ok_activation() {
    for(unsigned mode=0;mode<3;++mode) {
        init_fixture_clear();InitFunction kbd(6,false),tab(7,true);init_functions={&kbd,&tab};
        TEST_ASSERT(host_input_prepare_and_activate(&kbd.drv,&kbd.dev),"Keyboard first");
        const auto keyboard=kbd.bar;
        static unsigned selected,hits,delivered;selected=mode;hits=delivered=0;
        static bool pending;pending=false;
        init_driver_ok_action=+[](InitFunction& f) {
            activation_check_buffers(f);++hits;
            // Device DMA occurs while host_mmio_write8 is still on the stack.
            // Protect the event array tail as well as the allocator's page guard.
            std::memset(static_cast<uint8_t*>(f.drv.dma_virt)+0x200,0xD3,4096-0x200);
            init_emit_report(f);pending=true;
            if(selected==0) { // Defensive case: unexpected delivery despite mask.
#if BURSZTYNDIAGVIRTIOINPUTINIT
                input_activation_entry(f.drv.irq_vector,0x123456,0,0);
#endif
                mock_trigger_irq(f.drv.irq_vector);++delivered;pending=false;
#if BURSZTYNDIAGVIRTIOINPUTINIT
                input_activation_exit(f.drv.irq_vector);
#endif
            }
            assert(virtio_input_poll_events(&f.drv,64)==0);
            assert(f.drv.total_events_processed==0 && f.drv.eventq->last_used_idx==0);
            assert(f.drv.eventq->used->idx==3 && f.drv.eventq->avail->idx==64);
        };
        init_checkpoint_action=+[](InitFunction& f,const char* label) {
            if(std::strcmp(label,"MSI-X unmask done") || !pending) return;
            assert(f.dev.msix_table[0].vector_ctrl==0);
            assert(f.drv.initialized && f.drv.input_dev.registered);
            // Modes 1 and 2 represent a masked vector's pending completion;
            // mode 2 also services it immediately in the software-ready window.
            mock_trigger_irq(f.drv.irq_vector);++delivered;pending=false;
            if(selected==2) assert(virtio_input_poll_events(&f.drv,64)==3);
        };
        TEST_ASSERT(host_input_prepare_and_activate(&tab.drv,&tab.dev),"Synchronous activation returns");
        init_driver_ok_action=nullptr;init_checkpoint_action=nullptr;
        TEST_ASSERT(hits==1 && delivered==1,"Inside-store DMA and pending IRQ actually exercised");
        TEST_ASSERT(kbd.bar==keyboard,"Activation never changes keyboard BAR");
        TEST_ASSERT(virtio_input_poll_events(&tab.drv,64)==0,"Whole report drained exactly once by late activation");
        TEST_ASSERT(tab.drv.total_events_processed==3 && tab.drv.input_dev.abs_val_x==0 && tab.drv.input_dev.abs_val_y==0 && !input_pointer_is_active(&tab.drv.input_dev),"Cold activation report consumed without cursor replay or proof");
        init_emit_report(tab);
        TEST_ASSERT(virtio_input_poll_events(&tab.drv,64)==3 && tab.drv.input_dev.abs_val_x==100 && tab.drv.input_dev.abs_val_y==200 && input_pointer_is_active(&tab.drv.input_dev),"Next fresh complete report is admitted and proves liveness");
        for(unsigned i=0x200;i<4096;++i) TEST_ASSERT(static_cast<uint8_t*>(tab.drv.dma_virt)[i]==0xD3,"Eight-byte DMA bounds");
#if BURSZTYNDIAGVIRTIOINPUTINIT
        TEST_ASSERT(input_activation_trace.phase==2 && !input_activation_trace.armed,"Returned instruction and diagnostic window closed");
#endif
        TEST_ASSERT(virtio_input_reset(&tab.drv) && virtio_input_reset(&kbd.drv),"Cleanup");
        kbd.sentinels();tab.sentinels();init_fixture_clear();
    }
}

void test_input_installer_pmm_dma_addresses() {
    for(uint64_t target:{0x030AE000ULL,0x03875000ULL}) {
        init_fixture_clear();
        InitMultiboot mb;
        mb.ram(target-0x4000,0x104000);
        mb.module(target-0x4000+17,target-0x2000-1); // two intersecting pages
        mb.module(target+0x4000+23,target+0x6000-9); // a later active payload
        mb.finish();
        g_host_reserved_memory = {
            {target-0x4000,std::vector<uint8_t>(0x2000,0x6D)},
            {target+0x4000,std::vector<uint8_t>(0x2000,0x6D)}
        };
        TEST_ASSERT(host_phys_to_virt(target-0x4000)==g_host_reserved_memory[0].bytes.data(),"Module sentinel connected to guest physical map");
        for(uint64_t p=target-0x4000;p<target-0x2000;p+=4096) {
            TEST_ASSERT(!CzyRamkaWolna(p),"Whole rounded module stays reserved");
            TEST_ASSERT(pmm_diagnostic_page_owner(p)==PmmPageOwner::RESERVED_MODULE,"Module ownership");
        }
        TEST_ASSERT(pmm_diagnostic_page_owner(target)==PmmPageOwner::FREE,"DMA candidate is FREE before allocation");
        g_use_pmm_dma=true;
        InitFunction kbd(6,false),tab(7,true);init_functions={&kbd,&tab};
        TEST_ASSERT(host_input_prepare_and_activate(&kbd.drv,&kbd.dev),"Keyboard uses first two ordinary RAM pages");
        init_driver_ok_action=+[](InitFunction& f) { activation_check_buffers(f);init_emit_report(f);mock_trigger_irq(f.drv.irq_vector); };
        TEST_ASSERT(host_input_prepare_and_activate(&tab.drv,&tab.dev),"Tablet synchronous DMA with installer reservation layout");
        init_driver_ok_action=nullptr;
        TEST_ASSERT(tab.drv.eventq->dma_physical==target && tab.drv.dma_phys==target+4096,"Both exact reported address patterns");
        TEST_ASSERT(pmm_diagnostic_page_owner(target)==PmmPageOwner::ALLOCATED_PMM && pmm_diagnostic_page_owner(target+4096)==PmmPageOwner::ALLOCATED_PMM,"DMA allocated RAM");
        TEST_ASSERT(tab.drv.total_events_processed==3 && virtio_input_poll_events(&tab.drv,64)==0,"Activation report drained at higher physical addresses");
        for (const auto& reserved : g_host_reserved_memory)
            for (auto b : reserved.bytes) TEST_ASSERT(b==0x6D,"Physical module sentinel intact after device DMA");
        TEST_ASSERT(!CzyRamkaWolna(target+0x4000) && !CzyRamkaWolna(target+0x5000),"Later module never allocated");
        TEST_ASSERT(virtio_input_reset(&tab.drv) && virtio_input_reset(&kbd.drv),"DMA release");
        TEST_ASSERT(CzyRamkaWolna(target) && CzyRamkaWolna(target+4096),"DMA released only after reset");
        g_use_pmm_dma=false;g_host_reserved_memory.clear();init_fixture_clear();
    }
}

void test_input_pmm_module_page_boundaries() {
    for(unsigned offset:{0U,1U,4095U}) {
        InitMultiboot mb;mb.ram(0x3000000,0x10000);
        mb.module(0x3000000+offset,0x3002000+offset);mb.finish();
        const auto first=offset ? 0x3003000ULL:0x3002000ULL;
        TEST_ASSERT(reinterpret_cast<uintptr_t>(ZaalokujCiagleRamki(1))==first,"Allocation immediately follows full rounded module");
        TEST_ASSERT(reinterpret_cast<uintptr_t>(ZaalokujCiagleRamki(1))==first+4096,"Following event page");
        for(uint64_t p=0x3000000;p<first;p+=4096) TEST_ASSERT(!CzyRamkaWolna(p),"No intersecting module page free");
        TEST_ASSERT(pmm_diagnostic_lock_state()==0,"Allocator releases lock on return");
        ZwolnijCiagleRamki(reinterpret_cast<void*>(first),2);
    }
    InitMultiboot mb;mb.ram(0x3000000,0x4000);mb.framebuffer(0x3000000);mb.finish();
    TEST_ASSERT(!CzyRamkaWolna(0x3000000),"Framebuffer excluded even within available RAM");
    TEST_ASSERT(pmm_diagnostic_page_owner(0x3000000)==PmmPageOwner::FRAMEBUFFER,"Framebuffer ownership");
    TEST_ASSERT(reinterpret_cast<uintptr_t>(ZaalokujCiagleRamki(1))==0x3001000,"DMA cannot use framebuffer");
}

void test_input_activation_emergency_exception_record() {
#if BURSZTYNDIAGVIRTIOINPUTINIT
    for(unsigned vector:{2U,8U,13U,14U,18U,0x36U}) {
        input_activation_trace={};input_activation_trace.armed=1;input_activation_trace.phase=1;
        input_activation_entry(vector,0xCAFEBABE,0x17,0xDEAD000);
        TEST_ASSERT(input_activation_trace.phase==1 && input_activation_trace.vector==vector,"Exception does not erase store progress");
        TEST_ASSERT(input_activation_trace.rip==0xCAFEBABE && input_activation_trace.error==0x17 && input_activation_trace.cr2==0xDEAD000,"Emergency fields");
        input_activation_exit(vector);
        TEST_ASSERT(input_activation_trace.entries==1 && input_activation_trace.exits==1 && input_activation_trace.irq_depth==0,"Entry/exit balanced");
    }
    input_activation_trace.armed=0;
#endif
}

void test_input_pmm_exhaustion_and_lock_release() {
    InitMultiboot mb;mb.ram(0x3000000,0x4000);mb.module(0x3000001,0x3002001);mb.finish();
    auto* frame=ZaalokujCiagleRamki(1);
    TEST_ASSERT(reinterpret_cast<uintptr_t>(frame)==0x3003000,"Last RAM page");
    TEST_ASSERT(!ZaalokujCiagleRamki(1) && !ZaalokujRamke(),"Exhaustion bounded");
    TEST_ASSERT(pmm_diagnostic_lock_state()==0,"Failure paths release PMM lock");
    ZwolnijCiagleRamki(frame,1);
    TEST_ASSERT(ZaalokujCiagleRamki(1)==frame,"Allocator still usable after exhaustion");
    TEST_ASSERT(!ZaalokujCiagleRamki(0) && !ZaalokujCiagleRamki(SIZE_MAX),"Invalid requests fail without waiting");
    TEST_ASSERT(pmm_diagnostic_lock_state()==0,"Invalid requests leave lock clear");
}
