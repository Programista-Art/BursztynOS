// Menedzer zadan - testy hosta (ASan/UBSan).
//
// Czesc A: czysta logika jadra uzywana 1:1 przez scheduler i BWS 103/104
//          (rozliczanie_cpu.h, pamiec_procesu.h, procesy_migawka.h, tabela praw).
//          To testy deterministyczne na kontrolowanych licznikach - NIE sa
//          dowodem dokladnosci pomiaru na sprzecie.
// Czesc B: model aplikacji (CPU z roznic migawek, sortowanie, wybor po tozsamosci,
//          przeplyw zakonczenia, historia).
// Czesc C: rejestracja (pulpit, menu Start, ikony, instalacja, Makefile) i ikona PNG.
// Czesc D: prawdziwe okno BursztynUI aplikacji z symulowanym jadrem (ta sama
//          polityka ochrony/generacji co BWS 103/104), rysowanie, zmiana rozmiaru,
//          przewijanie, dialog potwierdzenia, anulowanie, ponowne uzycie PID.
#include "bursztyn_ui_host_fixture.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <typeinfo>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "rozliczanie_cpu.h"
#include "pamiec_procesu.h"
#include "procesy_migawka.h"
#include "uruchamianie_polityka.h"
#include "scheduler.h"
#include "app_icons.h"
#include "start_menu_model.h"

static size_t g_alokacje = 0;
void* operator new(size_t n) { ++g_alokacje; void* p = malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n) { ++g_alokacje; void* p = malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

#define private public
#define _start menedzer_zadan_entry
#include "../programy/menedzer_zadan/menedzer_zadan.cpp"
#undef _start
#undef private

int bws_utworz_warstwe(int, int, int, int, int) { return 1; }   // petla AplikacjaGUI nie jest uruchamiana w tescie
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL test_menedzer_zadan_host.cpp:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)

using namespace MenedzerZadan;
namespace RC = RozliczanieCpu;

// =====================================================================================
// A1. Rozliczanie CPU
// =====================================================================================
static void rozliczanie_cpu() {
    // Stale stanow modelu = scheduler.h
    CHECK(ST_PUSTY == PROCES_PUSTY && ST_GOTOWY == PROCES_GOTOWY && ST_ZABLOKOWANY == PROCES_ZABLOKOWANY);
    CHECK(ST_MYSZ == PROCES_ZABLOKOWANY_MYSZ && ST_ZDARZENIE == PROCES_ZABLOKOWANY_ZDARZENIE);
    CHECK(ST_KLAWIATURA == PROCES_ZABLOKOWANY_KLAWIATURA && ST_KONCZACY == PROCES_KONCZACY);
    CHECK(RC::MAKS == MAKS_PROCESOW);

    RC::Stan s;
    // Przed wlaczeniem nic nie jest liczone.
    s.przelacz(100, 3); s.tick();
    CHECK(s.rozliczony_ns == 0 && s.cpu_ns[3] == 0);

    s = {};
    s.wlacz(RC::ZRODLO_HPET, 1000, 0);
    // PID 0 pracuje 200 ns, wchodzi w HLT na 800 ns, timer przelacza na PID 2 (CPU-bound) na 4000 ns.
    s.bezczynnosc_poczatek(1200);
    s.przelacz(2000, 2);                     // timer w trakcie HLT: odcinek HLT -> bezczynnosc
    CHECK(s.cpu_ns[0] == 200);
    CHECK(s.bezczynnosc_ns == 800);
    s.przelacz(6000, 0);
    CHECK(s.cpu_ns[2] == 4000);
    // Po powrocie do PID 0 (za HLT) koniec bezczynnosci liczy prace PID 0, nie bezczynnosc.
    s.bezczynnosc_koniec(6010);
    CHECK(s.cpu_ns[0] == 210 && s.bezczynnosc_ns == 800);
    // Suma kubelkow = czas od wlaczenia.
    CHECK(s.rozliczony_ns == 6010 - 1000);
    CHECK(s.cpu_ns[0] + s.cpu_ns[2] + s.bezczynnosc_ns == s.rozliczony_ns);

    // HLT tylko dla PID 0: proces Ring 3 nie moze "byc bezczynnoscia".
    s.przelacz(7000, 5);
    s.bezczynnosc_poczatek(7100);
    CHECK(!s.w_hlt);
    s.przelacz(8000, 0);
    CHECK(s.cpu_ns[5] == 1000);

    // Migawka dolicza biezacy odcinek bez modyfikacji stanu.
    const uint64_t przed = s.cpu_ns[0];
    RC::Migawka m = s.migawka(8500);
    CHECK(m.cpu_ns[0] == przed + 500);
    CHECK(s.cpu_ns[0] == przed);
    CHECK(m.rozliczony_ns == s.rozliczony_ns + 500);
    m = s.migawka(8500);
    CHECK(m.cpu_ns[0] == przed + 500);       // powtarzalna, nie zeruje licznikow
    // Migawka w trakcie HLT dolicza bezczynnosc.
    s.bezczynnosc_poczatek(9000);
    m = s.migawka(9400);
    CHECK(m.bezczynnosc_ns == s.bezczynnosc_ns + 400);

    // Zegar cofniety (blad zrodla): odcinek 0, brak przepelnienia.
    s.przelacz(8000, 1);
    CHECK(s.cpu_ns[1] == 0);
    CHECK(s.rozliczony_ns < (1ULL << 40));

    // Niepoprawny PID: odcinek idzie do PID 0, brak zapisu poza tablica.
    s.przelacz(9100, 99);
    CHECK(s.pid == 0);
    s.przelacz(9200, -4);
    CHECK(s.pid == 0);

    // Nowy proces w slocie: licznik od zera (konsument rozpozna po generacji).
    s.cpu_ns[7] = 12345;
    s.nowy_proces(7);
    CHECK(s.cpu_ns[7] == 0);
    s.nowy_proces(99); s.nowy_proces(-1);

    // Tryb tickow (bez HPET): kazdy tick to okres kubelka biezacego.
    RC::Stan t;
    t.wlacz(RC::ZRODLO_TICKI, 0, 0, 4000000);
    t.przelacz(0, 3); t.tick(); t.tick();
    t.przelacz(0, 0); t.bezczynnosc_poczatek(0); t.tick();
    t.bezczynnosc_koniec(0); t.tick();
    CHECK(t.cpu_ns[3] == 8000000 && t.bezczynnosc_ns == 4000000 && t.cpu_ns[0] == 4000000);
    CHECK(t.rozliczony_ns == 16000000);
    CHECK(t.migawka(123456789).rozliczony_ns == 16000000);   // tryb tickow nie dolicza wirtualnie

    // Proces CPU-bound przez caly przedzial ~100 %, uspiony ~0 %.
    RC::Stan c;
    c.wlacz(RC::ZRODLO_HPET, 0, 0);
    RC::Migawka a = c.migawka(0);
    uint64_t teraz = 0;
    for (int i = 0; i < 250; ++i) {           // 1 s przy 250 Hz: PID 4 zuzywa caly tick, PID 0 ~10 us
        c.przelacz(teraz += 10000, 4);
        c.przelacz(teraz += 3990000, 0);
    }
    RC::Migawka b = c.migawka(teraz);
    CHECK(promile(b.cpu_ns[4] - a.cpu_ns[4], b.rozliczony_ns - a.rozliczony_ns) == 998);
    CHECK(promile(b.cpu_ns[6] - a.cpu_ns[6], b.rozliczony_ns - a.rozliczony_ns) == 0);
}

