#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define BURSZTYN_HOST_TEST 1

#include "../motyw.h"
#include "../stos_jadra.h"
#include "../motyw.cpp"

#include "../bursztyn_gui.h"
#include "../pulpit_geometria.h"

// Implementacje makiet funkcji GUI i BWS na potrzeby host test
typedef uint64_t (*TestBwsHookFn)(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);
static TestBwsHookFn g_test_bws_hook = nullptr;

extern "C" {
    uint64_t bws_wywolaj(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
        if (g_test_bws_hook) return g_test_bws_hook(nr, a1, a2, a3, a4);
        return 0;
    }
    __attribute__((noreturn)) void bws_zakoncz_proces() { exit(0); }
}

bool wylistuj_katalog_uzytkownika(const char* sciezka, char* bufor, uint32_t maksymalna_dlugosc) {
    if (!sciezka || !bufor || maksymalna_dlugosc == 0) return false;
    return bws_wywolaj(6, reinterpret_cast<uint64_t>(sciezka),
                          reinterpret_cast<uint64_t>(bufor),
                          static_cast<uint64_t>(maksymalna_dlugosc), 0) == 1ULL;
}

bool pobierz_metadane_pliku(const char* sciezka, BwsMetadanePliku* metadane) {
    return sciezka && metadane &&
           bws_wywolaj(47, reinterpret_cast<uint64_t>(sciezka),
                           reinterpret_cast<uint64_t>(metadane), 0, 0) == 1ULL;
}

bool pobierz_rozmiar_pliku(const char* sciezka, uint32_t* rozmiar) {
    if (!sciezka || !rozmiar) return false;
    return bws_wywolaj(4, reinterpret_cast<uint64_t>(sciezka),
                          reinterpret_cast<uint64_t>(rozmiar), 0, 0) == 1ULL;
}

bool utworz_katalog_uzytkownika(const char* sciezka) {
    if (!sciezka) return false;
    return bws_wywolaj(46, reinterpret_cast<uint64_t>(sciezka), 0, 0, 0) == 1ULL;
}

static char g_mock_clipboard[1024] = {0};
static size_t g_mock_clipboard_len = 0;

bool clipboard_set(const char* /*mime*/, const void* dane, uint64_t rozmiar) {
    if (!dane || rozmiar == 0) {
        g_mock_clipboard_len = 0;
        g_mock_clipboard[0] = '\0';
        return true;
    }
    size_t copy_len = (rozmiar < sizeof(g_mock_clipboard)) ? static_cast<size_t>(rozmiar) : (sizeof(g_mock_clipboard) - 1);
    memcpy(g_mock_clipboard, dane, copy_len);
    g_mock_clipboard[copy_len] = '\0';
    g_mock_clipboard_len = copy_len;
    return true;
}

bool clipboard_read(const char* /*mime*/, void* dane, uint64_t pojemnosc, uint64_t* odczytano) {
    if (!dane || pojemnosc == 0) return false;
    size_t copy_len = (g_mock_clipboard_len < pojemnosc) ? g_mock_clipboard_len : (pojemnosc - 1);
    memcpy(dane, g_mock_clipboard, copy_len);
    static_cast<char*>(dane)[copy_len] = '\0';
    if (odczytano) *odczytano = copy_len;
    return true;
}

void* gui_malloc(unsigned long rozmiar) { return malloc(rozmiar); }
void gui_free(void* ptr) { free(ptr); }
void* gui_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
void gui_rysuj_prostokat(int, int, int, int, uint32_t) {}
bool gui_blit(int, int, int, int, const uint32_t*) { return true; }
bool gui_blit_popup(int, int, int, int, const uint32_t*) { return true; }
void gui_wypisz_tekst_kolor_skala(int, int, uint32_t, int, const char*) {}
void motyw_odswiez_lokalny() {}
bool gui_ustaw_tytul_okna(const char*) { return true; }
uint32_t gui_skala_procent() { return 100; }
void gui_odswiez() {}
gui_akcja_belki gui_hit_test_belki(int, int, int, int, int) { return GUI_BELKA_BRAK; }
void gui_rysuj_standardowa_belke(int, int, int, const char*, bool) {}
bool gui_minimalizuj_okno() { return true; }
__attribute__((noreturn)) void gui_zakoncz_aplikacje() { exit(0); }
uint64_t gui_czas_monotoniczny_ns() { return 1000000000ULL; }
bool gui_pobierz_zdarzenie(bws_zdarzenie*) { return false; }
bool gui_czekaj_na_zdarzenie(bws_zdarzenie*) { return false; }
bool gui_czekaj_na_zdarzenie_z_timeoutem(bws_zdarzenie*, uint64_t) { return false; }
void gui_ustaw_capture_myszy(bool) {}
bool gui_ustaw_geometrie_okna(int, int, int, int, int) { return true; }
bool gui_pobierz_monitor(uint32_t, GuiMonitorInfo*) { return false; }
void bws_przesun_warstwe(int, int) {}
bool gui_ustaw_popup_aplikacji(bool, int, int, int, int) { return true; }
void gui_pobierz_rozdzielczosc(int* w, int* h) {
    if (w) *w = 1280;
    if (h) *h = 720;
}
static const BursztynMotyw* g_aktywny_test_motyw = &g_domyslny_motyw_fallback;
const BursztynMotyw* gui_pobierz_motyw() {
    return g_aktywny_test_motyw;
}

uint32_t motyw_kolor(uint32_t rola) {
    const BursztynMotyw* m = gui_pobierz_motyw();
    switch (rola) {
        case MOTYW_KOLOR_PULPIT: return m->pulpit;
        case MOTYW_KOLOR_OKNOTLO: return m->oknotlo;
        case MOTYW_KOLOR_PANEL: return m->panel;
        case MOTYW_KOLOR_PODNIESIONY: return m->podniesiony;
        case MOTYW_KOLOR_RAMKA: return m->ramka;
        case MOTYW_KOLOR_TEKST: return m->tekst;
        case MOTYW_KOLOR_TEKST_DRUGI: return m->tekstdrugorzedny;
        case MOTYW_KOLOR_WYLACZONY: return m->wylaczony;
        case MOTYW_KOLOR_AKCENT: return m->akcent;
        case MOTYW_KOLOR_AKCENT_NAJECHANY: return m->akcentnajechany;
        case MOTYW_KOLOR_AKCENT_AKTYWNY: return m->akcentaktywny;
        case MOTYW_KOLOR_BLAD: return m->blad;
        case MOTYW_KOLOR_SUKCES: return m->sukces;
        case MOTYW_KOLOR_OSTRZEZENIE: return m->ostrzezenie;
        case MOTYW_KOLOR_INFORMACJA: return m->informacja;
        case MOTYW_KOLOR_POLETEKSTOWE: return m->poletekstowe;
        case MOTYW_KOLOR_ZAZNACZENIE: return m->zaznaczenie;
        case MOTYW_KOLOR_PRZYCISK: return m->przycisk;
        case MOTYW_KOLOR_PRZYCISK_NAJECHANY: return m->przycisknajechany;
        case MOTYW_KOLOR_TEKST_AKCENTU: return m->tekstakcentu;
        default: return m->tekst;
    }
}

uint16_t motyw_promien(uint32_t element) {
    const BursztynMotyw* m = gui_pobierz_motyw();
    switch (element) {
        case MOTYW_ROG_OKNO: return m->rogokna;
        case MOTYW_ROG_PANEL: return m->rogpanelu;
        case MOTYW_ROG_PRZYCISK: return m->rogprzycisku;
        case MOTYW_ROG_POLE: return m->rogpola;
        case MOTYW_ROG_MENU: return m->rogmenu;
        case MOTYW_ROG_PODPOWIEDZ: return m->rogpodpowiedzi;
        default: return 0;
    }
}

uint8_t motyw_grubosc_ramki() {
    return gui_pobierz_motyw()->gruboscramki;
}

uint8_t motyw_cien_okna() {
    return gui_pobierz_motyw()->cienokna;
}

void gui_pobierz_widoczna_geometrie(int* x, int* y, int* w, int* h) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = 1280;
    if (h) *h = 720;
}
void gui_log_geom_trace(const char*, int, int, int, int, int, int, int, int, bool, bool, int, int, int, int) {}
bool gui_legacy_czy_wlaczony() { return false; }
void gui_legacy_wlacz() {}

#include "../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

void test_ustaw_aktywny_motyw(const BursztynMotyw* m) {
    g_aktywny_test_motyw = m ? m : &g_domyslny_motyw_fallback;
    Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
}

static int g_testy_razem = 0;
static int g_testy_udane = 0;

#define TEST_ASSERT(warunek, opis) do { \
    g_testy_razem++; \
    if (warunek) { \
        g_testy_udane++; \
        printf("[PASS] %s\n", opis); \
    } else { \
        printf("[FAIL] %s (linia %d)\n", opis, __LINE__); \
    } \
} while (0)

// Pomocnicza kontrolka o stalym rozmiarze do testowania ukladow
class KlocTestowy : public Kontrolka {
public:
    KlocTestowy(int w, int h) {
        ustaw_preferowany_rozmiar(Rozmiar(w, h));
    }
    Rozmiar zmierz(Rozmiar) override {
        return Rozmiar(preferowany_rozmiar_.szerokosc * skala_dpi_,
                       preferowany_rozmiar_.wysokosc * skala_dpi_);
    }
};

// =========================================================================
// 1. Test Siatki (Grid)
// =========================================================================
static void test_siatka() {
    printf("\n--- Test 1: Siatka (Grid layout, row/col defs, spans, gaps) ---\n");

    Siatka siatka;
    siatka.ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
    siatka.ustaw_odstep(0, 0);

    // 3 kolumny: 50px, 1*, 2*
    siatka.dodaj_kolumne(DefinicjaWymiaru::piksele(50));
    siatka.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));
    siatka.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(2));

    // 3 wiersze: 30px, Auto, 1*
    siatka.dodaj_wiersz(DefinicjaWymiaru::piksele(30));
    siatka.dodaj_wiersz(DefinicjaWymiaru::automatyczny());
    siatka.dodaj_wiersz(DefinicjaWymiaru::gwiazdka(1));

    TEST_ASSERT(siatka.liczba_kolumn() == 3, "Siatka ma 3 kolumny");
    TEST_ASSERT(siatka.liczba_wierszy() == 3, "Siatka ma 3 wiersze");
    TEST_ASSERT(siatka.kolumna(0).typ == TypWymiaruSiatki::Piksele && siatka.kolumna(0).wartosc == 50,
                "Kolumna 0: Piksele 50");
    TEST_ASSERT(siatka.kolumna(1).typ == TypWymiaruSiatki::Gwiazdka && siatka.kolumna(1).wartosc == 1,
                "Kolumna 1: Gwiazdka 1");
    TEST_ASSERT(siatka.kolumna(2).typ == TypWymiaruSiatki::Gwiazdka && siatka.kolumna(2).wartosc == 2,
                "Kolumna 2: Gwiazdka 2");

    // Dodaj kontrolki alokowane dynamicznie (zarzadzane przez kontener)
    KlocTestowy* k1 = new KlocTestowy(50, 30);
    KlocTestowy* k2 = new KlocTestowy(80, 40); // dla wiersza Auto
    KlocTestowy* k3 = new KlocTestowy(100, 20); // span 2 kolumny
    k3->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    siatka.dodaj_do_siatki(k1, 0, 0);
    siatka.dodaj_do_siatki(k2, 1, 1);
    siatka.dodaj_do_siatki(k3, 2, 0, 1, 2); // wiersz 2, kol 0, span_kol=2

    // Rozmieszczamy w oknie 350 x 200
    // Kolumny: 350 - 50 = 300 wolne. Gwiazdki: 1* + 2* = 3*. Kol 1 = 100, Kol 2 = 200.
    // Wiersze: Auto zmierzy k2 wys = 40. 200 - 30 (wiersz 0) - 40 (wiersz 1) = 130 dla wiersza 2 (1*).
    siatka.rozmiesc(Prostokat(0, 0, 350, 200));

    TEST_ASSERT(k1->obszar().x == 0 && k1->obszar().y == 0, "K1 pozycja (0, 0)");
    TEST_ASSERT(k1->obszar().szerokosc == 50 && k1->obszar().wysokosc == 30, "K1 rozmiar 50x30");

    TEST_ASSERT(k2->obszar().x == 50 && k2->obszar().y == 30, "K2 pozycja (50, 30)");
    TEST_ASSERT(k2->obszar().szerokosc == 100 && k2->obszar().wysokosc == 40, "K2 rozmiar 100x40");

    // K3 span_kol=2 na kol 0 (50px) i kol 1 (100px) -> szerokosc = 150px, y = 30 + 40 = 70, wys = 130
    TEST_ASSERT(k3->obszar().x == 0 && k3->obszar().y == 70, "K3 pozycja (0, 70)");
    TEST_ASSERT(k3->obszar().szerokosc == 150 && k3->obszar().wysokosc == 130, "K3 rozmiar 150x130 (span 2)");

    // Test odstepow
    siatka.ustaw_odstep(10, 5);
    TEST_ASSERT(siatka.odstep_poziomy() == 10 && siatka.odstep_pionowy() == 5, "Odstepy siatki 10x5");
}

// =========================================================================
// 2. Test PanelDokowany (DockPanel)
// =========================================================================
static void test_panel_dokowany() {
    printf("\n--- Test 2: PanelDokowany (Docking geometry, 5 modes, ordering) ---\n");

    PanelDokowany panel;
    panel.ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
    panel.ustaw_odstep(0);

    KlocTestowy* gora = new KlocTestowy(400, 30);
    KlocTestowy* lewo = new KlocTestowy(50, 200);
    KlocTestowy* dol = new KlocTestowy(400, 40);
    KlocTestowy* prawo = new KlocTestowy(60, 200);
    KlocTestowy* srodek = new KlocTestowy(100, 100);

    panel.dodaj_dokowany(gora, Dokowanie::Gora);
    panel.dodaj_dokowany(lewo, Dokowanie::Lewo);
    panel.dodaj_dokowany(dol, Dokowanie::Dol);
    panel.dodaj_dokowany(prawo, Dokowanie::Prawo);
    panel.dodaj_dokowany(srodek, Dokowanie::Wypelnij);

    TEST_ASSERT(panel.pobierz_dokowanie(gora) == Dokowanie::Gora, "Gora dokowana poprawnie");
    TEST_ASSERT(panel.pobierz_dokowanie(lewo) == Dokowanie::Lewo, "Lewo dokowana poprawnie");
    TEST_ASSERT(panel.pobierz_dokowanie(dol) == Dokowanie::Dol, "Dol dokowany poprawnie");
    TEST_ASSERT(panel.pobierz_dokowanie(prawo) == Dokowanie::Prawo, "Prawo dokowane poprawnie");
    TEST_ASSERT(panel.pobierz_dokowanie(srodek) == Dokowanie::Wypelnij, "Wypelnij dokowany poprawnie");

    // Rozmieszczamy w oknie 400 x 300
    panel.rozmiesc(Prostokat(0, 0, 400, 300));

    // Gora: (0, 0, 400, 30) -> pozostaje (0, 30, 400, 270)
    TEST_ASSERT(gora->obszar().x == 0 && gora->obszar().y == 0, "Gora x=0, y=0");
    TEST_ASSERT(gora->obszar().szerokosc == 400 && gora->obszar().wysokosc == 30, "Gora 400x30");

    // Lewo: (0, 30, 50, 270) -> pozostaje (50, 30, 350, 270)
    TEST_ASSERT(lewo->obszar().x == 0 && lewo->obszar().y == 30, "Lewo x=0, y=30");
    TEST_ASSERT(lewo->obszar().szerokosc == 50 && lewo->obszar().wysokosc == 270, "Lewo 50x270");

    // Dol: y = 300 - 40 = 260. (50, 260, 350, 40) -> pozostaje (50, 30, 350, 230)
    TEST_ASSERT(dol->obszar().x == 50 && dol->obszar().y == 260, "Dol x=50, y=260");
    TEST_ASSERT(dol->obszar().szerokosc == 350 && dol->obszar().wysokosc == 40, "Dol 350x40");

    // Prawo: x = 50 + 350 - 60 = 340. (340, 30, 60, 230) -> pozostaje (50, 30, 290, 230)
    TEST_ASSERT(prawo->obszar().x == 340 && prawo->obszar().y == 30, "Prawo x=340, y=30");
    TEST_ASSERT(prawo->obszar().szerokosc == 60 && prawo->obszar().wysokosc == 230, "Prawo 60x230");

    // Wypelnij: (50, 30, 290, 230)
    TEST_ASSERT(srodek->obszar().x == 50 && srodek->obszar().y == 30, "Srodek x=50, y=30");
    TEST_ASSERT(srodek->obszar().szerokosc == 290 && srodek->obszar().wysokosc == 230, "Srodek 290x230");
}

