/*
 * BRB - testy hosta parsera ELF64 i planu ladowania (Faza 3, T-ELF; ASan + UBSan).
 *
 * Uzycie:
 *   test-brb-elf                       testy jednostkowe + deterministyczne mutacje obrazu syntetycznego
 *   test-brb-elf --jadro <plik>        T-ELF-PROD: walidacja prawdziwego jadra, wydruk planu (porownywany
 *                                      z readelf przez narzedzia/brb/porownaj_plan_readelf.py) + mutacje
 *                                      naglowkow prawdziwego jadra
 */
#include "testy/brb_elf_fixture.h"

#include "rozruch/brb/wspolne/elf64.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using brb::BladElf;
using fixture::Obraz;
using fixture::ph;

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

brb::WynikElf parsuj(const Obraz& o, const brb::PolitykaElf& pol, brb::PlanLadowania* plan) {
    // Kopia o dokladnym rozmiarze na stercie: ASan wykryje kazdy odczyt poza plikiem.
    std::vector<uint8_t> kopia(o.b);
    return brb::zbuduj_plan_ladowania(kopia.empty() ? nullptr : kopia.data(), kopia.size(), pol, plan);
}

struct Przypadek {
    const char* nazwa;
    std::function<void(Obraz&)> zmien;
    BladElf oczekiwany;
    const brb::PolitykaElf* polityka;
};