// =====================================================================================
// A2. Pamiec procesu z tablic stron
// =====================================================================================
struct FakeTabele {
    std::vector<std::vector<uint64_t>> t;   // indeks tablicy = "adres fizyczny" >> 12
    uint64_t nowa() { t.emplace_back(512, 0); return uint64_t(t.size()) << 12; }
    uint64_t* tab(uint64_t fiz) { return t[(fiz >> 12) - 1].data(); }
    const uint64_t* z_wpisu(uint64_t e) {
        const uint64_t f = e & 0x000FFFFFFFFFF000ULL;
        if (!f || (f >> 12) > t.size()) return nullptr;
        return t[(f >> 12) - 1].data();
    }
};
static void pamiec_procesu() {
    using namespace PamiecProcesu;
    FakeTabele ft;
    const uint64_t pml4 = ft.nowa(), pdp = ft.nowa(), pd = ft.nowa(), pt1 = ft.nowa(), pt2 = ft.nowa();
    ft.tab(pml4)[0] = pdp | PTE_PRESENT | PTE_USER;
    ft.tab(pml4)[300] = ft.nowa() | PTE_PRESENT;      // polowa jadra: nigdy nie liczona
    ft.tab(pdp)[1] = pd | PTE_PRESENT | PTE_USER;
    ft.tab(pdp)[2] = 0x40000000ULL | PTE_PRESENT | PTE_USER | PDE_PS;   // duza strona: niespojnosc
    ft.tab(pdp)[3] = 0xC0000000ULL | PTE_PRESENT | PDE_PS;              // 1G supervisor (tozsamosc jadra): nie liczona
    ft.tab(pd)[3] = 0x00400000ULL | PTE_PRESENT | PDE_PS;               // 2M supervisor (tozsamosc jadra): nie liczona
    ft.tab(pd)[0] = pt1 | PTE_PRESENT | PTE_USER;
    ft.tab(pd)[1] = pt2 | PTE_PRESENT | PTE_USER;
    ft.tab(pd)[2] = 0x7777000ULL | PTE_PRESENT;       // tablica spoza "mapy bezposredniej"
    for (int i = 0; i < 10; ++i) ft.tab(pt1)[i] = (0x100000ULL + i * 4096) | PTE_PRESENT | PTE_USER;   // prywatne
    for (int i = 0; i < 3; ++i) ft.tab(pt2)[i] = (0x200000ULL + i * 4096) | PTE_PRESENT | PTE_USER | PTE_SHARED;  // SHM
    ft.tab(pt2)[5] = 0x300000ULL | PTE_PRESENT;       // supervisor: mapowanie jadra, nie procesu
    ft.tab(pt2)[6] = 0x301000ULL | PTE_USER;          // nieobecna
    auto tabela = [&](uint64_t e) -> const uint64_t* {
        const uint64_t f = e & 0x000FFFFFFFFFF000ULL;
        return f == 0x7777000ULL ? nullptr : ft.z_wpisu(e);
    };
    auto jadro = [](size_t i) { return i >= 256; };
    const Wynik w = policz(ft.tab(pml4), tabela, jadro);
    CHECK(w.prywatna_bajtow == 10 * 4096);
    CHECK(w.wspoldzielona_bajtow == 3 * 4096);
    CHECK(w.tablice_bajtow == 5 * 4096);              // PML4 + PDP + PD + 2xPT
    CHECK(w.duze_strony == 1);
    CHECK(w.pominiete_tablice == 1);
    CHECK(policz(nullptr, tabela, jadro).prywatna_bajtow == 0);

    // Wspoldzielona ramka zmapowana w DWOCH procesach: w zadnym nie jest pamiecia procesu (brak podwojnego liczenia).
    FakeTabele f2;
    const uint64_t a4 = f2.nowa(), a3 = f2.nowa(), a2 = f2.nowa(), a1 = f2.nowa();
    const uint64_t b4 = f2.nowa(), b3 = f2.nowa(), b2 = f2.nowa(), b1 = f2.nowa();
    f2.tab(a4)[1] = a3 | PTE_PRESENT | PTE_USER; f2.tab(a3)[0] = a2 | PTE_PRESENT | PTE_USER; f2.tab(a2)[0] = a1 | PTE_PRESENT | PTE_USER;
    f2.tab(b4)[1] = b3 | PTE_PRESENT | PTE_USER; f2.tab(b3)[0] = b2 | PTE_PRESENT | PTE_USER; f2.tab(b2)[0] = b1 | PTE_PRESENT | PTE_USER;
    f2.tab(a1)[0] = 0x900000ULL | PTE_PRESENT | PTE_USER | PTE_SHARED;
    f2.tab(b1)[0] = 0x900000ULL | PTE_PRESENT | PTE_USER | PTE_SHARED;
    f2.tab(a1)[1] = 0xA00000ULL | PTE_PRESENT | PTE_USER;
    auto t2 = [&](uint64_t e) { return f2.z_wpisu(e); };
    const Wynik wa = policz(f2.tab(a4), t2, jadro), wb = policz(f2.tab(b4), t2, jadro);
    CHECK(wa.prywatna_bajtow == 4096 && wb.prywatna_bajtow == 0);
    CHECK(wa.wspoldzielona_bajtow == 4096 && wb.wspoldzielona_bajtow == 4096);

    // Zwolnienie po zakonczeniu: liczona pamiec == to, co zwalnia regula zniszcz_hierarchie_procesu
    // (prywatne liscie USER + tablice); po wyczyszczeniu przestrzeni licznik wraca do samej PML4.
    uint64_t zwolnione = 0;
    std::function<void(uint64_t*, int)> zniszcz = [&](uint64_t* t, int poziom) {
        for (size_t i = 0; i < 512; ++i) {
            if (poziom == 4 && i >= 256) continue;
            const uint64_t e = t[i];
            if (!(e & PTE_PRESENT)) continue;
            if (poziom == 1) { if ((e & PTE_USER) && !(e & PTE_SHARED)) zwolnione += 4096; t[i] = 0; continue; }
            if ((poziom == 3 || poziom == 2) && (e & PDE_PS)) continue;
            const uint64_t* d = f2.z_wpisu(e); if (!d) continue;
            zniszcz(const_cast<uint64_t*>(d), poziom - 1); zwolnione += 4096; t[i] = 0;
        }
    };
    zniszcz(f2.tab(a4), 4); zwolnione += 4096;       // + strona PML4
    CHECK(zwolnione == wa.prywatna_bajtow + wa.tablice_bajtow);
    CHECK(policz(f2.tab(a4), t2, jadro).prywatna_bajtow == 0);
    CHECK(policz(f2.tab(a4), t2, jadro).tablice_bajtow == 4096);
}

// =====================================================================================
// A3. ABI i polityka BWS 103/104
// =====================================================================================
static void abi_i_polityka() {
    using namespace ProcesyMigawka;
    CHECK(BWS_PROCESY_MIGAWKA == 103 && BWS_PROCES_ZAKONCZ == 104 && BWS_PIERWSZY_WOLNY == 105);
    uint32_t n = 99;
    CHECK(!pojemnosc_bufora(0, &n));
    CHECK(!pojemnosc_bufora(sizeof(BwsProcesyNaglowek) - 1, &n));
    CHECK(!pojemnosc_bufora(1000, nullptr));
    CHECK(pojemnosc_bufora(sizeof(BwsProcesyNaglowek), &n) && n == 0);
    CHECK(pojemnosc_bufora(sizeof(BwsProcesyNaglowek) + sizeof(BwsProcesInfoV2) * 3 + 7, &n) && n == 3);
    CHECK(pojemnosc_bufora(UINT64_MAX, &n) && n == BWS_PROCESY_V2_MAKS);         // bez przepelnienia
    CHECK(bajtow_odpowiedzi(0) == sizeof(BwsProcesyNaglowek));
    CHECK(bajtow_odpowiedzi(16) == sizeof(BwsProcesyNaglowek) + 16 * sizeof(BwsProcesInfoV2));
    CHECK(bajtow_odpowiedzi(UINT32_MAX) == bajtow_odpowiedzi(16));
    CHECK(sizeof(BuforMigawki) == bajtow_odpowiedzi(16));

    // Kto moze pytac
    CHECK(wywolujacy_uprawniony(PRAWO_ZARZADZANIE_PROCESAMI | PRAWO_GUI, PRAWO_ZARZADZANIE_PROCESAMI, PZB_ZAUFANE, PZB_ZAUFANE));
    CHECK(!wywolujacy_uprawniony(PRAWO_GUI | PRAWO_SYSTEM_CONFIG, PRAWO_ZARZADZANIE_PROCESAMI, PZB_USLUGI, PZB_ZAUFANE));
    CHECK(!wywolujacy_uprawniony(PRAWO_ZARZADZANIE_PROCESAMI, PRAWO_ZARZADZANIE_PROCESAMI, PZB_UZYTKOWNIK, PZB_ZAUFANE));
    CHECK(!wywolujacy_uprawniony(PRAWO_ZARZADZANIE_PROCESAMI, PRAWO_ZARZADZANIE_PROCESAMI, PZB_PIASKOWNICA, PZB_ZAUFANE));
    CHECK(!wywolujacy_uprawniony(0, 0, PZB_JADRO, PZB_ZAUFANE));

    // Ochrona
    const int ja = 6; const uint8_t moj = PZB_ZAUFANE;
    auto och = [&](Cel c) { return ochrona(ja, moj, c, PZB_ZAUFANE); };
    CHECK(och({0, 1, PZB_JADRO, false}) == BWS_OCHRONA_JADRO);
    CHECK(och({6, 1, PZB_ZAUFANE, false}) == BWS_OCHRONA_WLASNY);
    CHECK(och({1, 4, PZB_ZAUFANE, true}) == BWS_OCHRONA_MENEDZER_OKIEN);
    CHECK(och({1, 4, PZB_PIASKOWNICA, true}) == BWS_OCHRONA_MENEDZER_OKIEN);   // sciezka wygrywa z poziomem
    CHECK(och({2, 1, PZB_USLUGI, false}) == BWS_OCHRONA_USLUGA);
    CHECK(och({2, 1, PZB_STEROWNIKI, false}) == BWS_OCHRONA_USLUGA);
    CHECK(och({3, 2, PZB_UZYTKOWNIK, false}) == BWS_OCHRONA_URUCHAMIANY);
    CHECK(och({3, 6, PZB_PIASKOWNICA, false}) == BWS_OCHRONA_KONCZACY);
    CHECK(och({3, 3, PZB_UZYTKOWNIK, false}) == BWS_OCHRONA_BRAK);
    CHECK(och({3, 1, PZB_ZAUFANE, false}) == BWS_OCHRONA_BRAK);      // np. Ustawienia
    CHECK(och({3, 1, PZB_PIASKOWNICA, false}) == BWS_OCHRONA_BRAK);
    CHECK(ochrona(6, PZB_UZYTKOWNIK, {3, 1, PZB_ZAUFANE, false}, PZB_ZAUFANE) == BWS_OCHRONA_WYZSZE_ZAUFANIE);

    // Decyzja BWS 104 (generacja = tozsamosc)
    auto dec = [&](bool upr, uint64_t pid, uint64_t gen, Cel c, uint64_t gs) {
        return decyzja_zakonczenia(upr, ja, moj, pid, gen, 16, c, gs, PZB_ZAUFANE);
    };
    const Cel notatnik{5, 3, PZB_UZYTKOWNIK, false};
    CHECK(dec(true, 5, 42, notatnik, 42) == BWS_ZAKONCZ_ZAAKCEPTOWANO);
    CHECK(dec(false, 5, 42, notatnik, 42) == BWS_ZAKONCZ_BRAK_UPRAWNIEN);
    CHECK(dec(true, 5, 41, notatnik, 42) == BWS_ZAKONCZ_BRAK_PROCESU);        // PID ponownie uzyty
    CHECK(dec(true, 5, 42, {5, 0, PZB_PIASKOWNICA, false}, 42) == BWS_ZAKONCZ_BRAK_PROCESU);   // slot pusty
    CHECK(dec(true, 5, 0, notatnik, 0) == BWS_ZAKONCZ_ZLE_ARGUMENTY);          // generacja 0
    CHECK(dec(true, 16, 42, notatnik, 42) == BWS_ZAKONCZ_ZLE_ARGUMENTY);
    CHECK(dec(true, UINT64_MAX, 42, notatnik, 42) == BWS_ZAKONCZ_ZLE_ARGUMENTY);
    CHECK(dec(true, 0, 1, {0, 1, PZB_JADRO, false}, 0) == BWS_ZAKONCZ_CHRONIONY);
    CHECK(dec(true, 6, 9, {6, 1, PZB_ZAUFANE, false}, 9) == BWS_ZAKONCZ_WLASNY);
    CHECK(dec(true, 1, 2, {1, 4, PZB_ZAUFANE, true}, 2) == BWS_ZAKONCZ_CHRONIONY);
    CHECK(dec(true, 2, 3, {2, 1, PZB_USLUGI, false}, 3) == BWS_ZAKONCZ_CHRONIONY);
    CHECK(dec(true, 5, 42, {5, 2, PZB_UZYTKOWNIK, false}, 42) == BWS_ZAKONCZ_URUCHAMIANY);
    CHECK(dec(true, 5, 42, {5, 6, PZB_PIASKOWNICA, false}, 42) == BWS_ZAKONCZ_JUZ_KONCZONY);
    CHECK(dec(true, 5, 42, {5, 99, PZB_UZYTKOWNIK, false}, 42) == BWS_ZAKONCZ_BRAK_PROCESU);

    // Prawo: znane, uprzywilejowane (zdejmowane przy dziecku PZB >= UZYTKOWNIK), nadawane tylko z tabeli jadra.
    CHECK((PRAWA_ZNANE & PRAWO_ZARZADZANIE_PROCESAMI) == PRAWO_ZARZADZANIE_PROCESAMI);
    CHECK((PRAWA_UPRZYWILEJOWANE & PRAWO_ZARZADZANIE_PROCESAMI) == PRAWO_ZARZADZANIE_PROCESAMI);
    const auto* tm = uruchamianie::znajdz_aplikacje_systemowa(AppIdentity::TaskManager);
    CHECK(tm && tm->poziom == PZB_ZAUFANE);
    CHECK(tm->prawa == (PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_ZARZADZANIE_PROCESAMI));
    CHECK(!(tm->prawa & (PRAWO_SYSTEM_CONFIG | PRAWO_URUCHOM_PROGRAM | PRAWO_PLIKI_ZAPISZ | PRAWO_SIEC | PRAWO_DEBUG | PRAWO_STEROWNIK)));
    int z_prawem = 0;
    for (const auto& a : uruchamianie::kAplikacjeSystemowe) z_prawem += (a.prawa & PRAWO_ZARZADZANIE_PROCESAMI) != 0;
    CHECK(z_prawem == 1);
    CHECK(!uruchamianie::znajdz_aplikacje_systemowa("/uzytkownicy/Pulpit/menedzer_zadan.cebula/menedzer_zadan.bur"));
    // Manifest nie zna nazwy tego prawa (loader.cpp) - zrodlo prawdy to tylko tabela jadra.
    std::ifstream loader("jadro/fs/loader.cpp");
    const std::string l((std::istreambuf_iterator<char>(loader)), {});
    CHECK(!l.empty() && l.find("zarzadzanie_procesami") == std::string::npos && l.find("ZARZADZANIE_PROCESAMI") == std::string::npos);
    // Struktura ABI bez wskaznikow jadra.
    CHECK(sizeof(BwsProcesInfoV2) == 168 && sizeof(BwsProcesyNaglowek) == 96);
}

