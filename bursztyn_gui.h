#pragma once

/*
 * Bursztyn OS - publiczne API GUI dla aplikacji Ring 3.
 *
 * Ten naglowek odpowiada implementacji z bursztyn_gui.cpp i udostepnia:
 *  - Bursztynowe Wywolania Systemowe (BWS),
 *  - API plikow, dzwieku, sieci i procesow,
 *  - API graficzne i warstwy,
 *  - prywatna sterte aplikacji,
 *  - podstawowe widgety GUI.
 */

#include <stdint.h>
#include "bws_zdarzenia.h"
#include "bws_pliki.h"
#include "skladacz_obrazu.h"
#include <stdbool.h>


struct GuiFragmentIkony {
    uint8_t x;
    uint8_t y;
    uint8_t szer;
    uint8_t wys;
    uint8_t kolor;
};

struct GuiIkona {
    uint8_t szer;
    uint8_t wys;

    const uint32_t* paleta;
    uint16_t liczba_kolorow;

    const GuiFragmentIkony* fragmenty;
    uint16_t liczba_fragmentow;
};


/* =========================================================================
 * 1. GLOWNE WYWOLANIE SYSTEMOWE BWS
 * ========================================================================= */

uint64_t bws_wywolaj(uint64_t nr_funkcji,
                     uint64_t arg1 = 0,
                     uint64_t arg2 = 0,
                     uint64_t arg3 = 0,
                     uint64_t arg4 = 0);

/* =========================================================================
 * 2. STANDARDOWE API SYSTEMOWE
 * ========================================================================= */

void wypisz(const char* tekst);

bool utworz(const char* sciezka);

/* BWS 46: tworzy katalog przez istniejace API PSF i kontrole PZB. */
bool utworz_katalog_uzytkownika(const char* sciezka);

bool zapisz_plik(const char* sciezka,
                 const char* dane,
                 uint32_t dlugosc);

bool czytaj_plik(const char* sciezka,
                 char* bufor,
                 uint32_t maksymalna_dlugosc);

/* BWS 6: tekstowa lista wpisow "[KAT]  nazwa" / "[PLIK] nazwa". */
bool wylistuj_katalog_uzytkownika(const char* sciezka,
                                  char* bufor,
                                  uint32_t maksymalna_dlugosc);

/* BWS 10: uruchomienie .bur przez loader i kontrole PZB. */
bool uruchom_program_uzytkownika(const char* sciezka);

/* Rozszerzenie BWS 10: uruchamia program z ograniczonym argumentem sciezki. */
bool uruchom_program_z_argumentem_uzytkownika(const char* program,
                                               const char* argument);

/* BWS 45: argument startowy biezacego procesu. */
bool pobierz_argument_startowy(char* bufor, uint32_t pojemnosc);

enum wynik_otwarcia_skojarzonego : uint32_t {
    OTWORZ_PLIK_BRAK_SKOJARZENIA = 0,
    OTWORZ_PLIK_URUCHOMIONO = 1,
    OTWORZ_PLIK_BLAD = 2
};

/* Jedno centralne miejsce skojarzen typow plikow dla aplikacji GUI. */
wynik_otwarcia_skojarzonego otworz_plik_skojarzony(const char* sciezka);

/* Addytywne ABI: zwraca rozmiar bez kopiowania danych. */
bool pobierz_rozmiar_pliku(const char* sciezka, uint32_t* rozmiar);

/* BWS 7/8 oraz addytywne BWS 47..56. */
bool usun_twor_uzytkownika(const char* sciezka);
bool zmien_nazwe_uzytkownika(const char* sciezka, const char* nowa_nazwa);
bool pobierz_metadane_pliku(const char* sciezka, BwsMetadanePliku* metadane);
bool przenies_twor_uzytkownika(const char* sciezka,
                               const char* folder_docelowy);
bool gui_rejestruj_cele_drop(const BwsCelDrop* cele, uint32_t liczba);
BwsWynikDrop gui_aktualizuj_drag(const char* sciezka,
                                 int x, int y, bool wykonaj_drop);
bool ustaw_schowek_plikow(const char* sciezka, BwsOperacjaSchowka operacja);
bool pobierz_schowek_plikow(BwsSchowekPlikow* schowek);
bool wyczysc_schowek_plikow(uint64_t oczekiwana_generacja);
bool kopiuj_twor_uzytkownika(const char* sciezka,
                             const char* folder_docelowy);
/* Popup aplikacji jest skladany nad zwyklymi oknami, ale pod menu systemu. */
bool gui_ustaw_popup_aplikacji(bool otwarty,
                               int x, int y, int szer, int wys);

