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
 *   text: offset 0x1000,  rozmiar 0x8000,  VA 0x601000
 *   data: offset 0x9000,  rozmiar 0x20000, VA 0x609000
 *
 * Zmiana tych wartosci wymaga jednoczesnej zmiany linkera aplikacji.
 */

#include "bursztyn_gui.h"

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
    "Naglowek BUR musi miec 60 bajtow"
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

char shell_ekran[SHELL_ROWS][SHELL_COLS + 1] = {};
int shell_wiersz = 0;
int shell_kolumna = 0;
int shell_x = SHELL_X_START;
int shell_y = SHELL_Y_START;
int shell_w = SHELL_W;
int shell_h = SHELL_H;
int shell_screen_w = 1024;
int shell_screen_h = 768;
int shell_restore_x = SHELL_X_START;
int shell_restore_y = SHELL_Y_START;
int shell_restore_w = SHELL_W;
int shell_restore_h = SHELL_H;
bool shell_maximized = false;
bool shell_minimized = false;
bool shell_drag = false;
int shell_drag_mouse_x = 0;
int shell_drag_mouse_y = 0;
int shell_drag_window_x = 0;
int shell_drag_window_y = 0;

struct ShellPerf {
    uint64_t key_count, full_redraw_count, partial_redraw_count;
    uint64_t dirty_area, compose_requests, allocations_key;
} shell_perf{};

void shell_dopisz_znak(char c);

int shell_caret_advance(int row,int column) {
    int advance=0;
    for(int c=0;c<column;++c){int a=gui_pobierz_szerokosc_znaku(
        static_cast<uint8_t>(shell_ekran[row][c]));if(a<1)a=1;advance+=a+1;}
    return advance;
}

void shell_rysuj_wiersz(int row,bool caret) {
    if(row<0||row>=SHELL_ROWS)return;
    const int x=shell_x+SHELL_TEXT_X,y=shell_y+SHELL_TEXT_Y+row*SHELL_LINE_H;
    const int width=shell_w-2*SHELL_TEXT_X;
    gui_rysuj_prostokat(x,y,width,SHELL_LINE_H,0x001A0B00);
    shell_ekran[row][SHELL_COLS]='\0';
    gui_wypisz_tekst_kolor(x,y,0x00E58A00,shell_ekran[row]);
    if(caret)gui_rysuj_prostokat(x+shell_caret_advance(row,shell_kolumna),y+14,8,2,0x00FFBF00);
    ++shell_perf.partial_redraw_count;shell_perf.dirty_area+=static_cast<uint64_t>(width)*SHELL_LINE_H;
}

void shell_rysuj_zmiane_wejscia(int old_row) {
    if(old_row!=shell_wiersz)shell_rysuj_wiersz(old_row,false);
    shell_rysuj_wiersz(shell_wiersz,true);
    gui_odswiez();++shell_perf.compose_requests;
}

void shell_rysuj_terminal() {
    ++shell_perf.full_redraw_count;
    gui_rysuj_okno(shell_x, shell_y, shell_w, shell_h, "Powloka Bursztynowa");
    gui_rysuj_standardowa_belke(shell_x, shell_y, shell_w,
                                "Powloka Bursztynowa", shell_maximized);
    if (shell_minimized) { gui_odswiez(); return; }
    gui_rysuj_prostokat(shell_x + 4, shell_y + SHELL_TITLE_H, shell_w - 8,
                        shell_h - SHELL_TITLE_H - 4, 0x001A0B00);
    for (int r = 0; r < SHELL_ROWS; ++r) {
        shell_ekran[r][SHELL_COLS] = '\0';
        gui_wypisz_tekst_kolor(shell_x + SHELL_TEXT_X,
                               shell_y + SHELL_TEXT_Y + r * SHELL_LINE_H,
                               0x00E58A00, shell_ekran[r]);
    }
    /* Kursor tekstowy jest zwyklym fragmentem powierzchni Shella. */
    int caret_advance = 0;
    for (int c = 0; c < shell_kolumna; ++c) {
        int a = gui_pobierz_szerokosc_znaku(
            static_cast<uint8_t>(shell_ekran[shell_wiersz][c]));
        if (a < 1) a = 1;
        caret_advance += a + 1;
    }
    gui_rysuj_prostokat(shell_x + SHELL_TEXT_X + caret_advance,
                        shell_y + SHELL_TEXT_Y + shell_wiersz * SHELL_LINE_H + 14,
                        8, 2, 0x00FFBF00);
    gui_odswiez();
}

