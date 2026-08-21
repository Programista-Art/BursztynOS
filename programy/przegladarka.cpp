/*
 * Bursztyn OS - Hussar
 *
 * Przegladarka WWW Ring 3 dla Bursztyn OS.
 *
 * Najwazniejsze zalozenia tej wersji:
 *
 *   - jedna warstwa GUI nalezaca do procesu,
 *   - maksymalnie 6 zakladek,
 *   - HTTP i HTTPS przez BWS 28..31,
 *   - do 5 przekierowan,
 *   - podstawowy renderer HTML bez JavaScript,
 *   - bezpieczny parser statusu HTTP z http_kody.h,
 *   - bezpieczne ograniczenia wszystkich buforow,
 *   - zakladki trzymaja tresc na stercie zamiast kopiowac 256 KiB struct,
 *   - specjalny statyczny bufor sieciowy ponizej 4 GiB.
 *
 * WAZNE O ABI SIECI:
 *
 * Aktualny wrapper bws_siec_pobierz_http()/https() pakuje wskaznik bufora
 * do gornych 32 bitow jednego argumentu syscall. Jadro dekoduje wiec tylko
 * 32-bitowy adres user-space. Sterta Ring 3 zaczyna sie pod 0x800000000,
 * dlatego bufor przekazywany bezposrednio z gui_malloc() zostalby obciety.
 *
 * `siec_bufor` jest celowo globalnym BSS aplikacji pod niskim VA 0x610000+.
 * Po naprawieniu ABI BWS29/BWS30 do pelnego uint64_t ten bounce-buffer
 * bedzie mozna usunac.
 */

#include "../bursztyn_gui.h"
#include "http_kody.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef BURSZTYN_DEBUG_GUI_PERF
#define BURSZTYN_DEBUG_GUI_PERF 0
#endif

/* =========================================================================
 * 1. FORMAT .bur
 * ========================================================================= */

/*
 * Nowy DATA/BSS ma 512 KiB, poniewaz zawiera 256 KiB niskiego bounce-buffera
 * wymaganego przez obecne 32-bitowe pakowanie wskaznika HTTP.
 *
 * przegladarka_linker.ld MUSI miec ten sam layout:
 *
 *   HEADER  off 0x0000  VA 0x600000  region 0x1000
 *   TEXT    off 0x1000  VA 0x601000  region 0xF000
 *   DATA    off 0x10000 VA 0x610000  memory 0x80000
 */
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
    offsetof(NaglowekBur, punkt_wejscia) == 4U,
    "Nieprawidlowy layout NaglowekBur"
);

static_assert(
    offsetof(NaglowekBur, dane_wirtualny) == 52U,
    "Nieprawidlowy layout NaglowekBur"
);

extern "C" [[noreturn]] void _start();

extern "C" {

__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},

    reinterpret_cast<uint64_t>(
        &_start
    ),

    0x1000ULL,
    0xF000ULL,
    0x601000ULL,

    0x10000ULL,
    0x80000ULL,
    0x610000ULL
};

}

/* =========================================================================
 * 2. STALE APLIKACJI
 * ========================================================================= */

namespace {

constexpr int MAX_ZAKLADKI =
    6;

constexpr int MAX_HISTORIA =
    20;

constexpr int MAX_ULUBIONE =
    10;

constexpr size_t URL_POJEMNOSC =
    2048U;

constexpr size_t DOMENA_POJEMNOSC =
    254U;

constexpr size_t SCIEZKA_POJEMNOSC =
    2048U;

constexpr size_t URL_WIDOCZNY_POJEMNOSC =
    512U;

constexpr size_t HTML_POJEMNOSC =
    256U *
    1024U;

constexpr size_t SIEC_POJEMNOSC =
    256U *
    1024U;

constexpr size_t ULUBIONE_PLIK_POJEMNOSC =
    24U * 1024U;

constexpr size_t STATUS_POJEMNOSC =
    96U;

constexpr int PASEK_SYSTEMOWY_WYS =
    40;

constexpr int TYTUL_WYS =
    26;

constexpr int ZAKLADKI_Y =
    28;

constexpr int ZAKLADKI_WYS =
    24;

constexpr int NARZEDZIA_Y =
    56;

constexpr int NARZEDZIA_WYS =
    36;

constexpr int TRESC_Y =
    96;

constexpr int STATUS_WYS =
    22;

constexpr int MIN_WIN_W =
    560;

constexpr int MIN_WIN_H =
    360;

constexpr int DOMYSLNY_WIN_W =
    800;

constexpr int DOMYSLNY_WIN_H =
    550;

constexpr int MAX_PRZEKIEROWAN =
    5;

constexpr int Z_ORDER_HUSSAR =
    10;

constexpr uint32_t KOLOR_TLO =
    0x00F8F9FAU;

constexpr uint32_t KOLOR_TEKST =
    0x00222222U;

constexpr uint32_t KOLOR_BIALY =
    0x00FFFFFFU;

constexpr uint32_t KOLOR_AKTYWNY =
    0x004A2500U;

constexpr uint32_t KOLOR_POMARANCZ =
    0x00E58A00U;

constexpr uint32_t KOLOR_STATUS =
    0x00FFBF00U;

constexpr uint32_t KOLOR_CZERWONY =
    0x00AA0000U;

/*
 * Bounce buffer BWS29/BWS30.
 *
 * Musi znajdowac sie w niskim DATA/BSS aplikacji, nie na stercie 32 GiB.
 */
char siec_bufor[
    SIEC_POJEMNOSC
] __attribute__((section(".bss"), aligned(4096))) = {};

/* =========================================================================
 * 3. STRUKTURY
 * ========================================================================= */

struct Zakladka {
    char url[
        URL_POJEMNOSC
    ];

    char* tresc;

    int przewin_y;

    bool to_jest_html;
    bool wczytana;
};

struct ParsedUrl {
    bool https;

    char domena[
        DOMENA_POJEMNOSC
    ];

    char sciezka[
        SCIEZKA_POJEMNOSC
    ];
};

struct OdpowiedzHttp {
    int kod;

    const char* body;
    size_t body_len;

    bool chunked;
    bool html;
    bool tekst;
};

/* =========================================================================
 * 4. STAN APLIKACJI
 * ========================================================================= */

Zakladka zakladki[
    MAX_ZAKLADKI
] = {};

int liczba_zakladek =
    0;

int aktywna_zakladka =
    0;

char historia[
    MAX_HISTORIA
][
    URL_POJEMNOSC
] = {};

int historia_idx =
    -1;

int historia_max =
    -1;

char ulubione[
    MAX_ULUBIONE
][
    URL_POJEMNOSC
] = {};

int ulubione_ilosc =
    0;

char plik_ulubionych[
    ULUBIONE_PLIK_POJEMNOSC
] = {};

char status_bufor[
    STATUS_POJEMNOSC
] = {};

bool menu_ulubione_otwarte =
    false;

bool menu_ustawienia_otwarte =
    false;

bool w_polu_url =
    false;

bool zmaksymalizowane =
    false;

bool aplikacja_zminimalizowana =
    false;

bool dragging =
    false;

int drag_off_x =
    0;

int drag_off_y =
    0;

int screen_w =
    1024;

int screen_h =
    768;

int WIN_X =
    50;

int WIN_Y =
    50;

int WIN_W =
    DOMYSLNY_WIN_W;

int WIN_H =
    DOMYSLNY_WIN_H;

int old_win_x =
    50;

int old_win_y =
    50;

int old_win_w =
    DOMYSLNY_WIN_W;

int old_win_h =
    DOMYSLNY_WIN_H;

int max_przewin_y =
    0;

int calkowita_wysokosc_strony =
    0;

/* =========================================================================
 * 5. FREESTANDING MEMCPY/MEMSET
 * ========================================================================= */

} // namespace

extern "C" void* memcpy(
    void* dest,
    const void* src,
    unsigned long n
) {
    if (!dest ||
        !src) {

        return dest;
    }

    uint8_t* d =
        static_cast<uint8_t*>(
            dest
        );

    const uint8_t* s =
        static_cast<const uint8_t*>(
            src
        );

    for (unsigned long i = 0;
         i < n;
         ++i) {

        d[i] =
            s[i];
    }

    return dest;
}

extern "C" void* memset(
    void* dest,
    int val,
    unsigned long n
) {
    if (!dest) {
        return dest;
    }

    uint8_t* d =
        static_cast<uint8_t*>(
            dest
        );

    for (unsigned long i = 0;
         i < n;
         ++i) {

        d[i] =
            static_cast<uint8_t>(
                val
            );
    }

    return dest;
}

