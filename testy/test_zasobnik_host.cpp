// Bursztynowy Zasobnik - testy hosta (ASan/UBSan): rejestr i uklad elementow, glify, powierzchnia, model stanu,
// wspolne pozycjonowanie paneli, kolejka powiadomien (granice, limity, zaufane zrodlo, czyszczenie tekstu),
// glosnosc HDA (mapowanie krokow, wyciszenie, nasycenie), autoryzacja i budowa odpowiedzi BWS 107-109 oraz
// kontroler z panelami wobec realistycznej atrapy jadra (synchronizacja, odmowy, zajety sprzet, brak backendu).
#include "bursztyn_ui_host_fixture.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <new>

static size_t g_alokacje = 0;
void* operator new(size_t n) { ++g_alokacje; void* p = malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n) { ++g_alokacje; void* p = malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

#include "../bursztyn_ui/bursztyn_ui.h"
#include "zasobnik_panele.h"
#include "kalendarz_paska.h"
#include "zasobnik_jadro.h"
#include "sterowniki/dzwiek/hda_glosnosc.h"

using namespace BursztynUI;
using namespace Zasobnik;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL test_zasobnik_host.cpp:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)

static void nacisnij(Kontrolka* k) {
    ZdarzenieMyszki e{}; e.typ = TypZdarzenia::WcisniecieMyszy; e.przycisk = 1;
    e.x = k->obszar_ekranu().x + k->obszar().szerokosc / 2; e.y = k->obszar_ekranu().y + k->obszar().wysokosc / 2;
    k->obsluz_wcisniecie_myszy(e);
    ZdarzenieMyszki u = e; u.typ = TypZdarzenia::ZwolnienieMyszy; k->obsluz_zwolnienie_myszy(u);
}

// ---------------------------------------------------------------- 1. rejestr
static void rejestr() {
    Rejestr r;
    CHECK(r.liczba() == 0 && !r.znajdz(Id::Dzwiek));
    CHECK(r.zarejestruj(Id::Dzwiek, Ikona::Glosnik, 2, 2));
    CHECK(!r.zarejestruj(Id::Dzwiek, Ikona::Glosnik, 0, 0));                 // stala tozsamosc: bez duplikatow
    CHECK(!r.zarejestruj(Id::Brak, Ikona::Glosnik, 0, 0));
    const uint32_t g0 = r.generacja();
    CHECK(r.aktualizuj(Id::Dzwiek, true, STAN_WYCISZONY, "Głośność: wyciszono") && r.generacja() != g0);
    const uint32_t g1 = r.generacja();
    CHECK(!r.aktualizuj(Id::Dzwiek, true, STAN_WYCISZONY, "inny dymek"));     // sam dymek: bez przerysowania
    CHECK(r.generacja() == g1 && !std::strcmp(r.znajdz(Id::Dzwiek)->dymek, "inny dymek"));
    CHECK(r.ustaw_widocznosc(Id::Dzwiek, false) && !r.ustaw_widocznosc(Id::Dzwiek, false) && !r.znajdz(Id::Dzwiek)->widoczny);
    CHECK(!r.aktualizuj(Id::Siec, true, 0, "x"));                              // nieznany element
    // dlugi dymek: uciety na granicy znaku UTF-8
    std::string dlugi; for (int i = 0; i < 80; ++i) dlugi += "ż";
    r.aktualizuj(Id::Dzwiek, false, 0, dlugi.c_str());
    const char* d = r.znajdz(Id::Dzwiek)->dymek;
    CHECK(std::strlen(d) < MAKS_DYMKA && std::strlen(d) % 2 == 0);
    // pojemnosc i usuwanie
    for (int i = 0; i < 20; ++i) r.zarejestruj(Id(10 + i), Ikona::Nosnik, 9, 9);
    CHECK(r.liczba() == MAKS_ELEMENTOW);
    CHECK(r.usun(Id::Dzwiek) && !r.znajdz(Id::Dzwiek) && !r.usun(Id::Dzwiek) && r.liczba() == MAKS_ELEMENTOW - 1);
}

// ---------------------------------------------------------------- 2. uklad, trafienia, przepelnienie, polozenie
static void uklad() {
    Rejestr r; zarejestruj_podstawowe(r);
    for (int skala : {1, 2}) {
        const SkalaUI s{uint32_t(skala)};
        for (int w : {640, 800, 1024, 1280, 1366, 1920, 3840}) {
            const int pasek_h = 40 * skala, pasek_y = 1080 - pasek_h, zegar_x = w - 150 * skala;
            Uklad u; uloz(r, zegar_x, pasek_y, pasek_h, w / 2, metryki(s), u);
            CHECK(u.liczba == 4 && u.ukryte == 0);
            CHECK(u.obszar.prawy() == zegar_x);                                       // przy zegarze, nie na nim
            const Metryki m = metryki(s);
            for (int i = 0; i < u.liczba; ++i) {
                const Prostokat b = u.prostokaty[i];
                CHECK(b.szerokosc == 32 * skala && b.wysokosc >= 24 * skala);           // wygodny cel trafienia
                CHECK(b.x >= u.obszar.x && b.prawy() <= zegar_x && b.y >= pasek_y && b.y + b.wysokosc <= pasek_y + pasek_h);
                if (i) CHECK(b.x == u.prostokaty[i - 1].prawy() + m.odstep);            // rowne odstepy
                CHECK(u.trafienie(b.x + b.szerokosc / 2, b.y + b.wysokosc / 2) == u.sloty[i]);
            }
            CHECK(u.trafienie(zegar_x + 1, pasek_y + 5) == Id::Brak && u.trafienie(u.obszar.x - 1, pasek_y + 5) == Id::Brak);
            // kolejnosc od lewej: siec, dzwiek, powiadomienia, szybkie ustawienia (najblizej zegara)
            CHECK(u.sloty[0] == Id::Siec && u.sloty[1] == Id::Dzwiek && u.sloty[2] == Id::Powiadomienia && u.sloty[3] == Id::SzybkieUstawienia);
        }
    }
    // brak miejsca: zostaja elementy o najwyzszym priorytecie; zegar nigdy nie jest zaslaniany
    const Metryki m = metryki(SkalaUI{1});
    for (int maks = 0; maks <= 200; maks += 4) {
        Uklad u; uloz(r, 1000, 680, 40, maks, m, u);
        CHECK(u.obszar.szerokosc <= (maks > 0 ? maks : 0) && u.liczba + u.ukryte == 4);
        if (u.liczba >= 1) CHECK(u.prostokat(Id::SzybkieUstawienia).szerokosc > 0);   // centrum zawsze dostepne
        if (u.liczba >= 2) CHECK(u.prostokat(Id::Powiadomienia).szerokosc > 0);
        if (u.liczba < 4) CHECK(u.prostokat(Id::Siec).czy_pusty());
        CHECK(u.obszar.szerokosc == 0 || u.obszar.prawy() == 1000);
    }
    // ukryty element (np. nosnik bez miejsca) nie ma prostokata, a widocznosc false go pomija
    r.ustaw_widocznosc(Id::Siec, false);
    { Uklad u; uloz(r, 1000, 680, 40, 400, m, u); CHECK(u.liczba == 3 && u.ukryte == 0 && u.prostokat(Id::Siec).czy_pusty()); }
    // wspolne polozenie paneli (takze kalendarza): w ekranie, nad paskiem, kotwica odslonieta
    for (int w : {640, 800, 1280, 1920}) for (int h : {480, 600, 720, 1080}) for (int skala : {1, 2}) {
        const SkalaUI s{uint32_t(skala)};
        const int pasek_y = h - 40 * skala;
        const Prostokat kotw{w - 200 * skala, pasek_y + 4, 32 * skala, 32 * skala};
        const Prostokat p = polozenie_panelu(kotw, w, pasek_y, 340 * s, 420 * s, 4 * s, 6 * s);
        CHECK(p.x >= 4 * skala - 1 && p.prawy() <= w && p.y >= 0 && p.y + p.wysokosc <= pasek_y - 6 * skala + 1);
        if (kotw.prawy() - p.szerokosc >= 4 * skala && kotw.prawy() <= w - 4 * skala) CHECK(p.prawy() == kotw.prawy());
        else CHECK(p.x == 4 * skala || p.prawy() == w - 4 * skala);
        const Prostokat k = KalendarzPaska::polozenie(kotw, w, pasek_y, 300 * s, 326 * s, 4 * s, 6 * s);
        const Prostokat z = polozenie_panelu(kotw, w, pasek_y, 300 * s, 326 * s, 4 * s, 6 * s);
        CHECK(k.x == z.x && k.y == z.y && k.szerokosc == z.szerokosc && k.wysokosc == z.wysokosc);
    }
}