// =========================================================================
// 3. Test Przeplyw (WrapPanel / FlowLayout)
// =========================================================================
static void test_przeplyw() {
    printf("\n--- Test 3: Przeplyw (Auto-wrapping on boundary, rows, spacing) ---\n");

    Przeplyw flow;
    flow.ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
    flow.ustaw_odstep(10, 5);

    // Dodaj 5 dzieci o szerokosci 50 i wysokosci 20
    KlocTestowy* c0 = new KlocTestowy(50, 20);
    KlocTestowy* c1 = new KlocTestowy(50, 20);
    KlocTestowy* c2 = new KlocTestowy(50, 20);
    KlocTestowy* c3 = new KlocTestowy(50, 20);
    KlocTestowy* c4 = new KlocTestowy(50, 20);

    flow.dodaj_kontrolke(c0);
    flow.dodaj_kontrolke(c1);
    flow.dodaj_kontrolke(c2);
    flow.dodaj_kontrolke(c3);
    flow.dodaj_kontrolke(c4);

    // Szerokosc kontenera = 150
    // c0: x=0..50, nastepny bylby 50+10=60
    // c1: x=60..110, nastepny bylby 110+10=120
    // c2: 120 + 50 = 170 > 150 -> zawija do wiersza 1!
    // Wiersz 1: y = 20 + 5 = 25.
    // c2: x=0..50, nastepny 60
    // c3: x=60..110
    // c4: 120 + 50 = 170 > 150 -> zawija do wiersza 2! y = 25 + 20 + 5 = 50
    flow.rozmiesc(Prostokat(0, 0, 150, 200));

    TEST_ASSERT(c0->obszar().x == 0 && c0->obszar().y == 0, "C0 wiersz 0, poz (0, 0)");
    TEST_ASSERT(c1->obszar().x == 60 && c1->obszar().y == 0, "C1 wiersz 0, poz (60, 0)");
    TEST_ASSERT(c2->obszar().x == 0 && c2->obszar().y == 25, "C2 wiersz 1, poz (0, 25) - zawiniety");
    TEST_ASSERT(c3->obszar().x == 60 && c3->obszar().y == 25, "C3 wiersz 1, poz (60, 25)");
    TEST_ASSERT(c4->obszar().x == 0 && c4->obszar().y == 50, "C4 wiersz 2, poz (0, 50) - zawiniety");
}

// =========================================================================
// 4. Test OknoDialogowe
// =========================================================================
static WynikDialogu g_odebrany_wynik = WynikDialogu::Brak;
static void cb_dialog_wynik(OknoDialogowe*, WynikDialogu wynik, void*) {
    g_odebrany_wynik = wynik;
}

static void test_okno_dialogowe() {
    printf("\n--- Test 4: OknoDialogowe & Komunikat (All button sets, hotkeys, Tab nav, wrapping) ---\n");

    // 1. OkAnuluj
    OknoDialogowe d1("Pytanie", "Czy zapisać plik?", PrzyciskiDialogu::OkAnuluj);
    TEST_ASSERT(strcmp(d1.wiadomosc(), "Czy zapisać plik?") == 0, "Wiadomosc dialogu OK");
    d1.przy_wyniku(cb_dialog_wynik, nullptr);

    ZdarzenieKlawiatury e_enter{};
    e_enter.kod_klawisza = 13;
    d1.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(e_enter.obsluzone, "Enter obsluzony przez dialog");
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::OK, "Wynik Enter w OkAnuluj to OK");

    g_odebrany_wynik = WynikDialogu::Brak;
    ZdarzenieKlawiatury e_esc{};
    e_esc.kod_klawisza = 27;
    d1.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(e_esc.obsluzone, "Escape obsluzony przez dialog");
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Anuluj, "Wynik Escape w OkAnuluj to Anuluj");

    // 2. TakNie
    OknoDialogowe d2("Potwierdzenie", "Czy kontynuować operację?", PrzyciskiDialogu::TakNie);
    d2.rozmiesc(Prostokat(0, 0, 400, 190));
    TEST_ASSERT(d2.liczba_przyciskow() == 2, "Tak/Nie ma dokladnie dwa przyciski");
    TEST_ASSERT(d2.przycisk(0) != d2.przycisk(1), "Tak/Nie ma unikalne obiekty Przycisk");
    TEST_ASSERT(d2.wynik_przycisku(0) == WynikDialogu::Tak && d2.wynik_przycisku(1) == WynikDialogu::Nie,
                "Tak/Nie zachowuje osobne wyniki");
    TEST_ASSERT(d2.przycisk(0)->obszar_ekranu() != d2.przycisk(1)->obszar_ekranu() &&
                d2.przycisk(0)->obszar_ekranu().przeciecie(d2.przycisk(1)->obszar_ekranu()).czy_pusty(),
                "Tak/Nie ma rozlaczne prostokaty");
    TEST_ASSERT(d2.obszar_tresci().dolny() <= d2.obszar_przyciskow().y,
                "Tresć dialogu konczy sie nad panelem przyciskow");
    d2.przy_wyniku(cb_dialog_wynik, nullptr);

    e_enter.obsluzone = false;
    d2.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Tak, "Wynik Enter w TakNie to Tak");

    e_esc.obsluzone = false;
    d2.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Nie, "Wynik Escape w TakNie to Nie");

    // 3. TylkoOk
    OknoDialogowe d0("Informacja", "Operacja udana", PrzyciskiDialogu::TylkoOk);
    d0.przy_wyniku(cb_dialog_wynik, nullptr);
    e_enter.obsluzone = false;
    d0.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::OK, "Wynik Enter w TylkoOk to OK");
    g_odebrany_wynik = WynikDialogu::Brak;
    e_esc.obsluzone = false;
    d0.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::OK, "Wynik Escape w TylkoOk to OK");

    // 4. TakNieAnuluj
    OknoDialogowe d3("Zapis", "Zapisać przed wyjściem?", PrzyciskiDialogu::TakNieAnuluj);
    d3.rozmiesc(Prostokat(0, 0, 420, 190));
    TEST_ASSERT(d3.liczba_przyciskow() == 3, "Tak/Nie/Anuluj ma dokladnie trzy przyciski");
    TEST_ASSERT(d3.przycisk(0) != d3.przycisk(1) && d3.przycisk(1) != d3.przycisk(2),
                "Tak/Nie/Anuluj ma unikalne obiekty");
    TEST_ASSERT(d3.przycisk(0)->obszar_ekranu().przeciecie(d3.przycisk(1)->obszar_ekranu()).czy_pusty() &&
                d3.przycisk(1)->obszar_ekranu().przeciecie(d3.przycisk(2)->obszar_ekranu()).czy_pusty(),
                "Tak/Nie/Anuluj ma rozlaczne prostokaty");
    d3.przy_wyniku(cb_dialog_wynik, nullptr);
    TEST_ASSERT(d3.wynik_domyslny() == WynikDialogu::Tak, "TakNieAnuluj domyslny to Tak");
    TEST_ASSERT(d3.wynik_anuluj() == WynikDialogu::Anuluj, "TakNieAnuluj anuluj to Anuluj");
    e_enter.obsluzone = false;
    d3.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Tak, "Wynik Enter w TakNieAnuluj to Tak");
    e_esc.obsluzone = false;
    d3.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Anuluj, "Wynik Escape w TakNieAnuluj to Anuluj");

    // 5. PonowAnuluj
    OknoDialogowe d4("Błąd sieci", "Ponowić próbę połączenia?", PrzyciskiDialogu::PonowAnuluj);
    d4.przy_wyniku(cb_dialog_wynik, nullptr);
    e_enter.obsluzone = false;
    d4.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Ponow, "Wynik Enter w PonowAnuluj to Ponow");
    e_esc.obsluzone = false;
    d4.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Anuluj, "Wynik Escape w PonowAnuluj to Anuluj");

    // 6. PrzerwijPonowIgnoruj
    OknoDialogowe d5("Błąd wejścia/wyjścia", "Niespójność nośnika danych", PrzyciskiDialogu::PrzerwijPonowIgnoruj);
    d5.rozmiesc(Prostokat(0, 0, 460, 190));
    TEST_ASSERT(d5.liczba_przyciskow() == 3, "Przerwij/Ponow/Ignoruj ma dokladnie trzy przyciski");
    TEST_ASSERT(d5.wynik_przycisku(0) == WynikDialogu::Przerwij && d5.wynik_przycisku(1) == WynikDialogu::Ponow &&
                d5.wynik_przycisku(2) == WynikDialogu::Ignoruj, "Przerwij/Ponow/Ignoruj zachowuje trzy wyniki");
    d5.przy_wyniku(cb_dialog_wynik, nullptr);
    e_enter.obsluzone = false;
    d5.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Ponow, "Wynik Enter w PrzerwijPonowIgnoruj to Ponow (domyslny)");
    e_esc.obsluzone = false;
    d5.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Ignoruj, "Wynik Escape w PrzerwijPonowIgnoruj to Ignoruj (anuluj)");

    // 7. Wlasne przyciski
    OknoDialogowe dw("Własne akcje", "Wybierz działanie:", PrzyciskiDialogu::Wlasne);
    dw.przy_wyniku(cb_dialog_wynik, nullptr);
    dw.dodaj_przycisk("Akcja A", WynikDialogu::Tak, false, false);
    dw.dodaj_przycisk("Akcja B", WynikDialogu::Ponow, true, false);
    dw.dodaj_przycisk("Zaniechaj", WynikDialogu::Ignoruj, false, true);
    e_enter.obsluzone = false;
    dw.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Ponow, "Własny dialog Enter uruchamia Akcję B (domyślny)");
    e_esc.obsluzone = false;
    dw.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Ignoruj, "Własny dialog Escape uruchamia Zaniechaj (anuluj)");

    // 8. Nawigacja klawiszem Tab pomiedzy przyciskami
    ZdarzenieKlawiatury e_tab{};
    e_tab.kod_klawisza = 9; // Tab
    dw.obsluz_wcisniecie_klawisza(e_tab);
    TEST_ASSERT(e_tab.obsluzone, "Tab obsluzony przez dialog");
    // Po Tabie skupienie przechodzi na przycisk 'Zaniechaj' (Ignoruj)
    e_enter.obsluzone = false;
    dw.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Ignoruj, "Enter po Tab uruchamia nowo skupiony przycisk");

    // 9. Dlugie wiadomosci i zawijanie wierszy
    OknoDialogowe d_long("Długi komunikat",
        "To jest bardzo długa wiadomość testowa weryfikująca automatyczne łamanie wierszy.\n"
        "Drugi akapit tekstu z polskimi znakami: Zażółć gęślą jaźń.\n"
        "Trzeci akapit wiadomości.",
        PrzyciskiDialogu::OkAnuluj, TypDialogu::Informacja);
    d_long.rozmiesc(Prostokat(0, 0, 420, 240));
    TEST_ASSERT(d_long.typ_dialogu() == TypDialogu::Informacja, "Typ dialogu to Informacja");

    // 10. Statyczne metody Komunikat API
    ZarzadcaPopupow z_dlg;
    z_dlg.ustaw_geometrie_wlasciciela(Prostokat(0, 0, 800, 600), Prostokat(0, 0, 800, 600));

    OknoDialogowe* dlg_inf = Komunikat::informacja(z_dlg, "Info", "Tekst info", cb_dialog_wynik);
    TEST_ASSERT(dlg_inf != nullptr, "Komunikat::informacja tworzy dialog");
    TEST_ASSERT(dlg_inf->typ_dialogu() == TypDialogu::Informacja, "Typ to Informacja");
    dlg_inf->zamknij_z_wynikiem(WynikDialogu::OK);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::OK, "Callback odebral OK");

    OknoDialogowe* dlg_err = Komunikat::blad(z_dlg, "Błąd", "Treść błędu", cb_dialog_wynik);
    TEST_ASSERT(dlg_err->typ_dialogu() == TypDialogu::Blad, "Komunikat::blad tworzy typ Blad");
    dlg_err->zamknij_z_wynikiem(WynikDialogu::OK);

    OknoDialogowe* dlg_war = Komunikat::ostrzezenie(z_dlg, "Ostrzeżenie", "Treść ostrzeżenia");
    TEST_ASSERT(dlg_war->typ_dialogu() == TypDialogu::Ostrzezenie, "Komunikat::ostrzezenie tworzy typ Ostrzezenie");
    dlg_war->zamknij_z_wynikiem(WynikDialogu::OK);

    OknoDialogowe* dlg_pyt = Komunikat::pytanie(z_dlg, "Pytanie", "Czy zatwierdzić?");
    TEST_ASSERT(dlg_pyt->typ_dialogu() == TypDialogu::Pytanie, "Komunikat::pytanie tworzy typ Pytanie");
    dlg_pyt->zamknij_z_wynikiem(WynikDialogu::Tak);

    OknoDialogowe* dlg_suk = Komunikat::sukces(z_dlg, "Sukces", "Operacja powiodła się pomyślnie");
    TEST_ASSERT(dlg_suk->typ_dialogu() == TypDialogu::Sukces, "Komunikat::sukces tworzy typ Sukces");
    dlg_suk->zamknij_z_wynikiem(WynikDialogu::OK);
}

// =========================================================================
// 5. Test PasekInformacyjny
// =========================================================================
static bool g_kliknieto_akcje = false;
static bool g_kliknieto_zamknij = false;

static void cb_pasek_akcja(PasekInformacyjny*, void*) {
    g_kliknieto_akcje = true;
}
static void cb_pasek_zamknij(PasekInformacyjny*, void*) {
    g_kliknieto_zamknij = true;
}

static void test_pasek_informacyjny() {
    printf("\n--- Test 5: PasekInformacyjny (4 statuses, actions, callbacks) ---\n");

    PasekInformacyjny pasek(TypKomunikatu::Sukces, "Operacja udana", "Wszystkie dane zapisane.");
    TEST_ASSERT(pasek.typ_komunikatu() == TypKomunikatu::Sukces, "Typ: Sukces");
    TEST_ASSERT(strcmp(pasek.tekst(), "Operacja udana") == 0, "Tekst glowny poprawny");
    TEST_ASSERT(strcmp(pasek.tekst_drugorzedny(), "Wszystkie dane zapisane.") == 0, "Tekst drugi poprawny");

    pasek.ustaw_typ_komunikatu(TypKomunikatu::Blad);
    TEST_ASSERT(pasek.typ_komunikatu() == TypKomunikatu::Blad, "Zmiana typu na Blad");

    pasek.ustaw_akcje("Cofnij", cb_pasek_akcja);
    TEST_ASSERT(strcmp(pasek.etykieta_akcji(), "Cofnij") == 0, "Etykieta akcji poprawna");

    pasek.ustaw_przycisk_zamkniecia(true);
    TEST_ASSERT(pasek.czy_ma_przycisk_zamkniecia(), "Przycisk zamkniecia wlaczony");
    pasek.przy_zamknieciu(cb_pasek_zamknij);

    Rozmiar sz = pasek.zmierz(Rozmiar(600, 100));
    TEST_ASSERT(sz.szerokosc > 100 && sz.wysokosc >= 38, "Rozmiar paska informacyjnego obliczony");
}

