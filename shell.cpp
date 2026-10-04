/*
 * Bursztyn OS - Powłoka Bursztynowa (bsh)
 *
 * Ring 3 / aplikacja .bur
 *
 * Funkcje:
 *  - podstawowe polecenia systemowe,
 *  - uruchamianie programow,
 *  - operacje BSP2,
 *  - DNS / ICMP / prosty HTTP,
 *  - historia polecen,
 *  - test RTC, PCI i HDA.
 *
 * Bezpieczenstwo:
 *  - brak niezabezpieczonych kopii stringow,
 *  - brak cichego obcinania sciezek/argumentow,
 *  - scisla walidacja IPv4,
 *  - odczyty plikow sa zawsze terminowane NUL-em przed wypisaniem,
 *  - parser komend obsluguje spacje i cudzyslowy,
 *  - nie korzystamy ze starych numerow BWS DNS/HTTP 12/13,
 *  - HTTP jest ograniczony przez aktualne ABI BWS do bufora w dolnych 4 GiB.
 *
 * UWAGA:
 * Naglowek BUR zachowuje obecny layout zgodny z notatnik_linker.ld:
 *
 *   text: offset 0x1000,  rozmiar 0x8000,  VA 0x1001000
 *   data: offset 0x9000,  rozmiar 0x20000, VA 0x1009000
 *
 * Zmiana tych wartosci wymaga jednoczesnej zmiany linkera aplikacji.
 */

#include "bursztyn_gui.h"
#include "bws_nowoczesne.h"
#include "bws_zdarzenia.h"
#include "motyw.h"
#include "bursztyn_ui/czcionka.h"
#include "bursztyn_ui/rysowanie.h"
#include "bursztyn_ui/pasek_przewijania.h"
#include "bursztyn_ui/menu_kontekstowe.h"
#include "wersja_systemu.h"
using namespace BursztynUI;


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef BURSZTYN_DEBUG_GUI_PERF
#define BURSZTYN_DEBUG_GUI_PERF 0
#endif

/* Shell ma wlasny terminal w prywatnej warstwie. Wszystkie dotychczasowe
 * wywolania wypisz() w tym pliku sa kierowane do tej powierzchni, a nie do
 * legacy term_buf jadra. */
#define wypisz shell_wypisz

/* =========================================================================
 * 1. NAGLOWEK BUR
 * ========================================================================= */

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

static_assert(
    sizeof(NaglowekBur) == 60,
    "Nagłówek BUR musi miec 60 bajtow"
);

static_assert(
    offsetof(NaglowekBur, punkt_wejscia) == 0x04,
    "Nieprawidlowy layout BUR"
);

static_assert(
    offsetof(NaglowekBur, tekst_przesuniecie) == 0x0C,
    "Nieprawidlowy layout BUR"
);

static_assert(
    offsetof(NaglowekBur, tekst_rozmiar) == 0x14,
    "Nieprawidlowy layout BUR"
);

static_assert(
    offsetof(NaglowekBur, tekst_wirtualny) == 0x1C,
    "Nieprawidlowy layout BUR"
);

static_assert(
    offsetof(NaglowekBur, dane_przesuniecie) == 0x24,
    "Nieprawidlowy layout BUR"
);

static_assert(
    offsetof(NaglowekBur, dane_rozmiar) == 0x2C,
    "Nieprawidlowy layout BUR"
);

static_assert(
    offsetof(NaglowekBur, dane_wirtualny) == 0x34,
    "Nieprawidlowy layout BUR"
);

namespace {

constexpr int SHELL_X_START = 20;
constexpr int SHELL_Y_START = 20;
constexpr int SHELL_W = 660;
constexpr int SHELL_H = 360;
constexpr int SHELL_TITLE_H = 28;
constexpr int SHELL_TEXT_X = 8;
constexpr int SHELL_TEXT_Y = 34;
constexpr int SHELL_LINE_H = 16;
constexpr int SHELL_COLS = 70;
constexpr int SHELL_ROWS = 19;
constexpr size_t VISUAL_COLUMNS = static_cast<size_t>(SHELL_COLS);

constexpr int MAXSCROLLBACKLINES = 512;

struct ShellLine {
    char tekst[4096];
    char opis[160];
    int len;
    int tekst_x; // logiczne wcięcie wiersza pomocy, niezależne od tekstu
    int opis_x; // 0 = jedna linia, >0 = logiczny offset kolumny opisu (100%)
};

ShellLine* g_linie = nullptr;
int g_liczba_linii = 1;
int g_pierwsza_linia = 0;
int g_scroll_linia = 0;
bool g_auto_scroll = true;

char* g_obecny_bufor_wejscia = nullptr;
PasekPrzewijania* g_pasek_przewijania = nullptr;

constexpr int SHELL_LOGICAL_W = 700;
constexpr int SHELL_LOGICAL_H = 450;
int shell_logical_w = SHELL_LOGICAL_W;
int shell_logical_h = SHELL_LOGICAL_H;

int shell_x = 20, shell_y = 20, shell_w = 700, shell_h = 450;
int shell_work_x = 0, shell_work_y = 0, shell_work_w = 1024, shell_work_h = 768;
int shell_screen_w = 1024, shell_screen_h = 768;
bool shell_maximized = false;
bool shell_minimized = false;
int shell_restore_x = 20, shell_restore_y = 20, shell_restore_w = 700, shell_restore_h = 450;

bool shell_drag = false;
int shell_drag_mouse_x = 0;
int shell_drag_mouse_y = 0;
int shell_drag_window_x = 0;
int shell_drag_window_y = 0;
bool shell_sb_dragging = false;

static constexpr int WYSOKOSC_BELKI_TYTULOWEJ = 28;

inline Prostokat shell_obszar_klienta() {
    const SkalaUI skala = ui_skala();
    const int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala;
    int klient_h = shell_h - belka_h;
    if (klient_h < 0) klient_h = 0;
    return Prostokat(shell_x, shell_y + belka_h, shell_w, klient_h);
}

#include "shell_terminal.h"



constexpr uint64_t BUR_TEXT_OFFSET = 0x1000ULL;
constexpr uint64_t BUR_TEXT_SIZE   = 0x20000ULL;
constexpr uint64_t BUR_TEXT_VADDR  = 0x1001000ULL;

constexpr uint64_t BUR_DATA_OFFSET = 0x21000ULL;
constexpr uint64_t BUR_DATA_SIZE   = 0x40000ULL;
constexpr uint64_t BUR_DATA_VADDR  = 0x1021000ULL;

constexpr size_t SHELL_INPUT_CAPACITY = 2048;
#include "shell_context.h"
#include "shell_sponsors.h"
constexpr size_t SHELL_HISTORY_ENTRY_CAPACITY = 2048;
constexpr size_t MAX_SCIEZKA = 512;
constexpr size_t MAX_NAZWA_HOSTA = 254;
constexpr size_t MAX_SCIEZKA_HTTP = 2048;

constexpr size_t HISTORIA_MAX = 5;

constexpr uint32_t BUF_CZYTAJ_PLIK = 4096;
constexpr uint32_t BUF_LISTA = 1024;
constexpr uint32_t BUF_PCI = 2048;

/*
 * Bufor jest STATYCZNY celowo.
 *
 * Aktualny wrapper bws_siec_pobierz_http() pakuje wskaznik do gornych
 * 32 bitow jednego argumentu ABI. Dziala wiec tylko, jezeli wskaznik bufora
 * miesci sie w 32 bitach. Statyczne .data/.bss obecnej aplikacji znajduje
 * sie przy 0x1009000, natomiast stos Ring 3 lezy wysoko i nie nadaje sie
 * do tego ABI.
 *
 * Po poprawieniu ABI BWS 29 ten warunek mozna usunac.
 */
constexpr uint32_t HTTP_BUF_SIZE = 32U * 1024U;

alignas(16)
char bufor_http[HTTP_BUF_SIZE] = {};

char historia[HISTORIA_MAX][SHELL_HISTORY_ENTRY_CAPACITY] = {};
char komenda_dla_historii[SHELL_HISTORY_ENTRY_CAPACITY] = {};
size_t historia_ilosc = 0;

/* =========================================================================
 * 2. PROSTE WRAPPERY BWS NIEOBECNE W bursztyn_gui.h
 * ========================================================================= */

bool bsh_wylistuj_katalog(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    if (!sciezka ||
        !bufor ||
        max_dlugosc == 0) {
        return false;
    }

    return
        bws_wywolaj(
            6,
            reinterpret_cast<uint64_t>(sciezka),
            reinterpret_cast<uint64_t>(bufor),
            static_cast<uint64_t>(max_dlugosc),
            0
        ) != 0;
}

bool bsh_usun_twor(
    const char* sciezka
) {
    if (!sciezka) {
        return false;
    }

    return
        bws_wywolaj(
            7,
            reinterpret_cast<uint64_t>(sciezka),
            0, 0, 0
        ) != 0;
}

bool bsh_zmien_nazwe(
    const char* sciezka,
    const char* nowa_nazwa
) {
    if (!sciezka ||
        !nowa_nazwa) {
        return false;
    }

    return
        bws_wywolaj(
            8,
            reinterpret_cast<uint64_t>(sciezka),
            reinterpret_cast<uint64_t>(nowa_nazwa),
            0, 0
        ) != 0;
}

bool bsh_pobierz_czas(
    char* bufor
) {
    if (!bufor) {
        return false;
    }

    return
        bws_wywolaj(
            9,
            reinterpret_cast<uint64_t>(bufor),
            0, 0, 0
        ) != 0;
}

bool bsh_uruchom_program(
    const char* sciezka
) {
    if (!sciezka) {
        return false;
    }

    return
        bws_wywolaj(
            10,
            reinterpret_cast<uint64_t>(sciezka),
            0, 0, 0
        ) != 0;
}

bool bsh_ping_ipv4(
    const uint8_t ip[4]
) {
    if (!ip) {
        return false;
    }

    return
        bws_wywolaj(
            11,
            ip[0],
            ip[1],
            ip[2],
            ip[3]
        ) != 0;
}

/* =========================================================================
 * 3. FUNKCJE STRING / PAMIEC BEZ LIBC
 * ========================================================================= */

size_t dlugosc_tekstu_limit(
    const char* tekst,
    size_t limit
) {
    if (!tekst) {
        return 0;
    }

    size_t i = 0;

    while (i < limit &&
           tekst[i] != '\0') {
        ++i;
    }

    return i;
}

bool tekst_zakonczony_w_limicie(
    const char* tekst,
    size_t limit,
    size_t* dlugosc_wyj = nullptr
) {
    if (!tekst ||
        limit == 0) {
        return false;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            if (dlugosc_wyj) {
                *dlugosc_wyj = i;
            }

            return true;
        }
    }

