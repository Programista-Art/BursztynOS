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

// Mocks for BursztynUI in host test
extern "C" {
    uint64_t bws_wywolaj(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) { return 0; }
    __attribute__((noreturn)) void bws_zakoncz_proces() { exit(0); }
}

uint8_t motyw_cien_okna() { return 1; }
bool clipboard_set(const char*, const void*, uint64_t) { return true; }
bool clipboard_read(const char*, void*, uint64_t, uint64_t*) { return false; }

void* gui_malloc(unsigned long r) { return malloc(r); }
void gui_free(void* p) { free(p); }
void* gui_realloc(void* p, size_t s) { return realloc(p, s); }
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
static int g_screen_w = 1280, g_screen_h = 720;
void gui_pobierz_rozdzielczosc(int* w, int* h) {
    if (w) *w = g_screen_w;
    if (h) *h = g_screen_h;
}

static const BursztynMotyw* g_aktywny_test_motyw = &g_domyslny_motyw_fallback;
const BursztynMotyw* gui_pobierz_motyw() { return g_aktywny_test_motyw; }

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

uint8_t motyw_grubosc_ramki() { return g_aktywny_test_motyw->gruboscramki; }

#include "../bursztyn_ui/typy.h"
#include "../bursztyn_ui/rysowanie.h"
#include "../bursztyn_ui/popup.h"
#include "../bursztyn_ui/kontrolka.h"
#include "../bursztyn_ui/lista_rozwijana.h"
#include "../bursztyn_ui/okno.h"

using namespace BursztynUI;

// Write framebuffer to PPM
static void save_ppm(const char* filename, int width, int height, const uint32_t* fb) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t pixel = fb[y * width + x];
            uint8_t rgb[3] = {
                static_cast<uint8_t>((pixel >> 16) & 0xFF),
                static_cast<uint8_t>((pixel >> 8) & 0xFF),
                static_cast<uint8_t>(pixel & 0xFF)
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("[SAVED PPM] %s (%dx%d)\n", filename, width, height);
}

// Draw standard desktop background: amber wallpaper, 15 desktop icons, taskbar, Start button, RTC clock
static void draw_desktop_background(uint32_t* fb, int width, int height) {
    KontekstRysowania k;
    k.ustaw_cel(fb, Prostokat(0, 0, width, height));

    // 1. Wallpaper: rich dark amber gradient
    for (int y = 0; y < height; ++y) {
        float factor = static_cast<float>(y) / height;
        uint8_t r = static_cast<uint8_t>(26 + factor * 20);
        uint8_t g = static_cast<uint8_t>(11 + factor * 14);
        uint8_t b = static_cast<uint8_t>(2 + factor * 6);
        uint32_t col = (r << 16) | (g << 8) | b;
        for (int x = 0; x < width; ++x) {
            fb[y * width + x] = col;
        }
    }

    // 2. 15 Desktop Icons
    const char* icons[15] = {
        "Komputer", "Dokumenty", "Ustawienia", "Kalkulator", "Notatnik",
        "Eksplorator", "Terminal", "Siec", "Kosz", "Tapety",
        "Aktualizacje", "Przegladarka", "Snake", "Kontrolki", "Testy"
    };
    int icon_w = 70, icon_h = 60, margin_x = 20, margin_y = 20;
    int cur_x = margin_x, cur_y = margin_y;
    for (int i = 0; i < 15; ++i) {
        // Icon backdrop
        k.rysuj_zaokraglony_prostokat(Prostokat(cur_x + 15, cur_y, 40, 36), 0x003A2208, 4);
        k.rysuj_ramke(Prostokat(cur_x + 15, cur_y, 40, 36), 0x00E58B00, 1);
        // Icon text
        k.rysuj_tekst(Punkt(cur_x + 5, cur_y + 40), icons[i], 0x00FFFFFF);

        cur_y += icon_h + 16;
        if (cur_y + icon_h + 40 >= height - 40) {
            cur_y = margin_y;
            cur_x += icon_w + 16;
        }
    }

    // 3. Taskbar (PASEK_WYS = 40)
    int taskbar_y = height - 40;
    k.rysuj_prostokat(Prostokat(0, taskbar_y, width, 40), 0x00141414);
    k.rysuj_prostokat(Prostokat(0, taskbar_y, width, 1), 0x00333333);

    // Start button
    k.rysuj_zaokraglony_prostokat(Prostokat(6, taskbar_y + 5, 80, 30), 0x002B1D0E, 4);
    k.rysuj_ramke(Prostokat(6, taskbar_y + 5, 80, 30), 0x00E58B00, 1);
    k.rysuj_tekst(Punkt(26, taskbar_y + 12), "Start", 0x00FFFFFF);

    // Clock
    k.rysuj_tekst(Punkt(width - 60, taskbar_y + 12), "10:30", 0x00E0E0E0);
}

