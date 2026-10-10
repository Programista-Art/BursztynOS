// Independent PCI functions for initialization tests. No g_mock BAR ownership.
struct InitFunction {
    VirtioPciDevice dev{};
    VirtioInputDriver drv{};
    std::vector<uint8_t> bar;
    uint8_t pci[256]{};
    bool tablet;
    bool accept_features = true, accept_vector = true, reset_completes = true;
    uint8_t abs_size = 20;
    uint8_t abs_y_size = 20;
    bool allow_driver_ok = true;
    int32_t abs_min = 0, abs_max = 32767;
    uint32_t accesses = 0, resets = 0;
    uint32_t notifies = 0;
    std::vector<uint8_t> statuses;
    InitFunction(uint8_t slot, bool tab, uint16_t size = 64, uint32_t cfg_len = 136)
        : bar(4096, 0xA5), tablet(tab) {
        dev.bus = 0; dev.slot = slot; dev.func = 0; dev.virtio_id = VIRTIO_ID_INPUT;
        dev.common_cfg = reinterpret_cast<VirtioPciCommonCfgRegs*>(bar.data() + 64);
        dev.common_cfg_len = 56;
        std::memset(bar.data()+64, 0, 56);
        dev.common_cfg->num_queues = 2; dev.common_cfg->queue_size = size;
        dev.common_cfg->config_msix_vector = VIRTIO_MSI_NO_VECTOR;
        dev.common_cfg->queue_msix_vector = VIRTIO_MSI_NO_VECTOR;
        dev.notify_base = bar.data()+256; dev.notify_cap_len = 8; dev.notify_multiplier = 4;
        dev.isr = bar.data()+288; dev.isr_cap_len = 1;
        dev.device_cfg = bar.data()+320; dev.device_cfg_len = cfg_len; dev.has_device_cfg = true;
        std::memset(bar.data()+320, 0, cfg_len);
        dev.has_msix = true; dev.msix_cap.cap_offset = 0xA0;
        dev.msix_cap.table_size = 1; dev.msix_cap.table_bir = 0;
        dev.msix_cap.pba_bir = 0; dev.msix_cap.pba_offset = 1024;
        dev.msix_table = reinterpret_cast<PciMsixTableEntry*>(bar.data()+768);
        std::memset(bar.data()+768, 0, 16);
        dev.bars[0].vaddr = bar.data(); dev.bars[0].size = bar.size(); dev.bars[0].is_mapped = true;
        pci[0xA0] = PCI_CAP_ID_MSIX;
    }
    bool allowed(size_t off, size_t n) const {
        return (off>=64 && off+n<=120) || (off>=256 && off+n<=264) ||
               (off==288 && n==1) || (off>=320 && off+n<=320+dev.device_cfg_len);
    }
    void sentinels() const {
        for (size_t i=0;i<bar.size();++i) {
            if (allowed(i,1) || (i>=768 && i<784)) continue;
            assert(bar[i]==0xA5);
        }
    }
};
static std::vector<InitFunction*> init_functions;
static void (*init_checkpoint_action)(InitFunction&, const char*) = nullptr;
// This hook runs INSIDE the MMIO status write, before its caller can return.
static void (*init_driver_ok_action)(InitFunction&) = nullptr;
static unsigned init_activation_barriers = 0;
extern "C" void host_input_activation_barrier() { ++init_activation_barriers; }
static InitFunction* init_find(const volatile void* addr, size_t width, size_t& off) {
    const auto a = reinterpret_cast<uintptr_t>(addr);
    for (auto* f : init_functions) {
        const auto base = reinterpret_cast<uintptr_t>(f->bar.data());
        if (a>=base && a<base+f->bar.size()) {
            off=a-base;
            assert(f->allowed(off,width));
            assert(++f->accesses < 50000); // deterministic MMIO watchdog
            return f;
        }
    }
    return nullptr;
}
static bool init_read(const volatile void* addr, size_t width, uint64_t& result) {
    size_t off=0; auto* f=init_find(addr,width,off); if(!f) return false;
    result=0;
    if(off==68 && width==4) { // device_feature bank
        result = f->dev.common_cfg->device_feature_select ? 0x101 : 0x30000000;
    } else std::memcpy(&result,f->bar.data()+off,width);
    return true;
}
static bool init_write(volatile void* addr, size_t width, uint64_t value) {
    size_t off=0; auto* f=init_find(addr,width,off); if(!f) return false;
    if(off==84 && width==1) {
        if ((value & VIRTIO_STATUS_DRIVER_OK) && !(value & VIRTIO_STATUS_FAILED)) {
            assert(f->drv.init_state == VirtioInputInitState::ACTIVATING);
            assert(f->dev.input_activation_allowed);
        }
        f->statuses.push_back(value);
        if(value==0) {
            ++f->resets;
            if(!f->reset_completes) return true;
            f->dev.common_cfg->queue_enable=0;
        }
        if(!f->accept_features) value &= ~VIRTIO_STATUS_FEATURES_OK;
        if(!f->allow_driver_ok) value &= ~VIRTIO_STATUS_DRIVER_OK;
    }
    if(off==90 && width==2 && !f->accept_vector) value=VIRTIO_MSI_NO_VECTOR;
    if (off>=256 && off<264) {
        assert(f->dev.common_cfg->device_status & VIRTIO_STATUS_DRIVER_OK);
        assert(f->drv.initialized && f->drv.input_dev.registered);
        ++f->notifies;
    }
    std::memcpy(f->bar.data()+off,&value,width);
    if (off==84 && width==1 && (value & VIRTIO_STATUS_DRIVER_OK) && init_driver_ok_action)
        init_driver_ok_action(*f);
    return true;
}
void host_input_init_checkpoint(VirtioPciDevice* dev, const char* label, uint64_t) {
    for(auto* f:init_functions) if(&f->dev==dev && init_checkpoint_action) init_checkpoint_action(*f,label);
}