void shell_przewin() {
    for (int r = 1; r < SHELL_ROWS; ++r)
        for (int c = 0; c <= SHELL_COLS; ++c)
            shell_ekran[r - 1][c] = shell_ekran[r][c];
    for (int c = 0; c <= SHELL_COLS; ++c)
        shell_ekran[SHELL_ROWS - 1][c] = '\0';
    shell_wiersz = SHELL_ROWS - 1;
}

void shell_dopisz_znak(char c) {
    if (c == '\r') { shell_kolumna = 0; return; }
    if (c == '\n') {
        shell_kolumna = 0;
        if (++shell_wiersz >= SHELL_ROWS) shell_przewin();
        return;
    }
    if (c == '\b') {
        if (shell_kolumna > 0) {
            shell_ekran[shell_wiersz][--shell_kolumna] = '\0';
        }
        return;
    }
    if (shell_kolumna >= SHELL_COLS) {
        shell_kolumna = 0;
        if (++shell_wiersz >= SHELL_ROWS) shell_przewin();
    }
    shell_ekran[shell_wiersz][shell_kolumna++] = c;
    shell_ekran[shell_wiersz][shell_kolumna] = '\0';
}

void shell_wypisz(const char* tekst) {
    if (!tekst) return;
    for (size_t i = 0; tekst[i] != '\0'; ++i) shell_dopisz_znak(tekst[i]);
    shell_rysuj_terminal();
}

void shell_obsluz_mysz(const bws_zdarzenie& e) {
    if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
        const gui_akcja_belki akcja =
            gui_hit_test_belki(e.x, e.y, shell_x, shell_y, shell_w);
        if (akcja == GUI_BELKA_ZAMKNIJ) gui_zakoncz_aplikacje();
        if (akcja == GUI_BELKA_MINIMALIZUJ) {
            shell_minimized = gui_minimalizuj_okno();
            shell_drag = false;
            gui_ustaw_capture_myszy(false);
            return;
        }
        if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
            if (!shell_maximized) {
                shell_restore_x=shell_x; shell_restore_y=shell_y;
                shell_restore_w=shell_w; shell_restore_h=shell_h;
                shell_x=0; shell_y=0; shell_w=shell_screen_w;
                shell_h=shell_screen_h > 40 ? shell_screen_h-40 : shell_screen_h;
                shell_maximized=true;
            } else {
                shell_x=shell_restore_x; shell_y=shell_restore_y;
                shell_w=shell_restore_w; shell_h=shell_restore_h;
                shell_maximized=false;
            }
            (void)bws_utworz_warstwe(shell_x,shell_y,shell_w,shell_h,10);
            shell_rysuj_terminal();
            return;
        }
        if (akcja == GUI_BELKA_DRAG && !shell_maximized) {
            shell_drag = true;
            shell_drag_mouse_x = e.x;
            shell_drag_mouse_y = e.y;
            shell_drag_window_x = shell_x;
            shell_drag_window_y = shell_y;
            gui_ustaw_capture_myszy(true);
        }
    } else if (e.typ == BWS_ZDARZENIE_MYSZ_RUCH && shell_drag) {
        const int nx = shell_drag_window_x + (e.x - shell_drag_mouse_x);
        const int ny = shell_drag_window_y + (e.y - shell_drag_mouse_y);
        if (nx != shell_x || ny != shell_y) {
            shell_x = nx;
            shell_y = ny;
            /* Przesuwamy metadane warstwy. Bufor pikseli pozostaje nietkniety. */
            bws_przesun_warstwe(shell_x, shell_y);
            gui_odswiez();
        }
    } else if (e.typ == BWS_ZDARZENIE_MYSZ_UP && shell_drag) {
        shell_drag = false;
        gui_ustaw_capture_myszy(false);
    }
}

