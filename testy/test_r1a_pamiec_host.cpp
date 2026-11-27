/*
 * Bursztyn OS - Faza R1A: testy hosta architektury pamieci jadra (rozdzielenie PA/VA).
 *
 * Testowany kod PRODUKCYJNY (ta sama kompilacja zrodel co w jadrze):
 *   jadro/mm/adresy.h            - typy adresow i sprawdzana arytmetyka,
 *   jadro/mm/obraz_jadra.h       - uklad obrazu, tlumaczenie okna, plan i budowa dolnych 4 GiB, straznik Ring 3,
 *   jadro/mm/kontrakt_pamieci.h  - walidator kontraktu, klasyfikacja wobec mapy MB2, adapter MB2,
 *   jadro/mm/pmm.cpp             - InicjalizujPMM / InicjalizujPMMZKontraktem, alokacja, zwalnianie, Direct-Map
 *                                  (BURSZTYN_PMM_HOST_TEST: bez instrukcji uprzywilejowanych i bez zerowania przez HHDM).
 *
 * SYMULOWANE (nie produkcyjne): pamiec fizyczna (rejestr zakresow PA -> bufor hosta; test nigdy nie dereferencjonuje
 * dowolnego adresu fizycznego), tlumaczenie przez aktywne tablice stron (model PA == VA albo model z bledem),
 * adresy fizyczne tablic PD/PT podawane budowniczemu oraz przejscie po zbudowanych tablicach.
 * Symulowane mapowanie nie-tozsamosciowe NIE jest dowodem, ze fizycznie przeniesione jadro produkcyjne startuje.
 *
 * Oczekiwane wartosci sa niezalezne od implementacji: stale liczbowe wyliczone recznie albo z definicji
 * (PA = baza_fizyczna + offset), a nie z funkcji testowanych.
 *
 * Uzycie: test-r1a-pamiec-host [--mbi plik:adres_hex:oczekiwanie ...]   oczekiwanie = OK | NVS
 */
#include "jadro/mm/adresy.h"
#include "jadro/mm/obraz_jadra.h"
#include "jadro/mm/kontrakt_pamieci.h"
#include "jadro/mm/pamiec.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

// Publiczne API PMM spoza pamiec.h (deklaracja jak w testy/test_virtio_input_activation.inc).
bool CzyRamkaWolna(uint64_t adres_fizyczny);

using namespace Adresy;
namespace OJ = ObrazJadra;
namespace KP = KontraktPamieci;

static unsigned g_ok = 0, g_bledy = 0;
#define SPRAWDZ(w) do { if (w) { ++g_ok; } else { ++g_bledy; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #w); } } while (0)

/* ---------------------------------------------------------------------------------------------------------------
 * Stale niezalezne: rzeczywisty uklad jadra bazowego (nm/readelf jadra fa8bea5a..., Faza R1A).
 * ------------------------------------------------------------------------------------------------------------- */
constexpr uint64_t K_START = 0x100000, K_TEXT = 0x101000, K_RODATA = 0x22B000, K_DATA = 0x2AA000, K_END = 0xD6F000;
constexpr uint64_t K_ROZMIAR = 0xC6F000;   // 0xD6F000 - 0x100000
// Konce zawartosci PT_LOAD (p_vaddr + p_memsz) z readelf: 0x100050, 0x22AD70, 0x2A9030, 0xD6ED10.
constexpr KP::SymboleObrazu SYMBOLE_PRODUKCYJNE = {K_START, 0x100050, K_TEXT, 0x22AD70, K_RODATA, 0x2A9030, K_DATA, 0xD6ED10, K_END};

static void segmenty_produkcyjne(OJ::Segment* s) {
    s[0] = {0x100000, 0x1000, 0x50, OJ::ODCZYT, 0};
    s[1] = {0x101000, 0x12A000, 0x129D70, OJ::ODCZYT | OJ::WYKONANIE, 0};
    s[2] = {0x22B000, 0x7F000, 0x7E030, OJ::ODCZYT, 0};
    s[3] = {0x2AA000, 0xAC5000, 0xAC4D10, OJ::ODCZYT | OJ::ZAPIS, 0};
}

static OJ::Uklad uklad_na(uint64_t pa0) {
    OJ::Segment s[4];
    segmenty_produkcyjne(s);
    OJ::Uklad u{};
    const OJ::BladUkladu b = OJ::zbuduj_uklad(K_START, K_ROZMIAR, pa0, s, 4, &u);
    if (b != OJ::BladUkladu::OK) std::printf("  (uklad 0x%llx: %s)\n", (unsigned long long)pa0, OJ::opis_bledu_ukladu(b));
    return u;
}

/* ---------------------------------------------------------------------------------------------------------------
 * A. Typy adresow i sprawdzana arytmetyka.
 * ------------------------------------------------------------------------------------------------------------- */
static void test_adresy() {
    static_assert(!std::is_convertible<uint64_t, AdresFizyczny>::value, "brak niejawnej konwersji u64 -> PA");
    static_assert(!std::is_convertible<AdresFizyczny, AdresWirtualny>::value, "brak niejawnej konwersji PA -> VA");
    static_assert(!std::is_convertible<AdresWirtualny, AdresFizyczny>::value, "brak niejawnej konwersji VA -> PA");
    static_assert(!std::is_convertible<AdresFizyczny, uint64_t>::value, "brak niejawnej konwersji PA -> u64");
    static_assert(!std::is_convertible<NumerRamki, AdresFizyczny>::value, "ramka != adres");
    uint64_t w = 7;
    SPRAWDZ(!dodaj_u64(UINT64_MAX, 1, &w) && w == 7);
    SPRAWDZ(dodaj_u64(UINT64_MAX - 1, 1, &w) && w == UINT64_MAX);
    SPRAWDZ(!odejmij_u64(4, 5, &w));
    SPRAWDZ(odejmij_u64(5, 5, &w) && w == 0);
    SPRAWDZ(!pomnoz_u64(0x100000000ULL, 0x100000000ULL, &w));
    SPRAWDZ(pomnoz_u64(0, UINT64_MAX, &w) && w == 0);
    SPRAWDZ(wyrownaj_w_gore_do_strony(0x1001, &w) && w == 0x2000);
    SPRAWDZ(wyrownaj_w_gore_do_strony(0x2000, &w) && w == 0x2000);
    SPRAWDZ(!wyrownaj_w_gore_do_strony(UINT64_MAX - 10, &w));
    SPRAWDZ(wyrownaj_w_dol_do_strony(0x2FFF) == 0x2000);
    LiczbaStron ls;
    SPRAWDZ(bajty_na_strony_w_gore(RozmiarBajtow(4097), &ls) && ls.wartosc == 2);
    SPRAWDZ(bajty_na_strony_w_gore(RozmiarBajtow(0), &ls) && ls.wartosc == 0);
    SPRAWDZ(!bajty_na_strony_w_gore(RozmiarBajtow(UINT64_MAX), &ls));
    RozmiarBajtow rb;
    SPRAWDZ(strony_na_bajty(LiczbaStron(3), &rb) && rb.wartosc == 0x3000);
    SPRAWDZ(!strony_na_bajty(LiczbaStron(1ULL << 53), &rb));
    // Pelna reprezentacja 64-bitowa i 52-bitowy limit architektury.
    SPRAWDZ(zakres_fizyczny_poprawny(AdresFizyczny(0x1000000000ULL), RozmiarBajtow(0x1000)));   // 64 GiB
    SPRAWDZ(zakres_fizyczny_poprawny(AdresFizyczny(0xFFFFFFFFFF000ULL), RozmiarBajtow(0x1000)));
    SPRAWDZ(!zakres_fizyczny_poprawny(AdresFizyczny(0xFFFFFFFFFF000ULL), RozmiarBajtow(0x2000)));
    SPRAWDZ(!zakres_fizyczny_poprawny(AdresFizyczny(0x1000), RozmiarBajtow(0)));
    SPRAWDZ(!zakres_fizyczny_poprawny(AdresFizyczny(UINT64_MAX - 4), RozmiarBajtow(16)));
    SPRAWDZ(!fizyczny_w_limicie(AdresFizyczny(1ULL << 52)));
    AdresFizyczny pa;
    SPRAWDZ(przesun_fizyczny(AdresFizyczny(0xFFFFFFFF000ULL), RozmiarBajtow(0x2000), &pa) && pa.wartosc == 0x100000001000ULL);
    SPRAWDZ(!przesun_fizyczny(AdresFizyczny((1ULL << 52) - 1), RozmiarBajtow(1), &pa));
    NumerRamki r;
    SPRAWDZ(ramka_z_adresu(AdresFizyczny(0x5000), &r) && r.wartosc == 5);
    SPRAWDZ(!ramka_z_adresu(AdresFizyczny(0x5008), &r));
    SPRAWDZ(adres_ramki(NumerRamki(0x123456789ULL), &pa) && pa.wartosc == 0x123456789000ULL);   // > 4 GiB bez obciecia
    SPRAWDZ(!adres_ramki(NumerRamki(1ULL << 40), &pa));
    SPRAWDZ(!kanoniczny(AdresWirtualny(0x0000800000000000ULL)));
    SPRAWDZ(kanoniczny(AdresWirtualny(0xFFFF800000000000ULL)));
    SPRAWDZ(kanoniczny(AdresWirtualny(0x00007FFFFFFFF000ULL)));
    SPRAWDZ(!zakres_wirtualny_poprawny(AdresWirtualny(0x00007FFFFFFFF000ULL), RozmiarBajtow(0x2000)));
    SPRAWDZ(zakresy_nachodza(0x1000, 0x1000, 0x1FFF, 1));
    SPRAWDZ(!zakresy_nachodza(0x1000, 0x1000, 0x2000, 1));
    SPRAWDZ(zakresy_nachodza(0x1000, 0, 0x5000, 1));   // niepoprawny = nachodzi (fail-closed)
    SPRAWDZ(zakres_zawiera(0x1000, 0x2000, 0x2000, 0x1000));
    SPRAWDZ(!zakres_zawiera(0x1000, 0x2000, 0x2000, 0x1001));
}