// =====================================================================================
// Pomocnik: migawka zbudowana jak w jadrze (te same funkcje polityki)
// =====================================================================================
struct Proc {
    bool zywy = false; int stan = 1; uint8_t poziom = PZB_UZYTKOWNIK; uint64_t gen = 0;
    uint64_t cpu = 0, start = 0, priv = 0, tabl = 8192, okno = 0, wsp = 0; std::string sciezka;
};
struct Jadro {
    Proc p[16];
    uint64_t teraz = 1000000000ULL, rozliczony = 0, bezcz = 0, nast_gen = 100;
    int wywolujacy = 6; uint8_t poziom_wyw = PZB_ZAUFANE;
    bool hpet = true, ram = true, prawo = true;
    unsigned c103 = 0, c104 = 0; uint64_t ost_pid = 0, ost_gen = 0;
    uint64_t wymus103 = 0;                   // != 0: zwroc ten kod bledu
    int uruchom(int pid, const char* s, uint8_t poziom = PZB_UZYTKOWNIK, int stan = 3) {
        p[pid] = {}; p[pid].zywy = true; p[pid].stan = stan; p[pid].poziom = poziom; p[pid].gen = nast_gen++;
        p[pid].start = teraz; p[pid].sciezka = s; p[pid].priv = 1024 * 1024; return pid;
    }
    // Uplyw czasu: kazdy proces z mapy dostaje swoj czas, reszta to bezczynnosc.
    void uplyw(uint64_t ns, std::initializer_list<std::pair<int, uint64_t>> praca) {
        uint64_t suma = 0;
        for (auto& [pid, t] : praca) { p[pid].cpu += t; suma += t; }
        teraz += ns; rozliczony += ns; bezcz += ns - suma;
    }
    void zwolnij(int pid) { p[pid] = {}; }
    void migawka(BwsProcesyNaglowek& n, BwsProcesInfoV2* w, uint32_t pojemnosc) {
        n = {}; n.wersja = BWS_PROCESY_V2_ABI; n.rozmiar_naglowka = sizeof(n); n.rozmiar_wpisu = sizeof(*w);
        n.czas_ns = teraz; n.czas_rozliczony_ns = rozliczony; n.czas_bezczynnosci_ns = bezcz;
        if (hpet) n.flagi |= BWS_MIGAWKA_CPU_HPET;
        if (ram) { n.flagi |= BWS_MIGAWKA_RAM; n.ram_zarzadzana_bajtow = 256ULL << 20; n.ram_wolna_bajtow = 200ULL << 20; }
        n.ram_firmware_bajtow = 512ULL << 20;
        n.pid_wywolujacego = wywolujacy; n.generacja_wywolujacego = p[wywolujacy].gen;
        for (int pid = 0; pid < 16; ++pid) {
            if (pid && !p[pid].zywy) continue;
            ++n.liczba_procesow;
            if (n.liczba_wpisow >= pojemnosc) { n.flagi |= BWS_MIGAWKA_OBCIETA; continue; }
            BwsProcesInfoV2& e = w[n.liczba_wpisow++]; e = {};
            const Proc& q = p[pid];
            e.pid = pid; e.stan = pid ? q.stan : 1; e.generacja = pid ? q.gen : 0; e.poziom_pzb = pid ? q.poziom : 0;
            e.czas_cpu_ns = q.cpu; e.czas_startu_ns = q.start;
            std::snprintf(e.sciezka, sizeof(e.sciezka), "%s", q.sciezka.c_str());
            const bool wm = AppIdentity::equal(e.sciezka, "/menedzer_okien.bur");
            if (pid == wywolujacy) e.flagi |= BWS_PROC_WYWOLUJACY;
            if (pid == 0) e.flagi |= BWS_PROC_JADRO;
            if (wm) e.flagi |= BWS_PROC_MENEDZER_OKIEN;
            e.ochrona = ProcesyMigawka::ochrona(wywolujacy, poziom_wyw, {pid, (int)e.stan, (uint8_t)e.poziom_pzb, wm}, PZB_ZAUFANE);
            if (e.ochrona == BWS_OCHRONA_BRAK) e.flagi |= BWS_PROC_MOZNA_ZAKONCZYC;
            if (pid && q.stan != 2) {
                e.flagi |= BWS_PROC_PAMIEC_ZMIERZONA; e.pamiec_prywatna_bajtow = q.priv; e.pamiec_tablic_stron_bajtow = q.tabl;
                e.pamiec_wspoldzielona_bajtow = q.wsp; e.stos_jadra_bajtow = 65536;
            }
            if (pid) e.bufor_okna_bajtow = q.okno;
        }
    }
    uint64_t bws(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t) {
        if (nr == BWS_PROCESY_MIGAWKA) {
            ++c103;
            if (wymus103) return wymus103;
            if (!prawo) return BWS_MIGAWKA_BRAK_UPRAWNIEN;
            if (a3 != BWS_PROCESY_V2_ABI) return BWS_MIGAWKA_ZLA_WERSJA;
            uint32_t poj = 0;
            if (!a1 || !ProcesyMigawka::pojemnosc_bufora(a2, &poj)) return BWS_MIGAWKA_ZLY_BUFOR;
            auto* n = reinterpret_cast<BwsProcesyNaglowek*>(a1);
            migawka(*n, reinterpret_cast<BwsProcesInfoV2*>(n + 1), poj);
            return BWS_MIGAWKA_OK;
        }
        if (nr == BWS_PROCES_ZAKONCZ) {
            ++c104; ost_pid = a1; ost_gen = a2;
            ProcesyMigawka::Cel c{int(a1 < 16 ? a1 : 0), 0, PZB_PIASKOWNICA, false};
            uint64_t gs = 0;
            if (a1 < 16) { const Proc& q = p[a1]; c.stan = a1 == 0 ? 1 : (q.zywy ? q.stan : 0); c.poziom = q.poziom;
                           c.menedzer_okien = q.sciezka == "/menedzer_okien.bur"; gs = a1 ? q.gen : 0; }
            uint64_t w = ProcesyMigawka::decyzja_zakonczenia(prawo, wywolujacy, poziom_wyw, a1, a2, 16, c, gs, PZB_ZAUFANE);
            if (w == BWS_ZAKONCZ_ZAAKCEPTOWANO) { p[a1].stan = 6; p[a1].poziom = PZB_PIASKOWNICA; }
            return w;
        }
        return 0;
    }
};
static Jadro* g_jadro = nullptr;
static uint64_t bws_symulowane(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) { return g_jadro->bws(nr, a1, a2, a3, a4); }
static uint64_t g_zegar = 5000000000ULL;
static uint64_t zegar_testowy() { return g_zegar; }

