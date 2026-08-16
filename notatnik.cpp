/*
 * Bursztyn OS - Notatnik
 *
 * Aplikacja Ring 3 korzystajaca z bursztyn_gui.
 *
 * Model dokumentu:
 *   - maks. 50 linii,
 *   - maks. 127 bajtow UTF-8 + NUL na linie,
 *   - maks. 6399 bajtow przy wczytywaniu pliku przez aktualne API,
 *   - zapis zachowuje puste linie pomiedzy tekstem,
 *   - edycja jest insert-mode: znak wstawia sie w srodku linii,
 *   - Enter dzieli linie, Backspace na poczatku laczy z poprzednia.
 *
 * Bezpieczenstwo:
 *   - kazda operacja na buforach ma jawny limit,
 *   - sciezki sa ograniczone do 127 bajtow i musza byc absolutne,
 *   - CR/LF i znaki kontrolne nie sa wpuszczane do sciezki,
 *   - parser pliku jest ograniczony rozmiarem temp_buf i obsluguje CRLF,
 *   - nie przekazujemy surowych wskaznikow Ring 3 poza wrappery GUI,
 *   - proces konczy sie przez gui_zakoncz_aplikacje(),
 *   - aplikacja tworzy wlasna warstwe compositor'a i przesuwa ja przez
 *     bws_przesun_warstwe(), zamiast wykonywac syscall 34 recznie.
 *
 * Ograniczenie aktualnego API plikowego:
 * czytaj_plik() zwraca tylko bool, a nie liczbe bajtow. Dlatego przed
 * odczytem zerujemy caly temp_buf i parser nigdy nie czyta poza jego koniec.
 */

#include "bursztyn_gui.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. NAGLOWEK .BUR
 * ========================================================================= */

struct NaglowekBur {
    uint8_t magia[4];

    uint64_t punkt_wejscia;

    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;

    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

static_assert(
    sizeof(NaglowekBur) == 60U,
    "Naglowek .bur musi miec dokladnie 60 bajtow"
);

static_assert(
    offsetof(NaglowekBur, punkt_wejscia) == 0x04U,
    "Nieprawidlowy layout .bur"
);

static_assert(
    offsetof(NaglowekBur, dane_wirtualny) == 0x34U,
    "Nieprawidlowy layout .bur"
);

extern "C" __attribute__((noreturn))
void _start();

/*
 * Zachowujemy obecny layout Notatnika:
 *
 *   header  0x0000
 *   text    file 0x1000, VA 0x601000, 32 KiB
 *   data    file 0x9000, VA 0x609000, 128 KiB
 *
 * Linker script Notatnika MUSI miec identyczny layout.
 */
extern "C" {

__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(
        &_start
    ),

    UINT64_C(0x1000),
    UINT64_C(0x8000),
    UINT64_C(0x601000),

    UINT64_C(0x9000),
    UINT64_C(0x20000),
    UINT64_C(0x609000)
};

}

/* =========================================================================
 * 2. LIMITY
 * ========================================================================= */

namespace {

constexpr int LICZBA_LINII =
    50;

constexpr int BAJTY_LINII =
    128;

constexpr int MAKS_TEKST_LINII =
    BAJTY_LINII - 1;

constexpr size_t BUFOR_PLIKU =
    6400U;

constexpr size_t MAKS_ODCZYT_PLIKU =
    BUFOR_PLIKU - 1U;

constexpr int BAJTY_SCIEZKI =
    128;

constexpr int MAKS_SCIEZKA =
    BAJTY_SCIEZKI - 1;

constexpr int BAJTY_STATUSU =
    96;

constexpr int LINE_H =
    16;

constexpr int TEXT_Y_OFFSET =
    50;

constexpr int STATUS_H =
    22;

constexpr int TASKBAR_H =
    40;

constexpr int Z_ORDER_NOTATNIKA =
    10;

constexpr int MIN_WIN_W =
    360;

constexpr int MIN_WIN_H =
    240;

constexpr int DOMYSLNY_WIN_X =
    150;

constexpr int DOMYSLNY_WIN_Y =
    80;

constexpr int DOMYSLNY_WIN_W =
    600;

constexpr int DOMYSLNY_WIN_H =
    400;

constexpr int PRZYBLIZONA_SZER_ZNAKU =
    9;

constexpr uint32_t KOLOR_BURSZTYN =
    0x00E58A00U;

constexpr uint32_t KOLOR_BURSZTYN_JASNY =
    0x00FFBF00U;

constexpr uint32_t KOLOR_TLO =
    0x001A0B00U;

constexpr uint32_t KOLOR_MENU =
    0x00301500U;

constexpr uint32_t KOLOR_MENU_OTWARTE =
    0x004A2500U;

constexpr uint32_t KOLOR_BIALY =
    0x00FFFFFFU;

constexpr uint32_t KOLOR_SZARY =
    0x00D1D5DBU;

constexpr uint32_t KOLOR_CZERWONY =
    0x00AA0000U;

/* =========================================================================
 * 3. STAN DOKUMENTU
 * ========================================================================= */

char dokument[
    LICZBA_LINII
][
    BAJTY_LINII
] __attribute__((section(".data"))) = {};

char liniowy_bufor[
    BUFOR_PLIKU
] __attribute__((section(".data"))) = {};

char temp_buf[
    BUFOR_PLIKU
] __attribute__((section(".data"))) = {};

char pasek_statusu[
    BAJTY_STATUSU
] __attribute__((section(".data"))) =
    "Gotowy.";

char sciezka_input[
    BAJTY_SCIEZKI
] __attribute__((section(".data"))) = {};

char aktualna_sciezka[
    BAJTY_SCIEZKI
] __attribute__((section(".data"))) =
    "/plik.txt";

int liczba_linii =
    1;

int cur_r =
    0;

int cur_c =
    0;

int scroll_y =
    0;

int scroll_x =
    0;

bool dokument_zmieniony =
    false;

/* =========================================================================
 * 4. STAN UI
 * ========================================================================= */

enum class TrybPracy : uint8_t {
    EDYCJA_TEKSTU = 0,
    WPROWADZANIE_SCIEZKI_ZAPIS,
    WPROWADZANIE_SCIEZKI_OTWORZ
};

TrybPracy tryb =
    TrybPracy::EDYCJA_TEKSTU;

int sciezka_len =
    0;

int WIN_X =
    DOMYSLNY_WIN_X;

int WIN_Y =
    DOMYSLNY_WIN_Y;

int WIN_W =
    DOMYSLNY_WIN_W;

int WIN_H =
    DOMYSLNY_WIN_H;

int old_win_x =
    DOMYSLNY_WIN_X;

int old_win_y =
    DOMYSLNY_WIN_Y;

int old_win_w =
    DOMYSLNY_WIN_W;

int old_win_h =
    DOMYSLNY_WIN_H;

int screen_w =
    1024;

int screen_h =
    768;

bool dragging =
    false;

int drag_off_x =
    0;

int drag_off_y =
    0;

bool menu_plik_otwarte =
    false;

bool menu_ustawienia_otwarte =
    false;

bool okno_pomoc_widoczne =
    false;

bool zmaksymalizowane =
    false;

bool aplikacja_zminimalizowana =
    false;

/* =========================================================================
 * 5. PROSTE OPERACJE PAMIECIOWE / TEKST
 * ========================================================================= */

void wyzeruj(
    void* ptr,
    size_t n
) {
    if (!ptr) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(
            ptr
        );

    for (size_t i = 0;
         i < n;
         ++i) {

        p[i] =
            0;
    }
}

size_t dlugosc_limit(
    const char* tekst,
    size_t limit
) {
    if (!tekst) {
        return limit;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            return i;
        }
    }