/* ---------------------------------------------------------------------------------------------------------------
 * B. Uklad obrazu i tlumaczenie.
 * ------------------------------------------------------------------------------------------------------------- */
static void test_uklad() {
    const OJ::Uklad tozs = uklad_na(0x100000);
    SPRAWDZ(tozs.poprawny && OJ::tozsamosciowy(tozs));
    AdresFizyczny pa;
    AdresWirtualny va;
    SPRAWDZ(OJ::va_na_pa(tozs, AdresWirtualny(0x1193E2), &pa) && pa.wartosc == 0x1193E2);   // _start

    // Rozne bazy fizyczne: VA stale, PA = baza + offset (wartosci wyliczone recznie).
    struct Przypadek { uint64_t baza, va, pa_oczekiwany; };
    const Przypadek p[] = {
        {0x3456000, 0x223456, 0x3579456},
        {0x1000000, 0xA25000, 0x1925000},          // pml4_table
        {0x40000000, 0xD6EFFF, 0x40C6EFFF},        // ostatni bajt okna
        {0xF3000000, 0x100000, 0xF3000000},
        {0x2100000, 0x2AA000, 0x22AA000},          // przesuniecie wyrownane do 2 MiB
    };
    for (const Przypadek& c : p) {
        const OJ::Uklad u = uklad_na(c.baza);
        SPRAWDZ(u.poprawny && !OJ::tozsamosciowy(u));
        SPRAWDZ(u.va_poczatek == 0x100000 && u.va_koniec == 0xD6F000);   // VA stabilne niezaleznie od bazy
        SPRAWDZ(OJ::va_na_pa(u, AdresWirtualny(c.va), &pa) && pa.wartosc == c.pa_oczekiwany);
        SPRAWDZ(OJ::pa_na_va(u, AdresFizyczny(c.pa_oczekiwany), &va) && va.wartosc == c.va);
        SPRAWDZ(!OJ::va_na_pa(u, AdresWirtualny(0xFFFFF), &pa));
        SPRAWDZ(!OJ::va_na_pa(u, AdresWirtualny(0xD6F000), &pa));
        SPRAWDZ(!OJ::va_na_pa(u, AdresWirtualny(0), &pa));
        SPRAWDZ(!OJ::va_na_pa(u, AdresWirtualny(0xFFFF800000100000ULL), &pa));   // HHDM to nie okno obrazu
        SPRAWDZ(!OJ::pa_na_va(u, AdresFizyczny(c.baza - 1), &va));
        SPRAWDZ(!OJ::pa_na_va(u, AdresFizyczny(c.baza + K_ROZMIAR), &va));
        SPRAWDZ(!OJ::zakres_va_na_pa(u, AdresWirtualny(0xD6E000), RozmiarBajtow(0x2000), &pa));
        SPRAWDZ(OJ::zakres_va_na_pa(u, AdresWirtualny(0xD6E000), RozmiarBajtow(0x1000), &pa));
        OffsetObrazu o;
        SPRAWDZ(OJ::offset_va(u, AdresWirtualny(0x22B000), &o) && o.wartosc == 0x12B000);
        SPRAWDZ(OJ::uprawnienia_va(u, 0x150000) == (OJ::ODCZYT | OJ::WYKONANIE));
        SPRAWDZ(OJ::uprawnienia_va(u, 0x300000) == (OJ::ODCZYT | OJ::ZAPIS));
        // Straznik Ring 3: okno VA i ramki podkladu niedostepne dla mapowan uzytkownika.
        SPRAWDZ(!OJ::mapowanie_uzytkownika_dozwolone(u, 0x200000, 0x7000000));
        SPRAWDZ(!OJ::mapowanie_uzytkownika_dozwolone(u, RING3_BASE, c.baza));
        SPRAWDZ(!OJ::mapowanie_uzytkownika_dozwolone(u, RING3_BASE, c.baza + K_ROZMIAR - 0x1000));
        SPRAWDZ(OJ::mapowanie_uzytkownika_dozwolone(u, RING3_BASE, c.baza + K_ROZMIAR));
    }
    // Niewyrownane wejscia, przepelnienia, limity.
    OJ::Segment s[4];
    segmenty_produkcyjne(s);
    OJ::Uklad u{};
    SPRAWDZ(OJ::zbuduj_uklad(0x100800, K_ROZMIAR, 0x100000, s, 4, &u) == OJ::BladUkladu::VA_NIEWYROWNANY);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x3456100, s, 4, &u) == OJ::BladUkladu::PA_NIEWYROWNANY);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR + 1, 0x100000, s, 4, &u) == OJ::BladUkladu::ROZMIAR_NIEPOPRAWNY);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, 0, 0x100000, s, 4, &u) == OJ::BladUkladu::ROZMIAR_NIEPOPRAWNY);
    SPRAWDZ(OJ::zbuduj_uklad(0xFFFFFFFFFFFFF000ULL, K_ROZMIAR, 0x100000, s, 4, &u) == OJ::BladUkladu::VA_POZA_OKNEM_4GIB);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0xFF000, s, 4, &u) == OJ::BladUkladu::PA_PONIZEJ_1MIB);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0xFFFFFFFFFF000ULL, s, 4, &u) == OJ::BladUkladu::PA_POZA_LIMITEM_ARCHITEKTURY);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0xFFFFFFFFFFFFF000ULL, s, 4, &u) == OJ::BladUkladu::PA_POZA_LIMITEM_ARCHITEKTURY);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000000ULL, s, 4, &u) == OJ::BladUkladu::PA_POWYZEJ_4GIB_TYMCZASOWE);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0xFF400000ULL, s, 4, &u) == OJ::BladUkladu::PA_POWYZEJ_4GIB_TYMCZASOWE);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, s, 0, &u) == OJ::BladUkladu::ZA_DUZO_SEGMENTOW);
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, s, 9, &u) == OJ::BladUkladu::ZA_DUZO_SEGMENTOW);
    OJ::Segment z[4];
    std::memcpy(z, s, sizeof z); z[2].va += 0x1000; z[2].rozmiar -= 0x1000;   // dziura
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, z, 4, &u) == OJ::BladUkladu::SEGMENTY_NIESPOJNE);
    std::memcpy(z, s, sizeof z); z[3].rozmiar += 0x1000;   // poza oknem
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, z, 4, &u) == OJ::BladUkladu::SEGMENTY_NIESPOJNE);
    std::memcpy(z, s, sizeof z); z[1].uprawnienia |= OJ::ZAPIS;
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, z, 4, &u) == OJ::BladUkladu::SEGMENT_ZAPIS_I_WYKONANIE);
    std::memcpy(z, s, sizeof z); z[0].uprawnienia = 0;
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, z, 4, &u) == OJ::BladUkladu::SEGMENT_UPRAWNIENIA);
    std::memcpy(z, s, sizeof z); z[1].zajete = 0;
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, z, 4, &u) == OJ::BladUkladu::SEGMENTY_NIESPOJNE);
    std::memcpy(z, s, sizeof z); z[1].zajete = z[1].rozmiar + 1;
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, z, 4, &u) == OJ::BladUkladu::SEGMENTY_NIESPOJNE);
    {   // bajty segmentow vs luz stron: 0x100050..0x101000 to luz za .boot
        const OJ::Uklad t = uklad_na(0x100000);
        SPRAWDZ(OJ::bajty_segmentow_przecinaja(t, 0x100040, 0x20));
        SPRAWDZ(!OJ::bajty_segmentow_przecinaja(t, 0x100080, 0x6C8));
        SPRAWDZ(!OJ::bajty_segmentow_przecinaja(t, 0xD6ED10, 0x2F0));
        SPRAWDZ(OJ::bajty_segmentow_przecinaja(t, 0xD6ED0F, 2));
        SPRAWDZ(OJ::podklad_przecina(t, 0x100080, 0x6C8));   // ale strona nalezy do podkladu
    }
    std::memcpy(z, s, sizeof z); z[0].uprawnienia |= 8;
    SPRAWDZ(OJ::zbuduj_uklad(K_START, K_ROZMIAR, 0x100000, z, 4, &u) == OJ::BladUkladu::SEGMENT_UPRAWNIENIA);
    // Niezainstalowany uklad: wszystko fail-closed.
    const OJ::Uklad pusty{};
    SPRAWDZ(!OJ::va_na_pa(pusty, AdresWirtualny(0x100000), &pa));
    SPRAWDZ(!OJ::mapowanie_uzytkownika_dozwolone(pusty, RING3_BASE, 0x7000000));
    SPRAWDZ(OJ::podklad_przecina(pusty, 0x7000000, 0x1000));
}

/* ---------------------------------------------------------------------------------------------------------------
 * C. Plan i budowa dolnych 4 GiB (kod produkcyjny vmm.cpp: zbuduj_dolne_4gib) + symulowane przejscie po tablicach.
 * ------------------------------------------------------------------------------------------------------------- */
struct Tablice {
    alignas(4096) uint64_t pd[4][512];
    alignas(4096) uint64_t pt0[512];
    alignas(4096) uint64_t pula[8][512];
};
constexpr uint64_t PA_PT0 = 0x7F000000, PA_PULA = 0x7F100000;   // symulowane PA tablic

static uint64_t pa_symulowane(const Tablice& t, const uint64_t* tab) {
    if (tab == t.pt0) return PA_PT0;
    for (int i = 0; i < 8; ++i) if (tab == t.pula[i]) return PA_PULA + 0x1000ULL * i;
    return 0;
}

static const uint64_t* tablica_z_pa(const Tablice& t, uint64_t pa) {
    if (pa == PA_PT0) return t.pt0;
    for (int i = 0; i < 8; ++i) if (pa == PA_PULA + 0x1000ULL * i) return t.pula[i];
    return nullptr;
}