void test_poprawne() {
    std::printf("[test] ELF: poprawne obrazy\n");
    const Obraz o = fixture::jadro_minimalne();
    brb::PlanLadowania p;
    const brb::WynikElf w = parsuj(o, brb::POLITYKA_JADRA_BURSZTYN, &p);
    SPRAWDZ(w.blad == BladElf::Brak && w.indeks_ph == -1);
    SPRAWDZ(p.liczba_segmentow == 3 && p.rozmiar_pliku == 0x3080);
    SPRAWDZ(p.segmenty[2].przesuniecie_pliku == 0x3000 && p.segmenty[2].adres_fizyczny == 0x102000 &&
            p.segmenty[2].adres_wirtualny == 0x102000 && p.segmenty[2].bajty_z_pliku == 0x80 &&
            p.segmenty[2].rozmiar_w_pamieci == 0x2000 && p.segmenty[2].bajty_zerowane == 0x1F80 &&
            p.segmenty[2].prawa == (brb::ELF_PF_R | brb::ELF_PF_W) && p.segmenty[2].strona_poczatek == 0x102000 &&
            p.segmenty[2].strona_koniec == 0x104000 && p.segmenty[2].liczba_stron == 2 && p.segmenty[2].wyrownanie == 0x1000);
    SPRAWDZ(p.wejscie_wirtualne == 0x101010 && p.wejscie_fizyczne == 0x101010 && p.segment_wejscia == 1);
    SPRAWDZ(p.zakres_poczatek == 0x100000 && p.zakres_koniec == 0x104000 && p.pamiec_stron == 0x4000);
    SPRAWDZ(p.bajty_z_pliku == 0x30 + 0x100 + 0x80 && p.bajty_zerowane == 0x1F80);
    SPRAWDZ(p.mb2.obecny && p.mb2.przesuniecie_pliku == 0x1000 && p.mb2.dlugosc == 48 && p.mb2.liczba_tagow == 2 &&
            p.mb2.segment == 0);   // naglowek 48 B = 0x30 B segmentu .boot: w calosci w segmencie 0
    SPRAWDZ(fixture::niezmienniki(p, o.b.size(), brb::POLITYKA_JADRA_BURSZTYN).empty());

    // Naglowek MB2 tylko czesciowo w segmencie: nadal poprawny (MB2 wymaga go w pliku), segment = -1.
    Obraz m = o;
    m.w64(ph(0) + fixture::P_FILESZ, 0x20);
    m.w64(ph(0) + fixture::P_MEMSZ, 0x20);
    SPRAWDZ(parsuj(m, brb::POLITYKA_JADRA_BURSZTYN, &p).blad == BladElf::Brak && p.mb2.segment == -1);

    // p_align 0 i 1: dozwolone bez wymogu kongruencji (gABI).
    for (uint64_t a : {0ull, 1ull}) {
        Obraz x = o;
        x.w64(ph(2) + fixture::P_ALIGN, a);
        x.w64(ph(2) + fixture::P_OFFSET, 0x3001);   // celowo niekongruentne
        x.b.resize(0x3081);
        SPRAWDZ(parsuj(x, brb::POLITYKA_JADRA_BURSZTYN, &p).blad == BladElf::Brak);
    }
    // Wspolna strona przy identycznych prawach jest dozwolona, a pamiec liczona bez powtorzen.
    Obraz s = o;
    s.segment(2, 1, brb::ELF_PF_R | brb::ELF_PF_X, 0x2100, 0x101100, 0x101100, 0x80, 0x80, 0x1000);
    SPRAWDZ(parsuj(s, brb::POLITYKA_JADRA_BURSZTYN, &p).blad == BladElf::Brak && p.pamiec_stron == 0x2000);
    SPRAWDZ(fixture::niezmienniki(p, s.b.size(), brb::POLITYKA_JADRA_BURSZTYN).empty());

    // Adresy powyzej 4 GiB (polityka ogolna) i jadro "higher-half" (vaddr != paddr): pelne 64 bity bez obciecia.
    Obraz h = o;
    const uint64_t baza_v = 0xFFFFFFFF80000000ull, baza_f = 0x0000000140000000ull;
    for (unsigned i = 0; i < 3; ++i) {
        h.w64(ph(i) + fixture::P_VADDR, baza_v + 0x1000ull * i);
        h.w64(ph(i) + fixture::P_PADDR, baza_f + 0x1000ull * i);
    }
    h.w64(24, baza_v + 0x1010);
    SPRAWDZ(parsuj(h, brb::POLITYKA_OGOLNA, &p).blad == BladElf::Brak);
    SPRAWDZ(p.wejscie_wirtualne == baza_v + 0x1010 && p.wejscie_fizyczne == baza_f + 0x1010);
    SPRAWDZ(p.zakres_poczatek == baza_f && p.zakres_koniec == baza_f + 0x3000 + 0x1000);
    SPRAWDZ(fixture::niezmienniki(p, h.b.size(), brb::POLITYKA_OGOLNA).empty());
    SPRAWDZ(parsuj(h, brb::POLITYKA_JADRA_BURSZTYN, &p).blad == BladElf::AdresNietozsamosciowy);

    // Segmenty nie-PT_LOAD (PT_NOTE, PT_GNU_STACK) sa ignorowane.
    Obraz n = o;
    n.w16(56, 5);
    n.segment(3, 4, 4, 0x3000, 0, 0, 0x10, 0x10, 4);
    n.segment(4, 0x6474E551u, 6, 0, 0, 0, 0, 0, 16);
    SPRAWDZ(parsuj(n, brb::POLITYKA_JADRA_BURSZTYN, &p).blad == BladElf::Brak && p.liczba_segmentow == 3);
}