    return limit;
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

    const size_t len =
        dlugosc_limit(
            zrodlo,
            pojemnosc
        );

    if (len >=
        pojemnosc) {

        cel[0] =
            '\0';

        return false;
    }

    for (size_t i = 0;
         i <= len;
         ++i) {

        cel[i] =
            zrodlo[i];
    }

    return true;
}

void ustaw_status(
    const char* tekst
) {
    if (!kopiuj_tekst(
            pasek_statusu,
            sizeof(pasek_statusu),
            tekst ? tekst : "")) {

        (void)kopiuj_tekst(
            pasek_statusu,
            sizeof(pasek_statusu),
            "Blad statusu."
        );
    }
}

int min_int(
    int a,
    int b
) {
    return
        a < b
            ? a
            : b;
}

int max_int(
    int a,
    int b
) {
    return
        a > b
            ? a
            : b;
}

int clamp_int(
    int v,
    int min_v,
    int max_v
) {
    if (v < min_v) {
        return min_v;
    }

    if (v > max_v) {
        return max_v;
    }

    return v;
}

/* =========================================================================
 * 6. UTF-8 / KURSOR
 * ========================================================================= */

bool utf8_kontynuacja(
    unsigned char c
) {
    return
        (c &
         0xC0U) ==
        0x80U;
}

int dlugosc_linii(
    int r
) {
    if (r < 0 ||
        r >= LICZBA_LINII) {

        return 0;
    }

    return
        static_cast<int>(
            dlugosc_limit(
                dokument[r],
                BAJTY_LINII
            )
        );
}

int utf8_poprzedni_start(
    const char* tekst,
    int pozycja
) {
    if (!tekst ||
        pozycja <= 0) {

        return 0;
    }

    int p =
        pozycja -
        1;

    while (p > 0 &&
           utf8_kontynuacja(
               static_cast<unsigned char>(
                   tekst[p]
               ))) {

        --p;
    }

    return p;
}

int utf8_nastepny_start(
    const char* tekst,
    int pozycja,
    int len
) {
    if (!tekst ||
        pozycja >= len) {

        return len;
    }

    int p =
        pozycja +
        1;

    while (p < len &&
           utf8_kontynuacja(
               static_cast<unsigned char>(
                   tekst[p]
               ))) {

        ++p;
    }

    return p;
}

int utf8_przytnij_do_granicy(
    const char* tekst,
    int pozycja
) {
    if (!tekst ||
        pozycja <= 0) {

        return 0;
    }

    int len =
        static_cast<int>(
            dlugosc_limit(
                tekst,
                BAJTY_LINII
            )
        );

    if (pozycja >= len) {
        return len;
    }

    int p =
        pozycja;

    while (p > 0 &&
           utf8_kontynuacja(
               static_cast<unsigned char>(
                   tekst[p]
               ))) {

        --p;
    }

    return p;
}

int utf8_liczba_znakow_do(
    const char* tekst,
    int bajty
) {
    if (!tekst ||
        bajty <= 0) {

        return 0;
    }

    int znaki =
        0;

    for (int i = 0;
         i < bajty &&
         tekst[i] != '\0';
         ++i) {

        if (!utf8_kontynuacja(
                static_cast<unsigned char>(
                    tekst[i]
                ))) {

            ++znaki;
        }
    }

    return znaki;
}

int utf8_bajt_dla_kolumny(
    const char* tekst,
    int kolumna
) {
    if (!tekst ||
        kolumna <= 0) {

        return 0;
    }

    int len =
        static_cast<int>(
            dlugosc_limit(
                tekst,
                BAJTY_LINII
            )
        );

    int p =
        0;

    int znak =
        0;

    while (p < len &&
           znak < kolumna) {

        p =
            utf8_nastepny_start(
                tekst,
                p,
                len
            );

        ++znak;
    }

    return p;
}

int kolumna_kursora() {
    return
        utf8_liczba_znakow_do(
            dokument[cur_r],
            cur_c
        );
}

/* =========================================================================
 * 7. DOKUMENT - RESET / NORMALIZACJA
 * ========================================================================= */

void wyczysc_dokument() {
    wyzeruj(
        dokument,
        sizeof(dokument)
    );

    liczba_linii =
        1;

    cur_r =
        0;

    cur_c =
        0;

    scroll_y =
        0;

    scroll_x =
        0;

    dokument_zmieniony =
        false;
}

void normalizuj_kursor() {
    liczba_linii =
        clamp_int(
            liczba_linii,
            1,
            LICZBA_LINII
        );

    cur_r =
        clamp_int(
            cur_r,
            0,
            liczba_linii - 1
        );

    const int len =
        dlugosc_linii(
            cur_r
        );

    cur_c =
        clamp_int(
            cur_c,
            0,
            len
        );

    cur_c =
        utf8_przytnij_do_granicy(
            dokument[cur_r],
            cur_c
        );

    scroll_y =
        clamp_int(
            scroll_y,
            0,
            liczba_linii - 1
        );

    if (scroll_x < 0) {
        scroll_x =
            0;
    }
}

/* =========================================================================
 * 8. EDYCJA - WSTAWIANIE / USUWANIE
 * ========================================================================= */

bool wstaw_bajt(
    unsigned char znak
) {
    normalizuj_kursor();

    char* linia =
        dokument[cur_r];

    const int len =
        dlugosc_linii(
            cur_r
        );

    if (len >=
        MAKS_TEKST_LINII) {

        ustaw_status(
            "Linia jest pelna."
        );

        return false;
    }

    for (int i = len;
         i >= cur_c;
         --i) {

        linia[i + 1] =
            linia[i];
    }

    linia[cur_c] =
        static_cast<char>(
            znak
        );

    ++cur_c;

    dokument_zmieniony =
        true;

    return true;
}

bool usun_poprzedni_znak() {
    normalizuj_kursor();

    if (cur_c > 0) {
        char* linia =
            dokument[cur_r];

        const int len =
            dlugosc_linii(
                cur_r
            );

        const int start =
            utf8_poprzedni_start(
                linia,
                cur_c
            );

        const int ile =
            cur_c -
            start;

        for (int i = start;
             i + ile <= len;
             ++i) {

            linia[i] =
                linia[i + ile];
        }

        cur_c =
            start;

        dokument_zmieniony =
            true;

        return true;
    }

    if (cur_r <= 0) {
        return false;
    }

    const int poprzedni_len =
        dlugosc_linii(
            cur_r - 1
        );

    const int obecny_len =
        dlugosc_linii(
            cur_r
        );

    if (poprzedni_len +
            obecny_len >
        MAKS_TEKST_LINII) {

        ustaw_status(
            "Nie mozna polaczyc: poprzednia linia jest za dluga."
        );

        return false;
    }

    char* poprzednia =
        dokument[
            cur_r - 1
        ];

    char* obecna =
        dokument[
            cur_r
        ];

    for (int i = 0;
         i <= obecny_len;
         ++i) {

        poprzednia[
            poprzedni_len + i] =
            obecna[i];
    }

    for (int r = cur_r;
         r <
            liczba_linii - 1;
         ++r) {

        for (int c = 0;
             c <
                BAJTY_LINII;
             ++c) {

            dokument[r][c] =
                dokument[r + 1][c];
        }
    }

    wyzeruj(
        dokument[
            liczba_linii - 1
        ],
        BAJTY_LINII
    );

    --liczba_linii;

    --cur_r;

    cur_c =
        poprzedni_len;

    dokument_zmieniony =
        true;

    return true;
}

