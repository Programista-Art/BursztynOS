#include "diagnostyka_pci.h"
#include "kursor_diag.h"
#include "gui_runtime_diag.h"
/*
 * Bursztyn OS - Implementacja Sterownika Urzadzen Wejsciowych VirtIO-INPUT
 *
 * Plik: virtio_input.cpp
 */

#include "virtio_input.h"
#include "virtio_input_qemu_diag.h"
#include "virtio_input_forensic.h"
#include "virtio_pci.h"
#include "virtqueue.h"
#include "input_device.h"
#include "pointer_coordinates.h"
#include "idt.h"
#include "apic.h"
#include "virtio_input_activation_diag.h"
#include "pmm_diagnostics.h"

#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <cstring>
#include <cstdio>
extern bool host_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages);
extern void host_dma_free(void* vaddr, uint64_t paddr, size_t pages);
extern void mock_virtio_input_on_config_select(VirtioPciDevice* dev, uint8_t select, uint8_t subsel);
extern void host_virtio_input_log(const char*) __attribute__((weak));
extern uint64_t host_virtio_input_time_ns() __attribute__((weak));
extern uint64_t host_virtio_input_geometry_generation() __attribute__((weak));
extern bool host_virtio_input_splash_active() __attribute__((weak));
static void input_klog(const char* msg) { if(host_virtio_input_log)host_virtio_input_log(msg); }

static bool input_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    return host_dma_alloc(size, alignment, vaddr, paddr, pages);
}

static void input_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    host_dma_free(vaddr, paddr, pages);
}

#else
#include "sterowniki/dma.h"
#include "grafika.h"
#include "pamiec.h"
#include "sterowniki/czas/hpet.h"
#include "ekran_powitalny_api.h"
extern "C" void* memset(void* s, int c, size_t n);
extern "C" void* memcpy(void* dest, const void* src, size_t n);
extern "C" size_t strlen(const char* s);
#define input_klog klog_serial

static bool input_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    DmaBuffer dma{};
    if (!dma_allocate(size, alignment, &dma)) {
        return false;
    }
    *vaddr = dma.virtual_address;
    *paddr = dma.physical_address;
    *pages = dma.page_count;
    return true;
}

static void input_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    DmaBuffer dma{};
    dma.virtual_address = vaddr;
    dma.physical_address = paddr;
    dma.page_count = pages;
    dma_release(&dma);
}
#endif

extern "C" int grafika_pobierz_mysz_x();
extern "C" int grafika_pobierz_mysz_y();
int grafika_pobierz_szerokosc();
int grafika_pobierz_wysokosc();
namespace VirtioInputForensic { void emit(const char* s) { input_klog(s); } }

namespace {
static uint64_t input_observed_ns() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return host_virtio_input_time_ns ? host_virtio_input_time_ns() : 0;
#else
    return hpet_dostepny() ? czas_monotoniczny_ns() : 0;
#endif
}
static uint64_t input_geometry_generation() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return host_virtio_input_geometry_generation ? host_virtio_input_geometry_generation() : 0;
#else
    return grafika_generacja_ekranu();
#endif
}
[[maybe_unused]] static bool input_splash_active() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return host_virtio_input_splash_active && host_virtio_input_splash_active();
#else
    return powitanie_aktywne();
#endif
}
// x86 coherent WB RAM: acquire orders device's used.idx publication before
// ring/payload reads; volatile byte loads below force exactly eight DMA reads.
static inline void input_dma_acquire() { asm volatile("lfence" ::: "memory"); }
static uint16_t input_wire16(const uint8_t* b) { return uint16_t(b[0])|(uint16_t(b[1])<<8); }
static uint32_t input_wire32(const uint8_t* b) {
    return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);
}
static uint32_t qemu_diag_bdf(const VirtioPciDevice* d){return (uint32_t(d->bus)<<16)|(uint32_t(d->slot)<<8)|d->func;}
static void qemu_diag(VirtioPciDevice* d,const char* label,uint64_t a=0,uint64_t b=0,uint64_t c=0){
    VirtioInputQemuDiag::record(qemu_diag_bdf(d),label,a,b,c);
    VirtioInputForensic::transition(qemu_diag_bdf(d),input_observed_ns(),label,a,b);
}


#if BURSZTYNDIAGVIRTIOINPUTINIT && !defined(BURSZTYN_VIRTIO_HOST_TEST)
bool input_activation_snapshot(VirtioInputDriver* drv) {
    auto* dev = drv->dev;
    input_activation_trace.bdf = (uint64_t(dev->bus) << 16) | (uint64_t(dev->slot) << 8) | dev->func;
    input_activation_trace.phase = 0;
    const auto heap_lock = heap_diagnostic_lock_state();
    const auto pmm_lock = pmm_diagnostic_lock_state();
    const auto pci_lock = pci_diagnostic_lock_state();
    const auto service_lock = __atomic_load_n(&drv->service_lock, __ATOMIC_RELAXED);
    const auto config_lock = __atomic_load_n(&dev->input_config_lock, __ATOMIC_RELAXED);
    input_activation_log("heap lock", heap_lock);
    input_activation_log("PMM lock", pmm_lock);
    input_activation_log("PCI lock", pci_lock);
    input_activation_log("input service lock", service_lock);
    input_activation_log("config lock", config_lock);
    if (heap_lock || pmm_lock || pci_lock || service_lock || config_lock) return false;
    uint32_t cmd = 0, ctrl = 0;
    input_activation_log("PCI snapshot begin", 0);
    if (!pci_diagnostic_try_read(dev->bus, dev->slot, dev->func, PCI_OFFSET_COMMAND_STATUS, &cmd))
        return false;
    input_activation_log("PCI command (INTx disable bit10)", uint16_t(cmd));
    if (drv->use_msix) {
        if (!pci_diagnostic_try_read(dev->bus, dev->slot, dev->func, dev->msix_cap.cap_offset, &ctrl)) return false;
        input_activation_log("MSI-X control (enable15/mask14)", ctrl >> 16);
        input_activation_log("MSI-X vector control", dev->msix_table[0].vector_ctrl);
        input_activation_log("MSI-X queue vector", dev->common_cfg->queue_msix_vector);
        input_activation_log("LAPIC vector pending", apic_diagnostic_pending(static_cast<uint8_t>(drv->irq_vector)));
        if (dev->msix_cap.pba_bir >= 6) return false;
        const auto& bar = dev->bars[dev->msix_cap.pba_bir];
        const auto offset = dev->msix_cap.pba_offset;
        if (!bar.is_mapped || !bar.vaddr || offset > bar.size || bar.size-offset < 8) return false;
        input_activation_log("MSI-X PBA read begin", reinterpret_cast<uintptr_t>(bar.vaddr + offset));
        input_activation_log("MSI-X PBA bits0..31", *reinterpret_cast<volatile const uint32_t*>(bar.vaddr + offset));
        // Report actual masking, and refuse an unsafe activation in the
        // diagnostic build instead of relying on cached msix_enabled alone.
        if (!(cmd & PCI_COMMAND_INT_DISABLE) || !(ctrl >> 16 & PCI_MSIX_CTRL_ENABLE) ||
            !(dev->msix_table[0].vector_ctrl & 1U)) return false;
    }
    // ISR read acknowledges legacy state. Initialization hasn't enabled normal
    // service; used.idx is independently recorded and early MSI-X flags persist.
    input_activation_log("ISR read begin", reinterpret_cast<uintptr_t>(dev->isr));
    input_activation_log("ISR read done", virtio_pci_read_isr(dev));
    input_activation_log("pending flag", __atomic_load_n(&drv->has_pending_events, __ATOMIC_RELAXED));
    input_activation_log("used index", drv->eventq->used->idx);
    input_activation_log("avail index", drv->eventq->avail->idx);
    pmm_diagnostic_dump_modules();
    const uint64_t bases[] = {drv->eventq->dma_physical, drv->dma_phys};
    const size_t pages[] = {drv->eventq->dma_pages, drv->dma_pages};
    static const char* const names[] = {"UNKNOWN", "FREE", "ALLOCATEDPMM", "RESERVEDMODULE", "KERNEL", "FRAMEBUFFER", "ACPI/RESERVED", "MMIO"};
    for (unsigned area = 0; area < 2; ++area) {
        if (pages[area] > 256) return false;
        for (size_t i = 0; i < pages[area]; ++i) {
            const uint64_t page = bases[area] + i * 4096;
            const auto owner = pmm_diagnostic_page_owner(page);
            input_activation_log(area ? "event DMA page" : "queue DMA page", page);
            input_activation_log(names[static_cast<unsigned>(owner)], page);
            if (owner != PmmPageOwner::ALLOCATED_PMM) return false;
        }
    }
    input_activation_log("descriptor base", dev->common_cfg->queue_desc);
    input_activation_log("avail base", dev->common_cfg->queue_driver);
    input_activation_log("used base", dev->common_cfg->queue_device);
    return true;
}
#endif

static VirtioInputDriver s_drivers[VIRTIO_INPUT_MAX_DRIVERS] = {};
static size_t s_driver_count = 0;
static bool s_late_activation_allowed = false;
#ifdef BURSZTYN_VIRTIO_HOST_TEST
static bool s_desktop_ready = true; // Existing fixtures model an interactive display.
#else
static bool s_desktop_ready = false;
#endif

