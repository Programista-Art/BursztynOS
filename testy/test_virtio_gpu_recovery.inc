// VirtIO-GPU bounded late-completion recovery: deterministic fault injection.
// Included by testy/test_virtio_pci.cpp (uses its MMIO mock and mock clock).
// State machine and policy: docs/virtio-gpu-mouse-reliability-2026-11-11.md

#include <algorithm>

namespace gpu_recovery_test {

// Every driver clock read advances mock time by `step_ns`; restores all
// injection knobs on scope exit (also on an early TEST_ASSERT return).
struct MockClock {
    MockClock(uint64_t start_ns, uint64_t step_ns) {
        g_host_use_mock_time = true;
        g_host_mock_time_ns = start_ns;
        g_host_mock_step_ns = step_ns;
    }
    ~MockClock() {
        g_host_use_mock_time = false;
        g_host_mock_step_ns = 0;
        g_gpu_complete_on_clock_read = 0;
        g_gpu_complete_jump_ns = 600000000ULL;
        g_mock.gpu_skip_reply = false;
        g_mock.gpu_override_len = -1;
        g_mock.gpu_override_desc_id = -1;
        g_mock.gpu_override_resp_type = 0;
    }
    MockClock(const MockClock&) = delete;
    MockClock& operator=(const MockClock&) = delete;
};

constexpr uint64_t MS = 1000000ULL;

// The device finally processes what it owns (a late completion).
static void device_completes_now(VirtioGpuDriver* drv) {
    g_mock.gpu_skip_reply = false;
    host_mmio_write16(virtio_pci_notify_address(drv->dev, drv->controlq->queue_notify_off), 0);
}

static size_t seen(uint32_t type) {
    return static_cast<size_t>(std::count(g_mock.gpu_commands_seen.begin(), g_mock.gpu_commands_seen.end(), type));
}

static bool boot(VirtioPciDevice& dev, VirtioGpuDriver*& drv) {
    if (!setup_mock_gpu_device(&dev) || !virtio_gpu_init(&dev)) return false;
    drv = virtio_gpu_get_driver(0);
    return drv && virtio_gpu_transfer_and_flush(drv, 0, 0, drv->fb_width, drv->fb_height) &&
           drv->cmd_state == VIRTIO_GPU_CMD_IDLE;
}

// Puts a RESOURCE_FLUSH into TIMED_OUT_PENDING (device keeps it).
static bool flush_times_out(VirtioGpuDriver* drv) {
    g_mock.gpu_skip_reply = true;
    const bool ok = virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 64, 64);
    return !ok && drv->cmd_state == VIRTIO_GPU_CMD_TIMED_OUT_PENDING && !drv->has_error;
}

static bool idle_and_consistent(VirtioGpuDriver* drv) {
    return drv->cmd_state == VIRTIO_GPU_CMD_IDLE && !drv->has_error && drv->pending_head == -1 &&
           drv->controlq->num_free == drv->controlq->queue_size &&
           drv->controlq->last_used_idx == drv->controlq->used->idx;
}

} // namespace gpu_recovery_test

void test_gpu_recovery_normal_completion_dormant() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    for (int i = 0; i < 50; ++i)
        TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 32, 32), "normal flush");
    TEST_ASSERT(idle_and_consistent(drv), "IDLE, queue fully reclaimed");
    TEST_ASSERT(drv->recovery.soft_timeouts == 0 && drv->recovery.late_completions == 0 &&
                drv->recovery.rejected_while_pending == 0 && drv->recovery.permanent_failures == 0,
                "recovery dormant while healthy");
    TEST_ASSERT(!drv->recovery_full_refresh, "no forced full refresh while healthy");
}

void test_gpu_recovery_completion_just_before_timeout() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    MockClock clock(1 * MS, 1 * MS);
    g_gpu_complete_jump_ns = 0;
    g_mock.gpu_skip_reply = true;
    g_gpu_complete_on_clock_read = 450;  // ~450 ms < 500 ms soft deadline
    TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 32, 32), "completes in time");
    TEST_ASSERT(drv->recovery.soft_timeouts == 0, "no soft timeout");
    TEST_ASSERT(idle_and_consistent(drv), "IDLE");
}