/* Przejscie: false = brak mapowania; *wpis = lisc (dla kontroli flag). */
static bool przejdz(const Tablice& t, uint64_t va, uint64_t* pa, uint64_t* wpis) {
    const uint64_t e = t.pd[va >> 30][(va >> 21) & 511];
    if (!(e & 1)) return false;
    if (e & 0x80) { *pa = (e & OJ::MASKA_ADRESU_2M) + (va & 0x1FFFFF); *wpis = e; return true; }
    const uint64_t* pt = tablica_z_pa(t, e & OJ::MASKA_ADRESU_4K);
    if (!pt) return false;
    const uint64_t l = pt[(va >> 12) & 511];
    if (!(l & 1)) return false;
    *pa = (l & OJ::MASKA_ADRESU_4K) + (va & 0xFFF);
    *wpis = l;
    return true;
}

static bool zbuduj(const OJ::Uklad& u, Tablice& t, size_t pula, size_t* uzyte) {
    std::memset(&t, 0xCC, sizeof t);   // smieci: budowniczy musi zapisac kazdy wpis
    OJ::TabliceDolne4GiB d{t.pd, t.pt0, t.pula, pula, 0};
    const bool ok = OJ::zbuduj_dolne_4gib(u, d, [&](const uint64_t* tab) { return pa_symulowane(t, tab); });
    if (uzyte) *uzyte = d.uzyte_pt;
    return ok;
}

static void test_plan_tozsamosciowy() {
    // Oczekiwany uklad SPRZED R1A (vmm.cpp przygotuj_mapowania_jadra), zbudowany niezaleznie od planu.
    static Tablice t;
    size_t uzyte = 99;
    SPRAWDZ(zbuduj(uklad_na(0x100000), t, 8, &uzyte));
    SPRAWDZ(uzyte == 0);   // obraz tozsamosciowy nie uzywa puli
    bool zgodne = true;
    for (uint64_t g = 0; g < 4; ++g) {
        for (uint64_t i = 0; i < 512; ++i) {
            uint64_t ocz;
            if (g == 0 && i == 0) {
                ocz = PA_PT0 | 0x3;
            } else {
                const uint64_t fiz = g * 0x40000000ULL + i * 0x200000ULL;
                ocz = fiz | 0x3 | 0x80;
                if (fiz == 0xFEE00000ULL || fiz == 0xFEC00000ULL) ocz |= 0x18;
            }
            if (t.pd[g][i] != ocz) zgodne = false;
        }
    }
    SPRAWDZ(zgodne);
    bool pt_zgodne = t.pt0[0] == 0;
    for (uint64_t i = 1; i < 512; ++i) if (t.pt0[i] != ((i * 0x1000) | 0x3)) pt_zgodne = false;
    SPRAWDZ(pt_zgodne);
}

static void test_plan_nietozsamosciowy() {
    const uint64_t bazy[] = {0x1000000, 0x3456000, 0x2100000, 0x40000000, 0x200000, 0xF3000000};
    static Tablice t;
    for (uint64_t baza : bazy) {
        const OJ::Uklad u = uklad_na(baza);
        size_t uzyte = 0;
        SPRAWDZ(zbuduj(u, t, 8, &uzyte));
        SPRAWDZ(uzyte <= 8 && uzyte == OJ::potrzebne_tablice_pt(u));
        uint64_t pa = 0, e = 0;
        SPRAWDZ(!przejdz(t, 0, &pa, &e));                               // NULL
        SPRAWDZ(przejdz(t, 0xFF000, &pa, &e) && pa == 0xFF000);         // niski 1 MiB: tozsamosc
        bool okno = true, flagi = true;
        for (uint64_t va = K_START; va < K_END; va += 0x1000) {
            if (!przejdz(t, va, &pa, &e) || pa != baza + (va - K_START)) okno = false;
            if (e & 0x4) flagi = false;                                 // supervisor-only
        }
        SPRAWDZ(okno);
        SPRAWDZ(flagi);
        // Alias podkladu (poza oknem) niezmapowany; strona tuz za podkladem - tozsamosc.
        bool alias = true;
        for (uint64_t pa_s = baza; pa_s < baza + K_ROZMIAR; pa_s += 0x1000) {
            if (pa_s >= K_START && pa_s < K_END) continue;
            if (przejdz(t, pa_s, &pa, &e)) alias = false;
        }
        SPRAWDZ(alias);
        if (!(baza + K_ROZMIAR >= K_START && baza + K_ROZMIAR < K_END)) {
            SPRAWDZ(przejdz(t, baza + K_ROZMIAR, &pa, &e) && pa == baza + K_ROZMIAR);
        }
        if (!(K_END >= baza && K_END < baza + K_ROZMIAR)) {
            SPRAWDZ(przejdz(t, K_END, &pa, &e) && pa == K_END);         // za oknem: tozsamosc
        }
        SPRAWDZ(przejdz(t, 0xFEE00000, &pa, &e) && pa == 0xFEE00000 && (e & 0x18) == 0x18);
        SPRAWDZ(przejdz(t, 0xFEC00000, &pa, &e) && pa == 0xFEC00000 && (e & 0x18) == 0x18);
        SPRAWDZ(przejdz(t, RING3_BASE + 0x40000000ULL, &pa, &e) || (RING3_BASE + 0x40000000ULL >= baza &&
                RING3_BASE + 0x40000000ULL < baza + K_ROZMIAR));
        bool bez_user = true;
        for (uint64_t g = 0; g < 4; ++g) for (uint64_t i = 0; i < 512; ++i) if (t.pd[g][i] & 0x4) bez_user = false;
        for (uint64_t i = 0; i < 512; ++i) if (t.pt0[i] & 0x4) bez_user = false;
        SPRAWDZ(bez_user);
    }
    // Przesuniecie wyrownane do 2 MiB: regiony w calosci w oknie jako strony 2 MiB.
    {
        const OJ::Uklad u = uklad_na(0x2100000);
        const OJ::PlanRegionu r = OJ::zaplanuj_region_2m(u, 0x400000);
        SPRAWDZ(r.rodzaj == OJ::RodzajRegionu::LISC_2M && r.pa_2m == 0x2400000);
    }
    // Wyczerpanie puli i blad tlumaczenia tablicy: fail-closed.
    {
        static Tablice t2;
        SPRAWDZ(!zbuduj(uklad_na(0x3456000), t2, 1, nullptr));
        OJ::TabliceDolne4GiB d{t2.pd, t2.pt0, t2.pula, 8, 0};
        SPRAWDZ(!OJ::zbuduj_dolne_4gib(uklad_na(0x3456000), d, [](const uint64_t*) { return uint64_t(0); }));
        SPRAWDZ(!OJ::zbuduj_dolne_4gib(OJ::Uklad{}, d, [](const uint64_t*) { return uint64_t(0x1000); }));
    }
    // Przeglad baz: kazda wyrownana baza [1 MiB, 4 GiB - rozmiar] krokiem 0x1F3000 miesci sie w puli 8 PT.
    size_t maks = 0, przypadki = 0;
    for (uint64_t baza = 0x100000; baza + K_ROZMIAR <= 0x100000000ULL; baza += 0x1F3000) {
        const size_t n = OJ::potrzebne_tablice_pt(uklad_na(baza));
        if (n > maks) maks = n;
        ++przypadki;
    }
    std::printf("  przeglad baz: %zu przypadkow, maks. PT puli = %zu (limit 8)\n", przypadki, maks);
    SPRAWDZ(przypadki > 2000 && maks <= 8);
}

/* ---------------------------------------------------------------------------------------------------------------
 * Symulowana pamiec fizyczna: zarejestrowane zakresy PA -> bufory hosta. Nic poza nimi nie jest dostepne.
 * ------------------------------------------------------------------------------------------------------------- */
struct Region { uint64_t pa; std::vector<uint8_t> dane; };
static std::vector<Region> g_pamiec;

static const uint8_t* dostep_sym(uint64_t pa, uint64_t n) {
    for (const Region& r : g_pamiec) {
        if (n && pa >= r.pa && pa - r.pa <= r.dane.size() && n <= r.dane.size() - (pa - r.pa)) return r.dane.data() + (pa - r.pa);
    }
    return nullptr;
}

/* Konstruktor MBI (MB2 spec 3.6). */
struct Mbi {
    std::vector<uint8_t> b = std::vector<uint8_t>(8, 0);
    void tag(uint32_t typ, const std::vector<uint8_t>& dane) {
        const uint32_t rozmiar = 8 + static_cast<uint32_t>(dane.size());
        const size_t o = b.size();
        b.resize(o + ((rozmiar + 7) & ~7U), 0);
        std::memcpy(&b[o], &typ, 4);
        std::memcpy(&b[o + 4], &rozmiar, 4);
        if (!dane.empty()) std::memcpy(&b[o + 8], dane.data(), dane.size());
    }
    void mapa(const std::vector<WpisMapyPamieciMB2>& wpisy) {
        std::vector<uint8_t> d(8 + 24 * wpisy.size());
        const uint32_t w = 24, v = 0;
        std::memcpy(&d[0], &w, 4);
        std::memcpy(&d[4], &v, 4);
        if (!wpisy.empty()) std::memcpy(&d[8], wpisy.data(), 24 * wpisy.size());
        tag(6, d);
    }
    void modul(uint32_t s, uint32_t e) { std::vector<uint8_t> d(8 + 8, 0); std::memcpy(&d[0], &s, 4); std::memcpy(&d[4], &e, 4); tag(3, d); }
    void framebuffer(uint64_t a, uint32_t pitch, uint32_t w, uint32_t h) {
        std::vector<uint8_t> d(24, 0);
        std::memcpy(&d[0], &a, 8); std::memcpy(&d[8], &pitch, 4); std::memcpy(&d[12], &w, 4); std::memcpy(&d[16], &h, 4);
        d[20] = 32; d[21] = 1;
        tag(8, d);
    }
    void baza(uint32_t a) { std::vector<uint8_t> d(4); std::memcpy(&d[0], &a, 4); tag(21, d); }
    std::vector<uint8_t> zakoncz() {
        tag(0, {});
        const uint32_t n = static_cast<uint32_t>(b.size());
        std::memcpy(&b[0], &n, 4);
        return b;
    }
};