    return false;
}

void wyzeruj(
    void* ptr,
    size_t rozmiar
) {
    if (!ptr) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(ptr);

    for (size_t i = 0;
         i < rozmiar;
         ++i) {
        p[i] = 0;
    }
}

bool kopiuj_tekst(
    char* cel,
    size_t pojemnosc,
    const char* zrodlo
) {
    if (!cel ||
        pojemnosc == 0 ||
        !zrodlo) {
        return false;
    }

    size_t dlugosc = 0;

    if (!tekst_zakonczony_w_limicie(
            zrodlo,
            pojemnosc,
            &dlugosc)) {
        cel[0] = '\0';
        return false;
    }

    for (size_t i = 0;
         i <= dlugosc;
         ++i) {
        cel[i] = zrodlo[i];
    }

    return true;
}

bool tekst_rowny(
    const char* a,
    const char* b
) {
    if (!a ||
        !b) {
        return false;
    }

    while (*a != '\0' &&
           *b != '\0') {

        if (*a != *b) {
            return false;
        }

        ++a;
        ++b;
    }

    return
        *a == '\0' &&
        *b == '\0';
}

bool jest_biala_spacja(
    char c
) {
    return
        c == ' ' ||
        c == '\t';
}

void uint_do_str(
    uint64_t wartosc,
    char* bufor,
    size_t pojemnosc
) {
    if (!bufor ||
        pojemnosc == 0) {
        return;
    }

    if (wartosc == 0) {
        if (pojemnosc >= 2) {
            bufor[0] = '0';
            bufor[1] = '\0';
        } else {
            bufor[0] = '\0';
        }

        return;
    }

    char odwrotnie[32] = {};
    size_t n = 0;

    while (wartosc != 0 &&
           n < sizeof(odwrotnie)) {

        odwrotnie[n++] =
            static_cast<char>(
                '0' +
                (wartosc % 10ULL)
            );

        wartosc /= 10ULL;
    }

    if (n + 1 > pojemnosc) {
        bufor[0] = '\0';
        return;
    }

    size_t out = 0;

    while (n > 0) {
        bufor[out++] =
            odwrotnie[--n];
    }

    bufor[out] = '\0';
}

/* =========================================================================
 * 4. WEJSCIE TERMINALA
 * ========================================================================= */

