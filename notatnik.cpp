/*
 * Notatnik (GUI) dla Bursztyn OS
 * Wersja zoptymalizowana (wykorzystuje zewnetrzna biblioteke bursztyn_gui)
 */

#include "bursztyn_gui.h"

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

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        4096,  16384, 0x601000, 
        20480, 32768, 0x605000  
    };
}

static char bufor[50][128] __attribute__((section(".data"))) = {};
static char liniowy_bufor[6400] __attribute__((section(".data"))) = {};
static char temp_buf[6400] __attribute__((section(".data"))) = {};
static char pasek_statusu[64] __attribute__((section(".data"))) = "Gotowy."; 

enum TrybPracy { EDYCJA_TEKSTU, WPROWADZANIE_SCIEZKI_ZAPIS, WPROWADZANIE_SCIEZKI_OTWORZ };
TrybPracy tryb = EDYCJA_TEKSTU;
static char sciezka_input[128] __attribute__((section(".data"))) = "";
int sciezka_len = 0;
static char aktualna_sciezka[128] __attribute__((section(".data"))) = "/plik.txt"; 

int cur_r = 0, cur_c = 0;
int scroll = 0;

int WIN_X = 150; int WIN_Y = 80; int WIN_W = 600; int WIN_H = 400;
bool dragging = false; int drag_off_x = 0, drag_off_y = 0;

bool menu_plik_otwarte = false;
bool menu_ustawienia_otwarte = false;
bool okno_pomoc_widoczne = false;
bool zmaksymalizowane = false;
bool aplikacja_zminimalizowana = false;

int old_win_x = 150, old_win_y = 80, old_win_w = 600, old_win_h = 400;
int screen_w = 1024, screen_h = 768; 

void ustaw_status(const char* txt) {
    int i = 0; while(txt[i] && i < 63) { pasek_statusu[i] = txt[i]; i++; } pasek_statusu[i] = '\0';
}

void RysujPrzyciskNaPaskuZadan(bool aktywny) {
    int px = 100; // Pozycja na pasku kontrolowanym przez Menedżer
    int py = screen_h - 40; 
    uint32_t kolor_tla = aktywny ? 0x004A2500 : 0x001A0B00;
    gui_rysuj_prostokat(px, py, 140, 40, kolor_tla);
    gui_rysuj_prostokat(px, py, 1, 40, 0x00E58A00);
    gui_rysuj_prostokat(px + 139, py, 1, 40, 0x00E58A00);
    gui_rysuj_prostokat(px, py, 140, 1, 0x00E58A00);
    gui_wypisz_tekst_kolor(px + 30, py + 12, aktywny ? 0x00FFFFFF : 0x00D1D5DB, "Notatnik");
}

#define LINE_H 16
#define TEXT_Y_OFFSET 50

