/*
 * Bursztyn OS - Test jednostkowy warstwy transportowej VirtIO 1.x Modern PCI
 *
 * Plik: testy/test_virtio_pci.cpp
 *
 * Pokrywa testami:
 *  - Wykrywanie i parsowanie modern VirtIO PCI (Vendor 0x1AF4)
 *  - Bezpieczne parsowanie lancucha PCI Capabilities
 *  - Walidacje struktur: COMMON_CFG, NOTIFY_CFG, ISR_CFG, DEVICE_CFG
 *  - Walidacje granic BAR, typu BAR, offsetow i dlugosci (overflow arithmetic)
 *  - Wykrywanie petli w lancuchu capabilities
 *  - Negocjacje cech (wymog VIRTIO_F_VERSION_1)
 *  - Zachowanie urzadzenia odrzucajacego FEATURES_OK
 *  - Maszyne stanow (reset, ACKNOWLEDGE, DRIVER, FEATURES_OK; brak DRIVER_OK w probe)
 *  - Obliczanie adresu powiadomien (notify) z mnoznikiem i ochrona przed overflow
 *  - Rejestry konfiguracji kolejek VirtQueue
 *  - Bezpieczny odczyt i zapis konfiguracji specyficznej dla urzadzenia (device-specific config)
 *  - Odczyt i czyszczenie rejestru ISR
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "jadro/gui/display_mode_policy.h"
#include <cassert>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

#include "idt.h"
#include "apic.h"
#include "virtio_pci.h"
#include "virtio_input_activation_diag.h"
#include "pmm_diagnostics.h"
#include "pamiec.h"
#include "virtqueue.h"
#include "virtio_rng.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "virtio_gpu.h"
#include "prezentacja_startu.h"
#include "input_device.h"
#include "virtio_input.h"
#include "sterowniki/usb/usb_hid.h"
#include "bws_zdarzenia.h"

extern "C" void ps2_klawiatura_przetworz_scancode(uint8_t scancode);
extern "C" void ps2_klawiatura_zresetuj_stan(void);
extern "C" void klawiatura_zresetuj_stan(void);
extern "C" uint32_t klawiatura_modyfikatory(void);
extern "C" char pobierz_znak_klawiatury(void);
extern "C" InputDevice* ps2_klawiatura_pobierz_urzadzenie(void);

// ============================================================================
// MOCK IDT I APIC DLA SRODOWISKA TESTOWEGO HOST
// ============================================================================

struct MockIdtEntry {
    IrqHandlerFn handler = nullptr;
    void* context = nullptr;
    bool allocated = false;
};

static MockIdtEntry g_mock_idt[256];
static uint32_t g_mock_idt_allocated_count = 0;

extern "C" int idt_alokuj_wektor_irq(void) {
    for (int i = IDT_PIERWSZY_WEKTOR_DYNAMICZNY; i <= IDT_OSTATNI_WEKTOR_DYNAMICZNY; ++i) {
        if (!g_mock_idt[i].allocated) {
            g_mock_idt[i].allocated = true;
            g_mock_idt[i].handler = nullptr;
            g_mock_idt[i].context = nullptr;
            g_mock_idt_allocated_count++;
            return i;
        }
    }
    return -1;
}

extern "C" bool idt_zwolnij_wektor_irq(uint8_t vector) {
    if (vector < IDT_PIERWSZY_WEKTOR_DYNAMICZNY || vector > IDT_OSTATNI_WEKTOR_DYNAMICZNY) return false;
    if (!g_mock_idt[vector].allocated) return false;
    g_mock_idt[vector].allocated = false;
    g_mock_idt[vector].handler = nullptr;
    g_mock_idt[vector].context = nullptr;
    if (g_mock_idt_allocated_count > 0) g_mock_idt_allocated_count--;
    return true;
}

extern "C" bool idt_zarejestruj_irq(uint8_t vector, IrqHandlerFn handler, void* context) {
    if (vector < IDT_PIERWSZY_WEKTOR_DYNAMICZNY || vector > IDT_OSTATNI_WEKTOR_DYNAMICZNY || !handler) return false;
    if (!g_mock_idt[vector].allocated || g_mock_idt[vector].handler) return false;
    g_mock_idt[vector].handler = handler;
    g_mock_idt[vector].context = context;
    return true;
}

extern "C" bool idt_wyrejestruj_irq(uint8_t vector) {
    if (vector < IDT_PIERWSZY_WEKTOR_DYNAMICZNY || vector > IDT_OSTATNI_WEKTOR_DYNAMICZNY) return false;
    g_mock_idt[vector].handler = nullptr;
    g_mock_idt[vector].context = nullptr;
    return true;
}

extern "C" uint32_t idt_zaalokowane_wektory_liczba(void) {
    return g_mock_idt_allocated_count;
}

extern "C" uint8_t apic_pobierz_lapic_id(void) {
    return 0;
}

void mock_trigger_irq(uint8_t vector) {
    if (g_mock_idt[vector].allocated && g_mock_idt[vector].handler) {
        g_mock_idt[vector].handler(g_mock_idt[vector].context);
    }
}

// ============================================================================
// FAKE PCI I FAKE MMIO DLA SRODOWISKA TESTOWEGO HOST
// ============================================================================

namespace {

struct MockBar {
    uint64_t phys_addr = 0xFEB00000ULL;
    uint64_t size = 0x4000ULL;
    bool is_64bit = false;
    bool is_io = false;
    bool is_sizing = false;
    std::vector<uint8_t> mem;

    MockBar() {
        mem.resize(size, 0);
    }

    void resize(uint64_t new_size) {
        size = new_size;
        mem.assign(new_size, 0);
    }
};

struct MockQueue {
    uint16_t size = 16;
    uint16_t enable = 0;
    uint16_t notify_off = 0;
    uint16_t msix_vector = 0xFFFF;
};

struct MockDeviceState {
    uint8_t pci_config[256]{};
    MockBar bars[6];

    // Stan MMIO VirtIO:
    uint32_t selected_feature_bank = 0;
    uint64_t device_features = VIRTIO_F_VERSION_1 | 0x1U;
    uint64_t driver_features = 0;
    uint8_t  device_status = 0;
    bool     accept_features = true;
    uint8_t  isr_val = 0;

    uint8_t  notify_bar = 0xFF;
    uint32_t notify_bar_off = 0;
    uint32_t notify_bar_len = 0;
    uint16_t last_notified_queue = 0xFFFFU;
    uint32_t notify_count = 0;

    uint16_t selected_queue = 0;
    MockQueue queues[8];
    bool     allow_queue_enable = true;
    bool     allow_driver_ok = true;
    bool     ignore_reset = false;
    bool     rng_auto_process = false;
    bool     rng_skip_reply = false;
    int32_t  rng_override_len = -1;
    int32_t  rng_override_desc_id = -1;
    uint8_t  rng_fill_byte = 0x5A;
    bool     rng_dynamic_bytes = false;
    uint32_t rng_processed_count = 0;
    int      bump_gen_on_read_count = 0;
    bool     always_bump_gen = false;

    // Stan MMIO VirtIO-BLK:
    bool     blk_auto_process = false;
    bool     blk_skip_reply = false;
    int32_t  blk_override_len = -1;
    int32_t  blk_override_desc_id = -1;
    int32_t  blk_override_status = -1;
    uint32_t blk_processed_count = 0;
    std::vector<uint8_t> blk_disk_data;

    // Stan MMIO VirtIO-NET:
    bool     net_auto_process = false;
    bool     net_skip_tx_reply = false;
    int32_t  net_override_len = -1;
    int32_t  net_override_desc_id = -1;
    uint32_t net_tx_processed_count = 0;
    std::vector<std::vector<uint8_t>> net_tx_frames;

    // Stan MMIO VirtIO-GPU:
    bool     gpu_auto_process = false;
    bool     gpu_skip_reply = false;
    int32_t  gpu_override_len = -1;
    int32_t  gpu_override_desc_id = -1;
    uint32_t gpu_override_resp_type = 0;
    uint32_t gpu_fail_cmd_type = 0;
    uint32_t gpu_display_w = 1280;
    uint32_t gpu_display_h = 800;
    uint32_t gpu_processed_count = 0;
    uint32_t gpu_last_nr_entries = 0;
    std::vector<uint32_t> gpu_commands_seen;
    bool gpu_capture_transfer=false;
    uint32_t gpu_transferred_resource=0;
    std::vector<uint32_t> gpu_transferred_frame;

    // Stan MSI-X:
    bool     msix_enabled = false;
    uint16_t msix_msg_ctrl = 0;
    uint8_t  msix_cap_offset = 0;
    uint8_t  msix_table_bar = 1;
    uint32_t msix_table_off = 0;
    uint16_t msix_table_size = 4;
    uint16_t config_msix_vector = 0xFFFF;

    // Stan MSI:
    bool     msi_enabled = false;
    uint16_t msi_msg_ctrl = 0;
    uint8_t  msi_cap_offset = 0;

    void reset() {
        std::memset(pci_config, 0, sizeof(pci_config));
        msix_enabled = false;
        msix_msg_ctrl = 0;
        msix_cap_offset = 0;
        msix_table_bar = 1;
        msix_table_off = 0;
        msix_table_size = 4;
        config_msix_vector = 0xFFFF;
        msi_enabled = false;
        msi_msg_ctrl = 0;
        msi_cap_offset = 0;
        for (int i = 0; i < 6; ++i) {
            bars[i] = MockBar();
            bars[i].phys_addr = 0xFEB00000ULL + i * 0x10000ULL;
        }
        selected_feature_bank = 0;
        device_features = VIRTIO_F_VERSION_1 | 0x1U;
        driver_features = 0;
        device_status = 0;
        accept_features = true;
        allow_driver_ok = true;
        ignore_reset = false;
        rng_auto_process = false;
        rng_skip_reply = false;
        rng_override_len = -1;
        rng_override_desc_id = -1;
        rng_fill_byte = 0x5A;
        rng_dynamic_bytes = false;
        rng_processed_count = 0;
        blk_auto_process = false;
        blk_skip_reply = false;
        blk_override_len = -1;
        blk_override_desc_id = -1;
        blk_override_status = -1;
        blk_processed_count = 0;
        blk_disk_data.clear();
        net_auto_process = false;
        net_skip_tx_reply = false;
        net_override_len = -1;
        net_override_desc_id = -1;
        net_tx_processed_count = 0;
        net_tx_frames.clear();
        gpu_auto_process = false;
        gpu_skip_reply = false;
        gpu_override_len = -1;
        gpu_override_desc_id = -1;
        gpu_override_resp_type = 0;
        gpu_fail_cmd_type = 0;
        gpu_display_w = 1280;
        gpu_display_h = 800;
        gpu_processed_count = 0;
        gpu_last_nr_entries = 0;
        gpu_commands_seen.clear();gpu_capture_transfer=false;
        gpu_transferred_resource=0;gpu_transferred_frame.clear();
        isr_val = 0;
        notify_bar = 0xFF;
        notify_bar_off = 0;
        notify_bar_len = 0;
        last_notified_queue = 0xFFFFU;
        notify_count = 0;
        selected_queue = 0;
        allow_queue_enable = true;
        bump_gen_on_read_count = 0;
        always_bump_gen = false;
        for (int i = 0; i < 8; ++i) {
            queues[i] = MockQueue();
            queues[i].size = 16;
            queues[i].enable = 0;
            queues[i].notify_off = static_cast<uint16_t>(i);
        }
    }
};

MockDeviceState g_mock;
std::vector<std::string> g_klog_lines;

struct HostDmaAllocEntry {
    void* vaddr;
    uint64_t paddr;
    size_t size;
    size_t pages;
};
static std::vector<HostDmaAllocEntry> g_host_dma_allocs;
static uint64_t g_next_dma_phys = 0x10000000ULL;
static bool g_mock_dma_alloc_fail = false;
static uint32_t g_mock_dma_alloc_fail_at = 0;
static uint32_t g_mock_dma_alloc_count = 0;
static bool g_use_pmm_dma = false;
struct HostReservedMemory {
    uint64_t physical;
    std::vector<uint8_t> bytes;
};
static std::vector<HostReservedMemory> g_host_reserved_memory;

struct HostFbAllocEntry {
    void* vaddr;
    size_t page_count;
    std::vector<uint64_t> phys_frames;
};
static std::vector<HostFbAllocEntry> g_host_fb_allocs;

static uint32_t g_virtqueue_mb_count = 0;
static uint32_t g_virtqueue_rmb_count = 0;
static uint32_t g_virtqueue_wmb_count = 0;
static void (*g_input_notify_hook)() = nullptr;
static void (*g_input_mb_hook)() = nullptr;

static void* host_phys_to_virt(uint64_t paddr) {
    // Reserved module contents participate in the same physical address map
    // as DMA, so a bad descriptor can actually overwrite their sentinels.
    for (auto& entry : g_host_reserved_memory) {
        if (paddr >= entry.physical && paddr - entry.physical < entry.bytes.size())
            return entry.bytes.data() + (paddr - entry.physical);
    }
    for (const auto& entry : g_host_dma_allocs) {
        if (paddr >= entry.paddr && paddr < entry.paddr + entry.pages * 4096) {
            return static_cast<uint8_t*>(entry.vaddr) + (paddr - entry.paddr);
        }
    }
    for (const auto& entry : g_host_fb_allocs) {
        for (size_t i = 0; i < entry.phys_frames.size(); ++i) {
            if (paddr >= entry.phys_frames[i] && paddr < entry.phys_frames[i] + 4096) {
                return static_cast<uint8_t*>(entry.vaddr) + (i * 4096) + (paddr - entry.phys_frames[i]);
            }
        }
    }
    return nullptr;
}

} // namespace

#include "mock_gpu_scanout.h"

void host_virtqueue_mb_hook() {
    g_virtqueue_mb_count++;
    if (g_input_mb_hook) g_input_mb_hook();
}

void host_virtqueue_rmb_hook() {
    g_virtqueue_rmb_count++;
}

void host_virtqueue_wmb_hook() {
    g_virtqueue_wmb_count++;
}

static void reset_virtqueue_counters() {
    g_virtqueue_mb_count = 0;
    g_virtqueue_rmb_count = 0;
    g_virtqueue_wmb_count = 0;
}

bool host_dma_alloc(size_t size, size_t alignment, void** vaddr, uint64_t* paddr, size_t* pages) {
    g_mock_dma_alloc_count++;
    if (g_mock_dma_alloc_fail || (g_mock_dma_alloc_fail_at > 0 && g_mock_dma_alloc_count == g_mock_dma_alloc_fail_at)) {
        return false;
    }
    if (size == 0) return false;
    if (alignment < 4096) alignment = 4096;

    void* ptr = nullptr;
    const size_t num_pages = (size + 4095) / 4096;
    if (posix_memalign(&ptr, alignment, num_pages * 4096 + 64) != 0) {
        return false;
    }
    std::memset(ptr, 0, num_pages * 4096);
    std::memset(static_cast<uint8_t*>(ptr) + num_pages * 4096, 0xD3, 64);

    uint64_t phys = g_use_pmm_dma ? reinterpret_cast<uintptr_t>(ZaalokujCiagleRamki(num_pages)) : g_next_dma_phys;
    if (!phys) { std::free(ptr); return false; }
    for (const auto& reserved : g_host_reserved_memory)
        assert(phys + num_pages * 4096 <= reserved.physical ||
               phys >= reserved.physical + reserved.bytes.size());
    if (!g_use_pmm_dma) g_next_dma_phys += num_pages * 4096;

    *vaddr = ptr;
    *paddr = phys;
    *pages = num_pages;

    g_host_dma_allocs.push_back({ptr, phys, size, num_pages});
    return true;
}

void host_dma_free(void* vaddr, uint64_t paddr, size_t pages) {
    (void)paddr;
    (void)pages;
    for (auto it = g_host_dma_allocs.begin(); it != g_host_dma_allocs.end(); ++it) {
        if (it->vaddr == vaddr) {
            for (unsigned i=0; i<64; ++i)
                assert(static_cast<uint8_t*>(vaddr)[it->pages * 4096+i]==0xD3);
            if (g_use_pmm_dma) ZwolnijCiagleRamki(reinterpret_cast<void*>(paddr), pages);
            std::free(vaddr);
            g_host_dma_allocs.erase(it);
            return;
        }
    }
    if (vaddr) {
        std::free(vaddr);
    }
}

bool host_fb_alloc(size_t total_bytes, void** out_vaddr, void** out_frames, size_t max_pages, size_t* out_page_count) {
    if (!out_vaddr || !out_frames || !out_page_count || total_bytes == 0) return false;
    const size_t num_pages = (total_bytes + 4095) / 4096;
    if (num_pages > max_pages) return false;

    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, num_pages * 4096) != 0) {
        return false;
    }
    std::memset(ptr, 0, num_pages * 4096);

    HostFbAllocEntry entry;
    entry.vaddr = ptr;
    entry.page_count = num_pages;

    static uint64_t s_fb_phys = 0x20000000ULL;
    for (size_t i = 0; i < num_pages; ++i) {
        if (i > 0 && (i % 4) == 0) {
            s_fb_phys += 0x10000ULL; // Celowa luka fizyczna co 4 strony dla weryfikacji scatter-gather
        }
        entry.phys_frames.push_back(s_fb_phys);
        out_frames[i] = reinterpret_cast<void*>(s_fb_phys);
        s_fb_phys += 4096;
    }

    g_host_fb_allocs.push_back(entry);
    *out_vaddr = ptr;
    *out_page_count = num_pages;
    return true;
}

void host_fb_free(void* vaddr, void** frames, size_t page_count) {
    (void)frames;
    (void)page_count;
    for (auto it = g_host_fb_allocs.begin(); it != g_host_fb_allocs.end(); ++it) {
        if (it->vaddr == vaddr) {
            std::free(vaddr);
            g_host_fb_allocs.erase(it);
            return;
        }
    }
    if (vaddr) {
        std::free(vaddr);
    }
}

// ============================================================================
// IMPLEMENTACJA HOOKOW DLA virtio_pci.cpp
// ============================================================================

#include "mock_virtio_input_init.inc"

uint32_t host_pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    for(auto* f:init_functions) if(f->dev.bus==bus && f->dev.slot==slot && f->dev.func==func) {
        uint32_t v=0; std::memcpy(&v,f->pci+(offset & ~3U),4); return v;
    }
    (void)bus; (void)slot; (void)func;
    offset &= ~3U;

    // Obsluga BAR (0x10 .. 0x24)
    if (offset >= 0x10 && offset <= 0x24) {
        const uint8_t bar_idx = (offset - 0x10) / 4;
        if (bar_idx < 6) {
            auto& bar = g_mock.bars[bar_idx];
            if (bar.is_sizing) {
                uint32_t mask = static_cast<uint32_t>(~(bar.size - 1U));
                if (bar.is_64bit) mask |= 0x04U;
                if (bar.is_io) mask |= 0x01U;
                return mask;
            }
            uint32_t val = static_cast<uint32_t>(bar.phys_addr & 0xFFFFFFFFU);
            if (bar.is_64bit) val |= 0x04U;
            if (bar.is_io) val |= 0x01U;
            return val;
        }
    }

    uint32_t val = 0;
    std::memcpy(&val, &g_mock.pci_config[offset], sizeof(uint32_t));
    return val;
}

void host_pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    for(auto* f:init_functions) if(f->dev.bus==bus && f->dev.slot==slot && f->dev.func==func) {
        std::memcpy(f->pci+(offset & ~3U),&val,4); return;
    }
    (void)bus; (void)slot; (void)func;
    offset &= ~3U;

    // Obsluga BAR (0x10 .. 0x24)
    if (offset >= 0x10 && offset <= 0x24) {
        const uint8_t bar_idx = (offset - 0x10) / 4;
        if (bar_idx < 6) {
            auto& bar = g_mock.bars[bar_idx];
            if (val == 0xFFFFFFFFU) {
                bar.is_sizing = true;
            } else {
                bar.is_sizing = false;
                bar.phys_addr = val & ~0xFULL;
            }
            return;
        }
    }

    std::memcpy(&g_mock.pci_config[offset], &val, sizeof(uint32_t));

    if (g_mock.msix_cap_offset != 0 && offset == (g_mock.msix_cap_offset & ~3U)) {
        uint16_t msg_ctrl = 0;
        std::memcpy(&msg_ctrl, &g_mock.pci_config[g_mock.msix_cap_offset + 2], 2);
        g_mock.msix_msg_ctrl = msg_ctrl;
        g_mock.msix_enabled = (msg_ctrl & PCI_MSIX_CTRL_ENABLE) != 0;
    }
    if (g_mock.msi_cap_offset != 0 && offset == (g_mock.msi_cap_offset & ~3U)) {
        uint16_t msg_ctrl = 0;
        std::memcpy(&msg_ctrl, &g_mock.pci_config[g_mock.msi_cap_offset + 2], 2);
        g_mock.msi_msg_ctrl = msg_ctrl;
        g_mock.msi_enabled = (msg_ctrl & PCI_MSI_CTRL_ENABLE) != 0;
    }
}

volatile uint8_t* host_bar_map(uint8_t bar, uint64_t phys_addr, uint64_t size) {
    (void)phys_addr; (void)size;
    if (bar >= 6) return nullptr;
    return g_mock.bars[bar].mem.data();
}

void host_klog(const char* msg) {
    if (msg) {
        g_klog_lines.emplace_back(msg);
    }
}

static uint64_t g_host_mock_time_ns = 0;
static bool g_host_use_mock_time = false;
// Deterministic recovery tests: every driver clock read advances mock time.
static uint64_t g_host_mock_step_ns = 0;
static uint64_t g_gpu_complete_jump_ns = 600000000ULL;
// Deliver a GPU response during the deadline read, after the caller's poll.
static unsigned g_gpu_complete_on_clock_read = 0;
void host_mmio_write16(volatile void* addr, uint16_t val);

// jadro/gui/graphics_perf.h (telemetry only) reads the kernel HPET API.
// Give it a plain host clock, independent of the driver's mocked deadline
// clock above, so telemetry never consumes injected completion events.
// Suites that #include this file and provide their own kernel clock (e.g.
// test_splash_production_path.cpp) define BURSZTYN_HOST_KERNEL_CLOCK_PROVIDED.
#ifndef BURSZTYN_HOST_KERNEL_CLOCK_PROVIDED
bool hpet_dostepny() { return true; }
uint64_t czas_monotoniczny_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
#endif

uint64_t host_czas_monotoniczny_ns(void) {
    if (g_gpu_complete_on_clock_read && --g_gpu_complete_on_clock_read == 0) {
        auto* drv = virtio_gpu_get_driver(0);
        g_mock.gpu_skip_reply = false;
        host_mmio_write16(virtio_pci_notify_address(drv->dev, drv->controlq->queue_notify_off), 0);
        g_host_mock_time_ns += g_gpu_complete_jump_ns;
    }
    if (g_host_use_mock_time) {
        g_host_mock_time_ns += g_host_mock_step_ns;
        return g_host_mock_time_ns;
    }
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()
    );
}

// Model flagi IF i instrukcji HLT dla gpu_wait_for_irq (virtio_gpu.cpp). Proces testu dziala w Ring 3: prawdziwe
// CLI/STI/HLT dalyby SIGSEGV, wiec sterownik w buildzie hostowym wola te funkcje. Model sprawdza protokol
// "cli; sprawdz; sti; hlt": HLT tylko po CLI (inaczej okno utraconego wybudzenia), STI tylko przy IF=0.
struct HostCpuModel {
    bool if_flag = true;               // jak zwykly kontekst syscallu w jadrze: przerwania wlaczone
    unsigned disables = 0, enables = 0, halts = 0, protocol_errors = 0;
    unsigned deliver_gpu_reply_on_halt = 0;   // N>0: N-ty HLT "budzi" przerwanie z odpowiedzia GPU
};
static HostCpuModel g_host_cpu;
bool host_cpu_irq_enabled() { return g_host_cpu.if_flag; }
void host_cpu_irq_disable() { if (!g_host_cpu.if_flag) ++g_host_cpu.protocol_errors; g_host_cpu.if_flag = false; ++g_host_cpu.disables; }
void host_cpu_irq_enable() { if (g_host_cpu.if_flag) ++g_host_cpu.protocol_errors; g_host_cpu.if_flag = true; ++g_host_cpu.enables; }
void host_cpu_enable_irq_and_halt() {
    if (g_host_cpu.if_flag) ++g_host_cpu.protocol_errors;   // HLT bez wczesniejszego CLI
    g_host_cpu.if_flag = true; ++g_host_cpu.halts;
    if (g_host_cpu.deliver_gpu_reply_on_halt && --g_host_cpu.deliver_gpu_reply_on_halt == 0) {
        auto* drv = virtio_gpu_get_driver(0);
        g_mock.gpu_skip_reply = false;
        host_mmio_write16(virtio_pci_notify_address(drv->dev, drv->controlq->queue_notify_off), 0);
    }
}

// Hooki MMIO
uint8_t host_mmio_read8(volatile const void* addr) {
    uint64_t separate=0; if(init_read(addr,1,separate)) return static_cast<uint8_t>(separate);
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (int b = 0; b < 6; ++b) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_mock.bars[b].mem.data());
        if (a >= base && a < base + g_mock.bars[b].mem.size()) {
            if (g_mock.always_bump_gen) {
                g_mock.bars[0].mem[0x15]++; // config_generation
            } else if (g_mock.bump_gen_on_read_count > 0) {
                g_mock.bump_gen_on_read_count--;
                g_mock.bars[0].mem[0x15]++; // config_generation
            }
            return g_mock.bars[b].mem[a - base];
        }
    }
    return 0;
}

uint16_t host_mmio_read16(volatile const void* addr) {
    uint64_t separate=0; if(init_read(addr,2,separate)) return static_cast<uint16_t>(separate);
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (int b = 0; b < 6; ++b) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_mock.bars[b].mem.data());
        if (a >= base && a + 2 <= base + g_mock.bars[b].mem.size()) {
            uint16_t v = 0;
            std::memcpy(&v, &g_mock.bars[b].mem[a - base], 2);
            return v;
        }
    }
    return 0;
}

uint32_t host_mmio_read32(volatile const void* addr) {
    uint64_t separate=0; if(init_read(addr,4,separate)) return static_cast<uint32_t>(separate);
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (int b = 0; b < 6; ++b) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_mock.bars[b].mem.data());
        if (a >= base && a + 4 <= base + g_mock.bars[b].mem.size()) {
            uint32_t v = 0;
            std::memcpy(&v, &g_mock.bars[b].mem[a - base], 4);
            return v;
        }
    }
    return 0;
}

void host_mmio_write8(volatile void* addr, uint8_t val) {
    if(init_write(addr,1,val)) return;
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (int b = 0; b < 6; ++b) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_mock.bars[b].mem.data());
        if (a >= base && a < base + g_mock.bars[b].mem.size()) {
            const size_t off = a - base;
            if (b == 0 && off == 20) {
                if (val == 0) {
                    if (g_mock.ignore_reset) {
                        // Sprzet symuluje zawieszenie resetu - ignoruje wpisanie 0
                        return;
                    }
                    if(g_mock.gpu_auto_process)gpu_scanout_model.reset_display();
                    for (int i = 0; i < 8; ++i) {
                        g_mock.queues[i].enable = 0;
                    }
                    g_mock.rng_processed_count = 0;
                    g_mock.blk_processed_count = 0;
                    g_mock.net_tx_processed_count = 0;
                    g_mock.gpu_processed_count = 0;
                    uint16_t zero16 = 0;
                    std::memcpy(&g_mock.bars[0].mem[0x1C], &zero16, 2);
                }
                if ((val & VIRTIO_STATUS_FEATURES_OK) != 0 && !g_mock.accept_features) {
                    val &= ~VIRTIO_STATUS_FEATURES_OK;
                }
                if ((val & VIRTIO_STATUS_DRIVER_OK) != 0 && !g_mock.allow_driver_ok) {
                    val &= ~VIRTIO_STATUS_DRIVER_OK;
                }
                g_mock.device_status = val;
            }
            g_mock.bars[b].mem[off] = val;
            return;
        }
    }
}

static void mock_check_and_trigger_msix(uint16_t queue_idx) {
    if (g_mock.msix_enabled) {
        if (queue_idx < 8) {
            uint16_t entry_idx = g_mock.queues[queue_idx].msix_vector;
            if (entry_idx != 0xFFFF && entry_idx < g_mock.msix_table_size) {
                uint8_t bar_idx = g_mock.msix_table_bar;
                if (bar_idx < 6) {
                    size_t entry_offset = g_mock.msix_table_off + static_cast<size_t>(entry_idx) * sizeof(PciMsixTableEntry);
                    if (entry_offset + sizeof(PciMsixTableEntry) <= g_mock.bars[bar_idx].mem.size()) {
                        auto* entry = reinterpret_cast<const PciMsixTableEntry*>(&g_mock.bars[bar_idx].mem[entry_offset]);
                        uint32_t ctrl = entry->vector_ctrl;
                        if ((ctrl & PCI_MSIX_VECTOR_CTRL_MASK) == 0) { // unmasked
                            uint8_t vector = static_cast<uint8_t>(entry->msg_data & 0xFF);
                            mock_trigger_irq(vector);
                            return;
                        }
                    }
                }
            }
        }
    } else if (g_mock.msi_enabled) {
        if (g_mock.msi_cap_offset != 0) {
            uint16_t ctrl = 0;
            std::memcpy(&ctrl, &g_mock.pci_config[g_mock.msi_cap_offset + 2], 2);
            bool is_64 = (ctrl & PCI_MSI_CTRL_64BIT) != 0;
            uint8_t data_off = g_mock.msi_cap_offset + (is_64 ? 12 : 8);
            uint16_t data = 0;
            std::memcpy(&data, &g_mock.pci_config[data_off], 2);
            uint8_t vector = static_cast<uint8_t>(data & 0xFF);
            mock_trigger_irq(vector);
            return;
        }
    } else {
        g_mock.isr_val |= 1;
    }
}

[[maybe_unused]] static void mock_trigger_config_msix() {
    if (g_mock.msix_enabled && g_mock.config_msix_vector != 0xFFFF && g_mock.config_msix_vector < g_mock.msix_table_size) {
        uint8_t bar_idx = g_mock.msix_table_bar;
        if (bar_idx < 6) {
            size_t entry_offset = g_mock.msix_table_off + static_cast<size_t>(g_mock.config_msix_vector) * sizeof(PciMsixTableEntry);
            if (entry_offset + sizeof(PciMsixTableEntry) <= g_mock.bars[bar_idx].mem.size()) {
                auto* entry = reinterpret_cast<const PciMsixTableEntry*>(&g_mock.bars[bar_idx].mem[entry_offset]);
                if ((entry->vector_ctrl & PCI_MSIX_VECTOR_CTRL_MASK) == 0) {
                    uint8_t vector = static_cast<uint8_t>(entry->msg_data & 0xFF);
                    mock_trigger_irq(vector);
                }
            }
        }
    }
}

void host_mmio_write16(volatile void* addr, uint16_t val) {
    if(init_write(addr,2,val)) return;
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (int b = 0; b < 6; ++b) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_mock.bars[b].mem.data());
        if (a >= base && a + 2 <= base + g_mock.bars[b].mem.size()) {
            const size_t off = a - base;

            // Sprawdzenie powiadomienia (notify)
            if (b == g_mock.notify_bar && off >= g_mock.notify_bar_off && off + 2 <= g_mock.notify_bar_off + g_mock.notify_bar_len) {
                g_mock.last_notified_queue = val;
                g_mock.notify_count++;
                if (g_input_notify_hook) g_input_notify_hook();

                if (g_mock.rng_auto_process && val == 0 && !g_mock.rng_skip_reply) {
                    VirtioRngDriver* drv = virtio_rng_get_driver();
                    if (drv && drv->vq && drv->vq->avail && drv->vq->used && drv->vq->desc) {
                        VirtQueue* vq = drv->vq;
                        while (g_mock.rng_processed_count != vq->avail->idx) {
                            const uint16_t a_idx = g_mock.rng_processed_count & (vq->queue_size - 1U);
                            const uint16_t desc_id = vq->avail->ring[a_idx];
                            if (desc_id < vq->queue_size) {
                                const uint64_t paddr = vq->desc[desc_id].addr;
                                const uint32_t len = vq->desc[desc_id].len;
                                void* vaddr = host_phys_to_virt(paddr);
                                if (vaddr) {
                                    uint8_t* p = static_cast<uint8_t*>(vaddr);
                                    for (uint32_t i = 0; i < len; ++i) {
                                        if (g_mock.rng_dynamic_bytes) {
                                            uint8_t b_val = static_cast<uint8_t>((g_mock.rng_processed_count * 17 + i * 31 + 7) & 0xFF);
                                            if (b_val == 0) b_val = 1;
                                            p[i] = b_val;
                                        } else {
                                            p[i] = g_mock.rng_fill_byte;
                                        }
                                    }
                                }
                                const uint32_t rep_len = (g_mock.rng_override_len >= 0) ?
                                    static_cast<uint32_t>(g_mock.rng_override_len) : len;
                                const uint32_t rep_id = (g_mock.rng_override_desc_id >= 0) ?
                                    static_cast<uint32_t>(g_mock.rng_override_desc_id) : desc_id;
                                const uint16_t u_idx = vq->used->idx & (vq->queue_size - 1U);
                                vq->used->ring[u_idx].id = rep_id;
                                vq->used->ring[u_idx].len = rep_len;
                                asm volatile("" ::: "memory");
                                vq->used->idx = static_cast<uint16_t>(vq->used->idx + 1U);
                                asm volatile("mfence" ::: "memory");
                            }
                            g_mock.rng_processed_count++;
                        }
                        mock_check_and_trigger_msix(val);
                    }
                }

                if (g_mock.blk_auto_process && val == 0 && !g_mock.blk_skip_reply) {
                    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
                    if (drv && drv->vq && drv->vq->avail && drv->vq->used && drv->vq->desc) {
                        VirtQueue* vq = drv->vq;
                        while (g_mock.blk_processed_count != vq->avail->idx) {
                            const uint16_t a_idx = g_mock.blk_processed_count & (vq->queue_size - 1U);
                            const uint16_t head_id = vq->avail->ring[a_idx];
                            if (head_id < vq->queue_size) {
                                void* vhdr = host_phys_to_virt(vq->desc[head_id].addr);
                                uint32_t type = 0;
                                uint64_t sector_512 = 0;
                                if (vhdr) {
                                    const auto* hdr = static_cast<const VirtIOBlkReqHeader*>(vhdr);
                                    type = hdr->type;
                                    sector_512 = hdr->sector;
                                }

                                uint32_t used_len = 0;

                                if (type == VIRTIO_BLK_T_FLUSH) {
                                    const uint16_t status_id = vq->desc[head_id].next;
                                    if (status_id < vq->queue_size) {
                                        void* vst = host_phys_to_virt(vq->desc[status_id].addr);
                                        if (vst) {
                                            uint8_t* pst = static_cast<uint8_t*>(vst);
                                            *pst = (g_mock.blk_override_status >= 0) ?
                                                static_cast<uint8_t>(g_mock.blk_override_status) :
                                                static_cast<uint8_t>(VIRTIO_BLK_S_OK);
                                        }
                                    }
                                    used_len = 1;
                                } else if (type == VIRTIO_BLK_T_IN) { // READ
                                    const uint16_t data_id = vq->desc[head_id].next;
                                    if (data_id < vq->queue_size) {
                                        uint32_t data_len = vq->desc[data_id].len;
                                        void* vdata = host_phys_to_virt(vq->desc[data_id].addr);
                                        const uint16_t status_id = vq->desc[data_id].next;
                                        uint8_t st = (g_mock.blk_override_status >= 0) ?
                                            static_cast<uint8_t>(g_mock.blk_override_status) :
                                            static_cast<uint8_t>(VIRTIO_BLK_S_OK);

                                        if (status_id < vq->queue_size) {
                                            void* vst = host_phys_to_virt(vq->desc[status_id].addr);
                                            if (vst) {
                                                *static_cast<uint8_t*>(vst) = st;
                                            }
                                        }

                                        if (st == VIRTIO_BLK_S_OK && vdata) {
                                            uint64_t disk_off = sector_512 * 512ULL;
                                            if (disk_off + data_len <= g_mock.blk_disk_data.size()) {
                                                std::memcpy(vdata, &g_mock.blk_disk_data[disk_off], data_len);
                                            } else {
                                                std::memset(vdata, 0, data_len);
                                            }
                                        }
                                        used_len = data_len + 1U;
                                    }
                                } else if (type == VIRTIO_BLK_T_OUT) { // WRITE
                                    const uint16_t data_id = vq->desc[head_id].next;
                                    if (data_id < vq->queue_size) {
                                        uint32_t data_len = vq->desc[data_id].len;
                                        void* vdata = host_phys_to_virt(vq->desc[data_id].addr);
                                        const uint16_t status_id = vq->desc[data_id].next;
                                        uint8_t st = (g_mock.blk_override_status >= 0) ?
                                            static_cast<uint8_t>(g_mock.blk_override_status) :
                                            static_cast<uint8_t>(VIRTIO_BLK_S_OK);

                                        if (status_id < vq->queue_size) {
                                            void* vst = host_phys_to_virt(vq->desc[status_id].addr);
                                            if (vst) {
                                                *static_cast<uint8_t*>(vst) = st;
                                            }
                                        }

                                        if (st == VIRTIO_BLK_S_OK && vdata) {
                                            uint64_t disk_off = sector_512 * 512ULL;
                                            if (disk_off + data_len <= g_mock.blk_disk_data.size()) {
                                                std::memcpy(&g_mock.blk_disk_data[disk_off], vdata, data_len);
                                            }
                                        }
                                        used_len = 1U;
                                    }
                                }

                                const uint32_t rep_len = (g_mock.blk_override_len >= 0) ?
                                    static_cast<uint32_t>(g_mock.blk_override_len) : used_len;
                                const uint32_t rep_id = (g_mock.blk_override_desc_id >= 0) ?
                                    static_cast<uint32_t>(g_mock.blk_override_desc_id) : head_id;

                                const uint16_t u_idx = vq->used->idx & (vq->queue_size - 1U);
                                vq->used->ring[u_idx].id = rep_id;
                                vq->used->ring[u_idx].len = rep_len;
                                asm volatile("" ::: "memory");
                                vq->used->idx = static_cast<uint16_t>(vq->used->idx + 1U);
                                asm volatile("mfence" ::: "memory");
                            }
                            g_mock.blk_processed_count++;
                        }
                        mock_check_and_trigger_msix(val);
                    }
                }

                if (g_mock.net_auto_process && val == 1 && !g_mock.net_skip_tx_reply) {
                    VirtioNetDriver* drv = virtio_net_get_driver(0);
                    if (drv && drv->tx_vq && drv->tx_vq->avail && drv->tx_vq->used && drv->tx_vq->desc) {
                        VirtQueue* vq = drv->tx_vq;
                        while (g_mock.net_tx_processed_count != vq->avail->idx) {
                            const uint16_t a_idx = g_mock.net_tx_processed_count & (vq->queue_size - 1U);
                            const uint16_t head_id = vq->avail->ring[a_idx];
                            if (head_id < vq->queue_size) {
                                void* vhdr = host_phys_to_virt(vq->desc[head_id].addr);
                                uint32_t len = vq->desc[head_id].len;
                                if (vhdr && len >= sizeof(VirtioNetHdr)) {
                                    const uint8_t* p = static_cast<const uint8_t*>(vhdr);
                                    std::vector<uint8_t> frame_data(p + sizeof(VirtioNetHdr), p + len);
                                    g_mock.net_tx_frames.push_back(frame_data);
                                }
                                const uint32_t rep_len = (g_mock.net_override_len >= 0) ?
                                    static_cast<uint32_t>(g_mock.net_override_len) : 0;
                                const uint32_t rep_id = (g_mock.net_override_desc_id >= 0) ?
                                    static_cast<uint32_t>(g_mock.net_override_desc_id) : head_id;

                                const uint16_t u_idx = vq->used->idx & (vq->queue_size - 1U);
                                vq->used->ring[u_idx].id = rep_id;
                                vq->used->ring[u_idx].len = rep_len;
                                asm volatile("" ::: "memory");
                                vq->used->idx = static_cast<uint16_t>(vq->used->idx + 1U);
                                asm volatile("mfence" ::: "memory");
                            }
                            g_mock.net_tx_processed_count++;
                        }
                        mock_check_and_trigger_msix(val);
                    }
                }

                if (g_mock.gpu_auto_process && val == 0 && !g_mock.gpu_skip_reply) {
                    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
                    if (drv && drv->controlq && drv->controlq->avail && drv->controlq->used && drv->controlq->desc) {
                        VirtQueue* vq = drv->controlq;
                        // 16-bit ring index semantics (wraps at 65536), as on a real device.
                        while (static_cast<uint16_t>(g_mock.gpu_processed_count) != vq->avail->idx) {
                            const uint16_t a_idx = g_mock.gpu_processed_count & (vq->queue_size - 1U);
                            const uint16_t head_id = vq->avail->ring[a_idx];
                            if (head_id < vq->queue_size) {
                                void* vreq = host_phys_to_virt(vq->desc[head_id].addr);
                                uint32_t req_len = vq->desc[head_id].len;
                                uint16_t resp_id = vq->desc[head_id].next;
                                void* vresp = (resp_id < vq->queue_size) ? host_phys_to_virt(vq->desc[resp_id].addr) : nullptr;
                                uint32_t resp_len = (resp_id < vq->queue_size) ? vq->desc[resp_id].len : 0;

                                uint32_t used_len = 0;
                                if (vreq && req_len >= sizeof(VirtioGpuCtrlHdr) && vresp && resp_len >= sizeof(VirtioGpuCtrlHdr)) {
                                    const auto* req_hdr = static_cast<const VirtioGpuCtrlHdr*>(vreq);
                                    auto* resp_hdr = static_cast<VirtioGpuCtrlHdr*>(vresp);
                                    g_mock.gpu_commands_seen.push_back(req_hdr->type);
                                    if(req_hdr->type!=g_mock.gpu_fail_cmd_type&&!g_mock.gpu_override_resp_type)
                                        gpu_scanout_model.execute(vreq,req_len);
                                    if(g_mock.gpu_capture_transfer&&req_hdr->type==VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D) {
                                        auto* drv=virtio_gpu_get_driver(0);
                                        auto* pixels=static_cast<uint32_t*>(drv->fb_vaddr);
                                        g_mock.gpu_transferred_resource=static_cast<const VirtioGpuTransferToHost2d*>(vreq)->resource_id;
                                        g_mock.gpu_transferred_frame.assign(pixels,pixels+drv->fb_size/4);
                                    }

                                    if (req_hdr->type == VIRTIO_GPU_CMD_GET_DISPLAY_INFO) {
                                        auto* dresp = static_cast<VirtioGpuRespDisplayInfo*>(vresp);
                                        std::memset(dresp, 0, sizeof(VirtioGpuRespDisplayInfo));
                                        dresp->hdr.type = (g_mock.gpu_override_resp_type > 0) ? g_mock.gpu_override_resp_type : VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
                                        dresp->pmodes[0].enabled = 1;
                                        dresp->pmodes[0].r.x = 0;
                                        dresp->pmodes[0].r.y = 0;
                                        dresp->pmodes[0].r.width = g_mock.gpu_display_w;
                                        dresp->pmodes[0].r.height = g_mock.gpu_display_h;
                                        used_len = sizeof(VirtioGpuRespDisplayInfo);
                                    } else {
                                        if (req_hdr->type == VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING) {
                                            if (req_len >= sizeof(VirtioGpuResourceAttachBacking)) {
                                                const auto* att = static_cast<const VirtioGpuResourceAttachBacking*>(vreq);
                                                g_mock.gpu_last_nr_entries = att->nr_entries;
                                            }
                                        }
                                        std::memset(resp_hdr, 0, sizeof(VirtioGpuCtrlHdr));
                                        if (g_mock.gpu_fail_cmd_type > 0 && req_hdr->type == g_mock.gpu_fail_cmd_type) {
                                            resp_hdr->type = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
                                        } else {
                                            resp_hdr->type = (g_mock.gpu_override_resp_type > 0) ? g_mock.gpu_override_resp_type : VIRTIO_GPU_RESP_OK_NODATA;
                                        }
                                        used_len = sizeof(VirtioGpuCtrlHdr);
                                    }
                                }

                                const uint32_t rep_len = (g_mock.gpu_override_len >= 0) ?
                                    static_cast<uint32_t>(g_mock.gpu_override_len) : used_len;
                                const uint32_t rep_id = (g_mock.gpu_override_desc_id >= 0) ?
                                    static_cast<uint32_t>(g_mock.gpu_override_desc_id) : head_id;

                                const uint16_t u_idx = vq->used->idx & (vq->queue_size - 1U);
                                vq->used->ring[u_idx].id = rep_id;
                                vq->used->ring[u_idx].len = rep_len;
                                asm volatile("" ::: "memory");
                                vq->used->idx = static_cast<uint16_t>(vq->used->idx + 1U);
                                asm volatile("mfence" ::: "memory");
                            }
                            g_mock.gpu_processed_count++;
                        }
                        mock_check_and_trigger_msix(val);
                    }
                }
            }

            // Obsluga rejestrow kolejki w common_cfg (BAR 0)
            if (b == 0) {
                if (off == 0x10) { // config_msix_vector
                    g_mock.config_msix_vector = val;
                    std::memcpy(&g_mock.bars[b].mem[off], &val, 2);
                    return;
                } else if (off == 0x16) { // queue_select
                    g_mock.selected_queue = val;
                    std::memcpy(&g_mock.bars[b].mem[off], &val, 2);
                    if (val < 8) {
                        uint16_t qs = g_mock.queues[val].size;
                        uint16_t qmv = g_mock.queues[val].msix_vector;
                        uint16_t qe = g_mock.queues[val].enable;
                        uint16_t qno = g_mock.queues[val].notify_off;
                        std::memcpy(&g_mock.bars[b].mem[0x18], &qs, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1A], &qmv, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1C], &qe, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1E], &qno, 2);
                    } else {
                        uint16_t zero = 0;
                        uint16_t no_vec = 0xFFFF;
                        std::memcpy(&g_mock.bars[b].mem[0x18], &zero, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1A], &no_vec, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1C], &zero, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1E], &zero, 2);
                    }
                    return;
                } else if (off == 0x18) { // queue_size
                    if (g_mock.selected_queue < 8) {
                        g_mock.queues[g_mock.selected_queue].size = val;
                    }
                } else if (off == 0x1A) { // queue_msix_vector
                    if (g_mock.selected_queue < 8) {
                        g_mock.queues[g_mock.selected_queue].msix_vector = val;
                    }
                    std::memcpy(&g_mock.bars[b].mem[off], &val, 2);
                    return;
                } else if (off == 0x1C) { // queue_enable
                    if (!g_mock.allow_queue_enable) {
                        val = 0;
                    }
                    if (g_mock.selected_queue < 8) {
                        g_mock.queues[g_mock.selected_queue].enable = val;
                    }
                } else if (off == 0x1E) { // queue_notify_off
                    if (g_mock.selected_queue < 8) {
                        g_mock.queues[g_mock.selected_queue].notify_off = val;
                    }
                }
            }

            std::memcpy(&g_mock.bars[b].mem[off], &val, 2);
            return;
        }
    }
}

void host_mmio_write32(volatile void* addr, uint32_t val) {
    if(init_write(addr,4,val)) return;
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (int b = 0; b < 6; ++b) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_mock.bars[b].mem.data());
        if (a >= base && a + 4 <= base + g_mock.bars[b].mem.size()) {
            const size_t off = a - base;
            std::memcpy(&g_mock.bars[b].mem[off], &val, 4);
            if (b == 0 && off == 0) {
                g_mock.selected_feature_bank = val;
                uint32_t f = 0;
                if (val == 0) {
                    f = static_cast<uint32_t>(g_mock.device_features & 0xFFFFFFFFU);
                } else {
                    f = static_cast<uint32_t>(g_mock.device_features >> 32);
                }
                std::memcpy(&g_mock.bars[0].mem[4], &f, 4);
            }
            return;
        }
    }
}

// ============================================================================
// HELPERY DO KONFIGURACJI MOCKA
// ============================================================================

namespace {

void setup_pci_hdr(uint16_t vendor_id, uint16_t dev_id, uint16_t sub_vendor, uint16_t sub_dev) {
    std::memcpy(&g_mock.pci_config[0x00], &vendor_id, 2);
    std::memcpy(&g_mock.pci_config[0x02], &dev_id, 2);
    uint16_t status = (1U << 4); // Capabilities list
    std::memcpy(&g_mock.pci_config[0x06], &status, 2);
    std::memcpy(&g_mock.pci_config[0x2C], &sub_vendor, 2);
    std::memcpy(&g_mock.pci_config[0x2E], &sub_dev, 2);
}

struct CapBuilderEntry {
    uint8_t  cfg_type;
    uint8_t  bar;
    uint32_t offset;
    uint32_t length;
    uint32_t multiplier; // tylko dla NOTIFY
};

uint8_t build_cap_chain(const std::vector<CapBuilderEntry>& entries, uint8_t tail_next = 0) {
    uint8_t current_ptr = 0x40;
    g_mock.pci_config[0x34] = current_ptr;

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const uint8_t next_ptr = (i + 1 < entries.size()) ? static_cast<uint8_t>(current_ptr + 24) : tail_next;
        const uint8_t cap_len = (e.cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) ? 20 : 16;

        g_mock.pci_config[current_ptr + 0] = 0x09; // PCI_CAP_ID_VNDR
        g_mock.pci_config[current_ptr + 1] = next_ptr;
        g_mock.pci_config[current_ptr + 2] = cap_len;
        g_mock.pci_config[current_ptr + 3] = e.cfg_type;
        g_mock.pci_config[current_ptr + 4] = e.bar;
        g_mock.pci_config[current_ptr + 5] = 0; // id
        std::memcpy(&g_mock.pci_config[current_ptr + 8], &e.offset, 4);
        std::memcpy(&g_mock.pci_config[current_ptr + 12], &e.length, 4);

        if (e.cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
            std::memcpy(&g_mock.pci_config[current_ptr + 16], &e.multiplier, 4);
            g_mock.notify_bar = e.bar;
            g_mock.notify_bar_off = e.offset;
            g_mock.notify_bar_len = e.length;
        }

        current_ptr = next_ptr;
    }
    return 0x40;
}

// Konfiguracja symulatora MMIO VirtIO w wybranym BAR
void wire_virtio_mmio(uint8_t bar_idx, uint32_t common_off, uint32_t isr_off) {
    auto& b = g_mock.bars[bar_idx];
    assert(common_off + sizeof(VirtioPciCommonCfgRegs) <= b.mem.size());
    assert(isr_off < b.mem.size());

    // Ustawienie urzadzenia w pamieci MMIO
    auto* common = reinterpret_cast<VirtioPciCommonCfgRegs*>(&b.mem[common_off]);
    common->num_queues = 4;
    common->config_generation = 1;
    common->device_status = 0;
    common->device_feature = static_cast<uint32_t>(g_mock.device_features & 0xFFFFFFFFU);
    common->config_msix_vector = g_mock.config_msix_vector;
    common->queue_select = 0;
    common->queue_size = g_mock.queues[0].size;
    common->queue_msix_vector = g_mock.queues[0].msix_vector;
    common->queue_enable = g_mock.queues[0].enable;
    common->queue_notify_off = g_mock.queues[0].notify_off;
    b.mem[isr_off] = g_mock.isr_val;
}

uint8_t setup_msix_cap(uint8_t cap_offset, uint8_t next_ptr, uint16_t table_size, uint8_t table_bir, uint32_t table_off, uint8_t pba_bir, uint32_t pba_off) {
    g_mock.msix_cap_offset = cap_offset;
    g_mock.msix_table_bar = table_bir;
    g_mock.msix_table_off = table_off;
    g_mock.msix_table_size = table_size;
    g_mock.msix_enabled = false;

    // Table size in msg_ctrl is (table_size - 1)
    uint16_t msg_ctrl = (table_size > 0) ? static_cast<uint16_t>((table_size - 1) & PCI_MSIX_CTRL_TABLE_SIZE) : 0;
    g_mock.msix_msg_ctrl = msg_ctrl;

    g_mock.pci_config[cap_offset + 0] = PCI_CAP_ID_MSIX; // 0x11
    g_mock.pci_config[cap_offset + 1] = next_ptr;
    std::memcpy(&g_mock.pci_config[cap_offset + 2], &msg_ctrl, 2);

    uint32_t table_reg = (table_off & PCI_MSIX_OFFSET_MASK) | (table_bir & PCI_MSIX_BIR_MASK);
    std::memcpy(&g_mock.pci_config[cap_offset + 4], &table_reg, 4);

    uint32_t pba_reg = (pba_off & PCI_MSIX_OFFSET_MASK) | (pba_bir & PCI_MSIX_BIR_MASK);
    std::memcpy(&g_mock.pci_config[cap_offset + 8], &pba_reg, 4);

    // Initialize table entries in BAR table_bir as masked
    if (table_bir < 6) {
        auto& bar = g_mock.bars[table_bir];
        size_t total_table_bytes = static_cast<size_t>(table_size) * sizeof(PciMsixTableEntry);
        if (table_off + total_table_bytes <= bar.mem.size()) {
            for (uint16_t i = 0; i < table_size; ++i) {
                auto* e = reinterpret_cast<PciMsixTableEntry*>(&bar.mem[table_off + i * sizeof(PciMsixTableEntry)]);
                e->msg_addr_low = 0;
                e->msg_addr_high = 0;
                e->msg_data = 0;
                e->vector_ctrl = 1; // Initially masked
            }
        }
    }

    return cap_offset;
}

uint8_t setup_msi_cap(uint8_t cap_offset, uint8_t next_ptr, bool is_64bit, bool has_pvm, uint8_t mmc_bits = 0) {
    g_mock.msi_cap_offset = cap_offset;
    g_mock.msi_enabled = false;

    uint16_t msg_ctrl = static_cast<uint16_t>((mmc_bits & 0x7) << PCI_MSI_CTRL_MMC_SHIFT);
    if (is_64bit) msg_ctrl |= PCI_MSI_CTRL_64BIT;
    if (has_pvm) msg_ctrl |= PCI_MSI_CTRL_PVM;
    g_mock.msi_msg_ctrl = msg_ctrl;

    g_mock.pci_config[cap_offset + 0] = PCI_CAP_ID_MSI; // 0x05
    g_mock.pci_config[cap_offset + 1] = next_ptr;
    std::memcpy(&g_mock.pci_config[cap_offset + 2], &msg_ctrl, 2);

    return cap_offset;
}

static bool setup_mock_rng_device_msix(VirtioPciDevice* dev, uint16_t table_size = 4, uint8_t table_bar = 1) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps, 0x88);
    setup_msix_cap(0x88, 0, table_size, table_bar, 0x000, table_bar, 0x800);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 16;
    return virtio_pci_probe(0, 4, 0, dev, 0);
}

static bool setup_mock_blk_device_msix(VirtioPciDevice* dev,
                                       uint64_t capacity_sectors = 2048,
                                       uint32_t blk_size = 512,
                                       uint16_t table_size = 4,
                                       uint8_t table_bar = 1) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1042, 0x1AF4, 0x0002);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 64, 0}
    };
    build_cap_chain(caps, 0xA0);
    setup_msix_cap(0xA0, 0, table_size, table_bar, 0x000, table_bar, 0x800);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;

    uint64_t feat = VIRTIO_F_VERSION_1 | VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_SIZE_MAX | VIRTIO_BLK_F_SEG_MAX;
    if (blk_size != 512) {
        feat |= VIRTIO_BLK_F_BLK_SIZE;
    }
    g_mock.device_features = feat;

    std::memcpy(&g_mock.bars[0].mem[0x100], &capacity_sectors, sizeof(capacity_sectors));
    uint32_t size_max = 131072;
    std::memcpy(&g_mock.bars[0].mem[0x108], &size_max, sizeof(size_max));
    uint32_t seg_max = 64;
    std::memcpy(&g_mock.bars[0].mem[0x10C], &seg_max, sizeof(seg_max));
    std::memcpy(&g_mock.bars[0].mem[0x114], &blk_size, sizeof(blk_size));

    return virtio_pci_probe(0, 4, 0, dev, 0);
}

} // namespace

// ============================================================================
// TESTY JEDNOSTKOWE
// ============================================================================

static int g_pass_count = 0;
static int g_fail_count = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::printf("  [FAIL] %s:%d: %s (%s)\n", __FILE__, __LINE__, msg, #cond); \
            g_fail_count++; \
            return; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        const char* filter = std::getenv("BURSZTYN_HOST_TEST_FILTER"); \
        if (filter && !std::strstr(#fn, filter)) break; \
        std::printf("[RUN] %s...\n", #fn); \
        const int before_fail = g_fail_count; \
        fn(); \
        if (g_fail_count == before_fail) { \
            std::printf("  [PASS] %s\n", #fn); \
            g_pass_count++; \
        } \
    } while (0)

// ----------------------------------------------------------------------------
// 1. Poprawne wykrycie urzadzenia Modern VirtIO PCI
// ----------------------------------------------------------------------------
void test_valid_modern_device() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004); // VirtIO-RNG modern ID

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 32, 0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    // Hook synchronizacji przed negocjacja
    auto* common = reinterpret_cast<VirtioPciCommonCfgRegs*>(&g_mock.bars[0].mem[0x000]);
    common->device_feature = static_cast<uint32_t>(g_mock.device_features & 0xFFFFFFFFU);

    // Poniewaz virtio_pci_probe wykonuje:
    // dev->common_cfg->device_feature_select = 0; odczyt; device_feature_select = 1; odczyt;
    // a w mocku oba odczyty czytaja z tej samej pamieci MMIO, ustawiamy w pamieci bit 32
    // w polu wyzszym jesli symulujemy, badz w naszym sterowniku device_feature_select
    // aktualizuje wartosc w pamieci mocka. Zrobimy to w hooku write32:
    const bool ok = virtio_pci_probe(0, 4, 0, &dev, 0);
    TEST_ASSERT(ok, "virtio_pci_probe powienien zwrocic true");
    TEST_ASSERT(dev.initialized, "Urzadzenie powinno byc oznaczone jako initialized");
    TEST_ASSERT(dev.virtio_id == VIRTIO_ID_RNG, "VirtIO ID powinno byc RNG (4)");
    TEST_ASSERT(dev.has_device_cfg, "Urzadzenie powinno miec konfiguracje specyficzna");
    TEST_ASSERT(dev.device_cfg_len == 32, "Dlugosc device_cfg powinna wynosic 32");

    // Status: ACKNOWLEDGE (1) | DRIVER (2) | FEATURES_OK (8) = 11. DRIVER_OK (4) NIE MOZE byc ustawione!
    const uint8_t status = virtio_pci_get_status(&dev);
    TEST_ASSERT((status & VIRTIO_STATUS_ACKNOWLEDGE) != 0, "Status musi zawierac ACKNOWLEDGE");
    TEST_ASSERT((status & VIRTIO_STATUS_DRIVER) != 0, "Status musi zawierac DRIVER");
    TEST_ASSERT((status & VIRTIO_STATUS_FEATURES_OK) != 0, "Status musi zawierac FEATURES_OK");
    TEST_ASSERT((status & VIRTIO_STATUS_DRIVER_OK) == 0, "Status NIE MOZE zawierac DRIVER_OK w generic probe");

    TEST_ASSERT((dev.driver_features & VIRTIO_F_VERSION_1) != 0, "Wynegocjowane cechy musza miec VIRTIO_F_VERSION_1");
}

// ----------------------------------------------------------------------------
// 2. Brak obowiazkowych struktur (COMMON, NOTIFY, ISR)
// ----------------------------------------------------------------------------
void test_missing_mandatory_caps() {
    // a. Brak COMMON_CFG
    {
        g_mock.reset();
        setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
        std::vector<CapBuilderEntry> caps = {
            {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
            {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
        };
        build_cap_chain(caps);
        VirtioPciDevice dev{};
        TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Brak COMMON_CFG musi odrzucic probe");
    }

    // b. Brak NOTIFY_CFG
    {
        g_mock.reset();
        setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
        std::vector<CapBuilderEntry> caps = {
            {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
            {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
        };
        build_cap_chain(caps);
        VirtioPciDevice dev{};
        TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Brak NOTIFY_CFG musi odrzucic probe");
    }

    // c. Brak ISR_CFG
    {
        g_mock.reset();
        setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
        std::vector<CapBuilderEntry> caps = {
            {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
            {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4}
        };
        build_cap_chain(caps);
        VirtioPciDevice dev{};
        TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Brak ISR_CFG musi odrzucic probe");
    }
}

// ----------------------------------------------------------------------------
// 3. Nieprawidlowy indeks BAR (BAR >= 6)
// ----------------------------------------------------------------------------
void test_invalid_bar_index() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 6, 0x000, 56, 0}, // BAR 6 nie istnieje
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    VirtioPciDevice dev{};
    TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "BAR index 6 musi odrzucic probe");
}

// ----------------------------------------------------------------------------
// 4. Obszar capability przekracza rozmiar BAR
// ----------------------------------------------------------------------------
void test_capability_exceeds_bar_bounds() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.bars[0].resize(0x1000); // BAR0 ma tylko 4 KiB

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x800, 0x1000, 4}, // 0x800 + 0x1000 = 0x1800 > 0x1000!
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    VirtioPciDevice dev{};
    TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Przekroczenie granic BAR musi odrzucic probe");
}

// ----------------------------------------------------------------------------
// 5. Arytmetyczne przepelnienie offset + length
// ----------------------------------------------------------------------------
void test_offset_length_overflow() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0xFFFFFFF0U, 0x20U, 0}, // offset + length zawija sie w 32-bit
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    VirtioPciDevice dev{};
    TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Overflow arytmetyczny offset+length musi odrzucic probe");
}

// ----------------------------------------------------------------------------
// 6. Wykrywanie zapetlenia w lancuchu PCI capabilities
// ----------------------------------------------------------------------------
void test_malformed_looped_capability_chain() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);

    // Cap 1 na 0x40 wskazuje na 0x60
    g_mock.pci_config[0x34] = 0x40;
    g_mock.pci_config[0x40 + 0] = 0x09;
    g_mock.pci_config[0x40 + 1] = 0x60; // next = 0x60
    g_mock.pci_config[0x40 + 2] = 16;
    g_mock.pci_config[0x40 + 3] = VIRTIO_PCI_CAP_COMMON_CFG;

    // Cap 2 na 0x60 wskazuje z powrotem na 0x40 (petla!)
    g_mock.pci_config[0x60 + 0] = 0x09;
    g_mock.pci_config[0x60 + 1] = 0x40; // next = 0x40 (petla!)
    g_mock.pci_config[0x60 + 2] = 16;
    g_mock.pci_config[0x60 + 3] = VIRTIO_PCI_CAP_NOTIFY_CFG;

    VirtioPciDevice dev{};
    TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Zapetlony lancuch capabilities musi byc odrzucony");
}

// ----------------------------------------------------------------------------
// 7. Nieprawidlowy wskaznik cap_ptr (< 0x40 lub > 0xFC)
// ----------------------------------------------------------------------------
void test_invalid_cap_ptr_range() {
    // a. cap_ptr < 0x40
    {
        g_mock.reset();
        setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
        g_mock.pci_config[0x34] = 0x20; // Niepoprawny, w naglowku standardowym PCI
        VirtioPciDevice dev{};
        TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "cap_ptr < 0x40 musi byc odrzucony");
    }

    // b. cap_ptr > 0xFC
    {
        g_mock.reset();
        setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
        g_mock.pci_config[0x34] = 0xFE;
        VirtioPciDevice dev{};
        TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "cap_ptr > 0xFC musi byc odrzucony");
    }
}

// ----------------------------------------------------------------------------
// 8. Odrzucenie obcietej struktury capability (cap_len < 16)
// ----------------------------------------------------------------------------
void test_truncated_cap_header() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);

    g_mock.pci_config[0x34] = 0x40;
    g_mock.pci_config[0x40 + 0] = 0x09;
    g_mock.pci_config[0x40 + 1] = 0x00;
    g_mock.pci_config[0x40 + 2] = 12; // Mniej niz sizeof(VirtioPciCapHeader) = 16
    g_mock.pci_config[0x40 + 3] = VIRTIO_PCI_CAP_COMMON_CFG;

    VirtioPciDevice dev{};
    TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Obcieta capability musi byc odrzucona");
}

// ----------------------------------------------------------------------------
// 9. Brak wsparcia dla VIRTIO_F_VERSION_1 (bit 32)
// ----------------------------------------------------------------------------
void test_unsupported_version_1() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.device_features = 0x0000000000000001ULL; // Brak bitu 32 (VIRTIO_F_VERSION_1)

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Brak VERSION_1 musi zakonczyc sie niepowodzeniem probe");
    TEST_ASSERT((virtio_pci_get_status(&dev) & VIRTIO_STATUS_FAILED) != 0, "Status urzadzenia musi miec flage FAILED");
}

// ----------------------------------------------------------------------------
// 10. Odrzucenie cech przez urzadzenie (skasowanie FEATURES_OK)
// ----------------------------------------------------------------------------
void test_device_clears_features_ok() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.device_features = VIRTIO_F_VERSION_1;
    g_mock.accept_features = false; // Urzadzenie odrzuca cechy

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(!virtio_pci_probe(0, 4, 0, &dev, 0), "Odrzucenie FEATURES_OK musi zwrocic false");
    TEST_ASSERT((virtio_pci_get_status(&dev) & VIRTIO_STATUS_FAILED) != 0, "Status musi miec flage FAILED po odrzuceniu");
}

// ----------------------------------------------------------------------------
// 11. Prawidlowa negocjacja cech driver_features
// ----------------------------------------------------------------------------
void test_feature_negotiation() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    // Urzadzenie oferuje VERSION_1 oraz bity 0 i 5
    g_mock.device_features = VIRTIO_F_VERSION_1 | (1ULL << 0) | (1ULL << 5);

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    // Zadamy bitow 0 i 10 (bit 10 nie jest oferowany przez sprzet, bit 5 nie jest zadany)
    const uint64_t requested = (1ULL << 0) | (1ULL << 10);
    const bool ok = virtio_pci_probe(0, 4, 0, &dev, requested);
    TEST_ASSERT(ok, "Probe powinien sie powiesc");

    // Wynegocjowane cechy powinny byc: VERSION_1 | (1ULL << 0)
    const uint64_t expected = VIRTIO_F_VERSION_1 | (1ULL << 0);
    TEST_ASSERT(dev.driver_features == expected, "Wynegocjowane cechy musza byc czescia wspolna + VERSION_1");
}

// ----------------------------------------------------------------------------
// 12. Sekwencja przejsc stanow i reset
// ----------------------------------------------------------------------------
void test_status_and_reset() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    virtio_pci_set_status(&dev, 0x55);
    TEST_ASSERT(virtio_pci_get_status(&dev) == 0x55, "Odczyt statusu musi byc rowny 0x55");

    const bool reset_ok = virtio_pci_reset(&dev);
    TEST_ASSERT(reset_ok, "Reset urzadzenia powinnien sie powiesc");
    TEST_ASSERT(virtio_pci_get_status(&dev) == 0, "Status po resecie musi byc rowny 0");
}

// ----------------------------------------------------------------------------
// 13. Obliczanie adresu powiadomien i obsluga mnoznika z ochrona przed overflow
// ----------------------------------------------------------------------------
void test_notify_address_calculation_and_overflow() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x100, 4}, // offset 0x1000, len 0x100 (256 B), mult 4
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    // a. Prawidlowe przesuniecie: notify_off = 0 -> base + 0
    volatile uint16_t* addr0 = virtio_pci_notify_address(&dev, 0);
    TEST_ASSERT(addr0 != nullptr, "Adres powiadomien dla offset 0 musi byc prawidlowy");
    TEST_ASSERT(addr0 == reinterpret_cast<volatile uint16_t*>(dev.notify_base), "Adres musi byc notify_base");

    // b. Prawidlowe przesuniecie: notify_off = 10 -> base + 40
    volatile uint16_t* addr10 = virtio_pci_notify_address(&dev, 10);
    TEST_ASSERT(addr10 != nullptr, "Adres powiadomien dla offset 10 musi byc prawidlowy");
    TEST_ASSERT(reinterpret_cast<uintptr_t>(addr10) == reinterpret_cast<uintptr_t>(dev.notify_base) + 40, "Przesuniecie musi byc 40");

    // c. Przekroczenie dlugosci notify_cap_len (256 bajtow):
    //    notify_off = 64 -> 64 * 4 = 256. 256 + sizeof(uint16_t) = 258 > 256 -> out of bounds!
    volatile uint16_t* addr_oob = virtio_pci_notify_address(&dev, 64);
    TEST_ASSERT(addr_oob == nullptr, "Adres wychodzacy poza notify_cap_len musi zwrocic nullptr");

    // d. Duzy offset powodujacy overflow:
    volatile uint16_t* addr_huge = virtio_pci_notify_address(&dev, 0xFFFFU);
    TEST_ASSERT(addr_huge == nullptr, "Duzy offset musi zwrocic nullptr");

    // e. Zapis notyfikacji kolejki
    virtio_pci_notify_queue(&dev, 2, 10);
    uint16_t written_val = 0;
    std::memcpy(&written_val, const_cast<const uint16_t*>(addr10), 2);
    TEST_ASSERT(written_val == 2, "Zapis do notify musi zapisac indeks kolejki (2)");
}

// ----------------------------------------------------------------------------
// 14. Rejestry konfiguracji kolejek VirtQueue
// ----------------------------------------------------------------------------
void test_queue_config_registers() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    // Wybor kolejki i zapis parametrow
    virtio_pci_select_queue(&dev, 1);
    TEST_ASSERT(dev.common_cfg->queue_select == 1, "queue_select musi byc rowne 1");

    virtio_pci_set_queue_size(&dev, 128);
    TEST_ASSERT(virtio_pci_get_queue_size(&dev) == 128, "queue_size musi byc rowne 128");

    virtio_pci_set_queue_msix_vector(&dev, 3);
    TEST_ASSERT(virtio_pci_get_queue_msix_vector(&dev) == 3, "queue_msix_vector musi byc rowne 3");

    virtio_pci_set_queue_enable(&dev, 1);
    TEST_ASSERT(virtio_pci_get_queue_enable(&dev) == 1, "queue_enable musi byc rowne 1");

    // Ustawienie 64-bitowych adresow pierscieni
    const uint64_t desc_phys = 0x1122334455667788ULL;
    const uint64_t driver_phys = 0x8877665544332211ULL;
    const uint64_t device_phys = 0xAABBCCDDEEFF0011ULL;

    virtio_pci_set_queue_desc(&dev, desc_phys);
    virtio_pci_set_queue_driver(&dev, driver_phys);
    virtio_pci_set_queue_device(&dev, device_phys);

    uint64_t read_desc = 0, read_driver = 0, read_device = 0;
    const uint8_t* cfg_bytes = reinterpret_cast<const uint8_t*>(const_cast<const void*>(reinterpret_cast<volatile const void*>(dev.common_cfg)));
    std::memcpy(&read_desc, cfg_bytes + 0x20, 8);
    std::memcpy(&read_driver, cfg_bytes + 0x28, 8);
    std::memcpy(&read_device, cfg_bytes + 0x30, 8);

    TEST_ASSERT(read_desc == desc_phys, "queue_desc musi byc zapisane poprawnie");
    TEST_ASSERT(read_driver == driver_phys, "queue_driver musi byc zapisane poprawnie");
    TEST_ASSERT(read_device == device_phys, "queue_device musi byc zapisane poprawnie");
}

// ----------------------------------------------------------------------------
// 15. Dost?p do device-specific config (odczyt/zapis, ochrona granic)
// ----------------------------------------------------------------------------
void test_device_specific_config_access() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x200, 16, 0} // Dlugosc 16 bajtow
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");
    TEST_ASSERT(dev.has_device_cfg, "Musi posiadac device_cfg");
    TEST_ASSERT(dev.device_cfg_len == 16, "Dlugosc musi byc 16");

    // Zapis 8-bit, 16-bit, 32-bit
    virtio_pci_write_device_config8(&dev, 0, 0xAB);
    TEST_ASSERT(virtio_pci_read_device_config8(&dev, 0) == 0xAB, "Zapis/odczyt config8");

    virtio_pci_write_device_config16(&dev, 2, 0x1234);
    TEST_ASSERT(virtio_pci_read_device_config16(&dev, 2) == 0x1234, "Zapis/odczyt config16");

    virtio_pci_write_device_config32(&dev, 4, 0xDEADBEEFU);
    TEST_ASSERT(virtio_pci_read_device_config32(&dev, 4) == 0xDEADBEEFU, "Zapis/odczyt config32");

    // Zapis 64-bit przez 2 x 32-bit
    virtio_pci_write_device_config32(&dev, 8, 0x11223344U);
    virtio_pci_write_device_config32(&dev, 12, 0x55667788U);
    TEST_ASSERT(virtio_pci_read_device_config64(&dev, 8) == 0x5566778811223344ULL, "Odczyt config64");

    // Proba zapisu i odczytu poza granicami (offset >= 16)
    virtio_pci_write_device_config8(&dev, 16, 0xFF); // poza buforem
    TEST_ASSERT(virtio_pci_read_device_config8(&dev, 16) == 0, "Odczyt poza buforem musi zwrocic 0");

    TEST_ASSERT(virtio_pci_read_device_config16(&dev, 15) == 0, "Odczyt 16-bit przy offset 15 musi zwrocic 0");
    TEST_ASSERT(virtio_pci_read_device_config32(&dev, 13) == 0, "Odczyt 32-bit przy offset 13 musi zwrocic 0");
    TEST_ASSERT(virtio_pci_read_device_config64(&dev, 9) == 0, "Odczyt 64-bit przy offset 9 musi zwrocic 0");
}

// ----------------------------------------------------------------------------
// 16. Rejestr ISR
// ----------------------------------------------------------------------------
void test_isr_read() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    g_mock.isr_val = 0x03;
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    TEST_ASSERT(virtio_pci_read_isr(&dev) == 0x03, "Odczyt ISR musi zwrocic 0x03");
}

// ----------------------------------------------------------------------------
// 17. Walidacja mnoznika powiadomien (notify multiplier)
// ----------------------------------------------------------------------------
void test_virtio_pci_notify_multiplier_validation() {
    // 0 jest dozwolone bez wzgledu na flage
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(0, false), "mnoznik 0 bez notification_data");
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(0, true), "mnoznik 0 z notification_data");

    // Potegi dwojki (specyfikacja VirtIO sec 4.1.4.4 wymaga 'even power of 2, or 0')
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(1, false), "mnoznik 1 odrzucony (wymaga parzystej potegi 2)");
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(1, true), "mnoznik 1 z notification_data odrzucony");
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(2, false), "mnoznik 2 bez notification_data");
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(2, true), "mnoznik 2 z notification_data (wymaga % 4 == 0)");
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(4, false), "mnoznik 4 bez notification_data");
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(4, true), "mnoznik 4 z notification_data");
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(8, true), "mnoznik 8 z notification_data");
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(4096, true), "mnoznik 4096");
    TEST_ASSERT(virtio_pci_validate_notify_multiplier(0x100000, true), "mnoznik 1 MiB");

    // Niebedace potegami dwojki
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(3, false), "mnoznik 3 odrzucony");
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(5, false), "mnoznik 5 odrzucony");
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(6, false), "mnoznik 6 odrzucony");
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(12, true), "mnoznik 12 odrzucony (nie potega 2 mimo % 4 == 0)");

    // Ponad limit 1 MiB
    TEST_ASSERT(!virtio_pci_validate_notify_multiplier(0x200000, false), "mnoznik 2 MiB odrzucony");
}

// ----------------------------------------------------------------------------
// 18. Spojny odczyt konfiguracji urzadzenia (config_generation retry)
// ----------------------------------------------------------------------------
void test_virtio_pci_consistent_config_read() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x200, 16, 0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    // Zapisujemy wzorcowe dane w DEVICE_CFG (0x200)
    for (uint8_t i = 0; i < 16; ++i) {
        g_mock.bars[0].mem[0x200 + i] = static_cast<uint8_t>(0xA0 + i);
    }

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    // 1. Spojny odczyt bez zaklocen (generation stabilne)
    uint8_t buf[16]{};
    TEST_ASSERT(virtio_pci_read_device_config_consistent(&dev, 0, buf, 8), "Odczyt spojny 8 bajtow");
    for (uint8_t i = 0; i < 8; ++i) {
        TEST_ASSERT(buf[i] == static_cast<uint8_t>(0xA0 + i), "Dane musza sie zgadzac");
    }

    // 2. Symulacja zmiany generation podczas odczytu (1 zmiana, sukces po ponowieniu)
    std::memset(buf, 0, sizeof(buf));
    g_mock.bump_gen_on_read_count = 1;
    TEST_ASSERT(virtio_pci_read_device_config_consistent(&dev, 0, buf, 8, 4), "Odczyt spojny z 1 ponowieniem");
    for (uint8_t i = 0; i < 8; ++i) {
        TEST_ASSERT(buf[i] == static_cast<uint8_t>(0xA0 + i), "Dane po ponowieniu musza byc poprawne");
    }

    // 3. Symulacja ciaglej niestabilnosci (przekroczenie limitu prob)
    g_mock.always_bump_gen = true;
    TEST_ASSERT(!virtio_pci_read_device_config_consistent(&dev, 0, buf, 8, 4), "Przekroczenie max_retries musi zwrocic false");
    g_mock.always_bump_gen = false;

    // 4. Granice i walidacja parametrow
    TEST_ASSERT(!virtio_pci_read_device_config_consistent(nullptr, 0, buf, 8), "dev=nullptr musi zwrocic false");
    TEST_ASSERT(!virtio_pci_read_device_config_consistent(&dev, 0, nullptr, 8), "dst=nullptr musi zwrocic false");
    TEST_ASSERT(!virtio_pci_read_device_config_consistent(&dev, 0, buf, 0), "len=0 musi zwrocic false");
    TEST_ASSERT(!virtio_pci_read_device_config_consistent(&dev, 10, buf, 10), "offset+length > len musi zwrocic false");
}

// ----------------------------------------------------------------------------
// 19. Tworzenie Split VirtQueue i weryfikacja struktur oraz rejestrow
// ----------------------------------------------------------------------------
void test_virtqueue_create_and_validation() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 64;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki o rozmiarze 64");
    TEST_ASSERT(virtqueue_get_size(vq) == 64, "Rozmiar kolejki = 64");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 64, "Liczba wolnych deskryptorow = 64");
    TEST_ASSERT(!virtqueue_has_used(vq), "Brak uzytych elementow na starcie");

    // Weryfikacja rejestrow kontrolera
    virtio_pci_select_queue(&dev, 0);
    TEST_ASSERT(virtio_pci_get_queue_enable(&dev) == 1, "Kolejka musi byc wlaczona w kontrolerze");
    TEST_ASSERT(virtio_pci_get_queue_size(&dev) == 64, "Rozmiar kolejki w kontrolerze = 64");

    // Weryfikacja 64-bitowych adresow DMA
    TEST_ASSERT(vq->desc_phys != 0, "Adres fizyczny desc nie moze byc 0");
    TEST_ASSERT(vq->driver_phys != 0, "Adres fizyczny driver nie moze byc 0");
    TEST_ASSERT(vq->device_phys != 0, "Adres fizyczny device nie moze byc 0");

    // Wyrownania VirtIO 1.0 (sec 2.6)
    TEST_ASSERT((reinterpret_cast<uintptr_t>(vq->desc) & 15) == 0, "Desc table wyrownane do 16 B");
    TEST_ASSERT((reinterpret_cast<uintptr_t>(vq->avail) & 1) == 0, "Avail ring wyrownane do 2 B");
    TEST_ASSERT((reinterpret_cast<uintptr_t>(vq->used) & 63) == 0, "Used ring wyrownane do 64 B");
    TEST_ASSERT((vq->desc_phys & 15) == 0, "Desc phys wyrownane do 16 B");
    TEST_ASSERT((vq->device_phys & 63) == 0, "Used phys wyrownane do 64 B");

    // Weryfikacja wyczyszczenia struktur
    TEST_ASSERT(vq->avail->flags == 0, "avail flags = 0");
    TEST_ASSERT(vq->avail->idx == 0, "avail idx = 0");
    TEST_ASSERT(vq->used->flags == 0, "used flags = 0");
    TEST_ASSERT(vq->used->idx == 0, "used idx = 0");

    virtqueue_destroy(vq);
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po zniszczeniu");
}

// ----------------------------------------------------------------------------
// 20. Odrzucanie nieprawidlowych rozmiarow kolejki
// ----------------------------------------------------------------------------
void test_virtqueue_invalid_sizes() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    // a. Rozmiar 0
    g_mock.queues[0].size = 0;
    TEST_ASSERT(virtqueue_create(&dev, 0) == nullptr, "Rozmiar 0 musi zostac odrzucony");

    // b. Rozmiary niebedace potega dwojki
    g_mock.queues[0].size = 3;
    TEST_ASSERT(virtqueue_create(&dev, 0) == nullptr, "Rozmiar 3 musi zostac odrzucony");
    g_mock.queues[0].size = 100;
    TEST_ASSERT(virtqueue_create(&dev, 0) == nullptr, "Rozmiar 100 musi zostac odrzucony");
    g_mock.queues[0].size = 1000;
    TEST_ASSERT(virtqueue_create(&dev, 0) == nullptr, "Rozmiar 1000 musi zostac odrzucony");

    // c. Rozmiar > 32768
    g_mock.queues[0].size = 65535;
    TEST_ASSERT(virtqueue_create(&dev, 0) == nullptr, "Rozmiar 65535 musi zostac odrzucony");

    // d. Rozmiar graniczny = 32768 (prawidlowy)
    g_mock.queues[0].size = 32768;
    VirtQueue* vq_max = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq_max != nullptr, "Rozmiar 32768 musi sie powiesc");
    TEST_ASSERT(virtqueue_get_size(vq_max) == 32768, "Rozmiar = 32768");
    virtqueue_destroy(vq_max);

    // e. Kolejka juz wlaczona
    g_mock.queues[0].size = 16;
    g_mock.queues[0].enable = 1;
    TEST_ASSERT(virtqueue_create(&dev, 0) == nullptr, "Kolejka juz wlaczona musi zostac odrzucona");
    g_mock.queues[0].enable = 0;

    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// ----------------------------------------------------------------------------
// 21. Rollback w przypadku bledu alokacji DMA lub odmowy urzadzenia
// ----------------------------------------------------------------------------
void test_virtqueue_failed_allocation_rollback() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    // a. Blad alokacji DMA
    g_mock.queues[0].size = 16;
    g_mock_dma_alloc_fail = true;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq == nullptr, "Blad alokacji DMA musi zwrocic nullptr");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow po bledzie alokacji DMA");
    g_mock_dma_alloc_fail = false;

    // b. Urzadzenie odrzuca queue_enable
    g_mock.allow_queue_enable = false;
    vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq == nullptr, "Odmowa queue_enable musi zwrocic nullptr");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow po odmowie queue_enable");
    virtio_pci_select_queue(&dev, 0);
    TEST_ASSERT(virtio_pci_get_queue_enable(&dev) == 0, "Rejestr queue_enable zresetowany do 0");
    TEST_ASSERT(virtio_pci_get_queue_desc(&dev) == 0, "Rejestr queue_desc zresetowany do 0");
    g_mock.allow_queue_enable = true;

    // c. Przekazanie nullptr
    TEST_ASSERT(virtqueue_create(nullptr, 0) == nullptr, "dev=nullptr musi zwrocic nullptr");
}

// ----------------------------------------------------------------------------
// 22. Zgloszenie pojedynczego deskryptora (virtqueue_submit) i bariery
// ----------------------------------------------------------------------------
void test_virtqueue_submit_single_descriptor() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");

    reset_virtqueue_counters();

    int cookie_val = 12345;
    VirtqBuffer buf{};
    buf.phys_addr = 0x20001000ULL;
    buf.len = 512;
    buf.is_write = false; // device-readable

    const int head = virtqueue_submit(vq, &buf, 1, &cookie_val);
    TEST_ASSERT(head == 0, "Zwrocony indeks glowy musi byc 0");

    // Weryfikacja deskryptora w pamieci
    TEST_ASSERT(vq->desc[0].addr == 0x20001000ULL, "Adres fizyczny w desc[0]");
    TEST_ASSERT(vq->desc[0].len == 512, "Dlugosc w desc[0]");
    TEST_ASSERT(vq->desc[0].flags == 0, "Flagi w desc[0] (brak NEXT i brak WRITE)");
    TEST_ASSERT(vq->desc[0].next == 0, "Pole next w desc[0] = 0");

    // Weryfikacja Available Ring
    TEST_ASSERT(vq->avail->ring[0] == 0, "avail->ring[0] zawiera indeks glowy");
    TEST_ASSERT(vq->avail->idx == 1, "avail->idx zwiekszone do 1");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 15, "Liczba wolnych = 15");

    // Weryfikacja uzycia barier pamieciowych Store-Store (wmb)
    TEST_ASSERT(g_virtqueue_wmb_count >= 2, "Wywolano wmb przed ring[] i przed avail->idx");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 23. Zgloszenie lancucha deskryptorow (descriptor chain)
// ----------------------------------------------------------------------------
void test_virtqueue_submit_chain() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");

    // Lancuch: 2 bufory readable (zadanie) + 1 bufor writable (odpowiedz)
    VirtqBuffer bufs[3]{};
    bufs[0].phys_addr = 0x3000;
    bufs[0].len = 16;
    bufs[0].is_write = false;

    bufs[1].phys_addr = 0x4000;
    bufs[1].len = 512;
    bufs[1].is_write = false;

    bufs[2].phys_addr = 0x5000;
    bufs[2].len = 1;
    bufs[2].is_write = true;

    const int head = virtqueue_submit(vq, bufs, 3, reinterpret_cast<void*>(0xBEEF));
    TEST_ASSERT(head == 0, "Indeks glowy lancucha = 0");

    // Deskryptor 0
    TEST_ASSERT(vq->desc[0].addr == 0x3000, "desc[0] addr");
    TEST_ASSERT(vq->desc[0].len == 16, "desc[0] len");
    TEST_ASSERT(vq->desc[0].flags == VRING_DESC_F_NEXT, "desc[0] flags NEXT");
    TEST_ASSERT(vq->desc[0].next == 1, "desc[0] wskazuje na 1");

    // Deskryptor 1
    TEST_ASSERT(vq->desc[1].addr == 0x4000, "desc[1] addr");
    TEST_ASSERT(vq->desc[1].len == 512, "desc[1] len");
    TEST_ASSERT(vq->desc[1].flags == VRING_DESC_F_NEXT, "desc[1] flags NEXT");
    TEST_ASSERT(vq->desc[1].next == 2, "desc[1] wskazuje na 2");

    // Deskryptor 2 (ostatni)
    TEST_ASSERT(vq->desc[2].addr == 0x5000, "desc[2] addr");
    TEST_ASSERT(vq->desc[2].len == 1, "desc[2] len");
    TEST_ASSERT(vq->desc[2].flags == VRING_DESC_F_WRITE, "desc[2] flags WRITE (brak NEXT)");

    // Available ring
    TEST_ASSERT(vq->avail->ring[0] == 0, "avail->ring publikuje tylko glowe");
    TEST_ASSERT(vq->avail->idx == 1, "avail->idx zwiekszone o 1");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 13, "Liczba wolnych = 13");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 24. Odrzucanie nieprawidlowej kolejnosci buforow, adresu 0 i overflow
// ----------------------------------------------------------------------------
void test_virtqueue_submit_invalid_buffer_order() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");

    // a. Writable PRZED readable (zlamanie VirtIO sec 2.6.4)
    VirtqBuffer bufs_bad_order[2]{};
    bufs_bad_order[0].phys_addr = 0x1000;
    bufs_bad_order[0].len = 64;
    bufs_bad_order[0].is_write = true; // najpierw write

    bufs_bad_order[1].phys_addr = 0x2000;
    bufs_bad_order[1].len = 64;
    bufs_bad_order[1].is_write = false; // potem read - ZABRONIONE!

    TEST_ASSERT(virtqueue_submit(vq, bufs_bad_order, 2, nullptr) == -1, "Write przed read odrzucone");

    // b. Adres fizyczny 0 z dlugoscia > 0
    VirtqBuffer buf_zero_addr{};
    buf_zero_addr.phys_addr = 0;
    buf_zero_addr.len = 64;
    buf_zero_addr.is_write = false;

    TEST_ASSERT(virtqueue_submit(vq, &buf_zero_addr, 1, nullptr) == -3, "Adres fizyczny 0 odrzucony (-3)");

    // c. Przepelnienie arytmetyczne sumarycznej dlugosci buforow
    VirtqBuffer bufs_overflow[2]{};
    bufs_overflow[0].phys_addr = 0x1000;
    bufs_overflow[0].len = 0xFFFFFFF0U;
    bufs_overflow[0].is_write = false;
    bufs_overflow[1].phys_addr = 0x2000;
    bufs_overflow[1].len = 0x20;
    bufs_overflow[1].is_write = false;

    TEST_ASSERT(virtqueue_submit(vq, bufs_overflow, 2, nullptr) == -1, "Overflow dlugosci buforow odrzucone");

    // d. Przekroczenie rozmiaru kolejki
    VirtqBuffer bufs_too_many[17]{};
    for (int i = 0; i < 17; ++i) {
        bufs_too_many[i].phys_addr = 0x1000 + i * 0x1000;
        bufs_too_many[i].len = 64;
    }
    TEST_ASSERT(virtqueue_submit(vq, bufs_too_many, 17, nullptr) == -1, "Zbyt duzo buforow odrzucone");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 25. Wyczerpanie deskryptorow w kolejce
// ----------------------------------------------------------------------------
void test_virtqueue_descriptor_exhaustion() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    // Kolejka o malym rozmiarze 4
    g_mock.queues[0].size = 4;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki o rozmiarze 4");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 4, "4 wolne deskryptory");

    VirtqBuffer bufs[4]{};
    for (int i = 0; i < 4; ++i) {
        bufs[i].phys_addr = 0x1000 + i * 0x1000;
        bufs[i].len = 64;
        bufs[i].is_write = false;
    }

    // 1. Zgloszenie 3 deskryptorow (sukces, 1 wolny)
    TEST_ASSERT(virtqueue_submit(vq, bufs, 3, nullptr) >= 0, "Zgloszenie 3 deskryptorow");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 1, "Pozostal 1 wolny deskryptor");

    // 2. Proba zgloszenia 2 deskryptorow gdy wolny jest tylko 1 (blad -2)
    TEST_ASSERT(virtqueue_submit(vq, bufs, 2, nullptr) == -2, "Brak miejsca na 2 deskryptory musi zwrocic -2");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 1, "Liczba wolnych niezmieniona po bledzie");
    TEST_ASSERT(vq->avail->idx == 1, "avail->idx niezmienione");

    // 3. Zgloszenie 1 deskryptora (sukces, 0 wolnych)
    TEST_ASSERT(virtqueue_submit(vq, bufs, 1, nullptr) >= 0, "Zgloszenie ostatniego deskryptora");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 0, "0 wolnych deskryptorow");

    // 4. Proba zgloszenia przy 0 wolnych (blad -2)
    TEST_ASSERT(virtqueue_submit(vq, bufs, 1, nullptr) == -2, "Wyczerpanie deskryptorow zwraca -2");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 26. Odbieranie zakonczen z Used Ring i bezpieczny recykling deskryptorow
// ----------------------------------------------------------------------------
void test_virtqueue_completion_and_recycling() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");

    int my_cookie = 777;
    VirtqBuffer bufs[2]{};
    bufs[0].phys_addr = 0x1000;
    bufs[0].len = 64;
    bufs[0].is_write = false;

    bufs[1].phys_addr = 0x2000;
    bufs[1].len = 128;
    bufs[1].is_write = true;

    const int head = virtqueue_submit(vq, bufs, 2, &my_cookie);
    TEST_ASSERT(head == 0, "Indeks glowy = 0");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 14, "14 wolnych deskryptorow");
    TEST_ASSERT(!virtqueue_has_used(vq), "Brak zakonczen przed dzialaniem urzadzenia");

    // Symulacja zakonczenia przez urzadzenie
    vq->used->ring[0].id = static_cast<uint32_t>(head);
    vq->used->ring[0].len = 128;
    vq->used->idx = 1;

    reset_virtqueue_counters();
    TEST_ASSERT(virtqueue_has_used(vq), "Pojawilo sie zakonczenie");
    TEST_ASSERT(g_virtqueue_rmb_count >= 1, "virtqueue_has_used uzywa rmb");

    void* cookie_out = nullptr;
    uint32_t len_out = 0;
    const int popped_id = virtqueue_pop_used(vq, &cookie_out, &len_out);

    TEST_ASSERT(popped_id == 0, "Zwrocony ID zakonczenia");
    TEST_ASSERT(cookie_out == &my_cookie, "Zwrocony prawidlowy cookie");
    TEST_ASSERT(len_out == 128, "Zwrocona dlugosc = 128");
    TEST_ASSERT(virtqueue_get_num_free(vq) == 16, "Oba deskryptory zrecyklingowane do puli wolnych");
    TEST_ASSERT(!virtqueue_has_used(vq), "Brak dalszych zakonczen");
    TEST_ASSERT(g_virtqueue_mb_count >= 1, "Bariera spojnosci DMA wywolana w pop_used");

    // Ponowne zgloszenie powinno bez przeszkod uzyc zwolnionych deskryptorow
    const int head2 = virtqueue_submit(vq, &bufs[0], 1, nullptr);
    TEST_ASSERT(head2 >= 0, "Ponowne zgloszenie zakonczone sukcesem");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 27. Zabezpieczenie przed uszkodzonymi zakonczeniami ze strony urzadzenia
// ----------------------------------------------------------------------------
void test_virtqueue_invalid_completions() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");

    VirtqBuffer bufs[2]{};
    bufs[0].phys_addr = 0x1000;
    bufs[0].len = 64;
    bufs[0].is_write = false;
    bufs[1].phys_addr = 0x2000;
    bufs[1].len = 64;
    bufs[1].is_write = true;

    virtqueue_submit(vq, bufs, 2, nullptr); // alokuje deskryptory 0 i 1

    // a. Urzadzenie zglasza id >= queue_size (np. 16)
    vq->used->ring[0].id = 16;
    vq->used->ring[0].len = 64;
    vq->used->idx = 1;
    TEST_ASSERT(virtqueue_pop_used(vq, nullptr, nullptr) == -2, "ID poza zakresem zwraca -2");

    // b. Urzadzenie zglasza niezaalokowany deskryptor (np. 5)
    vq->used->ring[0].id = 5;
    TEST_ASSERT(virtqueue_pop_used(vq, nullptr, nullptr) == -3, "Niezaalokowany deskryptor zwraca -3");

    // c. Urzadzenie zglasza deskryptor, ktory nie jest glowa lancucha (np. 1)
    vq->used->ring[0].id = 1;
    TEST_ASSERT(virtqueue_pop_used(vq, nullptr, nullptr) == -3, "Deskryptor bedacy w srodku lancucha zwraca -3");

    // d. Urzadzenie podaje dlugosc wieksza niz rozmiar bufora
    vq->used->ring[0].id = 0;
    vq->used->ring[0].len = 999999;
    TEST_ASSERT(virtqueue_pop_used(vq, nullptr, nullptr) == -4, "Zbyt duza dlugosc zwraca -4");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 28. Poprawnosc 16-bitowego wraparound ring index (65535 -> 0)
// ----------------------------------------------------------------------------
void test_virtqueue_ring_index_wraparound() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");

    // Ustawienie indekcow tuz przed przekroczeniem 16-bitowego zakresu: 65535
    vq->avail->idx = 65535;
    vq->used->idx = 65535;
    vq->last_used_idx = 65535;

    VirtqBuffer buf{};
    buf.phys_addr = 0x5000;
    buf.len = 64;
    buf.is_write = false;

    // 1. Submit: wpis trafia pod indeks 65535 & 15 == 15, a avail->idx staje sie 0!
    const int head = virtqueue_submit(vq, &buf, 1, reinterpret_cast<void*>(0x123));
    TEST_ASSERT(head >= 0, "Submit przy idx 65535");
    TEST_ASSERT(vq->avail->ring[15] == head, "Wpis pod indeksem 15");
    TEST_ASSERT(vq->avail->idx == 0, "avail->idx zawinal sie do 0!");

    // 2. Urzadzenie konczy zadanie: zapisuje do used->ring[15] i ustawia used->idx = 0
    vq->used->ring[15].id = static_cast<uint32_t>(head);
    vq->used->ring[15].len = 64;
    vq->used->idx = 0;

    // 3. virtqueue_has_used widzi, ze last_used_idx (65535) != used->idx (0)
    TEST_ASSERT(virtqueue_has_used(vq), "has_used dziala przy wraparound (65535 -> 0)");

    // 4. Pop used pobiera element spod indeksu 65535 & 15 == 15 i zwieksza last_used_idx do 0
    void* cookie_out = nullptr;
    const int popped = virtqueue_pop_used(vq, &cookie_out, nullptr);
    TEST_ASSERT(popped == head, "pop_used pobral wlasciwy deskryptor");
    TEST_ASSERT(cookie_out == reinterpret_cast<void*>(0x123), "cookie poprawne");
    TEST_ASSERT(vq->last_used_idx == 0, "last_used_idx zawinal sie do 0!");
    TEST_ASSERT(!virtqueue_has_used(vq), "Brak kolejnych zakonczen");

    // 5. Kolejny submit przy indeksie 0
    const int head2 = virtqueue_submit(vq, &buf, 1, nullptr);
    TEST_ASSERT(head2 >= 0, "Kolejny submit przy idx 0");
    TEST_ASSERT(vq->avail->ring[0] == head2, "Wpis pod indeksem 0");
    TEST_ASSERT(vq->avail->idx == 1, "avail->idx = 1");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 29. Powiadamianie (virtqueue_kick) i tlumienie (event suppression)
// ----------------------------------------------------------------------------
void test_virtqueue_kick_and_suppression() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");

    reset_virtqueue_counters();
    g_mock.notify_count = 0;
    g_mock.last_notified_queue = 0xFFFF;

    // 1. Kick bez tlumienia (used->flags = 0)
    vq->used->flags = 0;
    TEST_ASSERT(virtqueue_kick(vq, false), "Kick bez tlumienia musi zwrocic true");
    TEST_ASSERT(g_virtqueue_mb_count >= 1, "mfence wywolane przed odczytem flag");
    TEST_ASSERT(g_mock.notify_count == 1, "Powiadomienie wyslane do kontrolera");
    TEST_ASSERT(g_mock.last_notified_queue == 0, "Powiadomiono kolejke 0");

    // 2. Kick z tlumieniem (VRING_USED_F_NO_NOTIFY)
    vq->used->flags = VRING_USED_F_NO_NOTIFY;
    TEST_ASSERT(!virtqueue_kick(vq, false), "Kick z tlumieniem musi zwrocic false");
    TEST_ASSERT(g_mock.notify_count == 1, "Liczba powiadomien nie wzrosla");

    // 3. Wymuszony kick (force = true) pomimo tlumienia
    TEST_ASSERT(virtqueue_kick(vq, true), "Wymuszony kick musi powiadomic mimo VRING_USED_F_NO_NOTIFY");
    TEST_ASSERT(g_mock.notify_count == 2, "Wymuszone powiadomienie wyslane do kontrolera");

    virtqueue_destroy(vq);
}

// ----------------------------------------------------------------------------
// 30. Czyszczenie zasobow kolejki (virtqueue_destroy)
// ----------------------------------------------------------------------------
void test_virtqueue_destroy_cleanup() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);

    VirtioPciDevice dev{};
    TEST_ASSERT(virtio_pci_probe(0, 4, 0, &dev, 0), "Probe powinnien sie powiesc");

    g_mock.queues[0].size = 16;
    VirtQueue* vq = virtqueue_create(&dev, 0);
    TEST_ASSERT(vq != nullptr, "Utworzenie kolejki");
    TEST_ASSERT(g_host_dma_allocs.size() == 1, "Dokladnie 1 bufor DMA przydzielony");

    virtqueue_destroy(vq);

    // Weryfikacja zerowania rejestrow i braku wyciekow
    virtio_pci_select_queue(&dev, 0);
    TEST_ASSERT(virtio_pci_get_queue_desc(&dev) == 0, "queue_desc = 0 po destroy");
    TEST_ASSERT(virtio_pci_get_queue_driver(&dev) == 0, "queue_driver = 0 po destroy");
    TEST_ASSERT(virtio_pci_get_queue_device(&dev) == 0, "queue_device = 0 po destroy");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Wszystkie bufory DMA zwolnione bez wyciekow");
}

// ============================================================================
// TESTY STEROWNIKA VIRTIO-RNG
// ============================================================================

static bool setup_mock_rng_device(VirtioPciDevice* dev) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 16;
    return virtio_pci_probe(0, 4, 0, dev, 0);
}

// ----------------------------------------------------------------------------
// 31. VirtIO-RNG: Inicjalizacja i czyszczenie zasobow (init i cleanup)
// ----------------------------------------------------------------------------
void test_virtio_rng_init_and_cleanup() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");

    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");
    TEST_ASSERT(virtio_rng_is_ready(), "Sterownik zgloszony jako gotowy");

    VirtioRngDriver* drv = virtio_rng_get_driver();
    TEST_ASSERT(drv != nullptr, "Dostep do struktury sterownika");
    TEST_ASSERT(drv->initialized, "Flaga initialized == true");
    TEST_ASSERT(!drv->has_error, "Flaga has_error == false");
    TEST_ASSERT((virtio_pci_get_status(&dev) & VIRTIO_STATUS_DRIVER_OK) != 0, "DRIVER_OK ustawiony w kontrolerze");
    TEST_ASSERT(g_host_dma_allocs.size() == 2, "Przydzielono 2 bufory DMA (kolejka + bufor entropii)");

    virtio_rng_reset();
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik nieaktywny po resecie");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Wszystkie bufory DMA zwolnione bez wyciekow");
}

// ----------------------------------------------------------------------------
// 32. VirtIO-RNG: Pojedynczy udany odczyt bajtow
// ----------------------------------------------------------------------------
void test_virtio_rng_read_success() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_fill_byte = 0x42;

    uint8_t buf[16]{};
    size_t n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == sizeof(buf), "Odczytano oczekiwana liczbe bajtow (16)");
    for (size_t i = 0; i < sizeof(buf); ++i) {
        TEST_ASSERT(buf[i] == 0x42, "Poprawna zawartosc bajtow entropii");
    }

    VirtioRngDriver* drv = virtio_rng_get_driver();
    TEST_ASSERT(drv->total_bytes_read == 16, "Statystyka total_bytes_read == 16");
    TEST_ASSERT(drv->total_requests == 1, "Statystyka total_requests == 1");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow DMA po resecie");
}

// ----------------------------------------------------------------------------
// 33. VirtIO-RNG: Wielokrotne powtorzone odczyty i brak wyciekow deskryptorow
// ----------------------------------------------------------------------------
void test_virtio_rng_read_repeated() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_dynamic_bytes = true;

    VirtioRngDriver* drv = virtio_rng_get_driver();
    const uint16_t initial_free = virtqueue_get_num_free(drv->vq);

    uint8_t prev_buf[16]{};
    bool has_prev = false;

    for (int iter = 0; iter < 64; ++iter) {
        uint8_t buf[16]{};
        size_t n = virtio_rng_read(buf, sizeof(buf));
        TEST_ASSERT(n == sizeof(buf), "Kazdy odczyt zwraca pelny rozmiar");

        if (has_prev) {
            TEST_ASSERT(std::memcmp(buf, prev_buf, sizeof(buf)) != 0, "Kolejne bloki nie moga byc identyczne");
        }
        std::memcpy(prev_buf, buf, sizeof(buf));
        has_prev = true;

        TEST_ASSERT(virtqueue_get_num_free(drv->vq) == initial_free, "Brak wycieku deskryptorow w kolejce");
    }

    TEST_ASSERT(drv->total_bytes_read == 64 * 16, "Calkowita liczba odczytanych bajtow");
    TEST_ASSERT(drv->total_requests == 64, "Liczba wykonanych zadan");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// ----------------------------------------------------------------------------
// 34. VirtIO-RNG: Czesciowa realizacja zadania (partial completion)
// ----------------------------------------------------------------------------
void test_virtio_rng_read_partial() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_override_len = 10; // Urzadzenie zwraca tylko 10 bajtow z zadanych 32
    g_mock.rng_fill_byte = 0x7E;

    uint8_t buf[32];
    std::memset(buf, 0xAA, sizeof(buf));

    size_t n = virtio_rng_read_bytes(buf, sizeof(buf), 100);
    TEST_ASSERT(n == 10, "Zwr?cono dokladnie 10 bajtow");
    for (size_t i = 0; i < 10; ++i) {
        TEST_ASSERT(buf[i] == 0x7E, "Bajt odczytany z kontrolera poprawny");
    }
    for (size_t i = 10; i < 32; ++i) {
        TEST_ASSERT(buf[i] == 0, "Nieodczytane bajty bufora docelowego wyzerowane (brak ekspozycji starych danych)");
    }

    TEST_ASSERT(virtio_rng_is_ready(), "Sterownik pozostaje gotowy po poprawnym odczycie czesciowym");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// ----------------------------------------------------------------------------
// 35. VirtIO-RNG: Niepoprawna dlugosc zgloszona przez urzadzenie (len > requested)
// ----------------------------------------------------------------------------
void test_virtio_rng_read_invalid_len() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_override_len = 100; // Urzadzenie raportuje 100 bajtow (wiecej niz bufor 16 B)

    uint8_t buf[16]{};
    size_t n = virtio_rng_read_bytes(buf, sizeof(buf), 100000);
    TEST_ASSERT(n == 0, "Naruszenie dlugosci musi zwrocic 0 bajtow");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik przechodzi w stan bledu (fail-closed)");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po bledzie naruszenia dlugosci");
}

// ----------------------------------------------------------------------------
// 36. VirtIO-RNG: Timeout oczekiwania na urzadzenie (brak odpowiedzi sprzetu)
// ----------------------------------------------------------------------------
void test_virtio_rng_read_timeout() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_skip_reply = true; // Mock ignoruje zadanie, symulujac zawieszenie urzadzenia

    uint8_t buf[16]{};
    size_t n = virtio_rng_read_bytes(buf, sizeof(buf), 100);
    TEST_ASSERT(n == 0, "Timeout musi zwrocic 0 bajtow");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik wykonuje fail-closed reset po timeoucie");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po timeoucie");
}

// ----------------------------------------------------------------------------
// 37. VirtIO-RNG: Awaria urzadzenia przy ustawianiu DRIVER_OK (rollback)
// ----------------------------------------------------------------------------
void test_virtio_rng_device_failure_driver_ok() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");

    g_mock.allow_driver_ok = false; // Kontroler odrzuca bit DRIVER_OK

    TEST_ASSERT(!virtio_rng_init(&dev), "Inicjalizacja musi zawiesc w przypadku odrzucenia DRIVER_OK");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik nie moze byc gotowy");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Wszystkie zaalokowane bufory wycofane (zero wyciekow)");
}

// ----------------------------------------------------------------------------
// 38. VirtIO-RNG: Wyczerpanie deskryptorow kolejki
// ----------------------------------------------------------------------------
void test_virtio_rng_queue_exhaustion() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    VirtioRngDriver* drv = virtio_rng_get_driver();
    VirtqBuffer dummy_buf{};
    dummy_buf.phys_addr = drv->dma_physical;
    dummy_buf.len = 16;
    dummy_buf.is_write = true;

    while (virtqueue_get_num_free(drv->vq) > 0) {
        int id = virtqueue_submit(drv->vq, &dummy_buf, 1, nullptr);
        TEST_ASSERT(id >= 0, "Zgloszenie dummy deskryptora");
    }

    uint8_t buf[16]{};
    size_t n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == 0, "Brak deskryptorow musi bezpiecznie zwrocic 0 bajtow");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po wyczerpaniu kolejki");
}

// ----------------------------------------------------------------------------
// 39. VirtIO-RNG: Reset i ponowna inicjalizacja na tym samym urzadzeniu
// ----------------------------------------------------------------------------
void test_virtio_rng_reset_and_reinit() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_dynamic_bytes = true;

    // Cykl 1
    TEST_ASSERT(virtio_rng_init(&dev), "Init 1");
    uint8_t buf1[16]{};
    TEST_ASSERT(virtio_rng_read(buf1, sizeof(buf1)) == sizeof(buf1), "Read 1");
    virtio_rng_reset();
    TEST_ASSERT(!virtio_rng_is_ready(), "Niegotowy po resecie 1");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow po resecie 1");

    // Cykl 2
    TEST_ASSERT(virtio_rng_init(&dev), "Init 2");
    uint8_t buf2[16]{};
    TEST_ASSERT(virtio_rng_read(buf2, sizeof(buf2)) == sizeof(buf2), "Read 2");
    virtio_rng_reset();
    TEST_ASSERT(!virtio_rng_is_ready(), "Niegotowy po resecie 2");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow po resecie 2");
}

// ----------------------------------------------------------------------------
// 40. VirtIO-RNG: Test obciazeniowy (stress test) z 3 cyklami i 3072 transakcjami
// ----------------------------------------------------------------------------
void test_virtio_rng_stress_host() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_dynamic_bytes = true;

    TEST_ASSERT(virtio_rng_stress_test(&dev), "virtio_rng_stress_test wykonany pomyslnie");
    TEST_ASSERT(virtio_rng_is_ready(), "Sterownik pozostaje w stanie gotowosci po stress tescie");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po zakonczeniu calego testu obciazeniowego");
}

// ----------------------------------------------------------------------------
// 41. VirtIO-RNG: Niezgodnosc ID zwroconego deskryptora (descriptor ID mismatch)
// ----------------------------------------------------------------------------
void test_virtio_rng_descriptor_id_mismatch() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_override_desc_id = 99; // Kontroler zwraca falszywe ID deskryptora

    uint8_t buf[16]{};
    size_t n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == 0, "Descriptor ID mismatch musi zwrocic 0 bajtow");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik wykonuje fail-closed reset po niespojnosci deskryptora");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po bledzie niespojnosci deskryptora");
}

// ----------------------------------------------------------------------------
// 42. VirtIO-RNG: Czyszczenie i rollback zasobow po awarii alokacji DMA podczas init
// ----------------------------------------------------------------------------
void test_virtio_rng_cleanup_after_init_failure() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");

    // Wymuszenie bledu podczas alokacji bufora DMA entropii (alokacja #2)
    g_mock_dma_alloc_fail_at = 2;
    g_mock_dma_alloc_count = 0;

    TEST_ASSERT(!virtio_rng_init(&dev), "Inicjalizacja musi zwrocic false przy awarii DMA bufora");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik nie moze byc aktywny");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Wszystkie zaalokowane struktury VirtQueue zwolnione (zero wyciekow)");

    g_mock_dma_alloc_fail_at = 0;
    g_mock_dma_alloc_count = 0;
}

// ----------------------------------------------------------------------------
// 43. VirtIO-RNG: Awaria urzadzenia w trakcie dzialania (runtime device failure)
// ----------------------------------------------------------------------------
void test_virtio_rng_device_failure_runtime() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    // Symulacja zgloszenia bledu FAILED przez kontroler
    virtio_pci_set_status(&dev, VIRTIO_STATUS_FAILED);

    TEST_ASSERT(!virtio_rng_is_ready(), "virtio_rng_is_ready wykrywa stan FAILED kontrolera");
    uint8_t buf[16]{};
    size_t n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == 0, "Odczyt z urzadzenia w stanie bledu zwraca 0 bajtow");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po awarii urzadzenia");
}

// ----------------------------------------------------------------------------
// 44. VirtIO-RNG: Odrzucenie used.len > submitted buffer length jako korupcji
// ----------------------------------------------------------------------------
void test_virtio_rng_oversized_used_len_rejected() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    // Przypadek 1: Zadanie na 16 B, urzadzenie raportuje 17 B (wiecej niz submitted chunk)
    g_mock.rng_override_len = 17;

    uint8_t buf[16];
    std::memset(buf, 0xEE, sizeof(buf));

    size_t n = virtio_rng_read_bytes(buf, sizeof(buf), 100);
    TEST_ASSERT(n == 0, "Odrzucenie zgloszenia used.len > chunk zwraca 0 bajtow");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik w stanie bledu (fail-closed) po korupcji dlugosci");
    for (size_t i = 0; i < sizeof(buf); ++i) {
        TEST_ASSERT(buf[i] == 0, "Bufor docelowy bezpiecznie wyzerowany po korupcji");
    }

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po korupcji dlugosci");

    // Przypadek 2: Zadanie na 64 B (pelny bufor), urzadzenie raportuje 65 B
    TEST_ASSERT(virtio_rng_init(&dev), "Re-inicjalizacja po resecie");
    g_mock.rng_override_len = 65;

    uint8_t buf64[64];
    std::memset(buf64, 0xCC, sizeof(buf64));
    n = virtio_rng_read(buf64, sizeof(buf64));
    TEST_ASSERT(n == 0, "used.len > 64 B odrzucone bez clampowania");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik w stanie bledu");
    for (size_t i = 0; i < sizeof(buf64); ++i) {
        TEST_ASSERT(buf64[i] == 0, "Bufor docelowy 64 B bezpiecznie wyzerowany");
    }

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// ----------------------------------------------------------------------------
// 45. VirtIO-RNG: Prawdziwy pomocnik secure-zero (nieoptymalizowalny przez kompilator)
// ----------------------------------------------------------------------------
void test_virtio_rng_secure_zero_helper() {
    // 1. Sprawdzenie z zerowym wskaznikiem i zerowa dlugoscia (brak crasha)
    virtio_rng_secure_zero(nullptr, 128);
    virtio_rng_secure_zero(nullptr, 0);

    uint8_t dummy = 0x55;
    virtio_rng_secure_zero(&dummy, 0);
    TEST_ASSERT(dummy == 0x55, "Dlugosc 0 nie modyfikuje pamieci");

    // 2. Zerowanie roznych rozmiarow (wyrownane i niewyrownane)
    const size_t test_sizes[] = { 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 256 };
    for (size_t sz : test_sizes) {
        std::vector<uint8_t> mem(sz, 0xA5);
        virtio_rng_secure_zero(mem.data(), sz);
        bool all_zero = true;
        for (size_t i = 0; i < sz; ++i) {
            if (mem[i] != 0) {
                all_zero = false;
                break;
            }
        }
        TEST_ASSERT(all_zero, "Wszystkie bajty bezpiecznie wyzerowane dla danego rozmiaru");
    }
}

// ----------------------------------------------------------------------------
// 46. VirtIO-RNG: Wyga?ni?cie deadline w czasie monotonicznym
// ----------------------------------------------------------------------------
void test_virtio_rng_timeout_deadline_expiry() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_skip_reply = true; // Symulacja braku odpowiedzi kontrolera

    // Wlaczenie symulowanego zegara monotonicznego
    g_host_use_mock_time = true;
    g_host_mock_time_ns = 1000000000ULL; // 1.0 s

    // Zadanie z deadline 10 ms (10,000,000 ns).
    // Gdy mock time zostanie przestawiony o 20 ms w przod, deadline monotoniczny natychmiast wygasa.
    g_host_mock_time_ns += 20000000ULL; // 20 ms pozniej

    uint8_t buf[16];
    std::memset(buf, 0x55, sizeof(buf));
    size_t n = virtio_rng_read_bytes(buf, sizeof(buf), 10); // 10 ms timeout
    TEST_ASSERT(n == 0, "Przekroczenie deadline monotonicznego zwraca 0 bajtow");
    TEST_ASSERT(!virtio_rng_is_ready(), "Fail-closed po przekroczeniu deadline monotonicznego");
    for (size_t i = 0; i < sizeof(buf); ++i) {
        TEST_ASSERT(buf[i] == 0, "Bufor docelowy wyzerowany po timeoucie");
    }

    g_host_use_mock_time = false;
    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// ----------------------------------------------------------------------------
// 47. VirtIO-RNG: Timeout procedury resetu sprzetowego (brak statusu 0)
// ----------------------------------------------------------------------------
void test_virtio_rng_reset_timeout() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    // Kontroler symuluje zawieszenie: ignoruje wpisanie 0 do device_status
    g_mock.ignore_reset = true;

    // virtio_rng_reset wykonuje bounded wait z deadline 100 ms
    bool ok = virtio_rng_reset();
    TEST_ASSERT(!ok, "virtio_rng_reset musi zwrocic false w przypadku timeoutu resetu");
    TEST_ASSERT(!virtio_rng_is_ready(), "Sterownik nie moze byc gotowy po nieudanym resecie");

    VirtioRngDriver* drv = virtio_rng_get_driver();
    TEST_ASSERT(drv->has_error, "Flaga has_error aktywna po nieudanym resecie");

    // Przywrocenie normalnego zachowania resetu, aby zakonczyc test bez wyciekow
    g_mock.ignore_reset = false;
    TEST_ASSERT(virtio_rng_reset(), "virtio_rng_reset z potwierdzonym statusem 0 konczy sie sukcesem");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Pamiec DMA zwolniona po potwierdzonym resecie");
}

// ----------------------------------------------------------------------------
// 48. VirtIO-RNG: Pamiec DMA NIE jest zwalniana przed potwierdzonym resetem (kwarantanna)
// ----------------------------------------------------------------------------
void test_virtio_rng_dma_not_freed_before_confirmed_reset() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    // Przed resetem bufor DMA kolejki i entropii sa aktywne
    TEST_ASSERT(!g_host_dma_allocs.empty(), "Zaalokowane bufory DMA aktywne");
    const size_t alloc_count_before = g_host_dma_allocs.size();

    // Symulacja awarii resetu urzadzenia: status 0 nigdy sie nie pojawia
    g_mock.ignore_reset = true;
    bool reset_res = virtio_rng_reset();
    TEST_ASSERT(!reset_res, "Reset nieudany ze wzgledu na brak odpowiedzi sprzetu");

    // KWARANTANNA: Pamiec DMA NIE MOZE zostac zwolniona, poniewaz kontroler nie potwierdzil zatrzymania!
    TEST_ASSERT(!g_host_dma_allocs.empty(), "Kwarantanna DMA: pamiec DMA NIE zostala zwolniona");
    TEST_ASSERT(g_host_dma_allocs.size() == alloc_count_before, "Liczba alokacji nienaruszona (brak czesciowego uwolnienia)");

    // Teraz sprzet potwierdza reset
    g_mock.ignore_reset = false;
    TEST_ASSERT(virtio_rng_reset(), "Ponowny reset z potwierdzeniem statusu 0");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Wszystkie bufory DMA bezpiecznie zwolnione po potwierdzonym resecie");
}

// ----------------------------------------------------------------------------
// 49. VirtIO-RNG: Wspolbiezne odczyty entropii (serializacja blokada sterownika)
// ----------------------------------------------------------------------------
void test_virtio_rng_concurrent_requests() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_dynamic_bytes = true;

    constexpr int NUM_THREADS = 4;
    constexpr int REQS_PER_THREAD = 25;
    constexpr size_t READ_SIZE = 32;

    std::vector<std::thread> threads;
    std::vector<bool> thread_success(NUM_THREADS, false);

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([t, &thread_success]() {
            bool ok = true;
            for (int r = 0; r < REQS_PER_THREAD; ++r) {
                uint8_t buf[READ_SIZE];
                std::memset(buf, 0, sizeof(buf));
                size_t n = virtio_rng_read(buf, sizeof(buf));
                if (n != READ_SIZE) {
                    ok = false;
                    break;
                }
                bool has_data = false;
                for (size_t b = 0; b < READ_SIZE; ++b) {
                    if (buf[b] != 0) {
                        has_data = true;
                        break;
                    }
                }
                if (!has_data) {
                    ok = false;
                    break;
                }
            }
            thread_success[t] = ok;
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    for (int t = 0; t < NUM_THREADS; ++t) {
        TEST_ASSERT(thread_success[t], "Watek zakonczyl wszystkie zlecenia entropii pomyslnie");
    }

    VirtioRngDriver* drv = virtio_rng_get_driver();
    TEST_ASSERT(drv->total_requests == NUM_THREADS * REQS_PER_THREAD,
                "Calkowita liczba zadan rowna sumie zadan ze wszystkich watkow");
    TEST_ASSERT(drv->total_bytes_read == NUM_THREADS * REQS_PER_THREAD * READ_SIZE,
                "Calkowita liczba bajtow rowna sumie pobranych bajtow ze wszystkich watkow");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po wspolbieznych odczytach");
}

// ----------------------------------------------------------------------------
// 50. VirtIO-RNG: Zachowanie licznikow 64-bitowych i saturacja przy UINT64_MAX
// ----------------------------------------------------------------------------
void test_virtio_rng_counter_saturation() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device(&dev), "Probe urzadzenia VirtIO-RNG");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG");

    g_mock.rng_auto_process = true;
    g_mock.rng_fill_byte = 0x33;

    VirtioRngDriver* drv = virtio_rng_get_driver();
    TEST_ASSERT(drv != nullptr, "Dostep do sterownika");

    // 1. Sprawdzenie wartosci powyzej limitu 32-bit (0xFFFFFFFF)
    drv->total_bytes_read = 0x100000000ULL; // 4 GiB
    drv->total_requests = 0x200000000ULL;

    uint8_t buf[16]{};
    size_t n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == sizeof(buf), "Odczyt powiodl sie");
    TEST_ASSERT(drv->total_bytes_read == 0x100000000ULL + 16, "Poprawna inkrementacja powyzej 32 bitow");
    TEST_ASSERT(drv->total_requests == 0x200000000ULL + 1, "Poprawna inkrementacja zadan powyzej 32 bitow");

    // 2. Saturacja total_bytes_read przy UINT64_MAX (brak cichego wrapa do zera)
    drv->total_bytes_read = UINT64_MAX - 5;
    n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == sizeof(buf), "Odczyt 16 bajtow przy granicy saturacji");
    TEST_ASSERT(drv->total_bytes_read == UINT64_MAX, "total_bytes_read osiaga saturacje przy UINT64_MAX");

    // 3. Saturacja total_requests przy UINT64_MAX
    drv->total_requests = UINT64_MAX;
    n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == sizeof(buf), "Odczyt przy saturacji requests");
    TEST_ASSERT(drv->total_requests == UINT64_MAX, "total_requests pozostaje na UINT64_MAX bez overflow");

    virtio_rng_reset();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// ============================================================================
// TESTY STEROWNIKA VIRTIO-BLK
// ============================================================================

static bool setup_mock_blk_device(VirtioPciDevice* dev,
                                  uint64_t capacity_sectors = 2048,
                                  uint32_t blk_size = 512,
                                  uint64_t extra_features = (VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_SIZE_MAX | VIRTIO_BLK_F_SEG_MAX)) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1042, 0x1AF4, 0x0002);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 64, 0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;

    uint64_t feat = VIRTIO_F_VERSION_1 | extra_features;
    if (blk_size != 512) {
        feat |= VIRTIO_BLK_F_BLK_SIZE;
    }
    g_mock.device_features = feat;

    // Device config in BAR 0 at 0x100:
    std::memcpy(&g_mock.bars[0].mem[0x100], &capacity_sectors, sizeof(capacity_sectors));
    uint32_t size_max = 131072;
    std::memcpy(&g_mock.bars[0].mem[0x108], &size_max, sizeof(size_max));
    uint32_t seg_max = 64;
    std::memcpy(&g_mock.bars[0].mem[0x10C], &seg_max, sizeof(seg_max));
    std::memcpy(&g_mock.bars[0].mem[0x114], &blk_size, sizeof(blk_size));

    uint64_t disk_bytes = capacity_sectors * 512ULL;
    g_mock.blk_disk_data.assign(disk_bytes, 0);
    g_mock.blk_auto_process = true;
    g_mock.blk_skip_reply = false;
    g_mock.blk_override_len = -1;
    g_mock.blk_override_desc_id = -1;
    g_mock.blk_override_status = -1;
    g_mock.blk_processed_count = 0;

    return virtio_pci_probe(0, 5, 0, dev, 0);
}

// ----------------------------------------------------------------------------
// 51. VirtIO-BLK: Inicjalizacja i czyszczenie zasobow (init i reset)
// ----------------------------------------------------------------------------
void test_virtio_blk_init_and_cleanup() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev), "Probe urzadzenia VirtIO-BLK");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja sterownika VirtIO-BLK");
    TEST_ASSERT(virtio_blk_get_count() == 1, "Zarejestrowano dokladnie 1 dysk VirtIO-BLK");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(drv != nullptr, "Dostep do struktury sterownika");
    TEST_ASSERT(drv->initialized, "Sterownik initialized == true");
    TEST_ASSERT(!drv->has_error, "Sterownik has_error == false");
    TEST_ASSERT(drv->total_sectors == 2048, "Liczba sektorow == 2048");
    TEST_ASSERT(drv->logical_sector_size == 512, "Rozmiar sektora logicznego == 512");
    TEST_ASSERT(drv->bdev.total_sectors == 2048, "BlockDevice total_sectors == 2048");
    TEST_ASSERT(drv->bdev.device_type == BlockDeviceType::VirtIO, "Typ urzadzenia BlockDeviceType::VirtIO");
    TEST_ASSERT((virtio_pci_get_status(&dev) & VIRTIO_STATUS_DRIVER_OK) != 0, "DRIVER_OK w urzadzeniu");
    TEST_ASSERT(g_host_dma_allocs.size() == 3, "3 alokacje DMA (kolejka + ctrl + bounce data)");

    TEST_ASSERT(virtio_blk_reset(drv), "Reset urzadzenia VirtIO-BLK");
    TEST_ASSERT(!drv->initialized, "Sterownik nieaktywny po resecie");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po resecie dysku");
    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 52. VirtIO-BLK: Obsluga niestandardowego rozmiaru sektora (np. 4096 B)
// ----------------------------------------------------------------------------
void test_virtio_blk_custom_block_size() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 4096), "Probe urzadzenia z sektorem 4096");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku 4096 B");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(drv != nullptr, "Dostep do sterownika");
    TEST_ASSERT(drv->logical_sector_size == 4096, "Logiczny sektor 4096 B");
    TEST_ASSERT(drv->total_sectors == 256, "total_sectors = (2048 * 512) / 4096 = 256");
    TEST_ASSERT(drv->max_transfer_sectors == 32, "max_transfer_sectors = 128 KiB / 4096 = 32");

    uint8_t write_buf[4096];
    for (size_t i = 0; i < sizeof(write_buf); ++i) write_buf[i] = static_cast<uint8_t>((i + 0x33) & 0xFF);
    TEST_ASSERT(drv->bdev.write(2, 1, write_buf) == BlockError::Ok, "Zapis 1 bloku 4096 B na LBA 2");

    uint8_t read_buf[4096]{};
    TEST_ASSERT(drv->bdev.read(2, 1, read_buf) == BlockError::Ok, "Odczyt 1 bloku 4096 B z LBA 2");
    TEST_ASSERT(std::memcmp(write_buf, read_buf, sizeof(write_buf)) == 0, "Poprawnosc danych sektora 4096 B");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 53. VirtIO-BLK: Odczyt, zapis, flush i spojnosc danych
// ----------------------------------------------------------------------------
void test_virtio_blk_read_write_flush() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    uint8_t wbuf[1024];
    for (size_t i = 0; i < sizeof(wbuf); ++i) wbuf[i] = static_cast<uint8_t>((i * 13 + 0x77) & 0xFF);

    TEST_ASSERT(drv->bdev.write(10, 2, wbuf) == BlockError::Ok, "Zapis 2 sektorow na LBA 10");
    TEST_ASSERT(drv->bdev.flush() == BlockError::Ok, "Wykonanie flush");

    uint8_t rbuf[1024]{};
    TEST_ASSERT(drv->bdev.read(10, 2, rbuf) == BlockError::Ok, "Odczyt 2 sektorow z LBA 10");
    TEST_ASSERT(std::memcmp(wbuf, rbuf, sizeof(wbuf)) == 0, "Dane po readbacku identyczne");

    TEST_ASSERT(drv->total_writes == 1, "total_writes == 1");
    TEST_ASSERT(drv->total_reads == 1, "total_reads == 1");
    TEST_ASSERT(drv->total_flushes == 1, "total_flushes == 1");
    TEST_ASSERT(drv->total_sectors_written == 2, "total_sectors_written == 2");
    TEST_ASSERT(drv->total_sectors_read == 2, "total_sectors_read == 2");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 54. VirtIO-BLK: Automatyczne dzielenie transferow (chunking) przez BlockDevice
// ----------------------------------------------------------------------------
void test_virtio_blk_chunking_large_transfer() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 4096, 512), "Probe dysku 4096 sektorow");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    std::vector<uint8_t> big_w(300 * 512);
    for (size_t i = 0; i < big_w.size(); ++i) big_w[i] = static_cast<uint8_t>((i * 3 + 1) & 0xFF);

    TEST_ASSERT(drv->bdev.write(0, 300, big_w.data()) == BlockError::Ok, "Zapis 300 sektorow (chunking)");
    TEST_ASSERT(drv->total_writes == 2, "Zadanie 300 sektorow podzielone na 2 operacje sterownika (256 + 44)");

    std::vector<uint8_t> big_r(300 * 512, 0);
    TEST_ASSERT(drv->bdev.read(0, 300, big_r.data()) == BlockError::Ok, "Odczyt 300 sektorow (chunking)");
    TEST_ASSERT(drv->total_reads == 2, "Odczyt podzielony na 2 operacje sterownika (256 + 44)");
    TEST_ASSERT(std::memcmp(big_w.data(), big_r.data(), big_w.size()) == 0, "Spojnosc duzego transferu");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 55. VirtIO-BLK: Obsluga nosnika tylko do odczytu (VIRTIO_BLK_F_RO)
// ----------------------------------------------------------------------------
void test_virtio_blk_read_only_device() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512, VIRTIO_BLK_F_RO | VIRTIO_BLK_F_FLUSH), "Probe dysku R/O");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku R/O");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(drv->read_only, "Sterownik read_only == true");
    TEST_ASSERT(drv->bdev.read_only, "BlockDevice read_only == true");
    TEST_ASSERT(!drv->bdev.writable, "BlockDevice writable == false");

    uint8_t buf[512]{};
    TEST_ASSERT(drv->bdev.write(0, 1, buf) == BlockError::Unsupported, "Zapis odrzucony jako Unsupported dla dysku R/O");
    TEST_ASSERT(drv->bdev.read(0, 1, buf) == BlockError::Ok, "Odczyt z dysku R/O dozwolony");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 56. VirtIO-BLK: Odrzucenie flush gdy VIRTIO_BLK_F_FLUSH nie zostalo wynegocjowane
// ----------------------------------------------------------------------------
void test_virtio_blk_unsupported_flush() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512, 0), "Probe dysku bez FLUSH");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku bez FLUSH");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(!drv->has_flush, "has_flush == false");
    TEST_ASSERT(drv->bdev.flush() == BlockError::Unsupported, "flush() zwraca Unsupported");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 57. VirtIO-BLK: Obsluga bledu sprzetowego (VIRTIO_BLK_S_IOERR)
// ----------------------------------------------------------------------------
void test_virtio_blk_io_error_handling() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    g_mock.blk_override_status = VIRTIO_BLK_S_IOERR;

    uint8_t buf[512];
    std::memset(buf, 0xEE, sizeof(buf));
    TEST_ASSERT(drv->bdev.read(0, 1, buf) == BlockError::IOError, "read zwraca IOError");
    TEST_ASSERT(buf[0] == 0 && buf[511] == 0, "Bufor docelowy bezpiecznie wyzerowany po IOError");

    TEST_ASSERT(drv->bdev.write(0, 1, buf) == BlockError::IOError, "write zwraca IOError");
    TEST_ASSERT(drv->bdev.flush() == BlockError::IOError, "flush zwraca IOError");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 58. VirtIO-BLK: Obsluga kodu statusu VIRTIO_BLK_S_UNSUPP
// ----------------------------------------------------------------------------
void test_virtio_blk_unsupported_status() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    g_mock.blk_override_status = VIRTIO_BLK_S_UNSUPP;

    uint8_t buf[512];
    std::memset(buf, 0xDD, sizeof(buf));
    TEST_ASSERT(drv->bdev.read(0, 1, buf) == BlockError::Unsupported, "read zwraca Unsupported");
    TEST_ASSERT(buf[0] == 0 && buf[511] == 0, "Bufor wyzerowany po Unsupported read");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 59. VirtIO-BLK: Ochrona granic LBA i arytmetyka 64-bitowa bez overflow
// ----------------------------------------------------------------------------
void test_virtio_blk_bounds_and_overflow() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku 2048 sektorow");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    uint8_t buf[512]{};

    TEST_ASSERT(drv->bdev.read(2048, 1, buf) == BlockError::OutOfRange, "Odczyt poza pojemnoscia");
    TEST_ASSERT(drv->bdev.read(2047, 2, buf) == BlockError::OutOfRange, "Odczyt przekraczajacy pojemnosc");
    TEST_ASSERT(drv->bdev.read(UINT64_MAX, 1, buf) == BlockError::OutOfRange, "Odczyt UINT64_MAX");
    TEST_ASSERT(drv->bdev.read(0, 0, buf) == BlockError::InvalidArgument, "Odczyt 0 sektorow");
    TEST_ASSERT(drv->bdev.read(0, 1, nullptr) == BlockError::InvalidArgument, "Odczyt do nullptr");
    TEST_ASSERT(drv->bdev.write(2048, 1, buf) == BlockError::OutOfRange, "Zapis poza pojemnoscia");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 60. VirtIO-BLK: Timeout oczekiwania na zakonczenie i reset urzadzenia
// ----------------------------------------------------------------------------
void test_virtio_blk_timeout_and_reset() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    g_mock.blk_skip_reply = true;

    uint8_t buf[512]{};
    TEST_ASSERT(drv->bdev.read(0, 1, buf) == BlockError::Timeout, "read zwraca Timeout");
    TEST_ASSERT(!drv->initialized, "Sterownik oznaczony jako nieaktywny po timeout");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 61. VirtIO-BLK: Odrzucenie used.len > submitted buffer length jako korupcji
// ----------------------------------------------------------------------------
void test_virtio_blk_invalid_used_len_rejected() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    g_mock.blk_override_len = 514; // Wiecej niz zgloszony bufor 512 + 1 B

    uint8_t buf[512]{};
    TEST_ASSERT(drv->bdev.read(0, 1, buf) == BlockError::IOError, "Odrzucenie zgloszenia z used.len > buffer length");
    TEST_ASSERT(!drv->initialized, "Sterownik zresetowany po wykryciu uszkodzenia dlugosci");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 62. VirtIO-BLK: Odrzucenie niezgodnego identyfikatora deskryptora
// ----------------------------------------------------------------------------
void test_virtio_blk_invalid_desc_id_rejected() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    g_mock.blk_override_desc_id = 99;

    uint8_t buf[512]{};
    TEST_ASSERT(drv->bdev.read(0, 1, buf) == BlockError::IOError, "Odrzucenie zgloszenia z falszywym desc_id");
    TEST_ASSERT(!drv->initialized, "Sterownik zresetowany po wykryciu desc_id mismatch");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 63. VirtIO-BLK: Kwarantanna DMA przy braku potwierdzenia resetu sprzetowego
// ----------------------------------------------------------------------------
void test_virtio_blk_dma_quarantine_on_stuck_reset() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    g_mock.ignore_reset = true;

    const size_t allocs_before = g_host_dma_allocs.size();
    TEST_ASSERT(!virtio_blk_reset(drv), "Reset zwraca false gdy sprzet nie potwierdza statusu 0");
    TEST_ASSERT(g_host_dma_allocs.size() == allocs_before, "Pamiec DMA poddana kwarantannie - nie zostala zwolniona!");

    g_mock.ignore_reset = false;
    g_host_dma_allocs.clear();
    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 64. VirtIO-BLK: Wielokrotne powtorzone zlecenia i brak wyciekow deskryptorow
// ----------------------------------------------------------------------------
void test_virtio_blk_repeated_requests_and_recycling() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    const uint16_t initial_free = virtqueue_get_num_free(drv->vq);

    uint8_t buf[512]{};
    for (int i = 0; i < 256; ++i) {
        TEST_ASSERT(drv->bdev.read(0, 1, buf) == BlockError::Ok, "Odczyt powtarzany w petli");
        TEST_ASSERT(virtqueue_get_num_free(drv->vq) == initial_free, "Brak wycieku deskryptorow");
    }
    TEST_ASSERT(drv->total_reads == 256, "total_reads == 256");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 65. VirtIO-BLK: Rewalidacja stanu nosnika (revalidate)
// ----------------------------------------------------------------------------
void test_virtio_blk_revalidate() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(drv->bdev.revalidate() == BlockError::Ok, "Rewalidacja sprawnego dysku");

    virtio_pci_set_status(&dev, VIRTIO_STATUS_FAILED);
    TEST_ASSERT(drv->bdev.revalidate() == BlockError::DeviceUnavailable, "Rewalidacja odrzuca uszkodzone urzadzenie");

    virtio_blk_reset_all();
}

// ----------------------------------------------------------------------------
// 66. VirtIO-BLK: Test obciazeniowy (virtio_blk_stress_test)
// ----------------------------------------------------------------------------
void test_virtio_blk_stress_host() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 2048, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    // Domyslnie allow_destructive=false: krok zapisu 3 jest pomijany, test konczy sie sukcesem bez zapisu
    TEST_ASSERT(virtio_blk_stress_test(drv, false), "virtio_blk_stress_test (non-destructive) zakonczony sukcesem");
    // Weryfikacja ze zaden bajt w sektorze 2047 nie zostal zmodyfikowany
    TEST_ASSERT(g_mock.blk_disk_data[2047 * 512] == 0, "Sektor 2047 nie zostal zmodyfikowany w trybie bezpiecznym");

    virtio_blk_reset_all();
}

void test_virtio_blk_stress_destructive_no_marker() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 4096, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    // allow_destructive=true, ale brak znacznika na dysku: krok zapisu 3 musi zostac pominiety!
    TEST_ASSERT(virtio_blk_stress_test(drv, true), "virtio_blk_stress_test kontynuuje bezpiecznie");
    // Weryfikacja ze sektor 4095 nie zostal zapisany
    TEST_ASSERT(g_mock.blk_disk_data[4095 * 512] == 0, "Brak zapisu przy braku znacznika testowego");

    virtio_blk_reset_all();
}

void test_virtio_blk_stress_destructive_bad_crc() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 4096, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkTestMarker marker{};
    std::memcpy(marker.magic, VIRTIO_BLK_TEST_MARKER_MAGIC, sizeof(VIRTIO_BLK_TEST_MARKER_MAGIC));
    marker.signature = VIRTIO_BLK_TEST_SIGNATURE;
    marker.version = VIRTIO_BLK_TEST_VERSION;
    marker.flags = VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE;
    marker.scratch_lba = 100;
    marker.scratch_sectors = 1;
    marker.crc32 = 0xDEADBEEF; // Celowo bledny CRC
    std::memcpy(&g_mock.blk_disk_data[1 * 512], &marker, sizeof(marker));

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(virtio_blk_stress_test(drv, true), "virtio_blk_stress_test kontynuuje bezpiecznie przy bad CRC");
    TEST_ASSERT(g_mock.blk_disk_data[100 * 512] == 0, "Brak zapisu przy uszkodzonym CRC znacznika");

    virtio_blk_reset_all();
}

void test_virtio_blk_stress_destructive_boot_state_collision() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 4096, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkTestMarker marker{};
    std::memcpy(marker.magic, VIRTIO_BLK_TEST_MARKER_MAGIC, sizeof(VIRTIO_BLK_TEST_MARKER_MAGIC));
    marker.signature = VIRTIO_BLK_TEST_SIGNATURE;
    marker.version = VIRTIO_BLK_TEST_VERSION;
    marker.flags = VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE;
    marker.scratch_lba = 2046; // Celowa kolizja z A/B boot-state!
    marker.scratch_sectors = 1;
    marker.crc32 = rozruch_oblicz_crc32(&marker, offsetof(VirtioBlkTestMarker, crc32));
    std::memcpy(&g_mock.blk_disk_data[1 * 512], &marker, sizeof(marker));

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(virtio_blk_stress_test(drv, true), "virtio_blk_stress_test odrzuca kolizje z LBA 2046");
    TEST_ASSERT(g_mock.blk_disk_data[2046 * 512] == 0, "Brak zapisu w sektorze rozruchu 2046");

    virtio_blk_reset_all();
}

void test_virtio_blk_stress_destructive_valid_marker() {
    virtio_blk_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_blk_device(&dev, 4096, 512), "Probe dysku");
    TEST_ASSERT(virtio_blk_init(&dev), "Inicjalizacja dysku");

    VirtioBlkTestMarker marker{};
    std::memcpy(marker.magic, VIRTIO_BLK_TEST_MARKER_MAGIC, sizeof(VIRTIO_BLK_TEST_MARKER_MAGIC));
    marker.signature = VIRTIO_BLK_TEST_SIGNATURE;
    marker.version = VIRTIO_BLK_TEST_VERSION;
    marker.flags = VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE;
    marker.scratch_lba = 100;
    marker.scratch_sectors = 1;
    marker.crc32 = rozruch_oblicz_crc32(&marker, offsetof(VirtioBlkTestMarker, crc32));
    std::memcpy(&g_mock.blk_disk_data[1 * 512], &marker, sizeof(marker));

    VirtioBlkDriver* drv = virtio_blk_get_driver(0);
    TEST_ASSERT(virtio_blk_stress_test(drv, true), "virtio_blk_stress_test (destructive) zakonczony pelnym sukcesem");
    // Weryfikacja ze wzorzec testowy (pierwszy bajt 0xA5) zostal poprawnie zapisany w sektorze 100
    TEST_ASSERT(g_mock.blk_disk_data[100 * 512] == 0xA5, "Sektor 100 zawiera wzorzec testowy 0xA5");

    virtio_blk_reset_all();
}

// ============================================================================
// NOWE TESTY GENERIC PCI MSI / MSI-X ORAZ INTEGRACJI Z VIRTIO
// ============================================================================

// ----------------------------------------------------------------------------
// 67. Poprawne MSI (parsowanie 32/64-bit, wlaczenie, PVM mask/unmask, wylaczenie)
// ----------------------------------------------------------------------------
void test_msi_parsing_and_enable() {
    // 1. MSI 32-bit bez PVM
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0x40;
    setup_msi_cap(0x40, 0, false, false, 2); // MMC = 4 messages (mmc_bits=2)

    PciMsiCapability msi{};
    TEST_ASSERT(pci_find_msi(0, 4, 0, &msi), "pci_find_msi dla 32-bit MSI");
    TEST_ASSERT(msi.cap_offset == 0x40, "msi cap_offset == 0x40");
    TEST_ASSERT(!msi.is_64bit, "msi !is_64bit");
    TEST_ASSERT(!msi.has_pvm, "msi !has_pvm");
    TEST_ASSERT(msi.multiple_msg_capable == 4, "msi MMC == 4");

    TEST_ASSERT(pci_msi_enable(0, 4, 0, &msi, 1, 0x33), "pci_msi_enable dla 32-bit");
    TEST_ASSERT(g_mock.msi_enabled, "g_mock.msi_enabled == true");
    uint16_t cmd = 0;
    std::memcpy(&cmd, &g_mock.pci_config[0x04], 2);
    TEST_ASSERT((cmd & PCI_COMMAND_INT_DISABLE) != 0, "PCI INTx zablokowane po wlaczeniu MSI");

    uint32_t addr_low = 0;
    std::memcpy(&addr_low, &g_mock.pci_config[0x44], 4);
    TEST_ASSERT(addr_low == (0xFEE00000U | (1U << 12)), "MSI msg_addr_low zaprogramowane z APIC ID 1");

    uint16_t data = 0;
    std::memcpy(&data, &g_mock.pci_config[0x48], 2);
    TEST_ASSERT(data == 0x33, "MSI msg_data zaprogramowane z wektorem 0x33");

    pci_msi_disable(0, 4, 0, &msi);
    TEST_ASSERT(!g_mock.msi_enabled, "g_mock.msi_enabled == false po wylaczeniu");

    // 2. MSI 64-bit z PVM
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0x40;
    setup_msi_cap(0x40, 0, true, true, 0);

    TEST_ASSERT(pci_find_msi(0, 4, 0, &msi), "pci_find_msi dla 64-bit z PVM");
    TEST_ASSERT(msi.is_64bit, "msi is_64bit");
    TEST_ASSERT(msi.has_pvm, "msi has_pvm");

    TEST_ASSERT(pci_msi_enable(0, 4, 0, &msi, 2, 0x55), "pci_msi_enable dla 64-bit z PVM");
    TEST_ASSERT(g_mock.msi_enabled, "g_mock.msi_enabled == true");

    std::memcpy(&addr_low, &g_mock.pci_config[0x44], 4);
    TEST_ASSERT(addr_low == (0xFEE00000U | (2U << 12)), "MSI 64-bit msg_addr_low poprawny");

    uint32_t addr_high = 0;
    std::memcpy(&addr_high, &g_mock.pci_config[0x48], 4);
    TEST_ASSERT(addr_high == 0, "MSI 64-bit msg_addr_high == 0");

    std::memcpy(&data, &g_mock.pci_config[0x4C], 2);
    TEST_ASSERT(data == 0x55, "MSI 64-bit msg_data == 0x55");

    uint32_t mask_reg = 0xFFFFFFFF;
    std::memcpy(&mask_reg, &g_mock.pci_config[0x50], 4);
    TEST_ASSERT(mask_reg == 0, "PVM mask odmaskowane na starcie");

    pci_msi_set_mask(0, 4, 0, &msi, true);
    std::memcpy(&mask_reg, &g_mock.pci_config[0x50], 4);
    TEST_ASSERT(mask_reg == 0xFFFFFFFF, "PVM mask zamaskowane po pci_msi_set_mask(true)");

    pci_msi_set_mask(0, 4, 0, &msi, false);
    std::memcpy(&mask_reg, &g_mock.pci_config[0x50], 4);
    TEST_ASSERT(mask_reg == 0, "PVM mask odmaskowane po pci_msi_set_mask(false)");

    pci_msi_disable(0, 4, 0, &msi);
    TEST_ASSERT(!g_mock.msi_enabled, "g_mock.msi_enabled == false");
    std::memcpy(&mask_reg, &g_mock.pci_config[0x50], 4);
    TEST_ASSERT(mask_reg == 0xFFFFFFFF, "PVM mask zamaskowane po pci_msi_disable");
}

// ----------------------------------------------------------------------------
// 68. Poprawne MSI-X (parsowanie, granice tabeli, wlaczenie, wylaczenie)
// ----------------------------------------------------------------------------
void test_msix_parsing_and_bounds() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0x40;
    setup_msix_cap(0x40, 0, 8, 1, 0x1000, 1, 0x2000);

    PciMsixCapability msix{};
    TEST_ASSERT(pci_find_msix(0, 4, 0, &msix), "pci_find_msix");
    TEST_ASSERT(msix.cap_offset == 0x40, "cap_offset == 0x40");
    TEST_ASSERT(msix.table_size == 8, "table_size == 8");
    TEST_ASSERT(msix.table_bir == 1, "table_bir == 1");
    TEST_ASSERT(msix.table_offset == 0x1000, "table_offset == 0x1000");
    TEST_ASSERT(msix.pba_bir == 1, "pba_bir == 1");
    TEST_ASSERT(msix.pba_offset == 0x2000, "pba_offset == 0x2000");

    TEST_ASSERT(pci_msix_validate_bar_bounds(&msix, 0x4000, 0x4000), "validate_bar_bounds z 16 KiB BAR");

    TEST_ASSERT(pci_msix_enable(0, 4, 0, &msix), "pci_msix_enable");
    TEST_ASSERT(g_mock.msix_enabled, "g_mock.msix_enabled == true");
    uint16_t cmd = 0;
    std::memcpy(&cmd, &g_mock.pci_config[0x04], 2);
    TEST_ASSERT((cmd & PCI_COMMAND_INT_DISABLE) != 0, "PCI INTx zablokowane po wlaczeniu MSI-X");
    TEST_ASSERT((g_mock.msix_msg_ctrl & PCI_MSIX_CTRL_FUNC_MASK) == 0, "Function Mask wyczyszczone");

    pci_msix_disable(0, 4, 0, &msix);
    TEST_ASSERT(!g_mock.msix_enabled, "g_mock.msix_enabled == false po pci_msix_disable");
    TEST_ASSERT((g_mock.msix_msg_ctrl & PCI_MSIX_CTRL_FUNC_MASK) != 0, "Function Mask ustawione po pci_msix_disable");
}

// ----------------------------------------------------------------------------
// 69. Zdeformowane capabilities (wykrywanie petli, offset < 0x40, nieprawidlowe dopasowanie)
// ----------------------------------------------------------------------------
void test_malformed_capabilities() {
    // a. Petla w lancuchu capabilities (0x40 -> 0x60 -> 0x40)
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0x40;

    g_mock.pci_config[0x40] = 0x09; // VNDR
    g_mock.pci_config[0x41] = 0x60; // points to 0x60

    g_mock.pci_config[0x60] = 0x09; // VNDR
    g_mock.pci_config[0x61] = 0x40; // loops back to 0x40!

    TEST_ASSERT(pci_find_capability(0, 4, 0, PCI_CAP_ID_MSIX) == 0, "Wykrycie petli zwraca 0 bez zawieszenia");

    // b. Offset < 0x40 (np. 0x20 w standardowym naglowku)
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0x20;
    TEST_ASSERT(pci_find_capability(0, 4, 0, PCI_CAP_ID_MSIX) == 0, "Offset < 0x40 odrzucony");

    // c. Niezaalokowane / niepelne capability na koncu przestrzeni (offset 0xF8 dla MSI-X wymagajacego 12 B)
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0xF8;
    g_mock.pci_config[0xF8] = PCI_CAP_ID_MSIX;
    g_mock.pci_config[0xF9] = 0x00;

    PciMsixCapability msix{};
    TEST_ASSERT(!pci_find_msix(0, 4, 0, &msix), "Uciete capability MSI-X (0xF8 + 12 > 256) odrzucone");

    // d. Uciete capability MSI 64-bit z PVM na koncu (offset 0xF0 dla 24 B)
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0xF0;
    setup_msi_cap(0xF0, 0, true, true, 0); // Wymaga 24 B (0xF0 + 24 = 0x108 > 256)
    PciMsiCapability msi{};
    TEST_ASSERT(!pci_find_msi(0, 4, 0, &msi), "Uciete capability MSI (0xF0 + 24 > 256) odrzucone");
}

// ----------------------------------------------------------------------------
// 70. Nieprawidlowy BIR w MSI-X (fail-closed)
// ----------------------------------------------------------------------------
void test_msix_invalid_bir() {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1044, 0x1AF4, 0x0004);
    g_mock.pci_config[0x34] = 0x40;

    // Table BIR = 6 (nieistniejacy BAR, dopuszczalne 0..5)
    setup_msix_cap(0x40, 0, 4, 6, 0x000, 1, 0x800);
    PciMsixCapability msix{};
    TEST_ASSERT(!pci_find_msix(0, 4, 0, &msix), "MSI-X z table BIR == 6 odrzucone");

    // PBA BIR = 7
    setup_msix_cap(0x40, 0, 4, 1, 0x000, 7, 0x800);
    TEST_ASSERT(!pci_find_msix(0, 4, 0, &msix), "MSI-X z PBA BIR == 7 odrzucone");

    msix.table_bir = 6;
    msix.pba_bir = 1;
    msix.table_size = 4;
    TEST_ASSERT(!pci_msix_validate_bar_bounds(&msix, 0x4000, 0x4000), "validate_bar_bounds odrzuca BIR >= 6");
}

// ----------------------------------------------------------------------------
// 71. Przekroczenie granic BAR przez tabele MSI-X i ochrona arytmetyczna
// ----------------------------------------------------------------------------
void test_msix_table_bounds_overflow() {
    PciMsixCapability msix{};
    msix.table_bir = 1;
    msix.pba_bir = 1;
    msix.table_size = 4; // 4 * 16 = 64 B
    msix.table_offset = 0x1000;
    msix.pba_offset = 0x2000;

    // 1. Rozmiar BAR za maly dla table_offset + table_size * 16
    TEST_ASSERT(!pci_msix_validate_bar_bounds(&msix, 0x1020, 0x4000), "BAR za maly dla tabeli");

    // 2. table_offset poza rozmiarem BAR
    msix.table_offset = 0x5000;
    TEST_ASSERT(!pci_msix_validate_bar_bounds(&msix, 0x4000, 0x4000), "table_offset > bar_size");

    // 3. Rozmiar BAR = 0
    msix.table_offset = 0;
    TEST_ASSERT(!pci_msix_validate_bar_bounds(&msix, 0, 0x4000), "table_bar_size == 0");

    // 4. Przepelnienie arytmetyczne offsetu (table_offset bliski UINT64_MAX)
    msix.table_offset = 0xFFFFFFF8U;
    TEST_ASSERT(!pci_msix_validate_bar_bounds(&msix, 0x4000, 0x4000), "Arithmetic overflow table_offset");

    // 5. table_size == 0 lub > 2048
    msix.table_offset = 0;
    msix.table_size = 0;
    TEST_ASSERT(!pci_msix_validate_bar_bounds(&msix, 0x4000, 0x4000), "table_size == 0 odrzucone");

    msix.table_size = 2049;
    TEST_ASSERT(!pci_msix_validate_bar_bounds(&msix, 0x100000, 0x100000), "table_size > 2048 odrzucone");
}

// ----------------------------------------------------------------------------
// 72. Dynamiczny alokator wektorow IDT i bezpieczne zwalnianie
// ----------------------------------------------------------------------------
void test_idt_dynamic_vector_allocator() {
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Poczatkowo 0 zaalokowanych wektorow IDT");

    // 1. Pojedyncza alokacja i rejestracja
    int v1 = idt_alokuj_wektor_irq();
    TEST_ASSERT(v1 >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY && v1 <= IDT_OSTATNI_WEKTOR_DYNAMICZNY,
                "Wektor w dozwolonym zakresie dynamicznym (48..252)");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "Zaalokowano 1 wektor");

    bool handler_called = false;
    auto mock_isr = [](void* ctx) {
        *static_cast<bool*>(ctx) = true;
    };
    TEST_ASSERT(idt_zarejestruj_irq(static_cast<uint8_t>(v1), mock_isr, &handler_called), "Rejestracja handlera IRQ");

    mock_trigger_irq(static_cast<uint8_t>(v1));
    TEST_ASSERT(handler_called, "Handler poprawnie wywolany przez mock_trigger_irq");

    // 2. Wyrejestrowanie i zwalnianie
    TEST_ASSERT(idt_wyrejestruj_irq(static_cast<uint8_t>(v1)), "Wyrejestrowanie IRQ");
    handler_called = false;
    mock_trigger_irq(static_cast<uint8_t>(v1));
    TEST_ASSERT(!handler_called, "Handler nie jest wywolywany po wyrejestrowaniu");

    TEST_ASSERT(idt_zwolnij_wektor_irq(static_cast<uint8_t>(v1)), "Zwolnienie wektora IRQ");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Brak aktywnych wektorow po zwolnieniu");

    // 3. Podwojne zwalnianie i niepoprawne wektory
    TEST_ASSERT(!idt_zwolnij_wektor_irq(static_cast<uint8_t>(v1)), "Podwojne zwolnienie odrzucone");
    TEST_ASSERT(!idt_zwolnij_wektor_irq(32), "Zwolnienie wektora statycznego < 48 odrzucone");
    TEST_ASSERT(!idt_zwolnij_wektor_irq(255), "Zwolnienie wektora 255 odrzucone");

    // 4. Test wyczerpania puli (wszystkie wektory IDT_PIERWSZY..IDT_OSTATNI; 253 = programowe oddanie CPU, poza pula)
    constexpr int ROZMIAR_PULI = IDT_OSTATNI_WEKTOR_DYNAMICZNY - IDT_PIERWSZY_WEKTOR_DYNAMICZNY + 1;
    std::vector<int> vecs;
    for (int i = 0; i < ROZMIAR_PULI; ++i) {
        int v = idt_alokuj_wektor_irq();
        TEST_ASSERT(v >= 0, "Alokacja wektora z puli");
        TEST_ASSERT(v != IDT_WEKTOR_ODDANIA_CPU, "Wektor oddania CPU nigdy nie trafia do puli");
        vecs.push_back(v);
    }
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == ROZMIAR_PULI, "Zaalokowano wszystkie wektory puli");
    TEST_ASSERT(idt_alokuj_wektor_irq() == -1, "Wyczerpana pula zwraca -1");

    for (int v : vecs) {
        TEST_ASSERT(idt_zwolnij_wektor_irq(static_cast<uint8_t>(v)), "Zwolnienie wektora z pelnej puli");
    }
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Pula calkowicie pusta po zwolnieniu");
}

// ----------------------------------------------------------------------------
// 73. Indywidualne programowanie i maskowanie wpisow tabeli MSI-X
// ----------------------------------------------------------------------------
void test_msix_entry_programming_and_masking() {
    PciMsixTableEntry table[4]{};
    for (int i = 0; i < 4; ++i) {
        table[i].vector_ctrl = 1; // domyslnie zamaskowane
    }

    // 1. Programowanie wpisu 0 z unmask
    TEST_ASSERT(pci_msix_program_entry(table, 4, 0, 3, 0x48, false), "Programowanie wpisu 0");
    TEST_ASSERT(table[0].msg_addr_low == (0xFEE00000U | (3U << 12)), "msg_addr_low z APIC ID 3");
    TEST_ASSERT(table[0].msg_addr_high == 0, "msg_addr_high == 0");
    TEST_ASSERT(table[0].msg_data == 0x48, "msg_data == 0x48");
    TEST_ASSERT(!pci_msix_is_entry_masked(table, 4, 0), "Wpis 0 odmaskowany");

    // 2. Maskowanie wpisu 0
    TEST_ASSERT(pci_msix_mask_entry(table, 4, 0), "Maskowanie wpisu 0");
    TEST_ASSERT(pci_msix_is_entry_masked(table, 4, 0), "Wpis 0 zamaskowany po pci_msix_mask_entry");

    // 3. Odmaskowanie wpisu 0
    TEST_ASSERT(pci_msix_unmask_entry(table, 4, 0), "Odmaskowanie wpisu 0");
    TEST_ASSERT(!pci_msix_is_entry_masked(table, 4, 0), "Wpis 0 odmaskowany po pci_msix_unmask_entry");

    // 4. Proba zapisu poza granice tabeli (indeks 4 przy rozmiarze 4)
    TEST_ASSERT(!pci_msix_program_entry(table, 4, 4, 3, 0x48, false), "Odrzucenie zapisu poza tabela");
    TEST_ASSERT(!pci_msix_mask_entry(table, 4, 4), "Odrzucenie maskowania poza tabela");
    TEST_ASSERT(!pci_msix_unmask_entry(table, 4, 4), "Odrzucenie odmaskowania poza tabela");
    TEST_ASSERT(pci_msix_is_entry_masked(table, 4, 4), "Odpytanie o indeks poza tabela zwraca masked (failsafe)");
}

// ----------------------------------------------------------------------------
// 74. Przypisywanie i odlaczanie wektora MSI-X dla kolejek VirtIO
// ----------------------------------------------------------------------------
void test_virtio_queue_msix_vector_assignment() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device_msix(&dev), "Probe urzadzenia VirtIO z MSI-X");
    TEST_ASSERT(dev.has_msix, "Urzadzenie obsluguje MSI-X");

    // 1. Przypisanie wpisu MSI-X 1 do kolejki 0
    TEST_ASSERT(virtio_pci_set_queue_msix_vector_checked(&dev, 0, 1), "Przypisanie wpisu 1 do kolejki 0");
    TEST_ASSERT(virtio_pci_get_queue_msix_vector(&dev) == 1, "Potwierdzenie odczytu wpisu 1");

    // 2. Odlaczenie wektora (VIRTIO_MSI_NO_VECTOR = 0xFFFF)
    TEST_ASSERT(virtio_pci_set_queue_msix_vector_checked(&dev, 0, VIRTIO_MSI_NO_VECTOR), "Odlaczenie wektora kolejki 0");
    TEST_ASSERT(virtio_pci_get_queue_msix_vector(&dev) == VIRTIO_MSI_NO_VECTOR, "Kolejka 0 potwierdza VIRTIO_MSI_NO_VECTOR");

    // 3. Konfiguracja wektora konfiguracyjnego urzadzenia (config_msix_vector)
    virtio_pci_set_config_msix_vector(&dev, 2);
    TEST_ASSERT(virtio_pci_get_config_msix_vector(&dev) == 2, "config_msix_vector == 2");
    virtio_pci_set_config_msix_vector(&dev, VIRTIO_MSI_NO_VECTOR);
    TEST_ASSERT(virtio_pci_get_config_msix_vector(&dev) == VIRTIO_MSI_NO_VECTOR, "config_msix_vector == NO_VECTOR");

    virtio_pci_cleanup(&dev);
}

// ----------------------------------------------------------------------------
// 75. Dostarczanie przerwan MSI-X i bezpieczenstwo teardown (brak wyciekow)
// ----------------------------------------------------------------------------
void test_virtio_msix_delivery_and_teardown_safety() {
    virtio_rng_reset();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_rng_device_msix(&dev), "Probe urzadzenia RNG z MSI-X");
    TEST_ASSERT(virtio_rng_init(&dev), "Inicjalizacja sterownika VirtIO-RNG z MSI-X");

    VirtioRngDriver* drv = virtio_rng_get_driver();
    TEST_ASSERT(drv != nullptr, "Dostep do drv");
    TEST_ASSERT(drv->use_msix, "drv->use_msix == true");
    TEST_ASSERT(drv->irq_vector >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY && drv->irq_vector <= IDT_OSTATNI_WEKTOR_DYNAMICZNY,
                "Zaalokowany wektor IRQ w zakresie IDT");
    TEST_ASSERT(drv->irq_count == 0, "Poczatkowa liczba zgloszonych IRQ == 0");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "Dokladnie 1 wektor IDT zaalokowany");

    // 1. Zlecenie losowych danych i weryfikacja obslugi przez ISR
    g_mock.rng_auto_process = true;
    g_mock.rng_fill_byte = 0x7E;

    uint8_t buf[32]{};
    size_t n = virtio_rng_read(buf, sizeof(buf));
    TEST_ASSERT(n == sizeof(buf), "Odczyt 32 bajtow losowych");
    TEST_ASSERT(buf[0] == 0x7E && buf[31] == 0x7E, "Poprawne dane w buforze");
    TEST_ASSERT(drv->irq_count == 1, "Dokladnie 1 przerwanie MSI-X dostarczone do ISR");
    TEST_ASSERT(drv->request_done, "Flaga request_done ustawiona w minimalnym ISR");

    // 2. Bezpieczny teardown: maskowanie, wylaczenie MSI-X, reset dev, zwolnienie wektora
    const uint8_t old_vector = drv->irq_vector;
    virtio_rng_reset();

    TEST_ASSERT(virtio_pci_is_entry_masked(&dev, 0), "Wpis 0 MSI-X zamaskowany podczas resetu");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Wektor IDT zwolniony po resecie");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Pamiec DMA kolejki zwolniona po resecie");

    // 3. Ewentualne spoznione przerwanie na stary wektor nie uszkadza pamieci
    mock_trigger_irq(old_vector);
    TEST_ASSERT(drv->irq_count == 1, "Spoznione przerwanie po wyrejestrowaniu zignorowane bezpiecznie");
}

// ----------------------------------------------------------------------------
// 76. Wielokrotne cykle init/reset z MSI-X i weryfikacja braku wyciekow zasobow
// ----------------------------------------------------------------------------
void test_virtio_msix_repeated_cycles_no_leak() {
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Brak wektorow na poczatku testu wielokrotnych cykli");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak buforow DMA na poczatku testu");

    // 20 pelnych cykli init/read/reset dla VirtIO-RNG z MSI-X
    for (int cycle = 0; cycle < 20; ++cycle) {
        VirtioPciDevice dev{};
        TEST_ASSERT(setup_mock_rng_device_msix(&dev), "RNG probe w cyklu");
        TEST_ASSERT(virtio_rng_init(&dev), "RNG init w cyklu");

        VirtioRngDriver* drv = virtio_rng_get_driver();
        TEST_ASSERT(drv->use_msix, "RNG uzywa MSI-X w cyklu");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "Dokladnie 1 wektor zaalokowany");

        g_mock.rng_auto_process = true;
        uint8_t buf[16]{};
        TEST_ASSERT(virtio_rng_read(buf, sizeof(buf)) == sizeof(buf), "RNG odczyt w cyklu");
        TEST_ASSERT(drv->irq_count > 0, "RNG IRQ obsluzone w cyklu");

        virtio_rng_reset();
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Zero wyciekow wektorow IDT po RNG reset");
        TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po RNG reset");
    }

    // 20 pelnych cykli init/write/read/reset dla VirtIO-BLK z MSI-X
    for (int cycle = 0; cycle < 20; ++cycle) {
        virtio_blk_reset_all();
        VirtioPciDevice bdev{};
        TEST_ASSERT(setup_mock_blk_device_msix(&bdev), "BLK probe w cyklu");
        TEST_ASSERT(virtio_blk_init(&bdev), "BLK init w cyklu");

        VirtioBlkDriver* bdrv = virtio_blk_get_driver(0);
        TEST_ASSERT(bdrv->use_msix, "BLK uzywa MSI-X w cyklu");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "Dokladnie 1 wektor zaalokowany dla BLK");

        g_mock.blk_auto_process = true;
        g_mock.blk_disk_data.assign(2048 * 512, 0);

        uint8_t write_buf[512];
        std::memset(write_buf, 0xAB + cycle, sizeof(write_buf));
        TEST_ASSERT(bdrv->bdev.write(10, 1, write_buf) == BlockError::Ok, "BLK write w cyklu");
        TEST_ASSERT(bdrv->irq_count > 0, "BLK write IRQ dostarczone");

        const uint32_t irqs_after_write = bdrv->irq_count;
        uint8_t read_buf[512]{};
        TEST_ASSERT(bdrv->bdev.read(10, 1, read_buf) == BlockError::Ok, "BLK read w cyklu");
        TEST_ASSERT(bdrv->irq_count > irqs_after_write, "BLK read IRQ dostarczone");
        TEST_ASSERT(std::memcmp(write_buf, read_buf, sizeof(write_buf)) == 0, "Sp?jno?? danych dysku");

        virtio_blk_reset_all();
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Zero wyciekow wektorow IDT po BLK reset");
        TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po BLK reset");
    }
}

// ============================================================================
// TESTY STEROWNIKA VIRTIO-NET
// ============================================================================

static uint32_t g_mock_net_rx_delivered_count = 0;
static std::vector<std::vector<uint8_t>> g_mock_net_rx_packets;

void obsluz_pakiet_sieciowy(uint8_t* pakiet, uint16_t dlugosc) {
    g_mock_net_rx_delivered_count++;
    if (pakiet && dlugosc > 0) {
        g_mock_net_rx_packets.push_back(std::vector<uint8_t>(pakiet, pakiet + dlugosc));
    }
}

static bool setup_mock_net_device(VirtioPciDevice* dev,
                                  const uint8_t mac[6] = nullptr,
                                  uint16_t status = VIRTIO_NET_S_LINK_UP,
                                  uint64_t extra_features = (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS)) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1041, 0x1AF4, 0x0001);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 64, 0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32; // RX
    g_mock.queues[1].size = 32; // TX

    uint64_t feat = VIRTIO_F_VERSION_1 | extra_features;
    g_mock.device_features = feat;

    uint8_t default_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    const uint8_t* use_mac = mac ? mac : default_mac;
    std::memcpy(&g_mock.bars[0].mem[0x100], use_mac, 6);
    std::memcpy(&g_mock.bars[0].mem[0x106], &status, sizeof(status));

    g_mock.net_auto_process = true;
    g_mock.net_skip_tx_reply = false;
    g_mock.net_override_len = -1;
    g_mock.net_override_desc_id = -1;
    g_mock.net_tx_processed_count = 0;
    g_mock.net_tx_frames.clear();

    return virtio_pci_probe(0, 6, 0, dev, 0);
}

static bool setup_mock_net_device_msix(VirtioPciDevice* dev,
                                       const uint8_t mac[6] = nullptr,
                                       uint16_t status = VIRTIO_NET_S_LINK_UP,
                                       uint16_t table_size = 4,
                                       uint8_t table_bar = 1) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1041, 0x1AF4, 0x0001);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 64, 0}
    };
    build_cap_chain(caps, 0xA0);
    setup_msix_cap(0xA0, 0, table_size, table_bar, 0x000, table_bar, 0x800);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;
    g_mock.queues[1].size = 32;

    uint64_t feat = VIRTIO_F_VERSION_1 | VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS;
    g_mock.device_features = feat;

    uint8_t default_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    const uint8_t* use_mac = mac ? mac : default_mac;
    std::memcpy(&g_mock.bars[0].mem[0x100], use_mac, 6);
    std::memcpy(&g_mock.bars[0].mem[0x106], &status, sizeof(status));

    g_mock.net_auto_process = true;
    g_mock.net_skip_tx_reply = false;
    g_mock.net_override_len = -1;
    g_mock.net_override_desc_id = -1;
    g_mock.net_tx_processed_count = 0;
    g_mock.net_tx_frames.clear();

    return virtio_pci_probe(0, 6, 0, dev, 0);
}

static bool mock_inject_net_rx_packet(const void* frame, size_t len, uint32_t override_len = 0) {
    VirtioNetDriver* drv = virtio_net_get_driver(0);
    if (!drv || !drv->rx_vq || !drv->rx_vq->avail || !drv->rx_vq->used || !drv->rx_vq->desc) {
        return false;
    }
    VirtQueue* vq = drv->rx_vq;
    if (vq->last_used_idx == vq->avail->idx) {
        return false;
    }
    const uint16_t a_idx = vq->last_used_idx & (vq->queue_size - 1U);
    const uint16_t desc_id = vq->avail->ring[a_idx];
    if (desc_id >= vq->queue_size) return false;

    void* vaddr = host_phys_to_virt(vq->desc[desc_id].addr);
    if (!vaddr) return false;

    auto* hdr = static_cast<VirtioNetHdr*>(vaddr);
    std::memset(hdr, 0, sizeof(VirtioNetHdr));

    uint8_t* payload = static_cast<uint8_t*>(vaddr) + sizeof(VirtioNetHdr);
    std::memcpy(payload, frame, len);

    const uint16_t u_idx = vq->used->idx & (vq->queue_size - 1U);
    vq->used->ring[u_idx].id = desc_id;
    vq->used->ring[u_idx].len = (override_len > 0) ? override_len : static_cast<uint32_t>(sizeof(VirtioNetHdr) + len);
    asm volatile("" ::: "memory");
    vq->used->idx = static_cast<uint16_t>(vq->used->idx + 1U);
    asm volatile("mfence" ::: "memory");

    mock_check_and_trigger_msix(0);
    return true;
}

// ----------------------------------------------------------------------------
// 77. VirtIO-NET: Inicjalizacja, rejestracja i czyszczenie zasobow (init i reset)
// ----------------------------------------------------------------------------
void test_virtio_net_init_and_cleanup() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia VirtIO-NET");
    TEST_ASSERT(virtio_net_init(&dev), "Inicjalizacja sterownika VirtIO-NET");
    TEST_ASSERT(virtio_net_get_count() == 1, "Zarejestrowano dokladnie 1 karte VirtIO-NET");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    TEST_ASSERT(drv != nullptr, "Dostep do struktury sterownika");
    TEST_ASSERT(drv->initialized, "Sterownik initialized == true");
    TEST_ASSERT(!drv->has_error, "Sterownik has_error == false");
    TEST_ASSERT(drv->net_dev.active, "Karta aktywna w rejestrze NetworkDevice");
    TEST_ASSERT(drv->net_dev.device_type == NetDeviceType::VirtIO, "Typ karty NetDeviceType::VirtIO");
    TEST_ASSERT(drv->net_dev.mtu == 1500, "MTU domyslne == 1500");
    uint8_t expected_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    TEST_ASSERT(std::memcmp(drv->net_dev.mac, expected_mac, 6) == 0, "Adres MAC zgodny z odczytanym");
    TEST_ASSERT((virtio_pci_get_status(&dev) & VIRTIO_STATUS_DRIVER_OK) != 0, "DRIVER_OK w urzadzeniu");
    TEST_ASSERT(g_host_dma_allocs.size() == 4, "4 alokacje DMA (RX vq + TX vq + RX buf + TX buf)");

    TEST_ASSERT(virtio_net_reset(drv), "Reset karty VirtIO-NET");
    TEST_ASSERT(!drv->initialized, "Sterownik nieaktywny po resecie");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po resecie karty");
    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 78. VirtIO-NET: Negocjacja cech i odczyt adresu MAC
// ----------------------------------------------------------------------------
void test_virtio_net_negotiation_and_mac() {
    virtio_net_reset_all();

    // 1. Sukces z cecha MAC
    uint8_t custom_mac[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev, custom_mac), "Probe z custom MAC");
    TEST_ASSERT(virtio_net_init(&dev), "Init z custom MAC");
    VirtioNetDriver* drv = virtio_net_get_driver(0);
    TEST_ASSERT(std::memcmp(drv->net_dev.mac, custom_mac, 6) == 0, "Custom MAC przypisany prawidlowo");
    virtio_net_reset_all();

    // 2. Sukces bez cechy MAC (fallback do locally administered MAC)
    VirtioPciDevice dev_nomac{};
    TEST_ASSERT(setup_mock_net_device(&dev_nomac, nullptr, VIRTIO_NET_S_LINK_UP, VIRTIO_NET_F_STATUS), "Probe bez cechy MAC");
    TEST_ASSERT(virtio_net_init(&dev_nomac), "Init bez cechy MAC powiodl sie");
    drv = virtio_net_get_driver(0);
    TEST_ASSERT(net_mac_valid(drv->net_dev.mac), "Wygenerowany fallback MAC jest poprawny");
    virtio_net_reset_all();

    // 3. Odrzucenie braku VIRTIO_F_VERSION_1
    VirtioPciDevice dev_no_v1{};
    setup_pci_hdr(0x1AF4, 0x1041, 0x1AF4, 0x0001);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 64, 0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.device_features = VIRTIO_NET_F_MAC;
    virtio_pci_probe(0, 6, 0, &dev_no_v1, 0);
    TEST_ASSERT(!virtio_net_init(&dev_no_v1), "Init odrzuca urzadzenie bez VERSION_1");
    virtio_net_reset_all();

    // 4. Odrzucenie gdy urzadzenie czysci FEATURES_OK
    VirtioPciDevice dev_no_fok{};
    TEST_ASSERT(setup_mock_net_device(&dev_no_fok), "Probe dla testu odrzucenia FEATURES_OK");
    g_mock.accept_features = false;
    TEST_ASSERT(!virtio_net_init(&dev_no_fok), "Init odrzuca urzadzenie gdy FEATURES_OK odrzucone");
    virtio_net_reset_all();

    // 5. Odrzucenie gdy urzadzenie odrzuca DRIVER_OK
    VirtioPciDevice dev_no_dok{};
    TEST_ASSERT(setup_mock_net_device(&dev_no_dok), "Probe dla testu odrzucenia DRIVER_OK");
    g_mock.allow_driver_ok = false;
    TEST_ASSERT(!virtio_net_init(&dev_no_dok), "Init odrzuca urzadzenie gdy DRIVER_OK odrzucone");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Pamiec DMA zwolniona po odrzuceniu DRIVER_OK");
    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 79. VirtIO-NET: Stan fizycznego lacza (link state Up / Down)
// ----------------------------------------------------------------------------
void test_virtio_net_link_state() {
    virtio_net_reset_all();

    // 1. Karta z link up
    VirtioPciDevice dev_up{};
    TEST_ASSERT(setup_mock_net_device(&dev_up, nullptr, VIRTIO_NET_S_LINK_UP), "Probe link up");
    TEST_ASSERT(virtio_net_init(&dev_up), "Init link up");
    VirtioNetDriver* drv = virtio_net_get_driver(0);
    TEST_ASSERT(virtio_net_get_link_state(&drv->net_dev) == NetLinkState::Up, "get_link_state == Up");
    TEST_ASSERT(drv->net_dev.is_up(), "NetworkDevice::is_up() == true");
    virtio_net_reset_all();

    // 2. Karta z link down
    VirtioPciDevice dev_down{};
    TEST_ASSERT(setup_mock_net_device(&dev_down, nullptr, 0), "Probe link down (status=0)");
    TEST_ASSERT(virtio_net_init(&dev_down), "Init link down");
    drv = virtio_net_get_driver(0);
    TEST_ASSERT(virtio_net_get_link_state(&drv->net_dev) == NetLinkState::Down, "get_link_state == Down");
    TEST_ASSERT(!drv->net_dev.is_up(), "NetworkDevice::is_up() == false");

    uint8_t test_frame[64]{};
    for (int i = 0; i < 6; ++i) test_frame[i] = 0xFF;
    std::memcpy(&test_frame[6], drv->net_dev.mac, 6);
    test_frame[12] = 0x08; test_frame[13] = 0x06;
    TEST_ASSERT(drv->net_dev.transmit(test_frame, sizeof(test_frame)) == NetError::LinkDown, "transmit zwraca LinkDown przy braku lacza");
    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 80. VirtIO-NET: Podstawowa transmisja pakietu i statystyki
// ----------------------------------------------------------------------------
void test_virtio_net_transmit_basic_and_stats() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia");
    TEST_ASSERT(virtio_net_init(&dev), "Init urzadzenia");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    uint8_t frame[64];
    for (size_t i = 0; i < sizeof(frame); ++i) {
        frame[i] = static_cast<uint8_t>(0xA0 + (i % 16));
    }
    for (int i = 0; i < 6; ++i) frame[i] = 0xFF;
    std::memcpy(&frame[6], drv->net_dev.mac, 6);

    const NetError err = drv->net_dev.transmit(frame, sizeof(frame));
    TEST_ASSERT(err == NetError::Ok, "transmit zwraca NetError::Ok");
    TEST_ASSERT(g_mock.net_tx_frames.size() == 1, "Mock odebral dokladnie 1 ramke TX");
    TEST_ASSERT(g_mock.net_tx_frames[0].size() == sizeof(frame), "Dlugosc odebranej ramki == 64");
    TEST_ASSERT(std::memcmp(g_mock.net_tx_frames[0].data(), frame, sizeof(frame)) == 0, "Zawartosc ramki zgodna");

    NetDeviceStats stats{};
    drv->net_dev.get_stats(&stats);
    TEST_ASSERT(stats.tx_packets == 1, "stats.tx_packets == 1");
    TEST_ASSERT(stats.tx_bytes == sizeof(frame), "stats.tx_bytes == 64");
    TEST_ASSERT(stats.tx_errors == 0, "stats.tx_errors == 0");
    TEST_ASSERT(stats.tx_dropped == 0, "stats.tx_dropped == 0");

    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 81. VirtIO-NET: Walidacja granic MTU i dlugosci bufora transmisji
// ----------------------------------------------------------------------------
void test_virtio_net_transmit_mtu_and_length_validation() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia");
    TEST_ASSERT(virtio_net_init(&dev), "Init urzadzenia");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    uint8_t buf[1600]{};

    TEST_ASSERT(drv->net_dev.transmit(nullptr, 64) == NetError::InvalidArgument, "transmit(nullptr) zwraca InvalidArgument");
    TEST_ASSERT(drv->net_dev.transmit(buf, 13) == NetError::BufferTooSmall, "transmit(<14) zwraca BufferTooSmall");
    TEST_ASSERT(drv->net_dev.transmit(buf, 1515) == NetError::BufferTooLarge, "transmit(>MTU+14) zwraca BufferTooLarge");

    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 82. VirtIO-NET: Wyczerpanie kolejki TX (QueueFull) i prawidlowy odzysk
// ----------------------------------------------------------------------------
void test_virtio_net_transmit_queue_exhaustion() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia");
    TEST_ASSERT(virtio_net_init(&dev), "Init urzadzenia");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    g_mock.net_skip_tx_reply = true; // Urzadzenie nie odsyla zakonczen natychmiast

    uint8_t frame[60]{};
    for (int i = 0; i < 6; ++i) frame[i] = 0xFF;
    std::memcpy(&frame[6], drv->net_dev.mac, 6);

    NetError last_err = NetError::Ok;
    for (uint32_t i = 0; i < drv->tx_count + 5; ++i) {
        last_err = drv->net_dev.transmit(frame, sizeof(frame));
        if (last_err == NetError::QueueFull) {
            break;
        }
    }

    TEST_ASSERT(last_err == NetError::QueueFull, "Wyczerpanie kolejki TX zwraca QueueFull");
    NetDeviceStats stats{};
    drv->net_dev.get_stats(&stats);
    TEST_ASSERT(stats.tx_ring_full > 0, "Licznik tx_ring_full zainkrementowany");
    TEST_ASSERT(stats.tx_dropped > 0, "Licznik tx_dropped zainkrementowany");

    // Re-enable zakonczenia w mocku i symuluj zakonczenie zaleglych zadan
    g_mock.net_skip_tx_reply = false;
    VirtQueue* vq = drv->tx_vq;
    while (g_mock.net_tx_processed_count != vq->avail->idx) {
        const uint16_t a_idx = g_mock.net_tx_processed_count & (vq->queue_size - 1U);
        const uint16_t head_id = vq->avail->ring[a_idx];
        const uint16_t u_idx = vq->used->idx & (vq->queue_size - 1U);
        vq->used->ring[u_idx].id = head_id;
        vq->used->ring[u_idx].len = 0;
        vq->used->idx = static_cast<uint16_t>(vq->used->idx + 1U);
        g_mock.net_tx_processed_count++;
    }

    // poll_rx odzyskuje deskryptory
    virtio_net_poll_rx(&drv->net_dev);
    TEST_ASSERT(drv->net_dev.transmit(frame, sizeof(frame)) == NetError::Ok, "Transmisja powiodla sie po odzyskaniu deskryptorow");

    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 83. VirtIO-NET: Odbior ramek (RX) i uzupelnianie puli buforow (replenish)
// ----------------------------------------------------------------------------
void test_virtio_net_rx_receive_and_replenish() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia");
    TEST_ASSERT(virtio_net_init(&dev), "Init urzadzenia");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    g_mock_net_rx_delivered_count = 0;
    g_mock_net_rx_packets.clear();

    uint8_t rx_frame[60];
    for (size_t i = 0; i < sizeof(rx_frame); ++i) {
        rx_frame[i] = static_cast<uint8_t>(0x30 + i);
    }
    for (int i = 0; i < 6; ++i) rx_frame[i] = 0xFF;
    std::memcpy(&rx_frame[6], drv->net_dev.mac, 6);

    TEST_ASSERT(mock_inject_net_rx_packet(rx_frame, sizeof(rx_frame)), "Wstrzykniecie pakietu RX");

    virtio_net_poll_rx(&drv->net_dev);

    TEST_ASSERT(g_mock_net_rx_delivered_count == 1, "Dostarczono dokladnie 1 pakiet do obsluz_pakiet_sieciowy");
    TEST_ASSERT(g_mock_net_rx_packets.size() == 1, "Tablica odebranych pakietow zawiera 1 element");
    TEST_ASSERT(g_mock_net_rx_packets[0].size() == sizeof(rx_frame), "Dlugosc odebranej ramki == 60");
    TEST_ASSERT(std::memcmp(g_mock_net_rx_packets[0].data(), rx_frame, sizeof(rx_frame)) == 0, "Zawartosc odebranej ramki zgodna");

    NetDeviceStats stats{};
    drv->net_dev.get_stats(&stats);
    TEST_ASSERT(stats.rx_packets == 1, "stats.rx_packets == 1");
    TEST_ASSERT(stats.rx_bytes == sizeof(rx_frame), "stats.rx_bytes == 60");
    TEST_ASSERT(stats.rx_errors == 0, "stats.rx_errors == 0");
    TEST_ASSERT(virtqueue_get_num_free(drv->rx_vq) == 0, "Kolejka RX uzupelniona - 0 wolnych deskryptorow");

    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 84. VirtIO-NET: Uszkodzone pakiety odbiorcze (used.len < 24)
// ----------------------------------------------------------------------------
void test_virtio_net_rx_malformed_packets() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia");
    TEST_ASSERT(virtio_net_init(&dev), "Init urzadzenia");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    g_mock_net_rx_delivered_count = 0;
    g_mock_net_rx_packets.clear();

    uint8_t dummy[60]{};
    TEST_ASSERT(mock_inject_net_rx_packet(dummy, sizeof(dummy), 15), "Wstrzykniecie uszkodzonego pakietu (len 15)");

    virtio_net_poll_rx(&drv->net_dev);

    TEST_ASSERT(g_mock_net_rx_delivered_count == 0, "Uszkodzony pakiet NIE zostal przekazany do obsluz_pakiet_sieciowy");
    NetDeviceStats stats{};
    drv->net_dev.get_stats(&stats);
    TEST_ASSERT(stats.rx_errors == 1, "stats.rx_errors == 1");
    TEST_ASSERT(stats.rx_dropped == 1, "stats.rx_dropped == 1");

    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 85. VirtIO-NET: Konfiguracja i dostarczanie przerwan MSI-X dla RX i TX
// ----------------------------------------------------------------------------
void test_virtio_net_msix_operation() {
    virtio_net_reset_all();
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Brak wektorow IDT przed testem MSI-X");

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device_msix(&dev), "Probe urzadzenia z MSI-X");
    TEST_ASSERT(virtio_net_init(&dev), "Init z MSI-X");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    TEST_ASSERT(drv->use_msix, "drv->use_msix == true");
    TEST_ASSERT(drv->rx_irq_vector >= 0, "Wektor RX zaalokowany");
    TEST_ASSERT(drv->tx_irq_vector >= 0, "Wektor TX zaalokowany");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 2, "Dokladnie 2 wektory IDT zaalokowane (RX i TX)");

    uint8_t frame[60]{};
    for (int i = 0; i < 6; ++i) frame[i] = 0xFF;
    std::memcpy(&frame[6], drv->net_dev.mac, 6);
    TEST_ASSERT(drv->net_dev.transmit(frame, sizeof(frame)) == NetError::Ok, "Transmisja powiodla sie");
    TEST_ASSERT(drv->tx_irq_count > 0, "TX IRQ zgloszone do sterownika");

    TEST_ASSERT(mock_inject_net_rx_packet(frame, sizeof(frame)), "Wstrzykniecie pakietu RX");
    TEST_ASSERT(drv->rx_irq_count > 0, "RX IRQ zgloszone do sterownika");
    TEST_ASSERT(drv->rx_interrupt_pending, "rx_interrupt_pending == true");

    virtio_net_poll_rx(&drv->net_dev);
    TEST_ASSERT(!drv->rx_interrupt_pending, "rx_interrupt_pending zresetowane po poll_rx");

    virtio_net_reset(drv);
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Zero wyciekow wektorow IDT po resecie MSI-X");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po resecie MSI-X");
    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 86. VirtIO-NET: Kwarantanna DMA przy braku potwierdzenia resetu sprzetowego
// ----------------------------------------------------------------------------
void test_virtio_net_reset_quarantine_on_stuck_device() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia");
    TEST_ASSERT(virtio_net_init(&dev), "Init urzadzenia");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    g_mock.ignore_reset = true;

    const size_t allocs_before = g_host_dma_allocs.size();
    TEST_ASSERT(!virtio_net_reset(drv), "Reset zwraca false gdy urzadzenie nie zeruje statusu");
    TEST_ASSERT(drv->has_error, "Sterownik oznaczony jako uszkodzony (has_error = true)");
    TEST_ASSERT(g_host_dma_allocs.size() == allocs_before, "Pamiec DMA poddana kwarantannie - nie zostala zwolniona!");

    g_mock.ignore_reset = false;
    g_host_dma_allocs.clear();
    virtio_net_reset_all();
}

// ----------------------------------------------------------------------------
// 87. VirtIO-NET: Test obciazeniowy i cykliczne zawijanie buforow (stress)
// ----------------------------------------------------------------------------
void test_virtio_net_stress_and_wraparound() {
    virtio_net_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_net_device(&dev), "Probe urzadzenia");
    TEST_ASSERT(virtio_net_init(&dev), "Init urzadzenia");

    VirtioNetDriver* drv = virtio_net_get_driver(0);
    g_mock_net_rx_delivered_count = 0;
    g_mock_net_rx_packets.clear();

    uint8_t frame[64];
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 6; ++i) frame[i] = 0xFF;
    std::memcpy(&frame[6], drv->net_dev.mac, 6);

    constexpr int ITERS = 200;
    for (int i = 0; i < ITERS; ++i) {
        TEST_ASSERT(drv->net_dev.transmit(frame, sizeof(frame)) == NetError::Ok, "Transmit w petli stress");
        TEST_ASSERT(mock_inject_net_rx_packet(frame, sizeof(frame)), "Inject RX w petli stress");
        virtio_net_poll_rx(&drv->net_dev);
    }

    NetDeviceStats stats{};
    drv->net_dev.get_stats(&stats);
    TEST_ASSERT(stats.tx_packets == ITERS, "Liczba wyslanych pakietow == ITERS");
    TEST_ASSERT(stats.rx_packets == ITERS, "Liczba odebranych pakietow == ITERS");
    TEST_ASSERT(stats.tx_errors == 0, "tx_errors == 0");
    TEST_ASSERT(stats.rx_errors == 0, "rx_errors == 0");
    TEST_ASSERT(g_mock_net_rx_delivered_count == ITERS, "Dostarczono wszystkie pakiety do obsluz_pakiet_sieciowy");

    TEST_ASSERT(virtio_net_stress_test(drv), "virtio_net_stress_test zwrocilo true");

    virtio_net_reset(drv);
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po zakonczeniu testu stress");
    virtio_net_reset_all();
}

// ============================================================================
// MOCKI I TESTY VIRTIO-GPU
// ============================================================================

// A failing TEST_ASSERT returns early. Every GPU test therefore owns a scope
// guard declared after its stack VirtioPciDevice: it tears the driver down
// while the device object is still alive, so no global driver slot can keep a
// pointer into a dead stack frame (previously: ASan stack-use-after-return in
// the next test's virtio_gpu_reset_all and an invalid-bool UBSan load).
// Same lifetime rule for VirtIO-Input tests: a failing TEST_ASSERT returns
// early; the InputDevice registry must not keep a pointer into the dead
// frame (found 2026-11-11: ASan stack-use-after-return in input_reset_all).
struct InputRegistryScope {
    InputRegistryScope() = default;
    InputRegistryScope(const InputRegistryScope&) = delete;
    InputRegistryScope& operator=(const InputRegistryScope&) = delete;
    ~InputRegistryScope() { input_reset_all(); }
};
struct GpuDriverScope {
    GpuDriverScope() = default;
    GpuDriverScope(const GpuDriverScope&) = delete;
    GpuDriverScope& operator=(const GpuDriverScope&) = delete;
    ~GpuDriverScope() { virtio_gpu_reset_all(); }
};

static bool setup_mock_gpu_device(VirtioPciDevice* dev,
                                   uint32_t disp_w = 1280,
                                   uint32_t disp_h = 800,
                                   uint64_t extra_features = 0,
                                   uint8_t initial_status = 0) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1050, 0x1AF4, 0x0010);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 64, 0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32; // controlq
    g_mock.queues[1].size = 32; // cursorq

    uint64_t feat = VIRTIO_F_VERSION_1 | extra_features;
    g_mock.device_features = feat;

    g_mock.gpu_auto_process = true;
    g_mock.gpu_skip_reply = false;
    g_mock.gpu_override_len = -1;
    g_mock.gpu_override_desc_id = -1;
    g_mock.gpu_override_resp_type = 0;
    g_mock.gpu_display_w = disp_w;
    g_mock.gpu_display_h = disp_h;
    g_mock.gpu_processed_count = 0;
    g_mock.gpu_commands_seen.clear();
    g_mock.device_status=initial_status;g_mock.bars[0].mem[20]=initial_status;

    return virtio_pci_probe(0, 7, 0, dev, 0);
}

static bool setup_mock_gpu_device_msix(VirtioPciDevice* dev,
                                       uint32_t disp_w = 1280,
                                       uint32_t disp_h = 800,
                                       uint16_t table_size = 4,
                                       uint8_t table_bar = 1) {
    g_mock.reset();
    setup_pci_hdr(0x1AF4, 0x1050, 0x1AF4, 0x0010);
    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 64, 0}
    };
    build_cap_chain(caps, 0xA0);
    setup_msix_cap(0xA0, 0, table_size, table_bar, 0x000, table_bar, 0x800);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;
    g_mock.queues[1].size = 32;

    uint64_t feat = VIRTIO_F_VERSION_1;
    g_mock.device_features = feat;

    g_mock.gpu_auto_process = true;
    g_mock.gpu_skip_reply = false;
    g_mock.gpu_override_len = -1;
    g_mock.gpu_override_desc_id = -1;
    g_mock.gpu_override_resp_type = 0;
    g_mock.gpu_display_w = disp_w;
    g_mock.gpu_display_h = disp_h;
    g_mock.gpu_processed_count = 0;
    g_mock.gpu_commands_seen.clear();

    return virtio_pci_probe(0, 7, 0, dev, 0);
}

// The real command queue must never attach an untransferred initial resource.
void test_virtio_gpu_probe_reset_ownership() {
    for(uint8_t initial:{uint8_t(0),uint8_t(VIRTIO_STATUS_DRIVER_OK|VIRTIO_STATUS_DRIVER|VIRTIO_STATUS_ACKNOWLEDGE)}) {
        virtio_gpu_reset_all();gpu_scanout_model.reset();VirtioPciDevice dev{};
        TEST_ASSERT(setup_mock_gpu_device(&dev,1280,720,0,initial), "GPU probe feature negotiation");
        TEST_ASSERT(gpu_scanout_model.display_resets==(initial?1U:0U), "Cold GPU retains firmware; active owner resets once");
        const auto resets=gpu_scanout_model.display_resets;
        TEST_ASSERT(virtio_gpu_init(&dev), "GPU consumes completed PCI negotiation");
        TEST_ASSERT(gpu_scanout_model.display_resets==resets, "GPU init never resets its own clean probe again");
        virtio_gpu_reset_all();gpu_scanout_model.reset(false);
    }
}

void test_virtio_gpu_first_publication() {
    for(unsigned generation=0;generation<2;++generation) {
        virtio_gpu_reset_all();
        VirtioPciDevice dev{};
        GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
        TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe first-frame GPU");
        TEST_ASSERT(virtio_gpu_init(&dev), "Init first-frame GPU");
        auto* drv=virtio_gpu_get_driver(0);
        TEST_ASSERT(drv->initial_scanout_pending&&!drv->scanout_set, "New generation is not visible");
        for(auto cmd:g_mock.gpu_commands_seen)
            TEST_ASSERT(cmd!=VIRTIO_GPU_CMD_SET_SCANOUT&&cmd!=VIRTIO_GPU_CMD_RESOURCE_FLUSH&&cmd!=VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
                        "No blank initial publication");
        // A resolution change while unpublished also stays off-screen.
        TEST_ASSERT(virtio_gpu_setup_framebuffer(drv,1024,768), "Unpublished resolution change");
        TEST_ASSERT(drv->initial_scanout_pending&&!drv->scanout_set, "New resolution still pending");
        g_mock.gpu_commands_seen.clear();
        TEST_ASSERT(!virtio_gpu_transfer_and_flush(drv,0,0,16,16), "Partial first frame rejected");
        TEST_ASSERT(g_mock.gpu_commands_seen.empty(), "No partial GPU publication commands");
        // Distinct background/title/message sentinels prove the actual transfer
        // consumes this complete CPU frame and the current resource, not a stale one.
        std::vector<uint32_t> complete(drv->fb_size/4,0x101720);
        complete[302*1024+500]=0xE9BE76;complete[382*1024+500]=0xE1E6ED;
        memcpy(drv->fb_vaddr,complete.data(),drv->fb_size);
        g_mock.gpu_capture_transfer=true;
        TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,1024,768), "Publish complete initial frame");
        const std::vector<uint32_t> expected={VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,VIRTIO_GPU_CMD_SET_SCANOUT,VIRTIO_GPU_CMD_RESOURCE_FLUSH};
        TEST_ASSERT(g_mock.gpu_commands_seen==expected, "Complete transfer precedes scanout and single flush");
        TEST_ASSERT(g_mock.gpu_transferred_frame==complete&&g_mock.gpu_transferred_resource==drv->resource_id,
                    "First uploaded frame has complete title/message/background data in the new resource");
        g_mock.gpu_capture_transfer=false;
        TEST_ASSERT(drv->scanout_set&&!drv->initial_scanout_pending, "First publication complete");
        g_mock.gpu_commands_seen.clear();
        TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,16,16), "Later partial frame allowed");
        TEST_ASSERT(g_mock.gpu_commands_seen.size()==2, "Later frame has normal transfer/flush");
        virtio_gpu_reset_all();
    }
    // Failed initial upload/attachment never authorizes a blank scanout/flush.
    for(auto failure:{VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,VIRTIO_GPU_CMD_SET_SCANOUT}) {
        virtio_gpu_reset_all();VirtioPciDevice dev{};
        TEST_ASSERT(setup_mock_gpu_device(&dev)&&virtio_gpu_init(&dev), "Init failed-publication probe");
        auto* drv=virtio_gpu_get_driver(0);g_mock.gpu_commands_seen.clear();
        g_mock.gpu_fail_cmd_type=failure;
        TEST_ASSERT(!virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height), "Failed first publication propagates");
        TEST_ASSERT(drv->initial_scanout_pending&&!drv->scanout_set, "Failed first publication stays invisible");
        TEST_ASSERT(g_mock.gpu_commands_seen.size()==(failure==VIRTIO_GPU_CMD_SET_SCANOUT?2U:1U), "No flush follows failed first publication");
        g_mock.gpu_fail_cmd_type=0;virtio_gpu_reset_all();
    }

}

// ----------------------------------------------------------------------------
// 88. VirtIO-GPU: Inicjalizacja i zwalnianie zasobow
// ----------------------------------------------------------------------------
void test_virtio_gpu_init_and_cleanup() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe urzadzenia GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Inicjalizacja VirtIO-GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "Pobranie instancji drv != nullptr");
    TEST_ASSERT(drv->initialized, "drv->initialized == true");
    // Since 950352a the explicit guest boot policy (display_mode_policy,
    // default 1280x720) owns the initial mode; GET_DISPLAY_INFO (1280x800
    // here) is only a hint. The previous 1280x800 expectation was stale.
    TEST_ASSERT(drv->display_width == display_mode_policy.initial_width, "display_width == polityka gościa");
    TEST_ASSERT(drv->display_height == display_mode_policy.initial_height, "display_height == polityka gościa");
    TEST_ASSERT(drv->display_width == 1280 && drv->display_height == 720, "domyslna polityka 1280x720");
    TEST_ASSERT(drv->resource_id == 1, "resource_id == 1");
    TEST_ASSERT(drv->fb_vaddr != nullptr, "fb_vaddr != nullptr");

    TEST_ASSERT(virtio_gpu_reset(drv), "Reset i sprzatanie urzadzenia");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po resecie");
    TEST_ASSERT(g_host_fb_allocs.empty(), "Zero wyciekow stron bufora ramki po resecie");
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 89. VirtIO-GPU: Pobranie i walidacja informacji o wyswietlaczu (display info)
// ----------------------------------------------------------------------------
void test_virtio_gpu_get_display_info() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev, 1920, 1080), "Probe GPU 1920x1080");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    VirtioGpuRespDisplayInfo info{};
    TEST_ASSERT(virtio_gpu_cmd_get_display_info(drv, &info), "get_display_info");
    TEST_ASSERT(info.hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO, "Typ odpowiedzi OK_DISPLAY_INFO");
    TEST_ASSERT(info.pmodes[0].enabled == 1, "Scanout 0 aktywny");
    TEST_ASSERT(info.pmodes[0].r.width == 1920, "Scanout 0 width == 1920");
    TEST_ASSERT(info.pmodes[0].r.height == 1080, "Scanout 0 height == 1080");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 90. VirtIO-GPU: Tworzenie zasobu 2D (resource create 2d)
// ----------------------------------------------------------------------------
void test_virtio_gpu_resource_create_2d() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_cmd_resource_create_2d(drv, 2, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 800, 600), "resource_create_2d");
    TEST_ASSERT(virtio_gpu_cmd_resource_unref(drv, 2), "resource_unref");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 91. VirtIO-GPU: Podpinanie pamieci fizycznej scatter-gather (attach backing)
// ----------------------------------------------------------------------------
void test_virtio_gpu_attach_backing() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    VirtioGpuMemEntry entries[2]{};
    entries[0].addr = 0x100000; entries[0].length = 4096;
    entries[1].addr = 0x101000; entries[1].length = 4096;

    TEST_ASSERT(virtio_gpu_cmd_resource_create_2d(drv, 3, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 64, 64), "create_2d");
    TEST_ASSERT(virtio_gpu_cmd_resource_attach_backing(drv, 3, entries, 2), "attach_backing 2 wpisy");
    TEST_ASSERT(virtio_gpu_cmd_resource_detach_backing(drv, 3), "detach_backing");
    TEST_ASSERT(virtio_gpu_cmd_resource_unref(drv, 3), "resource_unref");

    std::vector<VirtioGpuMemEntry> fragmented(8100);
    for (size_t i = 0; i < fragmented.size(); ++i) {
        fragmented[i].addr = 0x200000ULL + i * 8192;
        fragmented[i].length = 4096;
    }
    TEST_ASSERT(virtio_gpu_cmd_resource_create_2d(drv, 4, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
                                                3840, 2160), "4K create_2d");
    TEST_ASSERT(virtio_gpu_cmd_resource_attach_backing(drv, 4, fragmented.data(), fragmented.size()),
                "4K backing: 8100 physically separate pages fit request DMA");
    TEST_ASSERT(virtio_gpu_cmd_resource_detach_backing(drv, 4), "4K detach");
    TEST_ASSERT(virtio_gpu_cmd_resource_unref(drv, 4), "4K unref");

    // Testy brzegowe limitu wpisow DMA attach_backing
    std::vector<VirtioGpuMemEntry> max_entries(8190);
    for (size_t i = 0; i < max_entries.size(); ++i) {
        max_entries[i].addr = 0x300000ULL + i * 4096;
        max_entries[i].length = 4096;
    }
    TEST_ASSERT(virtio_gpu_cmd_resource_create_2d(drv, 5, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 1024, 768), "res 5 create_2d");
    TEST_ASSERT(virtio_gpu_cmd_resource_attach_backing(drv, 5, max_entries.data(), 8190),
                "Maximum fitting entry count (8190) PASS");

    std::vector<VirtioGpuMemEntry> overflow_entries(8191);
    for (size_t i = 0; i < overflow_entries.size(); ++i) {
        overflow_entries[i].addr = 0x300000ULL + i * 4096;
        overflow_entries[i].length = 4096;
    }
    TEST_ASSERT(!virtio_gpu_cmd_resource_attach_backing(drv, 5, overflow_entries.data(), 8191),
                "Maximum + 1 entry count (8191) rejected");

    TEST_ASSERT(virtio_gpu_cmd_resource_detach_backing(drv, 5), "detach res 5");
    TEST_ASSERT(virtio_gpu_cmd_resource_unref(drv, 5), "unref res 5");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 92. VirtIO-GPU: Konfiguracja powiazania ze scanoutem (set scanout)
// ----------------------------------------------------------------------------
void test_virtio_gpu_set_scanout() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(!virtio_gpu_cmd_set_scanout(drv,0,drv->resource_id,0,0,drv->fb_width,drv->fb_height),
        "Raw SETSCANOUT cannot expose untransferred first resource");
    TEST_ASSERT(virtio_gpu_cmd_transfer_to_host_2d(drv,drv->resource_id,0,0,drv->fb_width,drv->fb_height),
        "Complete first transfer before raw scanout");
    TEST_ASSERT(virtio_gpu_cmd_set_scanout(drv, 0, 1, 0, 0, 1280, 800), "set_scanout");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 93. VirtIO-GPU: Przesylanie bufora do hosta (transfer to host 2d)
// ----------------------------------------------------------------------------
void test_virtio_gpu_transfer_to_host_2d() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    const uint64_t transfers_before = drv->transfer_count;
    TEST_ASSERT(virtio_gpu_cmd_transfer_to_host_2d(drv, 1, 10, 20, 100, 50), "transfer_to_host_2d dirty rect");
    TEST_ASSERT(drv->transfer_count == transfers_before + 1, "transfer_count zwiekszony o 1");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 94. VirtIO-GPU: Wymuszenie wyswietlenia ramki (resource flush)
// ----------------------------------------------------------------------------
void test_virtio_gpu_resource_flush() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv,drv->resource_id,0,0,drv->fb_width,drv->fb_height),
        "Raw flush cannot expose pending first resource");
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height),
        "Publish complete first resource before testing partial flush");
    const uint64_t flushes_before = drv->flush_count;
    TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, 1, 10, 20, 100, 50), "resource_flush dirty rect");
    TEST_ASSERT(drv->flush_count == flushes_before + 1, "flush_count zwiekszony o 1");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 95. VirtIO-GPU: Batch dirty rectangles has one bounding flush
// ----------------------------------------------------------------------------
void test_virtio_gpu_batched_dirty_publication() {
    virtio_gpu_reset_all();
    gpu_scanout_model.reset();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv, 0, 0, drv->fb_width, drv->fb_height),
                "Publish complete first resource before batch test");

    const uint32_t width = drv->fb_width;
    auto* framebuffer = static_cast<uint32_t*>(drv->fb_vaddr);
    framebuffer[20U * width + 10U] = 0x00112233U;
    framebuffer[80U * width + 100U] = 0x00445566U;

    g_mock.gpu_commands_seen.clear();
    gpu_scanout_model.transfer_rects.clear();
    gpu_scanout_model.flush_rects.clear();
    const VirtioGpuRect rects[] = {
        {10, 20, 20, 10},
        {100, 80, 30, 20}
    };
    TEST_ASSERT(virtio_gpu_transfer_rects_and_flush(drv, rects, 2),
                "Batch dirty rectangles transfer succeeds");
    const std::vector<uint32_t> expected_commands = {
        VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
        VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
        VIRTIO_GPU_CMD_RESOURCE_FLUSH
    };
    TEST_ASSERT(g_mock.gpu_commands_seen == expected_commands,
                "Batch emits two transfers and one flush");
    TEST_ASSERT(gpu_scanout_model.transfer_rects.size() == 2 &&
                    gpu_scanout_model.flush_rects.size() == 1,
                "Mock observed exact batch command counts");
    const VirtioGpuRect& flush = gpu_scanout_model.flush_rects.back();
    TEST_ASSERT(flush.x == 10 && flush.y == 20 &&
                    flush.width == 120 && flush.height == 80,
                "Flush covers the union of dirty rectangles");
    TEST_ASSERT(!gpu_scanout_model.visible.empty() &&
                    gpu_scanout_model.visible.back().pixels[20U * width + 10U] == 0x00112233U &&
                    gpu_scanout_model.visible.back().pixels[80U * width + 100U] == 0x00445566U,
                "Transferred dirty pixels are visible before the flush publication");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 96. VirtIO-GPU: Obsluga nieprawidlowej/blednej odpowiedzi urzadzenia
// ----------------------------------------------------------------------------
void test_virtio_gpu_malformed_response() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    g_mock.gpu_override_resp_type = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
    TEST_ASSERT(!virtio_gpu_cmd_resource_create_2d(drv, 5, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 100, 100), "Odrzucenie bledu urzadzenia");
    TEST_ASSERT(drv->error_count > 0, "error_count zarejestrowany");

    g_mock.gpu_override_resp_type = 0;
    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 96. VirtIO-GPU: Timeout polecenia kontrolnego
// ----------------------------------------------------------------------------
void test_virtio_gpu_command_timeout() {
    // Semantics since the 2026-11-11 recovery work: a RESOURCE_FLUSH timeout
    // keeps the descriptors with the device (TIMED_OUT_PENDING) instead of
    // latching has_error at once; permanence follows only at the hard deadline.
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height),
                "Complete initial frame before command timeout injection");
    g_mock.gpu_skip_reply = true;
    g_host_cpu = HostCpuModel{};
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, 1, 0, 0, 100, 100), "Timeout zwraca false");
    TEST_ASSERT(g_host_cpu.halts > 0, "Oczekiwanie z IF=1 usypia CPU (model HLT) zamiast wirowac");
    TEST_ASSERT(g_host_cpu.protocol_errors == 0 && g_host_cpu.if_flag, "Protokol cli/sti/hlt poprawny, IF przywrocone");
    TEST_ASSERT(drv->cmd_state == VIRTIO_GPU_CMD_TIMED_OUT_PENDING, "TIMED_OUT_PENDING po timeoucie");
    TEST_ASSERT(drv->recovery.soft_timeouts == 1, "soft_timeouts == 1");
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, 1, 0, 0, 100, 100), "Kolejne polecenie odrzucone");
    TEST_ASSERT(drv->recovery.rejected_while_pending == 1, "odrzucone bez wysylki");

    g_mock.gpu_skip_reply = false;
    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// Regresja (2026-11-16): gpu_wait_for_irq wykonywal CLI w procesie testu (SIGSEGV). Teraz IF/HLT ida przez model
// host_cpu_*; ponizsze przypadki sprawdzaja trzy galezie protokolu oczekiwania.
void test_virtio_gpu_wait_irq_protocol() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU (protokol IRQ)");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU (protokol IRQ)");
    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv, 0, 0, drv->fb_width, drv->fb_height), "Pierwsza klatka");

    // 1. Odpowiedz dostarczona przez przerwanie budzace HLT: dokladnie jeden HLT, polecenie zakonczone.
    g_host_cpu = HostCpuModel{}; g_host_cpu.deliver_gpu_reply_on_halt = 1;
    g_mock.gpu_skip_reply = true;
    const bool ok_hlt = virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 16, 16);
    TEST_ASSERT(ok_hlt && !drv->has_error, "Odpowiedz po wybudzeniu z HLT konczy polecenie");
    TEST_ASSERT(g_host_cpu.halts == 1 && g_host_cpu.disables == 1 && g_host_cpu.enables == 0, "Jeden cykl cli -> sti;hlt");
    TEST_ASSERT(g_host_cpu.protocol_errors == 0 && g_host_cpu.if_flag, "IF=1 po wybudzeniu");

    // 2. Odpowiedz widoczna miedzy CLI a HLT (odczyt zegara tuz przed oczekiwaniem): bez HLT, STI przywraca IF.
    g_host_cpu = HostCpuModel{};
    g_host_use_mock_time = true; g_host_mock_time_ns = 1;
    g_mock.gpu_skip_reply = true; g_gpu_complete_on_clock_read = 2;
    const bool ok_check = virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 16, 16);
    g_gpu_complete_on_clock_read = 0; g_host_use_mock_time = false;
    TEST_ASSERT(ok_check && !drv->has_error, "Odpowiedz wykryta po CLI konczy polecenie");
    TEST_ASSERT(g_host_cpu.halts == 0 && g_host_cpu.disables == 1 && g_host_cpu.enables == 1, "Brak HLT, gdy ring juz ma wpis");
    TEST_ASSERT(g_host_cpu.protocol_errors == 0 && g_host_cpu.if_flag, "IF przywrocone bez HLT");

    // 3. Wolajacy z wylaczonymi przerwaniami: tylko PAUSE, zadnego CLI/STI/HLT; IF pozostaje 0.
    g_host_cpu = HostCpuModel{}; g_host_cpu.if_flag = false;
    g_mock.gpu_skip_reply = true;
    const bool ok_masked = virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 16, 16);
    TEST_ASSERT(!ok_masked, "Brak odpowiedzi przy IF=0 konczy sie timeoutem");
    TEST_ASSERT(g_host_cpu.halts == 0 && g_host_cpu.disables == 0 && g_host_cpu.enables == 0, "IF=0: brak CLI/STI/HLT");
    TEST_ASSERT(!g_host_cpu.if_flag && g_host_cpu.protocol_errors == 0, "IF=0 zachowane");

    g_host_cpu = HostCpuModel{};
    g_mock.gpu_skip_reply = false;
    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// Regression (2026-11-11): a GPU test that returned early through a failing
// TEST_ASSERT left g_gpu_drivers[0].dev pointing into its dead stack frame;
// the next test's virtio_gpu_reset_all() then read it (ASan
// stack-use-after-return; with UAR off, UBSan invalid bool load of
// dev->input_init_active). GpuDriverScope must leave no driver behind.
static void gpu_scope_helper_returns_early(bool* initialized) {
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope;
    *initialized = setup_mock_gpu_device(&dev) && virtio_gpu_init(&dev);
    if (*initialized) return;  // early return with an initialized driver
}
void test_gpu_driver_scope_releases_device_on_early_return() {
    virtio_gpu_reset_all();
    bool initialized = false;
    gpu_scope_helper_returns_early(&initialized);
    TEST_ASSERT(initialized, "helper initialized a driver on its own stack device");
    TEST_ASSERT(virtio_gpu_get_driver(0) == nullptr, "no driver slot survives the helper's frame");
    TEST_ASSERT(virtio_gpu_get_driver_count() == 0, "driver count reset");
    virtio_gpu_reset_all();  // must not touch the dead stack device (ASan)
    TEST_ASSERT(g_host_dma_allocs.empty() && g_host_fb_allocs.empty(), "everything released by the scope");
}

// Regression: a response published during a delayed clock read is not a timeout.
void test_virtio_gpu_completion_at_deadline() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU for deadline race");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU for deadline race");
    auto* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv, 0, 0, drv->fb_width, drv->fb_height),
                "Complete initial frame before deadline injection");
    g_host_use_mock_time = true;
    g_host_mock_time_ns = 1;
    g_mock.gpu_skip_reply = true;
    g_gpu_complete_on_clock_read = 2;
    const bool flush_ok = virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 100, 100);
    const bool flush_healthy = !drv->has_error;
    const bool consumed = drv->controlq->last_used_idx == drv->controlq->used->idx;
    VirtioGpuMemEntry entry = drv->mem_entries[0];
    g_mock.gpu_skip_reply = true;
    g_gpu_complete_on_clock_read = 2;
    const bool attach_ok = virtio_gpu_cmd_resource_attach_backing(drv, drv->resource_id, &entry, 1);
    const bool attach_healthy = !drv->has_error;
    g_gpu_complete_on_clock_read = 0;
    g_mock.gpu_skip_reply = false;
    g_host_use_mock_time = false;
    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
    TEST_ASSERT(flush_ok, "Completion during expired deadline sample must succeed");
    TEST_ASSERT(flush_healthy, "Successful late observation must preserve presentation");
    TEST_ASSERT(consumed, "Completion consumed exactly once");
    TEST_ASSERT(attach_ok, "Attach backing also observes completion after deadline sample");
    TEST_ASSERT(attach_healthy, "Attach deadline race must not poison driver");
}

// ----------------------------------------------------------------------------
// 97. VirtIO-GPU: Ochrona przed przepelnieniem arytmetycznym (overflow safety)
// ----------------------------------------------------------------------------
void test_virtio_gpu_arithmetic_overflow() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(!virtio_gpu_cmd_resource_create_2d(drv, 10, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 0x80000000U, 100), "Odrzucenie szerokosci overflow");
    TEST_ASSERT(!virtio_gpu_cmd_resource_create_2d(drv, 10, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 5000, 5000), "Odrzucenie rozmiaru > 64 MiB");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 98. VirtIO-GPU: Odrzucanie nieprawidlowych wymiarow i granic
// ----------------------------------------------------------------------------
void test_virtio_gpu_invalid_dimensions() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(!virtio_gpu_cmd_resource_create_2d(drv, 11, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 0, 100), "Odrzucenie width=0");
    TEST_ASSERT(!virtio_gpu_cmd_resource_create_2d(drv, 11, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, 100, 0), "Odrzucenie height=0");
    TEST_ASSERT(!virtio_gpu_cmd_transfer_to_host_2d(drv, 1, 1000, 700, 500, 200), "Transfer poza zakresem odrzucony");
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, 1, 1000, 700, 500, 200), "Flush poza zakresem odrzucony");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 99. VirtIO-GPU: Odpornosc na wyczerpanie deskryptorow kolejki controlq
// ----------------------------------------------------------------------------
void test_virtio_gpu_queue_corruption() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height),
                "Complete initial frame before queue exhaustion injection");
    VirtqBuffer dummy_buf{};
    dummy_buf.phys_addr = drv->cmd_paddr;
    dummy_buf.len = 16;
    dummy_buf.is_write = true;
    while (virtqueue_submit(drv->controlq, &dummy_buf, 1, nullptr) >= 0) {}
    TEST_ASSERT(virtqueue_get_num_free(drv->controlq) == 0, "Kolejka wyczerpana");
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, 1, 0, 0, 10, 10), "Polecenie odrzucone przy braku deskryptorow");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 100. VirtIO-GPU: Kwarantanna DMA przy niepotwierdzonym resecie sprzetu
// ----------------------------------------------------------------------------
void test_virtio_gpu_reset_failure_quarantine() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height),
                "Complete old scanout before quarantine test");
    PrezentacjaStartu::stan().inicjalizacja=true;
    TEST_ASSERT(virtio_gpu_setup_framebuffer(drv,1024,768), "Pending startup replacement before failed reset");
    TEST_ASSERT(drv->initial_scanout_pending&&drv->old_fb_vaddr, "Retained visible old backing exists");
    const size_t allocs_before = g_host_dma_allocs.size();
    const size_t frames_before = g_host_fb_allocs.size();
    g_mock.ignore_reset = true;

    TEST_ASSERT(!virtio_gpu_reset(drv), "Reset zwraca false gdy sprzet ignoruje reset");
    TEST_ASSERT(drv->has_error, "drv->has_error == true");
    TEST_ASSERT(g_host_dma_allocs.size() == allocs_before, "Pamiec DMA poddana kwarantannie (niezwolniona)");
    TEST_ASSERT(g_host_fb_allocs.size()==frames_before&&frames_before==2&&drv->old_fb_vaddr,
                "Both current and retained-old backings quarantined until confirmed reset");

    g_mock.ignore_reset = false;
    virtio_gpu_reset_all();
    PrezentacjaStartu::zakoncz_inicjalizacje();
    TEST_ASSERT(g_host_dma_allocs.empty()&&g_host_fb_allocs.empty(), "Confirmed retry releases both quarantined backings");
}

// ----------------------------------------------------------------------------
// 101. VirtIO-GPU: Prawidlowa kolejnosc niszczenia zasobow (teardown ordering)
// ----------------------------------------------------------------------------
void test_virtio_gpu_resource_teardown_ordering() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height), "Complete first frame before runtime lifecycle");
    g_mock.gpu_commands_seen.clear();
    TEST_ASSERT(virtio_gpu_reset(drv), "Reset driver");

    bool seen_scanout = false;
    bool seen_detach = false;
    bool seen_unref = false;
    bool order_ok = true;

    for (uint32_t cmd : g_mock.gpu_commands_seen) {
        if (cmd == VIRTIO_GPU_CMD_SET_SCANOUT) {
            seen_scanout = true;
        } else if (cmd == VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING) {
            if (!seen_scanout) order_ok = false;
            seen_detach = true;
        } else if (cmd == VIRTIO_GPU_CMD_RESOURCE_UNREF) {
            if (!seen_detach) order_ok = false;
            seen_unref = true;
        }
    }
    TEST_ASSERT(order_ok && seen_scanout && seen_detach && seen_unref,
                "Kolejnosc teardown: scanout=0 -> detach_backing -> unref");

    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 102. VirtIO-GPU: Powtarzalne cykle init -> test pattern -> reset (stress/leaks)
// ----------------------------------------------------------------------------
void test_virtio_gpu_repeated_init_reset() {
    virtio_gpu_reset_all();
    constexpr int ITERS = 20;

    for (int i = 0; i < ITERS; ++i) {
        VirtioPciDevice dev{};
        GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
        TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe w petli");
        TEST_ASSERT(virtio_gpu_init(&dev), "Init w petli");
        VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
        TEST_ASSERT(virtio_gpu_render_test_pattern(drv), "Render test pattern w petli");
        TEST_ASSERT(virtio_gpu_reset(drv), "Reset w petli");
        TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA w petli");
        virtio_gpu_reset_all();
    }
}

// ----------------------------------------------------------------------------
// 103. VirtIO-GPU: Operacja z przerwaniami MSI-X
// ----------------------------------------------------------------------------
void test_virtio_gpu_msix_operation() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device_msix(&dev), "Probe GPU z MSI-X");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init z MSI-X");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv->use_msix, "drv->use_msix == true");
    TEST_ASSERT(drv->irq_vector >= 0, "Wektor IRQ przydzielony");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "1 wektor IDT zaalokowany");

    TEST_ASSERT(virtio_gpu_render_test_pattern(drv), "Render pattern z MSI-X");
    TEST_ASSERT(drv->irq_count > 0, "Licznik przerwan MSI-X > 0");

    virtio_gpu_reset(drv);
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Zero wyciekow wektorow IDT po resecie MSI-X");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 104. VirtIO-GPU: Weryfikacja stalych formatow pikseli ze specyfikacja VirtIO
// ----------------------------------------------------------------------------
void test_virtio_gpu_format_constants() {
    TEST_ASSERT(VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM == 1U, "VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM == 1");
    TEST_ASSERT(VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM == 2U, "VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM == 2");
    TEST_ASSERT(VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM == 3U, "VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM == 3");
    TEST_ASSERT(VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM == 4U, "VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM == 4");
    TEST_ASSERT(VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM == 67U, "VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM == 67");
    TEST_ASSERT(VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM == 68U, "VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM == 68");
}

// ----------------------------------------------------------------------------
// 105. VirtIO-GPU: Bufor ramki z nieciaglych stron fizycznych i koalescencja SG
// ----------------------------------------------------------------------------
void test_virtio_gpu_noncontiguous_backing() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    // The SG-coalescing geometry below needs a 1024x768 framebuffer; select it
    // through the guest mode policy that owns the initial mode.
    struct PolicyRestore { uint32_t w=display_mode_policy.initial_width,h=display_mode_policy.initial_height;
        ~PolicyRestore(){display_mode_policy.initial_width=w;display_mode_policy.initial_height=h;} } policy_restore;
    display_mode_policy.initial_width = 1024;
    display_mode_policy.initial_height = 768;
    TEST_ASSERT(setup_mock_gpu_device(&dev, 1024, 768), "Probe GPU 1024x768");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "drv != nullptr");
    TEST_ASSERT(drv->fb_vaddr != nullptr, "fb_vaddr != nullptr");
    TEST_ASSERT(drv->fb_pages == 768, "fb_pages == 768");
    // W host_fb_alloc co 4 strony wystepuje luka fizyczna, wiec 768 / 4 = 192 wpisy SG
    TEST_ASSERT(drv->nr_entries > 1, "nr_entries > 1 dla nieciaglych stron");
    TEST_ASSERT(drv->nr_entries == 192, "nr_entries == 192 (skoki co 4 strony)");
    TEST_ASSERT(drv->nr_entries == g_mock.gpu_last_nr_entries, "g_mock zarejestrowal poprawny nr_entries");
    TEST_ASSERT(!drv->is_contiguous_dma, "is_contiguous_dma == false");

    // Sprawdz renderowanie wzorca na nieciaglym buforze ramki
    TEST_ASSERT(virtio_gpu_render_test_pattern(drv), "Render test pattern");

    TEST_ASSERT(virtio_gpu_reset(drv), "Reset urzadzenia");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
    TEST_ASSERT(g_host_fb_allocs.empty(), "Zero wyciekow stron bufora ramki");
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 106. VirtIO-GPU: Rygorystyczna walidacja used_len odpowiedzi sprzetu
// ----------------------------------------------------------------------------
void test_virtio_gpu_used_len_validation() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "drv != nullptr");
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height),
                "Complete first frame before malformed flush response");

    // Przypadek 1: used_len = 0 (< sizeof(VirtioGpuCtrlHdr))
    g_mock.gpu_override_len = 0;
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, 1, 0, 0, 100, 100), "Odrzucenie used_len == 0");
    TEST_ASSERT(drv->has_error, "drv->has_error == true po znieksztalconym used_len");
    g_mock.gpu_override_len = -1;

    // Reset i reinit po bledzie
    TEST_ASSERT(virtio_gpu_reset(drv), "Reset po bledzie used_len");
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Re-probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Reinit GPU");
    drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(!drv->has_error, "drv->has_error == false po reinit");

    // Przypadek 2: used_len = 12 (< 24 bajty naglowka VirtioGpuCtrlHdr)
    g_mock.gpu_override_len = 12;
    TEST_ASSERT(!virtio_gpu_cmd_transfer_to_host_2d(drv, 1, 0, 0, 50, 50), "Odrzucenie used_len == 12 (< 24)");
    TEST_ASSERT(drv->has_error, "drv->has_error == true po used_len < 24");
    g_mock.gpu_override_len = -1;

    // Reset i reinit
    TEST_ASSERT(virtio_gpu_reset(drv), "Reset po used_len < 24");
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Re-probe GPU 2");
    TEST_ASSERT(virtio_gpu_init(&dev), "Reinit GPU");
    drv = virtio_gpu_get_driver(0);

    // Przypadek 3: used_len > resp_size (np. 1000 bajtow przy oczekiwanych 24 bajtach)
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height),
                "Complete first frame before oversized flush response");
    g_mock.gpu_override_len = 1000;
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, 1, 0, 0, 100, 100), "Odrzucenie used_len > resp_size");
    TEST_ASSERT(drv->has_error, "drv->has_error == true po used_len > resp_size");
    g_mock.gpu_override_len = -1;

    virtio_gpu_reset(drv);
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
    TEST_ASSERT(g_host_fb_allocs.empty(), "Zero wyciekow FB");
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 107. VirtIO-GPU: Propagacja i obsluga wszystkich kodow bledu urzadzenia
// ----------------------------------------------------------------------------
void test_virtio_gpu_error_responses() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "drv != nullptr");
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height),
                "Complete first frame before device error response injection");

    const uint32_t error_codes[] = {
        VIRTIO_GPU_RESP_ERR_UNSPEC,
        VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
        VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
        VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
        VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
        VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER
    };

    for (uint32_t err : error_codes) {
        g_mock.gpu_override_resp_type = err;
        const uint64_t prev_err_cnt = drv->error_count;
        bool ok = virtio_gpu_cmd_resource_flush(drv, 1, 0, 0, 10, 10);
        TEST_ASSERT(!ok, "Polecenie odrzucone dla kodu bledu");
        TEST_ASSERT(drv->error_count > prev_err_cnt, "Zwiekszony licznik bledow");
    }

    g_mock.gpu_override_resp_type = 0;
    TEST_ASSERT(virtio_gpu_reset(drv), "Reset po testach kodow bledu");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
    TEST_ASSERT(g_host_fb_allocs.empty(), "Zero wyciekow FB");
    virtio_gpu_reset_all();
}

static uint32_t g_mock_boot_w = 0;
static uint32_t g_mock_boot_h = 0;

void grafika_pobierz_boot_rozmiar(uint32_t* out_w, uint32_t* out_h) {
    if (out_w) *out_w = g_mock_boot_w;
    if (out_h) *out_h = g_mock_boot_h;
}

// ----------------------------------------------------------------------------
// 108. VirtIO-GPU: wybor poczatkowego trybu (HOST > bootloader > profil domyslny)
// ----------------------------------------------------------------------------
void test_virtio_gpu_boot_mode_preservation() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    struct PolicyRestore { uint32_t w=display_mode_policy.initial_width,h=display_mode_policy.initial_height;
        ~PolicyRestore(){display_mode_policy.initial_width=w;display_mode_policy.initial_height=h;g_mock_boot_w=g_mock_boot_h=0;} } policy_restore;

    /*
     * Polityka produktu od 950352a (docs/graphics-performance-2026-11-09.md):
     * jawna polityka gościa (display_mode_policy, domyślnie 1280x720, albo
     * bursztyn.video=WxH) wybiera tryb startowy. GET_DISPLAY_INFO (okno QEMU)
     * oraz tryb bootloadera/GOP są wyłącznie wskazówkami. Poprzednia wersja
     * tego testu opisywała starą politykę "host wygrywa" i była nieaktualna.
     */
    // Przypadek 1: host 1280x800, bootloader 640x480 -> wygrywa polityka 1280x720.
    g_mock_boot_w = 640;
    g_mock_boot_h = 480;
    TEST_ASSERT(setup_mock_gpu_device(&dev, 1280, 800), "Setup mock GPU z dinfo=1280x800");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU przy roznych trybach host/bootloader");
    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "drv != nullptr");
    TEST_ASSERT(drv->fb_width == 1280 && drv->fb_height == 720, "fb 1280x720 (polityka gościa)");
    TEST_ASSERT(drv->display_width == 1280 && drv->display_height == 720, "display 1280x720 (polityka gościa)");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();

    // Przypadek 2: polityka 1024x768 (np. bursztyn.video), host 1920x1080,
    // bootloader 1280x720 -> wygrywa polityka.
    display_mode_policy.initial_width = 1024;
    display_mode_policy.initial_height = 768;
    g_mock_boot_w = 1280;
    g_mock_boot_h = 720;
    TEST_ASSERT(setup_mock_gpu_device(&dev, 1920, 1080), "Setup mock GPU z dinfo=1920x1080");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU z polityka 1024x768");
    drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv->fb_width == 1024, "fb_width == 1024 (polityka bije host i GOP)");
    TEST_ASSERT(drv->fb_height == 768, "fb_height == 768 (polityka bije host i GOP)");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();

    // Przypadek 3: bootloader za duzy (> 64 MiB) nie wplywa na wybor.
    g_mock_boot_w = 50000;
    g_mock_boot_h = 50000;
    TEST_ASSERT(setup_mock_gpu_device(&dev, 1024, 768), "Setup mock GPU 1024x768");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU z niepoprawnym boot mode");
    drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv->fb_width == 1024, "fb_width == 1024 (odrzucony boot mode)");
    TEST_ASSERT(drv->fb_height == 768, "fb_height == 768 (odrzucony boot mode)");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 109. VirtIO-GPU: Dynamiczna zmiana rozdzielczosci i wycofanie zmian (rollback)
// ----------------------------------------------------------------------------
void test_virtio_gpu_runtime_modeset_and_rollback() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    g_mock_boot_w = 1280;
    g_mock_boot_h = 720;
    TEST_ASSERT(setup_mock_gpu_device(&dev, 1280, 720), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU z 1280x720");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height), "Complete first frame before runtime lifecycle");
    TEST_ASSERT(drv != nullptr, "drv != nullptr");
    const uint32_t initial_res_id = drv->resource_id;

    // Krok 1: Zmiana 1280x720 -> 1920x1080
    TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1920, 1080), "Zmiana na 1920x1080");
    TEST_ASSERT(drv->fb_width == 1920 && drv->fb_height == 1080, "Geometria 1920x1080");
    TEST_ASSERT(drv->resource_id != initial_res_id, "Nowy identyfikator zasobu");
    TEST_ASSERT(drv->old_resource_id == initial_res_id, "Poprzedni zasob w old_resource_id");
    TEST_ASSERT(drv->old_display_width == 1280 && drv->old_display_height == 720, "Stara geometria zachowana");

    // Symulacja udanego render/flush i zwolnienia starego zasobu
    virtio_gpu_free_stale_resource(drv);
    TEST_ASSERT(drv->old_resource_id == 0, "old_resource_id wyzerowany po zwolnieniu");
    TEST_ASSERT(drv->old_fb_vaddr == nullptr, "old_fb_vaddr wyzerowany");

    // Krok 2: Zmiana 1920x1080 -> 1024x768
    const uint32_t prev_id = drv->resource_id;
    TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1024, 768), "Zmiana na 1024x768");
    TEST_ASSERT(drv->fb_width == 1024 && drv->fb_height == 768, "Geometria 1024x768");
    TEST_ASSERT(drv->old_resource_id == prev_id, "Poprzedni zasob 1920x1080 w old_resource_id");

    // Krok 3: Symulacja wycofania zmian (rollback) - powrot do 1920x1080
    TEST_ASSERT(virtio_gpu_rollback_modeset(drv), "Rollback modeset");
    TEST_ASSERT(drv->fb_width == 1920 && drv->fb_height == 1080, "Przywrocona geometria 1920x1080");
    TEST_ASSERT(drv->resource_id == prev_id, "Przywrocony identyfikator zasobu");
    TEST_ASSERT(drv->old_resource_id == 0, "old_resource_id wyczyszczony");
    TEST_ASSERT(!drv->has_error, "drv->has_error == false");

    // Krok 4: Odrzucenie niepoprawnych wymiarow
    TEST_ASSERT(!virtio_gpu_setup_framebuffer(drv, 0, 0), "Odrzucenie 0x0");
    TEST_ASSERT(!virtio_gpu_setup_framebuffer(drv, 50000, 50000), "Odrzucenie 50000x50000");
    TEST_ASSERT(drv->fb_width == 1920 && drv->fb_height == 1080, "Stan nienaruszony");

    // Krok 5: Wykonanie pelnego pakietu virtio_gpu_test_runtime_modeset
    g_mock_boot_w = 1920;
    g_mock_boot_h = 1080;
    TEST_ASSERT(virtio_gpu_test_runtime_modeset(drv), "virtio_gpu_test_runtime_modeset");

    TEST_ASSERT(virtio_gpu_reset(drv), "Reset urzadzenia");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
    TEST_ASSERT(g_host_fb_allocs.empty(), "Zero wyciekow FB");
    virtio_gpu_reset_all();
    g_mock_boot_w = 0;
    g_mock_boot_h = 0;
}

// ----------------------------------------------------------------------------
// 110. VirtIO-GPU: Audyt cyklu 15-sekundowego potwierdzenia, wstrzykiwania bledow i zarzadzania pamiecia
// ----------------------------------------------------------------------------
void test_virtio_gpu_confirmation_lifecycle_and_injected_failures() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    GpuDriverScope gpu_scope; // resets drivers while the stack device is alive
    g_mock_boot_w = 1280;
    g_mock_boot_h = 720;
    TEST_ASSERT(setup_mock_gpu_device(&dev, 1280, 720), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU z 1280x720");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(virtio_gpu_transfer_and_flush(drv,0,0,drv->fb_width,drv->fb_height), "Complete first frame before runtime lifecycle");
    TEST_ASSERT(drv != nullptr, "drv != nullptr");
    TEST_ASSERT(drv->fb_width == 1280 && drv->fb_height == 720, "Poczatkowa geometria 1280x720");
    TEST_ASSERT(drv->resource_id == 1, "Poczatkowy resource_id == 1");
    TEST_ASSERT(drv->old_resource_id == 0, "Brak starego zasobu na starcie");
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "Dokladnie 1 aktywny bufor ramki");

    // ------------------------------------------------------------------------
    // SCENARIUSZ 1: Przelaczenie 1280x720 -> 1920x1080 z zatwierdzeniem (Confirm)
    // ------------------------------------------------------------------------
    // Krok 1a: Zmiana trybu w sterowniku (setup_framebuffer)
    TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1920, 1080), "Modeset 1280x720 -> 1920x1080");
    // W trakcie zmiany (przed pierwszym renderem/flushem):
    TEST_ASSERT(drv->resource_id == 2, "Aktywny nowy zasob 2 (1920x1080)");
    TEST_ASSERT(drv->old_resource_id == 1, "Poprzedni zasob 1 (1280x720) w buforze przejsciowym");
    TEST_ASSERT(g_host_fb_allocs.size() == 2, "Tymczasowo 2 aktywne bufory ramki w trakcie atomowej zmiany");

    // Krok 1b: Po pierwszym renderze i flushu nastepuje zwolnienie starego zasobu
    virtio_gpu_free_stale_resource(drv);
    TEST_ASSERT(drv->old_resource_id == 0, "old_resource_id wyczyszczony do 0");
    TEST_ASSERT(drv->old_fb_vaddr == nullptr, "old_fb_vaddr == nullptr");
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "W stanie 15s oczekiwania istnieje dokladnie 1 bufor VirtIO-GPU");

    // Krok 1c: Uzytkownik klika 'Zachowaj' (potwierdzenie):
    // Sprawdzenie odpornosci na podwojne zwolnienie (idempotentnosc free_stale_resource)
    virtio_gpu_free_stale_resource(drv);
    TEST_ASSERT(drv->old_resource_id == 0, "Brak double-free po potwierdzeniu");
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "Nadal dokladnie 1 aktywny bufor ramki");

    // ------------------------------------------------------------------------
    // SCENARIUSZ 2: Przelaczenie 1920x1080 -> 1024x768 z wycofaniem po timeout (Rollback)
    // ------------------------------------------------------------------------
    // Krok 2a: Przelaczenie na 1024x768
    TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1024, 768), "Modeset 1920x1080 -> 1024x768");
    TEST_ASSERT(drv->resource_id == 1, "Nowy zasob 1 (1024x768)");
    TEST_ASSERT(drv->old_resource_id == 2, "Stary zasob 2 (1920x1080)");
    virtio_gpu_free_stale_resource(drv);
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "W stanie 15s oczekiwania: dokladnie 1 bufor");

    // Krok 2b: Uplywa 15 sekund bez potwierdzenia -> sciezka rollback w ekran.cpp odtwarza poprzedni tryb
    // Odtwarzanie 1920x1080:
    TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1920, 1080), "Rollback: ponowne utworzenie 1920x1080");
    TEST_ASSERT(drv->resource_id == 2, "Przywrocony zasob 2 (1920x1080)");
    TEST_ASSERT(drv->old_resource_id == 1, "Tymczasowy niepotwierdzony zasob 1 (1024x768) do zwolnienia");
    virtio_gpu_free_stale_resource(drv);
    TEST_ASSERT(drv->old_resource_id == 0, "old_resource_id wyzerowany");
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "Po rollbacku powrot do dokladnie 1 aktywnego bufora");
    TEST_ASSERT(drv->fb_width == 1920 && drv->fb_height == 1080, "Geometria 1920x1080 poprawnie odzyskana");

    // ------------------------------------------------------------------------
    // SCENARIUSZ 3: Powtarzalne cykle Confirm oraz Rollback (20 iteracji)
    // ------------------------------------------------------------------------
    for (int i = 0; i < 20; ++i) {
        if (i % 2 == 0) {
            // Cykl confirm
            TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1280, 720), "Cykl confirm: switch 1280x720");
            virtio_gpu_free_stale_resource(drv);
            TEST_ASSERT(g_host_fb_allocs.size() == 1, "Cykl confirm: 1 bufor");
        } else {
            // Cykl rollback
            TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1024, 768), "Cykl rollback: switch 1024x768");
            virtio_gpu_free_stale_resource(drv);
            TEST_ASSERT(g_host_fb_allocs.size() == 1, "Cykl rollback: 1 bufor");
            // Wycofanie powrotne do 1280x720
            TEST_ASSERT(virtio_gpu_setup_framebuffer(drv, 1280, 720), "Cykl rollback: revert 1280x720");
            virtio_gpu_free_stale_resource(drv);
            TEST_ASSERT(g_host_fb_allocs.size() == 1, "Cykl rollback: 1 bufor po revert");
        }
    }
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "Po 20 cyklach dokladnie 1 bufor ramki");

    // ------------------------------------------------------------------------
    // SCENARIUSZ 4: Wstrzykiwanie bledow w trakcie procedury modesettingu
    // ------------------------------------------------------------------------
    const uint32_t baseline_res = drv->resource_id;

    // 4a. Blad w trakcie RESOURCE_CREATE_2D
    g_mock.gpu_fail_cmd_type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    TEST_ASSERT(!virtio_gpu_setup_framebuffer(drv, 2560, 1440), "Odrzucenie przy bledzie RESOURCE_CREATE_2D");
    g_mock.gpu_fail_cmd_type = 0;
    TEST_ASSERT(drv->resource_id == baseline_res, "Zasob poczatkowy nienaruszony");
    TEST_ASSERT(drv->old_resource_id == 0, "Brak starego zasobu w buforze przejsciowym");
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "Zero wyciekow FB po odrzuceniu create_2d");

    // 4b. Blad w trakcie RESOURCE_ATTACH_BACKING
    g_mock.gpu_fail_cmd_type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    TEST_ASSERT(!virtio_gpu_setup_framebuffer(drv, 2560, 1440), "Odrzucenie przy bledzie ATTACH_BACKING");
    g_mock.gpu_fail_cmd_type = 0;
    TEST_ASSERT(drv->resource_id == baseline_res, "Zasob poczatkowy nienaruszony");
    TEST_ASSERT(drv->old_resource_id == 0, "Brak starego zasobu w buforze przejsciowym");
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "Zero wyciekow FB po odrzuceniu attach_backing");

    // 4c. Blad w trakcie SETSCANOUT (mid-transition)
    g_mock.gpu_fail_cmd_type = VIRTIO_GPU_CMD_SET_SCANOUT;
    TEST_ASSERT(!virtio_gpu_setup_framebuffer(drv, 2560, 1440), "Odrzucenie przy bledzie SETSCANOUT");
    g_mock.gpu_fail_cmd_type = 0;
    TEST_ASSERT(drv->resource_id == baseline_res, "Zasob poczatkowy nienaruszony po nieudanym scanout");
    TEST_ASSERT(drv->old_resource_id == 0, "Brak starego zasobu w buforze przejsciowym");
    TEST_ASSERT(g_host_fb_allocs.size() == 1, "Zero wyciekow FB po nieudanym SETSCANOUT");

    // Weryfikacja ze istniejacy bufor nadal dziala poprawnie po odrzuconych probach
    TEST_ASSERT(virtio_gpu_cmd_transfer_to_host_2d(drv, drv->resource_id, 0, 0, 10, 10), "Transfer po awarii OK");
    TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, drv->resource_id, 0, 0, 10, 10), "Flush po awarii OK");

    TEST_ASSERT(virtio_gpu_reset(drv), "Reset urzadzenia");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po pelnym audycie");
    TEST_ASSERT(g_host_fb_allocs.empty(), "Zero wyciekow FB po pelnym audycie");
    virtio_gpu_reset_all();
    g_mock_boot_w = 0;
    g_mock_boot_h = 0;
}



// ============================================================================
// MOCK PODSYSTEMOW GRAFIKI, KLAWIATURY I INPUT DLA VIRTIO-INPUT
// ============================================================================

static int g_mock_screen_w = 1024;
static int g_mock_screen_h = 768;

int grafika_pobierz_szerokosc(void) { return g_mock_screen_w; }
int grafika_pobierz_wysokosc(void) { return g_mock_screen_h; }

struct MockInputCapture {
    int rel_dx = 0;
    int rel_dy = 0;
    int rel_wheel_v = 0;
    int rel_wheel_h = 0;
    int abs_x = -1;
    int abs_y = -1;
    uint8_t buttons = 0;
    uint32_t rel_calls = 0;
    uint32_t abs_calls = 0;
    uint32_t wheel_calls = 0;
    uint16_t last_key_code = 0;
    uint32_t last_key_val = 0;
    uint32_t key_calls = 0;
    uint32_t last_physical_code = 0;
    bool last_physical_down = false;
    uint32_t last_physical_mods = 0;
    uint32_t physical_calls = 0;
    uint32_t last_codepoint = 0;
    uint32_t last_text_mods = 0;
    uint32_t text_calls = 0;
    std::string last_source;

    void reset() {
        rel_dx = 0; rel_dy = 0;
        rel_wheel_v = 0; rel_wheel_h = 0;
        abs_x = -1; abs_y = -1;
        buttons = 0;
        rel_calls = 0; abs_calls = 0; wheel_calls = 0;
        last_key_code = 0; last_key_val = 0; key_calls = 0;
        last_physical_code = 0; last_physical_down = false; last_physical_mods = 0; physical_calls = 0;
        last_codepoint = 0; last_text_mods = 0; text_calls = 0;
        last_source.clear();
    }
};
static MockInputCapture g_mock_input_capture;
// Optional observation sink for the independent DMA/queue forensic fixture.
static void (*g_input_relative_observer)(int64_t, int64_t, uint8_t, const char*) = nullptr;

extern "C" void zaktualizuj_mysze_zrodlo(int dx, int dy, uint8_t przyciski, const char* zrodlo) {
    if (g_input_relative_observer) {
        g_input_relative_observer(dx, dy, przyciski, zrodlo);
        return;
    }
    g_mock_input_capture.rel_dx += dx;
    g_mock_input_capture.rel_dy += dy;
    g_mock_input_capture.buttons = przyciski;
    g_mock_input_capture.rel_calls++;
    if (zrodlo) g_mock_input_capture.last_source = zrodlo;
}

extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x, int y, uint8_t przyciski, const char* zrodlo) {
    g_mock_input_capture.abs_x = x;
    g_mock_input_capture.abs_y = y;
    g_mock_input_capture.buttons = przyciski;
    g_mock_input_capture.abs_calls++;
    if (zrodlo) g_mock_input_capture.last_source = zrodlo;
}

extern "C" void zaktualizuj_mysze_wzgledne_zrodlo(int32_t dx,int32_t dy,uint8_t buttons,const char* source) {
    if(g_input_relative_observer) {g_input_relative_observer(dx,-int64_t(dy),buttons,source);return;}
    assert(dy!=INT32_MIN); // Extremes use the independent 64-bit observer.
    zaktualizuj_mysze_zrodlo(dx,-dy,buttons,source);
}

extern "C" void zaktualizuj_kolo_myszy(int pion, int poziom, uint8_t przyciski) {
    g_mock_input_capture.rel_wheel_v += pion;
    g_mock_input_capture.rel_wheel_h += poziom;
    g_mock_input_capture.buttons = przyciski;
    g_mock_input_capture.wheel_calls++;
}

extern "C" int grafika_pobierz_mysz_x() { return g_mock_input_capture.abs_x; }
extern "C" int grafika_pobierz_mysz_y() { return g_mock_input_capture.abs_y; }
void klog_serial(const char* msg) { (void)msg; }

extern "C" void klawiatura_test_hook(uint16_t keycode, uint32_t value) {
    g_mock_input_capture.last_key_code = keycode;
    g_mock_input_capture.last_key_val = value;
    g_mock_input_capture.key_calls++;
}

extern "C" bool zaktualizuj_klawiature_gui(char znak) {
    (void)znak;
    return false; // Pozwala znakom trafic do bufor_klawiatury
}

extern "C" bool zaktualizuj_klawisz_fizyczny_gui(uint32_t kod, bool wcisniety, uint32_t modyfikatory) {
    g_mock_input_capture.last_physical_code = kod;
    g_mock_input_capture.last_physical_down = wcisniety;
    g_mock_input_capture.last_physical_mods = modyfikatory;
    g_mock_input_capture.physical_calls++;
    return true;
}

extern "C" bool zaktualizuj_tekst_gui(uint32_t codepoint, uint32_t modyfikatory) {
    g_mock_input_capture.last_codepoint = codepoint;
    g_mock_input_capture.last_text_mods = modyfikatory;
    g_mock_input_capture.text_calls++;
    return true;
}

extern "C" void scheduler_wybudz_klawiature() {
}

struct MockVirtioInputConfigState {
    int dev_type = 0; // 0: keyboard, 1: mouse, 2: tablet
    bool force_zero_size = false;
    bool malformed_size = false;
    uint8_t forced_size = 0;
    std::string name_override;
    std::string serial_override;
    VirtioInputDevids devids{1, 2, 3, 4};
    int abs_min_x = 0;
    int abs_max_x = 32767;
    int abs_min_y = 0;
    int abs_max_y = 32767;
    // Tablet (dev_type 2) advertises BTN_TOUCH. With prop_direct it is a
    // direct-touch device (touchscreen: motion needs contact); without it, it
    // models QEMU 8.x virtio-tablet (BTN_TOUCH bit, no INPUT_PROP_DIRECT).
    bool prop_direct = true;

    void reset() {
        prop_direct = true;
        dev_type = 0;
        force_zero_size = false;
        malformed_size = false;
        forced_size = 0;
        name_override.clear();
        serial_override.clear();
        devids = {1, 2, 3, 4};
        abs_min_x = 0; abs_max_x = 32767;
        abs_min_y = 0; abs_max_y = 32767;
    }
};
static MockVirtioInputConfigState g_mock_input_cfg;
static uint16_t g_mock_input_processed_idx = 0;

void mock_virtio_input_on_config_select(VirtioPciDevice* dev, uint8_t select, uint8_t subsel) {
    if (!dev) return;
    for(auto* f:init_functions) if(&f->dev==dev) {
        // Device-side config response; never use the driver's MMIO writes to
        // manufacture payload beyond the advertised capability.
        uint8_t payload[128]{}; uint8_t size=0;
        if(select==VIRTIO_INPUT_CFG_ID_NAME) {
            const char* name=f->tablet ? "QEMU Virtio Tablet" :
                (f->relative || f->buttons_only) ? "QEMU Virtio Mouse" : "QEMU Virtio Keyboard";
            size=std::strlen(name); std::memcpy(payload,name,size);
        } else if(select==VIRTIO_INPUT_CFG_EV_BITS) {
            const bool pointer=f->tablet || f->relative || f->buttons_only;
            if(subsel==INPUT_EV_KEY) { size=pointer?35:16; payload[pointer?34:0]=1; }
            if(subsel==INPUT_EV_REL && f->relative) { size=1; payload[0]=f->relative_bits; }
            if(subsel==INPUT_EV_ABS && f->tablet) { size=1; payload[0]=3; }
        } else if(select==VIRTIO_INPUT_CFG_ABS_INFO && f->tablet) {
            size=subsel==INPUT_ABS_X ? f->abs_size : f->abs_y_size; VirtioInputAbsInfo info{uint32_t(f->abs_min),uint32_t(f->abs_max),0,0,1};
            std::memcpy(payload,&info,sizeof(info));
        }
        if(dev->device_cfg_len>=3) f->bar[322]=size;
        for(size_t i=0;i<size && 8+i<dev->device_cfg_len;++i) f->bar[328+i]=payload[i];
        return;
    }

    if (g_mock_input_cfg.force_zero_size) {
        virtio_pci_write_device_config8(dev, 2, 0);
        return;
    }
    if (g_mock_input_cfg.malformed_size) {
        virtio_pci_write_device_config8(dev, 2, g_mock_input_cfg.forced_size);
        return;
    }

    switch (select) {
        case VIRTIO_INPUT_CFG_ID_NAME: {
            std::string name = g_mock_input_cfg.name_override;
            if (name.empty()) {
                if (g_mock_input_cfg.dev_type == 0) name = "VirtIO Keyboard";
                else if (g_mock_input_cfg.dev_type == 1) name = "VirtIO Mouse";
                else name = "VirtIO Tablet";
            }
            uint8_t sz = static_cast<uint8_t>(name.size() + 1);
            if (sz > 128) sz = 128;
            virtio_pci_write_device_config8(dev, 2, sz);
            for (size_t i = 0; i < name.size() && i < 127; ++i) {
                virtio_pci_write_device_config8(dev, 8 + i, static_cast<uint8_t>(name[i]));
            }
            virtio_pci_write_device_config8(dev, 8 + (name.size() < 127 ? name.size() : 127), 0);
            break;
        }
        case VIRTIO_INPUT_CFG_ID_SERIAL: {
            std::string serial = g_mock_input_cfg.serial_override;
            if (serial.empty()) serial = "BURSZTYN-INPUT-001";
            uint8_t sz = static_cast<uint8_t>(serial.size() + 1);
            if (sz > 128) sz = 128;
            virtio_pci_write_device_config8(dev, 2, sz);
            for (size_t i = 0; i < serial.size() && i < 127; ++i) {
                virtio_pci_write_device_config8(dev, 8 + i, static_cast<uint8_t>(serial[i]));
            }
            virtio_pci_write_device_config8(dev, 8 + (serial.size() < 127 ? serial.size() : 127), 0);
            break;
        }
        case VIRTIO_INPUT_CFG_ID_DEVIDS: {
            VirtioInputDevids ids = g_mock_input_cfg.devids;
            virtio_pci_write_device_config8(dev, 2, sizeof(ids));
            const auto* p = reinterpret_cast<const uint8_t*>(&ids);
            for (size_t i = 0; i < sizeof(ids); ++i) {
                virtio_pci_write_device_config8(dev, 8 + i, p[i]);
            }
            break;
        }
        case VIRTIO_INPUT_CFG_PROP_BITS: {
            virtio_pci_write_device_config8(dev, 2, 1);
            virtio_pci_write_device_config8(dev, 8,
                (g_mock_input_cfg.dev_type == 2 && g_mock_input_cfg.prop_direct) ? (1U << VIRTIO_INPUT_PROP_DIRECT) : 0);
            break;
        }
        case VIRTIO_INPUT_CFG_EV_BITS: {
            if (subsel == 0) {
                uint8_t bits[2] = {0, 0};
                if (g_mock_input_cfg.dev_type == 0) {
                    bits[0] = (1U << INPUT_EV_KEY);
                } else if (g_mock_input_cfg.dev_type == 1) {
                    bits[0] = (1U << INPUT_EV_KEY) | (1U << INPUT_EV_REL);
                } else if (g_mock_input_cfg.dev_type == 2) {
                    bits[0] = (1U << INPUT_EV_KEY) | (1U << INPUT_EV_ABS);
                }
                virtio_pci_write_device_config8(dev, 2, sizeof(bits));
                virtio_pci_write_device_config8(dev, 8, bits[0]);
                virtio_pci_write_device_config8(dev, 9, bits[1]);
            } else if (subsel == INPUT_EV_KEY) {
                uint8_t key_bits[64]{};
                if (g_mock_input_cfg.dev_type == 0) {
                    for (int k = 0; k < 16; ++k) key_bits[k] = 0xFF;
                } else if (g_mock_input_cfg.dev_type == 1) {
                    key_bits[34] = 0x07; // BTN_LEFT, BTN_RIGHT, BTN_MIDDLE
                } else if (g_mock_input_cfg.dev_type == 2) {
                    key_bits[34] = 0x01; // BTN_LEFT
                    key_bits[41] = 0x04; // BTN_TOUCH
                }
                virtio_pci_write_device_config8(dev, 2, sizeof(key_bits));
                for (size_t i = 0; i < sizeof(key_bits); ++i) {
                    virtio_pci_write_device_config8(dev, 8 + i, key_bits[i]);
                }
            } else if (subsel == INPUT_EV_REL) {
                if (g_mock_input_cfg.dev_type == 1) {
                    uint8_t rel_bits[2] = {
                        static_cast<uint8_t>((1U << INPUT_REL_X) | (1U << INPUT_REL_Y)),
                        static_cast<uint8_t>(1U << (INPUT_REL_WHEEL - 8))
                    };
                    virtio_pci_write_device_config8(dev, 2, sizeof(rel_bits));
                    virtio_pci_write_device_config8(dev, 8, rel_bits[0]);
                    virtio_pci_write_device_config8(dev, 9, rel_bits[1]);
                } else {
                    virtio_pci_write_device_config8(dev, 2, 0);
                }
            } else if (subsel == INPUT_EV_ABS) {
                if (g_mock_input_cfg.dev_type == 2) {
                    uint8_t abs_bits[1] = {
                        static_cast<uint8_t>((1U << INPUT_ABS_X) | (1U << INPUT_ABS_Y))
                    };
                    virtio_pci_write_device_config8(dev, 2, sizeof(abs_bits));
                    virtio_pci_write_device_config8(dev, 8, abs_bits[0]);
                } else {
                    virtio_pci_write_device_config8(dev, 2, 0);
                }
            } else {
                virtio_pci_write_device_config8(dev, 2, 0);
            }
            break;
        }
        case VIRTIO_INPUT_CFG_ABS_INFO: {
            if (g_mock_input_cfg.dev_type == 2) {
                VirtioInputAbsInfo info{};
                if (subsel == INPUT_ABS_X) {
                    info.min = static_cast<uint32_t>(g_mock_input_cfg.abs_min_x);
                    info.max = static_cast<uint32_t>(g_mock_input_cfg.abs_max_x);
                    info.fuzz = 0; info.flat = 0; info.res = 1;
                } else if (subsel == INPUT_ABS_Y) {
                    info.min = static_cast<uint32_t>(g_mock_input_cfg.abs_min_y);
                    info.max = static_cast<uint32_t>(g_mock_input_cfg.abs_max_y);
                    info.fuzz = 0; info.flat = 0; info.res = 1;
                }
                virtio_pci_write_device_config8(dev, 2, sizeof(info));
                const auto* p = reinterpret_cast<const uint8_t*>(&info);
                for (size_t i = 0; i < sizeof(info); ++i) {
                    virtio_pci_write_device_config8(dev, 8 + i, p[i]);
                }
            } else {
                virtio_pci_write_device_config8(dev, 2, 0);
            }
            break;
        }
        default:
            virtio_pci_write_device_config8(dev, 2, 0);
            break;
    }
}

static bool setup_mock_input_device(VirtioPciDevice* dev, uint8_t dev_type = 0) {
    g_mock.reset();
    g_mock_input_cfg.reset();
    g_mock_input_cfg.dev_type = dev_type;
    g_mock_input_capture.reset();
    g_mock_input_processed_idx = 0;

    setup_pci_hdr(0x1AF4, 0x1052, 0x1AF4, 0x0012);

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 256, 0}
    };
    build_cap_chain(caps);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;
    g_mock.queues[1].size = 32;

    g_mock.device_features = VIRTIO_F_VERSION_1;

    return virtio_pci_probe(0, 8, 0, dev, 0);
}

static bool setup_mock_input_device_msix(VirtioPciDevice* dev, uint8_t dev_type = 0,
                                         uint16_t table_size = 4, uint8_t table_bar = 1) {
    g_mock.reset();
    g_mock_input_cfg.reset();
    g_mock_input_cfg.dev_type = dev_type;
    g_mock_input_capture.reset();
    g_mock_input_processed_idx = 0;

    setup_pci_hdr(0x1AF4, 0x1052, 0x1AF4, 0x0012);

    std::vector<CapBuilderEntry> caps = {
        {VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x000, 56, 0},
        {VIRTIO_PCI_CAP_NOTIFY_CFG, 0, 0x1000, 0x1000, 4},
        {VIRTIO_PCI_CAP_ISR_CFG,    0, 0x080, 1,  0},
        {VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x100, 256, 0}
    };
    build_cap_chain(caps, 0xA0);
    setup_msix_cap(0xA0, 0, table_size, table_bar, 0x000, table_bar, 0x800);
    wire_virtio_mmio(0, 0x000, 0x080);
    g_mock.queues[0].size = 32;
    g_mock.queues[1].size = 32;

    g_mock.device_features = VIRTIO_F_VERSION_1;

    return virtio_pci_probe(0, 8, 0, dev, 0);
}

static bool mock_inject_input_event(VirtioInputDriver* drv, uint16_t type, uint16_t code, uint32_t value,
                                    int override_len = -1, int override_desc_id = -1) {
    if (!drv || !drv->eventq) return false;
    VirtQueue* vq = drv->eventq;
    if (!vq->avail || !vq->used || !vq->desc) return false;

    if (g_mock_input_processed_idx == vq->avail->idx) {
        return false;
    }

    const uint16_t a_idx = g_mock_input_processed_idx & (vq->queue_size - 1U);
    const uint16_t desc_id = vq->avail->ring[a_idx];
    if (desc_id >= vq->queue_size) return false;

    void* vaddr = host_phys_to_virt(vq->desc[desc_id].addr);
    if (vaddr) {
        auto* ev = static_cast<VirtioInputEvent*>(vaddr);
        ev->type = type;
        ev->code = code;
        ev->value = value;
    }

    const uint32_t rep_len = (override_len >= 0) ? static_cast<uint32_t>(override_len) : 8;
    const uint32_t rep_id = (override_desc_id >= 0) ? static_cast<uint32_t>(override_desc_id) : desc_id;

    const uint16_t u_idx = vq->used->idx & (vq->queue_size - 1U);
    vq->used->ring[u_idx].id = rep_id;
    vq->used->ring[u_idx].len = rep_len;
    asm volatile("" ::: "memory");
    vq->used->idx = static_cast<uint16_t>(vq->used->idx + 1U);
    asm volatile("mfence" ::: "memory");

    g_mock_input_processed_idx++;

    if (drv->use_msix) {
        mock_check_and_trigger_msix(0);
    } else {
        g_mock.isr_val |= 1;
    }
    return true;
}

// ============================================================================
// TESTY STEROWNIKA VIRTIO-INPUT (MODERN PCI 1.X) I ABSTRAKCJI INPUTDEVICE
// ============================================================================

// 1. VirtIO-INPUT: Odpytanie przestrzeni konfiguracji (NAME, SERIAL, DEVIDS, PROP, EV_BITS)
// Legacy event tests explicitly cross both lifecycle phases. Production init
// remains preparation only; the late barrier opens activation in this fixture.
static bool host_input_prepare_and_activate(VirtioInputDriver* drv, VirtioPciDevice* dev) {
    if (!virtio_input_prepare(drv, dev)) return false;
    (void)virtio_input_activate_prepared();
    return virtio_input_activate(drv);
}

void test_virtio_input_config_discovery() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 0), "Probe dev");

    char name[64]{};
    uint8_t sz = 0;
    TEST_ASSERT(virtio_input_query_config(&dev, VIRTIO_INPUT_CFG_ID_NAME, 0, name, &sz, sizeof(name)), "Query NAME");
    TEST_ASSERT(std::strcmp(name, "VirtIO Keyboard") == 0, "Poprawna nazwa urzadzenia");

    char serial[64]{};
    TEST_ASSERT(virtio_input_query_config(&dev, VIRTIO_INPUT_CFG_ID_SERIAL, 0, serial, &sz, sizeof(serial)), "Query SERIAL");
    TEST_ASSERT(std::strcmp(serial, "BURSZTYN-INPUT-001") == 0, "Poprawny numer seryjny");

    VirtioInputDevids ids{};
    TEST_ASSERT(virtio_input_query_config(&dev, VIRTIO_INPUT_CFG_ID_DEVIDS, 0, &ids, &sz, sizeof(ids)), "Query DEVIDS");
    TEST_ASSERT(ids.bustype == 1 && ids.vendor == 2 && ids.product == 3 && ids.version == 4, "Poprawne ID");

    uint8_t ev_bits[8]{};
    TEST_ASSERT(virtio_input_query_config(&dev, VIRTIO_INPUT_CFG_EV_BITS, 0, ev_bits, &sz, sizeof(ev_bits)), "Query EV_BITS");
    TEST_ASSERT((ev_bits[0] & (1U << INPUT_EV_KEY)) != 0, "EV_KEY obslugiwane");
}

// 2. VirtIO-INPUT: Klasyfikacja klawiatury (EV_KEY z kodami 1..127)
void test_virtio_input_keyboard_classification() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 0), "Probe dev");

    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init keyboard driver");
    TEST_ASSERT(drv.has_keyboard, "Sklasyfikowano jako klawiature");
    TEST_ASSERT(!drv.has_rel_mouse, "Nie jest mysza wzgledna");
    TEST_ASSERT(!drv.has_abs_tablet, "Nie jest tabletem bezwzglednym");
    TEST_ASSERT(drv.input_dev.kinds == INPUT_DEVICE_KIND_KEYBOARD, "kinds == KEYBOARD");
    TEST_ASSERT(input_find_device_by_name(drv.name) != nullptr, "Zarejestrowano w InputDevice");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow DMA");
    input_reset_all();
}

// 3. VirtIO-INPUT: Klasyfikacja myszy wzglednej (EV_REL + przyciski myszy)
void test_virtio_input_mouse_rel_classification() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");

    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init mouse driver");
    TEST_ASSERT(!drv.has_keyboard, "Nie jest klawiatura");
    TEST_ASSERT(drv.has_rel_mouse, "Sklasyfikowano jako mysz wzgledna");
    TEST_ASSERT(!drv.has_abs_tablet, "Nie jest tabletem");
    TEST_ASSERT(drv.input_dev.kinds == INPUT_DEVICE_KIND_MOUSE, "kinds == MOUSE");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow DMA");
    input_reset_all();
}

// 4. VirtIO-INPUT: Klasyfikacja tabletu bezwzglednego (EV_ABS + ABS_INFO min/max)
void test_virtio_input_tablet_abs_classification() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 2), "Probe dev");

    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init tablet driver");
    TEST_ASSERT(!drv.has_keyboard, "Nie jest klawiatura");
    TEST_ASSERT(!drv.has_rel_mouse, "Nie jest mysza wzgledna");
    TEST_ASSERT(drv.has_abs_tablet, "Sklasyfikowano jako tablet bezwzgledny");
    TEST_ASSERT(drv.input_dev.kinds == INPUT_DEVICE_KIND_TABLET, "kinds == TABLET");
    TEST_ASSERT(drv.input_dev.abs_x.min == 0 && drv.input_dev.abs_x.max == 32767, "Granice ABS_X");
    TEST_ASSERT(drv.input_dev.abs_y.min == 0 && drv.input_dev.abs_y.max == 32767, "Granice ABS_Y");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Brak wyciekow DMA");
    input_reset_all();
}

// 5. VirtIO-INPUT: Zdarzenia EV_KEY (wcisniecie, zwolnienie, auto-repeat)
void test_virtio_input_ev_key_press_release() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 0), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    g_mock_input_capture.reset();
    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_KEY_A, 1), "Inject press A");
    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0), "Inject SYN");
    TEST_ASSERT(virtio_input_poll_events(&drv, 10) == 2, "Poll 2 events");
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A, "Key A");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "Val 1 (press)");

    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_KEY_A, 0), "Inject release A");
    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0), "Inject SYN");
    TEST_ASSERT(virtio_input_poll_events(&drv, 10) == 2, "Poll 2 events");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 0, "Val 0 (release)");

    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_KEY_A, 2), "Inject repeat A");
    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0), "Inject SYN");
    TEST_ASSERT(virtio_input_poll_events(&drv, 10) == 2, "Poll 2 events");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 2, "Val 2 (repeat)");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 6. VirtIO-INPUT: Zdarzenia przyciskow myszy (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE)
void test_virtio_input_btn_events() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    g_mock_input_capture.reset();
    // LPM down
    mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_BTN_LEFT, 1);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT((g_mock_input_capture.buttons & 0x01) != 0, "LPM wcisniety");

    // PPM down
    mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_BTN_RIGHT, 1);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT((g_mock_input_capture.buttons & 0x03) == 0x03, "LPM i PPM wcisniete");

    // LPM up
    mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_BTN_LEFT, 0);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT((g_mock_input_capture.buttons & 0x01) == 0, "LPM zwolniony");
    TEST_ASSERT((g_mock_input_capture.buttons & 0x02) != 0, "PPM nadal wcisniety");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 7. VirtIO-INPUT: Ruch wzgledny myszy (REL_X, REL_Y) i inwersja osi Y
void test_virtio_input_rel_xy() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    g_mock_input_capture.reset();
    mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_X, 15);
    mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_Y, 8);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);

    TEST_ASSERT(g_mock_input_capture.rel_calls == 1, "Jedno wywolanie dyspozytora myszy");
    TEST_ASSERT(g_mock_input_capture.rel_dx == 15, "dx == 15");
    TEST_ASSERT(g_mock_input_capture.rel_dy == -8, "dy == -8 (inwersja osi dla Bursztyn OS)");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 8. VirtIO-INPUT: Kolo przewijania myszy (REL_WHEEL)
void test_virtio_input_rel_wheel() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    g_mock_input_capture.reset();
    mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_WHEEL, 2);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.rel_wheel_v == 2, "Wheel == +2");

    mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_WHEEL, static_cast<uint32_t>(-1));
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.rel_wheel_v == 1, "Wheel == +1 (2 - 1)");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 9. VirtIO-INPUT: Skalowanie bezwzgledne tabletu (ABS_X, ABS_Y) do rozdzielczosci ekranu
void test_virtio_input_abs_scaling() {
    input_reset_all();
    g_mock_screen_w = 1920;
    g_mock_screen_h = 1080;

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 2), "Probe tablet");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    TEST_ASSERT(mock_inject_input_event(&drv,INPUT_EV_KEY,INPUT_BTN_TOUCH,1),"Active touch contact for coordinate scaling");
    TEST_ASSERT(virtio_input_poll_events(&drv,1)==1,"Consume contact");
    g_mock_input_capture.reset();
    mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_X, 16383);
    mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_Y, 16383);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);

    TEST_ASSERT(g_mock_input_capture.abs_x == 959, "Skalowane X w 1920x1080");
    TEST_ASSERT(g_mock_input_capture.abs_y == 539, "Skalowane Y w 1920x1080");

    // Min: 0 -> 0
    mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_X, 0);
    mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_Y, 0);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.abs_x == 0 && g_mock_input_capture.abs_y == 0, "Naro?nik 0,0");

    // Max: 32767 -> 1919, 1079
    mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_X, 32767);
    mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_Y, 32767);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.abs_x == 1919 && g_mock_input_capture.abs_y == 1079, "Naro?nik 1919,1079");

    // The same registered tablet must use fresh framebuffer geometry, without
    // reset/re-registration or any DPI/work-area margin after a mode change.
    for(unsigned scale:{100U,125U}) {
        (void)scale; // input has physical pixels, deliberately no DPI argument
        for(auto mode:std::vector<std::pair<int,int>>{{1280,720},{1920,1080}}) {
            g_mock_screen_w=mode.first;g_mock_screen_h=mode.second;
            // Establish the display boundary before injecting the next report.
            // A report already queued at the boundary belongs to the old mode.
            TEST_ASSERT(virtio_input_poll_events(&drv,10)==0,"Empty modeset admission barrier");
            for(int i=0;i<=16;++i)for(int edge=0;edge<4;++edge) {
                int along=i*32767/16;
                int raw_x=edge==0?0:(edge==1?32767:along);
                int raw_y=edge==2?0:(edge==3?32767:along);
                mock_inject_input_event(&drv,INPUT_EV_ABS,INPUT_ABS_X,raw_x);
                mock_inject_input_event(&drv,INPUT_EV_ABS,INPUT_ABS_Y,raw_y);
                mock_inject_input_event(&drv,INPUT_EV_SYN,INPUT_SYN_REPORT,0);
                virtio_input_poll_events(&drv,10);
                // Rounded endpoint mapping: nearest pixel, 0 -> 0, 32767 -> extent-1.
                TEST_ASSERT(g_mock_input_capture.abs_x==int((int64_t(raw_x)*(mode.first-1)+16383)/32767) &&
                            g_mock_input_capture.abs_y==int((int64_t(raw_y)*(mode.second-1)+16383)/32767),
                            "Runtime modeset: tablet reaches every physical edge without margin");
            }
        }
    }

    g_mock_screen_w = 1024;
    g_mock_screen_h = 768;
    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// Regression (2026-11-11, native): QEMU >= 8.0 virtio-tablet advertises
// BTN_TOUCH (shared keymap_button) but no INPUT_PROP_DIRECT. It is a pointer
// tablet: hover motion must move the cursor without any contact event. A
// direct-touch device (PROP_DIRECT) keeps contact-gated motion.
void test_virtio_input_qemu_tablet_hover_without_contact() {
    for (const bool direct : {false, true}) {
        input_reset_all();
        g_mock_screen_w = 1280;
        g_mock_screen_h = 720;
        g_mock_input_cfg.reset();
        VirtioPciDevice dev{};
        TEST_ASSERT(setup_mock_input_device(&dev, 2), "Probe tablet");
        g_mock_input_cfg.prop_direct = direct;
        VirtioInputDriver drv{};
        InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
        TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");
        TEST_ASSERT(drv.input_dev.abs_contact_required == direct, "contact gating follows INPUT_PROP_DIRECT");
        g_mock_input_capture.reset();
        g_mock_input_capture.abs_x = g_mock_input_capture.abs_y = -1;
        // Hover: no BTN_TOUCH, exactly what QEMU sends for host pointer motion.
        mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_X, 32741);  // host px 1279 @1280
        mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_Y, 16383);  // QEMU: host px 360 @720
        mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
        virtio_input_poll_events(&drv, 10);
        if (!direct) {
            // Endpoint mapping of QEMU's half-open px*32767/size encoding is
            // within 1 px of the host pixel (1279 -> 1278, 360 -> 359).
            TEST_ASSERT(g_mock_input_capture.abs_x == 1278 && g_mock_input_capture.abs_y == 359,
                        "hover moves the cursor (rounded endpoint mapping)");
            mock_inject_input_event(&drv, INPUT_EV_ABS, INPUT_ABS_X, 0);
            mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
            virtio_input_poll_events(&drv, 10);
            TEST_ASSERT(g_mock_input_capture.abs_x == 0 && g_mock_input_capture.abs_y == 359, "X-only report keeps Y");
            mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_BTN_LEFT, 1);
            mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
            virtio_input_poll_events(&drv, 10);
            TEST_ASSERT(g_mock_input_capture.buttons & 0x01U, "left button at hover position");
        } else {
            TEST_ASSERT(g_mock_input_capture.abs_x == -1, "direct-touch device: no motion without contact");
        }
        TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    }
    g_mock_input_cfg.reset();
    input_reset_all();
}

// Independent ranges are discovered through the real config protocol, then
// processed through the real VirtIO queue -> EV_ABS -> SYN_REPORT dispatcher.
void test_virtio_input_asymmetric_abs_mapping() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev,2),"Probe asymmetric tablet");
    g_mock_input_cfg.abs_min_x=101;g_mock_input_cfg.abs_max_x=60001;
    g_mock_input_cfg.abs_min_y=503;g_mock_input_cfg.abs_max_y=9503;
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv,&dev),"Init asymmetric tablet");
    TEST_ASSERT(drv.input_dev.abs_x.min==101&&drv.input_dev.abs_x.max==60001&&
                drv.input_dev.abs_y.min==503&&drv.input_dev.abs_y.max==9503,
                "Independent ABS_INFO discovery for both axes");
    TEST_ASSERT(mock_inject_input_event(&drv,INPUT_EV_KEY,INPUT_BTN_TOUCH,1),"Active touch contact for asymmetric mapping");
    TEST_ASSERT(virtio_input_poll_events(&drv,1)==1,"Consume contact");
    auto send=[&](int x,int y,bool axis_x=true,bool axis_y=true){
        if(axis_x)mock_inject_input_event(&drv,INPUT_EV_ABS,INPUT_ABS_X,uint32_t(x));
        if(axis_y)mock_inject_input_event(&drv,INPUT_EV_ABS,INPUT_ABS_Y,uint32_t(y));
        mock_inject_input_event(&drv,INPUT_EV_SYN,INPUT_SYN_REPORT,0);
        virtio_input_poll_events(&drv,10);
    };
    // Finish the contact report before changing the display generation. An
    // incomplete contact, just like incomplete motion, cannot cross modeset.
    send(101,503);
    TEST_ASSERT(input_pointer_is_active(&drv.input_dev),"Complete initial contact report");
    // External harness state is deliberately not exposed to the guest. The
    // same raw report must map identically across normal/maximized/restored.
    struct HostWindow {int logical_w,logical_h,pixel_w,pixel_h,ui_scale;};
    const HostWindow hosts[]={{1280,720,1280,720,100},{1920,1040,2400,1300,125},
                              {1280,720,1600,900,125}};
    for(auto mode:std::vector<std::pair<int,int>>{{1280,720},{1920,1080},{1024,768},{800,600},{2560,540}}) {
        g_mock_screen_w=mode.first;g_mock_screen_h=mode.second;
        TEST_ASSERT(virtio_input_poll_events(&drv,10)==0,"Empty asymmetric modeset admission barrier");
        for(const auto& host:hosts) {
            TEST_ASSERT(host.pixel_w>=host.logical_w&&host.pixel_h>=host.logical_h,"Harness host DPI geometry");
            for(auto raw:std::vector<std::pair<int,int>>{{101,503},{60001,503},{101,9503},{60001,9503},
                {30101,5003},{102,504},{60000,9502},{161,512},{59941,9494},
                {30101,503},{30101,9503},{101,5003},{60001,5003},{0,0},{70000,10000}}) {
                send(raw.first,raw.second);
                const int x=raw.first<101?101:(raw.first>60001?60001:raw.first);
                const int y=raw.second<503?503:(raw.second>9503?9503:raw.second);
                // Rounded endpoint mapping (nearest pixel) over the advertised range.
                TEST_ASSERT(g_mock_input_capture.abs_x==int((int64_t(x-101)*(mode.first-1)+59900/2)/59900)&&
                            g_mock_input_capture.abs_y==int((int64_t(y-503)*(mode.second-1)+9000/2)/9000),
                            "Non-square mapping invariant across external host resize/DPI");
            }
            send(101,503);send(60001,0,true,false);
            TEST_ASSERT(g_mock_input_capture.abs_x==mode.first-1&&g_mock_input_capture.abs_y==0,
                        "X-only report preserves logical Y");
            send(0,9503,false,true);
            TEST_ASSERT(g_mock_input_capture.abs_x==mode.first-1&&g_mock_input_capture.abs_y==mode.second-1,
                        "Y-only report preserves logical X");
            // Multiple axis values before SYN coalesce to the last raw value.
            mock_inject_input_event(&drv,INPUT_EV_ABS,INPUT_ABS_X,101);
            mock_inject_input_event(&drv,INPUT_EV_ABS,INPUT_ABS_X,60001);
            mock_inject_input_event(&drv,INPUT_EV_SYN,INPUT_SYN_REPORT,0);
            virtio_input_poll_events(&drv,10);
            TEST_ASSERT(g_mock_input_capture.abs_x==mode.first-1,"Latest raw ABS X before SYN wins");
        }
    }
    // Full signed ranges exercise multiplication beyond 32 bits in the actual
    // dispatcher, independently of the VirtIO tablet's usual unsigned range.
    drv.input_dev.abs_x.min=INT32_MIN;drv.input_dev.abs_x.max=INT32_MAX;
    drv.input_dev.abs_y.min=-100;drv.input_dev.abs_y.max=100;
    g_mock_screen_w=1920;g_mock_screen_h=1080;
    TEST_ASSERT(virtio_input_poll_events(&drv,10)==0,"Empty signed-range modeset admission barrier");
    send(INT32_MAX,100);
    TEST_ASSERT(g_mock_input_capture.abs_x==1919&&g_mock_input_capture.abs_y==1079,"Wide multiplication max");
    send(INT32_MIN,-100);
    TEST_ASSERT(g_mock_input_capture.abs_x==0&&g_mock_input_capture.abs_y==0,"Signed minima");
    TEST_ASSERT(virtio_input_reset(&drv),"Reset asymmetric tablet");input_reset_all();
    g_mock_screen_w=1024;g_mock_screen_h=768;
}

// 10. VirtIO-INPUT: Buforowanie zmian do nadejscia SYN_REPORT
void test_virtio_input_syn_report() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    g_mock_input_capture.reset();
    mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_X, 20);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 0, "Brak zgloszenia przed SYN_REPORT");

    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 1, "Zgloszenie nastapilo po SYN_REPORT");
    TEST_ASSERT(g_mock_input_capture.rel_dx == 20, "dx == 20");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 11. VirtIO-INPUT: Obsluga uszkodzonego rozmiaru w konfiguracji urzadzenia
void test_virtio_input_malformed_config_size() {
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 0), "Probe dev");

    g_mock_input_cfg.malformed_size = true;
    g_mock_input_cfg.forced_size = 250;
    uint8_t out[128]{};
    uint8_t sz = 0;
    TEST_ASSERT(!virtio_input_query_config(&dev, VIRTIO_INPUT_CFG_ID_NAME, 0, out, &sz, sizeof(out)), "Oversized configuration rejected");
    TEST_ASSERT(sz == 0, "No untrusted size exposed to callers");

    g_mock_input_cfg.force_zero_size = true;
    TEST_ASSERT(!virtio_input_query_config(&dev, VIRTIO_INPUT_CFG_ID_NAME, 0, out, &sz, sizeof(out)), "Rozmiar 0 odrzucony");

    g_mock_input_cfg.reset();
}

// 12. VirtIO-INPUT: Odrzucenie pakietow used.len != 8 bajtow
void test_virtio_input_malformed_used_len() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    g_mock_input_capture.reset();
    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_X, 10, 4), "Inject len=4");
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 0, "Pakiet z len!=8 odrzucony");
    TEST_ASSERT(drv.total_events_processed == 0, "Brak inkrementacji przetworzonych zdarzen");

    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_X, 10, 16), "Inject len=16");
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 0, "Pakiet z len=16 odrzucony");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 13. VirtIO-INPUT: Odrzucenie niepoprawnego indeksu deskryptora (desc_id out of bounds)
void test_virtio_input_invalid_desc_id() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 0), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_KEY_A, 1, 8, 99);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(drv.has_error, "Ustawienie flagi bledu przy zlym desc_id");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 14. VirtIO-INPUT: Ignorowanie nieznanych typow zdarzen
void test_virtio_input_unsupported_event_type() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 0), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    g_mock_input_capture.reset();
    mock_inject_input_event(&drv, 0x9999, 1, 1);
    mock_inject_input_event(&drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(g_mock_input_capture.key_calls == 0, "Nieznany typ zdarzenia zignorowany");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 15. VirtIO-INPUT: Ciagly recycling buforow kolejki eventq
void test_virtio_input_queue_recycling() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    for (int batch = 0; batch < 3; ++batch) {
        for (int i = 0; i < 20; ++i) {
            TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_X, 1), "Inject");
        }
        size_t n = virtio_input_poll_events(&drv, 30);
        TEST_ASSERT(n == 20, "Odebrano cala partie zdarzen");
    }
    TEST_ASSERT(drv.total_events_processed == 60, "Przetworzono 60 zdarzen na 32-elementowej kolejce");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 16. VirtIO-INPUT: Przetestowanie zawijania indeksow 16-bitowych pier?cienia (wraparound)
void test_virtio_input_queue_wraparound() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 1), "Probe dev");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init");

    constexpr size_t TOTAL = 5000;
    for (size_t i = 0; i < TOTAL; ++i) {
        TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_X, 1), "Inject wraparound");
        size_t n = virtio_input_poll_events(&drv, 10);
        TEST_ASSERT(n == 1, "Odebrano pojedyncze zdarzenie");
    }
    TEST_ASSERT(drv.total_events_processed == TOTAL, "5000 zdarzen przetworzonych bez zaciecia");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 17. VirtIO-INPUT: Obsluga przerwan MSI-X i minimalnego ISR
void test_virtio_input_msix_setup_and_delivery() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev, 0), "Probe dev z MSI-X");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init z MSI-X");

    TEST_ASSERT(drv.use_msix, "Wlaczono tryb MSI-X");
    TEST_ASSERT(drv.irq_vector >= IDT_PIERWSZY_WEKTOR_DYNAMICZNY, "Wektor IDT zaalokowany");
    TEST_ASSERT(drv.irq_count == 0, "Brak zgloszonych IRQ");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "Zaalokowano 1 wektor");

    TEST_ASSERT(mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_KEY_ESC, 1), "Inject ESC");
    TEST_ASSERT(drv.irq_count == 1, "Otrzymano przerwanie MSI-X w minimalnym ISR");
    TEST_ASSERT(drv.has_pending_events, "Flaga has_pending_events ustawiona");

    virtio_input_poll_events(&drv, 10);
    TEST_ASSERT(!drv.has_pending_events, "Flaga wyczyszczona po obsludze");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Wektor IDT zwolniony");
    TEST_ASSERT(g_host_dma_allocs.empty(), "DMA zwolnione");
    input_reset_all();
}

// 18. VirtIO-INPUT: Fallback do odpytywania (polling fallback) przy braku MSI-X
void test_virtio_input_polling_fallback() {
    input_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device(&dev, 0), "Probe dev bez MSI-X");
    VirtioInputDriver drv{};
    InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init bez MSI-X");

    TEST_ASSERT(!drv.use_msix, "MSI-X wylaczone");
    TEST_ASSERT(drv.irq_vector == -1, "Brak wektora IRQ");
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Zero wektorow IDT");

    mock_inject_input_event(&drv, INPUT_EV_KEY, INPUT_KEY_ENTER, 1);
    TEST_ASSERT(virtio_input_poll_events(&drv, 10) == 1, "Zdarzenie odebrane w trybie polling");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset");
    input_reset_all();
}

// 19. VirtIO-INPUT: Wielokrotne cykle init -> eventy -> reset -> reinit
void test_virtio_input_repeated_init_reset() {
    input_reset_all();
    for (int cycle = 0; cycle < 20; ++cycle) {
        VirtioPciDevice dev{};
        TEST_ASSERT(setup_mock_input_device_msix(&dev, 1), "Probe cyklu");
        VirtioInputDriver drv{};
        InputRegistryScope input_scope_drv; // unregisters while the driver object is alive
        TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init cyklu");

        for (int i = 0; i < 5; ++i) {
            mock_inject_input_event(&drv, INPUT_EV_REL, INPUT_REL_X, 2);
        }
        TEST_ASSERT(virtio_input_poll_events(&drv, 10) == 5, "Poll w cyklu");
        TEST_ASSERT(virtio_input_reset(&drv), "Reset w cyklu");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Zero wektorow IDT po cyklu");
        TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po cyklu");
    }
    input_reset_all();
}

// 20. VirtIO-INPUT: Audit czyszczenia zasobow DMA, wektorow IDT i rejestru urzadzen
void test_virtio_input_dma_and_vector_cleanup() {
    input_reset_all();

    // 1. Cykl urzadzenia klawiatury
    {
        VirtioPciDevice dev_kbd{};
        VirtioInputDriver drv_kbd{};
        InputRegistryScope input_scope_drv_kbd; // unregisters while the driver object is alive
        TEST_ASSERT(setup_mock_input_device_msix(&dev_kbd, 0), "Probe kbd");
        TEST_ASSERT(host_input_prepare_and_activate(&drv_kbd, &dev_kbd), "Init kbd");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "1 wektor zaalokowany dla kbd");
        TEST_ASSERT(input_get_device_count() == 1, "1 urzadzenie zarejestrowane dla kbd");
        TEST_ASSERT(virtio_input_reset(&drv_kbd), "Reset kbd");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Wektor IDT zwolniony po kbd");
        TEST_ASSERT(input_get_device_count() == 0, "Urzadzenie wyrejestrowane po kbd");
        TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po kbd");
    }

    // 2. Cykl urzadzenia myszy
    {
        VirtioPciDevice dev_mouse{};
        VirtioInputDriver drv_mouse{};
        InputRegistryScope input_scope_drv_mouse; // unregisters while the driver object is alive
        TEST_ASSERT(setup_mock_input_device_msix(&dev_mouse, 1), "Probe mouse");
        TEST_ASSERT(host_input_prepare_and_activate(&drv_mouse, &dev_mouse), "Init mouse");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "1 wektor zaalokowany dla mouse");
        TEST_ASSERT(input_get_device_count() == 1, "1 urzadzenie zarejestrowane dla mouse");
        TEST_ASSERT(virtio_input_reset(&drv_mouse), "Reset mouse");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Wektor IDT zwolniony po mouse");
        TEST_ASSERT(input_get_device_count() == 0, "Urzadzenie wyrejestrowane po mouse");
        TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po mouse");
    }

    // 3. Cykl urzadzenia tabletu
    {
        VirtioPciDevice dev_tab{};
        VirtioInputDriver drv_tab{};
        InputRegistryScope input_scope_drv_tab; // unregisters while the driver object is alive
        TEST_ASSERT(setup_mock_input_device_msix(&dev_tab, 2), "Probe tab");
        TEST_ASSERT(host_input_prepare_and_activate(&drv_tab, &dev_tab), "Init tab");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 1, "1 wektor zaalokowany dla tab");
        TEST_ASSERT(input_get_device_count() == 1, "1 urzadzenie zarejestrowane dla tab");
        TEST_ASSERT(virtio_input_reset(&drv_tab), "Reset tab");
        TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Wektor IDT zwolniony po tab");
        TEST_ASSERT(input_get_device_count() == 0, "Urzadzenie wyrejestrowane po tab");
        TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA po tab");
    }

    virtio_input_reset_all();
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 0, "Wszystkie wektory IDT zwolnione");
    TEST_ASSERT(input_get_device_count() == 0, "Wszystkie urzadzenia wyrejestrowane");
    TEST_ASSERT(g_host_dma_allocs.empty(), "Wszystkie bufory DMA zwolnione (zero leak)");
}

// ----------------------------------------------------------------------------
// TESTY GENERYCZNEJ ARCHITEKTURY WEJSCIA (PS/2, USB HID, VIRTIO)
// ----------------------------------------------------------------------------

static VirtioInputDriver s_shared_input_drv{};

static void test_virtio_send_key(VirtioInputDriver* drv, uint16_t keycode, uint32_t val) {
    mock_inject_input_event(drv, INPUT_EV_KEY, keycode, val);
    mock_inject_input_event(drv, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(drv, 10);
}

// 21. Generyczne wejscie: Identyczne zdarzenie InputEvent dla tego samego klawisza (PS/2, USB, VirtIO)
void test_input_same_key_ps2_usb_virtio() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev, 0), "Probe VirtIO kbd");
    VirtioInputDriver& drv = s_shared_input_drv;
    std::memset(&drv, 0, sizeof(drv));
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init VirtIO kbd");

    // 1. Klawisz 'A' (Linux keycode 30 == INPUT_KEY_A)
    // a) PS/2: scancode 0x1E (press)
    ps2_klawiatura_przetworz_scancode(0x1E);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A, "PS/2: kod klawisza A");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "PS/2: wartosc press");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "PS/2: znak 'a' w buforze");
    // PS/2: scancode 0x9E (release)
    ps2_klawiatura_przetworz_scancode(0x9E);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A, "PS/2: kod klawisza A release");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 0, "PS/2: wartosc release");

    // b) USB HID: raport z Usage 0x04 (press)
    uint8_t rep_a[8] = { 0, 0, 0x04, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_a);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A, "USB: kod klawisza A");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "USB: wartosc press");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "USB: znak 'a' w buforze");
    // USB HID: pusty raport (release)
    uint8_t rep_empty[8] = { 0 };
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A, "USB: kod klawisza A release");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 0, "USB: wartosc release");

    // c) VirtIO: inject INPUT_KEY_A (press)
    test_virtio_send_key(&drv, INPUT_KEY_A, 1);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A, "VirtIO: kod klawisza A");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "VirtIO: wartosc press");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "VirtIO: znak 'a' w buforze");
    // VirtIO: inject release
    test_virtio_send_key(&drv, INPUT_KEY_A, 0);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A, "VirtIO: kod klawisza A release");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 0, "VirtIO: wartosc release");

    // 2. Klawisz '1' (Linux keycode 2 == INPUT_KEY_1)
    // a) PS/2: scancode 0x02
    ps2_klawiatura_przetworz_scancode(0x02);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_1, "PS/2: kod klawisza 1");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "PS/2: 1 press");
    TEST_ASSERT(pobierz_znak_klawiatury() == '1', "PS/2: znak '1' w buforze");
    ps2_klawiatura_przetworz_scancode(0x82);

    // b) USB HID: Usage 0x1E (Key 1)
    uint8_t rep_1[8] = { 0, 0, 0x1E, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_1);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_1, "USB: kod klawisza 1");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "USB: 1 press");
    TEST_ASSERT(pobierz_znak_klawiatury() == '1', "USB: znak '1' w buforze");
    usb_hid_przetworz_raport(rep_empty);

    // c) VirtIO: inject INPUT_KEY_1
    test_virtio_send_key(&drv, INPUT_KEY_1, 1);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_1, "VirtIO: kod klawisza 1");
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "VirtIO: 1 press");
    TEST_ASSERT(pobierz_znak_klawiatury() == '1', "VirtIO: znak '1' w buforze");
    test_virtio_send_key(&drv, INPUT_KEY_1, 0);

    TEST_ASSERT(virtio_input_reset(&drv), "Reset VirtIO kbd");
    input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// 22. Generyczne wejscie: Cykl Press i Release we wszystkich zrodlach wejscia
void test_input_press_release() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev, 0), "Probe VirtIO kbd");
    VirtioInputDriver& drv = s_shared_input_drv;
    std::memset(&drv, 0, sizeof(drv));
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init VirtIO kbd");

    // PS/2: Q (0x10 / 0x90)
    ps2_klawiatura_przetworz_scancode(0x10);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_Q && g_mock_input_capture.last_key_val == 1, "PS/2 Q press");
    TEST_ASSERT(g_mock_input_capture.last_physical_down == true, "PS/2 Q physical down");
    ps2_klawiatura_przetworz_scancode(0x90);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_Q && g_mock_input_capture.last_key_val == 0, "PS/2 Q release");
    TEST_ASSERT(g_mock_input_capture.last_physical_down == false, "PS/2 Q physical up");

    // USB HID: Q (Usage 0x14)
    uint8_t rep_q[8] = { 0, 0, 0x14, 0, 0, 0, 0, 0 };
    uint8_t rep_0[8] = { 0 };
    usb_hid_przetworz_raport(rep_q);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_Q && g_mock_input_capture.last_key_val == 1, "USB Q press");
    TEST_ASSERT(g_mock_input_capture.last_physical_down == true, "USB Q physical down");
    usb_hid_przetworz_raport(rep_0);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_Q && g_mock_input_capture.last_key_val == 0, "USB Q release");
    TEST_ASSERT(g_mock_input_capture.last_physical_down == false, "USB Q physical up");

    // VirtIO: Q
    test_virtio_send_key(&drv, INPUT_KEY_Q, 1);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_Q && g_mock_input_capture.last_key_val == 1, "VirtIO Q press");
    TEST_ASSERT(g_mock_input_capture.last_physical_down == true, "VirtIO Q physical down");
    test_virtio_send_key(&drv, INPUT_KEY_Q, 0);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_Q && g_mock_input_capture.last_key_val == 0, "VirtIO Q release");
    TEST_ASSERT(g_mock_input_capture.last_physical_down == false, "VirtIO Q physical up");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset VirtIO kbd");
    input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// 23. Generyczne wejscie: Modyfikatory (Shift, Ctrl, Alt, CapsLock, NumLock)
void test_input_modifiers() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev, 0), "Probe VirtIO kbd");
    VirtioInputDriver& drv = s_shared_input_drv;
    std::memset(&drv, 0, sizeof(drv));
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init VirtIO kbd");

    // 1. Shift (LeftShift, RightShift)
    // PS/2
    ps2_klawiatura_przetworz_scancode(0x2A); // LeftShift down
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) != 0, "PS/2 LShift set");
    ps2_klawiatura_przetworz_scancode(0xAA); // LeftShift up
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) == 0, "PS/2 LShift cleared");

    ps2_klawiatura_przetworz_scancode(0x36); // RightShift down
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) != 0, "PS/2 RShift set");
    ps2_klawiatura_przetworz_scancode(0xB6); // RightShift up
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) == 0, "PS/2 RShift cleared");

    // USB
    uint8_t rep_lshift[8] = { 0x02, 0, 0, 0, 0, 0, 0, 0 }; // LeftShift
    uint8_t rep_empty[8] = { 0 };
    usb_hid_przetworz_raport(rep_lshift);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) != 0, "USB LShift set");
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) == 0, "USB LShift cleared");

    // VirtIO
    test_virtio_send_key(&drv, INPUT_KEY_LEFTSHIFT, 1);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) != 0, "VirtIO LShift set");
    test_virtio_send_key(&drv, INPUT_KEY_LEFTSHIFT, 0);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_SHIFT) == 0, "VirtIO LShift cleared");

    // 2. Ctrl (LeftCtrl, RightCtrl)
    // PS/2 LeftCtrl (0x1D)
    ps2_klawiatura_przetworz_scancode(0x1D);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) != 0, "PS/2 LCtrl set");
    ps2_klawiatura_przetworz_scancode(0x9D);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) == 0, "PS/2 LCtrl cleared");

    // PS/2 RightCtrl (E0 1D)
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x1D);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) != 0, "PS/2 RCtrl set");
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x9D);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) == 0, "PS/2 RCtrl cleared");

    // USB RightCtrl (modifier bit 4 = 0x10)
    uint8_t rep_rctrl[8] = { 0x10, 0, 0, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_rctrl);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) != 0, "USB RCtrl set");
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) == 0, "USB RCtrl cleared");

    // VirtIO RightCtrl
    test_virtio_send_key(&drv, INPUT_KEY_RIGHTCTRL, 1);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) != 0, "VirtIO RCtrl set");
    test_virtio_send_key(&drv, INPUT_KEY_RIGHTCTRL, 0);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CTRL) == 0, "VirtIO RCtrl cleared");

    // 3. Alt (LeftAlt, RightAlt / AltGr)
    // PS/2 RightAlt / AltGr (E0 38)
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x38);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_ALT) != 0, "PS/2 AltGr set");
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0xB8);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_ALT) == 0, "PS/2 AltGr cleared");

    // USB RightAlt / AltGr (modifier bit 6 = 0x40)
    uint8_t rep_ralt[8] = { 0x40, 0, 0, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_ralt);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_ALT) != 0, "USB AltGr set");
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_ALT) == 0, "USB AltGr cleared");

    // VirtIO RightAlt
    test_virtio_send_key(&drv, INPUT_KEY_RIGHTALT, 1);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_ALT) != 0, "VirtIO AltGr set");
    test_virtio_send_key(&drv, INPUT_KEY_RIGHTALT, 0);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_ALT) == 0, "VirtIO AltGr cleared");

    // 4. CapsLock (toggle na press)
    // PS/2: 0x3A press, 0xBA release
    ps2_klawiatura_przetworz_scancode(0x3A);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) != 0, "PS/2 CapsLock toggled ON");
    ps2_klawiatura_przetworz_scancode(0xBA);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) != 0, "PS/2 CapsLock remains ON after release");
    ps2_klawiatura_przetworz_scancode(0x3A);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) == 0, "PS/2 CapsLock toggled OFF");
    ps2_klawiatura_przetworz_scancode(0xBA);

    // USB: Usage 0x39
    uint8_t rep_caps[8] = { 0, 0, 0x39, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_caps);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) != 0, "USB CapsLock toggled ON");
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) != 0, "USB CapsLock remains ON");
    usb_hid_przetworz_raport(rep_caps);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) == 0, "USB CapsLock toggled OFF");
    usb_hid_przetworz_raport(rep_empty);

    // VirtIO: INPUT_KEY_CAPSLOCK
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 1);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) != 0, "VirtIO CapsLock toggled ON");
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 0);
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 1);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_CAPS) == 0, "VirtIO CapsLock toggled OFF");
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 0);

    // 5. NumLock (toggle na press)
    // PS/2: 0x45
    ps2_klawiatura_przetworz_scancode(0x45);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_NUM) != 0, "PS/2 NumLock toggled ON");
    ps2_klawiatura_przetworz_scancode(0xC5);
    ps2_klawiatura_przetworz_scancode(0x45);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_NUM) == 0, "PS/2 NumLock toggled OFF");
    ps2_klawiatura_przetworz_scancode(0xC5);

    // USB: Usage 0x53
    uint8_t rep_num[8] = { 0, 0, 0x53, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_num);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_NUM) != 0, "USB NumLock toggled ON");
    usb_hid_przetworz_raport(rep_empty);
    usb_hid_przetworz_raport(rep_num);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_NUM) == 0, "USB NumLock toggled OFF");
    usb_hid_przetworz_raport(rep_empty);

    // VirtIO: INPUT_KEY_NUMLOCK
    test_virtio_send_key(&drv, INPUT_KEY_NUMLOCK, 1);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_NUM) != 0, "VirtIO NumLock toggled ON");
    test_virtio_send_key(&drv, INPUT_KEY_NUMLOCK, 0);
    test_virtio_send_key(&drv, INPUT_KEY_NUMLOCK, 1);
    TEST_ASSERT((klawiatura_modyfikatory() & BWS_MOD_NUM) == 0, "VirtIO NumLock toggled OFF");
    test_virtio_send_key(&drv, INPUT_KEY_NUMLOCK, 0);

    TEST_ASSERT(virtio_input_reset(&drv), "Reset VirtIO kbd");
    input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// 24. Generyczne wejscie: Klawisze rozszerzone (strzalki, nawigacja, klawiatura numeryczna)
void test_input_extended_keys() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev, 0), "Probe VirtIO kbd");
    VirtioInputDriver& drv = s_shared_input_drv;
    std::memset(&drv, 0, sizeof(drv));
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init VirtIO kbd");

    uint8_t rep_empty[8] = { 0 };

    // 1. Strzalka w gore (Up)
    // PS/2: E0 48
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x48);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_UP, "PS/2 Up: INPUT_KEY_UP");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B, "PS/2 Up: ANSI ESC");
    TEST_ASSERT(pobierz_znak_klawiatury() == '[', "PS/2 Up: ANSI [");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'A', "PS/2 Up: ANSI A");
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0xC8);

    // USB: Usage 0x52 (Up)
    uint8_t rep_up[8] = { 0, 0, 0x52, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_up);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_UP, "USB Up: INPUT_KEY_UP");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B, "USB Up: ANSI ESC");
    TEST_ASSERT(pobierz_znak_klawiatury() == '[', "USB Up: ANSI [");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'A', "USB Up: ANSI A");
    usb_hid_przetworz_raport(rep_empty);

    // VirtIO: INPUT_KEY_UP
    test_virtio_send_key(&drv, INPUT_KEY_UP, 1);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_UP, "VirtIO Up: INPUT_KEY_UP");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B, "VirtIO Up: ANSI ESC");
    TEST_ASSERT(pobierz_znak_klawiatury() == '[', "VirtIO Up: ANSI [");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'A', "VirtIO Up: ANSI A");
    test_virtio_send_key(&drv, INPUT_KEY_UP, 0);

    // 2. Home i End
    // PS/2: Home (E0 47) -> \x1B[H
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x47);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_HOME, "PS/2 Home: INPUT_KEY_HOME");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B, "PS/2 Home ESC");
    TEST_ASSERT(pobierz_znak_klawiatury() == '[', "PS/2 Home [");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'H', "PS/2 Home H");
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0xC7);

    // USB: Home (Usage 0x4A)
    uint8_t rep_home[8] = { 0, 0, 0x4A, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_home);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_HOME, "USB Home: INPUT_KEY_HOME");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B && pobierz_znak_klawiatury() == '[' && pobierz_znak_klawiatury() == 'H', "USB Home seq");
    usb_hid_przetworz_raport(rep_empty);

    // VirtIO: INPUT_KEY_HOME
    test_virtio_send_key(&drv, INPUT_KEY_HOME, 1);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_HOME, "VirtIO Home: INPUT_KEY_HOME");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B && pobierz_znak_klawiatury() == '[' && pobierz_znak_klawiatury() == 'H', "VirtIO Home seq");
    test_virtio_send_key(&drv, INPUT_KEY_HOME, 0);

    // 3. Klawiatura numeryczna: KPEnter i KPSlash
    // PS/2: KPEnter (E0 1C) -> '\n'
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x1C);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_KPENTER, "PS/2 KPEnter: INPUT_KEY_KPENTER");
    TEST_ASSERT(pobierz_znak_klawiatury() == '\n', "PS/2 KPEnter znak");
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x9C);

    // USB: KPEnter (Usage 0x58)
    uint8_t rep_kpent[8] = { 0, 0, 0x58, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_kpent);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_KPENTER, "USB KPEnter: INPUT_KEY_KPENTER");
    TEST_ASSERT(pobierz_znak_klawiatury() == '\n', "USB KPEnter znak");
    usb_hid_przetworz_raport(rep_empty);

    // VirtIO: INPUT_KEY_KPENTER
    test_virtio_send_key(&drv, INPUT_KEY_KPENTER, 1);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_KPENTER, "VirtIO KPEnter: INPUT_KEY_KPENTER");
    TEST_ASSERT(pobierz_znak_klawiatury() == '\n', "VirtIO KPEnter znak");
    test_virtio_send_key(&drv, INPUT_KEY_KPENTER, 0);

    // 4. Insert i Delete
    // USB: Insert (Usage 0x49) -> \x1B[2~
    uint8_t rep_ins[8] = { 0, 0, 0x49, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_ins);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_INSERT, "USB Insert: INPUT_KEY_INSERT");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B && pobierz_znak_klawiatury() == '[' && pobierz_znak_klawiatury() == '2' && pobierz_znak_klawiatury() == '~', "USB Insert seq");
    usb_hid_przetworz_raport(rep_empty);

    // USB: Delete (Usage 0x4C) -> \x1B[3~
    uint8_t rep_del[8] = { 0, 0, 0x4C, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_del);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_DELETE, "USB Delete: INPUT_KEY_DELETE");
    TEST_ASSERT(pobierz_znak_klawiatury() == 0x1B && pobierz_znak_klawiatury() == '[' && pobierz_znak_klawiatury() == '3' && pobierz_znak_klawiatury() == '~', "USB Delete seq");
    usb_hid_przetworz_raport(rep_empty);

    TEST_ASSERT(virtio_input_reset(&drv), "Reset VirtIO kbd");
    input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// 25. Generyczne wejscie: Auto-repeat klawiszy (PS/2, USB, VirtIO)
void test_input_repeat() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev, 0), "Probe VirtIO kbd");
    VirtioInputDriver& drv = s_shared_input_drv;
    std::memset(&drv, 0, sizeof(drv));
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init VirtIO kbd");

    // 1. PS/2 repeat: powtorne nadejscie scancode make (0x1E) bez up (0x9E)
    ps2_klawiatura_przetworz_scancode(0x1E);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "PS/2 initial press");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "Pierwszy znak 'a'");

    ps2_klawiatura_przetworz_scancode(0x1E);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 2, "PS/2 repeat 1");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "Powtorzony znak 'a'");

    ps2_klawiatura_przetworz_scancode(0x1E);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 2, "PS/2 repeat 2");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "Trzeci znak 'a'");

    ps2_klawiatura_przetworz_scancode(0x9E);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 0, "PS/2 release");

    // 2. USB identical state reports respect a typematic delay.
    g_host_use_mock_time=true;g_host_mock_time_ns=0;
    uint8_t rep_a[8] = { 0, 0, 0x04, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_a);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "USB initial press");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "USB pierwszy znak 'a'");

    usb_hid_przetworz_raport(rep_a);
    TEST_ASSERT(pobierz_znak_klawiatury()==0,"USB unchanged report before repeat delay emits no text");
    g_host_mock_time_ns=400000000ULL;
    usb_hid_przetworz_raport(rep_a);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 2, "USB repeat 1");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "USB powtorzony znak 'a'");

    uint8_t rep_empty[8] = { 0 };
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 0, "USB release");

    g_host_use_mock_time=false;
    // 3. VirtIO repeat: zdarzenie z value == 2
    test_virtio_send_key(&drv, INPUT_KEY_A, 1);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 1, "VirtIO initial press");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "VirtIO znak 'a'");

    test_virtio_send_key(&drv, INPUT_KEY_A, 2);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 2, "VirtIO repeat");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'a', "VirtIO powtorzony znak 'a'");

    test_virtio_send_key(&drv, INPUT_KEY_A, 0);
    TEST_ASSERT(g_mock_input_capture.last_key_val == 0, "VirtIO release");

    TEST_ASSERT(virtio_input_reset(&drv), "Reset VirtIO kbd");
    input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// 26. Generyczne wejscie: Brak duplikatow zdarzen i filtrowanie szumu/chordu
void test_input_no_duplicate_key_events() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    // 1. PS/2: Odrzucenie falszywego zwolnienia (spurious release bez wcisniecia)
    uint32_t calls_before = g_mock_input_capture.key_calls;
    ps2_klawiatura_przetworz_scancode(0x9E); // Release dla A, choc A nie byl wcisniety
    TEST_ASSERT(g_mock_input_capture.key_calls == calls_before, "Brak zdarzenia dla falszywego PS/2 release");

    // 2. USB HID: Wielo-klawiszowe raporty (chords) bez duplikatow
    // a) Wcisniecie klawisza A
    uint8_t rep_a[8] = { 0, 0, 0x04, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_a);
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A && g_mock_input_capture.last_key_val == 1, "Press A");
    uint32_t calls_after_a = g_mock_input_capture.key_calls;

    // b) Dochodzi klawisz B przy ciaglym trzymaniu A: [A, B]
    uint8_t rep_ab[8] = { 0, 0, 0x04, 0x05, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_ab);
    // Powinno wygenerowac TYLKO wcisniecie klawisza B, bez re-press lub repeat dla A
    TEST_ASSERT(g_mock_input_capture.key_calls == calls_after_a + 1, "Dokladnie 1 nowe zdarzenie dla klawisza B");
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_B && g_mock_input_capture.last_key_val == 1, "Press B");

    // c) Zwolnienie A przy trzymaniu B: [B]
    uint8_t rep_b[8] = { 0, 0, 0x05, 0, 0, 0, 0, 0 };
    usb_hid_przetworz_raport(rep_b);
    TEST_ASSERT(g_mock_input_capture.key_calls == calls_after_a + 2, "Dokladnie 1 nowe zdarzenie dla zwolnienia A");
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_A && g_mock_input_capture.last_key_val == 0, "Release A");

    // d) Zwolnienie B: []
    uint8_t rep_empty[8] = { 0 };
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT(g_mock_input_capture.key_calls == calls_after_a + 3, "Dokladnie 1 nowe zdarzenie dla zwolnienia B");
    TEST_ASSERT(g_mock_input_capture.last_key_code == INPUT_KEY_B && g_mock_input_capture.last_key_val == 0, "Release B");

    // e) Kolejne puste raporty nie moga generowac zadnych zdarzen
    uint32_t calls_after_all = g_mock_input_capture.key_calls;
    usb_hid_przetworz_raport(rep_empty);
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT(g_mock_input_capture.key_calls == calls_after_all, "Brak zbednych zdarzen dla pustych raportow USB");

    input_reset_all();
}

// 27. Generyczne wejscie: Wspolistnienie urzadzen i obsluga rezerwowa (fallback) PS/2 & USB
void test_input_usb_and_ps2_fallback() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    // Sprawdz rejestracje urzadzen PS/2 i USB
    InputDevice* ps2_dev = ps2_klawiatura_pobierz_urzadzenie();
    TEST_ASSERT(ps2_dev != nullptr, "ps2_dev != nullptr");
    TEST_ASSERT(input_find_device_by_name("ps2-keyboard") == ps2_dev, "Znaleziono ps2-keyboard w rejestrze");

    InputDevice* usb_dev = usb_klawiatura_pobierz_urzadzenie();
    TEST_ASSERT(usb_dev != nullptr, "usb_dev != nullptr");
    TEST_ASSERT(input_find_device_by_name("usb-keyboard") == usb_dev, "Znaleziono usb-keyboard w rejestrze");

    // Brak jakiejkolwiek klawiatury VirtIO. Klawiatury PS/2 i USB pracuja rownolegle i niezaleznie:
    // PS/2 pisze 'H' (scancode 0x23)
    ps2_klawiatura_przetworz_scancode(0x23);
    ps2_klawiatura_przetworz_scancode(0xA3);

    // USB pisze 'I' (usage 0x0C)
    uint8_t rep_i[8] = { 0, 0, 0x0C, 0, 0, 0, 0, 0 };
    uint8_t rep_0[8] = { 0 };
    usb_hid_przetworz_raport(rep_i);
    usb_hid_przetworz_raport(rep_0);

    // W buforze znakow pojawia sie "HI"
    TEST_ASSERT(pobierz_znak_klawiatury() == 'h', "Znak 'h' z PS/2");
    TEST_ASSERT(pobierz_znak_klawiatury() == 'i', "Znak 'i' z USB");

    input_reset_all();
}

// 28. Generyczne wejscie: Zachowanie istniejacego ukladu klawiatury Bursztyn (w tym polskie diakrytyki)
void test_bursztyn_keyboard_layout_preserved() {
    input_reset_all();
    klawiatura_zresetuj_stan();
    ps2_klawiatura_zresetuj_stan();
    usb_klawiatura_zresetuj_stan();
    g_mock_input_capture.reset();

    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_input_device_msix(&dev, 0), "Probe VirtIO kbd");
    VirtioInputDriver& drv = s_shared_input_drv;
    std::memset(&drv, 0, sizeof(drv));
    TEST_ASSERT(host_input_prepare_and_activate(&drv, &dev), "Init VirtIO kbd");

    uint8_t rep_empty[8] = { 0 };

    // 1. Zwykly uklad US: mala litera, duza ze Shiftem, CapsLock
    // Litera 'b'
    ps2_klawiatura_przetworz_scancode(0x30); // 'b'
    ps2_klawiatura_przetworz_scancode(0xB0);
    TEST_ASSERT(pobierz_znak_klawiatury() == 'b', "Mala litera 'b'");

    // Shift + 'b' -> 'B' (przez USB)
    uint8_t rep_shift_b[8] = { 0x02, 0, 0x05, 0, 0, 0, 0, 0 }; // LeftShift + B
    usb_hid_przetworz_raport(rep_shift_b);
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT(pobierz_znak_klawiatury() == 'B', "Duza litera 'B' przez USB");

    // CapsLock + 'b' -> 'B' (przez VirtIO)
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 1);
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 0);
    test_virtio_send_key(&drv, INPUT_KEY_B, 1);
    test_virtio_send_key(&drv, INPUT_KEY_B, 0);
    TEST_ASSERT(pobierz_znak_klawiatury() == 'B', "Caps Lock 'B' przez VirtIO");

    // CapsLock + Shift + 'b' -> 'b' (inwersja CapsLocka)
    test_virtio_send_key(&drv, INPUT_KEY_LEFTSHIFT, 1);
    test_virtio_send_key(&drv, INPUT_KEY_B, 1);
    test_virtio_send_key(&drv, INPUT_KEY_B, 0);
    test_virtio_send_key(&drv, INPUT_KEY_LEFTSHIFT, 0);
    TEST_ASSERT(pobierz_znak_klawiatury() == 'b', "Caps Lock + Shift -> 'b' przez VirtIO");

    // Wylacz CapsLock
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 1);
    test_virtio_send_key(&drv, INPUT_KEY_CAPSLOCK, 0);

    // 2. Polskie znaki diakrytyczne przez AltGr (Right Alt):
    // Sprawdzamy, czy generowane sa identyczne dwubajtowe ciagi UTF-8
    // a) AltGr + A -> '?' (0xC4 0x85) przez PS/2
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x38); // AltGr down
    ps2_klawiatura_przetworz_scancode(0x1E); // 'a' down
    ps2_klawiatura_przetworz_scancode(0x9E);
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0xB8); // AltGr up
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC4, "PS/2 '?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0x85, "PS/2 '?' b2");

    // b) AltGr + E -> '?' (0xC4 0x99) przez USB HID
    uint8_t rep_alt_e[8] = { 0x40, 0, 0x08, 0, 0, 0, 0, 0 }; // RightAlt + E
    usb_hid_przetworz_raport(rep_alt_e);
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC4, "USB '?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0x99, "USB '?' b2");

    // c) AltGr + S -> '?' (0xC5 0x9B) przez VirtIO
    test_virtio_send_key(&drv, INPUT_KEY_RIGHTALT, 1);
    test_virtio_send_key(&drv, INPUT_KEY_S, 1);
    test_virtio_send_key(&drv, INPUT_KEY_S, 0);
    test_virtio_send_key(&drv, INPUT_KEY_RIGHTALT, 0);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC5, "VirtIO '?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0x9B, "VirtIO '?' b2");

    // d) AltGr + Shift + S -> '?' (0xC5 0x9A) przez USB HID
    uint8_t rep_alt_shift_s[8] = { 0x42, 0, 0x16, 0, 0, 0, 0, 0 }; // RightAlt + LeftShift + S
    usb_hid_przetworz_raport(rep_alt_shift_s);
    usb_hid_przetworz_raport(rep_empty);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC5, "USB '?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0x9A, "USB '?' b2");

    // e) Pozostale polskie znaki: c(?), l(?), n(?), o(?), x(?), z(?) przez PS/2
    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0x38); // AltGr down

    // c -> ? (0xC4 0x87)
    ps2_klawiatura_przetworz_scancode(0x2E); ps2_klawiatura_przetworz_scancode(0xAE);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC4, "'?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0x87, "'?' b2");

    // l -> ? (0xC5 0x82)
    ps2_klawiatura_przetworz_scancode(0x26); ps2_klawiatura_przetworz_scancode(0xA6);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC5, "'?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0x82, "'?' b2");

    // n -> ? (0xC5 0x84)
    ps2_klawiatura_przetworz_scancode(0x31); ps2_klawiatura_przetworz_scancode(0xB1);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC5, "'?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0x84, "'?' b2");

    // o -> ? (0xC3 0xB3)
    ps2_klawiatura_przetworz_scancode(0x18); ps2_klawiatura_przetworz_scancode(0x98);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC3, "'?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xB3, "'?' b2");

    // x -> ? (0xC5 0xBA)
    ps2_klawiatura_przetworz_scancode(0x2D); ps2_klawiatura_przetworz_scancode(0xAD);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC5, "'?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xBA, "'?' b2");

    // z -> ? (0xC5 0xBC)
    ps2_klawiatura_przetworz_scancode(0x2C); ps2_klawiatura_przetworz_scancode(0xAC);
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xC5, "'?' b1");
    TEST_ASSERT(static_cast<uint8_t>(pobierz_znak_klawiatury()) == 0xBC, "'?' b2");

    ps2_klawiatura_przetworz_scancode(0xE0);
    ps2_klawiatura_przetworz_scancode(0xB8); // AltGr up

    TEST_ASSERT(virtio_input_reset(&drv), "Reset VirtIO kbd");
    input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}

// 29. Independent PS/2 and VirtIO sources; preferred metadata never gates input.
void test_input_pointer_ownership_and_no_duplicate_motion() {
    input_reset_all();
    g_mock_input_capture.reset();

    // 1. Zarejestruj mysz PS/2 (niski priorytet = 10)
    InputDevice ps2_dev{};
    std::strcpy(ps2_dev.name, "ps2-mouse");
    ps2_dev.kinds = INPUT_DEVICE_KIND_MOUSE;
    ps2_dev.bus_type = INPUT_BUS_PS2;
    ps2_dev.device_id = 0;
    TEST_ASSERT(input_register_device(&ps2_dev), "Rejestracja myszy PS/2");
    TEST_ASSERT(input_get_pointer_owner() == &ps2_dev, "Mysz PS/2 staje sie aktywnym wlascicielem kursora");

    // Ruch z PS/2 przy braku VirtIO jest akceptowany
    InputEvent ev_ps2_rel[2] = {
        { INPUT_EV_REL, INPUT_REL_X, 10 },
        { INPUT_EV_SYN, INPUT_SYN_REPORT, 0 }
    };
    input_dispatch_events(&ps2_dev, ev_ps2_rel, 2);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 1, "Ruch PS/2 zaakceptowany, rel_calls == 1");
    TEST_ASSERT(g_mock_input_capture.rel_dx == 10, "dx == 10 z PS/2");

    // 2. Podlacz mysz VirtIO (wysoki priorytet = 30)
    VirtioPciDevice dev_mouse{};
    VirtioInputDriver drv_mouse{};
    InputRegistryScope input_scope_drv_mouse; // unregisters while the driver object is alive
    TEST_ASSERT(setup_mock_input_device_msix(&dev_mouse, 1), "Probe VirtIO mouse");
    TEST_ASSERT(host_input_prepare_and_activate(&drv_mouse, &dev_mouse), "Init VirtIO mouse");
    TEST_ASSERT(input_get_pointer_owner() == &drv_mouse.input_dev, "VirtIO przejmuje wlasnosc kursora od PS/2");

    // 3. PS/2 remains active while VirtIO is the preferred diagnostic source.
    g_mock_input_capture.reset();
    input_dispatch_events(&ps2_dev, ev_ps2_rel, 2);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 1, "PS/2 remains usable beside VirtIO");

    // 4. Ruch z VirtIO jest normalnie akceptowany
    mock_inject_input_event(&drv_mouse, INPUT_EV_REL, INPUT_REL_X, 25);
    mock_inject_input_event(&drv_mouse, INPUT_EV_SYN, INPUT_SYN_REPORT, 0);
    virtio_input_poll_events(&drv_mouse, 10);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 2, "Both independent pointer reports delivered exactly once");
    TEST_ASSERT(g_mock_input_capture.rel_dx == 35, "PS/2 10 plus VirtIO 25 arrive exactly once");

    // 5. Odlaczenie/reset urzadzenia VirtIO -> fallback z powrotem do PS/2
    TEST_ASSERT(virtio_input_reset(&drv_mouse), "Reset VirtIO mouse");
    TEST_ASSERT(input_get_pointer_owner() == &ps2_dev, "Po wyrejestrowaniu VirtIO wlasnosc wraca do PS/2");

    // 6. PS/2 znowu steruje kursorem
    g_mock_input_capture.reset();
    input_dispatch_events(&ps2_dev, ev_ps2_rel, 2);
    TEST_ASSERT(g_mock_input_capture.rel_calls == 1, "Po fallbacku PS/2 znowu aktywne, rel_calls == 1");
    TEST_ASSERT(g_mock_input_capture.rel_dx == 10, "dx == 10 z PS/2");

    TEST_ASSERT(input_unregister_device(&ps2_dev), "Wyrejestrowanie myszy PS/2");
    TEST_ASSERT(input_get_pointer_owner() == nullptr, "Brak wlasciciela kursora");
    input_reset_all();
    TEST_ASSERT(g_host_dma_allocs.empty(), "Zero wyciekow DMA");
}


// ============================================================================
// PUNKT WEJSCIA TESTOW
// ============================================================================

#include "test_virtio_input_faults.inc"
#include "test_virtio_input_init.inc"
#include "test_virtio_input_activation.inc"
#include "test_virtio_input_lifecycle.inc"
#include "../sterowniki/virtio/virtio_input_qemu_diag.h"
#include "test_virtio_input_qemu_checkpoints.inc"
#include "test_virtio_gpu_recovery.inc"

int main() {
    input_set_pointer_ready(true);
    std::printf("========================================================\n");
    std::printf(" BURSZTYN OS - TESTY TRANSPORTU VIRTIO 1.X MODERN PCI\n");
    std::printf("========================================================\n");

    RUN_TEST(test_valid_modern_device);
    RUN_TEST(test_missing_mandatory_caps);
    RUN_TEST(test_invalid_bar_index);
    RUN_TEST(test_capability_exceeds_bar_bounds);
    RUN_TEST(test_offset_length_overflow);
    RUN_TEST(test_malformed_looped_capability_chain);
    RUN_TEST(test_invalid_cap_ptr_range);
    RUN_TEST(test_truncated_cap_header);
    RUN_TEST(test_unsupported_version_1);
    RUN_TEST(test_device_clears_features_ok);
    RUN_TEST(test_feature_negotiation);
    RUN_TEST(test_status_and_reset);
    RUN_TEST(test_notify_address_calculation_and_overflow);
    RUN_TEST(test_queue_config_registers);
    RUN_TEST(test_device_specific_config_access);
    RUN_TEST(test_isr_read);
    RUN_TEST(test_virtio_pci_notify_multiplier_validation);
    RUN_TEST(test_virtio_pci_consistent_config_read);
    RUN_TEST(test_virtqueue_create_and_validation);
    RUN_TEST(test_virtqueue_invalid_sizes);
    RUN_TEST(test_virtqueue_failed_allocation_rollback);
    RUN_TEST(test_virtqueue_submit_single_descriptor);
    RUN_TEST(test_virtqueue_submit_chain);
    RUN_TEST(test_virtqueue_submit_invalid_buffer_order);
    RUN_TEST(test_virtqueue_descriptor_exhaustion);
    RUN_TEST(test_virtqueue_completion_and_recycling);
    RUN_TEST(test_virtqueue_invalid_completions);
    RUN_TEST(test_virtqueue_ring_index_wraparound);
    RUN_TEST(test_virtqueue_kick_and_suppression);
    RUN_TEST(test_virtqueue_destroy_cleanup);
    RUN_TEST(test_virtio_rng_init_and_cleanup);
    RUN_TEST(test_virtio_rng_read_success);
    RUN_TEST(test_virtio_rng_read_repeated);
    RUN_TEST(test_virtio_rng_read_partial);
    RUN_TEST(test_virtio_rng_read_invalid_len);
    RUN_TEST(test_virtio_rng_read_timeout);
    RUN_TEST(test_virtio_rng_device_failure_driver_ok);
    RUN_TEST(test_virtio_rng_descriptor_id_mismatch);
    RUN_TEST(test_virtio_rng_cleanup_after_init_failure);
    RUN_TEST(test_virtio_rng_device_failure_runtime);
    RUN_TEST(test_virtio_rng_queue_exhaustion);
    RUN_TEST(test_virtio_rng_reset_and_reinit);
    RUN_TEST(test_virtio_rng_stress_host);
    RUN_TEST(test_virtio_rng_oversized_used_len_rejected);
    RUN_TEST(test_virtio_rng_secure_zero_helper);
    RUN_TEST(test_virtio_rng_timeout_deadline_expiry);
    RUN_TEST(test_virtio_rng_reset_timeout);
    RUN_TEST(test_virtio_rng_dma_not_freed_before_confirmed_reset);
    RUN_TEST(test_virtio_rng_concurrent_requests);
    RUN_TEST(test_virtio_rng_counter_saturation);

    // Testy VirtIO-BLK
    RUN_TEST(test_virtio_blk_init_and_cleanup);
    RUN_TEST(test_virtio_blk_custom_block_size);
    RUN_TEST(test_virtio_blk_read_write_flush);
    RUN_TEST(test_virtio_blk_chunking_large_transfer);
    RUN_TEST(test_virtio_blk_read_only_device);
    RUN_TEST(test_virtio_blk_unsupported_flush);
    RUN_TEST(test_virtio_blk_io_error_handling);
    RUN_TEST(test_virtio_blk_unsupported_status);
    RUN_TEST(test_virtio_blk_bounds_and_overflow);
    RUN_TEST(test_virtio_blk_timeout_and_reset);
    RUN_TEST(test_virtio_blk_invalid_used_len_rejected);
    RUN_TEST(test_virtio_blk_invalid_desc_id_rejected);
    RUN_TEST(test_virtio_blk_dma_quarantine_on_stuck_reset);
    RUN_TEST(test_virtio_blk_repeated_requests_and_recycling);
    RUN_TEST(test_virtio_blk_revalidate);
    RUN_TEST(test_virtio_blk_stress_host);
    RUN_TEST(test_virtio_blk_stress_destructive_no_marker);
    RUN_TEST(test_virtio_blk_stress_destructive_bad_crc);
    RUN_TEST(test_virtio_blk_stress_destructive_boot_state_collision);
    RUN_TEST(test_virtio_blk_stress_destructive_valid_marker);

    // Nowe testy Generic PCI MSI/MSI-X i VirtIO
    RUN_TEST(test_msi_parsing_and_enable);
    RUN_TEST(test_msix_parsing_and_bounds);
    RUN_TEST(test_malformed_capabilities);
    RUN_TEST(test_msix_invalid_bir);
    RUN_TEST(test_msix_table_bounds_overflow);
    RUN_TEST(test_idt_dynamic_vector_allocator);
    RUN_TEST(test_msix_entry_programming_and_masking);
    RUN_TEST(test_virtio_queue_msix_vector_assignment);
    RUN_TEST(test_virtio_msix_delivery_and_teardown_safety);
    RUN_TEST(test_virtio_msix_repeated_cycles_no_leak);

    // Testy VirtIO-NET
    RUN_TEST(test_virtio_net_init_and_cleanup);
    RUN_TEST(test_virtio_net_negotiation_and_mac);
    RUN_TEST(test_virtio_net_link_state);
    RUN_TEST(test_virtio_net_transmit_basic_and_stats);
    RUN_TEST(test_virtio_net_transmit_mtu_and_length_validation);
    RUN_TEST(test_virtio_net_transmit_queue_exhaustion);
    RUN_TEST(test_virtio_net_rx_receive_and_replenish);
    RUN_TEST(test_virtio_net_rx_malformed_packets);
    RUN_TEST(test_virtio_net_msix_operation);
    RUN_TEST(test_virtio_net_reset_quarantine_on_stuck_device);
    RUN_TEST(test_virtio_net_stress_and_wraparound);

    // Testy VirtIO-GPU (2D)
    RUN_TEST(test_virtio_gpu_probe_reset_ownership);
    RUN_TEST(test_virtio_gpu_first_publication);
    RUN_TEST(test_virtio_gpu_init_and_cleanup);
    RUN_TEST(test_virtio_gpu_get_display_info);
    RUN_TEST(test_virtio_gpu_resource_create_2d);
    RUN_TEST(test_virtio_gpu_attach_backing);
    RUN_TEST(test_virtio_gpu_set_scanout);
    RUN_TEST(test_virtio_gpu_transfer_to_host_2d);
    RUN_TEST(test_virtio_gpu_resource_flush);
    RUN_TEST(test_virtio_gpu_batched_dirty_publication);
    RUN_TEST(test_virtio_gpu_malformed_response);
    RUN_TEST(test_virtio_gpu_command_timeout);
    RUN_TEST(test_virtio_gpu_wait_irq_protocol);
    RUN_TEST(test_virtio_gpu_completion_at_deadline);
    RUN_TEST(test_gpu_recovery_normal_completion_dormant);
    RUN_TEST(test_gpu_recovery_completion_just_before_timeout);
    RUN_TEST(test_gpu_recovery_sync_command_completes_after_soft_deadline);
    RUN_TEST(test_gpu_recovery_async_late_completion_then_normal_operation);
    RUN_TEST(test_gpu_recovery_service_reaps_without_new_frame);
    RUN_TEST(test_gpu_recovery_completion_after_hard_deadline_fails_closed);
    RUN_TEST(test_gpu_recovery_completion_visible_at_deadline_check);
    RUN_TEST(test_gpu_recovery_no_completion_sync_and_async);
    RUN_TEST(test_gpu_recovery_invalid_late_responses);
    RUN_TEST(test_gpu_recovery_unexpected_descriptor_index);
    RUN_TEST(test_gpu_recovery_spurious_used_entry_while_idle);
    RUN_TEST(test_gpu_recovery_used_ring_wraparound);
    RUN_TEST(test_gpu_recovery_repeated_timeouts_and_lifetime);
    RUN_TEST(test_gpu_recovery_without_clock_fails_closed);
    RUN_TEST(test_gpu_driver_scope_releases_device_on_early_return);
    RUN_TEST(test_virtio_gpu_arithmetic_overflow);
    RUN_TEST(test_virtio_gpu_invalid_dimensions);
    RUN_TEST(test_virtio_gpu_queue_corruption);
    RUN_TEST(test_virtio_gpu_reset_failure_quarantine);
    RUN_TEST(test_virtio_gpu_resource_teardown_ordering);
    RUN_TEST(test_virtio_gpu_repeated_init_reset);
    RUN_TEST(test_virtio_gpu_msix_operation);
    RUN_TEST(test_virtio_gpu_format_constants);
    RUN_TEST(test_virtio_gpu_noncontiguous_backing);
    RUN_TEST(test_virtio_gpu_used_len_validation);
    RUN_TEST(test_virtio_gpu_error_responses);
    RUN_TEST(test_virtio_gpu_boot_mode_preservation);
    RUN_TEST(test_virtio_gpu_runtime_modeset_and_rollback);
    RUN_TEST(test_virtio_gpu_confirmation_lifecycle_and_injected_failures);

    // Testy VirtIO-INPUT i InputDevice
    RUN_TEST(test_input_synchronous_driver_ok_activation);
    RUN_TEST(test_input_prepare_activate_single_matrix);
    RUN_TEST(test_input_prepared_pair_idempotent_late_pass);
    RUN_TEST(test_input_late_failure_preserves_other_device_and_ps2);
    RUN_TEST(test_input_prepared_cleanup_and_pre_activation_failure);
    RUN_TEST(test_input_late_pair_synchronous_report_and_pending_irq);
    RUN_TEST(test_input_installer_pmm_dma_addresses);
    RUN_TEST(test_input_pmm_module_page_boundaries);
    RUN_TEST(test_input_pmm_exhaustion_and_lock_release);
#if BURSZTYNDIAGVIRTIOINPUTINIT
    RUN_TEST(test_input_activation_emergency_exception_record);
#endif
    RUN_TEST(test_input_init_reset_failure_quarantines_live_dma);
    RUN_TEST(test_input_init_irqs_at_each_readiness_boundary);
    RUN_TEST(test_input_init_registry_failure_cleanup);
    RUN_TEST(test_input_init_independent_functions_early_irq);
    RUN_TEST(test_input_init_queue_capacity_matrix);
    RUN_TEST(test_input_init_capability_lengths_and_wide_access);
    RUN_TEST(test_input_init_absinfo_failure_matrix);
    RUN_TEST(test_input_init_feature_vector_reset_failures);
    RUN_TEST(test_virtio_input_config_discovery);
    RUN_TEST(test_virtio_input_fault_short_completion_feedback);
    RUN_TEST(test_virtio_input_fault_duplicate_completion);
    RUN_TEST(test_virtio_input_dual_device_and_motion_stress);
    RUN_TEST(test_virtio_input_keyboard_classification);
    RUN_TEST(test_virtio_input_mouse_rel_classification);
    RUN_TEST(test_virtio_input_tablet_abs_classification);
    RUN_TEST(test_virtio_input_ev_key_press_release);
    RUN_TEST(test_virtio_input_btn_events);
    RUN_TEST(test_virtio_input_rel_xy);
    RUN_TEST(test_virtio_input_rel_wheel);
    RUN_TEST(test_virtio_input_abs_scaling);
    RUN_TEST(test_virtio_input_qemu_tablet_hover_without_contact);
    RUN_TEST(test_virtio_input_asymmetric_abs_mapping);
    RUN_TEST(test_virtio_input_syn_report);
    RUN_TEST(test_virtio_input_malformed_config_size);
    RUN_TEST(test_virtio_input_malformed_used_len);
    RUN_TEST(test_virtio_input_invalid_desc_id);
    RUN_TEST(test_virtio_input_unsupported_event_type);
    RUN_TEST(test_virtio_input_queue_recycling);
    RUN_TEST(test_virtio_input_queue_wraparound);
    RUN_TEST(test_virtio_input_msix_setup_and_delivery);
    RUN_TEST(test_virtio_input_polling_fallback);
    RUN_TEST(test_virtio_input_repeated_init_reset);
    RUN_TEST(test_virtio_input_dma_and_vector_cleanup);

    // Testy Generycznej Architektury Wejscia (PS/2, USB HID, VirtIO)
    RUN_TEST(test_input_same_key_ps2_usb_virtio);
    RUN_TEST(test_input_press_release);
    RUN_TEST(test_input_modifiers);
    RUN_TEST(test_input_extended_keys);
    RUN_TEST(test_input_repeat);
    RUN_TEST(test_input_no_duplicate_key_events);
    RUN_TEST(test_input_usb_and_ps2_fallback);
    RUN_TEST(test_bursztyn_keyboard_layout_preserved);
    RUN_TEST(test_input_pointer_ownership_and_no_duplicate_motion);
    RUN_TEST(test_input_qemu_relative_checkpoints_coexistence);

    std::printf("========================================================\n");
    std::printf("PODSUMOWANIE: PASS=%d, FAIL=%d\n", g_pass_count, g_fail_count);
    std::printf("========================================================\n");

    return (g_fail_count == 0) ? 0 : 1;
}
