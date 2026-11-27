/*
 * BRB - testy hosta Fazy 4A (ASan + UBSan): zakresy przydzialu, ladowanie segmentow, weryfikacja obrazu,
 * walidacja migawki mapy pamieci, mutacje i ograniczony fuzzing wejsc ladowania.
 *
 * Pamiec fizyczna jest SYMULOWANA: wektor z obszarami straznikow przed i za symulowanym zakresem fizycznym.
 * Test nigdy nie dereferencjonuje adresu fizycznego - tlumacz zwraca wskaznik tylko dla zakresu symulacji.
 * Po kazdej operacji sprawdzane jest, ze bajty poza POSIADANYMI zakresami (i straznicy) sa nienaruszone.
 *
 * Uzycie: test-brb-ladowanie [jadro_produkcyjne]
 */
#include "testy/brb_elf_fixture.h"

#include "rozruch/brb/wspolne/elf64.h"
#include "rozruch/brb/wspolne/ladowanie.h"
#include "rozruch/brb/wspolne/mapa_pamieci.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using brb::BladLadowania;
using brb::BladMapy;

namespace {

int g_sprawdzenia = 0, g_bledy = 0;
#define SPRAWDZ(w)                                                                 \
    do {                                                                           \
        ++g_sprawdzenia;                                                           \
        if (!(w)) {                                                                \
            ++g_bledy;                                                             \
            std::printf("  BLAD %s:%d: %s\n", __FILE__, __LINE__, #w);             \
        }                                                                          \
    } while (0)

constexpr uint8_t WZOR_OBCY = 0xCC;      // pamiec nieposiadana
constexpr uint8_t WZOR_STRAZNIKA = 0xEE; // obszary poza symulacja
constexpr uint64_t STRAZNIK = 4096;

struct PamiecSymulowana {
    uint64_t baza, rozmiar;
    std::vector<uint8_t> bajty;   // [STRAZNIK][rozmiar][STRAZNIK]
    PamiecSymulowana(uint64_t b, uint64_t r) : baza(b), rozmiar(r), bajty(r + 2 * STRAZNIK, WZOR_STRAZNIKA) {
        std::memset(bajty.data() + STRAZNIK, WZOR_OBCY, r);
    }
    static uint8_t* tlumacz(void* k, uint64_t adres, uint64_t dl) {
        auto* p = static_cast<PamiecSymulowana*>(k);
        if (adres < p->baza || dl > p->rozmiar || adres - p->baza > p->rozmiar - dl) return nullptr;
        return p->bajty.data() + STRAZNIK + (adres - p->baza);
    }
    brb::DostepFizyczny dostep() { return brb::DostepFizyczny{tlumacz, this}; }
    uint8_t* fiz(uint64_t a) { return bajty.data() + STRAZNIK + (a - baza); }
    bool straznicy_ok() const {
        for (uint64_t i = 0; i < STRAZNIK; ++i)
            if (bajty[i] != WZOR_STRAZNIKA || bajty[STRAZNIK + rozmiar + i] != WZOR_STRAZNIKA) return false;
        return true;
    }
    // Kazdy bajt poza posiadanymi zakresami ma nadal wzorzec obcy.
    bool obce_nienaruszone(const brb::ZakresyPrzydzialu& z) const {
        for (uint64_t off = 0; off < rozmiar; ++off) {
            const uint64_t a = baza + off;
            bool posiadany = false;
            for (uint32_t i = 0; i < z.liczba && !posiadany; ++i)
                posiadany = a >= z.zakresy[i].poczatek && a < z.zakresy[i].poczatek + z.zakresy[i].strony * 4096;
            if (!posiadany && bajty[STRAZNIK + off] != WZOR_OBCY) return false;
        }
        return true;
    }
    bool wszystko_obce() const {
        for (uint64_t off = 0; off < rozmiar; ++off) if (bajty[STRAZNIK + off] != WZOR_OBCY) return false;
        return straznicy_ok();
    }
};

brb::ZrodloJadra zrodlo_z(const std::vector<uint8_t>& d) {
    return brb::ZrodloJadra{d.data(), d.size(), 0x7F0000000000ull, (d.size() + 4095) / 4096 * 4096};
}

bool plan_z(const std::vector<uint8_t>& d, const brb::PolitykaElf& pol, brb::PlanLadowania* p) {
    return brb::zbuduj_plan_ladowania(d.data(), d.size(), pol, p).blad == brb::BladElf::Brak;
}

// Pelny cykl: plan -> zakresy -> ladowanie -> weryfikacja; kontrola danych, BSS, wypelnienia, obcych bajtow.
void sprawdz_pelny_cykl(const std::vector<uint8_t>& d, const brb::PolitykaElf& pol, PamiecSymulowana& m, const char* nazwa) {
    brb::PlanLadowania p;
    SPRAWDZ(plan_z(d, pol, &p));
    brb::ZakresyPrzydzialu z;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(p, &z) == BladLadowania::Brak);
    brb::RaportLadowania r;
    const brb::ZrodloJadra zr = zrodlo_z(d);
    SPRAWDZ(brb::zaladuj_segmenty(p, pol, zr, z, m.dostep(), &r) == BladLadowania::Brak);
    SPRAWDZ(brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), &r) == BladLadowania::Brak);
    uint64_t z_pliku = 0, bss = 0;
    for (uint32_t i = 0; i < p.liczba_segmentow; ++i) {
        const brb::SegmentPlanu& s = p.segmenty[i];
        SPRAWDZ(std::memcmp(m.fiz(s.adres_fizyczny), d.data() + s.przesuniecie_pliku, s.bajty_z_pliku) == 0);
        for (uint64_t k = 0; k < s.bajty_zerowane; ++k)
            if (m.fiz(s.adres_fizyczny + s.bajty_z_pliku)[k] != 0) { SPRAWDZ(false); break; }
        SPRAWDZ(r.segmenty[i].dane_ok && r.segmenty[i].bss_ok);
        z_pliku += s.bajty_z_pliku;
        bss += s.bajty_zerowane;
    }
    SPRAWDZ(r.bajty_skopiowane == z_pliku && r.bajty_bss == bss);
    SPRAWDZ(r.bajty_wypelnienia + z_pliku + bss == z.strony_lacznie * 4096);
    SPRAWDZ(m.obce_nienaruszone(z) && m.straznicy_ok());
    std::printf("[test] pelny cykl %s: segmenty=%u zakresy=%u strony=%llu z_pliku=%llu bss=%llu wypelnienie=%llu\n", nazwa,
                p.liczba_segmentow, z.liczba, (unsigned long long)z.strony_lacznie, (unsigned long long)z_pliku,
                (unsigned long long)bss, (unsigned long long)r.bajty_wypelnienia);
}