// =====================================================================================
// B. Model aplikacji
// =====================================================================================
static void zastosuj(Model& m, Jadro& j, uint32_t poj = 16) {
    BuforMigawki b{}; j.migawka(b.naglowek, b.wpisy, poj);
    CHECK(m.zastosuj(b.naglowek, b.wpisy, g_zegar));
}
static const MenedzerZadan::Wiersz* po_pid(Model& m, uint32_t pid) {
    for (uint32_t i = 0; i < m.liczba(); ++i) if (m.wiersz(i)->pid == pid) return m.wiersz(i);
    return nullptr;
}
static void model_cases() {
    // Walidacja odpowiedzi
    {
        Model m; Jadro j; j.uruchom(1, "/menedzer_okien.bur", PZB_ZAUFANE); j.uruchom(6, AppIdentity::TaskManager, PZB_ZAUFANE, 1);
        BuforMigawki b{}; j.migawka(b.naglowek, b.wpisy, 16);
        BwsProcesyNaglowek n = b.naglowek;
        n.wersja = 1; CHECK(!m.zastosuj(n, b.wpisy)); n = b.naglowek;
        n.rozmiar_wpisu = 160; CHECK(!m.zastosuj(n, b.wpisy)); n = b.naglowek;
        n.rozmiar_naglowka = 0; CHECK(!m.zastosuj(n, b.wpisy)); n = b.naglowek;
        n.liczba_wpisow = 17; n.liczba_procesow = 17; CHECK(!m.zastosuj(n, b.wpisy)); n = b.naglowek;
        n.liczba_procesow = n.liczba_wpisow - 1; CHECK(!m.zastosuj(n, b.wpisy)); n = b.naglowek;
        CHECK(!m.zastosuj(n, nullptr));
        BuforMigawki zle = b; zle.wpisy[1].pid = 0; CHECK(!m.zastosuj(zle.naglowek, zle.wpisy));     // duplikat PID
        zle = b; zle.wpisy[1].stan = 7; CHECK(!m.zastosuj(zle.naglowek, zle.wpisy));
        zle = b; zle.wpisy[1].stan = 0; CHECK(!m.zastosuj(zle.naglowek, zle.wpisy));
        zle = b; zle.wpisy[1].pid = 16; CHECK(!m.zastosuj(zle.naglowek, zle.wpisy));
        zle = b; std::memset(zle.wpisy[1].sciezka, 'A', sizeof(zle.wpisy[1].sciezka));             // brak NUL
        CHECK(m.zastosuj(zle.naglowek, zle.wpisy)); CHECK(std::strlen(po_pid(m, 1)->sciezka) == 63);
        CHECK(m.liczba() == 3);
    }
    // Zero, jeden, wiele procesow
    {
        Model m; BwsProcesyNaglowek n{}; n.wersja = 2; n.rozmiar_naglowka = sizeof(n); n.rozmiar_wpisu = sizeof(BwsProcesInfoV2);
        CHECK(m.zastosuj(n, nullptr)); CHECK(m.liczba() == 0); CHECK(m.indeks_wyboru() == -1);
        CHECK(m.zadaj_zakonczenia() == Prosba::BrakWyboru);
        Jadro j; zastosuj(m, j); CHECK(m.liczba() == 1); CHECK(!std::strcmp(m.wiersz(0)->nazwa, "Jądro systemu"));
        for (int pid = 1; pid < 16; ++pid) j.uruchom(pid, "/programy/notatnik.cebula/notatnik.bur");
        zastosuj(m, j); CHECK(m.liczba() == 16);
        Model obc; zastosuj(obc, j, 4); CHECK(obc.liczba() == 4 && obc.system().obcieta && obc.system().procesow == 16);
    }
    // CPU w przedziale, nie z licznika calego zycia
    {
        Model m; Jadro j; j.uruchom(1, "/menedzer_okien.bur", PZB_ZAUFANE); j.uruchom(5, "/programy/kalkulator.cebula/kalkulator.bur");
        j.uruchom(6, AppIdentity::TaskManager, PZB_ZAUFANE, 1);
        j.p[5].cpu = 50ULL * 1000000000ULL;   // dlugo liczyl wczesniej
        zastosuj(m, j);
        CHECK(po_pid(m, 5)->cpu_promile == NIEDOSTEPNE); CHECK(!m.system().cpu_dostepne);   // pierwsza probka
        j.uplyw(1000000000ULL, {{5, 1000000000ULL}});        // caly przedzial: 100 %
        zastosuj(m, j);
        CHECK(po_pid(m, 5)->cpu_promile == 1000); CHECK(po_pid(m, 1)->cpu_promile == 0);
        CHECK(m.system().cpu_promile == 1000 && m.system().bezczynnosc_promile == 0);
        j.uplyw(1000000000ULL, {{5, 250000000ULL}, {0, 50000000ULL}, {1, 100000000ULL}});
        zastosuj(m, j);
        CHECK(po_pid(m, 5)->cpu_promile == 250); CHECK(po_pid(m, 1)->cpu_promile == 100);
        CHECK(po_pid(m, 0)->cpu_promile == 50 && m.system().jadro_promile == 50);
        CHECK(m.system().cpu_promile == 400 && m.system().bezczynnosc_promile == 600);
        j.uplyw(1000000000ULL, {});                          // uspione/zablokowane: 0
        zastosuj(m, j);
        CHECK(po_pid(m, 5)->cpu_promile == 0 && m.system().cpu_promile == 0);
        // Brak uplywu czasu (dzielenie przez zero) i cofniety licznik: Niedostepne, nie wymyslona wartosc.
        zastosuj(m, j);
        CHECK(po_pid(m, 5)->cpu_promile == NIEDOSTEPNE && !m.system().cpu_dostepne);
        j.uplyw(1000000000ULL, {{5, 10}}); zastosuj(m, j);
        j.p[5].cpu -= 1000; j.uplyw(1000000000ULL, {}); zastosuj(m, j);
        CHECK(po_pid(m, 5)->cpu_promile == NIEDOSTEPNE);
        j.rozliczony -= 5; zastosuj(m, j);                   // zegar rozliczania sie cofnal
        CHECK(!m.system().cpu_dostepne);
        // Krotko zyjacy proces utworzony w przedziale: czas od startu.
        j.uplyw(1, {}); zastosuj(m, j);
        j.uruchom(7, "/programy/snake.cebula/snake.bur");
        j.uplyw(1000000000ULL, {{7, 300000000ULL}}); zastosuj(m, j);
        CHECK(po_pid(m, 7)->cpu_promile == 300);
        // Ponowne uzycie PID: nowa generacja nie dziedziczy roznicy po starym procesie.
        j.p[7].cpu = 900000000ULL; zastosuj(m, j);
        j.zwolnij(7); j.uruchom(7, "/programy/notatnik.cebula/notatnik.bur");
        j.p[7].start = 1;                                     // start sprzed przedzialu (nie dopasowany)
        j.uplyw(1000000000ULL, {{7, 100000000ULL}}); zastosuj(m, j);
        CHECK(po_pid(m, 7)->cpu_promile == NIEDOSTEPNE);
        j.uplyw(1000000000ULL, {{7, 200000000ULL}}); zastosuj(m, j);
        CHECK(po_pid(m, 7)->cpu_promile == 200);
        // Proces konczy sie miedzy probkami: znika z listy (zadnych starych rekordow).
        j.zwolnij(7); j.uplyw(1000, {}); zastosuj(m, j);
        CHECK(!po_pid(m, 7));
        // Blad odczytu zrywa przedzial (nastepna probka nie liczy CPU z dziury).
        m.blad_odczytu(); j.uplyw(1000000000ULL, {{5, 1000000000ULL}}); zastosuj(m, j);
        CHECK(po_pid(m, 5)->cpu_promile == NIEDOSTEPNE);
        // Bez zrodla czasu: CPU niedostepne.
        j.hpet = false; j.uplyw(1000000000ULL, {}); zastosuj(m, j); zastosuj(m, j);
        CHECK(!m.system().cpu_dostepne && m.system().cpu_promile == NIEDOSTEPNE);
    }
    // RAM: uzywana/wolna z PMM, pamiec procesu bez SHM, reszta = jadro i bufory
    {
        Model m; Jadro j; j.uruchom(3, "/programy/notatnik.cebula/notatnik.bur"); j.p[3].priv = 3 << 20; j.p[3].wsp = 8 << 20; j.p[3].okno = 1 << 20;
        j.uruchom(4, "/programy/kalkulator.cebula/kalkulator.bur", PZB_UZYTKOWNIK, 2);      // w budowie: bez pomiaru
        zastosuj(m, j);
        const MenedzerZadan::Wiersz* n = po_pid(m, 3);
        CHECK(n->pamiec() == (3u << 20) + 8192 + 65536 + (1u << 20));
        CHECK(!po_pid(m, 4)->pamiec_zmierzona() && po_pid(m, 4)->pamiec() == 0);
        const System& s = m.system();
        CHECK(s.ram_dostepna && s.ram_uzywana == (56ULL << 20) && s.ram_wolna == (200ULL << 20));
        CHECK(s.ram_procesow == n->pamiec()); CHECK(s.ram_reszta == s.ram_uzywana - s.ram_procesow);
        CHECK(s.ram_promile == 219);
        j.ram = false; zastosuj(m, j); CHECK(!m.system().ram_dostepna && m.system().ram_promile == NIEDOSTEPNE);
        // Zwolnienie pamieci po zakonczeniu procesu: znika z sumy przypisanej procesom.
        j.ram = true; j.zwolnij(3); zastosuj(m, j); CHECK(m.system().ram_procesow == 0);
    }
    // Sortowanie i wybor po tozsamosci
    {
        Model m; Jadro j;
        j.uruchom(2, "/programy/notatnik.cebula/notatnik.bur"); j.p[2].priv = 5 << 20;
        j.uruchom(3, "/programy/kalkulator.cebula/kalkulator.bur"); j.p[3].priv = 1 << 20;
        j.uruchom(4, "/programy/ekran.cebula/ekran.bur", PZB_ZAUFANE); j.p[4].priv = 9 << 20;
        j.uruchom(5, "/uzytkownicy/Pulpit/żółw.cebula/żółw.bur"); j.p[5].priv = 2 << 20;
        zastosuj(m, j); j.uplyw(1000000000ULL, {{2, 10}, {3, 300000000}, {4, 200000000}, {5, 100}}); zastosuj(m, j);
        auto kolejnosc = [&]() { std::string s; for (uint32_t i = 0; i < m.liczba(); ++i) s += char('0' + m.wiersz(i)->pid); return s; };
        CHECK(kolejnosc() == "03245");                        // domyslnie: Nazwa rosnaco (Jądro, Kalkulator, Notatnik, Ustawienia ekranu, żółw)
        CHECK(!std::strcmp(po_pid(m, 5)->nazwa, "żółw"));
        m.ustaw_sortowanie(MenedzerZadan::Kolumna::Pid, true); CHECK(kolejnosc() == "02345");
        m.ustaw_sortowanie(MenedzerZadan::Kolumna::Pid, false); CHECK(kolejnosc() == "54320");
        m.ustaw_sortowanie(MenedzerZadan::Kolumna::Procesor, false); CHECK(kolejnosc() == "34520");
        m.ustaw_sortowanie(MenedzerZadan::Kolumna::Pamiec, false); CHECK(kolejnosc() == "42530");
        m.ustaw_sortowanie(MenedzerZadan::Kolumna::Pamiec, true); CHECK(kolejnosc() == "03524");
        m.ustaw_sortowanie(MenedzerZadan::Kolumna::Nazwa, false); CHECK(kolejnosc() == "54230");
        m.ustaw_sortowanie(MenedzerZadan::Kolumna::Stan, true); CHECK(kolejnosc() == "02345");   // rowne stany: PID
        CHECK(porownaj_nazwy("Łódź", "lody") > 0 && porownaj_nazwy("ząb", "żaba") < 0 && porownaj_nazwy("Abc", "abc") == 0);
        // Wybor: ten sam proces po sortowaniu i odswiezeniu
        m.wybierz_indeks(static_cast<uint32_t>(m.indeks({3, j.p[3].gen})));
        const Tozsamosc kalk{3, j.p[3].gen};
        for (MenedzerZadan::Kolumna k : {MenedzerZadan::Kolumna::Nazwa, MenedzerZadan::Kolumna::Pid, MenedzerZadan::Kolumna::Procesor, MenedzerZadan::Kolumna::Pamiec}) {
            m.ustaw_sortowanie(k, false);
            CHECK(m.wybor() == kalk && m.wiersz(m.indeks_wyboru())->pid == 3);
        }
        j.uruchom(6, "/programy/snake.cebula/snake.bur"); zastosuj(m, j);   // nowy proces w trakcie
        CHECK(m.wybrany() && m.wybrany()->id() == kalk);
        // PID ponownie uzyty: wybor NIE przechodzi na nowy proces o tym samym PID
        j.zwolnij(3); j.uruchom(3, "/programy/notatnik.cebula/notatnik.bur"); zastosuj(m, j);
        CHECK(!m.wybrany() && m.indeks_wyboru() == -1 && m.wybor_zniknal() && !m.wybor_zniknal());
    }
    // Przeplyw zakonczenia
    {
        Model m; Jadro j; g_jadro = &j;
        j.uruchom(1, "/menedzer_okien.bur", PZB_ZAUFANE); j.uruchom(6, AppIdentity::TaskManager, PZB_ZAUFANE, 1);
        j.uruchom(2, "/programy/aktualizacje.cebula/aktualizacje.bur", PZB_USLUGI);
        j.uruchom(5, "/programy/notatnik.cebula/notatnik.bur");
        zastosuj(m, j);
        auto wybierz = [&](uint32_t pid) { m.wybierz_indeks(static_cast<uint32_t>(m.indeks({pid, pid ? j.p[pid].gen : 0}))); };
        unsigned wywolania = 0; uint64_t pid_w = 0, gen_w = 0;
        auto zakoncz = [&](uint32_t pid, uint64_t gen) { ++wywolania; pid_w = pid; gen_w = gen; return j.bws(BWS_PROCES_ZAKONCZ, pid, gen, 0, 0); };
        wybierz(0); CHECK(m.zadaj_zakonczenia() == Prosba::Chroniony);
        wybierz(1); CHECK(m.zadaj_zakonczenia() == Prosba::Chroniony);
        wybierz(2); CHECK(m.zadaj_zakonczenia() == Prosba::Chroniony);
        wybierz(6); CHECK(m.zadaj_zakonczenia() == Prosba::ZamknijSiebie);
        CHECK(m.etap() == Etap::Brak);
        CHECK(m.potwierdz(zakoncz, 1) == BWS_ZAKONCZ_ZLE_ARGUMENTY && wywolania == 0);   // bez potwierdzenia nic
        wybierz(5); CHECK(m.zadaj_zakonczenia() == Prosba::Potwierdz);
        CHECK(m.etap() == Etap::Potwierdzanie && !std::strcmp(m.cel_nazwa(), "Notatnik"));
        m.anuluj(); CHECK(m.etap() == Etap::Brak && wywolania == 0 && j.p[5].stan == 3);
        // Potwierdzenie uzywa zapamietanej tozsamosci, nawet gdy wybor sie zmienil.
        CHECK(m.zadaj_zakonczenia() == Prosba::Potwierdz);
        wybierz(1);
        CHECK(m.potwierdz(zakoncz, g_zegar) == BWS_ZAKONCZ_ZAAKCEPTOWANO);
        CHECK(wywolania == 1 && pid_w == 5 && gen_w == j.p[5].gen && j.p[1].stan == 3);
        CHECK(m.etap() == Etap::Oczekiwanie);
        // Zaakceptowane != zakonczone: proces nadal jest (KONCZACY), dopoki jadro nie zwolni slotu.
        zastosuj(m, j); CHECK(m.zdarzenie() == Zdarzenie::NadalKonczony);
        CHECK(!std::strcmp(nazwa_stanu(po_pid(m, 5)->stan, false), "Kończący"));
        CHECK(po_pid(m, 5)->ochrona == BWS_OCHRONA_KONCZACY && !po_pid(m, 5)->mozna_zakonczyc());
        j.zwolnij(5); zastosuj(m, j); CHECK(m.zdarzenie() == Zdarzenie::Zakonczony); CHECK(m.etap() == Etap::Brak);
        CHECK(m.zdarzenie() == Zdarzenie::Nic);
        // PID ponownie uzyty miedzy potwierdzeniem a wywolaniem: jadro odrzuca (BRAK_PROCESU), nowy proces zyje.
        j.uruchom(5, "/programy/kalkulator.cebula/kalkulator.bur"); zastosuj(m, j); wybierz(5);
        CHECK(m.zadaj_zakonczenia() == Prosba::Potwierdz);
        j.zwolnij(5); j.uruchom(5, "/programy/snake.cebula/snake.bur");
        CHECK(m.potwierdz(zakoncz, g_zegar) == BWS_ZAKONCZ_BRAK_PROCESU);
        CHECK(j.p[5].zywy && j.p[5].stan == 3 && m.etap() == Etap::Brak);
        // Przekroczony czas oczekiwania na sprzatanie.
        zastosuj(m, j); wybierz(5); CHECK(m.zadaj_zakonczenia() == Prosba::Potwierdz);
        CHECK(m.potwierdz(zakoncz, 1000) == BWS_ZAKONCZ_ZAAKCEPTOWANO);
        BuforMigawki b{}; j.migawka(b.naglowek, b.wpisy, 16);
        CHECK(m.zastosuj(b.naglowek, b.wpisy, 1000 + 11000000000ULL)); CHECK(m.zdarzenie() == Zdarzenie::PrzekroczonyCzas);
        // Odmowa uprawnien z jadra.
        j.zwolnij(5); j.uruchom(5, "/programy/snake.cebula/snake.bur"); zastosuj(m, j); wybierz(5);
        CHECK(m.zadaj_zakonczenia() == Prosba::Potwierdz); j.prawo = false;
        CHECK(m.potwierdz(zakoncz, 1) == BWS_ZAKONCZ_BRAK_UPRAWNIEN && j.p[5].stan == 3);
    }
    // Historia (bufor kolowy, ograniczony)
    {
        Model m; Jadro j; zastosuj(m, j);
        int cpu[HISTORIA], ram[HISTORIA];
        CHECK(m.historia(cpu, ram, HISTORIA) == 1);           // sama RAM w pierwszej probce
        for (int i = 0; i < 100; ++i) { j.uplyw(1000000000ULL, {{0, uint64_t(i) * 1000000ULL}}); zastosuj(m, j); }
        CHECK(m.historia(cpu, ram, HISTORIA) == HISTORIA);
        CHECK(cpu[HISTORIA - 1] == 99 && cpu[0] == 40);       // najnowsza na koncu, najstarsza z ostatnich 60
        CHECK(m.historia(cpu, nullptr, 5) == 5 && cpu[4] == 99 && cpu[0] == 95);
    }
}

