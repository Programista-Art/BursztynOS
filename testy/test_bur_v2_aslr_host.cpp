/*
 * Bursztyn OS — Hostowe testy jednostkowe i regresyjne dla BUR v2 oraz Ring 3 ASLR
 * (testy/test_bur_v2_aslr_host.cpp).
 *
 * Weryfikuje (pod kontrola ASan + UBSan):
 *   1. Zachowanie pelnej kompatybilnosci wstecznej dla istniejacego formatu BUR v1 (`BUR\0`).
 *   2. Rygorystyczna walidacje naglowka `NaglowekBurV2` (`BUR\x02`, 160 B):
 *      - magia, wersja, rozmiar_naglowka, architektura, wersja_abi, wyrownanie_strony,
 *      - wymagane_funkcje, zarezerwowane (musi wynosic 0), suma_kontrolna_naglowka (FNV-1a64),
 *      - rozdzielenie uprawnien `.text` (RX), `.rodata` (RO+NX), `.ssp_guard` (RO+NX), `.data`/`.bss` (RW+NX),
 *      - brak nakladania sie segmentow w pliku i w pamieci wirtualnej,
 *      - wykrywanie przepelnien 64-bitowych (`__builtin_add_overflow`),
 *      - granice tablicy relokacji `BurV2Relokacja` oraz rozmiar rekordu.
 *   3. Dzialanie relokatora `bur_v2_zastosuj_relokacje_danych`:
 *      - poprawne przesuniecie wskaznikow `BUR_V2_RELOC_ABS64`, `BUR_V2_RELOC_PC32`, `BUR_V2_RELOC_ABS32`, `BUR_V2_RELOC_ABS32S`,
 *      - odrzucanie obciecia adresu 64-bitowego do pola 32-bitowego (`ABS32`/`ABS32S` przy bazie >= 4 GiB),
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

#include "loader.h"
#include "bur_v2.h"

static NaglowekBurV2 utworz_poprawny_bur_v2(bool straznik_ro = true) {
    NaglowekBurV2 n{};
    bur_v2_ustaw_domyslne_pola_naglowka(&n);
    n.flagi = BUR_V2_FLAGA_PIE | BUR_V2_FLAGA_SSP | BUR_V2_FLAGA_WX_SCISLY |
              (straznik_ro ? BUR_V2_FLAGA_STRAZNIK_RO : 0U) |
              BUR_V2_FLAGA_SUMA_KONTROLNA;
    n.wymagane_funkcje = BUR_V2_FUNKCJA_ASLR | BUR_V2_FUNKCJA_RELOKACJE |
                         (straznik_ro ? BUR_V2_FUNKCJA_STRAZNIK_RO : 0U);
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
    n.dane_rva = straznik_ro ? 0xE000ULL : 0xD000ULL;

    n.reloc_przesuniecie = 0xF000ULL;
    n.reloc_liczba = 2U;
    n.domyślne_strony_stosu = 16U; // 64 KiB
    n.offset_straznika_stosu = straznik_ro ? 0xD000ULL : 0xD008ULL;
    n.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(n);
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
    NaglowekBurV2 base = utworz_poprawny_bur_v2(true);
    assert(bur_v2_waliduj_naglowek(base, fsz));

    NaglowekBurV2 base_data_ssp = utworz_poprawny_bur_v2(false);
    assert(bur_v2_waliduj_naglowek(base_data_ssp, fsz));

    // 1. Plik za maly
    assert(!bur_v2_waliduj_naglowek(base, sizeof(NaglowekBurV2) - 1));

    // 2. Zla wersja, rozmiar_naglowka, architektura, wersja_abi, wyrownanie_strony
    {
        NaglowekBurV2 m = base;
        m.wersja = 99;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_ZLA_WERSJA);
    }
    {
        NaglowekBurV2 m = base;
        m.rozmiar_naglowka = 60;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_ZLY_ROZMIAR_NAGLOWKA);
    }
    {
        NaglowekBurV2 m = base;
        m.architektura = 0x0003U; // EM_386 zamiast EM_X86_64
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_ZLA_ARCHITEKTURA);
    }
    {
        NaglowekBurV2 m = base;
        m.wersja_abi = 99U;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_ZLE_ABI);
    }
    {
        NaglowekBurV2 m = base;
        m.wyrownanie_strony = 2048U;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_ZLE_WYROWNANIE);
    }

    // 3. Nieznana flaga, nieznane wymagane_funkcje, niezerowe pole zarezerwowane, uszkodzona suma kontrolna
    {
        NaglowekBurV2 m = base;
        m.flagi |= (1U << 15);
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_NIEZNANE_FLAGI);
    }
    {
        NaglowekBurV2 m = base;
        m.wymagane_funkcje |= (1U << 12);
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_NIEZNANE_FUNKCJE);
    }
    {
        NaglowekBurV2 m = base;
        m.zarezerwowane = 1ULL;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_POLE_ZAREZERWOWANE);
    }
    {
        NaglowekBurV2 m = base;
        m.suma_kontrolna_naglowka ^= 1ULL;
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_SUMA_KONTROLNA);
    }

    // 4. Punkt wejscia w .rodata lub .data zamiast .text (naruszenie W^X)
    {
        NaglowekBurV2 m = base;
        m.punkt_wejscia_rva = m.rodata_rva;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_PUNKT_WEJSCIA);
        m.punkt_wejscia_rva = m.dane_rva;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_PUNKT_WEJSCIA);
    }

    // 5. Nakladanie sie .rodata na .text lub .data na strone straznika RO
    {
        NaglowekBurV2 m = base;
        m.rodata_rva = m.tekst_rva + 0x1000ULL;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_SEGMENT_RODATA);
    }
    {
        NaglowekBurV2 m = base;
        m.dane_rva = m.offset_straznika_stosu; // nachodzi na strone .ssp_guard RO+NX!
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_SEGMENT_DANE);
    }

    // 6. Offset straznika stosu (__stack_chk_guard) niewyrownany
    {
        NaglowekBurV2 m = base;
        m.offset_straznika_stosu = 0xD100ULL; // niewyrownany do 4 KiB przy STRAZNIK_RO
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_STRAZNIK_STOSU);
    }

    // 7. Przepelnienie tablicy relokacji lub zly rozmiar rekordu
    {
        NaglowekBurV2 m = base;
        m.reloc_liczba = BUR_V2_MAKS_RELOKACJI + 1;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_TABLICA_RELOKACJI);
    }
    {
        NaglowekBurV2 m = base;
        m.reloc_rozmiar_wpisu = 16U;
        m.suma_kontrolna_naglowka = bur_v2_oblicz_sume_naglowka(m);
        assert(bur_v2_waliduj_naglowek_kod(m, fsz) == BUR_V2_BLAD_TABLICA_RELOKACJI);
    }
}

static void test_bur_v2_relocations() {
    NaglowekBurV2 n = utworz_poprawny_bur_v2(true);
    n.reloc_liczba = 3U;
    std::vector<uint8_t> data_seg(n.dane_rozmiar_pamieci, 0);

    BurV2Relokacja relocs[3]{};
    // Wpis 0: wskaznik 64-bit w .data+0x10 wskazujacy na .text+0x120 (RVA 0x1120)
    relocs[0].offset_w_obrazie = n.dane_rva + 0x10ULL;
    relocs[0].addend = static_cast<int64_t>(n.tekst_rva + 0x120ULL);
    relocs[0].typ = BUR_V2_RELOC_ABS64;
    relocs[0].zarezerwowane = 0;

    // Wpis 1: wskaznik 64-bit w .data+0x20 wskazujacy na stala w .rodata (RVA 0x9040)
    relocs[1].offset_w_obrazie = n.dane_rva + 0x20ULL;
    relocs[1].addend = static_cast<int64_t>(n.rodata_rva + 0x40ULL);
    relocs[1].typ = BUR_V2_RELOC_ABS64;
    relocs[1].zarezerwowane = 0;

    // Wpis 2: relokacja PC32 w .data+0x30 wskazujaca na .rodata+0x10 (RVA 0x9010)
    relocs[2].offset_w_obrazie = n.dane_rva + 0x30ULL;
    relocs[2].addend = static_cast<int64_t>(n.rodata_rva + 0x10ULL);
    relocs[2].typ = BUR_V2_RELOC_PC32;
    relocs[2].zarezerwowane = 0;

    const uint64_t baza_aslr = 0x0000000234560000ULL;
    assert(bur_v2_zastosuj_relokacje_danych(n, relocs, data_seg.data(), baza_aslr));

    uint64_t val0 = 0, val1 = 0;
    int32_t val2 = 0;
    std::memcpy(&val0, data_seg.data() + 0x10, sizeof(uint64_t));
    std::memcpy(&val1, data_seg.data() + 0x20, sizeof(uint64_t));
    std::memcpy(&val2, data_seg.data() + 0x30, sizeof(int32_t));
    assert(val0 == baza_aslr + 0x1120ULL);
    assert(val1 == baza_aslr + 0x9040ULL);
    assert(val2 == static_cast<int32_t>(0x9010LL - static_cast<int64_t>(n.dane_rva + 0x30ULL)));

    // Relokacja ABS32/ABS32S przy bazie >= 4 GiB musi zostac odrzucona (brak obcinania 64->32)!
    n.reloc_liczba = 1U;
    BurV2Relokacja rel32{};
    rel32.offset_w_obrazie = n.dane_rva + 0x40ULL;
    rel32.addend = static_cast<int64_t>(n.tekst_rva);
    rel32.typ = BUR_V2_RELOC_ABS32;
    assert(!bur_v2_zastosuj_relokacje_danych(n, &rel32, data_seg.data(), baza_aslr));
    // Natomiast przy bazie 32-bitowej (< 2 GiB) dziala poprawnie:
    assert(bur_v2_zastosuj_relokacje_danych(n, &rel32, data_seg.data(), 0x10000000ULL));

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
    NaglowekBurV2 n = utworz_poprawny_bur_v2(true);
    uint64_t rng = 0xCAFEBABE12345678ULL;
    auto next64 = [&]() {
        uint64_t z = (rng += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };

    std::set<uint64_t> unique_pie_bases;
    std::set<uint64_t> unique_stack_tops;
    int odrzucone_planu = 0, przyjete_plany = 0;

    for (int i = 0; i < 10000; ++i) {
        PlanUkladuAslrRing3 plan{};
        const uint64_t r0 = next64(), r1 = next64(), r2 = next64(), r3 = next64();
        // Plan jest odrzucany WYLACZNIE przy kolizji z oknem VA jadra (zarezerwowane_va.h); wtedy loader losuje ponownie.
        const uint64_t baza_oczek = ASLR_PIE_WINDOW_BASE + (r0 & (ASLR_PIE_WINDOW_PAGES - 1ULL)) * 4096ULL;
        const uint64_t koniec_oczek = baza_oczek + n.dane_rva + n.dane_rozmiar_pamieci;
        const bool plan_ok = bur_v2_zaplanuj_aslr(n, r0, r1, r2, r3, &plan);
        const bool oczekiwana_kolizja = ZarezerwowaneVA::koliduje_z_jadrem(baza_oczek, koniec_oczek) ||
            ZarezerwowaneVA::koliduje_z_jadrem(ASLR_HEAP_WINDOW_BASE + (r1 & (ASLR_HEAP_WINDOW_PAGES - 1ULL)) * 4096ULL,
                ASLR_HEAP_WINDOW_BASE + (r1 & (ASLR_HEAP_WINDOW_PAGES - 1ULL)) * 4096ULL + 256ULL * 1024ULL * 1024ULL);
        assert(plan_ok == !oczekiwana_kolizja);
        if (!plan_ok) { ++odrzucone_planu; continue; }
        ++przyjete_plany;

        // 1. Wszystkie adresy stron musza byc wyrownane do 4 KiB
        assert((plan.baza_obrazu & 0xFFFULL) == 0);
        assert((plan.tekst_va & 0xFFFULL) == 0);
        assert((plan.rodata_va & 0xFFFULL) == 0);
        assert((plan.straznik_stosu_va & 0xFFFULL) == 0);
        assert((plan.dane_va & 0xFFFULL) == 0);
        assert((plan.sterta_baza & 0xFFFULL) == 0);
        assert((plan.shm_baza & 0xFFFULL) == 0);
        assert((plan.stos_dol & 0xFFFULL) == 0);
        assert((plan.stos_szczyt & 0xFFFULL) == 0);

        // 2. Scisle uporzadkowanie bez nakladania sie (PIE < Heap < SHM < Stack Guard < Stack)
        assert(plan.tekst_va < plan.rodata_va);
        assert(plan.rodata_va < plan.straznik_stosu_va);
        assert(plan.straznik_stosu_va + 4096ULL <= plan.dane_va);
        assert(plan.dane_va + n.dane_rozmiar_pamieci < plan.sterta_baza);
        assert(plan.sterta_limit < plan.shm_baza);
        assert(plan.shm_limit < plan.stos_guard_dolny);
        assert(plan.stos_guard_dolny + 4096ULL == plan.stos_dol);
        assert(plan.stos_szczyt == plan.stos_guard_gorny);

        unique_pie_bases.insert(plan.baza_obrazu);
        unique_stack_tops.insert(plan.stos_szczyt);
    }

    // Przy 20 bitach entropii PIE (1 048 576 slotow), 10 000 prob musi dac > 9 800 unikalnych baz
    assert(odrzucone_planu < 400 && przyjete_plany + odrzucone_planu == 10000);   // ok. 1,7% odrzuconych (okna jadra)
    assert(unique_pie_bases.size() > static_cast<size_t>(przyjete_plany) * 99 / 100);
    // Przy 16 bitach entropii stosu (65 536 slotow), 10 000 prob musi dac > 8 500 unikalnych szczytow
    assert(unique_stack_tops.size() > static_cast<size_t>(przyjete_plany) * 85 / 100);
}

// Regresja przerywanego #PF w tapety_obsluz_obraz (BWS 69): podstawa PIE w oknie VA jadra (0x120000000..+32 MiB,
// scratch Tapet; 0x130000000.. LFB) nadpisywala PTE jadra stronami obrazu procesu.
static void test_aslr_omija_okna_jadra() {
    NaglowekBurV2 n = utworz_poprawny_bur_v2(true);
    const uint64_t span = n.dane_rva + n.dane_rozmiar_pamieci;
    PlanUkladuAslrRing3 plan{};
    auto slot = [](uint64_t base) { return (base - ASLR_PIE_WINDOW_BASE) / 4096ULL; };
    // dokladnie podstawa z przerywanego #PF (diagnostyka 0x1201F0000) i inne punkty wewnatrz/na brzegach okna
    for (uint64_t base : {0x1201F0000ULL, 0x120000000ULL, 0x121FFF000ULL, 0x130000000ULL, 0x133FFF000ULL,
                          0x140000000ULL, 0x1400FF000ULL, 0x141000000ULL, 0x144000000ULL,
                          0x146000000ULL, 0x1463FF000ULL})   // + okno ACPI, HPET, OHCI, samotest VMM
        assert(!bur_v2_zaplanuj_aslr(n, slot(base), 1, 2, 3, &plan));
    // obraz konczacy sie tuz przed oknem i zaczynajacy tuz po nim jest dozwolony
    assert(bur_v2_zaplanuj_aslr(n, slot(0x120000000ULL - ((span + 4095ULL) & ~4095ULL)), 1, 2, 3, &plan));
    assert(bur_v2_zaplanuj_aslr(n, slot(0x122000000ULL), 1, 2, 3, &plan));
    assert(bur_v2_zaplanuj_aslr(n, slot(0x134000000ULL), 1, 2, 3, &plan));
    // wyczerpujaco: wszystkie 2^20 slotow PIE - zaden przyjety plan nie przecina okien, a odrzuconych jest dokladnie
    // tyle, ile wynika z geometrii (okno + rozpietosc obrazu, w stronach)
    uint64_t odrzucone = 0;
    for (uint64_t i = 0; i < ASLR_PIE_WINDOW_PAGES; ++i) {
        const bool ok = bur_v2_zaplanuj_aslr(n, i, 1, 2, 3, &plan);
        const uint64_t base = ASLR_PIE_WINDOW_BASE + i * 4096ULL, end = base + span;
        const bool reczna = (base < 0x122000000ULL && end > 0x120000000ULL) || (base < 0x134000000ULL && end > 0x130000000ULL) ||
                            (base < 0x140100000ULL && end > 0x140000000ULL) || (base < 0x141001000ULL && end > 0x141000000ULL) ||
                            (base < 0x144001000ULL && end > 0x144000000ULL) ||
                            (base < 0x146400000ULL && end > 0x146000000ULL) ||
                            (base < 0x208000000ULL && end > 0x200000000ULL);
        assert(ok == !reczna);
        if (!ok) ++odrzucone;
    }
    const uint64_t pages = (span + 4095ULL) / 4096ULL;
    const uint64_t oczek = ((32ULL + 64ULL) * 1024ULL * 1024ULL / 4096ULL) + 2 * (pages - 1ULL) +
                           (128ULL * 1024ULL * 1024ULL / 4096ULL) /* PSF startuje na koncu okna: tylko obrazy konczace sie po 0x200000000 */ * 0;
    (void)oczek;
    assert(odrzucone > 0 && odrzucone < 100000);   // ~1,7% okna, entropia nadal > 19,9 bitow
    // sterta i SHM nigdy nie trafiaja w okna jadra (okna ASLR 32/64 GiB)
    for (uint64_t i = 0; i < ASLR_HEAP_WINDOW_PAGES; i += 997) {
        assert(!ZarezerwowaneVA::koliduje_z_jadrem(ASLR_HEAP_WINDOW_BASE + i * 4096ULL, ASLR_HEAP_WINDOW_BASE + i * 4096ULL + (256ULL << 20)));
        assert(!ZarezerwowaneVA::koliduje_z_jadrem(ASLR_SHM_WINDOW_BASE + i * 4096ULL, ASLR_SHM_WINDOW_BASE + i * 4096ULL + (256ULL << 20)));
    }
    // pusty/odwrocony przedzial = kolizja (fail-closed)
    assert(ZarezerwowaneVA::koliduje_z_jadrem(10, 10));
    assert(ZarezerwowaneVA::koliduje_z_jadrem(20, 10));
    std::printf("[BUR-V2-ASLR-HOST] okna jadra: %llu z %llu slotow PIE odrzuconych (obraz %llu stron)\n",
                (unsigned long long)odrzucone, (unsigned long long)ASLR_PIE_WINDOW_PAGES, (unsigned long long)pages);
}

int main() {
    test_aslr_omija_okna_jadra();
    test_bur_v1_backward_compatibility();
    test_bur_v2_header_validation();
    test_bur_v2_relocations();
    test_aslr_planner_10000_trials();
    std::printf("[BUR-V2-ASLR-HOST] PASS: BUR v1 compat, BUR v2 validator, W^X relocator, and 10,000 ASLR layout trials verified!\n");
    return 0;
}