static inline int utf8_sekwencja_dlugosc(uint8_t b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

enum ShellKey { SHELL_LEFT=256, SHELL_RIGHT, SHELL_UP, SHELL_DOWN, SHELL_DELETE };

int pobierz_znak_blokujaco() {
    static uint32_t key_modifiers = 0;
    for (;;) {
        bws_zdarzenie e{};
        if (!gui_czekaj_na_zdarzenie(&e)) continue;
        // Handle before popup dispatch: an open menu consumes other events.
        // GUI has refreshed its local theme; refresh the UI styles as Okno does.
        if (e.typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) {
            Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
            if (g_pasek_przewijania) g_pasek_przewijania->zaktualizuj_motyw();
            shell_rysuj_terminal();
            if (g_context_popups) g_context_popups->zaktualizuj_motyw();
            continue;
        }
        if (e.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY) key_modifiers = e.przyciski;
        if (shell_context_event(e)) continue;

        if (e.typ == BWS_ZDARZENIE_EKRAN) {
            if (ui_skala_poprawna(e.kod)) {
                ui_skala_procent = e.kod;
            }
            gui_pobierz_rozdzielczosc(&shell_screen_w, &shell_screen_h);
            GuiMonitorInfo monitor{};
            if (gui_pobierz_monitor(0, &monitor)) {
                shell_work_x = monitor.work_x; shell_work_y = monitor.work_y;
                shell_work_w = monitor.work_szer; shell_work_h = monitor.work_wys;
            } else {
                shell_work_w = shell_screen_w;
                shell_work_h = shell_screen_h;
            }
            if (shell_maximized) {
                shell_x = shell_work_x; shell_y = shell_work_y;
                shell_w = shell_work_w; shell_h = shell_work_h;
            } else {
                shell_w = ui_skaluj(shell_logical_w, ui_skala_procent);
                shell_h = ui_skaluj(shell_logical_h, ui_skala_procent);
                if (shell_x + shell_w > shell_work_x + shell_work_w) {
                    shell_x = shell_work_x + shell_work_w - shell_w;
                }
                if (shell_y + shell_h > shell_work_y + shell_work_h) {
                    shell_y = shell_work_y + shell_work_h - shell_h;
                }
                if (shell_x < shell_work_x) shell_x = shell_work_x;
                if (shell_y < shell_work_y) shell_y = shell_work_y;
            }
            gui_ustaw_geometrie_okna(shell_x, shell_y, shell_w, shell_h);
            shell_rysuj_terminal();
            continue;
        }

        if (e.typ == BWS_ZDARZENIE_FOCUS && shell_minimized) {
            shell_minimized = false;
            shell_rysuj_terminal();
            continue;
        }
        if (e.typ == BWS_ZDARZENIE_ZAMKNIJ) gui_zakoncz_aplikacje();

        const Prostokat client = shell_obszar_klienta();
        const int client_x = client.x;
        const int client_y = client.y;
        const int max_linii = shell_page_rows();
        const int max_scroll = shell_max_scroll();

        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
            gui_akcja_belki akcja = gui_hit_test_belki(e.x, e.y, shell_x, shell_y, shell_w);
            if (akcja == GUI_BELKA_ZAMKNIJ) gui_zakoncz_aplikacje();
            if (akcja == GUI_BELKA_MINIMALIZUJ) {
                shell_minimized = gui_minimalizuj_okno();
                shell_drag = false;
                gui_ustaw_capture_myszy(false);
                continue;
            }
            if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                if (!shell_maximized) {
                    shell_restore_x = shell_x; shell_restore_y = shell_y;
                    shell_restore_w = shell_w; shell_restore_h = shell_h;
                    shell_x = shell_work_x; shell_y = shell_work_y;
                    shell_w = shell_work_w; shell_h = shell_work_h;
                    shell_maximized = true;
                } else {
                    shell_x = shell_restore_x; shell_y = shell_restore_y;
                    shell_w = shell_restore_w; shell_h = shell_restore_h;
                    shell_maximized = false;
                }
                if (!gui_ustaw_geometrie_okna(shell_x, shell_y, shell_w, shell_h)) gui_zakoncz_aplikacje();
                shell_rysuj_terminal();
                continue;
            }
            if (akcja == GUI_BELKA_DRAG && !shell_maximized) {
                shell_drag = true;
                shell_drag_mouse_x = e.x;
                shell_drag_mouse_y = e.y;
                shell_drag_window_x = shell_x;
                shell_drag_window_y = shell_y;
                gui_ustaw_capture_myszy(true);
                continue;
            }

            // Obsługa kliknięcia w pasek przewijania
            if (g_pasek_przewijania && max_scroll > 0) {
                int lx = e.x - client_x;
                int ly = e.y - client_y;
                ZdarzenieMyszki mz{};
                mz.typ = TypZdarzenia::WcisniecieMyszy;
                mz.x = lx;
                mz.y = ly;
                mz.przycisk = 1;
                g_pasek_przewijania->obsluz_wcisniecie_myszy(mz);
                if (mz.obsluzone || g_pasek_przewijania->czy_przeciagany()) {
                    shell_sb_dragging = true;
                    gui_ustaw_capture_myszy(true);
                    shell_set_scroll(static_cast<int>(g_pasek_przewijania->wartosc()));
                    shell_rysuj_terminal();
                    continue;
                }
            }
            if (shell_input_rect().zawiera(Punkt(e.x,e.y)) && g_obecny_bufor_wejscia) {
                bool had_output=g_output_selection;g_output_selection=false;
                g_select_input=true;g_select_drag=true;
                g_caret=g_input_anchor=shell_hit_input(e.x);
                gui_ustaw_capture_myszy(true);
                if(had_output) shell_rysuj_terminal(); else shell_odswiez_linie_wejsciowa();
                continue;
            }
            if (shell_output_rect().zawiera(Punkt(e.x,e.y))) {
                g_input_anchor=g_caret;g_select_input=false;g_select_drag=true;
                g_output_selection=true;
                g_selection_anchor=g_selection_end=shell_hit_output(e.x,e.y);
                gui_ustaw_capture_myszy(true);shell_rysuj_terminal();continue;
            }
        } else if (e.typ == BWS_ZDARZENIE_MYSZ_RUCH) {
            if(g_select_drag) {
                if(g_select_input) { g_caret=shell_hit_input(e.x);shell_odswiez_linie_wejsciowa(); }
                else { g_selection_end=shell_hit_output(e.x,e.y);shell_rysuj_terminal(); }
                continue;
            }
            if (shell_drag) {
                const int nx = shell_drag_window_x + (e.x - shell_drag_mouse_x);
                const int ny = shell_drag_window_y + (e.y - shell_drag_mouse_y);
                if (nx != shell_x || ny != shell_y) {
                    shell_x = nx;
                    shell_y = ny;
                    bws_przesun_warstwe(shell_x, shell_y);
                }
                continue;
            }
            if (shell_sb_dragging && g_pasek_przewijania) {
                int lx = e.x - client_x;
                int ly = e.y - client_y;
                ZdarzenieMyszki mz{};
                mz.typ = TypZdarzenia::RuchMyszy;
                mz.x = lx;
                mz.y = ly;
                mz.przycisk = 1;
                g_pasek_przewijania->obsluz_ruch_myszy(mz);
                shell_set_scroll(static_cast<int>(g_pasek_przewijania->wartosc()));
                shell_rysuj_terminal();
                continue;
            }
        } else if (e.typ == BWS_ZDARZENIE_MYSZ_UP) {
            if(g_select_drag) {
                g_select_drag=false;gui_ustaw_capture_myszy(false);continue;
            }
            if (shell_drag) {
                shell_drag = false;
                gui_ustaw_capture_myszy(false);
                gui_ustaw_geometrie_okna(shell_x, shell_y, shell_w, shell_h, shell_maximized ? 2 : 1);
                continue;
            }
            if (shell_sb_dragging && g_pasek_przewijania) {
                shell_sb_dragging = false;
                gui_ustaw_capture_myszy(false);
                int lx = e.x - client_x;
                int ly = e.y - client_y;
                ZdarzenieMyszki mz{};
                mz.typ = TypZdarzenia::ZwolnienieMyszy;
                mz.x = lx;
                mz.y = ly;
                mz.przycisk = 1;
                g_pasek_przewijania->obsluz_zwolnienie_myszy(mz);
                shell_set_scroll(static_cast<int>(g_pasek_przewijania->wartosc()));
                shell_rysuj_terminal();
                continue;
            }
        }

        if (e.typ == BWS_ZDARZENIE_MYSZ_KOLO /*25*/) {
            int delta = e.dy;
            if (delta == 0 && e.dx != 0) delta = e.dx;
            if (delta != 0) {
                shell_set_scroll(g_scroll_linia + (delta>0?-3:3));
                shell_rysuj_terminal();
            }
            continue;
        }

        if (e.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && e.x != 0 /*wcisniety*/) {
            bool usb = (e.kod & BWS_KEY_SOURCE_USB) != 0;
            uint32_t baza = usb ? (e.kod & 0xFFU) : (e.kod & 0x7FU);
            bool page_up = (usb && baza == 75U) || (!usb && baza == 0x49U);
            bool page_down = (usb && baza == 78U) || (!usb && baza == 0x51U);
            bool home = (usb && baza == 74U) || (!usb && baza == 0x47U);
            bool end = (usb && baza == 77U) || (!usb && baza == 0x4FU);

            if (page_up || page_down || home || end) {
                shell_set_scroll(home?0:(end?max_scroll:g_scroll_linia+(page_up?-max_linii:max_linii)));
                shell_rysuj_terminal();continue;
            }
            if ((e.przyciski & BWS_MOD_CTRL) && ((usb && baza==6U) || (!usb && baza==0x2EU))) {
                shell_copy_selection();continue;
            }
            // Physical keys are authoritative; their following ANSI byte stream is discarded.
            if ((usb && baza==80U) || (!usb && (e.kod&BWS_KEY_SOURCE_E0) && baza==0x4BU)) return SHELL_LEFT;
            if ((usb && baza==79U) || (!usb && (e.kod&BWS_KEY_SOURCE_E0) && baza==0x4DU)) return SHELL_RIGHT;
            if ((usb && baza==82U) || (!usb && (e.kod&BWS_KEY_SOURCE_E0) && baza==0x48U)) return SHELL_UP;
            if ((usb && baza==81U) || (!usb && (e.kod&BWS_KEY_SOURCE_E0) && baza==0x50U)) return SHELL_DOWN;
            if ((usb && baza==76U) || (!usb && (e.kod&BWS_KEY_SOURCE_E0) && baza==0x53U)) return SHELL_DELETE;
        }

        if (e.typ == BWS_ZDARZENIE_KLAWISZ && e.kod != 0) {
            static int ansi=0;
            unsigned char c=static_cast<unsigned char>(e.kod);
            if(c==27) { ansi=1;continue; }
            if(ansi==1) { ansi=c=='['?2:0;if(ansi) continue; }
            if(ansi==2) { if(c>=0x40 && c<=0x7e) ansi=0;continue; }
            if(c==3) { shell_copy_selection();continue; }
            // The keyboard also emits the printable letter for Ctrl+C.
            if(key_modifiers & BWS_MOD_CTRL) continue;
            // TEXT_INPUT is the parallel codepoint notification; consume the UTF-8 byte stream once.
            if(c<32 && c!='\n' && c!='\r' && c!='\b' && c!='\t') continue;
            return c;
        }
    }
}

