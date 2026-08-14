/*
 * Biblioteka Współdzielona GUI dla Bursztyn OS (Ring 3)
 * Zawiera wrappery do komunikacji z Jądrem (Syscalls)
 */

#include "bursztyn_gui.h"

// Główne wywołanie systemowe (most między aplikacją a Jądrem)
uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    register uint64_t r8 asm("r8") = nr_funkcji;
    register uint64_t r9 asm("r9") = arg1;
    register uint64_t r10 asm("r10") = arg2;
    register uint64_t r12 asm("r12") = arg3;
    register uint64_t r13 asm("r13") = arg4;
    register uint64_t rax asm("rax");
    
    asm volatile (
        "syscall" 
        : "=a" (rax) 
        : "r" (r8), "r" (r9), "r" (r10), "r" (r12), "r" (r13) 
        : "rcx", "r11", "memory"
    );
    return rax;
}

// ==========================================
// 1. STANDARDOWE API SYSTEMOWE
// ==========================================

void wypisz(const char* t) { 
    bws_wywolaj(1, (uint64_t)t); 
}

bool utworz(const char* p) { 
    return bws_wywolaj(2, (uint64_t)p) != 0; 
}

bool zapisz_plik(const char* p, const char* d, uint32_t l) { 
    return bws_wywolaj(3, (uint64_t)p, (uint64_t)d, l) != 0; 
}

char pobierz_znak() { 
    return (char)bws_wywolaj(4); 
}

bool czytaj_plik(const char* p, char* b, uint32_t m) { 
    return bws_wywolaj(5, (uint64_t)p, (uint64_t)b, m) != 0; 
}

void bws_dzwiek_test(uint32_t czestotliwosc, uint32_t czas) {
    // Przekazujemy częstotliwość w arg1 (r9) i czas w arg2 (r10)
    bws_wywolaj(27, czestotliwosc, czas, 0, 0);
}

extern "C" {
    bool bws_siec_dns(const char* domena, uint8_t* wyjsciowy_ip) {
        return bws_wywolaj(28, (uint64_t)domena, (uint64_t)wyjsciowy_ip, 0, 0) != 0;
    }

    bool bws_siec_pobierz_http(uint8_t* cel_ip, const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc) {
        return bws_wywolaj(29, (uint64_t)cel_ip, (uint64_t)domena, (uint64_t)sciezka, ((uint64_t)bufor << 32) | max_dlugosc) != 0;
    }

    bool bws_siec_pobierz_https(uint8_t* cel_ip, const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc) {
        return bws_wywolaj(30, (uint64_t)cel_ip, (uint64_t)domena, (uint64_t)sciezka, ((uint64_t)bufor << 32) | max_dlugosc) != 0;
    }

    bool bws_tls_certyfikat_zaufany() { return bws_wywolaj(31) != 0; }
}


// ==========================================
// 2. ZAAWANSOWANE API GRAFICZNE (Ring 3 GUI)
// ==========================================

void gui_rysuj_okno(int x, int y, int w, int h, const char* tytul) { 
    // Pakowanie argumentów do zmiennych 64-bitowych (żeby ominąć limit 4 argumentów)
    bws_wywolaj(14, ((uint64_t)x << 32) | y, ((uint64_t)w << 32) | h, (uint64_t)tytul); 
}

void gui_wypisz_tekst(int x, int y, const char* t) { 
    bws_wywolaj(15, x, y, (uint64_t)t); 
}

void gui_wyczyscz_obszar(int x, int y, int w, int h) { 
    bws_wywolaj(16, x, y, w, h); 
}

void gui_odswiez() { 
    bws_wywolaj(17); 
}

void gui_pobierz_mysz(int* x, int* y, uint8_t* b) { 
    bws_wywolaj(18, (uint64_t)x, (uint64_t)y, (uint64_t)b); 
}

void gui_odswiez_pulpit() { 
    bws_wywolaj(19); 
}

void gui_wypisz_tekst_kolor(int x, int y, uint32_t kolor, const char* t) { 
    bws_wywolaj(20, x, y, kolor, (uint64_t)t); 
}

void gui_wypisz_tekst_kolor_skala(int x, int y, uint32_t kolor, int skala, const char* tekst) {
    uint64_t kolor_skala = ((uint64_t)skala << 32) | kolor;
    bws_wywolaj(20, x, y, kolor_skala, (uint64_t)tekst);
}


void gui_rysuj_prostokat(int x, int y, int w, int h, uint32_t kolor) { 
    bws_wywolaj(21, ((uint64_t)x << 32) | y, ((uint64_t)w << 32) | h, kolor); 
}

void gui_ustaw_przejecie_myszy(bool stan) { 
    bws_wywolaj(22, stan ? 1 : 0); 
}

void gui_pobierz_rozdzielczosc(int* w, int* h) { 
    bws_wywolaj(23, (uint64_t)w, (uint64_t)h); 
}

int gui_pobierz_szerokosc_znaku(uint32_t z) {
    return (int)bws_wywolaj(24, z);
}



// ==========================================
// 3. ELEMENTY INTERFEJSU (WIDGETY)
// ==========================================

void RysujPrzycisk(int x, int y, int w, int h, uint32_t kolor_bg, uint32_t kolor_txt, const char* t) {
    gui_rysuj_prostokat(x, y, w, h, kolor_bg);
    gui_wypisz_tekst_kolor(x + 4, y + 2, kolor_txt, t);
}

// Oblicza szerokość tekstu w pikselach z uwzględnieniem skali i polskich znaków (UTF-8)
int oblicz_szerokosc_tekstu(const char* t, int skala) {
    int w = 0; int i = 0;
    while (t[i] != '\0') {
        uint32_t z = (uint8_t)t[i];
        if ((t[i] & 0xE0) == 0xC0 && t[i+1] != '\0') {
            uint8_t b1 = (uint8_t)t[i]; uint8_t b2 = (uint8_t)t[i+1];
            z = ((b1 & 0x1F) << 6) | (b2 & 0x3F);
            i++;
        }
        w += (gui_pobierz_szerokosc_znaku(z) + 1) * skala;
        i++;
    }
    return w;
}

// Rysuje tekst idealnie na środku zadanego obszaru (przydatne do przycisków i ikon)
void rysuj_tekst_wysrodkowany(int px, int py, int w, int h, int skala, uint32_t kolor, const char* t) {
    int szer_tekstu = oblicz_szerokosc_tekstu(t, skala);
    int wys_tekstu = 16 * skala; // Wysokość czcionki to zawsze 16px
    int tx = px + (w / 2) - (szer_tekstu / 2);
    int ty = py + (h / 2) - (wys_tekstu / 2);
    gui_wypisz_tekst_kolor_skala(tx, ty, kolor, skala, t);
}
