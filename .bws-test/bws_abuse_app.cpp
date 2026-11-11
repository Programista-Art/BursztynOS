
#include <stdint.h>
#include <stddef.h>
#include "jadro/bws/bws_api.h"
#include "jadro/bws/pzb.h"
#include "jadro/gui/bursztyn_gui.h"

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

static void log_s(const char* s) {
    (void)bws_wywolaj(BWS_WYPISZ, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

#define CHECK_BWS(cond, msg) \
    do { \
        if (!(cond)) { \
            log_s("\n[BWS-ABUSE-FAIL] " msg "\n"); \
            bws_zakoncz_proces(); \
            for (;;) {} \
        } \
    } while (0)

extern "C" __attribute__((noreturn)) void _start() {
    log_s("\n[BWS-ABUSE-QEMU] Start testow odpornosci BWS z nieuprzywilejowanego procesu Ring 3 (PZB_UZYTKOWNIK)\n");

    /* 1. Regresja Bug #1: BWS 99 z 0xDEADBEEFCAFEULL NIGDY nie moze wywolac paniki jadra! */
    const uint64_t r99 = bws_wywolaj(99ULL, 0xDEADBEEFCAFEULL, 0, 0, 0);
    CHECK_BWS(r99 == UINT64_MAX, "Bug #1 regression failed: BWS 99 did not return UINT64_MAX");
    log_s("[BWS-ABUSE-QEMU] 1/6 Regresja Bug #1 (BWS 99 + 0xDEADBEEFCAFEULL): OK\n");

    /* 2. Wskazniki NULL, jadro, HHDM, niekanoniczne oraz guard page stosu Ring 3 */
    const uint64_t bad_ptrs[] = {
        0x0ULL,
        0x100000ULL,               // Niska pamiec jadra
        0xFFFFFFFF80000000ULL,     // Wysoka pamiec kanoniczna
        0xFFFF800000100000ULL,     // HHDM jadra
        0x0001000000000000ULL,     // Adres niekanoniczny x86-64
        0x000000003FFFF000ULL,     // Niezmapowany adres w przestrzeni uzytkownika
        0x00007FFFEFFFF000ULL,     // Strona guard page ponizej stosu Ring 3 (RING3_STACK_TOP - 4096)
        0x00007FFFF0010000ULL,     // Niezmapowana strona powyzej 16-stronicowego stosu
    };
    for (size_t i = 0; i < sizeof(bad_ptrs) / sizeof(bad_ptrs[0]); ++i) {
        const uint64_t p = bad_ptrs[i];
        CHECK_BWS(bws_wywolaj(BWS_WYPISZ, p, 0, 0, 0) == 0, "BWS_WYPISZ accepted bad pointer");
        CHECK_BWS(bws_wywolaj(BWS_CZAS_RTC, p, 0, 0, 0) == 0, "BWS_CZAS_RTC accepted bad pointer");
        CHECK_BWS(bws_wywolaj(BWS_CZYTAJ_PLIK, reinterpret_cast<uint64_t>("/cytaty.txt"), p, 64, 0) == 0,
                  "BWS_CZYTAJ_PLIK accepted bad output pointer");
        CHECK_BWS(bws_wywolaj(BWS_GUI_POBIERZ_MYSZ, p, p, p, 0) == 0,
                  "BWS_GUI_POBIERZ_MYSZ accepted bad pointer");
        CHECK_BWS(bws_wywolaj(BWS_ZDARZENIE_POLL, p, 0, 0, 0) == 0,
                  "BWS_ZDARZENIE_POLL accepted bad pointer");
    }
    log_s("[BWS-ABUSE-QEMU] 2/6 Odrzucanie wskaznikow NULL/Jadro/HHDM/Niekanonicznych/GuardPage: OK\n");

    /* 3. Proba zapisu przez jadro do segmentu kodu tylko do odczytu (.text = 0x1001000) */
    const uint64_t ro_text_va = 0x1001000ULL;
    CHECK_BWS(bws_wywolaj(BWS_CZAS_RTC, ro_text_va, 0, 0, 0) == 0,
              "BWS_CZAS_RTC wrote into read-only .text segment");
    CHECK_BWS(bws_wywolaj(BWS_CZYTAJ_PLIK, reinterpret_cast<uint64_t>("/cytaty.txt"), ro_text_va, 32, 0) == 0,
              "BWS_CZYTAJ_PLIK wrote into read-only .text segment");
    CHECK_BWS(bws_wywolaj(BWS_GUI_ROZDZIELCZOSC, ro_text_va, ro_text_va + 4, 0, 0) == 0,
              "BWS_GUI_ROZDZIELCZOSC wrote into read-only .text segment");
    log_s("[BWS-ABUSE-QEMU] 3/6 Ochrona W^X przed zapisem jadra do .text procesu Ring 3: OK\n");

    /* 4. Przepelnienia liczb calkowitych w rozmiarach alokacji i buforow */
    CHECK_BWS(bws_wywolaj(BWS_ALOKUJ_STERTE, UINT64_MAX, 0, 0, 0) == 0,
              "BWS_ALOKUJ_STERTE(UINT64_MAX) did not fail");
    CHECK_BWS(bws_wywolaj(BWS_ALOKUJ_STERTE, 0x80000000ULL, 0, 0, 0) == 0,
              "BWS_ALOKUJ_STERTE(2GiB) did not fail");
    CHECK_BWS(bws_wywolaj(BWS_GUI_BLIT, 0, (4096ULL << 32) | 4096ULL, 0x1081000ULL, 0) == 0,
              "BWS_GUI_BLIT(4096x4096) did not fail");
    log_s("[BWS-ABUSE-QEMU] 4/6 Odrzucanie przepelnien rozmiarow (sterta/blit): OK\n");

    /* 5. Egzekwowanie PZB i PRAWO_* dla operacji systemowych z PZB_UZYTKOWNIK */
    uint8_t process_list[2048] = {};
    CHECK_BWS(bws_wywolaj(BWS_PROCESY_LISTA, reinterpret_cast<uint64_t>(process_list), 1, 0, 0) == 0,
              "Unprivileged process enumerated privileged process metadata");
    CHECK_BWS(bws_wywolaj(BWS_PROCES_ZABIJ, 1, 0, 0, 0) == 0,
              "Unprivileged process killed the system manager");
    CHECK_BWS(bws_wywolaj(BWS_RESET, 0, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_RESET");
    CHECK_BWS(bws_wywolaj(BWS_SHUTDOWN, 0, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_SHUTDOWN");
    CHECK_BWS(bws_wywolaj(BWS_PULPIT_GOTOWY, 0, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_PULPIT_GOTOWY");
    CHECK_BWS(bws_wywolaj(BWS_OTA_STATUS, 0x1081000ULL, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_OTA_STATUS");
    CHECK_BWS(bws_wywolaj(BWS_OTA_POLECENIE, 1, 0, 0, 0) != 0,
              "Unprivileged process allowed to call BWS_OTA_POLECENIE (expected BWS_OTA_BLAD_BRAK_UPRAWNIEN != 0)");
    CHECK_BWS(bws_wywolaj(BWS_BSF_CONFIG, 0x1081000ULL, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_BSF_CONFIG");
    log_s("[BWS-ABUSE-QEMU] 5/6 Egzekwowanie polityki PZB/PRAWO_* dla wywolan uprzywilejowanych: OK\n");

    /* 6. Odrzucanie traversalu sciezek w wywolaniach plikowych */
    CHECK_BWS(bws_wywolaj(BWS_ROZMIAR_PLIKU, reinterpret_cast<uint64_t>("/../../cytaty.txt"), 0, 0, 0) == 0,
              "Path traversal /../../ accepted");
    CHECK_BWS(bws_wywolaj(BWS_ROZMIAR_PLIKU, reinterpret_cast<uint64_t>("/.system/slot_a/../../cytaty.txt"), 0, 0, 0) == 0,
              "Slot path traversal accepted");
    log_s("[BWS-ABUSE-QEMU] 6/6 Odrzucanie traversalu sciezek BWS: OK\n");

    log_s("[BWS-ABUSE-QEMU] ALL_CHECKS_PASSED\n");
    bws_zakoncz_proces();
    for (;;) {}
}
