/*
 * Bursztyn OS - Test jednostkowy warstwy transportowej VirtIO 1.x Modern PCI
 *
 * Plik: narzedzia/test_virtio_pci.cpp
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
#include <cassert>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

#include "idt.h"
#include "apic.h"
#include "virtio_pci.h"
#include "virtqueue.h"
#include "virtio_rng.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "virtio_gpu.h"

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
    if (!g_mock_idt[vector].allocated) return false;
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
    uint32_t gpu_display_w = 1280;
    uint32_t gpu_display_h = 800;
    uint32_t gpu_processed_count = 0;
    uint32_t gpu_last_nr_entries = 0;
    std::vector<uint32_t> gpu_commands_seen;

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
        gpu_display_w = 1280;
        gpu_display_h = 800;
        gpu_processed_count = 0;
        gpu_last_nr_entries = 0;
        gpu_commands_seen.clear();
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

struct HostFbAllocEntry {
    void* vaddr;
    size_t page_count;
    std::vector<uint64_t> phys_frames;
};
static std::vector<HostFbAllocEntry> g_host_fb_allocs;

static uint32_t g_virtqueue_mb_count = 0;
static uint32_t g_virtqueue_rmb_count = 0;
static uint32_t g_virtqueue_wmb_count = 0;

static void* host_phys_to_virt(uint64_t paddr) {
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

void host_virtqueue_mb_hook() {
    g_virtqueue_mb_count++;
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
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return false;
    }
    std::memset(ptr, 0, size);

    size_t num_pages = (size + 4095) / 4096;
    uint64_t phys = g_next_dma_phys;
    g_next_dma_phys += num_pages * 4096;

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

uint32_t host_pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
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

uint64_t host_czas_monotoniczny_ns(void) {
    if (g_host_use_mock_time) {
        return g_host_mock_time_ns;
    }
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()
    );
}

// Hooki MMIO
uint8_t host_mmio_read8(volatile const void* addr) {
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
    const uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    for (int b = 0; b < 6; ++b) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_mock.bars[b].mem.data());
        if (a >= base && a + 2 <= base + g_mock.bars[b].mem.size()) {
            const size_t off = a - base;

            // Sprawdzenie powiadomienia (notify)
            if (b == g_mock.notify_bar && off >= g_mock.notify_bar_off && off + 2 <= g_mock.notify_bar_off + g_mock.notify_bar_len) {
                g_mock.last_notified_queue = val;
                g_mock.notify_count++;

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
                        while (g_mock.gpu_processed_count != vq->avail->idx) {
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
                                        resp_hdr->type = (g_mock.gpu_override_resp_type > 0) ? g_mock.gpu_override_resp_type : VIRTIO_GPU_RESP_OK_NODATA;
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
// 15. Dostęp do device-specific config (odczyt/zapis, ochrona granic)
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
    TEST_ASSERT(n == 10, "Zwrócono dokladnie 10 bajtow");
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
// 46. VirtIO-RNG: Wygaśnięcie deadline w czasie monotonicznym
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
    TEST_ASSERT(virtio_blk_stress_test(drv), "virtio_blk_stress_test zakonczony sukcesem");

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
                "Wektor w dozwolonym zakresie dynamicznym (48..254)");
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

    // 4. Test wyczerpania puli (wszystkie 207 wektorow 48..254)
    std::vector<int> vecs;
    for (int i = 0; i < 207; ++i) {
        int v = idt_alokuj_wektor_irq();
        TEST_ASSERT(v >= 0, "Alokacja wektora z puli");
        vecs.push_back(v);
    }
    TEST_ASSERT(idt_zaalokowane_wektory_liczba() == 207, "Zaalokowano wszystkie 207 wektorow");
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
        TEST_ASSERT(std::memcmp(write_buf, read_buf, sizeof(write_buf)) == 0, "Spójność danych dysku");

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

static bool setup_mock_gpu_device(VirtioPciDevice* dev,
                                  uint32_t disp_w = 1280,
                                  uint32_t disp_h = 800,
                                  uint64_t extra_features = 0) {
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

// ----------------------------------------------------------------------------
// 88. VirtIO-GPU: Inicjalizacja i zwalnianie zasobow
// ----------------------------------------------------------------------------
void test_virtio_gpu_init_and_cleanup() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe urzadzenia GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Inicjalizacja VirtIO-GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "Pobranie instancji drv != nullptr");
    TEST_ASSERT(drv->initialized, "drv->initialized == true");
    TEST_ASSERT(drv->display_width == 1280, "display_width == 1280");
    TEST_ASSERT(drv->display_height == 800, "display_height == 800");
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

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 92. VirtIO-GPU: Konfiguracja powiazania ze scanoutem (set scanout)
// ----------------------------------------------------------------------------
void test_virtio_gpu_set_scanout() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
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
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    const uint64_t flushes_before = drv->flush_count;
    TEST_ASSERT(virtio_gpu_cmd_resource_flush(drv, 1, 10, 20, 100, 50), "resource_flush dirty rect");
    TEST_ASSERT(drv->flush_count == flushes_before + 1, "flush_count zwiekszony o 1");

    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 95. VirtIO-GPU: Obsluga nieprawidlowej/blednej odpowiedzi urzadzenia
// ----------------------------------------------------------------------------
void test_virtio_gpu_malformed_response() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
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
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    g_mock.gpu_skip_reply = true;
    TEST_ASSERT(!virtio_gpu_cmd_resource_flush(drv, 1, 0, 0, 100, 100), "Timeout zwraca false");
    TEST_ASSERT(drv->has_error, "drv->has_error == true po timeoucie");

    g_mock.gpu_skip_reply = false;
    virtio_gpu_reset(drv);
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 97. VirtIO-GPU: Ochrona przed przepelnieniem arytmetycznym (overflow safety)
// ----------------------------------------------------------------------------
void test_virtio_gpu_arithmetic_overflow() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
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
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
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
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    const size_t allocs_before = g_host_dma_allocs.size();
    g_mock.ignore_reset = true;

    TEST_ASSERT(!virtio_gpu_reset(drv), "Reset zwraca false gdy sprzet ignoruje reset");
    TEST_ASSERT(drv->has_error, "drv->has_error == true");
    TEST_ASSERT(g_host_dma_allocs.size() == allocs_before, "Pamiec DMA poddana kwarantannie (niezwolniona)");

    g_mock.ignore_reset = false;
    g_host_dma_allocs.clear();
    virtio_gpu_reset_all();
}

// ----------------------------------------------------------------------------
// 101. VirtIO-GPU: Prawidlowa kolejnosc niszczenia zasobow (teardown ordering)
// ----------------------------------------------------------------------------
void test_virtio_gpu_resource_teardown_ordering() {
    virtio_gpu_reset_all();
    VirtioPciDevice dev{};
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
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
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "drv != nullptr");

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
    TEST_ASSERT(setup_mock_gpu_device(&dev), "Probe GPU");
    TEST_ASSERT(virtio_gpu_init(&dev), "Init GPU");

    VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
    TEST_ASSERT(drv != nullptr, "drv != nullptr");

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

// ============================================================================
// PUNKT WEJSCIA TESTOW
// ============================================================================

int main() {
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
    RUN_TEST(test_virtio_gpu_init_and_cleanup);
    RUN_TEST(test_virtio_gpu_get_display_info);
    RUN_TEST(test_virtio_gpu_resource_create_2d);
    RUN_TEST(test_virtio_gpu_attach_backing);
    RUN_TEST(test_virtio_gpu_set_scanout);
    RUN_TEST(test_virtio_gpu_transfer_to_host_2d);
    RUN_TEST(test_virtio_gpu_resource_flush);
    RUN_TEST(test_virtio_gpu_malformed_response);
    RUN_TEST(test_virtio_gpu_command_timeout);
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

    std::printf("========================================================\n");
    std::printf("PODSUMOWANIE: PASS=%d, FAIL=%d\n", g_pass_count, g_fail_count);
    std::printf("========================================================\n");

    return (g_fail_count == 0) ? 0 : 1;
}
