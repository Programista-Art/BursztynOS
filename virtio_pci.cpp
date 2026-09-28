/*
 * Bursztyn OS - Wspolna warstwa transportowa VirtIO 1.x Modern PCI
 *
 * Plik: virtio_pci.cpp
 */

#include "virtio_pci.h"

#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <cstdio>
#include <cstring>

extern uint32_t host_pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
extern void host_pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
extern volatile uint8_t* host_bar_map(uint8_t bar, uint64_t phys_addr, uint64_t size);
extern void host_klog(const char* msg);

extern uint8_t  host_mmio_read8(volatile const void* addr);
extern uint16_t host_mmio_read16(volatile const void* addr);
extern uint32_t host_mmio_read32(volatile const void* addr);
extern void host_mmio_write8(volatile void* addr, uint8_t val);
extern void host_mmio_write16(volatile void* addr, uint16_t val);
extern void host_mmio_write32(volatile void* addr, uint32_t val);

#define pci_odczytaj_dword_impl host_pci_read
#define pci_zapisz_dword_impl host_pci_write
#define bar_map_impl host_bar_map
#define klog_impl host_klog
#else
#include "pci.h"
#include "pamiec.h"
#include "grafika.h"

#define pci_odczytaj_dword_impl pci_odczytaj_dword
#define pci_zapisz_dword_impl pci_zapisz_dword
#define klog_impl klog_serial
#endif

namespace {

static inline uint8_t virtio_read8(volatile const void* addr) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return host_mmio_read8(addr);
#else
    return *reinterpret_cast<volatile const uint8_t*>(addr);
#endif
}

static inline uint16_t virtio_read16(volatile const void* addr) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return host_mmio_read16(addr);
#else
    return *reinterpret_cast<volatile const uint16_t*>(addr);
#endif
}

static inline uint32_t virtio_read32(volatile const void* addr) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return host_mmio_read32(addr);
#else
    return *reinterpret_cast<volatile const uint32_t*>(addr);
#endif
}

static inline void virtio_write8(volatile void* addr, uint8_t val) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    host_mmio_write8(addr, val);
#else
    *reinterpret_cast<volatile uint8_t*>(addr) = val;
    asm volatile("" ::: "memory");
#endif
}

static inline void virtio_write16(volatile void* addr, uint16_t val) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    host_mmio_write16(addr, val);
#else
    *reinterpret_cast<volatile uint16_t*>(addr) = val;
    asm volatile("" ::: "memory");
#endif
}

static inline void virtio_write32(volatile void* addr, uint32_t val) {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    host_mmio_write32(addr, val);
#else
    *reinterpret_cast<volatile uint32_t*>(addr) = val;
    asm volatile("" ::: "memory");
#endif
}

constexpr uint32_t MAX_VIRTIO_DEVICES = 16U;
VirtioPciDevice g_virtio_devices[MAX_VIRTIO_DEVICES]{};
uint32_t g_virtio_device_count = 0;

/* =========================================================================
 * BEZPIECZNE FORMATOWANIE LOGOW DO KLOG_SERIAL (BEZ MALLOC/PRINTF)
 * ========================================================================= */

void log_append_str(char* buf, size_t max_len, size_t& pos, const char* str) {
    if (!buf || !str) return;
    while (*str && pos + 1 < max_len) {
        buf[pos++] = *str++;
    }
    buf[pos] = '\0';
}

void log_append_u64_dec(char* buf, size_t max_len, size_t& pos, uint64_t val) {
    if (!buf) return;
    char tmp[24];
    size_t k = 0;
    if (val == 0) {
        tmp[k++] = '0';
    } else {
        while (val > 0 && k < sizeof(tmp)) {
            tmp[k++] = static_cast<char>('0' + (val % 10));
            val /= 10;
        }
    }
    while (k > 0 && pos + 1 < max_len) {
        buf[pos++] = tmp[--k];
    }
    buf[pos] = '\0';
}

void log_append_u64_hex(char* buf, size_t max_len, size_t& pos, uint64_t val, int min_digits) {
    if (!buf) return;
    const char hex_chars[] = "0123456789ABCDEF";
    char tmp[20];
    for (int i = 0; i < 16; ++i) {
        tmp[15 - i] = hex_chars[(val >> (i * 4)) & 0xFU];
    }
    int start = 0;
    while (start < 16 - min_digits && tmp[start] == '0') {
        start++;
    }
    for (int i = start; i < 16 && pos + 1 < max_len; ++i) {
        buf[pos++] = tmp[i];
    }
    buf[pos] = '\0';
}

#if (defined(BURSZTYN_DIAG_PCI_TRACE) && BURSZTYN_DIAG_PCI_TRACE) || \
    (defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT)
void pci_trace_bdf(uint8_t bus, uint8_t slot, uint8_t func, const char* phase) {
    char msg[128]{};
    size_t pos = 0;
    log_append_str(msg, sizeof(msg), pos, "[PCI TRACE] ");
    log_append_u64_hex(msg, sizeof(msg), pos, bus, 2);
    log_append_str(msg, sizeof(msg), pos, ":");
    log_append_u64_hex(msg, sizeof(msg), pos, slot, 2);
    log_append_str(msg, sizeof(msg), pos, ".");
    log_append_u64_dec(msg, sizeof(msg), pos, func);
    log_append_str(msg, sizeof(msg), pos, " ");
    log_append_str(msg, sizeof(msg), pos, phase);
    klog_impl(msg);
}

bool pci_trace_interrupts_enabled() {
#ifdef BURSZTYN_VIRTIO_HOST_TEST
    return false;
#else
    uint64_t rflags = 0;
    asm volatile("pushfq; popq %0" : "=r"(rflags) :: "memory");
    return (rflags & (UINT64_C(1) << 9)) != 0;
#endif
}
#else
inline void pci_trace_bdf(uint8_t, uint8_t, uint8_t, const char*) {}
inline bool pci_trace_interrupts_enabled() { return false; }
#endif

#ifndef BURSZTYN_DIAG_PCI_ENUM
#define BURSZTYN_DIAG_PCI_ENUM 0
#endif

