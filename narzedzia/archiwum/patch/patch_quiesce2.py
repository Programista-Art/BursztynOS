import re

with open("virtio_gpu.cpp", "r") as f:
    content = f.read()

# Replace virtio_gpu_quiesce_for_reboot completely
old_func_start = content.find("void virtio_gpu_quiesce_for_reboot(void) {")
old_func_end = content.find("bool virtio_gpu_reset(VirtioGpuDriver* drv) {")

if old_func_start != -1 and old_func_end != -1:
    new_func = """void virtio_gpu_quiesce_for_reboot(void) {
    constexpr uint64_t QUIESCE_TIMEOUT_NS = 200ULL * 1000000ULL; /* 200 ms */

    /* Upewnij sie, ze nowe zgloszenia GPU sa bezwzglednie zablokowane */
    virtio_gpu_stop_submissions();

    for (uint32_t i = 0; i < g_gpu_driver_count; ++i) {
        VirtioGpuDriver* drv = &g_gpu_drivers[i];
        if (!drv->initialized || drv->has_error || !drv->controlq) continue;

        const uint64_t start_ns = gpu_monotonic_ns();
        const uint64_t deadline_ns =
            (start_ns > UINT64_MAX - QUIESCE_TIMEOUT_NS)
                ? UINT64_MAX
                : (start_ns + QUIESCE_TIMEOUT_NS);

        bool locked = false;
        while (true) {
            if (__atomic_test_and_set(&drv->lock, __ATOMIC_ACQUIRE) == 0) {
                locked = true;
                break;
            }
            const uint64_t now = gpu_monotonic_ns();
            if (now != 0 && now >= deadline_ns) break;
            asm volatile("pause" ::: "memory");
        }

        if (!locked) {
            gpu_klog("[REBOOT TRACE] gpu quiesce lock TIMEOUT - przerywam quiesce urzadzenia");
            drv->has_error = true;
            continue; /* NIE dotykaj controlq, inna zablokowana petla moze go uzywac! */
        }

        /* 2. Sprawdz czy jest oczekujace polecenie lub nieodebrane zakonczenia */
        const bool has_pending =
            virtqueue_has_used(drv->controlq) ||
            (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) == 0 &&
             drv->controlq->avail->idx != drv->controlq->last_used_idx);

        if (has_pending) {
            bool quiesced = false;
            for (uint32_t spin = 0; spin < 10000000U; ++spin) {
                if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) ||
                    virtqueue_has_used(drv->controlq)) {
                    while (virtqueue_has_used(drv->controlq)) {
                        void* cookie = nullptr;
                        uint32_t used_len = 0;
                        if (virtqueue_pop_used(drv->controlq, &cookie, &used_len) < 0) break;
                    }
                    __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);
                    quiesced = true;
                    break;
                }
                const uint64_t now = gpu_monotonic_ns();
                if (now != 0 && now >= deadline_ns) break;
                asm volatile("pause" ::: "memory");
            }
            if (!quiesced) {
                gpu_klog("[REBOOT TRACE] gpu quiesce pop TIMEOUT");
                drv->has_error = true;
                __atomic_clear(&drv->lock, __ATOMIC_RELEASE);
                continue;
            }
        }

        /* 3. Wylacz scanout aby QEMU nie probowalo czytac pamieci podczas resetu maszyny */
        if (drv->scanout_set && drv->cmd_vaddr) {
            VirtioGpuSetScanout req{};
            req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
            req.scanout_id = 0;
            req.resource_id = 0; /* 0 = disable */
            req.r.x = 0; req.r.y = 0; req.r.width = 0; req.r.height = 0;
            
            VirtioGpuCtrlHdr resp{};
            
            /* Budujemy lancuch deskryptorow bezposrednio jak w virtio_gpu_send_command_locked */
            gpu_memcpy(drv->cmd_vaddr, &req, sizeof(req));
            uint8_t* resp_dma_vaddr = static_cast<uint8_t*>(drv->cmd_vaddr) + VIRTIO_GPU_CMD_RESP_OFFSET;
            gpu_secure_zero(resp_dma_vaddr, sizeof(resp));

            const uint64_t req_paddr = drv->cmd_paddr;
            const uint64_t resp_paddr = drv->cmd_paddr + VIRTIO_GPU_CMD_RESP_OFFSET;

            VirtqBuffer bufs[2];
            bufs[0].phys_addr = req_paddr;
            bufs[0].len = static_cast<uint32_t>(sizeof(req));
            bufs[0].is_write = false;

            bufs[1].phys_addr = resp_paddr;
            bufs[1].len = static_cast<uint32_t>(sizeof(resp));
            bufs[1].is_write = true;

            __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);
            
            const int head = virtqueue_submit(drv->controlq, bufs, 2, nullptr);
            if (head >= 0) {
                virtqueue_kick(drv->controlq, true);
                
                /* Czekamy na zakonczenie komendy disable */
                for (uint32_t spin = 0; spin < 10000000U; ++spin) {
                    if (__atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE) || virtqueue_has_used(drv->controlq)) {
                        while (virtqueue_has_used(drv->controlq)) {
                            void* cookie = nullptr;
                            uint32_t used_len = 0;
                            virtqueue_pop_used(drv->controlq, &cookie, &used_len);
                        }
                        __atomic_store_n(&drv->command_done, 0U, __ATOMIC_RELEASE);
                        break;
                    }
                    asm volatile("pause" ::: "memory");
                }
            }
        }

        __atomic_clear(&drv->lock, __ATOMIC_RELEASE);
    }
}

"""
    content = content[:old_func_start] + new_func + content[old_func_end:]
    with open("virtio_gpu.cpp", "w") as f:
        f.write(content)
    print("Patched successfully")
else:
    print("Could not find boundaries")