static bool tlumacz_tozsamosc(uint64_t va, uint64_t* pa) { *pa = va; return true; }

static std::vector<WpisMapyPamieciMB2> mapa_ram_64mib() {
    return {{0x0, 0x9F000, 1, 0}, {0x9F000, 0x61000, 2, 0}, {0x100000, 0x3F00000, 1, 0}, {0xFEC00000, 0x1000, 2, 0}};
}

/* ---------------------------------------------------------------------------------------------------------------
 * D. Adapter MB2 (kod produkcyjny) na symulowanej pamieci.
 * ------------------------------------------------------------------------------------------------------------- */
static std::vector<uint8_t> mbi_typowe() {
    Mbi m;
    m.mapa(mapa_ram_64mib());
    m.modul(0x1000000, 0x1234567);
    m.modul(0, 0x1000);                      // pomijany (start 0) - jak dotychczasowy PMM
    m.framebuffer(0xFD000000ULL, 5120, 1280, 720);
    m.baza(0x100000);
    return m.zakoncz();
}

static KP::Blad adapter(uint64_t mbi_pa, const std::vector<uint8_t>& mbi, bool (*tl)(uint64_t, uint64_t*), KP::KontraktV1* k,
                        uint64_t cr3 = 0xA25000) {
    g_pamiec = {{mbi_pa, mbi}};
    return KP::zbuduj_z_multiboot2(mbi_pa, SYMBOLE_PRODUKCYJNE, cr3, dostep_sym, tl, k);
}

static uint64_t g_zla_strona = 0;
static bool tlumacz_nieciagle(uint64_t va, uint64_t* pa) { *pa = va == g_zla_strona ? va + 0x200000 : va; return true; }
static bool tlumacz_dziura(uint64_t va, uint64_t* pa) { *pa = va; return va != g_zla_strona; }

static void test_adapter_mb2() {
    KP::KontraktV1 k{};
    SPRAWDZ(adapter(0x2000, mbi_typowe(), tlumacz_tozsamosc, &k) == KP::Blad::OK);
    SPRAWDZ(k.zrodlo == KP::ZRODLO_MB2_ZGODNOSC && k.jadro_baza_fizyczna == 0x100000 && k.jadro_baza_wirtualna == 0x100000);
    SPRAWDZ(k.jadro_rozmiar == 0xC6F000 && k.liczba_segmentow == 4);
    SPRAWDZ(k.segmenty[1].va == 0x101000 && k.segmenty[1].rozmiar == 0x12A000 && k.segmenty[1].uprawnienia == 5);
    SPRAWDZ(k.cr3_rozruchu == 0xA25000 && (k.flagi & KP::FLAGA_CR3_W_OBRAZIE));
    SPRAWDZ(k.mapa_adres == 0x2008 && k.mapa_format == KP::MAPA_MB2_TAG6 && k.mapa_rozmiar == 16 + 4 * 24);
    SPRAWDZ(k.liczba_zakresow == 4);   // obraz, MBI, 1 modul (drugi pominiety), framebuffer
    SPRAWDZ(k.zakresy[2].rodzaj == KP::RODZAJ_MODUL && k.zakresy[2].poczatek == 0x1000000 && k.zakresy[2].dlugosc == 0x234567);
    SPRAWDZ(k.zakresy[3].rodzaj == KP::RODZAJ_FRAMEBUFFER && k.zakresy[3].dlugosc == 5120ULL * 720);
    const KP::OczekiwanyObraz ocz = KP::oczekiwany_z_symboli(SYMBOLE_PRODUKCYJNE);
    OJ::Uklad u{};
    SPRAWDZ(KP::waliduj(&k, sizeof k, &ocz, &u) == KP::Blad::OK && OJ::tozsamosciowy(u));

    g_zla_strona = 0x500000;
    SPRAWDZ(adapter(0x2000, mbi_typowe(), tlumacz_nieciagle, &k) == KP::Blad::PODKLAD_NIECIAGLY);
    SPRAWDZ(adapter(0x2000, mbi_typowe(), tlumacz_dziura, &k) == KP::Blad::TLUMACZENIE_OBRAZU);
    g_zla_strona = 0x100000;
    SPRAWDZ(adapter(0x2000, mbi_typowe(), tlumacz_dziura, &k) == KP::Blad::TLUMACZENIE_OBRAZU);

    { Mbi m; m.mapa(mapa_ram_64mib()); m.baza(0x200000); SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::MB2_TAG21_NIEZGODNY); }
    { Mbi m; m.mapa(mapa_ram_64mib()); m.mapa(mapa_ram_64mib()); SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::MB2_WIELE_MAP); }
    { Mbi m; m.modul(0x1000000, 0x1001000); SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::MB2_BRAK_MAPY); }
    { Mbi m; m.mapa(mapa_ram_64mib()); m.framebuffer(0xFD000000ULL, 4, 1, 1); m.framebuffer(0xFD000000ULL, 4, 1, 1);
      SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::MB2_WIELE_FRAMEBUFFEROW); }
    { Mbi m; m.mapa(mapa_ram_64mib()); m.modul(0x1000000, 0x1000000 + 0x10000001U);   // > 256 MiB: pominiety (D-01)
      SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::OK && k.liczba_zakresow == 2); }
    { Mbi m; m.mapa(mapa_ram_64mib()); m.modul(0x2000, 0x3000);   // modul na MBI: podwojna wlasnosc
      SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::OK);
      SPRAWDZ(KP::waliduj(&k, sizeof k, &ocz, &u) == KP::Blad::DUPLIKAT_WLASNOSCI); }
    { Mbi m; m.mapa(mapa_ram_64mib()); m.modul(0x800000, 0x900000);   // modul na obrazie jadra
      SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::OK);
      SPRAWDZ(KP::waliduj(&k, sizeof k, &ocz, &u) == KP::Blad::DUPLIKAT_WLASNOSCI); }
    // MBI w luzie strony .boot (GRUB 2.12 BIOS: 0x100080) - dozwolone dla MB2; MBI na bajtach segmentu - nie.
    SPRAWDZ(adapter(0x100080, mbi_typowe(), tlumacz_tozsamosc, &k) == KP::Blad::OK);
    SPRAWDZ(KP::waliduj(&k, sizeof k, &ocz, &u) == KP::Blad::OK);
    SPRAWDZ(adapter(0x100048, mbi_typowe(), tlumacz_tozsamosc, &k) == KP::Blad::OK);
    SPRAWDZ(KP::waliduj(&k, sizeof k, &ocz, &u) == KP::Blad::DUPLIKAT_WLASNOSCI);
    { Mbi m; m.mapa(mapa_ram_64mib()); m.modul(0xD6F000 - 0x200, 0xD6F000 + 0x1000);   // modul od luzu za .bss
      SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::OK && KP::waliduj(&k, sizeof k, &ocz, &u) == KP::Blad::OK); }
    { Mbi m; m.mapa(mapa_ram_64mib()); m.modul(0xD6ED00, 0xD70000);   // modul na koncu .bss
      SPRAWDZ(adapter(0x2000, m.zakoncz(), tlumacz_tozsamosc, &k) == KP::Blad::OK && KP::waliduj(&k, sizeof k, &ocz, &u) == KP::Blad::DUPLIKAT_WLASNOSCI); }
    // Uszkodzone tagi.
    std::vector<uint8_t> b = mbi_typowe();
    { auto c = b; uint32_t z = 4; std::memcpy(&c[12], &z, 4); SPRAWDZ(adapter(0x2000, c, tlumacz_tozsamosc, &k) == KP::Blad::MB2_TAGI_USZKODZONE); }
    { auto c = b; uint32_t z = 0xFFFF; std::memcpy(&c[12], &z, 4); SPRAWDZ(adapter(0x2000, c, tlumacz_tozsamosc, &k) == KP::Blad::MB2_TAGI_USZKODZONE); }
    { auto c = b; c.resize(c.size() - 8); uint32_t n = uint32_t(c.size()); std::memcpy(&c[0], &n, 4);
      SPRAWDZ(adapter(0x2000, c, tlumacz_tozsamosc, &k) == KP::Blad::MB2_TAGI_USZKODZONE); }
    { auto c = b; uint32_t n = uint32_t(c.size()) + 64; std::memcpy(&c[0], &n, 4);   // rozmiar poza pamiecia
      SPRAWDZ(adapter(0x2000, c, tlumacz_tozsamosc, &k) == KP::Blad::MB2_BRAK_INFORMACJI); }
    SPRAWDZ(adapter(0x2004, b, tlumacz_tozsamosc, &k) == KP::Blad::MB2_BRAK_INFORMACJI);
    g_pamiec.clear();
    SPRAWDZ(KP::zbuduj_z_multiboot2(0x2000, SYMBOLE_PRODUKCYJNE, 0, dostep_sym, tlumacz_tozsamosc, &k) == KP::Blad::MB2_BRAK_INFORMACJI);
}

/* ---------------------------------------------------------------------------------------------------------------
 * E. Walidator kontraktu: mutacje deterministyczne i losowe (xorshift, staly zarodek).
 * ------------------------------------------------------------------------------------------------------------- */
