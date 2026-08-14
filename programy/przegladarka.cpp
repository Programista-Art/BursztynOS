/*
 * Przeglądarka Internetowa "Hussar" dla Bursztyn OS (Ring 3)
 * Wersja z obsługą myszy, inteligentnym parserem URL, skalowaniem czcionek
 * oraz inteligentną obsługą kodów błędów HTTP.
 */

#include "../bursztyn_gui.h"
#include <stdint.h>
#include <stdbool.h>

// --- DOŁĄCZENIE NOWEGO MODUŁU OBSŁUGI BŁĘDÓW HTTP ---
#include "http_kody.h"

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
        4096, 61440, 0x601000,
        65536, 0x210000, 0x610000
    };
    
    bool bws_siec_dns(const char* domena, uint8_t* wyjsciowy_ip);
    bool bws_siec_pobierz_http(uint8_t* cel_ip, const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc);
    bool bws_siec_pobierz_https(uint8_t* cel_ip, const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc);
    bool bws_tls_certyfikat_zaufany();
}

// =========================================================================
// DEKLARACJE WYPRZEDZAJĄCE
// =========================================================================
extern "C" void* memcpy(void* dest, const void* src, unsigned long n);
extern "C" void* memset(void* dest, int val, unsigned long n);
void RysujInterfejs(bool odswiez_tlo);
void PobierzStrone();
int dlugosc_tekstu(const char* s);
void dopisz_znak(char* s, char z, int max_len);
void usun_ostatni_znak(char* s);
void ustaw_status(const char* txt);
const char* oczysc_http(const char* zrodlo);
void usun_tagi_html(const char* wejscie, char* wyjscie, int limit);
void RysujDrzewoHTML(int start_x, int start_y, const char* html_kod);
bool czy_tag(const char* s, int pos, const char* tag);
void rysuj_html(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t domyslny_kolor, int przewin);
void rysuj_zwykly_tekst(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t kolor, int przewin);
void wypisz_skalowane(int x, int y, uint32_t kolor, int skala, const char* text);
void RysujPrzyciskLokalny(int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* txt);
void Nawiguj(bool nowa_strona, bool rozpoznaj_wyszukiwanie);
void WczytajUlubione();
void ZapiszUlubione();
void DodajObecnaDoUlubionych();

struct Zakladka {
    char url[256];
    char html[256000];
    int przewin_y;
    bool to_jest_html;
    bool wczytana;
};

#define MAX_ZAKLADKI 6
#define HTML_POJEMNOSC 256000

// Zmienne BSS
static Zakladka zakladki[MAX_ZAKLADKI] __attribute__((section(".bss")));
static char temp_bufor[HTML_POJEMNOSC] __attribute__((section(".bss")));
static char status_bufor[64];
static int glebokosc_przekierowan = 0;
static char plik_ulubionych[2560] __attribute__((section(".bss")));

char historia[20][256];
int historia_idx = -1;
int historia_max = -1;
char ulubione[10][256];
int ulubione_ilosc = 0;
bool menu_ulubione_otwarte = false;
bool menu_ustawienia_otwarte = false;
static bool nagraj_historie_po_sukcesie = false;

int liczba_zakladek;
int aktywna_zakladka;
int WIN_X = 50, WIN_Y = 50, WIN_W = 800, WIN_H = 550;
int old_win_x = 50, old_win_y = 50, old_win_w = 800, old_win_h = 550;
bool zmaksymalizowane;
bool dragging = false;
int drag_off_x = 0, drag_off_y = 0;
int screen_w = 1024, screen_h = 768;
int max_przewin_y;
int calkowita_wysokosc_strony;
bool w_polu_url;

static void ogranicz_przewiniecie() {
    int& przewin = zakladki[aktywna_zakladka].przewin_y;
    if (przewin < 0) przewin = 0;
    if (przewin > max_przewin_y) przewin = max_przewin_y;
}

static void kopiuj_tekst_limit(char* cel, const char* zrodlo, int pojemnosc) {
    int i = 0;
    while (zrodlo[i] && i + 1 < pojemnosc) { cel[i] = zrodlo[i]; i++; }
    if (zrodlo[i] && (((uint8_t)zrodlo[i] & 0xC0) == 0x80)) {
        while (i > 0 && (((uint8_t)cel[i - 1] & 0xC0) == 0x80)) i--;
        if (i > 0) i--;
    }
    cel[i] = '\0';
}

static bool tekst_ma_kropke(const char* tekst) {
    for (int i = 0; tekst[i]; i++) if (tekst[i] == '.') return true;
    return false;
}

static void przygotuj_adres_wyszukiwania(char* url) {
    if (url[0] == '\0' || tekst_ma_kropke(url)) return;
    char zapytanie[256];
    kopiuj_tekst_limit(zapytanie, url, sizeof(zapytanie));
    const char prefiks[] = "https://html.duckduckgo.com/html/?q=";
    int o = 0;
    while (prefiks[o] && o < 255) { url[o] = prefiks[o]; o++; }
    for (int i = 0; zapytanie[i] && o < 255; i++)
        url[o++] = zapytanie[i] == ' ' ? '+' : zapytanie[i];
    url[o] = '\0';
}

static void DopiszDoHistorii(const char* url) {
    if (historia_idx >= 0) {
        int i = 0;
        while (historia[historia_idx][i] && historia[historia_idx][i] == url[i]) i++;
        if (historia[historia_idx][i] == '\0' && url[i] == '\0') return;
    }
    if (historia_idx < 19) historia_idx++;
    else {
        for (int h = 1; h < 20; h++) kopiuj_tekst_limit(historia[h - 1], historia[h], 256);
        historia_idx = 19;
    }
    kopiuj_tekst_limit(historia[historia_idx], url, 256);
    historia_max = historia_idx;
}

void WczytajUlubione() {
    ulubione_ilosc = 0;
    for (int i = 0; i < 2560; i++) plik_ulubionych[i] = '\0';
    if (!czytaj_plik("/uzytkownicy/zakladki.txt", plik_ulubionych, 2559)) return;
    int i = 0;
    while (plik_ulubionych[i] && ulubione_ilosc < 10) {
        int j = 0;
        while (plik_ulubionych[i] && plik_ulubionych[i] != '\n' &&
               plik_ulubionych[i] != '\r' && j < 255)
            ulubione[ulubione_ilosc][j++] = plik_ulubionych[i++];
        ulubione[ulubione_ilosc][j] = '\0';
        while (plik_ulubionych[i] == '\n' || plik_ulubionych[i] == '\r') i++;
        if (j > 0) ulubione_ilosc++;
    }
}

void ZapiszUlubione() {
    int p = 0;
    for (int u = 0; u < ulubione_ilosc; u++) {
        for (int i = 0; ulubione[u][i] && p < 2559; i++) plik_ulubionych[p++] = ulubione[u][i];
        if (p < 2559) plik_ulubionych[p++] = '\n';
    }
    plik_ulubionych[p] = '\0';
    const char* sciezka = "/uzytkownicy/zakladki.txt";
    utworz(sciezka);
    if (zapisz_plik(sciezka, plik_ulubionych, p)) ustaw_status("Zapisano zakładki.");
    else ustaw_status("Błąd zapisu zakładek.");
}

void DodajObecnaDoUlubionych() {
    const char* url = zakladki[aktywna_zakladka].url;
    if (!url[0]) { ustaw_status("Brak adresu do zapisania."); return; }
    if (ulubione_ilosc >= 10) { ustaw_status("Lista zakładek jest pełna."); return; }
    kopiuj_tekst_limit(ulubione[ulubione_ilosc++], url, 256);
    ZapiszUlubione();
}

void Nawiguj(bool nowa_strona, bool rozpoznaj_wyszukiwanie) {
    char* url = zakladki[aktywna_zakladka].url;
    if (!url[0]) { ustaw_status("Wpisz adres lub szukaną frazę."); return; }
    if (rozpoznaj_wyszukiwanie) przygotuj_adres_wyszukiwania(url);
    nagraj_historie_po_sukcesie = nowa_strona;
    PobierzStrone();
    nagraj_historie_po_sukcesie = false;
}