namespace {

/* =========================================================================
 * 6. HELPERY TEKSTOWE
 * ========================================================================= */

size_t dlugosc_limit(
    const char* s,
    size_t limit
) {
    if (!s) {
        return 0;
    }

    size_t n =
        0;

    while (n < limit &&
           s[n] != '\0') {

        ++n;
    }

    return n;
}

bool ascii_biala(
    char c
) {
    return
        c == ' ' ||
        c == '\t' ||
        c == '\r' ||
        c == '\n';
}

char ascii_mala(
    char c
) {
    if (c >= 'A' &&
        c <= 'Z') {

        return
            static_cast<char>(
                c +
                ('a' - 'A')
            );
    }

    return c;
}

bool tekst_rowny_ci(
    const char* a,
    size_t a_len,
    const char* b
) {
    if (!a ||
        !b) {

        return false;
    }

    size_t i =
        0;

    while (b[i] != '\0') {
        if (i >= a_len ||
            ascii_mala(a[i]) !=
                ascii_mala(b[i])) {

            return false;
        }

        ++i;
    }

    return
        i ==
        a_len;
}

bool prefiks_ci(
    const char* tekst,
    const char* prefiks
) {
    if (!tekst ||
        !prefiks) {

        return false;
    }

    for (size_t i = 0;
         prefiks[i] != '\0';
         ++i) {

        if (tekst[i] == '\0' ||
            ascii_mala(
                tekst[i]) !=
            ascii_mala(
                prefiks[i])) {

            return false;
        }
    }

    return true;
}

bool kopiuj_limit(
    char* cel,
    size_t pojemnosc,
    const char* zrodlo
) {
    if (!cel ||
        pojemnosc == 0) {

        return false;
    }

    cel[0] =
        '\0';

    if (!zrodlo) {
        return true;
    }

    size_t i =
        0;

    while (zrodlo[i] != '\0' &&
           i + 1U <
               pojemnosc) {

        cel[i] =
            zrodlo[i];

        ++i;
    }

    /*
     * Nie zakoncz UTF-8 po samym lead byte albo w srodku continuation.
     */
    if (zrodlo[i] != '\0') {
        while (i > 0 &&
               (static_cast<uint8_t>(
                    cel[i - 1]) &
                0xC0U) ==
                    0x80U) {

            --i;
        }

        if (i > 0) {
            const uint8_t lead =
                static_cast<uint8_t>(
                    cel[i - 1]
                );

            size_t potrzeba =
                1;

            if ((lead &
                 0xE0U) ==
                0xC0U) {

                potrzeba =
                    2;
            } else if ((lead &
                        0xF0U) ==
                       0xE0U) {

                potrzeba =
                    3;
            } else if ((lead &
                        0xF8U) ==
                       0xF0U) {

                potrzeba =
                    4;
            }

            if (potrzeba > 1) {
                const size_t dostepne =
                    i -
                    (i - 1U);

                if (dostepne <
                    potrzeba) {

                    --i;
                }
            }
        }
    }

    cel[i] =
        '\0';

    return
        zrodlo[i] ==
        '\0';
}

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

bool dopisz_znak_limit(
    char* s,
    size_t pojemnosc,
    char znak
) {
    if (!s ||
        pojemnosc < 2U) {

        return false;
    }

    const size_t len =
        dlugosc_limit(
            s,
            pojemnosc
        );

    if (len >=
        pojemnosc ||
        len + 1U >=
            pojemnosc) {

        return false;
    }

    s[len] =
        znak;

    s[len + 1U] =
        '\0';

    return true;
}

size_t utf8_poprzedni(
    const char* s,
    size_t pos
) {
    if (!s ||
        pos == 0) {

        return 0;
    }

    --pos;

    while (pos > 0 &&
           (static_cast<uint8_t>(
                s[pos]) &
            0xC0U) ==
                0x80U) {

        --pos;
    }

    return pos;
}

void usun_ostatni_utf8(
    char* s,
    size_t pojemnosc
) {
    if (!s ||
        pojemnosc == 0) {

        return;
    }

    const size_t len =
        dlugosc_limit(
            s,
            pojemnosc
        );

    if (len == 0 ||
        len >= pojemnosc) {

        return;
    }

    s[
        utf8_poprzedni(
            s,
            len
        )
    ] =
        '\0';
}

void ustaw_status(
    const char* tekst
) {
    (void)kopiuj_limit(
        status_bufor,
        sizeof(status_bufor),
        tekst
    );
}

bool punkt_w_prostokacie(
    int px,
    int py,
    int x,
    int y,
    int w,
    int h
) {
    return
        w > 0 &&
        h > 0 &&
        px >= x &&
        py >= y &&
        px < x + w &&
        py < y + h;
}

/* =========================================================================
 * 7. UTF-8
 * ========================================================================= */

struct Utf8Znak {
    uint32_t kod;
    int bajty;
};

Utf8Znak dekoduj_utf8(
    const char* s
) {
    Utf8Znak z{
        0xFFFDU,
        1
    };

    if (!s ||
        s[0] == '\0') {

        z.kod =
            0;

        return z;
    }

    const uint8_t b0 =
        static_cast<uint8_t>(
            s[0]
        );

    if (b0 < 0x80U) {
        z.kod =
            b0;

        return z;
    }

    if ((b0 &
         0xE0U) ==
            0xC0U) {

        if (s[1] == '\0') {
            return z;
        }

        const uint8_t b1 =
            static_cast<uint8_t>(
                s[1]
            );

        if ((b1 &
             0xC0U) !=
                0x80U) {

            return z;
        }

        const uint32_t cp =
            ((b0 &
              0x1FU) << 6) |
            (b1 &
             0x3FU);

        if (cp <
            0x80U) {

            return z;
        }

        z.kod =
            cp;

        z.bajty =
            2;

        return z;
    }

    if ((b0 &
         0xF0U) ==
            0xE0U) {

        if (s[1] == '\0') {
            return z;
        }

        const uint8_t b1 =
            static_cast<uint8_t>(
                s[1]
            );

        if ((b1 &
             0xC0U) !=
                0x80U ||
            s[2] ==
                '\0') {

            return z;
        }

        const uint8_t b2 =
            static_cast<uint8_t>(
                s[2]
            );

        if ((b2 &
             0xC0U) !=
                0x80U) {

            return z;
        }

        const uint32_t cp =
            ((b0 &
              0x0FU) << 12) |
            ((b1 &
              0x3FU) << 6) |
            (b2 &
             0x3FU);

        if (cp <
                0x800U ||
            (cp >= 0xD800U &&
             cp <= 0xDFFFU)) {

            return z;
        }

        z.kod =
            cp;

        z.bajty =
            3;

        return z;
    }

    if ((b0 &
         0xF8U) ==
            0xF0U) {

        if (s[1] == '\0') {
            return z;
        }

        const uint8_t b1 =
            static_cast<uint8_t>(
                s[1]
            );

        if ((b1 &
             0xC0U) !=
                0x80U ||
            s[2] ==
                '\0') {

            return z;
        }

        const uint8_t b2 =
            static_cast<uint8_t>(
                s[2]
            );

        if ((b2 &
             0xC0U) !=
                0x80U ||
            s[3] ==
                '\0') {

            return z;
        }

        const uint8_t b3 =
            static_cast<uint8_t>(
                s[3]
            );

        if ((b3 &
             0xC0U) !=
                0x80U) {

            return z;
        }

        const uint32_t cp =
            ((b0 &
              0x07U) << 18) |
            ((b1 &
              0x3FU) << 12) |
            ((b2 &
              0x3FU) << 6) |
            (b3 &
             0x3FU);

        if (cp <
                0x10000U ||
            cp >
                0x10FFFFU) {

            return z;
        }

        z.kod =
            cp;

        z.bajty =
            4;

        return z;
    }

    return z;
}

int szerokosc_znaku(
    uint32_t unicode
) {
    int sw =
        static_cast<int>(
            bws_wywolaj(
                24,
                unicode
            )
        );

    if (sw <= 0 ||
        sw > 32) {

        sw =
            8;
    }

    return sw;
}

void wypisz_skalowane(
    int x,
    int y,
    uint32_t kolor,
    int skala,
    const char* tekst
) {
    if (!tekst ||
        skala < 1) {

        return;
    }

    const uint64_t arg =
        (static_cast<uint64_t>(
             static_cast<uint32_t>(
                 skala)) << 32) |
        kolor;

    bws_wywolaj(
        20,
        static_cast<uint64_t>(
            static_cast<int64_t>(
                x
            )
        ),
        static_cast<uint64_t>(
            static_cast<int64_t>(
                y
            )
        ),
        arg,
        reinterpret_cast<uint64_t>(
            tekst
        )
    );
}

/* =========================================================================
 * 8. ZAKLADKI / PAMIEC
 * ========================================================================= */

bool alokuj_tresc_zakladki(
    Zakladka& z
) {
    if (z.tresc) {
        return true;
    }

    z.tresc =
        static_cast<char*>(
            gui_malloc(
                HTML_POJEMNOSC
            )
        );

    if (!z.tresc) {
        return false;
    }

    z.tresc[0] =
        '\0';

    return true;
}

void wyczysc_zakladke(
    Zakladka& z,
    bool zwolnij_tresc
) {
    if (zwolnij_tresc &&
        z.tresc) {

        gui_free(
            z.tresc
        );
    }

    z.tresc =
        nullptr;

    z.url[0] =
        '\0';

    z.przewin_y =
        0;

    z.to_jest_html =
        false;

    z.wczytana =
        false;
}

bool nowa_zakladka() {
    if (liczba_zakladek >=
        MAX_ZAKLADKI) {

        ustaw_status(
            "Osiagnieto limit 6 zakladek."
        );

        return false;
    }

    Zakladka& z =
        zakladki[
            liczba_zakladek
        ];

    wyczysc_zakladke(
        z,
        false
    );

    if (!alokuj_tresc_zakladki(
            z)) {

        ustaw_status(
            "Brak pamieci na nowa zakladke."
        );

        return false;
    }

    aktywna_zakladka =
        liczba_zakladek;

    ++liczba_zakladek;

    max_przewin_y =
        0;

    calkowita_wysokosc_strony =
        0;

    ustaw_status(
        "Nowa zakladka."
    );

    return true;
}

void zamknij_zakladke(
    int indeks
) {
    if (indeks < 0 ||
        indeks >=
            liczba_zakladek) {

        return;
    }

    if (liczba_zakladek <= 1) {
        Zakladka& z =
            zakladki[0];

        z.url[0] =
            '\0';

        if (z.tresc) {
            z.tresc[0] =
                '\0';
        }

        z.przewin_y =
            0;

        z.to_jest_html =
            false;

        z.wczytana =
            false;

        max_przewin_y =
            0;

        ustaw_status(
            "Wyczyszczono zakladke."
        );

        return;
    }

    if (zakladki[indeks].tresc) {
        gui_free(
            zakladki[indeks].tresc
        );

        zakladki[indeks].tresc =
            nullptr;
    }

    /*
     * Przenosimy tylko mala strukture z POINTEREM do tresci.
     * Stara wersja kopiowala tutaj po 256 KiB na kazda zakladke.
     */
    for (int i = indeks;
         i + 1 <
            liczba_zakladek;
         ++i) {

        zakladki[i] =
            zakladki[i + 1];
    }

    --liczba_zakladek;

    /*
     * Ostatni wpis jest duplikatem przeniesionego pointera.
     * Zerujemy go bez free.
     */
    zakladki[
        liczba_zakladek
    ].tresc =
        nullptr;

    zakladki[
        liczba_zakladek
    ].url[0] =
        '\0';

    if (aktywna_zakladka >=
        liczba_zakladek) {

        aktywna_zakladka =
            liczba_zakladek -
            1;
    } else if (aktywna_zakladka >
               indeks) {

        --aktywna_zakladka;
    }

    max_przewin_y =
        0;

    ustaw_status(
        "Zamknieto zakladke."
    );
}

void zwolnij_wszystkie_zakladki() {
    for (int i = 0;
         i < liczba_zakladek;
         ++i) {

        wyczysc_zakladke(
            zakladki[i],
            true
        );
    }

    liczba_zakladek =
        0;

    aktywna_zakladka =
        0;
}

/* =========================================================================
 * 9. HISTORIA / ULUBIONE
 * ========================================================================= */

bool tekst_rowny(
    const char* a,
    const char* b
) {
    if (!a ||
        !b) {

        return false;
    }

    size_t i =
        0;

    while (a[i] != '\0' &&
           b[i] != '\0') {

        if (a[i] !=
            b[i]) {

            return false;
        }

        ++i;
    }

    return
        a[i] ==
        b[i];
}

void dopisz_do_historii(
    const char* url
) {
    if (!url ||
        url[0] ==
            '\0') {

        return;
    }

    if (dlugosc_limit(
            url,
            URL_POJEMNOSC) >=
        URL_POJEMNOSC) {

        ustaw_status(
            "Adres jest zbyt dlugi dla historii."
        );

        return;
    }

    if (historia_idx >= 0 &&
        tekst_rowny(
            historia[
                historia_idx
            ],
            url)) {

        return;
    }

    /*
     * Nawigacja po historii, a potem nowa strona: kasujemy forward branch.
     */
    if (historia_idx <
        historia_max) {

        historia_max =
            historia_idx;
    }

    if (historia_idx + 1 <
        MAX_HISTORIA) {

        ++historia_idx;
    } else {
        for (int i = 1;
             i <
                MAX_HISTORIA;
             ++i) {

            if (!kopiuj_limit(
                historia[
                    i - 1
                ],
                URL_POJEMNOSC,
                historia[i]
            )) {
                historia[i - 1][0] =
                    '\0';
                ustaw_status(
                    "Uszkodzony wpis historii."
                );
                return;
            }
        }

        historia_idx =
            MAX_HISTORIA -
            1;
    }

    if (!kopiuj_limit(
        historia[
            historia_idx
        ],
        URL_POJEMNOSC,
        url
    )) {
        historia[
            historia_idx
        ][0] = '\0';
        ustaw_status(
            "Adres jest zbyt dlugi dla historii."
        );
        return;
    }

    historia_max =
        historia_idx;
}

void wczytaj_ulubione() {
    ulubione_ilosc =
        0;

    wyzeruj(
        plik_ulubionych,
        sizeof(
            plik_ulubionych
        )
    );

    if (!czytaj_plik(
            "/uzytkownicy/zakladki.txt",
            plik_ulubionych,
            static_cast<uint32_t>(
                sizeof(
                    plik_ulubionych
                ) -
                1U
            )
        )) {

        return;
    }

    plik_ulubionych[
        sizeof(
            plik_ulubionych
        ) -
        1U
    ] =
        '\0';

    size_t i =
        0;

    while (plik_ulubionych[i] != '\0' &&
           ulubione_ilosc <
               MAX_ULUBIONE) {

        while (plik_ulubionych[i] ==
                   '\r' ||
               plik_ulubionych[i] ==
                   '\n') {

            ++i;
        }

        if (plik_ulubionych[i] ==
            '\0') {

            break;
        }

        size_t j =
            0;
        bool za_dlugi=false;

        while (plik_ulubionych[i] != '\0' &&
               plik_ulubionych[i] != '\r' &&
               plik_ulubionych[i] != '\n') {

            if (j + 1U <
                URL_POJEMNOSC) {

                ulubione[
                    ulubione_ilosc
                ][j++] =
                    plik_ulubionych[i];
            } else za_dlugi=true;

            ++i;
        }

        ulubione[
            ulubione_ilosc
        ][j] =
            '\0';

        if (j > 0&&!za_dlugi) {
            ++ulubione_ilosc;
        }else if(za_dlugi){
            ulubione[ulubione_ilosc][0]='\0';
            ustaw_status("Pominieto zbyt dlugi URL w pliku zakladek.");
        }
    }
}

void zapisz_ulubione() {
    size_t p =
        0;

    for (int u = 0;
         u <
            ulubione_ilosc;
         ++u) {

        for (size_t i = 0;
             ulubione[u][i] != '\0';
             ++i) {

            if (p + 2U >
                sizeof(
                    plik_ulubionych
                )) {

                ustaw_status(
                    "Lista zakladek jest zbyt duza."
                );

                return;
            }

            plik_ulubionych[
                p++
            ] =
                ulubione[u][i];
        }

        if (p + 2U >
            sizeof(
                plik_ulubionych
            )) {

            ustaw_status(
                "Lista zakladek jest zbyt duza."
            );

            return;
        }

        plik_ulubionych[
            p++
        ] =
            '\n';
    }

    plik_ulubionych[p] =
        '\0';

    const char* sciezka =
        "/uzytkownicy/zakladki.txt";

    /*
     * `utworz()` moze zwrocic false, gdy plik juz istnieje.
     * O powodzeniu decyduje faktyczny zapis.
     */
    (void)utworz(
        sciezka
    );

    if (zapisz_plik(
            sciezka,
            plik_ulubionych,
            static_cast<uint32_t>(
                p
            )
        )) {

        ustaw_status(
            "Zapisano zakladki."
        );
    } else {
        ustaw_status(
            "Blad zapisu zakladek."
        );
    }
}

void dodaj_obecna_do_ulubionych() {
    if (liczba_zakladek <= 0) {
        return;
    }

    const char* url =
        zakladki[
            aktywna_zakladka
        ].url;

    if (!url ||
        url[0] ==
            '\0') {

        ustaw_status(
            "Brak adresu do zapisania."
        );

        return;
    }

    for (int i = 0;
         i <
            ulubione_ilosc;
         ++i) {

        if (tekst_rowny(
                ulubione[i],
                url)) {

            ustaw_status(
                "Adres jest juz w zakladkach."
            );

            return;
        }
    }

    if (ulubione_ilosc >=
        MAX_ULUBIONE) {

        ustaw_status(
            "Lista zakladek jest pelna."
        );

        return;
    }

    if (!kopiuj_limit(
        ulubione[
            ulubione_ilosc
        ],
        URL_POJEMNOSC,
        url
    )) {
        ulubione[
            ulubione_ilosc
        ][0] = '\0';
        ustaw_status(
            "Adres jest zbyt dlugi dla zakladek."
        );
        return;
    }

    ++ulubione_ilosc;

    zapisz_ulubione();
}

/* =========================================================================
 * 10. URL / WYSZUKIWANIE
 * ========================================================================= */

bool url_ma_biale_znaki(
    const char* tekst
) {
    if (!tekst) {
        return false;
    }

    for (size_t i = 0;
         tekst[i] != '\0';
         ++i) {

        if (ascii_biala(
                tekst[i])) {

            return true;
        }
    }

    return false;
}

bool url_zaczyna_sie_schematem(
    const char* tekst
) {
    return
        prefiks_ci(
            tekst,
            "http://"
        ) ||
        prefiks_ci(
            tekst,
            "https://"
        );
}

bool query_unreserved(
    uint8_t c
) {
    return
        (c >= 'A' &&
         c <= 'Z') ||
        (c >= 'a' &&
         c <= 'z') ||
        (c >= '0' &&
         c <= '9') ||
        c == '-' ||
        c == '_' ||
        c == '.' ||
        c == '~';
}

char hex_cyfra(
    uint8_t n
) {
    static constexpr char HEX[] =
        "0123456789ABCDEF";

    return
        HEX[
            n &
            0x0FU
        ];
}

bool wejscie_jest_hostem(const char* tekst) {
    if (!tekst || tekst[0] == '\0' || url_ma_biale_znaki(tekst)) return false;
    bool poprzedni_separator = true;
    bool port = false;
    for (size_t i = 0; tekst[i] != '\0'; ++i) {
        const char c = tekst[i];
        if (c == ':') {
            if (port || poprzedni_separator || tekst[i + 1] == '\0') return false;
            port = true;
            poprzedni_separator = true;
            continue;
        }
        if (port) {
            if (c < '0' || c > '9') return false;
            poprzedni_separator = false;
            continue;
        }
        const bool alnum = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9');
        if (alnum) { poprzedni_separator = false; continue; }
        if (c == '.' || c == '-') {
            if (poprzedni_separator || tekst[i + 1] == '\0') return false;
            poprzedni_separator = true;
            continue;
        }
        return false;
    }
    return !poprzedni_separator;
}

bool przygotuj_adres_wyszukiwania(
    char* url
) {
    if (!url ||
        url[0] ==
            '\0') {

        return false;
    }

    if (url_zaczyna_sie_schematem(
            url)) {

        return true;
    }

    if (wejscie_jest_hostem(url)) {

        return true;
    }

    char wejscie[
        URL_POJEMNOSC
    ] = {};

    if (!kopiuj_limit(
        wejscie,
        sizeof(wejscie),
        url
    )) {
        return false;
    }

    static constexpr char PREFIKS[] =
        "https://html.duckduckgo.com/html/?q=";

    size_t p =
        0;

    for (size_t i = 0;
         PREFIKS[i] != '\0';
         ++i) {

        if (p + 1U >=
            URL_POJEMNOSC) {

            return false;
        }

        url[p++] =
            PREFIKS[i];
    }

    for (size_t i = 0;
         wejscie[i] != '\0';
         ++i) {

        const uint8_t c =
            static_cast<uint8_t>(
                wejscie[i]
            );

        if (query_unreserved(
                c)) {

            if (p + 1U >=
                URL_POJEMNOSC) {

                return false;
            }

            url[p++] =
                static_cast<char>(
                    c
                );

            continue;
        }

        if (p + 3U >=
            URL_POJEMNOSC) {

            return false;
        }

        url[p++] =
            '%';

        url[p++] =
            hex_cyfra(
                c >>
                4
            );

        url[p++] =
            hex_cyfra(
                c
            );
    }

    url[p] =
        '\0';

    return true;
}

bool domena_znak_poprawny(
    char c
) {
    return
        (c >= 'a' &&
         c <= 'z') ||
        (c >= 'A' &&
         c <= 'Z') ||
        (c >= '0' &&
         c <= '9') ||
        c == '-' ||
        c == '.';
}

bool parsuj_url(
    const char* url,
    ParsedUrl* wynik
) {
    if (!url ||
        !wynik ||
        url[0] ==
            '\0') {

        return false;
    }

    size_t i =
        0;

    if (prefiks_ci(
            url,
            "https://")) {

        wynik->https =
            true;

        i =
            8U;
    } else if (prefiks_ci(
                   url,
                   "http://")) {

        wynik->https =
            false;

        i =
            7U;
    } else {
        return false;
    }

    if (url[i] ==
        '\0') {

        return false;
    }

    size_t d =
        0;

    while (url[i] != '\0' &&
           url[i] != '/' &&
           url[i] != '?' &&
           url[i] != '#') {

        const char c =
            url[i];

        /*
         * Userinfo oraz niestandardowe porty nie sa obecnie wspierane przez
         * BWS29/BWS30, bo port jest na stale 80/443 w kernelu.
         */
        if (c == '@' ||
            c == ':') {

            return false;
        }

        if (!domena_znak_poprawny(
                c)) {

            return false;
        }

        if (d + 1U >=
            sizeof(
                wynik->domena
            )) {

            return false;
        }

        wynik->domena[d++] =
            c;

        ++i;
    }

    if (d == 0 ||
        wynik->domena[0] ==
            '.' ||
        wynik->domena[
            d - 1U] ==
            '.') {

        return false;
    }

    wynik->domena[d] =
        '\0';

    size_t p =
        0;

    if (url[i] ==
            '\0' ||
        url[i] ==
            '#') {

        wynik->sciezka[0] =
            '/';

        wynik->sciezka[1] =
            '\0';

        return true;
    }

    if (url[i] ==
        '?') {

        wynik->sciezka[p++] =
            '/';
    }

    while (url[i] != '\0' &&
           url[i] != '#') {

        const uint8_t c =
            static_cast<uint8_t>(
                url[i]
            );

        if (c < 0x20U ||
            c ==
                0x7FU) {

            return false;
        }

        if (p + 1U >=
            sizeof(
                wynik->sciezka
            )) {

            return false;
        }

        wynik->sciezka[p++] =
            static_cast<char>(
                c
            );

        ++i;
    }

    wynik->sciezka[p] =
        '\0';

    return
        p > 0;
}

/* =========================================================================
 * 11. HTTP HEADERS
 * ========================================================================= */

const char* znajdz_koniec_naglowkow(
    const char* dane,
    size_t dlugosc,
    size_t* body_offset
) {
    if (!dane ||
        !body_offset) {

        return nullptr;
    }

    for (size_t i = 0;
         i + 3U <
             dlugosc;
         ++i) {

        if (dane[i] ==
                '\r' &&
            dane[i + 1U] ==
                '\n' &&
            dane[i + 2U] ==
                '\r' &&
            dane[i + 3U] ==
                '\n') {

            *body_offset =
                i +
                4U;

            return
                dane +
                *body_offset;
        }
    }

    for (size_t i = 0;
         i + 1U <
             dlugosc;
         ++i) {

        if (dane[i] ==
                '\n' &&
            dane[i + 1U] ==
                '\n') {

            *body_offset =
                i +
                2U;

            return
                dane +
                *body_offset;
        }
    }

    return nullptr;
}

bool nazwa_headera_rowna(
    const char* linia,
    size_t nazwa_len,
    const char* oczekiwana
) {
    return
        tekst_rowny_ci(
            linia,
            nazwa_len,
            oczekiwana
        );
}

bool znajdz_header(
    const char* dane,
    size_t naglowki_len,
    const char* nazwa,
    char* wynik,
    size_t pojemnosc
) {
    if (!dane ||
        !nazwa ||
        !wynik ||
        pojemnosc == 0) {

        return false;
    }

    wynik[0] =
        '\0';

    size_t i =
        0;

    /*
     * Pomin status-line.
     */
    while (i < naglowki_len &&
           dane[i] != '\n') {

        ++i;
    }

    if (i < naglowki_len) {
        ++i;
    }

    while (i < naglowki_len) {
        const size_t line_start =
            i;

        while (i < naglowki_len &&
               dane[i] != '\n') {

            ++i;
        }

        size_t line_end =
            i;

        if (line_end >
                line_start &&
            dane[
                line_end -
                1U] ==
                '\r') {

            --line_end;
        }

        if (line_end ==
            line_start) {

            return false;
        }

        size_t colon =
            line_start;

        while (colon < line_end &&
               dane[colon] != ':') {

            ++colon;
        }

        if (colon < line_end &&
            nazwa_headera_rowna(
                dane +
                    line_start,
                colon -
                    line_start,
                nazwa)) {

            size_t value =
                colon +
                1U;

            while (value < line_end &&
                   (dane[value] ==
                        ' ' ||
                    dane[value] ==
                        '\t')) {

                ++value;
            }

            size_t value_end =
                line_end;

            while (value_end > value &&
                   (dane[
                        value_end -
                        1U] ==
                        ' ' ||
                    dane[
                        value_end -
                        1U] ==
                        '\t')) {

                --value_end;
            }

            const size_t n =
                value_end -
                value;

            const size_t kopia =
                n <
                        pojemnosc -
                        1U
                    ? n
                    : pojemnosc -
                        1U;

            for (size_t k = 0;
                 k < kopia;
                 ++k) {

                wynik[k] =
                    dane[
                        value +
                        k
                    ];
            }

            wynik[kopia] =
                '\0';

            return
                kopia ==
                n;
        }

        if (i < naglowki_len) {
            ++i;
        }
    }

    return false;
}

bool tekst_zawiera_ci(
    const char* tekst,
    const char* fragment
) {
    if (!tekst ||
        !fragment ||
        fragment[0] ==
            '\0') {

        return false;
    }

    for (size_t i = 0;
         tekst[i] != '\0';
         ++i) {

        size_t j =
            0;

        while (fragment[j] != '\0' &&
               tekst[i + j] != '\0' &&
               ascii_mala(
                   tekst[i + j]) ==
               ascii_mala(
                   fragment[j])) {

            ++j;
        }

        if (fragment[j] ==
            '\0') {

            return true;
        }
    }

    return false;
}

bool parsuj_odpowiedz_http(
    const char* dane,
    size_t dlugosc,
    OdpowiedzHttp* wynik
) {
    if (!dane ||
        !wynik ||
        dlugosc == 0) {

        return false;
    }

    wynik->kod =
        wyciagnij_kod_http(
            dane,
            dlugosc
        );

    if (!http_kod_poprawny(
            wynik->kod)) {

        return false;
    }

    size_t body_offset =
        0;

    wynik->body =
        znajdz_koniec_naglowkow(
            dane,
            dlugosc,
            &body_offset
        );

    if (!wynik->body ||
        body_offset >
            dlugosc) {

        return false;
    }

    wynik->body_len =
        dlugosc -
        body_offset;

    char transfer[
        64
    ] = {};

    wynik->chunked =
        znajdz_header(
            dane,
            body_offset,
            "transfer-encoding",
            transfer,
            sizeof(transfer)
        ) &&
        tekst_zawiera_ci(
            transfer,
            "chunked"
        );

    char content_type[
        96
    ] = {};

    const bool ma_typ =
        znajdz_header(
            dane,
            body_offset,
            "content-type",
            content_type,
            sizeof(content_type)
        );

    wynik->html =
        ma_typ &&
        (tekst_zawiera_ci(
             content_type,
             "text/html") ||
         tekst_zawiera_ci(
             content_type,
             "application/xhtml"));

    wynik->tekst =
        !ma_typ ||
        wynik->html ||
        tekst_zawiera_ci(
            content_type,
            "text/plain"
        ) ||
        tekst_zawiera_ci(
            content_type,
            "application/json"
        ) ||
        tekst_zawiera_ci(
            content_type,
            "application/xml"
        );

    if (!ma_typ &&
        wynik->body_len > 0) {

        size_t i =
            0;

        while (i <
                   wynik->body_len &&
               ascii_biala(
                   wynik->body[i])) {

            ++i;
        }

        if (i <
                wynik->body_len &&
            wynik->body[i] ==
                '<') {

            wynik->html =
                true;
        }
    }

    char encoding[
        64
    ] = {};

    if (znajdz_header(
            dane,
            body_offset,
            "content-encoding",
            encoding,
            sizeof(encoding)
        ) &&
        encoding[0] != '\0' &&
        !tekst_zawiera_ci(
            encoding,
            "identity"
        )) {

        /*
         * Kernel GET nie prosi o gzip, ale jezeli serwer mimo to kompresuje,
         * nie probujemy renderowac skompresowanych bajtow jako HTML.
         */
        wynik->tekst =
            false;
    }

    return true;
}

/* =========================================================================
 * 12. HTTP CHUNKED
 * ========================================================================= */

int hex_wartosc(
    char c
) {
    if (c >= '0' &&
        c <= '9') {

        return
            c -
            '0';
    }

    c =
        ascii_mala(
            c
        );

    if (c >= 'a' &&
        c <= 'f') {

        return
            c -
            'a' +
            10;
    }

    return -1;
}

bool dekoduj_chunked(
    const char* wejscie,
    size_t wej_len,
    char* wyjscie,
    size_t wyj_cap,
    size_t* wyj_len
) {
    if (!wejscie ||
        !wyjscie ||
        wyj_cap == 0 ||
        !wyj_len) {

        return false;
    }

    size_t in =
        0;

    size_t out =
        0;

    for (;;) {
        uint64_t chunk =
            0;

        bool ma_cyfre =
            false;

        while (in < wej_len) {
            const char c =
                wejscie[in];

            if (c == ';') {
                while (in < wej_len &&
                       wejscie[in] != '\n') {

                    ++in;
                }

                break;
            }

            if (c == '\r' ||
                c == '\n') {

                break;
            }

            const int hv =
                hex_wartosc(
                    c
                );

            if (hv < 0) {
                return false;
            }

            ma_cyfre =
                true;

            if (chunk >
                (UINT64_MAX -
                 static_cast<uint64_t>(
                     hv)) /
                    16ULL) {

                return false;
            }

            chunk =
                chunk *
                16ULL +
                static_cast<uint64_t>(
                    hv
                );

            ++in;
        }

        if (!ma_cyfre) {
            return false;
        }

        /*
         * Do konca linii rozmiaru.
         */
        while (in < wej_len &&
               wejscie[in] != '\n') {

            ++in;
        }

        if (in >= wej_len) {
            return false;
        }

        ++in;

        if (chunk == 0) {
            if (out >= wyj_cap) {
                return false;
            }

            wyjscie[out] =
                '\0';

            *wyj_len =
                out;

            return true;
        }

        if (chunk >
            wej_len -
            in) {

            return false;
        }

        if (chunk >
            wyj_cap -
            1U -
            out) {

            return false;
        }

        for (uint64_t j = 0;
             j < chunk;
             ++j) {

            wyjscie[
                out++
            ] =
                wejscie[
                    in++
                ];
        }

        if (in < wej_len &&
            wejscie[in] ==
                '\r') {

            ++in;
        }

        if (in >= wej_len ||
            wejscie[in] !=
                '\n') {

            return false;
        }

        ++in;
    }
}

/* =========================================================================
 * 13. REDIRECT
 * ========================================================================= */

__attribute__((noinline)) bool ustaw_url_z_location(
    char* cel,
    size_t pojemnosc,
    const char* location,
    const ParsedUrl& obecny
) {
    if (!cel ||
        pojemnosc == 0 ||
        !location ||
        location[0] ==
            '\0') {

        return false;
    }

    if (prefiks_ci(
            location,
            "http://") ||
        prefiks_ci(
            location,
            "https://")) {

        return
            kopiuj_limit(
                cel,
                pojemnosc,
                location
            );
    }

    char wynik[
        URL_POJEMNOSC
    ] = {};

    size_t p =
        0;

    const char* scheme =
        obecny.https
            ? "https://"
            : "http://";

    auto append = [&wynik, &p](
        const char* s
    ) -> bool {
        if (!s) {
            return false;
        }

        for (size_t i = 0;
             s[i] != '\0';
             ++i) {

            if (p + 1U >=
                sizeof(wynik)) {

                return false;
            }

            wynik[p++] =
                s[i];
        }

        wynik[p] =
            '\0';

        return true;
    };

    /*
     * Scheme-relative URL: //host/path
     */
    if (location[0] ==
            '/' &&
        location[1] ==
            '/') {

        const char* scheme_only =
            obecny.https
                ? "https:"
                : "http:";

        if (!append(
                scheme_only) ||
            !append(
                location)) {

            return false;
        }

        return
            kopiuj_limit(
                cel,
                pojemnosc,
                wynik
            );
    }

    if (!append(
            scheme) ||
        !append(
            obecny.domena)) {

        return false;
    }

    if (location[0] ==
        '/') {

        if (!append(
                location)) {

            return false;
        }

        return
            kopiuj_limit(
                cel,
                pojemnosc,
                wynik
            );
    }

    /*
     * Relative path: /katalog/stara -> /katalog/nowa
     */
    char katalog[
        SCIEZKA_POJEMNOSC
    ] = {};

    size_t slash =
        0;

    for (size_t i = 0;
         obecny.sciezka[i] != '\0';
         ++i) {

        if (obecny.sciezka[i] ==
            '/') {

            slash =
                i;
        }

        if (obecny.sciezka[i] ==
            '?' ||
            obecny.sciezka[i] ==
            '#') {

            break;
        }
    }

    size_t kp =
        0;

    for (size_t i = 0;
         i <= slash &&
         obecny.sciezka[i] != '\0';
         ++i) {

        if (kp + 1U >=
            sizeof(katalog)) {

            return false;
        }

        katalog[kp++] =
            obecny.sciezka[i];
    }

    if (kp == 0) {
        katalog[kp++] =
            '/';
    }

    katalog[kp] =
        '\0';

    if (!append(
            katalog) ||
        !append(
            location)) {

        return false;
    }

    return
        kopiuj_limit(
            cel,
            pojemnosc,
            wynik
        );
}

/* =========================================================================
 * 14. RENDERER TEKSTU
 * ========================================================================= */

struct Renderer {
    int x;
    int y;
    int szer;
    int wys;
    int przewin;

