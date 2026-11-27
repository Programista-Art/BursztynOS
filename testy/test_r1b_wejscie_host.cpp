/*
 * Bursztyn OS - Faza R1B: testy hosta natywnego wejscia 64-bit i komponentow bootstrapu.
 *
 * Testowany kod PRODUKCYJNY: jadro/arch/stan_wejscia64.h (walidacja stanu CPU), jadro/arch/metadane_jadra.h +
 * rozruch/brb/wspolne/metadane_jadra.cpp (wykrywanie wejscia na zwalidowanym planie ELF BRB), rozruch/brb/wspolne/
 * elf64.cpp (zgodnosc wejscia MB2), jadro/mm/plan_przejscia.h (tablice przejscia, analiza W^X i aliasow),
 * jadro/mm/obraz_jadra.h (plan bazowej przestrzeni, tozsamosc_bezpieczna), jadro/mm/kontrakt_pamieci.h
 * (przyjmij_kontrakt_natywny, zasob_tozsamosciowy), jadro/mm/pmm.cpp (PrzyjmijKontraktNatywny,
 * InicjalizujPMMZKontraktem, PamiecFizycznaDoOdczytu, PmmZasobyRozruchuTozsamosciowe; BURSZTYN_PMM_HOST_TEST).
 *
 * SYMULOWANE: stan procesora (struktura Stan - zadna instrukcja uprzywilejowana nie jest wykonywana), pamiec
 * fizyczna (rejestr PA -> bufor), tablice stron (bufory hosta pod symulowanymi PA). Symulacja NIE jest dowodem
 * natywnego wykonania; natywny rozruch: uprzaz R1B w QEMU (raport).
 *
 * Uzycie: test-r1b-wejscie-host [jadro.elf jadro.okrojone wejscie64_hex start_hex]
 */
#include "jadro/arch/metadane_jadra.h"
#include "jadro/arch/stan_wejscia64.h"
#include "jadro/mm/adresy.h"
#include "jadro/mm/kontrakt_pamieci.h"
#include "jadro/mm/obraz_jadra.h"
#include "jadro/mm/pamiec.h"
#include "jadro/mm/plan_przejscia.h"
#include "rozruch/brb/wspolne/elf64.h"
#include "rozruch/brb/wspolne/metadane_jadra.h"
#include "testy/brb_elf_fixture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

bool CzyRamkaWolna(uint64_t adres_fizyczny);

namespace OJ = ObrazJadra;
namespace KP = KontraktPamieci;
namespace PP = PlanPrzejscia;
namespace SW = StanWejscia64;
namespace MJ = MetadaneJadra;

static unsigned g_ok = 0, g_bledy = 0;
#define SPRAWDZ(w) do { if (w) { ++g_ok; } else { ++g_bledy; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #w); } } while (0)

/* Uklad jadra R1B (niezalezne stale z readelf/nm wariantu produkcyjnego R1B nie sa potrzebne: uzywamy ukladu
 * bazowego R1A jako fixture'u, bo testy nie zaleza od konkretnych rozmiarow). */
constexpr uint64_t K_START = 0x100000, K_ROZMIAR = 0xC6F000, K_END = 0xD6F000;
constexpr KP::SymboleObrazu SYMBOLE = {K_START, 0x100050, 0x101000, 0x22AD70, 0x22B000, 0x2A9030, 0x2AA000, 0xD6ED10, K_END};

static OJ::Uklad uklad_na(uint64_t pa0) {
    const KP::OczekiwanyObraz o = KP::oczekiwany_z_symboli(SYMBOLE);
    OJ::Uklad u{};
    if (OJ::zbuduj_uklad(o.va_poczatek, o.rozmiar, pa0, o.segmenty, o.liczba_segmentow, &u) != OJ::BladUkladu::OK) u = {};
    return u;
}

/* ---------------------------------------------------------------------------------------------------------------
 * A. Kontrakt stanu procesora (symulowany stan).
 * ------------------------------------------------------------------------------------------------------------- */
static SW::Stan stan_poprawny() {
    SW::Stan s{};
    s.magic = MJ::MAGIC_WEJSCIA_NATYWNEGO;
    s.kontrakt = 0x20101000;
    s.rsp_wejscia = 0x20100000;
    s.rflags_wejscia = 0x2;
    s.cr0 = SW::CR0_PE | SW::CR0_PG | SW::CR0_WP | 0x10;
    s.cr3 = 0x20000000;
    s.cr4 = SW::CR4_PAE | 0x10;
    s.efer = SW::EFER_LME | SW::EFER_LMA | SW::EFER_NXE;
    s.cs = 0x08;
    s.cpuid_max_rozszerzony = 0x80000008;
    s.cpuid_1_edx = SW::CPUID_PAE;
    s.cpuid_80000001_edx = SW::CPUID_LM | SW::CPUID_NX;
    s.cpuid_80000008_eax = 0x3027;   // 39 bitow fizycznych, 48 liniowych
    return s;
}