void test_gpu_recovery_sync_command_completes_after_soft_deadline() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    MockClock clock(1 * MS, 1 * MS);
    g_gpu_complete_jump_ns = 0;
    const size_t before = seen(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
    g_mock.gpu_skip_reply = true;
    g_gpu_complete_on_clock_read = 700;  // ~700 ms: after soft, before hard
    VirtioGpuRespDisplayInfo info{};
    TEST_ASSERT(virtio_gpu_cmd_get_display_info(drv, &info), "late validated completion is the real result");
    TEST_ASSERT(info.hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO && info.pmodes[0].r.width == 1280,
                "response copied from the late completion");
    TEST_ASSERT(seen(VIRTIO_GPU_CMD_GET_DISPLAY_INFO) == before + 1, "device executed it exactly once");
    TEST_ASSERT(drv->recovery.soft_timeouts == 1 && drv->recovery.late_completions == 1 &&
                drv->recovery.recoveries == 1, "counted as timeout + late completion + recovery");
    TEST_ASSERT(drv->recovery.max_late_ns >= 500 * MS, "lateness recorded");
    TEST_ASSERT(idle_and_consistent(drv), "IDLE");

    // SET_SCANOUT (state-changing) uses the same synchronous path.
    g_mock.gpu_skip_reply = true;
    g_gpu_complete_on_clock_read = 900;
    TEST_ASSERT(virtio_gpu_cmd_set_scanout(drv, 0, drv->resource_id, 0, 0, drv->fb_width, drv->fb_height),
                "SET_SCANOUT late completion");
    TEST_ASSERT(drv->recovery.late_completions == 2 && idle_and_consistent(drv), "second recovery");

    // ATTACH_BACKING (exact response length) as well.
    VirtioGpuMemEntry entry = drv->mem_entries[0];
    g_mock.gpu_skip_reply = true;
    g_gpu_complete_on_clock_read = 800;
    TEST_ASSERT(virtio_gpu_cmd_resource_attach_backing(drv, drv->resource_id, &entry, 1), "ATTACH late completion");
    TEST_ASSERT(drv->recovery.late_completions == 3 && idle_and_consistent(drv), "third recovery");
}

void test_gpu_recovery_async_late_completion_then_normal_operation() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    MockClock clock(1 * MS, 1 * MS);
    const size_t flushes = seen(VIRTIO_GPU_CMD_RESOURCE_FLUSH);
    const size_t transfers = seen(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
    TEST_ASSERT(flush_times_out(drv), "flush -> TIMED_OUT_PENDING, display not disabled");
    TEST_ASSERT(drv->pending_cmd_type == VIRTIO_GPU_CMD_RESOURCE_FLUSH, "pending command recorded");
    TEST_ASSERT(drv->controlq->num_free == drv->controlq->queue_size - 2, "descriptors still owned by device");

    // While pending: no submission, nothing written to the shared buffer.
    const auto* hdr = static_cast<const VirtioGpuCtrlHdr*>(drv->cmd_vaddr);
    TEST_ASSERT(!virtio_gpu_cmd_transfer_to_host_2d(drv, drv->resource_id, 0, 0, 8, 8), "transfer rejected");
    TEST_ASSERT(!virtio_gpu_transfer_and_flush(drv, 0, 0, 8, 8), "frame rejected");
    TEST_ASSERT(hdr->type == VIRTIO_GPU_CMD_RESOURCE_FLUSH, "device-owned request bytes untouched");
    TEST_ASSERT(drv->recovery.rejected_while_pending == 2, "rejections counted");
    TEST_ASSERT(seen(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D) == transfers, "no submission while pending");

    // Late completion arrives; the next frame reaps it and republishes all.
    device_completes_now(drv);
    TEST_ASSERT(seen(VIRTIO_GPU_CMD_RESOURCE_FLUSH) == flushes + 1, "original flush executed once");
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv, 4, 4, 8, 8), "first frame after recovery");
    TEST_ASSERT(drv->recovery.late_completions == 1 && drv->recovery.recoveries == 1, "recovered once");
    const auto* flush = static_cast<const VirtioGpuResourceFlush*>(drv->cmd_vaddr);
    TEST_ASSERT(flush->r.x == 0 && flush->r.y == 0 && flush->r.width == drv->fb_width &&
                flush->r.height == drv->fb_height, "dropped frames republished as one full frame");
    TEST_ASSERT(!drv->recovery_full_refresh, "full refresh consumed");
    TEST_ASSERT(seen(VIRTIO_GPU_CMD_RESOURCE_FLUSH) == flushes + 2, "no duplicate of the timed-out flush");
    for (int i = 0; i < 20; ++i)
        TEST_ASSERT(virtio_gpu_transfer_and_flush(drv, 0, 0, 16, 16), "normal frames after recovery");
    const VirtioGpuRect r{0, 0, 16, 16};
    TEST_ASSERT(virtio_gpu_transfer_rects_and_flush(drv, &r, 1), "batched path after recovery");
    TEST_ASSERT(idle_and_consistent(drv), "IDLE, queue consistent");
}