// ---------------------------------------------------------------- 3. glify i powierzchnia
static int licz(const std::vector<uint32_t>& b, uint32_t kolor) { int n = 0; for (uint32_t p : b) n += (p & 0xFFFFFF) == (kolor & 0xFFFFFF); return n; }
// Pokrycie (piksel inny niz tlo) i akcent (piksel niegrafitowy: tlo i rysunek sa szare, akcent pomaranczowy).
static int pokryte(const std::vector<uint32_t>& b, uint32_t tlo) { return int(b.size()) - licz(b, tlo); }
static int akcent(const std::vector<uint32_t>& b) { int n = 0; for (uint32_t p : b) n += ((p >> 16) & 255) != (p & 255); return n; }
static void glify() {
    for (int bok : {20, 40}) {
        const uint32_t tlo = 0xFF101010, fg = 0xFFEEEEEE, ak = 0xFFFFAA00;
        auto narysuj = [&](Ikona ik, uint32_t st) {
            std::vector<uint32_t> b(size_t(bok) * bok, tlo);
            Glif::rysuj(b.data(), bok, bok, 0, 0, bok, ik, st, fg, ak);
            return b;
        };
        for (Ikona ik : {Ikona::Glosnik, Ikona::Siec, Ikona::Dzwonek, Ikona::Ustawienia, Ikona::Nosnik}) {
            auto b = narysuj(ik, 0);
            CHECK(pokryte(b, tlo) > bok * 2);                                          // rozpoznawalny ksztalt
            CHECK(licz(b, tlo) > bok * bok / 4);
        }
        auto glosny = narysuj(Ikona::Glosnik, 0), cichy = narysuj(Ikona::Glosnik, STAN_POZIOM_NISKI), wyc = narysuj(Ikona::Glosnik, STAN_WYCISZONY);
        CHECK(pokryte(glosny, tlo) > pokryte(cichy, tlo) && akcent(wyc) > 0 && akcent(glosny) == 0);
        CHECK(akcent(narysuj(Ikona::Dzwonek, STAN_NIEPRZECZYTANE)) > 0 && akcent(narysuj(Ikona::Dzwonek, 0)) == 0);
        CHECK(akcent(narysuj(Ikona::Siec, STAN_ODLACZONY)) > 0 && akcent(narysuj(Ikona::Siec, 0)) == 0);
        CHECK(pokryte(narysuj(Ikona::Siec, STAN_NIEDOSTEPNY), tlo) > pokryte(narysuj(Ikona::Siec, 0), tlo));   // przekreslenie
        // rysowanie poza buforem jest przycinane (ASan)
        std::vector<uint32_t> b(16 * 16, tlo);
        Glif::rysuj(b.data(), 16, 16, -10, 8, bok, Ikona::Siec, 0, fg, ak);
    }
    // powierzchnia: przerysowanie tylko przy zmianie sygnatury, bez alokacji w stanie ustalonym
    Rejestr r; zarejestruj_podstawowe(r);
    Uklad u; uloz(r, 1130, 680, 40, 600, metryki(SkalaUI{1}), u);
    Powierzchnia p;
    int publikacji = 0;
    std::vector<uint32_t> ekran;
    auto publish = [&](Prostokat o, const uint32_t* px) { ++publikacji; ekran.assign(px, px + size_t(o.szerokosc) * o.wysokosc); return true; };
    CHECK(p.rysuj(r, u, Id::Brak, Id::Brak, SkalaUI{1}, false, publish) && publikacji == 1);
    const size_t a0 = g_alokacje;
    for (int i = 0; i < 50; ++i) CHECK(!p.rysuj(r, u, Id::Brak, Id::Brak, SkalaUI{1}, false, publish));
    CHECK(publikacji == 1 && g_alokacje == a0);
    CHECK(p.rysuj(r, u, Id::Dzwiek, Id::Brak, SkalaUI{1}, false, publish) && publikacji == 2);    // najechanie
    CHECK(p.rysuj(r, u, Id::Dzwiek, Id::Dzwiek, SkalaUI{1}, false, publish) && publikacji == 3);  // aktywny panel
    r.aktualizuj(Id::Dzwiek, true, STAN_WYCISZONY, "x");
    CHECK(p.rysuj(r, u, Id::Dzwiek, Id::Dzwiek, SkalaUI{1}, false, publish) && publikacji == 4);
    CHECK(g_alokacje == a0);
    // odrzucona publikacja: ponowienie przy nastepnym wywolaniu
    CHECK(!p.rysuj(r, u, Id::Brak, Id::Brak, SkalaUI{1}, false, [](Prostokat, const uint32_t*) { return false; }));
    CHECK(p.rysuj(r, u, Id::Brak, Id::Brak, SkalaUI{1}, false, publish));
}

