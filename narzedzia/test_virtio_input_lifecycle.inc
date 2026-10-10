extern "C" uint32_t virtio_input_host_activate_prepared(VirtioInputDriver* const*, size_t);

static void lifecycle_prepared(InitFunction& f) {
    TEST_ASSERT(f.drv.init_state==VirtioInputInitState::PREPARED,"Explicit PREPARED state");
    TEST_ASSERT(f.dev.common_cfg->device_status==0x0B,"Preparation must leave status exactly 0x0B");
    TEST_ASSERT(f.statuses==std::vector<uint8_t>({0,1,3,11}),"No early DRIVER_OK attempt");
    TEST_ASSERT(f.notifies==0 && !f.dev.input_activation_allowed,"No early notify or activation permission");
    TEST_ASSERT(!f.drv.initialized && !f.drv.input_dev.registered,"Prepared device is invisible to InputDevice");
    TEST_ASSERT(f.drv.eventq && f.drv.dma_events && f.drv.eventq->avail->idx==64,"Posted buffers retained");
    TEST_ASSERT(f.dev.msix_table[0].vector_ctrl==1,"MSI-X remains masked throughout preparation");
    TEST_ASSERT(virtio_input_poll_events(&f.drv,64)==0,"Prepared service stays disabled");
}

static InputDevice lifecycle_ps2() {
    InputDevice dev{};dev.bus_type=INPUT_BUS_PS2;dev.kinds=INPUT_DEVICE_KIND_MOUSE;
    std::strcpy(dev.name,"PS2 lifecycle fallback");return dev;
}

void test_input_prepare_activate_single_matrix() {
    for (bool tablet : {false,true}) {
        init_fixture_clear();InitFunction f(7,tablet);init_functions={&f};
        TEST_ASSERT(virtio_input_init(&f.drv,&f.dev),"Legacy init ABI is preparation only");
        lifecycle_prepared(f);
        TEST_ASSERT(!virtio_input_activate(&f.drv),"Per-device activation cannot bypass late barrier");
        virtio_pci_set_status(&f.dev,0x0F);
        lifecycle_prepared(f); // Shared transport also rejects an early write.
        const auto q=f.drv.eventq;const auto events=f.drv.dma_virt;
        const auto qphys=q->dma_physical, ephys=f.drv.dma_phys;
        const auto features=f.dev.driver_features;const auto resets=f.resets;
        const auto allocations=g_host_dma_allocs.size();const auto irq=f.drv.irq_vector;
        VirtioInputDriver* drivers[]={&f.drv};
        TEST_ASSERT(virtio_input_host_activate_prepared(drivers,1)==1,"Late activation succeeds");
        TEST_ASSERT(f.dev.common_cfg->device_status==0x0F && f.drv.init_state==VirtioInputInitState::ACTIVE,"Late DRIVER_OK and ACTIVE");
        TEST_ASSERT(f.drv.eventq==q && f.drv.dma_virt==events && q->dma_physical==qphys && f.drv.dma_phys==ephys,"No queue or DMA replacement");
        TEST_ASSERT(f.resets==resets && f.dev.driver_features==features && f.drv.irq_vector==irq && g_host_dma_allocs.size()==allocations,"No reset, renegotiation, allocation or new IRQ");
        TEST_ASSERT(f.notifies==1 && f.dev.msix_table[0].vector_ctrl==0,"Initial notification after readiness and unmask");
        TEST_ASSERT(virtio_input_reset(&f.drv),"Cleanup");f.sentinels();init_fixture_clear();
    }
}