static void virtio_input_msix_handler(void* context) {
    auto* drv = static_cast<VirtioInputDriver*>(context);
    if (!drv || __atomic_load_n(&drv->reset_requested, __ATOMIC_ACQUIRE)) return;
    const auto state = __atomic_load_n(&drv->init_state, __ATOMIC_ACQUIRE);
    if (state == VirtioInputInitState::UNINITIALIZED || state == VirtioInputInitState::FAILED) return;
    // IRQs never inspect queues or dispatch. Preserve an early indication even
    // before DRIVER_OK; normal service starts only after registry publication.
    __atomic_add_fetch(&drv->irq_count, 1U, __ATOMIC_RELAXED);
    __atomic_store_n(&drv->has_pending_events, true, __ATOMIC_RELEASE);
}

static void virtio_input_device_poll(InputDevice* dev) {
    if (!dev || !dev->driver_data) return;
    auto* drv = static_cast<VirtioInputDriver*>(dev->driver_data);
    virtio_input_poll_events(drv, 64);
}

} // namespace

#ifdef BURSZTYN_VIRTIO_HOST_TEST
extern "C" void virtio_input_host_early_irq(VirtioInputDriver* drv) {
    virtio_input_msix_handler(drv);
}
#endif

#ifndef BURSZTYN_DIAG_VIRTIO_INPUT
#define BURSZTYN_DIAG_VIRTIO_INPUT 0
#endif

// Mode 0: one error only; 1: first 128 completions/device plus errors;
// mode 2: error context only. No event-driven unbounded serial logging.
static void input_queue_diagnostic(VirtioInputDriver* drv, const char* reason,
                                   int head, size_t idx, const VirtioInputEvent* ev) {
    if (!reason && (BURSZTYN_DIAG_VIRTIO_INPUT != 1 || drv->diagnostic_events >= 128)) return;
    if (!reason) ++drv->diagnostic_events;
    char msg[512]{}; size_t pos = 0;
    auto txt = [&](const char* s) { while (s && *s && pos + 1 < sizeof(msg)) msg[pos++] = *s++; };
    auto num = [&](int64_t n) {
        uint64_t v = n < 0 ? uint64_t(-(n + 1)) + 1 : uint64_t(n);
        if (n < 0) txt("-");
        char b[24]; size_t i = 0;
        do { b[i++] = char('0' + v % 10); v /= 10; } while (v);
        while (i && pos + 1 < sizeof(msg)) msg[pos++] = b[--i];
    };
    auto field = [&](const char* key, int64_t v) { txt(key); num(v); };
    txt(reason ? "[VIRTIO-INPUT-ERR] disabled: " : "[VIRTIO-INPUT-DIAG] "); txt(reason);
    if (BURSZTYN_DIAG_VIRTIO_INPUT || reason) {
        field(" bdf=", drv->dev ? drv->dev->bus : -1); txt(":");
        num(drv->dev ? drv->dev->slot : -1); txt("."); num(drv->dev ? drv->dev->func : -1);
        txt(" role="); txt(drv->has_abs_tablet ? "tablet" : drv->has_keyboard ? "keyboard" : "relative");
        const auto* q = drv->eventq;
        field(" qsize=", q ? q->queue_size : 0); field(" queue=", q ? q->queue_index : 0);
        field(" vector=", drv->irq_vector); field(" avail=", q ? q->avail->idx : 0);
        field(" used=", q ? q->used->idx : 0); field(" seen=", q ? q->last_used_idx : 0);
        field(" id=", head); field(" buffer=", idx);
        field(" state=", idx < drv->num_event_bufs ? int(drv->buffer_state[idx]) : -1);
        field(" type=", ev ? ev->type : -1); field(" code=", ev ? ev->code : -1);
        field(" value=", ev ? int32_t(ev->value) : 0); field(" syn=", drv->syn_sequence);
        field(" input_queue_depth=", q ? uint16_t(q->used->idx - q->last_used_idx) : 0);
        txt(" owner="); const auto* owner = input_get_pointer_owner(); txt(owner ? owner->name : "none");
    }
    input_klog(msg);
}

static void input_fail_closed(VirtioInputDriver* drv, const char* reason, int head = -1, size_t idx = SIZE_MAX) {
    if (!drv->has_error) input_queue_diagnostic(drv, reason, head, idx, nullptr);
    drv->has_error = true;
    if(drv->dev)VirtioInputForensic::transition(qemu_diag_bdf(drv->dev),input_observed_ns(),reason,uint32_t(head),idx);
    input_discard_pointer_report(&drv->input_dev);
    __atomic_store_n(&drv->init_state, VirtioInputInitState::FAILED, __ATOMIC_RELEASE);
    __atomic_store_n(&drv->initialized, false, __ATOMIC_RELEASE);
    if (drv->eventq) { drv->eventq->avail->flags = VRING_AVAIL_F_NO_INTERRUPT; __atomic_thread_fence(__ATOMIC_SEQ_CST); }
    if (drv->dev && drv->use_msix) virtio_pci_mask_msix_entry(drv->dev, 0);
    input_unregister_device(&drv->input_dev);
    // Do not free/reset live DMA here. Explicit reset owns that lifecycle.
}

static bool input_post_buffer(VirtioInputDriver* drv, size_t idx) {
    if (idx >= drv->num_event_bufs ||
        (drv->buffer_state[idx] != VirtioInputBufferState::FREE &&
         drv->buffer_state[idx] != VirtioInputBufferState::BEING_PROCESSED)) return false;
    drv->dma_events[idx] = {};
    const VirtqBuffer buf{drv->dma_phys + idx * sizeof(VirtioInputEvent), sizeof(VirtioInputEvent), true};
    const int head = virtqueue_submit(drv->eventq, &buf, 1, reinterpret_cast<void*>(idx));
    if (head < 0) return false;
    drv->buffer_head[idx] = uint16_t(head);
    drv->buffer_state[idx] = VirtioInputBufferState::POSTED_TO_DEVICE;
    return true;
}

static bool input_init_failure(VirtioInputDriver* drv, const char* reason = "initialization rejected") {
    drv->failed_init_stage = drv->dev ? drv->dev->input_init_stage : 0;
    __atomic_store_n(&drv->init_state, VirtioInputInitState::FAILED, __ATOMIC_RELEASE);
    char msg[224]{}; size_t pos=0;
    auto text=[&](const char* p){while(p && *p && pos+1<sizeof(msg)) msg[pos++]=*p++;};
    auto number=[&](uint32_t n){char digits[12];unsigned i=0;do{digits[i++]=char('0'+n%10);n/=10;}while(n);while(i && pos+1<sizeof(msg))msg[pos++]=digits[--i];};
    text("[VIRTIO-INPUT] ");text(drv->has_abs_tablet ? "Tablet" : "Device");
    text(" initialization failed at stage ");number(drv->failed_init_stage);
    text(" bdf=");
    if (drv->dev) {number(drv->dev->bus);text(":");number(drv->dev->slot);text(".");number(drv->dev->func);}
    text(" reason=");text(reason);input_klog(msg);
    // Reset has a deterministic 10,000-read limit. A failed reset quarantines
    // the device and its DMA instead of freeing device-owned memory.
    VirtioPciDevice* dev = drv->dev;
    virtio_input_reset(drv);
#if BURSZTYNDIAGVIRTIOINPUTINIT
    input_activation_trace.armed = 0;
#endif
    if (dev) dev->input_init_active = false;
    drv->has_error = true;
    __atomic_store_n(&drv->init_state, VirtioInputInitState::FAILED, __ATOMIC_RELEASE);
    return false;
}

static void input_init_state(VirtioInputDriver* drv, VirtioInputInitState state) {
    __atomic_store_n(&drv->init_state, state, __ATOMIC_RELEASE);
    virtio_pci_input_init_trace(drv->dev, "init state", static_cast<uint8_t>(state));
    VirtioInputForensic::transition(qemu_diag_bdf(drv->dev),input_observed_ns(),"lifecycle",uint8_t(state));
}