static KP::KontraktV1 kontrakt_nietozsamosciowy(uint64_t pa0) {
    KP::KontraktV1 k{};
    k.sygnatura = KP::SYGNATURA; k.wersja = KP::WERSJA; k.rozmiar = sizeof(KP::KontraktV1);
    k.zrodlo = KP::ZRODLO_BRB_NATYWNE_PRZYSZLE; k.flagi = KP::FLAGA_TOZSAMOSC_4GIB_PRZED_VMM; k.model_podkladu = KP::PODKLAD_CIAGLY;
    k.jadro_baza_wirtualna = K_START; k.jadro_baza_fizyczna = pa0; k.jadro_rozmiar = K_ROZMIAR;
    k.liczba_segmentow = 4; segmenty_produkcyjne(k.segmenty);
    k.informacje_adres = 0x20000000; k.informacje_rozmiar = 0x1000;
    k.mapa_adres = 0x20000008; k.mapa_rozmiar = 16 + 4 * 24; k.mapa_format = KP::MAPA_MB2_TAG6; k.mapa_rozmiar_wpisu = 24;
    k.cr3_rozruchu = 0x20100000;
    k.liczba_zakresow = 3;
    k.zakresy[0] = {pa0, K_ROZMIAR, KP::RODZAJ_OBRAZ_JADRA, KP::ZYCIE_TRWALE, 0, 0};
    k.zakresy[1] = {0x20000000, 0x1000, KP::RODZAJ_INFORMACJE_ROZRUCHU, KP::ZYCIE_TRWALE, KP::ZAKRES_DOSTEP_TOZSAMOSCIOWY, 0};
    k.zakresy[2] = {0x20100000, 0x6000, KP::RODZAJ_TABLICE_STRON_ROZRUCHU, KP::ZYCIE_DO_PRZELACZENIA_CR3, 0, 0};
    return k;
}

static void test_walidator() {
    const KP::OczekiwanyObraz ocz = KP::oczekiwany_z_symboli(SYMBOLE_PRODUKCYJNE);
    OJ::Uklad u{};
    const KP::KontraktV1 dobry = kontrakt_nietozsamosciowy(0x3000000);
    SPRAWDZ(KP::waliduj(&dobry, sizeof dobry, &ocz, &u) == KP::Blad::OK && u.pa_poczatek == 0x3000000);
    // Bufor niewyrownany.
    std::vector<uint8_t> bufor(sizeof dobry + 1);
    std::memcpy(bufor.data() + 1, &dobry, sizeof dobry);
    SPRAWDZ(KP::waliduj(bufor.data() + 1, sizeof dobry, &ocz, &u) == KP::Blad::OK);
    // Obciecie.
    for (size_t n : {size_t(0), size_t(1), size_t(11), size_t(12), size_t(100), sizeof(dobry) - 1}) {
        const KP::Blad b = KP::waliduj(&dobry, n, &ocz, &u);
        SPRAWDZ(b == KP::Blad::OBCIETY);
    }
    SPRAWDZ(KP::waliduj(nullptr, sizeof dobry, &ocz, &u) == KP::Blad::OBCIETY);
    struct Mutacja { const char* nazwa; void (*f)(KP::KontraktV1&); KP::Blad oczekiwany; };
    const Mutacja m[] = {
        {"sygnatura", [](KP::KontraktV1& k) { k.sygnatura ^= 1; }, KP::Blad::ZLA_SYGNATURA},
        {"wersja 2", [](KP::KontraktV1& k) { k.wersja = 2; }, KP::Blad::WERSJA_NIEOBSLUGIWANA},
        {"wersja 0", [](KP::KontraktV1& k) { k.wersja = 0; }, KP::Blad::WERSJA_NIEOBSLUGIWANA},
        {"rozmiar+8", [](KP::KontraktV1& k) { k.rozmiar += 8; }, KP::Blad::ROZMIAR_NIEZGODNY},
        {"rozmiar-8", [](KP::KontraktV1& k) { k.rozmiar -= 8; }, KP::Blad::ROZMIAR_NIEZGODNY},
        {"zarezerwowane0", [](KP::KontraktV1& k) { k.zarezerwowane0 = 1; }, KP::Blad::POLA_ZAREZERWOWANE},
        {"zrodlo 0", [](KP::KontraktV1& k) { k.zrodlo = 0; }, KP::Blad::ZRODLO_NIEZNANE},
        {"zrodlo 3", [](KP::KontraktV1& k) { k.zrodlo = 3; }, KP::Blad::ZRODLO_NIEZNANE},
        {"flaga nieznana", [](KP::KontraktV1& k) { k.flagi |= 1U << 7; }, KP::Blad::FLAGI_NIEZNANE},
        {"model 2", [](KP::KontraktV1& k) { k.model_podkladu = 2; }, KP::Blad::MODEL_PODKLADU},
        {"PA niewyrownany", [](KP::KontraktV1& k) { k.jadro_baza_fizyczna += 0x10; }, KP::Blad::UKLAD_OBRAZU},
        {"PA > 4 GiB", [](KP::KontraktV1& k) { k.jadro_baza_fizyczna = 0x100000000ULL; k.zakresy[0].poczatek = 0x100000000ULL; }, KP::Blad::UKLAD_OBRAZU},
        {"VA inny niz linker", [](KP::KontraktV1& k) { k.jadro_baza_wirtualna = 0x200000; k.segmenty[0].va = 0x200000; k.segmenty[0].rozmiar = 0; }, KP::Blad::UKLAD_OBRAZU},
        {"okno przesuniete wzgledem linkera", [](KP::KontraktV1& k) { k.jadro_baza_wirtualna += 0x100000; for (auto& g : k.segmenty) if (g.rozmiar) g.va += 0x100000; }, KP::Blad::NIEZGODNY_Z_LINKEREM},
        {"zajete inne niz linker", [](KP::KontraktV1& k) { k.segmenty[3].zajete -= 0x10; }, KP::Blad::NIEZGODNY_Z_LINKEREM},
        {"BRB: zasob w luzie strony obrazu", [](KP::KontraktV1& k) { k.zakresy[k.liczba_zakresow++] = {0x3000080, 0x100, KP::RODZAJ_MODUL, KP::ZYCIE_DO_ZWOLNIENIA_PRZEZ_KONSUMENTA, 0, 0}; }, KP::Blad::DUPLIKAT_WLASNOSCI},
        {"rozmiar inny niz linker", [](KP::KontraktV1& k) { k.jadro_rozmiar += 0x1000; k.segmenty[3].rozmiar += 0x1000; k.zakresy[0].dlugosc += 0x1000; }, KP::Blad::NIEZGODNY_Z_LINKEREM},
        {"uprawnienia inne niz linker", [](KP::KontraktV1& k) { k.segmenty[2].uprawnienia = OJ::ODCZYT | OJ::ZAPIS; }, KP::Blad::NIEZGODNY_Z_LINKEREM},
        {"segment W+X", [](KP::KontraktV1& k) { k.segmenty[1].uprawnienia = 7; }, KP::Blad::UKLAD_OBRAZU},
        {"MB2 nie-tozsamosciowy", [](KP::KontraktV1& k) { k.zrodlo = KP::ZRODLO_MB2_ZGODNOSC; }, KP::Blad::MB2_NIETOZSAMOSCIOWY},
        {"mapa EFI", [](KP::KontraktV1& k) { k.mapa_format = KP::MAPA_EFI_PRZYSZLA; }, KP::Blad::MAPA_NIEOBSLUGIWANA},
        {"mapa format 9", [](KP::KontraktV1& k) { k.mapa_format = 9; }, KP::Blad::MAPA_NIEPOPRAWNA},
        {"mapa adres 0", [](KP::KontraktV1& k) { k.mapa_adres = 0; }, KP::Blad::MAPA_NIEPOPRAWNA},
        {"mapa niewyrownana", [](KP::KontraktV1& k) { k.mapa_adres += 4; }, KP::Blad::MAPA_NIEPOPRAWNA},
        {"mapa wpis 20", [](KP::KontraktV1& k) { k.mapa_rozmiar_wpisu = 20; }, KP::Blad::MAPA_NIEPOPRAWNA},
        {"mapa niepodzielna", [](KP::KontraktV1& k) { k.mapa_rozmiar += 8; }, KP::Blad::MAPA_NIEPOPRAWNA},
        {"informacje 0", [](KP::KontraktV1& k) { k.informacje_adres = 0; }, KP::Blad::INFORMACJE_NIEPOPRAWNE},
        {"informacje przepelnienie", [](KP::KontraktV1& k) { k.informacje_adres = 0xFFFFFFFFFFFFF000ULL; }, KP::Blad::INFORMACJE_NIEPOPRAWNE},
        {"brak zakresu informacji", [](KP::KontraktV1& k) { k.zakresy[1].rodzaj = KP::RODZAJ_TABLICE_STRON_ROZRUCHU; k.zakresy[1].flagi = 0; }, KP::Blad::INFORMACJE_NIEPOPRAWNE},
        {"za duzo zakresow", [](KP::KontraktV1& k) { k.liczba_zakresow = 65; }, KP::Blad::ZA_DUZO_ZAKRESOW},
        {"zakres dlugosc 0", [](KP::KontraktV1& k) { k.zakresy[2].dlugosc = 0; }, KP::Blad::ZAKRES_NIEPOPRAWNY},
        {"zakres przepelnienie", [](KP::KontraktV1& k) { k.zakresy[2].poczatek = UINT64_MAX - 0xFFF; }, KP::Blad::ZAKRES_NIEPOPRAWNY},
        {"rodzaj nieznany", [](KP::KontraktV1& k) { k.zakresy[2].rodzaj = 99; }, KP::Blad::ZAKRES_NIEPOPRAWNY},
        {"modul trwaly", [](KP::KontraktV1& k) { k.zakresy[2].rodzaj = KP::RODZAJ_MODUL; k.zakresy[2].czas_zycia = KP::ZYCIE_TRWALE; }, KP::Blad::ZAKRES_NIEPOPRAWNY},
        {"framebuffer z HHDM", [](KP::KontraktV1& k) { k.zakresy[k.liczba_zakresow++] = {0xFD000000ULL, 0x1000, KP::RODZAJ_FRAMEBUFFER, KP::ZYCIE_TRWALE, 0, 0}; }, KP::Blad::ZAKRES_NIEPOPRAWNY},
        {"flaga zakresu nieznana", [](KP::KontraktV1& k) { k.zakresy[2].flagi = 1U << 9; }, KP::Blad::ZAKRES_NIEPOPRAWNY},
        {"zarezerwowane zakresu", [](KP::KontraktV1& k) { k.zakresy[2].zarezerwowane = 1; }, KP::Blad::POLA_ZAREZERWOWANE},
        {"smieci w nieuzytym zakresie", [](KP::KontraktV1& k) { k.zakresy[40].dlugosc = 1; }, KP::Blad::POLA_ZAREZERWOWANE},
        {"smieci w nieuzytym segmencie", [](KP::KontraktV1& k) { k.segmenty[6].va = 1; }, KP::Blad::POLA_ZAREZERWOWANE},
        {"brak zakresu obrazu", [](KP::KontraktV1& k) { k.zakresy[0].rodzaj = KP::RODZAJ_TABLICE_STRON_ROZRUCHU; }, KP::Blad::OBRAZ_NIEOPISANY},
        {"zakres obrazu inny niz podklad", [](KP::KontraktV1& k) { k.zakresy[0].poczatek += 0x1000; }, KP::Blad::OBRAZ_NIEOPISANY},
        {"drugi zakres obrazu", [](KP::KontraktV1& k) { k.zakresy[k.liczba_zakresow++] = k.zakresy[0]; }, KP::Blad::DUPLIKAT_WLASNOSCI},
        {"nakladanie: tablice na informacjach", [](KP::KontraktV1& k) { k.zakresy[2].poczatek = 0x20000800; }, KP::Blad::DUPLIKAT_WLASNOSCI},
        {"nakladanie: modul na obrazie", [](KP::KontraktV1& k) { k.zakresy[k.liczba_zakresow++] = {0x3800000, 0x1000, KP::RODZAJ_MODUL, KP::ZYCIE_DO_ZWOLNIENIA_PRZEZ_KONSUMENTA, 0, 0}; }, KP::Blad::DUPLIKAT_WLASNOSCI},
        {"modul tozsamosciowy w cieniu okna", [](KP::KontraktV1& k) { k.zakresy[k.liczba_zakresow++] = {0x800000, 0x1000, KP::RODZAJ_MODUL, KP::ZYCIE_DO_ZWOLNIENIA_PRZEZ_KONSUMENTA, KP::ZAKRES_DOSTEP_TOZSAMOSCIOWY, 0}; }, KP::Blad::ZASOB_W_CIENIU_OKNA},
        {"MBI w cieniu okna", [](KP::KontraktV1& k) { k.informacje_adres = 0x200000; k.mapa_adres = 0x200008; k.zakresy[1].poczatek = 0x200000; }, KP::Blad::ZASOB_W_CIENIU_OKNA},
        {"CR3 poza opisem", [](KP::KontraktV1& k) { k.cr3_rozruchu = 0x30000000; }, KP::Blad::CR3_NIEOPISANY},
        {"CR3 niewyrownany", [](KP::KontraktV1& k) { k.cr3_rozruchu += 8; }, KP::Blad::CR3_NIEOPISANY},
        {"flaga CR3 w obrazie falszywa", [](KP::KontraktV1& k) { k.flagi |= KP::FLAGA_CR3_W_OBRAZIE; }, KP::Blad::CR3_NIEOPISANY},
        {"flaga CR3 bez CR3", [](KP::KontraktV1& k) { k.cr3_rozruchu = 0; k.flagi |= KP::FLAGA_CR3_W_OBRAZIE; }, KP::Blad::CR3_NIEOPISANY},
    };
    for (const Mutacja& x : m) {
        KP::KontraktV1 k = dobry;
        x.f(k);
        const KP::Blad b = KP::waliduj(&k, sizeof k, &ocz, &u);
        if (b != x.oczekiwany) std::printf("  mutacja '%s': %s (oczekiwano %s)\n", x.nazwa, KP::opis_bledu(b), KP::opis_bledu(x.oczekiwany));
        SPRAWDZ(b == x.oczekiwany);
    }
    std::printf("  mutacje deterministyczne: %zu\n", sizeof m / sizeof m[0]);
    // Mutacje losowe: kazdy wynik OK musi spelniac niezmienniki; zaden przypadek nie moze sie wywrocic (ASan/UBSan).
    uint64_t x = 0x52314131ULL;
    auto los = [&]() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };
    unsigned zaakceptowane = 0, niezmienniki = 0;
    const unsigned N = 200000;
    for (unsigned i = 0; i < N; ++i) {
        KP::KontraktV1 k = dobry;
        uint8_t* b = reinterpret_cast<uint8_t*>(&k);
        const unsigned ile = 1 + static_cast<unsigned>(los() % 4);
        for (unsigned j = 0; j < ile; ++j) b[los() % sizeof k] ^= static_cast<uint8_t>(1U << (los() % 8));
        OJ::Uklad w{};
        if (KP::waliduj(&k, sizeof k, &ocz, &w) == KP::Blad::OK) {
            ++zaakceptowane;
            const bool ok = w.poprawny && (w.pa_poczatek & 0xFFF) == 0 && w.pa_poczatek >= 0x100000 &&
                            w.pa_koniec <= 0x100000000ULL && w.va_poczatek == K_START && w.va_koniec == K_END &&
                            (k.zrodlo != KP::ZRODLO_MB2_ZGODNOSC || OJ::tozsamosciowy(w));
            if (!ok) ++niezmienniki;
        }
    }
    std::printf("  mutacje losowe: %u, zaakceptowane %u, naruszenia niezmiennikow %u\n", N, zaakceptowane, niezmienniki);
    SPRAWDZ(niezmienniki == 0);
}