void test_bledne() {
    std::printf("[test] ELF: obrazy bledne (oczekiwany konkretny kod)\n");
    const brb::PolitykaElf* J = &brb::POLITYKA_JADRA_BURSZTYN;
    const brb::PolitykaElf* G = &brb::POLITYKA_OGOLNA;
    static const brb::PolitykaElf maly_limit{0x100000, 0x1000000, 0x100000000ull, 0x3000, true, true};
    static const brb::PolitykaElf wejscie32{0, ~0ull, 0x100000000ull, 1ull << 40, false, false};
    using F = fixture::Obraz;
    const std::vector<Przypadek> lista = {
        {"obciety naglowek (63 B)", [](F& o) { o.b.resize(63); }, BladElf::ZaKrotki, J},
        {"plik pusty", [](F& o) { o.b.clear(); }, BladElf::ZaKrotki, J},
        {"zla sygnatura", [](F& o) { o.b[1] = 'X'; }, BladElf::ZlaSygnatura, J},
        {"ELFCLASS32", [](F& o) { o.b[4] = 1; }, BladElf::NieKlasa64, J},
        {"big-endian", [](F& o) { o.b[5] = 2; }, BladElf::NieLittleEndian, J},
        {"EI_VERSION 0", [](F& o) { o.b[6] = 0; }, BladElf::ZlaWersja, J},
        {"e_version 2", [](F& o) { o.w32(20, 2); }, BladElf::ZlaWersja, J},
        {"OSABI FreeBSD (9)", [](F& o) { o.b[7] = 9; }, BladElf::NieobslugiwaneAbi, J},
        {"ABIVERSION 1", [](F& o) { o.b[8] = 1; }, BladElf::NieobslugiwaneAbi, J},
        {"EM_386", [](F& o) { o.w16(18, 3); }, BladElf::ZlaArchitektura, J},
        {"EM_AARCH64", [](F& o) { o.w16(18, 183); }, BladElf::ZlaArchitektura, J},
        {"ET_DYN", [](F& o) { o.w16(16, 3); }, BladElf::NieobslugiwanyTyp, J},
        {"ET_REL", [](F& o) { o.w16(16, 1); }, BladElf::NieobslugiwanyTyp, J},
        {"e_ehsize 52", [](F& o) { o.w16(52, 52); }, BladElf::ZlyRozmiarNaglowka, J},
        {"e_phentsize 32", [](F& o) { o.w16(54, 32); }, BladElf::ZlyRozmiarWpisuPh, J},
        {"e_phentsize 64", [](F& o) { o.w16(54, 64); }, BladElf::ZlyRozmiarWpisuPh, J},
        {"e_phnum 0", [](F& o) { o.w16(56, 0); }, BladElf::ZlaLiczbaPh, J},
        {"e_phnum PN_XNUM", [](F& o) { o.w16(56, 0xFFFF); }, BladElf::ZlaLiczbaPh, J},
        {"e_phnum 17", [](F& o) { o.w16(56, 17); }, BladElf::ZlaLiczbaPh, J},
        {"tablica PH obcieta", [](F& o) { o.b.resize(64 + 56 * 2 + 10); }, BladElf::TablicaPhPozaPlikiem, J},
        {"e_phoff poza plikiem", [](F& o) { o.w64(32, 0x3080); }, BladElf::TablicaPhPozaPlikiem, J},
        {"e_phoff przepelnienie", [](F& o) { o.w64(32, ~0ull - 7); }, BladElf::TablicaPhPozaPlikiem, J},
        {"e_phoff w naglowku", [](F& o) { o.w64(32, 8); }, BladElf::TablicaPhPozaPlikiem, J},
        {"e_phoff niewyrownane", [](F& o) { o.w64(32, 65); }, BladElf::TablicaPhPozaPlikiem, J},
        {"PT_DYNAMIC", [](F& o) { o.w32(ph(1), 2); }, BladElf::NieobslugiwanySegment, J},
        {"PT_INTERP", [](F& o) { o.w32(ph(0), 3); }, BladElf::NieobslugiwanySegment, J},
        {"PT_TLS", [](F& o) { o.w32(ph(2), 7); }, BladElf::NieobslugiwanySegment, J},
        {"p_memsz 0", [](F& o) { o.w64(ph(2) + fixture::P_MEMSZ, 0); o.w64(ph(2) + fixture::P_FILESZ, 0); }, BladElf::ZerowySegment, J},
        {"p_filesz > p_memsz", [](F& o) { o.w64(ph(1) + fixture::P_FILESZ, 0x101); }, BladElf::PlikWiekszyNizPamiec, J},
        {"segment poza plikiem", [](F& o) { o.w64(ph(2) + fixture::P_FILESZ, 0x81); }, BladElf::SegmentPozaPlikiem, J},
        {"p_offset przepelnienie", [](F& o) { o.w64(ph(2) + fixture::P_OFFSET, ~0ull - 1); }, BladElf::SegmentPozaPlikiem, J},
        {"p_align 3", [](F& o) { o.w64(ph(1) + fixture::P_ALIGN, 3); }, BladElf::ZleWyrownanie, J},
        {"p_align 2^63+1", [](F& o) { o.w64(ph(1) + fixture::P_ALIGN, (1ull << 63) + 1); }, BladElf::ZleWyrownanie, J},
        {"kongruencja", [](F& o) { o.w64(ph(1) + fixture::P_OFFSET, 0x2010); o.b.resize(0x3080); }, BladElf::ZlaKongruencja, J},
        {"p_paddr + p_memsz przepelnienie", [](F& o) { o.segment(2, 1, 6, 0x3000, ~0ull - 0xFFF - 0x10000, ~0ull - 5, 0x80, 0x2000, 1); },
         BladElf::PrzepelnienieAdresu, G},
        {"p_vaddr + p_memsz przepelnienie", [](F& o) { o.segment(2, 1, 6, 0x3000, ~0ull - 5, 0x102000, 0x80, 0x2000, 1); },
         BladElf::PrzepelnienieAdresu, G},
        {"zaokraglenie strony przepelnia", [](F& o) { o.segment(2, 1, 6, 0x3000, 0xFFFFFFFFFFFFF000ull, 0xFFFFFFFFFFFFF001ull, 0x10, 0x10, 1); },
         BladElf::PrzepelnienieAdresu, G},
        {"segment ponizej 1 MiB", [](F& o) { o.segment(0, 1, 4, 0x1000, 0xFF000, 0xFF000, 0x30, 0x30, 0x1000); }, BladElf::AdresPozaZakresem, J},
        {"segment do 16 MiB + 1 strona", [](F& o) { o.w64(ph(2) + fixture::P_MEMSZ, 0x1000000 - 0x102000 + 1); }, BladElf::AdresPozaZakresem, J},
        {"segment powyzej 4 GiB (jadro)", [](F& o) { o.w64(ph(2) + fixture::P_VADDR, 0x100000000ull); o.w64(ph(2) + fixture::P_PADDR, 0x100000000ull); },
         BladElf::AdresPozaZakresem, J},
        {"p_paddr != p_vaddr (jadro)", [](F& o) { o.w64(ph(2) + fixture::P_PADDR, 0x202000); }, BladElf::AdresNietozsamosciowy, J},
        {"brak PF_R", [](F& o) { o.w32(ph(2) + fixture::P_FLAGS, brb::ELF_PF_W); }, BladElf::BrakPrawaOdczytu, J},
        {"W+X", [](F& o) { o.w32(ph(1) + fixture::P_FLAGS, 7); }, BladElf::ZapisIWykonanie, J},
        {"nieposortowane", [](F& o) { o.segment(2, 1, 6, 0x3000, 0x50000, 0x50000, 0x80, 0x80, 0x1000); },
         BladElf::Nieposortowane, G},
        {"nakladanie bajtow", [](F& o) { o.segment(2, 1, 4, 0x2000, 0x1010F0 - 0xF0 + 0xF0, 0x1010F0, 0x10, 0x10, 0x10); },
         BladElf::Nakladanie, J},
        {"nakladanie tylko wirtualne", [](F& o) { o.segment(2, 1, 4, 0x2000, 0x101080, 0x500000, 0x10, 0x10, 0x10); },
         BladElf::Nakladanie, G},
        {"wspolna strona, rozne prawa", [](F& o) { o.segment(2, 1, 6, 0x2100, 0x101100, 0x101100, 0x80, 0x80, 0x1000); },
         BladElf::KonfliktStrony, J},
        {"brak PT_LOAD", [](F& o) { for (unsigned i = 0; i < 3; ++i) o.w32(ph(i), 4); }, BladElf::BrakSegmentow, J},
        {"wejscie poza segmentami", [](F& o) { o.w64(24, 0x900000); }, BladElf::WejsciePozaSegmentem, J},
        {"wejscie w segmencie R", [](F& o) { o.w64(24, 0x100004); }, BladElf::WejscieNiewykonywalne, J},
        {"wejscie w segmencie RW", [](F& o) { o.w64(24, 0x102010); }, BladElf::WejscieNiewykonywalne, J},
        {"wejscie w BSS segmentu RX", [](F& o) { o.w64(ph(1) + fixture::P_MEMSZ, 0x800); o.w64(24, 0x101400); }, BladElf::WejscieWObszarzeZerowanym, J},
        {"wejscie >= 4 GiB", [](F& o) {
             for (unsigned i = 0; i < 3; ++i) { o.w64(ph(i) + fixture::P_VADDR, 0x200000000ull + 0x1000ull * i); o.w64(ph(i) + fixture::P_PADDR, 0x200000000ull + 0x1000ull * i); }
             o.w64(24, 0x200001010ull); },
         BladElf::WejsciePozaZakresem, &wejscie32},
        {"limit pamieci", [](F&) {}, BladElf::PrzekroczonyLimitPamieci, &maly_limit},
        {"brak Multiboot2", [](F& o) { o.w32(0x1000, 0); }, BladElf::BrakNaglowkaMb2, J},
        {"Multiboot2 zla suma", [](F& o) { o.w32(0x100C, 0); }, BladElf::ZlyNaglowekMb2, J},
        {"Multiboot2 architektura MIPS", [](F& o) { o.w32(0x1004, 4); o.w32(0x100C, uint32_t(0u - (0xE85250D6u + 4 + 48))); }, BladElf::ZlyNaglowekMb2, J},
        {"Multiboot2 bez tagu konca", [](F& o) { o.w16(0x1028, 9); }, BladElf::ZlyNaglowekMb2, J},
        {"Multiboot2 tag < 8 B", [](F& o) { o.w32(0x1014, 4); }, BladElf::ZlyNaglowekMb2, J},
        {"Multiboot2 poza oknem 32 KiB", [](F& o) {
             std::vector<uint8_t> mb(o.b.begin() + 0x1000, o.b.begin() + 0x1030);
             std::fill(o.b.begin() + 0x1000, o.b.begin() + 0x1030, 0);
             o.b.resize(0x9000);
             std::copy(mb.begin(), mb.end(), o.b.begin() + 0x8000); },
         BladElf::BrakNaglowkaMb2, J},
    };
    int idx = 0;
    for (const Przypadek& p : lista) {
        Obraz o = fixture::jadro_minimalne();
        p.zmien(o);
        brb::PlanLadowania plan;
        std::memset(&plan, 0xA5, sizeof(plan));
        const brb::WynikElf w = parsuj(o, *p.polityka, &plan);
        if (w.blad != p.oczekiwany)
            std::printf("  przypadek %d '%s': wynik=%d (%s), oczekiwany=%d\n", idx, p.nazwa, int(w.blad),
                        brb::opis_bledu_elf(w.blad), int(p.oczekiwany));
        SPRAWDZ(w.blad == p.oczekiwany);
        ++idx;
    }
    // Plik wiekszy niz 64 MiB: odrzucony przed odczytem czegokolwiek poza dlugoscia.
    std::vector<uint8_t> duzy(brb::ELF_MAKS_ROZMIAR_PLIKU + 1, 0);
    brb::PlanLadowania plan;
    SPRAWDZ(brb::zbuduj_plan_ladowania(duzy.data(), duzy.size(), brb::POLITYKA_JADRA_BURSZTYN, &plan).blad == BladElf::ZaDuzyPlik);
    SPRAWDZ(brb::zbuduj_plan_ladowania(nullptr, 100, brb::POLITYKA_JADRA_BURSZTYN, &plan).blad == BladElf::ZaKrotki);
    SPRAWDZ(brb::zbuduj_plan_ladowania(duzy.data(), 100, brb::POLITYKA_JADRA_BURSZTYN, nullptr).blad == BladElf::ZaKrotki);
    SPRAWDZ(plan.liczba_segmentow == 0 && plan.mb2.segment == -1);   // plan zerowany takze przy bledzie
    // Indeks naglowka programu w wyniku.
    Obraz o = fixture::jadro_minimalne();
    o.w32(ph(2) + fixture::P_FLAGS, 7);
    SPRAWDZ(parsuj(o, brb::POLITYKA_JADRA_BURSZTYN, &plan).indeks_ph == 2);
    // Kazdy kod ma opis.
    for (int k = 0; k <= int(BladElf::ZlyNaglowekMb2); ++k)
        SPRAWDZ(std::strcmp(brb::opis_bledu_elf(BladElf(k)), "nieznany blad ELF") != 0);
}