constexpr uint64_t BUR_TEXT_OFFSET = 0x1000ULL;
constexpr uint64_t BUR_TEXT_SIZE   = 0x8000ULL;
constexpr uint64_t BUR_TEXT_VADDR  = 0x601000ULL;

constexpr uint64_t BUR_DATA_OFFSET = 0x9000ULL;
constexpr uint64_t BUR_DATA_SIZE   = 0x20000ULL;
constexpr uint64_t BUR_DATA_VADDR  = 0x609000ULL;

constexpr size_t MAX_LINIA = 128;
constexpr size_t MAX_SCIEZKA = 64;
constexpr size_t MAX_NAZWA_HOSTA = 64;
constexpr size_t MAX_SCIEZKA_HTTP = 96;

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
 * sie przy 0x609000, natomiast stos Ring 3 lezy wysoko i nie nadaje sie
 * do tego ABI.
 *
 * Po poprawieniu ABI BWS 29 ten warunek mozna usunac.
 */
constexpr uint32_t HTTP_BUF_SIZE = 32U * 1024U;

alignas(16)
char bufor_http[HTTP_BUF_SIZE] = {};

char historia[HISTORIA_MAX][MAX_LINIA] = {};
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

void wypisz_znak(
    char c
) {
    const int old_row=shell_wiersz;
    shell_dopisz_znak(c);
    shell_rysuj_zmiane_wejscia(old_row);
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

char pobierz_znak_blokujaco() {
    for (;;) {
        bws_zdarzenie e{};
        if (!gui_czekaj_na_zdarzenie(&e)) continue;
        if (e.typ == BWS_ZDARZENIE_FOCUS && shell_minimized)
            shell_minimized = false;
        if (e.typ == BWS_ZDARZENIE_ZAMKNIJ) gui_zakoncz_aplikacje();
        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN ||
            e.typ == BWS_ZDARZENIE_MYSZ_RUCH ||
            e.typ == BWS_ZDARZENIE_MYSZ_UP) {
            shell_obsluz_mysz(e);
            continue;
        }
        if (e.typ == BWS_ZDARZENIE_KLAWISZ && e.kod != 0)
            return static_cast<char>(e.kod);
    }
}

/*
 * Zwraca true, jezeli cala linia zmiescila sie w buforze.
 * Po przekroczeniu limitu reszta wejscia jest konsumowana, ale nie zapisywana.
 */
bool pobierz_linie(
    char* bufor,
    size_t pojemnosc
) {
    if (!bufor ||
        pojemnosc < 2) {
        return false;
    }

    size_t pozycja = 0;
    bool przepelnienie = false;

    bufor[0] = '\0';

    for (;;) {
        char c =
            pobierz_znak_blokujaco();

        if (c == '\n' ||
            c == '\r') {

            bufor[pozycja] = '\0';
            return !przepelnienie;
        }

        /*
         * Backspace lub DEL.
         */
        if (c == '\b' ||
            static_cast<uint8_t>(c) == 0x7FU) {

            if (!przepelnienie &&
                pozycja > 0) {

                --pozycja;
                bufor[pozycja] = '\0';

                const int old_row=shell_wiersz;
                shell_dopisz_znak('\b');
                shell_rysuj_zmiane_wejscia(old_row);
                ++shell_perf.key_count;
            }

            continue;
        }

        /*
         * Klawiatura moze emitowac sekwencje ANSI dla klawiszy specjalnych.
         * Nie wprowadzamy surowego ESC do komendy.
         *
         * Aktualny byte-stream BWS nie pozwala niezawodnie rozpoznac calego
         * zdarzenia klawisza, wiec po prostu pomijamy sam ESC.
         */
        if (static_cast<uint8_t>(c) ==
            0x1BU) {
            continue;
        }

        /*
         * Pomijamy pozostale znaki sterujace poza TAB.
         */
        if (static_cast<uint8_t>(c) <
                0x20U &&
            c != '\t') {
            continue;
        }

        if (przepelnienie) {
            continue;
        }

        if (pozycja + 1 >=
            pojemnosc) {

            przepelnienie = true;
            continue;
        }

        bufor[pozycja++] = c;
        bufor[pozycja] = '\0';

        ++shell_perf.key_count;
        wypisz_znak(c);
    }
}