void test_zakresy() {
    std::printf("[test] zakresy przydzialu\n");
    brb::PlanLadowania p;
    const auto o = fixture::jadro_minimalne();
    SPRAWDZ(plan_z(o.b, brb::POLITYKA_JADRA_BURSZTYN, &p));
    brb::ZakresyPrzydzialu z;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(p, &z) == BladLadowania::Brak);
    SPRAWDZ(z.liczba == 3 && z.strony_lacznie == 4);
    SPRAWDZ(z.zakresy[0].poczatek == 0x100000 && z.zakresy[0].strony == 1);
    SPRAWDZ(z.zakresy[2].poczatek == 0x102000 && z.zakresy[2].strony == 2);
    // Wspolna strona (te same prawa) -> jeden scalony zakres, strona przydzielana raz.
    fixture::Obraz s = o;
    s.segment(2, 1, brb::ELF_PF_R | brb::ELF_PF_X, 0x2100, 0x101100, 0x101100, 0x80, 0x1F00, 0x1000);
    SPRAWDZ(plan_z(s.b, brb::POLITYKA_JADRA_BURSZTYN, &p));
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(p, &z) == BladLadowania::Brak && z.liczba == 2 && z.strony_lacznie == 3 &&
            z.zakresy[1].poczatek == 0x101000 && z.zakresy[1].strony == 2);
    // Plan uszkodzony w pamieci (strona niewyrownana / zerowa) -> odrzucenie.
    SPRAWDZ(plan_z(o.b, brb::POLITYKA_JADRA_BURSZTYN, &p));
    brb::PlanLadowania zly = p;
    zly.segmenty[1].strona_poczatek += 1;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(zly, &z) == BladLadowania::ZleZakresy);
    zly = p;
    zly.segmenty[1].strona_koniec = zly.segmenty[1].strona_poczatek;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(zly, &z) == BladLadowania::ZleZakresy);
    zly = p;
    zly.liczba_segmentow = 0;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(zly, &z) == BladLadowania::PustyPlan);
    zly = p;
    zly.liczba_segmentow = 200;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(zly, &z) == BladLadowania::ZaDuzoZakresow);
    zly = p;   // zakres konczacy sie za 2^64
    zly.segmenty[2].strona_poczatek = 0xFFFFFFFFFFFFF000ull;
    zly.segmenty[2].strona_koniec = 0xFFFFFFFFFFFFF000ull + 0x1000;   // przepelnienie w wyrazeniu -> 0
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(zly, &z) != BladLadowania::Brak);
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(p, nullptr) == BladLadowania::BrakDanych);
}