// Draw mouse cursor
static void draw_cursor(uint32_t* fb, int width, int height, int cx, int cy) {
    static const char* cursor_shape[16] = {
        "X               ",
        "XX              ",
        "X.X             ",
        "X..X            ",
        "X...X           ",
        "X....X          ",
        "X.....X         ",
        "X......X        ",
        "X.......X       ",
        "X.....XXXX      ",
        "X..X..X         ",
        "X.X X..X        ",
        "XX   X..X       ",
        "X     X..X      ",
        "       XX       ",
        "                "
    };
    for (int dy = 0; dy < 16; ++dy) {
        for (int dx = 0; dx < 16; ++dx) {
            int px = cx + dx;
            int py = cy + dy;
            if (px >= 0 && px < width && py >= 0 && py < height) {
                char c = cursor_shape[dy][dx];
                if (c == 'X') {
                    fb[py * width + px] = 0x00000000;
                } else if (c == '.') {
                    fb[py * width + px] = 0x00FFFFFF;
                }
            }
        }
    }
}

// Case 1: ComboBox at 1280x720
static void render_combobox_1280x720() {
    const int W = 1280, H = 720;
    g_screen_w = W; g_screen_h = H;
    uint32_t* fb = static_cast<uint32_t*>(calloc(W * H, sizeof(uint32_t)));

    draw_desktop_background(fb, W, H);

    // Window
    Okno okno("Ustawienia motywu", 460, 280);
    okno.ustaw_pozycje(360, 160);
    okno.przelicz_uklad();

    ZarzadcaPopupow popup_mgr;
    popup_mgr.ustaw_geometrie_wlasciciela(Prostokat(okno.x(), okno.y(), okno.szerokosc(), okno.wysokosc()), Prostokat(0, 0, W, H - 40));

    ListaRozwijana combo;
    combo.ustaw_zarzadce_popupow(&popup_mgr);
    combo.rozmiesc(Prostokat(40, 60, 240, 28));
    combo.dodaj_element("Bursztyn Ciemny");
    combo.dodaj_element("Bursztyn Jasny");
    combo.dodaj_element("Nocny Szmaragd");
    combo.dodaj_element("Wysoki Kontrast");
    combo.rozwin();

    // Render window
    KontekstRysowania kw;
    kw.ustaw_cel(fb, Prostokat(0, 0, W, H));
    okno.rysuj(kw);

    // Render popup directly on top in compositor z-order
    Prostokat pr = combo.prostokat_popupu();
    kw.rysuj_zaokraglony_prostokat(pr, 0x00222222, 4);
    kw.rysuj_ramke(pr, 0x00E58B00, 1);
    const char* items[] = {"Bursztyn Ciemny", "Bursztyn Jasny", "Nocny Szmaragd", "Wysoki Kontrast"};
    for (int i = 0; i < 4; ++i) {
        Prostokat item_r(pr.x + 2, pr.y + 4 + i * 22, pr.szerokosc - 4, 20);
        if (i == 1) { // hover/selected
            kw.rysuj_zaokraglony_prostokat(item_r, 0x00E58B00, 2);
            kw.rysuj_tekst(Punkt(item_r.x + 8, item_r.y + 2), items[i], 0x00000000);
        } else {
            kw.rysuj_tekst(Punkt(item_r.x + 8, item_r.y + 2), items[i], 0x00DDDDDD);
        }
    }

    // Cursor pointing at item 1
    draw_cursor(fb, W, H, pr.x + 100, pr.y + 32);

    save_ppm("/tmp/combobox_1280x720.ppm", W, H, fb);
    free(fb);
}

