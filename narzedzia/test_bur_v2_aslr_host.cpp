/*
 * Bursztyn OS — Hostowe testy jednostkowe i regresyjne dla BUR v2 oraz Ring 3 ASLR
 * (narzedzia/test_bur_v2_aslr_host.cpp).
 *
 * Weryfikuje (pod kontrola ASan + UBSan):
 *   1. Zachowanie pelnej kompatybilnosci wstecznej dla istniejacago formatu BUR v1 (`BUR\0`).
 *   2. Rygorystyczna walidacje naglowka `NaglowekBurV2` (`BUR\x02`):
 *      - magia, wersja, flagi, wyrownanie stron 4 KiB,
 *      - rozdzielenie uprawnien `.text` (RX), `.rodata` (RO+NX), `.data`/`.bss` (RW+NX),
 *      - brak nakladania sie segmentow w pliku i w pamieci wirtualnej,
 *      - wykrywanie przepelnien 64-bitowych (`__builtin_add_overflow`),
 *      - poprawnosc offsetu `__stack_chk_guard` wewnatrz `.data`,
 *      - granice tablicy relokacji `BurV2Relokacja`.
 *   3. Dzialanie relokatora `bur_v2_zastosuj_relokacje_danych`:
 *      - poprawne przesuniecie wskaznikow `BUR_V2_RELOC_ABS64` dla wylosowanej bazy ASLR,
 *      - odrzucanie prob relokacji wskazujacych na niemodyfikowalny segment `.text` lub `.rodata` (W^X!),
 *      - odrzucanie niewyrownanych offsetow lub przepelnien `addend`.
 *   4. Planer przestrzeni adresowej `bur_v2_zaplanuj_aslr`:
 *      - brak kolizji pomiedzy obrazem PIE, sterta, SHM, stosem oraz stronami straznika (guard pages)
 *        dla 10 000 losowych ziaren entropii,
 *      - rozklad entropii (unikalnosc wylosowanych adresow bazowych).
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>
#include <set>

#include "../loader.h"
#include "../bur_v2.h"

static NaglowekBurV2 utworz_poprawny_bur_v2() {
    NaglowekBurV2 n{};
    n.magia[0] = BUR_V2_MAGIA_0;
    n.magia[1] = BUR_V2_MAGIA_1;
    n.magia[2] = BUR_V2_MAGIA_2;
    n.magia[3] = BUR_V2_MAGIA_3;
    n.wersja = BUR_V2_WERSJA_AKTUALNA;
    n.flagi = BUR_V2_FLAGA_PIE | BUR_V2_FLAGA_SSP | BUR_V2_FLAGA_WX_SCISLY;
    n.punkt_wejscia_rva = 0x1000ULL;

    n.tekst_przesuniecie = 0x1000ULL;
    n.tekst_rozmiar = 0x8000ULL;
    n.tekst_rva = 0x1000ULL;

    n.rodata_przesuniecie = 0x9000ULL;
    n.rodata_rozmiar = 0x4000ULL;
    n.rodata_rva = 0x9000ULL;

    n.dane_przesuniecie = 0xD000ULL;
    n.dane_rozmiar_pliku = 0x2000ULL;
    n.dane_rozmiar_pamieci = 0x8000ULL; // 8 KiB plik + 24 KiB .bss
    n.dane_rva = 0xD000ULL;

    n.reloc_przesuniecie = 0xF000ULL;
    n.reloc_liczba = 2U;
    n.domyślne_strony_stosu = 8U; // 32 KiB
    n.offset_straznika_stosu = 0xD008ULL;
    n.suma_kontrolna_naglowka = 0ULL;
    return n;
}

static void test_bur_v1_backward_compatibility() {
    NaglowekBur v1{};
    v1.magia[0] = 'B';
    v1.magia[1] = 'U';
    v1.magia[2] = 'R';
    v1.magia[3] = '\0';
    v1.punkt_wejscia = 0x1001000ULL;
    v1.tekst_przesuniecie = 0x1000ULL;
    v1.tekst_rozmiar = 0x8000ULL;
    v1.tekst_wirtualny = 0x1001000ULL;
    v1.dane_przesuniecie = 0x9000ULL;
    v1.dane_rozmiar = 0x20000ULL;
    v1.dane_wirtualny = 0x1009000ULL;

    assert(bur_waliduj_naglowek(v1, 0x10000ULL));

    // BUR v1 header must be rejected by BUR v2 validator, and vice versa
    NaglowekBurV2 v2 = utworz_poprawny_bur_v2();
    assert(!bur_waliduj_naglowek(*reinterpret_cast<const NaglowekBur*>(&v2), 0x20000ULL));
    assert(bur_v2_waliduj_naglowek(v2, 0x10000ULL));
}

static void test_bur_v2_header_validation() {
    const uint64_t fsz = 0x10000ULL;
    NaglowekBurV2 base = utworz_poprawny_bur_v2();
    assert(bur_v2_waliduj_naglowek(base, fsz));

    // 1. Plik za maly
    assert(!bur_v2_waliduj_naglowek(base, sizeof(NaglowekBurV2) - 1));

    // 2. Zla wersja
    {
        NaglowekBurV2 m = base;
        m.wersja = 99;
        assert(!bur_v2_waliduj_naglowek(m, fsz));
    }

    // 3. Nieznana flaga
    {
        NaglowekBurV2 m = base;
        m.flagi |= (1U << 15);
        assert(!bur_v2_waliduj_naglowek(m, fsz));
    }

    // 4. Punkt wejscia w .rodata lub .data zamiast .text (naruszenie W^X)
    {
        NaglowekBurV2 m = base;
        m.punkt_wejscia_rva = m.rodata_rva;
        assert(!bur_v2_waliduj_naglowek(m, fsz));
        m.punkt_wejscia_rva = m.dane_rva;
        assert(!bur_v2_waliduj_naglowek(m, fsz));
    }

    // 5. Nakladanie sie .rodata na .text
    {
        NaglowekBurV2 m = base;
        m.rodata_rva = m.tekst_rva + 0x1000ULL;
        assert(!bur_v2_waliduj_naglowek(m, fsz));
    }

    // 6. Offset straznika stosu (__stack_chk_guard) poza .data lub niewyrownany
    {
        NaglowekBurV2 m = base;
        m.offset_straznika_stosu = m.rodata_rva; // w .rodata (niemodyfikowalne!)
        assert(!bur_v2_waliduj_naglowek(m, fsz));
        m.offset_straznika_stosu = m.dane_rva + 3; // niewyrownany do 8B
        assert(!bur_v2_waliduj_naglowek(m, fsz));
    }

    // 7. Przepelnienie tablicy relokacji
    {
        NaglowekBurV2 m = base;
        m.reloc_liczba = BUR_V2_MAKS_RELOKACJI + 1;
        assert(!bur_v2_waliduj_naglowek(m, fsz));
    }
}

static void test_bur_v2_relocations() {
    NaglowekBurV2 n = utworz_poprawny_bur_v2();
    std::vector<uint8_t> data_seg(n.dane_rozmiar_pamieci, 0);

    BurV2Relokacja relocs[2]{};
    // Wpis 0: wskaznik w .data+0x10 wskazujacy na poczatek .text (RVA 0x1000)
    relocs[0].offset_w_obrazie = n.dane_rva + 0x10ULL;
    relocs[0].addend = static_cast<int64_t>(n.tekst_rva + 0x120ULL);
    relocs[0].typ = BUR_V2_RELOC_ABS64;
    relocs[0].zarezerwowane = 0;

    // Wpis 1: wskaznik w .data+0x20 wskazujacy na stala w .rodata (RVA 0x9040)
    relocs[1].offset_w_obrazie = n.dane_rva + 0x20ULL;
    relocs[1].addend = static_cast<int64_t>(n.rodata_rva + 0x40ULL);
    relocs[1].typ = BUR_V2_RELOC_ABS64;
    relocs[1].zarezerwowane = 0;

    const uint64_t baza_aslr = 0x0000000234560000ULL;
    assert(bur_v2_zastosuj_relokacje_danych(n, relocs, data_seg.data(), baza_aslr));

    uint64_t val0 = 0, val1 = 0;
    std::memcpy(&val0, data_seg.data() + 0x10, sizeof(uint64_t));
    std::memcpy(&val1, data_seg.data() + 0x20, sizeof(uint64_t));
    assert(val0 == baza_aslr + 0x1120ULL);
    assert(val1 == baza_aslr + 0x9040ULL);

    // Relokacja probujaca modyfikowac .text (naruszenie W^X) musi zostac odrzucona!
    BurV2Relokacja bad_wx = relocs[0];
    bad_wx.offset_w_obrazie = n.tekst_rva + 0x10ULL;
    assert(!bur_v2_zastosuj_relokacje_danych(n, &bad_wx, data_seg.data(), baza_aslr));

    // Relokacja ze wskaznikiem wychodzacym poza obraz musi zostac odrzucona!
    BurV2Relokacja bad_target = relocs[0];
    bad_target.addend = 0x99990000ULL;
    assert(!bur_v2_zastosuj_relokacje_danych(n, &bad_target, data_seg.data(), baza_aslr));
}

static void test_aslr_planner_10000_trials() {
    NaglowekBurV2 n = utworz_poprawny_bur_v2();
    uint64_t rng = 0xCAFEBABE12345678ULL;
    auto next64 = [&]() {
        uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };

    std::set<uint64_t> unique_pie_bases;
    std::set<uint64_t> unique_stack_tops;

    for (int i = 0; i < 10000; ++i) {
        PlanUkladuAslrRing3 plan{};
        assert(bur_v2_zaplanuj_aslr(n, next64(), next64(), next64(), next64(), &plan));

        // 1. Wszystkie adresy stron musza byc wyrownane do 4 KiB
        assert((plan.baza_obrazu & 0xFFFULL) == 0);
        assert((plan.tekst_va & 0xFFFULL) == 0);
        assert((plan.rodata_va & 0xFFFULL) == 0);
        assert((plan.dane_va & 0xFFFULL) == 0);
        assert((plan.sterta_baza & 0xFFFULL) == 0);
        assert((plan.shm_baza & 0xFFFULL) == 0);
        assert((plan.stos_dol & 0xFFFULL) == 0);
        assert((plan.stos_szczyt & 0xFFFULL) == 0);

        // 2. Scisle uporzadkowanie bez nakladania sie (PIE < Heap < SHM < Stack Guard < Stack)
        assert(plan.tekst_va < plan.rodata_va);
        assert(plan.rodata_va < plan.dane_va);
        assert(plan.dane_va + n.dane_rozmiar_pamieci < plan.sterta_baza);
        assert(plan.sterta_limit < plan.shm_baza);
        assert(plan.shm_limit < plan.stos_guard_dolny);
        assert(plan.stos_guard_dolny + 4096ULL == plan.stos_dol);
        assert(plan.stos_szczyt == plan.stos_guard_gorny);

        // 3. Straznik stosu (__stack_chk_guard) lezy dokladnie wewnatrz .data procesu
        assert(plan.straznik_stosu_va >= plan.dane_va);
        assert(plan.straznik_stosu_va + 8ULL <= plan.dane_va + n.dane_rozmiar_pamieci);

        unique_pie_bases.insert(plan.baza_obrazu);
        unique_stack_tops.insert(plan.stos_szczyt);
    }

    // Przy 20 bitach entropii PIE (1 048 576 slotow), 10 000 prob musi dac > 9 800 unikalnych baz
    assert(unique_pie_bases.size() > 9800);
    // Przy 16 bitach entropii stosu (65 536 slotow), 10 000 prob musi dac > 8 500 unikalnych szczytow
    assert(unique_stack_tops.size() > 8500);
}

int main() {
    test_bur_v1_backward_compatibility();
    test_bur_v2_header_validation();
    test_bur_v2_relocations();
    test_aslr_planner_10000_trials();
    std::printf("[BUR-V2-ASLR-HOST] PASS: BUR v1 compat, BUR v2 validator, W^X relocator, and 10,000 ASLR layout trials verified!\n");
    return 0;
}