// =========================================================================
// GŁÓWNY PUNKT WEJŚCIA PROGRAMU (Musi być dokładnie tutaj)
// =========================================================================
extern "C" __attribute__((noreturn)) void _start() {
    liczba_zakladek = 1;
    aktywna_zakladka = 0;
    WIN_X = 50; WIN_Y = 50; WIN_W = 800; WIN_H = 550;
    old_win_x = WIN_X; old_win_y = WIN_Y; old_win_w = WIN_W; old_win_h = WIN_H;
    zmaksymalizowane = false;
    dragging = false;
    drag_off_x = 0; drag_off_y = 0;
    max_przewin_y = 0;
    calkowita_wysokosc_strony = 0;
    w_polu_url = false;

    const char* txt_start = "Gotowy";
    int z = 0; while (txt_start[z]) { status_bufor[z] = txt_start[z]; z++; }
    status_bufor[z] = '\0';

    for(int i = 0; i < MAX_ZAKLADKI; i++) {
        for(int j = 0; j < 256; j++) ((volatile char*)zakladki[i].url)[j] = 0;
        for(int j = 0; j < HTML_POJEMNOSC; j++) ((volatile char*)zakladki[i].html)[j] = 0;
        zakladki[i].przewin_y = 0;
        zakladki[i].to_jest_html = false;
        zakladki[i].wczytana = false;
    }
    for(int i = 0; i < HTML_POJEMNOSC; i++) ((volatile char*)temp_bufor)[i] = 0;
    WczytajUlubione();

    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    gui_ustaw_przejecie_myszy(true);

    bool dziala = true;
    uint8_t poprz_przycisk = 0;
    int stary_mysz_x = -1, stary_mysz_y = -1;
    int ansi_stan = 0;
    
    bool przerysuj = true;
    bool odswiez_tlo = true;
    bool scroll_dragging = false;
    int scroll_drag_start_my = 0;
    int scroll_drag_start_val = 0;

    const char* start_url = "example.com/";
    int iter = 0; while (start_url[iter]) { zakladki[0].url[iter] = start_url[iter]; iter++; }
    zakladki[0].url[iter] = '\0';
    zakladki[0].html[0] = '\0';

    while (dziala) {
        int mx, my; uint8_t mb;
        gui_pobierz_mysz(&mx, &my, &mb);
        
        bool klik = (mb == 1 && poprz_przycisk == 0);
        bool pusc = (mb == 0 && poprz_przycisk == 1);
        bool przytrzymany = (mb == 1);

        if (mx != stary_mysz_x || my != stary_mysz_y) {
            if (dragging) odswiez_tlo = true;
            przerysuj = true;
        }

        if (klik) {
            przerysuj = true; odswiez_tlo = true;

            bool obsluzono_menu = false;
            int menu_x = WIN_X + WIN_W - 270;
            int menu_y = WIN_Y + 94;
            if (menu_ulubione_otwarte && mx >= menu_x && mx <= menu_x + 260 &&
                my >= menu_y && my <= menu_y + 24 + ulubione_ilosc * 20) {
                if (my < menu_y + 24) DodajObecnaDoUlubionych();
                else {
                    int wybrana = (my - (menu_y + 24)) / 20;
                    if (wybrana >= 0 && wybrana < ulubione_ilosc) {
                        kopiuj_tekst_limit(zakladki[aktywna_zakladka].url,
                                           ulubione[wybrana], 256);
                        menu_ulubione_otwarte = false;
                        Nawiguj(true, false);
                    }
                }
                obsluzono_menu = true;
            } else if (menu_ustawienia_otwarte &&
                       mx >= WIN_X + WIN_W - 180 && mx <= WIN_X + WIN_W - 8 &&
                       my >= menu_y && my <= menu_y + 32) {
                ustaw_status("Brak opcji.");
                menu_ustawienia_otwarte = false;
                obsluzono_menu = true;
            }

            bool nad_scrollbarem = !obsluzono_menu && max_przewin_y > 0 &&
                mx >= WIN_X + WIN_W - 20 && mx <= WIN_X + WIN_W &&
                my >= WIN_Y + 96 && my < WIN_Y + WIN_H - 24;
            if (obsluzono_menu) {
                w_polu_url = false;
            } else if (nad_scrollbarem) {
                scroll_dragging = true;
                scroll_drag_start_my = my;
                scroll_drag_start_val = zakladki[aktywna_zakladka].przewin_y;
                w_polu_url = false;
            }
            else if (my >= WIN_Y && my <= WIN_Y + 26 &&
                     mx >= WIN_X && mx <= WIN_X + WIN_W) {
                if (mx >= WIN_X + WIN_W - 74 && mx <= WIN_X + WIN_W - 54) { dziala = false; }
                else if (mx >= WIN_X + WIN_W - 50 && mx <= WIN_X + WIN_W - 30) {
                    if (!zmaksymalizowane) {
                        old_win_x = WIN_X; old_win_y = WIN_Y; old_win_w = WIN_W; old_win_h = WIN_H;
                        WIN_X = 0; WIN_Y = 0; WIN_W = screen_w; WIN_H = screen_h - 40;
                        zmaksymalizowane = true;
                    } else {
                        WIN_X = old_win_x; WIN_Y = old_win_y; WIN_W = old_win_w; WIN_H = old_win_h;
                        zmaksymalizowane = false;
                    }
                }
                else if (mx >= WIN_X + WIN_W - 26 && mx <= WIN_X + WIN_W - 6) { dziala = false; }
                else if (!zmaksymalizowane) {
                    dragging = true;
                    drag_off_x = mx - WIN_X;
                    drag_off_y = my - WIN_Y;
                    w_polu_url = false;
                    ustaw_status("Przesuwanie okna...");
                }
            }
            else if (my >= WIN_Y + 28 && my <= WIN_Y + 52) {
                for(int i = 0; i < liczba_zakladek; i++) {
                    int tx = WIN_X + 10 + (i * 110);
                    if (mx >= tx && mx <= tx + 100) {
                        if (liczba_zakladek > 1 && mx >= tx + 80 && mx <= tx + 96) {
                            for(int j = i; j < liczba_zakladek-1; j++) zakladki[j] = zakladki[j+1];
                            liczba_zakladek--;
                            if (aktywna_zakladka >= liczba_zakladek) aktywna_zakladka = liczba_zakladek - 1;
                            ustaw_status("Zamknięto zakładkę.");
                        } else {
                            aktywna_zakladka = i;
                            ustaw_status("Przełączono zakładkę.");
                        }
                        w_polu_url = false;
                        break;
                    }
                }
                if (liczba_zakladek < MAX_ZAKLADKI) {
                    int plus_x = WIN_X + 10 + (liczba_zakladek * 110);
                    if (mx >= plus_x && mx <= plus_x + 24) {
                        zakladki[liczba_zakladek].url[0] = '\0';
                        zakladki[liczba_zakladek].html[0] = '\0';
                        zakladki[liczba_zakladek].przewin_y = 0;
                        zakladki[liczba_zakladek].to_jest_html = false;
                        aktywna_zakladka = liczba_zakladek;
                        liczba_zakladek++;
                        ustaw_status("Nowa zakładka.");
                    }
                }
            }
            else if (my >= WIN_Y + 56 && my <= WIN_Y + 92) {
                int narzedzia_y = WIN_Y + 56;
                int adres_x = WIN_X + 122;
                int adres_w = WIN_W - 412;
                if (mx >= WIN_X + 8 && mx <= WIN_X + 42 && historia_idx > 0) {
                    historia_idx--;
                    kopiuj_tekst_limit(zakladki[aktywna_zakladka].url,
                                       historia[historia_idx], 256);
                    Nawiguj(false, false);
                }
                else if (mx >= WIN_X + 44 && mx <= WIN_X + 78 && historia_idx < historia_max) {
                    historia_idx++;
                    kopiuj_tekst_limit(zakladki[aktywna_zakladka].url,
                                       historia[historia_idx], 256);
                    Nawiguj(false, false);
                }
                else if (mx >= WIN_X + 80 && mx <= WIN_X + 114) {
                    Nawiguj(false, false);
                }
                else if (mx >= adres_x && mx <= adres_x + adres_w &&
                         my >= narzedzia_y + 4 && my <= narzedzia_y + 32) {
                    w_polu_url = true; ustaw_status("Edycja adresu URL...");
                }
                else if (mx >= WIN_X + WIN_W - 286 && mx <= WIN_X + WIN_W - 258) {
                    zakladki[aktywna_zakladka].url[0] = '\0'; w_polu_url = true;
                }
                else if (mx >= WIN_X + WIN_W - 254 && mx <= WIN_X + WIN_W - 200) {
                    w_polu_url = false; Nawiguj(true, true);
                }
                else if (mx >= WIN_X + WIN_W - 196 && mx <= WIN_X + WIN_W - 108) {
                    menu_ulubione_otwarte = !menu_ulubione_otwarte;
                    menu_ustawienia_otwarte = false; w_polu_url = false;
                }
                else if (mx >= WIN_X + WIN_W - 104 && mx <= WIN_X + WIN_W - 8) {
                    menu_ustawienia_otwarte = !menu_ustawienia_otwarte;
                    menu_ulubione_otwarte = false; w_polu_url = false;
                }
                else { w_polu_url = false; }
            }
            else {
                w_polu_url = false;
                menu_ulubione_otwarte = false;
                menu_ustawienia_otwarte = false;
            }
        }

        if (pusc) { dragging = false; przerysuj = true; if(status_bufor[0]=='P') ustaw_status("Gotowy"); }
        if (mb == 0) scroll_dragging = false;

        if (scroll_dragging && przytrzymany) {
            zakladki[aktywna_zakladka].przewin_y =
                scroll_drag_start_val + (my - scroll_drag_start_my) * 3;
            ogranicz_przewiniecie();
            przerysuj = true;
            odswiez_tlo = false;
        }
        
        if (dragging && przytrzymany) {
            WIN_X = mx - drag_off_x; WIN_Y = my - drag_off_y;
            if (WIN_X < 0) WIN_X = 0;
            if (WIN_Y < 0) WIN_Y = 0;
            odswiez_tlo = true; przerysuj = true;
        }
        
        poprz_przycisk = mb; stary_mysz_x = mx; stary_mysz_y = my;

        char znak = pobierz_znak();
        if (znak != 0) {
            przerysuj = true; odswiez_tlo = false;
            if (ansi_stan == 0 && znak == '\x1B') { ansi_stan = 1; }
            else if (ansi_stan == 1 && znak == '[') { ansi_stan = 2; }
            else if (ansi_stan == 2) {
                ansi_stan = 0;
                if (znak == 'A') zakladki[aktywna_zakladka].przewin_y -= 100;
                else if (znak == 'B') zakladki[aktywna_zakladka].przewin_y += 100;
                ogranicz_przewiniecie();
            } else {
                ansi_stan = 0;
                if (w_polu_url) {
                    if (znak == '\n' || znak == '\r') { w_polu_url = false; Nawiguj(true, true); }
                    else if (znak == '\b') usun_ostatni_znak(zakladki[aktywna_zakladka].url);
                    // Bajty UTF-8 mają ustawiony bit 7 i przy signed char były odrzucane.
                    else if ((uint8_t)znak >= 32) dopisz_znak(zakladki[aktywna_zakladka].url, znak, 255);
                }
            }
        }

        if (przerysuj) {
            RysujInterfejs(odswiez_tlo);
            przerysuj = false; odswiez_tlo = false;
        }
    }

    gui_ustaw_przejecie_myszy(false);
    gui_odswiez_pulpit(); 
    gui_odswiez();
    bws_wywolaj(10, (uint64_t)"/menedzer_okien.bur");
    while(true);
}