    int logiczny_x;
    int logiczny_y;

    int line_height;

    uint32_t kolor;
    int skala;

    bool ostatnia_spacja;
};

void renderer_nowa_linia(
    Renderer& r,
    int dodatkowo = 0
) {
    r.logiczny_x =
        0;

    r.logiczny_y +=
        r.line_height *
        r.skala +
        dodatkowo;

    r.ostatnia_spacja =
        false;
}

void renderer_znak(
    Renderer& r,
    const char* bytes,
    uint32_t unicode,
    int bytes_count
) {
    if (!bytes ||
        bytes_count <= 0) {

        return;
    }

    const int sw =
        szerokosc_znaku(
            unicode
        );

    const int pelna =
        (sw +
         1) *
        r.skala;

    if (r.logiczny_x +
            pelna >
        r.szer) {

        renderer_nowa_linia(
            r
        );
    }

    const int draw_y =
        r.y +
        r.logiczny_y -
        r.przewin;

    if (draw_y +
            r.line_height *
            r.skala >=
            r.y &&
        draw_y <
            r.y +
            r.wys) {

        char temp[
            5
        ] = {};

        for (int i = 0;
             i < bytes_count &&
             i < 4;
             ++i) {

            temp[i] =
                bytes[i];
        }

        wypisz_skalowane(
            r.x +
                r.logiczny_x,
            draw_y,
            r.kolor,
            r.skala,
            temp
        );
    }

    r.logiczny_x +=
        pelna;
}

void renderer_spacja(
    Renderer& r
) {
    if (r.logiczny_x == 0 ||
        r.ostatnia_spacja) {

        return;
    }

    r.logiczny_x +=
        8 *
        r.skala;

    r.ostatnia_spacja =
        true;
}

void renderer_tekst(
    Renderer& r,
    const char* tekst
) {
    if (!tekst) {
        return;
    }

    for (size_t i = 0;
         tekst[i] != '\0';) {

        const char c =
            tekst[i];

        if (c == '\r') {
            ++i;
            continue;
        }

        if (c == '\n') {
            renderer_nowa_linia(
                r
            );

            ++i;
            continue;
        }

        if (c == ' ' ||
            c == '\t') {

            renderer_spacja(
                r
            );

            ++i;
            continue;
        }

        r.ostatnia_spacja =
            false;

        const Utf8Znak z =
            dekoduj_utf8(
                tekst +
                i
            );

        renderer_znak(
            r,
            tekst +
                i,
            z.kod,
            z.bajty
        );

        i +=
            static_cast<size_t>(
                z.bajty
            );
    }
}

/* =========================================================================
 * 15. HTML PARSER/RENDERER
 * ========================================================================= */

bool tag_nazwa_znak(
    char c
) {
    return
        (c >= 'a' &&
         c <= 'z') ||
        (c >= 'A' &&
         c <= 'Z') ||
        (c >= '0' &&
         c <= '9');
}

bool tag_rowny(
    const char* name,
    size_t len,
    const char* expected
) {
    return
        tekst_rowny_ci(
            name,
            len,
            expected
        );
}

bool html_tag_blokowy(
    const char* name,
    size_t len
) {
    return
        tag_rowny(
            name,
            len,
            "p") ||
        tag_rowny(
            name,
            len,
            "div") ||
        tag_rowny(
            name,
            len,
            "section") ||
        tag_rowny(
            name,
            len,
            "article") ||
        tag_rowny(
            name,
            len,
            "header") ||
        tag_rowny(
            name,
            len,
            "footer") ||
        tag_rowny(
            name,
            len,
            "li") ||
        tag_rowny(
            name,
            len,
            "tr") ||
        tag_rowny(
            name,
            len,
            "blockquote") ||
        tag_rowny(
            name,
            len,
            "pre");
}

bool html_heading(
    const char* name,
    size_t len
) {
    return
        tag_rowny(
            name,
            len,
            "h1") ||
        tag_rowny(
            name,
            len,
            "h2") ||
        tag_rowny(
            name,
            len,
            "h3");
}

bool html_entity(
    const char* s,
    uint32_t* unicode,
    size_t* zuzyto
) {
    if (!s ||
        s[0] != '&' ||
        !unicode ||
        !zuzyto) {

        return false;
    }

    struct Encja {
        const char* nazwa;
        uint32_t kod;
    };

    static constexpr Encja ENCJE[] = {
        {"&amp;",  '&'},
        {"&lt;",   '<'},
        {"&gt;",   '>'},
        {"&quot;", '"'},
        {"&#39;",  '\''},
        {"&apos;", '\''},
        {"&nbsp;", ' '}
    };

    for (const Encja& e :
         ENCJE) {

        size_t i =
            0;

        while (e.nazwa[i] != '\0' &&
               s[i] != '\0' &&
               e.nazwa[i] ==
                   s[i]) {

            ++i;
        }

        if (e.nazwa[i] ==
            '\0') {

            *unicode =
                e.kod;

            *zuzyto =
                i;

            return true;
        }
    }

    if (s[1] ==
        '#') {

        size_t i =
            2;

        bool hex =
            false;

        if (s[i] ==
                'x' ||
            s[i] ==
                'X') {

            hex =
                true;

            ++i;
        }

        uint32_t value =
            0;

        bool any =
            false;

        while (s[i] != '\0' &&
               s[i] != ';') {

            int digit =
                -1;

            if (hex) {
                digit =
                    hex_wartosc(
                        s[i]
                    );
            } else if (s[i] >= '0' &&
                       s[i] <= '9') {

                digit =
                    s[i] -
                    '0';
            }

            if (digit < 0) {
                return false;
            }

            const uint32_t base =
                hex
                    ? 16U
                    : 10U;

            if (value >
                (0x10FFFFU -
                 static_cast<uint32_t>(
                     digit)) /
                    base) {

                return false;
            }

            value =
                value *
                base +
                static_cast<uint32_t>(
                    digit
                );

            any =
                true;

            ++i;
        }

        if (!any ||
            s[i] != ';' ||
            value == 0 ||
            value >
                0x10FFFFU ||
            (value >= 0xD800U &&
             value <= 0xDFFFU)) {

            return false;
        }

        *unicode =
            value;

        *zuzyto =
            i +
            1U;

        return true;
    }

    return false;
}

int utf8_zapisz(
    uint32_t cp,
    char out[5]
) {
    if (!out) {
        return 0;
    }

    out[0] =
        '\0';

    if (cp <=
        0x7FU) {

        out[0] =
            static_cast<char>(
                cp
            );

        out[1] =
            '\0';

        return 1;
    }

    if (cp <=
        0x7FFU) {

        out[0] =
            static_cast<char>(
                0xC0U |
                (cp >> 6)
            );

        out[1] =
            static_cast<char>(
                0x80U |
                (cp &
                 0x3FU)
            );

        out[2] =
            '\0';

        return 2;
    }

    if (cp <=
        0xFFFFU) {

        out[0] =
            static_cast<char>(
                0xE0U |
                (cp >> 12)
            );

        out[1] =
            static_cast<char>(
                0x80U |
                ((cp >> 6) &
                 0x3FU)
            );

        out[2] =
            static_cast<char>(
                0x80U |
                (cp &
                 0x3FU)
            );

        out[3] =
            '\0';

        return 3;
    }

    out[0] =
        static_cast<char>(
            0xF0U |
            (cp >> 18)
        );

    out[1] =
        static_cast<char>(
            0x80U |
            ((cp >> 12) &
             0x3FU)
        );

    out[2] =
        static_cast<char>(
            0x80U |
            ((cp >> 6) &
             0x3FU)
        );

    out[3] =
        static_cast<char>(
            0x80U |
            (cp &
             0x3FU)
        );

    out[4] =
        '\0';

    return 4;
}

void renderuj_html(
    Renderer& r,
    const char* html
) {
    if (!html) {
        return;
    }

    enum class Ukryte {
        Nic,
        Head,
        Script,
        Style,
        Svg
    };

    Ukryte ukryte =
        Ukryte::Nic;

    int poprzednia_skala[
        16
    ] = {};

    int stack =
        0;

    bool widziano_body =
        false;

    bool w_body =
        false;

    for (size_t i = 0;
         html[i] != '\0';) {

        if (html[i] ==
            '<') {

            /*
             * Komentarz <!-- ... -->
             */
            if (html[i + 1U] ==
                    '!' &&
                html[i + 2U] ==
                    '-' &&
                html[i + 3U] ==
                    '-') {

                i +=
                    4U;

                while (html[i] != '\0' &&
                       !(html[i] ==
                             '-' &&
                         html[i + 1U] ==
                             '-' &&
                         html[i + 2U] ==
                             '>')) {

                    ++i;
                }

                if (html[i] != '\0') {
                    i +=
                        3U;
                }

                continue;
            }

            size_t p =
                i +
                1U;

            while (ascii_biala(
                       html[p])) {

                ++p;
            }

            bool closing =
                false;

            if (html[p] ==
                '/') {

                closing =
                    true;

                ++p;

                while (ascii_biala(
                           html[p])) {

                    ++p;
                }
            }

            const size_t name_start =
                p;

            while (tag_nazwa_znak(
                       html[p])) {

                ++p;
            }

            const size_t name_len =
                p -
                name_start;

            bool quote =
                false;

            char quote_char =
                '\0';

            size_t end =
                p;

            while (html[end] != '\0') {
                const char c =
                    html[end];

                if (quote) {
                    if (c ==
                        quote_char) {

                        quote =
                            false;
                    }
                } else if (c ==
                               '"' ||
                           c ==
                               '\'') {

                    quote =
                        true;

                    quote_char =
                        c;
                } else if (c ==
                           '>') {

                    ++end;
                    break;
                }

                ++end;
            }

            if (name_len == 0) {
                i =
                    end;

                continue;
            }

            const char* name =
                html +
                name_start;

            const bool is_head =
                tag_rowny(
                    name,
                    name_len,
                    "head"
                );

            const bool is_script =
                tag_rowny(
                    name,
                    name_len,
                    "script"
                );

            const bool is_style =
                tag_rowny(
                    name,
                    name_len,
                    "style"
                );

            const bool is_svg =
                tag_rowny(
                    name,
                    name_len,
                    "svg"
                );

            const bool is_body =
                tag_rowny(
                    name,
                    name_len,
                    "body"
                );

            if (is_body) {
                widziano_body =
                    true;

                w_body =
                    !closing;

                i =
                    end;

                continue;
            }

            Ukryte rodzaj =
                Ukryte::Nic;

            if (is_head) {
                rodzaj =
                    Ukryte::Head;
            } else if (is_script) {
                rodzaj =
                    Ukryte::Script;
            } else if (is_style) {
                rodzaj =
                    Ukryte::Style;
            } else if (is_svg) {
                rodzaj =
                    Ukryte::Svg;
            }

            if (ukryte !=
                Ukryte::Nic) {

                if (closing &&
                    rodzaj ==
                        ukryte) {

                    ukryte =
                        Ukryte::Nic;
                }

                i =
                    end;

                continue;
            }

            if (!closing &&
                rodzaj !=
                    Ukryte::Nic) {

                ukryte =
                    rodzaj;

                i =
                    end;

                continue;
            }

            /*
             * Jezeli HTML nie ma jawnego <body>, renderujemy zawartosc
             * poza head/script/style - czeste w prostych stronach/errorach.
             */
            const bool renderowac =
                !widziano_body ||
                w_body;

            if (renderowac) {
                if (tag_rowny(
                        name,
                        name_len,
                        "br")) {

                    renderer_nowa_linia(
                        r
                    );
                }

                if (html_heading(
                        name,
                        name_len)) {

                    if (!closing) {
                        if (r.logiczny_x != 0) {
                            renderer_nowa_linia(
                                r,
                                4
                            );
                        }

                        if (stack <
                            16) {

                            poprzednia_skala[
                                stack++
                            ] =
                                r.skala;
                        }

                        r.skala =
                            tag_rowny(
                                name,
                                name_len,
                                "h1")
                                ? 2
                                : 1;
                    } else {
                        renderer_nowa_linia(
                            r,
                            6
                        );

                        if (stack > 0) {
                            r.skala =
                                poprzednia_skala[
                                    --stack
                                ];
                        } else {
                            r.skala =
                                1;
                        }
                    }
                } else if (html_tag_blokowy(
                               name,
                               name_len)) {

                    if (closing ||
                        r.logiczny_x != 0) {

                        renderer_nowa_linia(
                            r,
                            4
                        );
                    }
                }

                if (!closing &&
                    tag_rowny(
                        name,
                        name_len,
                        "li")) {

                    renderer_tekst(
                        r,
                        "- "
                    );
                }
            }

            i =
                end;

            continue;
        }

        const bool renderowac =
            ukryte ==
                Ukryte::Nic &&
            (!widziano_body ||
             w_body);

        if (!renderowac) {
            ++i;
            continue;
        }

        if (html[i] ==
            '&') {

            uint32_t cp =
                0;

            size_t used =
                0;

            if (html_entity(
                    html +
                        i,
                    &cp,
                    &used)) {

                if (cp ==
                    ' ') {

                    renderer_spacja(
                        r
                    );
                } else {
                    char encoded[
                        5
                    ] = {};

                    const int n =
                        utf8_zapisz(
                            cp,
                            encoded
                        );

                    renderer_znak(
                        r,
                        encoded,
                        cp,
                        n
                    );

                    r.ostatnia_spacja =
                        false;
                }

                i +=
                    used;

                continue;
            }
        }

        const char c =
            html[i];

        if (ascii_biala(
                c)) {

            renderer_spacja(
                r
            );

            ++i;

            continue;
        }

        const Utf8Znak z =
            dekoduj_utf8(
                html +
                i
            );

        renderer_znak(
            r,
            html +
                i,
            z.kod,
            z.bajty
        );

        r.ostatnia_spacja =
            false;

        i +=
            static_cast<size_t>(
                z.bajty
            );
    }
}

void przelicz_scroll(
    const Renderer& r,
    int obszar_h
) {
    calkowita_wysokosc_strony =
        r.logiczny_y +
        r.line_height *
        r.skala +
        8;

    if (calkowita_wysokosc_strony >
        obszar_h) {

        max_przewin_y =
            calkowita_wysokosc_strony -
            obszar_h;
    } else {
        max_przewin_y =
            0;
    }

    if (liczba_zakladek <= 0) {
        return;
    }

    int& scroll =
        zakladki[
            aktywna_zakladka
        ].przewin_y;

    if (scroll < 0) {
        scroll =
            0;
    }

    if (scroll >
        max_przewin_y) {

        scroll =
            max_przewin_y;
    }
}

/* =========================================================================
 * 16. GUI - PRZYCISK / URL WIDTH
 * ========================================================================= */

int szerokosc_utf8(
    const char* tekst,
    int skala = 1
) {
    if (!tekst) {
        return 0;
    }

    int width =
        0;

    for (size_t i = 0;
         tekst[i] != '\0';) {

        const Utf8Znak z =
            dekoduj_utf8(
                tekst +
                i
            );

        width +=
            (szerokosc_znaku(
                 z.kod) +
             1) *
            skala;

        i +=
            static_cast<size_t>(
                z.bajty
            );
    }

    return width;
}

void RysujPrzyciskLokalny(
    int x,
    int y,
    int w,
    int h,
    uint32_t bg,
    uint32_t fg,
    const char* tekst
) {
    gui_rysuj_prostokat(
        x,
        y,
        w,
        h,
        bg
    );

    const int tw =
        szerokosc_utf8(
            tekst
        );

    const int tx =
        x +
        (w -
         tw) /
            2;

    int ty =
        y +
        (h -
         16) /
            2;

    if (ty <
        y) {

        ty =
            y;
    }

    gui_wypisz_tekst_kolor(
        tx,
        ty,
        fg,
        tekst
    );
}

/* =========================================================================
 * 17. LAYER/WINDOW
 * ========================================================================= */

void ogranicz_okno_do_ekranu() {
    if (screen_w <
        MIN_WIN_W) {

        WIN_W =
            screen_w;
    } else if (WIN_W <
               MIN_WIN_W) {

        WIN_W =
            MIN_WIN_W;
    } else if (WIN_W >
               screen_w) {

        WIN_W =
            screen_w;
    }

    const int max_h =
        screen_h -
        PASEK_SYSTEMOWY_WYS;

    if (max_h <
        MIN_WIN_H) {

        WIN_H =
            max_h;
    } else if (WIN_H <
               MIN_WIN_H) {

        WIN_H =
            MIN_WIN_H;
    } else if (WIN_H >
               max_h) {

        WIN_H =
            max_h;
    }

    if (WIN_X < 0) {
        WIN_X =
            0;
    }

    if (WIN_Y < 0) {
        WIN_Y =
            0;
    }

    if (WIN_X +
            WIN_W >
        screen_w) {

        WIN_X =
            screen_w -
            WIN_W;
    }

    if (WIN_Y +
            WIN_H >
        max_h) {

        WIN_Y =
            max_h -
            WIN_H;
    }

    if (WIN_X < 0) {
        WIN_X =
            0;
    }

    if (WIN_Y < 0) {
        WIN_Y =
            0;
    }
}

bool ustaw_geometrie_warstwy(
    int x,
    int y,
    int w,
    int h
) {
    const int stary_x =
        WIN_X;

    const int stary_y =
        WIN_Y;

    const int stary_w =
        WIN_W;

    const int stary_h =
        WIN_H;

    WIN_X =
        x;

    WIN_Y =
        y;

    WIN_W =
        w;

    WIN_H =
        h;

    ogranicz_okno_do_ekranu();

    if (bws_utworz_warstwe(
            WIN_X,
            WIN_Y,
            WIN_W,
            WIN_H,
            Z_ORDER_HUSSAR
        ) < 0) {

        WIN_X =
            stary_x;

        WIN_Y =
            stary_y;

        WIN_W =
            stary_w;

        WIN_H =
            stary_h;

        (void)bws_utworz_warstwe(
            WIN_X,
            WIN_Y,
            WIN_W,
            WIN_H,
            Z_ORDER_HUSSAR
        );

        return false;
    }

    return true;
}

void przelacz_maksymalizacje() {
    if (!zmaksymalizowane) {
        old_win_x =
            WIN_X;

        old_win_y =
            WIN_Y;

        old_win_w =
            WIN_W;

        old_win_h =
            WIN_H;

        if (ustaw_geometrie_warstwy(
                0,
                0,
                screen_w,
                screen_h -
                    PASEK_SYSTEMOWY_WYS)) {

            zmaksymalizowane =
                true;
        } else {
            ustaw_status(
                "Brak pamieci na maksymalizacje okna."
            );
        }
    } else {
        if (ustaw_geometrie_warstwy(
                old_win_x,
                old_win_y,
                old_win_w,
                old_win_h)) {

            zmaksymalizowane =
                false;
        } else {
            ustaw_status(
                "Nie udalo sie przywrocic rozmiaru okna."
            );
        }
    }

    max_przewin_y =
        0;
}

void zminimalizuj() {
    aplikacja_zminimalizowana = gui_minimalizuj_okno();

    dragging =
        false;

    menu_ulubione_otwarte =
        false;

    menu_ustawienia_otwarte =
        false;

    w_polu_url =
        false;

    gui_ustaw_capture_myszy(false);
}

/* =========================================================================
 * 18. GUI - RYSOWANIE
 * ========================================================================= */

void rysuj_krotki_url(
    int x,
    int y,
    uint32_t kolor,
    const char* url
) {
    char temp[
        16
    ] = {};

    if (!url ||
        url[0] ==
            '\0') {

        (void)kopiuj_limit(
            temp,
            sizeof(temp),
            "Nowa"
        );
    } else {
        (void)kopiuj_limit(
            temp,
            sizeof(temp),
            url
        );
    }

    gui_wypisz_tekst_kolor(
        x,
        y,
        kolor,
        temp
    );
}

struct HusarzPerf {
    uint64_t key_count, full_redraw_count, partial_redraw_count;
    uint64_t dirty_area, compose_requests, allocations_key;
} husarz_perf{};

void husarz_rysuj_pole_adresu() {
    if(aplikacja_zminimalizowana||liczba_zakladek<=0)return;
    const int x=WIN_X+122,y=WIN_Y+NARZEDZIA_Y+4;
    int width=WIN_W-122-286-8;if(width<80)width=80;
    const char* url=zakladki[aktywna_zakladka].url;
    const int max_text_width=width-13;
    size_t length=0;while(url[length]!='\0'&&length+1U<URL_POJEMNOSC)++length;
    size_t visible_bytes=static_cast<size_t>(max_text_width>0?max_text_width/9:0);
    if(visible_bytes+1U>URL_WIDOCZNY_POJEMNOSC)
        visible_bytes=URL_WIDOCZNY_POJEMNOSC-1U;
    size_t start=length>visible_bytes?length-visible_bytes:0;
    while(start<length&&(static_cast<uint8_t>(url[start])&0xC0U)==0x80U)++start;
    char visible[URL_WIDOCZNY_POJEMNOSC]{};
    (void)kopiuj_limit(visible,sizeof(visible),url+start);
    gui_rysuj_prostokat(x,y,width,28,w_polu_url?KOLOR_BIALY:0x00303030U);
    gui_wypisz_tekst_kolor(x+5,y+6,w_polu_url?0x00000001U:0x00D1D5DBU,visible);
    if(w_polu_url){int caret=x+5+szerokosc_utf8(visible);if(caret>x+width-8)caret=x+width-8;
        gui_wypisz_tekst_kolor(caret,y+6,0x00000001U,"_");}
    gui_odswiez();++husarz_perf.partial_redraw_count;++husarz_perf.compose_requests;
    husarz_perf.dirty_area+=static_cast<uint64_t>(width)*28U;
}

void RysujInterfejs(
    bool odswiez_tlo
) {
    ++husarz_perf.full_redraw_count;
    if (aplikacja_zminimalizowana) {
        return;
    }

    if (odswiez_tlo) {
        gui_odswiez_pulpit();
    }

    gui_rysuj_okno(
        WIN_X,
        WIN_Y,
        WIN_W,
        WIN_H,
        "Husarz - Polska Przegladarka WWW"
    );
    gui_rysuj_standardowa_belke(WIN_X, WIN_Y, WIN_W,
                                "Husarz - Polska Przegladarka WWW",
                                zmaksymalizowane);

    /*
     * Zakladki.
     */
    for (int i = 0;
         i <
            liczba_zakladek;
         ++i) {

        const int tx =
            WIN_X +
            10 +
            i *
                110;

        if (tx +
                100 >
            WIN_X +
                WIN_W -
                90) {

            break;
        }

        const uint32_t bg =
            i ==
                    aktywna_zakladka
                ? KOLOR_AKTYWNY
                : 0x00202020U;

        gui_rysuj_prostokat(
            tx,
            WIN_Y +
                ZAKLADKI_Y,
            100,
            ZAKLADKI_WYS,
            bg
        );

        rysuj_krotki_url(
            tx +
                5,
            WIN_Y +
                ZAKLADKI_Y +
                4,
            KOLOR_BIALY,
            zakladki[i].url
        );

        if (liczba_zakladek >
            1) {

            RysujPrzyciskLokalny(
                tx +
                    80,
                WIN_Y +
                    ZAKLADKI_Y +
                    2,
                16,
                20,
                KOLOR_CZERWONY,
                KOLOR_BIALY,
                "X"
            );
        }
    }

    if (liczba_zakladek <
        MAX_ZAKLADKI) {

        const int plus_x =
            WIN_X +
            10 +
            liczba_zakladek *
                110;

        if (plus_x +
                24 <
            WIN_X +
                WIN_W -
                80) {

            RysujPrzyciskLokalny(
                plus_x,
                WIN_Y +
                    ZAKLADKI_Y,
                24,
                24,
                KOLOR_POMARANCZ,
                0x001A0B00U,
                "+"
            );
        }
    }

    /*
     * Pasek nawigacji.
     */
    const int narzedzia_y =
        WIN_Y +
        NARZEDZIA_Y;

    gui_rysuj_prostokat(
        WIN_X +
            2,
        narzedzia_y,
        WIN_W -
            4,
        NARZEDZIA_WYS,
        0x00202020U
    );

    const uint32_t nieaktywny =
        0x00303030U;

    RysujPrzyciskLokalny(
        WIN_X +
            8,
        narzedzia_y +
            4,
        34,
        28,
        historia_idx >
                0
            ? KOLOR_AKTYWNY
            : nieaktywny,
        KOLOR_BIALY,
        "<-"
    );

    RysujPrzyciskLokalny(
        WIN_X +
            44,
        narzedzia_y +
            4,
        34,
        28,
        historia_idx >= 0 &&
                historia_idx <
                    historia_max
            ? KOLOR_AKTYWNY
            : nieaktywny,
        KOLOR_BIALY,
        "->"
    );

    RysujPrzyciskLokalny(
        WIN_X +
            80,
        narzedzia_y +
            4,
        34,
        28,
        KOLOR_AKTYWNY,
        KOLOR_BIALY,
        "R"
    );

    const int przyciski_prawe =
        286;

    const int adres_x =
        WIN_X +
        122;

    int adres_w =
        WIN_W -
        122 -
        przyciski_prawe -
        8;

    if (adres_w <
        80) {

        adres_w =
            80;
    }

    const uint32_t kolor_paska =
        w_polu_url
            ? KOLOR_BIALY
            : 0x00303030U;

    const uint32_t kolor_url =
        w_polu_url
            ? 0x00000001U
            : 0x00D1D5DBU;

    gui_rysuj_prostokat(
        adres_x,
        narzedzia_y +
            4,
        adres_w,
        28,
        kolor_paska
    );

    /*
     * URL moze byc dluzszy od paska. Na razie GUI API nie ma clippingu
     * tekstu, wiec tworzymy widoczny suffix/prefix ograniczony bajtowo.
     */
    char url_widoczny[URL_WIDOCZNY_POJEMNOSC] = {};

    if (liczba_zakladek > 0) {
        const char* pelny=zakladki[aktywna_zakladka].url;
        size_t len=dlugosc_limit(pelny,URL_POJEMNOSC);
        size_t widoczne=adres_w>13?static_cast<size_t>((adres_w-13)/9):0U;
        if(widoczne+1U>sizeof(url_widoczny))widoczne=sizeof(url_widoczny)-1U;
        size_t start=len>widoczne?len-widoczne:0U;
        while(start<len&&(static_cast<uint8_t>(pelny[start])&0xC0U)==0x80U)++start;
        (void)kopiuj_limit(
            url_widoczny,
            sizeof(url_widoczny),
            pelny+start
        );
    }

    gui_wypisz_tekst_kolor(
        adres_x +
            5,
        narzedzia_y +
            10,
        kolor_url,
        url_widoczny
    );

    if (w_polu_url) {
        int cursor_x =
            adres_x +
            5 +
            szerokosc_utf8(
                url_widoczny
            );

        if (cursor_x >
            adres_x +
                adres_w -
                8) {

            cursor_x =
                adres_x +
                adres_w -
                8;
        }

        gui_wypisz_tekst_kolor(
            cursor_x,
            narzedzia_y +
                10,
            0x00000001U,
            "_"
        );
    }

    RysujPrzyciskLokalny(
        WIN_X +
            WIN_W -
            286,
        narzedzia_y +
            4,
        28,
        28,
        KOLOR_CZERWONY,
        KOLOR_BIALY,
        "X"
    );

    RysujPrzyciskLokalny(
        WIN_X +
            WIN_W -
            254,
        narzedzia_y +
            4,
        54,
        28,
        KOLOR_POMARANCZ,
        0x001A0B00U,
        "Idz"
    );

    RysujPrzyciskLokalny(
        WIN_X +
            WIN_W -
            196,
        narzedzia_y +
            4,
        88,
        28,
        KOLOR_AKTYWNY,
        KOLOR_BIALY,
        "Zakladki"
    );

    RysujPrzyciskLokalny(
        WIN_X +
            WIN_W -
            104,
        narzedzia_y +
            4,
        96,
        28,
        KOLOR_AKTYWNY,
        KOLOR_BIALY,
        "Ustawienia"
    );

    /*
     * Status.
     */
    gui_rysuj_prostokat(
        WIN_X +
            2,
        WIN_Y +
            WIN_H -
            STATUS_WYS,
        WIN_W -
            4,
        STATUS_WYS -
            2,
        0x00202020U
    );

    gui_wypisz_tekst_kolor(
        WIN_X +
            8,
        WIN_Y +
            WIN_H -
            18,
        KOLOR_STATUS,
        status_bufor
    );

    /*
     * Tresc.
     */
    const int obszar_y =
        WIN_Y +
        TRESC_Y;

    const int obszar_h =
        WIN_H -
        TRESC_Y -
        STATUS_WYS -
        2;

    gui_rysuj_prostokat(
        WIN_X +
            2,
        obszar_y,
        WIN_W -
            4,
        obszar_h,
        KOLOR_TLO
    );

    if (liczba_zakladek >
        0) {

        Zakladka& z =
            zakladki[
                aktywna_zakladka
            ];

        const char* tresc =
            z.tresc;

        if (!tresc ||
            tresc[0] ==
                '\0') {

            tresc =
                "Wpisz adres strony lub zapytanie i wybierz Idz.";
        }

        Renderer r{
            WIN_X + 8,
            obszar_y + 5,
            WIN_W - 24,
            obszar_h - 10,
            z.przewin_y,

            0,
            0,

            20,

            KOLOR_TEKST,
            1,

            false
        };

        if (z.to_jest_html) {
            renderuj_html(
                r,
                tresc
            );
        } else {
            renderer_tekst(
                r,
                tresc
            );
        }

        przelicz_scroll(
            r,
            obszar_h -
            10
        );

        /*
         * Scrollbar.
         */
        if (max_przewin_y >
            0) {

            const int scroll_x =
                WIN_X +
                WIN_W -
                8;

            const int track_h =
                obszar_h -
                4;

            gui_rysuj_prostokat(
                scroll_x,
                obszar_y +
                    2,
                6,
                track_h,
                0x00CCCCCCU
            );

            int denom =
                obszar_h +
                max_przewin_y;

            if (denom <= 0) {
                denom =
                    1;
            }

            int suwak_h =
                (obszar_h *
                 obszar_h) /
                denom;

            if (suwak_h <
                12) {

                suwak_h =
                    12;
            }

            if (suwak_h >
                track_h) {

                suwak_h =
                    track_h;
            }

            const int droga =
                track_h -
                suwak_h;

            const int suwak_y =
                obszar_y +
                2 +
                (droga *
                 z.przewin_y) /
                    max_przewin_y;

            gui_rysuj_prostokat(
                scroll_x,
                suwak_y,
                6,
                suwak_h,
                KOLOR_POMARANCZ
            );
        }
    }

    /*
     * Menu Zakladek.
     */
    const int menu_y =
        WIN_Y +
        94;

    if (menu_ulubione_otwarte) {
        const int menu_x =
            WIN_X +
            WIN_W -
            270;

        const int menu_h =
            26 +
            ulubione_ilosc *
                20;

        gui_rysuj_prostokat(
            menu_x,
            menu_y,
            260,
            menu_h,
            KOLOR_AKTYWNY
        );

        gui_wypisz_tekst_kolor(
            menu_x +
                8,
            menu_y +
                5,
            KOLOR_BIALY,
            "+ Dodaj obecna strone"
        );

        for (int i = 0;
             i <
                ulubione_ilosc;
             ++i) {

            char label[
                30
            ] = {};

            (void)kopiuj_limit(
                label,
                sizeof(label),
                ulubione[i]
            );

            gui_wypisz_tekst_kolor(
                menu_x +
                    8,
                menu_y +
                    28 +
                    i *
                        20,
                KOLOR_BIALY,
                label
            );
        }
    }

    if (menu_ustawienia_otwarte) {
        const int menu_x =
            WIN_X +
            WIN_W -
            190;

        gui_rysuj_prostokat(
            menu_x,
            menu_y,
            180,
            48,
            KOLOR_AKTYWNY
        );

        gui_wypisz_tekst_kolor(
            menu_x +
                8,
            menu_y +
                7,
            KOLOR_BIALY,
            "JavaScript: wyl."
        );

        gui_wypisz_tekst_kolor(
            menu_x +
                8,
            menu_y +
                27,
            KOLOR_BIALY,
            "Renderer: prosty HTML"
        );
    }

    gui_odswiez();
}

/* =========================================================================
 * 19. POBIERANIE STRONY
 * ========================================================================= */

void ustaw_tresc(
    Zakladka& z,
    const char* tekst,
    bool html
) {
    if (!alokuj_tresc_zakladki(
            z)) {

        return;
    }

    (void)kopiuj_limit(
        z.tresc,
        HTML_POJEMNOSC,
        tekst
    );

    z.to_jest_html =
        html;

    z.wczytana =
        true;

    z.przewin_y =
        0;
}

void ustaw_blad_html(
    Zakladka& z,
    int kod,
    const char* opis
) {
    if (!alokuj_tresc_zakladki(
            z)) {

        return;
    }

    if (!zloz_strone_bledu(
            z.tresc,
            HTML_POJEMNOSC,
            kod,
            opis
        )) {

        (void)kopiuj_limit(
            z.tresc,
            HTML_POJEMNOSC,
            "Blad HTTP."
        );

        z.to_jest_html =
            false;
    } else {
        z.to_jest_html =
            true;
    }

    z.wczytana =
        true;

    z.przewin_y =
        0;
}

__attribute__((noinline)) bool pobierz_jeden_url(
    Zakladka& z,
    bool* redirect,
    char redirect_url[
        URL_POJEMNOSC
    ]
) {
    if (!redirect ||
        !redirect_url) {

        return false;
    }

    *redirect =
        false;

    redirect_url[0] =
        '\0';

    ParsedUrl p{};

    if (!parsuj_url(
            z.url,
            &p)) {

        ustaw_tresc(
            z,
            "Nieprawidlowy URL. Husarz obsluguje obecnie http:// i https:// bez niestandardowego portu.",
            false
        );

        ustaw_status(
            "Nieprawidlowy URL."
        );

        return false;
    }

    ustaw_status(
        "DNS: wyszukiwanie serwera..."
    );

    RysujInterfejs(
        true
    );

    uint8_t ip[
        4
    ] = {};

    if (!bws_siec_dns(
            p.domena,
            ip)) {

        ustaw_tresc(
            z,
            "Blad DNS: nie udalo sie rozwiazac nazwy domeny.",
            false
        );

        ustaw_status(
            "Blad DNS."
        );

        return false;
    }

    /*
     * Bounce buffer musi byc niskim adresem z powodu aktualnego BWS29/30.
     */
    const uint64_t adres_bufora =
        reinterpret_cast<uint64_t>(
            siec_bufor
        );

    if (adres_bufora >
        UINT32_MAX) {

        ustaw_tresc(
            z,
            "Blad ABI: bufor HTTP aplikacji nie miesci sie w 32-bitowym adresie BWS29/BWS30.",
            false
        );

        ustaw_status(
            "Blad ABI HTTP."
        );

        return false;
    }

    wyzeruj(
        siec_bufor,
        sizeof(
            siec_bufor
        )
    );

    ustaw_status(
        p.https
            ? "HTTPS: laczenie TLS..."
            : "HTTP: pobieranie..."
    );

    RysujInterfejs(
        true
    );

    const bool pobrano =
        p.https
            ? bws_siec_pobierz_https(
                  ip,
                  p.domena,
                  p.sciezka,
                  siec_bufor,
                  static_cast<uint32_t>(
                      sizeof(
                          siec_bufor
                      )
                  )
              )
            : bws_siec_pobierz_http(
                  ip,
                  p.domena,
                  p.sciezka,
                  siec_bufor,
                  static_cast<uint32_t>(
                      sizeof(
                          siec_bufor
                      )
                  )
              );

    if (!pobrano) {
        ustaw_tresc(
            z,
            p.https
                ? "Nie udalo sie pobrac strony HTTPS. Sprawdz siec, TLS, certyfikat CA i serwer."
                : "Nie udalo sie pobrac strony HTTP. Sprawdz siec i serwer.",
            false
        );

        ustaw_status(
            p.https
                ? "Blad HTTPS/TLS."
                : "Blad HTTP/TCP."
        );

        return false;
    }

    /*
     * Syscall dopisuje NUL. Nadal stosujemy bounded strlen.
     */
    const size_t response_len =
        dlugosc_limit(
            siec_bufor,
            sizeof(
                siec_bufor
            )
        );

    if (response_len == 0 ||
        response_len >=
            sizeof(
                siec_bufor
            )) {

        ustaw_tresc(
            z,
            "Odpowiedz HTTP jest pusta albo nieprawidlowo zakonczona.",
            false
        );

        ustaw_status(
            "Blad odpowiedzi HTTP."
        );

        return false;
    }

    OdpowiedzHttp odp{};

    if (!parsuj_odpowiedz_http(
            siec_bufor,
            response_len,
            &odp)) {

        ustaw_tresc(
            z,
            "Odpowiedz serwera nie zawiera poprawnej linii statusu/naglowkow HTTP.",
            false
        );

        ustaw_status(
            "Nierozpoznana odpowiedz HTTP."
        );

        return false;
    }

    if (http_kod_przekierowanie(
            odp.kod) &&
        (odp.kod == 301 ||
         odp.kod == 302 ||
         odp.kod == 303 ||
         odp.kod == 307 ||
         odp.kod == 308)) {

        size_t header_len =
            static_cast<size_t>(
                odp.body -
                siec_bufor
            );

        char location[
            URL_POJEMNOSC
        ] = {};

        if (!znajdz_header(
                siec_bufor,
                header_len,
                "location",
                location,
                sizeof(location)
            )) {

            ustaw_blad_html(
                z,
                odp.kod,
                "Serwer zwrocil przekierowanie bez poprawnego naglowka Location."
            );

            ustaw_status(
                "Blad przekierowania HTTP."
            );

            return false;
        }

        if (!ustaw_url_z_location(
                redirect_url,
                URL_POJEMNOSC,
                location,
                p)) {

            ustaw_blad_html(
                z,
                odp.kod,
                "Naglowek Location jest zbyt dlugi lub ma nieobslugiwany format."
            );

            ustaw_status(
                "Nieprawidlowe przekierowanie."
            );

            return false;
        }

        *redirect =
            true;

        return true;
    }

    if (!http_kod_sukces(
            odp.kod)) {

        ustaw_blad_html(
            z,
            odp.kod,
            pobierz_opis_kodu_http(
                odp.kod
            )
        );

        ustaw_status(
            "Odebrano status HTTP."
        );

        return true;
    }

    if (!odp.tekst) {
        ustaw_tresc(
            z,
            "Husarz nie potrafi jeszcze wyswietlic skompresowanej albo binarnej odpowiedzi tego typu.",
            false
        );

        ustaw_status(
            "Nieobslugiwany typ odpowiedzi."
        );

        return true;
    }

    if (!alokuj_tresc_zakladki(
            z)) {

        ustaw_status(
            "Brak pamieci na tresc strony."
        );

        return false;
    }

    size_t body_len =
        odp.body_len;

    bool body_ok =
        true;

    if (odp.chunked) {
        body_ok =
            dekoduj_chunked(
                odp.body,
                odp.body_len,
                z.tresc,
                HTML_POJEMNOSC,
                &body_len
            );
    } else {
        if (body_len >=
            HTML_POJEMNOSC) {

            body_len =
                HTML_POJEMNOSC -
                1U;

            body_ok =
                false;
        }

        for (size_t i = 0;
             i < body_len;
             ++i) {

            z.tresc[i] =
                odp.body[i];
        }

        z.tresc[
            body_len
        ] =
            '\0';
    }

    if (!body_ok) {
        /*
         * Przy nieudanym dechunkowaniu nie pokazujemy surowego framingu jako
         * HTML. To mogloby wygladac jak poprawna strona mimo uszkodzenia.
         */
        if (odp.chunked) {
            ustaw_tresc(
                z,
                "Nie udalo sie bezpiecznie zdekodowac Transfer-Encoding: chunked.",
                false
            );

            ustaw_status(
                "Blad dekodowania HTTP chunked."
            );

            return false;
        }

        ustaw_status(
            "Strona zostala ucieta do limitu Husarza."
        );
    } else if (p.https) {
        ustaw_status(
            bws_tls_certyfikat_zaufany()
                ? "HTTPS: polaczenie zweryfikowane."
                : "HTTPS: brak potwierdzenia zaufanego CA."
        );
    } else {
        ustaw_status(
            "Gotowy."
        );
    }

    z.to_jest_html =
        odp.html;

    z.wczytana =
        true;

    z.przewin_y =
        0;

    return true;
}

bool PobierzStrone(
    bool zapisz_historie
) {
    if (liczba_zakladek <= 0) {
        return false;
    }

    Zakladka& z =
        zakladki[
            aktywna_zakladka
        ];

    if (z.url[0] ==
        '\0') {

        ustaw_status(
            "Wpisz adres lub zapytanie."
        );

        return false;
    }

    /* Brak schematu oznacza jawnie domyslny transport HTTP/80. */
    if (!url_zaczyna_sie_schematem(
            z.url)) {

        char temp[
            URL_POJEMNOSC
        ] = {};

        if (!kopiuj_limit(
            temp,
            sizeof(temp),
            z.url
        )) {
            ustaw_status(
                "Adres URL jest zbyt dlugi."
            );
            return false;
        }

        if (!kopiuj_limit(
                z.url,
                sizeof(z.url),
                "http://")) {

            return false;
        }

        size_t p =
            dlugosc_limit(
                z.url,
                sizeof(z.url)
            );

        for (size_t i = 0;
             temp[i] != '\0';
             ++i) {

            if (p + 1U >=
                sizeof(z.url)) {

                ustaw_status(
                    "Adres URL jest zbyt dlugi."
                );

                return false;
            }

            z.url[p++] =
                temp[i];
        }

        z.url[p] =
            '\0';
    }

    z.wczytana =
        false;

    z.to_jest_html =
        false;

    z.przewin_y =
        0;

    ustaw_tresc(
        z,
        "Ladowanie strony...",
        false
    );

    for (int redirect_count = 0;
         redirect_count <=
             MAX_PRZEKIEROWAN;
         ++redirect_count) {

        bool redirect =
            false;

        char redirect_url[
            URL_POJEMNOSC
        ] = {};

        const bool ok =
            pobierz_jeden_url(
                z,
                &redirect,
                redirect_url
            );

        if (!ok) {
            return false;
        }

        if (!redirect) {
            if (zapisz_historie) {
                dopisz_do_historii(
                    z.url
                );
            }

            return true;
        }

        if (redirect_count >=
            MAX_PRZEKIEROWAN) {

            ustaw_blad_html(
                z,
                310,
                "Przekroczono limit 5 przekierowan."
            );

            ustaw_status(
                "Za duzo przekierowan."
            );

            return false;
        }

        if (!kopiuj_limit(
            z.url,
            sizeof(z.url),
            redirect_url
        )) {
            ustaw_status(
                "Adres przekierowania jest zbyt dlugi."
            );
            return false;
        }

        ustaw_status(
            "Przekierowanie HTTP..."
        );
    }

    return false;
}

void Nawiguj(
    bool nowa_strona,
    bool rozpoznaj_wyszukiwanie
) {
    if (liczba_zakladek <= 0) {
        return;
    }

    Zakladka& z =
        zakladki[
            aktywna_zakladka
        ];

    if (z.url[0] ==
        '\0') {

        ustaw_status(
            "Wpisz adres lub szukana fraze."
        );

        return;
    }

    if (rozpoznaj_wyszukiwanie &&
        !przygotuj_adres_wyszukiwania(
            z.url)) {

        ustaw_status(
            "Zapytanie jest zbyt dlugie."
        );

        return;
    }

    (void)PobierzStrone(
        nowa_strona
    );
}

/* =========================================================================
 * 20. OBSLUGA KLIKNIEC
 * ========================================================================= */

void ustaw_scroll(
    int wartosc
) {
    if (liczba_zakladek <= 0) {
        return;
    }

    if (wartosc < 0) {
        wartosc =
            0;
    }

    if (wartosc >
        max_przewin_y) {

        wartosc =
            max_przewin_y;
    }

    zakladki[
        aktywna_zakladka
    ].przewin_y =
        wartosc;
}

bool klik_menu(
    int mx,
    int my
) {
    const int menu_y =
        WIN_Y +
        94;

    if (menu_ulubione_otwarte) {
        const int menu_x =
            WIN_X +
            WIN_W -
            270;

        const int menu_h =
            26 +
            ulubione_ilosc *
                20;

        if (punkt_w_prostokacie(
                mx,
                my,
                menu_x,
                menu_y,
                260,
                menu_h)) {

            if (my <
                menu_y +
                26) {

                dodaj_obecna_do_ulubionych();
            } else {
                const int index =
                    (my -
                     (menu_y +
                      26)) /
                    20;

                if (index >= 0 &&
                    index <
                        ulubione_ilosc) {

                    if (!kopiuj_limit(
                        zakladki[
                            aktywna_zakladka
                        ].url,
                        URL_POJEMNOSC,
                        ulubione[
                            index
                        ]
                    )) {
                        ustaw_status(
                            "Uszkodzony adres w zakladkach."
                        );
                        return false;
                    }

                    menu_ulubione_otwarte =
                        false;

                    Nawiguj(
                        true,
                        false
                    );
                }
            }

            return true;
        }
    }

    if (menu_ustawienia_otwarte) {
        const int menu_x =
            WIN_X +
            WIN_W -
            190;

        if (punkt_w_prostokacie(
                mx,
                my,
                menu_x,
                menu_y,
                180,
                48)) {

            return true;
        }
    }

    return false;
}

void klik_zakladki(
    int mx,
    int my
) {
    for (int i = 0;
         i <
            liczba_zakladek;
         ++i) {

        const int tx =
            WIN_X +
            10 +
            i *
                110;

        if (!punkt_w_prostokacie(
                mx,
                my,
                tx,
                WIN_Y +
                    ZAKLADKI_Y,
                100,
                ZAKLADKI_WYS)) {

            continue;
        }

        if (liczba_zakladek >
                1 &&
            punkt_w_prostokacie(
                mx,
                my,
                tx +
                    80,
                WIN_Y +
                    ZAKLADKI_Y +
                    2,
                16,
                20)) {

            zamknij_zakladke(
                i
            );
        } else {
            aktywna_zakladka =
                i;

            max_przewin_y =
                0;

            ustaw_status(
                "Przelaczono zakladke."
            );
        }

        return;
    }

    if (liczba_zakladek <
        MAX_ZAKLADKI) {

        const int plus_x =
            WIN_X +
            10 +
            liczba_zakladek *
                110;

        if (punkt_w_prostokacie(
                mx,
                my,
                plus_x,
                WIN_Y +
                    ZAKLADKI_Y,
                24,
                24)) {

            (void)nowa_zakladka();
        }
    }
}

void klik_narzedzia(
    int mx,
    int my
) {
    if (liczba_zakladek <= 0) {
        return;
    }

    const int y =
        WIN_Y +
        NARZEDZIA_Y;

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X +
                8,
            y +
                4,
            34,
            28)) {

        if (historia_idx >
            0) {

            --historia_idx;

            if (!kopiuj_limit(
                zakladki[
                    aktywna_zakladka
                ].url,
                URL_POJEMNOSC,
                historia[
                    historia_idx
                ]
            )) {
                ++historia_idx;
                ustaw_status(
                    "Uszkodzony wpis historii."
                );
                return;
            }

            Nawiguj(
                false,
                false
            );
        }

        w_polu_url =
            false;

        return;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X +
                44,
            y +
                4,
            34,
            28)) {

        if (historia_idx >= 0 &&
            historia_idx <
                historia_max) {

            ++historia_idx;

            if (!kopiuj_limit(
                zakladki[
                    aktywna_zakladka
                ].url,
                URL_POJEMNOSC,
                historia[
                    historia_idx
                ]
            )) {
                --historia_idx;
                ustaw_status(
                    "Uszkodzony wpis historii."
                );
                return;
            }

            Nawiguj(
                false,
                false
            );
        }

        w_polu_url =
            false;

        return;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X +
                80,
            y +
                4,
            34,
            28)) {

        Nawiguj(
            false,
            false
        );

        w_polu_url =
            false;

        return;
    }

    const int adres_x =
        WIN_X +
        122;

    int adres_w =
        WIN_W -
        122 -
        286 -
        8;

    if (adres_w <
        80) {

        adres_w =
            80;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            adres_x,
            y +
                4,
            adres_w,
            28)) {

        w_polu_url =
            true;

        menu_ulubione_otwarte =
            false;

        menu_ustawienia_otwarte =
            false;

        ustaw_status(
            "Edycja adresu URL..."
        );

        return;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X +
                WIN_W -
                286,
            y +
                4,
            28,
            28)) {

        zakladki[
            aktywna_zakladka
        ].url[0] =
            '\0';

        w_polu_url =
            true;

        return;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X +
                WIN_W -
                254,
            y +
                4,
            54,
            28)) {

        w_polu_url =
            false;

        Nawiguj(
            true,
            true
        );

        return;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X +
                WIN_W -
                196,
            y +
                4,
            88,
            28)) {

        menu_ulubione_otwarte =
            !menu_ulubione_otwarte;

        menu_ustawienia_otwarte =
            false;

        w_polu_url =
            false;

        return;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X +
                WIN_W -
                104,
            y +
                4,
            96,
            28)) {

        menu_ustawienia_otwarte =
            !menu_ustawienia_otwarte;

        menu_ulubione_otwarte =
            false;

        w_polu_url =
            false;

        return;
    }

    w_polu_url =
        false;
}