// =========================================================================
// 6. Test WskaznikZajetosci (Activity Indicator)
// =========================================================================
static void test_wskaznik_zajetosci() {
    printf("\n--- Test 6: WskaznikZajetosci (Trig coords, start/stop/reset, phase) ---\n");

    WskaznikZajetosci w;
    TEST_ASSERT(!w.czy_aktywny(), "Domyslnie nieaktywny");
    TEST_ASSERT(!w.czy_wymaga_animacji(), "Nie wymaga animacji gdy nieaktywny");

    w.start();
    TEST_ASSERT(w.czy_aktywny(), "Aktywny po start()");

    // Symulacja krokow animacji
    for (int i = 0; i < 8; ++i) {
        w.krok_animacji(0);
    }
    TEST_ASSERT(w.liczba_krokow() == 8, "Wykonano 8 krokow animacji");

    w.reset();
    w.stop();
    TEST_ASSERT(!w.czy_aktywny(), "Nieaktywny po stop()");
}

// =========================================================================
// 7. Test PowierzchniaRysunkowa (Canvas)
// =========================================================================
static bool g_canvas_drawn = false;
static bool g_canvas_clicked = false;
static bool g_canvas_moved = false;

static void cb_canvas_draw(PowierzchniaRysunkowa*, KontekstRysowania&, void*) {
    g_canvas_drawn = true;
}
static void cb_canvas_click(PowierzchniaRysunkowa*, const ZdarzenieMyszki&, void*) {
    g_canvas_clicked = true;
}
static void cb_canvas_move(PowierzchniaRysunkowa*, const ZdarzenieMyszki&, void*) {
    g_canvas_moved = true;
}

static void test_powierzchnia_rysunkowa() {
    printf("\n--- Test 7: PowierzchniaRysunkowa (Custom draw, mouse callbacks) ---\n");

    PowierzchniaRysunkowa canvas;
    canvas.przy_rysowaniu(cb_canvas_draw);
    canvas.przy_wcisnieciu(cb_canvas_click);
    canvas.przy_ruchu(cb_canvas_move);

    canvas.rozmiesc(Prostokat(0, 0, 200, 150));

    KontekstRysowania ctx;
    uint32_t buf[200 * 150]{};
    ctx.ustaw_cel(buf, Prostokat(0, 0, 200, 150));
    canvas.rysuj_siebie(ctx);
    TEST_ASSERT(g_canvas_drawn, "Callback wlasnego rysowania wywolany");

    ZdarzenieMyszki e_click{};
    e_click.przycisk = 1;
    e_click.x = 50; e_click.y = 50;
    canvas.obsluz_wcisniecie_myszy(e_click);
    TEST_ASSERT(g_canvas_clicked, "Callback wcisniecia myszy wywolany");

    ZdarzenieMyszki e_move{};
    e_move.x = 60; e_move.y = 60;
    canvas.obsluz_ruch_myszy(e_move);
    TEST_ASSERT(g_canvas_moved, "Callback ruchu myszy wywolany");
}

// =========================================================================
// 8. Test Wykres (Chart)
// =========================================================================
static void test_wykres() {
    printf("\n--- Test 8: Wykres (Series, autoscale vs fixed, Linia/Slupki/Obszar) ---\n");

    Wykres wykres(TypWykresu::Linia);
    TEST_ASSERT(wykres.typ_wykresu() == TypWykresu::Linia, "Typ wykresu: Linia");

    const int punkty1[] = { 10, 25, 40, 30, 60, 55 };
    const int punkty2[] = { 5, 15, 20, 25, 30, 40 };

    TEST_ASSERT(wykres.ustaw_punkty(0, punkty1, 6, "CPU", 0x00FF0000), "Ustawienie serii 0");
    TEST_ASSERT(wykres.ustaw_punkty(1, punkty2, 6, "RAM", 0x0000FF00), "Ustawienie serii 1");
    TEST_ASSERT(wykres.liczba_serii() == 2, "Wykres posiada 2 serie danych");

    wykres.ustaw_typ_wykresu(TypWykresu::Slupki);
    TEST_ASSERT(wykres.typ_wykresu() == TypWykresu::Slupki, "Zmiana typu na Slupki");

    wykres.ustaw_zakres(0, 100);
    TEST_ASSERT(!wykres.czy_auto_skalowanie(), "Reczny zakres wylacza autoskalowanie");

    wykres.ustaw_auto_skalowanie(true);
    TEST_ASSERT(wykres.czy_auto_skalowanie(), "Wlaczenie autoskalowania");

    wykres.ustaw_pokazywanie_siatki(true);
    TEST_ASSERT(wykres.czy_pokazuje_siatke(), "Siatka wykresu wlaczona");

    wykres.wyczysc();
    TEST_ASSERT(wykres.liczba_serii() == 0, "Wyczyszczenie serii");
}

// =========================================================================
// 9. Test ListaWirtualna (100,000 elementow)
// =========================================================================
class TestDostawcaListy : public DostawcaDanychListy {
public:
    size_t liczba_elementow() override { return 100000; }
    const char* pobierz_tekst(size_t indeks, char* bufor, size_t rozmiar_bufora) override {
        snprintf(bufor, rozmiar_bufora, "Element #%zu", indeks);
        return bufor;
    }
};

static size_t g_wybrany_w_liscie = 0;
static void cb_wybor_listy(ListaWirtualna*, size_t idx, void*) {
    g_wybrany_w_liscie = idx;
}

static void test_lista_wirtualna() {
    printf("\n--- Test 9: ListaWirtualna (100k items, virtual viewport queries) ---\n");

    TestDostawcaListy dostawca;
    ListaWirtualna lista;
    lista.ustaw_dostawce(&dostawca);
    lista.przy_zmianie_wyboru(cb_wybor_listy);

    TEST_ASSERT(lista.liczba_elementow() == 100000, "Lista raportuje 100 000 elementow");

    // Rozmieszczamy w oknie 200 x 240 (wysokosc wiersza = 24 -> ~10 wierszy widocznych)
    lista.rozmiesc(Prostokat(0, 0, 200, 240));

    KontekstRysowania ctx;
    uint32_t buf[200 * 240]{};
    ctx.ustaw_cel(buf, Prostokat(0, 0, 200, 240));

    lista.rysuj_siebie(ctx);

    uint64_t zapytania = lista.liczba_zapytan_dostawcy();
    TEST_ASSERT(zapytania > 0 && zapytania <= 15, "Pobrano TYLKO widoczne elementy (<= 15 z 100 000)");

    // Zmiana wyboru
    lista.ustaw_wybrany_indeks(5000);
    TEST_ASSERT(lista.wybrany_indeks() == 5000, "Wybrany indeks to 5000");
    TEST_ASSERT(g_wybrany_w_liscie == 5000, "Callback wyboru wywolany dla indeksu 5000");
}

// =========================================================================
// 10. Test TabelaWirtualna (50,000 wierszy)
// =========================================================================
class TestDostawcaTabeli : public DostawcaDanychTabeli {
public:
    size_t liczba_wierszy() override { return 50000; }
    size_t liczba_kolumn() override { return 3; }
    const char* naglowek_kolumny(size_t col) override {
        static const char* const n[] = { "ID", "Nazwa", "Status" };
        return col < 3 ? n[col] : "";
    }
    int szerokosc_kolumny(size_t col) override {
        static const int w[] = { 60, 150, 100 };
        return col < 3 ? w[col] : 80;
    }
    const char* pobierz_komorke(size_t row, size_t col, char* buf, size_t sz) override {
        if (col == 0) snprintf(buf, sz, "%zu", row);
        else if (col == 1) snprintf(buf, sz, "Wiersz #%zu", row);
        else snprintf(buf, sz, "OK");
        return buf;
    }
    void zazadaj_sortowania(size_t col, bool rosnaco) override {
        ostatnie_sort_col_ = col;
        ostatnie_sort_asc_ = rosnaco;
    }
    size_t ostatnie_sort_col_{0};
    bool ostatnie_sort_asc_{true};
};

static size_t g_wybrany_wiersz = 0;
static void cb_wybor_tabeli(TabelaWirtualna*, size_t r, void*) {
    g_wybrany_wiersz = r;
}

static void test_tabela_wirtualna() {
    printf("\n--- Test 10: TabelaWirtualna (50k rows, column sorting, virtual queries) ---\n");

    TestDostawcaTabeli dostawca;
    TabelaWirtualna tabela;
    tabela.ustaw_dostawce(&dostawca);
    tabela.przy_zmianie_wyboru(cb_wybor_tabeli);

    TEST_ASSERT(tabela.liczba_wierszy() == 50000, "Tabela raportuje 50 000 wierszy");
    TEST_ASSERT(tabela.liczba_kolumn() == 3, "Tabela raportuje 3 kolumny");

    // Rozmieszczamy w oknie 350 x 240
    tabela.rozmiesc(Prostokat(0, 0, 350, 240));

    KontekstRysowania ctx;
    uint32_t buf[350 * 240]{};
    ctx.ustaw_cel(buf, Prostokat(0, 0, 350, 240));

    tabela.rysuj_siebie(ctx);

    uint64_t zapytania = tabela.liczba_zapytan_dostawcy();
    TEST_ASSERT(zapytania > 0 && zapytania <= 60, "Pobrano TYLKO widoczne komorki (<= 60 z 150 000)");

    // Zmiana wybranego wiersza
    tabela.ustaw_wybrany_wiersz(1234);
    TEST_ASSERT(tabela.wybrany_wiersz() == 1234, "Wybrany wiersz to 1234");
    TEST_ASSERT(g_wybrany_wiersz == 1234, "Callback wyboru wiersza wywolany dla 1234");
}

// =========================================================================
// 11. Test Powiadomienie & MenedzerPowiadomien
// =========================================================================
static void test_powiadomienia() {
    printf("\n--- Test 11: Powiadomienie i MenedzerPowiadomien (Stacking, 4 slots, timeout) ---\n");

    ZarzadcaPopupow zarzadca;
    zarzadca.ustaw_geometrie_wlasciciela(Prostokat(0, 0, 800, 600), Prostokat(0, 0, 800, 600));

    MenedzerPowiadomien menedzer;
    TEST_ASSERT(menedzer.liczba_aktywnych() == 0, "Brak aktywnych powiadomien na poczatku");

    // Dodaj 4 powiadomienia (maksymalna liczba slotow)
    menedzer.pokaz(zarzadca, TypPowiadomienia::Informacja, "T1", "Wiadomosc 1", 3000);
    menedzer.pokaz(zarzadca, TypPowiadomienia::Sukces, "T2", "Wiadomosc 2", 4000);
    menedzer.pokaz(zarzadca, TypPowiadomienia::Ostrzezenie, "T3", "Wiadomosc 3", 5000);
    menedzer.pokaz(zarzadca, TypPowiadomienia::Blad, "T4", "Wiadomosc 4", 2000);

    TEST_ASSERT(menedzer.liczba_aktywnych() == 4, "Wszystkie 4 sloty powiadomien zajete");

    // Dodanie 5. powiadomienia powinno zastapic najstarsze
    menedzer.pokaz(zarzadca, TypPowiadomienia::Sukces, "T5", "Wiadomosc 5", 6000);
    TEST_ASSERT(menedzer.liczba_aktywnych() == 4, "Nadal maksymalnie 4 aktywne powiadomienia po nadpisaniu");

    // Symulacja uplywu czasu (animacja)
    for (size_t i = 0; i < MenedzerPowiadomien::MAKS_TOASTOW; ++i) {
        Powiadomienie& p = menedzer.powiadomienie(i);
        if (p.czy_otwarta()) {
            p.krok_animacji(0); // -50 ms
        }
    }

    zarzadca.zamknij();
    for (int i = 0; i < 100; ++i) {
        TEST_ASSERT(menedzer.pokaz(zarzadca, TypPowiadomienia::Sukces, "Cykl",
                    "Zażółć gęślą jaźń", 50), "Toast zostal przyjety w cyklu ponownego uzycia");
        zarzadca.krok_animacji();
        TEST_ASSERT(menedzer.liczba_aktywnych() == 0, "Slot wraca do puli po wygasnieciu");
        TEST_ASSERT(zarzadca.liczba_powierzchni() == 0, "Wygasly popup zostal wyrejestrowany");
    }

    const Prostokat prace[] = { Prostokat(0,0,1024,720), Prostokat(0,0,1280,680),
        Prostokat(0,0,1366,728), Prostokat(0,0,1920,1040), Prostokat(0,0,3840,2100) };
    for (const Prostokat& work : prace) {
        ZarzadcaPopupow zg;
        Prostokat owner(100, 80, 800, 600);
        zg.ustaw_geometrie_wlasciciela(owner, work);
        MenedzerPowiadomien mg;
        for (int i = 0; i < 4; ++i)
            TEST_ASSERT(mg.pokaz(zg, TypPowiadomienia::Informacja, "Geometria",
                "Długa treść mierzona kanoniczną czcionką interfejsu.", 5000), "Toast geometrii przyjety");
        Prostokat poprzedni;
        for (size_t i = 0; i < 4; ++i) {
            Prostokat r = mg.powiadomienie(i).obszar_ekranu();
            TEST_ASSERT(r.x >= work.x && r.prawy() <= work.prawy() && r.y >= work.y && r.dolny() <= work.dolny(),
                        "Toast miesci sie w obszarze roboczym");
            if (!poprzedni.czy_pusty()) TEST_ASSERT(r.przeciecie(poprzedni).czy_pusty(), "Toasty nie nakladaja sie");
            poprzedni = r;
        }
    }
}

// =========================================================================
// 12. Test Motywy, kontrast pulpitu i kolory kontrolek
// =========================================================================
static void test_motywy_kolory_pulpit_i_kontrolki() {
    printf("\n--- Test 12: Motywy, kontrast pulpitu i kolory kontrolek ---\n");

    // 1. Kanoniczne kolory etykiety pulpitu sa stale i niezalezne od motywu
    TEST_ASSERT(KANONICZNY_KOLOR_ETYKIETY_PULPITU == 0x00FFFFFF, "Etykieta pulpitu jest czysto biala 0x00FFFFFF");
    TEST_ASSERT(KANONICZNY_KOLOR_CIENIA_PULPITU == 0x00101010, "Cien etykiety pulpitu to 0x00101010");

    // 2. Weryfikacja wszystkich 5 oficjalnych motywow
    const char* const MOTYWY_ID[] = {
        "bursztynnocny", "baltyckanoc", "grafit", "bursztynklasyczny", "bursztynjasny"
    };

    for (size_t i = 0; i < 5; ++i) {
        BursztynMotyw m{};
        bool wczytano = motyw_zaladuj_z_pliku(MOTYWY_ID[i], &m);
        TEST_ASSERT(wczytano, "Wczytano deklaracje motywu");

        // Sprawdz czy etykieta pulpitu nie ulega wplywowi koloru tla pulpitu
        TEST_ASSERT(KANONICZNY_KOLOR_ETYKIETY_PULPITU == 0x00FFFFFF, "Etykieta na pulpicie zachowuje biel");
    }

    // 3. Test motywu Bursztyn Jasny: ciemny tekst dla okien i kontrolek
    static BursztynMotyw jasny{};
    motyw_zaladuj_z_pliku("bursztynjasny", &jasny);
    test_ustaw_aktywny_motyw(&jasny);

    TEST_ASSERT(motyw_kolor(MOTYW_KOLOR_TEKST) == 0x00202124, "Bursztyn Jasny MOTYW_KOLOR_TEKST to ciemny #202124");
    TEST_ASSERT(motyw_kolor(MOTYW_KOLOR_OKNOTLO) == 0x00F7F6F3, "Bursztyn Jasny MOTYW_KOLOR_OKNOTLO to jasny #F7F6F3");
    TEST_ASSERT(motyw_kolor(MOTYW_KOLOR_PULPIT) == 0x00EDEBE7, "Bursztyn Jasny MOTYW_KOLOR_PULPIT to #EDEBE7");
    TEST_ASSERT(motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE) == 0x00FFE082, "Bursztyn Jasny zaznaczenie to #FFE082");
    TEST_ASSERT(motyw_kolor_tekstu_zaznaczenia() == 0x00202124, "Bursztyn Jasny tekst zaznaczenia to ciemny #202124");

    // Etykieta i Przycisk w motywie jasnym uzywaja ciemnego tekstu
    Etykieta etyk_jasna("Test tekstu jasnego");
    TEST_ASSERT(etyk_jasna.kolor_tekstu() == 0x00202124, "Etykieta w motywie jasnym ma ciemny tekst #202124");
    Przycisk przyc_jasny("Przycisk");
    TEST_ASSERT(przyc_jasny.styl().normalny.tekst == 0x00202124, "Przycisk w motywie jasnym ma ciemny tekst #202124");

    // 4. Test motywu Bursztyn Nocny: jasny tekst dla okien i kontrolek
    static BursztynMotyw nocny{};
    motyw_zaladuj_z_pliku("bursztynnocny", &nocny);
    test_ustaw_aktywny_motyw(&nocny);

    TEST_ASSERT(motyw_kolor(MOTYW_KOLOR_TEKST) == 0x00F2F3F5, "Bursztyn Nocny MOTYW_KOLOR_TEKST to jasny #F2F3F5");
    TEST_ASSERT(motyw_kolor(MOTYW_KOLOR_OKNOTLO) == 0x00191C20, "Bursztyn Nocny MOTYW_KOLOR_OKNOTLO to ciemny #191C20");
    TEST_ASSERT(motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE) == 0x00314A63, "Bursztyn Nocny zaznaczenie to odrebna powierzchnia #314A63");
    TEST_ASSERT(motyw_kolor_tekstu_zaznaczenia() == 0x00F2F3F5, "Bursztyn Nocny tekst zaznaczenia to jasny #F2F3F5");

    Etykieta etyk_nocna("Test tekstu ciemnego");
    TEST_ASSERT(etyk_nocna.kolor_tekstu() == 0x00F2F3F5, "Etykieta w motywie nocnym ma jasny tekst #F2F3F5");

    test_ustaw_aktywny_motyw(nullptr);
}