bool usun_nastepny_znak() {
    normalizuj_kursor();

    char* linia =
        dokument[cur_r];

    const int len =
        dlugosc_linii(
            cur_r
        );

    if (cur_c < len) {
        const int koniec =
            utf8_nastepny_start(
                linia,
                cur_c,
                len
            );

        const int ile =
            koniec -
            cur_c;

        for (int i = cur_c;
             i + ile <= len;
             ++i) {

            linia[i] =
                linia[i + ile];
        }

        dokument_zmieniony =
            true;

        return true;
    }

    if (cur_r + 1 >=
        liczba_linii) {

        return false;
    }

    const int nastepny_len =
        dlugosc_linii(
            cur_r + 1
        );

    if (len +
            nastepny_len >
        MAKS_TEKST_LINII) {

        ustaw_status(
            "Nie mozna polaczyc: linia jest za dluga."
        );

        return false;
    }

    for (int i = 0;
         i <= nastepny_len;
         ++i) {

        linia[len + i] =
            dokument[
                cur_r + 1
            ][i];
    }

    for (int r = cur_r + 1;
         r <
            liczba_linii - 1;
         ++r) {

        for (int c = 0;
             c <
                BAJTY_LINII;
             ++c) {

            dokument[r][c] =
                dokument[r + 1][c];
        }
    }

    wyzeruj(
        dokument[
            liczba_linii - 1
        ],
        BAJTY_LINII
    );

    --liczba_linii;

    dokument_zmieniony =
        true;

    return true;
}

bool podziel_linie() {
    normalizuj_kursor();

    if (liczba_linii >=
        LICZBA_LINII) {

        ustaw_status(
            "Osiagnieto limit 50 linii."
        );

        return false;
    }

    /*
     * Najpierw robimy miejsce na nowy wiersz.
     */
    for (int r = liczba_linii;
         r > cur_r + 1;
         --r) {

        for (int c = 0;
             c <
                BAJTY_LINII;
             ++c) {

            dokument[r][c] =
                dokument[r - 1][c];
        }
    }

    char* stara =
        dokument[cur_r];

    char* nowa =
        dokument[
            cur_r + 1
        ];

    wyzeruj(
        nowa,
        BAJTY_LINII
    );

    const int len =
        dlugosc_linii(
            cur_r
        );

    const int tail =
        len -
        cur_c;

    for (int i = 0;
         i < tail;
         ++i) {

        nowa[i] =
            stara[
                cur_c + i
            ];
    }

    nowa[tail] =
        '\0';

    stara[cur_c] =
        '\0';

    ++liczba_linii;

    ++cur_r;

    cur_c =
        0;

    dokument_zmieniony =
        true;

    return true;
}

/* =========================================================================
 * 9. RUCH KURSORA
 * ========================================================================= */

void kursor_lewo() {
    normalizuj_kursor();

    if (cur_c > 0) {
        cur_c =
            utf8_poprzedni_start(
                dokument[cur_r],
                cur_c
            );

        return;
    }

    if (cur_r > 0) {
        --cur_r;

        cur_c =
            dlugosc_linii(
                cur_r
            );
    }
}

void kursor_prawo() {
    normalizuj_kursor();

    const int len =
        dlugosc_linii(
            cur_r
        );

    if (cur_c < len) {
        cur_c =
            utf8_nastepny_start(
                dokument[cur_r],
                cur_c,
                len
            );

        return;
    }

    if (cur_r + 1 <
        liczba_linii) {

        ++cur_r;
        cur_c = 0;
    }
}

void kursor_gora() {
    normalizuj_kursor();

    if (cur_r <= 0) {
        return;
    }

    const int kolumna =
        kolumna_kursora();

    --cur_r;

    cur_c =
        utf8_bajt_dla_kolumny(
            dokument[cur_r],
            kolumna
        );
}

void kursor_dol() {
    normalizuj_kursor();

    if (cur_r + 1 >=
        liczba_linii) {

        return;
    }

    const int kolumna =
        kolumna_kursora();

    ++cur_r;

    cur_c =
        utf8_bajt_dla_kolumny(
            dokument[cur_r],
            kolumna
        );
}

void kursor_home() {
    cur_c =
        0;
}

void kursor_end() {
    cur_c =
        dlugosc_linii(
            cur_r
        );
}

/* =========================================================================
 * 10. SCROLL
 * ========================================================================= */

int liczba_widocznych_linii() {
    const int dostepne =
        WIN_H -
        TEXT_Y_OFFSET -
        STATUS_H -
        4;

    if (dostepne <= 0) {
        return 1;
    }

    const int n =
        dostepne /
        LINE_H;

    return
        max_int(
            1,
            n
        );
}

int liczba_widocznych_kolumn() {
    const int dostepne =
        WIN_W -
        24;

    if (dostepne <=
        PRZYBLIZONA_SZER_ZNAKU) {

        return 1;
    }

    return
        max_int(
            1,
            dostepne /
            PRZYBLIZONA_SZER_ZNAKU
        );
}

void dopasuj_scroll_do_kursora() {
    normalizuj_kursor();

    const int widoczne_linie =
        liczba_widocznych_linii();

    if (cur_r <
        scroll_y) {

        scroll_y =
            cur_r;
    }

    if (cur_r >=
        scroll_y +
            widoczne_linie) {

        scroll_y =
            cur_r -
            widoczne_linie +
            1;
    }

    const int kolumna =
        kolumna_kursora();

    const int widoczne_kolumny =
        liczba_widocznych_kolumn();

    if (kolumna <
        scroll_x) {

        scroll_x =
            kolumna;
    }

    if (kolumna >=
        scroll_x +
            widoczne_kolumny) {

        scroll_x =
            kolumna -
            widoczne_kolumny +
            1;
    }

    if (scroll_x < 0) {
        scroll_x = 0;
    }
}

/* =========================================================================
 * 11. WIDOCZNY FRAGMENT LINII
 * ========================================================================= */

void zbuduj_widoczna_linie(
    int r,
    char* out,
    size_t pojemnosc
) {
    if (!out ||
        pojemnosc == 0) {

        return;
    }

    out[0] =
        '\0';

    if (r < 0 ||
        r >= liczba_linii) {

        return;
    }

    const char* linia =
        dokument[r];

    const int start =
        utf8_bajt_dla_kolumny(
            linia,
            scroll_x
        );

    const int len =
        dlugosc_linii(
            r
        );

    const int max_kolumn =
        liczba_widocznych_kolumn();

    int p =
        start;

    int znaki =
        0;

    size_t out_i =
        0;

    while (p < len &&
           znaki <
               max_kolumn &&
           out_i + 1U <
               pojemnosc) {

        const int next =
            utf8_nastepny_start(
                linia,
                p,
                len
            );

        const int bytes =
            next -
            p;

        if (bytes <= 0 ||
            out_i +
                static_cast<size_t>(
                    bytes) +
                1U >
                pojemnosc) {

            break;
        }

        for (int i = p;
             i < next;
             ++i) {

            out[out_i++] =
                linia[i];
        }

        p =
            next;

        ++znaki;
    }

    out[out_i] =
        '\0';
}

/* =========================================================================
 * 12. SCIEZKI
 * ========================================================================= */

bool sciezka_poprawna(
    const char* sciezka
) {
    if (!sciezka) {
        return false;
    }

    const size_t len =
        dlugosc_limit(
            sciezka,
            BAJTY_SCIEZKI
        );

    if (len == 0 ||
        len >
            static_cast<size_t>(
                MAKS_SCIEZKA) ||
        sciezka[0] != '/') {

        return false;
    }

    for (size_t i = 0;
         i < len;
         ++i) {

        const unsigned char c =
            static_cast<unsigned char>(
                sciezka[i]
            );

        if (c < 0x20U ||
            c == 0x7FU) {

            return false;
        }
    }

    return true;
}

