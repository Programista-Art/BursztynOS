with open("virtio_gpu.cpp", "r") as f:
    content = f.read()

target = """    if (!completed) {
        char timing[160]{}; size_t pos = 0;
        log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU] timeout elapsed_ns=");
        log_append_u64_dec(timing, sizeof(timing), pos, gpu_monotonic_ns() - start_ns);
        log_append_str(timing, sizeof(timing), pos, " spins_left=");
        log_append_u64_dec(timing, sizeof(timing), pos, spin_guard);
        gpu_klog(timing);
        gpu_klog("[VIRTIO-GPU] BLAD KRYTYCZNY: Timeout polecenia kontrolnego!");
        drv->has_error = true;
        drv->error_count++;
        /* NIGDY nie traktujemy timeoutu jako sukces i NIE zwalniamy DMA uzywanego przez sprzet! */
        return false;
    }"""

replacement = """    if (!completed) {
        char timing[400]{}; size_t pos = 0;
        log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU] timeout elapsed_ns=");
        log_append_u64_dec(timing, sizeof(timing), pos, gpu_monotonic_ns() - start_ns);
        log_append_str(timing, sizeof(timing), pos, " spins_left=");
        log_append_u64_dec(timing, sizeof(timing), pos, spin_guard);
        gpu_klog(timing);
        gpu_klog("[VIRTIO-GPU] BLAD KRYTYCZNY: Timeout polecenia kontrolnego!");
        
        const uint32_t cmd_type = static_cast<const VirtioGpuCtrlHdr*>(req)->type;
        const uint32_t act_type = static_cast<const VirtioGpuCtrlHdr*>(static_cast<const void*>(resp_dma_vaddr))->type;
        const char* cmd_name = "UNKNOWN";
        switch(cmd_type) {
            case 0x0100: cmd_name = "GET_DISPLAY_INFO"; break;
            case 0x0101: cmd_name = "RESOURCE_CREATE_2D"; break;
            case 0x0102: cmd_name = "RESOURCE_UNREF"; break;
            case 0x0103: cmd_name = "SET_SCANOUT"; break;
            case 0x0104: cmd_name = "RESOURCE_FLUSH"; break;
            case 0x0105: cmd_name = "TRANSFER_TO_HOST_2D"; break;
            case 0x0106: cmd_name = "RESOURCE_ATTACH_BACKING"; break;
            case 0x0107: cmd_name = "RESOURCE_DETACH_BACKING"; break;
            case 0x0300: cmd_name = "UPDATE_CURSOR"; break;
            case 0x0301: cmd_name = "MOVE_CURSOR"; break;
        }

        pos = 0;
        log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] cmd=");
        log_append_u64_dec(timing, sizeof(timing), pos, cmd_type);
        log_append_str(timing, sizeof(timing), pos, " (");
        log_append_str(timing, sizeof(timing), pos, cmd_name);
        log_append_str(timing, sizeof(timing), pos, ") exp_resp=");
        log_append_u64_dec(timing, sizeof(timing), pos, expected_resp_type);
        log_append_str(timing, sizeof(timing), pos, " act_resp=");
        log_append_u64_dec(timing, sizeof(timing), pos, act_type);
        gpu_klog(timing);

        pos = 0;
        log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] qstate: avail=");
        log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->avail ? drv->controlq->avail->idx : 0);
        log_append_str(timing, sizeof(timing), pos, " used=");
        log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->used ? drv->controlq->used->idx : 0);
        log_append_str(timing, sizeof(timing), pos, " last_used=");
        log_append_u64_dec(timing, sizeof(timing), pos, drv->controlq->last_used_idx);
        log_append_str(timing, sizeof(timing), pos, " head=");
        log_append_u64_dec(timing, sizeof(timing), pos, head);
        gpu_klog(timing);

        pos = 0;
        log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] drvstate: cmd_done=");
        log_append_u64_dec(timing, sizeof(timing), pos, __atomic_load_n(&drv->command_done, __ATOMIC_ACQUIRE));
        log_append_str(timing, sizeof(timing), pos, " lock=");
        log_append_u64_dec(timing, sizeof(timing), pos, drv->lock);
        log_append_str(timing, sizeof(timing), pos, " err=");
        log_append_u64_dec(timing, sizeof(timing), pos, drv->has_error);
        log_append_str(timing, sizeof(timing), pos, " quiesc=");
        log_append_u64_dec(timing, sizeof(timing), pos, __atomic_load_n(&g_gpu_reboot_quiescing, __ATOMIC_ACQUIRE));
        gpu_klog(timing);

        if (drv->dev) {
            uint16_t isr = virtio_pci_read_isr(drv->dev);
            pos = 0;
            log_append_str(timing, sizeof(timing), pos, "[VIRTIO-GPU-DIAG] pci isr=");
            log_append_u64_dec(timing, sizeof(timing), pos, isr);
            gpu_klog(timing);
        }

        drv->has_error = true;
        drv->error_count++;
        /* NIGDY nie traktujemy timeoutu jako sukces i NIE zwalniamy DMA uzywanego przez sprzet! */
        return false;
    }"""

if target in content:
    content = content.replace(target, replacement)
    with open("virtio_gpu.cpp", "w") as f:
        f.write(content)
    print("Patched successfully")
else:
    print("Target not found")
