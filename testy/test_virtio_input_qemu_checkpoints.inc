// Production transport, synthetic used rings, independent absolute pointer.
void test_input_qemu_relative_checkpoints_coexistence() {
    input_reset_all();g_mock_input_capture.reset();
    VirtioInputQemuDiag::count=VirtioInputQemuDiag::emitted=VirtioInputQemuDiag::source_count=0;
    InputDevice absolute{};absolute.bus_type=INPUT_BUS_VIRTIO;absolute.device_id=0x500;
    absolute.kinds=INPUT_DEVICE_KIND_TABLET;absolute.pointer_mode=INPUT_POINTER_ABSOLUTE;
    absolute.abs_x.min=absolute.abs_y.min=0;absolute.abs_x.max=absolute.abs_y.max=32767;
    TEST_ASSERT(input_register_device(&absolute),"Independent absolute source registers");
    const InputEvent point[]={{INPUT_EV_ABS,INPUT_ABS_X,100},{INPUT_EV_ABS,INPUT_ABS_Y,200},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&absolute,point,3);
    TEST_ASSERT(input_pointer_is_active(&absolute),"Absolute source proven by valid live SYN");
    VirtioPciDevice dev{};VirtioInputDriver drv{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev,1),"Canonical relative mouse transport");
    TEST_ASSERT(virtio_input_prepare(&drv,&dev),"Prepare queue and buffers");
    TEST_ASSERT(drv.init_state==VirtioInputInitState::PREPARED&&!drv.input_dev.registered,"Not registered before queue ready");
    TEST_ASSERT(drv.eventq->avail->idx==drv.num_event_bufs,"All buffers published before DRIVER_OK");
    TEST_ASSERT(!(virtio_pci_get_status(&dev)&VIRTIO_STATUS_DRIVER_OK),"DRIVER_OK absent in PREPARED");
    printf("VIRTIO STATE prepare status=%u avail=%u used=%u registered=%u active=%u\n",virtio_pci_get_status(&dev),drv.eventq->avail->idx,drv.eventq->used->idx,drv.input_dev.registered,input_pointer_is_active(&drv.input_dev));
    virtio_input_activate_prepared();
    TEST_ASSERT(virtio_input_activate(&drv),"Late activation");
    TEST_ASSERT(drv.has_rel_mouse&&!drv.has_abs_tablet,"Relative classification preserved");
    TEST_ASSERT(drv.input_dev.registered&&!input_pointer_is_active(&drv.input_dev),"Registration does not prove activity");
    TEST_ASSERT(input_get_pointer_owner()==&absolute,"Other source remains preferred metadata");
    printf("VIRTIO STATE activate status=%u avail=%u used=%u registered=%u active=%u\n",virtio_pci_get_status(&dev),drv.eventq->avail->idx,drv.eventq->used->idx,drv.input_dev.registered,input_pointer_is_active(&drv.input_dev));
    g_mock_input_capture.reset();
    for(unsigned report=0;report<4;++report){
        const int dx=report%2?-11:7,dy=report%2?-13:5;
        mock_inject_input_event(&drv,INPUT_EV_REL,INPUT_REL_X,uint32_t(dx));
        mock_inject_input_event(&drv,INPUT_EV_REL,INPUT_REL_Y,uint32_t(dy));
        mock_inject_input_event(&drv,INPUT_EV_SYN,INPUT_SYN_REPORT,0);
        mock_trigger_irq(drv.irq_vector);
        const int x=g_mock_input_capture.rel_dx,y=g_mock_input_capture.rel_dy;
        TEST_ASSERT(virtio_input_poll_events(&drv,64)==3,"Synthetic used events decoded once");
        TEST_ASSERT(g_mock_input_capture.rel_dx==x+dx&&g_mock_input_capture.rel_dy==y-dy,"Signed X/Y with existing single legacy Y adapter");
        TEST_ASSERT(input_pointer_is_active(&absolute)&&input_pointer_is_active(&drv.input_dev),"Both sources remain active");
        TEST_ASSERT(input_get_pointer_owner()==&absolute,"Preferred metadata has no admission role");
        printf("VIRTIO STATE report=%u status=%u avail=%u used=%u events=%llu irq=%u dx=%d dy=%d active=%u,%u\n",
            report,virtio_pci_get_status(&dev),drv.eventq->avail->idx,drv.eventq->used->idx,
            (unsigned long long)drv.total_events_processed,drv.irq_count,g_mock_input_capture.rel_dx,g_mock_input_capture.rel_dy,
            input_pointer_is_active(&absolute),input_pointer_is_active(&drv.input_dev));
        input_dispatch_events(&absolute,point,3); // stationary absolute never invalidates relative
    }
    TEST_ASSERT(VirtioInputQemuDiag::source_count>=1,"Bounded recorder tracks source independently");
    TEST_ASSERT(virtio_input_reset(&drv),"Safe reset");input_unregister_device(&absolute);input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(),"All DMA released");
}