static void test_stan_cpu() {
    const uint64_t M = MJ::MAGIC_WEJSCIA_NATYWNEGO;
    SPRAWDZ(SW::waliduj_stan(stan_poprawny(), M) == SW::Blad::OK);
    SPRAWDZ(SW::bity_fizyczne(stan_poprawny()) == 39);
    struct P { const char* n; void (*f)(SW::Stan&); SW::Blad b; };
    const P p[] = {
        {"magic", [](SW::Stan& s) { s.magic ^= 1; }, SW::Blad::ZLY_MAGIC},
        {"CPL 3", [](SW::Stan& s) { s.cs = 0x0B; }, SW::Blad::CPL_NIE_ZERO},
        {"IF", [](SW::Stan& s) { s.rflags_wejscia |= SW::RFLAGS_IF; }, SW::Blad::PRZERWANIA_WLACZONE},
        {"DF", [](SW::Stan& s) { s.rflags_wejscia |= SW::RFLAGS_DF; }, SW::Blad::KIERUNEK_USTAWIONY},
        {"RSP+8", [](SW::Stan& s) { s.rsp_wejscia += 8; }, SW::Blad::STOS_NIEWYROWNANY},
        {"RSP+1", [](SW::Stan& s) { s.rsp_wejscia += 1; }, SW::Blad::STOS_NIEWYROWNANY},
        {"brak LM", [](SW::Stan& s) { s.cpuid_80000001_edx &= ~SW::CPUID_LM; }, SW::Blad::BRAK_LONG_MODE_CPUID},
        {"brak lisci rozszerzonych", [](SW::Stan& s) { s.cpuid_max_rozszerzony = 0x80000000; }, SW::Blad::BRAK_LONG_MODE_CPUID},
        {"brak PAE", [](SW::Stan& s) { s.cpuid_1_edx = 0; }, SW::Blad::BRAK_PAE},
        {"CR0.PG=0", [](SW::Stan& s) { s.cr0 &= ~SW::CR0_PG; }, SW::Blad::TRYB_NIE_IA32E},
        {"CR4.PAE=0", [](SW::Stan& s) { s.cr4 &= ~SW::CR4_PAE; }, SW::Blad::TRYB_NIE_IA32E},
        {"EFER.LMA=0", [](SW::Stan& s) { s.efer &= ~SW::EFER_LMA; }, SW::Blad::TRYB_NIE_IA32E},
        {"LA57", [](SW::Stan& s) { s.cr4 |= SW::CR4_LA57; }, SW::Blad::STRONICOWANIE_5P},
        {"PCIDE", [](SW::Stan& s) { s.cr4 |= SW::CR4_PCIDE; }, SW::Blad::PCID_WLACZONE},
        {"WP=0", [](SW::Stan& s) { s.cr0 &= ~SW::CR0_WP; }, SW::Blad::WP_WYLACZONE},
        {"CR3 niewyrownany", [](SW::Stan& s) { s.cr3 |= 0x18; }, SW::Blad::CR3_NIEPOPRAWNY},
        {"CR3 = 0", [](SW::Stan& s) { s.cr3 = 0; }, SW::Blad::CR3_NIEPOPRAWNY},
        {"CR3 >= 2^39", [](SW::Stan& s) { s.cr3 = 1ULL << 39; }, SW::Blad::CR3_NIEPOPRAWNY},
        {"NX bez NXE", [](SW::Stan& s) { s.efer &= ~SW::EFER_NXE; }, SW::Blad::NXE_NIEZGODNE},
        {"NXE bez NX", [](SW::Stan& s) { s.cpuid_80000001_edx &= ~SW::CPUID_NX; }, SW::Blad::NXE_NIEZGODNE},
        {"MAXPHYADDR 33", [](SW::Stan& s) { s.cpuid_80000008_eax = 0x3021; }, SW::Blad::SZEROKOSC_FIZYCZNA},
        {"MAXPHYADDR 60", [](SW::Stan& s) { s.cpuid_80000008_eax = 0x303C; }, SW::Blad::SZEROKOSC_FIZYCZNA},
        {"kontrakt 0", [](SW::Stan& s) { s.kontrakt = 0; }, SW::Blad::KONTRAKT_NIEPOPRAWNY},
        {"kontrakt niewyrownany", [](SW::Stan& s) { s.kontrakt += 4; }, SW::Blad::KONTRAKT_NIEPOPRAWNY},
        {"kontrakt >= 4 GiB", [](SW::Stan& s) { s.kontrakt = 0x100000000ULL; }, SW::Blad::KONTRAKT_NIEPOPRAWNY},
    };
    for (const P& x : p) {
        SW::Stan s = stan_poprawny();
        x.f(s);
        const SW::Blad b = SW::waliduj_stan(s, M);
        if (b != x.b) std::printf("  stan '%s': %s\n", x.n, SW::opis(b));
        SPRAWDZ(b == x.b);
    }
    // Brak liscia 80000008h: 36 bitow; CR3 ponizej 64 GiB poprawny, powyzej - nie.
    SW::Stan s = stan_poprawny();
    s.cpuid_max_rozszerzony = 0x80000001;
    SPRAWDZ(SW::bity_fizyczne(s) == 36 && SW::waliduj_stan(s, M) == SW::Blad::OK);
    s.cr3 = 0x1000000000ULL;
    SPRAWDZ(SW::waliduj_stan(s, M) == SW::Blad::CR3_NIEPOPRAWNY);
    // Bez NX i bez NXE: poprawne (tablice przejscia bez bitu 63).
    s = stan_poprawny();
    s.cpuid_80000001_edx &= ~SW::CPUID_NX;
    s.efer &= ~SW::EFER_NXE;
    SPRAWDZ(SW::waliduj_stan(s, M) == SW::Blad::OK);
}

/* ---------------------------------------------------------------------------------------------------------------
 * B. Metadane wejscia i zgodnosc wejscia MB2 (plan ELF BRB).
 * ------------------------------------------------------------------------------------------------------------- */
static void zapisz_metadane(fixture::Obraz& o, uint64_t off, uint64_t baza, uint64_t wejscie) {
    o.w64(off, MJ::SYGNATURA); o.w16(off + 8, MJ::WERSJA); o.w16(off + 10, MJ::ROZMIAR);
    o.w32(off + 12, MJ::KONTRAKT_WEJSCIA_R1B); o.w64(off + 16, baza); o.w64(off + 24, wejscie);
    o.w64(off + 32, 0x1000); o.w32(off + 40, MJ::CECHY_R1B); o.w32(off + 44, 0); o.w64(off + 48, 0);
    o.w64(off + 56, MJ::SYGNATURA_KONCOWA);
}

/* Syntetyczne jadro z metadanymi za naglowkiem MB2 (seg0 rozszerzony do 0x30 + 0x40 + wyrownanie). */
static fixture::Obraz jadro_z_metadanymi(uint64_t wejscie64 = 0x101080) {
    fixture::Obraz o = fixture::jadro_minimalne();
    o.segment(0, 1, brb::ELF_PF_R, 0x1000, 0x100000, 0x100000, 0x80, 0x80, 0x1000);
    zapisz_metadane(o, 0x1040, 0x100000, wejscie64);
    return o;
}

static brb::BladMetadanych metadane(const fixture::Obraz& o, brb::MetadaneWejscia* w, brb::BladElf* be = nullptr) {
    brb::PlanLadowania plan{};
    const brb::WynikElf r = brb::zbuduj_plan_ladowania(o.b.data(), o.b.size(), brb::POLITYKA_JADRA_BURSZTYN, &plan);
    if (be) *be = r.blad;
    if (r.blad != brb::BladElf::Brak) return brb::BladMetadanych::BrakDanych;
    return brb::znajdz_metadane_wejscia(o.b.data(), o.b.size(), plan, w);
}