void test_gpu_recovery_service_reaps_without_new_frame() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    MockClock clock(1 * MS, 1 * MS);
    TEST_ASSERT(flush_times_out(drv), "pending");
    TEST_ASSERT(!virtio_gpu_service_recovery(drv), "nothing to reap yet");
    device_completes_now(drv);
    TEST_ASSERT(virtio_gpu_service_recovery(drv), "idle service call reaps late completion");
    TEST_ASSERT(idle_and_consistent(drv) && drv->recovery_full_refresh, "IDLE, full refresh requested");
}

void test_gpu_recovery_completion_after_hard_deadline_fails_closed() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    const size_t dma_before = g_host_dma_allocs.size();
    MockClock clock(1 * MS, 1 * MS);
    TEST_ASSERT(flush_times_out(drv), "pending");
    g_host_mock_time_ns += VIRTIO_GPU_RECOVERY_HARD_DEADLINE_MS * MS + 1;
    // Deadline check with nothing in the used ring: fail closed ...
    TEST_ASSERT(!virtio_gpu_transfer_and_flush(drv, 0, 0, 8, 8), "no recovery after the bound");
    TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE && drv->has_error, "PERMANENT_FAILURE");
    TEST_ASSERT(drv->recovery.late_completions == 0 && drv->recovery.permanent_failures == 1, "not claimed as recovered");
    TEST_ASSERT(g_host_dma_allocs.size() == dma_before, "DMA kept (quarantine) until confirmed reset");
    // ... and a completion arriving after that decision is never accepted.
    device_completes_now(drv);
    TEST_ASSERT(!virtio_gpu_service_recovery(drv) && drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE &&
                drv->recovery.late_completions == 0, "completion after the decision ignored");
    const size_t total = g_mock.gpu_commands_seen.size();
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 8, 8), "rejected");
    TEST_ASSERT(g_mock.gpu_commands_seen.size() == total, "nothing submitted after permanent failure");
    TEST_ASSERT(virtio_gpu_reset(drv), "confirmed hardware reset");
    TEST_ASSERT(g_host_dma_allocs.empty() && g_host_fb_allocs.empty(), "DMA released only after reset");
}

void test_gpu_recovery_completion_visible_at_deadline_check() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    MockClock clock(1 * MS, 1 * MS);
    TEST_ASSERT(flush_times_out(drv), "pending");
    device_completes_now(drv);  // published while the guest was not polling
    g_host_mock_time_ns += VIRTIO_GPU_RECOVERY_HARD_DEADLINE_MS * MS + 1;
    TEST_ASSERT(virtio_gpu_service_recovery(drv), "validated completion seen at the deadline check is reaped");
    TEST_ASSERT(idle_and_consistent(drv) && drv->recovery.late_completions == 1, "recovered, bounded by one check");
}

