/*
 * BRB - Faza R2: testy hosta elastycznego umieszczenia jadra, rejestru zasobow, tablic przejscia i ich niezaleznej
 * weryfikacji oraz pelnej transakcji faza_r2_zaladuj_natywnie na symulowanym UEFI.
 *
 * Kod PRODUKCYJNY: rozruch/brb/wspolne/{umiejscowienie,ladowanie (przesun_plan, ladowanie z przesunieciem),
 * rejestr_zasobow, weryfikacja_tablic, metadane_jadra, elf64}.cpp, rozruch/brb/uefi/ladowanie_natywne.cpp,
 * jadro/mm/plan_przejscia.h. SYMULOWANE: pamiec fizyczna (bufor ze strefami straznika - test nie dereferencjonuje
 * prawdziwych adresow fizycznych), uslugi AllocatePages/FreePages/GetMemoryMap (ms_abi), cechy procesora.
 *
 * Uzycie: test-brb-r2 [jadro_okrojone_R1B]
 */
#include "rozruch/brb/uefi/ladowanie_natywne.h"
#include "rozruch/brb/wspolne/elf64.h"
#include "rozruch/brb/wspolne/ladowanie.h"
#include "rozruch/brb/wspolne/metadane_jadra.h"
#include "rozruch/brb/wspolne/rejestr_zasobow.h"
#include "rozruch/brb/wspolne/umiejscowienie.h"
#include "rozruch/brb/wspolne/weryfikacja_tablic.h"
#include "jadro/arch/metadane_jadra.h"
#include "jadro/mm/plan_przejscia.h"
#include "testy/brb_elf_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int g_ok = 0, g_bledy = 0;
#define SPRAWDZ(w) do { if (w) { ++g_ok; } else { ++g_bledy; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #w); } } while (0)