bool obsluz_klik(
    int mx,
    int my
) {
    if (aplikacja_zminimalizowana) {
        return true;
    }

    if (!punkt_w_prostokacie(
            mx,
            my,
            WIN_X,
            WIN_Y,
            WIN_W,
            WIN_H)) {

        return false;
    }

    if (klik_menu(
            mx,
            my)) {

        return true;
    }

    /*
     * Klik poza otwartym menu zamyka je.
     */
    if (menu_ulubione_otwarte ||
        menu_ustawienia_otwarte) {

        menu_ulubione_otwarte =
            false;

        menu_ustawienia_otwarte =
            false;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X,
            WIN_Y,
            WIN_W,
            TYTUL_WYS)) {

        if (punkt_w_prostokacie(
                mx,
                my,
                WIN_X +
                    WIN_W -
                    74,
                WIN_Y +
                    4,
                20,
                20)) {

            zminimalizuj();

            return true;
        }

        if (punkt_w_prostokacie(
                mx,
                my,
                WIN_X +
                    WIN_W -
                    50,
                WIN_Y +
                    4,
                20,
                20)) {

            przelacz_maksymalizacje();

            return true;
        }

        if (punkt_w_prostokacie(
                mx,
                my,
                WIN_X +
                    WIN_W -
                    26,
                WIN_Y +
                    4,
                20,
                20)) {

            return false;
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

            w_polu_url =
                false;
        }

        return true;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X,
            WIN_Y +
                ZAKLADKI_Y,
            WIN_W,
            ZAKLADKI_WYS)) {

        klik_zakladki(
            mx,
            my
        );

        return true;
    }

    if (punkt_w_prostokacie(
            mx,
            my,
            WIN_X,
            WIN_Y +
                NARZEDZIA_Y,
            WIN_W,
            NARZEDZIA_WYS)) {

        klik_narzedzia(
            mx,
            my
        );

        return true;
    }

    w_polu_url =
        false;

    return true;
}

