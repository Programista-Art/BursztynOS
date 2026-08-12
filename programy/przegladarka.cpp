/*
 * Przeglądarka Internetowa "Hussar" dla Bursztyn OS (Ring 3)
 * Wersja z obsługą myszy, pełnym oknem, parsowaniem HTML i skalowaniem.
 */

#include "../bursztyn_gui.h"
#include <stdint.h>
#include <stdbool.h>

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
        20480, 524288, 0x605000  // 512 KB Pamięci dla zakładek (Loader zaalokuje to w locie)
    };
    
    bool bws_siec_dns(const char* domena, uint8_t* wyjsciowy_ip);
    bool bws_siec_pobierz_http(uint8_t* cel_ip, const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc);
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
bool czy_tag(const char* s, int pos, const char* tag);
void rysuj_html(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t domyslny_kolor, int przewin);
void rysuj_zwykly_tekst(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t kolor, int przewin);
void wypisz_skalowane(int x, int y, uint32_t kolor, int skala, const char* text);
void RysujPrzyciskLokalny(int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* txt);

struct Zakladka {
    char url[256];
    char html[32000];
    int przewin_y;
    bool to_jest_html;
    bool wczytana;
};

#define MAX_ZAKLADKI 6

// Zmienne BSS
static Zakladka zakladki[MAX_ZAKLADKI]; 
static char temp_bufor[32000]; 
static char status_bufor[64];

int liczba_zakladek;
int aktywna_zakladka;
int okno_x, okno_y, okno_w, okno_h;
int stare_okno_x, stare_okno_y, stare_okno_w, stare_okno_h;
bool zmaksymalizowane;
bool przeciagane;
int chwyt_x, chwyt_y;
int ekran_w, ekran_h;
int max_przewin_y;
bool w_polu_url;

