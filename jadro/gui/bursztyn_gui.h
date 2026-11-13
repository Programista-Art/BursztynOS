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
#include "bws_api.h"
#include "bws_gui_batch.h"
#include "bws_nowoczesne.h"
#include "skladacz_obrazu.h"
#include "legacy_ui.h"
#include "motyw.h"
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

/* Jawny adapter dla aplikacji sprzed BursztynUI. Nie zmienia ABI BWS:
 * konwersja logiczne 100% <-> piksele fizyczne odbywa sie w Ring 3. */
void gui_legacy_wlacz();
bool gui_legacy_czy_wlaczony();
uint32_t gui_legacy_skala_procent();


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
WynikPlikowy usun_drzewo_uzytkownika(const char* sciezka);
// Replace a completed region, including transparent pixels (BWS57 mode 2).
bool gui_zastap_region(int x,int y,int szer,int wys,const uint32_t* piksele);
bool gui_zastap_regiony(const BwsGuiRegion* regions,uint32_t count,uint32_t display_generation=0);
// BWS42 optional exact executable filter; same bounded snapshot ABI.
uint32_t gui_pobierz_okna_programu(GuiOknoInfo* okna,uint32_t max,const char* path);
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
bool ustaw_schowek_wielu_plikow(const char (*sciezki)[BWS_DROP_SCIEZKA_MAX],uint32_t liczba,BwsOperacjaSchowka operacja);
bool pobierz_element_schowka_plikow(BwsSchowekPlikow* wynik,uint32_t indeks,uint64_t generacja);
bool usun_prefiks_schowka_plikow(uint64_t generacja,uint32_t liczba);
bool kopiuj_twor_uzytkownika(const char* sciezka,
                             const char* folder_docelowy);
bool czytaj_fragment_pliku(const char* sciezka, uint64_t offset,
                           char* bufor, uint32_t dlugosc,
                           uint32_t* odczytano);
/* BWS 60: monotoniczny czas HPET w nanosekundach (0, gdy HPET niedostepny). */
uint64_t gui_czas_monotoniczny_ns();
/* Popup aplikacji jest skladany nad zwyklymi oknami, ale pod menu systemu. */
bool gui_ustaw_popup_aplikacji(bool otwarty,
                               int x, int y, int szer, int wys);

/* BWS 61..83: addytywne fundamenty nowoczesnych aplikacji. */
bool gui_ustaw_geometrie_okna(int x, int y, int szer, int wys, int stan = 0);
void gui_ustaw_izolacje_geometrii(bool wlaczona);
bool gui_czy_izolacja_geometrii_wlaczona();
void gui_pobierz_widoczna_geometrie(int* x, int* y, int* w, int* h);
void gui_pobierz_izolowana_geometrie(int* x, int* y, int* w, int* h);
#if defined(BURSZTYNDIAGGEOMTRACE) && !defined(BURSZTYN_DIAG_GEOM_TRACE)
#define BURSZTYN_DIAG_GEOM_TRACE BURSZTYNDIAGGEOMTRACE
#endif

#ifndef BURSZTYN_DIAG_GEOM_TRACE
#define BURSZTYN_DIAG_GEOM_TRACE 0
#endif

void gui_log_geom_trace(const char* caller, int old_x, int old_y, int old_w, int old_h,
                        int req_x, int req_y, int req_w, int req_h,
                        bool isolation, bool reaches_kernel,
                        int vis_x, int vis_y, int vis_w, int vis_h);
uint32_t gui_liczba_monitorow();
bool gui_pobierz_monitor(uint32_t indeks, GuiMonitorInfo* info);
// Optional BWS62 extended snapshot; legacy 48-byte monitor ABI is unchanged.
inline bool gui_pobierz_geometrie_ekranu(DisplayGeometry* geometry) {
    return geometry && bws_wywolaj(BWS_GUI_MONITOR_INFO,0,reinterpret_cast<uint64_t>(geometry),sizeof(*geometry),0)==1;
}
inline bool gui_potwierdz_geometrie_pulpitu(const DisplayGeometry& g) {
    return bws_wywolaj(BWS_GUI_USTAW_WORK_AREA,
        (uint64_t(uint32_t(g.work_x))<<32)|uint32_t(g.work_y),
        (uint64_t(uint32_t(g.work_width))<<32)|uint32_t(g.work_height),g.generation,0)==1;
}
bool gui_ustaw_obszar_roboczy(int x, int y, int szer, int wys);

uint64_t shm_create(uint64_t rozmiar, uint32_t flagi);
void* shm_map(uint64_t handle, uint32_t flagi);
bool shm_unmap(void* adres);
bool shm_close(uint64_t handle);

uint64_t ipc_create();
bool ipc_send(uint64_t endpoint, const BwsIpcWyslij* wiadomosc);
bool ipc_receive(uint64_t endpoint, BwsIpcOdbierz* wiadomosc);
bool ipc_close(uint64_t endpoint);

uint64_t file_watch(const char* sciezka, uint32_t maska);
bool file_unwatch(uint64_t handle);

bool clipboard_format(uint32_t indeks, char* mime, uint32_t pojemnosc);
bool clipboard_size(const char* mime, uint64_t* rozmiar);
bool clipboard_read(const char* mime, void* dane, uint64_t pojemnosc,
                    uint64_t* odczytano);
bool clipboard_set(const char* mime, const void* dane, uint64_t rozmiar);

bool gui_ustaw_kursor(uint32_t typ);
bool gui_ustaw_kursor_custom(const GuiCustomCursorInfo* info);