// Deterministyczne mutacje: kazdy bajt naglowka ELF i tablicy PH zastepowany wartosciami granicznymi;
// parser nie moze wyjsc poza bufor (ASan), wywolac UB (UBSan) ani zaakceptowac planu sprzecznego
// z niezaleznymi niezmiennikami.
size_t mutacje(const std::vector<uint8_t>& wzor, uint64_t zakres, const brb::PolitykaElf& pol, const char* nazwa) {
    static const uint8_t wartosci[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
    std::vector<uint8_t> b(wzor);
    size_t przebiegi = 0, przyjete = 0;
    brb::PlanLadowania plan;
    for (uint64_t off = 0; off < zakres && off < b.size(); ++off) {
        const uint8_t stary = b[off];
        for (uint8_t v : wartosci) {
            if (v == stary) continue;
            b[off] = v;
            const brb::WynikElf w = brb::zbuduj_plan_ladowania(b.data(), b.size(), pol, &plan);
            ++przebiegi;
            if (w.blad == BladElf::Brak) {
                ++przyjete;
                const std::string n = fixture::niezmienniki(plan, b.size(), pol);
                if (!n.empty()) std::printf("  mutacja %s off=%llu v=%u: naruszony niezmiennik: %s\n", nazwa,
                                            (unsigned long long)off, v, n.c_str());
                SPRAWDZ(n.empty());
            }
        }
        b[off] = stary;
    }
    // Obciecia pliku na kazdej dlugosci w obszarze naglowkow.
    for (uint64_t dl = 0; dl < zakres && dl < wzor.size(); ++dl) {
        std::vector<uint8_t> krotki(wzor.begin(), wzor.begin() + long(dl));
        const brb::WynikElf w = brb::zbuduj_plan_ladowania(krotki.empty() ? nullptr : krotki.data(), krotki.size(), pol, &plan);
        ++przebiegi;
        SPRAWDZ(w.blad != BladElf::Brak);
    }
    std::printf("[test] mutacje %s: przebiegi=%zu, zaakceptowane (z poprawnymi niezmiennikami)=%zu\n", nazwa, przebiegi, przyjete);
    return przebiegi;
}

std::vector<uint8_t> wczytaj(const char* sciezka) {
    std::vector<uint8_t> d;
    FILE* f = std::fopen(sciezka, "rb");
    if (!f) { std::fprintf(stderr, "Nie mozna otworzyc %s: %s\n", sciezka, std::strerror(errno)); return d; }
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && d.size() <= brb::ELF_MAKS_ROZMIAR_PLIKU) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    return d;
}

