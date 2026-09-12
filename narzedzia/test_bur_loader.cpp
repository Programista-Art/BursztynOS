/*
 * Bursztyn OS - Kompleksowy zestaw testow jednostkowych loadera programow .bur
 *
 * Testuje dokladna walidacje naglowka bur_waliduj_naglowek() dla wszystkich
 * przypadkow brzegowych okreslonych w specyfikacji:
 *  1. small existing application (32 KiB)
 *  2. Aktualizacje Systemu (192 KiB)
 *  3. exactly at TEXT maximum (64 MiB)
 *  4. 1 byte above maximum / not page-aligned (64 MiB + 1 B)
 *  5. truncated text
 *  6. oversized text
 *  7. text offset overflow
 *  8. text plus rodata overflow
 *  9. data overlap (w pliku i w VA)
 * 10. stack overlap
 * 11. entry point outside TEXT
 * 12. malformed header (uszkodzona magia, rozmiar < 60)
 * 13. old-format application compatibility (BUR\0 oraz nieznana wersja)
 *
 * Kompilacja hostowa:
 *   g++ -std=c++17 -Wall -Wextra -Wpedantic -DBURSZTYN_LOADER_HOST_TEST \
 *       loader.cpp narzedzia/test_bur_loader.cpp -o narzedzia/test_bur_loader
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cassert>

#include "../loader.h"
#include "../pamiec.h"

static int zdane = 0;
static int zlane = 0;

static void raport(const char* nazwa, bool wynik, bool oczekiwany) {
    const bool zgodne = (wynik == oczekiwany);
    if (zgodne) {
        ++zdane;
        std::printf("  PASS [%s]\n", nazwa);
    } else {
        ++zlane;
        std::printf("  FAIL [%s] wynik=%d, oczekiwany=%d\n", nazwa, wynik ? 1 : 0, oczekiwany ? 1 : 0);
    }
}

static NaglowekBur utworz_poprawny_naglowek(
    uint64_t tekst_rozmiar = 0x8000,
    uint64_t dane_rozmiar = 0x20000,
    uint64_t tekst_va = 0x1001000,
    uint64_t tekst_off = 0x1000
) {
    NaglowekBur n{};
    n.magia[0] = BUR_MAGIA_0;
    n.magia[1] = BUR_MAGIA_1;
    n.magia[2] = BUR_MAGIA_2;
    n.magia[3] = BUR_MAGIA_3;

    n.punkt_wejscia = tekst_va;
    n.tekst_przesuniecie = tekst_off;
    n.tekst_rozmiar = tekst_rozmiar;
    n.tekst_wirtualny = tekst_va;

    n.dane_przesuniecie = tekst_off + tekst_rozmiar;
    n.dane_rozmiar = dane_rozmiar;
    n.dane_wirtualny = tekst_va + tekst_rozmiar;

    return n;
}

int main() {
    std::printf("=== TESTY LOADERA BURSZTYN OS (.bur) ===\n");

    // 1. Small existing application (32 KiB TEXT, 128 KiB DATA)
    {
        NaglowekBur n = utworz_poprawny_naglowek(0x8000, 0x20000, 0x1001000, 0x1000);
        uint64_t rozmiar_pliku = n.dane_przesuniecie + 0x1000;
        raport("1.1 small-existing-app-32k", bur_waliduj_naglowek(n, rozmiar_pliku), true);
    }

    // 2. Aktualizacje Systemu (192 KiB TEXT, 128 KiB DATA)
    {
        NaglowekBur n = utworz_poprawny_naglowek(0x30000, 0x20000, 0x1001000, 0x1000);
        n.punkt_wejscia = 0x1001c27;
        uint64_t rozmiar_pliku = 200768; // 0x31000 + 64 bajty inicjalizowanych danych
        raport("2.1 aktualizacje-systemu-192k", bur_waliduj_naglowek(n, rozmiar_pliku), true);
    }

    // 3. Exactly at TEXT maximum (64 MiB)
    {
        NaglowekBur n = utworz_poprawny_naglowek(BUR_MAKS_TEKST_ROZMIAR, 0x10000, 0x1001000, 0x1000);
        uint64_t rozmiar_pliku = n.dane_przesuniecie + 0x1000;
        raport("3.1 exactly-at-text-maximum-64m", bur_waliduj_naglowek(n, rozmiar_pliku), true);
    }

    // 4. 1 byte above maximum / not page-aligned (64 MiB + 1 B i inne niewyrownane)
    {
        // 4a. 64 MiB + 1 B
        NaglowekBur n1 = utworz_poprawny_naglowek(BUR_MAKS_TEKST_ROZMIAR + 1, 0x10000, 0x1001000, 0x1000);
        raport("4.1 64m-plus-1-byte-rejected", bur_waliduj_naglowek(n1, BUR_MAKS_ROZMIAR_PLIKU), false);

        // 4b. Niewyrownany tekst_rozmiar (32 KiB + 1 B)
        NaglowekBur n2 = utworz_poprawny_naglowek(0x8001, 0x10000, 0x1001000, 0x1000);
        raport("4.2 unaligned-text-size-rejected", bur_waliduj_naglowek(n2, 0x20000), false);

        // 4c. Niewyrownany tekst_przesuniecie (0x1001)
        NaglowekBur n3 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1001);
        raport("4.3 unaligned-text-offset-rejected", bur_waliduj_naglowek(n3, 0x20000), false);

        // 4d. Niewyrownany tekst_wirtualny (0x1001001)
        NaglowekBur n4 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001001, 0x1000);
        n4.punkt_wejscia = 0x1001001;
        raport("4.4 unaligned-text-va-rejected", bur_waliduj_naglowek(n4, 0x20000), false);

        // 4e. Niewyrownany dane_rozmiar (0x10001)
        NaglowekBur n5 = utworz_poprawny_naglowek(0x8000, 0x10001, 0x1001000, 0x1000);
        raport("4.5 unaligned-data-size-rejected", bur_waliduj_naglowek(n5, 0x20000), false);

        // 4f. Niewyrownany dane_przesuniecie (0x9001)
        NaglowekBur n6 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        n6.dane_przesuniecie = 0x9001;
        raport("4.6 unaligned-data-offset-rejected", bur_waliduj_naglowek(n6, 0x20000), false);

        // 4g. Niewyrownany dane_wirtualny (0x1009001)
        NaglowekBur n7 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        n7.dane_wirtualny = 0x1009001;
        raport("4.7 unaligned-data-va-rejected", bur_waliduj_naglowek(n7, 0x20000), false);
    }

    // 5. Truncated text (plik za krotki w stosunku do deklarowanego segmentu TEXT)
    {
        NaglowekBur n = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        uint64_t za_maly_plik = 0x5000; // tekst konczy sie na 0x9000
        raport("5.1 truncated-text-rejected", bur_waliduj_naglowek(n, za_maly_plik), false);
    }

    // 6. Oversized text (przekroczenie limitu BUR_MAKS_TEKST_ROZMIAR)
    {
        NaglowekBur n = utworz_poprawny_naglowek(BUR_MAKS_TEKST_ROZMIAR + 4096, 0x10000, 0x1001000, 0x1000);
        raport("6.1 oversized-text-rejected", bur_waliduj_naglowek(n, BUR_MAKS_ROZMIAR_PLIKU), false);
    }

    // 7. Text offset overflow (przepelnienie arytmetyczne uint64_t)
    {
        // 7a. Offset w pliku przepelnia uint64_t
        NaglowekBur n1 = utworz_poprawny_naglowek(0x2000, 0, 0x1001000, 0xFFFFFFFFFFFFF000ULL);
        raport("7.1 text-file-offset-overflow-rejected", bur_waliduj_naglowek(n1, 0xFFFFFFFFFFFFFFFFULL), false);

        // 7b. Adres wirtualny tekstu przepelnia uint64_t
        NaglowekBur n2 = utworz_poprawny_naglowek(0x2000, 0, 0xFFFFFFFFFFFFF000ULL, 0x1000);
        n2.punkt_wejscia = 0xFFFFFFFFFFFFF000ULL;
        raport("7.2 text-va-overflow-rejected", bur_waliduj_naglowek(n2, 0x10000), false);
    }

    // 8. Text plus rodata overflow / total size overflow
    {
        // 8a. Suma TEXT + DATA przekracza BUR_MAKS_CALKOWITY_ROZMIAR (128 MiB)
        NaglowekBur n1 = utworz_poprawny_naglowek(BUR_MAKS_TEKST_ROZMIAR, 68ULL * 1024 * 1024, 0x1001000, 0x1000);
        raport("8.1 total-size-exceeds-128m-rejected", bur_waliduj_naglowek(n1, BUR_MAKS_ROZMIAR_PLIKU), false);

        // 8b. Suma TEXT + DATA przepelnia arytmetycznie uint64_t
        NaglowekBur n2 = utworz_poprawny_naglowek(0x10000, 0xFFFFFFFFFFFF0000ULL, 0x1001000, 0x1000);
        raport("8.2 text-plus-data-arithmetic-overflow-rejected", bur_waliduj_naglowek(n2, 0x20000), false);
    }

    // 9. Data overlap (w pliku i w VA)
    {
        // 9a. Kolizja w pamieci wirtualnej (DATA zaczyna sie wewnatrz TEXT)
        NaglowekBur n1 = utworz_poprawny_naglowek(0x20000, 0x10000, 0x1001000, 0x1000);
        n1.dane_wirtualny = 0x1010000; // wewnatrz [0x1001000, 0x1021000)
        raport("9.1 data-va-overlap-rejected", bur_waliduj_naglowek(n1, 0x50000), false);

        // 9b. Kolizja w pliku (DATA zaczyna sie wewnatrz fragmentu pliku TEXT)
        NaglowekBur n2 = utworz_poprawny_naglowek(0x20000, 0x10000, 0x1001000, 0x1000);
        n2.dane_przesuniecie = 0x10000; // wewnatrz [0x1000, 0x21000)
        raport("9.2 data-file-overlap-rejected", bur_waliduj_naglowek(n2, 0x50000), false);
    }

    // 10. Stack overlap (kolizja ze stosem Ring 3 i strona straznika)
    {
        // 10a. TEXT VA nachodzi na obszar stosu
        NaglowekBur n1 = utworz_poprawny_naglowek(0x8000, 0, RING3_STACK_TOP, 0x1000);
        n1.punkt_wejscia = RING3_STACK_TOP;
        raport("10.1 text-stack-overlap-rejected", bur_waliduj_naglowek(n1, 0x10000), false);

        // 10b. TEXT VA nachodzi na strone straznika stosu (guard page)
        NaglowekBur n2 = utworz_poprawny_naglowek(0x2000, 0, RING3_STACK_TOP - 4096, 0x1000);
        n2.punkt_wejscia = RING3_STACK_TOP - 4096;
        raport("10.2 text-stack-guard-overlap-rejected", bur_waliduj_naglowek(n2, 0x10000), false);

        // 10c. DATA VA nachodzi na obszar stosu
        NaglowekBur n3 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        n3.dane_wirtualny = RING3_STACK_TOP;
        raport("10.3 data-stack-overlap-rejected", bur_waliduj_naglowek(n3, 0x20000), false);
    }

    // 11. Entry point outside TEXT
    {
        // 11a. Punkt wejscia ponizej poczatku TEXT
        NaglowekBur n1 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        n1.punkt_wejscia = 0x1000000;
        raport("11.1 entry-below-text-rejected", bur_waliduj_naglowek(n1, 0x20000), false);

        // 11b. Punkt wejscia dokladnie na granicy konca TEXT
        NaglowekBur n2 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        n2.punkt_wejscia = 0x1009000; // koniec TEXT
        raport("11.2 entry-at-text-end-rejected", bur_waliduj_naglowek(n2, 0x20000), false);

        // 11c. Punkt wejscia wewnatrz segmentu DATA
        NaglowekBur n3 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        n3.punkt_wejscia = 0x1009020;
        raport("11.3 entry-in-data-rejected", bur_waliduj_naglowek(n3, 0x20000), false);

        // 11d. Punkt wejscia w obszarze stosu
        NaglowekBur n4 = utworz_poprawny_naglowek(0x8000, 0x10000, 0x1001000, 0x1000);
        n4.punkt_wejscia = RING3_STACK_TOP + 0x1000;
        raport("11.4 entry-in-stack-rejected", bur_waliduj_naglowek(n4, 0x20000), false);
    }

    // 12. Malformed header (uszkodzona magia, rozmiar pliku < 60)
    {
        // 12a. Rozmiar pliku 59 bajtow (< sizeof(NaglowekBur) == 60)
        NaglowekBur n1 = utworz_poprawny_naglowek();
        raport("12.1 file-size-less-than-header-rejected", bur_waliduj_naglowek(n1, 59), false);

        // 12b. Rozmiar pliku 0 bajtow
        raport("12.2 file-size-zero-rejected", bur_waliduj_naglowek(n1, 0), false);

        // 12c. Bledna magia "XUR\0"
        NaglowekBur n2 = utworz_poprawny_naglowek();
        n2.magia[0] = 'X';
        raport("12.3 corrupted-magic-rejected", bur_waliduj_naglowek(n2, 0x20000), false);

        // 12d. Bledna magia "\0\0\0\0"
        NaglowekBur n3 = utworz_poprawny_naglowek();
        std::memset(n3.magia, 0, 4);
        raport("12.4 null-magic-rejected", bur_waliduj_naglowek(n3, 0x20000), false);

        // 12e. Magia ELF "\x7FELF"
        NaglowekBur n4 = utworz_poprawny_naglowek();
        n4.magia[0] = 0x7F; n4.magia[1] = 'E'; n4.magia[2] = 'L'; n4.magia[3] = 'F';
        raport("12.5 elf-magic-rejected", bur_waliduj_naglowek(n4, 0x20000), false);

        // 12f. Zerowy rozmiar tekstu
        NaglowekBur n5 = utworz_poprawny_naglowek();
        n5.tekst_rozmiar = 0;
        raport("12.6 zero-text-size-rejected", bur_waliduj_naglowek(n5, 0x20000), false);

        // 12g. Offset tekstu mniejszy niz rozmiar naglowka (< 60)
        NaglowekBur n6 = utworz_poprawny_naglowek();
        n6.tekst_przesuniecie = 0;
        raport("12.7 text-offset-less-than-header-rejected", bur_waliduj_naglowek(n6, 0x20000), false);
    }

    // 13. Old-format application compatibility (BUR\0 oraz nieobslugiwane wersje)
    {
        // 13a. Kanoniczny format "BUR\0" jest w pelni poprawny
        NaglowekBur n1 = utworz_poprawny_naglowek(0x8000, 0x20000, 0x1001000, 0x1000);
        raport("13.1 canonical-bur0-accepted", bur_waliduj_naglowek(n1, 0x20000), true);

        // 13b. Nieznana wersja "BUR1" jest odrzucana (fail-closed)
        NaglowekBur n2 = utworz_poprawny_naglowek(0x8000, 0x20000, 0x1001000, 0x1000);
        n2.magia[3] = '1';
        raport("13.2 unknown-version-bur1-rejected", bur_waliduj_naglowek(n2, 0x20000), false);

        // 13c. Nieznana wersja "BUR2" jest odrzucana (fail-closed)
        NaglowekBur n3 = utworz_poprawny_naglowek(0x8000, 0x20000, 0x1001000, 0x1000);
        n3.magia[3] = '2';
        raport("13.3 unknown-version-bur2-rejected", bur_waliduj_naglowek(n3, 0x20000), false);
    }

    std::printf("========================================\n");
    std::printf("WYNIKI TESTOW: zdane=%d, zlane=%d\n", zdane, zlane);
    return (zlane == 0) ? 0 : 1;
}