void rozpocznij_wprowadzanie_sciezki(
    TrybPracy nowy_tryb,
    bool z_aktualna
) {
    tryb =
        nowy_tryb;

    sciezka_len =
        0;

    sciezka_input[0] =
        '\0';

    if (z_aktualna) {
        if (kopiuj_tekst(
                sciezka_input,
                sizeof(sciezka_input),
                aktualna_sciezka)) {

            sciezka_len =
                static_cast<int>(
                    dlugosc_limit(
                        sciezka_input,
                        sizeof(sciezka_input)
                    )
                );
        }
    }

    menu_plik_otwarte =
        false;

    menu_ustawienia_otwarte =
        false;
}

/* =========================================================================
 * 13. ZAPIS DOKUMENTU
 * ========================================================================= */

bool serializuj_dokument(
    uint32_t* wynik_dlugosc
) {
    if (!wynik_dlugosc) {
        return false;
    }

    size_t p =
        0;

    for (int r = 0;
         r <
            liczba_linii;
         ++r) {

        const size_t len =
            dlugosc_limit(
                dokument[r],
                BAJTY_LINII
            );

        if (len >=
            BAJTY_LINII) {

            return false;
        }

        if (len >
            MAKS_ODCZYT_PLIKU -
                p) {

            return false;
        }

        for (size_t i = 0;
             i < len;
             ++i) {

            liniowy_bufor[p++] =
                dokument[r][i];
        }

        /*
         * Newline umieszczamy POMIEDZY liniami, niezaleznie od tego czy
         * linia jest pusta. Stara wersja tracila puste linie.
         */
        if (r + 1 <
            liczba_linii) {

            if (p >=
                MAKS_ODCZYT_PLIKU) {

                return false;
            }

            liniowy_bufor[p++] =
                '\n';
        }
    }

    liniowy_bufor[p] =
        '\0';

    *wynik_dlugosc =
        static_cast<uint32_t>(
            p
        );

    return true;
}

bool zapisz_do_pliku(
    const char* sciezka_docelowa
) {
    if (!sciezka_poprawna(
            sciezka_docelowa)) {

        ustaw_status(
            "Blad: nieprawidlowa sciezka."
        );

        return false;
    }

    uint32_t dlugosc =
        0;

    if (!serializuj_dokument(
            &dlugosc)) {

        ustaw_status(
            "Blad: dokument jest za duzy do zapisu."
        );

        return false;
    }

    ustaw_status(
        "Zapisywanie..."
    );

    /*
     * utworz() moze zwrocic false, gdy plik juz istnieje. Ostateczny
     * wynik zapisania danych okresla zapisz_plik().
     */
    (void)utworz(
        sciezka_docelowa
    );

    if (!zapisz_plik(
            sciezka_docelowa,
            liniowy_bufor,
            dlugosc)) {

        ustaw_status(
            "Blad: zapis nie powiodl sie."
        );

        return false;
    }

    (void)kopiuj_tekst(
        aktualna_sciezka,
        sizeof(aktualna_sciezka),
        sciezka_docelowa
    );

    dokument_zmieniony =
        false;

    ustaw_status(
        "Zapisano pomyslnie."
    );

    return true;
}

/* =========================================================================
 * 14. ODCZYT DOKUMENTU
 * ========================================================================= */

bool parsuj_plik_do_dokumentu(
    bool* uciety
) {
    if (!uciety) {
        return false;
    }

    *uciety =
        false;

    wyzeruj(
        dokument,
        sizeof(dokument)
    );

    int r =
        0;

    int c =
        0;

    liczba_linii =
        1;

    bool poprzedni_cr =
        false;

    for (size_t i = 0;
         i <
            MAKS_ODCZYT_PLIKU;
         ++i) {

        const unsigned char ch =
            static_cast<unsigned char>(
                temp_buf[i]
            );

        if (ch == 0) {
            break;
        }

        if (ch == '\r') {
            /*
             * CRLF i samotne CR traktujemy jak koniec linii.
             */
            if (r + 1 >=
                LICZBA_LINII) {

                *uciety =
                    true;

                break;
            }

            ++r;
            c = 0;

            liczba_linii =
                r + 1;

            poprzedni_cr =
                true;

            continue;
        }

        if (ch == '\n') {
            if (poprzedni_cr) {
                poprzedni_cr =
                    false;

                continue;
            }

            if (r + 1 >=
                LICZBA_LINII) {

                *uciety =
                    true;

                break;
            }

            ++r;
            c = 0;

            liczba_linii =
                r + 1;

            continue;
        }

        poprzedni_cr =
            false;

        if (c >=
            MAKS_TEKST_LINII) {

            /*
             * Nie nadpisujemy kolejnej linii. Pomijamy reszte za dlugiej
             * linii do jej newline i raportujemy truncation.
             */
            *uciety =
                true;

            continue;
        }

        dokument[r][c++] =
            static_cast<char>(
                ch
            );

        dokument[r][c] =
            '\0';
    }

    /*
     * Jezeli ostatni bezpieczny bajt jest niezerowy, plik mogl byc wiekszy
     * niz aktualne API potrafi zwrocic. Nie czytamy dalej.
     */
    if (temp_buf[
            MAKS_ODCZYT_PLIKU - 1U] !=
        '\0') {

        *uciety =
            true;
    }

    cur_r =
        0;

    cur_c =
        0;

    scroll_y =
        0;

    scroll_x =
        0;

    dokument_zmieniony =
        false;

    normalizuj_kursor();

    return true;
}

bool otworz_z_pliku(
    const char* sciezka_zrodlowa
) {
    if (!sciezka_poprawna(
            sciezka_zrodlowa)) {

        ustaw_status(
            "Blad: nieprawidlowa sciezka."
        );

        return false;
    }

    ustaw_status(
        "Otwieranie pliku..."
    );

    wyzeruj(
        temp_buf,
        sizeof(temp_buf)
    );

    if (!czytaj_plik(
            sciezka_zrodlowa,
            temp_buf,
            static_cast<uint32_t>(
                MAKS_ODCZYT_PLIKU
            ))) {

        ustaw_status(
            "Blad: nie mozna odczytac pliku."
        );

        return false;
    }

    /*
     * NUL gwarantowany nawet gdy syscall wypelnil caly dozwolony obszar.
     */
    temp_buf[
        MAKS_ODCZYT_PLIKU] =
        '\0';

    bool uciety =
        false;

    if (!parsuj_plik_do_dokumentu(
            &uciety)) {

        ustaw_status(
            "Blad parsera dokumentu."
        );

        return false;
    }

    (void)kopiuj_tekst(
        aktualna_sciezka,
        sizeof(aktualna_sciezka),
        sciezka_zrodlowa
    );

    if (uciety) {
        ustaw_status(
            "Wczytano, ale dokument zostal uciety do limitow Notatnika."
        );
    } else {
        ustaw_status(
            "Wczytano plik pomyslnie."
        );
    }

    return true;
}

/* =========================================================================
 * 15. GEOMETRIA OKNA / WARSTWA
 * ========================================================================= */

void ogranicz_geometrie_okna() {
    if (screen_w < MIN_WIN_W) {
        screen_w =
            MIN_WIN_W;
    }

    if (screen_h <
        MIN_WIN_H +
            TASKBAR_H) {

        screen_h =
            MIN_WIN_H +
            TASKBAR_H;
    }

    WIN_W =
        clamp_int(
            WIN_W,
            MIN_WIN_W,
            screen_w
        );

    WIN_H =
        clamp_int(
            WIN_H,
            MIN_WIN_H,
            screen_h -
                TASKBAR_H
        );

    WIN_X =
        clamp_int(
            WIN_X,
            0,
            max_int(
                0,
                screen_w -
                    WIN_W
            )
        );

    WIN_Y =
        clamp_int(
            WIN_Y,
            0,
            max_int(
                0,
                screen_h -
                    TASKBAR_H -
                    WIN_H
            )
        );
}