/* =========================================================================
 * 21. KLAWIATURA
 * ========================================================================= */

void obsluz_znak(
    char znak,
    int* ansi_stan
) {
    if (!ansi_stan ||
        liczba_zakladek <= 0) {

        return;
    }

    Zakladka& z =
        zakladki[
            aktywna_zakladka
        ];

    if (*ansi_stan == 0 &&
        znak ==
            '\x1B') {

        *ansi_stan =
            1;

        return;
    }

    if (*ansi_stan == 1) {
        if (znak ==
            '[') {

            *ansi_stan =
                2;

            return;
        }

        *ansi_stan =
            0;

        return;
    }

    if (*ansi_stan == 2) {
        *ansi_stan =
            0;

        if (znak ==
            'A') {

            ustaw_scroll(
                z.przewin_y -
                100
            );
        } else if (znak ==
                   'B') {

            ustaw_scroll(
                z.przewin_y +
                100
            );
        }

        return;
    }

    *ansi_stan =
        0;

    if (!w_polu_url) {
        return;
    }

    if (znak ==
            '\n' ||
        znak ==
            '\r') {

        w_polu_url =
            false;

        Nawiguj(
            true,
            true
        );

        return;
    }

    if (znak ==
        '\b') {

        usun_ostatni_utf8(
            z.url,
            sizeof(
                z.url
            )
        );

        return;
    }

    const uint8_t b =
        static_cast<uint8_t>(
            znak
        );

    if (b >=
        0x20U) {

        if (!dopisz_znak_limit(
                z.url,
                sizeof(z.url),
                znak)) {

            ustaw_status(
                "Adres URL osiagnal limit 2047 bajtow."
            );
        }
    }
}