// ---------------------------------------------------------------- 4. model stanu
static BwsZasobnikStanV1 stan_bazowy() {
    BwsZasobnikStanV1 s{};
    s.wersja = uint32_t(BWS_ZASOBNIK_V1); s.rozmiar = sizeof(s);
    s.dzwiek.flagi = BWS_DZW_URZADZENIE | BWS_DZW_REGULACJA | BWS_DZW_WYCISZANIE;
    s.dzwiek.backend = BWS_DZW_BACKEND_WZMACNIACZ; s.dzwiek.glosnosc = 80;
    s.siec.flagi = BWS_ZSIEC_INTERFEJS | BWS_ZSIEC_IPV4 | BWS_ZSIEC_BRAMA | BWS_ZSIEC_DNS;
    s.siec.liczba_interfejsow = 1;
    const uint8_t ip[4] = {10, 0, 2, 15}; std::memcpy(s.siec.ip, ip, 4);
    s.siec.interfejs.typ = BWS_SIEC_VIRTIO; s.siec.interfejs.flagi = BWS_SIEC_AKTYWNA | BWS_SIEC_GLOWNA;
    return s;
}
static void model() {
    Rejestr r; zarejestruj_podstawowe(r);
    BwsZasobnikStanV1 s = stan_bazowy();
    CHECK(stan_poprawny(s));
    (void)aktualizuj(r, s, true);                                                // domyslny wyglad = ten stan
    CHECK(!std::strcmp(r.znajdz(Id::Dzwiek)->dymek, "Głośność: 80 %") && r.znajdz(Id::Dzwiek)->stan == 0);
    CHECK(!std::strcmp(r.znajdz(Id::Siec)->dymek, "Sieć: Ethernet · 10.0.2.15"));
    CHECK(!std::strcmp(r.znajdz(Id::Powiadomienia)->dymek, "Brak powiadomień"));
    CHECK(!aktualizuj(r, s, true));                                              // ten sam stan: bez przerysowania
    s.dzwiek.glosnosc = 20; aktualizuj(r, s, true); CHECK(r.znajdz(Id::Dzwiek)->stan == STAN_POZIOM_NISKI);
    s.dzwiek.glosnosc = 50; aktualizuj(r, s, true); CHECK(r.znajdz(Id::Dzwiek)->stan == STAN_POZIOM_SREDNI);
    s.dzwiek.flagi |= BWS_DZW_WYCISZONY; aktualizuj(r, s, true);
    CHECK(r.znajdz(Id::Dzwiek)->stan == STAN_WYCISZONY && !std::strcmp(r.znajdz(Id::Dzwiek)->dymek, "Głośność: wyciszono"));
    s.dzwiek.flagi = BWS_DZW_URZADZENIE; aktualizuj(r, s, true);                // urzadzenie bez regulacji
    CHECK((r.znajdz(Id::Dzwiek)->stan & STAN_NIEDOSTEPNY) && std::strstr(r.znajdz(Id::Dzwiek)->dymek, "niedostępna"));
    s.dzwiek.flagi = 0; aktualizuj(r, s, true);
    CHECK(!r.znajdz(Id::Dzwiek)->wlaczony && !std::strcmp(r.znajdz(Id::Dzwiek)->dymek, "Dźwięk: brak urządzenia audio"));
    // siec: brak konfiguracji, lacze odlaczone, brak karty
    s.siec.flagi = BWS_ZSIEC_INTERFEJS; aktualizuj(r, s, true);
    CHECK(r.znajdz(Id::Siec)->stan == STAN_OSTRZEZENIE && std::strstr(r.znajdz(Id::Siec)->dymek, "brak konfiguracji"));
    s.siec.interfejs.flagi |= BWS_SIEC_LACZE_ZNANE; aktualizuj(r, s, true);
    CHECK(r.znajdz(Id::Siec)->stan == STAN_ODLACZONY && !std::strcmp(r.znajdz(Id::Siec)->dymek, "Sieć: kabel odłączony"));
    s.siec.flagi = 0; aktualizuj(r, s, true);
    CHECK((r.znajdz(Id::Siec)->stan & STAN_NIEDOSTEPNY) && !std::strcmp(r.znajdz(Id::Siec)->dymek, "Sieć: brak karty sieciowej"));
    // powiadomienia
    s.powiadomienia_liczba = 3; s.powiadomienia_nieprzeczytane = 2; aktualizuj(r, s, true);
    CHECK(r.znajdz(Id::Powiadomienia)->stan == STAN_NIEPRZECZYTANE && !std::strcmp(r.znajdz(Id::Powiadomienia)->dymek, "Powiadomienia: 2 nieprzeczytanych"));
    s.powiadomienia_nieprzeczytane = 0; aktualizuj(r, s, true);
    CHECK(r.znajdz(Id::Powiadomienia)->stan == 0 && !std::strcmp(r.znajdz(Id::Powiadomienia)->dymek, "Powiadomienia: brak nowych"));
    // nosniki: element pojawia sie i znika razem z urzadzeniem (bez wiszacego elementu)
    CHECK(!r.znajdz(Id::Nosniki));
    s.liczba_nosnikow = 1; s.nosniki[0].flagi = BWS_NOS_WYMIENNY; CHECK(aktualizuj(r, s, true) && r.znajdz(Id::Nosniki));
    s.liczba_nosnikow = 0; CHECK(aktualizuj(r, s, true) && !r.znajdz(Id::Nosniki));
    // brak/niepoprawna odpowiedz: wszystko niedostepne, bateria i wydajnosc nigdy nie sa rejestrowane
    BwsZasobnikStanV1 zly{}; CHECK(!stan_poprawny(zly));
    zly = stan_bazowy(); zly.liczba_nosnikow = 99; CHECK(!stan_poprawny(zly));
    aktualizuj(r, BwsZasobnikStanV1{}, false);
    CHECK((r.znajdz(Id::Dzwiek)->stan & STAN_NIEDOSTEPNY) && (r.znajdz(Id::Siec)->stan & STAN_NIEDOSTEPNY) && r.liczba() == 4);
    // tekst pomocniczy
    char t[64]; Tekst x(t, sizeof(t)); x.bajty(999); x << " "; x.bajty(1000); x << " "; x.bajty(1234567); x << " "; x.bajty(0);
    CHECK(!std::strcmp(t, "999 B 1,0 kB 1,2 MB 0 B"));
}