/* =========================================================================
 * 2b. WSPOLNY DIALOG MODALNY DLA APLIKACJI GUI
 * ========================================================================= */

/*
 * Modal jest nieprzezroczystym popupem wlasciciela: kompozytor sklada
 * surface procesu jeszcze raz nad wszystkimi oknami (pod system overlayem)
 * wewnatrz zarejestrowanego prostokata. Zadnych pseudo-przezroczystosci:
 * dialog ma pelne, inicjalizowane tlo i ramke. Otwarcie/zamkniecie korzysta
 * z dirty rect kompozytora - po zamknieciu tlo odtwarza scena, bez recznego
 * kopiowania framebuffera.
 */

constexpr uint32_t GUI_MODAL_TLO = 0x00391B02U;
constexpr uint32_t GUI_MODAL_RAMKA = 0x00E58A00U;
constexpr uint32_t GUI_MODAL_RAMKA_CIEMNA = 0x00794400U;
constexpr uint32_t GUI_MODAL_TEKST = 0x00FFFFFFU;
constexpr uint32_t GUI_MODAL_TEKST_DRUGI = 0x00D1D5DBU;
constexpr uint32_t GUI_MODAL_POLE_TLO = 0x00280F00U;
constexpr uint32_t GUI_MODAL_POLE_AKTYWNE = 0x00603800U;
constexpr uint32_t GUI_MODAL_PRZYCISK_OK = 0x00E58A00U;

enum gui_modal_wynik : uint32_t {
    GUI_MODAL_BRAK = 0,
    GUI_MODAL_OK = 1,
    GUI_MODAL_ANULUJ = 2
};

struct GuiModalProstokat {
    int x;
    int y;
    int szer;
    int wys;
};

struct GuiModalDialog {
    bool otwarty;
    bool potwierdzenie;      /* true = pytanie bez pola edycji */
    char tytul[48];
    char tekst[64];          /* pole edycji w trybie wprowadzania */
    char pytanie[128];       /* tresc w trybie potwierdzenia */
    char etykieta_ok[12];
    uint32_t kolor_ok;
    uint32_t limit_tekstu;   /* maksymalna dlugosc pola w bajtach */
    uint32_t dlugosc;
    uint32_t karetka;        /* offset w bajtach */
    uint32_t widoczne_od;    /* poczatek widocznego okna pola */
    GuiModalProstokat obszar;
    GuiModalProstokat ramka;
    GuiModalProstokat pole;
    GuiModalProstokat przycisk_ok;
    GuiModalProstokat przycisk_anuluj;
};

/* Obszar to prostokat popupu w wspolrzednych ekranu (dla okna: klient).
   limit_tekstu 0 oznacza pelna pojemnosc pola (63 bajty). */
bool gui_modal_otworz(GuiModalDialog* dialog,
                      const char* tytul,
                      const char* tekst_poczatkowy,   /* NULL = potwierdzenie */
                      const char* pytanie,
                      const GuiModalProstokat* obszar,
                      uint32_t limit_tekstu = 0);

/* Etykieta/kolor OK przed gui_modal_otworz (np. czerwone "Usun"). */
void gui_modal_ustaw_przycisk_ok(GuiModalDialog* dialog,
                                 const char* etykieta,
                                 uint32_t kolor);

void gui_modal_zamknij(GuiModalDialog* dialog);
bool gui_modal_otwarty(const GuiModalDialog* dialog);
const char* gui_modal_tekst(const GuiModalDialog* dialog);
const GuiModalProstokat* gui_modal_pole(const GuiModalDialog* dialog);
const GuiModalProstokat* gui_modal_ramka(const GuiModalDialog* dialog);

gui_modal_wynik gui_modal_klik(GuiModalDialog* dialog, int mysz_x, int mysz_y);
bool gui_modal_pole_trafione(const GuiModalDialog* dialog,
                             int mysz_x, int mysz_y);
void gui_modal_karetka_za_mysza(GuiModalDialog* dialog, int mysz_x);

/* Zwraca OK/ANULUJ dla Enter/Escape, inaczej BRAK (pole zaktualizowane). */
gui_modal_wynik gui_modal_klawisz(GuiModalDialog* dialog, char znak);
bool gui_modal_dopisz(GuiModalDialog* dialog, char znak);
bool gui_modal_usun_wstecz(GuiModalDialog* dialog);
bool gui_modal_usun_przed(GuiModalDialog* dialog);      /* Delete */
bool gui_modal_przesun_karetke(GuiModalDialog* dialog, int kierunek);

void gui_modal_rysuj(GuiModalDialog* dialog);

char pobierz_znak();

/* =========================================================================
 * 3. PRYWATNA STERTA PROCESU RING 3
 * ========================================================================= */