static void test_metadane_syntetyczne() {
    brb::MetadaneWejscia w{};
    brb::BladElf be;
    SPRAWDZ(metadane(jadro_z_metadanymi(), &w, &be) == brb::BladMetadanych::Brak && be == brb::BladElf::Brak);
    SPRAWDZ(w.wejscie64 == 0x101080 && w.wejscie64_offset == 0x1080 && w.przesuniecie_pliku == 0x1040 &&
            w.segment == 0 && w.segment_wejscia == 1 && w.baza_wirtualna == 0x100000);
    struct P { const char* n; void (*f)(fixture::Obraz&); brb::BladMetadanych b; };
    const P p[] = {
        {"wersja 2", [](fixture::Obraz& o) { o.w16(0x1048, 2); }, brb::BladMetadanych::NieobslugiwanaWersja},
        {"rozmiar 56", [](fixture::Obraz& o) { o.w16(0x104A, 56); }, brb::BladMetadanych::ZlyRozmiar},
        {"sygnatura koncowa", [](fixture::Obraz& o) { o.w64(0x1078, 0); }, brb::BladMetadanych::ZlaSygnaturaKoncowa},
        {"kontrakt 2", [](fixture::Obraz& o) { o.w32(0x104C, 2); }, brb::BladMetadanych::NieobslugiwanyKontrakt},
        {"flagi", [](fixture::Obraz& o) { o.w32(0x106C, 1); }, brb::BladMetadanych::PolaZarezerwowane},
        {"zarezerwowane", [](fixture::Obraz& o) { o.w64(0x1070, 1); }, brb::BladMetadanych::PolaZarezerwowane},
        {"cecha nieznana", [](fixture::Obraz& o) { o.w32(0x1068, 0x10); }, brb::BladMetadanych::NieznaneCechy},
        {"wyrownanie 0x800", [](fixture::Obraz& o) { o.w64(0x1060, 0x800); }, brb::BladMetadanych::ZleWyrownanie},
        {"wyrownanie 0x3000", [](fixture::Obraz& o) { o.w64(0x1060, 0x3000); }, brb::BladMetadanych::ZleWyrownanie},
        {"baza niezgodna", [](fixture::Obraz& o) { o.w64(0x1050, 0x200000); }, brb::BladMetadanych::BazaNiezgodna},
        {"wejscie niekanoniczne", [](fixture::Obraz& o) { o.w64(0x1058, 0x0000800000000000ULL); }, brb::BladMetadanych::WejscieNiekanoniczne},
        {"wejscie poza segmentami", [](fixture::Obraz& o) { o.w64(0x1058, 0x500000); }, brb::BladMetadanych::WejsciePozaSegmentem},
        {"wejscie w .data (bez X)", [](fixture::Obraz& o) { o.w64(0x1058, 0x102010); }, brb::BladMetadanych::WejscieNiewykonywalne},
        {"wejscie w .boot (bez X)", [](fixture::Obraz& o) { o.w64(0x1058, 0x100010); }, brb::BladMetadanych::WejscieNiewykonywalne},
        {"wejscie == e_entry", [](fixture::Obraz& o) { o.w64(0x1058, 0x101010); }, brb::BladMetadanych::WejscieRowneElf},
        {"druga sygnatura", [](fixture::Obraz& o) { o.w64(0x1000 - 0x40, MJ::SYGNATURA); }, brb::BladMetadanych::Niejednoznaczne},
        {"brak sygnatury", [](fixture::Obraz& o) { o.w64(0x1040, 0); }, brb::BladMetadanych::NieZnaleziono},
        {"poza segmentem (naglowek pliku)", [](fixture::Obraz& o) { o.w64(0x1040, 0); for (int i = 0; i < 64; ++i) o.b[0x200 + i] = o.b[0x1040 + i]; o.w64(0x200, MJ::SYGNATURA); }, brb::BladMetadanych::PozaSegmentem},
        {"w segmencie wykonywalnym", [](fixture::Obraz& o) { o.w64(0x1040, 0); zapisz_metadane(o, 0x2080, 0x100000, 0x101090); }, brb::BladMetadanych::PozaSegmentem},
    };
    for (const P& x : p) {
        fixture::Obraz o = jadro_z_metadanymi();
        x.f(o);
        const brb::BladMetadanych b = metadane(o, &w);
        if (b != x.b) std::printf("  metadane '%s': %s\n", x.n, brb::opis_bledu_metadanych(b));
        SPRAWDZ(b == x.b);
    }
    // Wejscie w czesci BSS segmentu wykonywalnego (poza p_filesz).
    {
        fixture::Obraz o = jadro_z_metadanymi(0x101180);
        o.segment(1, 1, brb::ELF_PF_R | brb::ELF_PF_X, 0x2000, 0x101000, 0x101000, 0x100, 0x200, 0x1000);
        SPRAWDZ(metadane(o, &w) == brb::BladMetadanych::WejsciePozaPlikiem);
    }
    // Obciecie: struktura na koncu pliku.
    {
        fixture::Obraz o = jadro_z_metadanymi();
        std::vector<uint8_t> krotki(o.b.begin(), o.b.begin() + 0x1060);
        brb::PlanLadowania plan{};
        // Plan z pelnego pliku, dane obciete: czytelnik nie moze wyjsc poza bufor.
        brb::zbuduj_plan_ladowania(o.b.data(), o.b.size(), brb::POLITYKA_JADRA_BURSZTYN, &plan);
        SPRAWDZ(brb::znajdz_metadane_wejscia(krotki.data(), krotki.size(), plan, &w) == brb::BladMetadanych::Obciete);
        SPRAWDZ(brb::znajdz_metadane_wejscia(nullptr, 0, plan, &w) == brb::BladMetadanych::BrakDanych);
    }
    // Mutacje losowe bajtow metadanych: wynik OK tylko przy spelnionych niezmiennikach.
    uint64_t x = 0x52314232ULL;
    auto los = [&]() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };
    unsigned przyjete = 0, naruszenia = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        fixture::Obraz o = jadro_z_metadanymi();
        o.b[0x1040 + los() % 64] ^= uint8_t(1U << (los() % 8));
        if (metadane(o, &w) == brb::BladMetadanych::Brak) {
            ++przyjete;
            if (!(w.wejscie64 >= 0x101000 && w.wejscie64 < 0x101100 && w.wejscie64 != 0x101010 &&
                  w.baza_wirtualna == 0x100000 && (w.wyrownanie_podkladu & (w.wyrownanie_podkladu - 1)) == 0 &&
                  w.wyrownanie_podkladu >= 0x1000)) ++naruszenia;
        }
    }
    std::printf("  metadane: mutacje losowe 100000, przyjete %u, naruszenia %u\n", przyjete, naruszenia);
    SPRAWDZ(naruszenia == 0);
}