// =========================================================================
// IMPLEMENTACJA FUNKCJI
// =========================================================================

extern "C" void* memcpy(void* dest, const void* src, unsigned long n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (unsigned long i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

extern "C" void* memset(void* dest, int val, unsigned long n) {
    char* d = (char*)dest;
    for (unsigned long i = 0; i < n; i++) d[i] = (char)val;
    return dest;
}

void wypisz_skalowane(int x, int y, uint32_t kolor, int skala, const char* text) {
    uint64_t arg_kolor_skala = ((uint64_t)skala << 32) | kolor;
    bws_wywolaj(20, (uint64_t)x, (uint64_t)y, arg_kolor_skala, (uint64_t)text);
}

void RysujPrzyciskLokalny(int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* txt) {
    gui_rysuj_prostokat(x, y, w, h, bg);
    
    int text_w = 0;
    int i = 0;
    while (txt[i] != '\0') {
        uint32_t unicode = (uint8_t)txt[i];
        int char_bytes = 1;
        if (((uint8_t)txt[i] & 0xE0) == 0xC0 && txt[i+1] != '\0' &&
            ((uint8_t)txt[i+1] & 0xC0) == 0x80) {
            unicode = (((uint8_t)txt[i] & 0x1F) << 6) | ((uint8_t)txt[i+1] & 0x3F);
            char_bytes = 2;
        }
        int sw = (int)bws_wywolaj(24, unicode);
        if (sw <= 0) sw = 8;
        text_w += sw + 1; 
        i += char_bytes;
    }
    
    int px = x + (w - text_w) / 2;
    int py = y + (h - 16) / 2;
    if (py < y) py = y;
    gui_wypisz_tekst_kolor(px, py, fg, txt);
}

int dlugosc_tekstu(const char* s) { int len = 0; while (s[len]) len++; return len; }
void dopisz_znak(char* s, char z, int max_len) { int len = dlugosc_tekstu(s); if (len < max_len - 1) { s[len] = z; s[len+1] = '\0'; } }
void usun_ostatni_znak(char* s) {
    int len = dlugosc_tekstu(s);
    if (len == 0) return;
    len--;
    // Nie zostawiaj osieroconego bajtu kontynuacji UTF-8.
    while (len > 0 && (((uint8_t)s[len] & 0xC0) == 0x80)) len--;
    s[len] = '\0';
}
void ustaw_status(const char* txt) {
    int i = 0;
    while (txt[i] && i < 63) { status_bufor[i] = txt[i]; i++; }
    // Ograniczony bufor nie może kończyć się w środku znaku UTF-8.
    if (txt[i] != '\0' && (((uint8_t)txt[i] & 0xC0) == 0x80))
        while (i > 0 && (((uint8_t)status_bufor[i - 1] & 0xC0) == 0x80)) i--;
    if (txt[i] != '\0' && (((uint8_t)txt[i] & 0xC0) == 0x80) && i > 0) i--;
    status_bufor[i] = '\0';
}

const char* oczysc_http(const char* zrodlo) {
    int i = 0;
    while (zrodlo[i] != '\0') {
        if (zrodlo[i] == '\r' && zrodlo[i+1] == '\n' && zrodlo[i+2] == '\r' && zrodlo[i+3] == '\n') {
            return &zrodlo[i+4];
        }
        i++;
    }
    return zrodlo; 
}

static char html_mala_litera(char znak) {
    return (znak >= 'A' && znak <= 'Z') ? znak + ('a' - 'A') : znak;
}

static bool html_nazwa_rowna(const char* nazwa, int dlugosc, const char* wzorzec) {
    int i = 0;
    while (wzorzec[i]) {
        if (i >= dlugosc || html_mala_litera(nazwa[i]) != wzorzec[i]) return false;
        i++;
    }
    return i == dlugosc;
}

static bool html_tag_blokowy(const char* nazwa, int dlugosc) {
    return html_nazwa_rowna(nazwa, dlugosc, "p") ||
           html_nazwa_rowna(nazwa, dlugosc, "div") ||
           html_nazwa_rowna(nazwa, dlugosc, "h1") ||
           html_nazwa_rowna(nazwa, dlugosc, "h2") ||
           html_nazwa_rowna(nazwa, dlugosc, "h3") ||
           html_nazwa_rowna(nazwa, dlugosc, "h4") ||
           html_nazwa_rowna(nazwa, dlugosc, "h5") ||
           html_nazwa_rowna(nazwa, dlugosc, "h6") ||
           html_nazwa_rowna(nazwa, dlugosc, "li") ||
           html_nazwa_rowna(nazwa, dlugosc, "header") ||
           html_nazwa_rowna(nazwa, dlugosc, "footer") ||
           html_nazwa_rowna(nazwa, dlugosc, "section") ||
           html_nazwa_rowna(nazwa, dlugosc, "article") ||
           html_nazwa_rowna(nazwa, dlugosc, "tr");
}

void usun_tagi_html(const char* wejscie, char* wyjscie, int limit) {
    enum PominSekcje { POMIN_NIC, POMIN_HEAD, POMIN_SCRIPT, POMIN_STYLE, POMIN_SVG };
    PominSekcje pomin = POMIN_NIC;
    int i = 0;
    int o = 0;
    bool oczekuje_spacji = false;

    while (wejscie[i] && o + 1 < limit) {
        if (wejscie[i] == '<') {
            // Komentarze HTML moga zawierac znak '>', wiec konczymy je dopiero na "-->".
            if (wejscie[i + 1] == '!' && wejscie[i + 2] == '-' && wejscie[i + 3] == '-') {
                i += 4;
                while (wejscie[i] && !(wejscie[i] == '-' && wejscie[i + 1] == '-' && wejscie[i + 2] == '>')) i++;
                if (wejscie[i]) i += 3;
                continue;
            }

            int koniec = i + 1;
            bool zamykajacy = false;
            while (wejscie[koniec] == ' ' || wejscie[koniec] == '\t' || wejscie[koniec] == '\r' || wejscie[koniec] == '\n') koniec++;
            if (wejscie[koniec] == '/') { zamykajacy = true; koniec++; }
            while (wejscie[koniec] == ' ' || wejscie[koniec] == '\t') koniec++;
            int nazwa_start = koniec;
            while ((wejscie[koniec] >= 'a' && wejscie[koniec] <= 'z') ||
                   (wejscie[koniec] >= 'A' && wejscie[koniec] <= 'Z') ||
                   (wejscie[koniec] >= '0' && wejscie[koniec] <= '9')) koniec++;
            int nazwa_len = koniec - nazwa_start;
            while (wejscie[koniec] && wejscie[koniec] != '>') koniec++;
            if (wejscie[koniec] == '>') koniec++;

            PominSekcje rodzaj = POMIN_NIC;
            if (html_nazwa_rowna(wejscie + nazwa_start, nazwa_len, "head")) rodzaj = POMIN_HEAD;
            else if (html_nazwa_rowna(wejscie + nazwa_start, nazwa_len, "script")) rodzaj = POMIN_SCRIPT;
            else if (html_nazwa_rowna(wejscie + nazwa_start, nazwa_len, "style")) rodzaj = POMIN_STYLE;
            else if (html_nazwa_rowna(wejscie + nazwa_start, nazwa_len, "svg")) rodzaj = POMIN_SVG;

            if (pomin != POMIN_NIC) {
                if (zamykajacy && rodzaj == pomin) pomin = POMIN_NIC;
                i = koniec;
                continue;
            }
            if (!zamykajacy && rodzaj != POMIN_NIC) {
                pomin = rodzaj;
                i = koniec;
                continue;
            }

            bool nowa_linia = html_nazwa_rowna(wejscie + nazwa_start, nazwa_len, "br") ||
                               (zamykajacy && html_tag_blokowy(wejscie + nazwa_start, nazwa_len));
            if (nowa_linia && o > 0 && wyjscie[o - 1] != '\n') {
                while (o > 0 && wyjscie[o - 1] == ' ') o--;
                if (o + 1 < limit) wyjscie[o++] = '\n';
                oczekuje_spacji = false;
            }
            i = koniec;
            continue;
        }

        if (pomin != POMIN_NIC) { i++; continue; }

        char znak = wejscie[i];
        int zuzyto = 1;
        if (znak == '&') {
            if (czy_tag(wejscie, i, "&nbsp;")) { znak = ' '; zuzyto = 6; }
            else if (czy_tag(wejscie, i, "&amp;")) { znak = '&'; zuzyto = 5; }
            else if (czy_tag(wejscie, i, "&lt;")) { znak = '<'; zuzyto = 4; }
            else if (czy_tag(wejscie, i, "&gt;")) { znak = '>'; zuzyto = 4; }
        }

        if (znak == ' ' || znak == '\t' || znak == '\r' || znak == '\n') {
            oczekuje_spacji = o > 0 && wyjscie[o - 1] != '\n';
        } else {
            if (oczekuje_spacji && o + 1 < limit) wyjscie[o++] = ' ';
            oczekuje_spacji = false;
            if (o + 1 < limit) wyjscie[o++] = znak;
        }
        i += zuzyto;
    }
    while (o > 0 && (wyjscie[o - 1] == ' ' || wyjscie[o - 1] == '\n')) o--;
    wyjscie[o] = '\0';
}

bool czy_tag(const char* s, int pos, const char* tag) {
    int i = 0;
    while(tag[i] != '\0') {
        if ((s[pos+i] | 32) != tag[i]) return false; 
        i++;
    }
    return true;
}

struct HtmlStyl {
    uint32_t kolor;
    int skala;
    char tag[16];
};

static bool html_fragment_rowny(const char* tekst, int poczatek, int koniec, const char* wzorzec) {
    int i = 0;
    while (wzorzec[i]) {
        if (poczatek + i >= koniec || html_mala_litera(tekst[poczatek + i]) != wzorzec[i]) return false;
        i++;
    }
    return poczatek + i == koniec;
}

static int css_liczba_px(const char* tekst, int poczatek, int koniec) {
    while (poczatek < koniec && (tekst[poczatek] == ' ' || tekst[poczatek] == '\t')) poczatek++;
    int wynik = 0;
    while (poczatek < koniec && tekst[poczatek] >= '0' && tekst[poczatek] <= '9')
        wynik = wynik * 10 + (tekst[poczatek++] - '0');
    return wynik;
}

static bool css_kolor_hex(const char* tekst, int poczatek, int koniec, uint32_t* wynik) {
    while (poczatek < koniec && (tekst[poczatek] == ' ' || tekst[poczatek] == '\t')) poczatek++;
    if (poczatek >= koniec || tekst[poczatek++] != '#' || poczatek + 6 > koniec) return false;
    uint32_t kolor = 0;
    for (int i = 0; i < 6; i++) {
        char c = html_mala_litera(tekst[poczatek++]);
        int cyfra = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (cyfra < 0) return false;
        kolor = (kolor << 4) | (uint32_t)cyfra;
    }
    *wynik = kolor;
    return true;
}

static void html_inline_css(const char* html, int od, int do_, uint32_t* kolor,
                            bool* ma_tlo, uint32_t* tlo, int* szer, int* wys) {
    int i = od;
    while (i < do_) {
        if (html_mala_litera(html[i]) == 's' && i + 5 < do_ &&
            czy_tag(html, i, "style")) {
            int p = i + 5;
            while (p < do_ && (html[p] == ' ' || html[p] == '\t')) p++;
            if (p >= do_ || html[p] != '=') { i++; continue; }
            p++;
            while (p < do_ && (html[p] == ' ' || html[p] == '\t')) p++;
            if (p >= do_ || (html[p] != '"' && html[p] != '\'')) return;
            char cytat = html[p++];
            int styl_koniec = p;
            while (styl_koniec < do_ && html[styl_koniec] != cytat) styl_koniec++;

            while (p < styl_koniec) {
                while (p < styl_koniec && (html[p] == ' ' || html[p] == '\t' || html[p] == ';')) p++;
                int nazwa_od = p;
                while (p < styl_koniec && html[p] != ':' && html[p] != ';') p++;
                int nazwa_do = p;
                while (nazwa_do > nazwa_od && (html[nazwa_do - 1] == ' ' || html[nazwa_do - 1] == '\t')) nazwa_do--;
                if (p >= styl_koniec || html[p] != ':') { p++; continue; }
                int wartosc_od = ++p;
                while (p < styl_koniec && html[p] != ';') p++;
                int wartosc_do = p;
                if (html_fragment_rowny(html, nazwa_od, nazwa_do, "background-color"))
                    *ma_tlo = css_kolor_hex(html, wartosc_od, wartosc_do, tlo);
                else if (html_fragment_rowny(html, nazwa_od, nazwa_do, "color"))
                    css_kolor_hex(html, wartosc_od, wartosc_do, kolor);
                else if (html_fragment_rowny(html, nazwa_od, nazwa_do, "width"))
                    *szer = css_liczba_px(html, wartosc_od, wartosc_do);
                else if (html_fragment_rowny(html, nazwa_od, nazwa_do, "height"))
                    *wys = css_liczba_px(html, wartosc_od, wartosc_do);
            }
            return;
        }
        i++;
    }
}

void RysujDrzewoHTML(int start_x, int start_y, const char* html_kod) {
    const int min_y = WIN_Y + 98;
    const int max_x = WIN_X + WIN_W - 16;
    const int max_y = WIN_Y + WIN_H - 12;
    int kursor_x = start_x;
    int kursor_y = start_y;
    uint32_t kolor = 0x00000000;
    int skala = 1;
    bool w_body = false;
    enum UkrytaSekcja { UKRYTA_NIC, UKRYTA_HEAD, UKRYTA_STYLE, UKRYTA_SCRIPT } ukryta = UKRYTA_NIC;
    HtmlStyl stos[32];
    int stos_n = 0;
    bool byla_spacja = false;

    for (int i = 0; html_kod[i];) {
        if (html_kod[i] == '<') {
            int p = i + 1;
            bool zamkniecie = false;
            while (html_kod[p] == ' ' || html_kod[p] == '\t' || html_kod[p] == '\n') p++;
            if (html_kod[p] == '/') { zamkniecie = true; p++; }
            while (html_kod[p] == ' ' || html_kod[p] == '\t') p++;
            int nazwa_od = p;
            while ((html_kod[p] >= 'a' && html_kod[p] <= 'z') ||
                   (html_kod[p] >= 'A' && html_kod[p] <= 'Z') ||
                   (html_kod[p] >= '0' && html_kod[p] <= '9')) p++;
            int nazwa_do = p;
            int tag_koniec = p;
            while (html_kod[tag_koniec] && html_kod[tag_koniec] != '>') tag_koniec++;

            bool tag_head = html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "head");
            bool tag_style = html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "style");
            bool tag_script = html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "script");
            bool tag_body = html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "body");
            if (!zamkniecie && tag_head) ukryta = UKRYTA_HEAD;
            else if (!zamkniecie && tag_style) ukryta = UKRYTA_STYLE;
            else if (!zamkniecie && tag_script) ukryta = UKRYTA_SCRIPT;
            else if (zamkniecie && ((tag_head && ukryta == UKRYTA_HEAD) ||
                     (tag_style && ukryta == UKRYTA_STYLE) || (tag_script && ukryta == UKRYTA_SCRIPT))) ukryta = UKRYTA_NIC;
            else if (tag_body) w_body = !zamkniecie;
            else if (w_body && ukryta == UKRYTA_NIC) {
                bool blok = html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "br") ||
                            html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "div") ||
                            html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "p") ||
                            html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "h1");
                if (zamkniecie) {
                    if (stos_n > 0 && html_fragment_rowny(html_kod, nazwa_od, nazwa_do,
                                                          stos[stos_n - 1].tag)) {
                        kolor = stos[--stos_n].kolor;
                        skala = stos[stos_n].skala;
                    }
                } else {
                    uint32_t poprzedni_kolor = kolor;
                    int poprzednia_skala = skala;
                    bool ma_tlo = false; uint32_t tlo = 0; int szer = 0, wys = 0;
                    html_inline_css(html_kod, p, tag_koniec, &kolor, &ma_tlo, &tlo, &szer, &wys);
                    if (html_fragment_rowny(html_kod, nazwa_od, nazwa_do, "h1")) skala = 2;
                    if ((kolor != poprzedni_kolor || skala != poprzednia_skala) && stos_n < 32) {
                        stos[stos_n].kolor = poprzedni_kolor;
                        stos[stos_n].skala = poprzednia_skala;
                        int n = 0;
                        while (nazwa_od + n < nazwa_do && n < 15) {
                            stos[stos_n].tag[n] = html_mala_litera(html_kod[nazwa_od + n]);
                            n++;
                        }
                        stos[stos_n].tag[n] = '\0';
                        stos_n++;
                    }
                    if (ma_tlo && szer > 0 && wys > 0 && kursor_x < max_x && kursor_y < max_y) {
                        if (kursor_x + szer > max_x) szer = max_x - kursor_x;
                        if (kursor_y + wys > max_y) wys = max_y - kursor_y;
                        if (szer > 0 && wys > 0 && kursor_y + wys >= min_y)
                            gui_rysuj_prostokat(kursor_x, kursor_y < min_y ? min_y : kursor_y,
                                               szer, wys, tlo);
                    }
                }
                if (blok) { kursor_x = start_x; kursor_y += 20 * skala; byla_spacja = false; }
            }
            i = html_kod[tag_koniec] ? tag_koniec + 1 : tag_koniec;
            continue;
        }

        if (!w_body || ukryta != UKRYTA_NIC) { i++; continue; }
        char znak[3] = {0, 0, 0};
        uint32_t unicode = (uint8_t)html_kod[i];
        int bajty_znaku = 1;
        znak[0] = html_kod[i];
        if ((((uint8_t)html_kod[i] & 0xE0) == 0xC0) &&
            html_kod[i + 1] != '\0' && (((uint8_t)html_kod[i + 1] & 0xC0) == 0x80)) {
            znak[1] = html_kod[i + 1];
            unicode = (((uint8_t)html_kod[i] & 0x1F) << 6) |
                      ((uint8_t)html_kod[i + 1] & 0x3F);
            bajty_znaku = 2;
        }
        i += bajty_znaku;
        if (znak[0] == ' ' || znak[0] == '\n' || znak[0] == '\r' || znak[0] == '\t') { byla_spacja = true; continue; }
        if (byla_spacja && kursor_x > start_x) kursor_x += 8 * skala;
        byla_spacja = false;
        int szer_znaku = (int)bws_wywolaj(24, unicode);
        if (szer_znaku <= 0 || szer_znaku > 16) szer_znaku = 8;
        if (kursor_x + (szer_znaku + 1) * skala > max_x) { kursor_x = start_x; kursor_y += 20 * skala; }
        if (kursor_y >= min_y && kursor_y < max_y) {
            wypisz_skalowane(kursor_x, kursor_y, kolor, skala, znak);
        }
        kursor_x += (szer_znaku + 1) * skala;
    }
    calkowita_wysokosc_strony = (kursor_y - start_y) + 20;
    int wysokosc_obszaru_roboczego = WIN_H - 96 - 24;
    max_przewin_y = calkowita_wysokosc_strony > wysokosc_obszaru_roboczego
        ? calkowita_wysokosc_strony - wysokosc_obszaru_roboczego : 0;
    ogranicz_przewiniecie();
}