void test_input_prepared_pair_idempotent_late_pass() {
    init_fixture_clear();InputDevice ps2=lifecycle_ps2();TEST_ASSERT(input_register_device(&ps2),"PS2 available");
    InitFunction kbd(6,false),tab(7,true);init_functions={&kbd,&tab};
    TEST_ASSERT(virtio_input_prepare(&kbd.drv,&kbd.dev) && virtio_input_prepare(&tab.drv,&tab.dev),"Prepare both");
    lifecycle_prepared(kbd);lifecycle_prepared(tab);
    TEST_ASSERT(input_get_pointer_owner()==&ps2 && input_get_device_count()==1,"PS2 owns pointer until successful activation");
    VirtioInputDriver* drivers[]={&kbd.drv,&tab.drv};
    TEST_ASSERT(virtio_input_host_activate_prepared(drivers,2)==2,"Common pass activates keyboard and tablet");
    TEST_ASSERT(input_get_pointer_owner()==&tab.drv.input_dev && input_get_device_count()==3,"Successful tablet owns pointer");
    const auto kstatuses=kbd.statuses,tstatuses=tab.statuses;
    const auto kbar=kbd.bar,tbar=tab.bar;
    const auto allocations=g_host_dma_allocs.size();const auto vectors=g_mock_idt_allocated_count;
    TEST_ASSERT(virtio_input_host_activate_prepared(drivers,2)==2,"Repeated late pass idempotent");
    TEST_ASSERT(virtio_input_activate(&tab.drv),"Repeated individual activation idempotent");
    TEST_ASSERT(kbd.statuses==kstatuses && tab.statuses==tstatuses && kbd.bar==kbar && tab.bar==tbar,"Second pass makes no hardware accesses that mutate state");
    TEST_ASSERT(kbd.resets==1 && tab.resets==1 && kbd.notifies==1 && tab.notifies==1,"Exactly one reset/activation/initial notify per device");
    TEST_ASSERT(input_get_device_count()==3 && g_host_dma_allocs.size()==allocations && g_mock_idt_allocated_count==vectors,"No duplicate registration or resources");
    TEST_ASSERT(!virtio_input_prepare(&tab.drv,&tab.dev),"Active instance cannot be prepared again");
    TEST_ASSERT(virtio_input_reset(&tab.drv) && virtio_input_reset(&kbd.drv),"Cleanup");
    TEST_ASSERT(input_get_pointer_owner()==&ps2,"PS2 resumes ownership after removal");
    input_unregister_device(&ps2);init_fixture_clear();
}

void test_input_late_failure_preserves_other_device_and_ps2() {
    for (bool fail_tablet : {false,true}) {
        init_fixture_clear();InputDevice ps2=lifecycle_ps2();TEST_ASSERT(input_register_device(&ps2),"PS2 fallback");
        InitFunction kbd(6,false),tab(7,true);init_functions={&kbd,&tab};
        TEST_ASSERT(virtio_input_prepare(&kbd.drv,&kbd.dev) && virtio_input_prepare(&tab.drv,&tab.dev),"Both prepared");
        auto& failed=fail_tablet?tab:kbd;auto& good=fail_tablet?kbd:tab;
        failed.allow_driver_ok=false;VirtioInputDriver* drivers[]={&kbd.drv,&tab.drv};
        TEST_ASSERT(virtio_input_host_activate_prepared(drivers,2)==1,"A failed function does not stop the pass");
        TEST_ASSERT(good.drv.init_state==VirtioInputInitState::ACTIVE && good.drv.initialized,"Remaining device active");
        TEST_ASSERT(failed.drv.init_state==VirtioInputInitState::FAILED && !failed.drv.dev && !failed.drv.eventq && !failed.drv.dma_virt,"Rejected DRIVER_OK cleanly resets/releases");
        TEST_ASSERT(!failed.drv.input_dev.registered && failed.notifies==0 && failed.dev.msix_table[0].vector_ctrl==1,"No publication/notify/unmask on failure");
        TEST_ASSERT(input_get_pointer_owner()==(fail_tablet?&ps2:&tab.drv.input_dev),"Failed tablet never steals PS2 ownership");
        const auto failed_statuses=failed.statuses;
        TEST_ASSERT(virtio_input_host_activate_prepared(drivers,2)==1 && failed.statuses==failed_statuses,"Failed device is not retried");
        TEST_ASSERT(virtio_input_reset(&good.drv),"Cleanup survivor");input_unregister_device(&ps2);init_fixture_clear();
    }
}