// Case 2: ComboBox at 1920x1080
static void render_combobox_1920x1080() {
    const int W = 1920, H = 1080;
    g_screen_w = W; g_screen_h = H;
    uint32_t* fb = static_cast<uint32_t*>(calloc(W * H, sizeof(uint32_t)));

    draw_desktop_background(fb, W, H);

    Okno okno("Ustawienia ekranu", 540, 360);
    okno.ustaw_pozycje(640, 280);
    okno.przelicz_uklad();

    ZarzadcaPopupow popup_mgr;
    popup_mgr.ustaw_geometrie_wlasciciela(Prostokat(okno.x(), okno.y(), okno.szerokosc(), okno.wysokosc()), Prostokat(0, 0, W, H - 40));

    ListaRozwijana combo;
    combo.ustaw_zarzadce_popupow(&popup_mgr);
    combo.rozmiesc(Prostokat(50, 80, 280, 32));
    combo.dodaj_element("1920x1080 (Full HD)");
    combo.dodaj_element("1280x720 (HD)");
    combo.dodaj_element("2560x1440 (QHD)");
    combo.dodaj_element("3440x1440 (UWQHD)");
    combo.dodaj_element("3840x2160 (4K UHD)");
    combo.rozwin();

    KontekstRysowania kw;
    kw.ustaw_cel(fb, Prostokat(0, 0, W, H));
    okno.rysuj(kw);

    Prostokat pr = combo.prostokat_popupu();
    kw.rysuj_zaokraglony_prostokat(pr, 0x00222222, 4);
    kw.rysuj_ramke(pr, 0x00E58B00, 1);
    const char* items[] = {"1920x1080 (Full HD)", "1280x720 (HD)", "2560x1440 (QHD)", "3440x1440 (UWQHD)", "3840x2160 (4K UHD)"};
    for (int i = 0; i < 5; ++i) {
        Prostokat item_r(pr.x + 2, pr.y + 4 + i * 22, pr.szerokosc - 4, 20);
        if (i == 0) { // selected
            kw.rysuj_zaokraglony_prostokat(item_r, 0x00E58B00, 2);
            kw.rysuj_tekst(Punkt(item_r.x + 8, item_r.y + 2), items[i], 0x00000000);
        } else {
            kw.rysuj_tekst(Punkt(item_r.x + 8, item_r.y + 2), items[i], 0x00DDDDDD);
        }
    }

    draw_cursor(fb, W, H, pr.x + 120, pr.y + 14);

    save_ppm("/tmp/combobox_1920x1080.ppm", W, H, fb);
    free(fb);
}

// Case 3: ComboBox near bottom-right edge (opens above anchor)
static void render_combobox_bottom_right() {
    const int W = 1280, H = 720;
    g_screen_w = W; g_screen_h = H;
    uint32_t* fb = static_cast<uint32_t*>(calloc(W * H, sizeof(uint32_t)));

    draw_desktop_background(fb, W, H);

    // Window near bottom right edge: x = 920, y = 460, width = 340, height = 210
    Okno okno("Konfiguracja urzadzen", 340, 210);
    okno.ustaw_pozycje(920, 450);
    okno.przelicz_uklad();

    ZarzadcaPopupow popup_mgr;
    popup_mgr.ustaw_geometrie_wlasciciela(Prostokat(okno.x(), okno.y(), okno.szerokosc(), okno.wysokosc()), Prostokat(0, 0, W, H - 40));

    ListaRozwijana combo;
    combo.ustaw_zarzadce_popupow(&popup_mgr);
    // Control at local y = 140 -> screen y = 450 + 140 = 590, bottom = 618.
    // Work area bottom is 680. Below room is only 62px.
    // Popup height is 96px, so it must open ABOVE the field!
    combo.rozmiesc(Prostokat(20, 140, 240, 28));
    combo.dodaj_element("Klawiatura VirtIO");
    combo.dodaj_element("Mysz USB HID");
    combo.dodaj_element("Tablet bezwzgledny");
    combo.dodaj_element("Karta sieciowa VirtIO");
    combo.rozwin();

    KontekstRysowania kw;
    kw.ustaw_cel(fb, Prostokat(0, 0, W, H));
    okno.rysuj(kw);

    // Calculate popup bounds: opens ABOVE field
    Prostokat pr = combo.prostokat_popupu();
    kw.rysuj_zaokraglony_prostokat(pr, 0x00222222, 4);
    kw.rysuj_ramke(pr, 0x00E58B00, 1);
    const char* items[] = {"Klawiatura VirtIO", "Mysz USB HID", "Tablet bezwzgledny", "Karta sieciowa VirtIO"};
    for (int i = 0; i < 4; ++i) {
        Prostokat item_r(pr.x + 2, pr.y + 4 + i * 22, pr.szerokosc - 4, 20);
        if (i == 2) {
            kw.rysuj_zaokraglony_prostokat(item_r, 0x00E58B00, 2);
            kw.rysuj_tekst(Punkt(item_r.x + 8, item_r.y + 2), items[i], 0x00000000);
        } else {
            kw.rysuj_tekst(Punkt(item_r.x + 8, item_r.y + 2), items[i], 0x00DDDDDD);
        }
    }

    draw_cursor(fb, W, H, pr.x + 110, pr.y + 50);

    save_ppm("/tmp/combobox_bottom_right.ppm", W, H, fb);
    free(fb);
}