bool utworz_lub_zmien_warstwe() {
    ogranicz_geometrie_okna();

    return
        bws_utworz_warstwe(
            WIN_X,
            WIN_Y,
            WIN_W,
            WIN_H,
            Z_ORDER_NOTATNIKA
        ) >= 0;
}

bool ustaw_maksymalizacje(
    bool stan
) {
    if (stan ==
        zmaksymalizowane) {

        return true;
    }

    const int poprzedni_x =
        WIN_X;

    const int poprzedni_y =
        WIN_Y;

    const int poprzedni_w =
        WIN_W;

    const int poprzedni_h =
        WIN_H;

    if (stan) {
        old_win_x =
            WIN_X;

        old_win_y =
            WIN_Y;

        old_win_w =
            WIN_W;

        old_win_h =
            WIN_H;

        WIN_X =
            0;

        WIN_Y =
            0;

        WIN_W =
            screen_w;

        WIN_H =
            screen_h -
            TASKBAR_H;
    } else {
        WIN_X =
            old_win_x;

        WIN_Y =
            old_win_y;

        WIN_W =
            old_win_w;

        WIN_H =
            old_win_h;
    }

    ogranicz_geometrie_okna();

    if (!utworz_lub_zmien_warstwe()) {
        WIN_X =
            poprzedni_x;

        WIN_Y =
            poprzedni_y;

        WIN_W =
            poprzedni_w;

        WIN_H =
            poprzedni_h;

        (void)utworz_lub_zmien_warstwe();

        ustaw_status(
            "Blad: nie mozna zmienic rozmiaru warstwy."
        );

        return false;
    }

    zmaksymalizowane =
        stan;

    dopasuj_scroll_do_kursora();

    return true;
}

void ustaw_minimalizacje(
    bool stan
) {
    if (aplikacja_zminimalizowana ==
        stan) {

        return;
    }

    aplikacja_zminimalizowana =
        stan;

    dragging =
        false;

    menu_plik_otwarte =
        false;

    menu_ustawienia_otwarte =
        false;

    if (stan) {
        /*
         * Dla warstwy procesu gui_odswiez_pulpit() czysci powierzchnie
         * aplikacji do przezroczystosci.
         */
        gui_odswiez_pulpit();
        gui_odswiez();
    }
}

/* =========================================================================
 * 16. RYSOWANIE
 * ========================================================================= */

void rysuj_ramke(
    int x,
    int y,
    int w,
    int h,
    uint32_t kolor
) {
    if (w <= 1 ||
        h <= 1) {

        return;
    }

    gui_rysuj_prostokat(
        x,
        y,
        w,
        1,
        kolor
    );

    gui_rysuj_prostokat(
        x,
        y + h - 1,
        w,
        1,
        kolor
    );

    gui_rysuj_prostokat(
        x,
        y,
        1,
        h,
        kolor
    );

    gui_rysuj_prostokat(
        x + w - 1,
        y,
        1,
        h,
        kolor
    );
}

void rysuj_status() {
    gui_rysuj_prostokat(
        WIN_X + 2,
        WIN_Y + WIN_H - STATUS_H,
        WIN_W - 4,
        STATUS_H - 2,
        KOLOR_TLO
    );

    if (tryb ==
        TrybPracy::WPROWADZANIE_SCIEZKI_ZAPIS) {

        gui_wypisz_tekst_kolor(
            WIN_X + 8,
            WIN_Y + WIN_H - 18,
            KOLOR_BURSZTYN,
            "Zapisz jako:"
        );

        gui_wypisz_tekst_kolor(
            WIN_X + 120,
            WIN_Y + WIN_H - 18,
            KOLOR_BIALY,
            sciezka_input
        );

        return;
    }

    if (tryb ==
        TrybPracy::WPROWADZANIE_SCIEZKI_OTWORZ) {

        gui_wypisz_tekst_kolor(
            WIN_X + 8,
            WIN_Y + WIN_H - 18,
            KOLOR_BURSZTYN,
            "Otworz plik:"
        );

        gui_wypisz_tekst_kolor(
            WIN_X + 120,
            WIN_Y + WIN_H - 18,
            KOLOR_BIALY,
            sciezka_input
        );

        return;
    }

    gui_wypisz_tekst_kolor(
        WIN_X + 8,
        WIN_Y + WIN_H - 18,
        KOLOR_SZARY,
        pasek_statusu
    );

    if (dokument_zmieniony) {
        gui_wypisz_tekst_kolor(
            WIN_X + WIN_W - 25,
            WIN_Y + WIN_H - 18,
            KOLOR_BURSZTYN_JASNY,
            "*"
        );
    }
}

void rysuj_menu_plik() {
    if (!menu_plik_otwarte) {
        return;
    }

    const int x =
        WIN_X + 5;

    const int y =
        WIN_Y + 46;

    const int w =
        150;

    const int h =
        88;

    gui_rysuj_prostokat(
        x,
        y,
        w,
        h,
        KOLOR_MENU_OTWARTE
    );

    rysuj_ramke(
        x,
        y,
        w,
        h,
        KOLOR_BURSZTYN
    );

    gui_wypisz_tekst_kolor(
        x + 6,
        y + 5,
        KOLOR_BIALY,
        "Nowy            Ctrl+N"
    );

    gui_wypisz_tekst_kolor(
        x + 6,
        y + 21,
        KOLOR_BIALY,
        "Otworz...       Ctrl+O"
    );

    gui_wypisz_tekst_kolor(
        x + 6,
        y + 37,
        KOLOR_BIALY,
        "Zapisz          Ctrl+S"
    );

    gui_wypisz_tekst_kolor(
        x + 6,
        y + 53,
        KOLOR_BIALY,
        "Zapisz jako..."
    );

    gui_wypisz_tekst_kolor(
        x + 6,
        y + 69,
        KOLOR_BIALY,
        "Zamknij"
    );
}

void rysuj_menu_ustawienia() {
    if (!menu_ustawienia_otwarte) {
        return;
    }

    const int x =
        WIN_X + 40;

    const int y =
        WIN_Y + 46;

    const int w =
        190;

    const int h =
        48;

    gui_rysuj_prostokat(
        x,
        y,
        w,
        h,
        KOLOR_MENU_OTWARTE
    );

    rysuj_ramke(
        x,
        y,
        w,
        h,
        KOLOR_BURSZTYN
    );

    gui_wypisz_tekst_kolor(
        x + 6,
        y + 7,
        KOLOR_SZARY,
        "Motyw: Bursztyn"
    );

    gui_wypisz_tekst_kolor(
        x + 6,
        y + 25,
        KOLOR_SZARY,
        "Kodowanie: UTF-8"
    );
}

