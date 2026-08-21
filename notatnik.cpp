/*
 * Bursztyn OS - Notatnik
 *
 * Aplikacja Ring 3 korzystajaca z bursztyn_gui.
 *
 * Model dokumentu:
 *   - jeden bounded bufor 64 KiB,
 *   - linie maja zmienna dlugosc i sa rozdzielone NUL-em wewnatrz bufora,
 *   - nie istnieje osobny maly limit dlugosci pojedynczej linii,
 *   - zapis zachowuje puste linie pomiedzy tekstem,
 *   - edycja jest insert-mode: znak wstawia sie w srodku linii,
 *   - Enter dzieli linie, Backspace na poczatku laczy z poprzednia.
 *
 * Bezpieczenstwo:
 *   - kazda operacja na buforach ma jawny limit,
 *   - sciezki sa ograniczone do 127 bajtow i musza byc absolutne,
 *   - CR/LF i znaki kontrolne nie sa wpuszczane do sciezki,
 *   - parser pliku pracuje in-place i obsluguje CRLF,
 *   - nie przekazujemy surowych wskaznikow Ring 3 poza wrappery GUI,
 *   - proces konczy sie przez gui_zakoncz_aplikacje(),
 *   - aplikacja tworzy wlasna warstwe compositor'a i przesuwa ja przez
 *     bws_przesun_warstwe(), zamiast wykonywac syscall 34 recznie.
 *
 * Rozmiar jest pobierany addytywnym wywolaniem BWS 44 przed odczytem, wiec
 * load i save korzystaja z tego samego limitu calego dokumentu.
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

constexpr size_t DOKUMENT_POJEMNOSC =
    64U * 1024U;

constexpr size_t MAKS_ROZMIAR_PLIKU =
    DOKUMENT_POJEMNOSC - 1U;

/* To limit chwilowego fragmentu viewportu, a nie limit linii dokumentu. */
constexpr size_t WIDOCZNA_LINIA_POJEMNOSC =
    1024U;

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

#ifndef BURSZTYN_DEBUG_GUI_PERF
#define BURSZTYN_DEBUG_GUI_PERF 0
#endif

/* =========================================================================
 * 3. STAN DOKUMENTU
 * ========================================================================= */

char dokument[
    DOKUMENT_POJEMNOSC
] __attribute__((section(".bss"), aligned(16))) = {};

/* Liczy wszystkie bajty modelu, lacznie z koncowym separatorem NUL. */
size_t dokument_uzyte = 1U;

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
    "/uzytkownicy/plik.txt";

int liczba_linii =
    1;

int cur_r =
    0;

int cur_c =
    0;

int scroll_y =
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

struct ProstokatEdytora {
    int x;
    int y;
    int szerokosc;
    int wysokosc;
};

struct WierszEkranowy {
    int linia;
    int poczatek;
    int koniec;
};

struct StanLayoutEdycji {
    bool poprawny;
    int scroll;
    int wiersz_kursora;
    int pierwszy_wiersz_linii;
    int wiersze_linii;
    int linia;
    int kursor_bajt;
    int poprzedni_kodpunkt;
    int dlugosc;
    int poczatek_segmentu;
    int koniec_segmentu;
    int wszystkie_linie;
};

struct ZakresRedraw {
    bool pelny_viewport;
    int pierwszy_wiersz;
    int ostatni_wiersz;
};

#if BURSZTYN_DEBUG_GUI_PERF
volatile uint64_t notatnik_key = 0;
volatile uint64_t notatnik_redraw_pixels = 0;
volatile uint64_t notatnik_full_viewport_redraw = 0;
volatile uint64_t notatnik_visual_row_redraw = 0;
#endif

int cache_szerokosci_ascii[128] = {};

/*
 * Jedyne zrodlo geometrii pola tekstowego. Helper jest celowo bez cache:
 * kazde renderowanie, hit-test i przeliczenie soft wrapu korzysta z
 * aktualnych WIN_W/WIN_H, rowniez bezposrednio po maximize/restore.
 */
ProstokatEdytora aktualny_prostokat_edytora() {
    ProstokatEdytora wynik{};
    wynik.x = WIN_X + 8;
    wynik.y = WIN_Y + TEXT_Y_OFFSET;
    wynik.szerokosc = WIN_W - 16;
    if (wynik.szerokosc < 1) wynik.szerokosc = 1;
    wynik.wysokosc = WIN_H - TEXT_Y_OFFSET - STATUS_H - 4;
    if (wynik.wysokosc < 1) wynik.wysokosc = 1;
    return wynik;
}

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

bool przesun_dokument(size_t cel, size_t zrodlo, size_t ile) {
    if (cel > DOKUMENT_POJEMNOSC || zrodlo > dokument_uzyte ||
        ile > dokument_uzyte - zrodlo ||
        ile > DOKUMENT_POJEMNOSC - cel) {
        return false;
    }
    if (ile == 0 || cel == zrodlo) return true;
    if (cel < zrodlo) {
        for (size_t i = 0; i < ile; ++i) dokument[cel + i] = dokument[zrodlo + i];
    } else {
        for (size_t i = ile; i > 0; --i) dokument[cel + i - 1U] = dokument[zrodlo + i - 1U];
    }
    return true;
}

size_t offset_linii(int r) {
    if (r < 0 || r >= liczba_linii || dokument_uzyte == 0 ||
        dokument_uzyte > DOKUMENT_POJEMNOSC) return dokument_uzyte;
    size_t p = 0;
    for (int linia = 0; linia < r; ++linia) {
        while (p < dokument_uzyte && dokument[p] != '\0') ++p;
        if (p >= dokument_uzyte) return dokument_uzyte;
        ++p;
    }
    return p < dokument_uzyte ? p : dokument_uzyte;
}