void test_gpu_recovery_no_completion_sync_and_async() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    {
        MockClock clock(1 * MS, 10 * MS);
        g_mock.gpu_skip_reply = true;
        VirtioGpuRespDisplayInfo info{};
        TEST_ASSERT(!virtio_gpu_cmd_get_display_info(drv, &info), "sync command: bounded wait then failure");
        TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE && drv->has_error, "fail closed");
        TEST_ASSERT(g_host_mock_time_ns >= VIRTIO_GPU_RECOVERY_HARD_DEADLINE_MS * MS &&
                    g_host_mock_time_ns < (VIRTIO_GPU_RECOVERY_HARD_DEADLINE_MS + 100) * MS,
                    "waited exactly until the hard deadline");
        TEST_ASSERT(drv->lock == 0, "lock released after failure");
    }
    TEST_ASSERT(virtio_gpu_reset(drv), "reset");
    virtio_gpu_reset_all();
    TEST_ASSERT(boot(dev, drv), "re-init after reset");
    {
        MockClock clock(1 * MS, 1 * MS);
        TEST_ASSERT(flush_times_out(drv), "async pending");
        for (int i = 0; i < 9; ++i) {
            g_host_mock_time_ns += 1000 * MS;
            TEST_ASSERT(!virtio_gpu_transfer_and_flush(drv, 0, 0, 8, 8), "still pending, rejected quickly");
        }
        TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_TIMED_OUT_PENDING, "pending within bound");
        g_host_mock_time_ns += 1000 * MS;
        TEST_ASSERT(!virtio_gpu_transfer_and_flush(drv, 0, 0, 8, 8), "rejected");
        TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE, "permanent after hard deadline");
        TEST_ASSERT(drv->lock == 0, "lock released");
    }
}

void test_gpu_recovery_invalid_late_responses() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    {
        // Late completion carrying a device error: queue is consistent, the
        // dropped frame stays dropped, normal operation continues.
        MockClock clock(1 * MS, 1 * MS);
        TEST_ASSERT(flush_times_out(drv), "pending");
        g_mock.gpu_override_resp_type = VIRTIO_GPU_RESP_ERR_UNSPEC;
        device_completes_now(drv);
        g_mock.gpu_override_resp_type = 0;
        TEST_ASSERT(virtio_gpu_service_recovery(drv), "error response reaped, queue consistent");
        TEST_ASSERT(drv->recovery.late_error_responses == 1 && idle_and_consistent(drv), "counted, IDLE");
        TEST_ASSERT(virtio_gpu_transfer_and_flush(drv, 0, 0, 8, 8), "normal frame afterwards");

        // Synchronous command whose late response is an error: false, no poison.
        g_mock.gpu_skip_reply = true;
        g_gpu_complete_jump_ns = 0;
        g_gpu_complete_on_clock_read = 700;
        g_mock.gpu_override_resp_type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        TEST_ASSERT(!virtio_gpu_cmd_set_scanout(drv, 0, drv->resource_id, 0, 0, 8, 8), "late error is a failure");
        g_mock.gpu_override_resp_type = 0;
        TEST_ASSERT(idle_and_consistent(drv), "but queue stays usable");
    }
    {
        // Late completion with an impossible used length: fail closed.
        MockClock clock(1 * MS, 1 * MS);
        const uint64_t recoveries_before = drv->recovery.recoveries;
        const uint64_t late_before = drv->recovery.late_completions;
        TEST_ASSERT(flush_times_out(drv), "pending");
        g_mock.gpu_override_len = 3;  // < sizeof(VirtioGpuCtrlHdr)
        device_completes_now(drv);
        g_mock.gpu_override_len = -1;
        TEST_ASSERT(!virtio_gpu_service_recovery(drv), "invalid used_len never accepted");
        TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE && drv->has_error, "PERMANENT_FAILURE");
        TEST_ASSERT(drv->recovery.recoveries == recoveries_before, "no recovery claimed for the invalid response");
        TEST_ASSERT(drv->recovery.late_completions == late_before, "invalid response not counted as late completion");
    }
}

void test_gpu_recovery_unexpected_descriptor_index() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    MockClock clock(1 * MS, 1 * MS);
    TEST_ASSERT(flush_times_out(drv), "pending");
    g_mock.gpu_override_desc_id = (drv->pending_head + 5) % static_cast<int32_t>(drv->controlq->queue_size);
    device_completes_now(drv);
    g_mock.gpu_override_desc_id = -1;
    TEST_ASSERT(!virtio_gpu_service_recovery(drv), "foreign descriptor id rejected");
    TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE && drv->has_error, "fail closed");
    TEST_ASSERT(drv->recovery.late_completions == 0, "no recovery claimed");
}

