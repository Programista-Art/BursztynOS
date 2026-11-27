// Exercise production locks; substitute only privileged IRQ and port I/O.
#include "heap.h"
#include "pamiec.h"
#include "pci.h"
#include "pmm_diagnostics.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {
thread_local bool irq_enabled = true;
thread_local bool audit_restores = true;
thread_local bool pending_irq = false;
thread_local unsigned deferred_irqs = 0;
uint32_t config_address = 0;
uint32_t config_data[32][64]{};
bool check_busy_pci = false;
unsigned busy_reads = 0;
alignas(16) std::array<uint8_t, 4096> heap_memory{};

void assert_unlocked() {
    assert(heap_diagnostic_lock_state() == 0);
    assert(pmm_diagnostic_lock_state() == 0);
    assert(pci_diagnostic_lock_state() == 0);
}

struct TestMultiboot {
    std::vector<uint64_t> words;
    // R1A: mapa opisuje tez RAM pod obrazem jadra (host: 1..2 MiB). PMM nie dopisuje juz obrazu jako
    // "dostepnego" na sile - obraz poza RAM z mapy jest krytycznym bledem wlasnosci pamieci.
    TestMultiboot() : words(10, 0) {
        const uint32_t header[] = {80, 0, 6, 64, 24, 0};
        std::memcpy(words.data(), header, sizeof(header));
        const WpisMapyPamieciMB2 kernel_ram{0x00100000, 0x100000, 1, 0};
        std::memcpy(reinterpret_cast<uint8_t*>(words.data()) + 24, &kernel_ram, sizeof(kernel_ram));
        const WpisMapyPamieciMB2 ram{0x03000000, 0x100000, 1, 0};
        std::memcpy(reinterpret_cast<uint8_t*>(words.data()) + 48, &ram, sizeof(ram));
        const uint32_t end[] = {0, 8};
        std::memcpy(reinterpret_cast<uint8_t*>(words.data()) + 72, end, sizeof(end));
        words[0] = 80;
    }
};

void test_heap_irq_and_failure_paths() {
    for (bool enabled : {false, true}) {
        irq_enabled = enabled;
        inicjalizuj_sterte_jadra(heap_memory.data(), heap_memory.size());
        assert(irq_enabled == enabled);
        void* p = kmalloc(3000);
        assert(p && !kmalloc(3000) && !kmalloc(UINT64_MAX));
        kfree(static_cast<uint8_t*>(p) + 1);
        kfree(p);
        kfree(p);
        assert(heap_zajete_bajty() == 0 && irq_enabled == enabled);
        inicjalizuj_sterte_jadra(nullptr, 0);
        assert(!kmalloc(16));
        assert_unlocked();
    }
    assert(deferred_irqs > 0);
}

void test_pmm_irq_and_failure_paths() {
    TestMultiboot mb;
    for (bool enabled : {false, true}) {
        irq_enabled = enabled;
        InicjalizujPMM(reinterpret_cast<uintptr_t>(mb.words.data()));
        void* p = ZaalokujCiagleRamki(256);
        assert(reinterpret_cast<uintptr_t>(p) == 0x03000000);
        assert(!ZaalokujCiagleRamki(1) && !ZaalokujRamke());
        assert(!ZaalokujCiagleRamki(SIZE_MAX));
        ZwolnijCiagleRamki(p, 256);
        assert(ZaalokujCiagleRamki(1) == p);
        ZwolnijCiagleRamki(p, 1);
        assert(irq_enabled == enabled);
        assert_unlocked();
    }
}

void test_pci_irq_and_nonblocking_snapshot() {
    check_busy_pci = true;
    for (bool enabled : {false, true}) {
        irq_enabled = enabled;
        pci_zapisz_dword(0, 7, 0, 4, 0x12340400);
        assert(pci_odczytaj_dword(0, 7, 0, 4) == 0x12340400);
        uint32_t value = 0;
        assert(pci_diagnostic_try_read(0, 7, 0, 4, &value));
        assert(value == 0x12340400);
        assert(pci_odczytaj_dword(0, 32, 0, 4) == UINT32_MAX);
        assert(!pci_diagnostic_try_read(0, 32, 0, 4, &value));
        assert(irq_enabled == enabled);
        assert_unlocked();
    }
    check_busy_pci = false;
    assert(busy_reads >= 6);
}

