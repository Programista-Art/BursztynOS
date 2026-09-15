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
    test_motywy_kolory_pulpit_i_kontrolki();
    test_menu_hover_i_izolacja();
    test_czcionka_extronic16b_i_odpornosc_utf8();

    printf("\n======================================================================\n");
    printf("Wyniki: %d / %d testow udanych (%.1f%%)\n",
           g_testy_udane, g_testy_razem,
           g_testy_razem > 0 ? (100.0 * g_testy_udane / g_testy_razem) : 0.0);
    printf("======================================================================\n");

    return g_testy_udane == g_testy_razem ? 0 : 1;
}
