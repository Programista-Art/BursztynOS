
#include <stdint.h>
#include <stddef.h>
#include "bws_api.h"
#include "pzb.h"
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
    0x1000ULL, 0x4c000ULL, 0x1001000ULL,
    0x4d000ULL, 0x20000ULL, 0x104d000ULL
};
}

__attribute__((noinline))
static int uszkodz_stos_ring3(size_t n) {
    volatile uint8_t buf[32];
    for (size_t i = 0; i < 32; ++i) buf[i] = 0x22;
    volatile uint8_t* p = buf;
    for (size_t i = 0; i < n; ++i) p[i] = 0x77;
    return static_cast<int>(buf[0]);
}

static void log_ring3(const char* s) {
    (void)bws_wywolaj(BWS_WYPISZ, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

extern "C" __attribute__((noreturn)) void _start() {
    log_ring3("\n[SSP-RING3-TEST] Start jednorazowego programu testowego Ring 3 (PID 1)\n");
    if (__stack_chk_guard != BUR_SSP_GUARD_PLACEHOLDER &&
        __stack_chk_guard != 0ULL &&
        (__stack_chk_guard & 0xFFULL) == 0ULL) {
        log_ring3("[SSP-RING3-TEST] GUARD_INITIALIZED_OK\n");
    } else {
        log_ring3("[SSP-RING3-TEST] GUARD_INIT_FAIL\n");
    }

    /* Uruchamiamy drugi proces Ring 3 (/programy/test.cebula/test.bur), aby
       udowodnic ze po zabiciu naszego procesu przez SSP jadro oraz inne
       procesy Ring 3 dzialaja dalej bez przeszkod. */
    (void)bws_wywolaj(BWS_URUCHOM_PROGRAM,
                      reinterpret_cast<uint64_t>("/programy/test.cebula/test.bur"),
                      0, 0, 0);

    log_ring3("[SSP-RING3-TEST] Wywolanie funkcji z celowym przepelnieniem bufora stosu...\n");
    (void)uszkodz_stos_ring3(56);

    /* Ten kod NIGDY nie moze sie wykonac! */
    log_ring3("[SSP-RING3-TEST] BLAD_POWROT_Z_USZKODZONEJ_FUNKCJI\n");
    bws_zakoncz_proces();
    for (;;) {}
}