/* =========================================================================
 * 22. START / PETLA
 * ========================================================================= */

void inicjalizuj_stan() {
    liczba_zakladek =
        0;

    aktywna_zakladka =
        0;

    historia_idx =
        -1;

    historia_max =
        -1;

    ulubione_ilosc =
        0;

    menu_ulubione_otwarte =
        false;

    menu_ustawienia_otwarte =
        false;

    w_polu_url =
        false;

    zmaksymalizowane =
        false;

    aplikacja_zminimalizowana =
        false;

    dragging =
        false;

    max_przewin_y =
        0;

    calkowita_wysokosc_strony =
        0;

    gui_pobierz_rozdzielczosc(
        &screen_w,
        &screen_h
    );

    if (screen_w <= 0 ||
        screen_h <=
            PASEK_SYSTEMOWY_WYS) {

        gui_zakoncz_aplikacje();
    }

    WIN_W =
        screen_w <
                DOMYSLNY_WIN_W
            ? screen_w
            : DOMYSLNY_WIN_W;

    WIN_H =
        screen_h -
            PASEK_SYSTEMOWY_WYS <
                DOMYSLNY_WIN_H
            ? screen_h -
                PASEK_SYSTEMOWY_WYS
            : DOMYSLNY_WIN_H;

    WIN_X =
        (screen_w -
         WIN_W) /
        2;

    WIN_Y =
        40;

    if (WIN_Y +
            WIN_H >
        screen_h -
            PASEK_SYSTEMOWY_WYS) {

        WIN_Y =
            0;
    }

    ogranicz_okno_do_ekranu();

    old_win_x =
        WIN_X;

    old_win_y =
        WIN_Y;

    old_win_w =
        WIN_W;

    old_win_h =
        WIN_H;

    ustaw_status(
        "Gotowy."
    );

    if (!nowa_zakladka()) {
        gui_zakoncz_aplikacje();
    }

    (void)kopiuj_limit(
        zakladki[0].url,
        sizeof(
            zakladki[0].url
        ),
        "http://example.com/"
    );

    wczytaj_ulubione();
}

} // namespace