#if BURSZTYN_DIAG_PCI_ENUM
void pci_diag_scan_enter(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t dev_id, uint8_t cap_ptr) {
    char msg[160]{};
    size_t pos = 0;
    log_append_str(msg, sizeof(msg), pos, "[PCI-SCAN] enter BDF ");
    log_append_u64_hex(msg, sizeof(msg), pos, bus, 2);
    log_append_str(msg, sizeof(msg), pos, ":");
    log_append_u64_hex(msg, sizeof(msg), pos, slot, 2);
    log_append_str(msg, sizeof(msg), pos, ".");
    log_append_u64_dec(msg, sizeof(msg), pos, func);
    log_append_str(msg, sizeof(msg), pos, " vendor=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, vendor, 4);
    log_append_str(msg, sizeof(msg), pos, " device=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, dev_id, 4);
    log_append_str(msg, sizeof(msg), pos, " capability pointer=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, cap_ptr, 2);
    klog_impl(msg);
}

void pci_diag_cap(uint8_t offset, uint8_t id, uint8_t next, uint8_t cfg_type, uint8_t bar) {
    char msg[160]{};
    size_t pos = 0;
    log_append_str(msg, sizeof(msg), pos, "[PCI-SCAN] cap offset=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, offset, 2);
    log_append_str(msg, sizeof(msg), pos, " id=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, id, 2);
    log_append_str(msg, sizeof(msg), pos, " next=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, next, 2);
    if (id == 0x09) {
        log_append_str(msg, sizeof(msg), pos, " VirtIO type=");
        log_append_u64_dec(msg, sizeof(msg), pos, cfg_type);
        log_append_str(msg, sizeof(msg), pos, " BAR=");
        log_append_u64_dec(msg, sizeof(msg), pos, bar);
    }
    klog_impl(msg);
}

void pci_diag_bar(uint8_t bar_idx, uint32_t raw_low, uint64_t bar_size, volatile const void* vaddr) {
    char msg[160]{};
    size_t pos = 0;
    log_append_str(msg, sizeof(msg), pos, "[PCI-SCAN] BAR ");
    log_append_u64_dec(msg, sizeof(msg), pos, bar_idx);
    log_append_str(msg, sizeof(msg), pos, " raw=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, raw_low, 8);
    log_append_str(msg, sizeof(msg), pos, " size=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, bar_size, 8);
    log_append_str(msg, sizeof(msg), pos, " mapped MMIO address=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, reinterpret_cast<uintptr_t>(vaddr), 16);
    klog_impl(msg);
}

inline void pci_diag_step(const char* step) {
    klog_impl(step);
}

void pci_diag_features(const char* prefix, uint64_t features) {
    char msg[128]{};
    size_t pos = 0;
    log_append_str(msg, sizeof(msg), pos, prefix);
    log_append_u64_hex(msg, sizeof(msg), pos, features, 8);
    klog_impl(msg);
}
#endif

/* =========================================================================
 * POMOCNICZY DOSTEP DO PCI CONFIG SPACE
 * ========================================================================= */

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_odczytaj_dword_impl(bus, slot, func, offset & ~3U);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    const uint32_t dword = pci_read32(bus, slot, func, offset);
    const uint32_t shift = (offset & 2U) * 8U;
    return static_cast<uint16_t>((dword >> shift) & 0xFFFFU);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    const uint32_t dword = pci_read32(bus, slot, func, offset);
    const uint32_t shift = (offset & 3U) * 8U;
    return static_cast<uint8_t>((dword >> shift) & 0xFFU);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    pci_zapisz_dword_impl(bus, slot, func, offset & ~3U, val);
}

/* =========================================================================
 * WALIDACJA I MAPOWANIE MMIO BAR
 * ========================================================================= */

bool virtio_pci_map_bar(uint8_t bus, uint8_t slot, uint8_t func,
                        uint8_t bar_idx, VirtioPciBarInfo* bar_out) {
    if (bar_idx >= 6 || !bar_out) return false;
    if (bar_out->is_mapped) return true; // Juz zmapowany

    const uint8_t bar_offset = static_cast<uint8_t>(0x10 + bar_idx * 4);
    const uint32_t orig_low = pci_read32(bus, slot, func, bar_offset);
    if (orig_low == 0 || orig_low == 0xFFFFFFFFU) return false;

    // Modern VirtIO wymaga pamieci MMIO (bit 0 == 0)
    if ((orig_low & 1U) != 0) return false;

    const uint8_t mem_type = static_cast<uint8_t>((orig_low >> 1) & 3U);
    const bool is_64 = (mem_type == 2U);
    if (is_64 && bar_idx >= 5) return false; // BAR5 nie moze byc 64-bitowy

    uint32_t orig_high = 0;
    if (is_64) {
        orig_high = pci_read32(bus, slot, func, bar_offset + 4);
    }

    // Bezpieczne sprawdzanie rozmiaru BAR: wylaczamy dekodowanie pamieci
    const uint32_t cmd_status = pci_read32(bus, slot, func, 0x04);
    const uint16_t cmd = static_cast<uint16_t>(cmd_status & 0xFFFFU);
    pci_write32(bus, slot, func, 0x04, (cmd_status & 0xFFFF0000U) | (cmd & ~0x0002U));

    pci_write32(bus, slot, func, bar_offset, 0xFFFFFFFFU);
    const uint32_t mask_low = pci_read32(bus, slot, func, bar_offset);
    pci_write32(bus, slot, func, bar_offset, orig_low);

    uint64_t mask64 = 0;
    uint64_t phys_addr = 0;
    if (is_64) {
        pci_write32(bus, slot, func, bar_offset + 4, 0xFFFFFFFFU);
        const uint32_t mask_high = pci_read32(bus, slot, func, bar_offset + 4);
        pci_write32(bus, slot, func, bar_offset + 4, orig_high);
        mask64 = (static_cast<uint64_t>(mask_high) << 32) | (mask_low & ~0xFULL);
        phys_addr = (static_cast<uint64_t>(orig_high) << 32) | (orig_low & ~0xFULL);
    } else {
        mask64 = static_cast<uint64_t>(mask_low & ~0xFULL) | 0xFFFFFFFF00000000ULL;
        phys_addr = static_cast<uint64_t>(orig_low & ~0xFULL);
    }

    // Przywrocenie stanu rejestru Command
    pci_write32(bus, slot, func, 0x04, cmd_status);

    if (mask64 == 0 || phys_addr == 0) return false;
    const uint64_t bar_size = ~mask64 + 1U;
    if (bar_size == 0) return false;

#ifdef BURSZTYN_VIRTIO_HOST_TEST
    volatile uint8_t* vaddr = bar_map_impl(bar_idx, phys_addr, bar_size);
    if (!vaddr) return false;
#else
    if (bar_size > UINT64_MAX - phys_addr) return false;
    const uint64_t page_start = phys_addr & ~0xFFFULL;
    const uint64_t page_end = (phys_addr + bar_size - 1U) & ~0xFFFULL;
    const uint32_t flagi = VMM_FLAGA_OBECNA | VMM_FLAGA_ZAPIS | VMM_FLAGA_PWT | VMM_FLAGA_PCD;
    for (uint64_t p = page_start; p <= page_end; p += 4096) {
        ZmapujStrone(reinterpret_cast<void*>(p), reinterpret_cast<void*>(p), flagi);
        asm volatile("invlpg (%0)" :: "r"(p) : "memory");
        if (p > UINT64_MAX - 4096) break;
    }
    volatile uint8_t* vaddr = reinterpret_cast<volatile uint8_t*>(phys_addr);
#endif

    bar_out->phys_addr = phys_addr;
    bar_out->size = bar_size;
    bar_out->vaddr = vaddr;
    bar_out->is_mapped = true;
    bar_out->is_64bit = is_64;
#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_bar(bar_idx, orig_low, bar_size, vaddr);
#endif
    return true;
}

} // namespace

/* =========================================================================
 * 5. IMPLEMENTACJA PUBLICZNEGO API TRANSPORTU VIRTIO PCI
 * ========================================================================= */

const char* virtio_pci_device_name(uint16_t virtio_id) {
    switch (virtio_id) {
        case VIRTIO_ID_NET:     return "VirtIO-NET (Karta sieciowa)";
        case VIRTIO_ID_BLOCK:   return "VirtIO-BLK (Urzadzenie blokowe)";
        case VIRTIO_ID_CONSOLE: return "VirtIO-Console";
        case VIRTIO_ID_RNG:     return "VirtIO-RNG (Generator losowy)";
        case VIRTIO_ID_BALLOON: return "VirtIO-Balloon";
        case VIRTIO_ID_SCSI:    return "VirtIO-SCSI";
        case VIRTIO_ID_GPU:     return "VirtIO-GPU (Karta graficzna)";
        case VIRTIO_ID_INPUT:   return "VirtIO-Input (Urzadzenie wejsciowe)";
        case VIRTIO_ID_SOCKET:  return "VirtIO-VSOCK";
        default:                return "VirtIO-Nieznane";
    }
}

void virtio_pci_diag_trace_device(const VirtioPciDevice* dev, const char* phase) {
    if (!dev) return;
    pci_trace_bdf(dev->bus, dev->slot, dev->func, phase);
}

void virtio_pci_diag_trace_type(uint16_t virtio_id, const char* phase) {
    for (uint32_t i = 0; i < g_virtio_device_count; ++i) {
        const VirtioPciDevice& dev = g_virtio_devices[i];
        if (dev.virtio_id == virtio_id) {
            pci_trace_bdf(dev.bus, dev.slot, dev.func, phase);
        }
    }
}

void virtio_pci_diag_trace_queue(const VirtioPciDevice* dev, uint16_t queue_index, const char* phase) {
#if (defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT)
    if (!dev) return;
    char msg[160]{};
    size_t pos = 0;
    log_append_str(msg, sizeof(msg), pos, "[VIRTIO TRACE] BDF ");
    log_append_u64_hex(msg, sizeof(msg), pos, dev->bus, 2);
    log_append_str(msg, sizeof(msg), pos, ":");
    log_append_u64_hex(msg, sizeof(msg), pos, dev->slot, 2);
    log_append_str(msg, sizeof(msg), pos, ".");
    log_append_u64_dec(msg, sizeof(msg), pos, dev->func);
    log_append_str(msg, sizeof(msg), pos, " queue=");
    log_append_u64_dec(msg, sizeof(msg), pos, queue_index);
    log_append_str(msg, sizeof(msg), pos, " ");
    log_append_str(msg, sizeof(msg), pos, phase ? phase : "?");
    klog_impl(msg);
#else
    (void)dev;
    (void)queue_index;
    (void)phase;
#endif
}

uint8_t virtio_pci_get_status(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    return virtio_read8(&dev->common_cfg->device_status);
}

void virtio_pci_set_status(VirtioPciDevice* dev, uint8_t status) {
    if (!dev || !dev->common_cfg) return;
#if defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT
    if (status == VIRTIO_STATUS_RESET) {
        // Also cover the GPU driver's second reset, which calls set_status directly.
        pci_trace_bdf(dev->bus, dev->slot, dev->func, "8-bit device_status reset store begin");
    }
#endif
    virtio_write8(&dev->common_cfg->device_status, status);
#if defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT
    if (status == VIRTIO_STATUS_RESET) {
        pci_trace_bdf(dev->bus, dev->slot, dev->func, "8-bit device_status reset store returned");
    }
    if ((status & VIRTIO_STATUS_DRIVER_OK) != 0) {
        pci_trace_bdf(dev->bus, dev->slot, dev->func, "DRIVEROK");
    }
#endif
}

bool virtio_pci_reset(VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return false;
#if defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT
    constexpr size_t status_offset = offsetof(VirtioPciCommonCfgRegs, device_status);
    if (dev->common_cfg_bar >= 6 || dev->common_cfg_len <= status_offset) {
        klog_impl("[VIRTIO RESET TRACE] rejected: status outside common capability");
        return false;
    }
    const auto& bar = dev->bars[dev->common_cfg_bar];
    const uintptr_t common = reinterpret_cast<uintptr_t>(dev->common_cfg);
    const uintptr_t base = reinterpret_cast<uintptr_t>(bar.vaddr);
    if (!bar.is_mapped || common < base || common - base > bar.size ||
        dev->common_cfg_len > bar.size - (common - base)) {
        klog_impl("[VIRTIO RESET TRACE] rejected: common capability outside BAR");
        return false;
    }
    const uint64_t cap_offset = common - base;
    char msg[512]{};
    size_t pos = 0;
    log_append_str(msg, sizeof(msg), pos, "[VIRTIO RESET TRACE] bdf=");
    log_append_u64_hex(msg, sizeof(msg), pos, dev->bus, 2);
    log_append_str(msg, sizeof(msg), pos, ":");
    log_append_u64_hex(msg, sizeof(msg), pos, dev->slot, 2);
    log_append_str(msg, sizeof(msg), pos, ".");
    log_append_u64_dec(msg, sizeof(msg), pos, dev->func);
    log_append_str(msg, sizeof(msg), pos, " transport=modern bar=");
    log_append_u64_dec(msg, sizeof(msg), pos, dev->common_cfg_bar);
    log_append_str(msg, sizeof(msg), pos, " barbase=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, bar.phys_addr, 16);
    log_append_str(msg, sizeof(msg), pos, " capoffset=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, cap_offset, 8);
    log_append_str(msg, sizeof(msg), pos, " caplength=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, dev->common_cfg_len, 8);
    log_append_str(msg, sizeof(msg), pos, " commoncfg=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, common, 16);
    log_append_str(msg, sizeof(msg), pos, " devicestatusaddr=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, common + status_offset, 16);
    log_append_str(msg, sizeof(msg), pos, " physical=0x");
    log_append_u64_hex(msg, sizeof(msg), pos, bar.phys_addr + cap_offset + status_offset, 16);
    log_append_str(msg, sizeof(msg), pos, " oldstatus read begin");
    klog_impl(msg);
    // Mark the read separately so a stalled read cannot be mistaken for a write.
    const uint8_t old_status = virtio_pci_get_status(dev);
    pos = 0;
    log_append_str(msg, sizeof(msg), pos, "[VIRTIO RESET TRACE] oldstatus=");
    log_append_u64_dec(msg, sizeof(msg), pos, old_status);
    log_append_str(msg, sizeof(msg), pos, " accesswidth=8 offset=0x14 status write begin");
    klog_impl(msg);
#endif
#if (defined(BURSZTYN_DIAG_PCI_TRACE) && BURSZTYN_DIAG_PCI_TRACE) || \
    (defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT)
    pci_trace_bdf(dev->bus, dev->slot, dev->func,
                  pci_trace_interrupts_enabled() ? "reset begin IF=1" : "reset begin IF=0");
#else
    pci_trace_bdf(dev->bus, dev->slot, dev->func, "reset begin");
#endif
    virtio_pci_set_status(dev, VIRTIO_STATUS_RESET);
#if defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT
    klog_impl("[VIRTIO RESET TRACE] status write returned");
#endif
    for (uint32_t i = 0; i < 10000; ++i) {
        if (virtio_pci_get_status(dev) == 0) {
            pci_trace_bdf(dev->bus, dev->slot, dev->func, "reset end");
            return true;
        }
        asm volatile("pause" ::: "memory");
    }
    const bool reset = virtio_pci_get_status(dev) == 0;
    pci_trace_bdf(dev->bus, dev->slot, dev->func, reset ? "reset end" : "reset timeout");
    return reset;
}

uint64_t virtio_pci_read_device_features(VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    virtio_write32(&dev->common_cfg->device_feature_select, 0);
    const uint32_t f0 = virtio_read32(&dev->common_cfg->device_feature);

    virtio_write32(&dev->common_cfg->device_feature_select, 1);
    const uint32_t f1 = virtio_read32(&dev->common_cfg->device_feature);

    return (static_cast<uint64_t>(f1) << 32) | f0;
}

void virtio_pci_write_driver_features(VirtioPciDevice* dev, uint64_t features) {
    if (!dev || !dev->common_cfg) return;
    virtio_write32(&dev->common_cfg->driver_feature_select, 0);
    virtio_write32(&dev->common_cfg->driver_feature, static_cast<uint32_t>(features & 0xFFFFFFFFU));

    virtio_write32(&dev->common_cfg->driver_feature_select, 1);
    virtio_write32(&dev->common_cfg->driver_feature, static_cast<uint32_t>(features >> 32));
}

bool virtio_pci_negotiate_features(VirtioPciDevice* dev, uint64_t requested_features) {
    if (!dev || !dev->common_cfg) return false;

    pci_trace_bdf(dev->bus, dev->slot, dev->func, "feature read begin");

#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_step("[PCI-SCAN] feature read begin");
#endif
    dev->device_features = virtio_pci_read_device_features(dev);
    pci_trace_bdf(dev->bus, dev->slot, dev->func, "feature read end");
#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_features("[PCI-SCAN] feature read end: device_features=0x", dev->device_features);
#endif

    // Standard VirtIO 1.x: urzadzenie MUSI udostepniac VIRTIO_F_VERSION_1
    if ((dev->device_features & VIRTIO_F_VERSION_1) == 0) {
        klog_impl("[VIRTIO-PCI] initialization rejected: device does not offer VIRTIO_F_VERSION_1");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }

#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_step("[PCI-SCAN] feature negotiation begin");
#endif
    pci_trace_bdf(dev->bus, dev->slot, dev->func, "feature selection");
    dev->driver_features = (dev->device_features & requested_features) | VIRTIO_F_VERSION_1;
    virtio_pci_write_driver_features(dev, dev->driver_features);

    if ((dev->driver_features & VIRTIO_F_NOTIFICATION_DATA) != 0) {
        if (!virtio_pci_validate_notify_multiplier(dev->notify_multiplier, true)) {
            klog_impl("[VIRTIO-PCI] initialization rejected: notify multiplier incompatible with VIRTIO_F_NOTIFICATION_DATA");
            virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
            return false;
        }
    }

    // Ustawienie FEATURES_OK
    uint8_t status = virtio_pci_get_status(dev);
    status |= VIRTIO_STATUS_FEATURES_OK;
    pci_trace_bdf(dev->bus, dev->slot, dev->func, "FEATURESOK set");
    virtio_pci_set_status(dev, status);

    // Weryfikacja: urzadzenie musi zachowac bit FEATURES_OK
    const uint8_t status_after = virtio_pci_get_status(dev);
    pci_trace_bdf(dev->bus, dev->slot, dev->func, "FEATURESOK verification");
    if ((status_after & VIRTIO_STATUS_FEATURES_OK) == 0) {
        klog_impl("[VIRTIO-PCI] initialization rejected: device cleared FEATURES_OK");
        virtio_pci_set_status(dev, VIRTIO_STATUS_FAILED);
        return false;
    }
#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_features("[PCI-SCAN] feature negotiation end: driver_features=0x", dev->driver_features);
#endif

    return true;
}

void virtio_pci_select_queue(VirtioPciDevice* dev, uint16_t queue_index) {
    if (!dev || !dev->common_cfg) return;
    virtio_write16(&dev->common_cfg->queue_select, queue_index);
}

uint16_t virtio_pci_get_queue_size(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    return virtio_read16(&dev->common_cfg->queue_size);
}

void virtio_pci_set_queue_size(VirtioPciDevice* dev, uint16_t size) {
    if (!dev || !dev->common_cfg) return;
    virtio_write16(&dev->common_cfg->queue_size, size);
}

uint16_t virtio_pci_get_queue_msix_vector(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0xFFFFU;
    return virtio_read16(&dev->common_cfg->queue_msix_vector);
}

void virtio_pci_set_queue_msix_vector(VirtioPciDevice* dev, uint16_t vector) {
    if (!dev || !dev->common_cfg) return;
    virtio_write16(&dev->common_cfg->queue_msix_vector, vector);
}

uint16_t virtio_pci_get_queue_enable(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    return virtio_read16(&dev->common_cfg->queue_enable);
}

void virtio_pci_set_queue_enable(VirtioPciDevice* dev, uint16_t enable) {
    if (!dev || !dev->common_cfg) return;
    virtio_write16(&dev->common_cfg->queue_enable, enable);
}

uint16_t virtio_pci_get_queue_notify_off(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    return virtio_read16(&dev->common_cfg->queue_notify_off);
}

void virtio_pci_set_queue_desc(VirtioPciDevice* dev, uint64_t desc_phys) {
    if (!dev || !dev->common_cfg) return;
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<volatile uint8_t*>(dev->common_cfg) + 0x20);
    virtio_write32(p, static_cast<uint32_t>(desc_phys & 0xFFFFFFFFU));
    virtio_write32(p + 1, static_cast<uint32_t>(desc_phys >> 32));
}

void virtio_pci_set_queue_driver(VirtioPciDevice* dev, uint64_t driver_phys) {
    if (!dev || !dev->common_cfg) return;
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<volatile uint8_t*>(dev->common_cfg) + 0x28);
    virtio_write32(p, static_cast<uint32_t>(driver_phys & 0xFFFFFFFFU));
    virtio_write32(p + 1, static_cast<uint32_t>(driver_phys >> 32));
}

void virtio_pci_set_queue_device(VirtioPciDevice* dev, uint64_t device_phys) {
    if (!dev || !dev->common_cfg) return;
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(
        reinterpret_cast<volatile uint8_t*>(dev->common_cfg) + 0x30);
    virtio_write32(p, static_cast<uint32_t>(device_phys & 0xFFFFFFFFU));
    virtio_write32(p + 1, static_cast<uint32_t>(device_phys >> 32));
}

uint64_t virtio_pci_get_queue_desc(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    volatile const uint32_t* p = reinterpret_cast<volatile const uint32_t*>(
        reinterpret_cast<volatile const uint8_t*>(dev->common_cfg) + 0x20);
    const uint64_t low = virtio_read32(p);
    const uint64_t high = virtio_read32(p + 1);
    return low | (high << 32);
}

uint64_t virtio_pci_get_queue_driver(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    volatile const uint32_t* p = reinterpret_cast<volatile const uint32_t*>(
        reinterpret_cast<volatile const uint8_t*>(dev->common_cfg) + 0x28);
    const uint64_t low = virtio_read32(p);
    const uint64_t high = virtio_read32(p + 1);
    return low | (high << 32);
}

uint64_t virtio_pci_get_queue_device(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    volatile const uint32_t* p = reinterpret_cast<volatile const uint32_t*>(
        reinterpret_cast<volatile const uint8_t*>(dev->common_cfg) + 0x30);
    const uint64_t low = virtio_read32(p);
    const uint64_t high = virtio_read32(p + 1);
    return low | (high << 32);
}

volatile uint16_t* virtio_pci_notify_address(const VirtioPciDevice* dev, uint16_t queue_notify_off) {
    if (!dev || !dev->notify_base) return nullptr;
    const uint64_t rel = static_cast<uint64_t>(queue_notify_off) * dev->notify_multiplier;
    if (rel > UINT64_MAX - sizeof(uint16_t)) return nullptr;
    if (rel + sizeof(uint16_t) > dev->notify_cap_len) return nullptr;
    return reinterpret_cast<volatile uint16_t*>(dev->notify_base + rel);
}

void virtio_pci_notify_queue(VirtioPciDevice* dev, uint16_t queue_index, uint16_t queue_notify_off) {
    volatile uint16_t* addr = virtio_pci_notify_address(dev, queue_notify_off);
    if (addr) {
        virtio_write16(addr, queue_index);
    }
}

uint8_t virtio_pci_read_isr(VirtioPciDevice* dev) {
    if (!dev || !dev->isr) return 0;
    return virtio_read8(dev->isr);
}

uint8_t virtio_pci_get_config_generation(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return 0;
    return virtio_read8(&dev->common_cfg->config_generation);
}

uint8_t virtio_pci_read_device_config8(const VirtioPciDevice* dev, uint32_t offset) {
    if (!dev || !dev->device_cfg || offset >= dev->device_cfg_len) return 0;
    return virtio_read8(dev->device_cfg + offset);
}

uint16_t virtio_pci_read_device_config16(const VirtioPciDevice* dev, uint32_t offset) {
    if (!dev || !dev->device_cfg || offset > dev->device_cfg_len - sizeof(uint16_t)) return 0;
    return virtio_read16(dev->device_cfg + offset);
}

uint32_t virtio_pci_read_device_config32(const VirtioPciDevice* dev, uint32_t offset) {
    if (!dev || !dev->device_cfg || offset > dev->device_cfg_len - sizeof(uint32_t)) return 0;
    return virtio_read32(dev->device_cfg + offset);
}

uint64_t virtio_pci_read_device_config64(const VirtioPciDevice* dev, uint32_t offset) {
    if (!dev || !dev->device_cfg || offset > dev->device_cfg_len - sizeof(uint64_t)) return 0;
    volatile const uint32_t* p = reinterpret_cast<volatile const uint32_t*>(dev->device_cfg + offset);
    const uint32_t lo = virtio_read32(p);
    const uint32_t hi = virtio_read32(p + 1);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

void virtio_pci_write_device_config8(VirtioPciDevice* dev, uint32_t offset, uint8_t val) {
    if (!dev || !dev->device_cfg || offset >= dev->device_cfg_len) return;
    virtio_write8(dev->device_cfg + offset, val);
}

void virtio_pci_write_device_config16(VirtioPciDevice* dev, uint32_t offset, uint16_t val) {
    if (!dev || !dev->device_cfg || offset > dev->device_cfg_len - sizeof(uint16_t)) return;
    virtio_write16(dev->device_cfg + offset, val);
}

void virtio_pci_write_device_config32(VirtioPciDevice* dev, uint32_t offset, uint32_t val) {
    if (!dev || !dev->device_cfg || offset > dev->device_cfg_len - sizeof(uint32_t)) return;
    virtio_write32(dev->device_cfg + offset, val);
}

bool virtio_pci_validate_notify_multiplier(uint32_t multiplier, bool has_notification_data) {
    if (multiplier == 0) return true;
    if (multiplier > 0x100000) return false;
    if ((multiplier & (multiplier - 1U)) != 0) return false;
    if (has_notification_data) {
        return (multiplier % 4U == 0);
    } else {
        return (multiplier % 2U == 0);
    }
}

bool virtio_pci_read_device_config_consistent(const VirtioPciDevice* dev,
                                              uint32_t offset,
                                              void* dst,
                                              uint32_t length,
                                              uint32_t max_retries) {
    if (!dev || !dev->device_cfg || !dst || length == 0) return false;
    if (offset > dev->device_cfg_len || length > dev->device_cfg_len - offset) return false;
    if (max_retries == 0) max_retries = 16;

    uint8_t* out = static_cast<uint8_t*>(dst);
    for (uint32_t r = 0; r < max_retries; ++r) {
        const uint8_t gen_before = virtio_pci_get_config_generation(dev);
        asm volatile("" ::: "memory");
        for (uint32_t i = 0; i < length; ++i) {
            out[i] = virtio_pci_read_device_config8(dev, offset + i);
        }
        asm volatile("" ::: "memory");
        const uint8_t gen_after = virtio_pci_get_config_generation(dev);
        if (gen_before == gen_after) {
            return true;
        }
    }
    return false;
}

void virtio_pci_cleanup(VirtioPciDevice* dev) {
    if (!dev) return;
    if (dev->msix_enabled && dev->has_msix) {
        if (dev->msix_table) {
            for (uint16_t i = 0; i < dev->msix_cap.table_size; ++i) {
                pci_msix_mask_entry(dev->msix_table, dev->msix_cap.table_size, i);
            }
        }
        pci_msix_disable(dev->bus, dev->slot, dev->func, &dev->msix_cap);
        dev->msix_enabled = false;
    }
    if (dev->msi_enabled && dev->has_msi) {
        pci_msi_disable(dev->bus, dev->slot, dev->func, &dev->msi_cap);
        dev->msi_enabled = false;
    }
    if (dev->common_cfg) {
        virtio_pci_reset(dev);
    }
    *dev = {};
}

/* =========================================================================
 * 6. PROBOWANIE URZADZENIA VIRTIO MODERN PCI
 * ========================================================================= */

bool virtio_pci_probe(uint8_t bus, uint8_t slot, uint8_t func,
                      VirtioPciDevice* dev, uint64_t requested_features) {
    pci_trace_bdf(bus, slot, func, "virtio probe entered");
    if (!dev) return false;
    *dev = {};
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;

    // 1. Sprawdzenie Vendor ID (musi byc 0x1AF4)
    dev->pci_vendor_id = pci_read16(bus, slot, func, 0x00);
    if (dev->pci_vendor_id != VIRTIO_PCI_VENDOR_ID) {
        return false;
    }

    dev->pci_device_id = pci_read16(bus, slot, func, 0x02);
    dev->revision = pci_read8(bus, slot, func, 0x08);
    dev->subsystem_vendor_id = pci_read16(bus, slot, func, 0x2C);
    dev->subsystem_device_id = pci_read16(bus, slot, func, 0x2E);

    // Ustalenie typu urzadzenia VirtIO (VirtIO Device Type ID):
    // Dla Modern PCI (0x1040..0x107F): Device ID = 0x1040 + VirtIO Device Type ID
    // Dla Transitional PCI (0x1000..0x103F): VirtIO Device Type ID z Subsystem Device ID
    if (dev->pci_device_id >= 0x1040U && dev->pci_device_id <= 0x107FU) {
        dev->virtio_id = static_cast<uint16_t>(dev->pci_device_id - 0x1040U);
    } else if (dev->pci_device_id >= 0x1000U && dev->pci_device_id <= 0x103FU) {
        dev->virtio_id = dev->subsystem_device_id;
    } else {
        return false;
    }

    // 2. Weryfikacja obecnosci listy PCI Capabilities (Status bit 4)
    const uint16_t pci_status = pci_read16(bus, slot, func, 0x06);
    if ((pci_status & (1U << 4)) == 0) {
        return false;
    }

    const uint8_t raw_cap_ptr = pci_read8(bus, slot, func, 0x34);
    if ((raw_cap_ptr & 0x03U) != 0) {
        klog_impl("[VIRTIO-PCI] initialization rejected: unaligned capability pointer");
        return false;
    }
    if (raw_cap_ptr != 0 && (raw_cap_ptr < 0x40 || raw_cap_ptr > 0xFC)) {
        klog_impl("[VIRTIO-PCI] initialization rejected: malformed capability pointer offset");
        return false;
    }

#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_scan_enter(bus, slot, func, dev->pci_vendor_id, dev->pci_device_id, raw_cap_ptr);
#endif

    uint8_t cap_ptr = raw_cap_ptr;
    bool visited[256]{};
    uint32_t hops = 0;
    constexpr uint32_t MAX_CAP_HOPS = 48;

    VirtioPciCapHeader cap_common{};
    bool has_common = false;
    VirtioPciNotifyCapHeader cap_notify{};
    bool has_notify = false;
    VirtioPciCapHeader cap_isr{};
    bool has_isr = false;
    VirtioPciCapHeader cap_device{};
    bool has_device = false;

    // 3. Bezpieczne parsowanie lancucha capabilities
    pci_trace_bdf(bus, slot, func, "capability walk begin");
    while (cap_ptr != 0 && hops < MAX_CAP_HOPS) {
        if (cap_ptr < 0x40 || cap_ptr > 0xFC) {
            klog_impl("[VIRTIO-PCI] initialization rejected: malformed capability pointer offset");
            return false;
        }
        if (visited[cap_ptr]) {
            klog_impl("[VIRTIO-PCI] initialization rejected: capability chain loop detected");
            return false;
        }
        visited[cap_ptr] = true;
        hops++;

        const uint8_t cap_vndr = pci_read8(bus, slot, func, cap_ptr);
        const uint8_t raw_cap_next = pci_read8(bus, slot, func, cap_ptr + 1);
        if ((raw_cap_next & 0x03U) != 0) {
            klog_impl("[VIRTIO-PCI] initialization rejected: unaligned next capability pointer");
            return false;
        }
        if (raw_cap_next != 0 && (raw_cap_next < 0x40 || raw_cap_next > 0xFC)) {
            klog_impl("[VIRTIO-PCI] initialization rejected: next capability pointer out of bounds");
            return false;
        }
        const uint8_t cap_next = raw_cap_next;
        const uint8_t cap_len  = pci_read8(bus, slot, func, cap_ptr + 2);

#if BURSZTYN_DIAG_PCI_ENUM
        const uint8_t diag_cfg = (cap_vndr == 0x09) ? pci_read8(bus, slot, func, cap_ptr + 3) : 0;
        const uint8_t diag_bar = (cap_vndr == 0x09) ? pci_read8(bus, slot, func, cap_ptr + 4) : 0;
        pci_diag_cap(cap_ptr, cap_vndr, raw_cap_next, diag_cfg, diag_bar);
#endif

        if (cap_vndr == 0x09) { // PCI_CAP_ID_VNDR
            if (cap_len < sizeof(VirtioPciCapHeader)) {
                klog_impl("[VIRTIO-PCI] initialization rejected: truncated vendor capability");
                return false;
            }

            const uint8_t cfg_type = pci_read8(bus, slot, func, cap_ptr + 3);
            const uint8_t bar = pci_read8(bus, slot, func, cap_ptr + 4);
            const uint8_t id = pci_read8(bus, slot, func, cap_ptr + 5);
            const uint32_t offset = pci_read32(bus, slot, func, cap_ptr + 8);
            const uint32_t length = pci_read32(bus, slot, func, cap_ptr + 12);

            if (bar >= 6) {
                klog_impl("[VIRTIO-PCI] initialization rejected: invalid capability BAR index >= 6");
                return false;
            }

            switch (cfg_type) {
                case VIRTIO_PCI_CAP_COMMON_CFG:
                    if (!has_common) {
#if BURSZTYN_DIAG_PCI_ENUM
                        pci_diag_step("[PCI-SCAN] common cfg discovery");
#endif
                        if (length < sizeof(VirtioPciCommonCfgRegs)) {
                            klog_impl("[VIRTIO-PCI] initialization rejected: common config length too small");
                            return false;
                        }
                        cap_common.cap_vndr = cap_vndr;
                        cap_common.cap_next = cap_next;
                        cap_common.cap_len  = cap_len;
                        cap_common.cfg_type = cfg_type;
                        cap_common.bar      = bar;
                        cap_common.id       = id;
                        cap_common.offset   = offset;
                        cap_common.length   = length;
                        has_common = true;
                    }
                    break;

                case VIRTIO_PCI_CAP_NOTIFY_CFG:
                    if (!has_notify) {
#if BURSZTYN_DIAG_PCI_ENUM
                        pci_diag_step("[PCI-SCAN] notify cfg discovery");
#endif
                        if (cap_len < sizeof(VirtioPciNotifyCapHeader)) {
                            klog_impl("[VIRTIO-PCI] initialization rejected: notify capability truncated");
                            return false;
                        }
                        if (length < 2) {
                            klog_impl("[VIRTIO-PCI] initialization rejected: notify config length < 2");
                            return false;
                        }
                        const uint32_t multiplier = pci_read32(bus, slot, func, cap_ptr + 16);
                        if (!virtio_pci_validate_notify_multiplier(multiplier, false)) {
                            klog_impl("[VIRTIO-PCI] initialization rejected: invalid notify multiplier");
                            return false;
                        }
                        cap_notify.cap.cap_vndr = cap_vndr;
                        cap_notify.cap.cap_next = cap_next;
                        cap_notify.cap.cap_len  = cap_len;
                        cap_notify.cap.cfg_type = cfg_type;
                        cap_notify.cap.bar      = bar;
                        cap_notify.cap.id       = id;
                        cap_notify.cap.offset   = offset;
                        cap_notify.cap.length   = length;
                        cap_notify.notify_off_multiplier = multiplier;
                        has_notify = true;
                    }
                    break;

                case VIRTIO_PCI_CAP_ISR_CFG:
                    if (!has_isr) {
#if BURSZTYN_DIAG_PCI_ENUM
                        pci_diag_step("[PCI-SCAN] ISR cfg discovery");
#endif
                        if (length < 1) {
                            klog_impl("[VIRTIO-PCI] initialization rejected: ISR config length < 1");
                            return false;
                        }
                        cap_isr.cap_vndr = cap_vndr;
                        cap_isr.cap_next = cap_next;
                        cap_isr.cap_len  = cap_len;
                        cap_isr.cfg_type = cfg_type;
                        cap_isr.bar      = bar;
                        cap_isr.id       = id;
                        cap_isr.offset   = offset;
                        cap_isr.length   = length;
                        has_isr = true;
                    }
                    break;

                case VIRTIO_PCI_CAP_DEVICE_CFG:
                    if (!has_device) {
#if BURSZTYN_DIAG_PCI_ENUM
                        pci_diag_step("[PCI-SCAN] device cfg discovery");
#endif
                        cap_device.cap_vndr = cap_vndr;
                        cap_device.cap_next = cap_next;
                        cap_device.cap_len  = cap_len;
                        cap_device.cfg_type = cfg_type;
                        cap_device.bar      = bar;
                        cap_device.id       = id;
                        cap_device.offset   = offset;
                        cap_device.length   = length;
                        has_device = true;
                    }
                    break;

                default:
                    break;
            }
        }

        cap_ptr = cap_next;
    }

    if (hops >= MAX_CAP_HOPS) {
        klog_impl("[VIRTIO-PCI] initialization rejected: capability chain exceeded max hops");
        return false;
    }
    pci_trace_bdf(bus, slot, func, "capability walk end");

    // 4. Wymog obecnosci obowiazkowych capabilities (common, notify, isr)
    if (!has_common || !has_notify || !has_isr) {
        klog_impl("[VIRTIO-PCI] initialization rejected: missing mandatory common/notify/ISR capabilities");
        return false;
    }

    // 5. Rozwiazanie i walidacja BAR oraz przesuniec dla kazdej struktury
    auto validate_and_resolve = [&](const VirtioPciCapHeader& cap, volatile uint8_t** ptr_out) -> bool {
        if (!virtio_pci_map_bar(bus, slot, func, cap.bar, &dev->bars[cap.bar])) {
            klog_impl("[VIRTIO-PCI] initialization rejected: failed to map capability BAR");
            return false;
        }
        const auto& bar_info = dev->bars[cap.bar];
        if (!bar_info.is_mapped || !bar_info.vaddr) return false;

        // Ochrona przed overflow: offset + length
        if (cap.offset > UINT64_MAX - cap.length) return false;
        const uint64_t end_off = static_cast<uint64_t>(cap.offset) + cap.length;
        if (end_off > bar_info.size) {
            klog_impl("[VIRTIO-PCI] initialization rejected: capability range exceeds BAR size");
            return false;
        }
        if (bar_info.phys_addr > UINT64_MAX - end_off) return false;

        const uintptr_t vaddr_num = reinterpret_cast<uintptr_t>(bar_info.vaddr);
        if (vaddr_num > UINTPTR_MAX - end_off) return false;

        *ptr_out = bar_info.vaddr + cap.offset;
        return true;
    };

    pci_trace_bdf(bus, slot, func, "BAR mapping begin");
    volatile uint8_t* ptr_common = nullptr;
    if (!validate_and_resolve(cap_common, &ptr_common)) return false;
    dev->common_cfg = reinterpret_cast<volatile VirtioPciCommonCfgRegs*>(ptr_common);
    dev->common_cfg_len = cap_common.length;
    dev->common_cfg_bar = cap_common.bar;

    volatile uint8_t* ptr_notify = nullptr;
    if (!validate_and_resolve(cap_notify.cap, &ptr_notify)) return false;
    dev->notify_base = ptr_notify;
    dev->notify_cap_len = cap_notify.cap.length;
    dev->notify_multiplier = cap_notify.notify_off_multiplier;
    dev->notify_bar = cap_notify.cap.bar;

    volatile uint8_t* ptr_isr = nullptr;
    if (!validate_and_resolve(cap_isr, &ptr_isr)) return false;
    dev->isr = ptr_isr;
    dev->isr_cap_len = cap_isr.length;
    dev->isr_bar = cap_isr.bar;

    if (has_device) {
        volatile uint8_t* ptr_dev = nullptr;
        if (!validate_and_resolve(cap_device, &ptr_dev)) return false;
        dev->device_cfg = ptr_dev;
        dev->device_cfg_len = cap_device.length;
        dev->device_cfg_bar = cap_device.bar;
        dev->has_device_cfg = true;
    }
    pci_trace_bdf(bus, slot, func, "BAR mapping end");
#if defined(BURSZTYN_DIAG_VIRTIO_BOOT) && BURSZTYN_DIAG_VIRTIO_BOOT
    const VirtioPciCapHeader* diag_caps[] = {&cap_common, &cap_notify.cap, &cap_isr, &cap_device};
    const char* diag_names[] = {"common cfg", "notify cfg", "ISR cfg", "device cfg"};
    for (unsigned i = 0; i < (has_device ? 4U : 3U); ++i) {
        char msg[192]{};
        size_t pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO RESET TRACE] transport=modern ");
        log_append_str(msg, sizeof(msg), pos, diag_names[i]);
        log_append_str(msg, sizeof(msg), pos, " bar=");
        log_append_u64_dec(msg, sizeof(msg), pos, diag_caps[i]->bar);
        log_append_str(msg, sizeof(msg), pos, " offset=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, diag_caps[i]->offset, 8);
        log_append_str(msg, sizeof(msg), pos, " length=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, diag_caps[i]->length, 8);
        klog_impl(msg);
    }
#endif

    // 5.5. Wykrycie i walidacja MSI-X oraz MSI dla urzadzenia
    pci_trace_bdf(bus, slot, func, "MSI-X/MSI discovery begin");
#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_step("[PCI-SCAN] MSI/MSI-X capability discovery begin");
#endif
    PciMsixCapability msix_info{};
    if (pci_find_msix(bus, slot, func, &msix_info)) {
        if (msix_info.table_bir < 6 && virtio_pci_map_bar(bus, slot, func, msix_info.table_bir, &dev->bars[msix_info.table_bir])) {
            const auto& tbar = dev->bars[msix_info.table_bir];
            if (tbar.is_mapped && tbar.vaddr && pci_msix_validate_bar_bounds(&msix_info, tbar.size, 0)) {
                dev->has_msix = true;
                dev->msix_cap = msix_info;
                dev->msix_table = reinterpret_cast<volatile PciMsixTableEntry*>(tbar.vaddr + msix_info.table_offset);
                dev->msix_table_bar = msix_info.table_bir;
            } else {
                dev->has_msix = false;
            }
        } else {
            dev->has_msix = false;
        }
    } else {
        dev->has_msix = false;
    }

    PciMsiCapability msi_info{};
    if (pci_find_msi(bus, slot, func, &msi_info)) {
        dev->has_msi = true;
        dev->msi_cap = msi_info;
    } else {
        dev->has_msi = false;
    }
#if BURSZTYN_DIAG_PCI_ENUM
    {
        char msg[128]{};
        size_t pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[PCI-SCAN] MSI/MSI-X capability discovery: has_msix=");
        log_append_str(msg, sizeof(msg), pos, dev->has_msix ? "true" : "false");
        log_append_str(msg, sizeof(msg), pos, " has_msi=");
        log_append_str(msg, sizeof(msg), pos, dev->has_msi ? "true" : "false");
        klog_impl(msg);
    }
#endif
    pci_trace_bdf(bus, slot, func, "MSI-X/MSI discovery end");

    // 6. Aktywacja MMIO i Bus Mastering w PCI Command
    const uint32_t cmd_status_end = pci_read32(bus, slot, func, 0x04);
    const uint16_t cmd_end = static_cast<uint16_t>(cmd_status_end & 0xFFFFU) | (0x0002U | 0x0004U); // PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER
    pci_write32(bus, slot, func, 0x04, (cmd_status_end & 0xFFFF0000U) | cmd_end);

    // 7. Sekwencja inicjalizacji stanu VirtIO (VirtIO 1.0 sec 3.1.1):
    //    a. Reset urzadzenia (status = 0)
    pci_trace_bdf(bus, slot, func, "reset/features begin");
#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_step("[PCI-SCAN] virtio reset begin");
#endif
    if (!virtio_pci_reset(dev)) {
        klog_impl("[VIRTIO-PCI] initialization rejected: device reset timed out");
        return false;
    }
#if BURSZTYN_DIAG_PCI_ENUM
    pci_diag_step("[PCI-SCAN] virtio reset end");
#endif

    //    b. Ustawienie bitu ACKNOWLEDGE (1)
    virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);

    //    c. Ustawienie bitu DRIVER (2)
    virtio_pci_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    //    d. Negocjacja cech (wymog VIRTIO_F_VERSION_1) oraz ustawienie i weryfikacja FEATURES_OK (8)
    if (!virtio_pci_negotiate_features(dev, requested_features)) {
        return false;
    }
    pci_trace_bdf(bus, slot, func, "reset/features end");

    dev->initialized = true;

    // Diagnostyka znalezienia transportu
    {
        char msg[192];
        size_t pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO-PCI] device=");
        log_append_str(msg, sizeof(msg), pos, virtio_pci_device_name(dev->virtio_id));
        log_append_str(msg, sizeof(msg), pos, " modern transport found (BDF ");
        log_append_u64_hex(msg, sizeof(msg), pos, bus, 2);
        log_append_str(msg, sizeof(msg), pos, ":");
        log_append_u64_hex(msg, sizeof(msg), pos, slot, 2);
        log_append_str(msg, sizeof(msg), pos, ".");
        log_append_u64_dec(msg, sizeof(msg), pos, func);
        log_append_str(msg, sizeof(msg), pos, ")");
        klog_impl(msg);

        pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO-PCI] common cfg BAR=");
        log_append_u64_dec(msg, sizeof(msg), pos, dev->common_cfg_bar);
        log_append_str(msg, sizeof(msg), pos, " offset=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, cap_common.offset, 1);
        log_append_str(msg, sizeof(msg), pos, " len=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, dev->common_cfg_len, 1);
        klog_impl(msg);

        pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO-PCI] notify cfg BAR=");
        log_append_u64_dec(msg, sizeof(msg), pos, dev->notify_bar);
        log_append_str(msg, sizeof(msg), pos, " offset=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, cap_notify.cap.offset, 1);
        log_append_str(msg, sizeof(msg), pos, " len=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, dev->notify_cap_len, 1);
        log_append_str(msg, sizeof(msg), pos, " multiplier=");
        log_append_u64_dec(msg, sizeof(msg), pos, dev->notify_multiplier);
        klog_impl(msg);

        pos = 0;
        log_append_str(msg, sizeof(msg), pos, "[VIRTIO-PCI] features device=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, dev->device_features, 8);
        log_append_str(msg, sizeof(msg), pos, " driver=0x");
        log_append_u64_hex(msg, sizeof(msg), pos, dev->driver_features, 8);
        klog_impl(msg);
    }

    pci_trace_bdf(bus, slot, func, "virtio probe return");
    return true;
}

uint32_t virtio_pci_liczba_urzadzen() {
    return g_virtio_device_count;
}

VirtioPciDevice* virtio_pci_pobierz_urzadzenie(uint32_t indeks) {
    if (indeks < g_virtio_device_count) {
        return &g_virtio_devices[indeks];
    }
    return nullptr;
}

void virtio_pci_inicjalizuj() {
    g_virtio_device_count = 0;
    for (uint16_t bus16 = 0; bus16 < 256; ++bus16) {
        const uint8_t bus = static_cast<uint8_t>(bus16);
        for (uint8_t slot = 0; slot < 32; ++slot) {
            // QEMU exposes the boot-critical devices on bus 0.  Trace these
            // reads before touching config space so a stop in a CF8/CFC
            // transaction is distinguishable from a failed VirtIO probe.
            if (bus == 0) {
                pci_trace_bdf(bus, slot, 0, "enter BDF");
            }
            const uint16_t vendor0 = pci_read16(bus, slot, 0, 0x00);
            if (bus == 0) {
                pci_trace_bdf(bus, slot, 0, "vendor read");
            }
            if (vendor0 == 0xFFFFU) continue;

            const uint32_t hdr = pci_read32(bus, slot, 0, 0x0C);
            const bool multi = (hdr & 0x00800000U) != 0;
            const uint8_t max_func = multi ? 7U : 0U;

            for (uint8_t func = 0; func <= max_func; ++func) {
                if (func != 0 && pci_read16(bus, slot, func, 0x00) == 0xFFFFU) {
                    continue;
                }
                const uint16_t vendor = pci_read16(bus, slot, func, 0x00);
                if (bus == 0) {
                    pci_trace_bdf(bus, slot, func, "vendor/device read");
                }
                if (vendor != VIRTIO_PCI_VENDOR_ID) {
                    continue;
                }
                const uint16_t dev_id = pci_read16(bus, slot, func, 0x02);
                if (dev_id < 0x1000U || dev_id > 0x107FU) {
                    continue;
                }

#if BURSZTYN_DIAG_PCI_ENUM
                pci_diag_scan_enter(bus, slot, func, vendor, dev_id, pci_read8(bus, slot, func, 0x34));
#endif

                if (g_virtio_device_count < MAX_VIRTIO_DEVICES) {
                    pci_trace_bdf(bus, slot, func, "driver probe begin");
                    VirtioPciDevice& dev = g_virtio_devices[g_virtio_device_count];
                    pci_trace_bdf(bus, slot, func, "virtio probe call");
                    if (virtio_pci_probe(bus, slot, func, &dev, 0)) {
                        g_virtio_device_count++;
                    }
                    pci_trace_bdf(bus, slot, func, "virtio probe returned");
                    pci_trace_bdf(bus, slot, func, "driver probe end");
                    pci_trace_bdf(bus, slot, func, "leave BDF");
                }
            }
        }
    }
}

uint16_t virtio_pci_get_config_msix_vector(const VirtioPciDevice* dev) {
    if (!dev || !dev->common_cfg) return VIRTIO_MSI_NO_VECTOR;
    return virtio_read16(&dev->common_cfg->config_msix_vector);
}

void virtio_pci_set_config_msix_vector(VirtioPciDevice* dev, uint16_t vector) {
    if (!dev || !dev->common_cfg) return;
    virtio_write16(&dev->common_cfg->config_msix_vector, vector);
}

bool virtio_pci_set_queue_msix_vector_checked(VirtioPciDevice* dev, uint16_t queue_index, uint16_t entry_index) {
    if (!dev || !dev->common_cfg) return false;
    virtio_pci_select_queue(dev, queue_index);
    virtio_pci_set_queue_msix_vector(dev, entry_index);
    const uint16_t confirmed = virtio_pci_get_queue_msix_vector(dev);
    return (confirmed == entry_index);
}

bool virtio_pci_program_msix_entry(VirtioPciDevice* dev, uint16_t entry_index, uint8_t apic_id, uint8_t vector, bool masked) {
    if (!dev || !dev->has_msix || !dev->msix_table) return false;
    return pci_msix_program_entry(dev->msix_table, dev->msix_cap.table_size, entry_index, apic_id, vector, masked);
}

bool virtio_pci_mask_msix_entry(VirtioPciDevice* dev, uint16_t entry_index) {
    if (!dev || !dev->has_msix || !dev->msix_table) return false;
    return pci_msix_mask_entry(dev->msix_table, dev->msix_cap.table_size, entry_index);
}

bool virtio_pci_unmask_msix_entry(VirtioPciDevice* dev, uint16_t entry_index) {
    if (!dev || !dev->has_msix || !dev->msix_table) return false;
    return pci_msix_unmask_entry(dev->msix_table, dev->msix_cap.table_size, entry_index);
}

bool virtio_pci_is_entry_masked(const VirtioPciDevice* dev, uint16_t entry_index) {
    if (!dev || !dev->has_msix || !dev->msix_table) return true;
    return pci_msix_is_entry_masked(dev->msix_table, dev->msix_cap.table_size, entry_index);
}
