
#include <stdint.h>
#include <stddef.h>
#include "bws_api.h"
#include "bursztyn_gui.h"
#include "stack_protector.h"

struct NaglowekBur {
    uint8_t magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    (uint64_t)&_start,
    0x1000ULL, 0x80000ULL, 0x1001000ULL,
    0x81000ULL, 0x20000ULL, 0x1081000ULL
};
}

__attribute__((noinline))
static int bezpieczna_funkcja_stosu(uint8_t v) {
    volatile uint8_t buf[32];
    for (size_t i = 0; i < 32; ++i) buf[i] = static_cast<uint8_t>(v + i);
    return static_cast<int>(buf[31]);
}

static void log_ring3(const char* s) {
    (void)bws_wywolaj(BWS_WYPISZ, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

extern "C" __attribute__((noreturn)) void _start() {
    for (int i = 0; i < 64; ++i) {
        (void)bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0);
    }
    if (__stack_chk_guard != BUR_SSP_GUARD_PLACEHOLDER &&
        __stack_chk_guard != 0ULL &&
        (__stack_chk_guard & 0xFFULL) == 0ULL &&
        bezpieczna_funkcja_stosu(10) == 41) {
        log_ring3("\n[SSP-RING3-TEST] SURVIVOR_PROCESS_ALIVE_AFTER_SSP_KILL\n");
    } else {
        log_ring3("\n[SSP-RING3-TEST] SURVIVOR_GUARD_INVALID\n");
    }
    bws_zakoncz_proces();
    for (;;) {}
}