void rysuj_html(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t domyslny_kolor, int przewin) {
    int obecny_x = px;
    int obecny_y = py - przewin;
    int wys_linii = 20; 
    int skala = 1;
    uint32_t kolor = domyslny_kolor;
    bool pomin_tekst = false;

    int i = 0;
    while (tekst[i] != '\0') {
        if (obecny_y > py + max_wys) break;

        if (tekst[i] == '<') {
            if (czy_tag(tekst, i, "<style") || czy_tag(tekst, i, "<script") || czy_tag(tekst, i, "<head")) {
                pomin_tekst = true;
            } else if (czy_tag(tekst, i, "</style") || czy_tag(tekst, i, "</script") || czy_tag(tekst, i, "</head")) {
                pomin_tekst = false;
            } else if (!pomin_tekst) {
                if (czy_tag(tekst, i, "<h1") || czy_tag(tekst, i, "<h2")) skala = 2;
                else if (czy_tag(tekst, i, "</h1") || czy_tag(tekst, i, "</h2")) skala = 1;
                else if (czy_tag(tekst, i, "<p") || czy_tag(tekst, i, "</p") || czy_tag(tekst, i, "<br") || czy_tag(tekst, i, "<li") || czy_tag(tekst, i, "<div")) {
                    obecny_x = px; 
                    obecny_y += (wys_linii * skala) + 6; 
                }
            }
            while (tekst[i] != '>' && tekst[i] != '\0') i++;
            if (tekst[i] == '>') i++;
            continue;
        }

        if (pomin_tekst) { i++; continue; }

        uint32_t unicode = (uint8_t)tekst[i];
        int char_bytes = 1;
        if (((uint8_t)tekst[i] & 0xE0) == 0xC0 && tekst[i+1] != '\0' &&
            ((uint8_t)tekst[i+1] & 0xC0) == 0x80) {
            unicode = (((uint8_t)tekst[i] & 0x1F) << 6) | ((uint8_t)tekst[i+1] & 0x3F);
            char_bytes = 2;
        }

        int szer_znaku = (int)bws_wywolaj(24, unicode);
        if (szer_znaku <= 0 || szer_znaku > 16) szer_znaku = 8;
        int pelna_szer = (szer_znaku + 1) * skala;

        if (tekst[i] == ' ' || tekst[i] == '\n' || tekst[i] == '\t' || tekst[i] == '\r') {
            if (obecny_x > px) obecny_x += 8 * skala;
            i += char_bytes;
            continue;
        }

        if (obecny_x + pelna_szer > px + max_szer) {
            obecny_x = px;
            obecny_y += (wys_linii * skala);
        }
        
        if (obecny_y >= py && obecny_y < py + max_wys) {
            char znak[3] = { tekst[i], '\0', '\0' };
            if (char_bytes == 2) znak[1] = tekst[i+1];
            wypisz_skalowane(obecny_x, obecny_y, kolor, skala, znak);
        }
        
        obecny_x += pelna_szer;
        i += char_bytes;
    }

    if (obecny_y > py + max_wys) max_przewin_y = (obecny_y - (py + max_wys)) + (wys_linii * skala);
    else max_przewin_y = 0;
}