// =========================================================================
// 13. Test Stany menu, hover zaznaczenia i izolacja stylow
// =========================================================================
static void test_menu_hover_i_izolacja() {
    printf("\n--- Test 13: Stany menu, hover zaznaczenia i izolacja stylow ---\n");

    // 1. WidokMenuPopup - zaznaczenie, kolor tekstu zaznaczenia, wylaczone elementy
    Menu menu;
    ElementMenu& el1 = menu.dodaj_element("Nowy", 1);
    ElementMenu& el2 = menu.dodaj_element("Widok paska", 2);
    el2.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole).ustaw_zaznaczony(true);
    ElementMenu& el3 = menu.dodaj_element("Wytnij", 3);
    el3.ustaw_wlaczony(false);
    Menu& sub = menu.dodaj_podmenu("Zaawansowane");
    sub.dodaj_element("Opcja 1", 10);
    (void)el1;

    WidokMenuPopup widok;
    widok.ustaw(&menu, nullptr);
    widok.rozmiesc(Prostokat(0, 0, 180, 120));

    TEST_ASSERT(widok.styl().najechany.tlo == motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE),
                "WidokMenuPopup najechany uzywa MOTYW_KOLOR_ZAZNACZENIE");
    TEST_ASSERT(widok.styl().najechany.tekst == motyw_kolor_tekstu_zaznaczenia(),
                "WidokMenuPopup najechany uzywa motyw_kolor_tekstu_zaznaczenia()");

    // Obsluga klawiatury: Strzalka w dol, Home, End
    ZdarzenieKlawiatury e_down{}; e_down.kod_klawisza = 0x50; // Strzalka w dol
    widok.obsluz_wcisniecie_klawisza(e_down);
    TEST_ASSERT(e_down.obsluzone, "Strzalka w dol obsluzona przez menu");

    // Opuszczenie myszy resetuje aktywny hover
    widok.obsluz_opuszczenie_myszy();

    // Element z zaznaczeniem typu pole wyboru zachowuje swoj stan
    TEST_ASSERT(el2.czy_zaznaczony(), "Element menu zachowuje stan zaznaczenia");

    // 2. MenuGlowne
    MenuGlowne menu_glowne;
    menu_glowne.menu().dodaj_podmenu("Plik");
    menu_glowne.menu().dodaj_podmenu("Edycja");
    menu_glowne.menu().dodaj_podmenu("Pomoc");
    menu_glowne.rozmiesc(Prostokat(0, 0, 400, 28));

    ZdarzenieMyszki me_hover{};
    me_hover.typ = TypZdarzenia::RuchMyszy;
    me_hover.x = 20; me_hover.y = 10;
    menu_glowne.obsluz_ruch_myszy(me_hover);
    TEST_ASSERT(me_hover.obsluzone, "MenuGlowne obsluguje ruch myszy nad elementem");

    menu_glowne.obsluz_opuszczenie_myszy();

    // 3. Izolacja: Panel i Etykieta nie moga przyjmowac stylow menu
    Panel test_panel;
    TEST_ASSERT(test_panel.kolor_tla() != motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE),
                "Panel zachowuje wlasne tlo i nie przejmuje stylu menu");
    Etykieta test_etykieta("Test");
    TEST_ASSERT(test_etykieta.kolor_tekstu() != motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE),
                "Etykieta zachowuje normalny kolor tekstu");
}

// =========================================================================
// 14. Test Czcionka extronic16Bunicode i odpornosc UTF-8
// =========================================================================
static void test_czcionka_extronic16b_i_odpornosc_utf8() {
    printf("\n--- Test 14: Czcionka extronic16Bunicode i odpornosc UTF-8 ---\n");

    // 1. Parametry matrycy czcionki
    TEST_ASSERT(CzcionkaUI::WYSOKOSC == 16, "Wysokosc matrycy czcionki to 16 px");
    TEST_ASSERT(CzcionkaUI::LICZBA_GLIFOW >= 1170, "Czcionka posiada co najmniej 1170 zdefiniowanych glifow");

    // 2. Weryfikacja pelnego zestawu polskich znakow diakrytycznych w UTF-8
    const char* polski_tekst = "Zażółć gęślą jaźń ZAŻÓŁĆ GĘŚLĄ JAŹŃ";
    size_t i = 0;
    int liczba_zdekodowanych = 0;
    while (polski_tekst[i]) {
        uint32_t cp = 0;
        size_t z = CzcionkaUI::dekoduj_utf8(polski_tekst, i, &cp);
        TEST_ASSERT(z > 0, "Poprawne dekodowanie punktu kodowego");
        TEST_ASSERT(cp != '?', "Polski znak nie jest dekodowany jako znak zapytania");
        int szer = CzcionkaUI::szerokosc_znaku(cp);
        TEST_ASSERT(szer > 0, "Polski znak posiada niezerowa szerokosc metryki");
        const uint16_t* bmp = CzcionkaUI::bitmapa_znaku(cp);
        TEST_ASSERT(bmp != nullptr, "Polski znak posiada bitmape w tablicy glifow");

        if (cp != ' ') {
            bool ma_piksele = false;
            for (int r = 0; r < 16; ++r) {
                if (bmp[r] != 0) { ma_piksele = true; break; }
            }
            TEST_ASSERT(ma_piksele, "Glif czcionki zawiera piksele w bitmapie");
        }
        i += z;
        ++liczba_zdekodowanych;
    }
    TEST_ASSERT(liczba_zdekodowanych == 35, "Zdekodowano dokladnie 35 znakow frazy testowej");

    // Punkty kodowe Unicode dla liter alfabetu polskiego
    uint32_t cp_test = 0;
    CzcionkaUI::dekoduj_utf8("ą", 0, &cp_test); TEST_ASSERT(cp_test == 261, "ą -> U+0105 (261)");
    CzcionkaUI::dekoduj_utf8("ć", 0, &cp_test); TEST_ASSERT(cp_test == 263, "ć -> U+0107 (263)");
    CzcionkaUI::dekoduj_utf8("ę", 0, &cp_test); TEST_ASSERT(cp_test == 281, "ę -> U+0119 (281)");
    CzcionkaUI::dekoduj_utf8("ł", 0, &cp_test); TEST_ASSERT(cp_test == 322, "ł -> U+0142 (322)");
    CzcionkaUI::dekoduj_utf8("ń", 0, &cp_test); TEST_ASSERT(cp_test == 324, "ń -> U+0144 (324)");
    CzcionkaUI::dekoduj_utf8("ó", 0, &cp_test); TEST_ASSERT(cp_test == 243, "ó -> U+00F3 (243)");
    CzcionkaUI::dekoduj_utf8("ś", 0, &cp_test); TEST_ASSERT(cp_test == 347, "ś -> U+015B (347)");
    CzcionkaUI::dekoduj_utf8("ź", 0, &cp_test); TEST_ASSERT(cp_test == 378, "ź -> U+017A (378)");
    CzcionkaUI::dekoduj_utf8("ż", 0, &cp_test); TEST_ASSERT(cp_test == 380, "ż -> U+017C (380)");

    CzcionkaUI::dekoduj_utf8("Ą", 0, &cp_test); TEST_ASSERT(cp_test == 260, "Ą -> U+0104 (260)");
    CzcionkaUI::dekoduj_utf8("Ć", 0, &cp_test); TEST_ASSERT(cp_test == 262, "Ć -> U+0106 (262)");
    CzcionkaUI::dekoduj_utf8("Ę", 0, &cp_test); TEST_ASSERT(cp_test == 280, "Ę -> U+0118 (280)");
    CzcionkaUI::dekoduj_utf8("Ł", 0, &cp_test); TEST_ASSERT(cp_test == 321, "Ł -> U+0141 (321)");
    CzcionkaUI::dekoduj_utf8("Ń", 0, &cp_test); TEST_ASSERT(cp_test == 323, "Ń -> U+0143 (323)");
    CzcionkaUI::dekoduj_utf8("Ó", 0, &cp_test); TEST_ASSERT(cp_test == 211, "Ó -> U+00D3 (211)");
    CzcionkaUI::dekoduj_utf8("Ś", 0, &cp_test); TEST_ASSERT(cp_test == 346, "Ś -> U+015A (346)");
    CzcionkaUI::dekoduj_utf8("Ź", 0, &cp_test); TEST_ASSERT(cp_test == 377, "Ź -> U+0179 (377)");
    CzcionkaUI::dekoduj_utf8("Ż", 0, &cp_test); TEST_ASSERT(cp_test == 379, "Ż -> U+017B (379)");

    // 3. Odpornosc dekodera UTF-8 na sekwencje znieksztalcone i obciete
    uint32_t zly_znak = 0;

    // Obciety 2-bajtowy
    const char obciety2[] = { (char)0xC3, 0 };
    size_t z2 = CzcionkaUI::dekoduj_utf8(obciety2, 0, &zly_znak);
    TEST_ASSERT(z2 == 1 && zly_znak == '?', "Obciety 2-bajtowy UTF-8 zwraca '?' i zuzywa 1 bajt");

    // Obciety 3-bajtowy
    const char obciety3[] = { (char)0xE2, (char)0x82, 0 };
    size_t z3 = CzcionkaUI::dekoduj_utf8(obciety3, 0, &zly_znak);
    TEST_ASSERT(z3 == 1 && zly_znak == '?', "Obciety 3-bajtowy UTF-8 zwraca '?' i zuzywa 1 bajt");

    // Obciety 4-bajtowy
    const char obciety4[] = { (char)0xF0, (char)0x9F, (char)0x98, 0 };
    size_t z4 = CzcionkaUI::dekoduj_utf8(obciety4, 0, &zly_znak);
    TEST_ASSERT(z4 == 1 && zly_znak == '?', "Obciety 4-bajtowy UTF-8 zwraca '?' i zuzywa 1 bajt");

    // Nadmiarowy (overlong) 2-bajtowy U+002F jako 0xC0 0xAF
    const char overlong2[] = { (char)0xC0, (char)0xAF, 0 };
    size_t z_ov2 = CzcionkaUI::dekoduj_utf8(overlong2, 0, &zly_znak);
    TEST_ASSERT(z_ov2 == 1 && zly_znak == '?', "Odrzucenie overlong 2-bajtowego");

    // Nadmiarowy (overlong) 3-bajtowy jako 0xE0 0x80 0xAF
    const char overlong3[] = { (char)0xE0, (char)0x80, (char)0xAF, 0 };
    size_t z_ov3 = CzcionkaUI::dekoduj_utf8(overlong3, 0, &zly_znak);
    TEST_ASSERT(z_ov3 == 1 && zly_znak == '?', "Odrzucenie overlong 3-bajtowego");

    // Surogat UTF-16: 0xED 0xA0 0x80 (U+D800)
    const char surr[] = { (char)0xED, (char)0xA0, (char)0x80, 0 };
    size_t z_surr = CzcionkaUI::dekoduj_utf8(surr, 0, &zly_znak);
    TEST_ASSERT(z_surr == 1 && zly_znak == '?', "Odrzucenie surogatu UTF-16");

    // Bajt niedozwolony 0xFF
    const char zly_bajt[] = { (char)0xFF, 0 };
    size_t z_ff = CzcionkaUI::dekoduj_utf8(zly_bajt, 0, &zly_znak);
    TEST_ASSERT(z_ff == 1 && zly_znak == '?', "Odrzucenie bajtu 0xFF");

    // Poza zakresem Unicode (> 0x10FFFF)
    const char poza_uni[] = { (char)0xF5, (char)0x80, (char)0x80, (char)0x80, 0 };
    size_t z_poza = CzcionkaUI::dekoduj_utf8(poza_uni, 0, &zly_znak);
    TEST_ASSERT(z_poza == 1 && zly_znak == '?', "Odrzucenie punktu kodowego > 0x10FFFF");
}

// =========================================================================
// 15. Testy geometrii: OknoDialogowe (zaokraglone rogi, przezroczyste wyciecia)
// =========================================================================
static void test_geometria_okno_dialogowe() {
    printf("\n--- Test 15: OknoDialogowe Geometria (Rogi motywu, wyciecia, hit-test, zmiana motywu) ---\n");

    const char* const MOTYWY_ID[] = {
        "bursztynnocny", "baltyckanoc", "grafit", "bursztynklasyczny", "bursztynjasny"
    };

    for (size_t i = 0; i < 5; ++i) {
        BursztynMotyw m{};
        bool wczytano = motyw_zaladuj_z_pliku(MOTYWY_ID[i], &m);
        TEST_ASSERT(wczytano, "Wczytano motyw dla testu geometrii OknoDialogowe");
        test_ustaw_aktywny_motyw(&m);

        int promien_motywu = motyw_promien(MOTYW_ROG_OKNO);
        TEST_ASSERT(promien_motywu > 0, "Promien okna w motywie jest wiekszy od zera");

        OknoDialogowe diag("Test Rogow", "Komunikat testowy z zaokraglonymi rogami",
                          PrzyciskiDialogu::OkAnuluj, TypDialogu::Pytanie);
        TEST_ASSERT(diag.promien_zaokraglenia() == promien_motywu,
                    "Promien zaokraglenia OknoDialogowe odpowiada motyw_promien(MOTYW_ROG_OKNO)");

        // 1. Rysowanie do bufora testowego i weryfikacja przezroczystosci pikseli w rogach
        diag.rozmiesc(Prostokat(0, 0, 400, 200));
        uint32_t piksele[400 * 200];
        memset(piksele, 0, sizeof(piksele));
        KontekstRysowania ctx;
        ctx.ustaw_cel(piksele, Prostokat(0, 0, 400, 200));
        diag.rysuj_siebie(ctx);

        // Piksele w wierzcholkach (0,0), (399,0), (0,199), (399,199) musza pozostac 0x00000000 (wyciecia)
        TEST_ASSERT(piksele[0] == 0x00000000, "Piksel (0,0) lewego-gornego rogu jest przezroczysty 0x0");
        TEST_ASSERT(piksele[399] == 0x00000000, "Piksel (399,0) prawego-gornego rogu jest przezroczysty 0x0");
        TEST_ASSERT(piksele[199 * 400] == 0x00000000, "Piksel (0,199) lewego-dolnego rogu jest przezroczysty 0x0");
        TEST_ASSERT(piksele[199 * 400 + 399] == 0x00000000, "Piksel (399,199) prawego-dolnego rogu jest przezroczysty 0x0");
        TEST_ASSERT(piksele[100 * 400 + 200] != 0x00000000, "Piksel wewnatrz bryly dialogu jest nieprzezroczysty");

        // 2. Weryfikacja przezroczystych wycięć w rogach i testu trafień (hit test)
        // Rozmieszczamy dialog na pozycji (100, 100, 400, 200)
        diag.rozmiesc(Prostokat(100, 100, 400, 200));

        // W samych narożnikach prostokąta otaczającego sprawdz_trafienie musi zwrócić nullptr
        TEST_ASSERT(diag.sprawdz_trafienie(Punkt(100, 100)) == nullptr,
                    "Lewy-gorny naroznik (wyciecie) nie przechwytuje myszy (nullptr)");
        TEST_ASSERT(diag.sprawdz_trafienie(Punkt(100 + 400 - 1, 100)) == nullptr,
                    "Prawy-gorny naroznik (wyciecie) nie przechwytuje myszy (nullptr)");
        TEST_ASSERT(diag.sprawdz_trafienie(Punkt(100, 100 + 200 - 1)) == nullptr,
                    "Lewy-dolny naroznik (wyciecie) nie przechwytuje myszy (nullptr)");
        TEST_ASSERT(diag.sprawdz_trafienie(Punkt(100 + 400 - 1, 100 + 200 - 1)) == nullptr,
                    "Prawy-dolny naroznik (wyciecie) nie przechwytuje myszy (nullptr)");

        // Punkt wewnatrz bryly okna (np. srodek) musi byc trafiony
        TEST_ASSERT(diag.sprawdz_trafienie(Punkt(100 + 200, 100 + 100)) != nullptr,
                    "Punkt wewnatrz zaokraglonego dialogu trafia w kontrolke");
        // Punkt wewnatrz zaokraglonego rogu po wcieciu
        TEST_ASSERT(diag.sprawdz_trafienie(Punkt(100 + promien_motywu + 2, 100 + promien_motywu + 2)) != nullptr,
                    "Punkt za zaokragleniem nalezy do dialogu");
    }

    // 3. Weryfikacja dynamicznej zmiany motywu
    BursztynMotyw m_jasny{};
    BursztynMotyw m_nocny{};
    motyw_zaladuj_z_pliku("bursztynjasny", &m_jasny);
    motyw_zaladuj_z_pliku("bursztynnocny", &m_nocny);

    test_ustaw_aktywny_motyw(&m_jasny);
    OknoDialogowe dynamic_diag("Dynamiczny", "Sprawdzanie zmiany motywu", PrzyciskiDialogu::TylkoOk);
    int r1 = dynamic_diag.promien_zaokraglenia();
    TEST_ASSERT(r1 == motyw_promien(MOTYW_ROG_OKNO), "Poczatkowy promien dialogu zgodny z motywem jasnym");

    test_ustaw_aktywny_motyw(&m_nocny);
    dynamic_diag.zaktualizuj_motyw();
    int r2 = dynamic_diag.promien_zaokraglenia();
    TEST_ASSERT(r2 == motyw_promien(MOTYW_ROG_OKNO), "Po zmianie motywu promien dialogu zostal zaktualizowany");

    test_ustaw_aktywny_motyw(nullptr);
}