void RysujInterfejs(bool odswiez_tlo) {
    if (odswiez_tlo) {
        gui_odswiez_pulpit(); // Wywołanie systemowe do jądra - czyszczenie ekranu
        
        // Rysujemy pasek menedżera, ale zostawiamy 120px miejsca na Zegar z prawej strony!
        gui_rysuj_prostokat(0, screen_h - 40, screen_w - 120, 40, 0x001A0B00); 
        gui_rysuj_prostokat(0, screen_h - 40, screen_w - 120, 2, 0x00E58A00);
        RysujPrzycisk(10, screen_h - 35, 80, 30, 0x00E58A00, 0x001A0B00, " Menu");
        RysujPrzyciskNaPaskuZadan(!aplikacja_zminimalizowana); 
    }
    
    // Blokada rysowania jeśli okno ma być schowane
    if (aplikacja_zminimalizowana) { gui_odswiez(); return; }
      
    
    gui_rysuj_okno(WIN_X, WIN_Y, WIN_W, WIN_H, "Notatnik");
    RysujPrzycisk(WIN_X + WIN_W - 74, WIN_Y + 4, 20, 20, 0x00E58A00, 0x001A0B00, "-");
    RysujPrzycisk(WIN_X + WIN_W - 50, WIN_Y + 4, 20, 20, 0x00E58A00, 0x001A0B00, zmaksymalizowane ? "v" : "^");
    RysujPrzycisk(WIN_X + WIN_W - 26, WIN_Y + 4, 20, 20, 0x00AA0000, 0x00FFFFFF, "X");

    gui_rysuj_prostokat(WIN_X + 2, WIN_Y + 26, WIN_W - 4, 20, 0x00301500);
    gui_wypisz_tekst_kolor(WIN_X + 10, WIN_Y + 28, 0x00FFBF00, "Plik");
    gui_wypisz_tekst_kolor(WIN_X + 60, WIN_Y + 28, 0x00FFBF00, "Ustawienia");
    gui_wypisz_tekst_kolor(WIN_X + 160, WIN_Y + 28, 0x00FFBF00, "Pomoc");

    gui_rysuj_prostokat(WIN_X + 2, WIN_Y + WIN_H - 22, WIN_W - 4, 20, 0x001A0B00);
    
    if (tryb == WPROWADZANIE_SCIEZKI_ZAPIS) {
        gui_wypisz_tekst_kolor(WIN_X + 8, WIN_Y + WIN_H - 18, 0x00E58A00, "Zapisz jako: ");
        gui_wypisz_tekst_kolor(WIN_X + 130, WIN_Y + WIN_H - 18, 0x00FFFFFF, sciezka_input);
        gui_wypisz_tekst_kolor(WIN_X + 130 + (sciezka_len * 9), WIN_Y + WIN_H - 18, 0x00FFFFFF, "_");
    } else if (tryb == WPROWADZANIE_SCIEZKI_OTWORZ) {
        gui_wypisz_tekst_kolor(WIN_X + 8, WIN_Y + WIN_H - 18, 0x00E58A00, "Otworz plik: ");
        gui_wypisz_tekst_kolor(WIN_X + 130, WIN_Y + WIN_H - 18, 0x00FFFFFF, sciezka_input);
        gui_wypisz_tekst_kolor(WIN_X + 130 + (sciezka_len * 9), WIN_Y + WIN_H - 18, 0x00FFFFFF, "_");
    } else {
        gui_wypisz_tekst_kolor(WIN_X + 8, WIN_Y + WIN_H - 18, 0x00D1D5DB, pasek_statusu);
    }

    int max_lines = (WIN_H - TEXT_Y_OFFSET - 25) / LINE_H;
    for (int i = 0; i < max_lines; i++) {
        int actual_r = scroll + i;
        if (actual_r >= 50) break;
        int y_pos = WIN_Y + TEXT_Y_OFFSET + (i * LINE_H);
        gui_wyczyscz_obszar(WIN_X + 8, y_pos, WIN_W - 16, LINE_H);
        if (bufor[actual_r][0] != '\0') gui_wypisz_tekst(WIN_X + 8, y_pos, bufor[actual_r]);
        
        if (actual_r == cur_r && tryb == EDYCJA_TEKSTU && !okno_pomoc_widoczne) {
            int screen_x = 0;
            for (int j = 0; j < cur_c; j++) { if ((bufor[actual_r][j] & 0xC0) != 0x80) screen_x++; }
            char kursor[2] = {'_', '\0'}; gui_wypisz_tekst(WIN_X + 8 + (screen_x * 9), y_pos, kursor);
        }
    }

    if (menu_plik_otwarte) {
        gui_rysuj_prostokat(WIN_X + 5, WIN_Y + 46, 130, 72, 0x004A2500); 
        gui_rysuj_prostokat(WIN_X + 5, WIN_Y + 46, 130, 1, 0x00E58A00);
        gui_rysuj_prostokat(WIN_X + 5, WIN_Y + 117, 130, 1, 0x00E58A00);
        gui_rysuj_prostokat(WIN_X + 5, WIN_Y + 46, 1, 72, 0x00E58A00);
        gui_rysuj_prostokat(WIN_X + 134, WIN_Y + 46, 1, 72, 0x00E58A00);
        gui_wypisz_tekst_kolor(WIN_X + 10, WIN_Y + 50, 0x00FFFFFF, "Nowy plik");
        gui_wypisz_tekst_kolor(WIN_X + 10, WIN_Y + 66, 0x00FFFFFF, "Otworz...");
        gui_wypisz_tekst_kolor(WIN_X + 10, WIN_Y + 82, 0x00FFFFFF, "Zapisz jako...");
        gui_wypisz_tekst_kolor(WIN_X + 10, WIN_Y + 98, 0x00FFFFFF, "Zamknij");
    }
    else if (menu_ustawienia_otwarte) {
        gui_rysuj_prostokat(WIN_X + 55, WIN_Y + 46, 190, 40, 0x004A2500); 
        gui_rysuj_prostokat(WIN_X + 55, WIN_Y + 46, 190, 1, 0x00E58A00);
        gui_rysuj_prostokat(WIN_X + 55, WIN_Y + 85, 190, 1, 0x00E58A00);
        gui_rysuj_prostokat(WIN_X + 55, WIN_Y + 46, 1, 40, 0x00E58A00);
        gui_rysuj_prostokat(WIN_X + 244, WIN_Y + 46, 1, 40, 0x00E58A00);
        gui_wypisz_tekst_kolor(WIN_X + 60, WIN_Y + 50, 0x00D1D5DB, "Motyw: Bursztyn");
        gui_wypisz_tekst_kolor(WIN_X + 60, WIN_Y + 66, 0x00D1D5DB, "Czcionka: Systemowa");
    }

    if (okno_pomoc_widoczne) {
        int px = WIN_X + (WIN_W / 2) - 150; int py = WIN_Y + (WIN_H / 2) - 100;
        gui_rysuj_prostokat(px, py, 300, 200, 0x00280F00); 
        gui_rysuj_prostokat(px, py, 300, 24, 0x00E58A00);  
        gui_rysuj_prostokat(px, py, 300, 1, 0x00E58A00); 
        gui_rysuj_prostokat(px, py+199, 300, 1, 0x00E58A00);
        gui_rysuj_prostokat(px, py, 1, 200, 0x00E58A00);
        gui_rysuj_prostokat(px+299, py, 1, 200, 0x00E58A00);
        gui_wypisz_tekst_kolor(px + 8, py + 4, 0x001A0B00, "O programie");
        gui_wypisz_tekst_kolor(px + 20, py + 40, 0x00FFFFFF, "Notatnik");
        gui_wypisz_tekst_kolor(px + 20, py + 60, 0x00D1D5DB, "Wersja: 1.0 (Ring 3)");
        gui_wypisz_tekst_kolor(px + 20, py + 80, 0x00D1D5DB, "Twórca: Programista Art");
        gui_wypisz_tekst_kolor(px + 20, py + 100, 0x00D1D5DB,"Data: Sierpień 2026");
        gui_wypisz_tekst_kolor(px + 20, py + 140, 0x00FFBF00, "Bursztyn OS - Edycja GUI");
        RysujPrzycisk(px + 110, py + 165, 80, 24, 0x00E58A00, 0x001A0B00, "   OK");
    }
    gui_odswiez();
}