void test_ladowanie_poprawne(const std::vector<uint8_t>* produkcyjne) {
    std::printf("[test] ladowanie i weryfikacja (poprawne)\n");
    {
        PamiecSymulowana m(0x100000, 0x100000);
        sprawdz_pelny_cykl(fixture::jadro_minimalne().b, brb::POLITYKA_JADRA_BURSZTYN, m, "syntetyczne");
    }
    {   // Segment w calosci zerowany (p_filesz = 0) i segment niewyrownany do strony (p_align = 16).
        fixture::Obraz o = fixture::jadro_minimalne();
        o.w16(56, 4);
        o.segment(3, 1, brb::ELF_PF_R | brb::ELF_PF_W, 0x3000, 0x105000, 0x105000, 0, 0x3000, 0x1000);
        o.segment(2, 1, brb::ELF_PF_R | brb::ELF_PF_W, 0x3010, 0x102010, 0x102010, 0x70, 0x1FF0, 0x10);
        PamiecSymulowana m(0x100000, 0x100000);
        sprawdz_pelny_cykl(o.b, brb::POLITYKA_JADRA_BURSZTYN, m, "BSS-only+niewyrownany");
    }
    {   // Wspolna strona przy tych samych prawach: drugi segment nie kasuje pierwszego.
        fixture::Obraz o = fixture::jadro_minimalne();
        o.segment(2, 1, brb::ELF_PF_R | brb::ELF_PF_X, 0x2100, 0x101100, 0x101100, 0x80, 0x1F00, 0x1000);
        for (int i = 0; i < 0x80; ++i) o.b[0x2100 + i] = uint8_t(0x40 + i);
        PamiecSymulowana m(0x100000, 0x100000);
        sprawdz_pelny_cykl(o.b, brb::POLITYKA_JADRA_BURSZTYN, m, "wspolna-strona");
    }
    {   // Adresy powyzej 4 GiB (polityka ogolna): pelne 64 bity w zakresach i tlumaczu.
        fixture::Obraz o = fixture::jadro_minimalne();
        for (unsigned i = 0; i < 3; ++i) {
            o.w64(fixture::ph(i) + fixture::P_VADDR, 0x140000000ull + 0x1000ull * i);
            o.w64(fixture::ph(i) + fixture::P_PADDR, 0x140000000ull + 0x1000ull * i);
        }
        o.w64(24, 0x140001010ull);
        PamiecSymulowana m(0x140000000ull, 0x10000);
        sprawdz_pelny_cykl(o.b, brb::POLITYKA_OGOLNA, m, "powyzej-4GiB");
    }
    if (produkcyjne) {   // Jadro produkcyjne: 0x100000-0xD6F000 w symulacji 1-16 MiB.
        PamiecSymulowana m(0x100000, 0xF00000);
        sprawdz_pelny_cykl(*produkcyjne, brb::POLITYKA_JADRA_BURSZTYN, m, "jadro-produkcyjne");
    } else {
        std::printf("[test] jadro produkcyjne: NIE URUCHOMIONO (nie podano pliku)\n");
    }
}