bool pobierz_linie(char* bufor, size_t pojemnosc) {
    if (!bufor || pojemnosc < 2) return false;
    bool przepelnienie=false;
    bufor[0]=0;g_caret=g_input_anchor=0;g_input_scroll_px=0;
    g_obecny_bufor_wejscia=bufor;
    g_input_capacity=pojemnosc;
    int history_index=-1;char unfinished[SHELL_INPUT_CAPACITY]={};
    shell_odswiez_linie_wejsciowa();
    for (;;) {
        int c=pobierz_znak_blokujaco();
        size_t length=shell_length(bufor);
        if(c=='\n' || c=='\r') {
            bool nonwhite=false;
            for(size_t i=0;i<length;++i) if(!jest_biala_spacja(bufor[i])) nonwhite=true;
            if(!nonwhite) {
                bufor[0]=0;g_caret=g_input_anchor=0;g_input_scroll_px=0;
                history_index=-1;przepelnienie=false;shell_odswiez_linie_wejsciowa();continue;
            }
            g_obecny_bufor_wejscia=nullptr;g_caret=g_input_anchor=0;
            g_auto_scroll=true;g_output_selection=false;
            wypisz("powłoka> ");wypisz(bufor);wypisz("\n");
            return !przepelnienie;
        }
        if(c==SHELL_LEFT || c==SHELL_RIGHT) {
            if(g_caret!=g_input_anchor) {
                if(c==SHELL_LEFT) g_caret=g_caret<g_input_anchor?g_caret:g_input_anchor;
                else g_caret=g_caret>g_input_anchor?g_caret:g_input_anchor;
            } else g_caret=c==SHELL_LEFT?shell_previous(bufor,g_caret):shell_next(bufor,g_caret);
            g_input_anchor=g_caret;shell_odswiez_linie_wejsciowa();continue;
        }
        if(c==SHELL_UP || c==SHELL_DOWN) {
            if(c==SHELL_UP && historia_ilosc) {
                if(history_index<0) kopiuj_tekst(unfinished,sizeof(unfinished),bufor);
                if(history_index+1<static_cast<int>(historia_ilosc)) ++history_index;
            } else if(c==SHELL_DOWN && history_index>=0) --history_index;
            else continue;
            kopiuj_tekst(bufor,pojemnosc,history_index<0?unfinished:historia[history_index]);
            g_caret=g_input_anchor=shell_length(bufor);g_input_scroll_px=0;przepelnienie=false;
            shell_odswiez_linie_wejsciowa();continue;
        }
        if(c=='\b' || c==127 || c==SHELL_DELETE) {
            if(g_caret!=g_input_anchor) shell_delete_input_selection(bufor);
            else if(c==SHELL_DELETE) shell_delete_range(bufor,g_caret,shell_next(bufor,g_caret));
            else if(g_caret) shell_delete_range(bufor,shell_previous(bufor,g_caret),g_caret);
            shell_odswiez_linie_wejsciowa();continue;
        }
        char bytes[5]={static_cast<char>(c),0,0,0,0};
        int count=utf8_sekwencja_dlugosc(static_cast<uint8_t>(c));
        for(int i=1;i<count;++i) {
            int next=pobierz_znak_blokujaco();
            if(next>255 || (next&0xc0)!=0x80) { count=0;break; }
            bytes[i]=static_cast<char>(next);
        }
        if(!count) continue;
        uint32_t cp; if(CzcionkaUI::dekoduj_utf8(bytes,0,&cp)!=static_cast<size_t>(count)) continue;
        size_t selected=g_caret>g_input_anchor?g_caret-g_input_anchor:g_input_anchor-g_caret;
        if(length-selected+count>=pojemnosc) { przepelnienie=true;continue; }
        shell_delete_input_selection(bufor);length=shell_length(bufor);
        for(size_t i=length+1;i>g_caret;--i) bufor[i+count-1]=bufor[i-1];
        for(int i=0;i<count;++i) bufor[g_caret+i]=bytes[i];
        g_caret+=count;g_input_anchor=g_caret;
        shell_odswiez_linie_wejsciowa();
    }
}

/* =========================================================================
 * 5. PARSER WIERSZA POLECEN
 * ========================================================================= */

struct Argumenty {
    static constexpr size_t MAX_ARG = 8;
    char* dane[MAX_ARG];
    size_t liczba;
};

bool parse_argumenty(
    char* linia,
    Argumenty* wynik
) {
    if (!linia ||
        !wynik) {
        return false;
    }

    wynik->liczba = 0;

    for (size_t i = 0;
         i < Argumenty::MAX_ARG;
         ++i) {
        wynik->dane[i] = nullptr;
    }

    size_t i = 0;

    while (linia[i] != '\0') {
        while (jest_biala_spacja(
                   linia[i])) {
            ++i;
        }

        if (linia[i] == '\0') {
            break;
        }

        if (wynik->liczba >=
            Argumenty::MAX_ARG) {
            return false;
        }

        bool cudzyslow =
            linia[i] == '"';

        if (cudzyslow) {
            ++i;
            wynik->dane[wynik->liczba]=&linia[i];
            if(linia[i]=='"')return false;
            while(linia[i]!='\0'&&linia[i]!='"')++i;
            if(linia[i]!='"')return false;
            linia[i++]='\0';
            if(linia[i]!='\0'&&!jest_biala_spacja(linia[i]))return false;
        }else{
            wynik->dane[wynik->liczba]=&linia[i];
            while(linia[i]!='\0'&&!jest_biala_spacja(linia[i]))++i;
            if(linia[i]!='\0')linia[i++]='\0';
        }
        while(jest_biala_spacja(linia[i]))++i;

        ++wynik->liczba;
    }

    return true;
}

/* =========================================================================
 * 6. SCIEZKI
 * ========================================================================= */

bool utworz_sciezke_absolutna(
    const char* wejscie,
    char* wyjscie,
    size_t pojemnosc
) {
    if (!wejscie ||
        !wyjscie ||
        pojemnosc < 2) {
        return false;
    }

    size_t dlugosc = 0;

    if (!tekst_zakonczony_w_limicie(
            wejscie,
            pojemnosc,
            &dlugosc)) {
        return false;
    }

    if (dlugosc == 0) {
        return false;
    }

    if (wejscie[0] == '/') {
        return kopiuj_tekst(
            wyjscie,
            pojemnosc,
            wejscie
        );
    }

    /*
     * Potrzebujemy '/' + wejscie + '\0'.
     */
    if (dlugosc + 2 >
        pojemnosc) {
        return false;
    }

    wyjscie[0] = '/';

    for (size_t i = 0;
         i <= dlugosc;
         ++i) {
        wyjscie[i + 1] =
            wejscie[i];
    }

    return true;
}

/* =========================================================================
 * 7. IPv4 / DNS
 * ========================================================================= */

bool parsuj_ipv4(
    const char* tekst,
    uint8_t wynik[4]
) {
    if (!tekst ||
        !wynik) {
        return false;
    }

    size_t pos = 0;

    for (size_t oktet = 0;
         oktet < 4;
         ++oktet) {

        if (tekst[pos] < '0' ||
            tekst[pos] > '9') {
            return false;
        }

        uint32_t wartosc = 0;
        size_t cyfr = 0;

        while (tekst[pos] >= '0' &&
               tekst[pos] <= '9') {

            if (cyfr >= 3) {
                return false;
            }

            wartosc =
                wartosc * 10U +
                static_cast<uint32_t>(
                    tekst[pos] - '0'
                );

            if (wartosc > 255U) {
                return false;
            }

            ++cyfr;
            ++pos;
        }

        wynik[oktet] =
            static_cast<uint8_t>(
                wartosc
            );

        if (oktet < 3) {
            if (tekst[pos] != '.') {
                return false;
            }

            ++pos;
        } else {
            if (tekst[pos] != '\0') {
                return false;
            }
        }
    }

    return true;
}

void wypisz_ipv4(
    const uint8_t ip[4]
) {
    if (!ip) {
        return;
    }

    char liczba[4] = {};

    for (size_t i = 0;
         i < 4;
         ++i) {

        uint_do_str(
            ip[i],
            liczba,
            sizeof(liczba)
        );

        wypisz(liczba);

        if (i != 3) {
            wypisz(".");
        }
    }
}