// ---------------------------------------------------------------- 5. kolejka powiadomien
static void powiadomienia() {
    using Powiadomienia::Kolejka;
    char c[64];
    CHECK(Powiadomienia::oczysc(c, sizeof(c), "  Ala\n\tma\x01kota  ", 64) && !std::strcmp(c, "Ala ma kota"));
    CHECK(Powiadomienia::oczysc(c, sizeof(c), "zła\xC3", 64) && !std::strcmp(c, "zła"));             // uciety znak
    CHECK(Powiadomienia::oczysc(c, sizeof(c), "a\xC0\xAF" "b", 64) && !std::strcmp(c, "a"));          // nadmiarowe kodowanie
    CHECK(Powiadomienia::oczysc(c, sizeof(c), "a\xED\xA0\x80" "b", 64) && !std::strcmp(c, "a"));      // surogat
    CHECK(Powiadomienia::oczysc(c, 6, "żółw", 64) == 4 && !std::strcmp(c, "żó"));                     // granica znaku
    { char bez_nul[8]; std::memset(bez_nul, 'x', sizeof(bez_nul));                                   // bez NUL w zrodle
      CHECK(Powiadomienia::oczysc(c, sizeof(c), bez_nul, sizeof(bez_nul)) == 8); }
    CHECK(!Powiadomienia::oczysc(c, sizeof(c), "\n\n", 64) && c[0] == 0);
    Powiadomienia::nazwa_programu(c, sizeof(c), "/programy/kalendarz.cebula/kalendarz.bur", 64); CHECK(!std::strcmp(c, "kalendarz"));
    Powiadomienia::nazwa_programu(c, sizeof(c), "", 64); CHECK(!std::strcmp(c, "aplikacja"));

    Kolejka k;
    BwsPowiadomieniaListaV1 l{};
    k.migawka(l); CHECK(l.liczba == 0 && l.wersja == uint32_t(BWS_ZASOBNIK_V1) && l.rozmiar == sizeof(l));
    const uint64_t a = k.dodaj_systemowe(BWS_POW_INFO, "Wykryto urządzenie", "opis", 100);
    CHECK(a && k.liczba() == 1 && k.nieprzeczytane() == 1);
    CHECK(!k.dodaj_systemowe(BWS_POW_INFO, "", "pusty tytul", 100));                 // bez tytulu: odrzucone
    // aplikacja: zrodlo, PID i nadawca zawsze od jadra; poziom BLAD zarezerwowany dla systemu
    uint64_t id = 0;
    CHECK(k.dodaj_aplikacji(5, "system", BWS_POW_INFO, "System Bursztyn OS", 64, "treść", 160, 200, &id) == BWS_ZAS_OK && id > a);
    k.migawka(l);
    CHECK(l.wpisy[0].id == id && l.wpisy[0].zrodlo == BWS_POW_ZRODLO_APLIKACJA && l.wpisy[0].pid == 5 &&
          !std::strcmp(l.wpisy[0].nadawca, "system") && l.wpisy[1].zrodlo == BWS_POW_ZRODLO_SYSTEM);
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_BLAD, "t", 64, "", 160, 200) == BWS_ZAS_ZLE_ARGUMENTY);
    CHECK(k.dodaj_aplikacji(0, "x", BWS_POW_INFO, "t", 64, "", 160, 200) == BWS_ZAS_ZLE_ARGUMENTY);
    CHECK(k.dodaj_aplikacji(16, "x", BWS_POW_INFO, "t", 64, "", 160, 200) == BWS_ZAS_ZLE_ARGUMENTY);
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_INFO, "\x01\x02", 64, "", 160, 200) == BWS_ZAS_ZLE_ARGUMENTY);
    // limit czestosci: 3 na okno 30 s na proces, potem LIMIT; nowe okno odblokowuje
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_INFO, "2", 64, "", 160, 300) == BWS_ZAS_OK);
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_INFO, "3", 64, "", 160, 400) == BWS_ZAS_OK);
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_INFO, "4", 64, "", 160, 500) == BWS_ZAS_LIMIT);
    CHECK(k.dodaj_aplikacji(6, "y", BWS_POW_INFO, "inny proces", 64, "", 160, 500) == BWS_ZAS_OK);
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_INFO, "4", 64, "", 160, 200 + 30000) == BWS_ZAS_OK);   // 4. wpis procesu
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_INFO, "5", 64, "", 160, 200 + 30001) == BWS_ZAS_LIMIT); // maks 4 w kolejce
    k.posprzataj_proces(5);                                                           // nowy proces z tym PID: wlasny limit
    CHECK(k.dodaj_aplikacji(5, "x", BWS_POW_INFO, "5", 64, "", 160, 200 + 30002) == BWS_ZAS_LIMIT); // wpisy wciaz w kolejce
    // ograniczona kolejka: systemowe zawsze wchodza (wypada najstarszy przeczytany, potem aplikacji)
    for (int i = 0; i < 40; ++i) CHECK(k.dodaj_systemowe(BWS_POW_OSTRZEZENIE, "zdarzenie", "", 1000 + i));
    CHECK(k.liczba() == BWS_POW_MAKS && k.nieprzeczytane() == BWS_POW_MAKS);
    k.migawka(l);
    CHECK(l.liczba == BWS_POW_MAKS);
    for (uint32_t i = 1; i < l.liczba; ++i) CHECK(l.wpisy[i - 1].id > l.wpisy[i].id);   // od najnowszego
    for (uint32_t i = 0; i < l.liczba; ++i) CHECK(l.wpisy[i].zrodlo == BWS_POW_ZRODLO_SYSTEM);
    // pelna kolejka nieprzeczytanych systemowych: aplikacja nie wypiera ich
    CHECK(k.dodaj_aplikacji(7, "z", BWS_POW_INFO, "spam", 64, "", 160, 90000) == BWS_ZAS_LIMIT);
    // przeczytane / usun / wyczysc + generacja
    const uint64_t g = k.generacja();
    CHECK(k.oznacz_przeczytane(l.wpisy[3].id) == BWS_ZAS_OK && k.generacja() == g + 1 && k.nieprzeczytane() == BWS_POW_MAKS - 1);
    CHECK(k.oznacz_przeczytane(l.wpisy[3].id) == BWS_ZAS_OK && k.generacja() == g + 1);   // idempotentne
    CHECK(k.oznacz_przeczytane(999999) == BWS_ZAS_BRAK && k.usun(0) == BWS_ZAS_BRAK);
    CHECK(k.dodaj_aplikacji(7, "z", BWS_POW_INFO, "po odczycie", 64, "", 160, 90000) == BWS_ZAS_OK);  // wypiera przeczytany
    CHECK(k.usun(l.wpisy[0].id) == BWS_ZAS_OK && k.liczba() == BWS_POW_MAKS - 1);
    k.przeczytaj_wszystkie(); CHECK(k.nieprzeczytane() == 0);
    k.wyczysc(); CHECK(k.liczba() == 0); k.migawka(l); CHECK(l.liczba == 0 && l.nieprzeczytane == 0);
    // dlugie teksty: ograniczone, zawsze NUL
    std::string dlugi(1000, 'a');
    CHECK(k.dodaj_aplikacji(8, "w", BWS_POW_INFO, dlugi.c_str(), 1000, dlugi.c_str(), 1000, 100000) == BWS_ZAS_OK);
    k.migawka(l); CHECK(std::strlen(l.wpisy[0].tytul) == BWS_POW_TYTUL - 1 && std::strlen(l.wpisy[0].tresc) == BWS_POW_TRESC - 1);
}

// ---------------------------------------------------------------- 6. glosnosc HDA
static void glosnosc() {
    using namespace HdaGlosnosc;
    Wzmacniacz w; w.jest = true; w.caps = QEMU_AMP_CAPS; w.gain_ref = 0x4A; w.nid = 2;
    CHECK(w.regulowany() && w.wyciszanie() && kroki(w.caps) == 0x4A);
    Stan s;
    CHECK(s.procent == 100 && !s.wyciszony);
    CHECK(krok(w, 100) == 0x4A && krok(w, 0) == 0 && krok(w, 50) == 37 && krok(w, 200) == 0x4A);   // 100 % = dotychczasowe
    for (uint32_t p = 1; p <= 100; ++p) CHECK(krok(w, p) >= krok(w, p - 1));                         // monotonicznie
    CHECK(payload(w, s) == (AMP_OUT_LR | 0x4A));
    s.wyciszony = true; CHECK(payload(w, s) == (AMP_OUT_LR | AMP_MUTE | 0x4A));                  // wyciszenie = bit Mute, poziom zachowany
    s.wyciszony = false; s.procent = 0; CHECK(payload(w, s) == (AMP_OUT_LR | AMP_MUTE));
    // konserwatywne wzmocnienie topologii (-18 dB): 100 % = ten sam krok co wczesniej
    Wzmacniacz t; t.jest = true; t.caps = 0x80034A4A; t.gain_ref = 56;
    s.procent = 100; CHECK((payload(t, s) & 0x7F) == 56);
    // bez Mute Capable: wyciszenie niedostepne, poziom 0 = najnizszy krok bez bitu Mute
    Wzmacniacz b; b.jest = true; b.caps = 0x00034A4A; b.gain_ref = 40;
    CHECK(b.regulowany() && !b.wyciszanie());
    s.procent = 0; CHECK(payload(b, s) == AMP_OUT_LR);
    // bez krokow: brak regulacji sprzetowej (backend programowy)
    Wzmacniacz z; z.jest = true; z.caps = 0; z.gain_ref = 0; CHECK(!z.regulowany());
    CHECK(payload_staly(0x7F | 0x80) == (AMP_OUT_LR | 0x7F));
    // programowe wzmocnienie: nasycenie, wyciszenie, tozsamosc przy backendzie sprzetowym
    Stan p; p.procent = 50;
    CHECK(skaluj_probke(20000, p, true) == 10000 && skaluj_probke(-20000, p, true) == -10000);
    p.procent = 100; CHECK(skaluj_probke(40000, p, true) == 32767 && skaluj_probke(-40000, p, true) == -32768);
    p.procent = 1000; CHECK(skaluj_probke(30000, p, true) == 30000);                              // ograniczenie do 100 %
    p.procent = 30; p.wyciszony = true; CHECK(skaluj_probke(30000, p, true) == 0);
    CHECK(skaluj_probke(30000, p, false) == 30000 && skaluj_probke(99999, p, false) == 32767);    // bez podwojnego tlumienia
}