uint64_t socket_create(uint32_t typ = BWS_SOCKET_TCP_IPV4);
bool socket_connect(uint64_t handle, const uint8_t ipv4[4], uint16_t port);
bool socket_send(uint64_t handle, const void* dane, uint32_t rozmiar,
                 BwsIoResult* wynik);
bool socket_recv(uint64_t handle, void* dane, uint32_t pojemnosc,
                 BwsIoResult* wynik);
bool socket_close(uint64_t handle);

/* Ring3 helper PSF; nie tworzy rejestru ani nowego ABI kernela. */
bool ustawienia_sciezka(const char* app_id, const char* klucz,
                        char* wynik, uint32_t pojemnosc);

/*
 * Trwale ustawienia per aplikacja.
 *
 * Kazdy klucz to osobny plik pod /uzytkownicy/domyslny/Ustawienia/<app>/<klucz>.
 * Plik jest maly (do 4096 B) i tekstowy, wiec mozna go podejrzec
 * w Eksploratorze i Notatniku.
 *
 * ustawienia_zapisz: tworzy katalog aplikacji, jesli go nie ma, i zapisuje
 *                    wartosc. Nadpisuje poprzednia.
 * ustawienia_wczytaj: zwraca false, gdy klucza nie ma. Bufor jest wtedy
 *                     wyzerowany - brak danych nie jest bledem fatalnym.
 * ustawienia_usun:   usuwa pojedynczy klucz.
 *
 * Wszystkie funkcje uzywaja PSF przez BWS (PRAWO_PLIKI_*), wiec podlegaja
 * tej samej kontroli PZB co reszta operacji plikowych aplikacji.
 */
bool ustawienia_zapisz(const char* app_id, const char* klucz,
                       const char* wartosc);
bool ustawienia_wczytaj(const char* app_id, const char* klucz,
                        char* wynik, uint32_t pojemnosc);
bool ustawienia_usun(const char* app_id, const char* klucz);

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
uint64_t gui_liczba_alokacji_sterty();
uint64_t gui_liczba_aktywnych_alokacji_sterty();

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

bool gui_blit(int x,
              int y,
              int szer,
              int wys,
              const uint32_t* piksele);
// Physical coordinates; source-over ARGB into the caller-owned layer (BWS57/3).
bool gui_blit_alpha(int x,int y,int width,int height,const uint32_t* pixels);

bool gui_blit_popup(int x,
                    int y,
                    int szer,
                    int wys,
                    const uint32_t* piksele);

/* BWS58: ustawia tytul warstwy biezacego procesu dla taskbara
   (GuiOknoInfo.tytul) i wysyla BWS_ZDARZENIE_OKNO_TYTUL. */
bool gui_ustaw_tytul_okna(const char* tytul);

void gui_ustaw_przejecie_myszy(bool stan);
void gui_ustaw_capture_myszy(bool stan);
bool gui_pobierz_zdarzenie(bws_zdarzenie* zdarzenie);
bool gui_czekaj_na_zdarzenie(bws_zdarzenie* zdarzenie);
bool gui_czekaj_na_zdarzenie_z_timeoutem(bws_zdarzenie* zdarzenie, uint64_t timeout_ticki);

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
bool gui_potwierdz_gotowosc_pulpitu();

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
 * 5.1 CENTRALNY PODSYSTEM MOTYWOW (Ring 3)
 * ========================================================================= */

const BursztynMotyw* gui_pobierz_motyw();
uint32_t motyw_kolor(uint32_t rola);
uint16_t motyw_promien(uint32_t element);
uint8_t motyw_grubosc_ramki();
uint8_t motyw_cien_okna();

inline uint32_t motyw_kolor_jasnosc(uint32_t c) {
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    return (299U * r + 587U * g + 114U * b) / 1000U;
}

inline uint32_t motyw_kolor_tekstu_na_tle(uint32_t tlo) {
    return (motyw_kolor_jasnosc(tlo) > 130) ? motyw_kolor(MOTYW_KOLOR_TEKST) : motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);
}

inline uint32_t motyw_kolor_tekstu_zaznaczenia() {
    uint32_t tlo_zaz = motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE);
    uint32_t y_tlo = motyw_kolor_jasnosc(tlo_zaz);
    uint32_t txt = motyw_kolor(MOTYW_KOLOR_TEKST);
    uint32_t txt_akcent = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);
    uint32_t y_txt = motyw_kolor_jasnosc(txt);
    uint32_t y_akcent = motyw_kolor_jasnosc(txt_akcent);
    uint32_t roznica_txt = y_tlo > y_txt ? y_tlo - y_txt : y_txt - y_tlo;
    uint32_t roznica_akcent = y_tlo > y_akcent ? y_tlo - y_akcent : y_akcent - y_tlo;
    return roznica_txt >= roznica_akcent ? txt : txt_akcent;
}

void motyw_odswiez_lokalny();
bool motyw_polecenie_ustaw(const char* id);
bool motyw_polecenie_podglad(const char* id);
bool motyw_polecenie_zatwierdz();
bool motyw_polecenie_przywroc();

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

// Binary-safe BWS29/30 mode; returns exact response bytes, zero on failure.
uint32_t bws_siec_pobierz_bajty(bool https, uint8_t* ip, const char* domain, const char* path, char* buffer, uint32_t capacity);
// BSF 2.2: jak wyzej, z naglowkiem "Origin" (zserializowany origin dokumentu,
// np. dla @font-face w trybie CORS). nullptr = bez naglowka.
uint32_t bws_siec_pobierz_bajty_origin(bool https, uint8_t* ip, const char* domain, const char* path, char* buffer, uint32_t capacity, const char* origin);

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