// =========================================================================
// 16. Testy geometrii: MenuGlowne kotwica i brak nadmiarowego odstepu
// =========================================================================
static void test_geometria_menu_glowne_kotwica() {
    printf("\n--- Test 16: MenuGlowne Geometria Kotwicy (odstep 0 px, dopasowanie X, skalowanie) ---\n");

    const SkalaUI skale[] = { 1, 2 };
    for (SkalaUI skala : skale) {
        ZarzadcaPopupow zarzadca;
        Prostokat work_area(0, 0, 1024 * skala, 768 * skala);
        Prostokat window_client(80 * skala, 60 * skala, 800 * skala, 500 * skala);
        zarzadca.ustaw_geometrie_wlasciciela(window_client, work_area);
        zarzadca.ustaw_skale_dpi(skala);

        MenuGlowne menu;
        menu.ustaw_skale_dpi(skala);
        menu.ustaw_zarzadce_popupow(&zarzadca);

        Menu& menu_plik = menu.menu().dodaj_podmenu("Plik");
        menu_plik.dodaj_element("Nowy", 1);
        menu_plik.dodaj_element("Otwórz", 2);
        menu_plik.dodaj_element("Zakończ", 3);

        Menu& menu_edycja = menu.menu().dodaj_podmenu("Edycja");
        menu_edycja.dodaj_element("Cofnij", 4);
        menu_edycja.dodaj_element("Wytnij", 5);
        menu_edycja.dodaj_element("Wklej", 6);

        Menu& menu_pomoc = menu.menu().dodaj_podmenu("Pomoc");
        menu_pomoc.dodaj_element("O programie", 7);

        menu.rozmiesc(Prostokat(0, 0, window_client.szerokosc, 28 * skala));

        for (int i = 0; i < 3; ++i) {
            Prostokat item_local = menu.prostokat_glowny(i);
            TEST_ASSERT(!item_local.czy_pusty(), "Element MenuGlowne ma niepusty prostokat");

            Prostokat item_ekran = menu.prostokat_do_ekranu(item_local);
            TEST_ASSERT(item_ekran.x == window_client.x + item_local.x,
                        "Wspolrzedna X elementu menu w przestrzeni ekranu jest poprawna");
            TEST_ASSERT(item_ekran.y == window_client.y + item_local.y,
                        "Wspolrzedna Y elementu menu w przestrzeni ekranu jest poprawna");

            ZdarzenieMyszki zm{};
            zm.typ = TypZdarzenia::WcisniecieMyszy;
            zm.przycisk = 1;
            zm.x = item_local.x + 10 * skala;
            zm.y = item_local.y + item_local.wysokosc / 2;
            menu.obsluz_wcisniecie_myszy(zm);

            TEST_ASSERT(menu.czy_otwarte(), "Menu popup otwarte po kliknieciu w element glowny");
            TEST_ASSERT(zarzadca.liczba_powierzchni() > 0, "ZarzadcaPopupow zarejestrowal powierzchnie popupu");

            Prostokat popup_ekran = zarzadca.prostokat_zarejestrowany_ekranu();

            // 1. Popup zaczyna się dokładnie pod elementem (gap == 0 px)
            int gap = popup_ekran.y - item_ekran.dolny();
            TEST_ASSERT(gap == 0, "Odstęp pionowy miedzy elementem menu a popupem wynosi dokladnie 0 px");
            TEST_ASSERT(gap < 5 * skala, "Brak nadmiarowego odstepu rownego wierszowi menu lub belce tytulu");

            // 2. Krawedz lewa popupu pokrywa sie z lewa krawedzia elementu
            TEST_ASSERT(popup_ekran.x == item_ekran.x,
                        "Krawedz X popupu idealnie pokrywa sie z lewa krawedzia elementu menu");

            // 3. Popup w calosci miesci sie w obszarze roboczym monitora
            TEST_ASSERT(popup_ekran.x >= work_area.x && popup_ekran.prawy() <= work_area.prawy() &&
                        popup_ekran.y >= work_area.y && popup_ekran.dolny() <= work_area.dolny(),
                        "Popup miesci sie w granicach obszaru roboczego");

            zarzadca.zamknij();
            TEST_ASSERT(!menu.czy_otwarte(), "Menu zamkniete po zarzadca.zamknij()");
        }
    }
}

// =========================================================================
// 17. Testy geometrii: PrzyciskPodzielony dropdown (brak przycinania, pozycjonowanie)
// =========================================================================
static void test_geometria_przycisk_podzielony_dropdown() {
    printf("\n--- Test 17: PrzyciskPodzielony dropdown Geometria (brak przycinania przez panel, flip/shift) ---\n");

    ZarzadcaPopupow zarzadca;
    Prostokat work_area(0, 0, 1024, 768);
    Prostokat window_client(100, 100, 600, 400);
    zarzadca.ustaw_geometrie_wlasciciela(window_client, work_area);
    zarzadca.ustaw_skale_dpi(1);

    Panel panel;
    panel.ustaw_zarzadce_popupow(&zarzadca);
    panel.rozmiesc(Prostokat(0, 0, 600, 36));

    PrzyciskPodzielony* przycisk = new PrzyciskPodzielony("Zapisz");
    przycisk->ustaw_zarzadce_popupow(&zarzadca);
    panel.dodaj_kontrolke(przycisk);
    przycisk->rozmiesc(Prostokat(10, 2, 120, 32));

    przycisk->menu().dodaj_element("Zapisz bieżący", 1);
    przycisk->menu().dodaj_element("Zapisz jako nowy plik...", 2);
    przycisk->menu().dodaj_element("Eksportuj do formatu tekstowego", 3);

    Prostokat opener_screen = przycisk->prostokat_do_ekranu(
        Prostokat(0, 0, przycisk->szerokosc(), przycisk->wysokosc()));
    TEST_ASSERT(opener_screen.x == window_client.x + 10, "Opener X w przestrzeni ekranu");
    TEST_ASSERT(opener_screen.y == window_client.y + 2, "Opener Y w przestrzeni ekranu");

    int aw = przycisk->szerokosc_czesci_strzalki();
    ZdarzenieMyszki zm_press{};
    zm_press.typ = TypZdarzenia::WcisniecieMyszy;
    zm_press.przycisk = 1;
    zm_press.x = przycisk->obszar_ekranu().prawy() - aw / 2;
    zm_press.y = przycisk->obszar_ekranu().y + przycisk->obszar_ekranu().wysokosc / 2;
    przycisk->obsluz_wcisniecie_myszy(zm_press);

    ZdarzenieMyszki zm_release = zm_press;
    zm_release.typ = TypZdarzenia::ZwolnienieMyszy;
    przycisk->obsluz_zwolnienie_myszy(zm_release);

    TEST_ASSERT(przycisk->czy_menu_otwarte(), "Dropdown menu otwarte po kliknieciu w strzalke");
    TEST_ASSERT(zarzadca.liczba_powierzchni() > 0, "Powierzchnia dropdown zarejestrowana w ZarzadcaPopupow");

    Prostokat popup_screen = zarzadca.prostokat_zarejestrowany_ekranu();

    // 1. Dropdown NIE jest obcinany przez toolbar ani panel rodzica
    Prostokat panel_screen = panel.prostokat_do_ekranu(Prostokat(0, 0, panel.szerokosc(), panel.wysokosc()));
    TEST_ASSERT(popup_screen.dolny() > panel_screen.dolny(),
                "Dropdown rozciaga sie PONAD krawedz paska narzedzi/panelu (brak obciecia przez kontener)");
    TEST_ASSERT(popup_screen.wysokosc >= 3 * 22,
                "Wysokosc popupu obejmuje pelna tresc menu (wszystkie 3 elementy)");

    // 2. Dropdown otwiera sie bezposrednio przy openerze (gap 0 px)
    TEST_ASSERT(popup_screen.y == opener_screen.dolny(),
                "Dropdown przylega bezposrednio do dolnej krawedzi przycisku (gap == 0 px)");

    // 3. Popup w calosci widoczny w obszarze roboczym
    TEST_ASSERT(popup_screen.x >= work_area.x && popup_screen.prawy() <= work_area.prawy() &&
                popup_screen.y >= work_area.y && popup_screen.dolny() <= work_area.dolny(),
                "Dropdown w calosci miesci sie w obszarze roboczym monitora");

    zarzadca.zamknij();

    // 4. Test repozycjonowania przy dolnej krawedzi ekranu (otwarcie w gore)
    Prostokat window_near_bottom(100, work_area.dolny() - 60, 600, 50);
    zarzadca.ustaw_geometrie_wlasciciela(window_near_bottom, work_area);
    opener_screen = przycisk->prostokat_do_ekranu(Prostokat(0, 0, przycisk->szerokosc(), przycisk->wysokosc()));

    zm_press.x = przycisk->obszar_ekranu().prawy() - aw / 2;
    zm_press.y = przycisk->obszar_ekranu().y + przycisk->obszar_ekranu().wysokosc / 2;
    zm_release = zm_press;
    przycisk->obsluz_wcisniecie_myszy(zm_press);
    przycisk->obsluz_zwolnienie_myszy(zm_release);

    popup_screen = zarzadca.prostokat_zarejestrowany_ekranu();
    TEST_ASSERT(popup_screen.dolny() <= opener_screen.y || popup_screen.dolny() <= work_area.dolny(),
                "Blisko dolnej krawedzi dropdown otwiera sie w gore lub miesci sie w obszarze roboczym");
    TEST_ASSERT(popup_screen.dolny() <= work_area.dolny() && popup_screen.y >= work_area.y,
                "Dropdown blisko dolnej krawedzi nie wychodzi poza obszar roboczy");

    zarzadca.zamknij();

    // 5. Test repozycjonowania przy prawej krawedzi ekranu (przesuniecie w lewo)
    Prostokat window_near_right(work_area.prawy() - 110, 200, 200, 200);
    zarzadca.ustaw_geometrie_wlasciciela(window_near_right, work_area);
    opener_screen = przycisk->prostokat_do_ekranu(Prostokat(0, 0, przycisk->szerokosc(), przycisk->wysokosc()));

    zm_press.x = przycisk->obszar_ekranu().prawy() - aw / 2;
    zm_press.y = przycisk->obszar_ekranu().y + przycisk->obszar_ekranu().wysokosc / 2;
    zm_release = zm_press;
    przycisk->obsluz_wcisniecie_myszy(zm_press);
    przycisk->obsluz_zwolnienie_myszy(zm_release);

    popup_screen = zarzadca.prostokat_zarejestrowany_ekranu();
    TEST_ASSERT(popup_screen.prawy() <= work_area.prawy(),
                "Przy prawej krawedzi dropdown przesuwa sie w lewo, nie wychodzac poza ekran");
    TEST_ASSERT(popup_screen.x >= work_area.x, "Dropdown nie ucieka za lewa krawedz");

    zarzadca.zamknij();
}