namespace {

/* ---------------------------------------------------------------------------------------------------------------
 * Symulowana pamiec fizyczna ze strefami straznika.
 * ------------------------------------------------------------------------------------------------------------- */
constexpr uint64_t SYM_BAZA = 0x1000000, SYM_ROZMIAR = 0x5000000;   // [16 MiB, 96 MiB)
constexpr uint8_t WZOR = 0xCC;

struct PamiecSym {
    std::vector<uint8_t> b = std::vector<uint8_t>(SYM_ROZMIAR, WZOR);
    static uint8_t* wsk(void* k, uint64_t a, uint64_t n) {
        auto* p = static_cast<PamiecSym*>(k);
        if (n == 0 || a < SYM_BAZA || a - SYM_BAZA > SYM_ROZMIAR || n > SYM_ROZMIAR - (a - SYM_BAZA)) return nullptr;
        return p->b.data() + (a - SYM_BAZA);
    }
    brb::DostepFizyczny dostep() { return brb::DostepFizyczny{wsk, this}; }
    // Liczba bajtow != WZOR poza podanymi zakresami (zapisy poza posiadana pamiecia).
    uint64_t zapisy_poza(const std::vector<std::pair<uint64_t, uint64_t>>& dozwolone) const {
        uint64_t n = 0;
        for (uint64_t i = 0; i < SYM_ROZMIAR; ++i) {
            const uint64_t a = SYM_BAZA + i;
            bool ok = false;
            for (auto& z : dozwolone) if (a >= z.first && a < z.second) ok = true;
            if (!ok && b[i] != WZOR) ++n;
        }
        return n;
    }
};

/* ---------------------------------------------------------------------------------------------------------------
 * Jadra testowe.
 * ------------------------------------------------------------------------------------------------------------- */
void zapisz_metadane(fixture::Obraz& o, uint64_t off, uint64_t baza, uint64_t wejscie, uint64_t wyrownanie = 0x1000) {
    o.w64(off, MetadaneJadra::SYGNATURA); o.w16(off + 8, MetadaneJadra::WERSJA); o.w16(off + 10, MetadaneJadra::ROZMIAR);
    o.w32(off + 12, MetadaneJadra::KONTRAKT_WEJSCIA_R1B); o.w64(off + 16, baza); o.w64(off + 24, wejscie);
    o.w64(off + 32, wyrownanie); o.w32(off + 40, MetadaneJadra::CECHY_R1B); o.w32(off + 44, 0); o.w64(off + 48, 0);
    o.w64(off + 56, MetadaneJadra::SYGNATURA_KONCOWA);
}

std::vector<uint8_t> jadro_syntetyczne(uint64_t wyrownanie = 0x1000) {
    fixture::Obraz o = fixture::jadro_minimalne();
    o.segment(0, 1, brb::ELF_PF_R, 0x1000, 0x100000, 0x100000, 0x80, 0x80, 0x1000);
    zapisz_metadane(o, 0x1040, 0x100000, 0x101080, wyrownanie);
    for (int i = 0; i < 0x80; ++i) o.b[0x3000 + i] = uint8_t(0x40 + i);   // dane .data
    return o.b;
}

struct Jadro {
    std::vector<uint8_t> d;
    brb::PlanLadowania plan{};
    brb::MetadaneWejscia meta{};
    bool ok = false;
    explicit Jadro(std::vector<uint8_t> dane) : d(std::move(dane)) {
        ok = brb::zbuduj_plan_ladowania(d.data(), d.size(), brb::POLITYKA_JADRA_BURSZTYN, &plan).blad == brb::BladElf::Brak &&
             brb::znajdz_metadane_wejscia(d.data(), d.size(), plan, &meta) == brb::BladMetadanych::Brak;
    }
};

std::vector<uint8_t> wczytaj(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/* ---------------------------------------------------------------------------------------------------------------
 * A. Wymagania i wybor bazy.
 * ------------------------------------------------------------------------------------------------------------- */
void test_umiejscowienie() {
    std::printf("[R2] A: wymagania i wybor bazy\n");
    Jadro j(jadro_syntetyczne());
    SPRAWDZ(j.ok);
    brb::WymaganiaObrazu w{};
    SPRAWDZ(brb::wymagania_obrazu(j.plan, j.meta, &w) == brb::BladUmiejscowienia::Brak);
    SPRAWDZ(w.baza_wirtualna == 0x100000 && w.rozmiar == 0x4000 && w.strony == 4 && w.wyrownanie == 0x1000 && w.strony_przydzialu == 4);
    // Wyrownanie 2 MiB z metadanych: dopelnienie 511 stron.
    Jadro j2(jadro_syntetyczne(0x200000));
    brb::WymaganiaObrazu w2{};
    SPRAWDZ(j2.ok && brb::wymagania_obrazu(j2.plan, j2.meta, &w2) == brb::BladUmiejscowienia::Brak);
    SPRAWDZ(w2.wyrownanie == 0x200000 && w2.strony_przydzialu == 4 + 511);
    brb::WybraneUmiejscowienie u{};
    // Przydzial niewyrownany do 2 MiB: baza wewnatrz przydzialu, dopelnienie przed/po.
    SPRAWDZ(brb::wybierz_baze(w2, 0x3005000, w2.strony_przydzialu, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::Brak);
    SPRAWDZ(u.baza == 0x3200000 && u.dopelnienie_przed == 0x1FB000 && u.dopelnienie_po == (0x3005000 + 515 * 0x1000) - (0x3200000 + 0x4000));
    SPRAWDZ(brb::wybierz_baze(w2, 0x3200000, w2.strony_przydzialu, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::Brak && u.baza == 0x3200000 && u.dopelnienie_przed == 0);
    // Za maly przydzial (bez dopelnienia), niewyrownany, zero stron, ponad 4 GiB, szerokosc, przepelnienie.
    SPRAWDZ(brb::wybierz_baze(w2, 0x3005000, 4, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::ZaMalyPrzydzial);
    SPRAWDZ(brb::wybierz_baze(w, 0x3005800, 4, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::PrzydzialNiewyrownany);
    SPRAWDZ(brb::wybierz_baze(w, 0x3005000, 0, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::PrzydzialNiewyrownany);
    SPRAWDZ(brb::wybierz_baze(w, 0, 4, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::PrzydzialNiewyrownany);
    SPRAWDZ(brb::wybierz_baze(w, 0xFFFFE000, 4, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::PozaLimitem);   // przecina 4 GiB
    SPRAWDZ(brb::wybierz_baze(w, 0xFFFFC000, 4, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 39, &u) == brb::BladUmiejscowienia::Brak);       // konczy sie dokladnie na 4 GiB
    SPRAWDZ(brb::wybierz_baze(w, 0x1000000000ULL, 4, ~0ULL, 36, &u) == brb::BladUmiejscowienia::PozaSzerokoscia);
    SPRAWDZ(brb::wybierz_baze(w, 0x3000000, 4, brb::UMIESZCZENIE_LIMIT_TYMCZASOWY, 35, &u) == brb::BladUmiejscowienia::PozaSzerokoscia);
    SPRAWDZ(brb::wybierz_baze(w, 0xFFFFFFFFFFFFF000ULL, 4, ~0ULL, 52, &u) == brb::BladUmiejscowienia::Przepelnienie);
    SPRAWDZ(brb::wybierz_baze(w2, 0xFFFFFFFFFFE01000ULL, 4, ~0ULL, 52, &u) == brb::BladUmiejscowienia::Przepelnienie);
    // Metadane niezgodne z planem / wyrownanie niepoprawne.
    { brb::MetadaneWejscia m = j.meta; m.baza_wirtualna = 0x200000; SPRAWDZ(brb::wymagania_obrazu(j.plan, m, &w) == brb::BladUmiejscowienia::BazaNiezgodna); }
    { brb::MetadaneWejscia m = j.meta; m.wyrownanie_podkladu = 0x800; SPRAWDZ(brb::wymagania_obrazu(j.plan, m, &w) == brb::BladUmiejscowienia::Wyrownanie); }
    { brb::MetadaneWejscia m = j.meta; m.wyrownanie_podkladu = 0x80000000ULL; SPRAWDZ(brb::wymagania_obrazu(j.plan, m, &w) == brb::BladUmiejscowienia::Wyrownanie); }
    { brb::PlanLadowania p = j.plan; p.segmenty[1].wyrownanie = 0x200000;   // p_align > baza VA -> brak kongruencji
      SPRAWDZ(brb::wymagania_obrazu(p, j.meta, &w) == brb::BladUmiejscowienia::Wyrownanie); }
    { brb::PlanLadowania p = j.plan; p.segmenty[2].adres_fizyczny += 0x1000;   // plan nie-tozsamosciowy
      SPRAWDZ(brb::wymagania_obrazu(p, j.meta, &w) == brb::BladUmiejscowienia::PlanNieciagly); }
    { brb::PlanLadowania p = j.plan; p.segmenty[2].rozmiar_w_pamieci = 0x10000;   // poza zakresem planu
      SPRAWDZ(brb::wymagania_obrazu(p, j.meta, &w) == brb::BladUmiejscowienia::PlanNieciagly); }
}

/* ---------------------------------------------------------------------------------------------------------------
 * B. Przesuniecie planu (stale niezalezne: uklad jadra R1B z readelf).
 * ------------------------------------------------------------------------------------------------------------- */
void test_przesuniecie(const std::vector<uint8_t>* r1b) {
    std::printf("[R2] B: przesuniecie planu\n");
    Jadro j(jadro_syntetyczne());
    brb::PlanLadowania p{};
    for (uint64_t baza : {0x3005000ULL, 0x2000000ULL, 0x7FF00000ULL, 0xFFFFC000ULL}) {
        SPRAWDZ(brb::przesun_plan(j.plan, baza, &p) == brb::BladLadowania::Brak);
        SPRAWDZ(p.segmenty[0].adres_fizyczny == baza && p.segmenty[1].adres_fizyczny == baza + 0x1000 &&
                p.segmenty[2].adres_fizyczny == baza + 0x2000);
        SPRAWDZ(p.segmenty[1].adres_wirtualny == 0x101000 && p.wejscie_wirtualne == 0x101010 && p.wejscie_fizyczne == baza + 0x1010);
        SPRAWDZ(p.zakres_poczatek == baza && p.zakres_koniec == baza + 0x4000);
        SPRAWDZ(p.segmenty[2].przesuniecie_pliku == j.plan.segmenty[2].przesuniecie_pliku && p.segmenty[2].prawa == j.plan.segmenty[2].prawa);
    }
    SPRAWDZ(brb::przesun_plan(j.plan, 0x3005800, &p) == brb::BladLadowania::PrzesuniecieNiepoprawne);
    SPRAWDZ(brb::przesun_plan(j.plan, 0, &p) == brb::BladLadowania::PrzesuniecieNiepoprawne);
    SPRAWDZ(brb::przesun_plan(j.plan, 0xFFFFFFFFFFFFE000ULL, &p) == brb::BladLadowania::PrzesuniecieNiepoprawne);
    { brb::PlanLadowania q = j.plan; q.segmenty[1].adres_fizyczny = 0x500000; SPRAWDZ(brb::przesun_plan(q, 0x3000000, &p) == brb::BladLadowania::PrzesuniecieNiepoprawne); }
    { brb::PlanLadowania q = j.plan; q.liczba_segmentow = 0; SPRAWDZ(brb::przesun_plan(q, 0x3000000, &p) == brb::BladLadowania::PustyPlan); }
    if (r1b && !r1b->empty()) {
        Jadro k(*r1b);
        SPRAWDZ(k.ok);
        SPRAWDZ(brb::przesun_plan(k.plan, 0x3005000, &p) == brb::BladLadowania::Brak);
        // readelf jadra R1B: p_vaddr 0x100000, 0x101000, 0x22F000, 0x2AE000; wejscie natywne 0x11960A.
        SPRAWDZ(p.segmenty[0].adres_fizyczny == 0x3005000 && p.segmenty[1].adres_fizyczny == 0x3006000 &&
                p.segmenty[2].adres_fizyczny == 0x3134000 && p.segmenty[3].adres_fizyczny == 0x31B3000);
        SPRAWDZ(k.meta.wejscie64 == 0x11960A && k.plan.wejscie_wirtualne == 0x1193E2);
        SPRAWDZ(p.zakres_koniec - p.zakres_poczatek == 0xC7C000);
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * C. Ladowanie i weryfikacja pod rozne bazy (symulowana pamiec, strefy straznika).
 * ------------------------------------------------------------------------------------------------------------- */
void test_ladowanie(const std::vector<uint8_t>& dane, const char* nazwa) {
    std::printf("[R2] C: ladowanie z przesunieciem (%s)\n", nazwa);
    Jadro j(dane);
    SPRAWDZ(j.ok);
    brb::WymaganiaObrazu w{};
    SPRAWDZ(brb::wymagania_obrazu(j.plan, j.meta, &w) == brb::BladUmiejscowienia::Brak);
    for (uint64_t baza : {SYM_BAZA + 0x5000, SYM_BAZA + 0x2000000, SYM_BAZA + SYM_ROZMIAR - w.rozmiar - 0x3000}) {
        PamiecSym m;
        brb::PlanLadowania p{};
        SPRAWDZ(brb::przesun_plan(j.plan, baza, &p) == brb::BladLadowania::Brak);
        brb::ZakresyPrzydzialu z{};
        z.liczba = 1; z.zakresy[0] = brb::ZakresStron{baza, w.strony}; z.strony_lacznie = w.strony;
        const brb::ZrodloJadra zr{j.d.data(), j.d.size(), reinterpret_cast<uintptr_t>(j.d.data()), (j.d.size() + 4095) & ~4095ULL};
        brb::RaportLadowania r{};
        SPRAWDZ(brb::zaladuj_segmenty(p, brb::POLITYKA_JADRA_BURSZTYN, zr, z, m.dostep(), &r, &baza) == brb::BladLadowania::Brak);
        SPRAWDZ(brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), &r) == brb::BladLadowania::Brak);
        SPRAWDZ(m.zapisy_poza({{baza, baza + w.rozmiar}}) == 0);   // strefy straznika nietkniete
        // Niezalezne porownanie z plikiem: kazdy segment pod baza + (p_vaddr - 0x100000).
        bool ok = true;
        for (uint32_t i = 0; i < j.plan.liczba_segmentow; ++i) {
            const auto& s = j.plan.segmenty[i];
            const uint8_t* c = m.b.data() + (baza + (s.adres_wirtualny - 0x100000) - SYM_BAZA);
            if (std::memcmp(c, j.d.data() + s.przesuniecie_pliku, s.bajty_z_pliku) != 0) ok = false;
            for (uint64_t k = s.bajty_z_pliku; k < s.rozmiar_w_pamieci; ++k) if (c[k]) ok = false;
        }
        SPRAWDZ(ok);
        // Uszkodzenia wykrywane przez weryfikacje: dane, BSS, luka/wypelnienie.
        uint8_t* obraz = m.b.data() + (baza - SYM_BAZA);
        const auto& s1 = j.plan.segmenty[1];
        obraz[s1.adres_wirtualny - 0x100000 + 3] ^= 1;
        SPRAWDZ(brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), &r) == brb::BladLadowania::WeryfikacjaDanych);
        obraz[s1.adres_wirtualny - 0x100000 + 3] ^= 1;
        const auto& s_bss = j.plan.segmenty[j.plan.liczba_segmentow - 1];
        if (s_bss.bajty_zerowane) {
            obraz[s_bss.adres_wirtualny - 0x100000 + s_bss.bajty_z_pliku] = 7;
            SPRAWDZ(brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), &r) == brb::BladLadowania::WeryfikacjaBss);
            obraz[s_bss.adres_wirtualny - 0x100000 + s_bss.bajty_z_pliku] = 0;
        }
        obraz[0xF00] = 9;   // luz strony za segmentem .boot (wypelnienie, poza segmentami)
        SPRAWDZ(j.plan.segmenty[0].rozmiar_w_pamieci < 0xF00);
        SPRAWDZ(brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), &r) == brb::BladLadowania::WeryfikacjaWypelnienia);
        obraz[0xF00] = 0;
        SPRAWDZ(brb::weryfikuj_zaladowany_obraz(p, zr, z, m.dostep(), &r) == brb::BladLadowania::Brak);
        // Inna baza w planie niz przekazana: ponowne wyliczenie rozni sie -> brak zapisu.
        PamiecSym m2;
        uint64_t inna = baza + 0x1000;
        SPRAWDZ(brb::zaladuj_segmenty(p, brb::POLITYKA_JADRA_BURSZTYN, zr, z, m2.dostep(), &r, &inna) == brb::BladLadowania::NiespojnyPlan);
        SPRAWDZ(brb::zaladuj_segmenty(p, brb::POLITYKA_JADRA_BURSZTYN, zr, z, m2.dostep(), &r) == brb::BladLadowania::NiespojnyPlan);   // bez przesuniecia
        SPRAWDZ(m2.zapisy_poza({}) == 0);
        // Cel poza posiadanym zakresem (zakres o strone za krotki).
        brb::ZakresyPrzydzialu za_malo = z;
        za_malo.zakresy[0].strony -= 1;
        SPRAWDZ(brb::zaladuj_segmenty(p, brb::POLITYKA_JADRA_BURSZTYN, zr, za_malo, m2.dostep(), &r, &baza) == brb::BladLadowania::CelPozaPrzydzialem);
        SPRAWDZ(m2.zapisy_poza({}) == 0);
    }
    // Zrodlo nachodzace na cel.
    {
        PamiecSym m;
        const uint64_t baza = SYM_BAZA + 0x10000;
        brb::PlanLadowania p{};
        brb::przesun_plan(j.plan, baza, &p);
        brb::ZakresyPrzydzialu z{};
        z.liczba = 1; z.zakresy[0] = brb::ZakresStron{baza, w.strony};
        const brb::ZrodloJadra zr{j.d.data(), j.d.size(), baza - 0x1000, 0x2000 + j.d.size()};
        brb::RaportLadowania r{};
        uint64_t b = baza;
        SPRAWDZ(brb::zaladuj_segmenty(p, brb::POLITYKA_JADRA_BURSZTYN, zr, z, m.dostep(), &r, &b) == brb::BladLadowania::ZrodloNakladaSieNaCel);
        SPRAWDZ(m.zapisy_poza({}) == 0);
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * D. Rejestr zasobow.
 * ------------------------------------------------------------------------------------------------------------- */
struct Zwalniacz {
    std::vector<std::pair<uint64_t, uint64_t>> wywolania;
    int blad_przy = -1;
    static bool f(void* k, uint64_t a, uint64_t s) {
        auto* z = static_cast<Zwalniacz*>(k);
        z->wywolania.push_back({a, s});
        return int(z->wywolania.size()) - 1 != z->blad_przy;
    }
};

void test_rejestr() {
    std::printf("[R2] D: rejestr zasobow\n");
    brb::RejestrZasobow r;
    brb::rejestr_inicjalizuj(&r);
    int32_t k = 0;
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x10000000, 0x100, brb::RodzajZasobu::ObrazBrb, true, &k) == brb::BladRejestru::Brak);
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x20000000, 0x10, brb::RodzajZasobu::PodkladJadra, false, &k) == brb::BladRejestru::Brak);
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x100FF000, 2, brb::RodzajZasobu::TabliceStron, false, &k) == brb::BladRejestru::Nakladanie && k == 0);   // na obraz BRB
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x2000F000, 1, brb::RodzajZasobu::StosPrzejscia, false, &k) == brb::BladRejestru::Nakladanie && k == 1);
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x20010000, 1, brb::RodzajZasobu::StosPrzejscia, false, &k) == brb::BladRejestru::Brak);   // tuz za
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x20020800, 1, brb::RodzajZasobu::StosPrzejscia, false, &k) == brb::BladRejestru::ZakresNiepoprawny);
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x20030000, 0, brb::RodzajZasobu::StosPrzejscia, false, &k) == brb::BladRejestru::ZakresNiepoprawny);
    SPRAWDZ(brb::rejestr_dodaj(&r, 0xFFFFFFFFFFFFF000ULL, 2, brb::RodzajZasobu::StosPrzejscia, false, &k) == brb::BladRejestru::ZakresNiepoprawny);
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x0000800000000000ULL, 1, brb::RodzajZasobu::StosPrzejscia, false, &k) == brb::BladRejestru::ZakresNiepoprawny);
    SPRAWDZ(brb::rejestr_posiada(r, 0x20000000, 0x10000, brb::RodzajZasobu::PodkladJadra));
    SPRAWDZ(!brb::rejestr_posiada(r, 0x20000000, 0x10001, brb::RodzajZasobu::PodkladJadra));
    SPRAWDZ(!brb::rejestr_posiada(r, 0x20000000, 0x1000, brb::RodzajZasobu::TabliceStron));
    SPRAWDZ(!brb::rejestr_posiada(r, 0x10000000, 0x1000, brb::RodzajZasobu::ObrazBrb));   // chroniony != posiadany
    for (uint32_t i = r.liczba; i < brb::REJESTR_MAKS; ++i)
        brb::rejestr_dodaj(&r, 0x30000000 + 0x1000ULL * i, 1, brb::RodzajZasobu::InformacjeRozruchu, false, nullptr);
    SPRAWDZ(brb::rejestr_dodaj(&r, 0x40000000, 1, brb::RodzajZasobu::StosPrzejscia, false, &k) == brb::BladRejestru::Pelny);
    // Zwalnianie: odwrotna kolejnosc, chronione pominiete, blad = porzucony, brak podwojnego zwolnienia.
    Zwalniacz z;
    z.blad_przy = 2;
    const uint32_t wlasne = brb::rejestr_wlasne(r);
    SPRAWDZ(brb::rejestr_zwolnij_wszystko(&r, Zwalniacz::f, &z) == 1);
    SPRAWDZ(z.wywolania.size() == wlasne && z.wywolania.front().first == 0x30000000 + 0x1000ULL * (brb::REJESTR_MAKS - 1));
    SPRAWDZ(z.wywolania.back().first == 0x20000000);
    SPRAWDZ(brb::rejestr_wlasne(r) == 0 && r.bledy_zwalniania == 1 && r.zwolnione == wlasne - 1);
    bool porzucony = false;
    for (uint32_t i = 0; i < r.liczba; ++i) if (r.wpisy[i].porzucony) porzucony = true;
    SPRAWDZ(porzucony);
    const size_t przed = z.wywolania.size();
    SPRAWDZ(brb::rejestr_zwolnij_wszystko(&r, Zwalniacz::f, &z) == 0 && z.wywolania.size() == przed);   // drugie sprzatanie: nic
    // Zwolniony zakres nie blokuje nowego przydzialu w tym samym miejscu.
    brb::RejestrZasobow r2;
    brb::rejestr_inicjalizuj(&r2);
    brb::rejestr_dodaj(&r2, 0x50000000, 4, brb::RodzajZasobu::PodkladJadra, false, nullptr);
    Zwalniacz z2;
    brb::rejestr_zwolnij_wszystko(&r2, Zwalniacz::f, &z2);
    SPRAWDZ(brb::rejestr_sprawdz(r2, 0x50000000, 4, &k) == brb::BladRejestru::Brak);
}