extern "C" {

bool virtio_input_query_config(VirtioPciDevice* dev, uint8_t select, uint8_t subsel,
                               void* out_data, uint8_t* out_size, size_t max_out) {
    if (out_size) *out_size = 0;
    if (!dev || !dev->has_device_cfg || dev->device_cfg_len < 8 ||
        (out_data && max_out == 0)) {
        return false;
    }
    if (__atomic_exchange_n(&dev->input_config_lock, 1U, __ATOMIC_ACQUIRE)) return false;
    struct ConfigGuard {
        uint32_t* lock;
        ~ConfigGuard() { __atomic_store_n(lock, 0U, __ATOMIC_RELEASE); }
    } guard{&dev->input_config_lock};

    const uint8_t diagnostic_generation_before=virtio_pci_get_config_generation(dev);
    virtio_pci_input_init_trace(dev, "config select store begin", select);
    virtio_pci_write_device_config8(dev, 0, select);
    virtio_pci_input_init_trace(dev, "config select store done", select);
    virtio_pci_input_init_trace(dev, "config subsel store begin", subsel);
    virtio_pci_write_device_config8(dev, 1, subsel);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    virtio_pci_input_init_trace(dev, "config subsel store done", subsel);

#ifdef BURSZTYN_VIRTIO_HOST_TEST
    mock_virtio_input_on_config_select(dev, select, subsel);
#endif

    virtio_pci_input_init_trace(dev, "config size read begin", dev->device_cfg_len);
    const uint8_t size = virtio_pci_read_device_config8(dev, 2);
    virtio_pci_input_init_trace(dev, "config size read done", size);
    if (size == 0 || size > 128 || size > dev->device_cfg_len - 8) {
        return false;
    }

    uint8_t copy_len = size;
    if (copy_len > 128) {
        copy_len = 128; // Maksymalny rozmiar unii wg specyfikacji VirtIO
    }
    if (max_out > 0 && copy_len > max_out) {
        copy_len = static_cast<uint8_t>(max_out);
    }

    virtio_pci_input_init_trace(dev, "config payload read begin", copy_len);
    if (out_data && copy_len > 0) {
        auto* dst = static_cast<uint8_t*>(out_data);
        for (uint8_t i = 0; i < copy_len; ++i) {
            dst[i] = virtio_pci_read_device_config8(dev, 8 + i);
        }
    }
    virtio_pci_input_init_trace(dev, "config payload read done", copy_len);
    if (select == VIRTIO_INPUT_CFG_EV_BITS && out_data && copy_len)
        virtio_pci_input_init_trace(dev, "event bitmap first byte", *static_cast<uint8_t*>(out_data));
    const uint8_t diagnostic_generation_after=virtio_pci_get_config_generation(dev);
    // Only the initial name/capability reads are recorded, never stress queries.
    if(dev->input_init_active && select==VIRTIO_INPUT_CFG_EV_BITS && out_data){
        const auto* bits=static_cast<const uint8_t*>(out_data);uint64_t low=0;
        for(unsigned i=0;i<copy_len&&i<8;++i)low|=uint64_t(bits[i])<<(i*8);
        qemu_diag(dev,"[VINPUT EVBITS] type/low64/buttons272-279",subsel,low,copy_len>34?bits[34]:0);
        if(subsel==INPUT_EV_REL)qemu_diag(dev,"[VINPUT RELBITS] size/low64",copy_len,low);
        if(subsel==INPUT_EV_KEY)qemu_diag(dev,"[VINPUT KEYBITS] buttons272-279/touch330",copy_len>34?bits[34]:0,copy_len>41?bits[41]:0);
    }
    if(dev->input_init_active)qemu_diag(dev,"[VINPUT config generations] select-subsel/before/after",(uint32_t(select)<<8)|subsel,diagnostic_generation_before,diagnostic_generation_after);
    if (out_size) *out_size = copy_len;
    return true;
}

static bool virtio_input_classify_device(VirtioInputDriver* drv) {
    if (!drv || !drv->dev) return false;

    drv->has_keyboard = false;
    drv->has_rel_mouse = false;
    drv->has_abs_tablet = false;
    drv->input_dev.kinds = INPUT_DEVICE_KIND_UNKNOWN;

    // 1. Analiza bitmapy klawiszy i przyciskow (subsel = INPUT_EV_KEY)
    bool has_kbd_keys = false;

    uint8_t key_bits[128]{};
    uint8_t key_bits_size = 0;
    if (virtio_input_query_config(
            drv->dev, VIRTIO_INPUT_CFG_EV_BITS, INPUT_EV_KEY, key_bits, &key_bits_size, sizeof(key_bits)) && key_bits_size > 0) {

        // Sprawdzenie standardowych klawiszy klawiatury (kody 1..127 w bajtach 0..15)
        const uint8_t max_b = (key_bits_size < 16) ? key_bits_size : 16;
        for (uint8_t b = 0; b < max_b; ++b) {
            if (key_bits[b] != 0) {
                has_kbd_keys = true;
                break;
            }
        }

        // BTN_TOUCH = 0x14A = 330 = bajt 41 bit 2
        drv->input_dev.abs_contact_required = key_bits_size > 41 && (key_bits[41] & (1U << 2));
    }

    // 2. Analiza osi wzglednych (subsel = INPUT_EV_REL)
    uint8_t rel_bits[128]{};
    uint8_t rel_bits_size = 0;
    if (virtio_input_query_config(
            drv->dev, VIRTIO_INPUT_CFG_EV_BITS, INPUT_EV_REL, rel_bits, &rel_bits_size, sizeof(rel_bits)) && rel_bits_size > 0) {
        const bool rel_x = (rel_bits[0] & (1U << INPUT_REL_X)) != 0;
        const bool rel_y = (rel_bits[0] & (1U << INPUT_REL_Y)) != 0;
        if (rel_x && rel_y) {
            drv->has_rel_mouse = true;
        }
    }

    // 3. Analiza osi bezwzglednych (subsel = INPUT_EV_ABS)
    uint8_t abs_bits[128]{};
    uint8_t abs_bits_size = 0;
    if (virtio_input_query_config(
            drv->dev, VIRTIO_INPUT_CFG_EV_BITS, INPUT_EV_ABS, abs_bits, &abs_bits_size, sizeof(abs_bits)) && abs_bits_size > 0) {
        const bool abs_x = (abs_bits[0] & (1U << INPUT_ABS_X)) != 0;
        const bool abs_y = (abs_bits[0] & (1U << INPUT_ABS_Y)) != 0;
        if (abs_x && abs_y) {
            drv->has_abs_tablet = true;

            // Odpytanie o granice ABS_X
            VirtioInputAbsInfo info_x{};
            uint8_t sz_x = 0;
            if (virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_ABS_INFO, INPUT_ABS_X,
                                          &info_x, &sz_x, sizeof(info_x)) && sz_x >= sizeof(info_x)) {
                if (static_cast<int32_t>(info_x.min) < static_cast<int32_t>(info_x.max)) {
                    drv->input_dev.abs_x.min = static_cast<int32_t>(info_x.min);
                    drv->input_dev.abs_x.max = static_cast<int32_t>(info_x.max);
                    drv->input_dev.abs_x.fuzz = static_cast<int32_t>(info_x.fuzz);
                    drv->input_dev.abs_x.flat = static_cast<int32_t>(info_x.flat);
                    drv->input_dev.abs_x.res = static_cast<int32_t>(info_x.res);
                } else return false;
            } else return false;
            virtio_pci_input_init_trace(drv->dev, "ABS_X range", (uint64_t(info_x.min) << 32) | info_x.max);

            // Odpytanie o granice ABS_Y
            VirtioInputAbsInfo info_y{};
            uint8_t sz_y = 0;
            if (virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_ABS_INFO, INPUT_ABS_Y,
                                          &info_y, &sz_y, sizeof(info_y)) && sz_y >= sizeof(info_y)) {
                if (static_cast<int32_t>(info_y.min) < static_cast<int32_t>(info_y.max)) {
                    drv->input_dev.abs_y.min = static_cast<int32_t>(info_y.min);
                    drv->input_dev.abs_y.max = static_cast<int32_t>(info_y.max);
                    drv->input_dev.abs_y.fuzz = static_cast<int32_t>(info_y.fuzz);
                    drv->input_dev.abs_y.flat = static_cast<int32_t>(info_y.flat);
                    drv->input_dev.abs_y.res = static_cast<int32_t>(info_y.res);
                } else return false;
            } else return false;
            virtio_pci_input_init_trace(drv->dev, "ABS_Y range", (uint64_t(info_y.min) << 32) | info_y.max);
        }
    }

#if BURSZTYNDIAGGUIRUNTIME || BURSZTYNDIAGABSMAP
    char caps[256]{};unsigned pos=0;
    auto txt=[&](const char* p){while(*p&&pos+1<sizeof(caps))caps[pos++]=*p++;};
    auto num=[&](int64_t v){if(v<0){txt("-");v=-v;}char t[24];unsigned i=0;do{t[i++]=char('0'+v%10);v/=10;}while(v);while(i&&pos+1<sizeof(caps))caps[pos++]=t[--i];};
    txt("[INPUT CAPS] rel_size=");num(rel_bits_size);txt(" rel_byte0=");num(rel_bits[0]);
    txt(" abs_size=");num(abs_bits_size);txt(" abs_byte0=");num(abs_bits[0]);
    txt(" rel_xy=");num(drv->has_rel_mouse);txt(" abs_xy=");num(drv->has_abs_tablet);
    txt(" abs_x_min=");num(drv->input_dev.abs_x.min);txt(" abs_x_max=");num(drv->input_dev.abs_x.max);
    txt(" abs_y_min=");num(drv->input_dev.abs_y.min);txt(" abs_y_max=");num(drv->input_dev.abs_y.max);txt("\n");input_klog(caps);
#endif

    if (has_kbd_keys) {
        drv->has_keyboard = true;
    }
    // Buttons alone do not establish REL_X/REL_Y capability.

    // Ustawienie flag klasyfikacji urzadzenia
    if (drv->has_keyboard)   drv->input_dev.kinds |= INPUT_DEVICE_KIND_KEYBOARD;
    if (drv->has_rel_mouse)  drv->input_dev.kinds |= INPUT_DEVICE_KIND_MOUSE;
    if (drv->has_abs_tablet) drv->input_dev.kinds |= INPUT_DEVICE_KIND_TABLET;
    return drv->input_dev.kinds != INPUT_DEVICE_KIND_UNKNOWN;
}