void test_ladowanie_bledne() {
    std::printf("[test] ladowanie: odrzucenia bez zapisu i bledy weryfikacji\n");
    const std::vector<uint8_t> d = fixture::jadro_minimalne().b;
    brb::PlanLadowania p;
    SPRAWDZ(plan_z(d, brb::POLITYKA_JADRA_BURSZTYN, &p));
    brb::ZakresyPrzydzialu z;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(p, &z) == BladLadowania::Brak);
    const brb::PolitykaElf& J = brb::POLITYKA_JADRA_BURSZTYN;
    brb::RaportLadowania r;
    struct Przypadek {
        const char* nazwa;
        void (*zmien)(brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu&);
        BladLadowania oczekiwany;
    };
    const Przypadek lista[] = {
        {"plan zmieniony (adres)", [](brb::PlanLadowania& pl, brb::ZrodloJadra&, brb::ZakresyPrzydzialu&) { pl.segmenty[1].adres_fizyczny += 0x10; }, BladLadowania::NiespojnyPlan},
        {"plan zmieniony (filesz)", [](brb::PlanLadowania& pl, brb::ZrodloJadra&, brb::ZakresyPrzydzialu&) { pl.segmenty[2].bajty_z_pliku += 1; }, BladLadowania::NiespojnyPlan},
        {"plan: indeks wejscia poza tablica", [](brb::PlanLadowania& pl, brb::ZrodloJadra&, brb::ZakresyPrzydzialu&) { pl.segment_wejscia = 999; }, BladLadowania::NiespojnyPlan},
        {"zrodlo obciete", [](brb::PlanLadowania&, brb::ZrodloJadra& zr, brb::ZakresyPrzydzialu&) { zr.rozmiar -= 1; }, BladLadowania::ZrodloNiezgodne},
        {"zrodlo wieksze niz bufor", [](brb::PlanLadowania&, brb::ZrodloJadra& zr, brb::ZakresyPrzydzialu&) { zr.rozmiar_bufora = 4096; }, BladLadowania::ZrodloNiezgodne},
        {"bufor zrodla nachodzi na cel", [](brb::PlanLadowania&, brb::ZrodloJadra& zr, brb::ZakresyPrzydzialu&) { zr.adres_fizyczny = 0x101000; }, BladLadowania::ZrodloNakladaSieNaCel},
        {"przepelnienie bufora zrodla", [](brb::PlanLadowania&, brb::ZrodloJadra& zr, brb::ZakresyPrzydzialu&) { zr.adres_fizyczny = ~0ull - 100; }, BladLadowania::PrzepelnienieArytmetyki},
        {"cel obciety (zakres krotszy)", [](brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu& zk) { zk.zakresy[2].strony = 1; }, BladLadowania::CelPozaPrzydzialem},
        {"brak zakresu celu", [](brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu& zk) { zk.liczba = 2; }, BladLadowania::CelPozaPrzydzialem},
        {"zakresy nakladajace sie", [](brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu& zk) { zk.zakresy[1].poczatek = 0x100000; }, BladLadowania::ZakresyNakladajaSie},
        {"zero stron", [](brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu& zk) { zk.zakresy[0].strony = 0; }, BladLadowania::ZleZakresy},
        {"adres niewyrownany", [](brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu& zk) { zk.zakresy[0].poczatek += 8; }, BladLadowania::ZleZakresy},
        {"przepelnienie zakresu", [](brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu& zk) { zk.zakresy[2].poczatek = 0xFFFFFFFFFFFFF000ull; zk.zakresy[2].strony = 2; }, BladLadowania::ZleZakresy},
        {"za duzo zakresow", [](brb::PlanLadowania&, brb::ZrodloJadra&, brb::ZakresyPrzydzialu& zk) { zk.liczba = 17; }, BladLadowania::ZaDuzoZakresow},
        {"brak zrodla", [](brb::PlanLadowania&, brb::ZrodloJadra& zr, brb::ZakresyPrzydzialu&) { zr.dane = nullptr; }, BladLadowania::BrakDanych},
    };
    for (const Przypadek& c : lista) {
        brb::PlanLadowania pl = p;
        brb::ZrodloJadra zr = zrodlo_z(d);
        brb::ZakresyPrzydzialu zk = z;
        c.zmien(pl, zr, zk);
        PamiecSymulowana m(0x100000, 0x100000);
        const BladLadowania b = brb::zaladuj_segmenty(pl, J, zr, zk, m.dostep(), &r);
        if (b != c.oczekiwany) std::printf("  przypadek '%s': wynik=%d (%s)\n", c.nazwa, int(b), brb::opis_bledu_ladowania(b));
        SPRAWDZ(b == c.oczekiwany);
        SPRAWDZ(m.wszystko_obce());   // odrzucenie PRZED jakimkolwiek zapisem
    }
    {   // Regresja: plan rowny co do pol, ale z innymi bajtami dopelnienia (po bool mb2.obecny) jest ZGODNY;
        // zmiana pola mb2 jest wykrywana. (Wczesniej memcmp calej struktury dawal falszywe BRB-E303.)
        brb::PlanLadowania pl = p;
        reinterpret_cast<uint8_t*>(&pl)[offsetof(brb::PlanLadowania, mb2) + 1] ^= 0xA5;
        reinterpret_cast<uint8_t*>(&pl)[offsetof(brb::PlanLadowania, liczba_segmentow) + 4] ^= 0x5A;
        PamiecSymulowana m(0x100000, 0x100000);
        SPRAWDZ(brb::zaladuj_segmenty(pl, J, zrodlo_z(d), z, m.dostep(), &r) == BladLadowania::Brak);
        pl = p;
        pl.mb2.liczba_tagow += 1;
        PamiecSymulowana m2(0x100000, 0x100000);
        SPRAWDZ(brb::zaladuj_segmenty(pl, J, zrodlo_z(d), z, m2.dostep(), &r) == BladLadowania::NiespojnyPlan && m2.wszystko_obce());
    }
    {   // Tlumacz odmawia dostepu do posiadanego zakresu (np. poza symulacja) - brak zapisu.
        PamiecSymulowana m(0x100000, 0x2000);   // tylko 2 z 4 stron dostepne
        SPRAWDZ(brb::zaladuj_segmenty(p, J, zrodlo_z(d), z, m.dostep(), &r) == BladLadowania::BrakDostepuDoPamieci);
        SPRAWDZ(m.wszystko_obce());
    }
    // Bledy weryfikacji: poprawne ladowanie, potem wstrzykniete uszkodzenie.
    struct Uszkodzenie { const char* nazwa; uint64_t adres; BladLadowania oczekiwany; };
    const Uszkodzenie usz[] = {
        {"bajt danych .text", 0x101010, BladLadowania::WeryfikacjaDanych},
        {"bajt naglowka MB2", 0x100004, BladLadowania::WeryfikacjaDanych},
        {"bajt BSS", 0x102800, BladLadowania::WeryfikacjaBss},
        {"ostatni bajt BSS", 0x103FFF, BladLadowania::WeryfikacjaBss},
        {"wypelnienie po .boot", 0x100030, BladLadowania::WeryfikacjaWypelnienia},
        {"wypelnienie po .text", 0x101FFF, BladLadowania::WeryfikacjaWypelnienia},
    };
    for (const Uszkodzenie& u : usz) {
        PamiecSymulowana m(0x100000, 0x100000);
        const brb::ZrodloJadra zr = zrodlo_z(d);
        SPRAWDZ(brb::zaladuj_segmenty(p, J, zr, z, m.dostep(), &r) == BladLadowania::Brak);
        *m.fiz(u.adres) ^= 0x5A;
        const BladLadowania b = brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), &r);
        if (b != u.oczekiwany || r.adres_bledu != u.adres)
            std::printf("  uszkodzenie '%s': wynik=%d adres=0x%llx\n", u.nazwa, int(b), (unsigned long long)r.adres_bledu);
        SPRAWDZ(b == u.oczekiwany && r.adres_bledu == u.adres);
    }
    {   // Weryfikacja z planem, w ktorym wejscie wskazuje segment niewykonywalny.
        PamiecSymulowana m(0x100000, 0x100000);
        const brb::ZrodloJadra zr = zrodlo_z(d);
        SPRAWDZ(brb::zaladuj_segmenty(p, J, zr, z, m.dostep(), &r) == BladLadowania::Brak);
        brb::PlanLadowania pl = p;
        pl.segment_wejscia = 2;
        SPRAWDZ(brb::weryfikuj_zaladowany_obraz(pl, zr, z, m.dostep(), &r) == BladLadowania::WejscieNiewykonywalne);
        pl.segment_wejscia = 77;
        SPRAWDZ(brb::weryfikuj_zaladowany_obraz(pl, zr, z, m.dostep(), &r) == BladLadowania::WejscieNiewykonywalne);
        SPRAWDZ(brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), nullptr) == BladLadowania::BrakDanych);
    }
    for (int k = 0; k <= int(BladLadowania::WejscieNiewykonywalne); ++k)
        SPRAWDZ(std::strcmp(brb::opis_bledu_ladowania(BladLadowania(k)), "nieznany blad ladowania") != 0);
}