// =========================================================================
// GŁÓWNY PUNKT WEJŚCIA PROGRAMU (Musi być dokładnie tutaj)
// =========================================================================
extern "C" __attribute__((noreturn)) void _start() {
    liczba_zakladek = 1;
    aktywna_zakladka = 0;
    okno_x = 50; okno_y = 50; okno_w = 820; okno_h = 560;
    stare_okno_x = 50; stare_okno_y = 50; stare_okno_w = 820; stare_okno_h = 560;
    zmaksymalizowane = false;
    przeciagane = false;
    chwyt_x = 0; chwyt_y = 0;
    max_przewin_y = 0;
    w_polu_url = false;

    const char* txt_start = "Gotowy";
    int z = 0; while (txt_start[z]) { status_bufor[z] = txt_start[z]; z++; }
    status_bufor[z] = '\0';

    for(int i = 0; i < MAX_ZAKLADKI; i++) {
        for(int j = 0; j < 256; j++) ((volatile char*)zakladki[i].url)[j] = 0;
        for(int j = 0; j < 32000; j++) ((volatile char*)zakladki[i].html)[j] = 0;
        zakladki[i].przewin_y = 0;
        zakladki[i].to_jest_html = false;
        zakladki[i].wczytana = false;
    }
    for(int i = 0; i < 32000; i++) ((volatile char*)temp_bufor)[i] = 0;

    gui_pobierz_rozdzielczosc(&ekran_w, &ekran_h);
    gui_ustaw_przejecie_myszy(true);

    bool dziala = true;
    uint8_t poprz_przycisk = 0;
    int stary_mysz_x = -1, stary_mysz_y = -1;
    int ansi_stan = 0;
    
    bool przerysuj = true;
    bool odswiez_tlo = true;

    // Domyślny startowy adres URL
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
            if (przeciagane) odswiez_tlo = true;
            przerysuj = true;
        }

        if (klik) {
            przerysuj = true; odswiez_tlo = true;
            
            // 1. Przyciski okna (Prawy róg)
            if (my >= okno_y + 4 && my <= okno_y + 24) {
                if (mx >= okno_x + okno_w - 74 && mx <= okno_x + okno_w - 54) { dziala = false; } 
                else if (mx >= okno_x + okno_w - 50 && mx <= okno_x + okno_w - 30) { 
                    if (!zmaksymalizowane) {
                        stare_okno_x = okno_x; stare_okno_y = okno_y; stare_okno_w = okno_w; stare_okno_h = okno_h;
                        okno_x = 0; okno_y = 0; okno_w = ekran_w; okno_h = ekran_h - 40; 
                        zmaksymalizowane = true;
                    } else {
                        okno_x = stare_okno_x; okno_y = stare_okno_y; okno_w = stare_okno_w; okno_h = stare_okno_h;
                        zmaksymalizowane = false;
                    }
                }
                else if (mx >= okno_x + okno_w - 26 && mx <= okno_x + okno_w - 6) { dziala = false; } 
            }
            // 2. Przeciąganie okna
            else if (my >= okno_y && my <= okno_y + 26 && mx >= okno_x && mx < okno_x + okno_w - 80) {
                przeciagane = true; chwyt_x = mx - okno_x; chwyt_y = my - okno_y; w_polu_url = false;
                ustaw_status("Przesuwanie okna...");
            }
            // 3. Zakładki
            else if (my >= okno_y + 28 && my <= okno_y + 52) {
                for(int i = 0; i < liczba_zakladek; i++) {
                    int tx = okno_x + 10 + (i * 110);
                    if (mx >= tx && mx <= tx + 100) {
                        if (liczba_zakladek > 1 && mx >= tx + 80 && mx <= tx + 96) {
                            for(int j = i; j < liczba_zakladek-1; j++) zakladki[j] = zakladki[j+1];
                            liczba_zakladek--;
                            if (aktywna_zakladka >= liczba_zakladek) aktywna_zakladka = liczba_zakladek - 1;
                            ustaw_status("Zamknieto zakladke.");
                        } else {
                            aktywna_zakladka = i;
                            ustaw_status("Przelaczono zakladke.");
                        }
                        w_polu_url = false;
                        break;
                    }
                }
                if (liczba_zakladek < MAX_ZAKLADKI) {
                    int plus_x = okno_x + 10 + (liczba_zakladek * 110);
                    if (mx >= plus_x && mx <= plus_x + 24) {
                        zakladki[liczba_zakladek].url[0] = '\0';
                        zakladki[liczba_zakladek].html[0] = '\0';
                        zakladki[liczba_zakladek].przewin_y = 0;
                        zakladki[liczba_zakladek].to_jest_html = false;
                        aktywna_zakladka = liczba_zakladek;
                        liczba_zakladek++;
                        ustaw_status("Nowa zakladka.");
                    }
                }
            }
            // 4. Pasek adresu i Idź
            else if (my >= okno_y + 56 && my <= okno_y + 92) {
                int narzedzia_y = okno_y + 56;
                if (mx >= okno_x + 10 && mx <= okno_x + okno_w - 210 && my >= narzedzia_y + 4 && my <= narzedzia_y + 32) {
                    w_polu_url = true; ustaw_status("Edycja adresu URL...");
                }
                else if (mx >= okno_x + okno_w - 200 && mx <= okno_x + okno_w - 160) { w_polu_url = false; usun_ostatni_znak(zakladki[aktywna_zakladka].url); }
                else if (mx >= okno_x + okno_w - 150 && mx <= okno_x + okno_w - 110) { w_polu_url = false; if(zakladki[aktywna_zakladka].url[0] != '\0') PobierzStrone(); }
                else if (mx >= okno_x + okno_w - 100 && mx <= okno_x + okno_w - 10) { w_polu_url = false; PobierzStrone(); }
                else { w_polu_url = false; }
            }
            else { w_polu_url = false; }
        }

        if (pusc) { przeciagane = false; przerysuj = true; if(status_bufor[0]=='P') ustaw_status("Gotowy"); }
        
        // Aktualizacja pozycji przeciąganego okna
        if (przeciagane && przytrzymany) {
            okno_x = mx - chwyt_x; okno_y = my - chwyt_y;
            if (okno_x < 0) okno_x = 0;
            if (okno_y < 0) okno_y = 0;
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
                if (znak == 'A' && zakladki[aktywna_zakladka].przewin_y > 0) zakladki[aktywna_zakladka].przewin_y -= 20;
                else if (znak == 'B' && zakladki[aktywna_zakladka].przewin_y < max_przewin_y) zakladki[aktywna_zakladka].przewin_y += 20;
            } else {
                ansi_stan = 0;
                if (w_polu_url) {
                    if (znak == '\n' || znak == '\r') { w_polu_url = false; PobierzStrone(); }
                    else if (znak == '\b') usun_ostatni_znak(zakladki[aktywna_zakladka].url);
                    else if (znak >= 32) dopisz_znak(zakladki[aktywna_zakladka].url, znak, 255);
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

// Nowa, niezawodna funkcja skalująca
void wypisz_skalowane(int x, int y, uint32_t kolor, int skala, const char* text) {
    uint64_t arg_kolor_skala = ((uint64_t)skala << 32) | kolor;
    bws_wywolaj(20, (uint64_t)x, (uint64_t)y, arg_kolor_skala, (uint64_t)text);
}

// INTELEGIENTNE RYSOWANIE PRZYCISKÓW (Idealne wyśrodkowanie bez krzywego tekstu)
void RysujPrzyciskLokalny(int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* txt) {
    gui_rysuj_prostokat(x, y, w, h, bg);
    
    // Obliczamy fizyczną, precyzyjną szerokość napisu w pikselach
    int text_w = 0;
    int i = 0;
    while (txt[i] != '\0') {
        uint32_t unicode = (uint8_t)txt[i];
        int char_bytes = 1;
        if ((txt[i] & 0xE0) == 0xC0 && txt[i+1] != '\0') {
            unicode = (((uint8_t)txt[i] & 0x1F) << 6) | ((uint8_t)txt[i+1] & 0x3F);
            char_bytes = 2;
        }
        int sw = (int)bws_wywolaj(24, unicode);
        if (sw <= 0) sw = 8;
        text_w += sw + 1; // szerokość znaku + 1px odstępu
        i += char_bytes;
    }
    
    // Rysowanie z wyliczeniem matematycznego środka
    int px = x + (w - text_w) / 2;
    int py = y + (h - 16) / 2;
    if (py < y) py = y;
    gui_wypisz_tekst_kolor(px, py, fg, txt);
}

int dlugosc_tekstu(const char* s) { int len = 0; while (s[len]) len++; return len; }
void dopisz_znak(char* s, char z, int max_len) { int len = dlugosc_tekstu(s); if (len < max_len - 1) { s[len] = z; s[len+1] = '\0'; } }
void usun_ostatni_znak(char* s) { int len = dlugosc_tekstu(s); if (len > 0) s[len-1] = '\0'; }
void ustaw_status(const char* txt) { int i = 0; while(txt[i] && i < 63) { status_bufor[i] = txt[i]; i++; } status_bufor[i] = '\0'; }

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

bool czy_tag(const char* s, int pos, const char* tag) {
    int i = 0;
    while(tag[i] != '\0') {
        if ((s[pos+i] | 32) != tag[i]) return false; 
        i++;
    }
    return true;
}

// ZAAWANSOWANY SILNIK RENDERUJĄCY HTML Z DYNAMICZNĄ SZEROKOŚCIĄ
void rysuj_html(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t domyslny_kolor, int przewin) {
    int obecny_x = px;
    int obecny_y = py - przewin;
    int wys_linii = 20; // Zwiększona interlinia (koniec z pionowym nachodzeniem na siebie)
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
                    obecny_y += (wys_linii * skala) + 6; // Odstęp akapitu
                }
            }
            while (tekst[i] != '>' && tekst[i] != '\0') i++;
            if (tekst[i] == '>') i++;
            continue;
        }

        if (pomin_tekst) { i++; continue; }

        uint32_t unicode = (uint8_t)tekst[i];
        int char_bytes = 1;
        if ((tekst[i] & 0xE0) == 0xC0 && tekst[i+1] != '\0') {
            unicode = (((uint8_t)tekst[i] & 0x1F) << 6) | ((uint8_t)tekst[i+1] & 0x3F);
            char_bytes = 2;
        }

        // POPRAWKA KRYTYCZNA: Pobieramy dokładną, dynamiczną szerokość tego konkretnego znaku
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
        
        // Zamiast sztywnego 9*skala, dodajemy faktyczną szerokość litery
        obecny_x += pelna_szer;
        i += char_bytes;
    }

    if (obecny_y > py + max_wys) max_przewin_y = (obecny_y - (py + max_wys)) + (wys_linii * skala);
    else max_przewin_y = 0;
}