void rysuj_pomoc() {
    if (!okno_pomoc_widoczne) {
        return;
    }

    const int w =
        min_int(
            360,
            WIN_W - 20
        );

    const int h =
        min_int(
            220,
            WIN_H - 20
        );

    const int px =
        WIN_X +
        (WIN_W - w) /
        2;

    const int py =
        WIN_Y +
        (WIN_H - h) /
        2;

    gui_rysuj_prostokat(
        px,
        py,
        w,
        h,
        0x00280F00U
    );

    gui_rysuj_prostokat(
        px,
        py,
        w,
        24,
        KOLOR_BURSZTYN
    );

    rysuj_ramke(
        px,
        py,
        w,
        h,
        KOLOR_BURSZTYN
    );

    gui_wypisz_tekst_kolor(
        px + 8,
        py + 4,
        KOLOR_TLO,
        "O programie"
    );

    gui_wypisz_tekst_kolor(
        px + 20,
        py + 42,
        KOLOR_BIALY,
        "Notatnik - Bursztyn OS"
    );

    gui_wypisz_tekst_kolor(
        px + 20,
        py + 64,
        KOLOR_SZARY,
        "Ring 3 / UTF-8 / bezpieczne bufory"
    );

    gui_wypisz_tekst_kolor(
        px + 20,
        py + 86,
        KOLOR_SZARY,
        "Ctrl+S zapis, Ctrl+O otworz, Ctrl+N nowy"
    );

    gui_wypisz_tekst_kolor(
        px + 20,
        py + 108,
        KOLOR_SZARY,
        "Strzalki/Home/End/Delete obsluguja edycje"
    );

    gui_wypisz_tekst_kolor(
        px + 20,
        py + 140,
        KOLOR_BURSZTYN_JASNY,
        "Bursztyn OS"
    );

    RysujPrzycisk(
        px +
            (w - 80) /
            2,
        py + h - 36,
        80,
        24,
        KOLOR_BURSZTYN,
        KOLOR_TLO,
        "  OK"
    );
}

void rysuj_tekst_dokumentu() {
    const int max_lines =
        liczba_widocznych_linii();

    char widoczna[
        BAJTY_LINII
    ] = {};

    for (int i = 0;
         i <
            max_lines;
         ++i) {

        const int actual_r =
            scroll_y +
            i;

        const int y =
            WIN_Y +
            TEXT_Y_OFFSET +
            i *
                LINE_H;

        gui_wyczyscz_obszar(
            WIN_X + 8,
            y,
            WIN_W - 16,
            LINE_H
        );

        if (actual_r >=
            liczba_linii) {

            continue;
        }

        zbuduj_widoczna_linie(
            actual_r,
            widoczna,
            sizeof(widoczna)
        );

        if (widoczna[0] != '\0') {
            gui_wypisz_tekst(
                WIN_X + 8,
                y,
                widoczna
            );
        }

        if (actual_r ==
                cur_r &&
            tryb ==
                TrybPracy::EDYCJA_TEKSTU &&
            !okno_pomoc_widoczne) {

            const int kolumna =
                kolumna_kursora();

            const int ekran_kolumna =
                kolumna -
                scroll_x;

            if (ekran_kolumna >= 0 &&
                ekran_kolumna <
                    liczba_widocznych_kolumn()) {

                gui_wypisz_tekst_kolor(
                    WIN_X +
                        8 +
                        ekran_kolumna *
                            PRZYBLIZONA_SZER_ZNAKU,
                    y,
                    KOLOR_BURSZTYN_JASNY,
                    "_"
                );
            }
        }
    }
}

void rysuj_interfejs(
    bool wyczysc_warstwe
) {
    if (aplikacja_zminimalizowana) {
        gui_odswiez();
        return;
    }

    if (wyczysc_warstwe) {
        gui_odswiez_pulpit();
    }

    gui_rysuj_okno(
        WIN_X,
        WIN_Y,
        WIN_W,
        WIN_H,
        dokument_zmieniony
            ? "Notatnik *"
            : "Notatnik"
    );

    RysujPrzycisk(
        WIN_X + WIN_W - 74,
        WIN_Y + 4,
        20,
        20,
        KOLOR_BURSZTYN,
        KOLOR_TLO,
        "-"
    );

    RysujPrzycisk(
        WIN_X + WIN_W - 50,
        WIN_Y + 4,
        20,
        20,
        KOLOR_BURSZTYN,
        KOLOR_TLO,
        zmaksymalizowane
            ? "v"
            : "^"
    );

    RysujPrzycisk(
        WIN_X + WIN_W - 26,
        WIN_Y + 4,
        20,
        20,
        KOLOR_CZERWONY,
        KOLOR_BIALY,
        "X"
    );

    gui_rysuj_prostokat(
        WIN_X + 2,
        WIN_Y + 26,
        WIN_W - 4,
        20,
        KOLOR_MENU
    );

    gui_wypisz_tekst_kolor(
        WIN_X + 10,
        WIN_Y + 28,
        KOLOR_BURSZTYN_JASNY,
        "Plik"
    );

    gui_wypisz_tekst_kolor(
        WIN_X + 48,
        WIN_Y + 28,
        KOLOR_BURSZTYN_JASNY,
        "Ustawienia"
    );

    gui_wypisz_tekst_kolor(
        WIN_X + 132,
        WIN_Y + 28,
        KOLOR_BURSZTYN_JASNY,
        "Pomoc"
    );

    rysuj_tekst_dokumentu();
    rysuj_status();
    rysuj_menu_plik();
    rysuj_menu_ustawienia();
    rysuj_pomoc();

    gui_odswiez();
}

/* =========================================================================
 * 17. MYSZ - KURSOR W TEKSCIE
 * ========================================================================= */

bool mysz_w_prostokacie(
    int mx,
    int my,
    int x,
    int y,
    int w,
    int h
) {
    return
        mx >= x &&
        my >= y &&
        mx < x + w &&
        my < y + h;
}

void ustaw_kursor_z_myszy(
    int mx,
    int my
) {
    const int text_y =
        WIN_Y +
        TEXT_Y_OFFSET;

    const int i =
        (my -
         text_y) /
        LINE_H;

    int r =
        scroll_y +
        i;

    r =
        clamp_int(
            r,
            0,
            liczba_linii - 1
        );

    const int x =
        max_int(
            0,
            mx -
                (WIN_X + 8)
        );

    const int kolumna =
        scroll_x +
        x /
            PRZYBLIZONA_SZER_ZNAKU;

    cur_r =
        r;

    cur_c =
        utf8_bajt_dla_kolumny(
            dokument[cur_r],
            kolumna
        );

    dopasuj_scroll_do_kursora();
}

/* =========================================================================
 * 18. OPERACJE MENU
 * ========================================================================= */

void nowy_dokument() {
    wyczysc_dokument();

    (void)kopiuj_tekst(
        aktualna_sciezka,
        sizeof(aktualna_sciezka),
        "/plik.txt"
    );

    ustaw_status(
        "Nowy dokument."
    );
}

void zapisz_aktualny() {
    (void)zapisz_do_pliku(
        aktualna_sciezka
    );
}

void zatwierdz_sciezke() {
    if (!sciezka_poprawna(
            sciezka_input)) {

        ustaw_status(
            "Sciezka musi byc absolutna i bez znakow kontrolnych."
        );

        return;
    }

    if (!kopiuj_tekst(
            aktualna_sciezka,
            sizeof(aktualna_sciezka),
            sciezka_input)) {

        ustaw_status(
            "Sciezka jest za dluga."
        );

        return;
    }

    const TrybPracy poprzedni =
        tryb;

    tryb =
        TrybPracy::EDYCJA_TEKSTU;

    if (poprzedni ==
        TrybPracy::WPROWADZANIE_SCIEZKI_ZAPIS) {

        (void)zapisz_do_pliku(
            aktualna_sciezka
        );
    } else if (poprzedni ==
               TrybPracy::WPROWADZANIE_SCIEZKI_OTWORZ) {

        (void)otworz_z_pliku(
            aktualna_sciezka
        );
    }
}

/* =========================================================================
 * 19. ANSI ESCAPE
 * ========================================================================= */

enum class StanANSI : uint8_t {
    BRAK = 0,
    ESC,
    CSI,
    CSI_3
};

