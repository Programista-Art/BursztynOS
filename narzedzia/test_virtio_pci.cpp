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

    uint16_t last_notified_queue = 0xFFFFU;
    uint32_t notify_count = 0;

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
        last_notified_queue = 0xFFFFU;
        notify_count = 0;
    }
};

MockDeviceState g_mock;
std::vector<std::string> g_klog_lines;

} // namespace

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

    std::printf("========================================================\n");
    std::printf("PODSUMOWANIE: PASS=%d, FAIL=%d\n", g_pass_count, g_fail_count);
    std::printf("========================================================\n");

    return (g_fail_count == 0) ? 0 : 1;
}