void test_gpu_recovery_spurious_used_entry_while_idle() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    VirtQueue* q = drv->controlq;
    const uint16_t slot = q->used->idx & (q->queue_size - 1U);
    q->used->ring[slot].id = 0;
    q->used->ring[slot].len = sizeof(VirtioGpuCtrlHdr);
    q->used->idx = static_cast<uint16_t>(q->used->idx + 1U);
    const size_t total = g_mock.gpu_commands_seen.size();
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 8, 8), "not submitted");
    TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE, "unexpected used entry -> fail closed");
    TEST_ASSERT(g_mock.gpu_commands_seen.size() == total, "nothing submitted");
}

void test_gpu_recovery_used_ring_wraparound() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    // Fast-forward an idle queue to just below the 16-bit wrap, consistently on
    // the driver and device side (as after 65,532 completed commands).
    VirtQueue* q = drv->controlq;
    TEST_ASSERT(idle_and_consistent(drv), "idle before fast-forward");
    q->avail->idx = q->used->idx = q->last_used_idx = 65532U;
    g_mock.gpu_processed_count = 65532U;
    for (int i = 0; i < 2; ++i)
        TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 1, 1), "fill");
    TEST_ASSERT(q->last_used_idx == 65534U, "at 65534");
    MockClock clock(1 * MS, 1 * MS);
    TEST_ASSERT(flush_times_out(drv), "pending at used idx 65534");
    TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 1, 1) == false, "rejected while pending");
    device_completes_now(drv);
    TEST_ASSERT(virtio_gpu_service_recovery(drv), "late completion reaped across 65535");
    for (int i = 0; i < 4; ++i)
        TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 1, 1), "post-wrap command");
    TEST_ASSERT(drv->controlq->last_used_idx == 3U && idle_and_consistent(drv), "indices wrapped consistently");
    TEST_ASSERT(flush_times_out(drv), "timeout after wrap");
    device_completes_now(drv);
    TEST_ASSERT(virtio_gpu_service_recovery(drv) && idle_and_consistent(drv), "recovery after wrap");
}

void test_gpu_recovery_repeated_timeouts_and_lifetime() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    const size_t dma = g_host_dma_allocs.size(), fb = g_host_fb_allocs.size();
    void* const cmd = drv->cmd_vaddr;
    void* const fbv = drv->fb_vaddr;
    MockClock clock(1 * MS, 1 * MS);
    for (int cycle = 1; cycle <= 6; ++cycle) {
        TEST_ASSERT(flush_times_out(drv), "timeout");
        TEST_ASSERT(g_host_dma_allocs.size() == dma && g_host_fb_allocs.size() == fb, "no DMA freed while pending");
        TEST_ASSERT(drv->cmd_vaddr == cmd && drv->fb_vaddr == fbv, "buffers not reallocated");
        device_completes_now(drv);
        TEST_ASSERT(virtio_gpu_transfer_and_flush(drv, 0, 0, 8, 8), "frame after recovery");
        TEST_ASSERT(drv->recovery.recoveries == static_cast<uint64_t>(cycle), "one recovery per cycle");
    }
    TEST_ASSERT(drv->recovery.soft_timeouts == 6 && drv->recovery.permanent_failures == 0, "counters");
    TEST_ASSERT(idle_and_consistent(drv), "IDLE");

    // Reset while a command is pending: no new command, DMA freed only after
    // the device confirmed the reset.
    TEST_ASSERT(flush_times_out(drv), "pending before reset");
    const size_t total = g_mock.gpu_commands_seen.size();
    TEST_ASSERT(virtio_gpu_reset(drv), "reset while pending");
    TEST_ASSERT(g_mock.gpu_commands_seen.size() == total, "reset sent no command into the owned buffer");
    TEST_ASSERT(g_host_dma_allocs.empty() && g_host_fb_allocs.empty(), "released after confirmed reset");
    TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_IDLE, "state cleared by reset");
}

void test_gpu_recovery_without_clock_fails_closed() {
    using namespace gpu_recovery_test;
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    VirtioGpuDriver* drv = nullptr;
    TEST_ASSERT(boot(dev, drv), "boot");
    MockClock clock(0, 0);  // gpu_monotonic_ns() == 0: no HPET
    g_mock.gpu_skip_reply = true;
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 8, 8), "spin guard bounds the wait");
    TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_PERMANENT_FAILURE, "unmeasurable hard deadline -> fail closed");
}