void test_input_prepared_cleanup_and_pre_activation_failure() {
    for (bool reject_before_store : {false,true}) {
        init_fixture_clear();InitFunction f(7,true);init_functions={&f};
        const auto allocations=g_host_dma_allocs.size();
        const auto vectors=g_mock_idt_allocated_count;
        TEST_ASSERT(virtio_input_prepare(&f.drv,&f.dev),"Prepared resources");lifecycle_prepared(f);
        if (reject_before_store) {
            f.dev.common_cfg->device_status=VIRTIO_STATUS_DEVICE_NEEDS_RESET|0x0B;
            VirtioInputDriver* drivers[]={&f.drv};
            TEST_ASSERT(virtio_input_host_activate_prepared(drivers,1)==0,"Changed prepared status fails before DRIVER_OK");
            TEST_ASSERT(f.drv.init_state==VirtioInputInitState::FAILED,"Failure retained");
        } else TEST_ASSERT(virtio_input_reset(&f.drv),"Prepared teardown does not activate");
        for (auto status:f.statuses) TEST_ASSERT(!(status&VIRTIO_STATUS_DRIVER_OK),"Cleanup never activates a PREPARED device");
        TEST_ASSERT(f.notifies==0 && !f.drv.dev && !f.drv.dma_virt && !f.drv.eventq,"No notify and all resources released");
        TEST_ASSERT(g_host_dma_allocs.size()==allocations && g_mock_idt_allocated_count==vectors,"DMA and IRQ baseline restored");
        f.sentinels();init_fixture_clear();
    }
}

void test_input_late_pair_synchronous_report_and_pending_irq() {
    for (unsigned mode=0;mode<3;++mode) {
        init_fixture_clear();InputDevice ps2=lifecycle_ps2();TEST_ASSERT(input_register_device(&ps2),"PS2 fallback");
        InitFunction kbd(6,false),tab(7,true);init_functions={&kbd,&tab};
        TEST_ASSERT(virtio_input_prepare(&kbd.drv,&kbd.dev) && virtio_input_prepare(&tab.drv,&tab.dev),"Both remain PREPARED before pass");
        lifecycle_prepared(kbd);lifecycle_prepared(tab);
        // A premature IRQ records only a hint; it cannot expose the tablet.
        mock_trigger_irq(tab.drv.irq_vector);
        TEST_ASSERT(virtio_input_poll_events(&tab.drv,64)==0 && input_get_pointer_owner()==&ps2,"Prepared early IRQ inert");
        static unsigned selected, reports, irqs;selected=mode;reports=irqs=0;
        static bool pending;pending=false;
        init_driver_ok_action=+[](InitFunction& f) {
            if (!f.tablet) return;
            TEST_ASSERT(f.drv.init_state==VirtioInputInitState::ACTIVATING && !f.drv.input_dev.registered,"Synchronous event sees complete queue but no public tablet");
            TEST_ASSERT(input_get_pointer_owner()->bus_type==INPUT_BUS_PS2,"Ownership unchanged inside DRIVER_OK");
            init_emit_report(f);++reports;pending=true;
            if (selected==0) {mock_trigger_irq(f.drv.irq_vector);++irqs;pending=false;}
            TEST_ASSERT(virtio_input_poll_events(&f.drv,64)==0 && f.drv.eventq->last_used_idx==0,"No partial dispatch during store");
        };
        init_checkpoint_action=+[](InitFunction& f,const char* label) {
            if (!std::strcmp(label,"MSI-X unmask begin"))
                TEST_ASSERT(f.drv.initialized && f.drv.input_dev.registered && f.dev.msix_table[0].vector_ctrl==1,"Publish software readiness before unmask");
            if (f.tablet && pending && !std::strcmp(label,"MSI-X unmask done") && selected==1) {
                mock_trigger_irq(f.drv.irq_vector);++irqs;pending=false;
            }
        };
        VirtioInputDriver* drivers[]={&kbd.drv,&tab.drv};
        TEST_ASSERT(virtio_input_host_activate_prepared(drivers,2)==2,"Late pair activation returns");
        TEST_ASSERT(reports==1 && irqs==(mode==2?0U:1U),"Synchronous/latched/no-IRQ variants actually exercised");
        TEST_ASSERT(tab.drv.total_events_processed==3 && tab.drv.input_dev.abs_val_x==100 && tab.drv.input_dev.abs_val_y==200,"Whole immediate ABS report delivered after readiness");
        TEST_ASSERT(virtio_input_poll_events(&tab.drv,64)==0,"No duplicate delivery");
        TEST_ASSERT(input_get_pointer_owner()==&tab.drv.input_dev,"Tablet owns pointer only after successful activation");
        init_driver_ok_action=nullptr;init_checkpoint_action=nullptr;
        TEST_ASSERT(virtio_input_reset(&tab.drv) && virtio_input_reset(&kbd.drv),"Cleanup");input_unregister_device(&ps2);init_fixture_clear();
    }
}