void obsluz_ansi(
    StanANSI* stan,
    char c
) {
    if (!stan) {
        return;
    }

    switch (*stan) {
        case StanANSI::BRAK:
            if (c == '\x1B') {
                *stan =
                    StanANSI::ESC;
            }
            break;

        case StanANSI::ESC:
            if (c == '[') {
                *stan =
                    StanANSI::CSI;
            } else {
                *stan =
                    StanANSI::BRAK;
            }
            break;

        case StanANSI::CSI:
            switch (c) {
                case 'A':
                    kursor_gora();
                    *stan = StanANSI::BRAK;
                    break;

                case 'B':
                    kursor_dol();
                    *stan = StanANSI::BRAK;
                    break;

                case 'C':
                    kursor_prawo();
                    *stan = StanANSI::BRAK;
                    break;

                case 'D':
                    kursor_lewo();
                    *stan = StanANSI::BRAK;
                    break;

                case 'H':
                    kursor_home();
                    *stan = StanANSI::BRAK;
                    break;

                case 'F':
                    kursor_end();
                    *stan = StanANSI::BRAK;
                    break;

                case '3':
                    *stan = StanANSI::CSI_3;
                    break;

                default:
                    *stan = StanANSI::BRAK;
                    break;
            }
            break;

        case StanANSI::CSI_3:
            if (c == '~') {
                (void)usun_nastepny_znak();
            }

            *stan =
                StanANSI::BRAK;
            break;
    }

    dopasuj_scroll_do_kursora();
}

/* =========================================================================
 * 20. KLAWIATURA
 * ========================================================================= */

void obsluz_sciezke(
    char c
) {
    const unsigned char uc =
        static_cast<unsigned char>(
            c
        );

    if (c == '\n' ||
        c == '\r') {

        zatwierdz_sciezke();
        return;
    }

    if (c == '\x1B') {
        tryb =
            TrybPracy::EDYCJA_TEKSTU;

        ustaw_status(
            "Anulowano operacje plikowa."
        );

        return;
    }

    if (c == '\b' ||
        uc == 0x7FU) {

        if (sciezka_len > 0) {
            --sciezka_len;

            while (sciezka_len > 0 &&
                   utf8_kontynuacja(
                       static_cast<unsigned char>(
                           sciezka_input[
                               sciezka_len]
                       ))) {

                --sciezka_len;
            }

            sciezka_input[
                sciezka_len] =
                '\0';
        }

        return;
    }

    if (uc < 0x20U) {
        return;
    }

    if (sciezka_len >=
        MAKS_SCIEZKA) {

        ustaw_status(
            "Sciezka jest za dluga."
        );

        return;
    }

    sciezka_input[
        sciezka_len++] =
        c;

    sciezka_input[
        sciezka_len] =
        '\0';
}

void obsluz_edycje(
    StanANSI* ansi,
    char c
) {
    if (!ansi) {
        return;
    }

    const unsigned char uc =
        static_cast<unsigned char>(
            c
        );

    /*
     * Ctrl+N / Ctrl+O / Ctrl+S.
     */
    if (*ansi ==
            StanANSI::BRAK &&
        uc == 0x0EU) {

        nowy_dokument();
        return;
    }

    if (*ansi ==
            StanANSI::BRAK &&
        uc == 0x0FU) {

        rozpocznij_wprowadzanie_sciezki(
            TrybPracy::WPROWADZANIE_SCIEZKI_OTWORZ,
            false
        );

        return;
    }

    if (*ansi ==
            StanANSI::BRAK &&
        uc == 0x13U) {

        zapisz_aktualny();
        return;
    }

    if (*ansi !=
            StanANSI::BRAK ||
        c == '\x1B') {

        obsluz_ansi(
            ansi,
            c
        );

        return;
    }

    if (c == '\n' ||
        c == '\r') {

        (void)podziel_linie();

        dopasuj_scroll_do_kursora();

        return;
    }

    if (c == '\b' ||
        uc == 0x7FU) {

        (void)usun_poprzedni_znak();

        dopasuj_scroll_do_kursora();

        return;
    }

    if (uc >= 0x20U) {
        if (wstaw_bajt(
                uc)) {

            ustaw_status(
                "Edycja..."
            );
        }

        dopasuj_scroll_do_kursora();
    }
}

/* =========================================================================
 * 21. KLIK MENU PLIK
 * ========================================================================= */

bool obsluz_klik_menu_plik(
    int mx,
    int my,
    bool* wyjdz
) {
    if (!menu_plik_otwarte ||
        !wyjdz) {

        return false;
    }

    const int x =
        WIN_X + 5;

    const int y =
        WIN_Y + 46;

    if (!mysz_w_prostokacie(
            mx,
            my,
            x,
            y,
            150,
            88)) {

        return false;
    }

    const int item =
        (my -
         (y + 4)) /
        16;

    menu_plik_otwarte =
        false;

    switch (item) {
        case 0:
            nowy_dokument();
            break;

        case 1:
            rozpocznij_wprowadzanie_sciezki(
                TrybPracy::WPROWADZANIE_SCIEZKI_OTWORZ,
                false
            );
            break;

        case 2:
            zapisz_aktualny();
            break;

        case 3:
            rozpocznij_wprowadzanie_sciezki(
                TrybPracy::WPROWADZANIE_SCIEZKI_ZAPIS,
                true
            );
            break;

        case 4:
            *wyjdz =
                true;
            break;

        default:
            break;
    }

    return true;
}

/* =========================================================================
 * 22. MYSZ
 * ========================================================================= */