/* =========================================================================
 * 5. PARSER WIERSZA POLECEN
 * ========================================================================= */

struct Argumenty {
    static constexpr size_t MAX_ARG = 5;
    static constexpr size_t MAX_ARG_LEN = 96;

    char dane[MAX_ARG][MAX_ARG_LEN];
    size_t liczba;
};

bool parse_argumenty(
    const char* linia,
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
        wynik->dane[i][0] = '\0';
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

        char* dst =
            wynik->dane[
                wynik->liczba];

        size_t out = 0;
        bool cudzyslow =
            linia[i] == '"';

        if (cudzyslow) {
            ++i;
        }

        while (linia[i] != '\0') {
            const char c =
                linia[i];

            if (cudzyslow) {
                if (c == '"') {
                    ++i;
                    break;
                }
            } else if (jest_biala_spacja(c)) {
                break;
            }

            if (out + 1 >=
                Argumenty::MAX_ARG_LEN) {
                return false;
            }

            dst[out++] = c;
            ++i;
        }

        if (cudzyslow &&
            linia[i - 1] != '"') {
            return false;
        }

        dst[out] = '\0';

        if (out == 0) {
            return false;
        }

        if (!cudzyslow) {
            while (jest_biala_spacja(
                       linia[i])) {
                ++i;
            }
        } else {
            /*
             * Po zamknieciu cudzyslowu wymagamy konca lub separatora.
             */
            if (linia[i] != '\0' &&
                !jest_biala_spacja(
                    linia[i])) {
                return false;
            }

            while (jest_biala_spacja(
                       linia[i])) {
                ++i;
            }
        }

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
             j < MAX_LINIA;
             ++j) {

            historia[i][j] =
                historia[i - 1][j];
        }
    }

    wyzeruj(
        historia[0],
        MAX_LINIA
    );

    (void)kopiuj_tekst(
        historia[0],
        MAX_LINIA,
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
    wypisz(
        "Polecenia Powłoki Bursztynowej:\n"
        "  pomoc                         - pokazuje pomoc\n"
        "  notatnik                      - uruchamia Notatnik\n"
        "  kalkulator                    - uruchamia Kalkulator\n"
        "  przegladarka                  - uruchamia przegladarke Husarz\n"
        "  uruchom <plik.bur>            - uruchamia program .bur\n"
        "  pulpit | wyjdz | exit         - konczy powloke\n"
        "  czas                          - pokazuje czas RTC\n"
        "  system                        - informacje o systemie\n"
        "  pci                           - wyswietla /logi/pci.txt\n"
        "  ping <IPv4|domena>            - DNS + ICMP echo\n"
        "  pobierz <host> <url> <plik>   - pobiera tekst HTTP do BSP2\n"
        "  utworz <plik>                 - tworzy pusty plik\n"
        "  zapisz <plik> <tekst>         - nadpisuje plik tekstem\n"
        "  czytaj <plik>                 - wyswietla plik tekstowy\n"
        "  pliki [katalog]               - listuje katalog\n"
        "  usun <sciezka>                - usuwa plik/pusty katalog\n"
        "  zmien_nazwe <sciezka> <nazwa> - zmienia nazwe\n"
        "  gdzie                         - pokazuje katalog roboczy\n"
        "  historia                      - ostatnie polecenia\n"
        "  cytat                         - pokazuje /cytaty.txt\n"
        "  dzwiek                        - test HDA 880 Hz\n"
        "  czysc                         - wizualnie czysci terminal\n"
        "  losuj                         - rzut kostka (nie kryptograficzny)\n"
        "  pisz <tekst>                  - wypisuje tekst\n"
        "\n"
        "Argument zawierajacy spacje mozna zapisac w cudzyslowie.\n"
    );
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
            "Blad: sciezka jest pusta lub zbyt dluga.\n"
        );
        return;
    }

    wypisz("Uruchamianie procesu: ");
    wypisz(sciezka);
    wypisz("...\n");

    if (!bsh_uruchom_program(
            sciezka)) {

        wypisz(
            "Blad: nie udalo sie uruchomic programu. "
            "Sprawdz plik, prawa PZB i blokade wielu instancji.\n"
        );
    }
}

