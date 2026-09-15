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

// Implementacje makiet funkcji GUI i BWS na potrzeby host test
extern "C" {
    uint64_t bws_wywolaj(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) { return 0; }
    __attribute__((noreturn)) void bws_zakoncz_proces() { exit(0); }
}

void* gui_malloc(unsigned long rozmiar) { return malloc(rozmiar); }
void gui_free(void* ptr) { free(ptr); }
void* gui_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
void gui_rysuj_prostokat(int, int, int, int, uint32_t) {}
bool gui_blit(int, int, int, int, const uint32_t*) { return true; }
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
void gui_pobierz_rozdzielczosc(int* w, int* h) {
    if (w) *w = 1280;
    if (h) *h = 720;
}
const BursztynMotyw* gui_pobierz_motyw() {
    return &g_domyslny_motyw_fallback;
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
    printf("\n--- Test 4: OknoDialogowe (Buttons, Enter/Escape hotkeys, callbacks) ---\n");

    // OkAnuluj
    OknoDialogowe d1("Pytanie", "Czy zapisac plik?", PrzyciskiDialogu::OkAnuluj);
    TEST_ASSERT(strcmp(d1.wiadomosc(), "Czy zapisac plik?") == 0, "Wiadomosc dialogu OK");
    d1.przy_wyniku(cb_dialog_wynik, nullptr);

    // Symulacja Enter (kod 13) -> OK
    ZdarzenieKlawiatury e_enter{};
    e_enter.kod_klawisza = 13;
    d1.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(e_enter.obsluzone, "Enter obsluzony przez dialog");
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::OK, "Wynik Enter w OkAnuluj to OK");

    // Symulacja Escape (kod 27) -> Anuluj
    g_odebrany_wynik = WynikDialogu::Brak;
    ZdarzenieKlawiatury e_esc{};
    e_esc.kod_klawisza = 27;
    d1.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(e_esc.obsluzone, "Escape obsluzony przez dialog");
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Anuluj, "Wynik Escape w OkAnuluj to Anuluj");

    // TakNie
    OknoDialogowe d2("Potwierdzenie", "Czy kontynuowac?", PrzyciskiDialogu::TakNie);
    d2.przy_wyniku(cb_dialog_wynik, nullptr);

    e_enter.obsluzone = false;
    d2.obsluz_wcisniecie_klawisza(e_enter);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Tak, "Wynik Enter w TakNie to Tak");

    e_esc.obsluzone = false;
    d2.obsluz_wcisniecie_klawisza(e_esc);
    TEST_ASSERT(g_odebrany_wynik == WynikDialogu::Nie, "Wynik Escape w TakNie to Nie");
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
}

int main() {
    printf("======================================================================\n");
    printf(" BURSZTYN OS - TESTY JEDNOSTKOWE NOWYCH KOMPONENTOW BURSZTYNUI\n");
    printf("======================================================================\n");

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

    printf("\n======================================================================\n");
    printf("Wyniki: %d / %d testow udanych (%.1f%%)\n",
           g_testy_udane, g_testy_razem,
           g_testy_razem > 0 ? (100.0 * g_testy_udane / g_testy_razem) : 0.0);
    printf("======================================================================\n");

    return g_testy_udane == g_testy_razem ? 0 : 1;
}