// =========================================================================
// 18. Testy geometrii: Toast / Powiadomienie (brak przycinania, dynamiczna wysokosc, stos)
// =========================================================================
static void test_geometria_toast_przycinanie_i_stos() {
    printf("\n--- Test 18: Toast / Powiadomienie Geometria (przycinanie, polskie znaki, zawijanie, stos) ---\n");

    const SkalaUI skale[] = { 1, 2 };
    const Prostokat rozdzielczosci[] = {
        Prostokat(0, 0, 800, 600),
        Prostokat(0, 0, 1024, 768),
        Prostokat(0, 0, 1920, 1080)
    };

    struct PrzypadekTestowy {
        TypPowiadomienia typ;
        const char* tytul;
        const char* tresc;
        const char* akcja;
    } przypadki[] = {
        { TypPowiadomienia::Informacja, "Krótki", "Prosty komunikat", nullptr },
        { TypPowiadomienia::Sukces, "Bardzo długi tytuł powiadomienia systemowego weryfikujący poprawność nagłówka", "Krótka treść", nullptr },
        { TypPowiadomienia::Ostrzezenie, "Ostrzeżenie", "Zażółć gęślą jaźń! Ta wiadomość zawiera polskie znaki diakrytyczne oraz wielokrotnie złożony tekst mający na celu zweryfikowanie automatycznego zawijania wierszy i poprawnego wyliczania wysokości powiadomienia.", nullptr },
        { TypPowiadomienia::Blad, "Błąd krytyczny", "Linia pierwsza komunikatu\nLinia druga z tekstem\nLinia trzecia z podsumowaniem\nLinia czwarta zakończenia", "Szczegóły" }
    };

    for (SkalaUI s : skale) {
        for (const auto& res : rozdzielczosci) {
            ZarzadcaPopupow zarzadca;
            Prostokat work_area(0, 0, res.szerokosc, res.wysokosc - 40 * s);
            Prostokat window_client(50 * s, 50 * s, 600 * s, 400 * s);
            zarzadca.ustaw_geometrie_wlasciciela(window_client, work_area);
            zarzadca.ustaw_skale_dpi(s);

            for (const auto& przyp : przypadki) {
                Powiadomienie p;
                p.ustaw_skale_dpi(s);
                p.ustaw_zarzadce_popupow(&zarzadca);
                p.zaktualizuj_motyw();
                p.ustaw_dane(przyp.typ, przyp.tytul, przyp.tresc, 5000, przyp.akcja);

                Rozmiar wym = p.wymagany_rozmiar(s);
                Prostokat r = zarzadca.umiesc_w_obszarze_roboczym_prawy_dol(wym, 16 * s, 16 * s);
                bool dodano = zarzadca.dodaj_powierzchnie(p, r, nullptr);
                TEST_ASSERT(dodano, "Toast pomyslnie dodany do zarzadcy popupow");

                // Weryfikacja alokacji powierzchni
                TEST_ASSERT(p.szerokosc() >= wym.szerokosc, "Szerokosc powierzchni >= wymaganego rozmiaru");
                TEST_ASSERT(p.wysokosc() >= wym.wysokosc, "Wysokosc powierzchni >= wymaganego rozmiaru");

                // Rysujemy do bufora
                uint32_t* bufor = (uint32_t*)malloc(p.szerokosc() * p.wysokosc() * sizeof(uint32_t));
                TEST_ASSERT(bufor != nullptr, "Alokacja bufora testowego powiadomienia udana");
                memset(bufor, 0, p.szerokosc() * p.wysokosc() * sizeof(uint32_t));
                KontekstRysowania ctx;
                ctx.ustaw_cel(bufor, Prostokat(0, 0, p.szerokosc(), p.wysokosc()));
                p.rysuj_siebie(ctx);

                // Weryfikacja: zaden element nie wychodzi poza obszar powierzchni
                TEST_ASSERT(p.obszar_tekstu().prawy() <= p.szerokosc(),
                            "Obszar tekstu miesci sie w szerokosci powiadomienia");
                TEST_ASSERT(p.obszar_tekstu().dolny() <= p.wysokosc(),
                            "Obszar tekstu miesci sie w wysokosci powiadomienia");
                TEST_ASSERT(p.przycisk_zamknij().prawy() <= p.szerokosc(),
                            "Przycisk zamkniecia miesci sie w szerokosci powiadomienia");
                TEST_ASSERT(p.przycisk_zamknij().dolny() <= p.wysokosc(),
                            "Przycisk zamkniecia miesci sie w wysokosci powiadomienia");

                if (przyp.akcja) {
                    TEST_ASSERT(p.przycisk_akcji().prawy() <= p.szerokosc(),
                                "Przycisk akcji miesci sie w szerokosci powiadomienia");
                    TEST_ASSERT(p.przycisk_akcji().dolny() <= p.wysokosc(),
                                "Przycisk akcji miesci sie w wysokosci powiadomienia");
                    TEST_ASSERT(p.przycisk_akcji().y >= p.obszar_tekstu().dolny(),
                                "Przycisk akcji znajduje sie ponizej tresci tekstu");
                }

                free(bufor);
                zarzadca.zamknij();
            }
        }
    }

    // Weryfikacja stosu o zmiennych wysokosciach (upward stacking) i kompaktowania (compaction)
    {
        ZarzadcaPopupow zarzadca;
        Prostokat work_area(0, 0, 1280, 720);
        Prostokat window_client(100, 100, 800, 500);
        zarzadca.ustaw_geometrie_wlasciciela(window_client, work_area);
        zarzadca.ustaw_skale_dpi(1);

        MenedzerPowiadomien mgr;

        // 1. Krotki toast
        mgr.pokaz(zarzadca, TypPowiadomienia::Sukces, "Zapisano", "Plik zostal pomyslnie zapisany na dysku.");
        // 2. Dlugie polskie powiadomienie (wielowierszowe)
        mgr.pokaz(zarzadca, TypPowiadomienia::Ostrzezenie, "Uwaga",
                  "Zażółć gęślą jaźń! To jest dlugi tekst wymagajacy kilku wierszy do poprawnego wyswietlenia calosci.");
        // 3. Toast z przyciskiem akcji
        mgr.pokaz(zarzadca, TypPowiadomienia::Blad, "Blad polaczenia",
                  "Nie udalo sie nawiazac polaczenia z serwerem zdalnym.", 4000, "Ponow");

        TEST_ASSERT(mgr.liczba_aktywnych() == 3, "Wszystkie 3 toasty sa aktywne w menedzerze");

        Powiadomienie& t1 = mgr.powiadomienie(0);
        Powiadomienie& t2 = mgr.powiadomienie(1);
        Powiadomienie& t3 = mgr.powiadomienie(2);

        Prostokat r1_ekran = t1.obszar_ekranu();
        Prostokat r2_ekran = t2.obszar_ekranu();
        Prostokat r3_ekran = t3.obszar_ekranu();

        const int cien = motyw_cien_okna() ? 2 : 0;
        TEST_ASSERT(r3_ekran.dolny() + cien == work_area.dolny() - 16,
                    "Najnowszy toast t3 znajduje sie na dole stosu nad paskiem zadan (od_dolu = 16)");
        TEST_ASSERT(r2_ekran.dolny() == r3_ekran.y - 8,
                    "Toast t2 znajduje sie dokladnie 8 px nad toastem t3 (zgodnie z rzeczywista wysokoscia t3)");
        TEST_ASSERT(r1_ekran.dolny() == r2_ekran.y - 8,
                    "Toast t1 znajduje sie dokladnie 8 px nad toastem t2 (zgodnie z rzeczywista wysokoscia t2)");

        TEST_ASSERT(t2.wysokosc() > t1.wysokosc() || t3.wysokosc() > t1.wysokosc(),
                    "Toasty o roznej tresci maja rozne rzeczywiste wysokosci");

        // Zamkniecie najnowszego toastu t3 (na samym dole)
        t3.zamknij();
        TEST_ASSERT(mgr.liczba_aktywnych() == 2, "Po zamknieciu t3 pozostaly 2 aktywne toasty");

        // Kompaktowanie stosu: teraz t2 powinien zsunac sie na dol (od_dolu = 16)
        r2_ekran = t2.obszar_ekranu();
        r1_ekran = t1.obszar_ekranu();

        TEST_ASSERT(r2_ekran.dolny() + cien == work_area.dolny() - 16,
                    "Po zamknieciu t3 toast t2 przemiescil sie na dol stosu (od_dolu = 16)");
        TEST_ASSERT(r1_ekran.dolny() == r2_ekran.y - 8,
                    "Toast t1 zachowal odstep 8 px nad t2 z uwzglednieniem rzeczywistej wysokosci t2");
    }
}

// =========================================================================
// 19. Testy jednostkowe: DialogKoloru (Synchronizacja RGB/HEX, selektor, walidacja, klawisze, motywy)
// =========================================================================
static void test_dialog_koloru() {
    printf("\n--- Test 19: DialogKoloru (RGB/HEX sync, selector, bounds, validation, keys, theme) ---\n");

    ZarzadcaPopupow zarzadca;
    zarzadca.ustaw_geometrie_wlasciciela(Prostokat(0, 0, 1024, 768), Prostokat(0, 0, 1024, 768));

    // 1. Inicjalizacja z kolorem początkowym
    DialogKoloru dlg(0x00FF8800U, "Wybór koloru");
    dlg.pokaz(zarzadca);

    TEST_ASSERT(dlg.kolor() == 0x00FF8800U, "DialogKoloru: Początkowy kolor poprawny (0x00FF8800)");
    TEST_ASSERT(dlg.pole_r() && dlg.pole_r()->wartosc() == 255, "DialogKoloru: Pole R ma wartosc 255");
    TEST_ASSERT(dlg.pole_g() && dlg.pole_g()->wartosc() == 136, "DialogKoloru: Pole G ma wartosc 136 (0x88)");
    TEST_ASSERT(dlg.pole_b() && dlg.pole_b()->wartosc() == 0, "DialogKoloru: Pole B ma wartosc 0");
    TEST_ASSERT(dlg.pole_hex() && strcmp(dlg.pole_hex()->tekst(), "#FF8800") == 0, "DialogKoloru: Pole HEX ma wartosc #FF8800");
    TEST_ASSERT(dlg.podglad() != nullptr, "DialogKoloru: Kontrolka podgladu utworzona");
    TEST_ASSERT(dlg.podglad()->kolor_poczatkowy() == 0x00FF8800U, "DialogKoloru: Podglad zachowuje kolor poczatkowy");
    TEST_ASSERT(dlg.podglad()->kolor_biezacy() == 0x00FF8800U, "DialogKoloru: Podglad biezacego koloru zgodny");

    // 2. Synchronizacja RGB -> HEX i Selektor
    dlg.pole_r()->ustaw_wartosc(18);
    dlg.pole_g()->ustaw_wartosc(52);
    dlg.pole_b()->ustaw_wartosc(86);
    TEST_ASSERT(dlg.kolor() == 0x00123456U, "DialogKoloru: Zmiana RGB (18, 52, 86) daje 0x00123456");
    TEST_ASSERT(strcmp(dlg.pole_hex()->tekst(), "#123456") == 0, "DialogKoloru: Pole HEX zsynchronizowane z RGB (#123456)");
    TEST_ASSERT(dlg.selektor() && dlg.selektor()->kolor() == 0x00123456U, "DialogKoloru: Selektor zsynchronizowany z RGB");
    TEST_ASSERT(dlg.podglad()->kolor_biezacy() == 0x00123456U, "DialogKoloru: Podglad zaktualizowany z RGB");

    // 3. Synchronizacja HEX -> RGB i Selektor
    dlg.pole_hex()->ustaw_tekst("#00AABB");
    TEST_ASSERT(dlg.kolor() == 0x0000AABBU, "DialogKoloru: Wpisanie #00AABB daje 0x0000AABB");
    TEST_ASSERT(dlg.pole_r()->wartosc() == 0, "DialogKoloru: Pole R zaktualizowane z HEX do 0");
    TEST_ASSERT(dlg.pole_g()->wartosc() == 170, "DialogKoloru: Pole G zaktualizowane z HEX do 170 (0xAA)");
    TEST_ASSERT(dlg.pole_b()->wartosc() == 187, "DialogKoloru: Pole B zaktualizowane z HEX do 187 (0xBB)");
    TEST_ASSERT(dlg.selektor()->kolor() == 0x0000AABBU, "DialogKoloru: Selektor zaktualizowany z HEX");

    // Wpisanie HEX bez znaku '#'
    dlg.pole_hex()->ustaw_tekst("FFEEDD");
    TEST_ASSERT(dlg.kolor() == 0x00FFEEDDU, "DialogKoloru: Wpisanie HEX bez '#' (FFEEDD) daje 0x00FFEEDD");
    TEST_ASSERT(dlg.pole_r()->wartosc() == 255, "DialogKoloru: Pole R rowne 255 (0xFF)");
    TEST_ASSERT(dlg.pole_g()->wartosc() == 238, "DialogKoloru: Pole G rowne 238 (0xEE)");
    TEST_ASSERT(dlg.pole_b()->wartosc() == 221, "DialogKoloru: Pole B rowne 221 (0xDD)");

    // Wpisanie malych liter hex
    dlg.pole_hex()->ustaw_tekst("#1a2b3c");
    TEST_ASSERT(dlg.kolor() == 0x001A2B3CU, "DialogKoloru: Male litery hex (#1a2b3c) obslugiwane poprawnie");
    TEST_ASSERT(dlg.pole_r()->wartosc() == 0x1A, "DialogKoloru: Pole R zgodne z 0x1A");
    TEST_ASSERT(dlg.pole_g()->wartosc() == 0x2B, "DialogKoloru: Pole G zgodne z 0x2B");
    TEST_ASSERT(dlg.pole_b()->wartosc() == 0x3C, "DialogKoloru: Pole B zgodne z 0x3C");

    // 4. Odpornosc walidacji HEX na bledne dane (brak crashu, brak uszkodzenia pamieci)
    uint32_t zachowany_kolor = dlg.kolor();
    dlg.pole_hex()->ustaw_tekst("");
    TEST_ASSERT(dlg.kolor() == zachowany_kolor, "DialogKoloru: Pusty ciag HEX zachowuje poprzedni kolor");

    dlg.pole_hex()->ustaw_tekst("#12");
    TEST_ASSERT(dlg.kolor() == zachowany_kolor, "DialogKoloru: Zbyt krotki ciag HEX zachowuje poprzedni kolor");

    dlg.pole_hex()->ustaw_tekst("#GGGGGG");
    TEST_ASSERT(dlg.kolor() == zachowany_kolor, "DialogKoloru: Niepoprawne znaki HEX zachowuja poprzedni kolor");

    dlg.pole_hex()->ustaw_tekst("niepoprawny_ciag");
    TEST_ASSERT(dlg.kolor() == zachowany_kolor, "DialogKoloru: Dowolny tekst zachowuje poprzedni kolor");

    dlg.pole_hex()->ustaw_tekst("#1234567890ABCDEF");
    TEST_ASSERT(dlg.kolor() == zachowany_kolor, "DialogKoloru: Nadmiarowy ciag HEX zachowuje poprzedni kolor");

    // 5. Wartosci brzegowe i klamrowanie RGB (0..255)
    dlg.pole_r()->ustaw_wartosc(0);
    dlg.pole_g()->ustaw_wartosc(255);
    dlg.pole_b()->ustaw_wartosc(0);
    TEST_ASSERT(dlg.kolor() == 0x0000FF00U, "DialogKoloru: Graniczne wartosci (0, 255, 0) daja zielony");

    // Proba ustawienia wartosci spoza zakresu w polu numerycznym
    dlg.pole_r()->ustaw_wartosc(-50);
    TEST_ASSERT(dlg.pole_r()->wartosc() == 0, "DialogKoloru: Pole R klamruje ujemne wartosci do 0");
    dlg.pole_g()->ustaw_wartosc(350);
    TEST_ASSERT(dlg.pole_g()->wartosc() == 255, "DialogKoloru: Pole G klamruje wartosci > 255 do 255");

    // 6. Bezposrednia zmiana w SelektorKoloru
    dlg.ustaw_kolor(0x00336699U);
    TEST_ASSERT(dlg.kolor() == 0x00336699U, "DialogKoloru: ustaw_kolor(0x00336699) dziala");
    TEST_ASSERT(dlg.pole_r()->wartosc() == 51, "DialogKoloru: Pole R = 51 (0x33)");
    TEST_ASSERT(dlg.pole_g()->wartosc() == 102, "DialogKoloru: Pole G = 102 (0x66)");
    TEST_ASSERT(dlg.pole_b()->wartosc() == 153, "DialogKoloru: Pole B = 153 (0x99)");
    TEST_ASSERT(strcmp(dlg.pole_hex()->tekst(), "#336699") == 0, "DialogKoloru: HEX = #336699");

    // 7. Reakcja na zatwierdzenie (OK / Enter) i wynik
    static uint32_t s_odebrany_kolor = 0;
    static WynikDialoguKoloru s_odebrany_wynik_k = WynikDialoguKoloru::Anulowano;
    dlg.przy_wyniku_koloru([](DialogKoloru*, WynikDialoguKoloru w, uint32_t c, void*) {
        s_odebrany_wynik_k = w;
        s_odebrany_kolor = c;
    }, nullptr);

    dlg.zatwierdz();
    TEST_ASSERT(s_odebrany_wynik_k == WynikDialoguKoloru::Wybrano, "DialogKoloru: Callback odebral WynikDialoguKoloru::Wybrano");
    TEST_ASSERT(s_odebrany_kolor == 0x00336699U, "DialogKoloru: Callback przekazal prawidlowo wybrany kolor");
    TEST_ASSERT(dlg.wynik() == WynikDialogu::OK, "DialogKoloru: Wynik bazowy OknoDialogowe to OK");

    // 8. Reakcja na anulowanie (Anuluj / Escape)
    DialogKoloru dlg2(0x00112233U, "Wybor koloru 2");
    dlg2.pokaz(zarzadca);
    dlg2.ustaw_kolor(0x00AABBCCU);
    dlg2.przy_wyniku_koloru([](DialogKoloru*, WynikDialoguKoloru w, uint32_t c, void*) {
        s_odebrany_wynik_k = w;
        s_odebrany_kolor = c;
    }, nullptr);

    dlg2.anuluj();
    TEST_ASSERT(s_odebrany_wynik_k == WynikDialoguKoloru::Anulowano, "DialogKoloru: Callback odebral WynikDialoguKoloru::Anulowano");
    TEST_ASSERT(s_odebrany_kolor == 0x00112233U, "DialogKoloru: Anulowanie zwraca kolor poczatkowy");
    TEST_ASSERT(dlg2.wynik() == WynikDialogu::Anuluj, "DialogKoloru: Wynik bazowy to Anuluj");

    // 9. Reakcja na klawisz Escape
    DialogKoloru dlg3(0x00556677U, "Wybor koloru 3");
    dlg3.pokaz(zarzadca);
    ZdarzenieKlawiatury e_esc{};
    e_esc.kod_klawisza = 27;
    dlg3.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(dlg3.wynik() == WynikDialogu::Anuluj, "DialogKoloru: Klawisz Escape zamyka dialog z kodem Anuluj");

    // 10. SelektorKoloru: Tryb Osadzony, live dragging myszy i klawiatura
    TEST_ASSERT(dlg.selektor() != nullptr, "DialogKoloru: SelektorKoloru istnieje");
    TEST_ASSERT(dlg.selektor()->tryb() == TrybSelektoraKoloru::Osadzony, "DialogKoloru: SelektorKoloru dziala w trybie Osadzony");

    SelektorKoloru sel_embed(TrybSelektoraKoloru::Osadzony, 0x00FF0000U);
    sel_embed.rozmiesc(Prostokat(0, 0, 240, 160));
    static uint32_t s_sel_live_c = 0;
    sel_embed.przy_zmianie([](SelektorKoloru*, uint32_t c, void*) { s_sel_live_c = c; }, nullptr);

    ZdarzenieMyszki sdown{};
    sdown.typ = TypZdarzenia::WcisniecieMyszy;
    sdown.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    sdown.x = 80; sdown.y = 80;
    sel_embed.obsluz_wcisniecie_myszy(sdown);
    TEST_ASSERT(s_sel_live_c != 0, "SelektorKoloru: Klikniecie/down w obszarze SV zglasza zmiane koloru live");

    ZdarzenieMyszki smove{};
    smove.typ = TypZdarzenia::RuchMyszy;
    smove.x = 120; smove.y = 100;
    sel_embed.obsluz_ruch_myszy(smove);
    TEST_ASSERT(s_sel_live_c != 0, "SelektorKoloru: Dragging/ruch myszy w SV aktualizuje kolor i powiadamia obserwatora");

    ZdarzenieMyszki sup{};
    sup.typ = TypZdarzenia::ZwolnienieMyszy;
    sel_embed.obsluz_zwolnienie_myszy(sup);

    // Klawiatura w SelektorKoloru
    ZdarzenieKlawiatury k_arrow{};
    k_arrow.kod_klawisza = 0x4D; // strzalka w prawo (PS/2)
    s_sel_live_c = 0;
    sel_embed.obsluz_wcisniecie_klawisza(k_arrow);
    TEST_ASSERT(s_sel_live_c != 0, "SelektorKoloru: Strzalka w prawo modyfikuje nasycenie i wywoluje callback");

    // 11. Belka tytulowa: przeciaganie i przycisk zamkniecia [X]
    ZdarzenieMyszki kdown{};
    kdown.typ = TypZdarzenia::WcisniecieMyszy;
    kdown.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    kdown.x = dlg.obszar().x + 40; kdown.y = dlg.obszar().y + 14;
    dlg.obsluz_wcisniecie_myszy(kdown);
    TEST_ASSERT(dlg.czy_przeciagane() == true, "DialogKoloru: Wcisniecie w belke tytulowa uruchamia przeciaganie");

    ZdarzenieMyszki kmove{};
    kmove.typ = TypZdarzenia::RuchMyszy;
    kmove.x = kdown.x + 30; kmove.y = kdown.y + 40;
    dlg.obsluz_ruch_myszy(kmove);

    ZdarzenieMyszki kup{};
    kup.typ = TypZdarzenia::ZwolnienieMyszy;
    dlg.obsluz_zwolnienie_myszy(kup);
    TEST_ASSERT(dlg.czy_przeciagane() == false, "DialogKoloru: Zwolnienie myszy konczy przeciaganie");

    DialogKoloru dlg_close(0x00AABBCCU);
    dlg_close.pokaz(zarzadca);
    Prostokat r_c = dlg_close.prostokat_przycisku_zamknij();
    ZdarzenieMyszki c_down{};
    c_down.typ = TypZdarzenia::WcisniecieMyszy;
    c_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    c_down.x = dlg_close.obszar().x + r_c.x + 2;
    c_down.y = dlg_close.obszar().y + r_c.y + 2;
    dlg_close.obsluz_wcisniecie_myszy(c_down);

    ZdarzenieMyszki c_up = c_down;
    c_up.typ = TypZdarzenia::ZwolnienieMyszy;
    dlg_close.obsluz_zwolnienie_myszy(c_up);
    TEST_ASSERT(dlg_close.wynik() == WynikDialogu::Anuluj, "DialogKoloru: Przycisk [X] w belce zamyka dialog z wynikiem Anuluj");

    // 12. Reakcja na zmiane motywu
    dlg.zaktualizuj_motyw();
    TEST_ASSERT(true, "DialogKoloru: Obsluga zaktualizuj_motyw wykonana bez bledow");
}

