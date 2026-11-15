#include "jadro/mm/pamiec.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace {

struct TestMultiboot {
    uint64_t slowa[8]{};

    TestMultiboot() {
        const uint32_t naglowek[] = {64, 0, 6, 40, 24, 0};
        std::memcpy(slowa, naglowek, sizeof(naglowek));
        const WpisMapyPamieciMB2 ram{
            0x00200000ULL, 0x80000000ULL, MULTIBOOT_MEMORY_AVAILABLE, 0};
        std::memcpy(reinterpret_cast<uint8_t*>(slowa) + 24, &ram, sizeof(ram));
        const uint32_t koniec[] = {0, 8};
        std::memcpy(reinterpret_cast<uint8_t*>(slowa) + 48, koniec,
                    sizeof(koniec));
        slowa[0] = 56;
    }
};

}  // namespace

int main() {
    TestMultiboot mb;
    InicjalizujPMM(reinterpret_cast<uintptr_t>(mb.slowa));

    void* user_a = ZaalokujRamkeUzytkownika();
    void* user_b = ZaalokujRamkeUzytkownika();
    assert(user_a && user_b && user_a != user_b);
    assert(reinterpret_cast<uintptr_t>(user_a) >= 0x40000000ULL);
    assert(reinterpret_cast<uintptr_t>(user_b) >= 0x40000000ULL);

    void* dma = ZaalokujRamke();
    assert(dma && reinterpret_cast<uintptr_t>(dma) < 0x40000000ULL);

    ZwolnijRamke(user_a);
    ZwolnijRamke(user_b);
    ZwolnijRamke(dma);

    std::printf("[PMM-USER-HOST] PASS\n");
    return 0;
}