/* ---------------------------------------------------------------------------------------------------------------
 * F. Klasyfikacja wobec mapy pamieci MB2.
 * ------------------------------------------------------------------------------------------------------------- */
static std::vector<uint8_t> tag_mapy(const std::vector<WpisMapyPamieciMB2>& w) {
    Mbi m;
    m.mapa(w);
    std::vector<uint8_t> b = m.b;
    return std::vector<uint8_t>(b.begin() + 8, b.begin() + 8 + 16 + 24 * w.size());
}

static void test_klasyfikacja() {
    auto klas = [](const std::vector<WpisMapyPamieciMB2>& w, uint64_t p, uint64_t n) {
        const auto t = tag_mapy(w);
        return KP::klasyfikuj_wobec_mapy_mb2(t.data(), t.size(), p, n);
    };
    SPRAWDZ(klas(mapa_ram_64mib(), K_START, K_ROZMIAR).blad == KP::Blad::OK);
    // Uklad OVMF z S3 (zmierzony w 4A-R): NVS 0x800000-0x808000, 0x80B000-0x80C000, 0x810000-0x900000.
    const std::vector<WpisMapyPamieciMB2> ovmf = {
        {0x100000, 0x700000, 1, 0}, {0x800000, 0x8000, 4, 0}, {0x808000, 0x3000, 1, 0}, {0x80B000, 0x1000, 4, 0},
        {0x80C000, 0x4000, 1, 0}, {0x810000, 0xF0000, 4, 0}, {0x900000, 0x7E70F000, 1, 0}};
    const KP::WynikKlasyfikacji w = klas(ovmf, K_START, K_ROZMIAR);
    SPRAWDZ(w.blad == KP::Blad::OBRAZ_NA_ACPI_NVS && w.typ == 4 && w.adres == 0x800000);
    SPRAWDZ(klas(ovmf, 0x900000, 0x100000).blad == KP::Blad::OK);
    SPRAWDZ(klas({{0x100000, 0x7F00000, 1, 0}, {0x400000, 0x1000, 2, 0}}, K_START, K_ROZMIAR).blad == KP::Blad::OBRAZ_NA_ZAREZERWOWANEJ);
    SPRAWDZ(klas({{0x100000, 0x7F00000, 1, 0}, {0x400000, 0x1000, 3, 0}}, K_START, K_ROZMIAR).blad == KP::Blad::OBRAZ_NA_ACPI_ODZYSKIWALNEJ);
    SPRAWDZ(klas({{0x100000, 0x7F00000, 1, 0}, {0x400000, 0x1000, 5, 0}}, K_START, K_ROZMIAR).blad == KP::Blad::OBRAZ_NA_USZKODZONEJ);
    SPRAWDZ(klas({{0x100000, 0x7F00000, 1, 0}, {0x400000, 0x1000, 7, 0}}, K_START, K_ROZMIAR).blad == KP::Blad::OBRAZ_NA_NIEZNANYM_TYPIE);
    const KP::WynikKlasyfikacji d = klas({{0x100000, 0x300000, 1, 0}, {0x500000, 0x7000000, 1, 0}}, K_START, K_ROZMIAR);
    SPRAWDZ(d.blad == KP::Blad::OBRAZ_POZA_RAM && d.adres == 0x400000);
    // Nieposortowane, nachodzace wpisy RAM pokrywajace obraz.
    SPRAWDZ(klas({{0x600000, 0x7000000, 1, 0}, {0x100000, 0x600000, 1, 0}, {0x300000, 0x100000, 1, 0}}, K_START, K_ROZMIAR).blad == KP::Blad::OK);
    // Powyzej 4 GiB: pelne 64 bity.
    SPRAWDZ(klas({{0x100000000ULL, 0x100000000ULL, 1, 0}}, 0x180000000ULL, K_ROZMIAR).blad == KP::Blad::OK);
    SPRAWDZ(klas({{0x100000000ULL, 0x100000000ULL, 1, 0}, {0x180001000ULL, 0x1000, 4, 0}}, 0x180000000ULL, K_ROZMIAR).adres == 0x180001000ULL);
    // Mapa uszkodzona.
    SPRAWDZ(klas({{0xFFFFFFFFFFFFF000ULL, 0x2000, 2, 0}}, K_START, K_ROZMIAR).blad == KP::Blad::MAPA_NIEPOPRAWNA);
    SPRAWDZ(KP::klasyfikuj_wobec_mapy_mb2(nullptr, 40, K_START, K_ROZMIAR).blad == KP::Blad::MAPA_NIEPOPRAWNA);
}

