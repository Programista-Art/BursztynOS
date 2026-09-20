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
#include <stddef.h>

// Jedyny bufor tla skladacza i wspolny, bootowo mapowany scratch BMP/BTP.
bool grafika_przygotuj_bufory_tapety();
uint8_t* grafika_scratch_tapety(size_t* pojemnosc);
uint32_t* grafika_bufor_tapety(size_t* pojemnosc);
void grafika_publikuj_tapete();
bool grafika_czy_tapeta();
void grafika_tapeta_solid();

/* =========================================================================
 * 1. INICJALIZACJA I LOGOWANIE
 * ========================================================================= */

void InicjalizujGrafike(uint64_t adres_info_multiboot);
void wypisz_log(const char* tekst);
void klog_serial(const char* tekst);
void boot_log_gui(const char* tekst);

/* =========================================================================
 * 2. INFORMACJE O EKRANIE I PREZENTACJA
 * ========================================================================= */

int grafika_pobierz_szerokosc();
int grafika_pobierz_wysokosc();
uint32_t* grafika_pobierz_wiersz_backbuffer(int y);

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
void grafika_odtworz_tlo_regionu(int x, int y, int szer, int wys);

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
void grafika_zakoncz_skladanie_regionu(int x, int y, int szer, int wys);
void grafika_prezentuj_region(int x, int y, int szer, int wys);
void grafika_prezentuj_kursor();
void grafika_prezentuj_kursor_w(int x, int y);
void grafika_zakoncz_scene();
void grafika_naloz_kursor_regionu(int x, int y, int szer, int wys);
void grafika_pobierz_pozycje_kursora(int* x, int* y);
void grafika_pobierz_granice_kursora(int* max_x, int* max_y);
uint64_t grafika_generacja_ekranu();
void grafika_pobierz_overlay_kursora(int* x, int* y, bool* widoczny);

/* Nakladka jadra rysowana przez compositor. */
void rysuj_zegar_rtc();

/* Stan gotowości powłoki i paska zadań */
void grafika_ustaw_pasek_zadan_gotowy(bool gotowy);
bool grafika_czy_pasek_zadan_gotowy(void);

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

int WypiszTekst(const char* tekst,
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

void zaktualizuj_mysze_zrodlo(int dx,
                              int dy,
                              uint8_t przyciski,
                              const char* zrodlo);

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

bool bws_gui_blit(int x,
                  int y,
                  int szer,
                  int wys,
                  const uint32_t* piksele);

bool bws_gui_ustaw_tytul_okna(const char* tytul);

void bws_gui_ustaw_przejecie_myszy(bool stan);
void bws_gui_ustaw_capture(bool stan);
void bws_gui_usun_stan_procesu(int pid);
void bws_gui_aktywuj_warstwe(int pid);
bool bws_gui_minimalizuj_warstwe(int pid);
bool bws_gui_aktywuj_okno(uint64_t window_id);
int bws_gui_aktywny_pid();
void bws_gui_powiadom_lifecycle(uint32_t typ, int pid);

void bws_gui_pobierz_rozdzielczosc(int* szer,
                                   int* wys);

int bws_gui_pobierz_szerokosc_znaku(uint32_t unicode);

#ifdef __cplusplus
} // extern "C"
#endif

#include "ekran_format.h"
struct StanBuforowEkranu {
    TrybEkranu tryb;
    uint8_t* backbuffer;
    uint32_t* tapeta;
    size_t bajty, tapeta_bajty;
    bool ma_tapete;
};
const MozliwosciEkranu& grafika_mozliwosci_ekranu();
TrybEkranu grafika_tryb_ekranu();
uint32_t grafika_tryby_ekranu(TrybEkranu*,uint32_t);
bool grafika_tryb_obslugiwany(const TrybEkranu&);
bool grafika_przygotuj_ekran(const TrybEkranu&, StanBuforowEkranu*);
void grafika_pobierz_stan(StanBuforowEkranu*);
bool grafika_zmien_sprzet(const TrybEkranu&);
void grafika_wymien_stan(const StanBuforowEkranu&);
void grafika_zwolnij_stan(StanBuforowEkranu*);
struct VirtioGpuDriver;
bool grafika_aktywuj_virtio_gpu(VirtioGpuDriver* drv);