// ---------------------------------------------------------------------------------------------------------
// Migawka mapy pamieci
// ---------------------------------------------------------------------------------------------------------
struct Mapa {
    uint64_t krok = 48;
    std::vector<uint8_t> b;
    void dodaj(uint32_t typ, uint64_t poczatek, uint64_t strony, uint64_t atr = 0xF) {
        const size_t o = b.size();
        b.resize(o + krok, 0x5A);   // dopelnienie firmware (krok > 40) celowo niezerowe
        auto w32 = [&](size_t off, uint32_t v) { for (int i = 0; i < 4; ++i) b[off + i] = uint8_t(v >> (8 * i)); };
        auto w64 = [&](size_t off, uint64_t v) { for (int i = 0; i < 8; ++i) b[off + i] = uint8_t(v >> (8 * i)); };
        w32(o, typ); w32(o + 4, 0); w64(o + 8, poczatek); w64(o + 16, 0); w64(o + 24, strony); w64(o + 32, atr);
    }
};

Mapa mapa_ovmf_podobna(uint64_t krok) {
    Mapa m;
    m.krok = krok;
    m.dodaj(4, 0x0, 1);                                   // BS data
    m.dodaj(7, 0x1000, 0x9F);                             // wolna
    m.dodaj(0, 0xA0000, 0x60);                            // zarezerwowana (VGA/BIOS)
    m.dodaj(7, 0x100000, 0x700);                          // wolna 1-8 MiB
    m.dodaj(10, 0x800000, 8);                             // ACPI NVS (jak OVMF)
    m.dodaj(4, 0x900000, 0xE80);                          // BS data (DXE FV)
    m.dodaj(2, 0x7000000, 0x100);                         // loader data
    m.dodaj(5, 0x7F00000, 0x10, 0x800000000000000Full);   // runtime code
    m.dodaj(9, 0x7F10000, 0x10);                          // ACPI reclaim
    m.dodaj(11, 0xFFC00000, 0x400);                       // MMIO (flash)
    m.dodaj(7, 0x100000000ull, 0x40000);                  // wolna powyzej 4 GiB
    m.dodaj(0x70000001u, 0x200000000ull, 0x10);           // typ OEM - nieznany
    return m;
}