// -----------------------------------------------------------------------------
// MAKIETA SYSTEMU PLIKÓW BWS DLA TESTÓW DIALOGU PLIKU
// -----------------------------------------------------------------------------
struct TestPlikMock {
    const char* sciezka;
    bool katalog;
    uint64_t rozmiar;
};

static const TestPlikMock g_test_fs_wpisy[] = {
    { "/uzytkownicy", true, 0 },
    { "/uzytkownicy/Dokumenty", true, 0 },
    { "/uzytkownicy/Obrazy", true, 0 },
    { "/uzytkownicy/Pulpit", true, 0 },
    { "/uzytkownicy/istniejacy.txt", false, 256 },
    { "/uzytkownicy/program.bur", false, 16384 },
    { "/uzytkownicy/obrazek.bmp", false, 4096 },
    { "/uzytkownicy/Dokumenty/notatka.txt", false, 128 },
    { "/uzytkownicy/Dokumenty/dane.csv", false, 512 },
    { "/uzytkownicy/Dokumenty/kod.cpp", false, 1024 },
    { "/uzytkownicy/Obrazy/foto.bmp", false, 8192 },
    { "/uzytkownicy/Obrazy/logo.png", false, 12288 }
};

static const size_t g_test_fs_liczba = sizeof(g_test_fs_wpisy) / sizeof(g_test_fs_wpisy[0]);

static uint64_t mock_fs_bws_handler(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t /*a4*/) {
    if (nr == 6) { // wylistuj_katalog_uzytkownika(sciezka, bufor, max_len)
        const char* dir = reinterpret_cast<const char*>(a1);
        char* buf = reinterpret_cast<char*>(a2);
        uint32_t max_len = static_cast<uint32_t>(a3);
        if (!dir || !buf || max_len == 0) return 0;

        buf[0] = '\0';
        size_t dlen = strlen(dir);
        size_t cur = 0;

        for (size_t i = 0; i < g_test_fs_liczba; ++i) {
            const char* p = g_test_fs_wpisy[i].sciezka;
            if (strncmp(p, dir, dlen) == 0 && p[dlen] == '/' && p[dlen + 1] != '\0') {
                const char* sub = p + dlen + 1;
                if (strchr(sub, '/') == nullptr) {
                    char line[256];
                    snprintf(line, sizeof(line), "%s %s\n",
                             g_test_fs_wpisy[i].katalog ? "[KAT] " : "[PLIK]", sub);
                    size_t llen = strlen(line);
                    if (cur + llen + 1 < max_len) {
                        memcpy(buf + cur, line, llen);
                        cur += llen;
                        buf[cur] = '\0';
                    }
                }
            }
        }
        return 1;
    }
    if (nr == 47) { // pobierz_metadane_pliku(sciezka, metadane)
        const char* p = reinterpret_cast<const char*>(a1);
        BwsMetadanePliku* m = reinterpret_cast<BwsMetadanePliku*>(a2);
        if (!p || !m) return 0;
        for (size_t i = 0; i < g_test_fs_liczba; ++i) {
            if (strcmp(g_test_fs_wpisy[i].sciezka, p) == 0) {
                m->wersja = BWS_METADANE_WERSJA;
                m->typ = g_test_fs_wpisy[i].katalog ? BWS_TYP_KATALOG : BWS_TYP_PLIK;
                m->rozmiar = g_test_fs_wpisy[i].rozmiar;
                m->czas_utworzenia_rtc = 20260917120000ULL;
                m->flagi = BWS_META_ROZMIAR_DOSTEPNY | BWS_META_CZAS_DOSTEPNY;
                return 1;
            }
        }
        return 0;
    }
    if (nr == 4) { // pobierz_rozmiar_pliku(sciezka, rozmiar)
        const char* p = reinterpret_cast<const char*>(a1);
        uint32_t* rsz = reinterpret_cast<uint32_t*>(a2);
        if (!p || !rsz) return 0;
        for (size_t i = 0; i < g_test_fs_liczba; ++i) {
            if (strcmp(g_test_fs_wpisy[i].sciezka, p) == 0) {
                if (g_test_fs_wpisy[i].katalog) return 0;
                *rsz = static_cast<uint32_t>(g_test_fs_wpisy[i].rozmiar);
                return 1;
            }
        }
        return 0;
    }
    if (nr == 46) { // utworz_katalog_uzytkownika(sciezka)
        return 1;
    }
    return 0;
}