void rysuj_zwykly_tekst(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t kolor, int przewin) {
    int obecny_x = px;
    int obecny_y = py - przewin;
    int wys_linii = 20; 
    int skala = 1;

    int i = 0;
    while (tekst[i] != '\0') {
        if (obecny_y > py + max_wys) break;
        
        uint32_t unicode = (uint8_t)tekst[i];
        int char_bytes = 1;
        if (((uint8_t)tekst[i] & 0xE0) == 0xC0 && tekst[i+1] != '\0' &&
            ((uint8_t)tekst[i+1] & 0xC0) == 0x80) {
            unicode = (((uint8_t)tekst[i] & 0x1F) << 6) | ((uint8_t)tekst[i+1] & 0x3F);
            char_bytes = 2;
        }

        int szer_znaku = (int)bws_wywolaj(24, unicode);
        if (szer_znaku <= 0 || szer_znaku > 16) szer_znaku = 8;
        int pelna_szer = (szer_znaku + 1) * skala;

        if (tekst[i] == '\n' || tekst[i] == '\r') {
            obecny_x = px; obecny_y += wys_linii * skala; i += char_bytes; continue;
        }

        if (obecny_x + pelna_szer > px + max_szer) { 
            obecny_x = px; 
            obecny_y += wys_linii * skala; 
        }

        if (obecny_y >= py && obecny_y < py + max_wys) {
            char znak[3] = { tekst[i], '\0', '\0' };
            if (char_bytes == 2) znak[1] = tekst[i+1];
            wypisz_skalowane(obecny_x, obecny_y, kolor, skala, znak);
        }
        obecny_x += pelna_szer;
        i += char_bytes;
    }
    if (obecny_y > py + max_wys) max_przewin_y = (obecny_y - (py + max_wys)) + (wys_linii * skala);
    else max_przewin_y = 0;
}