// ---------------------------------------------------------------- 7. autoryzacja i odpowiedzi jadra
static void jadro() {
    using PopupAutoryzacja::Wywolujacy;
    const char wm[64] = "/menedzer_okien.bur", wm2[64] = "/programy/menedzer_okien.cebula/menedzer_okien.bur";
    const char ust[64] = "/programy/ustawienia.cebula/ustawienia.bur";
    const uint64_t sys = PRAWO_GUI | PRAWO_SYSTEM_CONFIG;
    CHECK(ZasobnikJadro::menedzer_okien(Wywolujacy{sys, PZB_ZAUFANE, wm, 64, false}));
    CHECK(ZasobnikJadro::menedzer_okien(Wywolujacy{sys, PZB_ZAUFANE, wm2, 64, false}));
    CHECK(!ZasobnikJadro::menedzer_okien(Wywolujacy{sys, PZB_ZAUFANE, ust, 64, false}));      // te same prawa, inna sciezka
    CHECK(!ZasobnikJadro::menedzer_okien(Wywolujacy{PRAWO_GUI, PZB_ZAUFANE, wm, 64, false})); // sciezka bez praw
    CHECK(!ZasobnikJadro::menedzer_okien(Wywolujacy{sys, PZB_UZYTKOWNIK, wm, 64, false}));
    CHECK(ZasobnikJadro::moze_zglaszac(Wywolujacy{PRAWO_GUI, PZB_UZYTKOWNIK, ust, 64, false}));
    CHECK(!ZasobnikJadro::moze_zglaszac(Wywolujacy{0, PZB_UZYTKOWNIK, ust, 64, false}));
    CHECK(!ZasobnikJadro::bufor_poprawny(0, 4096, 10) && !ZasobnikJadro::bufor_poprawny(0x1000, 9, 10) && ZasobnikJadro::bufor_poprawny(0x1000, 10, 10));
    // dzwiek
    BwsZasDzwiekV1 d{};
    HdaGlosnoscInfo h{}; h.procent = 70; ZasobnikJadro::dzwiek(d, h);
    CHECK(d.flagi == 0 && d.urzadzenie[0] == 0);                                                     // brak urzadzenia
    h.urzadzenie = true; h.backend = HDA_GLOSNOSC_WZMACNIACZ; h.wyciszanie = true; h.pci_vendor = 0x8086; h.cel = 1; h.kroki = 74; h.wezel = 2;
    ZasobnikJadro::dzwiek(d, h);
    CHECK(d.flagi == (BWS_DZW_URZADZENIE | BWS_DZW_REGULACJA | BWS_DZW_WYCISZANIE) && d.backend == BWS_DZW_BACKEND_WZMACNIACZ &&
          d.glosnosc == 70 && !std::strcmp(d.urzadzenie, "Intel HDA · głośnik") && d.kroki == 74);
    h.pci_vendor = 0x10DE; h.cel = 3; ZasobnikJadro::dzwiek(d, h); CHECK(!std::strcmp(d.urzadzenie, "NVIDIA HDA · wyjście liniowe"));
    h.backend = HDA_GLOSNOSC_BRAK; ZasobnikJadro::dzwiek(d, h); CHECK(!(d.flagi & BWS_DZW_REGULACJA));
    h.procent = 500; ZasobnikJadro::dzwiek(d, h); CHECK(d.glosnosc == 100);
    // siec IPv4
    BwsZasSiecV1 n{};
    const uint8_t ip[4] = {10, 0, 2, 15}, m[4] = {255, 255, 255, 0}, gw[4] = {10, 0, 2, 2}, zero[4] = {};
    ZasobnikJadro::ipv4(n, false, ip, m, gw, gw); CHECK(n.flagi == 0 && n.ip[0] == 0);              // brak DHCP: nic
    ZasobnikJadro::ipv4(n, true, ip, m, gw, zero);
    CHECK((n.flagi & BWS_ZSIEC_IPV4) && (n.flagi & BWS_ZSIEC_BRAMA) && !(n.flagi & BWS_ZSIEC_DNS) && n.ip[3] == 15);
    // nosniki: tylko wymienne, bezpieczne usuwanie nigdy
    BwsDyskInfoV1 dysk{}; dysk.flagi = BWS_DYSK_SYSTEMOWY; BwsZasNosnikV1 no{};
    CHECK(!ZasobnikJadro::nosnik(no, dysk));
    dysk.flagi = BWS_DYSK_WYMIENNY | BWS_DYSK_TYLKO_ODCZYT; std::memset(dysk.model, 'M', sizeof(dysk.model));
    CHECK(ZasobnikJadro::nosnik(no, dysk) && (no.flagi & BWS_NOS_TYLKO_ODCZYT) && !(no.flagi & BWS_NOS_BEZPIECZNE_USUWANIE) &&
          no.model[sizeof(no.model) - 1] == 0);
    // polecenia
    Powiadomienia::Kolejka k;
    CHECK(ZasobnikJadro::polecenie_powiadomien(k, 77, 0) == BWS_ZAS_ZLE_ARGUMENTY);
    CHECK(ZasobnikJadro::polecenie_powiadomien(k, BWS_ZAS_POL_USUN, 5) == BWS_ZAS_BRAK);
    CHECK(ZasobnikJadro::wynik_glosnosci(HDA_GLOSNOSC_OK) == BWS_ZAS_OK && ZasobnikJadro::wynik_glosnosci(HDA_GLOSNOSC_ZAJETE) == BWS_ZAS_ZAJETE &&
          ZasobnikJadro::wynik_glosnosci(HDA_GLOSNOSC_NIEOBSLUGIWANE) == BWS_ZAS_NIEOBSLUGIWANE);
    // ABI
    CHECK(BWS_ZASOBNIK_STAN == 107 && BWS_ZASOBNIK_POLECENIE == 108 && BWS_POWIADOMIENIA == 109 && BWS_PIERWSZY_WOLNY == 112 && BWS_INFORMACJA_MAGAZYNU == 111);
    CHECK(sizeof(BwsZasobnikStanV1) % 8 == 0 && sizeof(BwsPowiadomieniaListaV1) == 24 + BWS_POW_MAKS * sizeof(BwsPowiadomienieV1));
}