static std::vector<uint8_t> wczytaj(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void test_metadane_jadra(const char* elf, const char* okrojony, uint64_t wejscie64, uint64_t start) {
    for (const char* sciezka : {elf, okrojony}) {
        const std::vector<uint8_t> d = wczytaj(sciezka);
        if (d.empty()) { std::printf("  NIE URUCHOMIONO: brak %s\n", sciezka); continue; }
        brb::PlanLadowania plan{};
        const brb::WynikElf r = brb::zbuduj_plan_ladowania(d.data(), d.size(), brb::POLITYKA_JADRA_BURSZTYN, &plan);
        SPRAWDZ(r.blad == brb::BladElf::Brak);
        SPRAWDZ(plan.wejscie_wirtualne == start && plan.mb2.obecny && plan.mb2.dlugosc == 80);   // wejscie MB2 bez zmian
        brb::MetadaneWejscia w{};
        const brb::BladMetadanych b = brb::znajdz_metadane_wejscia(d.data(), d.size(), plan, &w);
        std::printf("  %s: plan=%s metadane=%s wejscie64=0x%llx (oczekiwane 0x%llx), e_entry=0x%llx\n", sciezka,
                    brb::opis_bledu_elf(r.blad), brb::opis_bledu_metadanych(b), (unsigned long long)w.wejscie64,
                    (unsigned long long)wejscie64, (unsigned long long)plan.wejscie_wirtualne);
        SPRAWDZ(b == brb::BladMetadanych::Brak && w.wejscie64 == wejscie64 && w.baza_wirtualna == 0x100000 &&
                w.kontrakt_wejscia == 1 && w.segment == 0);
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * C. Tablice przejscia (PlanPrzejscia) + analiza W^X/aliasow.
 * ------------------------------------------------------------------------------------------------------------- */
struct Pamiec {
    std::map<uint64_t, uint64_t*> tablice;   // PA -> tablica
    const uint64_t* operator()(uint64_t pa) const { auto it = tablice.find(pa); return it == tablice.end() ? nullptr : it->second; }
};

struct Pula {
    uint64_t pa;
    std::vector<uint64_t> bufor;
    Pamiec pamiec;
    explicit Pula(uint64_t p, uint32_t ramki) : pa(p), bufor(size_t(ramki) * 512, 0xCCCCCCCCCCCCCCCCULL) {
        for (uint32_t i = 0; i < ramki; ++i) pamiec.tablice[p + 0x1000ULL * i] = bufor.data() + size_t(i) * 512;
    }
};

static bool przejdz(const Pamiec& m, uint64_t cr3, uint64_t va, uint64_t* pa, uint64_t* lisc) {
    uint64_t t = cr3;
    for (int poziom = 4; poziom >= 1; --poziom) {
        const uint64_t* tab = m(t);
        if (!tab) return false;
        const uint64_t e = tab[(va >> (12 + 9 * (poziom - 1))) & 511];
        if (!(e & 1)) return false;
        if (poziom == 1) { *pa = (e & PP::MASKA_ADRESU) | (va & 0xFFF); *lisc = e; return true; }
        t = e & PP::MASKA_ADRESU;
    }
    return false;
}

static void test_plan_przejscia() {
    const uint64_t bazy[] = {0x3005000, 0x2100000, 0x40000000, 0x7FF00000};
    for (uint64_t baza : bazy) {
        const OJ::Uklad u = uklad_na(baza);
        SPRAWDZ(u.poprawny);
        const uint64_t mbi = 0x9000000, stos = 0x9010000, kontrakt = 0x9014000, modul = 0xA000000, pula_pa = 0x9800000;
        const PP::ZakresTozsamosci z[] = {
            {mbi, 0x1D60, PP::KLASA_TYLKO_ODCZYT, 0}, {stos, 0x4000, PP::KLASA_STOS, 0},
            {kontrakt, sizeof(KP::KontraktV1), PP::KLASA_TYLKO_ODCZYT, 0}, {modul, 0x77FF20, PP::KLASA_TYLKO_ODCZYT, 0},
            {0x9020000, 0x1000, PP::KLASA_KOD_PRZEJSCIA, 0}};
        Pula pula(pula_pa, 64);
        const PP::Parametry par{&u, z, 5, true, 39, pula_pa, pula.bufor.data(), 64};
        PP::Wynik w{};
        const PP::Blad b = PP::zbuduj(par, &w);
        if (b != PP::Blad::OK) std::printf("  plan 0x%llx: %s\n", (unsigned long long)baza, PP::opis(b));
        SPRAWDZ(b == PP::Blad::OK && w.cr3 == pula_pa && w.uzyte_ramki <= 64 && w.wx_egzekwowane);
        uint64_t pa = 0, e = 0;
        // Okno: tekst R+X (bez W), rodata R+NX, data RW+NX, .boot R+NX; zawsze bez U.
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, 0x101000, &pa, &e) && pa == baza + 0x1000 && !(e & PP::PTE_W) && !(e & PP::PTE_NX) && !(e & PP::PTE_U));
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, 0x22B123, &pa, &e) && pa == baza + 0x12B123 && !(e & PP::PTE_W) && (e & PP::PTE_NX));
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, 0xD6E000, &pa, &e) && pa == baza + 0xC6E000 && (e & PP::PTE_W) && (e & PP::PTE_NX));
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, 0x100000, &pa, &e) && pa == baza && !(e & PP::PTE_W) && (e & PP::PTE_NX));
        // Tozsamosc tylko dla zakresow: stos RW+NX, kod przejscia R+X, MBI R+NX, pula R+NX.
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, stos + 0x3FF8, &pa, &e) && pa == stos + 0x3FF8 && (e & PP::PTE_W) && (e & PP::PTE_NX));
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, 0x9020010, &pa, &e) && pa == 0x9020010 && !(e & PP::PTE_W) && !(e & PP::PTE_NX));
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, mbi + 0x1D5F, &pa, &e) && !(e & PP::PTE_W));
        SPRAWDZ(przejdz(pula.pamiec, w.cr3, pula_pa + 0x3F000, &pa, &e) && !(e & PP::PTE_W) && (e & PP::PTE_NX));
        // Wszystko inne niezmapowane: strona 0, niski 1 MiB, alias podkladu, tuz za oknem, LAPIC.
        SPRAWDZ(!przejdz(pula.pamiec, w.cr3, 0, &pa, &e) && !przejdz(pula.pamiec, w.cr3, 0xB8000, &pa, &e));
        SPRAWDZ(!przejdz(pula.pamiec, w.cr3, baza + 0x1000, &pa, &e) || (baza + 0x1000 >= K_START && baza + 0x1000 < K_END));
        SPRAWDZ(!przejdz(pula.pamiec, w.cr3, K_END, &pa, &e) && !przejdz(pula.pamiec, w.cr3, 0xFEE00000, &pa, &e));
        // Analiza calej hierarchii.
        std::vector<PP::Lisc> bufor(16384);
        const PP::RaportWx r = PP::analizuj(w.cr3, true, pula.pamiec, bufor.data(), bufor.size());
        SPRAWDZ(r.poprawny && r.liscie_wx == 0 && r.liscie_user == 0 && r.aliasy_wx == 0);
        SPRAWDZ(r.liscie == K_ROZMIAR / 0x1000 + 2 + 4 + 1 + 0x780 + 1 + 64);
    }
    const OJ::Uklad u = uklad_na(0x3005000);
    auto blad = [&](const PP::ZakresTozsamosci* z, uint32_t n, uint64_t pula_pa, uint32_t ramki, unsigned bity = 39, bool nx = true) {
        Pula pula(pula_pa, ramki ? ramki : 1);
        const PP::Parametry par{&u, z, n, nx, bity, pula_pa, pula.bufor.data(), ramki};
        PP::Wynik w{};
        return PP::zbuduj(par, &w);
    };
    const PP::ZakresTozsamosci okno[] = {{0x200000, 0x1000, PP::KLASA_STOS, 0}};
    SPRAWDZ(blad(okno, 1, 0x9800000, 64) == PP::Blad::KOLIZJA_Z_OKNEM);
    const PP::ZakresTozsamosci alias[] = {{0x3006000, 0x10, PP::KLASA_TYLKO_ODCZYT, 0}};
    SPRAWDZ(blad(alias, 1, 0x9800000, 64) == PP::Blad::ALIAS_PODKLADU);
    const PP::ZakresTozsamosci konflikt[] = {{0x9000000, 0x1000, PP::KLASA_STOS, 0}, {0x9000800, 0x10, PP::KLASA_KOD_PRZEJSCIA, 0}};
    SPRAWDZ(blad(konflikt, 2, 0x9800000, 64) == PP::Blad::KONFLIKT_KLAS);
    const PP::ZakresTozsamosci te_same[] = {{0x9000000, 0x1000, PP::KLASA_TYLKO_ODCZYT, 0}, {0x9000800, 0x10, PP::KLASA_TYLKO_ODCZYT, 0}};
    SPRAWDZ(blad(te_same, 2, 0x9800000, 64) == PP::Blad::OK);
    SPRAWDZ(blad(nullptr, 0, 0x3100000, 64) == PP::Blad::PULA_KOLIZJA);           // pula na podkladzie
    SPRAWDZ(blad(nullptr, 0, 0x800000, 64) == PP::Blad::PULA_KOLIZJA);            // pula w oknie VA
    const PP::ZakresTozsamosci na_puli[] = {{0x9800000, 0x10, PP::KLASA_TYLKO_ODCZYT, 0}};
    SPRAWDZ(blad(na_puli, 1, 0x9800000, 64) == PP::Blad::PULA_KOLIZJA);
    SPRAWDZ(blad(nullptr, 0, 0x9800000, 3) == PP::Blad::PULA_WYCZERPANA);
    SPRAWDZ(blad(nullptr, 0, 0x9800800, 64) == PP::Blad::PULA_NIEPOPRAWNA);
    SPRAWDZ(blad(nullptr, 0, 0x9800000, 0) == PP::Blad::PULA_NIEPOPRAWNA);
    SPRAWDZ(blad(nullptr, 0, 0x9800000, 64, 35) == PP::Blad::SZEROKOSC_FIZYCZNA);
    const PP::ZakresTozsamosci daleko[] = {{0x1000000000ULL, 0x1000, PP::KLASA_TYLKO_ODCZYT, 0}};   // 64 GiB
    SPRAWDZ(blad(daleko, 1, 0x9800000, 64, 36) == PP::Blad::ZAKRES_POZA_SZEROKOSCIA);
    SPRAWDZ(blad(daleko, 1, 0x9800000, 64, 39) == PP::Blad::OK);
    const PP::ZakresTozsamosci niekan[] = {{0x0000800000000000ULL - 0x800, 0x1000, PP::KLASA_TYLKO_ODCZYT, 0}};
    SPRAWDZ(blad(niekan, 1, 0x9800000, 64, 52) == PP::Blad::ZAKRES_NIEKANONICZNY);
    const PP::ZakresTozsamosci zero[] = {{0x10, 0x10, PP::KLASA_TYLKO_ODCZYT, 0}};
    SPRAWDZ(blad(zero, 1, 0x9800000, 64) == PP::Blad::ZAKRES_STRONA_ZERO);
    const PP::ZakresTozsamosci przep[] = {{UINT64_MAX - 0x10, 0x100, PP::KLASA_TYLKO_ODCZYT, 0}};
    SPRAWDZ(blad(przep, 1, 0x9800000, 64) == PP::Blad::ZAKRES_NIEPOPRAWNY);
    const PP::ZakresTozsamosci pusty[] = {{0x9000000, 0, PP::KLASA_TYLKO_ODCZYT, 0}};
    SPRAWDZ(blad(pusty, 1, 0x9800000, 64) == PP::Blad::ZAKRES_NIEPOPRAWNY);
    const PP::ZakresTozsamosci klasa[] = {{0x9000000, 0x10, 99, 0}};
    SPRAWDZ(blad(klasa, 1, 0x9800000, 64) == PP::Blad::KLASA_NIEZNANA);
    SPRAWDZ(PP::zbuduj(PP::Parametry{nullptr, nullptr, 0, true, 39, 0x9800000, nullptr, 1}, nullptr) == PP::Blad::BRAK_DANYCH);
    // Bez NX: plan poprawny, ale W^X nieegzekwowalne - analiza to wykazuje.
    {
        Pula pula(0x9800000, 64);
        const PP::Parametry par{&u, nullptr, 0, false, 39, 0x9800000, pula.bufor.data(), 64};
        PP::Wynik w{};
        SPRAWDZ(PP::zbuduj(par, &w) == PP::Blad::OK && !w.wx_egzekwowane);
        std::vector<PP::Lisc> bufor(16384);
        const PP::RaportWx r = PP::analizuj(w.cr3, false, pula.pamiec, bufor.data(), bufor.size());
        SPRAWDZ(r.poprawny && r.liscie_wx > 0);
    }
}

