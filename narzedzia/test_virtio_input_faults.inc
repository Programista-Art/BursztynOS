// Included by the production VirtIO host harness. No nested virtualization.
static VirtioInputDriver* fault_driver;
static unsigned fault_feedback;
static void short_completion_feedback() {
    // A finite host watchdog bounds the OLD implementation too. A real device
    // returning each repost indefinitely would never empty its used ring.
    if (++fault_feedback < 4096)
        assert(mock_inject_input_event(fault_driver, INPUT_EV_ABS, INPUT_ABS_X, 1, 4));
}

static bool mock_inject_input_event_for(VirtioInputDriver* drv, uint16_t& consumed,
                                        uint16_t type, uint16_t code, uint32_t value) {
    if (!drv || !drv->eventq) return false;
    VirtQueue* q = drv->eventq;
    if (consumed == q->avail->idx) return false;
    const uint16_t slot = consumed & (q->queue_size - 1U);
    const uint16_t id = q->avail->ring[slot];
    if (id >= q->queue_size) return false;
    auto* ev = static_cast<VirtioInputEvent*>(host_phys_to_virt(q->desc[id].addr));
    if (!ev) return false;
    ev->type = type; ev->code = code; ev->value = value;
    const uint16_t used_slot = q->used->idx & (q->queue_size - 1U);
    q->used->ring[used_slot].id = id;
    q->used->ring[used_slot].len = sizeof(VirtioInputEvent);
    asm volatile("" ::: "memory");
    q->used->idx = uint16_t(q->used->idx + 1U);
    asm volatile("mfence" ::: "memory");
    ++consumed;
    if (drv->use_msix) mock_check_and_trigger_msix(0);
    return true;
}