// ---------------------------------------------------------------- 8. kontroler z atrapa jadra
struct Atrapa {
    bool urzadzenie = true, regulacja = true, mute_cap = true, zajete = false, wm = true;
    uint32_t backend = HDA_GLOSNOSC_WZMACNIACZ, glosnosc = 100, generacja = 0;
    bool wyciszony = false, ipv4 = true, lacze_znane = true, lacze = true, karta = true;
    uint32_t nosnikow = 0;
    uint64_t rx = 0, tx = 0, czas_ns = 1000000000ULL;
    Powiadomienia::Kolejka kolejka;
    int stanow = 0, polecen = 0, list = 0;
    std::string motyw = "bursztynnocny";
} J;
static uint64_t atrapa(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t) {
    if (nr == BWS_ZASOBNIK_STAN) {
        ++J.stanow;
        if (!J.wm) return BWS_ZAS_BRAK_UPRAWNIEN;
        if (a3 != BWS_ZASOBNIK_V1) return BWS_ZAS_ZLA_WERSJA;
        if (!a1 || a2 < sizeof(BwsZasobnikStanV1)) return BWS_ZAS_ZLY_BUFOR;
        BwsZasobnikStanV1 s{};
        s.wersja = uint32_t(BWS_ZASOBNIK_V1); s.rozmiar = sizeof(s);
        HdaGlosnoscInfo h{};
        h.urzadzenie = J.urzadzenie; h.backend = J.regulacja ? J.backend : HDA_GLOSNOSC_BRAK; h.wyciszanie = J.mute_cap;
        h.wyciszony = J.wyciszony; h.procent = J.glosnosc; h.generacja = J.generacja; h.pci_vendor = 0x8086; h.cel = 1;
        ZasobnikJadro::dzwiek(s.dzwiek, h);
        const uint8_t ip[4] = {10, 0, 2, 15}, m[4] = {255, 255, 255, 0}, gw[4] = {10, 0, 2, 2}, dns[4] = {10, 0, 2, 3};
        ZasobnikJadro::ipv4(s.siec, J.ipv4, ip, m, gw, dns);
        if (J.karta) {
            s.siec.flagi |= BWS_ZSIEC_INTERFEJS; s.siec.liczba_interfejsow = 1;
            BwsSiecInfoV1& i = s.siec.interfejs;
            i.id = 1; i.typ = BWS_SIEC_VIRTIO; std::strcpy(i.nazwa, "virtio-net0");
            i.flagi = BWS_SIEC_AKTYWNA | BWS_SIEC_GLOWNA | (J.lacze_znane ? BWS_SIEC_LACZE_ZNANE : 0) | (J.lacze ? BWS_SIEC_LACZE : 0);
            i.rx_bajty = J.rx; i.tx_bajty = J.tx;
        }
        s.siec.czas_ns = J.czas_ns;
        for (uint32_t k = 0; k < J.nosnikow && k < BWS_ZAS_MAKS_NOSNIKOW; ++k) {
            BwsDyskInfoV1 d{}; d.id = k; d.flagi = BWS_DYSK_WYMIENNY; d.sektory = 2048; d.rozmiar_sektora = 512;
            ZasobnikJadro::nosnik(s.nosniki[s.liczba_nosnikow++], d);
        }
        s.powiadomienia_liczba = J.kolejka.liczba(); s.powiadomienia_nieprzeczytane = J.kolejka.nieprzeczytane();
        s.powiadomienia_generacja = J.kolejka.generacja();
        std::memcpy(reinterpret_cast<void*>(a1), &s, sizeof(s));
        return BWS_ZAS_OK;
    }
    if (nr == BWS_ZASOBNIK_POLECENIE) {
        ++J.polecen;
        if (a1 == BWS_ZAS_POL_GLOSNOSC || a1 == BWS_ZAS_POL_WYCISZENIE) {
            if (!J.urzadzenie || !J.regulacja) return BWS_ZAS_NIEOBSLUGIWANE;
            if (J.zajete) return BWS_ZAS_ZAJETE;
            if (a1 == BWS_ZAS_POL_GLOSNOSC) { if (a2 > 100) return BWS_ZAS_ZLE_ARGUMENTY; J.glosnosc = uint32_t(a2); }
            else { if (!J.mute_cap) return BWS_ZAS_NIEOBSLUGIWANE; J.wyciszony = a2 == 1; }
            ++J.generacja;
            return BWS_ZAS_OK;
        }
        return ZasobnikJadro::polecenie_powiadomien(J.kolejka, a1, a2);
    }
    if (nr == BWS_POWIADOMIENIA && a1 == BWS_POW_POL_POBIERZ) {
        ++J.list;
        if (!a2 || a3 < sizeof(BwsPowiadomieniaListaV1)) return BWS_ZAS_ZLY_BUFOR;
        J.kolejka.migawka(*reinterpret_cast<BwsPowiadomieniaListaV1*>(a2));
        return BWS_ZAS_OK;
    }
    if (nr == BWS_MOTYW_POLECENIE && a1 == MOTYW_CMD_USTAW) { J.motyw = reinterpret_cast<const char*>(a2); return 1; }
    return 0;
}

static bws_zdarzenie zd(uint32_t typ, int x, int y) { bws_zdarzenie e{}; e.typ = typ; e.x = x; e.y = y; return e; }
static Prostokat srodek_ikony(Kontroler& k, Id id) { return k.uklad.prostokat(id); }