void polecenie_ping(
    const char* cel
) {
    if (!cel ||
        cel[0] == '\0') {

        wypisz(
            "Skladnia: ping <adres IPv4 lub domena>\n"
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
                "Blad: wyslanie ICMP nie powiodlo sie.\n"
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
            "Blad: nieprawidlowy adres IPv4. "
            "Wymagane sa 4 oktety 0..255.\n"
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
            "Blad: nazwa domeny jest zbyt dluga.\n"
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
            "Blad: nie udalo sie rozwiazac domeny DNS.\n"
        );
        return;
    }

    wypisz("Adres: ");
    wypisz_ipv4(ip);
    wypisz("\nPING...\n");

    if (!bsh_ping_ipv4(ip)) {
        wypisz(
            "Blad: wyslanie ICMP nie powiodlo sie.\n"
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
            "Skladnia: pobierz <domena> <sciezka_HTTP> <plik_docelowy>\n"
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
            "Blad: nieprawidlowa lub zbyt dluga domena.\n"
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
            "Blad: sciezka HTTP musi zaczynac sie od '/' i miescic w limicie.\n"
        );
        return;
    }

    char cel[MAX_SCIEZKA] = {};

    if (!utworz_sciezke_absolutna(
            sciezka_pliku,
            cel,
            sizeof(cel))) {

        wypisz(
            "Blad: nieprawidlowa sciezka pliku docelowego.\n"
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
            "Blad: DNS nie zwrocil adresu.\n"
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
            "Blad ABI: bufor HTTP lezy powyzej 4 GiB. "
            "BWS 29 wymaga jeszcze poprawy sposobu przekazywania wskaznika.\n"
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
            "Blad: pobieranie HTTP nie powiodlo sie.\n"
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
            "Blad: serwer zwrocil pusty bufor lub aktualne ABI nie przekazuje dlugosci odpowiedzi.\n"
        );
        return;
    }

    if (odebrano >=
        HTTP_BUF_SIZE) {

        wypisz(
            "Blad: odpowiedz HTTP nie jest poprawnym tekstem NUL-terminated.\n"
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
            "Blad: odpowiedz pobrano, ale nie udalo sie zapisac jej w BSP2.\n"
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
    wypisz(" bajtow do ");
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
            "Blad: nieprawidlowa sciezka.\n"
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
            "Blad odczytu pliku.\n"
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
            "Blad: nieprawidlowa sciezka katalogu.\n"
        );
        return;
    }

    char bufor[BUF_LISTA] = {};

    if (!bsh_wylistuj_katalog(
            sciezka,
            bufor,
            BUF_LISTA)) {

        wypisz(
            "Blad: katalog nie istnieje lub brak uprawnien.\n"
        );
        return;
    }

    bufor[
        BUF_LISTA - 1U] =
        '\0';

    wypisz("Zawartosc ");
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
    wypisz("Nie mozna odczytac cytatow.\n");
}