/* =========================================================================
 * 8. HISTORIA
 * ========================================================================= */

void dodaj_do_historii(
    const char* komenda
) {
    if (!komenda ||
        komenda[0] == '\0') {
        return;
    }

    for (size_t i = HISTORIA_MAX - 1;
         i > 0;
         --i) {

        for (size_t j = 0;
             j < SHELL_HISTORY_ENTRY_CAPACITY;
             ++j) {

            historia[i][j] =
                historia[i - 1][j];
        }
    }

    wyzeruj(
        historia[0],
        SHELL_HISTORY_ENTRY_CAPACITY
    );

    (void)kopiuj_tekst(
        historia[0],
        SHELL_HISTORY_ENTRY_CAPACITY,
        komenda
    );

    if (historia_ilosc <
        HISTORIA_MAX) {
        ++historia_ilosc;
    }
}

/* =========================================================================
 * 9. RDTSC - TYLKO DO ZABAWY / LOSUJ
 * ========================================================================= */

uint64_t pobierz_cykle() {
    uint32_t lo = 0;
    uint32_t hi = 0;

    asm volatile(
        "rdtsc"
        : "=a"(lo),
          "=d"(hi)
        :
        : "memory"
    );

    return
        (static_cast<uint64_t>(hi) << 32) |
        static_cast<uint64_t>(lo);
}

/* =========================================================================
 * 10. POMOC
 * ========================================================================= */

void wypisz_pomoc() {
    shell_wypisz_wiersz_pomocy("Dostępne polecenia:", nullptr);
    shell_wypisz_wiersz_pomocy("", nullptr);

    shell_wypisz_wiersz_pomocy("  Sieć", nullptr);
    shell_wypisz_wiersz_pomocy("    ping <IPv4|domena>", "Sprawdza dostępność hosta przez ICMP.");
    shell_wypisz_wiersz_pomocy("    pobierz <host> <url> <plik>", "Pobiera plik HTTP i zapisuje go w BSP2.");
    shell_wypisz_wiersz_pomocy("", nullptr);

    shell_wypisz_wiersz_pomocy("  Pliki i katalogi", nullptr);
    shell_wypisz_wiersz_pomocy("    utworz <plik>", "Tworzy pusty plik.");
    shell_wypisz_wiersz_pomocy("    zapisz <plik> <tekst>", "Zapisuje tekst do pliku.");
    shell_wypisz_wiersz_pomocy("    czytaj <plik>", "Wyświetla zawartość pliku.");
    shell_wypisz_wiersz_pomocy("    pliki [katalog]", "Wyświetla zawartość katalogu.");
    shell_wypisz_wiersz_pomocy("    usun <ścieżka>", "Usuwa plik lub pusty katalog.");
    shell_wypisz_wiersz_pomocy("    zmiennazwe <ścieżka> <nazwa>", "Zmienia nazwę pliku lub katalogu.");
    shell_wypisz_wiersz_pomocy("", nullptr);

    shell_wypisz_wiersz_pomocy("  Powłoka", nullptr);
    shell_wypisz_wiersz_pomocy("    gdzie", "Wyświetla bieżący katalog.");
    shell_wypisz_wiersz_pomocy("    historia", "Wyświetla historię poleceń.");
    shell_wypisz_wiersz_pomocy("    czysc", "Czyści terminal.");
    shell_wypisz_wiersz_pomocy("    dzwiek", "Odtwarza test HDA 880 Hz.");
    shell_wypisz_wiersz_pomocy("    losuj", "Losuje liczbę.");
    shell_wypisz_wiersz_pomocy("    pisz <tekst>", "Wyświetla tekst.");
    shell_wypisz_wiersz_pomocy("", nullptr);

    shell_wypisz_wiersz_pomocy("  System", nullptr);
    shell_wypisz_wiersz_pomocy("    notatnik", "Uruchamia Notatnik.");
    shell_wypisz_wiersz_pomocy("    kalkulator", "Uruchamia Kalkulator.");
    shell_wypisz_wiersz_pomocy("    przegladarka", "Uruchamia Przeglądarkę Husarz.");
    shell_wypisz_wiersz_pomocy("    uruchom <plik.bur>", "Uruchamia program .bur.");
    shell_wypisz_wiersz_pomocy("    pulpit | wyjdz | exit", "Kończy powłokę.");
    shell_wypisz_wiersz_pomocy("    czas", "Pokazuje czas RTC.");
    shell_wypisz_wiersz_pomocy("    system", "Informacje o systemie.");
    shell_wypisz_wiersz_pomocy("    wsparcie", "Wyświetla listę wspierających Bursztyn OS.");
    shell_wypisz_wiersz_pomocy("    wersja", "Pokazuje aktualną wersję systemu.");
    shell_wypisz_wiersz_pomocy("    aktualizacja [sprawdz]", "Pokazuje wersję i stan aktualizacji.");
    shell_wypisz_wiersz_pomocy("    pci", "Wyświetla /logi/pci.txt.");
    shell_wypisz_wiersz_pomocy("    pomoc", "Pokazuje ten ekran pomocy.");

    dodaj_nowa_linie();
    shell_rysuj_terminal();
}

/* =========================================================================
 * 11. POLECENIA
 * ========================================================================= */

void polecenie_uruchom(
    const char* sciezka_wej
) {
    char sciezka[MAX_SCIEZKA] = {};

    if (!utworz_sciezke_absolutna(
            sciezka_wej,
            sciezka,
            sizeof(sciezka))) {

        wypisz(
            "Błąd: ścieżka jest pusta lub zbyt długa.\n"
        );
        return;
    }

    wypisz("Uruchamianie procesu: ");
    wypisz(sciezka);
    wypisz("...\n");

    if (!bsh_uruchom_program(
            sciezka)) {

        wypisz(
            "Błąd: nie udało się uruchomić programu. "
            "Sprawdź plik, prawa PZB i blokadę wielu instancji.\n"
        );
    }
}

void polecenie_ping(
    const char* cel
) {
    if (!cel ||
        cel[0] == '\0') {

        wypisz(
            "Składnia: ping <adres IPv4 lub domena>\n"
        );
        return;
    }

    uint8_t ip[4] = {};

    if (parsuj_ipv4(
            cel,
            ip)) {

        wypisz("PING ");
        wypisz_ipv4(ip);
        wypisz("...\n");

        if (!bsh_ping_ipv4(ip)) {
            wypisz(
                "Błąd: wysłanie ICMP nie powiodło się.\n"
            );
        }

        return;
    }

    /*
     * Jezeli argument zawiera cyfry i kropki, ale nie jest poprawnym IPv4,
     * nie traktujemy go jako domeny. Pomaga wykryc np. 999.1.1.1.
     */
    bool tylko_ipv4_znaki = true;

    for (size_t i = 0;
         cel[i] != '\0';
         ++i) {

        const char c = cel[i];

        if (!((c >= '0' &&
               c <= '9') ||
              c == '.')) {

            tylko_ipv4_znaki = false;
            break;
        }
    }

    if (tylko_ipv4_znaki) {
        wypisz(
            "Błąd: nieprawidłowy adres IPv4. "
            "Wymagane są 4 oktety 0..255.\n"
        );
        return;
    }

    size_t dlugosc_hosta = 0;

    if (!tekst_zakonczony_w_limicie(
            cel,
            MAX_NAZWA_HOSTA,
            &dlugosc_hosta) ||
        dlugosc_hosta == 0) {

        wypisz(
            "Błąd: nazwa domeny jest zbyt długa.\n"
        );
        return;
    }

    wypisz("DNS: ");
    wypisz(cel);
    wypisz("...\n");

    if (!bws_siec_dns(
            cel,
            ip)) {

        wypisz(
            "Błąd: nie udało się rozwiązać domeny DNS.\n"
        );
        return;
    }

    wypisz("Adres: ");
    wypisz_ipv4(ip);
    wypisz("\nPING...\n");

    if (!bsh_ping_ipv4(ip)) {
        wypisz(
            "Błąd: wysłanie ICMP nie powiodło się.\n"
        );
    }
}