static void kontroler() {
    J = Atrapa{};
    std::vector<Akcja> akcje; int przed = 0;
    {
        Kontroler k;
        k.bws = atrapa;
        k.dane_wm = &akcje;
        k.uruchom = [](Akcja a, void* p) { static_cast<std::vector<Akcja>*>(p)->push_back(a); };
        static int* licznik = nullptr; licznik = &przed;
        k.przed_otwarciem = [](void*) { ++*licznik; };
        const int W = 1280, H = 720, PY = 680, PH = 40, ZX = 1130;
        k.konfiguruj(W, H, ZX, PY, PH, 500, SkalaUI{1});
        CHECK(k.uklad.liczba == 4 && k.poczatek() < ZX);
        // odczyt co 2 s (bez panelu), natychmiast z wymus
        CHECK(k.odswiez(1000000000ULL) && J.stanow == 1);
        CHECK(!k.odswiez(1500000000ULL) && J.stanow == 1);
        k.odswiez(3100000000ULL); CHECK(J.stanow == 2);
        CHECK(!std::strcmp(k.rejestr.znajdz(Id::Dzwiek)->dymek, "Głośność: 100 %"));
        // klik ikony: panel Głośność nad ikona, w ekranie; drugi klik tej samej ikony zamyka
        const Prostokat ik = srodek_ikony(k, Id::Dzwiek);
        CHECK(k.klik(ik.x + 5, ik.y + 5) && k.otwarty() && k.aktywny() == Id::Dzwiek && przed == 1);
        auto* pg = static_cast<PanelGlosnosci*>(k.panel(Id::Dzwiek));
        CHECK(pg->obszar().y + pg->obszar().wysokosc <= PY && pg->obszar().x >= 0 && pg->obszar().prawy() <= W);
        CHECK(!std::strcmp(pg->tytul()->tekst(), "Głośność") && !std::strcmp(pg->urzadzenie()->tekst(), "Intel HDA · głośnik"));
        CHECK(pg->suwak()->czy_wlaczona() && pg->suwak()->wartosc() == 100 && pg->wycisz()->czy_wlaczona());
        // suwak -> jadro -> wartosc z jadra (ikona i dymek zsynchronizowane)
        pg->suwak()->ustaw_wartosc(40);
        CHECK(J.glosnosc == 40 && pg->suwak()->wartosc() == 40 && !std::strcmp(k.rejestr.znajdz(Id::Dzwiek)->dymek, "Głośność: 40 %"));
        CHECK(k.rejestr.znajdz(Id::Dzwiek)->stan == STAN_POZIOM_SREDNI);
        // wyciszenie i przywrocenie poprzedniego poziomu
        nacisnij(pg->wycisz());
        CHECK(J.wyciszony && J.glosnosc == 40 && !std::strcmp(pg->wycisz()->tekst(), "Włącz dźwięk") && k.rejestr.znajdz(Id::Dzwiek)->stan == STAN_WYCISZONY);
        nacisnij(pg->wycisz());
        CHECK(!J.wyciszony && J.glosnosc == 40 && !std::strcmp(pg->wycisz()->tekst(), "Wycisz"));
        // zajety sprzet: stan bez zmian, suwak wraca do wartosci jadra, komunikat
        J.zajete = true;
        pg->suwak()->ustaw_wartosc(90);
        CHECK(J.glosnosc == 40 && pg->suwak()->wartosc() == 40 && std::strstr(pg->komunikat()->tekst(), "zajęte"));
        J.zajete = false;
        // zmiana z innego komponentu: panel i ikona nadazaja przy kolejnym odczycie (1 s przy otwartym panelu)
        J.glosnosc = 75; ++J.generacja;
        k.odswiez(5000000000ULL);
        CHECK(pg->suwak()->wartosc() == 75 && J.glosnosc == 75 && !std::strcmp(k.rejestr.znajdz(Id::Dzwiek)->dymek, "Głośność: 75 %"));
        // TIMER nigdy nie jest pochlaniany; ruch w panelu tak
        CHECK(!k.obsluz(zd(BWS_ZDARZENIE_TIMER, 0, 0)) && k.otwarty());
        const Prostokat po = pg->obszar();
        CHECK(k.obsluz(zd(BWS_ZDARZENIE_MYSZ_RUCH, po.x + 5, po.y + 5)) && k.otwarty());
        // klik w ikone tego samego panelu przy otwartym panelu: pochloniety, panel zamkniety
        CHECK(k.obsluz(zd(BWS_ZDARZENIE_MYSZ_DOWN, ik.x + 3, ik.y + 3)) && !k.otwarty());
        // przelaczanie: Sieć -> Powiadomienia zamyka poprzedni; klik w inna ikone przechodzi dalej
        const Prostokat is = srodek_ikony(k, Id::Siec), ip = srodek_ikony(k, Id::Powiadomienia);
        CHECK(k.klik(is.x + 2, is.y + 2) && k.aktywny() == Id::Siec);
        CHECK(!k.obsluz(zd(BWS_ZDARZENIE_MYSZ_DOWN, ip.x + 2, ip.y + 2)) && !k.otwarty());      // zamyka, nie pochlania
        CHECK(k.klik(ip.x + 2, ip.y + 2) && k.aktywny() == Id::Powiadomienia);                    // WM: ten sam klik otwiera
        CHECK(!k.panel(Id::Siec)->czy_otwarta());
        // klik poza panelem i zasobnikiem (np. przycisk Start): zamyka i przechodzi dalej
        CHECK(!k.obsluz(zd(BWS_ZDARZENIE_MYSZ_DOWN, 10, PY + 10)) && !k.otwarty());
        // Escape zamyka aktywny panel
        k.klik(is.x + 2, is.y + 2);
        bws_zdarzenie esc{}; esc.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY; esc.kod = BWS_KEY_SOURCE_USB | 41; esc.x = 1;
        CHECK(k.obsluz(esc) && !k.otwarty() && k.aktywny() == Id::Brak);
        // panel Sieć: tylko prawdziwe dane, tempo z dwoch migawek, "Niedostępne" bez konfiguracji
        J.rx = 1000000; J.tx = 2000; J.czas_ns = 2000000000ULL;
        k.klik(is.x + 2, is.y + 2);
        auto* ps = static_cast<PanelSieci*>(k.panel(Id::Siec));
        CHECK(!std::strcmp(ps->wartosc(0)->tekst(), "virtio-net0") && !std::strcmp(ps->wartosc(1)->tekst(), "VirtIO-Net"));
        CHECK(!std::strcmp(ps->wartosc(2)->tekst(), "Połączono") && !std::strcmp(ps->wartosc(3)->tekst(), "10.0.2.15 (DHCP)"));
        CHECK(!std::strcmp(ps->wartosc(4)->tekst(), "255.255.255.0") && !std::strcmp(ps->wartosc(5)->tekst(), "10.0.2.2") &&
              !std::strcmp(ps->wartosc(6)->tekst(), "10.0.2.3"));
        J.rx = 1500000; J.tx = 2000; J.czas_ns = 3000000000ULL;
        k.odswiez(9000000000ULL);
        CHECK(!std::strcmp(ps->wartosc(7)->tekst(), "1,5 MB · 500,0 kB/s") && !std::strcmp(ps->wartosc(8)->tekst(), "2,0 kB · 0 B/s"));
        J.ipv4 = false; J.lacze_znane = false;
        k.odswiez(11000000000ULL);
        CHECK(!std::strcmp(ps->wartosc(3)->tekst(), "Niedostępne") && !std::strcmp(ps->wartosc(5)->tekst(), "Niedostępne") &&
              !std::strcmp(ps->wartosc(2)->tekst(), "Nieznany"));
        J.karta = false;
        k.odswiez(13000000000ULL);
        CHECK(!std::strcmp(ps->wartosc(0)->tekst(), "Brak karty sieciowej") && !std::strcmp(ps->wartosc(7)->tekst(), "Niedostępne"));
        J.karta = true; J.ipv4 = true; J.lacze_znane = true;
        k.zamknij();
        // powiadomienia: prawdziwe zdarzenia z kolejki jadra, zaufane zrodlo, usuwanie, czyszczenie, odczytanie
        k.klik(ip.x + 2, ip.y + 2);
        auto* pp = static_cast<PanelPowiadomien*>(k.panel(Id::Powiadomienia));
        CHECK(!std::strcmp(pp->brak()->tekst(), "Brak powiadomień") && !pp->przycisk_wyczysc()->czy_wlaczona());
        k.zamknij();
        J.kolejka.dodaj_systemowe(BWS_POW_SUKCES, "Sieć skonfigurowana", "Adres IPv4 przydzielony przez DHCP.", 1000);
        J.kolejka.dodaj_aplikacji(5, "system", BWS_POW_INFO, "System Bursztyn OS: aktualizacja!", 64, "fałszywy komunikat", 160, 2000);
        k.odswiez(15000000000ULL);
        CHECK(k.rejestr.znajdz(Id::Powiadomienia)->stan == STAN_NIEPRZECZYTANE);
        k.klik(ip.x + 2, ip.y + 2);
        CHECK(!pp->brak()->czy_widoczna() && std::strstr(pp->naglowek(0)->tekst(), "Aplikacja: system"));
        CHECK(!pp->naglowek(2)->czy_widoczna() && !pp->przycisk_usun(3)->czy_widoczna());       // puste miejsca nie zajmuja wysokosci
        const int h2 = pp->obszar().wysokosc;
        CHECK(pp->obszar().dolny() <= PY);
        CHECK(!std::strncmp(pp->naglowek(1)->tekst(), "System · ", 9) && !std::strcmp(pp->tytul_wpisu(1)->tekst(), "Sieć skonfigurowana"));
        CHECK(pp->przycisk_przeczytane()->czy_wlaczona());
        nacisnij(pp->przycisk_usun(0));
        CHECK(J.kolejka.liczba() == 1 && !std::strcmp(pp->tytul_wpisu(0)->tekst(), "Sieć skonfigurowana"));
        CHECK(pp->obszar().wysokosc < h2 && pp->obszar().dolny() <= PY && pp->czy_otwarta());    // panel krotszy, nadal nad paskiem
        k.zamknij();                                                                             // obejrzane = przeczytane
        CHECK(J.kolejka.nieprzeczytane() == 0 && k.rejestr.znajdz(Id::Powiadomienia)->stan == 0);
        k.klik(ip.x + 2, ip.y + 2); nacisnij(pp->przycisk_wyczysc());
        CHECK(J.kolejka.liczba() == 0 && !std::strcmp(pp->brak()->tekst(), "Brak powiadomień"));
        k.zamknij();
        // szybkie ustawienia: glosnosc wspolna z panelem, motyw przez istniejace API, aplikacje przez WM, jasnosc niedostepna
        const Prostokat iq = srodek_ikony(k, Id::SzybkieUstawienia);
        k.klik(iq.x + 2, iq.y + 2);
        auto* pq = static_cast<PanelSzybkichUstawien*>(k.panel(Id::SzybkieUstawienia));
        CHECK(!std::strcmp(pq->tytul()->tekst(), "Szybkie ustawienia") && pq->suwak()->wartosc() == 75);
        CHECK(std::strstr(pq->jasnosc()->tekst(), "niedostępna"));
        pq->suwak()->ustaw_wartosc(30); CHECK(J.glosnosc == 30);
        pq->przelacznik()->ustaw_wartosc(true); CHECK(J.motyw == "bursztynjasny");
        nacisnij(pq->przycisk_siec()); CHECK(k.aktywny() == Id::Siec && !pq->czy_otwarta());       // centrum: otwiera inny panel
        k.zamknij(); k.klik(iq.x + 2, iq.y + 2);
        nacisnij(pq->przycisk_ustawienia()); CHECK(!k.otwarty() && akcje.size() == 1 && akcje[0] == Akcja::Ustawienia);
        k.klik(iq.x + 2, iq.y + 2); nacisnij(pq->przycisk_ekran()); CHECK(akcje.size() == 2 && akcje[1] == Akcja::UstawieniaEkranu);
        // brak backendu glosnosci: kontrolki wylaczone, polecenia odrzucone przez jadro, bez udawanej wartosci
        J.regulacja = false;
        k.odswiez(20000000000ULL, true);
        k.klik(ik.x + 2, ik.y + 2);
        CHECK(!pg->suwak()->czy_wlaczona() && pg->suwak()->wartosc() == 0 && std::strstr(k.rejestr.znajdz(Id::Dzwiek)->dymek, "niedostępna"));
        k.ustaw_glosnosc(50); CHECK(J.glosnosc == 30 && std::strstr(pg->komunikat()->tekst(), "nieobsługiwana"));
        J.urzadzenie = false; k.odswiez(21000000000ULL, true);
        CHECK(!std::strcmp(pg->urzadzenie()->tekst(), "Brak urządzenia audio") && !pg->wycisz()->czy_wlaczona());
        k.zamknij();
        J.urzadzenie = true; J.regulacja = true;
        // nosnik wymienny: element i panel tylko z urzadzeniem; zniknięcie zamyka panel (bez wiszacej kotwicy)
        J.nosnikow = 1;
        k.odswiez(22000000000ULL, true);
        CHECK(k.rejestr.znajdz(Id::Nosniki) && k.uklad.liczba == 5);
        const Prostokat in = srodek_ikony(k, Id::Nosniki);
        CHECK(k.klik(in.x + 2, in.y + 2) && k.aktywny() == Id::Nosniki);
        J.nosnikow = 0;
        k.odswiez(23000000000ULL, true);
        CHECK(!k.rejestr.znajdz(Id::Nosniki) && !k.otwarty() && k.uklad.liczba == 4);
        // brak uprawnien / niepoprawna odpowiedz: wszystko niedostepne
        J.wm = false; k.odswiez(24000000000ULL, true);
        CHECK(!k.dane.ok && (k.rejestr.znajdz(Id::Dzwiek)->stan & STAN_NIEDOSTEPNY));
        J.wm = true; k.odswiez(25000000000ULL, true); CHECK(k.dane.ok);
        // powtarzane otwieranie/zamykanie: bez wzrostu alokacji (panele tworzone raz, ASan pilnuje wyciekow)
        const Prostokat ikn = srodek_ikony(k, Id::Dzwiek);
        k.klik(ikn.x + 2, ikn.y + 2); k.zamknij();
        const size_t a0 = g_alokacje;
        for (int i = 0; i < 200; ++i) {
            const Id cel = i % 3 == 0 ? Id::Dzwiek : i % 3 == 1 ? Id::Siec : Id::SzybkieUstawienia;
            const Prostokat b = srodek_ikony(k, cel);
            CHECK(k.klik(b.x + 2, b.y + 2) && k.aktywny() == cel);
            CHECK(!k.obsluz(zd(BWS_ZDARZENIE_TIMER, 0, 0)));
            k.zamknij();
        }
        CHECK(g_alokacje - a0 < 50);
        // najechanie zmienia wyglad tylko przy zmianie elementu
        CHECK(k.najechanie(ikn.x + 1, ikn.y + 1) && !k.najechanie(ikn.x + 2, ikn.y + 2) && k.najechanie(-1, -1));
        // rozne skale i rozdzielczosci: elementy w obrebie paska, panel w ekranie
        for (int skala : {1, 2}) for (int w : {800, 1366, 1920}) {
            const SkalaUI s{uint32_t(skala)};
            const int ph = 40 * skala, py = 900 - ph, zx = w - 150 * skala;
            k.konfiguruj(w, 900, zx, py, ph, w / 3, s);
            for (int i = 0; i < k.uklad.liczba; ++i) {
                const Prostokat b = k.uklad.prostokaty[i];
                CHECK(b.prawy() <= zx && b.y >= py && b.y + b.wysokosc <= py + ph);
                CHECK(k.klik(b.x + 2, b.y + 2));
                Zasobnik::Panel* p = k.panel(k.uklad.sloty[i]);
                CHECK(p->czy_otwarta() && p->obszar().x >= 0 && p->obszar().prawy() <= w && p->obszar().y + p->obszar().wysokosc <= py);
                k.zamknij();
            }
        }
    }
}

int main() {
    rejestr();
    uklad();
    glify();
    model();
    powiadomienia();
    glosnosc();
    jadro();
    kontroler();
    std::printf("test-zasobnik-host: %u sprawdzen OK\n", checks);
    return 0;
}