void RysujInterfejs(bool odswiez_tlo) {
    if (odswiez_tlo) {
        gui_odswiez_pulpit();
        gui_rysuj_prostokat(0, screen_h - 40, screen_w, 40, 0x001A0B00);
        gui_rysuj_prostokat(0, screen_h - 40, screen_w, 2, 0x00E58A00);
        RysujPrzycisk(10, screen_h - 35, 80, 30, 0x00E58A00, 0x001A0B00, " Menu");

        gui_rysuj_prostokat(100, screen_h - 40, 140, 40, 0x004A2500);
        gui_rysuj_prostokat(100, screen_h - 40, 1, 40, 0x00E58A00);
        gui_rysuj_prostokat(239, screen_h - 40, 1, 40, 0x00E58A00);
        gui_rysuj_prostokat(100, screen_h - 40, 140, 1, 0x00E58A00);
        gui_wypisz_tekst_kolor(130, screen_h - 28, 0x00FFFFFF, "Hussar");
    }

    gui_rysuj_okno(WIN_X, WIN_Y, WIN_W, WIN_H, "Hussar - Polska Przeglądarka WWW");
    
    RysujPrzyciskLokalny(WIN_X + WIN_W - 74, WIN_Y + 4, 20, 20, 0x00E58A00, 0x001A0B00, "-");
    RysujPrzyciskLokalny(WIN_X + WIN_W - 50, WIN_Y + 4, 20, 20, 0x00E58A00, 0x001A0B00, zmaksymalizowane ? "v" : "^");
    RysujPrzyciskLokalny(WIN_X + WIN_W - 26, WIN_Y + 4, 20, 20, 0x00AA0000, 0x00FFFFFF, "X");

    int zakladka_y = WIN_Y + 28;
    for(int i = 0; i < liczba_zakladek; i++) {
        int tx = WIN_X + 10 + (i * 110);
        uint32_t bg = (i == aktywna_zakladka) ? 0x004A2500 : 0x00202020;
        uint32_t tk = (i == aktywna_zakladka) ? 0x00FFFFFF : 0x00D1D5DB;
        gui_rysuj_prostokat(tx, zakladka_y, 100, 24, bg);
        
        char krotki_url[12] = {0};
        int j = 0;
        while (j < 8 && zakladki[i].url[j]) { krotki_url[j] = zakladki[i].url[j]; j++; }
        if (j == 8 && (((uint8_t)krotki_url[j - 1] & 0xE0) == 0xC0)) krotki_url[--j] = '\0';
        if(zakladki[i].url[0] == '\0') { krotki_url[0]='N'; krotki_url[1]='o'; krotki_url[2]='w'; krotki_url[3]='a'; krotki_url[4]='\0'; }
        gui_wypisz_tekst_kolor(tx + 5, zakladka_y + 4, tk, krotki_url);

        if (liczba_zakladek > 1) {
            RysujPrzyciskLokalny(tx + 80, zakladka_y + 2, 16, 20, 0x00AA0000, 0x00FFFFFF, "X");
        }
    }
    
    if (liczba_zakladek < MAX_ZAKLADKI) {
        int plus_x = WIN_X + 10 + (liczba_zakladek * 110);
        RysujPrzyciskLokalny(plus_x, zakladka_y, 24, 24, 0x00E58A00, 0x001A0B00, "+");
    }

    int narzedzia_y = WIN_Y + 56;
    gui_rysuj_prostokat(WIN_X + 2, narzedzia_y, WIN_W - 4, 36, 0x00202020);

    uint32_t aktywny = 0x004A2500;
    uint32_t nieaktywny = 0x00303030;
    RysujPrzyciskLokalny(WIN_X + 8, narzedzia_y + 4, 34, 28,
                         historia_idx > 0 ? aktywny : nieaktywny, 0x00FFFFFF, "<-");
    RysujPrzyciskLokalny(WIN_X + 44, narzedzia_y + 4, 34, 28,
                         historia_idx < historia_max ? aktywny : nieaktywny, 0x00FFFFFF, "->");
    RysujPrzyciskLokalny(WIN_X + 80, narzedzia_y + 4, 34, 28,
                         aktywny, 0x00FFFFFF, "C");

    int adres_x = WIN_X + 122;
    int adres_w = WIN_W - 412;
    uint32_t kolor_paska = w_polu_url ? 0x00FFFFFF : 0x00303030;
    uint32_t kolor_txt_url = w_polu_url ? 0x00000000 : 0x00D1D5DB;
    gui_rysuj_prostokat(adres_x, narzedzia_y + 4, adres_w, 28, kolor_paska);
    gui_wypisz_tekst_kolor(adres_x + 5, narzedzia_y + 10, kolor_txt_url,
                           zakladki[aktywna_zakladka].url);
    if (w_polu_url) {
        int url_len = dlugosc_tekstu(zakladki[aktywna_zakladka].url);
        int px_kursora = adres_x + 5;
        for(int k=0; k<url_len;) {
             uint32_t unicode = (uint8_t)zakladki[aktywna_zakladka].url[k];
             int bajty = 1;
             if (((uint8_t)zakladki[aktywna_zakladka].url[k] & 0xE0) == 0xC0 &&
                 k + 1 < url_len && ((uint8_t)zakladki[aktywna_zakladka].url[k + 1] & 0xC0) == 0x80) {
                 unicode = (((uint8_t)zakladki[aktywna_zakladka].url[k] & 0x1F) << 6) |
                           ((uint8_t)zakladki[aktywna_zakladka].url[k + 1] & 0x3F);
                 bajty = 2;
             }
             int sw = bws_wywolaj(24, unicode);
             px_kursora += (sw > 0 ? sw : 8) + 1;
             k += bajty;
        }
        gui_wypisz_tekst_kolor(px_kursora, narzedzia_y + 10, 0x00000000, "_");
    }

    RysujPrzyciskLokalny(WIN_X + WIN_W - 286, narzedzia_y + 4, 28, 28,
                         0x00AA0000, 0x00FFFFFF, "X");
    RysujPrzyciskLokalny(WIN_X + WIN_W - 254, narzedzia_y + 4, 54, 28,
                         0x00E58A00, 0x001A0B00, "Idź");
    RysujPrzyciskLokalny(WIN_X + WIN_W - 196, narzedzia_y + 4, 88, 28,
                         aktywny, 0x00FFFFFF, "Zakładki");
    RysujPrzyciskLokalny(WIN_X + WIN_W - 104, narzedzia_y + 4, 96, 28,
                         aktywny, 0x00FFFFFF, "Ustawienia");

    gui_rysuj_prostokat(WIN_X + 2, WIN_Y + WIN_H - 22, WIN_W - 4, 20, 0x00202020);
    gui_wypisz_tekst_kolor(WIN_X + 8, WIN_Y + WIN_H - 18, 0x00FFBF00, status_bufor);

    int obszar_y = WIN_Y + 96;
    int obszar_h = WIN_H - 96 - 24;
    gui_rysuj_prostokat(WIN_X + 2, obszar_y, WIN_W - 4, obszar_h, 0x00F8F9FA);
    
    if (zakladki[aktywna_zakladka].to_jest_html) {
        RysujDrzewoHTML(WIN_X + 8, obszar_y + 5 - zakladki[aktywna_zakladka].przewin_y,
                        zakladki[aktywna_zakladka].html);
    } else {
        const char* do_wysw = zakladki[aktywna_zakladka].html;
        if(do_wysw[0] == '\0') do_wysw = "Wpisz adres strony u góry i wciśnij „Idź”.";
        rysuj_zwykly_tekst(WIN_X + 8, obszar_y + 5, WIN_W - 24, obszar_h - 10, do_wysw, 0x00222222, zakladki[aktywna_zakladka].przewin_y);
    }

    // Layout powyżej wyznacza aktualne max_przewin_y; dopiero teraz rysujemy suwak.
    if (max_przewin_y > 0) {
        int scroll_x = WIN_X + WIN_W - 8;
        gui_rysuj_prostokat(scroll_x, obszar_y + 2, 6, obszar_h - 4, 0x00CCCCCC);

        int mianownik = obszar_h + max_przewin_y;
        if (mianownik <= 0) mianownik = 1;

        int suwak_h = (obszar_h * obszar_h) / mianownik;
        if (suwak_h < 10) suwak_h = 10;
        int suwak_y = obszar_y + 2 +
            ((obszar_h - 4 - suwak_h) * zakladki[aktywna_zakladka].przewin_y) / max_przewin_y;
        gui_rysuj_prostokat(scroll_x, suwak_y, 6, suwak_h, 0x00E58A00);
    }

    int menu_x = WIN_X + WIN_W - 270;
    int menu_y = WIN_Y + 94;
    if (menu_ulubione_otwarte) {
        int menu_h = 24 + ulubione_ilosc * 20;
        gui_rysuj_prostokat(menu_x, menu_y, 260, menu_h, 0x004A2500);
        gui_rysuj_prostokat(menu_x, menu_y, 260, 1, 0x00E58A00);
        gui_wypisz_tekst_kolor(menu_x + 8, menu_y + 4, 0x00FFFFFF,
                               "+ Dodaj obecną stronę");
        for (int u = 0; u < ulubione_ilosc; u++) {
            char etykieta[29];
            kopiuj_tekst_limit(etykieta, ulubione[u], sizeof(etykieta));
            gui_wypisz_tekst_kolor(menu_x + 8, menu_y + 26 + u * 20,
                                   0x00FFFFFF, etykieta);
        }
    } else if (menu_ustawienia_otwarte) {
        int ustawienia_x = WIN_X + WIN_W - 180;
        gui_rysuj_prostokat(ustawienia_x, menu_y, 172, 32, 0x004A2500);
        gui_rysuj_prostokat(ustawienia_x, menu_y, 172, 1, 0x00E58A00);
        gui_wypisz_tekst_kolor(ustawienia_x + 12, menu_y + 8,
                               0x00FFFFFF, "Brak opcji");
    }

    gui_odswiez();
}

