/*
 * Aplikacja Bursztyn OS (Zgodna z C++17, bursztyn_gui.h i ABI Linkera)
 * Wygenerowana przez: Bursztyn Builder RAD v2.9
 */
#include "bursztyn_gui.h"
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Format .bur (Naglowek wymagany przez Loadera)
// -----------------------------------------------------------------------------

struct NaglowekBur {
    uint8_t  magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

#ifndef BUR_TEKST_ROZMIAR
#define BUR_TEKST_ROZMIAR 32768ULL
#endif

static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 4096ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY      = 0x601000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = BUR_TEKST_PRZESUNIECIE + BUR_TEKST_ROZMIAR;
static constexpr uint64_t BUR_DANE_WIRTUALNY       = BUR_TEKST_WIRTUALNY + BUR_TEKST_ROZMIAR;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 131072ULL;

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
        BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
    };
}

// -----------------------------------------------------------------------------
// Zmienne preprocesora dla pętli GUI
// -----------------------------------------------------------------------------
static int WIN_X = 310;
static int WIN_Y = 150;
static int WIN_W = 180;
static int WIN_H = 220;
static bool zmaksymalizowane = false;
static bool aplikacja_zminimalizowana = false;
static bool dragging = false;
static int drag_off_x = 0;
static int drag_off_y = 0;
static int screen_w = 1024, screen_h = 768;
static int restore_x = 0, restore_y = 0, restore_w = 0, restore_h = 0;

static bool combo_otwarty_TComboBox_3_3 = false;
static int  combo_wybor_TComboBox_3_3 = 0;

static bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void ogranicz_pozycje_okna(int* x, int* y) {
    if (!x || !y) return;
    int max_x = screen_w - WIN_W;
    int max_y = screen_h - 40 - WIN_H;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x > max_x) *x = max_x;
    if (*y > max_y) *y = max_y;
}

static void RysujInterfejs(bool wyczysc_warstwe) {
    if (wyczysc_warstwe) gui_odswiez_pulpit();
    if (aplikacja_zminimalizowana) return;

    // Tlo Okna i Belka Systemowa
    gui_rysuj_okno(WIN_X, WIN_Y, WIN_W, WIN_H, "Uruchom Apkę");
    gui_rysuj_standardowa_belke(WIN_X, WIN_Y, WIN_W, "Uruchom Apkę", zmaksymalizowane);

    // TGroupBox_4 (TGroupBox)
    gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 40, 140, 90, 0xFF121212);
    gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 40, 140, 1, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 40, 1, 90, 0x00404040);
    gui_wypisz_tekst_kolor_skala(WIN_X + 20 + 6, WIN_Y + 40 - 6, 0x00A0A0A0, 1, "Uruchom aplikację");
    // TComboBox_3 (TComboBox)
    gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 140, 140, 24, 0xFF723737);
    gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 140, 140, 1, 0x00404040);
    { const char* combo_items_TComboBox_3_3[] = { "Opcja 1", "Opcja 2", "Opcja 3" };
      const int combo_n_TComboBox_3_3 = 3;
      const char* combo_aktualny_TComboBox_3_3 = (combo_wybor_TComboBox_3_3 >= 0 && combo_wybor_TComboBox_3_3 < combo_n_TComboBox_3_3) ? combo_items_TComboBox_3_3[combo_wybor_TComboBox_3_3] : "";
      gui_wypisz_tekst_kolor_skala(WIN_X + 20 + 4, WIN_Y + 140 + 4, 0xFF000000, 1, combo_aktualny_TComboBox_3_3);
      gui_rysuj_prostokat(WIN_X + 142, WIN_Y + 140, 18, 24, 0x00E0E0E0);
      gui_wypisz_tekst_kolor_skala(WIN_X + 146, WIN_Y + 144, 0x00303030, 1, combo_otwarty_TComboBox_3_3 ? "^" : "v");
      if (combo_otwarty_TComboBox_3_3) {
          const int lista_h_TComboBox_3_3 = combo_n_TComboBox_3_3 * 16;
          gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 140 + 24, 140, lista_h_TComboBox_3_3, 0x00FFFFFF);
          gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 140 + 24, 140, 1, 0x00404040);
          for (int i_TComboBox_3_3 = 0; i_TComboBox_3_3 < combo_n_TComboBox_3_3; ++i_TComboBox_3_3) {
              int iy_TComboBox_3_3 = WIN_Y + 140 + 24 + i_TComboBox_3_3 * 16;
              if (i_TComboBox_3_3 == combo_wybor_TComboBox_3_3) gui_rysuj_prostokat(WIN_X + 20, iy_TComboBox_3_3, 140, 16, 0x000078D7);
              gui_wypisz_tekst_kolor_skala(WIN_X + 20 + 4, iy_TComboBox_3_3 + 2, i_TComboBox_3_3 == combo_wybor_TComboBox_3_3 ? 0x00FFFFFF : 0x00000000, 1, combo_items_TComboBox_3_3[i_TComboBox_3_3]);
          }
      }
    }
    // TButton_2 (TButton)
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50, 120, 30, 0xFF440808);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50, 120, 1, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50 + 30 - 1, 120, 1, 0x00101010);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50, 1, 30, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 30 + 120 - 1, WIN_Y + 50, 1, 30, 0x00101010);
    rysuj_tekst_wysrodkowany(WIN_X + 30, WIN_Y + 50, 120, 30, 1, 0xFFFFFFFF, "Notatnik");
    // TButton_5 (TButton)
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 90, 120, 30, 0xFF17446E);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 90, 120, 1, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 90 + 30 - 1, 120, 1, 0x00101010);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 90, 1, 30, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 30 + 120 - 1, WIN_Y + 90, 1, 30, 0x00101010);
    rysuj_tekst_wysrodkowany(WIN_X + 30, WIN_Y + 90, 120, 30, 1, 0xFFFFFFFF, "Kalkulator");

    gui_odswiez();
}

extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    if (screen_w <= 0 || screen_h <= 0) gui_zakoncz_aplikacje();
    ogranicz_pozycje_okna(&WIN_X, &WIN_Y);
    if (bws_utworz_warstwe(WIN_X, WIN_Y, WIN_W, WIN_H, 10) < 0) gui_zakoncz_aplikacje();
    gui_ustaw_przejecie_myszy(true);
    bool wyjdz = false;
    RysujInterfejs(true);

    while (!wyjdz) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        if (zdarzenie.typ == BWS_ZDARZENIE_FOCUS && aplikacja_zminimalizowana) aplikacja_zminimalizowana = false;
        const int mx = zdarzenie.x; const int my = zdarzenie.y;
        const bool lewy = (zdarzenie.przyciski & 0x01U) != 0;
        const bool klik_lewy = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;
        const bool puszczenie_lewego = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP;
        bool redraw = false; bool pelne_czyszczenie = false;

        if (aplikacja_zminimalizowana) continue;

        // Klikniecie w pozycje rozwinietej listy ComboBox (obsluga PRZED reszta trafien,
        // bo lista rysuje sie NAD innymi kontrolkami).
        bool combo_klik_obsluzony = false;
        if (klik_lewy) {
            if (combo_otwarty_TComboBox_3_3) {
                const int n_TComboBox_3_3 = 3;
                if (punkt_w_prostokacie(mx, my, WIN_X + 20, WIN_Y + 140 + 24, 140, n_TComboBox_3_3 * 16)) {
                    int wybrany_TComboBox_3_3 = (my - (WIN_Y + 140 + 24)) / 16;
                    if (wybrany_TComboBox_3_3 >= 0 && wybrany_TComboBox_3_3 < n_TComboBox_3_3) combo_wybor_TComboBox_3_3 = wybrany_TComboBox_3_3;
                    combo_otwarty_TComboBox_3_3 = false; redraw = true; pelne_czyszczenie = true; combo_klik_obsluzony = true;
                } else if (punkt_w_prostokacie(mx, my, WIN_X + 20, WIN_Y + 140, 140, 24)) {
                    combo_otwarty_TComboBox_3_3 = false; redraw = true; pelne_czyszczenie = true; combo_klik_obsluzony = true;
                } else {
                    combo_otwarty_TComboBox_3_3 = false; redraw = true; pelne_czyszczenie = true;
                }
            }
        }

        if (!combo_klik_obsluzony && klik_lewy && punkt_w_prostokacie(mx, my, WIN_X, WIN_Y, WIN_W, WIN_H)) {
            const gui_akcja_belki akcja = gui_hit_test_belki(mx, my, WIN_X, WIN_Y, WIN_W);
            if (akcja == GUI_BELKA_MINIMALIZUJ) {
                aplikacja_zminimalizowana = gui_minimalizuj_okno();
                dragging = false; gui_ustaw_capture_myszy(false);
            } else if (akcja == GUI_BELKA_ZAMKNIJ) {
                wyjdz = true; dragging = false;
            } else if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                if (!zmaksymalizowane) {
                    restore_x = WIN_X; restore_y = WIN_Y; restore_w = WIN_W; restore_h = WIN_H;
                    WIN_X = 0; WIN_Y = 0; WIN_W = screen_w; WIN_H = screen_h - 40;
                    zmaksymalizowane = true;
                } else {
                    WIN_X = restore_x; WIN_Y = restore_y; WIN_W = restore_w; WIN_H = restore_h;
                    zmaksymalizowane = false;
                }
                if (bws_utworz_warstwe(WIN_X, WIN_Y, WIN_W, WIN_H, 10) < 0) gui_zakoncz_aplikacje();
                redraw = true; pelne_czyszczenie = true;
            } else if (akcja == GUI_BELKA_DRAG && !zmaksymalizowane) {
                dragging = true; gui_ustaw_capture_myszy(true);
                drag_off_x = mx - WIN_X; drag_off_y = my - WIN_Y;
            } else {
                if (punkt_w_prostokacie(mx, my, WIN_X + 30, WIN_Y + 90, 120, 30)) {
                    bws_wywolaj(10, (uint64_t)"/programy/kalkulator.cebula/kalkulator.bur");
                    redraw = true; pelne_czyszczenie = true;
                }
                else if (punkt_w_prostokacie(mx, my, WIN_X + 30, WIN_Y + 50, 120, 30)) {
                    bws_wywolaj(10, (uint64_t)"/programy/notatnik.cebula/notatnik.bur");
                    redraw = true; pelne_czyszczenie = true;
                }
                else if (punkt_w_prostokacie(mx, my, WIN_X + 20, WIN_Y + 140, 140, 24)) {
                    combo_otwarty_TComboBox_3_3 = !combo_otwarty_TComboBox_3_3;
                    redraw = true; pelne_czyszczenie = true;
                }
            }
        }

        if (dragging && lewy) {
            int nowy_x = mx - drag_off_x; int nowy_y = my - drag_off_y;
            ogranicz_pozycje_okna(&nowy_x, &nowy_y);
            if (nowy_x != WIN_X || nowy_y != WIN_Y) { WIN_X = nowy_x; WIN_Y = nowy_y; bws_przesun_warstwe(WIN_X, WIN_Y); gui_odswiez(); }
        }
        if (puszczenie_lewego && dragging) { dragging = false; gui_ustaw_capture_myszy(false); }
        if (redraw && !wyjdz) RysujInterfejs(pelne_czyszczenie);
    }
    gui_zakoncz_aplikacje();
}