void polecenie_pci() {
    char bufor[BUF_PCI] = {};

    if (!czytaj_plik(
            "/logi/pci.txt",
            bufor,
            BUF_PCI - 1U)) {

        wypisz(
            "Blad: brak raportu /logi/pci.txt lub brak uprawnien.\n"
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
                "Skladnia: pomoc\n"
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
                "Skladnia: notatnik\n"
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
                "Skladnia: kalkulator\n"
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
                "Skladnia: przegladarka\n"
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
                "Skladnia: wyjdz\n"
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
                "Skladnia: uruchom <plik.bur>\n"
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
                "Skladnia: ping <IPv4 lub domena>\n"
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
                "Skladnia: pobierz <domena> <sciezka_HTTP> <plik_docelowy>\n"
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
                "Skladnia: czas\n"
            );
            return;
        }

        char bufor[32] = {};

        if (!bsh_pobierz_czas(
                bufor)) {

            wypisz(
                "Blad: RTC/BWS nie zwrocil czasu.\n"
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
                "Skladnia: system\n"
            );
            return;
        }

        wypisz(
            "OS: Bursztyn OS x86_64\n"
            "Jadro: autorskie, monolityczne, paging x86_64 4-level\n"
            "Procesy: Ring 3 + PZB/BZL + prywatne przestrzenie adresowe\n"
            "FS: BSP2 / Bursztynowy System Plikow\n"
            "GUI: autorski kompozytor warstw\n"
            "Siec: E1000, DHCP, ARP, ICMP, DNS, TCP/HTTP\n"
        );
        return;
    }

    if (tekst_rowny(
            cmd,
            "pci")) {

        if (a.liczba != 1) {
            wypisz(
                "Skladnia: pci\n"
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
                "Skladnia: gdzie\n"
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
                "Skladnia: historia\n"
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
            "czysc")) {

        if (a.liczba != 1) {
            wypisz(
                "Skladnia: czysc\n"
            );
            return;
        }

        /*
         * Terminal BWS nie ma jeszcze dedykowanego clear-screen.
         */
        for (size_t i = 0;
             i < 40;
             ++i) {
            wypisz("\n");
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "losuj")) {

        if (a.liczba != 1) {
            wypisz(
                "Skladnia: losuj\n"
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
                "Skladnia: cytat\n"
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
                "Skladnia: pliki [katalog]\n"
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
                "Skladnia: czytaj <plik>\n"
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
                "Skladnia: utworz <plik>\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Blad: nieprawidlowa lub zbyt dluga sciezka.\n"
            );
            return;
        }

        if (utworz(sciezka)) {
            wypisz("Utworzono: ");
            wypisz(sciezka);
            wypisz("\n");
        } else {
            wypisz(
                "Blad: plik istnieje, sciezka jest bledna lub brak uprawnien.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "zapisz")) {

        if (a.liczba != 3) {
            wypisz(
                "Skladnia: zapisz <plik> <tekst>\n"
                "Dla tekstu ze spacjami uzyj cudzyslowu.\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Blad: nieprawidlowa sciezka.\n"
            );
            return;
        }

        size_t dlugosc = 0;

        if (!tekst_zakonczony_w_limicie(
                a.dane[2],
                Argumenty::MAX_ARG_LEN,
                &dlugosc)) {

            wypisz(
                "Blad: tekst jest za dlugi.\n"
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
                "Blad zapisu lub brak PRAWO_PLIKI_ZAPISZ.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "usun")) {

        if (a.liczba != 2) {
            wypisz(
                "Skladnia: usun <sciezka>\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Blad: nieprawidlowa sciezka.\n"
            );
            return;
        }

        if (tekst_rowny(
                sciezka,
                "/")) {

            wypisz(
                "Blad: nie mozna usunac korzenia '/'.\n"
            );
            return;
        }

        if (bsh_usun_twor(
                sciezka)) {

            wypisz("Usunieto: ");
            wypisz(sciezka);
            wypisz("\n");
        } else {
            wypisz(
                "Blad: obiekt nie istnieje, katalog nie jest pusty lub brak uprawnien.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "zmien_nazwe")) {

        if (a.liczba != 3) {
            wypisz(
                "Skladnia: zmien_nazwe <sciezka> <nowa_nazwa>\n"
            );
            return;
        }

        char sciezka[MAX_SCIEZKA] = {};

        if (!utworz_sciezke_absolutna(
                a.dane[1],
                sciezka,
                sizeof(sciezka))) {

            wypisz(
                "Blad: nieprawidlowa sciezka.\n"
            );
            return;
        }

        /*
         * Nowa nazwa nie moze zawierac slash, bo BWS 8 zmienia nazwe
         * wewnatrz tego samego katalogu.
         */
        if (a.dane[2][0] == '\0') {
            wypisz(
                "Blad: pusta nazwa.\n"
            );
            return;
        }

        for (size_t i = 0;
             a.dane[2][i] != '\0';
             ++i) {

            if (a.dane[2][i] == '/' ||
                a.dane[2][i] == '\\') {

                wypisz(
                    "Blad: nowa nazwa nie moze zawierac '/' ani '\\\\'.\n"
                );
                return;
            }
        }

        if (bsh_zmien_nazwe(
                sciezka,
                a.dane[2])) {

            wypisz(
                "Zmieniono nazwe.\n"
            );
        } else {
            wypisz(
                "Blad: zmiana nazwy nie powiodla sie.\n"
            );
        }

        return;
    }

    if (tekst_rowny(
            cmd,
            "dzwiek")) {

        if (a.liczba != 1) {
            wypisz(
                "Skladnia: dzwiek\n"
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

    if (tekst_rowny(
            cmd,
            "pisz")) {

        if (a.liczba != 2) {
            wypisz(
                "Skladnia: pisz <tekst>\n"
                "Dla tekstu ze spacjami uzyj cudzyslowu.\n"
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
    gui_pobierz_rozdzielczosc(&shell_screen_w, &shell_screen_h);
    if (bws_utworz_warstwe(shell_x, shell_y, shell_w, shell_h, 10) < 0)
        gui_zakoncz_aplikacje();
    gui_ustaw_przejecie_myszy(true);
    shell_rysuj_terminal();

    wypisz(
        "\n"
        "==================================================\n"
        " Powłoka Bursztynowa bsh v2.3\n"
        " Ring 3 / BWS / BSP2 / PZB\n"
        "==================================================\n"
        "Wpisz 'pomoc', aby zobaczyc polecenia.\n"
    );

    char linia[MAX_LINIA] = {};

    for (;;) {
        wypisz("\npowloka> ");

        const bool cala_linia =
            pobierz_linie(
                linia,
                sizeof(linia)
            );

        wypisz("\n");

        if (!cala_linia) {
            wypisz(
                "Blad: polecenie przekracza limit 127 bajtow i zostalo odrzucone.\n"
            );
            continue;
        }

        if (linia[0] == '\0') {
            continue;
        }

        Argumenty argumenty{};

        if (!parse_argumenty(
                linia,
                &argumenty)) {

            wypisz(
                "Blad skladni: za duzo argumentow, argument jest za dlugi "
                "lub brakuje zamykajacego cudzyslowu.\n"
            );
            continue;
        }

        if (argumenty.liczba == 0) {
            continue;
        }

        dodaj_do_historii(
            linia
        );

        wykonaj_polecenie(
            argumenty
        );
    }
}