int test_jadra(const char* sciezka) {
    const std::vector<uint8_t> d = wczytaj(sciezka);
    if (d.empty()) return 2;
    brb::PlanLadowania p;
    const brb::WynikElf w = brb::zbuduj_plan_ladowania(d.data(), d.size(), brb::POLITYKA_JADRA_BURSZTYN, &p);
    std::printf("PROD plik=%s rozmiar=%zu wynik=%d opis=%s\n", sciezka, d.size(), int(w.blad), brb::opis_bledu_elf(w.blad));
    if (w.blad != BladElf::Brak) return 1;
    std::printf("PROD wejscie=0x%llx wejscie_fiz=0x%llx segmenty=%u\n", (unsigned long long)p.wejscie_wirtualne,
                (unsigned long long)p.wejscie_fizyczne, p.liczba_segmentow);
    for (uint32_t i = 0; i < p.liczba_segmentow; ++i) {
        const brb::SegmentPlanu& s = p.segmenty[i];
        std::printf("PROD seg ph=%u offset=0x%llx vaddr=0x%llx paddr=0x%llx filesz=0x%llx memsz=0x%llx align=0x%llx flagi=%c%c%c "
                    "zera=0x%llx strony=0x%llx-0x%llx liczba_stron=%llu\n",
                    s.indeks_ph, (unsigned long long)s.przesuniecie_pliku, (unsigned long long)s.adres_wirtualny,
                    (unsigned long long)s.adres_fizyczny, (unsigned long long)s.bajty_z_pliku,
                    (unsigned long long)s.rozmiar_w_pamieci, (unsigned long long)s.wyrownanie,
                    (s.prawa & brb::ELF_PF_R) ? 'R' : ' ', (s.prawa & brb::ELF_PF_W) ? 'W' : ' ',
                    (s.prawa & brb::ELF_PF_X) ? 'E' : ' ', (unsigned long long)s.bajty_zerowane,
                    (unsigned long long)s.strona_poczatek, (unsigned long long)s.strona_koniec,
                    (unsigned long long)s.liczba_stron);
    }
    std::printf("PROD zakres=0x%llx-0x%llx pamiec_stron=0x%llx z_pliku=0x%llx zera=0x%llx\n",
                (unsigned long long)p.zakres_poczatek, (unsigned long long)p.zakres_koniec,
                (unsigned long long)p.pamiec_stron, (unsigned long long)p.bajty_z_pliku, (unsigned long long)p.bajty_zerowane);
    std::printf("PROD mb2 obecny=%d offset=0x%llx dlugosc=%u architektura=%u tagi=%u segment=%d\n", int(p.mb2.obecny),
                (unsigned long long)p.mb2.przesuniecie_pliku, p.mb2.dlugosc, p.mb2.architektura, p.mb2.liczba_tagow,
                p.mb2.segment);
    SPRAWDZ(fixture::niezmienniki(p, d.size(), brb::POLITYKA_JADRA_BURSZTYN).empty());
    // Mutacje naglowka ELF, tablicy PH i naglowka MB2 prawdziwego jadra (obszar 0..0x1060).
    mutacje(d, 0x1060, brb::POLITYKA_JADRA_BURSZTYN, "jadro-produkcyjne");
    std::printf("PROD testy: sprawdzenia=%d bledy=%d\n", g_sprawdzenia, g_bledy);
    return g_bledy == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--jadro") == 0) return test_jadra(argv[2]);
    if (argc == 3 && std::strcmp(argv[1], "--zapisz-syntetyczne") == 0) {
        // Faza 4A (QEMU): male syntetyczne jadro 0x100000-0x104000 (fixture) z wyroznialnym wzorcem danych,
        // do natywnej proby kopiowania i weryfikacji w wolnej pamieci OVMF. Nigdy nie jest wykonywane.
        fixture::Obraz o = fixture::jadro_minimalne();
        for (int i = 0; i < 0x80; ++i) o.b[0x3000 + i] = uint8_t(0xB0 ^ i);
        FILE* f = std::fopen(argv[2], "wb");
        if (!f || std::fwrite(o.b.data(), 1, o.b.size(), f) != o.b.size()) return 1;
        return std::fclose(f) == 0 ? 0 : 1;
    }
    std::printf("BRB - testy hosta ELF64 (Faza 3)\n");
    test_poprawne();
    test_bledne();
    const Obraz o = fixture::jadro_minimalne();
    mutacje(o.b, 0x1060, brb::POLITYKA_JADRA_BURSZTYN, "syntetyczne-jadro");
    mutacje(o.b, 0x1060, brb::POLITYKA_OGOLNA, "syntetyczne-ogolna");
    std::printf("BRB testy ELF: sprawdzenia=%d bledy=%d\n", g_sprawdzenia, g_bledy);
    return g_bledy == 0 ? 0 : 1;
}