/* ---------------------------------------------------------------------------------------------------------------
 * G. PMM produkcyjny z kontraktu (symulowana pamiec, rozne bazy fizyczne).
 * ------------------------------------------------------------------------------------------------------------- */
static void przygotuj_srodowisko_pmm(const KP::SymboleObrazu& sym) {
    PmmHost::symbole = sym;
    PmmHost::dostep = dostep_sym;
    PmmHost::tlumacz = nullptr;
    PmmHost::cr3 = 0;
}

static void przywroc_srodowisko_pmm() {
    PmmHost::symbole = {0x100000ULL, 0x100050ULL, 0x101000ULL, 0x17F000ULL, 0x180000ULL,
                        0x1BF000ULL, 0x1C0000ULL, 0x1FF800ULL, 0x200000ULL};
    PmmHost::dostep = nullptr;
    PmmHost::tlumacz = nullptr;
    PmmHost::cr3 = 0;
}

/* Kontrakt z mapa w symulowanej pamieci pod 0x20000000 (MBI-podobny obszar informacji). */
static KP::KontraktV1 kontrakt_z_mapa(uint64_t pa0, const std::vector<WpisMapyPamieciMB2>& mapa, uint32_t zrodlo) {
    const std::vector<uint8_t> t = tag_mapy(mapa);
    std::vector<uint8_t> info(0x1000, 0);
    std::memcpy(&info[8], t.data(), t.size());
    g_pamiec = {{0x20000000, info}};
    KP::KontraktV1 k = kontrakt_nietozsamosciowy(pa0);
    k.zrodlo = zrodlo;
    k.mapa_rozmiar = t.size();
    if (zrodlo == KP::ZRODLO_MB2_ZGODNOSC) k.cr3_rozruchu = 0, k.liczba_zakresow = 2, k.zakresy[2] = {};
    return k;
}

static void test_pmm(uint64_t pa0, uint32_t zrodlo) {
    przygotuj_srodowisko_pmm(SYMBOLE_PRODUKCYJNE);
    std::vector<WpisMapyPamieciMB2> mapa = {{0x0, 0x9F000, 1, 0}, {0x100000, 0x3FF00000, 1, 0},
                                            {0x40000000, 0x10000, 3, 0}, {0x40010000, 0x3FFF0000, 1, 0},
                                            {0x100000000ULL, 0x40000000, 1, 0}};
    KP::KontraktV1 k = kontrakt_z_mapa(pa0, mapa, zrodlo);
    k.zakresy[k.liczba_zakresow++] = {0x21000000, 0x5000, KP::RODZAJ_MODUL, KP::ZYCIE_DO_ZWOLNIENIA_PRZEZ_KONSUMENTA, KP::ZAKRES_DOSTEP_TOZSAMOSCIOWY, 0};
    k.zakresy[k.liczba_zakresow++] = {0x900000000ULL, 0x800000, KP::RODZAJ_FRAMEBUFFER, KP::ZYCIE_TRWALE, KP::ZAKRES_BEZ_DIRECT_MAP, 0};
    SPRAWDZ(InicjalizujPMMZKontraktem(&k, sizeof k));
    const StatusPamieciRozruchu st = PmmStatusRozruchu();
    if (st.kod) std::printf("  PMM 0x%llx: %s\n", (unsigned long long)pa0, st.opis);
    SPRAWDZ(st.kod == 0);
    const OJ::Uklad* u = ObrazJadraAktywny();
    SPRAWDZ(u && u->pa_poczatek == pa0 && u->va_poczatek == K_START);
    // Direct-Map obejmuje podklad obrazu (statyczne tablice stron), ale ramki obrazu nie sa dostepne.
    SPRAWDZ(FizycznyZakresDoWirtualnego(pa0, 0x1000) == reinterpret_cast<void*>(BAZA_MAPOWANIA_FIZYCZNEGO + pa0));
    uint64_t fiz = 0;
    SPRAWDZ(WirtualnyDoFizycznego(reinterpret_cast<void*>(BAZA_MAPOWANIA_FIZYCZNEGO + pa0 + 0x925000), &fiz) && fiz == pa0 + 0x925000);
    SPRAWDZ(!WirtualnyDoFizycznego(reinterpret_cast<void*>(K_START + 0x925000), &fiz));   // VA obrazu nie jest adresem DMA
    SPRAWDZ(!CzyRamkaWolna(pa0) && !CzyRamkaWolna(pa0 + K_ROZMIAR - 0x1000));
    SPRAWDZ(!CzyZakresWMapowaniuFizycznym(0x40000000, 0x1000));   // ACPI reclaimable bez aliasu i bez alokacji
    SPRAWDZ(!CzyRamkaWolna(0x40000000));
    SPRAWDZ(!CzyRamkaWolna(0x20000000));                          // informacje rozruchu
    SPRAWDZ(!CzyRamkaWolna(0x21000000));                          // modul (zajety do OdblokujZakres)
    // Proba zwolnienia ramek obrazu i informacji: bez efektu.
    const uint64_t wolne_przed = pmm_pobierz_wolne_ramki();
    ZwolnijRamke(reinterpret_cast<void*>(pa0));
    ZwolnijRamke(reinterpret_cast<void*>(pa0 + 0x1000));
    ZwolnijCiagleRamki(reinterpret_cast<void*>(pa0), 4);
    ZwolnijRamke(reinterpret_cast<void*>(0x20000000));
    OdblokujZakres(pa0, K_ROZMIAR);
    OdblokujRamke(pa0);
    SPRAWDZ(pmm_pobierz_wolne_ramki() == wolne_przed && !CzyRamkaWolna(pa0));
    // Modul wraca do puli dopiero przez OdblokujZakres.
    OdblokujZakres(0x21000000, 0x5000);
    SPRAWDZ(CzyRamkaWolna(0x21000000) && pmm_pobierz_wolne_ramki() == wolne_przed + 5);
    // Double-free.
    void* r = ZaalokujRamke();
    SPRAWDZ(r != nullptr);
    const uint64_t po_alokacji = pmm_pobierz_wolne_ramki();
    ZwolnijRamke(r);
    ZwolnijRamke(r);
    SPRAWDZ(pmm_pobierz_wolne_ramki() == po_alokacji + 1);
    // DMA (logika dma_allocate: ZaalokujCiagleRamki + Direct-Map): PA poza obrazem, VA = HHDM(PA).
    void* dma = ZaalokujCiagleRamki(16);
    SPRAWDZ(dma && !OJ::podklad_przecina(*u, reinterpret_cast<uint64_t>(dma), 16 * 0x1000));
    SPRAWDZ(FizycznyZakresDoWirtualnego(reinterpret_cast<uint64_t>(dma), 16 * 0x1000) ==
            reinterpret_cast<void*>(BAZA_MAPOWANIA_FIZYCZNEGO + reinterpret_cast<uint64_t>(dma)));
    ZwolnijCiagleRamki(dma, 16);
    // Pelne wyczerpanie alokatora: zadna ramka obrazu, cienia okna, informacji, ACPI ani niskiego 1 MiB.
    std::vector<uint64_t> ramki;
    for (;;) { void* f = ZaalokujRamke(); if (!f) break; ramki.push_back(reinterpret_cast<uint64_t>(f)); }
    bool czyste = !ramki.empty();
    for (uint64_t f : ramki) {
        if (f < 0x100000 || (f >= pa0 && f < pa0 + K_ROZMIAR) || (f >= 0x20000000 && f < 0x20001000) ||
            (f >= 0x40000000 && f < 0x40010000) || f >= 0x40000000ULL /* limit DMA 1 GiB */) czyste = false;
        if (pa0 != K_START && f >= K_START && f < K_END) czyste = false;   // cien okna (obraz nie-tozsamosciowy)
    }
    std::printf("  PMM baza 0x%llx (%s): przydzielono %zu ramek, kolizje: %s\n", (unsigned long long)pa0,
                zrodlo == KP::ZRODLO_MB2_ZGODNOSC ? "MB2" : "przyszle BRB", ramki.size(), czyste ? "brak" : "WYKRYTE");
    SPRAWDZ(czyste);
    for (uint64_t f : ramki) ZwolnijRamke(reinterpret_cast<void*>(f));
    przywroc_srodowisko_pmm();
}