void* gui_malloc(unsigned long rozmiar);
void gui_free(void* ptr);

void* operator new(unsigned long rozmiar);
void* operator new[](unsigned long rozmiar);

void operator delete(void* p) noexcept;
void operator delete[](void* p) noexcept;

void operator delete(void* p,
                     unsigned long rozmiar) noexcept;

void operator delete[](void* p,
                       unsigned long rozmiar) noexcept;

/* =========================================================================
 * 4. API GRAFICZNE RING 3
 * ========================================================================= */

void gui_rysuj_okno(int x,
                    int y,
                    int w,
                    int h,
                    const char* tytul);

void gui_wypisz_tekst(int x,
                      int y,
                      const char* tekst);

void gui_wyczyscz_obszar(int x,
                         int y,
                         int w,
                         int h);

void gui_odswiez();

void gui_pobierz_mysz(int* x,
                      int* y,
                      uint8_t* przyciski);

void gui_odswiez_pulpit();

void gui_wypisz_tekst_kolor(int x,
                            int y,
                            uint32_t kolor,
                            const char* tekst);

void gui_wypisz_tekst_kolor_skala(int x,
                                  int y,
                                  uint32_t kolor,
                                  int skala,
                                  const char* tekst);

void gui_rysuj_prostokat(int x,
                         int y,
                         int szer,
                         int wys,
                         uint32_t kolor);

void gui_ustaw_przejecie_myszy(bool stan);
void gui_ustaw_capture_myszy(bool stan);
bool gui_pobierz_zdarzenie(bws_zdarzenie* zdarzenie);
bool gui_czekaj_na_zdarzenie(bws_zdarzenie* zdarzenie);

void gui_pobierz_rozdzielczosc(int* w,
                               int* h);

int gui_pobierz_szerokosc_znaku(uint32_t znak);
int gui_pobierz_wysokosc_fontu();

/*
 * Tworzy jedna warstwe nalezaca do biezacego procesu.
 * Zwraca identyfikator warstwy albo -1 przy bledzie.
 */
int bws_utworz_warstwe(int x,
                       int y,
                       int szer,
                       int wys,
                       int z_order);

/*
 * Przesuwa warstwe nalezaca do biezacego procesu.
 */
void bws_przesun_warstwe(int nowy_x,
                         int nowy_y);

void gui_ustaw_system_overlay(bool otwarty,
                              int x, int y, int szer, int wys);
bool gui_minimalizuj_okno();
uint32_t gui_pobierz_okna(GuiOknoInfo* okna, uint32_t max);
bool gui_aktywuj_okno(uint64_t window_id);

/* =========================================================================
 * 5. WIDGETY I FUNKCJE POMOCNICZE
 * ========================================================================= */

int oblicz_szerokosc_tekstu(const char* tekst,
                            int skala);

void rysuj_tekst_wysrodkowany(int px,
                              int py,
                              int w,
                              int h,
                              int skala,
                              uint32_t kolor,
                              const char* tekst);

void RysujPrzycisk(int x,
                   int y,
                   int w,
                   int h,
                   uint32_t kolor_bg,
                   uint32_t kolor_txt,
                   const char* tekst);

enum gui_akcja_belki : uint32_t {
    GUI_BELKA_BRAK = 0,
    GUI_BELKA_DRAG,
    GUI_BELKA_MINIMALIZUJ,
    GUI_BELKA_MAKSYMALIZUJ,
    GUI_BELKA_ZAMKNIJ
};

void gui_rysuj_standardowa_belke(int x, int y, int szer,
                                 const char* tytul, bool zmaksymalizowane);
gui_akcja_belki gui_hit_test_belki(int mx, int my,
                                   int x, int y, int szer);

/* =========================================================================
 * 6. DZWIEK
 * ========================================================================= */

void bws_dzwiek_test(uint32_t czestotliwosc,
                     uint32_t czas);

/* =========================================================================
 * 7. SIEC I ZARZADZANIE PROCESEM
 * ========================================================================= */

extern "C" {

bool bws_siec_dns(const char* domena,
                  uint8_t* wyjsciowy_ip);

bool bws_siec_pobierz_http(uint8_t* cel_ip,
                           const char* domena,
                           const char* sciezka,
                           char* bufor,
                           uint32_t max_dlugosc);

bool bws_siec_pobierz_https(uint8_t* cel_ip,
                            const char* domena,
                            const char* sciezka,
                            char* bufor,
                            uint32_t max_dlugosc);

bool bws_tls_certyfikat_zaufany();

__attribute__((noreturn))
void bws_zakoncz_proces();

__attribute__((noreturn))
void gui_zakoncz_aplikacje();

} 