void ZapiszDoPliku(const char* sciezka_docelowa) {
    ustaw_status("Zapisywanie..."); RysujInterfejs(false);
    int idx = 0;
    for (int r = 0; r < 50; r++) {
        int c = 0; while (bufor[r][c] != '\0' && idx < 6399) { liniowy_bufor[idx++] = bufor[r][c]; c++; }
        if (bufor[r][0] != '\0' && idx < 6399) liniowy_bufor[idx++] = '\n';
    }
    liniowy_bufor[idx] = '\0';
    utworz(sciezka_docelowa);
    if (zapisz_plik(sciezka_docelowa, liniowy_bufor, idx)) ustaw_status("Zapisano pomyslnie!");
    else ustaw_status("Blad: Zapis nie powiodl sie.");
}

void OtworzZPliku(const char* sciezka_zrodlowa) {
    ustaw_status("Otwieranie pliku..."); RysujInterfejs(false);
    for(int r=0; r<50; r++) for(int c=0; c<128; c++) bufor[r][c] = 0;
    cur_r = 0; cur_c = 0; scroll = 0; for(int i=0; i<6400; i++) temp_buf[i] = 0;
    if (czytaj_plik(sciezka_zrodlowa, temp_buf, 6399)) {
        int r = 0, c = 0;
        for (int i = 0; temp_buf[i] != '\0'; i++) {
            if (temp_buf[i] == '\n') { r++; c = 0; }
            else if (c < 127 && r < 50) { bufor[r][c++] = temp_buf[i]; }
        }
        ustaw_status("Wczytano plik pomyslnie.");
    } else ustaw_status("Blad: Brak pliku na dysku!"); 
}