/* D. Analiza W^X obecnej przestrzeni jadra (symulowana z produkcyjnego zbuduj_dolne_4gib + alias HHDM). */
static void test_wx_przestrzeni_jadra() {
    for (uint64_t baza : {uint64_t(0x100000), uint64_t(0x3005000)}) {
        const OJ::Uklad u = uklad_na(baza);
        static uint64_t pd[4][512], pt0[512], pula[8][512], pdp[512], pml4[512], hhdm_pdp[512], hhdm_pd[512];
        std::memset(pml4, 0, sizeof pml4); std::memset(pdp, 0, sizeof pdp);
        std::memset(hhdm_pdp, 0, sizeof hhdm_pdp); std::memset(hhdm_pd, 0, sizeof hhdm_pd);
        Pamiec m;
        m.tablice[0x7E000000] = pml4; m.tablice[0x7E001000] = pdp; m.tablice[0x7E002000] = pt0;
        m.tablice[0x7E003000] = hhdm_pdp; m.tablice[0x7E004000] = hhdm_pd;
        for (int g = 0; g < 4; ++g) m.tablice[0x7E010000 + 0x1000ULL * g] = pd[g];
        for (int i = 0; i < 8; ++i) m.tablice[0x7E020000 + 0x1000ULL * i] = pula[i];
        OJ::TabliceDolne4GiB t{pd, pt0, pula, 8, 0};
        SPRAWDZ(OJ::zbuduj_dolne_4gib(u, t, [&](const uint64_t* tab) -> uint64_t {
            if (tab == pt0) return 0x7E002000;
            for (int i = 0; i < 8; ++i) if (tab == pula[i]) return 0x7E020000 + 0x1000ULL * i;
            return 0;
        }));
        pml4[0] = 0x7E001000 | 3;
        for (int g = 0; g < 4; ++g) pdp[g] = (0x7E010000 + 0x1000ULL * g) | 3;
        // HHDM (jak vmm.cpp): podklad obrazu w Direct-Map jako RW+NX, strony 2 MiB tam, gdzie wyrownane.
        pml4[256] = 0x7E003000 | 3;
        hhdm_pdp[0] = 0x7E004000 | 3;
        for (uint64_t r = baza & ~0x1FFFFFULL; r < u.pa_koniec; r += 0x200000) hhdm_pd[r >> 21] = r | 0x83 | PP::PTE_NX;
        std::vector<PP::Lisc> bufor(65536);
        const PP::RaportWx raport = PP::analizuj(0x7E000000, true, m, bufor.data(), bufor.size());
        std::printf("  przestrzen jadra (baza 0x%llx): liscie=%llu W+X=%llu aliasy W/X=%llu user=%llu\n",
                    (unsigned long long)baza, (unsigned long long)raport.liscie, (unsigned long long)raport.liscie_wx,
                    (unsigned long long)raport.aliasy_wx, (unsigned long long)raport.liscie_user);
        // Udokumentowany stan R1A/R1B: W^X jadra NIE jest egzekwowane (okno RWX + alias HHDM).
        SPRAWDZ(raport.poprawny && raport.liscie_wx > 0 && raport.aliasy_wx > 0 && raport.liscie_user == 0);
    }
}