bool virtio_input_prepare(VirtioInputDriver* drv, VirtioPciDevice* dev) {
    if (!drv || !dev || !dev->common_cfg || dev->virtio_id != VIRTIO_ID_INPUT) {
        return false;
    }
    // Never overwrite an active instance or DMA quarantined after reset failure.
    if (drv->dev || drv->eventq || drv->dma_virt) return false;
    DiagPciAttempt diagnostic(DIAG_VIRTIO_INPUT,"Kontroler VirtIO Input","virtio-input",
        dev->bus,dev->slot,dev->func,dev->pci_vendor_id,dev->pci_device_id);

    // Zerowanie struktury sterownika
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    std::memset(drv, 0, sizeof(VirtioInputDriver));
#else
    memset(drv, 0, sizeof(VirtioInputDriver));
#endif
    drv->dev = dev;
    drv->irq_vector = -1;
    qemu_diag(dev,"[VINPUT PCI] vendor/device/virtio-id",dev->pci_vendor_id,dev->pci_device_id,dev->virtio_id);
    qemu_diag(dev,"[VINPUT transport] modern/transitional",dev->common_cfg!=nullptr,dev->pci_device_id<0x1040);
    dev->input_init_active = true;
    dev->input_activation_allowed = false;
    dev->input_init_stage = 0;
    virtio_pci_input_init_trace(dev, "begin/commoncfg", reinterpret_cast<uintptr_t>(dev->common_cfg));
    virtio_pci_input_init_trace(dev, "devicecfg address", reinterpret_cast<uintptr_t>(dev->device_cfg));
    virtio_pci_input_init_trace(dev, "devicecfg length", dev->device_cfg_len);
    input_init_state(drv, VirtioInputInitState::RESETTING);

    // 1. Reset sprzetowy kontrolera
    virtio_pci_input_init_trace(dev, "reset begin");
    if (!virtio_pci_reset(dev)) return input_init_failure(drv, "reset status never became zero");
    virtio_pci_input_init_trace(dev, "reset done");
    qemu_diag(dev,"[VINPUT status] reset",0);

    // 2. Ustawienie statusu ACKNOWLEDGE i DRIVER
    virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    input_init_state(drv, VirtioInputInitState::NEGOTIATING);
    qemu_diag(dev,"[VINPUT status] ACK/DRIVER",1,3);

    // 3. Negocjacja cech (wymog standardu VirtIO 1.x Modern)
    if (!virtio_pci_negotiate_features(dev, 0)) {
        input_klog("[VIRTIO-INPUT-ERR] Negocjacja cech VirtIO-INPUT nie powiodla sie");
        return input_init_failure(drv, "feature negotiation rejected");
    }

    qemu_diag(dev,"[VINPUT features] offered/accepted/FEATURES_OK",dev->device_features,dev->driver_features,virtio_pci_get_status(dev));
    // 4. Odpytanie przestrzeni konfiguracji o nazwe, serial, devids i mozliwosci
    uint8_t name_sz = 0;
    if (virtio_input_query_config(dev, VIRTIO_INPUT_CFG_ID_NAME, 0, drv->name, &name_sz, sizeof(drv->name) - 1)) {
        drv->name[name_sz] = '\0';
    } else {
        drv->name[0] = 'v'; drv->name[1] = 'i'; drv->name[2] = 'n'; drv->name[3] = 'p'; drv->name[4] = '\0';
    }

    VirtioInputQemuDiag::name(qemu_diag_bdf(dev),drv->name);
    uint8_t serial_sz = 0;
    if (virtio_input_query_config(dev, VIRTIO_INPUT_CFG_ID_SERIAL, 0, drv->serial, &serial_sz, sizeof(drv->serial) - 1)) {
        drv->serial[serial_sz] = '\0';
    }

    uint8_t ids_sz = 0;
    virtio_input_query_config(dev, VIRTIO_INPUT_CFG_ID_DEVIDS, 0, &drv->ids, &ids_sz, sizeof(drv->ids));

    // Kopiowanie nazwy do InputDevice
    for (size_t i = 0; i < sizeof(drv->input_dev.name) - 1 && drv->name[i]; ++i) {
        drv->input_dev.name[i] = drv->name[i];
        drv->input_dev.name[i + 1] = '\0';
    }

    // Klasyfikacja urzadzenia z bitmapy EV_BITS
    virtio_pci_input_init_trace(dev, "role detection begin");
    if (!virtio_input_classify_device(drv)) return input_init_failure(drv, "unsupported input capabilities or invalid ABSINFO");
    // Pointer mode follows the advertised axes, independently for each BDF.
    if (drv->has_abs_tablet)
        drv->input_dev.pointer_mode = drv->has_rel_mouse ?
            INPUT_POINTER_RELATIVE_AND_ABSOLUTE : INPUT_POINTER_ABSOLUTE;
    else if (drv->has_rel_mouse)
        drv->input_dev.pointer_mode = INPUT_POINTER_RELATIVE;
    else
        drv->input_dev.pointer_mode = INPUT_POINTER_UNSPECIFIED;
    virtio_pci_input_init_trace(dev, "role detection done/kinds", drv->input_dev.kinds);

    qemu_diag(dev,"[VINPUT classification] keyboard/relative/absolute",drv->has_keyboard,drv->has_rel_mouse,drv->has_abs_tablet);
    // 5. Utworzenie Split VirtQueue 0 (eventq)
    input_init_state(drv, VirtioInputInitState::QUEUE_SETUP);
    virtio_pci_input_init_trace(dev, "queue count read begin");
    const uint16_t queue_count = virtio_pci_get_num_queues(dev);
    virtio_pci_input_init_trace(dev, "queue count read done", queue_count);
    qemu_diag(dev,"[VINPUT queue count]",queue_count);
    if (queue_count < 2) return input_init_failure(drv, "missing input queues");
    // statusq (1) is unused: this driver sends no LED/status feedback.
    virtio_pci_input_init_trace(dev, "eventq create begin");
    drv->eventq = virtqueue_create(dev, 0);
    virtio_pci_input_init_trace(dev, "eventq create done", drv->eventq ? drv->eventq->queue_size : 0);
    if (!drv->eventq) {
        input_klog("[VIRTIO-INPUT-ERR] Nie mozna utworzyc eventq (queue 0)");
        return input_init_failure(drv, "eventq setup rejected");
    }

    const uint16_t qsize = virtqueue_get_size(drv->eventq);
    size_t num_bufs = (qsize < VIRTIO_INPUT_NUM_EVENT_BUFS) ? qsize : VIRTIO_INPUT_NUM_EVENT_BUFS;
    if (num_bufs == 0) {
        return input_init_failure(drv, "empty eventq");
    }
    drv->num_event_bufs = num_bufs;

    // 6. Alokacja stabilnego bufora DMA na pre-postowane deskryptory zdarzen
    const size_t dma_bytes = num_bufs * sizeof(VirtioInputEvent);
    virtio_pci_input_init_trace(dev, "event DMA allocate begin", dma_bytes);
    if (!input_dma_alloc(dma_bytes, 4096, &drv->dma_virt, &drv->dma_phys, &drv->dma_pages)) {
        return input_init_failure(drv, "event DMA allocation failed");
    }
    virtio_pci_input_init_trace(dev, "event DMA allocate done/physical", drv->dma_phys);
    drv->dma_events = reinterpret_cast<VirtioInputEvent*>(drv->dma_virt);

    // 7. Wypelnienie eventq poczatkowa pula buforow (device-writable)
    virtio_pci_input_init_trace(dev, "buffers post begin", num_bufs);
    for (size_t i = 0; i < num_bufs; ++i) {
        if (!input_post_buffer(drv, i)) {
            input_klog("[VIRTIO-INPUT-ERR] Blad pre-postowania bufora eventq");
            return input_init_failure(drv, "event buffer post rejected");
        }
    }

    input_init_state(drv, VirtioInputInitState::BUFFERS_POSTED);
    virtio_pci_input_init_trace(dev, "buffers post done/avail", drv->eventq->avail->idx);
    virtio_pci_input_init_trace(dev, "buffers used", drv->eventq->used->idx);

    // 8. Konfiguracja przerwan MSI-X dla queue 0 (jesli sprzet obsluguje)
    drv->use_msix = false;
    drv->irq_vector = -1;
    if (dev->has_msix && dev->msix_cap.table_size >= 1) {
        virtio_pci_input_init_trace(dev, "IDT allocate begin");
        const int vec = idt_alokuj_wektor_irq();
        virtio_pci_input_init_trace(dev, "IDT allocate done", vec);
        if (vec >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY) {
            virtio_pci_input_init_trace(dev, "IDT register begin");
            if (idt_zarejestruj_irq(static_cast<uint8_t>(vec), virtio_input_msix_handler, drv)) {
                virtio_pci_input_init_trace(dev, "IDT register done");
                virtio_pci_input_init_trace(dev, "APIC destination read begin");
                const uint8_t lapic = apic_pobierz_lapic_id();
                virtio_pci_input_init_trace(dev, "APIC destination read done", lapic);
                virtio_pci_input_init_trace(dev, "MSI-X table program begin", reinterpret_cast<uintptr_t>(dev->msix_table));
                if (virtio_pci_program_msix_entry(dev, 0, lapic, static_cast<uint8_t>(vec), true)) {
                    virtio_pci_input_init_trace(dev, "MSI-X table program done");
                    virtio_pci_input_init_trace(dev, "queue MSI-X assign begin");
                    if (virtio_pci_set_queue_msix_vector_checked(dev, 0, 0)) {
                        virtio_pci_input_init_trace(dev, "queue MSI-X assign done", 0);
                        virtio_pci_input_init_trace(dev, "PCI MSI-X enable begin");
                        if (pci_msix_enable(dev->bus, dev->slot, dev->func, &dev->msix_cap)) {
                            virtio_pci_input_init_trace(dev, "PCI MSI-X enable done");
                            dev->msix_enabled = true;
                            drv->use_msix = true;
                            drv->irq_vector = vec;
                        }
                    }
                }
            }
            if (!drv->use_msix) {
                virtio_pci_mask_msix_entry(dev, 0);
                pci_msix_disable(dev->bus, dev->slot, dev->func, &dev->msix_cap);
                dev->msix_enabled = false;
                virtio_pci_set_queue_msix_vector_checked(dev, 0, VIRTIO_MSI_NO_VECTOR);
                idt_wyrejestruj_irq(static_cast<uint8_t>(vec));
                idt_zwolnij_wektor_irq(static_cast<uint8_t>(vec));
            }
        }
    }

    if (drv->use_msix) {
        virtio_pci_input_init_trace(dev, "config MSI-X disable begin", VIRTIO_MSI_NO_VECTOR);
        virtio_pci_set_config_msix_vector(dev, VIRTIO_MSI_NO_VECTOR);
        virtio_pci_input_init_trace(dev, "config MSI-X disable store done");
        virtio_pci_input_init_trace(dev, "config MSI-X readback begin");
        const uint16_t config_vector = virtio_pci_get_config_msix_vector(dev);
        virtio_pci_input_init_trace(dev, "config MSI-X readback done", config_vector);
        if (config_vector != VIRTIO_MSI_NO_VECTOR)
            return input_init_failure(drv, "config MSI-X NO_VECTOR rejected");
    }
    if (!drv->use_msix) {
        // There is no VirtIO input INTx handler. A rejected MSI-X assignment
        // must not turn an immediate tablet report into an unacknowledged
        // level interrupt. Poll the used ring with INTx disabled instead.
        virtio_pci_input_init_trace(dev, "polling INTx disable begin");
        if (!virtio_pci_disable_intx(dev))
            return input_init_failure(drv, "cannot suppress polling-mode INTx");
        if (dev->has_msi) pci_msi_disable(dev->bus, dev->slot, dev->func, &dev->msi_cap);
        virtio_pci_input_init_trace(dev, "polling INTx disable done");
    }
    input_init_state(drv, VirtioInputInitState::INTERRUPTS_READY);

    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const uint8_t prepared_status = virtio_pci_get_status(dev);
    if (prepared_status != (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK))
        return input_init_failure(drv, "PREPARED status must remain 0x0B");
    qemu_diag(dev,"[VINPUT queue ready] selected/size/posted",drv->eventq->queue_index,drv->eventq->queue_size,drv->eventq->avail->idx);
    const auto* q=drv->eventq;
    const uint64_t base=q->dma_physical;const auto virt=reinterpret_cast<uintptr_t>(q->dma_virtual);
    qemu_diag(dev,"[VINPUT ring physical] desc/avail/used",base+reinterpret_cast<uintptr_t>(q->desc)-virt,base+reinterpret_cast<uintptr_t>(q->avail)-virt,base+reinterpret_cast<uintptr_t>(q->used)-virt);
    qemu_diag(dev,"[VINPUT interrupts] MSIX/MSI/vector",drv->use_msix,dev->msi_enabled,drv->irq_vector);
    qemu_diag(dev,"[VINPUT status] PREPARED",prepared_status);
    input_init_state(drv, VirtioInputInitState::PREPARED);
    virtio_pci_input_init_trace(dev, "PREPARED/status (no DRIVER_OK)", prepared_status);
    diagnostic.ready(DIAG_INACTIVE);
    return true;
}