size_t znajdz_koniec_tekstu(
    const char* bufor,
    size_t pojemnosc
) {
    if (!bufor) {
        return 0;
    }

    for (size_t i = 0;
         i < pojemnosc;
         ++i) {

        if (bufor[i] == '\0') {
            return i;
        }
    }

    return pojemnosc;
}

void polecenie_pobierz(
    const char* domena,
    const char* sciezka_http,
    const char* sciezka_pliku
) {
    if (!domena ||
        !sciezka_http ||
        !sciezka_pliku) {

        wypisz(
            "Składnia: pobierz <domena> <sciezka_HTTP> <plik_docelowy>\n"
        );
        return;
    }

    size_t host_len = 0;
    size_t http_len = 0;

    if (!tekst_zakonczony_w_limicie(
            domena,
            MAX_NAZWA_HOSTA,
            &host_len) ||
        host_len == 0) {

        wypisz(
            "Błąd: nieprawidłowa lub zbyt długa domena.\n"
        );
        return;
    }

    if (!tekst_zakonczony_w_limicie(
            sciezka_http,
            MAX_SCIEZKA_HTTP,
            &http_len) ||
        http_len == 0 ||
        sciezka_http[0] != '/') {

        wypisz(
            "Błąd: ścieżka HTTP musi zaczynać się od '/' i mieścić w limicie.\n"
        );
        return;
    }

    char cel[MAX_SCIEZKA] = {};

    if (!utworz_sciezke_absolutna(
            sciezka_pliku,
            cel,
            sizeof(cel))) {

        wypisz(
            "Błąd: nieprawidłowa ścieżka pliku docelowego.\n"
        );
        return;
    }

    uint8_t ip[4] = {};

    wypisz("DNS: ");
    wypisz(domena);
    wypisz("...\n");

    if (!bws_siec_dns(
            domena,
            ip)) {

        wypisz(
            "Błąd: DNS nie zwrócił adresu.\n"
        );
        return;
    }

    wypisz("IP: ");
    wypisz_ipv4(ip);
    wypisz("\nHTTP GET ");
    wypisz(sciezka_http);
    wypisz("\n");

    /*
     * Ochrona starego ABI BWS 29.
     */
    const uint64_t adres_bufora =
        reinterpret_cast<uint64_t>(
            bufor_http
        );

    if (adres_bufora >
        UINT32_MAX) {

        wypisz(
            "Błąd ABI: bufor HTTP leży powyżej 4 GiB. "
            "BWS 29 wymaga jeszcze poprawy sposobu przekazywania wskaźnika.\n"
        );
        return;
    }

    wyzeruj(
        bufor_http,
        sizeof(bufor_http)
    );

    /*
     * Zostawiamy ostatni bajt na NUL, aby odpowiedz tekstowa zawsze mogla
     * zostac bezpiecznie zmierzona.
     */
    if (!bws_siec_pobierz_http(
            ip,
            domena,
            sciezka_http,
            bufor_http,
            HTTP_BUF_SIZE - 1U)) {

        wypisz(
            "Błąd: pobieranie HTTP nie powiodło się.\n"
        );
        return;
    }

    bufor_http[
        HTTP_BUF_SIZE - 1U] =
        '\0';

    const size_t odebrano =
        znajdz_koniec_tekstu(
            bufor_http,
            HTTP_BUF_SIZE
        );

    if (odebrano == 0) {
        wypisz(
            "Błąd: serwer zwrócił pusty bufor lub aktualne ABI nie przekazuje długości odpowiedzi.\n"
        );
        return;
    }

    if (odebrano >=
        HTTP_BUF_SIZE) {

        wypisz(
            "Błąd: odpowiedź HTTP nie jest poprawnym tekstem NUL-terminated.\n"
        );
        return;
    }

    /*
     * utworz() moze zwrocic false, gdy plik juz istnieje. To nie jest
     * problem - zapisz_plik() nadpisuje istniejacy plik.
     */
    (void)utworz(cel);

    if (!zapisz_plik(
            cel,
            bufor_http,
            static_cast<uint32_t>(
                odebrano))) {

        wypisz(
            "Błąd: odpowiedź pobrano, ale nie udało się zapisać jej w BSP2.\n"
        );
        return;
    }

    wypisz("Zapisano ");
    char liczba[24] = {};

    uint_do_str(
        odebrano,
        liczba,
        sizeof(liczba)
    );

    wypisz(liczba);
    wypisz(" bajtów do ");
    wypisz(cel);
    wypisz(".\n");
}

void polecenie_czytaj(
    const char* wejscie
) {
    char sciezka[MAX_SCIEZKA] = {};

    if (!utworz_sciezke_absolutna(
            wejscie,
            sciezka,
            sizeof(sciezka))) {

        wypisz(
            "Błąd: nieprawidłowa ścieżka.\n"
        );
        return;
    }

    char bufor[BUF_CZYTAJ_PLIK] = {};

    /*
     * Czytamy najwyzej N-1, bo czytaj_plik() jest API binarnym i samo
     * nie dopisuje terminatora.
     */
    if (!czytaj_plik(
            sciezka,
            bufor,
            BUF_CZYTAJ_PLIK - 1U)) {

        wypisz(
            "Błąd odczytu pliku.\n"
        );
        return;
    }

    bufor[
        BUF_CZYTAJ_PLIK - 1U] =
        '\0';

    wypisz("--- ");
    wypisz(sciezka);
    wypisz(" ---\n");
    wypisz(bufor);
    wypisz("\n");
}

void polecenie_pliki(
    const char* wejscie
) {
    char sciezka[MAX_SCIEZKA] = {};

    if (!wejscie ||
        wejscie[0] == '\0') {

        sciezka[0] = '/';
        sciezka[1] = '\0';
    } else if (!utworz_sciezke_absolutna(
                   wejscie,
                   sciezka,
                   sizeof(sciezka))) {

        wypisz(
            "Błąd: nieprawidłowa ścieżka katalogu.\n"
        );
        return;
    }

    char bufor[BUF_LISTA] = {};

    if (!bsh_wylistuj_katalog(
            sciezka,
            bufor,
            BUF_LISTA)) {

        wypisz(
            "Błąd: katalog nie istnieje lub brak uprawnień.\n"
        );
        return;
    }

    bufor[
        BUF_LISTA - 1U] =
        '\0';

    wypisz("Zawartość ");
    wypisz(sciezka);
    wypisz(":\n");

    if (bufor[0] == '\0') {
        wypisz("  <pusty katalog>\n");
    } else {
        wypisz(bufor);

        const size_t len =
            dlugosc_tekstu_limit(
                bufor,
                BUF_LISTA
            );

        if (len > 0 &&
            bufor[len - 1] != '\n') {
            wypisz("\n");
        }
    }
}

void polecenie_cytat() {
    constexpr const char* SCIEZKA =
        "/cytaty.txt";

    char bufor[1024] = {};
    if (!czytaj_plik(SCIEZKA, bufor, sizeof(bufor) - 1U)) {
        wypisz("Nie znaleziono pliku cytaty.txt.\n");
        return;
    }
    bufor[sizeof(bufor) - 1U] = '\0';

    uint32_t liczba = 0;
    bool poczatek = true;
    for (size_t i = 0; bufor[i] != '\0'; ++i) {
        if (poczatek && bufor[i] != '\n' && bufor[i] != '\r') ++liczba;
        poczatek = bufor[i] == '\n';
    }
    if (liczba == 0) {
        wypisz("Brak cytatow.\n");
        return;
    }

    /* Lekki PRNG uzytkowy. Nie jest i nie moze byc zrodlem entropii TLS. */
    static uint32_t stan = UINT32_C(0xB0527A11);
    stan ^= stan << 13U;
    stan ^= stan >> 17U;
    stan ^= stan << 5U;
    const uint32_t wybrany = stan % liczba;

    uint32_t indeks = 0;
    size_t start = 0;
    for (size_t i = 0;; ++i) {
        const bool koniec = bufor[i] == '\n' || bufor[i] == '\0';
        if (!koniec) continue;
        if (i > start) {
            if (indeks == wybrany) {
                bufor[i] = '\0';
                wypisz(bufor + start);
                wypisz("\n");
                return;
            }
            ++indeks;
        }
        if (bufor[i] == '\0') break;
        start = i + 1U;
    }
    wypisz("Nie można odczytać cytatów.\n");
}