void test_mapa() {
    std::printf("[test] migawka mapy pamieci: walidacja i klasyfikacja\n");
    for (uint64_t krok : {40ull, 48ull, 56ull, 512ull}) {
        const Mapa m = mapa_ovmf_podobna(krok);
        brb::WidokMapy w;
        brb::StatystykiMapy st;
        SPRAWDZ(brb::waliduj_mape(m.b.data(), m.b.size(), krok, 1, &w, &st) == BladMapy::Brak);
        SPRAWDZ(w.liczba == 12 && w.rozmiar_deskryptora == krok);
        SPRAWDZ(st.strony[uint8_t(brb::KlasaPamieci::Wolna)] == 0x9F + 0x700 + 0x40000);
        SPRAWDZ(st.strony[uint8_t(brb::KlasaPamieci::Nieznana)] == 0x10);
        SPRAWDZ(st.strony[uint8_t(brb::KlasaPamieci::AcpiNvs)] == 8);
        SPRAWDZ(st.najwyzszy_adres == 0x200000000ull + 0x10000 && st.liczba_powyzej_4gib == 2);
        const brb::WpisMapy e = brb::wpis(w, 10);
        SPRAWDZ(e.poczatek == 0x100000000ull && e.koniec == 0x140000000ull && e.klasa == brb::KlasaPamieci::Wolna);
        SPRAWDZ(brb::wpis(w, 11).klasa == brb::KlasaPamieci::Nieznana);
        SPRAWDZ(brb::wpis(w, 7).atrybuty == 0x800000000000000Full && brb::wpis(w, 7).klasa == brb::KlasaPamieci::UslugiCzasuWykonania);
        // Dowody dla zakresu jadra produkcyjnego: wolna + ACPI NVS + BS data, bez luk.
        struct Licznik { int n; uint32_t typy[8]; } lk{0, {}};
        const uint64_t luki = brb::wpisy_w_zakresie(w, 0x2AA000, 0xD6F000, [](void* c, const brb::WpisMapy& x) {
            auto* l = static_cast<Licznik*>(c);
            if (l->n < 8) l->typy[l->n] = x.typ;
            ++l->n;
        }, &lk);
        SPRAWDZ(luki == 0x900000 - 0x808000 && lk.n == 3 && lk.typy[0] == 7 && lk.typy[1] == 10 && lk.typy[2] == 4);   // luka 0x808000-0x900000 w mapie testowej
        SPRAWDZ(brb::wpisy_w_zakresie(w, 0x50000000, 0x50002000, nullptr, nullptr) == 0x2000);   // luka w mapie
    }
    // Kazdy typ UEFI 0..15 ma klase; typy spoza zakresu i OEM/OS sa nieznane (nigdy wolne).
    SPRAWDZ(brb::klasyfikuj_typ(7) == brb::KlasaPamieci::Wolna);
    for (uint32_t t : {16u, 100u, 0x6FFFFFFFu, 0x70000000u, 0x80000000u, 0xFFFFFFFFu})
        SPRAWDZ(brb::klasyfikuj_typ(t) == brb::KlasaPamieci::Nieznana);
    for (uint32_t t = 0; t < 16; ++t) SPRAWDZ(brb::klasyfikuj_typ(t) != brb::KlasaPamieci::Nieznana);
    // Bledy.
    const Mapa wzor = mapa_ovmf_podobna(48);
    brb::WidokMapy w;
    auto zle = [&](const Mapa& m, uint64_t rozmiar, uint64_t krok, uint32_t wer) {
        return brb::waliduj_mape(m.b.data(), rozmiar, krok, wer, &w, nullptr);
    };
    SPRAWDZ(zle(wzor, wzor.b.size(), 39, 1) == BladMapy::ZlyRozmiarDeskryptora);
    SPRAWDZ(zle(wzor, wzor.b.size(), 44, 1) == BladMapy::ZlyRozmiarDeskryptora);
    SPRAWDZ(zle(wzor, wzor.b.size(), 520, 1) == BladMapy::ZlyRozmiarDeskryptora);
    SPRAWDZ(zle(wzor, wzor.b.size(), 48, 2) == BladMapy::ZlaWersja);
    SPRAWDZ(zle(wzor, wzor.b.size() - 8, 48, 1) == BladMapy::RozmiarNiewielokrotny);
    SPRAWDZ(zle(wzor, 0, 48, 1) == BladMapy::PustaMapa);
    SPRAWDZ(brb::waliduj_mape(nullptr, 48, 48, 1, &w, nullptr) == BladMapy::BrakDanych);
    SPRAWDZ(brb::waliduj_mape(wzor.b.data(), 48, 48, 1, nullptr, nullptr) == BladMapy::BrakDanych);
    {
        Mapa m;
        for (uint32_t i = 0; i < brb::MAPA_MAKS_WPISOW + 1; ++i) m.dodaj(7, uint64_t(i) * 0x1000, 1);
        SPRAWDZ(zle(m, m.b.size(), 48, 1) == BladMapy::ZaDuzoWpisow);
    }
    { Mapa m; m.dodaj(7, 0x1008, 1); SPRAWDZ(zle(m, m.b.size(), 48, 1) == BladMapy::NiewyrownanyAdres); }
    { Mapa m; m.dodaj(7, 0x1000, 0); SPRAWDZ(zle(m, m.b.size(), 48, 1) == BladMapy::ZeroStron); }
    { Mapa m; m.dodaj(7, 0xFFFFFFFFFFFFF000ull, 2); SPRAWDZ(zle(m, m.b.size(), 48, 1) == BladMapy::PrzepelnienieZakresu); }
    { Mapa m; m.dodaj(7, 0x1000, 0x10000000000000ull); SPRAWDZ(zle(m, m.b.size(), 48, 1) == BladMapy::PrzepelnienieZakresu); }
    { Mapa m; m.dodaj(7, 0x100000, 0x10); m.dodaj(4, 0x10F000, 1); SPRAWDZ(zle(m, m.b.size(), 48, 1) == BladMapy::NakladanieWpisow); }
    for (int k = 0; k <= int(BladMapy::NakladanieWpisow); ++k)
        SPRAWDZ(std::strcmp(brb::opis_bledu_mapy(BladMapy(k)), "nieznany blad mapy") != 0);
}