// =====================================================================================
// Formatowanie i teksty
// =====================================================================================
static void teksty() {
    char b[48];
    format_pamieci(0, b, sizeof(b)); CHECK(!std::strcmp(b, "0 KiB"));
    format_pamieci(1536, b, sizeof(b)); CHECK(!std::strcmp(b, "2 KiB"));
    format_pamieci(1048576, b, sizeof(b)); CHECK(!std::strcmp(b, "1,0 MiB"));
    format_pamieci(1572864, b, sizeof(b)); CHECK(!std::strcmp(b, "1,5 MiB"));
    format_pamieci(3ULL << 30, b, sizeof(b)); CHECK(!std::strcmp(b, "3,0 GiB"));
    format_procentu(NIEDOSTEPNE, b, sizeof(b)); CHECK(!std::strcmp(b, "Niedostępne"));
    format_procentu(123, b, sizeof(b)); CHECK(!std::strcmp(b, "12,3 %"));
    format_procentu(1000, b, sizeof(b)); CHECK(!std::strcmp(b, "100,0 %"));
    format_czasu(59ULL * 1000000000ULL, b, sizeof(b)); CHECK(!std::strcmp(b, "59 s"));
    format_czasu(125ULL * 1000000000ULL, b, sizeof(b)); CHECK(!std::strcmp(b, "2 min 05 s"));
    format_czasu(3725ULL * 1000000000ULL, b, sizeof(b)); CHECK(!std::strcmp(b, "1 h 02 min"));
    char male[4]; format_pamieci(123456789, male, sizeof(male)); CHECK(std::strlen(male) == 3);
    CHECK(promile(0, 0) == NIEDOSTEPNE && promile(5, 3) == 1000 && promile(1, 3) == 333);
    CHECK(promile(UINT64_MAX / 2, UINT64_MAX) == 500);        // bez przepelnienia
    // Polskie nazwy stanow tylko dla stanow schedulera
    CHECK(!std::strcmp(nazwa_stanu(1, true), "Uruchomiony") && !std::strcmp(nazwa_stanu(1, false), "Gotowy"));
    CHECK(!std::strcmp(nazwa_stanu(2, false), "Uruchamianie") && !std::strcmp(nazwa_stanu(6, false), "Kończący"));
    CHECK(!std::strcmp(nazwa_stanu(3, false), "Oczekujący (mysz)") && !std::strcmp(nazwa_stanu(99, false), "Nieznany"));
    // Nazwy z rejestru (ta sama tozsamosc co ikony), potem systemowe, potem plik
    nazwa_procesu(5, "/programy/notatnik.cebula/notatnik.bur", b, sizeof(b)); CHECK(!std::strcmp(b, "Notatnik"));
    nazwa_procesu(1, "/menedzer_okien.bur", b, sizeof(b)); CHECK(!std::strcmp(b, "Menedżer okien (pulpit)"));
    nazwa_procesu(7, "/uzytkownicy/x.cebula/gra.bur", b, sizeof(b)); CHECK(!std::strcmp(b, "gra"));
    nazwa_procesu(7, "", b, sizeof(b)); CHECK(!std::strcmp(b, "(bez nazwy)"));
    nazwa_procesu(7, "/a/", b, sizeof(b)); CHECK(!std::strcmp(b, "(bez nazwy)"));
    nazwa_procesu(0, "", b, sizeof(b)); CHECK(!std::strcmp(b, "Jądro systemu"));
    nazwa_procesu(8, AppIdentity::TaskManager, b, sizeof(b)); CHECK(!std::strcmp(b, "Menedżer zadań"));
    for (uint32_t o = 0; o <= 8; ++o) CHECK(powod_ochrony(o) != nullptr);
    {   // Tekst: obciecie nie przecina znaku UTF-8
        char b[6]; Tekst x(b, sizeof(b)); x.dodaj("abcą");  CHECK(!std::strcmp(b, "abcą"));    // 5 B + NUL: miesci sie
        Tekst y(b, sizeof(b)); y.dodaj("abcdą");  CHECK(!std::strcmp(b, "abcd"));                // ą nie zmiesciloby sie w calosci
        Tekst z(b, sizeof(b)); z.dodaj("ab€x");  CHECK(!std::strcmp(b, "ab€"));                   // € = 3 B
        Tekst q(b, sizeof(b)); q.dodaj("abc€");  CHECK(!std::strcmp(b, "abc"));
    }
    for (uint32_t o = 0; o <= 8; ++o) {                // pasek stanu: krotki tekst, miesci sie obok przyciskow
        size_t n = 0; while (krotki_powod_ochrony(o)[n]) ++n;
        CHECK(n <= 36);
    }
    for (uint64_t w = 0; w <= 9; ++w) CHECK(std::strlen(opis_wyniku_zakonczenia(w)) > 10);
    CHECK(std::strstr(opis_wyniku_zakonczenia(BWS_ZAKONCZ_ZAAKCEPTOWANO), "przyjęło"));
}