// Keep the mock PCI transport/config and DMA pool alive for two independent
// devices at once. Device-specific config bytes and IRQ vectors must not alias.
void test_virtio_input_dual_device_and_motion_stress() {
    input_reset_all();
    g_mock.reset();
    g_mock_input_cfg.reset();
    g_mock_input_cfg.dev_type = 0;
    g_mock_input_capture.reset();
    g_mock_input_processed_idx = 0;
    setup_pci_hdr(0x1AF4, 0x1052, 0x1AF4, 0x0012);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG, 0, 0x080, 1, 0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 256, 0}
    };
    build_cap_chain(caps, 0xA0);
    setup_msix_cap(0xA0, 0, 4, 1, 0x000, 1, 0x800);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;
    g_mock.queues[1].size = 32;
    g_mock.device_features = VIRTIO_F_VERSION_1;
    VirtioPciDevice kbd_dev{};
    TEST_ASSERT(virtio_pci_probe(0, 6, 0, &kbd_dev, 0), "Probe keyboard BDF");
    VirtioInputDriver kbd{};
    TEST_ASSERT(host_input_prepare_and_activate(&kbd, &kbd_dev), "Initialize keyboard instance");
    const int kbd_vector = kbd.irq_vector;
    auto* kbd_queue = kbd.eventq;
    auto* kbd_events = kbd.dma_events;
    uint16_t keyboard_consumed = 0;
    MockBar keyboard_bars[6];
    for (size_t i = 0; i < 6; ++i) keyboard_bars[i] = std::move(g_mock.bars[i]);

    // The second PCI function has its own device-config selector/data bytes,
    // queue registers and MSI-X table; model that independently in the mock.
    g_mock.reset();
    g_mock_input_cfg.reset();
    g_mock_input_cfg.dev_type = 2;
    g_mock_input_capture.reset();
    g_mock_input_processed_idx = 0;
    setup_pci_hdr(0x1AF4, 0x1052, 0x1AF4, 0x0012);
    build_cap_chain(caps, 0xA0);
    setup_msix_cap(0xA0, 0, 4, 1, 0x000, 1, 0x800);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;
    g_mock.queues[1].size = 32;
    g_mock.device_features = VIRTIO_F_VERSION_1;
    VirtioPciDevice tab_dev{};
    TEST_ASSERT(virtio_pci_probe(0, 7, 0, &tab_dev, 0), "Probe tablet BDF");
    VirtioInputDriver tablet{};
    TEST_ASSERT(host_input_prepare_and_activate(&tablet, &tab_dev), "Initialize tablet instance");
    uint16_t tablet_consumed = 0;
    TEST_ASSERT(kbd.dev == &kbd_dev && kbd.eventq == kbd_queue && kbd.dma_events == kbd_events &&
                kbd.has_keyboard && !kbd.has_abs_tablet, "Keyboard mutable state remains isolated");
    TEST_ASSERT(tablet.has_abs_tablet && tablet.irq_vector != kbd_vector &&
                tablet.eventq != kbd_queue && tablet.dma_events != kbd_events,
                "Tablet owns independent queue, buffers and vector");
    TEST_ASSERT(virtio_pci_get_queue_msix_vector(&tab_dev) == 0,
                "Each PCI function uses its own table entry zero");

    // Exercise 1,000,000 ABS/SYN records through a queue smaller than the
    // stream. Every poll is bounded to 32 descriptors and preserves all
    // completions; alternately deliver keyboard events during the same run.
    constexpr uint32_t REPORTS = 1000000;
    uint32_t completed = 0;
    g_mock_input_capture.reset();
    for (uint32_t i = 0; i < REPORTS; ++i) {
        if (uint16_t(tablet.eventq->avail->idx - tablet_consumed) < 3) {
            const size_t n = virtio_input_poll_events(&tablet, 32);
            TEST_ASSERT(n > 0 && n <= 32, "Tablet service progresses with a strict per-call bound");
        }
        TEST_ASSERT(mock_inject_input_event_for(&tablet, tablet_consumed, INPUT_EV_ABS, INPUT_ABS_X, i), "Inject tablet X");
        TEST_ASSERT(mock_inject_input_event_for(&tablet, tablet_consumed, INPUT_EV_ABS, INPUT_ABS_Y, REPORTS - i), "Inject tablet Y");
        TEST_ASSERT(mock_inject_input_event_for(&tablet, tablet_consumed, INPUT_EV_SYN, INPUT_SYN_REPORT, 0), "Inject tablet SYN");
        completed += 3;
        if ((i & 0x3ffU) == 0) {
            const uint32_t before = kbd.total_events_processed;
            TEST_ASSERT(mock_inject_input_event_for(&kbd, keyboard_consumed, INPUT_EV_KEY, INPUT_KEY_A,
                                                     ((i >> 10) & 1U) ? 1 : 0),
                        "Inject concurrent keyboard key");
            TEST_ASSERT(mock_inject_input_event_for(&kbd, keyboard_consumed, INPUT_EV_SYN, INPUT_SYN_REPORT, 0),
                        "Inject keyboard SYN");
            TEST_ASSERT(virtio_input_poll_events(&kbd, 32) == 2, "Keyboard remains serviceable");
            TEST_ASSERT(kbd.total_events_processed == before + 2, "Keyboard counters are independent");
        }
        if (tablet_consumed == tablet.eventq->avail->idx || (i & 0x1fU) == 0x1fU) {
            const size_t n = virtio_input_poll_events(&tablet, 32);
            TEST_ASSERT(n <= 32, "No poll exceeds descriptor budget");
        }
    }
    size_t guard = 0;
    while (virtqueue_has_used(tablet.eventq) && guard++ < REPORTS * 3U) {
        TEST_ASSERT(virtio_input_poll_events(&tablet, 32) <= 32, "Final drain remains bounded");
    }
    TEST_ASSERT(guard < REPORTS * 3U && tablet.total_events_processed == completed,
                "One million tablet events drain without loss or nontermination");
    TEST_ASSERT(tablet.eventq->avail->idx == uint16_t(tablet.total_events_processed + 32),
                "Avail index wraps naturally while maintaining 32 posted buffers");
    TEST_ASSERT(g_mock_input_capture.abs_calls == REPORTS,
                "One absolute pointer commit is emitted for every SYN_REPORT");
    TEST_ASSERT(!kbd.has_error && !tablet.has_error, "Both devices remain healthy");
    TEST_ASSERT(virtio_input_reset(&tablet), "Reset tablet instance");
    TEST_ASSERT(virtio_input_reset(&kbd), "Reset keyboard instance");
    input_reset_all();
}

void test_virtio_input_fault_short_completion_feedback() {
    input_reset_all();
    VirtioPciDevice dev{};
    assert(setup_mock_input_device(&dev, 2));
    VirtioInputDriver drv{};
    assert(host_input_prepare_and_activate(&drv, &dev));
    assert(mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_X, 1, 4));
    fault_driver = &drv; fault_feedback = 0;
    g_input_notify_hook = short_completion_feedback;
    virtio_input_poll_events(&drv, 1);
    g_input_notify_hook = nullptr;
    const auto feedback = fault_feedback;
    assert(virtio_input_reset(&drv));
    input_reset_all();
    TEST_ASSERT(feedback <= 1, "Short completions cannot bypass the per-call bound and sustain notification feedback");
}

void test_virtio_input_fault_duplicate_completion() {
    input_reset_all();
    VirtioPciDevice dev{};
    assert(setup_mock_input_device(&dev, 2));
    VirtioInputDriver drv{};
    assert(host_input_prepare_and_activate(&drv, &dev));
    assert(mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_X, 10));
    const auto id = drv.eventq->used->ring[0].id;
    assert(mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0, 8, id));
    virtio_input_poll_events(&drv, 64);
    const bool error = drv.has_error;
    assert(virtio_input_reset(&drv));
    input_reset_all();
    TEST_ASSERT(error, "Duplicate used ID is rejected before the descriptor can be reposted");
}