// ---------------------------------------------------------------------------------------------------------
// Mutacje i ograniczony fuzzing: zadna kombinacja planu/zakresow/zrodla nie moze zapisac poza posiadana
// pamiecia; zaakceptowane ladowanie musi przejsc niezalezna weryfikacje.
// ---------------------------------------------------------------------------------------------------------
struct Losowe {
    uint64_t s;
    uint64_t nast() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    uint64_t do_(uint64_t n) { return n ? nast() % n : 0; }
};

void test_fuzz_ladowania(uint64_t iteracje) {
    const std::vector<uint8_t> d = fixture::jadro_minimalne().b;
    brb::PlanLadowania p0;
    SPRAWDZ(plan_z(d, brb::POLITYKA_JADRA_BURSZTYN, &p0));
    brb::ZakresyPrzydzialu z0;
    SPRAWDZ(brb::zbuduj_zakresy_przydzialu(p0, &z0) == BladLadowania::Brak);
    Losowe los{0x42524234ull};
    uint64_t przyjete = 0, naruszenia = 0;
    PamiecSymulowana m(0x100000, 0x8000);
    static const uint64_t graniczne[] = {0, 1, 0xFFF, 0x1000, 0x100000, 0x101000, 0x107000, 0x108000, ~0ull, ~0ull - 0xFFF};
    for (uint64_t it = 0; it < iteracje; ++it) {
        brb::PlanLadowania pl = p0;
        brb::ZakresyPrzydzialu zk = z0;
        brb::ZrodloJadra zr = zrodlo_z(d);
        const unsigned zmian = 1 + unsigned(los.do_(3));
        for (unsigned k = 0; k < zmian; ++k) {
            const uint64_t v = los.do_(2) ? graniczne[los.do_(10)] : los.nast();
            switch (los.do_(8)) {
                case 0: zk.zakresy[los.do_(3)].poczatek = v & ~0xFFFull; break;
                case 1: zk.zakresy[los.do_(3)].strony = v % 16; break;
                case 2: zk.liczba = uint32_t(los.do_(5)); break;
                case 3: pl.segmenty[los.do_(3)].adres_fizyczny = v; break;
                case 4: pl.segmenty[los.do_(3)].rozmiar_w_pamieci = v; break;
                case 5: zr.adres_fizyczny = v; break;
                case 6: zr.rozmiar = d.size() - los.do_(2); break;
                default: pl.segment_wejscia = uint32_t(los.do_(20));
            }
        }
        std::memset(m.bajty.data() + STRAZNIK, WZOR_OBCY, m.rozmiar);
        brb::RaportLadowania r;
        const BladLadowania b = brb::zaladuj_segmenty(pl, brb::POLITYKA_JADRA_BURSZTYN, zr, zk, m.dostep(), &r);
        if (b == BladLadowania::Brak) {
            ++przyjete;
            if (brb::weryfikuj_zaladowany_obraz(pl, zr, zk, m.dostep(), &r) != BladLadowania::Brak) ++naruszenia;
            if (!m.obce_nienaruszone(zk)) ++naruszenia;
        } else if (!m.wszystko_obce()) {
            ++naruszenia;   // odrzucenie po zapisie
        }
        if (!m.straznicy_ok()) ++naruszenia;
    }
    std::printf("[FUZZ-LADOWANIE] iteracje=%llu przyjete=%llu naruszenia=%llu\n", (unsigned long long)iteracje,
                (unsigned long long)przyjete, (unsigned long long)naruszenia);
    SPRAWDZ(naruszenia == 0 && przyjete > 0);
    // Mutacje bajtow migawki mapy: walidator nie wychodzi poza bufor (ASan) i nie akceptuje nakladania.
    const Mapa wzor = mapa_ovmf_podobna(48);
    uint64_t przebiegi = 0;
    for (size_t off = 0; off < wzor.b.size(); ++off)
        for (uint8_t v : {0x00, 0x01, 0x10, 0x80, 0xFF}) {
            Mapa m2 = wzor;
            m2.b[off] = v;
            brb::WidokMapy w;
            brb::StatystykiMapy st;
            if (brb::waliduj_mape(m2.b.data(), m2.b.size(), 48, 1, &w, &st) == BladMapy::Brak) {
                for (uint32_t i = 0; i < w.liczba; ++i)
                    for (uint32_t j = i + 1; j < w.liczba; ++j) {
                        const brb::WpisMapy a = brb::wpis(w, i), c = brb::wpis(w, j);
                        if (a.poczatek < c.koniec && c.poczatek < a.koniec) ++naruszenia;
                    }
            }
            ++przebiegi;
        }
    std::printf("[MUTACJE-MAPA] przebiegi=%llu naruszenia=%llu\n", (unsigned long long)przebiegi, (unsigned long long)naruszenia);
    SPRAWDZ(naruszenia == 0);
}

std::vector<uint8_t> wczytaj(const char* p) {
    std::vector<uint8_t> d;
    FILE* f = std::fopen(p, "rb");
    if (!f) return d;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    return d;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("BRB - testy hosta Fazy 4A (ladowanie, weryfikacja, mapa pamieci)\n");
    std::vector<uint8_t> prod;
    if (argc > 1) prod = wczytaj(argv[1]);
    test_zakresy();
    test_ladowanie_poprawne(prod.empty() ? nullptr : &prod);
    test_ladowanie_bledne();
    test_mapa();
    test_fuzz_ladowania(20000);
    std::printf("BRB testy Fazy 4A: sprawdzenia=%d bledy=%d\n", g_sprawdzenia, g_bledy);
    return g_bledy == 0 ? 0 : 1;
}
