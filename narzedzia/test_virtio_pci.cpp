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

#include "virtio_pci.h"
#include "virtqueue.h"

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
    int      bump_gen_on_read_count = 0;
    bool     always_bump_gen = false;

    void reset() {
        std::memset(pci_config, 0, sizeof(pci_config));
        for (int i = 0; i < 6; ++i) {
            bars[i] = MockBar();
            bars[i].phys_addr = 0xFEB00000ULL + i * 0x10000ULL;
        }
        selected_feature_bank = 0;
        device_features = VIRTIO_F_VERSION_1 | 0x1U;
        driver_features = 0;
        device_status = 0;
        accept_features = true;
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

static uint32_t g_virtqueue_mb_count = 0;
static uint32_t g_virtqueue_rmb_count = 0;
static uint32_t g_virtqueue_wmb_count = 0;

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
    if (g_mock_dma_alloc_fail) {
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
                if ((val & VIRTIO_STATUS_FEATURES_OK) != 0 && !g_mock.accept_features) {
                    val &= ~VIRTIO_STATUS_FEATURES_OK;
                }
                g_mock.device_status = val;
            }
            g_mock.bars[b].mem[off] = val;
            return;
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
            }

            // Obsluga rejestrow kolejki w common_cfg (BAR 0)
            if (b == 0) {
                if (off == 0x16) { // queue_select
                    g_mock.selected_queue = val;
                    std::memcpy(&g_mock.bars[b].mem[off], &val, 2);
                    if (val < 8) {
                        uint16_t qs = g_mock.queues[val].size;
                        uint16_t qe = g_mock.queues[val].enable;
                        uint16_t qno = g_mock.queues[val].notify_off;
                        std::memcpy(&g_mock.bars[b].mem[0x18], &qs, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1C], &qe, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1E], &qno, 2);
                    } else {
                        uint16_t zero = 0;
                        std::memcpy(&g_mock.bars[b].mem[0x18], &zero, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1C], &zero, 2);
                        std::memcpy(&g_mock.bars[b].mem[0x1E], &zero, 2);
                    }
                    return;
                } else if (off == 0x18) { // queue_size
                    if (g_mock.selected_queue < 8) {
                        g_mock.queues[g_mock.selected_queue].size = val;
                    }
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

uint8_t build_cap_chain(const std::vector<CapBuilderEntry>& entries) {
    uint8_t current_ptr = 0x40;
    g_mock.pci_config[0x34] = current_ptr;

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const uint8_t next_ptr = (i + 1 < entries.size()) ? static_cast<uint8_t>(current_ptr + 24) : 0;
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
    common->queue_select = 0;
    common->queue_size = g_mock.queues[0].size;
    common->queue_enable = g_mock.queues[0].enable;
    common->queue_notify_off = g_mock.queues[0].notify_off;
    b.mem[isr_off] = g_mock.isr_val;
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

    std::printf("========================================================\n");
    std::printf("PODSUMOWANIE: PASS=%d, FAIL=%d\n", g_pass_count, g_fail_count);
    std::printf("========================================================\n");

    return (g_fail_count == 0) ? 0 : 1;
}