// =====================================================================================
// C. Rejestracja: pulpit, menu Start, pasek zadan (ikony), instalacja, budowanie
// =====================================================================================
static std::string plik(const char* sciezka) {
    std::ifstream f(sciezka, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}
static void rejestracja() {
    using namespace DesktopEntries;
    // Skrot dopisany NA KONCU: indeksy i id wczesniejszych skrotow bez zmian (zapisane pozycje zostaja).
    static const char* const poprzednie[] = {"notatnik", "kalkulator", "husarz", "testy-systemu", "snake", "eksplorator",
        "obrazy", "demo-ui", "kontrolki2", "kontrolki3", "aktualizacje", "ustawienia", "kontrolki4", "test-dzwieku",
        "diagnostyka-systemu", "ustawienia-ekranu"};
    CHECK(Count == 17);
    for (int i = 0; i < 16; ++i) CHECK(!std::strcmp(Launchers[i].id, poprzednie[i]));
    const Launcher& l = Launchers[16];
    CHECK(!std::strcmp(l.name, "Menedżer zadań") && !std::strcmp(l.title, "Menedżer zadań"));
    CHECK(!std::strcmp(l.id, "menedzer-zadan") && l.icon == TypIkonyAplikacji::MenedzerZadan);
    CHECK(!std::strcmp(l.path, AppIdentity::TaskManager));
    CHECK(!std::strcmp(l.icon_png, "/ikonki/app_menedzer_zadan.png"));
    CHECK(icon_png_for(TypIkonyAplikacji::MenedzerZadan) == l.icon_png);
    for (int i = 0; i < 16; ++i) CHECK(std::strcmp(Launchers[i].id, l.id) && std::strcmp(Launchers[i].path, l.path));
    // Ta sama ikona: pulpit, pasek zadan (rejestr ikon po sciezce programu) i menu Start.
    CHECK(!std::strcmp(AppIcons::icon_for_executable(AppIdentity::TaskManager), l.icon_png));
    int start = -1;
    for (int i = 0; i < StartMenu::Count; ++i) if (!std::strcmp(StartMenu::apps[i].name, "Menedżer zadań")) start = i;
    CHECK(start >= 0 && StartMenu::apps[start].action == 22 && !std::strcmp(StartMenu::apps[start].icon_png, l.icon_png));
    for (int i = 0; i < StartMenu::Count; ++i) if (i != start) CHECK(StartMenu::apps[i].action != 22);
    StartMenu::Model sm; sm.filter("proce"); CHECK(sm.count >= 1 && sm.action() == 22);
    sm.filter("menedżer z"); CHECK(sm.count == 1 && sm.action() == 22);
    // WM uruchamia te sama tozsamosc (akcja 22) - bez osobnego mechanizmu startu.
    const std::string wm = plik("programy/menedzer_okien/menedzer_okien.cpp");
    const size_t c22 = wm.find("case 22:");
    CHECK(c22 != std::string::npos && wm.find("uruchom_lub_aktywuj(AppIdentity::TaskManager", c22) < c22 + 120);
    CHECK(wm.find("case TypIkonyAplikacji::MenedzerZadan:") != std::string::npos);
    // Instalacja (Live i system zainstalowany uzywaja tej samej funkcji), katalog paczki, ikona, suma SHA-256.
    const std::string k = plik("jadro/rdzen/kernel.cpp");
    CHECK(k.find("\"/programy/menedzer_zadan.cebula/opis.aplikacji\",MANIFEST_MENEDZER_ZADAN") != std::string::npos);
    CHECK(k.find("{\"/ikonki/app_menedzer_zadan.png\",_binary_ikonki_app_menedzer_zadan_png_start") != std::string::npos);
    CHECK(plik("jadro/fs/psf_bootstrap.h").find("\"/programy/menedzer_zadan.cebula\",") != std::string::npos);
    const std::string mk = plik("Makefile");
    CHECK(mk.find("/programy/menedzer_zadan.cebula/menedzer_zadan.bur=menedzer_zadan.bin") != std::string::npos);
    CHECK(mk.find("\tmenedzer_zadan_blob.o \\") != std::string::npos && mk.find("\tikona_app_menedzer_zadan_blob.o \\") != std::string::npos);
    // Ikona: oryginalny PNG 128x128 RGBA (jak pozostale ikony aplikacji).
    const std::string png = plik("ikonki/app_menedzer_zadan.png");
    CHECK(png.size() > 1000 && png.size() < 256 * 1024 && png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
    CHECK(png.compare(12, 4, "IHDR") == 0);
    auto be32 = [&](size_t o) { return (uint32_t(uint8_t(png[o])) << 24) | (uint32_t(uint8_t(png[o + 1])) << 16) | (uint32_t(uint8_t(png[o + 2])) << 8) | uint8_t(png[o + 3]); };
    CHECK(be32(16) == 128 && be32(20) == 128 && png[24] == 8 && png[25] == 6 && png[28] == 0);
    CHECK(png.find("IEND") != std::string::npos);
}

// =====================================================================================
// D. Okno BursztynUI z symulowanym jadrem
// =====================================================================================
static void rysuj(OknoMenedzera& o) { KontekstRysowania ctx; o.rysuj(ctx, true); }
static void w_kliencie(Kontrolka* c, const Prostokat& kl) {
    if (!c->czy_widoczna()) return;
    const auto r = c->obszar_ekranu();
    if (!(r.x >= kl.x && r.y >= kl.y && r.x + r.szerokosc <= kl.x + kl.szerokosc && r.y + r.wysokosc <= kl.y + kl.wysokosc))
        std::fprintf(stderr, "poza klientem: %s r=%d,%d %dx%d klient=%d,%d %dx%d\n", typeid(*c).name(), r.x, r.y, r.szerokosc, r.wysokosc, kl.x, kl.y, kl.szerokosc, kl.wysokosc);
    CHECK(r.x >= kl.x && r.y >= kl.y && r.x + r.szerokosc <= kl.x + kl.szerokosc && r.y + r.wysokosc <= kl.y + kl.wysokosc);
    for (size_t i = 0; i < c->liczba_dzieci(); ++i) w_kliencie(c->dziecko(i), kl);
}
static int wiersz_pid(OknoMenedzera& o, uint32_t pid) {
    for (uint32_t i = 0; i < o.model().liczba(); ++i) if (o.model().wiersz(i)->pid == pid) return int(i);
    return -1;
}
static void okno_cases() {
    Jadro j; g_jadro = &j;
    j.uruchom(1, "/menedzer_okien.bur", PZB_ZAUFANE);
    j.uruchom(6, AppIdentity::TaskManager, PZB_ZAUFANE, 1);
    j.uruchom(3, "/programy/notatnik.cebula/notatnik.bur"); j.p[3].okno = 2 << 20;
    j.uruchom(4, "/programy/kalkulator.cebula/kalkulator.bur");
    auto* o = new OknoMenedzera;
    o->wywolaj = bws_symulowane; o->zegar_ns = zegar_testowy;
    o->ustaw_geometrie(40, 40, 900, 580);
    o->init();
    o->przelicz_uklad();                                      // jak AplikacjaGUI::uruchom przed pierwsza klatka
    CHECK(o->probki() == 1 && j.c103 == 1 && o->model().liczba() == 5);
    CHECK(!o->tabela()->ma_wybor());                          // brak domyslnego zaznaczenia
    CHECK(o->tabela()->liczba_kolumn() == 6);
    rysuj(*o);
    w_kliencie(o->kontrolka_glowna(), o->obszar_klienta());
    char kom[64];
    CHECK(!std::strcmp(o->tabela()->dostawca()->naglowek_kolumny(4), "Pamięć"));
    CHECK(!std::strcmp(o->tabela()->dostawca()->pobierz_komorke(0, 0, kom, sizeof(kom)), "Jądro systemu"));
    CHECK(!std::strcmp(o->tabela()->dostawca()->pobierz_komorke(0, 4, kom, sizeof(kom)), "Niedostępne"));
    CHECK(!std::strcmp(o->tabela()->dostawca()->pobierz_komorke(0, 3, kom, sizeof(kom)), "Niedostępne"));   // brak przedzialu
    j.uplyw(1000000000ULL, {{3, 400000000ULL}}); g_zegar += 1000000000ULL; o->odswiez();
    const int r3 = wiersz_pid(*o, 3);
    CHECK(!std::strcmp(o->tabela()->dostawca()->pobierz_komorke(r3, 3, kom, sizeof(kom)), "40,0 %"));
    CHECK(!std::strcmp(o->tabela()->dostawca()->pobierz_komorke(r3, 2, kom, sizeof(kom)), "Oczekujący (mysz)"));
    CHECK(!std::strcmp(o->tabela()->dostawca()->pobierz_komorke(wiersz_pid(*o, 6), 2, kom, sizeof(kom)), "Uruchomiony"));
    CHECK(!std::strcmp(o->tabela()->dostawca()->pobierz_komorke(r3, 5, kom, sizeof(kom)), "1 s"));

    // Brak alokacji przy odswiezaniu (staly bufor migawki, model bez alokacji).
    for (int i = 0; i < 3; ++i) { j.uplyw(1000000000ULL, {}); o->odswiez(); rysuj(*o); }
    const size_t przed = g_alokacje;
    for (int i = 0; i < 50; ++i) { j.uplyw(1000000000ULL, {{4, 1000}}); o->odswiez(); }
    CHECK(g_alokacje == przed);

    // Wybor -> sortowanie klikiem w naglowek -> ten sam proces nadal zaznaczony
    o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 3)));
    CHECK(o->model().wybrany() && o->model().wybrany()->pid == 3);
    // Szczegoly: tekst miesci sie w etykiecie (bez obciecia), a odswiezenie bez zmian nie uniewaznia etykiet
    // (natywnie: obciety tekst > 255 B byl ukladany od nowa co sekunde, ~50% CPU pod TCG).
    {
        rysuj(*o);
        CHECK(std::strstr(o->etykieta_szczegolow()->tekst(), "/programy/notatnik.cebula/notatnik.bur"));
        CHECK(std::strstr(o->etykieta_pamieci()->tekst(), "(niewliczana); sterta zarezerwowana"));
        CHECK(!std::strstr(o->etykieta_pamieci()->tekst(), "pomiar niepełny"));
        for (int i = 0; i < 3; ++i) {
            j.uplyw(1000000000ULL, {{3, 1000}}); o->odswiez();
            CHECK(!o->etykieta_szczegolow()->czy_brudna() && !o->etykieta_pamieci()->czy_brudna());
            CHECK(!o->etykieta_stanu()->czy_brudna());
            rysuj(*o);
        }
        // Najdluzsza mozliwa sciezka (63 B) i 20-cyfrowa generacja nadal miesci sie w limicie etykiety.
        char dluga[BWS_PROCES_SCIEZKA_MAKS];
        std::memset(dluga, 'a', sizeof(dluga) - 1); dluga[0] = '/'; dluga[sizeof(dluga) - 1] = '\0';
        j.uruchom(9, dluga); j.p[9].gen = 0xFFFFFFFFFFFFFFFFULL; j.uplyw(1000, {}); o->odswiez();
        o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 9)));
        const size_t n1 = std::strlen(o->etykieta_szczegolow()->tekst()), n2 = std::strlen(o->etykieta_pamieci()->tekst());
        CHECK(n1 + 1 < Kontrolka::MAKS_DLUGOSC_TEKSTU && n2 + 1 < Kontrolka::MAKS_DLUGOSC_TEKSTU);
        CHECK(std::strstr(o->etykieta_szczegolow()->tekst(), dluga) && std::strstr(o->etykieta_szczegolow()->tekst(), "18446744073709551615"));
        j.zwolnij(9); j.uplyw(1000, {}); o->odswiez();
        o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 3)));
        CHECK(o->model().wybrany() && o->model().wybrany()->pid == 3);
    }
    for (size_t k = 0; k < 6; ++k) {
        o->tabela()->dostawca()->zazadaj_sortowania(k, k % 2 == 0);
        CHECK(o->tabela()->wybrany_wiersz() == static_cast<size_t>(wiersz_pid(*o, 3)));
    }
    // Nowy proces w trakcie i proces konczacy sie sam: lista aktualna, wybor zostaje przy Notatniku
    j.uruchom(7, "/gry/snake.cebula/snake.bur"); j.uplyw(1000, {}); o->odswiez();
    CHECK(o->model().liczba() == 6 && o->model().wybrany()->pid == 3);
    CHECK(o->tabela()->wybrany_wiersz() == static_cast<size_t>(wiersz_pid(*o, 3)));
    j.zwolnij(7); j.uplyw(1000, {}); o->odswiez(); CHECK(o->model().liczba() == 5 && wiersz_pid(*o, 7) < 0);

    // Zakoncz zadanie: dialog, anulowanie (zadnego wywolania jadra), potwierdzenie
    o->zakoncz_zadanie();
    CHECK(o->dialog_pytanie() && o->dialog_pytanie()->czy_widoczna());
    CHECK(std::strstr(o->dialog_pytanie()->wiadomosc(), "Czy na pewno chcesz zakończyć proces Notatnik (PID 3)?"));
    CHECK(std::strstr(o->dialog_pytanie()->wiadomosc(), "Niezapisane zmiany"));
    rysuj(*o);
    o->dialog_pytanie()->zamknij_z_wynikiem(WynikDialogu::Nie);
    CHECK(j.c104 == 0 && j.p[3].stan == 3 && o->model().etap() == Etap::Brak);
    CHECK(std::strstr(o->tekst_banera(), "Anulowano"));
    o->zakoncz_zadanie();
    const uint64_t gen3 = j.p[3].gen;
    o->dialog_pytanie()->zamknij_z_wynikiem(WynikDialogu::Tak);
    CHECK(j.c104 == 1 && j.ost_pid == 3 && j.ost_gen == gen3 && o->ostatni_wynik_zakonczenia() == BWS_ZAKONCZ_ZAAKCEPTOWANO);
    CHECK(j.p[3].stan == 6 && wiersz_pid(*o, 3) >= 0);        // zaakceptowano, jeszcze nie posprzatano
    CHECK(std::strstr(o->tekst_banera(), "Kończenie procesu"));
    j.zwolnij(3); j.uplyw(1000, {}); o->odswiez();             // jadro zwolnilo zasoby
    CHECK(wiersz_pid(*o, 3) < 0 && !o->model().wybrany() && !o->tabela()->ma_wybor());
    CHECK(std::strstr(o->tekst_banera(), "jądro zwolniło jego zasoby"));   // nie nadpisane informacja o wyborze

    // Proces chroniony: informacja, bez wywolania jadra
    o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 1)));
    o->zakoncz_zadanie();
    CHECK(o->dialog_info() && o->dialog_info()->czy_widoczna() && std::strstr(o->dialog_info()->wiadomosc(), "Menedżer okien"));
    CHECK(j.c104 == 1);
    o->dialog_info()->zamknij_z_wynikiem(WynikDialogu::OK);
    o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 0)));
    o->zakoncz_zadanie(); CHECK(std::strstr(o->dialog_info()->wiadomosc(), "PID 0") && j.c104 == 1);
    o->dialog_info()->zamknij_z_wynikiem(WynikDialogu::OK);
    // Wlasne okno: zwykle zamkniecie (pytanie), "Nie" nic nie robi
    o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 6)));
    o->zakoncz_zadanie(); CHECK(std::strstr(o->dialog_pytanie()->wiadomosc(), "zamknąć Menedżer zadań"));
    o->dialog_pytanie()->zamknij_z_wynikiem(WynikDialogu::Nie); CHECK(j.c104 == 1);

    // PID ponownie uzyty w trakcie dialogu: jadro odrzuca, nowy proces nietkniety, komunikat bledu
    o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 4)));
    o->zakoncz_zadanie();
    j.zwolnij(4); j.uruchom(4, "/programy/eksplorator.cebula/eksplorator-plikow.bur");
    o->dialog_pytanie()->zamknij_z_wynikiem(WynikDialogu::Tak);
    CHECK(j.c104 == 2 && o->ostatni_wynik_zakonczenia() == BWS_ZAKONCZ_BRAK_PROCESU && j.p[4].stan == 3);
    CHECK(o->dialog_info()->czy_widoczna() && std::strstr(o->dialog_info()->wiadomosc(), "już nie istnieje"));
    o->dialog_info()->zamknij_z_wynikiem(WynikDialogu::OK);

    // Telemetria niedostepna: komunikat, dane nie sa wymyslane
    const uint32_t przed_liczba = o->model().liczba();
    j.wymus103 = BWS_MIGAWKA_BRAK_UPRAWNIEN; o->odswiez();
    CHECK(o->bledy() == 1 && std::strstr(o->tekst_banera(), "Brak uprawnienia") && o->model().liczba() == przed_liczba);
    j.wymus103 = 0; j.uplyw(1000000000ULL, {}); o->odswiez();
    CHECK(o->model().wiersz(0)->cpu_promile == NIEDOSTEPNE);     // przedzial po bledzie zaczyna sie od nowa

    // Wiele procesow, przewijanie, zmiana rozmiaru (minimum i duze okno), skale, motyw, karta Wydajnosc
    for (int pid = 2; pid < 16; ++pid) if (!j.p[pid].zywy) j.uruchom(pid, "/programy/notatnik.cebula/notatnik.bur");
    j.uplyw(1000000000ULL, {}); o->odswiez(); CHECK(o->model().liczba() == 16);
    for (auto [w, h] : {std::pair{620, 420}, std::pair{1280, 800}, std::pair{900, 580}}) {
        o->ustaw_geometrie(10, 10, w, h); o->przelicz_uklad(); rysuj(*o);
        w_kliencie(o->kontrolka_glowna(), o->obszar_klienta());
        ZdarzenieRolki e{}; e.delta_y = -3; o->tabela()->obsluz_rolke(e); rysuj(*o);
    }
    CHECK(o->tabela()->pierwszy_widoczny_wiersz() > 0 || o->tabela()->ostatni_widoczny_wiersz() >= 15);
    for (uint32_t s : {125u, 150u, 200u, 100u}) {
        bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_EKRAN; ev.kod = s; o->obsluz_zdarzenie(ev); rysuj(*o);
    }
    bws_zdarzenie motyw{}; motyw.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY; o->obsluz_zdarzenie(motyw); rysuj(*o);
    CHECK(o->wykres_cpu_->liczba_serii() == 0);                // ukryta karta Wydajnosc nie jest przeliczana
    o->karty_->ustaw_aktywna_karte(1); j.uplyw(1000000000ULL, {}); o->odswiez(); rysuj(*o);
    CHECK(o->wykres_cpu_->liczba_serii() == 1 && o->wykres_ram_->liczba_serii() == 1);
    { int cpu[HISTORIA]; const size_t n = o->model().historia(cpu, nullptr, HISTORIA); CHECK(n > 0 && n <= HISTORIA); }
    CHECK(std::strstr(o->info_ram_->tekst(), "RAM zarządzana przez system"));
    CHECK(std::strstr(o->info_cpu_->tekst(), "HPET"));

    // ---- Wydajnosc karty Wydajnosc (natywnie: zacinanie kursora - koszt rysowania raz na sekunde)
    auto przebieg = [&](int i) {                               // zmienne obciazenie CPU, stala pamiec RAM
        j.uplyw(1000000000ULL, {{2, 100000000ULL + uint64_t(i % 7) * 90000000ULL}}); o->odswiez();
    };
    o->ustaw_geometrie(10, 10, 900, 580); o->przelicz_uklad(); rysuj(*o);
    for (int i = 0; i < HISTORIA + 10; ++i) { przebieg(i); KontekstRysowania c; o->rysuj(c, false); }
    {   // historia ograniczona; plaska seria RAM nie jest przerysowywana, zmienna seria CPU - tak (raz na probke)
        int cpu[HISTORIA], ram[HISTORIA];
        CHECK(o->model().historia(cpu, ram, HISTORIA) == HISTORIA);
        CHECK(o->wykres_cpu_->seria(0)->liczba_punktow == HISTORIA && o->wykres_ram_->seria(0)->liczba_punktow == HISTORIA);
        const uint64_t pelne = o->liczba_pelnych_odrysowan();
        przebieg(3);
        CHECK(o->wykres_cpu_->czy_brudna() && !o->wykres_ram_->czy_brudna());
        KontekstRysowania c; o->rysuj(c, false);
        CHECK(o->liczba_pelnych_odrysowan() == pelne);         // odswiezenie danych nie przerysowuje calego okna
        CHECK(!o->wykres_cpu_->czy_brudna() && !o->info_cpu_->czy_brudna());
    }
    {   // brak alokacji w normalnym cyklu probka -> rysowanie na karcie Wydajnosc
        const size_t a0 = g_alokacje;
        for (int i = 0; i < 50; ++i) { przebieg(i); KontekstRysowania c; o->rysuj(c, false); }
        CHECK(g_alokacje == a0);
    }
    {   // ruch myszy miedzy probkami: zadnego przerysowania karty ani pelnego okna
        const uint64_t px0 = o->liczba_brudnych_pikseli(), pelne = o->liczba_pelnych_odrysowan();
        for (int i = 0; i < 300; ++i) {
            bws_zdarzenie e{}; e.typ = BWS_ZDARZENIE_MYSZ_RUCH; e.x = 20 + (i * 13) % 880; e.y = 150 + (i * 7) % 420;
            o->obsluz_zdarzenie(e); KontekstRysowania c; o->rysuj(c, false);
        }
        CHECK(o->liczba_pelnych_odrysowan() == pelne);
        CHECK(o->liczba_brudnych_pikseli() - px0 < 20000);    // co najwyzej podswietlenie zakladki/przycisku
        CHECK(!o->wykres_cpu_->czy_brudna() && !o->wykres_ram_->czy_brudna());
        przebieg(5); CHECK(o->wykres_cpu_->czy_brudna());     // probka w trakcie ruchu myszy nadal aktualizuje wykres
        KontekstRysowania c; o->rysuj(c, false);
    }
    {   // ukryta karta: zadnej pracy wykresow ani etykiet Wydajnosci
        o->karty_->ustaw_aktywna_karte(0); { KontekstRysowania c; o->rysuj(c, false); }
        const int ost = o->wykres_cpu_->seria(0)->punkty[HISTORIA - 1];
        for (int i = 0; i < 10; ++i) { przebieg(i + 1); KontekstRysowania c; o->rysuj(c, false); }
        CHECK(!o->wykres_cpu_->czy_brudna() && !o->wykres_ram_->czy_brudna() && !o->info_cpu_->czy_brudna());
        CHECK(o->wykres_cpu_->seria(0)->punkty[HISTORIA - 1] == ost);   // dane wykresu nie byly przeliczane
        o->karty_->ustaw_aktywna_karte(1);                     // powrot: natychmiast aktualne dane
        CHECK(o->wykres_cpu_->czy_brudna());
        { KontekstRysowania c; o->rysuj(c, false); }
    }
    {   // wielokrotne przelaczanie kart: bez alokacji i z poprawna geometria
        for (int i = 0; i < 4; ++i) { o->karty_->ustaw_aktywna_karte(i & 1); KontekstRysowania c; o->rysuj(c, false); }
        const size_t a0 = g_alokacje;
        for (int i = 0; i < 40; ++i) {
            o->karty_->ustaw_aktywna_karte(i & 1); przebieg(i); KontekstRysowania c; o->rysuj(c, false);
        }
        CHECK(g_alokacje == a0);
        CHECK(o->model().liczba() == 16 && o->tabela()->liczba_wierszy() == 16);
    }
    {   // rozmiary i skale z widoczna karta Wydajnosc: geometria w obszarze klienta, wykresy i opisy widoczne
        o->karty_->ustaw_aktywna_karte(1);
        for (uint32_t s : {100u, 150u, 200u}) {
            bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_EKRAN; ev.kod = s; o->obsluz_zdarzenie(ev);
            for (auto [w, h] : {std::pair{620, 420}, std::pair{1280, 800}, std::pair{900, 580}}) {
                o->ustaw_geometrie(0, 0, w * int(s) / 100, h * int(s) / 100); o->przelicz_uklad(); rysuj(*o);
                w_kliencie(o->kontrolka_glowna(), o->obszar_klienta());
                CHECK(o->wykres_cpu_->obszar_ekranu().szerokosc > 0 && o->wykres_cpu_->obszar_ekranu().wysokosc > 0);
                CHECK(o->info_cpu_->obszar_ekranu().wysokosc >= CzcionkaUI::pobierz_wysokosc(SkalaUI::procent(s)));
                przebieg(1); KontekstRysowania c; o->rysuj(c, false);
            }
        }
        bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_EKRAN; ev.kod = 100; o->obsluz_zdarzenie(ev);
        o->ustaw_geometrie(10, 10, 900, 580); o->przelicz_uklad(); rysuj(*o);
    }
    {   // po pracy na Wydajnosci sortowanie i wybor na Procesach nadal dzialaja
        o->karty_->ustaw_aktywna_karte(0); rysuj(*o);
        o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 5)));
        o->tabela()->dostawca()->zazadaj_sortowania(4, false);
        CHECK(o->model().wybrany() && o->model().wybrany()->pid == 5);
        CHECK(o->tabela()->wybrany_wiersz() == static_cast<size_t>(wiersz_pid(*o, 5)));
    }
    o->karty_->ustaw_aktywna_karte(0); rysuj(*o);
    delete o;
    g_jadro = nullptr;
}