void obsluz_klik(
    int mx,
    int my,
    bool* wyjdz,
    bool* redraw,
    bool* pelne
) {
    if (!wyjdz ||
        !redraw ||
        !pelne) {

        return;
    }

    /*
     * Gdy aplikacja jest zminimalizowana, obecny menedzer pulpitu ma
     * przypiety przycisk Notatnika w okolicy x=100..132. Pozwalamy klikowi
     * przywrocic istniejaca instancje zamiast rysowac taskbar z aplikacji.
     */
    if (aplikacja_zminimalizowana) {
        if (my >=
                screen_h -
                    TASKBAR_H &&
            mx >= 100 &&
            mx <= 140) {

            ustaw_minimalizacje(
                false
            );

            *redraw =
                true;

            *pelne =
                true;
        }

        return;
    }

    if (okno_pomoc_widoczne) {
        const int w =
            min_int(
                360,
                WIN_W - 20
            );

        const int h =
            min_int(
                220,
                WIN_H - 20
            );

        const int px =
            WIN_X +
            (WIN_W - w) /
            2;

        const int py =
            WIN_Y +
            (WIN_H - h) /
            2;

        if (mysz_w_prostokacie(
                mx,
                my,
                px +
                    (w - 80) /
                    2,
                py + h - 36,
                80,
                24)) {

            okno_pomoc_widoczne =
                false;

            *pelne =
                true;
        }

        *redraw =
            true;

        return;
    }

    /*
     * Otwarte menu ma pierwszenstwo przed normalna zawartoscia okna.
     */
    if (obsluz_klik_menu_plik(
            mx,
            my,
            wyjdz)) {

        *redraw =
            true;

        *pelne =
            true;

        return;
    }

    if (menu_ustawienia_otwarte &&
        mysz_w_prostokacie(
            mx,
            my,
            WIN_X + 40,
            WIN_Y + 46,
            190,
            48)) {

        menu_ustawienia_otwarte =
            false;

        ustaw_status(
            "Ustawienia sa obecnie tylko informacyjne."
        );

        *redraw =
            true;

        *pelne =
            true;

        return;
    }

    if (!mysz_w_prostokacie(
            mx,
            my,
            WIN_X,
            WIN_Y,
            WIN_W,
            WIN_H)) {

        menu_plik_otwarte =
            false;

        menu_ustawienia_otwarte =
            false;

        *redraw =
            true;

        return;
    }

    /*
     * Pasek tytulu.
     */
    if (my <
        WIN_Y + 26) {

        if (mysz_w_prostokacie(
                mx,
                my,
                WIN_X + WIN_W - 74,
                WIN_Y + 4,
                20,
                20)) {

            ustaw_minimalizacje(
                true
            );

            *redraw =
                true;

            *pelne =
                true;

            return;
        }

        if (mysz_w_prostokacie(
                mx,
                my,
                WIN_X + WIN_W - 50,
                WIN_Y + 4,
                20,
                20)) {

            if (ustaw_maksymalizacje(
                    !zmaksymalizowane)) {

                *redraw =
                    true;

                *pelne =
                    true;
            }

            return;
        }

        if (mysz_w_prostokacie(
                mx,
                my,
                WIN_X + WIN_W - 26,
                WIN_Y + 4,
                20,
                20)) {

            *wyjdz =
                true;

            return;
        }

        if (!zmaksymalizowane) {
            dragging =
                true;

            drag_off_x =
                mx -
                WIN_X;

            drag_off_y =
                my -
                WIN_Y;

            menu_plik_otwarte =
                false;

            menu_ustawienia_otwarte =
                false;
        }

        return;
    }

    /*
     * Pasek menu.
     */
    if (my >=
            WIN_Y + 26 &&
        my <
            WIN_Y + 46) {

        if (mx <
            WIN_X + 45) {

            menu_plik_otwarte =
                !menu_plik_otwarte;

            menu_ustawienia_otwarte =
                false;
        } else if (mx <
                   WIN_X + 128) {

            menu_ustawienia_otwarte =
                !menu_ustawienia_otwarte;

            menu_plik_otwarte =
                false;
        } else if (mx <
                   WIN_X + 180) {

            okno_pomoc_widoczne =
                true;

            menu_plik_otwarte =
                false;

            menu_ustawienia_otwarte =
                false;
        } else {
            menu_plik_otwarte =
                false;

            menu_ustawienia_otwarte =
                false;
        }

        *redraw =
            true;

        *pelne =
            true;

        return;
    }

    /*
     * Klik w obszar tekstu ustawia kursor.
     */
    const int text_h =
        liczba_widocznych_linii() *
        LINE_H;

    if (tryb ==
            TrybPracy::EDYCJA_TEKSTU &&
        mysz_w_prostokacie(
            mx,
            my,
            WIN_X + 8,
            WIN_Y + TEXT_Y_OFFSET,
            WIN_W - 16,
            text_h)) {

        ustaw_kursor_z_myszy(
            mx,
            my
        );

        menu_plik_otwarte =
            false;

        menu_ustawienia_otwarte =
            false;

        *redraw =
            true;

        return;
    }

    menu_plik_otwarte =
        false;

    menu_ustawienia_otwarte =
        false;

    *redraw =
        true;
}

/* =========================================================================
 * 23. DRAGGING
 * ========================================================================= */

void aktualizuj_dragging(
    int mx,
    int my
) {
    if (!dragging ||
        aplikacja_zminimalizowana ||
        zmaksymalizowane) {

        return;
    }

    int nowy_x =
        mx -
        drag_off_x;

    int nowy_y =
        my -
        drag_off_y;

    nowy_x =
        clamp_int(
            nowy_x,
            0,
            max_int(
                0,
                screen_w -
                    WIN_W
            )
        );

    nowy_y =
        clamp_int(
            nowy_y,
            0,
            max_int(
                0,
                screen_h -
                    TASKBAR_H -
                    WIN_H
            )
        );

    if (nowy_x ==
            WIN_X &&
        nowy_y ==
            WIN_Y) {

        return;
    }

    WIN_X =
        nowy_x;

    WIN_Y =
        nowy_y;

    bws_przesun_warstwe(
        WIN_X,
        WIN_Y
    );

    /*
     * Warstwa zawiera juz narysowana zawartosc; compositor moze ja
     * przesunac bez ponownego renderowania wszystkich widgetow.
     */
    gui_odswiez();
}

/* =========================================================================
 * 24. START
 * ========================================================================= */

} // namespace

extern "C" __attribute__((noreturn))
void _start() {
    wyczysc_dokument();

    tryb =
        TrybPracy::EDYCJA_TEKSTU;

    sciezka_len =
        0;

    sciezka_input[0] =
        '\0';

    dragging =
        false;

    menu_plik_otwarte =
        false;

    menu_ustawienia_otwarte =
        false;

    okno_pomoc_widoczne =
        false;

    zmaksymalizowane =
        false;

    aplikacja_zminimalizowana =
        false;

    gui_pobierz_rozdzielczosc(
        &screen_w,
        &screen_h
    );

    if (screen_w <= 0 ||
        screen_h <=
            TASKBAR_H) {

        gui_zakoncz_aplikacje();
    }

    ogranicz_geometrie_okna();

    /*
     * Notatnik jest normalnym oknem aplikacji, wiec tworzy wlasna warstwe
     * z_order=10. Pulpit menedzera ma z_order=0.
     */
    if (!utworz_lub_zmien_warstwe()) {
        gui_zakoncz_aplikacje();
    }

    gui_ustaw_przejecie_myszy(
        true
    );

    StanANSI ansi =
        StanANSI::BRAK;

    uint8_t poprzednie_przyciski =
        0;

    int old_mx =
        -1;

    int old_my =
        -1;

    bool wyjdz =
        false;

    bool redraw =
        true;

    bool pelne =
        true;

    while (!wyjdz) {
        int mx =
            0;

        int my =
            0;

        uint8_t mb =
            0;

        gui_pobierz_mysz(
            &mx,
            &my,
            &mb
        );

        const bool lewy =
            (mb &
             0x01U) != 0;

        const bool poprzedni_lewy =
            (poprzednie_przyciski &
             0x01U) != 0;

        const bool klik =
            lewy &&
            !poprzedni_lewy;

        const bool puszczenie =
            !lewy &&
            poprzedni_lewy;

        if (klik) {
            obsluz_klik(
                mx,
                my,
                &wyjdz,
                &redraw,
                &pelne
            );
        }

        if (dragging &&
            lewy) {

            aktualizuj_dragging(
                mx,
                my
            );
        }

        if (puszczenie) {
            if (dragging) {
                dragging =
                    false;

                redraw =
                    true;
            }
        }

        if ((mx != old_mx ||
             my != old_my) &&
            !dragging) {

            /*
             * Kursor myszy jest rysowany przez compositor, wiec samo
             * przesuniecie nie wymaga pelnego redraw UI.
             */
            gui_odswiez();
        }

        poprzednie_przyciski =
            mb;

        old_mx =
            mx;

        old_my =
            my;

        const char c =
            pobierz_znak();

        if (c != 0 &&
            !aplikacja_zminimalizowana) {

            redraw =
                true;

            menu_plik_otwarte =
                false;

            menu_ustawienia_otwarte =
                false;

            if (okno_pomoc_widoczne) {
                if (c == '\n' ||
                    c == '\r' ||
                    c == '\x1B') {

                    okno_pomoc_widoczne =
                        false;

                    pelne =
                        true;
                }
            } else if (tryb !=
                       TrybPracy::EDYCJA_TEKSTU) {

                obsluz_sciezke(
                    c
                );
            } else {
                obsluz_edycje(
                    &ansi,
                    c
                );
            }
        }

        if (redraw &&
            !dragging) {

            rysuj_interfejs(
                pelne
            );

            redraw =
                false;

            pelne =
                false;
        }

        asm volatile(
            "pause"
            :
            :
            : "memory"
        );
    }

    gui_zakoncz_aplikacje();
}
