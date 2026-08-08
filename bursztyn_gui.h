#pragma once
#include <stdint.h>
#include <stdbool.h>

// 1. Główne wywołanie systemowe do Jądra
uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0);

// 2. Standardowe API Systemowe
void wypisz(const char* t);
bool utworz(const char* p);
bool zapisz_plik(const char* p, const char* d, uint32_t l);
bool czytaj_plik(const char* p, char* b, uint32_t m);
char pobierz_znak();

// 3. Zaawansowane API Graficzne (Ring 3 GUI)
void gui_rysuj_okno(int x, int y, int w, int h, const char* tytul);
void gui_wypisz_tekst(int x, int y, const char* t);
void gui_wyczyscz_obszar(int x, int y, int w, int h);
void gui_odswiez();
void gui_pobierz_mysz(int* x, int* y, uint8_t* b);
void gui_odswiez_pulpit();
void gui_wypisz_tekst_kolor(int x, int y, uint32_t kolor, const char* tekst);
void gui_wypisz_tekst_kolor_skala(int x, int y, uint32_t kolor, int skala, const char* tekst); // <--- Musi tu być!
void gui_rysuj_prostokat(int x, int y, int szer, int wys, uint32_t kolor);
void gui_ustaw_przejecie_myszy(bool stan);
void gui_pobierz_rozdzielczosc(int* w, int* h);
int gui_pobierz_szerokosc_znaku(uint32_t z); 
// Nowe funkcje pomocnicze do wyśrodkowanego tekstu
int oblicz_szerokosc_tekstu(const char* t, int skala);
void rysuj_tekst_wysrodkowany(int px, int py, int w, int h, int skala, uint32_t kolor, const char* t);

// 4. Elementy Interfejsu (Widgets)
void RysujPrzycisk(int x, int y, int w, int h, uint32_t kolor_bg, uint32_t kolor_txt, const char* t);