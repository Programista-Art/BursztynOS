/*
 * =====================================================================
 *  Bursztyn OS SDK - Moduł UI (User Interface)
 *  Zestaw wysokopoziomowych widgetów bazujących na bursztyn_api.h
 * =====================================================================
 */

#pragma once

#include "bursztyn_api.h"
#include "bursztyn_libc.h"

// Typ wyliczeniowy dla akcji paska tytułowego (z Twojego kodu)
enum b_ui_akcja_belki : uint32_t {
    B_UI_BELKA_BRAK = 0,
    B_UI_BELKA_DRAG,
    B_UI_BELKA_MINIMALIZUJ,
    B_UI_BELKA_MAKSYMALIZUJ,
    B_UI_BELKA_ZAMKNIJ
};

// =====================================================================
// WIDGETY GRAFICZNE
// =====================================================================

// Rysuje tekst wyśrodkowany wewnątrz zadanego prostokąta
static inline void b_ui_draw_centered_text(int px, int py, int w, int h, int skala, uint32_t kolor, const char* tekst) {
    int len = strlen(tekst);
    // Zakładamy, że b_gui_get_char_width() to BWS 24.
    // Dla uproszczenia (jeśli czcionka jest stałej szerokości 8px):
    int text_w = len * (8 * skala); 
    int text_h = 16 * skala;
    
    int offset_x = (w - text_w) / 2;
    int offset_y = (h - text_h) / 2;
    
    if (offset_x < 0) offset_x = 0;
    if (offset_y < 0) offset_y = 0;
    
    b_gui_draw_text(px + offset_x, py + offset_y, kolor, skala, tekst);
}

// Rysuje klasyczny Bursztynowy Przycisk (z Twojego bursztyn_gui)
static inline void b_ui_draw_button(int x, int y, int w, int h, uint32_t kolor_bg, uint32_t kolor_txt, const char* tekst) {
    b_gui_draw_rect(x, y, w, h, kolor_bg);
    b_ui_draw_centered_text(x, y, w, h, 1, kolor_txt, tekst);
}

// Rysuje standardową belkę okna z przyciskami -, ^, X
static inline void b_ui_draw_window_bar(int x, int y, int szer, const char* tytul, bool aktywne) {
    uint32_t kolor_paska = aktywne ? 0x00FFBF00 : 0x008A5A00;
    uint32_t kolor_tekstu = aktywne ? 0x001A0B00 : 0x00D1D5DB;

    // Tło paska
    b_gui_draw_rect(x, y, szer, 24, kolor_paska);
    b_gui_draw_text(x + 6, y + 4, kolor_tekstu, 1, tytul);

    // Przyciski kontrolne (wymiary z Menedżera Okien)
    int btn_y = y + 2;
    b_ui_draw_button(x + szer - 74, btn_y, 20, 20, 0x00E58A00, 0x001A0B00, "-");
    b_ui_draw_button(x + szer - 50, btn_y, 20, 20, 0x00E58A00, 0x001A0B00, "^");
    b_ui_draw_button(x + szer - 26, btn_y, 20, 20, 0x00AA0000, 0x00FFFFFF, "X");
}

// Sprawdza, w co kliknął użytkownik na belce okna
static inline b_ui_akcja_belki b_ui_hit_test_bar(int mx, int my, int okno_x, int okno_y, int okno_szer) {
    if (my < okno_y || my > okno_y + 24) return B_UI_BELKA_BRAK;
    
    if (mx >= okno_x + okno_szer - 26 && mx <= okno_x + okno_szer - 6) return B_UI_BELKA_ZAMKNIJ;
    if (mx >= okno_x + okno_szer - 50 && mx <= okno_x + okno_szer - 30) return B_UI_BELKA_MAKSYMALIZUJ;
    if (mx >= okno_x + okno_szer - 74 && mx <= okno_x + okno_szer - 54) return B_UI_BELKA_MINIMALIZUJ;
    
    if (mx >= okno_x && mx < okno_x + okno_szer - 75) return B_UI_BELKA_DRAG;
    
    return B_UI_BELKA_BRAK;
}

// =====================================================================
// POMOCNIKI KONSOLI (Przeniesione z shell.bur)
// =====================================================================

// Czeka na wpisanie pełnej linii tekstu (zakończonej Enterem)
static inline void b_console_get_line(char* bufor, int max_dlugosc) {
    int pozycja = 0;
    while (true) {
        char c = b_getchar(); 
        if (c == 0) continue; 

        if (c == '\n' || c == '\r') {
            bufor[pozycja] = '\0';
            break;
        } 
        else if (c == '\b') {
            if (pozycja > 0) { 
                pozycja--; 
                b_print("\b"); 
            }
        } 
        else if (pozycja < max_dlugosc - 1) {
            bufor[pozycja++] = c;
            char tmp[2] = {c, '\0'}; 
            b_print(tmp);
        }
    }
}
