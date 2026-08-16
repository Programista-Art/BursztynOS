#pragma once

/*
 * Bursztyn OS - publiczny interfejs podsystemu grafiki jadra.
 *
 * Naglowek odpowiada aktualnej implementacji grafika.cpp:
 *  - HAL ekranu 32 BPP,
 *  - backbuffer,
 *  - compositor warstw,
 *  - podstawowe prymitywy rysowania,
 *  - obsluga myszy i kursora,
 *  - funkcje BWS GUI wywolywane przez warstwe syscalli.
 */

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. INICJALIZACJA I LOGOWANIE
 * ========================================================================= */

void InicjalizujGrafike(uint64_t adres_info_multiboot);
void wypisz_log(const char* tekst);

/* =========================================================================
 * 2. INFORMACJE O EKRANIE I PREZENTACJA
 * ========================================================================= */

int grafika_pobierz_szerokosc();
int grafika_pobierz_wysokosc();

void PrzeniesNaEkran();

void PrzeniesFragmentNaEkran(int x,
                             int y,
                             int szer,
                             int wys);

/* =========================================================================
 * 3. COMPOSITOR / SKLADANIE WARSTW
 * ========================================================================= */

/*
 * Rozpoczyna skladanie nowej klatki.
 * Kursor zostaje ukryty, a zapis grafiki kierowany jest bezposrednio
 * do backbufferu.
 */
void grafika_rozpocznij_skladanie();

/*
 * Odtwarza tapete albo awaryjny kolor tla do backbufferu.
 */
void grafika_odtworz_tlo_skladania();

/*
 * Surowy zapis piksela compositora bez przekierowania do warstwy procesu.
 */
void grafika_zapisz_surowy_piksel(int x,
                                  int y,
                                  uint32_t kolor);

/*
 * Konczy skladanie, rysuje kursor i prezentuje gotowa klatke.
 */
void grafika_zakoncz_skladanie();

/* Nakladka jadra rysowana przez compositor. */
void rysuj_zegar_rtc();

/* =========================================================================
 * 4. PODSTAWOWE PRYMITYWY RYSOWANIA
 * ========================================================================= */

void PostawPiksel(int x,
                  int y,
                  uint32_t kolor);

uint32_t PobierzPiksel(int x,
                       int y);

void RysujProstokat(int x,
                    int y,
                    int szer,
                    int wys,
                    uint32_t kolor);

void RysujZnak(uint32_t unicode,
               int x,
               int y,
               uint32_t kolor_tekstu,
               uint32_t kolor_tla,
               bool przezroczyste_tlo,
               int skala);

void WypiszTekst(const char* tekst,
                 int x,
                 int y,
                 uint32_t kolor_tekstu,
                 int skala);

/*
 * Stara sciezka renderowania terminala jadra.
 * Aplikacje Ring 3 nie powinny jej wywolywac.
 */
void OdswiezEkran();

/* =========================================================================
 * 5. FUNKCJE WYWOLYWANE Z INNYCH MODULOW JADRA
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

void wypisz_na_ekranie(const char* tekst);

bool zaktualizuj_klawiature_gui(char znak);

void zaktualizuj_mysze(int dx,
                       int dy,
                       uint8_t przyciski);

void obsluga_przerwania_zegara();

void wczytaj_tapete_z_dysku();

bool gui_czy_zamknieto_powloke();

/* =========================================================================
 * 6. IMPLEMENTACJA BWS GUI PO STRONIE JADRA
 * ========================================================================= */

void bws_gui_rysuj_okno(int x,
                        int y,
                        int szer,
                        int wys,
                        const char* tytul);

void bws_gui_wypisz_tekst(int x,
                          int y,
                          const char* tekst);

void bws_gui_wyczyscz_obszar(int x,
                             int y,
                             int szer,
                             int wys);

void bws_gui_odswiez();

void bws_gui_pobierz_mysz(int* x,
                          int* y,
                          uint8_t* przyciski);

void bws_gui_odswiez_pulpit();

void bws_gui_wypisz_tekst_kolor(int x,
                                int y,
                                uint64_t kolor_skala,
                                const char* tekst);

void bws_gui_rysuj_prostokat(int x,
                             int y,
                             int szer,
                             int wys,
                             uint32_t kolor);

void bws_gui_ustaw_przejecie_myszy(bool stan);

void bws_gui_pobierz_rozdzielczosc(int* szer,
                                   int* wys);

int bws_gui_pobierz_szerokosc_znaku(uint32_t unicode);

#ifdef __cplusplus
} // extern "C"
#endif