/* ---------------------------------------------------------------------------------------------------------------
 * E. Przyjecie kontraktu natywnego: koniec-do-konca plan przejscia -> przejscie po tablicach -> kontrakt.
 * ------------------------------------------------------------------------------------------------------------- */
struct Region { uint64_t pa; std::vector<uint8_t> d; };
static std::vector<Region> g_pamiec;
static const uint8_t* dostep(uint64_t pa, uint64_t n) {
    for (const Region& r : g_pamiec) if (n && pa >= r.pa && pa - r.pa <= r.d.size() && n <= r.d.size() - (pa - r.pa)) return r.d.data() + (pa - r.pa);
    return nullptr;
}

constexpr uint64_t BAZA = 0x3005000, INFO = 0x9000000, POOL = 0x9800000, STOS = 0x9010000, KONTRAKT = 0x9014000;
static Pula* g_pula = nullptr;

static std::vector<uint8_t> mbi_z_mapa(std::vector<WpisMapyPamieciMB2> wpisy) {
    std::vector<uint8_t> b(16 + 16 + 24 * wpisy.size() + 8, 0);
    const uint32_t calosc = uint32_t(b.size()), t6 = 6, r6 = uint32_t(16 + 24 * wpisy.size()), ew = 24;
    std::memcpy(&b[0], &calosc, 4);
    std::memcpy(&b[8], &t6, 4); std::memcpy(&b[12], &r6, 4); std::memcpy(&b[16], &ew, 4);
    std::memcpy(&b[24], wpisy.data(), 24 * wpisy.size());
    return b;
}

static KP::KontraktV1 kontrakt_natywny(const std::vector<uint8_t>& mbi) {
    KP::KontraktV1 k{};
    k.sygnatura = KP::SYGNATURA; k.wersja = KP::WERSJA; k.rozmiar = sizeof(k);
    k.zrodlo = KP::ZRODLO_NATYWNE_64; k.flagi = KP::FLAGA_TOZSAMOSC_ZASOBOW; k.model_podkladu = KP::PODKLAD_CIAGLY;
    const KP::OczekiwanyObraz o = KP::oczekiwany_z_symboli(SYMBOLE);
    k.jadro_baza_wirtualna = K_START; k.jadro_baza_fizyczna = BAZA; k.jadro_rozmiar = K_ROZMIAR;
    k.liczba_segmentow = o.liczba_segmentow;
    for (uint32_t i = 0; i < o.liczba_segmentow; ++i) k.segmenty[i] = o.segmenty[i];
    k.cr3_rozruchu = POOL;
    k.informacje_adres = INFO; k.informacje_rozmiar = mbi.size();
    k.mapa_adres = INFO + 8; k.mapa_rozmiar = 16 + 24 * ((mbi.size() - 24) / 24); k.mapa_format = KP::MAPA_MB2_TAG6; k.mapa_rozmiar_wpisu = 24;
    k.liczba_zakresow = 4;
    k.zakresy[0] = {BAZA, K_ROZMIAR, KP::RODZAJ_OBRAZ_JADRA, KP::ZYCIE_TRWALE, 0, 0};
    k.zakresy[1] = {INFO, mbi.size(), KP::RODZAJ_INFORMACJE_ROZRUCHU, KP::ZYCIE_TRWALE, KP::ZAKRES_DOSTEP_TOZSAMOSCIOWY, 0};
    k.zakresy[2] = {POOL, 64 * 0x1000, KP::RODZAJ_TABLICE_STRON_ROZRUCHU, KP::ZYCIE_DO_PRZELACZENIA_CR3, 0, 0};
    k.zakresy[3] = {STOS, 0x5000, KP::RODZAJ_ODZYSKIWALNY_ROZRUCHU, KP::ZYCIE_DO_PRZELACZENIA_CR3, 0, 0};
    return k;
}

static std::vector<uint8_t> bajty(const KP::KontraktV1& k) {
    std::vector<uint8_t> b(sizeof(k));
    std::memcpy(b.data(), &k, sizeof(k));
    return b;
}

/* Tlumaczenie przez symulowane tablice przejscia, z ograniczeniem do zakresow TABLICE (jak w pmm.cpp). */
static bool tlumacz_planem(const KP::KontraktV1& k, uint64_t va, uint64_t* pa) {
    uint64_t t = k.cr3_rozruchu;
    for (int poziom = 4; poziom >= 1; --poziom) {
        if (!KP::zasob_tozsamosciowy(k, t, 0x1000)) return false;
        const uint64_t* tab = g_pula->pamiec(t);
        if (!tab) return false;
        const uint64_t e = tab[(va >> (12 + 9 * (poziom - 1))) & 511];
        if (!(e & 1)) return false;
        if (poziom == 1) { *pa = (e & PP::MASKA_ADRESU) | (va & 0xFFF); return true; }
        t = e & PP::MASKA_ADRESU;
    }
    return false;
}