/* ---------------------------------------------------------------------------------------------------------------
 * E. Tablice przejscia: budowa (PlanPrzejscia) + NIEZALEZNA weryfikacja + mutacje.
 * ------------------------------------------------------------------------------------------------------------- */
struct Uklad {
    brb::PlanLadowania p{};
    ObrazJadra::Uklad u{};
    uint64_t baza = 0, rozmiar = 0, wejscie = 0;
};

Uklad uklad_dla(const Jadro& j, uint64_t baza) {
    Uklad x;
    brb::WymaganiaObrazu w{};
    brb::wymagania_obrazu(j.plan, j.meta, &w);
    brb::przesun_plan(j.plan, baza, &x.p);
    ObrazJadra::Segment seg[8]{};
    const uint32_t n = x.p.liczba_segmentow;
    for (uint32_t i = 0; i < n; ++i) {
        const auto& s = x.p.segmenty[i];
        const uint64_t poczatek = s.adres_wirtualny & ~0xFFFULL;
        const uint64_t nast = i + 1 < n ? (x.p.segmenty[i + 1].adres_wirtualny & ~0xFFFULL) : w.baza_wirtualna + w.rozmiar;
        uint32_t up = ObrazJadra::ODCZYT;
        if (s.prawa & brb::ELF_PF_W) up |= ObrazJadra::ZAPIS;
        if (s.prawa & brb::ELF_PF_X) up |= ObrazJadra::WYKONANIE;
        seg[i] = {poczatek, nast - poczatek, (s.adres_wirtualny - poczatek) + s.rozmiar_w_pamieci, up, 0};
    }
    ObrazJadra::zbuduj_uklad(w.baza_wirtualna, w.rozmiar, baza, seg, n, &x.u);
    x.baza = baza; x.rozmiar = w.rozmiar; x.wejscie = j.meta.wejscie64;
    return x;
}