void rysuj_zwykly_tekst(int px, int py, int max_szer, int max_wys, const char* tekst, uint32_t kolor, int przewin) {
    int obecny_x = px;
    int obecny_y = py - przewin;
    int wys_linii = 20; // Zwiększona interlinia
    int skala = 1;

    int i = 0;
    while (tekst[i] != '\0') {
        if (obecny_y > py + max_wys) break;
        
        uint32_t unicode = (uint8_t)tekst[i];
        int char_bytes = 1;
        if ((tekst[i] & 0xE0) == 0xC0 && tekst[i+1] != '\0') {
            unicode = (((uint8_t)tekst[i] & 0x1F) << 6) | ((uint8_t)tekst[i+1] & 0x3F);
            char_bytes = 2;
        }

        // Dynamiczna szerokość dla zwykłego tekstu
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
        gui_rysuj_prostokat(0, ekran_h - 40, ekran_w, 40, 0x001A0B00);
        gui_rysuj_prostokat(0, ekran_h - 40, ekran_w, 2, 0x00E58A00);
        
        // Zastępujemy biblioteczne przyciski naszą perfekcyjną funkcją
        RysujPrzyciskLokalny(10, ekran_h - 35, 80, 30, 0x00E58A00, 0x001A0B00, "Menu");
        RysujPrzyciskLokalny(100, ekran_h - 35, 140, 30, 0x004A2500, 0x00FFFFFF, "Hussar");
    }

    gui_rysuj_okno(okno_x, okno_y, okno_w, okno_h, "Hussar - Przegladarka WWW");
    
    // Przyciski kontrolne okna
    RysujPrzyciskLokalny(okno_x + okno_w - 74, okno_y + 4, 20, 20, 0x00E58A00, 0x001A0B00, "-");
    RysujPrzyciskLokalny(okno_x + okno_w - 50, okno_y + 4, 20, 20, 0x00E58A00, 0x001A0B00, zmaksymalizowane ? "v" : "^");
    RysujPrzyciskLokalny(okno_x + okno_w - 26, okno_y + 4, 20, 20, 0x00AA0000, 0x00FFFFFF, "X");

    int zakladka_y = okno_y + 28;
    for(int i = 0; i < liczba_zakladek; i++) {
        int tx = okno_x + 10 + (i * 110);
        uint32_t bg = (i == aktywna_zakladka) ? 0x004A2500 : 0x00202020;
        uint32_t tk = (i == aktywna_zakladka) ? 0x00FFFFFF : 0x00D1D5DB;
        gui_rysuj_prostokat(tx, zakladka_y, 100, 24, bg);
        
        char krotki_url[12] = {0};
        for(int j=0; j<8 && zakladki[i].url[j]; j++) krotki_url[j] = zakladki[i].url[j];
        if(zakladki[i].url[0] == '\0') { krotki_url[0]='N'; krotki_url[1]='o'; krotki_url[2]='w'; krotki_url[3]='a'; krotki_url[4]='\0'; }
        gui_wypisz_tekst_kolor(tx + 5, zakladka_y + 4, tk, krotki_url);

        if (liczba_zakladek > 1) {
            RysujPrzyciskLokalny(tx + 80, zakladka_y + 2, 16, 20, 0x00AA0000, 0x00FFFFFF, "X");
        }
    }
    
    if (liczba_zakladek < MAX_ZAKLADKI) {
        int plus_x = okno_x + 10 + (liczba_zakladek * 110);
        RysujPrzyciskLokalny(plus_x, zakladka_y, 24, 24, 0x00E58A00, 0x001A0B00, "+");
    }

    int narzedzia_y = okno_y + 56;
    gui_rysuj_prostokat(okno_x + 2, narzedzia_y, okno_w - 4, 36, 0x00202020);

    uint32_t kolor_paska = w_polu_url ? 0x00FFFFFF : 0x00303030;
    uint32_t kolor_txt_url = w_polu_url ? 0x00000000 : 0x00D1D5DB;
    gui_rysuj_prostokat(okno_x + 10, narzedzia_y + 4, okno_w - 220, 28, kolor_paska);
    gui_wypisz_tekst_kolor(okno_x + 15, narzedzia_y + 10, kolor_txt_url, zakladki[aktywna_zakladka].url);
    if (w_polu_url) {
        int url_len = dlugosc_tekstu(zakladki[aktywna_zakladka].url);
        // Oblicz precyzyjnie szerokość wpisanego URL'a
        int px_kursora = okno_x + 15;
        for(int k=0; k<url_len; k++) {
             int sw = bws_wywolaj(24, (uint8_t)zakladki[aktywna_zakladka].url[k]);
             px_kursora += (sw > 0 ? sw : 8) + 1;
        }
        gui_wypisz_tekst_kolor(px_kursora, narzedzia_y + 10, 0x00000000, "_");
    }

    // Pasek przycisków
    RysujPrzyciskLokalny(okno_x + okno_w - 200, narzedzia_y + 4, 40, 28, 0x00AA0000, 0x00FFFFFF, "<-"); 
    RysujPrzyciskLokalny(okno_x + okno_w - 150, narzedzia_y + 4, 40, 28, 0x004A2500, 0x00FFFFFF, "O");   
    RysujPrzyciskLokalny(okno_x + okno_w - 100, narzedzia_y + 4, 90, 28, 0x00E58A00, 0x001A0B00, "Idz");

    gui_rysuj_prostokat(okno_x + 2, okno_y + okno_h - 22, okno_w - 4, 20, 0x00202020);
    gui_wypisz_tekst_kolor(okno_x + 8, okno_y + okno_h - 18, 0x00FFBF00, status_bufor);

    int obszar_y = okno_y + 96;
    int obszar_h = okno_h - 96 - 24;
    gui_rysuj_prostokat(okno_x + 2, obszar_y, okno_w - 4, obszar_h, 0x00F8F9FA);
    
    if (max_przewin_y > 0) {
        int scroll_x = okno_x + okno_w - 8;
        gui_rysuj_prostokat(scroll_x, obszar_y + 2, 6, obszar_h - 4, 0x00CCCCCC);
        
        int mianownik = obszar_h + max_przewin_y;
        if (mianownik <= 0) mianownik = 1;
        
        int suwak_h = (obszar_h * obszar_h) / mianownik;
        if (suwak_h < 10) suwak_h = 10;
        int suwak_y = obszar_y + 2 + ((obszar_h - 4 - suwak_h) * zakladki[aktywna_zakladka].przewin_y) / max_przewin_y;
        gui_rysuj_prostokat(scroll_x, suwak_y, 6, suwak_h, 0x00E58A00);
    }

    if (zakladki[aktywna_zakladka].to_jest_html) {
        rysuj_html(okno_x + 8, obszar_y + 5, okno_w - 24, obszar_h - 10, zakladki[aktywna_zakladka].html, 0x00222222, zakladki[aktywna_zakladka].przewin_y);
    } else {
        const char* do_wysw = zakladki[aktywna_zakladka].html;
        if(do_wysw[0] == '\0') do_wysw = "Wpisz adres strony u gory i wcisnij 'Idz'.";
        rysuj_zwykly_tekst(okno_x + 8, obszar_y + 5, okno_w - 24, obszar_h - 10, do_wysw, 0x00222222, zakladki[aktywna_zakladka].przewin_y);
    }

    gui_odswiez();
}