// Etykiety tworzone z pustym tekstem i wypelniane po ukladzie (ustaw_tekst nie przelicza ukladu) musza miec od razu
// wysokosc na swoja tresc. Natywnie opisy pod wykresami Wydajnosci byly niewidoczne (4 px) do pierwszej zmiany rozmiaru.
static void uklad_bez_przeliczenia() {
    Jadro j; g_jadro = &j;
    j.uruchom(1, "/menedzer_okien.bur", PZB_ZAUFANE);
    j.uruchom(6, AppIdentity::TaskManager, PZB_ZAUFANE, 1);
    j.uruchom(3, "/programy/notatnik.cebula/notatnik.bur");
    auto* o = new OknoMenedzera;
    o->wywolaj = bws_symulowane; o->zegar_ns = zegar_testowy;
    o->ustaw_geometrie(40, 40, 900, 580); o->init(); o->przelicz_uklad();   // jedyny uklad - jak przy starcie
    const int linia = CzcionkaUI::pobierz_wysokosc(SkalaUI::procent(100));
    for (int i = 0; i < 2; ++i) { j.uplyw(1000000000ULL, {{3, 1000}}); g_zegar += 1000000000ULL; o->odswiez(); rysuj(*o); }
    o->tabela()->ustaw_wybrany_wiersz(static_cast<size_t>(wiersz_pid(*o, 3))); rysuj(*o);
    CHECK(o->pamiec_procesu_->tekst()[0] != '\0');
    CHECK(o->etykieta_szczegolow()->obszar_ekranu().wysokosc >= linia);
    CHECK(o->pamiec_procesu_->obszar_ekranu().wysokosc >= 2 * linia);            // dwie linie pamieci
    o->karty_->ustaw_aktywna_karte(1);
    for (int i = 0; i < 2; ++i) { j.uplyw(1000000000ULL, {{3, 1000}}); g_zegar += 1000000000ULL; o->odswiez(); rysuj(*o); }
    CHECK(o->info_cpu_->obszar_ekranu().wysokosc >= 4 * linia);                 // >= 4 linie opisu CPU
    CHECK(o->info_ram_->obszar_ekranu().wysokosc >= 4 * linia);
    w_kliencie(o->kontrolka_glowna(), o->obszar_klienta());
    delete o;
    g_jadro = nullptr;
}

int main() {
    rozliczanie_cpu();
    pamiec_procesu();
    abi_i_polityka();
    model_cases();
    teksty();
    rejestracja();
    okno_cases();
    uklad_bez_przeliczenia();
    std::printf("test-menedzer-zadan-host: %u sprawdzen OK\n", checks);
    return 0;
}