void test_concurrent_heap_pmm_and_pci() {
    inicjalizuj_sterte_jadra(heap_memory.data(), heap_memory.size());
    TestMultiboot mb;
    InicjalizujPMM(reinterpret_cast<uintptr_t>(mb.words.data()));
    std::array<std::thread, 4> threads;
    std::array<std::array<void*, 16>, 4> frames{};
    std::atomic<unsigned> allocated{0};
    for (unsigned t = 0; t < threads.size(); ++t) {
        threads[t] = std::thread([&, t] {
            audit_restores = false; // Other CPUs may legitimately own a lock.
            for (auto& frame : frames[t]) { frame = ZaalokujRamke(); assert(frame); }
            allocated.fetch_add(1);
            while (allocated.load() != threads.size()) std::this_thread::yield();
            for (unsigned i = 0; i < 100; ++i) {
                auto* p = static_cast<uint8_t*>(kmalloc(64));
                assert(p);
                std::memset(p, t + 1, 64);
                std::this_thread::yield();
                for (unsigned j = 0; j < 64; ++j) assert(p[j] == t + 1);
                kfree(p);
                const uint32_t value = (t << 16) | i;
                pci_zapisz_dword(0, t, 0, 4, value);
                assert(pci_odczytaj_dword(0, t, 0, 4) == value);
                assert(irq_enabled);
            }
        });
    }
    for (auto& thread : threads) thread.join();
    std::vector<void*> all;
    for (const auto& batch : frames) for (void* frame : batch) {
        for (void* previous : all) assert(previous != frame);
        all.push_back(frame);
    }
    for (void* frame : all) ZwolnijRamke(frame);
    assert(heap_zajete_bajty() == 0);
    assert_unlocked();
}
} // namespace

uint64_t host_memory_irq_save() {
    const uint64_t flags = irq_enabled ? 1ULL << 9 : 0;
    irq_enabled = false;
    if (audit_restores) pending_irq = true;
    return flags;
}

void host_memory_irq_restore(uint64_t flags) {
    assert(!irq_enabled);
    irq_enabled = (flags & (1ULL << 9)) != 0;
    if (audit_restores) {
        // Every early return must release the lock before reenabling IRQs.
        if (irq_enabled) assert_unlocked();
        if (irq_enabled && pending_irq) { pending_irq = false; ++deferred_irqs; }
    }
}

void host_pci_lock_io_write(uint16_t port, uint32_t value) {
    assert(!irq_enabled && pci_diagnostic_lock_state() == 1);
    if (port == 0xCF8) {
        config_address = value;
        if (check_busy_pci) {
            uint32_t snapshot = 0;
            // Called while the actual PCI lock is owned: one attempt, no spin.
            assert(!pci_diagnostic_try_read(0, 7, 0, 4, &snapshot));
            assert(!irq_enabled && pci_diagnostic_lock_state() == 1);
            ++busy_reads;
        }
        std::this_thread::yield();
    } else {
        assert(port == 0xCFC);
        config_data[(config_address >> 11) & 31][(config_address & 255) / 4] = value;
    }
}

uint32_t host_pci_lock_io_read(uint16_t port) {
    assert(port == 0xCFC && !irq_enabled && pci_diagnostic_lock_state() == 1);
    return config_data[(config_address >> 11) & 31][(config_address & 255) / 4];
}

int main() {
    test_heap_irq_and_failure_paths();
    test_pmm_irq_and_failure_paths();
    test_pci_irq_and_nonblocking_snapshot();
    irq_enabled = true;
    test_concurrent_heap_pmm_and_pci();
    std::puts("PASS=4 FAIL=0: production heap/PMM/PCI locks, IRQ restoration, contention and failure paths");
}