bool virtio_input_init(VirtioInputDriver* drv, VirtioPciDevice* dev) {
    return virtio_input_prepare(drv, dev);
}

bool virtio_input_activate(VirtioInputDriver* drv) {
    if (!drv || !__atomic_load_n(&s_late_activation_allowed, __ATOMIC_ACQUIRE)) return false;
    auto state = __atomic_load_n(&drv->init_state, __ATOMIC_ACQUIRE);
    if (state == VirtioInputInitState::ACTIVE)
        return __atomic_load_n(&drv->initialized, __ATOMIC_ACQUIRE);
    if (state != VirtioInputInitState::PREPARED || !drv->dev || !drv->eventq ||
        __atomic_load_n(&drv->reset_requested, __ATOMIC_ACQUIRE)) return false;
    if (!__atomic_compare_exchange_n(&drv->init_state, &state, VirtioInputInitState::ACTIVATING,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    auto* dev = drv->dev;
    DiagPciAttempt diagnostic(DIAG_VIRTIO_INPUT,"Kontroler VirtIO Input","virtio-input",
        dev->bus,dev->slot,dev->func,dev->pci_vendor_id,dev->pci_device_id);
    virtio_pci_input_init_trace(dev, "late activation state before", static_cast<uint8_t>(state));
    // Resume the prepared transport. No reset, allocation, queue rebuild or
    // feature negotiation is allowed in this phase.
    const uint8_t expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    if (virtio_pci_get_status(dev) != expected)
        return input_init_failure(drv, "prepared status changed before late activation");

#if BURSZTYNDIAGVIRTIOINPUTINIT && !defined(BURSZTYN_VIRTIO_HOST_TEST)
    if (!input_activation_snapshot(drv))
        return input_init_failure(drv, "activation snapshot: lock/mask/DMA ownership unsafe");
#endif

    // Publish ordinary DMA memory before activating the device model. Volatile
    // MMIO alone is not a compiler barrier for descriptor/ring writes. Per-post
    // release fences already precede avail.idx; this closes the activation edge.
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    extern void host_input_activation_barrier() __attribute__((weak));
    if (host_input_activation_barrier) host_input_activation_barrier();
#endif

    // 9. Aktywacja urzadzenia (DRIVER_OK tylko po pelnym przygotowaniu kolejki eventq)
    virtio_pci_input_init_trace(dev, "status before DRIVER_OK read begin");
    const uint8_t cur_st = virtio_pci_get_status(dev);
    virtio_pci_input_init_trace(dev, "status before DRIVER_OK read done", cur_st);
    dev->input_activation_allowed = true;
    virtio_pci_set_status(dev, static_cast<uint8_t>(cur_st | VIRTIO_STATUS_DRIVER_OK));
    dev->input_activation_allowed = false;
    qemu_diag(dev,"[VINPUT DRIVER_OK store]",cur_st|VIRTIO_STATUS_DRIVER_OK);
    virtio_pci_input_init_trace(dev, "DRIVER_OK readback begin");
    const uint8_t ready_status = virtio_pci_get_status(dev);
    virtio_pci_input_init_trace(dev, "DRIVER_OK readback done", ready_status);
    qemu_diag(dev,"[VINPUT DRIVER_OK readback]",ready_status);
    if ((ready_status & VIRTIO_STATUS_DRIVER_OK) == 0 ||
        (ready_status & (VIRTIO_STATUS_FAILED | VIRTIO_STATUS_DEVICE_NEEDS_RESET)) != 0) {
        input_klog("[VIRTIO-INPUT-ERR] Blad ustawienia DRIVER_OK");
        return input_init_failure(drv, "DRIVER_OK rejected");
    }
    input_init_state(drv, VirtioInputInitState::DRIVER_READY);

    // 10. Rejestracja w dyspozytorze InputDevice
    drv->input_dev.bus_type = INPUT_BUS_VIRTIO;
    drv->input_dev.device_id = drv->dev ? ((static_cast<uint32_t>(drv->dev->bus) << 16) | (static_cast<uint32_t>(drv->dev->slot) << 8) | drv->dev->func) : 1;
    drv->input_dev.driver_data = drv;
    drv->input_dev.poll_fn = virtio_input_device_poll;
    asm volatile("" ::: "memory");
    drv->registration_used=drv->eventq->used->idx;
    input_dma_acquire();
    drv->pre_registration_events=uint16_t(drv->registration_used-drv->eventq->last_used_idx);
    virtio_pci_input_init_trace(dev, "InputDevice register begin");
    if (!input_register_device(&drv->input_dev)) return input_init_failure(drv, "input registry rejected device");
    virtio_pci_input_init_trace(dev, "InputDevice register done");
    input_init_state(drv, VirtioInputInitState::REGISTERED);
    qemu_diag(dev,"[VINPUT registered] state/active",unsigned(drv->input_dev.pointer_state),input_pointer_is_active(&drv->input_dev));
    __atomic_store_n(&drv->initialized, true, __ATOMIC_RELEASE);
    virtio_pci_input_init_trace(dev, "InputDevice readiness published", 1);
    virtio_pci_input_init_trace(dev, "MSI-X unmask begin", drv->use_msix);
    if (drv->use_msix) virtio_pci_unmask_msix_entry(dev, 0);
    virtio_pci_input_init_trace(dev, "MSI-X unmask done");
    virtio_pci_input_init_trace(dev, "first usedidx after activation", drv->eventq->used->idx);
    (void)virtio_input_poll_events(drv, VIRTIO_INPUT_NUM_EVENT_BUFS);
    if (!drv->initialized || drv->has_error)
        return input_init_failure(drv, "activation completion validation failed");
    // No available-buffer notification is permitted before DRIVER_OK.
    virtio_pci_input_init_trace(dev, "initial notify begin");
    if (!virtqueue_kick(drv->eventq, true))
        return input_init_failure(drv, "initial notify failed");
    virtio_pci_input_init_trace(dev, "initial notify done");
    input_init_state(drv, VirtioInputInitState::ACTIVE);
    virtio_pci_input_init_trace(dev, "late activation state after", static_cast<uint8_t>(drv->init_state));
    virtio_pci_input_init_trace(dev, "complete");
#if BURSZTYNDIAGVIRTIOINPUTINIT
    input_activation_trace.armed = 0;
#endif
    dev->input_init_active = false;

    input_klog("[VIRTIO-INPUT] Prawidlowo zainicjalizowano urzadzenie:");
    if (drv->has_keyboard)   input_klog("  - Rola: Klawiatura (INPUT_DEVICE_KIND_KEYBOARD)");
    if (drv->has_rel_mouse)  input_klog("  - Rola: Mysz wzgledna (INPUT_DEVICE_KIND_MOUSE)");
    if (drv->has_abs_tablet) input_klog("  - Rola: Tablet bezwzgledny (INPUT_DEVICE_KIND_TABLET)");
    if (drv->use_msix) {
        input_klog("  - Tryb przerwan: MSI-X");
    } else {
        input_klog("  - Tryb przerwan: Polling fallback");
    }
    input_klog("  - Podsystem: Zarejestrowano w InputDevice");

    diagnostic.ready();
    return true;
}

bool virtio_input_commit_geometry(VirtioInputDriver* drv,int width,int height,uint64_t generation) {
    if(!drv || __atomic_exchange_n(&drv->service_lock,1U,__ATOMIC_ACQUIRE))return false;
    if(!drv->initialized || !drv->eventq || drv->reset_requested) {
        __atomic_store_n(&drv->service_lock,0U,__ATOMIC_RELEASE);return false;
    }
    // The cutoff belongs to the display commit, not a later device poll.
    // Preserve existing startup/SYN_DROPPED admission and partial-report rules.
    const uint16_t published=drv->eventq->used->idx;
    input_dma_acquire();
    if(drv->input_dev.report_valid)drv->discard_until_syn=true;
    input_discard_pointer_report(&drv->input_dev);
    drv->stale_until_used=published;
    drv->stale_completions=uint16_t(published-drv->eventq->last_used_idx)!=0;
    drv->pointer_width=width;drv->pointer_height=height;drv->pointer_generation=generation;
    // Splash may still fade after the desktop handshake. Pointer admission
    // follows desktop readiness + geometry only; presentation owns the overlay.
    drv->pointer_interactive=input_pointer_geometry_ready() &&
        __atomic_load_n(&s_desktop_ready,__ATOMIC_ACQUIRE);
    __atomic_store_n(&drv->service_lock,0U,__ATOMIC_RELEASE);
    return true;
}

size_t virtio_input_poll_events(VirtioInputDriver* drv, size_t max_events) {
    if (!drv || __atomic_exchange_n(&drv->service_lock, 1U, __ATOMIC_ACQUIRE)) return 0;
    if (!__atomic_load_n(&drv->initialized, __ATOMIC_ACQUIRE) || !drv->eventq ||
        __atomic_load_n(&drv->reset_requested, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&drv->service_lock, 0U, __ATOMIC_RELEASE);
        if (__atomic_load_n(&drv->reset_requested, __ATOMIC_ACQUIRE)) virtio_input_reset(drv);
        return 0;
    }
    VirtQueue* q = drv->eventq;
    (void)__atomic_exchange_n(&drv->has_pending_events, false, __ATOMIC_ACQ_REL);
    q->avail->flags = VRING_AVAIL_F_NO_INTERRUPT;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // Observe publication, THEN acquire the ring entries and writable buffers.
    const uint16_t published = q->used->idx;
    input_dma_acquire();
    const uint16_t pending = uint16_t(published - q->last_used_idx);
    const auto bdf=qemu_diag_bdf(drv->dev);
    if(__atomic_load_n(&drv->irq_count,__ATOMIC_RELAXED))VirtioInputQemuDiag::once(bdf,0,"[VINPUT first IRQ]",drv->irq_count);
    if(pending)VirtioInputQemuDiag::once(bdf,1,"[VINPUT first used ring change]",published,q->last_used_idx);

    const int width=grafika_pobierz_szerokosc(),height=grafika_pobierz_wysokosc();
    const auto generation=input_geometry_generation();
    const bool geometry=input_pointer_geometry_ready();
    // Desktop handshake unlocks the pointer; splash fade must not discard motion.
    const bool interactive=geometry && __atomic_load_n(&s_desktop_ready,__ATOMIC_ACQUIRE);
    if(interactive!=drv->pointer_interactive || width!=drv->pointer_width ||
       height!=drv->pointer_height || generation!=drv->pointer_generation) {
        // No event timestamp exists on the wire. The first consumer turn at a
        // display boundary snapshots a precise used-index cutoff. Completions
        // already published at that cutoff are old, including split reports.
        if(drv->input_dev.report_valid)drv->discard_until_syn=true;
        input_discard_pointer_report(&drv->input_dev);
        drv->stale_until_used=published;drv->stale_completions=pending!=0;
        VirtioInputForensic::transition(bdf,input_observed_ns(),"pointer admission boundary",interactive,published);
        drv->pointer_interactive=interactive;drv->pointer_width=width;drv->pointer_height=height;
        drv->pointer_generation=generation;
    }

    uint64_t seen_buffers = 0;
    bool valid = pending <= drv->num_event_bufs && pending <= q->outstanding_heads;
    if (!valid) input_fail_closed(drv, "used index exceeds posted capacity");
    for (uint16_t i = 0; valid && i < pending; ++i) {
        const auto ring = uint16_t(q->last_used_idx + i) & (q->queue_size - 1U);
        const uint32_t id = q->used->ring[ring].id;
        const uint32_t len = q->used->ring[ring].len;
        if (id >= q->queue_size || !q->desc_state[id].is_allocated ||
            !q->desc_state[id].is_chain_head || q->desc_state[id].chain_len != 1) {
            input_fail_closed(drv, "invalid used descriptor", int(id)); valid = false; break;
        }
        const auto& state=q->desc_state[id];
        const auto idx = reinterpret_cast<uintptr_t>(state.cookie);
        // Larger buffers are legal. This driver posts eight bytes, so a
        // completion larger than that offered capacity is malformed, not an
        // input-event ABI extension. Only copy the required eight bytes.
        if (idx >= drv->num_event_bufs || drv->buffer_head[idx] != id ||
            drv->buffer_state[idx] != VirtioInputBufferState::POSTED_TO_DEVICE ||
            (seen_buffers & (uint64_t(1) << idx)) || len < sizeof(VirtioInputEvent) ||
            len>state.buffer_len || state.buffer_len<sizeof(VirtioInputEvent) ||
            state.buffer_addr!=drv->dma_phys+idx*sizeof(VirtioInputEvent) ||
            len>(drv->num_event_bufs-idx)*sizeof(VirtioInputEvent)) {
            input_fail_closed(drv, "duplicate ownership or invalid event length/address", int(id), idx);
            valid = false; break;
        }
        seen_buffers |= uint64_t(1) << idx;
    }

    const size_t budget = max_events && max_events < drv->num_event_bufs ? max_events : drv->num_event_bufs;
    const size_t count = pending < budget ? pending : budget;
    struct CopiedEvent { uint8_t bytes[8];uint16_t sequence,head;uint32_t len;size_t buffer; };
    CopiedEvent events[VIRTIO_INPUT_NUM_EVENT_BUFS]{};
    size_t taken = 0;
    for (; valid && taken < count; ++taken) {
        auto& e=events[taken];e.sequence=q->last_used_idx;
        void* cookie = nullptr;
        const int head = virtqueue_pop_used(q, &cookie, &e.len);
        if (head < 0) { input_fail_closed(drv, "corrupt descriptor chain", head); valid = false; break; }
        e.head=uint16_t(head);e.buffer=reinterpret_cast<uintptr_t>(cookie);
        drv->buffer_state[e.buffer] = VirtioInputBufferState::RETURNED_IN_USED_RING;
        input_dma_acquire();
        const auto* dma=reinterpret_cast<volatile const uint8_t*>(drv->dma_virt)+e.buffer*8;
        for(unsigned byte=0;byte<8;++byte)e.bytes[byte]=dma[byte];
        asm volatile("" ::: "memory");
        drv->buffer_state[e.buffer] = VirtioInputBufferState::BEING_PROCESSED;
    }
    // Private copies precede EVERY repost. An immediate device completion on
    // a reused descriptor cannot overwrite the event being dispatched below.
    for (size_t i = 0; valid && i < taken; ++i) {
        if (!input_post_buffer(drv, events[i].buffer)) {
            input_fail_closed(drv, "buffer repost failed", -1, events[i].buffer); valid = false;
        }
    }
    if (valid && taken) (void)virtqueue_kick(q, false);

    size_t delivered = 0;
    for (size_t i = 0; valid && i < taken &&
         !__atomic_load_n(&drv->reset_requested, __ATOMIC_ACQUIRE); ++i) {
        const auto& copied=events[i];
        const InputEvent ev{input_wire16(copied.bytes),input_wire16(copied.bytes+2),input_wire32(copied.bytes+4)};
        const int32_t value=input_signed_value(ev.value);
        auto& device=drv->input_dev;
        const bool pointer=(device.kinds&(INPUT_DEVICE_KIND_MOUSE|INPUT_DEVICE_KIND_TABLET))!=0;
        const bool pointer_event=pointer && (ev.type==INPUT_EV_REL || ev.type==INPUT_EV_ABS ||
            ev.type==INPUT_EV_SYN || (ev.type==INPUT_EV_KEY && ev.code>=INPUT_BTN_MISC));
        const bool stale=drv->stale_completions;
        if(stale && uint16_t(copied.sequence+1)==drv->stale_until_used)drv->stale_completions=false;
        const bool sync=ev.type==INPUT_EV_SYN && ev.code==INPUT_SYN_REPORT;
        VirtioInputForensic::Event trace{};
        trace.ns=input_observed_ns();trace.bdf=bdf;trace.queue=q->queue_index;
        trace.used_idx=published;trace.used_element=copied.sequence&(q->queue_size-1U);
        trace.descriptor=copied.head;trace.used_len=copied.len;
        for(unsigned byte=0;byte<8;++byte)trace.raw[byte]=copied.bytes[byte];
        trace.type=ev.type;trace.code=ev.code;trace.value=value;
        trace.role=uint8_t(device.kinds);trace.lifecycle=uint8_t(drv->init_state);
        trace.mode=device.pointer_mode;trace.kinds=device.kinds;
        trace.readiness=uint8_t(device.registered)|(uint8_t(geometry)<<1)|(uint8_t(interactive)<<2);
        trace.proven=device.pointer_state;trace.before_x=device.rel_x;trace.before_y=device.rel_y;
        trace.cursor_x=grafika_pobierz_mysz_x();trace.cursor_y=grafika_pobierz_mysz_y();trace.width=width;trace.height=height;
        const bool before_registration=drv->pre_registration_events!=0;
        if(before_registration)--drv->pre_registration_events;
        trace.phases=(before_registration?1:0)|(!input_pointer_is_active(&device)?2:0)|
            (!geometry?4:0)|(!interactive || stale?8:0);
        trace.action=ev.type!=INPUT_EV_SYN?VirtioInputForensic::None:sync?VirtioInputForensic::Report:
            ev.code==INPUT_SYN_DROPPED?VirtioInputForensic::Dropped:VirtioInputForensic::Unsupported;
        trace.outcome=VirtioInputForensic::Retained;

        if(pointer_event && ev.type==INPUT_EV_SYN && ev.code==INPUT_SYN_DROPPED) {
            input_discard_pointer_report(&device);drv->discard_until_syn=true;
            trace.outcome=VirtioInputForensic::Discarded;
        } else if(pointer_event && (!interactive || stale || drv->discard_until_syn)) {
            input_discard_pointer_report(&device);
            if(ev.type==INPUT_EV_REL || ev.type==INPUT_EV_ABS || ev.type==INPUT_EV_KEY)drv->discard_until_syn=true;
            if(drv->discard_until_syn)trace.action=VirtioInputForensic::Resync;
            if(sync)drv->discard_until_syn=false;
            trace.outcome=VirtioInputForensic::Discarded;
        } else {
            const bool activity=device.rel_x || device.rel_y || device.rel_wheel || device.rel_hwheel ||
                device.has_abs_x || device.has_abs_y || device.buttons_state!=device.published_buttons;
            input_dispatch_event(&device,&ev);
            if(sync) {
                trace.outcome=pointer && activity && input_pointer_is_active(&device)?VirtioInputForensic::Published:VirtioInputForensic::Empty;
                if(trace.outcome==VirtioInputForensic::Published){trace.dx=trace.before_x;trace.dy=trace.before_y;}
            }
        }
        trace.after_x=device.rel_x;trace.after_y=device.rel_y;
        trace.new_x=grafika_pobierz_mysz_x();trace.new_y=grafika_pobierz_mysz_y();
        auto magnitude=[](int32_t n){return n<0?-int64_t(n):int64_t(n);};
        trace.anomaly=(ev.type==INPUT_EV_REL && ((ev.code==INPUT_REL_X && width>0 && magnitude(value)>width) ||
            (ev.code==INPUT_REL_Y && height>0 && magnitude(value)>height))) ||
            (width>0 && magnitude(trace.dx)>width) || (height>0 && magnitude(trace.dy)>height);
        VirtioInputForensic::event(trace); // Observation never changes admission.
#if BURSZTYNDIAGVIRTIOINPUTINIT
        if(!drv->first_event_logged) {
            drv->first_event_logged=true;input_activation_trace.bdf=bdf;
            input_activation_log("first input event type/code",(uint64_t(ev.type)<<32)|ev.code);
            input_activation_log("first input event value",ev.value);
        }
#endif

        if ((ev.type == INPUT_EV_ABS || ev.type == INPUT_EV_REL) && ev.code <= 1) kursor_diag_input();
        if(sync)++drv->syn_sequence;
        VirtioInputQemuDiag::once(bdf,2,"[VINPUT first event] type/code/value",ev.type,ev.code,ev.value);
        if(ev.type==INPUT_EV_REL)VirtioInputQemuDiag::once(bdf,3,"[VINPUT first EVREL] code/value",ev.code,ev.value);
        if(ev.type==INPUT_EV_SYN)VirtioInputQemuDiag::once(bdf,4,"[VINPUT first SYN] code/value",ev.code,ev.value);
        ++delivered;++drv->total_events_processed;
    }
    if (valid) {
        q->avail->flags = 0;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (virtqueue_has_used(q)) __atomic_store_n(&drv->has_pending_events, true, __ATOMIC_RELEASE);
        if(interactive && !drv->stale_completions && !drv->discard_until_syn)
            VirtioInputForensic::interactive(bdf,input_observed_ns());
    }
    __atomic_store_n(&drv->service_lock, 0U, __ATOMIC_RELEASE);
    if (__atomic_load_n(&drv->reset_requested, __ATOMIC_ACQUIRE)) virtio_input_reset(drv);
    return delivered;
}

bool virtio_input_reset(VirtioInputDriver* drv) {
    if (!drv) return false;
    __atomic_store_n(&drv->reset_requested, true, __ATOMIC_RELEASE);
    if (__atomic_exchange_n(&drv->service_lock, 1U, __ATOMIC_ACQUIRE)) return false;
    __atomic_store_n(&drv->init_state, VirtioInputInitState::RESETTING, __ATOMIC_RELEASE);
    __atomic_store_n(&drv->initialized, false, __ATOMIC_RELEASE);
    if(drv->dev)VirtioInputForensic::transition(qemu_diag_bdf(drv->dev),input_observed_ns(),"reset",drv->total_events_processed);
    input_discard_pointer_report(&drv->input_dev);
    drv->pointer_interactive=drv->discard_until_syn=drv->stale_completions=false;

    // 1. Wyrejestrowanie urzadzenia wejsciowego z dyspozytora
    input_unregister_device(&drv->input_dev);

    // 2. Zamaskowanie i wylaczenie MSI-X oraz zwolnienie wektora IDT
    if (drv->use_msix && drv->dev) {
        virtio_pci_mask_msix_entry(drv->dev, 0);
        pci_msix_disable(drv->dev->bus, drv->dev->slot, drv->dev->func, &drv->dev->msix_cap);
        drv->dev->msix_enabled = false;
        virtio_pci_set_queue_msix_vector_checked(drv->dev, 0, VIRTIO_MSI_NO_VECTOR);
        virtio_pci_set_config_msix_vector(drv->dev, VIRTIO_MSI_NO_VECTOR);
        if (drv->irq_vector >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY) {
            idt_wyrejestruj_irq(static_cast<uint8_t>(drv->irq_vector));
            idt_zwolnij_wektor_irq(static_cast<uint8_t>(drv->irq_vector));
        }
        drv->use_msix = false;
        drv->irq_vector = -1;
    }

    // 3. Reset urzadzenia PCI z potwierdzeniem monotonicznym (status == 0)
    bool reset_ok = !drv->dev && !drv->eventq && !drv->dma_virt;
    if (drv->dev) {
        drv->dev->input_activation_allowed = false;
        reset_ok = virtio_pci_reset(drv->dev);
        drv->dev->input_init_active = false;
    }

    // 4. Zwalnianie kolejek i pamieci DMA tylko w razie potwierdzonego resetu
    if (reset_ok) {
        if (drv->eventq) {
            virtqueue_destroy(drv->eventq);
            drv->eventq = nullptr;
        }
        if (drv->statusq) {
            virtqueue_destroy(drv->statusq);
            drv->statusq = nullptr;
        }
        if (drv->dma_virt) {
            input_dma_free(drv->dma_virt, drv->dma_phys, drv->dma_pages);
            drv->dma_virt = nullptr;
            drv->dma_phys = 0;
            drv->dma_pages = 0;
            drv->dma_events = nullptr;
        }
        drv->num_event_bufs = 0;
        for (auto& state : drv->buffer_state) state = VirtioInputBufferState::FREE;
        drv->dev = nullptr;
        __atomic_store_n(&drv->init_state, VirtioInputInitState::UNINITIALIZED, __ATOMIC_RELEASE);
        __atomic_store_n(&drv->reset_requested, false, __ATOMIC_RELEASE);
    } else {
        __atomic_store_n(&drv->init_state, VirtioInputInitState::FAILED, __ATOMIC_RELEASE);
        input_klog("[VIRTIO-INPUT-WARN] Reset kontrolera nie powiodl sie; bufor DMA w kwarantannie.");
    }

    __atomic_store_n(&drv->has_pending_events, false, __ATOMIC_RELEASE);
    __atomic_store_n(&drv->service_lock, 0U, __ATOMIC_RELEASE);
    return reset_ok;
}

void virtio_input_reset_all(void) {
    for (size_t i = 0; i < s_driver_count; ++i) {
        if (s_drivers[i].initialized || s_drivers[i].dev != nullptr) {
            virtio_input_reset(&s_drivers[i]);
        }
    }
    bool retained = false;
    for (size_t i = 0; i < s_driver_count; ++i) if (s_drivers[i].dev) retained = true;
    if (!retained) {
        s_driver_count = 0;
        __atomic_store_n(&s_late_activation_allowed, false, __ATOMIC_RELEASE);
    }
}

void virtio_input_quiesce_for_reboot(void) {
    for (size_t i = 0; i < s_driver_count; ++i) {
        auto* drv = &s_drivers[i];
        __atomic_store_n(&drv->reset_requested, true, __ATOMIC_RELEASE);
        __atomic_store_n(&drv->initialized, false, __ATOMIC_RELEASE);
        if (drv->dev && drv->use_msix) virtio_pci_mask_msix_entry(drv->dev, 0);
        // No queue memory is freed during reboot handoff. An interrupted idle
        // service can safely unwind; input IRQs cannot flood the GPU quiesce.
    }
}

uint32_t virtio_input_probe_and_prepare(void) {
    const uint32_t count = virtio_pci_liczba_urzadzen();
    uint32_t initialized = 0;

    for (uint32_t i = 0; i < count; ++i) {
        VirtioPciDevice* dev = virtio_pci_pobierz_urzadzenie(i);
        if (dev && dev->virtio_id == VIRTIO_ID_INPUT) {
            bool existing = false;
            for (size_t j = 0; j < s_driver_count; ++j) {
                if (s_drivers[j].dev == dev) {
                    existing = true;
                    const auto state = __atomic_load_n(&s_drivers[j].init_state, __ATOMIC_ACQUIRE);
                    if (state == VirtioInputInitState::PREPARED || state == VirtioInputInitState::ACTIVE) ++initialized;
                    break;
                }
            }
            if (existing) continue;
            if (s_driver_count < VIRTIO_INPUT_MAX_DRIVERS) {
                VirtioInputDriver* drv = &s_drivers[s_driver_count];
                if (virtio_input_prepare(drv, dev)) {
                    s_driver_count++;
                    initialized++;
                } else if (drv->dev) {
                    // Keep a quarantined/reset-failed slot reachable.
                    s_driver_count++;
                }
            }
        }
    }
    return initialized;
}

uint32_t virtio_input_probe_and_init(void) {
    return virtio_input_probe_and_prepare();
}

static uint32_t input_activate_batch(VirtioInputDriver* const* drivers, size_t count) {
    // Kernel startup calls this barrier only after filesystem/service/display
    // bootstrap and before creating Ring3 processes, in every boot mode.
    __atomic_store_n(&s_late_activation_allowed, true, __ATOMIC_RELEASE);
    uint32_t active = 0;
    for (size_t i = 0; i < count; ++i) {
        if (virtio_input_activate(drivers[i])) ++active;
    }
    return active;
}

uint32_t virtio_input_activate_prepared(void) {
    VirtioInputDriver* drivers[VIRTIO_INPUT_MAX_DRIVERS]{};
    for (size_t i = 0; i < s_driver_count; ++i) drivers[i] = &s_drivers[i];
    return input_activate_batch(drivers, s_driver_count);
}

#ifdef BURSZTYN_VIRTIO_HOST_TEST
// Same production pass, with fixture-owned stable driver instances.
uint32_t virtio_input_host_activate_prepared(VirtioInputDriver* const* drivers, size_t count) {
    return input_activate_batch(drivers, count);
}
#endif

size_t virtio_input_get_count(void) {
    return s_driver_count;
}

VirtioInputDriver* virtio_input_get_driver(size_t index) {
    if (index >= s_driver_count) return nullptr;
    return &s_drivers[index];
}

#if BURSZTYNDIAGABSMAP
extern "C" void input_abs_map_diag_obsluz();
#endif
void virtio_input_obsluz(void) {
    VirtioInputQemuDiag::dump();
    for (size_t i = 0; i < s_driver_count; ++i) {
        if (s_drivers[i].initialized) {
            virtio_input_poll_events(&s_drivers[i], 64);
        }
    }
    VirtioInputForensic::dump();
#if BURSZTYNDIAGABSMAP
    input_abs_map_diag_obsluz();
#endif
}

void virtio_input_set_desktop_ready(bool ready) {
    __atomic_store_n(&s_desktop_ready,ready,__ATOMIC_RELEASE);
}

bool virtio_input_stress_test(VirtioInputDriver* drv) {
    if (!drv || !drv->initialized || !drv->dev || !drv->eventq) {
        return false;
    }

    input_klog("[VIRTIO-INPUT-STRESS] Rozpoczynam test obciazeniowy VirtIO-INPUT...");

    // 1. Weryfikacja zapytan konfiguracyjnych
    uint8_t name[64]{};
    uint8_t nsz = 0;
    if (!virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_ID_NAME, 0, name, &nsz, sizeof(name) - 1)) {
        input_klog("[VIRTIO-INPUT-STRESS] Blad odpytania CFG_ID_NAME");
        return false;
    }

    // 2. Weryfikacja zapytan EV_BITS
    uint8_t evs[32]{};
    uint8_t evsz = 0;
    const uint8_t query_subsel = drv->has_keyboard ? INPUT_EV_KEY : (drv->has_abs_tablet ? INPUT_EV_ABS : INPUT_EV_REL);
    if (!virtio_input_query_config(drv->dev, VIRTIO_INPUT_CFG_EV_BITS, query_subsel, evs, &evsz, sizeof(evs))) {
        input_klog("[VIRTIO-INPUT-STRESS] Blad odpytania EV_BITS");
        return false;
    }

    // 3. Weryfikacja bezpieczenstwa znieksztalconych zapytan
    uint8_t dummy[16]{};
    uint8_t dummy_sz = 0;
    // Zapytanie z nieznanym selectem 0xFF powinno zwrocic false lub size=0 bez bledu MMIO
    (void)virtio_input_query_config(drv->dev, 0xFF, 0, dummy, &dummy_sz, sizeof(dummy));

    input_klog("[VIRTIO-INPUT-STRESS] SUCCESS: konfiguracja i odpytanie VirtIO-INPUT poprawne.");
    return true;
}

bool virtio_input_test_teardown_reinit(void) {
    // An automated/self-test call cannot reopen early hardware activation.
    if (!__atomic_load_n(&s_late_activation_allowed, __ATOMIC_ACQUIRE)) return false;
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return true;
#else
    if (s_driver_count == 0) return true;

    input_klog("[VIRTIO-INPUT-TEST] === Rozpoczynanie audytu cykli teardown/reinit VirtIO-INPUT ===");

    // 1. Zresetuj urzadzenia, aby uzyskac czysty stan bazowy
    virtio_input_reset_all();

    const uint64_t baseline_pmm = pmm_pobierz_wolne_ramki();
    const uint32_t baseline_vectors = idt_zaalokowane_wektory_liczba();

    constexpr int CYCLES = 5;
    for (int c = 0; c < CYCLES; ++c) {
        const uint32_t count = virtio_input_probe_and_prepare();
        const uint32_t active = virtio_input_activate_prepared();
        if (count == 0 || active != count) {
            input_klog("[VIRTIO-INPUT-TEST] BLAD: Re-inicjalizacja w cyklu nie powiodla sie!");
            return false;
        }

        virtio_input_reset_all();

        const uint64_t curr_pmm = pmm_pobierz_wolne_ramki();
        const uint32_t curr_vectors = idt_zaalokowane_wektory_liczba();

        if (curr_pmm != baseline_pmm || curr_vectors != baseline_vectors) {
            input_klog("[VIRTIO-INPUT-TEST] BLAD WYCIEKU ZASOBOW w cyklu!");
            return false;
        }
    }

    // 2. Finalna re-inicjalizacja urzadzen przed dalsza praca systemu
    const uint32_t final_init = virtio_input_probe_and_prepare();
    if (final_init == 0 || virtio_input_activate_prepared() != final_init) {
        input_klog("[VIRTIO-INPUT-TEST] BLAD: Finalna inicjalizacja urzadzen po testach cykli!");
        return false;
    }

    input_klog("[VIRTIO-INPUT-TEST] Cykle teardown/reinit: PASS (PMM delta=0, DMA delta=0, IDT delta=0).");
    return true;
#endif
}

bool virtio_input_stress_test_all(void) {
    if (!virtio_input_test_teardown_reinit()) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < s_driver_count; ++i) {
        if (s_drivers[i].initialized) {
            if (!virtio_input_stress_test(&s_drivers[i])) {
                ok = false;
            }
        }
    }
    if (ok) {
        input_klog("[VIRTIO-INPUT-STRESS] Test obciazeniowy zakonczony: PASS");
    }
    return ok;
}

} // extern "C"