struct Tablice {
    std::vector<uint64_t> pula;
    uint64_t pula_pa;
    static const uint64_t* tab(void* k, uint64_t pa) {
        auto* t = static_cast<Tablice*>(k);
        if (pa < t->pula_pa || pa - t->pula_pa >= t->pula.size() * 8 || (pa & 0xFFF)) return nullptr;
        return t->pula.data() + (pa - t->pula_pa) / 8;
    }
    uint64_t* wpis(uint64_t cr3, uint64_t va, int poziom_docelowy) {   // wskaznik wpisu na danym poziomie
        uint64_t t = cr3;
        for (int poziom = 4; poziom >= 1; --poziom) {
            uint64_t* tab_ = const_cast<uint64_t*>(Tablice::tab(this, t));
            if (!tab_) return nullptr;
            uint64_t* e = &tab_[(va >> (12 + 9 * (poziom - 1))) & 511];
            if (poziom == poziom_docelowy) return e;
            t = *e & 0x000FFFFFFFFFF000ULL;
        }
        return nullptr;
    }
};

void test_tablice(const Jadro& j, const char* nazwa) {
    std::printf("[R2] E: tablice przejscia i niezalezna weryfikacja (%s)\n", nazwa);
    for (uint64_t baza : {0x3005000ULL, 0x7E000000ULL}) {
        const Uklad x = uklad_dla(j, baza);
        SPRAWDZ(x.u.poprawny);
        const uint64_t kod = 0x9000000, stos = 0x9010000, info = 0x9020000, pula_pa = 0x9800000;
        Tablice t{std::vector<uint64_t>(64 * 512, 0xCCCCCCCCCCCCCCCCULL), pula_pa};
        const PlanPrzejscia::ZakresTozsamosci z[3] = {{kod, 0x1000, PlanPrzejscia::KLASA_KOD_PRZEJSCIA, 0},
                                                      {stos, 0x4000, PlanPrzejscia::KLASA_STOS, 0},
                                                      {info, 0x10000, PlanPrzejscia::KLASA_TYLKO_ODCZYT, 0}};
        PlanPrzejscia::Wynik wy{};
        SPRAWDZ(PlanPrzejscia::zbuduj(PlanPrzejscia::Parametry{&x.u, z, 3, true, 39, pula_pa, t.pula.data(), 64}, &wy) == PlanPrzejscia::Blad::OK);
        const brb::ZasobTozsamosciowy zz[3] = {{kod, 0x1000, brb::ZASOB_KOD_PRZEJSCIA}, {stos, 0x4000, brb::ZASOB_STOS},
                                               {info, 0x10000, brb::ZASOB_TYLKO_ODCZYT}};
        const brb::OczekiwaniaTablic o{&x.p, 0x100000, baza, x.rozmiar, x.wejscie, zz, 3, pula_pa, 64, wy.cr3, info, 2408, 39, true};
        const brb::DostepTablic d{Tablice::tab, &t};
        brb::RaportTablic r{};
        SPRAWDZ(brb::weryfikuj_tablice_przejscia(o, d, &r) == brb::BladTablic::Brak);
        SPRAWDZ(r.strony_okna == x.rozmiar / 0x1000 && r.strony_zasobow == 1 + 4 + 16 && r.strony_puli == 64);
        SPRAWDZ(r.wejscie_pa == baza + (x.wejscie - 0x100000));
        // Mutacje tablic -> konkretne bledy.
        auto mutuj = [&](uint64_t va, int poziom, uint64_t xor_, uint64_t ustaw, brb::BladTablic ocz, const char* opis) {
            uint64_t* e = t.wpis(wy.cr3, va, poziom);
            if (!e) { SPRAWDZ(false); return; }
            const uint64_t stare = *e;
            *e = (stare ^ xor_) | ustaw;
            brb::RaportTablic rr{};
            const brb::BladTablic b = brb::weryfikuj_tablice_przejscia(o, d, &rr);
            if (b != ocz) std::printf("  mutacja '%s': %s\n", opis, brb::opis_bledu_tablic(b));
            SPRAWDZ(b == ocz);
            *e = stare;
        };
        const uint64_t tekst = x.wejscie & ~0xFFFULL, rodata = x.p.segmenty[2].adres_wirtualny & ~0xFFFULL;
        mutuj(tekst, 1, 0, 1ULL << 2, brb::BladTablic::BitUzytkownika, "U w lisciu tekstu");
        mutuj(tekst, 3, 0, 1ULL << 2, brb::BladTablic::BitUzytkownika, "U w PDPTE");
        mutuj(tekst, 1, 0, 1ULL << 1, brb::BladTablic::WPlusX, "W na tekscie");
        mutuj(tekst, 1, 0x1000, 0, brb::BladTablic::ZlyAdresOkna, "zly PA strony okna");
        mutuj(tekst, 1, 1, 0, brb::BladTablic::BrakStronyOkna, "usuniety lisc okna");
        if (x.p.liczba_segmentow > 3) mutuj(rodata, 1, 1ULL << 63, 0, brb::BladTablic::ZleUprawnieniaOkna, "rodata wykonywalne");
        mutuj(tekst, 1, 0, 1ULL << 45, brb::BladTablic::BitZarezerwowany, "adres > MAXPHYADDR");
        mutuj(tekst, 2, 0, 1ULL << 7, brb::BladTablic::BitZarezerwowany, "PS w PDE");
        mutuj(stos, 1, 1ULL << 1, 0, brb::BladTablic::ZleUprawnieniaZasobu, "stos bez W");
        mutuj(info, 1, 1, 0, brb::BladTablic::BrakStronyZasobu, "brak strony informacji");
        mutuj(kod, 1, 0x1000, 0, brb::BladTablic::ZlaTozsamosc, "kod przejscia nie-tozsamosciowy");
        mutuj(tekst, 2, 0, 0x7000000000ULL & 0, brb::BladTablic::Brak, "kontrola: bez zmian");
        {   // Lisc nieoczekiwany: tozsamosc strony obok kodu przejscia w tej samej PT.
            uint64_t* e = t.wpis(wy.cr3, kod + 0x5000, 1);
            SPRAWDZ(e && !(*e & 1));
            *e = (kod + 0x5000) | 1;
            brb::RaportTablic rr{};
            SPRAWDZ(brb::weryfikuj_tablice_przejscia(o, d, &rr) == brb::BladTablic::MapowanieNieoczekiwane);
            *e = 0;
        }
        {   // Tablica posrednia poza pula.
            uint64_t* e = t.wpis(wy.cr3, kod, 2);
            const uint64_t stare = *e;
            *e = (stare & ~0x000FFFFFFFFFF000ULL) | 0x5000000;
            brb::RaportTablic rr{};
            SPRAWDZ(brb::weryfikuj_tablice_przejscia(o, d, &rr) == brb::BladTablic::TablicaPozaPula);
            *e = stare;
        }
        {   // Oczekiwania: brak NX, zasob na podkladzie, CR3 poza pula, kontrakt poza zasobami.
            brb::OczekiwaniaTablic o2 = o; o2.nx = false;
            brb::RaportTablic rr{};
            SPRAWDZ(brb::weryfikuj_tablice_przejscia(o2, d, &rr) == brb::BladTablic::BrakNx);
            brb::ZasobTozsamosciowy zl[3] = {zz[0], zz[1], {baza + 0x1000, 0x1000, brb::ZASOB_TYLKO_ODCZYT}};
            o2 = o; o2.zasoby = zl;
            SPRAWDZ(brb::weryfikuj_tablice_przejscia(o2, d, &rr) == brb::BladTablic::AliasPodkladu);
            o2 = o; o2.cr3 = 0x5000000;
            SPRAWDZ(brb::weryfikuj_tablice_przejscia(o2, d, &rr) == brb::BladTablic::Cr3Niepoprawny);
            o2 = o; o2.kontrakt_pa = stos;   // stos jest RW - nie spelnia R+NX
            SPRAWDZ(brb::weryfikuj_tablice_przejscia(o2, d, &rr) == brb::BladTablic::KontraktNiedostepny);
            o2 = o; o2.wejscie64 = x.p.segmenty[2].adres_wirtualny;   // wejscie w .rodata (bez X)
            SPRAWDZ(brb::weryfikuj_tablice_przejscia(o2, d, &rr) == brb::BladTablic::WejscieNiepoprawne);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * F. Pelna transakcja faza_r2_zaladuj_natywnie na symulowanym UEFI.
 * ------------------------------------------------------------------------------------------------------------- */
struct Firmware {
    PamiecSym pamiec;
    std::vector<std::pair<uint64_t, uint64_t>> zajete;                 // [od, do) - firmware
    std::vector<std::pair<uint64_t, uint64_t>> przydzialy;             // aktywne w symulacji (PA, strony)
    std::vector<std::pair<uint64_t, uint64_t>> host;                   // aktywne bufory hosta (AnyPages)
    std::vector<uint64_t> historia;                                    // adresy przydzialow MaxAddress
    int max_wywolania = 0, max_blad_przy = -1, free_blad_przy = -1, free_wywolania = 0;
    uint64_t przesuniecie_zwrotu = 0;                                   // >0: zwroc zly (niewyrownany) adres
    bool podwojny = false;                                               // zwroc dwa razy ten sam adres
    bool zle_zwolnienie = false;
    EFI_BOOT_SERVICES bs{};
    EFI_LOADED_IMAGE_PROTOCOL obraz{};
};
Firmware* F = nullptr;

bool wolne(uint64_t a, uint64_t k) {
    if (a < SYM_BAZA || k > SYM_BAZA + SYM_ROZMIAR) return false;
    for (auto& z : F->zajete) if (a < z.second && z.first < k) return false;
    for (auto& z : F->przydzialy) if (a < z.first + z.second * 4096 && z.first < k) return false;
    return true;
}

EFI_STATUS EFIAPI f_alloc(EFI_ALLOCATE_TYPE typ, EFI_MEMORY_TYPE t, UINTN strony, EFI_PHYSICAL_ADDRESS* adres) {
    if (!adres || strony == 0) return EFI_INVALID_PARAMETER;
    if (typ == AllocateAnyPages) {
        if (t != EfiLoaderData) return EFI_INVALID_PARAMETER;
        void* p = std::aligned_alloc(4096, strony * 4096);
        if (!p) return EFI_OUT_OF_RESOURCES;
        *adres = reinterpret_cast<uintptr_t>(p);
        F->host.push_back({*adres, strony});
        return EFI_SUCCESS;
    }
    if (typ != AllocateMaxAddress || (t != EfiLoaderData && t != EfiLoaderCode)) return EFI_INVALID_PARAMETER;
    const int n = F->max_wywolania++;
    if (n == F->max_blad_przy) return EFI_OUT_OF_RESOURCES;
    const uint64_t max = *adres;
    if (strony > SYM_ROZMIAR / 4096) return EFI_OUT_OF_RESOURCES;
    if (F->podwojny && !F->historia.empty()) { *adres = F->historia.back(); F->przydzialy.push_back({*adres, strony}); return EFI_SUCCESS; }
    for (uint64_t k = std::min(max + 1, SYM_BAZA + SYM_ROZMIAR) & ~0xFFFULL; k >= SYM_BAZA + strony * 4096; k -= 4096) {
        const uint64_t a = k - strony * 4096;
        if (wolne(a, k)) {
            F->przydzialy.push_back({a, strony});
            F->historia.push_back(a);
            *adres = a + F->przesuniecie_zwrotu;
            return EFI_SUCCESS;
        }
    }
    return EFI_OUT_OF_RESOURCES;
}

EFI_STATUS EFIAPI f_free(EFI_PHYSICAL_ADDRESS a, UINTN strony) {
    const int n = F->free_wywolania++;
    for (size_t i = 0; i < F->host.size(); ++i) if (F->host[i].first == a) {
        if (F->host[i].second != strony) F->zle_zwolnienie = true;
        std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(a)));
        F->host.erase(F->host.begin() + long(i));
        return EFI_SUCCESS;
    }
    for (size_t i = 0; i < F->przydzialy.size(); ++i) if (F->przydzialy[i].first == a) {
        if (F->przydzialy[i].second != strony) F->zle_zwolnienie = true;
        if (n == F->free_blad_przy) return EFI_DEVICE_ERROR;
        F->przydzialy.erase(F->przydzialy.begin() + long(i));
        return EFI_SUCCESS;
    }
    F->zle_zwolnienie = true;
    return EFI_NOT_FOUND;
}

EFI_STATUS EFIAPI f_mapa(UINTN* rozmiar, EFI_MEMORY_DESCRIPTOR* mapa, UINTN* klucz, UINTN* krok, UINT32* wersja) {
    struct D { UINT32 typ; uint64_t a, strony; };
    std::vector<D> d = {{EfiConventionalMemory, 0x100000, 0x700},       // 1..8 MiB
                        {EfiACPIMemoryNVS, 0x800000, 0x100},            // NVS 8..9 MiB (jak OVMF z S3)
                        {EfiBootServicesData, 0x900000, 0x700},
                        {EfiConventionalMemory, SYM_BAZA, SYM_ROZMIAR / 4096}};
    const UINTN potrzeba = d.size() * 48;
    if (!rozmiar || !klucz || !krok || !wersja) return EFI_INVALID_PARAMETER;
    *krok = 48; *wersja = 1;
    if (*rozmiar < potrzeba) { *rozmiar = potrzeba; return EFI_BUFFER_TOO_SMALL; }
    auto* b = reinterpret_cast<uint8_t*>(mapa);
    std::memset(b, 0, potrzeba);
    for (size_t i = 0; i < d.size(); ++i) {
        std::memcpy(b + i * 48, &d[i].typ, 4);
        std::memcpy(b + i * 48 + 8, &d[i].a, 8);
        std::memcpy(b + i * 48 + 24, &d[i].strony, 8);
    }
    *rozmiar = potrzeba;
    *klucz = 0x1234;
    return EFI_SUCCESS;
}

extern "C" char __executable_start[], _end[];

// Model UART (COM1): rejestry w tablicy, LSR zawsze gotowy, bajty danych (DLAB = 0) dopisywane do tekstu.
struct Uart {
    uint8_t r[8] = {};
    std::string tekst;
    static uint8_t czytaj(void* k, uint16_t port) {
        auto* u = static_cast<Uart*>(k);
        if (port == 0x3F8 + 5) return 0x60;
        return u->r[(port - 0x3F8) & 7];
    }
    static void zapisz(void* k, uint16_t port, uint8_t v) {
        auto* u = static_cast<Uart*>(k);
        const unsigned i = (port - 0x3F8) & 7;
        if (i == 0 && !(u->r[3] & 0x80)) u->tekst += char(v);
        u->r[i] = v;
    }
};

struct WynikR2 {
    EFI_STATUS s;
    brb::RaportR2 r;
    std::string log;
};


WynikR2 uruchom_r2(Firmware& fw, const Jadro& j, brb::CechyProcesora cpu = {39, true, true, false}) {
    F = &fw;
    fw.bs.AllocatePages = f_alloc;
    fw.bs.FreePages = f_free;
    fw.bs.GetMemoryMap = f_mapa;
    fw.obraz.ImageBase = __executable_start;
    fw.obraz.ImageSize = UINT64(_end - __executable_start);
    // Bufor zrodla: prawdziwe bajty pliku w buforze hosta (jak plik_jadra po odczycie).
    const UINTN strony = (j.d.size() + 4095) / 4096;
    void* buf = std::aligned_alloc(4096, strony * 4096);
    std::memset(buf, 0, strony * 4096);
    std::memcpy(buf, j.d.data(), j.d.size());
    const brb::BuforJadra bufor{reinterpret_cast<uintptr_t>(buf), strony, j.d.size()};
    WynikR2 w{};
    Uart uart;
    brb::LogSzeregowy l;
    l.inicjalizuj(brb::PortyWeWy{Uart::czytaj, Uart::zapisz, &uart});
    w.s = brb::faza_r2_zaladuj_natywnie(&fw.bs, &fw.obraz, bufor, j.plan, j.meta, cpu, fw.pamiec.dostep(), l, &w.r);
    w.log = uart.tekst;
    std::free(buf);
    F = nullptr;
    return w;
}

void test_faza_r2(const Jadro& j, const char* nazwa) {
    std::printf("[R2] F: transakcja faza_r2_zaladuj_natywnie (%s)\n", nazwa);
    brb::WymaganiaObrazu wym{};
    brb::wymagania_obrazu(j.plan, j.meta, &wym);
    uint64_t pierwsza_baza = 0;
    {   // Sukces.
        Firmware fw;
        const WynikR2 w = uruchom_r2(fw, j);
        if (w.r.blad != brb::BladR2::Brak) std::printf("  blad: %s tablice=%s plan=%u\n", brb::opis_bledu_r2(w.r.blad), brb::opis_bledu_tablic(w.r.blad_tablic), w.r.blad_planu);
        SPRAWDZ(w.s == EFI_SUCCESS && w.r.blad == brb::BladR2::Brak && w.r.etap == brb::EtapR2::ZasobyZwolnione);
        SPRAWDZ(w.r.umiejscowienie.baza != 0x100000 && w.r.umiejscowienie.baza % 0x1000 == 0);
        SPRAWDZ(w.r.umiejscowienie.baza + wym.rozmiar <= SYM_BAZA + SYM_ROZMIAR);
        SPRAWDZ(w.r.tablice.wejscie_pa == w.r.umiejscowienie.baza + (j.meta.wejscie64 - 0x100000));
        SPRAWDZ(w.r.zakresy_nvs == 1 && w.r.bajty_nvs == 0x100000);
        SPRAWDZ(fw.przydzialy.empty() && fw.host.empty() && !fw.zle_zwolnienie && w.r.wlasne_po_sprzataniu == 0 && w.r.bledy_sprzatania == 0);
        pierwsza_baza = w.r.umiejscowienie.baza;
        SPRAWDZ(w.log.find("[BRB] R2: obraz zweryfikowany bajt po bajcie") != std::string::npos);
        SPRAWDZ(w.log.find("[BRB] R2: tablice zweryfikowane niezaleznie") != std::string::npos);
        SPRAWDZ(w.log.find("[BRB] R2 mapa: ACPI NVS 0x0000000000800000-0x0000000000900000") != std::string::npos);
        SPRAWDZ(w.log.find("[BRB] R2: zaden wlasny przydzial nie nachodzi na ACPI NVS") != std::string::npos);
        // Zapisy wylacznie w przydzielonych zakresach (z historii firmware) - nic poza nimi.
        uint64_t poza = 0;
        for (uint64_t i = 0; i < SYM_ROZMIAR; ++i) {
            const uint64_t a = SYM_BAZA + i;
            bool w_przydziale = false;
            for (size_t h = 0; h < fw.historia.size(); ++h) if (a >= fw.historia[h] && a < fw.historia[h] + 64 * 0x1000 + wym.strony_przydzialu * 0x1000) w_przydziale = true;
            if (!w_przydziale && fw.pamiec.b[i] != WZOR) ++poza;
        }
        SPRAWDZ(poza == 0);
    }
    {   // Druga, inna baza: firmware ma zajeta gorna czesc pamieci.
        Firmware fw;
        fw.zajete = {{SYM_BAZA + SYM_ROZMIAR - 0x1800000, SYM_BAZA + SYM_ROZMIAR}};
        const WynikR2 w = uruchom_r2(fw, j);
        SPRAWDZ(w.r.blad == brb::BladR2::Brak && w.r.umiejscowienie.baza != pierwsza_baza && w.r.umiejscowienie.baza + wym.rozmiar <= SYM_BAZA + SYM_ROZMIAR - 0x1800000);
        SPRAWDZ(fw.przydzialy.empty() && fw.host.empty());
    }
    {   // Brak pamieci ciaglej (cala symulacja zajeta).
        Firmware fw;
        fw.zajete = {{SYM_BAZA, SYM_BAZA + SYM_ROZMIAR}};
        const WynikR2 w = uruchom_r2(fw, j);
        SPRAWDZ(w.r.blad == brb::BladR2::BrakPamieciCiaglej && w.s == EFI_OUT_OF_RESOURCES && fw.przydzialy.empty() && fw.host.empty());
        SPRAWDZ(fw.pamiec.zapisy_poza({}) == 0);
    }
    {   // Odmowa puli tablic (2. przydzial MaxAddress) po zaladowaniu: pelne wycofanie.
        Firmware fw;
        fw.max_blad_przy = 1;
        const WynikR2 w = uruchom_r2(fw, j);
        SPRAWDZ(w.r.blad == brb::BladR2::PrzydzialZasobu && w.r.etap == brb::EtapR2::ObrazZweryfikowany);
        SPRAWDZ(fw.przydzialy.empty() && fw.host.empty() && w.r.wlasne_po_sprzataniu == 0);
    }
    {   // Odmowa ostatniego zasobu (informacje): wycofanie podkladu i trzech zasobow.
        Firmware fw;
        fw.max_blad_przy = 4;
        const WynikR2 w = uruchom_r2(fw, j);
        SPRAWDZ(w.r.blad == brb::BladR2::PrzydzialZasobu && fw.przydzialy.empty() && w.r.rejestr.zwolnione == 4);
    }
    {   // FreePages zwraca blad: raport, zasob porzucony (nie zwalniany ponownie).
        Firmware fw;
        fw.free_blad_przy = 3;
        const WynikR2 w = uruchom_r2(fw, j);
        SPRAWDZ(w.r.blad == brb::BladR2::SprzatanieNieudane && w.s == EFI_DEVICE_ERROR && w.r.bledy_sprzatania == 1);
        SPRAWDZ(fw.przydzialy.size() == 1 && !fw.zle_zwolnienie && w.r.wlasne_po_sprzataniu == 0);
    }
    {   // Firmware zwraca adres niewyrownany: odrzucenie przed zapisem, przydzial zwolniony.
        Firmware fw;
        fw.przesuniecie_zwrotu = 0x800;
        const WynikR2 w = uruchom_r2(fw, j);
        SPRAWDZ(w.r.blad == brb::BladR2::Rejestr && fw.pamiec.zapisy_poza({}) == 0);
    }
    {   // Firmware zwraca ten sam adres dwa razy (konflikt z zywym zasobem).
        Firmware fw;
        fw.podwojny = true;
        const WynikR2 w = uruchom_r2(fw, j);
        SPRAWDZ(w.r.blad == brb::BladR2::Rejestr && w.r.etap == brb::EtapR2::ObrazZweryfikowany && w.r.wlasne_po_sprzataniu == 0);
    }
    {   // CPU: brak NX, aktywne LA57, za mala szerokosc fizyczna - odmowa bez przydzialu.
        for (brb::CechyProcesora c : {brb::CechyProcesora{39, false, true, false}, brb::CechyProcesora{39, true, true, true},
                                      brb::CechyProcesora{35, true, true, false}, brb::CechyProcesora{39, true, false, false}}) {
            Firmware fw;
            const WynikR2 w = uruchom_r2(fw, j, c);
            SPRAWDZ(w.r.blad == brb::BladR2::Procesor && fw.max_wywolania == 0 && fw.przydzialy.empty());
        }
    }
    {   // Metadane niezgodne z planem (baza): odmowa przed przydzialem.
        Firmware fw;
        Jadro z(j.d);
        z.meta.baza_wirtualna = 0x200000;
        const WynikR2 w = uruchom_r2(fw, z);
        SPRAWDZ(w.r.blad == brb::BladR2::Wymagania && fw.max_wywolania == 0);
    }
}

/* G. Metadane w pliku: brak, duplikat, wersja, rozmiar, wejscie (polityka sciezki natywnej). */
void test_metadane() {
    std::printf("[R2] G: metadane natywne (polityka odmowy)\n");
    auto bm = [](std::vector<uint8_t> d) {
        brb::PlanLadowania p{};
        brb::MetadaneWejscia m{};
        if (brb::zbuduj_plan_ladowania(d.data(), d.size(), brb::POLITYKA_JADRA_BURSZTYN, &p).blad != brb::BladElf::Brak) return brb::BladMetadanych::BrakDanych;
        return brb::znajdz_metadane_wejscia(d.data(), d.size(), p, &m);
    };
    SPRAWDZ(bm(jadro_syntetyczne()) == brb::BladMetadanych::Brak);
    SPRAWDZ(bm(fixture::jadro_minimalne().b) == brb::BladMetadanych::NieZnaleziono);   // historyczny fixture bez metadanych
    { auto d = jadro_syntetyczne(); std::memcpy(&d[0xF00], &d[0x1040], 8); SPRAWDZ(bm(d) == brb::BladMetadanych::Niejednoznaczne); }
    { auto d = jadro_syntetyczne(); d[0x1048] = 2; SPRAWDZ(bm(d) == brb::BladMetadanych::NieobslugiwanaWersja); }
    { auto d = jadro_syntetyczne(); d[0x104A] = 56; SPRAWDZ(bm(d) == brb::BladMetadanych::ZlyRozmiar); }
    { auto d = jadro_syntetyczne(); d[0x1059] = 0x50; SPRAWDZ(bm(d) == brb::BladMetadanych::WejsciePozaSegmentem); }   // 0x105080
    { auto d = jadro_syntetyczne(); d[0x1059] = 0x20; SPRAWDZ(bm(d) == brb::BladMetadanych::WejscieNiewykonywalne); }  // 0x102080 -> .data
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("BRB - testy hosta Fazy R2\n");
    std::vector<uint8_t> r1b;
    if (argc > 1) r1b = wczytaj(argv[1]);
    test_umiejscowienie();
    test_przesuniecie(&r1b);
    test_ladowanie(jadro_syntetyczne(), "syntetyczne");
    test_rejestr();
    test_metadane();
    Jadro syn(jadro_syntetyczne());
    test_tablice(syn, "syntetyczne");
    test_faza_r2(syn, "syntetyczne");
    if (!r1b.empty()) {
        test_ladowanie(r1b, "jadro R1B");
        Jadro k(r1b);
        SPRAWDZ(k.ok);
        test_tablice(k, "jadro R1B");
        test_faza_r2(k, "jadro R1B");
    } else {
        std::printf("[R2] jadro R1B: NIE URUCHOMIONO (brak pliku)\n");
    }
    std::printf("BRB R2: sprawdzenia=%d bledy=%d\n", g_ok + g_bledy, g_bledy);
    return g_bledy ? 1 : 0;
}