static void test_pmm_odmowy() {
    przygotuj_srodowisko_pmm(SYMBOLE_PRODUKCYJNE);
    struct Przypadek { const char* nazwa; std::vector<WpisMapyPamieciMB2> mapa; KP::Blad oczekiwany; uint32_t typ; uint64_t adres; };
    const Przypadek p[] = {
        {"ACPI NVS pod obrazem (OVMF S3)", {{0x100000, 0x700000, 1, 0}, {0x800000, 0x8000, 4, 0}, {0x808000, 0x3F7F8000, 1, 0}}, KP::Blad::OBRAZ_NA_ACPI_NVS, 4, 0x800000},
        {"runtime/zarezerwowana pod obrazem", {{0x100000, 0x3FF00000, 1, 0}, {0x3000000 + 0x5000, 0x2000, 2, 0}}, KP::Blad::OBRAZ_NA_ZAREZERWOWANEJ, 2, 0x3005000},
        {"ACPI reclaimable pod obrazem", {{0x100000, 0x3FF00000, 1, 0}, {0x3C00000, 0x1000, 3, 0}}, KP::Blad::OBRAZ_NA_ACPI_ODZYSKIWALNEJ, 3, 0x3C00000},
        {"obraz poza RAM", {{0x100000, 0x3000000, 1, 0}, {0x4000000, 0x3C000000, 1, 0}}, KP::Blad::OBRAZ_POZA_RAM, 0, 0x3100000},
    };
    for (const Przypadek& c : p) {
        const uint64_t pa0 = (c.oczekiwany == KP::Blad::OBRAZ_NA_ACPI_NVS) ? 0x100000 : 0x3000000;
        KP::KontraktV1 k = kontrakt_z_mapa(pa0, c.mapa, pa0 == 0x100000 ? KP::ZRODLO_MB2_ZGODNOSC : KP::ZRODLO_BRB_NATYWNE_PRZYSZLE);
        if (pa0 != 0x100000) k.zakresy[2] = {0x20100000, 0x6000, KP::RODZAJ_TABLICE_STRON_ROZRUCHU, KP::ZYCIE_DO_PRZELACZENIA_CR3, 0, 0};
        const bool ok = InicjalizujPMMZKontraktem(&k, sizeof k);
        const StatusPamieciRozruchu st = PmmStatusRozruchu();
        if (st.kod != uint32_t(c.oczekiwany)) std::printf("  '%s': %s\n", c.nazwa, st.opis);
        SPRAWDZ(!ok && st.kod == uint32_t(c.oczekiwany) && st.typ_mapy == c.typ && st.adres == c.adres);
        SPRAWDZ(ObrazJadraAktywny() == nullptr && ZaalokujRamke() == nullptr && !CzyZakresWMapowaniuFizycznym(0x100000, 0x1000));
    }
    // Kontrakt niepoprawny strukturalnie: fail-closed.
    KP::KontraktV1 k = kontrakt_z_mapa(0x3000000, mapa_ram_64mib(), KP::ZRODLO_BRB_NATYWNE_PRZYSZLE);
    k.wersja = 2;
    SPRAWDZ(!InicjalizujPMMZKontraktem(&k, sizeof k) && PmmStatusRozruchu().kod == uint32_t(KP::Blad::WERSJA_NIEOBSLUGIWANA));
    k.wersja = 1;
    SPRAWDZ(!InicjalizujPMMZKontraktem(&k, 64) && PmmStatusRozruchu().kod == uint32_t(KP::Blad::OBCIETY));
    // Mapa w kontrakcie wskazuje pamiec niedostepna (symulacja): fail-closed.
    k = kontrakt_z_mapa(0x3000000, mapa_ram_64mib(), KP::ZRODLO_BRB_NATYWNE_PRZYSZLE);
    g_pamiec.clear();
    SPRAWDZ(!InicjalizujPMMZKontraktem(&k, sizeof k) && PmmStatusRozruchu().kod == uint32_t(KP::Blad::MAPA_NIEPOPRAWNA));
    przywroc_srodowisko_pmm();
}

/* Sciezka zgodnosci: InicjalizujPMM(mbi) z syntetycznym MBI w symulowanej pamieci. */
static void test_pmm_mb2() {
    przygotuj_srodowisko_pmm(SYMBOLE_PRODUKCYJNE);
    g_pamiec = {{0x2000, mbi_typowe()}};
    PmmHost::cr3 = 0xA25000;
    InicjalizujPMM(0x2000);
    SPRAWDZ(PmmStatusRozruchu().kod == 0 && ObrazJadraAktywny() && OJ::tozsamosciowy(*ObrazJadraAktywny()));
    SPRAWDZ(!CzyRamkaWolna(0x100000) && !CzyRamkaWolna(0xD6E000) && CzyRamkaWolna(0xD6F000));
    SPRAWDZ(!CzyRamkaWolna(0x1000000) && !CzyRamkaWolna(0x1234000));   // modul
    SPRAWDZ(CzyRamkaWolna(0x1235000));
    SPRAWDZ(!CzyRamkaWolna(0x2000));                                    // MBI (i niski 1 MiB)
    // MBI w luzie strony .boot (jak GRUB BIOS) + modul czesciowo w luzie za .bss: start dozwolony, strony obrazu
    // pozostaja trwale zarezerwowane rowniez po zwolnieniu modulu.
    { Mbi m; m.mapa(mapa_ram_64mib()); m.modul(0xD6F000 - 0x200, 0xD6F000 + 0x3000); m.baza(0x100000); g_pamiec = {{0x100080, m.zakoncz()}}; }
    InicjalizujPMM(0x100080);
    SPRAWDZ(PmmStatusRozruchu().kod == 0 && !CzyRamkaWolna(0x100000) && !CzyRamkaWolna(0xD6E000) && !CzyRamkaWolna(0xD70000));
    OdblokujZakres(0xD6F000 - 0x200, 0x3200);
    SPRAWDZ(!CzyRamkaWolna(0xD6E000) && CzyRamkaWolna(0xD6F000) && CzyRamkaWolna(0xD71000));
    // Ten sam MBI, ale tag 21 niezgodny: fail-closed przed jakakolwiek rezerwacja.
    { Mbi m; m.mapa(mapa_ram_64mib()); m.baza(0x200000); g_pamiec = {{0x2000, m.zakoncz()}}; }
    InicjalizujPMM(0x2000);
    SPRAWDZ(PmmStatusRozruchu().kod == uint32_t(KP::Blad::MB2_TAG21_NIEZGODNY) && !ObrazJadraAktywny() && !ZaalokujRamke());
    przywroc_srodowisko_pmm();
}

/* H. Rzeczywiste MBI z GRUB 2.12 / OVMF (zrzuty Fazy 4A-R, tylko odczyt). */
static void test_mbi_grub(const char* sciezka, uint64_t adres, const std::string& oczekiwanie) {
    std::ifstream f(sciezka, std::ios::binary);
    std::vector<uint8_t> mbi((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (mbi.size() < 16) { std::printf("  NIE URUCHOMIONO: brak %s\n", sciezka); return; }
    przygotuj_srodowisko_pmm(SYMBOLE_PRODUKCYJNE);
    g_pamiec = {{adres, mbi}};
    KP::KontraktV1 k{};
    const KP::Blad b = KP::zbuduj_z_multiboot2(adres, SYMBOLE_PRODUKCYJNE, 0, dostep_sym, tlumacz_tozsamosc, &k);
    InicjalizujPMM(adres);
    const StatusPamieciRozruchu st = PmmStatusRozruchu();
    std::printf("  GRUB MBI %s: adapter=%s, PMM=%s (typ=%u adres=0x%llx), zakresow=%u\n", sciezka, KP::opis_bledu(b),
                st.opis, st.typ_mapy, (unsigned long long)st.adres, k.liczba_zakresow);
    SPRAWDZ(b == KP::Blad::OK);
    if (oczekiwanie == "NVS") {
        SPRAWDZ(st.kod == uint32_t(KP::Blad::OBRAZ_NA_ACPI_NVS) && st.typ_mapy == 4 && st.adres == 0x800000);
        SPRAWDZ(!ObrazJadraAktywny() && !ZaalokujRamke());
    } else {
        SPRAWDZ(st.kod == 0 && ObrazJadraAktywny() && OJ::tozsamosciowy(*ObrazJadraAktywny()));
        SPRAWDZ(!CzyRamkaWolna(0x800000) && !CzyRamkaWolna(0x100000));
    }
    przywroc_srodowisko_pmm();
}

/* I. Uklad Ring 3 i granice obrazu niezmienione. */
static void test_ring3() {
    static_assert(RING3_BASE == 0x1000000ULL && RING3_DEFAULT_TEXT_VA == 0x1001000ULL && RING3_DEFAULT_DATA_VA == 0x1009000ULL,
                  "uklad Ring 3 v1 niezmieniony");
    static_assert(RING3_HEAP_BASE == 0x800000000ULL && RING3_SHM_BASE == 0x900000000ULL && RING3_STACK_TOP == 0x00007FFFF0000000ULL,
                  "uklad Ring 3 niezmieniony");
    static_assert(BAZA_MAPOWANIA_FIZYCZNEGO == 0xFFFF800000000000ULL, "Direct-Map niezmieniony");
    SPRAWDZ(K_END <= KERNEL_MAX_END_ALLOWED);
    const OJ::Uklad u = uklad_na(0x5000000);
    SPRAWDZ(!OJ::okno_przecina(u, RING3_BASE, 0x1000) && !OJ::okno_przecina(u, RING3_HEAP_BASE, 0x1000));
    SPRAWDZ(OJ::zaplanuj_strone_bazowa(u, RING3_DEFAULT_TEXT_VA).rodzaj == OJ::RodzajStrony::TOZSAMOSC);
}

int main(int argc, char** argv) {
    std::printf("[R1A] A: typy adresow\n"); test_adresy();
    std::printf("[R1A] B: uklad obrazu i tlumaczenie\n"); test_uklad();
    std::printf("[R1A] C: plan dolnych 4 GiB (tozsamosc = uklad sprzed R1A)\n"); test_plan_tozsamosciowy(); test_plan_nietozsamosciowy();
    std::printf("[R1A] D: adapter MB2\n"); test_adapter_mb2();
    std::printf("[R1A] E: walidator kontraktu i mutacje\n"); test_walidator();
    std::printf("[R1A] F: klasyfikacja wobec mapy pamieci\n"); test_klasyfikacja();
    std::printf("[R1A] G: PMM z kontraktu\n");
    test_pmm(0x100000, KP::ZRODLO_MB2_ZGODNOSC);
    for (uint64_t b : {0x3000000ULL, 0x5432000ULL, 0x2100000ULL, 0x200000ULL, 0x30000000ULL})
        test_pmm(b, KP::ZRODLO_BRB_NATYWNE_PRZYSZLE);
    test_pmm_odmowy();
    test_pmm_mb2();
    std::printf("[R1A] H: rzeczywiste MBI z GRUB (4A-R)\n");
    unsigned mbi = 0;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--mbi") != 0) continue;
        const std::string a = argv[++i];
        const size_t p1 = a.find(':'), p2 = a.rfind(':');
        if (p1 == std::string::npos || p2 == p1) continue;
        test_mbi_grub(a.substr(0, p1).c_str(), std::strtoull(a.substr(p1 + 1, p2 - p1 - 1).c_str(), nullptr, 16), a.substr(p2 + 1));
        ++mbi;
    }
    if (!mbi) std::printf("  NIE URUCHOMIONO: brak argumentow --mbi\n");
    std::printf("[R1A] I: uklad Ring 3\n"); test_ring3();
    std::printf("[R1A] WYNIK: PASS=%u FAIL=%u\n", g_ok, g_bledy);
    return g_bledy ? 1 : 0;
}