extern "C" [[noreturn]] void _start() {
    inicjalizuj_stan();

    gui_ustaw_przejecie_myszy(
        true
    );

    if (bws_utworz_warstwe(
            WIN_X,
            WIN_Y,
            WIN_W,
            WIN_H,
            Z_ORDER_HUSSAR
        ) < 0) {

        zwolnij_wszystkie_zakladki();

        gui_ustaw_przejecie_myszy(
            false
        );

        gui_zakoncz_aplikacje();
    }

    RysujInterfejs(
        true
    );

    bool dziala =
        true;

    bool scroll_dragging =
        false;

    int scroll_drag_start_y =
        0;

    int scroll_drag_start_value =
        0;

    int ansi_stan =
        0;

    while (dziala) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        if (zdarzenie.typ == BWS_ZDARZENIE_FOCUS && aplikacja_zminimalizowana)
            aplikacja_zminimalizowana = false;
        const int mx = zdarzenie.x;
        const int my = zdarzenie.y;
        const uint8_t mb = static_cast<uint8_t>(zdarzenie.przyciski);

        const bool lewy =
            (mb &
             1U) != 0;

        const bool klik = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;

        const bool pusc = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP;

        bool trzeba_rysowac =
            false;

        bool pelne_tlo =
            false;

        if (klik) {
            if (!aplikacja_zminimalizowana &&
                punkt_w_prostokacie(
                    mx,
                    my,
                    WIN_X +
                        WIN_W -
                        26,
                    WIN_Y +
                        4,
                    20,
                    20)) {

                dziala =
                    false;
            } else {
                const bool wewnatrz =
                    obsluz_klik(
                        mx,
                        my
                    );

                (void)wewnatrz;

                trzeba_rysowac =
                    !aplikacja_zminimalizowana &&
                    !dragging;

                pelne_tlo =
                    !dragging;
            }

            if (!aplikacja_zminimalizowana &&
                max_przewin_y >
                    0) {

                const int obszar_y =
                    WIN_Y +
                    TRESC_Y;

                const int obszar_h =
                    WIN_H -
                    TRESC_Y -
                    STATUS_WYS -
                    2;

                if (punkt_w_prostokacie(
                        mx,
                        my,
                        WIN_X +
                            WIN_W -
                            20,
                        obszar_y,
                        20,
                        obszar_h)) {

                    scroll_dragging =
                        true;
                    gui_ustaw_capture_myszy(true);

                    scroll_drag_start_y =
                        my;

                    scroll_drag_start_value =
                        zakladki[
                            aktywna_zakladka
                        ].przewin_y;

                    dragging =
                        false;
                }
            }
        }

        if (dragging &&
            lewy &&
            !zmaksymalizowane) {

            WIN_X =
                mx -
                drag_off_x;

            WIN_Y =
                my -
                drag_off_y;

            ogranicz_okno_do_ekranu();

            bws_przesun_warstwe(
                WIN_X,
                WIN_Y
            );
        }

        if (scroll_dragging &&
            lewy &&
            liczba_zakladek >
                0) {

            const int obszar_h =
                WIN_H -
                TRESC_Y -
                STATUS_WYS -
                2;

            const int ruch =
                my -
                scroll_drag_start_y;

            int skala =
                max_przewin_y >
                        obszar_h
                    ? max_przewin_y /
                        (obszar_h > 0
                             ? obszar_h
                             : 1)
                    : 1;

            if (skala <
                1) {

                skala =
                    1;
            }

            ustaw_scroll(
                scroll_drag_start_value +
                ruch *
                    skala
            );

            trzeba_rysowac =
                true;
        }

        if (pusc) {
            if (dragging) {
                dragging =
                    false;
                gui_ustaw_capture_myszy(false);
            }

            scroll_dragging =
                false;
            gui_ustaw_capture_myszy(false);
        }

        const char znak = zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ
            ? static_cast<char>(zdarzenie.kod) : 0;

        bool szybkie_pole_url=false;
        if (znak !=
            0) {

            const bool url_przed=w_polu_url;

            obsluz_znak(
                znak,
                &ansi_stan
            );

            const uint8_t u=static_cast<uint8_t>(znak);
            szybkie_pole_url=url_przed&&w_polu_url&&
                (u>=0x20U||znak=='\b'||u==0x7FU);
            ++husarz_perf.key_count;
            trzeba_rysowac=!szybkie_pole_url;
        }

        if(szybkie_pole_url)husarz_rysuj_pole_adresu();

        if (trzeba_rysowac &&
            !aplikacja_zminimalizowana) {

            RysujInterfejs(
                pelne_tlo
            );
        }
    }

    /*
     * Wyjscie w uporzadkowanej kolejnosci.
     */
    zwolnij_wszystkie_zakladki();

    gui_zakoncz_aplikacje();
}