static char przegladarka_mala_litera(char znak) {
    return (znak >= 'A' && znak <= 'Z') ? znak + ('a' - 'A') : znak;
}

static bool przegladarka_prefiks(const char* tekst, const char* prefiks) {
    int i = 0;
    while (prefiks[i]) {
        if (tekst[i] != prefiks[i]) return false;
        i++;
    }
    return true;
}

static bool znajdz_location(const char* odpowiedz, char* wynik, int pojemnosc) {
    int i = 0;
    while (odpowiedz[i]) {
        if (odpowiedz[i] == '\r' && odpowiedz[i + 1] == '\n' &&
            odpowiedz[i + 2] == '\r' && odpowiedz[i + 3] == '\n') break;

        bool poczatek_linii = (i == 0 || odpowiedz[i - 1] == '\n');
        const char nazwa[] = "location:";
        bool pasuje = poczatek_linii;
        for (int j = 0; pasuje && nazwa[j]; j++)
            pasuje = przegladarka_mala_litera(odpowiedz[i + j]) == nazwa[j];

        if (pasuje) {
            i += 9;
            while (odpowiedz[i] == ' ' || odpowiedz[i] == '\t') i++;
            int j = 0;
            while (odpowiedz[i] && odpowiedz[i] != '\r' && odpowiedz[i] != '\n' && j + 1 < pojemnosc)
                wynik[j++] = odpowiedz[i++];
            wynik[j] = '\0';
            return j > 0;
        }
        i++;
    }
    wynik[0] = '\0';
    return false;
}

static void ustaw_url_z_przekierowania(char* cel, const char* location,
                                       bool bylo_https, const char* domena) {
    int j = 0;
    const char* schemat = bylo_https ? "https:" : "http:";
    const char* pelny_schemat = bylo_https ? "https://" : "http://";

    if (przegladarka_prefiks(location, "http://") || przegladarka_prefiks(location, "https://")) {
        while (location[j] && j < 255) { cel[j] = location[j]; j++; }
    } else {
        const char* poczatek = (location[0] == '/' && location[1] == '/') ? schemat : pelny_schemat;
        int i = 0;
        while (poczatek[i] && j < 255) cel[j++] = poczatek[i++];
        if (!(location[0] == '/' && location[1] == '/')) {
            i = 0;
            while (domena[i] && j < 255) cel[j++] = domena[i++];
            if (location[0] != '/' && j < 255) cel[j++] = '/';
        }
        i = 0;
        while (location[i] && j < 255) cel[j++] = location[i++];
    }
    cel[j] = '\0';
}