static void test_kontrakt_natywny() {
    const std::vector<uint8_t> mbi = mbi_z_mapa({{0x100000, 0x7FF00000, 1, 0}});
    const OJ::Uklad u = uklad_na(BAZA);
    Pula pula(POOL, 64);
    g_pula = &pula;
    const PP::ZakresTozsamosci z[] = {{INFO, mbi.size(), PP::KLASA_TYLKO_ODCZYT, 0}, {STOS, 0x4000, PP::KLASA_STOS, 0},
                                      {KONTRAKT, sizeof(KP::KontraktV1), PP::KLASA_TYLKO_ODCZYT, 0}};
    PP::Wynik w{};
    SPRAWDZ(PP::zbuduj(PP::Parametry{&u, z, 3, true, 39, POOL, pula.bufor.data(), 64}, &w) == PP::Blad::OK);
    const KP::OczekiwanyObraz ocz = KP::oczekiwany_z_symboli(SYMBOLE);
    auto przyjmij = [&](const KP::KontraktV1& k, uint64_t pa_k, uint64_t cr3, KP::KontraktV1* kopia, OJ::Uklad* uk) {
        g_pamiec = {{INFO, mbi}, {KONTRAKT, bajty(k)}};
        return KP::przyjmij_kontrakt_natywny(pa_k, cr3, ocz, dostep, tlumacz_planem, kopia, uk);
    };
    KP::KontraktV1 kopia{};
    OJ::Uklad uk{};
    const KP::KontraktV1 dobry = kontrakt_natywny(mbi);
    SPRAWDZ(przyjmij(dobry, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::OK && uk.pa_poczatek == BAZA && uk.va_poczatek == K_START);
    SPRAWDZ(przyjmij(dobry, KONTRAKT, POOL | 0x18, &kopia, &uk) == KP::Blad::OK);   // PWT/PCD w CR3 to flagi, nie adres
    SPRAWDZ(przyjmij(dobry, KONTRAKT, POOL + 0x1000, &kopia, &uk) == KP::Blad::CR3_NIEZGODNY);
    SPRAWDZ(przyjmij(dobry, 0, POOL, &kopia, &uk) == KP::Blad::KONTRAKT_POZA_ZAKRESEM);
    SPRAWDZ(przyjmij(dobry, KONTRAKT + 4, POOL, &kopia, &uk) == KP::Blad::KONTRAKT_POZA_ZAKRESEM);
    SPRAWDZ(przyjmij(dobry, 0x100000000ULL, POOL, &kopia, &uk) == KP::Blad::KONTRAKT_POZA_ZAKRESEM);
    SPRAWDZ(przyjmij(dobry, KONTRAKT + 0x1000, POOL, &kopia, &uk) == KP::Blad::KONTRAKT_POZA_ZAKRESEM);   // brak pamieci
    { KP::KontraktV1 k = dobry; k.zrodlo = KP::ZRODLO_MB2_ZGODNOSC; k.flagi |= KP::FLAGA_TOZSAMOSC_4GIB_PRZED_VMM;
      SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::MB2_NIETOZSAMOSCIOWY); }
    { KP::KontraktV1 k = dobry; k.flagi = KP::FLAGA_TOZSAMOSC_4GIB_PRZED_VMM;
      SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::TOZSAMOSC_NIEOKRESLONA); }
    { KP::KontraktV1 k = dobry; k.flagi = 0;
      SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::TOZSAMOSC_NIEOKRESLONA); }
    { KP::KontraktV1 k = dobry; k.zakresy[3].dlugosc = 0x4000;   // kontrakt (0x9014000) poza zakresem odzyskiwalnym
      SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::KONTRAKT_POZA_ZAKRESEM); }
    { KP::KontraktV1 k = dobry; k.wersja = 2; SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::WERSJA_NIEOBSLUGIWANA); }
    { KP::KontraktV1 k = dobry; k.jadro_baza_fizyczna = BAZA + 0x1000; k.zakresy[0].poczatek = BAZA + 0x1000;   // klamstwo o bazie
      SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::PODKLAD_NIEZGODNY_Z_KONTRAKTEM); }
    { KP::KontraktV1 k = dobry; k.mapa_adres = INFO + 0x2000;   // mapa poza informacjami
      SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::MAPA_NIEPOPRAWNA); }
    { KP::KontraktV1 k = dobry; k.zakresy[2].dlugosc = 0x1000;   // tablice przejscia poza opisem => pomiar niemozliwy
      SPRAWDZ(przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::TLUMACZENIE_OBRAZU); }
    // Dziura w tablicach (strona okna niezmapowana) => pomiar odrzucony.
    {
        uint64_t pa = 0, e = 0;
        SPRAWDZ(przejdz(pula.pamiec, POOL, 0x500000, &pa, &e));
        uint64_t t = POOL;
        for (int poziom = 4; poziom > 1; --poziom) t = pula.pamiec(t)[(0x500000ULL >> (12 + 9 * (poziom - 1))) & 511] & PP::MASKA_ADRESU;
        uint64_t* pt = const_cast<uint64_t*>(pula.pamiec(t));
        const uint64_t zapis = pt[(0x500000 >> 12) & 511];
        pt[(0x500000 >> 12) & 511] = 0;
        SPRAWDZ(przyjmij(dobry, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::TLUMACZENIE_OBRAZU);
        pt[(0x500000 >> 12) & 511] = zapis + 0x1000;   // nieciaglosc
        SPRAWDZ(przyjmij(dobry, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::PODKLAD_NIECIAGLY);
        pt[(0x500000 >> 12) & 511] = zapis;
    }
    // Mutacje losowe kopii kontraktu: kazdy przyjety wynik spelnia niezmienniki natywne.
    uint64_t x = 0x52314233ULL;
    auto los = [&]() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };
    unsigned przyjete = 0, naruszenia = 0;
    for (unsigned i = 0; i < 20000; ++i) {
        KP::KontraktV1 k = dobry;
        uint8_t* b = reinterpret_cast<uint8_t*>(&k);
        b[los() % sizeof(k)] ^= uint8_t(1U << (los() % 8));
        if (przyjmij(k, KONTRAKT, POOL, &kopia, &uk) == KP::Blad::OK) {
            ++przyjete;
            if (!(uk.pa_poczatek == BAZA && kopia.zrodlo == KP::ZRODLO_NATYWNE_64 && kopia.cr3_rozruchu == POOL &&
                  (kopia.flagi & KP::FLAGA_TOZSAMOSC_ZASOBOW))) ++naruszenia;
        }
    }
    std::printf("  kontrakt natywny: mutacje losowe 20000, przyjete %u, naruszenia %u\n", przyjete, naruszenia);
    SPRAWDZ(naruszenia == 0);
}