void PobierzStrone() {
    ustaw_status("DNS: Szukanie serwera...");
    zakladki[aktywna_zakladka].wczytana = false;
    zakladki[aktywna_zakladka].to_jest_html = false;
    const char* l1 = "Ladowanie strony...";
    int p = 0; while(l1[p]) { zakladki[aktywna_zakladka].html[p] = l1[p]; p++; }
    zakladki[aktywna_zakladka].html[p] = '\0';
    zakladki[aktywna_zakladka].przewin_y = 0;
    
    RysujInterfejs(false); 

    char domena[64] = {0};
    char sciezka[128] = {0};
    
    int i = 0;
    while (zakladki[aktywna_zakladka].url[i] != '/' && zakladki[aktywna_zakladka].url[i] != '\0' && i < 63) {
        domena[i] = zakladki[aktywna_zakladka].url[i]; i++;
    }
    domena[i] = '\0';
    
    int k = 0;
    if (zakladki[aktywna_zakladka].url[i] == '/') {
        while (zakladki[aktywna_zakladka].url[i] != '\0' && k < 127) { sciezka[k++] = zakladki[aktywna_zakladka].url[i++]; }
        sciezka[k] = '\0';
    } else { sciezka[0] = '/'; sciezka[1] = '\0'; }

    uint8_t ip_serwera[4] = {0,0,0,0};
    if (bws_siec_dns(domena, ip_serwera)) {
        ustaw_status("HTTP: Pobieranie...");
        RysujInterfejs(false); 

        for(int c = 0; c < 32000; c++) temp_bufor[c] = 0;

        if (bws_siec_pobierz_http(ip_serwera, domena, sciezka, temp_bufor, 31999)) {
            const char* html_start = oczysc_http(temp_bufor);
            int j = 0;
            while(html_start[j] != '\0' && j < 31999) { zakladki[aktywna_zakladka].html[j] = html_start[j]; j++; }
            zakladki[aktywna_zakladka].html[j] = '\0';
            zakladki[aktywna_zakladka].to_jest_html = true;
            ustaw_status("Gotowy");
        } else {
            zakladki[aktywna_zakladka].to_jest_html = false;
            const char* err = "Blad HTTP.";
            p = 0; while(err[p]) { zakladki[aktywna_zakladka].html[p] = err[p]; p++; }
            zakladki[aktywna_zakladka].html[p] = '\0';
            ustaw_status("Blad HTTP");
        }
    } else {
        zakladki[aktywna_zakladka].to_jest_html = false;
        const char* err = "Blad DNS.";
        p = 0; while(err[p]) { zakladki[aktywna_zakladka].html[p] = err[p]; p++; }
        zakladki[aktywna_zakladka].html[p] = '\0';
        ustaw_status("Blad DNS");
    }
}