extern "C" __attribute__((noreturn)) void _start() {
    for(int r=0; r<50; r++) for(int c=0; c<128; c++) bufor[r][c] = 0;
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h); gui_ustaw_przejecie_myszy(true);

    int ansi_stan = 0; uint8_t poprz_przycisk = 0; int old_mx = -1, old_my = -1;
    bool wyjdz = false; bool redraw = true; bool odswiez_tlo = true;

    while (!wyjdz) {
        int mx, my; uint8_t mb; gui_pobierz_mysz(&mx, &my, &mb);
        bool klik = (mb == 1 && poprz_przycisk == 0);
        bool pusc = (mb == 0 && poprz_przycisk == 1);
        bool przytrzymany = (mb == 1);

        if (mx != old_mx || my != old_my) { if (dragging && !aplikacja_zminimalizowana) odswiez_tlo = true; redraw = true; }

        if (klik) {
            if (my >= screen_h - 40) {
                if (mx >= 100 && mx <= 240) { aplikacja_zminimalizowana = !aplikacja_zminimalizowana; odswiez_tlo = true; redraw = true; }
            }
            else if (!aplikacja_zminimalizowana) {
                redraw = true; odswiez_tlo = true;
                if (okno_pomoc_widoczne) {
                    int px = WIN_X + (WIN_W / 2) - 150; int py = WIN_Y + (WIN_H / 2) - 100;
                    if (mx >= px + 110 && mx <= px + 190 && my >= py + 165 && my <= py + 189) okno_pomoc_widoczne = false;
                }
                else if (mx >= WIN_X && mx <= WIN_X + WIN_W && my >= WIN_Y && my <= WIN_Y + WIN_H) {
                    if (my >= WIN_Y && my <= WIN_Y + 26) {
                        if (mx >= WIN_X + WIN_W - 74 && mx <= WIN_X + WIN_W - 54) { aplikacja_zminimalizowana = true; odswiez_tlo = true; } 
                        else if (mx >= WIN_X + WIN_W - 50 && mx <= WIN_X + WIN_W - 30) {
                            if (!zmaksymalizowane) { old_win_x = WIN_X; old_win_y = WIN_Y; old_win_w = WIN_W; old_win_h = WIN_H; WIN_X = 0; WIN_Y = 0; WIN_W = screen_w; WIN_H = screen_h - 40; zmaksymalizowane = true; } 
                            else { WIN_X = old_win_x; WIN_Y = old_win_y; WIN_W = old_win_w; WIN_H = old_win_h; zmaksymalizowane = false; }
                        }
                        else if (mx >= WIN_X + WIN_W - 26 && mx <= WIN_X + WIN_W - 6) { wyjdz = true; }
                        else if (!zmaksymalizowane) { dragging = true; drag_off_x = mx - WIN_X; drag_off_y = my - WIN_Y; menu_plik_otwarte = false; menu_ustawienia_otwarte = false; }
                    } 
                    else if (my > WIN_Y + 26 && my <= WIN_Y + 46) {
                        if (mx >= WIN_X + 5 && mx <= WIN_X + 50) { menu_plik_otwarte = !menu_plik_otwarte; menu_ustawienia_otwarte = false; }
                        else if (mx >= WIN_X + 55 && mx <= WIN_X + 150) { menu_ustawienia_otwarte = !menu_ustawienia_otwarte; menu_plik_otwarte = false; }
                        else if (mx >= WIN_X + 155 && mx <= WIN_X + 215) { okno_pomoc_widoczne = true; menu_plik_otwarte = false; menu_ustawienia_otwarte = false; }
                        else { menu_plik_otwarte = false; menu_ustawienia_otwarte = false; }
                    }
                    else if (menu_plik_otwarte && mx >= WIN_X + 5 && mx <= WIN_X + 135 && my > WIN_Y + 46 && my <= WIN_Y + 118) {
                        if (my >= WIN_Y + 46 && my < WIN_Y + 62) { for(int r=0; r<50; r++) for(int c=0; c<128; c++) bufor[r][c] = 0; cur_r = 0; cur_c = 0; scroll = 0; menu_plik_otwarte = false; ustaw_status("Nowy plik otwarty."); }
                        else if (my >= WIN_Y + 62 && my < WIN_Y + 78) { tryb = WPROWADZANIE_SCIEZKI_OTWORZ; sciezka_len = 0; sciezka_input[0] = '\0'; menu_plik_otwarte = false; }
                        else if (my >= WIN_Y + 78 && my < WIN_Y + 94) { tryb = WPROWADZANIE_SCIEZKI_ZAPIS; for(sciezka_len=0; aktualna_sciezka[sciezka_len] != '\0'; sciezka_len++) { sciezka_input[sciezka_len] = aktualna_sciezka[sciezka_len]; } sciezka_input[sciezka_len] = '\0'; menu_plik_otwarte = false; }
                        else if (my >= WIN_Y + 94 && my <= WIN_Y + 118) { wyjdz = true; menu_plik_otwarte = false; }
                    } 
                    else if (menu_ustawienia_otwarte && mx >= WIN_X + 55 && mx <= WIN_X + 245 && my > WIN_Y + 46 && my <= WIN_Y + 86) { menu_ustawienia_otwarte = false; ustaw_status("Te opcje wczytaly sie poprawnie."); }
                    else { menu_plik_otwarte = false; menu_ustawienia_otwarte = false; }
                } else { menu_plik_otwarte = false; menu_ustawienia_otwarte = false; }
            }
        }

        if (pusc) { dragging = false; redraw = true; }
        if (dragging && przytrzymany && !aplikacja_zminimalizowana) { WIN_X = mx - drag_off_x; WIN_Y = my - drag_off_y; if (WIN_X < 0) { WIN_X = 0; } if (WIN_Y < 0) { WIN_Y = 0; } odswiez_tlo = true; redraw = true; }
        poprz_przycisk = mb; old_mx = mx; old_my = my;

        // Poprawiona polska nazwa wywołania API klawiatury
        char c = pobierz_znak();
        if (c != 0) {
            if (aplikacja_zminimalizowana) continue; 
            redraw = true; odswiez_tlo = false; menu_plik_otwarte = false; menu_ustawienia_otwarte = false;
            unsigned char uc = (unsigned char)c;

            if (okno_pomoc_widoczne) { if (c == '\n' || c == '\r' || c == '\x1B') { okno_pomoc_widoczne = false; odswiez_tlo = true; } }
            else if (tryb == WPROWADZANIE_SCIEZKI_ZAPIS || tryb == WPROWADZANIE_SCIEZKI_OTWORZ) {
                if (c == '\n' || c == '\r') {
                    for(int i=0; i<=sciezka_len; i++) aktualna_sciezka[i] = sciezka_input[i];
                    if (tryb == WPROWADZANIE_SCIEZKI_ZAPIS) { tryb = EDYCJA_TEKSTU; ZapiszDoPliku(aktualna_sciezka); } else { tryb = EDYCJA_TEKSTU; OtworzZPliku(aktualna_sciezka); }
                } 
                else if (c == '\x1B') { tryb = EDYCJA_TEKSTU; ustaw_status("Anulowano operacje plikowa."); }
                else if (c == '\b') { if (sciezka_len > 0) { sciezka_input[--sciezka_len] = '\0'; } } 
                else if (uc >= 32 && sciezka_len < 60) { sciezka_input[sciezka_len++] = c; sciezka_input[sciezka_len] = '\0'; }
            } 
            else {
                ustaw_status("Edycja..."); 
                if (ansi_stan == 0 && c == '\x1B') { ansi_stan = 1; }
                else if (ansi_stan == 1 && c == '[') { ansi_stan = 2; }
                else if (ansi_stan == 2) {
                    ansi_stan = 0;
                    if (c == 'A' && cur_r > 0) cur_r--;
                    else if (c == 'B' && cur_r < 49) cur_r++;
                    else if (c == 'C' && cur_c < 127 && bufor[cur_r][cur_c] != '\0') { cur_c++; while (cur_c < 127 && (bufor[cur_r][cur_c] & 0xC0) == 0x80) cur_c++; }
                    else if (c == 'D' && cur_c > 0) { cur_c--; while (cur_c > 0 && (bufor[cur_r][cur_c] & 0xC0) == 0x80) cur_c--; }
                    if (cur_r < scroll) scroll = cur_r;
                    if (cur_r >= scroll + 15) scroll = cur_r - 14;
                } else {
                    ansi_stan = 0;
                    if (c == '\n' || c == '\r') { cur_r++; cur_c = 0; }
                    else if (c == '\b') {
                        if (cur_c > 0) { cur_c--; if (cur_c > 0 && (bufor[cur_r][cur_c-1] & 0xE0) == 0xC0) cur_c--; bufor[cur_r][cur_c] = '\0'; }
                        else if (cur_r > 0) { cur_r--; cur_c = 0; while (cur_c < 127 && bufor[cur_r][cur_c] != '\0') cur_c++; }
                        if (cur_r < scroll) scroll = cur_r;
                    }
                    else if (uc >= 32 && cur_c < 127) { bufor[cur_r][cur_c++] = c; }
                    if (cur_r >= scroll + 15) scroll = cur_r - 14;
                }
            }
        }
        if (redraw) { RysujInterfejs(odswiez_tlo); redraw = false; odswiez_tlo = false; }
    }

    gui_ustaw_przejecie_myszy(false); 
    gui_odswiez_pulpit(); 
    gui_odswiez();
    // BARDZO WAŻNE: Zamykając aplikację wracamy do Menedżera Okien, a nie do terminala shell.bur!
    bws_wywolaj(10, (uint64_t)"/menedzer_okien.bur");
    while(true);
}