/* ---------------------------------------------------------------------------------------------------------------
 * F. PMM produkcyjny: natywny kontrakt, dostep fizyczny, bramka zasobow, czas zycia zasobow rozruchu.
 * ------------------------------------------------------------------------------------------------------------- */
static void test_pmm_natywny() {
    PmmHost::symbole = SYMBOLE;
    PmmHost::dostep = dostep;
    // ACPI NVS w cieniu okna VA (0x800000): nie-RAM, poza HHDM - jedyna droga bylaby tozsamosc, ktora pokazuje obraz.
    const std::vector<uint8_t> mbi = mbi_z_mapa({{0x100000, 0x700000, 1, 0}, {0x800000, 0x100000, 4, 0}, {0x900000, 0x3F700000, 1, 0},
                                                 {0x40000000, 0x10000, 3, 0}, {0x40010000, 0x3FFF0000, 1, 0}});
    const KP::KontraktV1 dobry = kontrakt_natywny(mbi);
    g_pamiec = {{INFO, mbi}, {KONTRAKT, bajty(dobry)}};
    // Przyjecie przez PMM (w hoscie tlumaczenie = model PA == VA, wiec baza musi byc tozsamosciowa: uzywamy
    // podstawienia tlumacza przesuwajacego okno na BAZA - model tablic przejscia).
    PmmHost::tlumacz = [](uint64_t va, uint64_t* pa) { *pa = va - K_START + BAZA; return va >= K_START && va < K_END; };
    const KP::KontraktV1* k = PrzyjmijKontraktNatywny(KONTRAKT, POOL);
    SPRAWDZ(k != nullptr && PmmStatusRozruchu().kod == 0);
    SPRAWDZ(PrzyjmijKontraktNatywny(KONTRAKT, POOL + 0x1000) == nullptr &&
            PmmStatusRozruchu().kod == uint32_t(KP::Blad::CR3_NIEZGODNY));
    SPRAWDZ(PrzyjmijKontraktNatywny(KONTRAKT + 8, POOL) == nullptr);
    k = PrzyjmijKontraktNatywny(KONTRAKT, POOL);
    SPRAWDZ(k && InicjalizujPMMZKontraktem(k, sizeof(*k)) && PmmStatusRozruchu().kod == 0);
    const OJ::Uklad* u = ObrazJadraAktywny();
    SPRAWDZ(u && u->pa_poczatek == BAZA && !OJ::tozsamosciowy(*u));
    // Czas zycia: tablice przejscia, stos przejscia i kontrakt sa zajete i nie wracaja przez zadne API.
    const uint64_t wolne = pmm_pobierz_wolne_ramki();
    ZwolnijRamke(reinterpret_cast<void*>(POOL));
    ZwolnijCiagleRamki(reinterpret_cast<void*>(STOS), 4);
    OdblokujZakres(POOL, 64 * 0x1000);
    OdblokujRamke(KONTRAKT);
    SPRAWDZ(pmm_pobierz_wolne_ramki() == wolne && !CzyRamkaWolna(POOL) && !CzyRamkaWolna(STOS) && !CzyRamkaWolna(KONTRAKT));
    SPRAWDZ(!CzyRamkaWolna(0x500000));   // cien okna VA (rezerwacja R1A zachowana)
    // Dostep fizyczny PRZED VMM (FLAGA_TOZSAMOSC_ZASOBOW): tylko zasoby opisane w kontrakcie.
    SPRAWDZ(PamiecFizycznaDoOdczytu(INFO, 16) == reinterpret_cast<const void*>(INFO));
    SPRAWDZ(PamiecFizycznaDoOdczytu(KONTRAKT, 64) == reinterpret_cast<const void*>(KONTRAKT));
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x40000000, 0x100) == nullptr);   // ACPI: nieopisany przed VMM
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x20000000, 16) == nullptr);
    SPRAWDZ(PamiecFizycznaDoOdczytu(INFO, 0) == nullptr && PamiecFizycznaDoOdczytu(UINT64_MAX - 4, 16) == nullptr);
    // Bramka zasobow (MBI poza oknem i aliasem) i po VMM.
    uint64_t adres = 0;
    SPRAWDZ(PmmZasobyRozruchuTozsamosciowe(&adres));
    PmmUstawDirectMapAktywny();
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x20000000, 16) == reinterpret_cast<const void*>(BAZA_MAPOWANIA_FIZYCZNEGO + 0x20000000));
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x40000000, 0x100) == reinterpret_cast<const void*>(0x40000000));   // ACPI: tozsamosc z planu
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x800000, 0x100) == nullptr);       // nie-RAM w oknie obrazu (VA pokazuje obraz)
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x7FF000, 0x2000) == nullptr);      // przecina NVS w oknie
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x500000, 0x100) == reinterpret_cast<const void*>(BAZA_MAPOWANIA_FIZYCZNEGO + 0x500000));   // RAM w cieniu okna: HHDM = wlasciwa ramka
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x80000000, 0x100) != nullptr);     // poza RAM, tozsamosc (MMIO/ACPI) - jak dotad
    SPRAWDZ(PamiecFizycznaDoOdczytu(0x100000000ULL, 0x100) == nullptr); // > 4 GiB: okno ACPI w acpi.cpp
    SPRAWDZ(PamiecFizycznaDoOdczytu(0, 0x10) == nullptr);               // strona 0
    PmmHost::tlumacz = nullptr;
    PmmHost::dostep = nullptr;
}

int main(int argc, char** argv) {
    std::printf("[R1B] A: kontrakt stanu procesora\n"); test_stan_cpu();
    std::printf("[R1B] B: metadane wejscia (syntetyczne)\n"); test_metadane_syntetyczne();
    std::printf("[R1B] B: metadane wejscia (jadro)\n");
    if (argc >= 5) test_metadane_jadra(argv[1], argv[2], std::strtoull(argv[3], nullptr, 16), std::strtoull(argv[4], nullptr, 16));
    else std::printf("  NIE URUCHOMIONO: brak sciezek jadra\n");
    std::printf("[R1B] C: tablice przejscia i W^X\n"); test_plan_przejscia();
    std::printf("[R1B] D: W^X obecnej przestrzeni jadra (symulacja)\n"); test_wx_przestrzeni_jadra();
    std::printf("[R1B] E: przyjecie kontraktu natywnego\n"); test_kontrakt_natywny();
    std::printf("[R1B] F: PMM natywny i dostep fizyczny\n"); test_pmm_natywny();
    std::printf("[R1B] WYNIK: PASS=%u FAIL=%u\n", g_ok, g_bledy);
    return g_bledy ? 1 : 0;
}