char* pobierz_linie(int r) {
    const size_t p = offset_linii(r);
    return p < dokument_uzyte ? dokument + p : nullptr;
}

const char* pobierz_linie_const(int r) {
    return pobierz_linie(r);
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
    const size_t p = offset_linii(r);
    if (p >= dokument_uzyte) return 0;
    const size_t len = dlugosc_limit(dokument + p, dokument_uzyte - p);
    if (len >= dokument_uzyte - p || len > static_cast<size_t>(INT32_MAX)) return 0;
    return static_cast<int>(len);
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
    int pozycja,
    int len
) {
    if (!tekst ||
        pozycja <= 0) {

        return 0;
    }

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

int szerokosc_fragmentu(
    const char* tekst,
    int poczatek,
    int koniec,
    int len
);

bool pobierz_wiersz_ekranowy(
    int indeks,
    WierszEkranowy* wynik
);

int wiersz_ekranowy_kursora();

int bajt_dla_piksela(
    const char* linia,
    int poczatek,
    int koniec,
    int len,
    int x
);

/* =========================================================================
 * 7. DOKUMENT - RESET / NORMALIZACJA
 * ========================================================================= */

void wyczysc_dokument() {
    wyzeruj(
        dokument,
        sizeof(dokument)
    );

    dokument_uzyte = 1U;

    liczba_linii =
        1;

    cur_r =
        0;

    cur_c =
        0;

    scroll_y =
        0;

    dokument_zmieniony =
        false;
}

void normalizuj_kursor() {
    liczba_linii =
        clamp_int(
            liczba_linii,
            1,
            static_cast<int>(DOKUMENT_POJEMNOSC)
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

    const char* linia = pobierz_linie_const(cur_r);
    cur_c = linia ? utf8_przytnij_do_granicy(linia,cur_c,len) : 0;

    if (scroll_y < 0) scroll_y = 0;
}

/* =========================================================================
 * 8. EDYCJA - WSTAWIANIE / USUWANIE
 * ========================================================================= */

bool wstaw_bajt(
    unsigned char znak
) {
    normalizuj_kursor();
    if (dokument_uzyte >= DOKUMENT_POJEMNOSC) {
        ustaw_status("Dokument osiagnal limit 64 KiB.");
        return false;
    }
    size_t wymagane=1U;
    if(znak>=0xC2U&&znak<=0xDFU)wymagane=2U;
    else if(znak>=0xE0U&&znak<=0xEFU)wymagane=3U;
    else if(znak>=0xF0U&&znak<=0xF4U)wymagane=4U;
    if(wymagane>DOKUMENT_POJEMNOSC-dokument_uzyte){
        ustaw_status("Brak miejsca na caly znak UTF-8.");return false;
    }
    const size_t linia = offset_linii(cur_r);
    if (linia >= dokument_uzyte || cur_c < 0 ||
        static_cast<size_t>(cur_c) > dokument_uzyte - linia) return false;
    const size_t pozycja = linia + static_cast<size_t>(cur_c);
    const size_t tail = dokument_uzyte - pozycja;
    if (!przesun_dokument(pozycja + 1U,pozycja,tail)) return false;
    dokument[pozycja] = static_cast<char>(znak);
    ++dokument_uzyte;
    ++cur_c;
    dokument_zmieniony = true;
    return true;
}

bool usun_poprzedni_znak() {
    normalizuj_kursor();

    if (cur_c > 0) {
        char* linia = pobierz_linie(cur_r);
        if (!linia) return false;
        const int start = utf8_poprzedni_start(linia,cur_c);
        const size_t ile = static_cast<size_t>(cur_c-start);
        const size_t pozycja = offset_linii(cur_r)+static_cast<size_t>(start);
        const size_t zrodlo = pozycja+ile;
        if (zrodlo > dokument_uzyte ||
            !przesun_dokument(pozycja,zrodlo,dokument_uzyte-zrodlo)) return false;
        dokument_uzyte-=ile;
        cur_c=start;
        dokument_zmieniony=true;
        return true;
    }

    if (cur_r <= 0) {
        return false;
    }

    const int poprzedni_len =
        dlugosc_linii(
            cur_r - 1
        );

    const size_t obecna = offset_linii(cur_r);
    if (obecna == 0 || obecna >= dokument_uzyte) return false;
    /* Usuwamy wylacznie separator NUL miedzy liniami. */
    if (!przesun_dokument(obecna-1U,obecna,dokument_uzyte-obecna)) return false;
    --dokument_uzyte;
    --liczba_linii;
    --cur_r;
    cur_c=poprzedni_len;
    dokument_zmieniony=true;
    return true;
}

bool usun_nastepny_znak() {
    normalizuj_kursor();

    char* linia=pobierz_linie(cur_r);
    if(!linia)return false;
    const int len=dlugosc_linii(cur_r);

    if (cur_c < len) {
        const int koniec=utf8_nastepny_start(linia,cur_c,len);
        const size_t ile=static_cast<size_t>(koniec-cur_c);
        const size_t pozycja=offset_linii(cur_r)+static_cast<size_t>(cur_c);
        const size_t zrodlo=pozycja+ile;
        if(zrodlo>dokument_uzyte||!przesun_dokument(pozycja,zrodlo,dokument_uzyte-zrodlo))return false;
        dokument_uzyte-=ile;dokument_zmieniony=true;
        return true;
    }

    if (cur_r + 1 >=
        liczba_linii) {

        return false;
    }

    const size_t separator=offset_linii(cur_r)+static_cast<size_t>(len);
    if(separator+1U>dokument_uzyte||
       !przesun_dokument(separator,separator+1U,dokument_uzyte-separator-1U))return false;
    --dokument_uzyte;--liczba_linii;dokument_zmieniony=true;
    return true;
}

bool podziel_linie() {
    normalizuj_kursor();

    if (dokument_uzyte >= DOKUMENT_POJEMNOSC) {
        ustaw_status("Dokument osiagnal limit 64 KiB.");
        return false;
    }
    const size_t poczatek=offset_linii(cur_r);
    if(poczatek>=dokument_uzyte||cur_c<0)return false;
    const size_t pozycja=poczatek+static_cast<size_t>(cur_c);
    if(pozycja>=dokument_uzyte||
       !przesun_dokument(pozycja+1U,pozycja,dokument_uzyte-pozycja))return false;
    dokument[pozycja]='\0';++dokument_uzyte;++liczba_linii;++cur_r;cur_c=0;
    dokument_zmieniony=true;
    return true;
}

/* =========================================================================
 * 9. RUCH KURSORA
 * ========================================================================= */

void kursor_lewo() {
    normalizuj_kursor();

    if (cur_c > 0) {
        const char* linia=pobierz_linie_const(cur_r);
        if(!linia)return;
        cur_c =
            utf8_poprzedni_start(
                linia,
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
        const char* linia=pobierz_linie_const(cur_r);
        if(!linia)return;
        cur_c =
            utf8_nastepny_start(
                linia,
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

    const int obecny_indeks = wiersz_ekranowy_kursora();
    WierszEkranowy obecny{};
    WierszEkranowy cel{};
    if (!pobierz_wiersz_ekranowy(obecny_indeks, &obecny) ||
        !pobierz_wiersz_ekranowy(obecny_indeks - 1, &cel)) return;

    const char* obecna_linia = pobierz_linie_const(obecny.linia);
    const char* docelowa_linia = pobierz_linie_const(cel.linia);
    if (!obecna_linia || !docelowa_linia) return;
    const int x = szerokosc_fragmentu(
        obecna_linia, obecny.poczatek, cur_c, dlugosc_linii(obecny.linia));
    cur_r = cel.linia;
    const int len = dlugosc_linii(cur_r);
    cur_c = bajt_dla_piksela(
        docelowa_linia, cel.poczatek, cel.koniec, len, x);
}

void kursor_dol() {
    normalizuj_kursor();

    const int obecny_indeks = wiersz_ekranowy_kursora();
    WierszEkranowy obecny{};
    WierszEkranowy cel{};
    if (!pobierz_wiersz_ekranowy(obecny_indeks, &obecny) ||
        !pobierz_wiersz_ekranowy(obecny_indeks + 1, &cel)) return;

    const char* obecna_linia = pobierz_linie_const(obecny.linia);
    const char* docelowa_linia = pobierz_linie_const(cel.linia);
    if (!obecna_linia || !docelowa_linia) return;
    const int x = szerokosc_fragmentu(
        obecna_linia, obecny.poczatek, cur_c, dlugosc_linii(obecny.linia));
    cur_r = cel.linia;
    const int len = dlugosc_linii(cur_r);
    cur_c = bajt_dla_piksela(
        docelowa_linia, cel.poczatek, cel.koniec, len, x);
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
    const ProstokatEdytora editor =
        aktualny_prostokat_edytora();
    return max_int(1, editor.wysokosc / LINE_H);
}

uint32_t utf8_kodpunkt(
    const char* tekst,
    int poczatek,
    int koniec
) {
    if (!tekst || poczatek < 0 || poczatek >= koniec) return 0;

    const uint8_t b0 = static_cast<uint8_t>(tekst[poczatek]);
    const int bajty = koniec - poczatek;
    if (bajty == 2 && b0 >= 0xC2U && b0 <= 0xDFU) {
        const uint8_t b1 = static_cast<uint8_t>(tekst[poczatek + 1]);
        if (utf8_kontynuacja(b1)) {
            return (static_cast<uint32_t>(b0 & 0x1FU) << 6) |
                static_cast<uint32_t>(b1 & 0x3FU);
        }
    } else if (bajty == 3 && b0 >= 0xE0U && b0 <= 0xEFU) {
        const uint8_t b1 = static_cast<uint8_t>(tekst[poczatek + 1]);
        const uint8_t b2 = static_cast<uint8_t>(tekst[poczatek + 2]);
        if (utf8_kontynuacja(b1) && utf8_kontynuacja(b2)) {
            return (static_cast<uint32_t>(b0 & 0x0FU) << 12) |
                (static_cast<uint32_t>(b1 & 0x3FU) << 6) |
                static_cast<uint32_t>(b2 & 0x3FU);
        }
    } else if (bajty == 4 && b0 >= 0xF0U && b0 <= 0xF4U) {
        const uint8_t b1 = static_cast<uint8_t>(tekst[poczatek + 1]);
        const uint8_t b2 = static_cast<uint8_t>(tekst[poczatek + 2]);
        const uint8_t b3 = static_cast<uint8_t>(tekst[poczatek + 3]);
        if (utf8_kontynuacja(b1) && utf8_kontynuacja(b2) &&
            utf8_kontynuacja(b3)) {
            return (static_cast<uint32_t>(b0 & 0x07U) << 18) |
                (static_cast<uint32_t>(b1 & 0x3FU) << 12) |
                (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
                static_cast<uint32_t>(b3 & 0x3FU);
        }
    }
    return b0;
}

int szerokosc_znaku_ekranowego(
    const char* tekst,
    int poczatek,
    int koniec
) {
    const int bajty = koniec - poczatek;
    if (tekst && bajty >= 3) {
        /*
         * Obecny renderer GUI dekoduje sekwencje 2-bajtowe, a dluzsze
         * wyswietla bajt po bajcie. Sumujemy dokladnie taki advance, lecz
         * wrap i hit-test nadal traktuja cala sekwencje jako jeden znak.
         */
        int suma = 0;
        for (int i = poczatek; i < koniec; ++i) {
            int szerokosc = gui_pobierz_szerokosc_znaku(
                static_cast<uint8_t>(tekst[i]));
            if (szerokosc < 1) szerokosc = 1;
            if (szerokosc > 64) szerokosc = 8;
            suma += szerokosc + 1;
        }
        return suma;
    }
    uint32_t znak = utf8_kodpunkt(tekst, poczatek, koniec);
    if (znak < 128U && cache_szerokosci_ascii[znak] > 0) {
        return cache_szerokosci_ascii[znak];
    }
    int szerokosc = gui_pobierz_szerokosc_znaku(znak);
    if (szerokosc < 1) szerokosc = 1;
    if (szerokosc > 64) szerokosc = 8;
    const int advance = szerokosc + 1;
    if (znak < 128U) cache_szerokosci_ascii[znak] = advance;
    return advance;
}

int szerokosc_fragmentu(
    const char* tekst,
    int poczatek,
    int koniec,
    int len
) {
    if (!tekst || poczatek < 0 || koniec < poczatek || poczatek > len) {
        return 0;
    }
    koniec = min_int(koniec, len);
    int piksele = 0;
    for (int p = poczatek; p < koniec;) {
        const int next = utf8_nastepny_start(tekst, p, len);
        if (next <= p || next > koniec) break;
        const int advance = szerokosc_znaku_ekranowego(tekst, p, next);
        if (piksele > INT32_MAX - advance) return INT32_MAX;
        piksele += advance;
        p = next;
    }
    return piksele;
}

/*
 * Soft wrap dzieli tylko widok. Zwracane offsety zawsze wskazuja granice
 * kodpunktow UTF-8, a co najmniej jeden kodpunkt trafia do wiersza nawet,
 * gdy pojedynczy glif jest szerszy od bardzo malego viewportu.
 */
int koniec_wiersza_ekranowego(
    const char* linia,
    int poczatek,
    int len,
    int szerokosc
) {
    if (!linia || poczatek >= len) return len;
    if (szerokosc < 1) szerokosc = 1;

    int p = poczatek;
    int piksele = 0;
    while (p < len) {
        const int next = utf8_nastepny_start(linia, p, len);
        if (next <= p) break;
        const int advance = szerokosc_znaku_ekranowego(linia, p, next);
        if (p > poczatek && advance > szerokosc - piksele) break;
        piksele += advance;
        p = next;
    }
    if (p == poczatek) p = utf8_nastepny_start(linia, p, len);
    return p;
}

int liczba_wierszy_ekranowych() {
    const int szerokosc = aktualny_prostokat_edytora().szerokosc;
    int wynik = 0;
    size_t offset = 0;
    for (int r = 0; r < liczba_linii && offset < dokument_uzyte; ++r) {
        const size_t pozostalo = dokument_uzyte - offset;
        const size_t len_size = dlugosc_limit(dokument + offset, pozostalo);
        if (len_size >= pozostalo || len_size > static_cast<size_t>(INT32_MAX)) {
            break;
        }
        const int len = static_cast<int>(len_size);
        if (len == 0) {
            ++wynik;
        } else {
            int p = 0;
            while (p < len) {
                const int next = koniec_wiersza_ekranowego(
                    dokument + offset, p, len, szerokosc);
                if (next <= p) break;
                ++wynik;
                p = next;
            }
        }
        offset += len_size + 1U;
    }
    return max_int(1, wynik);
}

bool pobierz_wiersz_ekranowy(
    int indeks,
    WierszEkranowy* wynik
) {
    if (!wynik || indeks < 0) return false;
    const int szerokosc = aktualny_prostokat_edytora().szerokosc;
    int numer = 0;
    size_t offset = 0;
    for (int r = 0; r < liczba_linii && offset < dokument_uzyte; ++r) {
        const size_t pozostalo = dokument_uzyte - offset;
        const size_t len_size = dlugosc_limit(dokument + offset, pozostalo);
        if (len_size >= pozostalo || len_size > static_cast<size_t>(INT32_MAX)) {
            return false;
        }
        const int len = static_cast<int>(len_size);
        if (len == 0) {
            if (numer == indeks) {
                *wynik = {r, 0, 0};
                return true;
            }
            ++numer;
        } else {
            int p = 0;
            while (p < len) {
                const int next = koniec_wiersza_ekranowego(
                    dokument + offset, p, len, szerokosc);
                if (next <= p) return false;
                if (numer == indeks) {
                    *wynik = {r, p, next};
                    return true;
                }
                ++numer;
                p = next;
            }
        }
        offset += len_size + 1U;
    }
    return false;
}

bool nastepny_wiersz_ekranowy(WierszEkranowy* wiersz) {
    if (!wiersz || wiersz->linia < 0 || wiersz->linia >= liczba_linii) {
        return false;
    }
    const int len = dlugosc_linii(wiersz->linia);
    if (wiersz->koniec < len) {
        const char* linia = pobierz_linie_const(wiersz->linia);
        if (!linia) return false;
        wiersz->poczatek = wiersz->koniec;
        wiersz->koniec = koniec_wiersza_ekranowego(
            linia,
            wiersz->poczatek,
            len,
            aktualny_prostokat_edytora().szerokosc
        );
        return wiersz->koniec > wiersz->poczatek;
    }
    if (wiersz->linia + 1 >= liczba_linii) return false;
    ++wiersz->linia;
    wiersz->poczatek = 0;
    const char* linia = pobierz_linie_const(wiersz->linia);
    if (!linia) return false;
    const int nastepny_len = dlugosc_linii(wiersz->linia);
    wiersz->koniec = nastepny_len == 0 ? 0 : koniec_wiersza_ekranowego(
        linia,
        0,
        nastepny_len,
        aktualny_prostokat_edytora().szerokosc
    );
    return nastepny_len == 0 || wiersz->koniec > 0;
}

int wiersz_ekranowy_kursora() {
    const int szerokosc = aktualny_prostokat_edytora().szerokosc;
    int numer = 0;
    size_t offset = 0;
    for (int r = 0; r < liczba_linii && offset < dokument_uzyte; ++r) {
        const size_t pozostalo = dokument_uzyte - offset;
        const size_t len_size = dlugosc_limit(dokument + offset, pozostalo);
        if (len_size >= pozostalo || len_size > static_cast<size_t>(INT32_MAX)) {
            break;
        }
        const int len = static_cast<int>(len_size);
        if (len == 0) {
            if (r == cur_r) return numer;
            ++numer;
        } else {
            int p = 0;
            while (p < len) {
                const int next = koniec_wiersza_ekranowego(
                    dokument + offset, p, len, szerokosc);
                if (next <= p) break;
                if (r == cur_r && (cur_c < next || next == len)) return numer;
                ++numer;
                p = next;
            }
        }
        offset += len_size + 1U;
    }
    return max_int(0, numer - 1);
}

StanLayoutEdycji pobierz_stan_layoutu_edycji() {
    StanLayoutEdycji stan{};
    stan.scroll = scroll_y;
    stan.wiersz_kursora = wiersz_ekranowy_kursora();
    stan.linia = cur_r;
    stan.kursor_bajt = cur_c;
    const char* aktualna_linia = pobierz_linie_const(cur_r);
    stan.poprzedni_kodpunkt = aktualna_linia
        ? utf8_poprzedni_start(aktualna_linia, cur_c)
        : cur_c;
    stan.dlugosc = dlugosc_linii(cur_r);
    stan.wszystkie_linie = liczba_linii;
    if (!aktualna_linia) return stan;

    if (stan.dlugosc == 0) {
        stan.wiersze_linii = 1;
        stan.pierwszy_wiersz_linii = stan.wiersz_kursora;
        stan.poprawny = true;
        return stan;
    }

    const int szerokosc = aktualny_prostokat_edytora().szerokosc;
    int lokalny_wiersz = 0;
    int lokalny_wiersz_kursora = -1;
    for (int p = 0; p < stan.dlugosc;) {
        const int next = koniec_wiersza_ekranowego(
            aktualna_linia,
            p,
            stan.dlugosc,
            szerokosc
        );
        if (next <= p) return stan;
        if (lokalny_wiersz_kursora < 0 &&
            (cur_c < next || next == stan.dlugosc)) {
            lokalny_wiersz_kursora = lokalny_wiersz;
            stan.poczatek_segmentu = p;
            stan.koniec_segmentu = next;
        }
        ++lokalny_wiersz;
        p = next;
    }

    stan.wiersze_linii = lokalny_wiersz;
    if (lokalny_wiersz_kursora < 0 || stan.wiersze_linii <= 0) return stan;
    stan.pierwszy_wiersz_linii =
        stan.wiersz_kursora - lokalny_wiersz_kursora;
    stan.poprawny = true;
    return stan;
}

ZakresRedraw zaplanuj_redraw_po_edycji(
    const StanLayoutEdycji& przed,
    const StanLayoutEdycji& po,
    bool zwykla_edycja,
    bool backspace
) {
    const int ostatni_widoczny =
        scroll_y + liczba_widocznych_linii() - 1;

    if (!przed.poprawny || !po.poprawny || przed.scroll != po.scroll) {
        return {true, scroll_y, ostatni_widoczny};
    }

    int pierwszy = min_int(przed.wiersz_kursora, po.wiersz_kursora);
    /* Edycja dokladnie na granicy wrapu moze zmienic takze poprzedni
       visual row (np. gdy nowy, waski glif jeszcze sie w nim zmiesci). */
    if (przed.poczatek_segmentu > 0 &&
        zwykla_edycja &&
        (przed.kursor_bajt == przed.poczatek_segmentu ||
         (backspace &&
          przed.poprzedni_kodpunkt == przed.poczatek_segmentu))) {
        pierwszy = min_int(pierwszy, przed.wiersz_kursora - 1);
    }

    if (!zwykla_edycja || przed.linia != po.linia) {
        return {false, pierwszy, ostatni_widoczny};
    }

    const int koniec_przed =
        przed.pierwszy_wiersz_linii + przed.wiersze_linii - 1;
    const int koniec_po =
        po.pierwszy_wiersz_linii + po.wiersze_linii - 1;
    const bool ostatni_segment_przed =
        przed.koniec_segmentu == przed.dlugosc;
    const bool ostatni_segment_po =
        po.koniec_segmentu == po.dlugosc;

    if (przed.wiersze_linii == po.wiersze_linii &&
        ostatni_segment_przed && ostatni_segment_po) {
        return {false, pierwszy,
            max_int(przed.wiersz_kursora, po.wiersz_kursora)};
    }

    if (przed.wiersze_linii == po.wiersze_linii) {
        return {false, pierwszy, max_int(koniec_przed, koniec_po)};
    }

    /* Zmiana liczby visual rows przesuwa kolejne linie logiczne. Jesli
       takie istnieja, tylko widoczny ogon viewportu wymaga odswiezenia. */
    int ostatni = max_int(koniec_przed, koniec_po);
    if (po.linia + 1 < po.wszystkie_linie ||
        przed.linia + 1 < przed.wszystkie_linie) {
        ostatni = ostatni_widoczny;
    }
    return {false, pierwszy, ostatni};
}

int bajt_dla_piksela(
    const char* linia,
    int poczatek,
    int koniec,
    int len,
    int x
) {
    if (!linia || x <= 0) return poczatek;
    int piksele = 0;
    for (int p = poczatek; p < koniec;) {
        const int next = utf8_nastepny_start(linia, p, len);
        if (next <= p || next > koniec) break;
        const int advance = szerokosc_znaku_ekranowego(linia, p, next);
        if (x < piksele + advance / 2) return p;
        if (x < piksele + advance) return next;
        piksele += advance;
        p = next;
    }
    return koniec;
}

void dopasuj_scroll_do_kursora() {
    normalizuj_kursor();

    const int widoczne_linie =
        liczba_widocznych_linii();

    const int wszystkie_linie =
        liczba_wierszy_ekranowych();

    const int wiersz_kursora =
        wiersz_ekranowy_kursora();

    const int maks_scroll =
        max_int(0, wszystkie_linie - widoczne_linie);

    scroll_y = clamp_int(scroll_y, 0, maks_scroll);

    if (wiersz_kursora <
        scroll_y) {
        scroll_y =
            wiersz_kursora;
    }

    if (wiersz_kursora >=
        scroll_y +
            widoczne_linie) {
        scroll_y =
            wiersz_kursora -
            widoczne_linie +
            1;
    }

    scroll_y = clamp_int(scroll_y, 0, maks_scroll);
}

/* =========================================================================
 * 11. WIERSZE EKRANOWE
 * ========================================================================= */

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
    if (!wynik_dlugosc || dokument_uzyte == 0 ||
        dokument_uzyte > DOKUMENT_POJEMNOSC ||
        dokument[dokument_uzyte-1U] != '\0') return false;
    for(size_t i=0;i+1U<dokument_uzyte;++i)
        if(dokument[i]=='\0')dokument[i]='\n';
    *wynik_dlugosc=static_cast<uint32_t>(dokument_uzyte-1U);
    return true;
}

void przywroc_separatory_dokumentu(){
    if(dokument_uzyte==0||dokument_uzyte>DOKUMENT_POJEMNOSC)return;
    for(size_t i=0;i+1U<dokument_uzyte;++i)
        if(dokument[i]=='\n')dokument[i]='\0';
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

    const bool zapis_ok=zapisz_plik(
            sciezka_docelowa,
            dokument,
            dlugosc);

    /* Syscall kopiuje synchronicznie; po nim wracamy do separatorow modelu. */
    przywroc_separatory_dokumentu();

    if (!zapis_ok) {

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
    size_t rozmiar_pliku_wej
) {
    if(rozmiar_pliku_wej>MAKS_ROZMIAR_PLIKU)return false;
    /* Plik tekstowy z osadzonym NUL-em jest odrzucany, nie cicho ucinany. */
    for(size_t i=0;i<rozmiar_pliku_wej;++i)if(dokument[i]=='\0')return false;
    size_t src=0,dst=0;liczba_linii=1;
    while(src<rozmiar_pliku_wej){
        const char ch=dokument[src++];
        if(ch=='\r'){
            dokument[dst++]='\0';++liczba_linii;
            if(src<rozmiar_pliku_wej&&dokument[src]=='\n')++src;
        }else if(ch=='\n'){
            dokument[dst++]='\0';++liczba_linii;
        }else dokument[dst++]=ch;
    }
    dokument[dst++]='\0';dokument_uzyte=dst;

    cur_r =
        0;

    cur_c =
        0;

    scroll_y =
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

    uint32_t rozmiar=0;
    if(!pobierz_rozmiar_pliku(sciezka_zrodlowa,&rozmiar)){
        ustaw_status("Blad: nie mozna pobrac rozmiaru pliku.");return false;
    }
    if(rozmiar>MAKS_ROZMIAR_PLIKU){
        ustaw_status("Blad: plik przekracza limit dokumentu 64 KiB.");return false;
    }

    if (!czytaj_plik(
            sciezka_zrodlowa,
            dokument,
            rozmiar)) {

        ustaw_status(
            "Blad: nie mozna odczytac pliku."
        );

        return false;
    }

    if (!parsuj_plik_do_dokumentu(
            rozmiar)) {

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

    ustaw_status(
        "Wczytano plik pomyslnie."
    );

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
        if (!gui_minimalizuj_okno()) aplikacja_zminimalizowana = false;
        gui_ustaw_capture_myszy(false);
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

        gui_wypisz_tekst_kolor(
            WIN_X + 120 + oblicz_szerokosc_tekstu(sciezka_input, 1),
            WIN_Y + WIN_H - 18,
            KOLOR_BURSZTYN_JASNY,
            "_"
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

        gui_wypisz_tekst_kolor(
            WIN_X + 120 + oblicz_szerokosc_tekstu(sciezka_input, 1),
            WIN_Y + WIN_H - 18,
            KOLOR_BURSZTYN_JASNY,
            "_"
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

void rysuj_pole_sciezki() {
    const int x = WIN_X + 120;
    const int y = WIN_Y + WIN_H - 18;
    const int w = WIN_W - 128;

    gui_rysuj_prostokat(x, y, w, 18, KOLOR_TLO);

    /* Najszerszy glif ma 16 px + 1 px odstepu. Pokazujemy koniec sciezki,
       ale nigdy nie pozwalamy tekstowi rozszerzyc dirty poza pole. */
    const int maks_znakow = (w - 18) / 17;
    int start = sciezka_len > maks_znakow ? sciezka_len - maks_znakow : 0;
    while (start < sciezka_len &&
           utf8_kontynuacja(static_cast<unsigned char>(sciezka_input[start])))
        ++start;
    const char* widoczna = sciezka_input + start;
    gui_wypisz_tekst_kolor(x, y, KOLOR_BIALY, widoczna);

    const int advance = oblicz_szerokosc_tekstu(widoczna, 1);
    gui_wypisz_tekst_kolor(x + advance, y,
                           KOLOR_BURSZTYN_JASNY, "_");
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

void rysuj_wiersz_ekranowy(
    const WierszEkranowy& wiersz,
    int numer_na_ekranie,
    bool rysuj_karetke
) {
    const ProstokatEdytora editor = aktualny_prostokat_edytora();
    const char* linia = pobierz_linie_const(wiersz.linia);
    if (!linia) return;
    const int len = dlugosc_linii(wiersz.linia);
    const int y = editor.y + numer_na_ekranie * LINE_H;
    int x = editor.x;

    /*
     * BWS przyjmuje maks. 1023 bajty tekstu na wywolanie. Bardzo szeroki
     * wiersz rysujemy wiec kilkoma fragmentami, bez tworzenia dodatkowego
     * zawiniecia i bez przecinania UTF-8.
     */
    for (int p = wiersz.poczatek; p < wiersz.koniec;) {
        char fragment[WIDOCZNA_LINIA_POJEMNOSC] = {};
        size_t out = 0;
        int koniec_fragmentu = p;
        while (koniec_fragmentu < wiersz.koniec) {
            const int next = utf8_nastepny_start(linia, koniec_fragmentu, len);
            if (next <= koniec_fragmentu || next > wiersz.koniec) break;
            const size_t bajty = static_cast<size_t>(next - koniec_fragmentu);
            if (out + bajty + 1U > sizeof(fragment)) break;
            for (int i = koniec_fragmentu; i < next; ++i) {
                fragment[out++] = linia[i];
            }
            koniec_fragmentu = next;
        }
        if (koniec_fragmentu <= p) break;
        fragment[out] = '\0';
        gui_wypisz_tekst(x, y, fragment);
        x += szerokosc_fragmentu(linia, p, koniec_fragmentu, len);
        p = koniec_fragmentu;
    }

    if (rysuj_karetke && tryb == TrybPracy::EDYCJA_TEKSTU &&
        !okno_pomoc_widoczne) {
        int caret_x = editor.x + szerokosc_fragmentu(
            linia, wiersz.poczatek, cur_c, len);
        caret_x = clamp_int(caret_x, editor.x, editor.x + editor.szerokosc - 2);
        gui_rysuj_prostokat(
            caret_x, y + LINE_H - 2, 2, 2, KOLOR_BURSZTYN_JASNY);
    }
}

void rysuj_zakres_wierszy_ekranowych(
    int pierwszy_wiersz,
    int ostatni_wiersz,
    bool pelny_viewport
) {
    const ProstokatEdytora editor = aktualny_prostokat_edytora();
    const int max_lines = liczba_widocznych_linii();
    const int caret_wiersz = wiersz_ekranowy_kursora();
    const int pierwszy_widoczny = scroll_y;
    const int ostatni_widoczny = scroll_y + max_lines - 1;

    if (pelny_viewport) {
        pierwszy_wiersz = pierwszy_widoczny;
        ostatni_wiersz = ostatni_widoczny;
    } else {
        pierwszy_wiersz = max_int(pierwszy_wiersz, pierwszy_widoczny);
        ostatni_wiersz = min_int(ostatni_wiersz, ostatni_widoczny);
        if (pierwszy_wiersz > ostatni_wiersz) return;
    }

    const int y = editor.y +
        (pierwszy_wiersz - pierwszy_widoczny) * LINE_H;
    int wysokosc = (ostatni_wiersz - pierwszy_wiersz + 1) * LINE_H;
    if (pelny_viewport) wysokosc = editor.wysokosc;
    wysokosc = min_int(wysokosc, editor.y + editor.wysokosc - y);
    if (wysokosc <= 0) return;

    gui_wyczyscz_obszar(
        editor.x, y, editor.szerokosc, wysokosc);

#if BURSZTYN_DEBUG_GUI_PERF
    notatnik_redraw_pixels += static_cast<uint64_t>(editor.szerokosc) *
        static_cast<uint64_t>(wysokosc);
    if (pelny_viewport) {
        ++notatnik_full_viewport_redraw;
    } else {
        notatnik_visual_row_redraw += static_cast<uint64_t>(
            ostatni_wiersz - pierwszy_wiersz + 1);
    }
#endif

    WierszEkranowy wiersz{};
    if (!pobierz_wiersz_ekranowy(pierwszy_wiersz, &wiersz)) return;
    for (int indeks = pierwszy_wiersz;
         indeks <= ostatni_wiersz;
         ++indeks) {
        rysuj_wiersz_ekranowy(
            wiersz,
            indeks - pierwszy_widoczny,
            indeks == caret_wiersz
        );
        if (!nastepny_wiersz_ekranowy(&wiersz)) break;
    }
}

void rysuj_tekst_dokumentu() {
    rysuj_zakres_wierszy_ekranowych(0, 0, true);
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
    gui_rysuj_standardowa_belke(WIN_X, WIN_Y, WIN_W,
        dokument_zmieniony ? "Notatnik *" : "Notatnik", zmaksymalizowane);

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
    const ProstokatEdytora editor = aktualny_prostokat_edytora();

    const int i =
        (my -
         editor.y) /
        LINE_H;

    WierszEkranowy wiersz{};
    if (!pobierz_wiersz_ekranowy(scroll_y + i, &wiersz)) {
        cur_r = liczba_linii - 1;
        cur_c = dlugosc_linii(cur_r);
        dopasuj_scroll_do_kursora();
        return;
    }

    const int x =
        max_int(
            0,
            mx -
                editor.x
        );

    cur_r = wiersz.linia;
    const char* linia=pobierz_linie_const(cur_r);
    const int len=dlugosc_linii(cur_r);
    cur_c = linia ? bajt_dla_piksela(
        linia, wiersz.poczatek, wiersz.koniec, len, x) : 0;

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
        "/uzytkownicy/plik.txt"
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
    char sciezka_docelowa[BAJTY_SCIEZKI] = {};

    if (sciezka_input[0] == '/') {
        if (!kopiuj_tekst(sciezka_docelowa,
                          sizeof(sciezka_docelowa),
                          sciezka_input)) {
            ustaw_status("Sciezka jest za dluga.");
            return;
        }
    } else {
        static const char katalog[] = "/uzytkownicy/";
        size_t p = 0;
        while (katalog[p] != '\0') {
            sciezka_docelowa[p] = katalog[p];
            ++p;
        }
        for (size_t i = 0; sciezka_input[i] != '\0'; ++i) {
            if (p + 1U >= sizeof(sciezka_docelowa)) {
                ustaw_status("Sciezka jest za dluga.");
                return;
            }
            sciezka_docelowa[p++] = sciezka_input[i];
        }
        sciezka_docelowa[p] = '\0';
    }

    if (!sciezka_poprawna(
            sciezka_docelowa)) {

        ustaw_status(
            "Nieprawidlowa sciezka pliku."
        );

        return;
    }

    if (!kopiuj_tekst(
            aktualna_sciezka,
            sizeof(aktualna_sciezka),
            sciezka_docelowa)) {

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
            gui_ustaw_capture_myszy(true);

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
    const ProstokatEdytora editor = aktualny_prostokat_edytora();
    const int text_h = liczba_widocznych_linii() * LINE_H;

    if (tryb ==
            TrybPracy::EDYCJA_TEKSTU &&
        mysz_w_prostokacie(
            mx,
            my,
            editor.x,
            editor.y,
            editor.szerokosc,
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
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        bool szybka_sciezka = false;
        bool szybki_edytor = false;
        ZakresRedraw szybki_zakres{};
        if (zdarzenie.typ == BWS_ZDARZENIE_FOCUS && aplikacja_zminimalizowana)
            aplikacja_zminimalizowana = false;
        int mx = old_mx;
        int my = old_my;
        uint8_t mb = poprzednie_przyciski;
        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH ||
            zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN ||
            zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP) {
            mx = zdarzenie.x; my = zdarzenie.y;
            mb = static_cast<uint8_t>(zdarzenie.przyciski);
        }

        const bool lewy =
            (mb &
             0x01U) != 0;

        const bool klik = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;

        const bool puszczenie = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP;

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
                gui_ustaw_capture_myszy(false);
            }
        }

        poprzednie_przyciski =
            mb;

        old_mx =
            mx;

        old_my =
            my;

        const char c = zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ
            ? static_cast<char>(zdarzenie.kod) : 0;

        if (c != 0 &&
            !aplikacja_zminimalizowana) {

            redraw =
                true;

            const bool byl_popup = menu_plik_otwarte ||
                menu_ustawienia_otwarte || okno_pomoc_widoczne;
            const int stary_c = cur_c;
            const TrybPracy stary_tryb = tryb;

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
                szybka_sciezka = !byl_popup && tryb == stary_tryb;
            } else {
                const StanLayoutEdycji layout_przed =
                    pobierz_stan_layoutu_edycji();

                obsluz_edycje(
                    &ansi,
                    c
                );

                const StanLayoutEdycji layout_po =
                    pobierz_stan_layoutu_edycji();

                const unsigned char uc = static_cast<unsigned char>(c);
                const bool zwykly_znak = uc >= 0x20U && uc != 0x7FU;
                const bool backspace = c == '\b' || uc == 0x7FU;
                const bool zwykla_edycja = zwykly_znak ||
                    (backspace && stary_c > 0);
                if (!byl_popup && tryb == TrybPracy::EDYCJA_TEKSTU &&
                    zwykla_edycja) {
                    szybki_edytor = true;
                    szybki_zakres = zaplanuj_redraw_po_edycji(
                        layout_przed, layout_po, true, backspace);
#if BURSZTYN_DEBUG_GUI_PERF
                    ++notatnik_key;
#endif
                } else if (!byl_popup &&
                           tryb == TrybPracy::EDYCJA_TEKSTU &&
                           (c == '\n' || c == '\r' || c == '\b' ||
                            uc == 0x7FU)) {
                    szybki_edytor = true;
                    szybki_zakres = zaplanuj_redraw_po_edycji(
                        layout_przed, layout_po, false, backspace);
                }
            }
        }

        if (!dragging && szybka_sciezka) {
            rysuj_pole_sciezki();
            gui_odswiez();
            redraw = false;
        } else if (!dragging && szybki_edytor) {
            if (szybki_zakres.pelny_viewport) {
                rysuj_tekst_dokumentu();
            } else {
                rysuj_zakres_wierszy_ekranowych(
                    szybki_zakres.pierwszy_wiersz,
                    szybki_zakres.ostatni_wiersz,
                    false
                );
            }
            gui_odswiez();
            redraw = false;
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