// --- ZMODYFIKOWANA FUNKCJA PobierzStrone() Z OBSŁUGĄ KODÓW HTTP ---
void PobierzStrone() {
    char* url = zakladki[aktywna_zakladka].url;
    if (!przegladarka_prefiks(url, "http://") && !przegladarka_prefiks(url, "https://")) {
        int dlugosc = dlugosc_tekstu(url);
        if (dlugosc > 247) dlugosc = 247;
        for (int j = dlugosc; j >= 0; j--) url[j + 8] = url[j];
        const char https[] = "https://";
        for (int j = 0; j < 8; j++) url[j] = https[j];
    }

    ustaw_status("DNS: Szukanie serwera...");
    zakladki[aktywna_zakladka].wczytana = false;
    zakladki[aktywna_zakladka].to_jest_html = false;
    const char* l1 = "Ładowanie strony...";
    int p = 0; while(l1[p]) { zakladki[aktywna_zakladka].html[p] = l1[p]; p++; }
    zakladki[aktywna_zakladka].html[p] = '\0';
    zakladki[aktywna_zakladka].przewin_y = 0;
    
    // DNS jest synchroniczny. Najpierw pokaz caly pulpit, pasek zadan i status,
    // aby zatrzymana na czas sieci petla programu nie pozostawila polramki.
    RysujInterfejs(true);
    gui_odswiez();

    char domena[64] = {0};
    char sciezka[256] = {0};
    
    int i = 0;
    const char* raw_url = zakladki[aktywna_zakladka].url;
    bool uzyj_https = true;
    
    // Ignorowanie HTTP
    if (raw_url[0] == 'h' && raw_url[1] == 't' && raw_url[2] == 't' && raw_url[3] == 'p' && raw_url[4] == ':' && raw_url[5] == '/' && raw_url[6] == '/') {
        i = 7;
        uzyj_https = false;
    }
    // Wykrywanie i elegancka blokada HTTPS
    else if (raw_url[0] == 'h' && raw_url[1] == 't' && raw_url[2] == 't' && raw_url[3] == 'p' && raw_url[4] == 's' && raw_url[5] == ':' && raw_url[6] == '/' && raw_url[7] == '/') {
        i = 8;
        uzyj_https = true;
    }

    int d_idx = 0;
    while (raw_url[i] != '/' && raw_url[i] != '\0' && d_idx < 63) {
        domena[d_idx++] = raw_url[i++];
    }
    domena[d_idx] = '\0';
    
    int k = 0;
    if (raw_url[i] == '/') {
        while (raw_url[i] != '\0' && k < 255) { sciezka[k++] = raw_url[i++]; }
        sciezka[k] = '\0';
    } else { sciezka[0] = '/'; sciezka[1] = '\0'; }

    uint8_t ip_serwera[4] = {0,0,0,0};
    if (bws_siec_dns(domena, ip_serwera)) {
        ustaw_status(uzyj_https ? "TLS: Uścisk dłoni..." : "HTTP: Pobieranie...");
        // Pobranie HTTP/HTTPS rowniez blokuje proces do zakonczenia transmisji.
        RysujInterfejs(true);
        gui_odswiez();

        for(int c = 0; c < HTML_POJEMNOSC; c++) temp_bufor[c] = 0;

        // Jeśli Jądro poprawnie odebrało ramki TCP
        bool pobrano = uzyj_https
            ? bws_siec_pobierz_https(ip_serwera, domena, sciezka, temp_bufor, HTML_POJEMNOSC - 1)
            : bws_siec_pobierz_http(ip_serwera, domena, sciezka, temp_bufor, HTML_POJEMNOSC - 1);
        if (pobrano) {
            
            // --- ODCZYTUJEMY KOD STATUSU Z MODUŁU http_kody.h ---
            int kod_http = wyciagnij_kod_http(temp_bufor);
            
            if (kod_http >= 200 && kod_http < 300) {
                // Zachowujemy surowy HTML: silnik layoutu interpretuje tagi i CSS
                // podczas rysowania, zamiast bezpowrotnie zamieniac je na tekst.
                const char* html_start = oczysc_http(temp_bufor);
                int j = 0;
                while(html_start[j] != '\0' && j < HTML_POJEMNOSC - 1) {
                    zakladki[aktywna_zakladka].html[j] = html_start[j];
                    j++;
                }
                if (html_start[j] != '\0' && (((uint8_t)html_start[j] & 0xC0) == 0x80))
                    while (j > 0 && (((uint8_t)zakladki[aktywna_zakladka].html[j - 1] & 0xC0) == 0x80)) j--;
                if (html_start[j] != '\0' && (((uint8_t)html_start[j] & 0xC0) == 0x80) && j > 0) j--;
                zakladki[aktywna_zakladka].html[j] = '\0';
                zakladki[aktywna_zakladka].to_jest_html = true;
                if (nagraj_historie_po_sukcesie) {
                    DopiszDoHistorii(zakladki[aktywna_zakladka].url);
                    nagraj_historie_po_sukcesie = false;
                }
                if (uzyj_https && !bws_tls_certyfikat_zaufany())
                    ustaw_status("HTTPS: szyfrowane, certyfikat bez zaufanego CA");
                else ustaw_status(uzyj_https ? "HTTPS: połączenie bezpieczne" : "Gotowy");
                
            } else if (kod_http == 301 || kod_http == 302 || kod_http == 307 || kod_http == 308) {
                char location[256] = {0};
                if (glebokosc_przekierowan < 3 && znajdz_location(temp_bufor, location, sizeof(location))) {
                    ustaw_url_z_przekierowania(zakladki[aktywna_zakladka].url,
                                               location, uzyj_https, domena);
                    glebokosc_przekierowan++;
                    ustaw_status("Przekierowanie HTTP...");
                    PobierzStrone();
                    glebokosc_przekierowan--;
                    return;
                }

                const char* opis = glebokosc_przekierowan >= 3
                    ? "Przekroczono limit przekierowań." : "Brak nagłówka Location.";
                zloz_strone_bledu(zakladki[aktywna_zakladka].html, kod_http, opis);
                zakladki[aktywna_zakladka].to_jest_html = true;
                ustaw_status("Błąd przekierowania HTTP");
            } else if (kod_http > 0) {
                // Kod 3xx, 4xx, 5xx - Serwer zwrócił błąd lub przekierowanie
                const char* opis = pobierz_opis_kodu_http(kod_http);
                zloz_strone_bledu(zakladki[aktywna_zakladka].html, kod_http, opis);
                zakladki[aktywna_zakladka].to_jest_html = true; // Złożyliśmy to w ładny HTML
                ustaw_status("Odebrano status HTTP");
                
            } else {
                // Kod = 0, brak poprawnego nagłówka HTTP w pakiecie TCP
                const char* err = "<h1>Błąd protokołu</h1><br><p>Odpowiedź serwera nie jest zgodna ze standardem HTTP.</p>";
                int err_p = 0; while(err[err_p]) { zakladki[aktywna_zakladka].html[err_p] = err[err_p]; err_p++; }
                zakladki[aktywna_zakladka].html[err_p] = '\0';
                zakladki[aktywna_zakladka].to_jest_html = true;
                ustaw_status("Nierozpoznana odpowiedź");
            }
            
        } else {
            // Problem z samym połączeniem TCP w Jądrze
            zakladki[aktywna_zakladka].to_jest_html = false;
            const char* err = "Błąd HTTP: Brak połączenia TCP.";
            int err_p = 0; while(err[err_p]) { zakladki[aktywna_zakladka].html[err_p] = err[err_p]; err_p++; }
            zakladki[aktywna_zakladka].html[err_p] = '\0';
            ustaw_status("Błąd HTTP");
            // Blad TCP/TLS nie jest przekierowaniem. Natychmiast zakoncz probe,
            // aby nie uruchomic ponownie DNS ani polaczenia z rekurencji redirectu.
            return;
        }
    } else {
        zakladki[aktywna_zakladka].to_jest_html = false;
        const char* err = "Błąd DNS: Nie udało się rozwiązać adresu IP domeny.";
        int err_p = 0; while(err[err_p]) { zakladki[aktywna_zakladka].html[err_p] = err[err_p]; err_p++; }
        zakladki[aktywna_zakladka].html[err_p] = '\0';
        ustaw_status("Błąd DNS");
    }
}