// Case 4: Overlapping windows after 1000 movements
static void render_overlapping_windows() {
    const int W = 1280, H = 720;
    g_screen_w = W; g_screen_h = H;
    uint32_t* fb = static_cast<uint32_t*>(calloc(W * H, sizeof(uint32_t)));

    draw_desktop_background(fb, W, H);

    // Window 1: Lower window "Eksplorator Plikow"
    Okno w1("Eksplorator Plikow", 560, 380);
    w1.ustaw_pozycje(180, 120);
    w1.przelicz_uklad();

    // Window 2: Upper window "Wlasciwosci systemu"
    Okno w2("Wlasciwosci systemu", 360, 280);

    // Simulate 1000 movements of w2 across w1
    // Final position overlapping the lower-right of w1: x = 440, y = 240
    for (int i = 0; i < 1000; ++i) {
        int x = 200 + (i % 260);
        int y = 140 + (i % 120);
        w2.ustaw_pozycje(x, y);
    }
    w2.ustaw_pozycje(440, 240);
    w2.przelicz_uklad();

    // Render lower window first, completely
    KontekstRysowania k1;
    k1.ustaw_cel(fb, Prostokat(0, 0, W, H));
    w1.rysuj(k1);

    // Inside lower window, draw some mock file list rows to demonstrate pixel perfection
    for (int row = 0; row < 7; ++row) {
        Prostokat row_r(w1.x() + 20, w1.y() + 60 + row * 32, w1.szerokosc() - 40, 28);
        k1.rysuj_prostokat(row_r, (row % 2 == 0) ? 0x00262626 : 0x001F1F1F);
        k1.rysuj_tekst(Punkt(row_r.x + 10, row_r.y + 6), (row == 0) ? "BursztynOS.iso" : (row == 1) ? "wirtualny_dysk.img" : "raport.txt", 0x00E0E0E0);
    }

    // Render upper window strictly in z-order
    KontekstRysowania k2;
    k2.ustaw_cel(fb, Prostokat(0, 0, W, H));
    w2.rysuj(k2);

    // Inside upper window, draw some property rows
    k2.rysuj_tekst(Punkt(w2.x() + 30, w2.y() + 60), "Bursztyn OS wersja 2.0", 0x00FFFFFF);
    k2.rysuj_tekst(Punkt(w2.x() + 30, w2.y() + 90), "Jadro: x86_64 SMP Preempt", 0x00AAAAAA);
    k2.rysuj_tekst(Punkt(w2.x() + 30, w2.y() + 120), "Pamiec RAM: 2048 MiB", 0x00AAAAAA);
    k2.rysuj_tekst(Punkt(w2.x() + 30, w2.y() + 150), "Grafika: VirtIO-GPU 2D", 0x00AAAAAA);

    // Cursor on the upper window title bar
    draw_cursor(fb, W, H, w2.x() + 100, w2.y() + 12);

    save_ppm("/tmp/overlapping_windows_after_movement.ppm", W, H, fb);
    free(fb);
}

int main() {
    printf("[RENDER] Generating UI screenshots...\n");
    render_combobox_1280x720();
    render_combobox_1920x1080();
    render_combobox_bottom_right();
    render_overlapping_windows();
    printf("[RENDER] Done.\n");
    return 0;
}