void polecenie_pci() {
    char bufor[BUF_PCI] = {};

    if (!czytaj_plik(
            "/logi/pci.txt",
            bufor,
            BUF_PCI - 1U)) {

        wypisz(
            "Błąd: brak raportu /logi/pci.txt lub brak uprawnień.\n"
        );
        return;
    }

    bufor[
        BUF_PCI - 1U] =
        '\0';

    wypisz(
        "--- Raport PCI zapisany przez Ring 0 ---\n"
    );

    wypisz(bufor);
    wypisz("\n");
}

/* =========================================================================
 * 12. DISPATCH POLECEN
 * ========================================================================= */

void wykonaj_polecenie(
    const Argumenty& a
) {
    if (a.liczba == 0) {
        return;
    }

    const char* cmd =
        a.dane[0];

    if (tekst_rowny(
            cmd,
            "pomoc") ||
        tekst_rowny(
            cmd,
            "help")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: pomoc\n"
            );
            return;
        }

        wypisz_pomoc();
        return;
    }

    if (tekst_rowny(
            cmd,
            "notatnik")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: notatnik\n"
            );
            return;
        }

        polecenie_uruchom(
            "/programy/notatnik.cebula/notatnik.bur"
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "kalkulator")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: kalkulator\n"
            );
            return;
        }

        polecenie_uruchom(
            "/programy/kalkulator.cebula/kalkulator.bur"
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "przegladarka")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: przegladarka\n"
            );
            return;
        }

        polecenie_uruchom(
            "/programy/przegladarka.cebula/przegladarka.bur"
        );
        return;
    }

    if (tekst_rowny(cmd, "pulpit") ||
        tekst_rowny(cmd, "wyjdz") ||
        tekst_rowny(cmd, "exit")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: wyjdz\n"
            );
            return;
        }

        wypisz(
            "Powrot do Menedzera Okien...\n"
        );

        gui_zakoncz_aplikacje();
    }

    if (tekst_rowny(
            cmd,
            "uruchom")) {

        if (a.liczba != 2) {
            wypisz(
                "Składnia: uruchom <plik.bur>\n"
            );
            return;
        }

        polecenie_uruchom(
            a.dane[1]
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "ping")) {

        if (a.liczba != 2) {
            wypisz(
                "Składnia: ping <IPv4 lub domena>\n"
            );
            return;
        }

        polecenie_ping(
            a.dane[1]
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "pobierz")) {

        if (a.liczba != 4) {
            wypisz(
                "Składnia: pobierz <domena> <sciezka_HTTP> <plik_docelowy>\n"
            );
            return;
        }

        polecenie_pobierz(
            a.dane[1],
            a.dane[2],
            a.dane[3]
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "czas")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: czas\n"
            );
            return;
        }

        char bufor[32] = {};

        if (!bsh_pobierz_czas(
                bufor)) {

            wypisz(
                "Błąd: RTC/BWS nie zwrócił czasu.\n"
            );
            return;
        }

        bufor[
            sizeof(bufor) - 1U] =
            '\0';

        wypisz("Aktualny czas RTC: ");
        wypisz(bufor);
        wypisz("\n");
        return;
    }

    if (tekst_rowny(
            cmd,
            "system")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: system\n"
            );
            return;
        }

        wypisz(
            "OS: " BURSZTYN_NAZWA_WERSJI "\n"
            "Autor: Dymitr Wygowski\n"
            "E-mail: programista.art@gmail.com\n"
            "Jądro: autorskie, monolityczne, paging x86_64 4-level\n"
            "Procesy: Ring 3 + PZB/BZL + prywatne przestrzenie adresowe\n"
            "FS: BSP2 / Bursztynowy System Plików\n"
            "GUI: autorski kompozytor warstw\n"
            "Sieć: E1000, DHCP, ARP, ICMP, DNS, TCP/HTTP\n"
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "pci")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: pci\n"
            );
            return;
        }

        polecenie_pci();
        return;
    }

    if (tekst_rowny(
            cmd,
            "gdzie")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: gdzie\n"
            );
            return;
        }

        /*
         * Obecna powloka nie posiada jeszcze chdir/cwd w ABI.
         */
        wypisz(
            "Obecny katalog roboczy: /\n"
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "historia")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: historia\n"
            );
            return;
        }

        if (historia_ilosc == 0) {
            wypisz(
                "<historia pusta>\n"
            );
            return;
        }

        for (size_t i = 0;
             i < historia_ilosc;
             ++i) {

            char numer[8] = {};

            uint_do_str(
                i + 1,
                numer,
                sizeof(numer)
            );

            wypisz(numer);
            wypisz(". ");
            wypisz(historia[i]);
            wypisz("\n");
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "czysc") || tekst_rowny(cmd, "czyść")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: czysc\n"
            );
            return;
        }

        g_liczba_linii = 1;
        g_rows_dirty = true;
        g_output_selection = false;
        g_pierwsza_linia = 0;
        g_scroll_linia = 0;
        g_auto_scroll = true;
        g_linie[0].len = 0;
        g_linie[0].tekst_x = 0;
        g_linie[0].tekst[0] = '\0';
        g_linie[0].opis[0] = '\0';
        g_linie[0].opis_x = 0;
        shell_rysuj_terminal();
        return;
    }

    if (tekst_rowny(
            cmd,
            "losuj")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: losuj\n"
            );
            return;
        }

        const uint64_t cykle =
            pobierz_cykle();

        const uint64_t kosc =
            cykle % 6ULL + 1ULL;

        char wynik[8] = {};

        uint_do_str(
            kosc,
            wynik,
            sizeof(wynik)
        );

        wypisz(
            "Rzut kostka (RDTSC, nie kryptograficzny): "
        );

        wypisz(wynik);
        wypisz("\n");
        return;
    }

    if (tekst_rowny(
            cmd,
            "cytat")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: cytat\n"
            );
            return;
        }

        polecenie_cytat();
        return;
    }

    if (tekst_rowny(
            cmd,
            "pliki")) {

        if (a.liczba > 2) {
            wypisz(
                "Składnia: pliki [katalog]\n"
            );
            return;
        }

        polecenie_pliki(
            a.liczba == 2
                ? a.dane[1]
                : nullptr
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "czytaj")) {

        if (a.liczba != 2) {
            wypisz(
                "Składnia: czytaj <plik>\n"
            );
            return;
        }

        polecenie_czytaj(
            a.dane[1]
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "utworz")) {

        if (a.liczba != 2) {
            wypisz(
                "Składnia: utworz <plik>\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Błąd: nieprawidłowa lub zbyt długa ścieżka.\n"
            );
            return;
        }

        if (utworz(sciezka)) {
            wypisz("Utworzono: ");
            wypisz(sciezka);
            wypisz("\n");
        } else {
            wypisz(
                "Błąd: plik istnieje, ścieżka jest błędna lub brak uprawnień.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "zapisz")) {

        if (a.liczba != 3) {
            wypisz(
                "Składnia: zapisz <plik> <tekst>\n"
                "Dla tekstu ze spacjami użyj cudzysłowu.\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Błąd: nieprawidłowa ścieżka.\n"
            );
            return;
        }

        size_t dlugosc = 0;

        if (!tekst_zakonczony_w_limicie(
                a.dane[2],
                SHELL_INPUT_CAPACITY,
                &dlugosc)) {

            wypisz(
                "Błąd: tekst jest za długi.\n"
            );
            return;
        }

        /*
         * Utworzenie jest best-effort: jesli plik juz istnieje, zapis
         * nadal moze sie udac.
         */
        (void)utworz(sciezka);

        if (zapisz_plik(
                sciezka,
                a.dane[2],
                static_cast<uint32_t>(
                    dlugosc))) {

            wypisz("Zapisano.\n");
        } else {
            wypisz(
                "Błąd zapisu lub brak PRAWO_PLIKI_ZAPISZ.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "usun") || tekst_rowny(cmd, "usuń")) {

        if (a.liczba != 2) {
            wypisz(
                "Składnia: usun <ścieżka>\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Błąd: nieprawidłowa ścieżka.\n"
            );
            return;
        }

        if (tekst_rowny(
                sciezka,
                "/")) {

            wypisz(
                "Błąd: nie można usunąć korzenia '/'.\n"
            );
            return;
        }

        if (bsh_usun_twor(
                sciezka)) {

            wypisz("Usunięto: ");
            wypisz(sciezka);
            wypisz("\n");
        } else {
            wypisz(
                "Błąd: obiekt nie istnieje, katalog nie jest pusty lub brak uprawnień.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "zmien_nazwe") || tekst_rowny(cmd, "zmiennazwe") || tekst_rowny(cmd, "zmieńnazwę")) {

        if (a.liczba != 3) {
            wypisz(
                "Składnia: zmiennazwe <ścieżka> <nowa_nazwa>\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Błąd: nieprawidłowa ścieżka.\n"
            );
            return;
        }

        /*
         * Nowa nazwa nie moze zawierac slash, bo BWS 8 zmienia nazwe
         * wewnatrz tego samego katalogu.
         */
        if (a.dane[2][0] == '\0') {
            wypisz(
                "Błąd: pusta nazwa.\n"
            );
            return;
        }

        for (size_t i = 0;
             a.dane[2][i] != '\0';
             ++i) {

            if (a.dane[2][i] == '/' ||
                a.dane[2][i] == '\\') {

                wypisz(
                    "Błąd: nowa nazwa nie może zawierać '/' ani '\\\\'.\n"
                );
                return;
            }
        }

        if (bsh_zmien_nazwe(
                sciezka,
                a.dane[2])) {

            wypisz(
                "Zmieniono nazwę.\n"
            );
        } else {
            wypisz(
                "Błąd: zmiana nazwy nie powiodła się.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "dzwiek")) {

        if (a.liczba != 1) {
            wypisz(
                "Składnia: dzwiek\n"
            );
            return;
        }

        wypisz(
            "Odtwarzanie tonu testowego HDA 880 Hz / 500 ms...\n"
        );

        bws_dzwiek_test(
            880,
            500
        );

        return;
    }

    if (tekst_rowny(cmd, "wersja")) {
        if (a.liczba != 1) {
            wypisz("Składnia: wersja\n");
            return;
        }
        wypisz(BURSZTYN_NAZWA_WERSJI "\n");
        return;
    }

    if (tekst_rowny(cmd, "wsparcie")) {
        if(a.liczba!=1) wypisz("Składnia: wsparcie\n");
        else shell_sponsors();
        return;
    }

    if (tekst_rowny(cmd, "aktualizacja")) {
        if (a.liczba > 2) {
            wypisz("Składnia: aktualizacja [sprawdz]\n");
            return;
        }
        wypisz("Aktualna wersja: " BURSZTYN_NAZWA_WERSJI "\n");
        char wykaz[512] = {};
        if (!czytaj_plik("/aktualizacje/wykazaktualizacji.wykaz", wykaz,
                          sizeof(wykaz) - 1U)) {
            wypisz("Dostępna wersja: brak danych (użyj: pobierz <host> /aktualizacje/wykazaktualizacji.wykaz /aktualizacje/wykazaktualizacji.wykaz, potem Sprawdź aktualizacje).\n");
        } else {
            wykaz[sizeof(wykaz) - 1U] = '\0';
            wypisz("Wykaz (początek):\n");
            size_t i = 0;
            int linie = 0;
            while (wykaz[i] != '\0' && linie < 3) {
                if (wykaz[i] == '\n') ++linie;
                ++i;
                if (i >= sizeof(wykaz) - 1U) break;
            }
            wykaz[i] = '\0';
            wypisz(wykaz);
        }
        wypisz("Sprawdź aktualizacje | Pobierz | Zainstaluj | Uruchom ponownie\n");
        wypisz("Instalacja wymaga potwierdzenia i nie następuje automatycznie.\n");
        return;
    }

    if (tekst_rowny(
            cmd,
            "pisz")) {
        if (a.liczba != 2) {
            wypisz(
                "Składnia: pisz <tekst>\n"
                "Dla tekstu ze spacjami użyj cudzysłowu.\n"
            );
            return;
        }

        wypisz(a.dane[1]);
        wypisz("\n");
        return;
    }

    wypisz("Nieznane polecenie: '");
    wypisz(cmd);
    wypisz("'. Wpisz 'pomoc'.\n");
}

} // namespace

/* =========================================================================
 * 13. NAGLOWEK BINARNY APLIKACJI
 * ========================================================================= */

extern "C"
__attribute__((noreturn))
void _start();

extern "C" {

__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},

    reinterpret_cast<uint64_t>(
        &_start
    ),

    BUR_TEXT_OFFSET,
    BUR_TEXT_SIZE,
    BUR_TEXT_VADDR,

    BUR_DATA_OFFSET,
    BUR_DATA_SIZE,
    BUR_DATA_VADDR
};

} // extern "C"

/* =========================================================================
 * 14. ENTRY POINT
 * ========================================================================= */

extern "C"
__attribute__((noreturn))
void _start() {
    g_linie = new ShellLine[MAXSCROLLBACKLINES]{};
    if (!g_linie) gui_zakoncz_aplikacje();
    GuiMonitorInfo monitor{};
    if (gui_pobierz_monitor(0, &monitor)) {
        if (ui_skala_poprawna(monitor.scale_x1000 / 10U)) {
            ui_skala_procent = monitor.scale_x1000 / 10U;
        }
        shell_work_x = monitor.work_x;
        shell_work_y = monitor.work_y;
        shell_work_w = monitor.work_szer;
        shell_work_h = monitor.work_wys;
    } else {
        gui_pobierz_rozdzielczosc(&shell_screen_w, &shell_screen_h);
        shell_work_w = shell_screen_w;
        shell_work_h = shell_screen_h;
    }

    shell_w = ui_skaluj(shell_logical_w, ui_skala_procent);
    shell_h = ui_skaluj(shell_logical_h, ui_skala_procent);
    if (shell_w > shell_work_w) shell_w = shell_work_w;
    if (shell_h > shell_work_h) shell_h = shell_work_h;

    shell_x = shell_work_x + (shell_work_w - shell_w) / 2;
    shell_y = shell_work_y + (shell_work_h - shell_h) / 2;
    if (shell_x < shell_work_x) shell_x = shell_work_x;
    if (shell_y < shell_work_y) shell_y = shell_work_y;

    if (bws_utworz_warstwe(shell_x, shell_y, shell_w, shell_h, 10) < 0)
        gui_zakoncz_aplikacje();
    gui_ustaw_tytul_okna("Powłoka Bursztynowa");
    gui_ustaw_geometrie_okna(shell_x, shell_y, shell_w, shell_h);

    g_pasek_przewijania = new PasekPrzewijania();
    g_pasek_przewijania->ustaw_orientacje(Orientacja::Pionowa);

    wypisz(
        "\n"
        "==================================================\n"
        " Powłoka Bursztynowa bsh v3.0 (Unicode)\n"
        " " BURSZTYN_NAZWA_WERSJI "\n"
        " Autor: Dymitr Wygowski\n"
        " Ring 3 / BWS / BSP2 / PZB\n"
        "==================================================\n"
        "Wpisz 'pomoc', aby zobaczyć polecenia.\n"
    );

    char linia[SHELL_INPUT_CAPACITY] = {};

    for (;;) {
        const bool cala_linia = pobierz_linie(linia, sizeof(linia));

        if (!cala_linia) {
            wypisz("Błąd: polecenie przekracza limit 2047 bajtów i zostało odrzucone.\n");
            continue;
        }
        if (linia[0] == '\0') {
            continue;
        }

        Argumenty argumenty{};
        if(!kopiuj_tekst(komenda_dla_historii, sizeof(komenda_dla_historii), linia)){
            wypisz("Błąd: nie można zapisać komendy w historii.\n");
            continue;
        }

        if (!parse_argumenty(linia, &argumenty)) {
            wypisz("Błąd składni: za dużo argumentów lub brakuje zamykającego cudzysłowu.\n");
            continue;
        }
        if (argumenty.liczba == 0) continue;

        dodaj_do_historii(komenda_dla_historii);
        wykonaj_polecenie(argumenty);
    }
}