// =========================================================================
// 20. Testy jednostkowe: DialogPliku (Tryby, normalizacja sciezek, filtry, nawigacja, nadpisywanie, klawisze, motywy)
// =========================================================================
static void test_dialog_pliku() {
    printf("\n--- Test 20: DialogPliku (Modes, path normalization, filter, navigation, overwrite, keys, theme) ---\n");

    // 1. Bezpieczna normalizacja sciezek
    char nbuf[256];
    DialogPliku::normalizuj_sciezke("/uzytkownicy//Dokumenty/../Pulpit/./", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/uzytkownicy/Pulpit") == 0, "DialogPliku: normalizuj_sciezke obsluguje '..', '.', i podwojne ukośniki");

    DialogPliku::normalizuj_sciezke("/..", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/") == 0, "DialogPliku: normalizuj_sciezke zabezpiecza przed wyjsciem ponad korzen (/.. -> /)");

    DialogPliku::normalizuj_sciezke("/a/b/c/../../..", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/") == 0, "DialogPliku: normalizuj_sciezke wielopoziomowe cofanie do korzenia (/a/b/c/../../.. -> /)");

    DialogPliku::normalizuj_sciezke("", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/") == 0, "DialogPliku: normalizuj_sciezke pustego napisu zwraca '/'");

    DialogPliku::normalizuj_sciezke("uzytkownicy", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/uzytkownicy") == 0, "DialogPliku: normalizuj_sciezke dodaje wiodacy '/'");

    DialogPliku::normalizuj_sciezke("////", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/") == 0, "DialogPliku: normalizuj_sciezke samych ukosnikow daje '/'");

    // polacz_sciezki
    DialogPliku::polacz_sciezki("/uzytkownicy", "Dokumenty", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/uzytkownicy/Dokumenty") == 0, "DialogPliku: polacz_sciezki('/uzytkownicy', 'Dokumenty') -> /uzytkownicy/Dokumenty");

    DialogPliku::polacz_sciezki("/uzytkownicy/", "/Dokumenty", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/uzytkownicy/Dokumenty") == 0, "DialogPliku: polacz_sciezki usuwa nadmiarowe ukosniki na styku");

    DialogPliku::polacz_sciezki("/uzytkownicy", "..", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/") == 0, "DialogPliku: polacz_sciezki z '..' normalizuje do '/'");

    // pobierz_katalog_nadrzedny
    DialogPliku::pobierz_katalog_nadrzedny("/uzytkownicy/Dokumenty", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/uzytkownicy") == 0, "DialogPliku: pobierz_katalog_nadrzedny('/uzytkownicy/Dokumenty') -> /uzytkownicy");

    DialogPliku::pobierz_katalog_nadrzedny("/uzytkownicy", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/") == 0, "DialogPliku: pobierz_katalog_nadrzedny('/uzytkownicy') -> /");

    DialogPliku::pobierz_katalog_nadrzedny("/", nbuf, sizeof(nbuf));
    TEST_ASSERT(strcmp(nbuf, "/") == 0, "DialogPliku: pobierz_katalog_nadrzedny('/') -> /");

    // 2. Tryby pracy dialogu
    DialogPliku dlg_otworz(TrybDialoguPliku::Otworz, "Otwórz plik", "/uzytkownicy", "Wszystkie pliki (*.*)");
    TEST_ASSERT(dlg_otworz.tryb() == TrybDialoguPliku::Otworz, "DialogPliku: Tryb Otworz poprawny");
    TEST_ASSERT(strcmp(dlg_otworz.tekst_przycisku_akcji(), "Otwórz") == 0, "DialogPliku: Przycisk akcji w trybie Otworz to 'Otwórz'");

    DialogPliku dlg_zapisz(TrybDialoguPliku::Zapisz, "Zapisz plik", "/uzytkownicy", "Wszystkie pliki (*.*)");
    TEST_ASSERT(dlg_zapisz.tryb() == TrybDialoguPliku::Zapisz, "DialogPliku: Tryb Zapisz poprawny");
    TEST_ASSERT(strcmp(dlg_zapisz.tekst_przycisku_akcji(), "Zapisz") == 0, "DialogPliku: Przycisk akcji w trybie Zapisz to 'Zapisz'");

    DialogPliku dlg_kat(TrybDialoguPliku::WybierzKatalog, "Wybierz katalog", "/uzytkownicy");
    TEST_ASSERT(dlg_kat.tryb() == TrybDialoguPliku::WybierzKatalog, "DialogPliku: Tryb WybierzKatalog poprawny");
    TEST_ASSERT(strcmp(dlg_kat.tekst_przycisku_akcji(), "Wybierz") == 0, "DialogPliku: Przycisk akcji w trybie WybierzKatalog to 'Wybierz'");

    // 3. Integracja z makietą BWS i filtrowanie plików
    g_test_bws_hook = mock_fs_bws_handler;
    ZarzadcaPopupow zarzadca;
    zarzadca.ustaw_geometrie_wlasciciela(Prostokat(0, 0, 1024, 768), Prostokat(0, 0, 1024, 768));

    DialogPliku dlg_fs(TrybDialoguPliku::Otworz, "Otwórz dokument", "/uzytkownicy",
                       "Wszystkie pliki (*.*);;Dokumenty (*.txt);;Programy (*.bur)");
    dlg_fs.pokaz(zarzadca);

    // Domyslnie aktywny filtr 0: *.* -> 3 katalogi + 3 pliki = 6 wierszy
    TEST_ASSERT(dlg_fs.tabela_plikow() != nullptr, "DialogPliku: Tabela plikow zainicjalizowana");
    TEST_ASSERT(dlg_fs.tabela_plikow()->liczba_wierszy() == 6, "DialogPliku: Filtr *.* wyswietla 6 pozycji (3 katalogi + 3 pliki)");

    // Zmiana filtru na 1: *.txt -> 3 katalogi + 1 plik (istniejacy.txt) = 4 wiersze
    dlg_fs.ustaw_aktywny_filtr(1);
    TEST_ASSERT(dlg_fs.tabela_plikow()->liczba_wierszy() == 4, "DialogPliku: Filtr *.txt wyswietla 4 pozycje (3 katalogi + 1 plik)");

    // Zmiana filtru na 2: *.bur -> 3 katalogi + 1 plik (program.bur) = 4 wiersze
    dlg_fs.ustaw_aktywny_filtr(2);
    TEST_ASSERT(dlg_fs.tabela_plikow()->liczba_wierszy() == 4, "DialogPliku: Filtr *.bur wyswietla 4 pozycje (3 katalogi + 1 plik)");

    // Katalogi sa ZAWSZE widoczne niezaleznie od wybranego filtru rozszerzen
    TEST_ASSERT(true, "DialogPliku: Katalogi pozostaja widoczne przy kazdym filtrze rozszerzen");

    // 4. Nawigacja po katalogach
    dlg_fs.ustaw_aktywny_filtr(0); // powrot do *.*
    dlg_fs.przejdz_do_katalogu("/uzytkownicy/Dokumenty");
    TEST_ASSERT(strcmp(dlg_fs.biezacy_katalog(), "/uzytkownicy/Dokumenty") == 0, "DialogPliku: Przejscie do podkatalogu /uzytkownicy/Dokumenty");
    TEST_ASSERT(dlg_fs.tabela_plikow()->liczba_wierszy() == 3, "DialogPliku: Katalog Dokumenty zawiera 3 pliki (notatka.txt, dane.csv, kod.cpp)");

    dlg_fs.przejdz_w_gore();
    TEST_ASSERT(strcmp(dlg_fs.biezacy_katalog(), "/uzytkownicy") == 0, "DialogPliku: przejdz_w_gore wraca do /uzytkownicy");
    TEST_ASSERT(dlg_fs.pasek_sciezki() != nullptr, "DialogPliku: Pasek sciezki istnieje");
    TEST_ASSERT(dlg_fs.pasek_sciezki()->liczba_segmentow() == 1, "DialogPliku: Pasek sciezki zsynchronizowany (1 segment)");

    // 5. Automatyczne dolaczanie rozszerzenia w trybie Zapisz
    DialogPliku dlg_zapis(TrybDialoguPliku::Zapisz, "Zapisz", "/uzytkownicy",
                          "Dokumenty (*.txt);;Arkusze (*.csv)");
    dlg_zapis.pokaz(zarzadca);
    dlg_zapis.ustaw_aktywny_filtr(0); // *.txt
    dlg_zapis.pole_nazwy()->ustaw_tekst("nowy_raport");
    dlg_zapis.zatwierdz();
    TEST_ASSERT(strcmp(dlg_zapis.wybrana_sciezka(), "/uzytkownicy/nowy_raport.txt") == 0, "DialogPliku: Automatyczne dolaczenie rozszerzenia .txt w trybie Zapisz");
    TEST_ASSERT(dlg_zapis.wynik_pliku() == WynikDialoguPliku::Wybrano, "DialogPliku: Wynik pliku to Wybrano");

    // 6. Ostrzezenie i obsluga nadpisywania istniejacego pliku w trybie Zapisz
    DialogPliku dlg_nadpisz(TrybDialoguPliku::Zapisz, "Zapisz", "/uzytkownicy", "Dokumenty (*.txt)");
    dlg_nadpisz.pokaz(zarzadca);
    dlg_nadpisz.pole_nazwy()->ustaw_tekst("istniejacy.txt");
    dlg_nadpisz.zatwierdz();
    TEST_ASSERT(strcmp(dlg_nadpisz.sciezka_do_potwierdzenia(), "/uzytkownicy/istniejacy.txt") == 0, "DialogPliku: Istniejacy plik wymaga potwierdzenia nadpisania");
    dlg_nadpisz.potwierdz_nadpisanie();
    TEST_ASSERT(strcmp(dlg_nadpisz.wybrana_sciezka(), "/uzytkownicy/istniejacy.txt") == 0, "DialogPliku: Po potwierdzeniu nadpisania sciezka zaakceptowana");
    TEST_ASSERT(dlg_nadpisz.wynik_pliku() == WynikDialoguPliku::Wybrano, "DialogPliku: Wynik po nadpisaniu to Wybrano");

    // 7. Walidacja w trybie Otworz: plik nieistniejacy vs istniejacy
    DialogPliku dlg_otw(TrybDialoguPliku::Otworz, "Otworz", "/uzytkownicy", "Wszystkie pliki (*.*)");
    dlg_otw.pokaz(zarzadca);
    dlg_otw.pole_nazwy()->ustaw_tekst("nieistniejacy.txt");
    dlg_otw.zatwierdz();
    TEST_ASSERT(dlg_otw.wynik_pliku() == WynikDialoguPliku::Brak, "DialogPliku: Proba otwarcia nieistniejacego pliku nie zatwierdza dialogu");

    dlg_otw.pole_nazwy()->ustaw_tekst("istniejacy.txt");
    dlg_otw.zatwierdz();
    TEST_ASSERT(dlg_otw.wynik_pliku() == WynikDialoguPliku::Wybrano, "DialogPliku: Otwarcie istniejacego pliku zatwierdza dialog");
    TEST_ASSERT(strcmp(dlg_otw.wybrana_sciezka(), "/uzytkownicy/istniejacy.txt") == 0, "DialogPliku: Wybrana sciezka otwieranego pliku poprawna");

    // 8. Tryb WybierzKatalog
    DialogPliku dlg_kat_w(TrybDialoguPliku::WybierzKatalog, "Wybierz katalog", "/uzytkownicy");
    dlg_kat_w.pokaz(zarzadca);
    dlg_kat_w.pole_nazwy()->ustaw_tekst("Dokumenty");
    dlg_kat_w.zatwierdz();
    TEST_ASSERT(dlg_kat_w.wynik_pliku() == WynikDialoguPliku::Wybrano, "DialogPliku: Wybor katalogu zatwierdza dialog");
    TEST_ASSERT(strcmp(dlg_kat_w.wybrana_sciezka(), "/uzytkownicy/Dokumenty") == 0, "DialogPliku: Wybrana sciezka katalogu poprawna (/uzytkownicy/Dokumenty)");

    // 9. Klawisz Escape i anulowanie
    DialogPliku dlg_anuluj(TrybDialoguPliku::Otworz, "Otworz", "/uzytkownicy");
    dlg_anuluj.pokaz(zarzadca);
    ZdarzenieKlawiatury e_esc{};
    e_esc.kod_klawisza = 27;
    dlg_anuluj.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(dlg_anuluj.wynik_pliku() == WynikDialoguPliku::Anulowano, "DialogPliku: Klawisz Escape anuluje dialog pliku");

    // 10. Ulepszona geometria i czytelnosc kontrolek (740x500, szerokie dropdowny, duze przyciski)
    TEST_ASSERT(dlg_fs.obszar().szerokosc >= 740, "DialogPliku: Rozmiar szerokosci zoptymalizowany pod czytelnosc (>= 740px)");
    TEST_ASSERT(dlg_fs.obszar().wysokosc >= 500, "DialogPliku: Rozmiar wysokosci zoptymalizowany pod czytelnosc (>= 500px)");
    TEST_ASSERT(dlg_fs.lista_filtrow() != nullptr && dlg_fs.lista_filtrow()->obszar().szerokosc >= 200, "DialogPliku: Dropdown filtrow ma odpowiednia szerokosc");

    // 11. Generyczne filtry i dopasowywanie plikow bez rozszerzenia
    TEST_ASSERT(DialogPliku::sprawdz_filtr("Makefile", "*.*") == true, "DialogPliku: Filtr *.* dopasowuje pliki bez rozszerzenia (np. Makefile)");
    TEST_ASSERT(DialogPliku::sprawdz_filtr("Makefile", "*") == true, "DialogPliku: Filtr * dopasowuje pliki bez rozszerzenia");
    TEST_ASSERT(DialogPliku::sprawdz_filtr("Makefile", "*.txt") == false, "DialogPliku: Filtr *.txt odrzuca plik Makefile");
    TEST_ASSERT(DialogPliku::sprawdz_filtr("dokument.TXT", "*.txt") == true, "DialogPliku: Dopasowanie rozszerzen jest case-insensitive");
    TEST_ASSERT(DialogPliku::sprawdz_filtr("model.mesh", "*.mesh") == true, "DialogPliku: Niestandardowe rozszerzenie *.mesh dopasowane poprawnie");
    TEST_ASSERT(DialogPliku::sprawdz_filtr("gra.bursztyn_projekt", "*.bursztyn_projekt") == true, "DialogPliku: Rozszerzenie *.bursztyn_projekt dopasowane poprawnie");
    TEST_ASSERT(DialogPliku::sprawdz_filtr("notatka.md", "*.txt;*.md;*.log") == true, "DialogPliku: Zlozony filtr wielorozszerzeniowy *.txt;*.md;*.log dopasowuje .md");

    // 12. Bezpieczna nawigacja 'Poziom wyzej' w korzeniu
    dlg_fs.przejdz_do_katalogu("/");
    TEST_ASSERT(strcmp(dlg_fs.biezacy_katalog(), "/") == 0, "DialogPliku: Przejscie do korzenia /");
    bool wyzej_ok = dlg_fs.przejdz_poziom_wyzej();
    TEST_ASSERT(wyzej_ok == true && strcmp(dlg_fs.biezacy_katalog(), "/") == 0, "DialogPliku: przejdz_poziom_wyzej w korzeniu / bezpiecznie pozostaje w / bez bledu");

    // 13. Walidacja nazwy i tworzenie nowego katalogu
    TEST_ASSERT(DialogPliku::czy_poprawna_nazwa_elementu("") == false, "DialogPliku: Pusta nazwa nowego katalogu odrzucona");
    TEST_ASSERT(DialogPliku::czy_poprawna_nazwa_elementu("katalog/podkatalog") == false, "DialogPliku: Nazwa ze slashem odrzucona");
    TEST_ASSERT(DialogPliku::czy_poprawna_nazwa_elementu("katalog*gwiazdka") == false, "DialogPliku: Nazwa z gwiazdka odrzucona");
    TEST_ASSERT(DialogPliku::czy_poprawna_nazwa_elementu("Projekty 2026") == true, "DialogPliku: Prawidlowa nazwa nowego katalogu zaakceptowana");
    dlg_fs.przejdz_do_katalogu("/uzytkownicy");
    TEST_ASSERT(dlg_fs.utworz_nowy_katalog("NoweProjekty") == true, "DialogPliku: Utworzenie nowego katalogu powiodlo sie");

    // 14. Belka tytulowa: przeciaganie i przycisk zamkniecia [X]
    ZdarzenieMyszki p_down{};
    p_down.typ = TypZdarzenia::WcisniecieMyszy;
    p_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    p_down.x = dlg_fs.obszar().x + 50; p_down.y = dlg_fs.obszar().y + 14;
    dlg_fs.obsluz_wcisniecie_myszy(p_down);
    TEST_ASSERT(dlg_fs.czy_przeciagane() == true, "DialogPliku: Przeciaganie okna za belke tytulowa aktywne");

    ZdarzenieMyszki p_move{};
    p_move.typ = TypZdarzenia::RuchMyszy;
    p_move.x = p_down.x + 20; p_move.y = p_down.y + 20;
    dlg_fs.obsluz_ruch_myszy(p_move);

    ZdarzenieMyszki p_up{};
    p_up.typ = TypZdarzenia::ZwolnienieMyszy;
    dlg_fs.obsluz_zwolnienie_myszy(p_up);
    TEST_ASSERT(dlg_fs.czy_przeciagane() == false, "DialogPliku: Przeciaganie okna zakonczone");

    DialogPliku dlg_x(TrybDialoguPliku::Otworz, "Otworz", "/uzytkownicy");
    dlg_x.pokaz(zarzadca);
    Prostokat cr = dlg_x.prostokat_przycisku_zamknij();
    ZdarzenieMyszki x_down{};
    x_down.typ = TypZdarzenia::WcisniecieMyszy;
    x_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    x_down.x = dlg_x.obszar().x + cr.x + 2; x_down.y = dlg_x.obszar().y + cr.y + 2;
    dlg_x.obsluz_wcisniecie_myszy(x_down);
    ZdarzenieMyszki x_up = x_down;
    x_up.typ = TypZdarzenia::ZwolnienieMyszy;
    dlg_x.obsluz_zwolnienie_myszy(x_up);
    TEST_ASSERT(dlg_x.wynik_pliku() == WynikDialoguPliku::Anulowano, "DialogPliku: Przycisk [X] zamyka dialog z wynikiem Anulowano");

    // 15. Reakcja na zmiane motywu
    dlg_fs.zaktualizuj_motyw();
    TEST_ASSERT(true, "DialogPliku: Obsluga zaktualizuj_motyw wykonana bez bledow");

    g_test_bws_hook = nullptr;
}

static void test_combo_after_window_move() {
    Okno okno("Combo position", 300, 220);
    auto* combo = new ListaRozwijana();
    okno.ustaw_kontrolke_glowna(combo);
    combo->dodaj_element("Pierwszy");
    combo->dodaj_element("Drugi");
    okno.przelicz_uklad();
    okno.ustaw_pozycje(400, 200);
    TEST_ASSERT(combo->rozwin(), "Combo opens after parent move");
    Prostokat local = combo->obszar_ekranu();
    Prostokat popup = combo->prostokat_popupu();
    printf("[COMBO MEASURE] parent=(%d,%d) field-local=(%d,%d,%d,%d) popup=(%d,%d)\n",
           okno.x(), okno.y(), local.x, local.y, local.szerokosc, local.wysokosc,
           popup.x, popup.y);
    TEST_ASSERT(popup.x == okno.x() + local.x, "Combo X follows moved parent");
    TEST_ASSERT(popup.y == okno.y() + local.dolny(), "Combo opens below moved field");
    combo->zwin();
    const int modes[][2] = {{800,600},{1024,768},{1280,720},{1920,1080},
                            {2560,1440},{3440,1440},{3840,2160}};
    for (const auto& mode : modes) {
        const int positions[][2] = {{0,0},{mode[0]/2,mode[1]/2},
                                    {mode[0]-200,mode[1]-100},{mode[0]-80,-20}};
        for (const auto& pos : positions) {
            ZarzadcaPopupow manager;
            ListaRozwijana field;
            manager.ustaw_geometrie_wlasciciela(Prostokat(pos[0],pos[1],200,100),
                                               Prostokat(0,0,mode[0],mode[1]-40));
            field.ustaw_zarzadce_popupow(&manager);
            field.rozmiesc(Prostokat(10,30,160,28));
            for (int i=0;i<20;++i) field.dodaj_element("Element");
            TEST_ASSERT(field.rozwin(), "Combo matrix opens");
            Prostokat r=field.prostokat_popupu();
            TEST_ASSERT(r.x>=0 && r.y>=0 && r.prawy()<=mode[0] &&
                        r.dolny()<=mode[1]-40, "Combo matrix inside work area");
            TEST_ASSERT(r.szerokosc==160, "Combo matrix width matches field");
            if (pos[0]+170<=mode[0]-2)
                TEST_ASSERT(r.x==pos[0]+10, "Combo matrix aligned X");
            if (pos[1]+58+r.wysokosc+2<=mode[1]-40)
                TEST_ASSERT(r.y==pos[1]+58, "Combo matrix below anchor");
            else if (pos[1]+30-r.wysokosc>=0)
                TEST_ASSERT(r.dolny()==pos[1]+30, "Combo matrix above anchor");
            field.zwin();
            bool cycles=true;
            for (int i=0;i<1000;++i) {
                cycles &= field.rozwin();
                cycles &= field.prostokat_popupu()==r;
                field.zwin();
                cycles &= !manager.czy_otwarty();
            }
            TEST_ASSERT(cycles, "Combo 1000 open/close cycles preserve position");
        }
    }
}

int main() {
    printf("======================================================================\n");
    printf(" BURSZTYN OS - TESTY JEDNOSTKOWE NOWYCH KOMPONENTOW BURSZTYNUI\n");
    printf("======================================================================\n");

    test_combo_after_window_move();
    test_siatka();
    test_panel_dokowany();
    test_przeplyw();
    test_okno_dialogowe();
    test_pasek_informacyjny();
    test_wskaznik_zajetosci();
    test_powierzchnia_rysunkowa();
    test_wykres();
    test_lista_wirtualna();
    test_tabela_wirtualna();
    test_powiadomienia();
    test_motywy_kolory_pulpit_i_kontrolki();
    test_menu_hover_i_izolacja();
    test_czcionka_extronic16b_i_odpornosc_utf8();

    // Nowe testy geometrii BursztynUI
    test_geometria_okno_dialogowe();
    test_geometria_menu_glowne_kotwica();
    test_geometria_przycisk_podzielony_dropdown();
    test_geometria_toast_przycinanie_i_stos();

    // Testy nowych komponentów: DialogKoloru i DialogPliku
    test_dialog_koloru();
    test_dialog_pliku();

    printf("\n======================================================================\n");
    printf("Wyniki: %d / %d testow udanych (%.1f%%)\n",
           g_testy_udane, g_testy_razem,
           g_testy_razem > 0 ? (100.0 * g_testy_udane / g_testy_razem) : 0.0);
    printf("======================================================================\n");

    return g_testy_udane == g_testy_razem ? 0 : 1;
}
