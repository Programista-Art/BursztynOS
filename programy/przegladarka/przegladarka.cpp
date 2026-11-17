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
 * `siec_bufor` jest celowo globalnym BSS aplikacji pod niskim VA 0x1010000+.
 * Po naprawieniu ABI BWS29/BWS30 do pelnego uint64_t ten bounce-buffer
 * bedzie mozna usunac.
 */

#include "bursztyn_gui.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "../http_kody.h"
#include "../../jadro/siec/http_origin.h"
#include "../husarz/pipeline.h"
#include "../husarz/work_clock.h"
#include "../husarz/resource.h"
#include "../husarz/async.h"
#include "../husarz/webfonts.h"
#ifdef HUSARZ_WORKER_ONLY
#include "../../bsf/webfont.h"
#endif
#include "../../obrazy/dekoder_obrazu.h"
#include "../husarz/script.h"
#include "../husarz/js_dom.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

using namespace BursztynUI;

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
 *   HEADER  off 0x0000  VA 0x1000000  region 0x1000
 *   TEXT    off 0x1000  VA 0x1001000  region 0xD8000
 *   DATA    off 0xD9000 VA 0x10D9000  memory 0x80000
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

#ifndef HUSARZ_HOST_TEST
extern "C" {

#ifdef HUSARZ_WORKER_ONLY
constexpr uint64_t BUR_TEXT_SIZE=0x10000ULL,BUR_DATA_OFFSET=0x11000ULL,BUR_DATA_VADDR=0x1011000ULL;
#else
constexpr uint64_t BUR_TEXT_SIZE=0xD8000ULL,BUR_DATA_OFFSET=0xD9000ULL,BUR_DATA_VADDR=0x10D9000ULL;
#endif
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},

    reinterpret_cast<uint64_t>(
        &_start
    ),

    0x1000ULL,
    BUR_TEXT_SIZE,
    0x1001000ULL,

    BUR_DATA_OFFSET,
    0x80000ULL,
    BUR_DATA_VADDR
};

}

#endif

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

constexpr int DOMYSLNY_WIN_W =
    800;

constexpr int DOMYSLNY_WIN_H =
    550;

constexpr int MAX_PRZEKIEROWAN =
    5;

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

struct PendingCss {
    struct Item{Husarz::ResourceEntry *entry{};uint16_t node{};} items[Husarz::MaxStylesheets]{};
    unsigned count{};
    ~PendingCss(){for(unsigned i=0;i<count;++i)Husarz::ResourceCache::unpin(items[i].entry);}
};
void browser_cancel_work(uint32_t identity);
struct Zakladka {
    char url[
        URL_POJEMNOSC
    ];

    char* tresc;
    char published_url[URL_POJEMNOSC]{};

    uint32_t identity{}, navigation{};
    Husarz::Document* dokument;
    Husarz::Js::BrowserRuntime *javascript{};
    bool dokument_brudny;
    bool work_pending{};uint32_t resource_revision{};uint64_t navigation_started{};bool navigation_measured{};PendingCss *pending_css{};
    // BSF 2.2 browser-scoped web fonts of the current navigation.
    struct BrowserFonts *fonts{};bool fonts_changed{};

    bool to_jest_html;
    bool wczytana;
};

uint32_t next_tab_identity{};
void ensure_tab_identity(Zakladka &z){
    if(!z.identity){z.identity=++next_tab_identity;if(!z.identity)z.identity=++next_tab_identity;}
}

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



bool nawigacja_trwa=false;
int maksimum_przewijania();
void zresetuj_przewijanie();



/* =========================================================================
 * 5. FREESTANDING MEMCPY/MEMSET
 * ========================================================================= */

} // namespace


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

[[maybe_unused]] bool dopisz_znak_limit(
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

[[maybe_unused]] size_t utf8_poprzedni(
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

[[maybe_unused]] void usun_ostatni_utf8(
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

void zaktualizuj_pasek_stanu_tekst(
    const char* txt
);

void ustaw_status(
    const char* tekst
) {
    (void)kopiuj_limit(
        status_bufor,
        sizeof(status_bufor),
        tekst
    );

    zaktualizuj_pasek_stanu_tekst(
        status_bufor
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

void webfonts_release(Zakladka &);
void wyczysc_zakladke(
    Zakladka& z,
    bool zwolnij_tresc
) {
    webfonts_release(z);
    if (zwolnij_tresc &&
        z.tresc) {

        gui_free(
            z.tresc
        );
    }

    if(zwolnij_tresc){browser_cancel_work(z.identity);delete z.pending_css;z.pending_css=nullptr;}
    if (zwolnij_tresc){delete z.javascript;z.javascript=nullptr;delete z.dokument;}
    Husarz::browser_storage.close(z.identity);z.identity=z.navigation=0;
    z.dokument = nullptr;
    z.dokument_brudny = true;
    z.tresc =
        nullptr;

    z.url[0] =
        '\0';
    z.published_url[0]=0;
    z.dokument_brudny = true;

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
    zresetuj_przewijanie();

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
    z.dokument_brudny = true;

        z.to_jest_html =
            false;

        z.wczytana =
            false;
    zresetuj_przewijanie();

        ustaw_status(
            "Wyczyszczono zakladke."
        );

        return;
    }

    webfonts_release(zakladki[indeks]);
    browser_cancel_work(zakladki[indeks].identity);delete zakladki[indeks].pending_css;zakladki[indeks].pending_css=nullptr;
    delete zakladki[indeks].javascript;zakladki[indeks].javascript=nullptr;
    delete zakladki[indeks].dokument;
    zakladki[indeks].dokument = nullptr;
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
    zakladki[liczba_zakladek].dokument = nullptr;zakladki[liczba_zakladek].javascript=nullptr;
    zakladki[liczba_zakladek].pending_css=nullptr;zakladki[liczba_zakladek].work_pending=false;
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
    zresetuj_przewijanie();

    ustaw_status(
        "Zamknieto zakladke."
    );
}

[[maybe_unused]] void zwolnij_wszystkie_zakladki() {
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

bool parsuj_url(
    const char* url,
    ParsedUrl* wynik
) {
    if(!wynik)return false;
    auto *u=new Husarz::Url;if(!u)return false;
    bool ok=Husarz::parse_absolute_url(url,*u);
    // BWS29/30 currently connect to the scheme's fixed port. Explicit default
    // ports are supported; other ports must never be silently redirected to it.
    if(ok&&u->port!=(u->secure?443:80))ok=false;
    if(ok){wynik->https=u->secure;kopiuj_limit(wynik->domena,sizeof(wynik->domena),u->host);kopiuj_limit(wynik->sciezka,sizeof(wynik->sciezka),u->target);}
    delete u;return ok;
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
        ) || tekst_zawiera_ci(content_type, "text/css") ||
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
    if(!cel||!location)return false;
    struct Scratch{char base[URL_POJEMNOSC]{};};auto *scratch=new Scratch;if(!scratch)return false;
    size_t n=0;const char *scheme=obecny.https?"https://":"http://";bool ok=true;
    auto add=[&](const char *text){for(size_t i=0;text[i];++i){if(n+1>=sizeof(scratch->base))return false;scratch->base[n++]=text[i];}scratch->base[n]=0;return true;};
    ok=add(scheme)&&add(obecny.domena)&&add(obecny.sciezka);
    if(ok){ok=Husarz::resolve_resource_url(scratch->base,location,cel,pojemnosc);}delete scratch;return ok;
}

// Parsing is independent of viewport geometry and painting.
__attribute__((noinline)) bool document_url_base(const char *url,const Husarz::Document *doc,ParsedUrl &base) {
    const char *origin=url;
    for(int i=0;i<liczba_zakladek;++i)if(doc && zakladki[i].dokument==doc && zakladki[i].published_url[0]){origin=zakladki[i].published_url;break;}
    if(!parsuj_url(origin,&base))return false;
    if(!doc || !doc->base_url || !*doc->str(doc->base_url))return true;
    struct Address { char text[URL_POJEMNOSC]{}; }; auto *address=new Address;
    if(!address)return true;
    if(ustaw_url_z_location(address->text,sizeof(address->text),doc->str(doc->base_url),base)) {
        auto *candidate=new ParsedUrl{};
        if(candidate && parsuj_url(address->text,candidate))base=*candidate;
        delete candidate;
    }
    delete address; return true;
}

void renderuj_html(Husarz::Document& document,const char* source,bool html) {
    Husarz::parse_html(document,source,Husarz::length(source,HTML_POJEMNOSC),html);
    Husarz::collect_styles(document);
    Husarz::prepare_controls(document);
}
void przygotuj_dokument_zakladki(Zakladka& z,bool resources);
void odswiez_pasek_stanu_natychmiast();

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
    z.dokument_brudny = true;
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
    z.dokument_brudny = true;
}

bool adres_ipv4_literal(const char *host, uint8_t out[4]) {
    if (!host)
        return false;
    size_t pos = 0;
    for (unsigned octet = 0; octet < 4; ++octet) {
        unsigned value = 0, digits = 0;
        while (host[pos] >= '0' && host[pos] <= '9') {
            if (++digits > 3)
                return false;
            value = value * 10 + unsigned(host[pos++] - '0');
        }
        if (!digits || value > 255)
            return false;
        out[octet] = uint8_t(value);
        if (octet < 3) {
            if (host[pos++] != '.')
                return false;
        } else if (host[pos])
            return false;
    }
    return true;
}
__attribute__((noinline)) bool consume_http_response(Zakladka &z,bool *redirect,char *redirect_url,const ParsedUrl &p,const char *data,size_t response_len,bool trusted) {
    *redirect=false; redirect_url[0]=0;
    OdpowiedzHttp odp{};

    if (!parsuj_odpowiedz_http(
            data,
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
                data
            );

        char location[
            URL_POJEMNOSC
        ] = {};

        if (!znajdz_header(
                data,
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
            trusted
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
    z.dokument_brudny = true;

    return true;
}

#ifdef HUSARZ_HOST_TEST
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

    struct ParsedOwner {ParsedUrl* value{new ParsedUrl{}};~ParsedOwner(){delete value;}} parsed;
    if(!parsed.value){ustaw_tresc(z,"Brak pamięci na adres strony.",false);return false;}
    ParsedUrl& p=*parsed.value;

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

    odswiez_pasek_stanu_natychmiast();

    uint8_t ip[
        4
    ] = {};

    if (!adres_ipv4_literal(p.domena,ip)&&!bws_siec_dns(
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

    // BWS94/95 preserve full-width pointers for relocatable applications.
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

    odswiez_pasek_stanu_natychmiast();

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

    return consume_http_response(z,redirect,redirect_url,p,siec_bufor,response_len,bws_tls_certyfikat_zaufany());
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
    z.dokument_brudny = true;

    ustaw_status("Ładowanie strony...");
    odswiez_pasek_stanu_natychmiast();

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

#endif

void begin_async_navigation(Zakladka&,bool);
void async_tick();
void async_receive();
void async_shutdown();
void cancel_async_tab(Zakladka&);

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

#ifdef HUSARZ_HOST_TEST
    nawigacja_trwa=true;
    (void)PobierzStrone(nowa_strona);
    przygotuj_dokument_zakladki(z,true);
    nawigacja_trwa=false;
#else
    begin_async_navigation(z,nowa_strona);
#endif
}

/* =========================================================================
 * 20. BURSZTYN UI - MODERN INTEGRATION
 * ========================================================================= */

class WidokStrony;
class OknoHusarz;

struct HusarzApp {
    AplikacjaGUI* app{nullptr};
    OknoHusarz* okno{nullptr};
    MenuGlowne* menu{nullptr};
    MenuKontekstowe* menu_kontekstowe{nullptr};
    MenuKontekstowe* menu_zakladki_popup{nullptr};

    Panel* pasek_kart{nullptr};
    Przycisk* btn_karty[MAX_ZAKLADKI]{};
    Przycisk* btn_zamknij_karty[MAX_ZAKLADKI]{};
    Przycisk* btn_nowa_karta{nullptr};

    Panel* pasek_narzedzi{nullptr};
    Przycisk* btn_wstecz{nullptr};
    Przycisk* btn_dalej{nullptr};
    Przycisk* btn_odswiez{nullptr};
    Przycisk* btn_zatrzymaj{nullptr};
    Przycisk* btn_dom{nullptr};
    PoleTekstowe* txt_adres{nullptr};
    Przycisk* btn_idz{nullptr};
    Przycisk* btn_zakladki{nullptr};

    WidokStrony* widok_strony{nullptr};
    PasekPrzewijania* suwak_pionowy{nullptr};
    PasekStanu* pasek_stanu{nullptr};

    void zbuduj_ui();
    void aktualizuj_pasek_kart();
    void aktualizuj_pasek_stanu();
    void przelacz_zakladke(int idx);
    void nowa_zakladka_gui();
    void zamknij_zakladke_gui(int idx);
    void przejdz_do_url(const char* url);
    void wstecz();
    void dalej();
    void odswiez();
    void zatrzymaj();
    void dom();
    void dodaj_obecna_do_ulubionych_akcja();
    void kopiuj_adres_do_schowka();
    void wklej_adres_ze_schowka();
    void przewin_do(int y);
    void pokaz_o_programie();
    void pokaz_zakladki();
};

static HusarzApp g_husarz;

void zaktualizuj_pasek_stanu_tekst(const char* txt) {
    if (g_husarz.pasek_stanu) {
        g_husarz.pasek_stanu->ustaw_tekst(0, txt ? txt : "");
    }
}

void odswiez_pasek_stanu_natychmiast();

int advance_font(void*,uint32_t cp,int font,bool bold,bool) {
    return Husarz::clamp64((int64_t(CzcionkaUI::szerokosc_znaku(cp)+1)*font+8)/16+(bold?1:0),1,1024);
}
int height_font(void*,int font){return font;}
const uint16_t* bitmap_font(void*,uint32_t cp){return CzcionkaUI::bitmapa_znaku(cp);}
int width_font(void*,uint32_t cp){return CzcionkaUI::szerokosc_znaku(cp);}
#if defined(BSF_NATIVE)
uint32_t browser_font_generation{};
int advance_family(void*,uint32_t cp,int px,bool bold,bool italic,uint32_t family){int a=BSF::measure_styled_cp(cp,px,bold,BSF::family_name(family),italic);return a>=0?a:BSF::family_monospace(family)?px:advance_font(nullptr,cp,px,bold,false);}
int height_family(void*,int px,uint32_t family){int h=BSF::height(px,BSF::family_name(family));return h>0?h:px;}
bool alpha_font(void*,uint32_t cp,int px,bool bold,bool italic,uint32_t family,Husarz::AlphaGlyph& out){const char* name=BSF::family_name(family);auto* g=BSF::glyph(cp,px,bold,name,italic);if(!g)return false;BSF::pin(g);out.width=g->width;out.height=g->height;out.x=g->x;out.y=BSF::ascent(px,name)+g->y;out.pixels=g->alpha;out.lease=const_cast<BSF::Glyph*>(g);out.release=BSF::unpin;return true;}
bool cached_font(void*,uint32_t cp,int px,bool bold,bool italic,uint32_t family){return BSF::cached(cp,px,bold,BSF::family_name(family),italic);}
const Husarz::Metrics browser_metrics{nullptr,advance_font,height_font,advance_family,height_family};
const Husarz::Glyphs browser_glyphs{nullptr,bitmap_font,width_font,alpha_font,cached_font};
#else
const Husarz::Metrics browser_metrics{nullptr,advance_font,height_font};
const Husarz::Glyphs browser_glyphs{nullptr,bitmap_font,width_font};
#endif

unsigned browser_edit_command(const ZdarzenieKlawiatury &e) {
    unsigned key=e.polecenie(),raw=e.kod_skanu;
    if((key|raw)&KLAWISZ_FIZYCZNY_GUI) {
        if(!raw)raw=key;
        if(raw&BWS_KEY_SOURCE_USB){switch(raw&255U){case 4:return 'a';case 6:return 'c';case 27:return 'x';case 25:return 'v';default:return 0;}}
        switch(raw&127U){case 0x1e:return 'a';case 0x2e:return 'c';case 0x2d:return 'x';case 0x2f:return 'v';default:return 0;}
    }
    switch(key){case 1:return 'a';case 3:return 'c';case 24:return 'x';case 22:return 'v';default:break;}
    return key<128?unsigned(Husarz::lower(char(key))):0;
}

void przewin_kolem(PasekPrzewijania& bar,ZdarzenieRolki& e){
    if(!e.delta_y)return;
    int64_t units=e.delta_y;if(units>=120||units<=-120)units/=120;
    bar.przewin_o(-units*bar.maly_krok());e.obsluzone=true;
}
class SuwakStrony : public PasekPrzewijania {
public:void obsluz_rolke(ZdarzenieRolki& e)override{przewin_kolem(*this,e);}
};
void submit_browser_form(Husarz::Document&,uint16_t);
void browser_script_step(uint64_t now);
void browser_flush_storage();
bool browser_script_event(Husarz::Document&,uint16_t,const char*,unsigned action=0,uint16_t control=0,unsigned key=0,bool ctrl=false,bool shift=false,bool alt=false,int x=0,int y=0);
bool browser_script_bypass{},browser_key_bypass{};
Husarz::ViewportRequest browser_viewport{};
void browser_request_work(Zakladka&);
bool browser_has_work();
bool browser_active_busy();
void browser_document_painted();
void browser_step_work();
void append_decimal(char *out,size_t &position,uint64_t value);
class WidokStrony : public Kontrolka {
    uint32_t selection_generation_{},selection_revision_{};uint16_t selection_node_{};uint32_t selection_tab_{};
    uint32_t* pixels_{};size_t capacity_{};
    uint32_t *back_pixels_{};size_t back_capacity_{};
    Husarz::Work paint_task_{};Husarz::WorkDriver paint_driver_{};Husarz::Surface paint_surface_{};
    bool paint_requested_{true};Husarz::Document *paint_document_{};
    uint32_t paint_generation_{},paint_revision_{},paint_reflow_{};
    int paint_width_{},paint_height_{},paint_scroll_{};uint16_t paint_focus_{},paint_select_{};
    const Husarz::Document *painted_{};uint32_t painted_generation_{},painted_revision_{},painted_reflow_{};
    int painted_width_{},painted_height_{},painted_scroll_{-1};uint16_t painted_focus_{},painted_select_{};
public:
    WidokStrony(){czy_moze_miec_fokus_=true;preferowany_rozmiar_=Rozmiar(-1,-1);}
    ~WidokStrony()override{paint_task_.cancel();if(pixels_)gui_free(pixels_);if(back_pixels_)gui_free(back_pixels_);}
    void przygotuj(bool reset=false){
        if(!g_husarz.suwak_pionowy||liczba_zakladek<=0)return;
        auto& z=zakladki[aktywna_zakladka];
        if(!z.dokument||(z.dokument_brudny&&!nawigacja_trwa))przygotuj_dokument_zakladki(z,false);
        auto* d=z.dokument;int w=obszar_.szerokosc,h=obszar_.wysokosc;
        if(w<1||h<1)return;
#ifdef HUSARZ_HOST_TEST
        if(d&&(d->styles_dirty||d->layout_width!=w||d->layout_revision!=d->revision))Husarz::reflow(*d,w,browser_metrics);
#else
        if(browser_viewport.request(w,h) && d){HUSARZ_COUNT(*d,resize_received,1);}
        if(z.dokument_brudny || !d || d->styles_dirty || d->layout_width!=w || d->layout_revision!=d->revision || (z.pending_css && z.pending_css->count))browser_request_work(z);
#endif
        auto& bar=*g_husarz.suwak_pionowy;
        bar.ustaw_zakres(0,d&&d->height>h?d->height-h:0);
        bar.ustaw_rozmiar_strony(h);bar.ustaw_maly_krok(d?d->styles[d->tag(1,"html")?1:0].line*3:60);
        bar.ustaw_duzy_krok(h>40?h-40:h);bar.ustaw_wlaczona(bar.maksimum()>0);
        if(reset)bar.ustaw_wartosc(0);
        size_t count=w<=4096&&h<=4096?size_t(w)*h:0;
        if(count>4U*1024*1024)count=0;
#ifdef HUSARZ_HOST_TEST
        if(count>capacity_){auto* next=static_cast<uint32_t*>(gui_malloc(count*sizeof(uint32_t)));
            if(next){if(pixels_)gui_free(pixels_);pixels_=next;capacity_=count;}}
#else
        (void)count;paint_requested_=true;
#endif
        uniewaznij();
    }
    void rozmiesc(Prostokat r)override{Kontrolka::rozmiesc(r);przygotuj();}
    bool czy_wymaga_animacji() const override;
    void krok_animacji(uint64_t now=0) override;
    void zaktualizuj_motyw()override{
        Kontrolka::zaktualizuj_motyw();
#if defined(BSF_NATIVE)
        uint32_t current=BSF::generation();
        if(current!=browser_font_generation){browser_font_generation=current;cancel_paint();for(int i=0;i<liczba_zakladek;++i){++zakladki[i].resource_revision;zakladki[i].work_pending=true;}uniewaznij();}
#endif
    }
    void rysuj_siebie(KontekstRysowania& ctx)override{
        const int w=obszar_.szerokosc,h=obszar_.wysokosc;
        if(w<=0||h<=0)return;
        auto* d=liczba_zakladek>0?zakladki[aktywna_zakladka].dokument:nullptr;
        if(!d||w>4096||h>4096
#ifdef HUSARZ_HOST_TEST
            ||!pixels_||size_t(w)*h>capacity_
#endif
        ){
            ctx.rysuj_prostokat({0,0,w,h},0xffffff);
            ctx.rysuj_tekst({8,8},"Dane strony lub bufor widoku niedostępne.",0x000000);return;}
#ifdef HUSARZ_HOST_TEST
        Husarz::Surface surface{pixels_,w,h};
        int scroll=int(g_husarz.suwak_pionowy->wartosc());
        dirty_focus(*d);
        bool full=d->damage.full || painted_generation_!=d->generation || painted_width_!=w || painted_height_!=h || painted_scroll_!=scroll;
        bool visual=painted_revision_!=d->paint_revision || painted_focus_!=d->focused_control || painted_select_!=d->open_select;
        if(full || (visual && (d->damage.full || painted_select_!=d->open_select)))
            Husarz::paint(*d,surface,scroll,browser_metrics,browser_glyphs);
        else if(visual)for(unsigned i=0;i<d->damage.count;++i){auto r=d->damage.rectangles[i];r.y-=scroll;
            r=Husarz::intersect(r,{0,0,w,h});if(r.width && r.height)Husarz::paint(*d,surface,scroll,browser_metrics,browser_glyphs,r);}
        painted_=d;painted_generation_=d->generation;painted_revision_=d->paint_revision;painted_reflow_=d->reflow_count;
        painted_width_=w;painted_height_=h;painted_scroll_=scroll;painted_focus_=d->focused_control;painted_select_=d->open_select;d->damage.clear();
        Punkt origin=ctx.pobierz_przesuniecie();
        for(int y=0;y<h;++y)ctx.rysuj_wiersz_ekranu(origin.x,origin.y+y,w,pixels_+size_t(y)*w);
#else
        int scroll=int(g_husarz.suwak_pionowy->wartosc());
        if(painted_!=d || painted_generation_!=d->generation || painted_revision_!=d->paint_revision || painted_reflow_!=d->reflow_count || painted_width_!=w || painted_height_!=h || painted_scroll_!=scroll || painted_focus_!=d->focused_control || painted_select_!=d->open_select)paint_requested_=true;
        ctx.rysuj_prostokat({0,0,w,h},d->user_agent.canvas);
        Punkt origin=ctx.pobierz_przesuniecie();
        if(pixels_){int rows=h<painted_height_?h:painted_height_,columns=w<painted_width_?w:painted_width_;
            for(int y=0;y<rows;++y)ctx.rysuj_wiersz_ekranu(origin.x,origin.y+y,columns,pixels_+size_t(y)*painted_width_);}
#endif
    }
#ifdef HUSARZ_HOST_TEST
    uint64_t document_pixel_hash() {
        KontekstRysowania ctx; auto area=obszar_ekranu(); ctx.dodaj_przesuniecie({area.x,area.y}); rysuj_siebie(ctx);
        uint64_t hash=1469598103934665603ULL;
        for (size_t i=0;i<size_t(obszar_.szerokosc)*obszar_.wysokosc;++i) { hash^=pixels_[i]; hash*=1099511628211ULL; }
        return hash;
    }
#endif
    void dirty_focus(Husarz::Document &d){
        if(painted_generation_!=d.generation)return;
        if(d.open_select && painted_revision_!=d.paint_revision)Husarz::invalidate_select_popup(d,g_husarz.suwak_pionowy?int(g_husarz.suwak_pionowy->wartosc()):0,obszar_.szerokosc,obszar_.wysokosc);
        if(painted_focus_==d.focused_control)return;
        uint16_t ids[2]={painted_focus_,d.focused_control};for(uint16_t id:ids)if(id && id<d.node_count){auto &b=d.boxes[id];d.damage.add({b.x,b.y,b.width,b.height});}
    }
    bool paint_pending()const{return bool(paint_task_.handle) || (paint_requested_ && liczba_zakladek>0 && zakladki[aktywna_zakladka].dokument); }
    void cancel_paint(){paint_task_.cancel();paint_driver_={};paint_requested_=true;paint_document_=nullptr;}
    Husarz::Work render_pending_page(){
        auto &d=*paint_document_;
        bool full=painted_generation_!=d.generation || painted_width_!=paint_width_ || painted_height_!=paint_height_ || painted_scroll_!=paint_scroll_ || d.damage.full || painted_select_!=paint_select_;
        if(full)co_await Husarz::paint_work(d,paint_surface_,paint_scroll_,browser_metrics,browser_glyphs,{0,0,paint_width_,paint_height_});
        else {
            co_await Husarz::copy_document_bytes(back_pixels_,pixels_,size_t(paint_width_)*paint_height_*4);
            Husarz::DirtyRectangles regions=d.damage;
            for(unsigned i=0;i<regions.count;++i){auto r=regions.rectangles[i];r.y-=paint_scroll_;
                r=Husarz::intersect(r,{0,0,paint_width_,paint_height_});if(r.width && r.height)co_await Husarz::paint_work(d,paint_surface_,paint_scroll_,browser_metrics,browser_glyphs,r);}
        }
    }
    void advance_paint(){
#ifndef HUSARZ_HOST_TEST
        auto *d=liczba_zakladek?zakladki[aktywna_zakladka].dokument:nullptr;
        int w=obszar_.szerokosc,h=obszar_.wysokosc,scroll=g_husarz.suwak_pionowy?int(g_husarz.suwak_pionowy->wartosc()):0;
        if(paint_task_.handle && (d!=paint_document_ || !d || d->generation!=paint_generation_ || d->paint_revision!=paint_revision_ || d->reflow_count!=paint_reflow_ || w!=paint_width_ || h!=paint_height_ || scroll!=paint_scroll_ || d->focused_control!=paint_focus_ || d->open_select!=paint_select_))cancel_paint();
        if(!d || w<1 || h<1 || w>4096 || h>4096 || size_t(w)*h>4U*1024*1024)return;
        // Keep published pixels while the mutable model needs new geometry.
        if(d->styles_dirty || d->layout_revision!=d->revision || d->layout_width!=w)return;
        if(!paint_task_.handle){
            if(!paint_requested_)return;
            if(painted_generation_==d->generation && painted_revision_==d->paint_revision && painted_reflow_==d->reflow_count && painted_width_==w && painted_height_==h && painted_scroll_==scroll && painted_focus_==d->focused_control && painted_select_==d->open_select && !d->damage.full && !d->damage.count){paint_requested_=false;browser_document_painted();return;}
            size_t count=size_t(w)*h;
            if(count>back_capacity_){auto *p=static_cast<uint32_t*>(gui_malloc(count*4));if(!p)return;if(back_pixels_)gui_free(back_pixels_);back_pixels_=p;back_capacity_=count;}
            dirty_focus(*d);
            paint_document_=d;paint_generation_=d->generation;paint_revision_=d->paint_revision;paint_reflow_=d->reflow_count;
            paint_width_=w;paint_height_=h;paint_scroll_=scroll;paint_focus_=d->focused_control;paint_select_=d->open_select;
            paint_surface_={back_pixels_,w,h};paint_driver_={};paint_driver_.allow_clock=false;paint_driver_.clock=Husarz::native_work_clock.valid?Husarz::native_work_now:nullptr;paint_task_=render_pending_page();
            if(!paint_task_.handle)return;
        }
        if(!paint_task_.step(paint_driver_))return;
        if(!paint_driver_.failed){
            auto *swap=pixels_;pixels_=back_pixels_;back_pixels_=swap;size_t old=capacity_;capacity_=back_capacity_;back_capacity_=old;
            painted_=d;painted_generation_=paint_generation_;painted_revision_=paint_revision_;painted_reflow_=paint_reflow_;
            painted_width_=w;painted_height_=h;painted_scroll_=scroll;painted_focus_=paint_focus_;painted_select_=paint_select_;
            d->damage.clear();paint_requested_=false;uniewaznij();browser_document_painted();
#if HUSARZ_PERF
            if(paint_driver_.maximum_ns>d->perf.max_slice_ns)d->perf.max_slice_ns=paint_driver_.maximum_ns;
            if(paint_driver_.maximum>d->perf.max_slice_work)d->perf.max_slice_work=paint_driver_.maximum;
            Husarz::report_performance(*d,browser_viewport);
#endif
        }
        paint_task_.cancel();paint_driver_={};
#endif
    }
    void obsluz_rolke(ZdarzenieRolki& e)override{
        if((!e.delta_y && !e.delta_x)||!g_husarz.suwak_pionowy)return;
        auto *d=liczba_zakladek>0?zakladki[aktywna_zakladka].dokument:nullptr;
        if(d){
            auto screen=obszar_ekranu();int x=e.x-screen.x,y=e.y-screen.y+int(g_husarz.suwak_pionowy->wartosc());
            for(unsigned i=d->control_count;i-- >0;){auto &c=d->controls[i];auto &b=d->boxes[c.node];
                if(!Husarz::selectable(*d,c) || !d->styles[c.node].visible || d->styles[c.node].display==Husarz::Display::None)continue;
                auto clip=Husarz::intersect(Husarz::ancestor_clip(*d,c.node,0,obszar_.szerokosc,Husarz::MaxDimension),{b.x,b.y,b.width,b.height});
                if(x<clip.x || y<clip.y || int64_t(x)>=int64_t(clip.x)+clip.width || int64_t(y)>=int64_t(clip.y)+clip.height)continue;
                int64_t dx=e.delta_x,dy=e.delta_y;if(dx>=120 || dx<=-120)dx/=120;if(dy>=120 || dy<=-120)dy/=120;
                if(e.czy_shift()){dx+=dy;dy=0;}
                int line=Husarz::ControlTextWalker(*d,c,browser_metrics).line;
                if(Husarz::control_scroll(*d,c,browser_metrics,-dx*line*3,-dy*line*3)){przygotuj();e.obsluzone=true;return;}
                break;
            }
        }
        przewin_kolem(*g_husarz.suwak_pionowy,e);
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e)override{
        auto* bar=g_husarz.suwak_pionowy;if(!bar)return;
        auto *d=liczba_zakladek>0?zakladki[aktywna_zakladka].dokument:nullptr;
        unsigned key=e.polecenie();
        if(d&&!browser_key_bypass&&browser_script_event(*d,d->focused_control?d->focused_control:1,"keydown",4,0,key,e.czy_ctrl(),e.czy_shift(),e.czy_alt())){e.obsluzone=true;return;}
#if HUSARZ_DEBUG_RESOURCES
        if (d && d->open_select) {
            char diagnostic[128]="[HUSARZ FORM] key="; size_t used=18;
            append_decimal(diagnostic,used,key);
            const char *label=" focus="; for(unsigned i=0;label[i];++i)diagnostic[used++]=label[i];
            append_decimal(diagnostic,used,d->focused_control);
            label=" select="; for(unsigned i=0;label[i];++i)diagnostic[used++]=label[i];
            append_decimal(diagnostic,used,d->open_select);
            diagnostic[used++]='\n';diagnostic[used]=0;wypisz(diagnostic);
        }
#endif
        if (d && key==9) {
            uint16_t old_focus=d->focused_control;d->open_select=0;
            int current=-1;
            for (unsigned i=0;i<d->control_count;++i) if (d->controls[i].node==d->focused_control) current=int(i);
            for (unsigned step=0;step<d->control_count;++step) {
                current=(current+(e.czy_shift()?-1:1)+int(d->control_count))%int(d->control_count);
                uint16_t node=d->controls[current].node;
                if (!d->has_attr(node,"disabled") && d->styles[node].display!=Husarz::Display::None) { d->focused_control=node; break; }
            }
            if(old_focus!=d->focused_control){if(old_focus)browser_script_event(*d,old_focus,"blur");if(d->focused_control)browser_script_event(*d,d->focused_control,"focus");}
            uniewaznij(); e.obsluzone=true; return;
        }
        if (d && d->focused_control) {
            auto *c=Husarz::control(*d,d->focused_control);
            bool edited=false;
            if(c && e.czy_ctrl() && !e.czy_alt()) {
                unsigned command=browser_edit_command(e);
                if((command=='a' && Husarz::form_key(*d,'a',e.czy_shift(),true)) || Husarz::form_clipboard(*d,command,{clipboard_set,clipboard_read},&edited)) {
                    if(edited)browser_script_event(*d,c->node,"input");
                    Husarz::control_keep_caret(*d,*c,browser_metrics);przygotuj();e.obsluzone=true;return;
                }
            }
            if (c && (Husarz::control_vertical_key(*d,key,e.czy_shift(),browser_metrics) || (!e.czy_ctrl() && Husarz::control_horizontal_key(*d,key,e.czy_shift(),browser_metrics)) || Husarz::form_key(*d,key,e.czy_shift(),e.czy_ctrl(),&edited))) {
                if(edited)browser_script_event(*d,c->node,"input");
                Husarz::control_keep_caret(*d,*c,browser_metrics);przygotuj(); e.obsluzone=true; return;
            }
            if (c && d->tag(c->node,"select") && (key==0x48 || key==0x50 || key==32 || key==13)) {
                if (key==13 && d->open_select) d->open_select=0;
                else if (key==32 || key==13) d->open_select=c->node;
                else {auto previous=c->option;Husarz::form_select(*d,*c,key==0x48?-1:1);if(c->option!=previous){browser_script_event(*d,c->node,"input");browser_script_event(*d,c->node,"change");}}
                przygotuj(); e.obsluzone=true; return;
            }
            if (c && (key==32 || key==13) && (Husarz::input_type(*d,c->node,"checkbox") || Husarz::input_type(*d,c->node,"radio"))) {
                bool previous=c->checked;Husarz::form_toggle(*d,*c);if(c->checked!=previous){browser_script_event(*d,c->node,"input");browser_script_event(*d,c->node,"change");} przygotuj(); e.obsluzone=true; return;
            }
            if (c && key==13) { submit_browser_form(*d,c->node); e.obsluzone=true; return; }
        }
        switch(key){
        case 0x48:bar->przewin_o(-bar->maly_krok()/3);break;
        case 0x50:bar->przewin_o(bar->maly_krok()/3);break;
        case 0x49:bar->przewin_strone(-1);break;
        case 0x51:bar->przewin_strone(1);break;
        case 0x47:bar->ustaw_wartosc(0);break;
        case 0x4f:bar->ustaw_wartosc(bar->maksimum());break;
        default:return;}
        e.obsluzone=true;
    }
    void obsluz_zwolnienie_klawisza(ZdarzenieKlawiatury& e)override{
        if(liczba_zakladek){auto *d=zakladki[aktywna_zakladka].dokument;if(d)browser_script_event(*d,d->focused_control?d->focused_control:1,"keyup",0,0,e.polecenie(),e.czy_ctrl(),e.czy_shift(),e.czy_alt());}
    }
    void obsluz_wprowadzenie_tekstu(ZdarzenieWprowadzaniaTekstu& e) override {
        auto *d=liczba_zakladek>0?zakladki[aktywna_zakladka].dokument:nullptr;
        if(d&&d->focused_control&&browser_script_event(*d,d->focused_control,"@text",5,0,e.punkt_kodowy)){e.obsluzone=true;return;}
        bool edited=false;
        if (d && Husarz::form_insert(*d,e.punkt_kodowy,&edited)) {
            if(edited)browser_script_event(*d,d->focused_control,"input");
            if(auto *c=Husarz::control(*d,d->focused_control))Husarz::control_keep_caret(*d,*c,browser_metrics);
            przygotuj(); e.obsluzone=true;
        }
    }
    void script_default_click(uint16_t node,int x,int y,bool shift);
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e)override;
    void obsluz_ruch_myszy(ZdarzenieMyszki& e)override {
        if(liczba_zakladek){auto *d=zakladki[aktywna_zakladka].dokument;if(d&&d->layout_revision==d->revision){auto screen=obszar_ekranu();int x=e.x-screen.x,y=e.y-screen.y+int(g_husarz.suwak_pionowy->wartosc());uint16_t target=0;for(uint16_t i=1;i<d->node_count;++i){auto b=d->boxes[i];if(!d->nodes[i].detached&&d->styles[i].display!=Husarz::Display::None&&x>=b.x&&y>=b.y&&int64_t(x)<int64_t(b.x)+b.width&&int64_t(y)<int64_t(b.y)+b.height)target=i;}if(target)browser_script_event(*d,target,"mousemove",0,0,0,e.czy_ctrl(),e.czy_shift(),e.czy_alt(),x,y);}}
        if(!selection_node_ || liczba_zakladek<=0)return;
        auto &tab=zakladki[aktywna_zakladka];auto *d=tab.dokument;
        if(!d || d->generation!=selection_generation_ || d->revision!=selection_revision_ || d->focused_control!=selection_node_ || uint32_t(aktywna_zakladka)!=selection_tab_){selection_node_=0;return;}
        if(auto *c=Husarz::control(*d,selection_node_)) {
            auto screen=obszar_ekranu();Husarz::control_pointer(*d,*c,browser_metrics,e.x-screen.x,e.y-screen.y+int(g_husarz.suwak_pionowy->wartosc()),true);
            przygotuj();e.obsluzone=true;
        }
    }
    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e)override {
        if(selection_node_){obsluz_ruch_myszy(e);selection_node_=0;e.obsluzone=true;}
        if(liczba_zakladek&&zakladki[aktywna_zakladka].dokument){auto &d=*zakladki[aktywna_zakladka].dokument;if(d.layout_revision==d.revision){auto screen=obszar_ekranu();int x=e.x-screen.x,y=e.y-screen.y+int(g_husarz.suwak_pionowy->wartosc());uint16_t target=0;for(uint16_t i=1;i<d.node_count;++i){auto b=d.boxes[i];if(!d.nodes[i].detached&&d.styles[i].display!=Husarz::Display::None&&x>=b.x&&y>=b.y&&int64_t(x)<int64_t(b.x)+b.width&&int64_t(y)<int64_t(b.y)+b.height)target=i;}if(target&&d.nodes[target].is_text)target=d.nodes[target].parent;if(target)browser_script_event(d,target,"mouseup");}}
        Kontrolka::obsluz_zwolnienie_myszy(e);
    }
};

__attribute__((noinline)) void otworz_argument_startowy(){
    char argument[URL_POJEMNOSC]{};
    if(pobierz_argument_startowy(argument,sizeof(argument))&&*argument)g_husarz.przejdz_do_url(argument);
    else g_husarz.przelacz_zakladke(0);
}

class OknoHusarz : public Okno {
public:
    explicit OknoHusarz(const char* tytul, int szer = DOMYSLNY_WIN_W, int wys = DOMYSLNY_WIN_H)
        : Okno(tytul, szer, wys) {}
    void zamknij()override {
        // The base window closes through a non-returning process syscall.
        // Flush and stop the single-instance worker before that boundary.
        browser_flush_storage();
        async_shutdown();
        Okno::zamknij();
    }
    ~OknoHusarz()override {
        zarzadca_popupow().zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
        auto* root=kontrolka_glowna();ustaw_kontrolke_glowna(nullptr);delete root;
        delete g_husarz.menu_kontekstowe;delete g_husarz.menu_zakladki_popup;
        async_shutdown();
        g_husarz={};
    }

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        if(echo_klawiatury(ev))return;
#if HUSARZ_DEBUG_RESOURCES
        if (ev.typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && ev.x && liczba_zakladek>0 &&
            zakladki[aktywna_zakladka].dokument && zakladki[aktywna_zakladka].dokument->open_select)
            wypisz(fokus()==g_husarz.widok_strony ? "[HUSARZ FORM] viewport focus\n" : "[HUSARZ FORM] other focus\n");
#endif
        if(ev.typ==BWS_ZDARZENIE_IPC_READY){async_receive();return;}
        if (ev.typ == BWS_ZDARZENIE_OTWORZ_PLIK) {
            otworz_argument_startowy();
            return;
        }

        if (ev.typ == BWS_ZDARZENIE_KLAWISZ || ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY) {
            bool fizyczny = (ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY);
            bool wcisniety = !fizyczny || ev.x != 0;
            if (wcisniety) {
                uint32_t kod = ev.kod;
                bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
                uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);
                // AltGr (takze Ctrl+AltGr) wpisuje polskie znaki: nie jest skrotem.
                bool ctrl = bws_mod_ctrl_skrotu(ev.przyciski);
                bool alt = bws_mod_alt_skrotu(ev.przyciski);

                // F5 - Odswiez
                bool is_f5 = (kod == 0x3F || (usb && baza == 0x3E) || (!usb && baza == 0x3F));
                if (is_f5) {
                    g_husarz.odswiez();
                    return;
                }

                // Esc - Zatrzymaj
                bool is_esc = (kod == 27 || (usb && baza == 41) || (!usb && baza == 1));
                if (is_esc) {
                    g_husarz.zatrzymaj();
                    return;
                }

                if (ctrl) {
                    // Ctrl+L - Zaznacz pasek adresu
                    if (kod == 'l' || kod == 'L' || (usb && baza == 15) || (!usb && baza == 0x26)) {
                        if (g_husarz.txt_adres) {
                            ustaw_fokus(g_husarz.txt_adres);
                            g_husarz.txt_adres->zaznacz_wszystko();
                        }
                        return;
                    }
                    // Ctrl+R - Odswiez
                    if (kod == 'r' || kod == 'R' || (usb && baza == 21) || (!usb && baza == 0x13)) {
                        g_husarz.odswiez();
                        return;
                    }
                    // Ctrl+T - Nowa karta
                    if (kod == 't' || kod == 'T' || (usb && baza == 23) || (!usb && baza == 0x14)) {
                        g_husarz.nowa_zakladka_gui();
                        return;
                    }
                    // Ctrl+W - Zamknij karte
                    if (kod == 'w' || kod == 'W' || (usb && baza == 26) || (!usb && baza == 0x11)) {
                        g_husarz.zamknij_zakladke_gui(aktywna_zakladka);
                        return;
                    }
                    // Ctrl+D - Dodaj do zakladek
                    if (kod == 'd' || kod == 'D' || (usb && baza == 7) || (!usb && baza == 0x20)) {
                        g_husarz.dodaj_obecna_do_ulubionych_akcja();
                        return;
                    }
                }

                if (alt) {
                    // Alt+Left - Wstecz
                    if ((usb && baza == 80) || (!usb && baza == 0x4B)) {
                        g_husarz.wstecz();
                        return;
                    }
                    // Alt+Right - Dalej
                    if ((usb && baza == 79) || (!usb && baza == 0x4D)) {
                        g_husarz.dalej();
                        return;
                    }
                    // Alt+Home - Strona domowa
                    if ((usb && baza == 74) || (!usb && baza == 0x47)) {
                        g_husarz.dom();
                        return;
                    }
                }
            }
        }

        // Web controls own editing and intra-document Tab before the native
        // window's global focus traversal. Right Alt belongs to Polish AltGr,
        // not to native menu activation while the document has keyboard focus.
        if ((ev.typ==BWS_ZDARZENIE_KLAWISZ || ev.typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY) &&
            fokus()==g_husarz.widok_strony && g_husarz.widok_strony && !zarzadca_popupow().czy_otwarty()) {
            bool physical=ev.typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
            bool usb=(ev.kod&BWS_KEY_SOURCE_USB)!=0;
            if (physical && ((usb && (ev.kod&255U)==230U) ||
                (!usb && (ev.kod&BWS_KEY_SOURCE_E0) && (ev.kod&127U)==0x38U))) return;
            if (!physical || ev.x) {
                ZdarzenieKlawiatury key;
                key.kod_klawisza=klawisz_polecenia(ev);
                key.kod_skanu=physical?ev.kod|KLAWISZ_FIZYCZNY_GUI:0;
                key.modyfikatory=ev.przyciski;
                g_husarz.widok_strony->obsluz_wcisniecie_klawisza(key);
                if(key.obsluzone)return;
            }
        }
        Okno::obsluz_zdarzenie(ev);
    }
};

int maksimum_przewijania(){return g_husarz.suwak_pionowy?int(g_husarz.suwak_pionowy->maksimum()):0;}
void zresetuj_przewijanie(){if(g_husarz.suwak_pionowy){g_husarz.suwak_pionowy->ustaw_zakres(0,0);g_husarz.suwak_pionowy->ustaw_wartosc(0);}}
[[maybe_unused]] void odswiez_pasek_stanu_natychmiast(){
    if(!g_husarz.okno||!g_husarz.pasek_stanu||KontekstRysowania::g_tlum_rysowanie_globalne)return;
    // Always paint through the owning window and its translated/clipped tree.
    g_husarz.okno->przelicz_uklad();
    KontekstRysowania ctx;g_husarz.okno->rysuj(ctx);gui_odswiez();
}
Husarz::ResourceCache *resource_cache{};
Husarz::ResourceCache *cache_resources() {
    if (!resource_cache) resource_cache=new Husarz::ResourceCache(gui_malloc,gui_free);
    return resource_cache;
}
// The shared codec's temporary allocations have a separate hard workspace budget.
size_t decode_workspace{};
void *browser_image_alloc(size_t n) {
    constexpr size_t limit=16*1024*1024;
    if (!n || n>limit || decode_workspace>limit-n) return nullptr;
    auto *p=static_cast<size_t*>(gui_malloc(n+sizeof(size_t)));
    if (!p) return nullptr;
    *p=n; decode_workspace+=n; return p+1;
}
void browser_image_free(void *ptr) {
    if (!ptr) return;
    auto *p=static_cast<size_t*>(ptr)-1; decode_workspace-=*p; gui_free(p);
}
bool decode_browser_image(Husarz::ResourceCache &cache,Husarz::ResourceEntry &entry) {
    if (entry.pixels) return true;
    uint32_t w=0,h=0; auto format=rozpoznaj_obraz(entry.raw,entry.size);
    auto result=format==FormatObrazu::PNG ? sprawdz_png(entry.raw,entry.size,&w,&h) :
                format==FormatObrazu::JPEG ? sprawdz_jpeg(entry.raw,entry.size,&w,&h) :
                format==FormatObrazu::GIF ? sprawdz_gif(entry.raw,entry.size,&w,&h) : BladObrazu::Format;
    if (result!=BladObrazu::OK || !w || !h || w>Husarz::MaxImageSide || h>Husarz::MaxImageSide ||
        uint64_t(w)*h>Husarz::MaxImagePixels) { ++cache.image_failures; return false; }
    size_t bytes=size_t(w)*h*4;
    ++entry.refs; // room() may evict other resources, never the compressed source being decoded.
    bool room=cache.room(0,bytes,false);
    --entry.refs;
    if (!room) return false;
    DekodowanyObraz decoded{};
    result=dekoduj_obraz_z_alfa(entry.raw,entry.size,&decoded,browser_image_alloc,browser_image_free);
    if (result!=BladObrazu::OK) { ++cache.image_failures; return false; }
    auto *pixels=static_cast<uint32_t*>(gui_malloc(bytes));
    if (!pixels) { browser_image_free(decoded.piksele); return false; }
    for (size_t i=0;i<bytes;++i) reinterpret_cast<uint8_t*>(pixels)[i]=decoded.piksele[i];
    browser_image_free(decoded.piksele);
    entry.pixels=pixels; entry.width=w; entry.height=h; entry.decoded=bytes;
    cache.decoded_bytes+=bytes; ++cache.images_decoded; return true;
}
// Diagnostics are compiled out of ordinary release builds. Values are escaped
// so a malformed resource cannot forge another serial diagnostic line.
void resource_field(const char *key,const char *value) {
#if HUSARZ_DEBUG_RESOURCES
    char *line=new char[2304];if(!line)return;size_t n=0;
    const char *prefix="[HUSARZ RESOURCE] ";while(*prefix)line[n++]=*prefix++;
    for(size_t i=0;key[i]&&n<200;++i)line[n++]=key[i];line[n++]='=';
    for(size_t i=0;value&&value[i]&&n<2300;++i){unsigned c=uint8_t(value[i]);line[n++]=c<32||c==127?'?':char(c);}
    line[n++]='\n';line[n]=0;wypisz(line);delete[] line;
#else
    (void)key;(void)value;
#endif
}
void resource_number(const char *key,uint64_t value){
#if HUSARZ_DEBUG_RESOURCES
    char text[24]{};size_t n=0;char reverse[24];unsigned k=0;do{reverse[k++]=char('0'+value%10);value/=10;}while(value);while(k)text[n++]=reverse[--k];resource_field(key,text);
#else
    (void)key;(void)value;
#endif
}
void resource_address(const char *url,Husarz::ResourceType type){
#if HUSARZ_DEBUG_RESOURCES
    resource_field("type",type==Husarz::ResourceType::Css?"stylesheet":type==Husarz::ResourceType::Script?"script":type==Husarz::ResourceType::Image?"image":"fetch");
    resource_field("resolved",url);auto *parsed=new ParsedUrl{};
    if(parsed&&parsuj_url(url,parsed)){resource_field("scheme",parsed->https?"https":"http");resource_field("host",parsed->domena);resource_number("port",parsed->https?443:80);resource_field("request-target",parsed->sciezka);size_t split=0;while(parsed->sciezka[split]&&parsed->sciezka[split]!='?')++split;char mark=parsed->sciezka[split];parsed->sciezka[split]=0;resource_field("path",parsed->sciezka);parsed->sciezka[split]=mark;resource_field("query",mark?parsed->sciezka+split+1:"");size_t fragment=0;while(url[fragment]&&url[fragment]!='#')++fragment;resource_field("fragment",url[fragment]?url+fragment+1:"");}
    else resource_field("error","URL_PARSE");delete parsed;
#else
    (void)url;(void)type;
#endif
}
[[maybe_unused]] Husarz::Work debug_computed_style(Husarz::Document &d,uint16_t node){
#if HUSARZ_DEBUG_RESOURCES
    if(node>=d.node_count)co_return;
    resource_field("CSS COMPUTED node",*d.attr(node,"id")?d.attr(node,"id"):d.str(d.nodes[node].tag));
    Husarz::Winner winners[Husarz::PCount];uint32_t budget=100000;
    auto offer=[&](const Husarz::Declaration &source,int specificity,uint64_t order){auto decl=source;Husarz::Span v{d.str(decl.value),Husarz::length(d.str(decl.value),Husarz::MaxAttrLength)};
        if(Husarz::has_variable(v)){char value[Husarz::MaxResolvedValue+1]{};size_t n=0;Husarz::VariableResolver resolver{d};if(!resolver.expand(node,v,value,sizeof(value),n))return;decl.value=d.store(value,n,Husarz::MaxResolvedValue);}
        Husarz::offer_declaration(d,winners,decl,specificity,order);};
    for(unsigned r=0;r<d.rule_count;++r){co_await Husarz::WorkPoint{1};const auto &rule=d.rules[r];if(rule.pseudo||d.style_viewport_width<rule.media_min||d.style_viewport_width>rule.media_max)continue;
        auto selector=Husarz::selector({d.str(rule.selector),Husarz::length(d.str(rule.selector),Husarz::MaxSelectorLength)});bool matched=false;co_await Husarz::matches_work(d,node,selector,budget,matched);if(!matched)continue;
        resource_field("matched-selector",d.str(rule.selector));resource_number("specificity",unsigned(selector.specificity));resource_number("source-order",rule.order);
        for(unsigned i=0;i<rule.count;++i){co_await Husarz::WorkPoint{Husarz::MaxSliceWork};offer(d.declarations[rule.first+i],selector.specificity,uint64_t(rule.order)*(Husarz::MaxDeclarations+1)+i);}
    }
    const auto &n=d.nodes[node];for(unsigned i=0;i<n.inline_count;++i){co_await Husarz::WorkPoint{Husarz::MaxSliceWork};offer(d.declarations[n.inline_first+i],1<<24,(UINT64_C(1)<<60)+i);}
    const int properties[]={Husarz::PColor,Husarz::PBackground};const char *names[]={"color-source","background-source"};
    for(unsigned i=0;i<2;++i){const auto &winner=winners[properties[i]];resource_field(names[i],winner.specificity<0?(i==0?"inherited/UA":"initial"):winner.base_url?d.str(winner.base_url):winner.user_agent?"UA":"inline author style");if(winner.specificity>=0){char value[Husarz::MaxResolvedValue+1]{};size_t bytes=winner.value.size;if(bytes>Husarz::MaxResolvedValue)bytes=Husarz::MaxResolvedValue;for(size_t j=0;j<bytes;++j)value[j]=winner.value.data[j];resource_field("winning-value",value);}}
    auto color=[&](const char *name,uint32_t value){char text[8]="#000000";const char *digits="0123456789abcdef";for(unsigned i=0;i<6;++i)text[6-i]=digits[(value>>(i*4))&15];resource_field(name,text);};
    color("computed-color",d.styles[node].color);color("computed-background",d.styles[node].background);resource_number("font-weight",d.styles[node].bold?700:400);resource_field("display",d.styles[node].grid?"grid":d.styles[node].flex?"flex":d.styles[node].display==Husarz::Display::Block?"block":"inline");
#else
    (void)d;(void)node;
#endif
    co_return;
}
bool resource_mime_is(const char *value,const char *expected) {
    return Husarz::mime_is(value,expected);
}
// HTTP responses must use the binary-safe count, never strlen on image data.
Husarz::ResourceEntry *fetch_browser_resource(const char *url,Husarz::ResourceType type,const char *origin=nullptr) {
    // BSF 2.2: only a valid serialized origin is ever sent (kernel re-checks).
    if(origin&&!http_origin_poprawny(origin))origin=nullptr;
    struct Address { char normalized[URL_POJEMNOSC]{},original[URL_POJEMNOSC]{},location[URL_POJEMNOSC]{},visited[MAX_PRZEKIEROWAN+1][URL_POJEMNOSC]{}; };
    struct AddressOwner { Address *value{new Address}; ~AddressOwner(){delete value;} } address;
    if (!address.value || !Husarz::normalize_resource_url(url,address.value->normalized,sizeof(address.value->normalized))) return nullptr;
    url=address.value->normalized;
    resource_address(url,type);
    auto *cache=cache_resources(); if (!cache) return nullptr;
    if (auto *hit=cache->find(url,type,gui_czas_monotoniczny_ns())) {resource_field("cache","HIT");return hit;}
    resource_field("cache","MISS");
    auto &address_data=*address.value;kopiuj_limit(address_data.original,sizeof(address_data.original),url);
    OdpowiedzHttp response{};size_t header_size=0;bool received=false;
    for(unsigned hop=0;hop<=MAX_PRZEKIEROWAN;++hop){
        for(unsigned previous=0;previous<hop;++previous)if(Husarz::equal(address_data.visited[previous],url)){resource_field("error","REDIRECT_LOOP");return nullptr;}
        kopiuj_limit(address_data.visited[hop],sizeof(address_data.visited[hop]),url);
        auto *parsed=new ParsedUrl{};if(!parsed)return nullptr;
        if(!parsuj_url(url,parsed)){delete parsed;resource_field("error","URL_PARSE");return nullptr;}
        uint8_t ip[4]{};
        if(!adres_ipv4_literal(parsed->domena,ip)&&!bws_siec_dns(parsed->domena,ip)){delete parsed;resource_field("error","DNS_FAILED");return nullptr;}
        uint32_t count=bws_siec_pobierz_bajty_origin(parsed->https,ip,parsed->domena,parsed->sciezka,siec_bufor,sizeof(siec_bufor),origin);
        resource_number("response-bytes",count);
        if(!count||!parsuj_odpowiedz_http(siec_bufor,count,&response)){delete parsed;resource_field("error",count?"HTTP_PARSE":"REQUEST_FAILED");return nullptr;}
        resource_number("HTTP",response.kod);resource_number("body-bytes",response.body_len);header_size=size_t(response.body-siec_bufor);
        if(response.kod==301||response.kod==302||response.kod==303||response.kod==307||response.kod==308){
            if(type==Husarz::ResourceType::Fetch){delete parsed;resource_field("error","FETCH_REDIRECT_POLICY");return nullptr;}
            if(hop==MAX_PRZEKIEROWAN){delete parsed;resource_field("error","REDIRECT_LIMIT");return nullptr;}
            if(!znajdz_header(siec_bufor,header_size,"location",address_data.location,sizeof(address_data.location))){delete parsed;resource_field("error","REDIRECT_LOCATION");return nullptr;}
            resource_field("redirect-Location",address_data.location);
            bool resolved=ustaw_url_z_location(address_data.normalized,sizeof(address_data.normalized),address_data.location,*parsed);delete parsed;
            if(!resolved){resource_field("error","REDIRECT_URL");return nullptr;}
            resource_address(url,type);continue;
        }
        delete parsed;
        if(type!=Husarz::ResourceType::Fetch&&!http_kod_sukces(response.kod)){resource_field("error","HTTP_STATUS");return nullptr;}
        received=true;break;
    }
    if(!received)return nullptr;
    if(type==Husarz::ResourceType::Fetch&&!response.chunked&&response.body_len>Husarz::Js::BrowserRuntime::FetchBodyLimit)return nullptr;
    char mime[96]{}, directive[128]{}, content_length[32]{};
    if (!znajdz_header(siec_bufor,header_size,"content-type",mime,sizeof(mime))&&type!=Husarz::ResourceType::Fetch&&type!=Husarz::ResourceType::Font) {resource_field("error","MIME_MISSING");return nullptr;}
    // BSF 2.2 web fonts: the signature decides the format; documents,
    // scripts, stylesheets and images are never parsed as fonts.
    if (type==Husarz::ResourceType::Font && !Husarz::font_mime_allowed(mime)) {resource_field("error","FONT_MIME_REJECTED");return nullptr;}
    if(type==Husarz::ResourceType::Fetch&&(response.kod>=300&&response.kod<400))return nullptr;
    resource_field("MIME",mime);
    if (type==Husarz::ResourceType::Css && !resource_mime_is(mime,"text/css")) {resource_field("error","MIME_REJECTED");return nullptr;}
    if(type==Husarz::ResourceType::Css)resource_field("MIME-result","accepted");
    if(type==Husarz::ResourceType::Script && !resource_mime_is(mime,"text/javascript") && !resource_mime_is(mime,"application/javascript") && !resource_mime_is(mime,"application/x-javascript"))return nullptr;
    if (type==Husarz::ResourceType::Image && !resource_mime_is(mime,"image/png") && !resource_mime_is(mime,"image/jpeg") && !resource_mime_is(mime,"image/gif")) return nullptr;
    if (znajdz_header(siec_bufor,header_size,"content-length",content_length,sizeof(content_length))) {
        size_t expected=0;
        for (size_t i=0;content_length[i];++i) {
            if (content_length[i]<'0' || content_length[i]>'9' || expected>Husarz::MaxResourceBytes) return nullptr;
            expected=expected*10+unsigned(content_length[i]-'0');
        }
        if (!response.chunked && expected!=response.body_len) return nullptr;
    }
    // Until conditional revalidation exists, private/no-cache/no-store resources
    // are transient entries removed after their document releases them.
    bool transient=znajdz_header(siec_bufor,header_size,"cache-control",directive,sizeof(directive)) &&
        (tekst_zawiera_ci(directive,"no-store") || tekst_zawiera_ci(directive,"no-cache"));
    Husarz::ResourceEntry *entry=nullptr;
    if (response.chunked) {
        auto *body=static_cast<char*>(gui_malloc(Husarz::MaxResourceBytes+1));
        size_t size=0;
        if (body && dekoduj_chunked(response.body,response.body_len,body,type==Husarz::ResourceType::Fetch?Husarz::Js::BrowserRuntime::FetchBodyLimit+1:Husarz::MaxResourceBytes+1,&size))
            entry=cache->insert(address_data.original,type,reinterpret_cast<uint8_t*>(body),size);
        if (body) gui_free(body);
    } else entry=cache->insert(address_data.original,type,reinterpret_cast<const uint8_t*>(response.body),response.body_len);
    if (entry) {
        kopiuj_limit(entry->source_url,sizeof(entry->source_url),url);
        kopiuj_limit(entry->content_type,sizeof(entry->content_type),mime);
        if(!znajdz_header(siec_bufor,header_size,"access-control-allow-origin",entry->allow_origin,sizeof(entry->allow_origin)))entry->allow_origin[0]=0;
        znajdz_header(siec_bufor,header_size,"etag",entry->etag,sizeof(entry->etag));
        znajdz_header(siec_bufor,header_size,"last-modified",entry->modified,sizeof(entry->modified));
        entry->status=uint16_t(response.kod);entry->transient=transient||(type==Husarz::ResourceType::Fetch&&!http_kod_sukces(response.kod));
        const char *age=directive;
        for (size_t i=0;directive[i];++i) if (prefiks_ci(directive+i,"max-age=")) { age=directive+i+8; unsigned seconds=0,digits=0;
            while (*age>='0' && *age<='9' && digits++<10) { seconds=seconds>3600?3600:seconds*10+unsigned(*age++-'0'); }
            entry->cache_seconds=seconds>3600?3600:seconds;
            if (!entry->cache_seconds) entry->transient=true;
            break;
        }
        entry->created=gui_czas_monotoniczny_ns();
        entry->expires=entry->created?entry->created+uint64_t(entry->cache_seconds)*1000000000:0;
        if(entry->transient) entry->url[0]=0;
    }
    return entry;
}
void webfont_apply(const Husarz::ResourceTask &,const Husarz::WorkerPayload &);
void queue_fonts(Zakladka &);
#include "../husarz/worker.inc"
#include "../husarz/async_runtime.inc"
#include "../husarz/webfont_runtime.inc"
#include "../husarz/pipeline_runtime.inc"
#include "../husarz/script_runtime.inc"

__attribute__((noinline)) void przygotuj_dokument_zakladki(Zakladka& z,bool resources){
#ifndef HUSARZ_HOST_TEST
    (void)resources;if(!z.dokument || z.dokument_brudny)browser_request_work(z);return;
#endif
    if(z.dokument&&!z.dokument_brudny)return;
    auto* next=new Husarz::Document;
    if(!next){ustaw_status("Brak pamięci na dokument.");return;}
    const char* source=z.tresc&&*z.tresc?z.tresc:"Wpisz adres strony lub zapytanie i wybierz Przejdź.";
    renderuj_html(*next,source,z.to_jest_html);
    if(resources&&z.to_jest_html){
        struct ResourceScratch { ParsedUrl base{},css_base{}; char url[URL_POJEMNOSC]{}; };
        auto *scratch=new ResourceScratch;
        if (scratch && document_url_base(z.url,next,scratch->base)) {
            unsigned stylesheets=0;
            for (uint16_t i=1;i<next->node_count && stylesheets<Husarz::MaxStylesheets;++i) {
                if (!next->tag(i,"link") || !Husarz::token_ci(next->attr(i,"rel"),"stylesheet")) continue;
                if (!ustaw_url_z_location(scratch->url,sizeof(scratch->url),next->attr(i,"href"),scratch->base)) continue;
                ++stylesheets;
                auto *entry=fetch_browser_resource(scratch->url,Husarz::ResourceType::Css);
                if (entry) {
                    Husarz::parse_css(*next,reinterpret_cast<const char*>(entry->raw),entry->size,uint32_t(i)*4096,next->store(scratch->url,Husarz::length(scratch->url,URL_POJEMNOSC)));
                    if(entry->transient && resource_cache) resource_cache->remove(*entry);
                }
            }
            Husarz::compute_styles(*next);
            for (uint16_t i=1;i<next->node_count && next->image_count<Husarz::MaxDocumentImages;++i) {
                for (unsigned bg=0;bg<2;++bg) {
                    const char *src=bg ? next->str(next->styles[i].background_image) : next->tag(i,"img") ? next->attr(i,"src") : "";
                    char *url=scratch->url;
                    if (bg && *src) {
                        Husarz::Span value{src,Husarz::length(src,Husarz::MaxAttrLength)};
                        if (value.size<5) continue;
                        value=Husarz::trimmed(src+4,value.size-5);
                        if (value.size>=2 && (value.data[0]=='\'' || value.data[0]=='"') && value.data[value.size-1]==value.data[0]) {
                            ++value.data; value.size-=2;
                        }
                        if (!value.size || value.size>=sizeof(scratch->url)) continue;
                        for (size_t j=0;j<value.size;++j) url[j]=value.data[j];
                        url[value.size]=0;
                        src=url;
                    }
                    const ParsedUrl *base=&scratch->base;
                    if (bg && next->styles[i].background_base && parsuj_url(next->str(next->styles[i].background_base),&scratch->css_base)) base=&scratch->css_base;
                    if (!*src || !ustaw_url_z_location(url,sizeof(scratch->url),src,*base)) continue;
                    auto *entry=fetch_browser_resource(url,Husarz::ResourceType::Image);
                    if (entry && resource_cache && decode_browser_image(*resource_cache,*entry)) resource_cache->bind(*next,i,*entry,bg!=0);
                }
            }
        }
        delete scratch;
    }
    delete z.dokument;z.dokument=next;z.dokument_brudny=false;kopiuj_limit(z.published_url,sizeof(z.published_url),z.url);
    if(next->truncated)ustaw_status("Osiągnięto limit dokumentu; wyświetlono część strony.");
    // The caller lays out the published document after this stack frame returns.
    if(liczba_zakladek>0 && &z==&zakladki[aktywna_zakladka]) zresetuj_przewijanie();
}
void submit_browser_form(Husarz::Document &d,uint16_t control) {
    uint16_t form=Husarz::form_owner(d,control); if (!form) return;
    if(!browser_script_bypass&&browser_script_event(d,form,"submit",2,control))return;
    const char *method=d.attr(form,"method");
    if (*method && !Husarz::equal_ci(method,Husarz::length(method,16),"get")) { ustaw_status("Ta metoda formularza nie jest obsługiwana (GET)."); g_husarz.aktualizuj_pasek_stanu(); return; }
    struct FormScratch { ParsedUrl base{}; char url[URL_POJEMNOSC]{}, query[URL_POJEMNOSC]{}; };
    auto *scratch=new FormScratch; if (!scratch) return;
    const char *action=d.attr(form,"action"); if (!*action) action=zakladki[aktywna_zakladka].published_url[0]?zakladki[aktywna_zakladka].published_url:zakladki[aktywna_zakladka].url;
    bool ready=document_url_base(zakladki[aktywna_zakladka].url,&d,scratch->base) &&
        ustaw_url_z_location(scratch->url,sizeof(scratch->url),action,scratch->base) &&
        Husarz::form_query(d,form,control,scratch->query,sizeof(scratch->query));
    if (ready) {
        size_t n=0; while (scratch->url[n] && scratch->url[n]!='?' && scratch->url[n]!='#') ++n;
        size_t q=Husarz::length(scratch->query,sizeof(scratch->query));
        if (n+q+2<sizeof(scratch->url)) {
            scratch->url[n++]='?'; for (size_t i=0;i<=q;++i) scratch->url[n+i]=scratch->query[i];
            g_husarz.przejdz_do_url(scratch->url);
        } else ustaw_status("Formularz przekracza limit adresu URL.");
    }
    delete scratch;
}
void WidokStrony::obsluz_wcisniecie_myszy(ZdarzenieMyszki& e){
    Kontrolka::obsluz_wcisniecie_myszy(e);
    selection_node_=0;
    if(!e.czy_lewy_przycisk())return;
    if(liczba_zakladek<=0||!zakladki[aktywna_zakladka].dokument)return;
    auto& d=*zakladki[aktywna_zakladka].dokument;
    if(d.layout_revision!=d.revision||d.styles_dirty)return;
    auto screen=obszar_ekranu();
    int px=e.x-screen.x, py=e.y-screen.y+int(g_husarz.suwak_pionowy->wartosc());
    if (d.open_select) {
        auto *popup=new Husarz::SelectPopup(Husarz::select_popup(d,d.open_select,int(g_husarz.suwak_pionowy->wartosc()),obszar_.szerokosc,obszar_.wysokosc));
        int local_y=e.y-screen.y;
        uint16_t open=d.open_select; d.open_select=0;
        if(!popup)return;
        if (px>=popup->x && px<popup->x+popup->width && local_y>=popup->y && local_y<popup->y+popup->height) {
            if(auto *c=Husarz::control(d,open)){auto previous=c->option;Husarz::choose_option(d,*c,popup->start+unsigned((local_y-popup->y)/popup->row));if(c->option!=previous){browser_script_event(d,open,"input");browser_script_event(d,open,"change");}}
            delete popup; przygotuj(); e.obsluzone=true; return;
        }
        delete popup;
    }
    uint16_t clicked=0;
    for (uint16_t i=1;i<d.node_count;++i) {
        const auto &b=d.boxes[i]; const auto &s=d.styles[i];
        if ((Husarz::is_control(d,i) || d.tag(i,"label")) && s.visible && s.display!=Husarz::Display::None &&
            px>=b.x && py>=b.y && int64_t(px)<int64_t(b.x)+b.width && int64_t(py)<int64_t(b.y)+b.height) {
            auto clip=Husarz::ancestor_clip(d,i,0,obszar_.szerokosc,Husarz::MaxDimension);
            if (px>=clip.x && py>=clip.y && int64_t(px)<int64_t(clip.x)+clip.width && int64_t(py)<int64_t(clip.y)+clip.height) clicked=i;
        }
    }
    if (clicked && d.tag(clicked,"label")) {
        const char *target=d.attr(clicked,"for");
        for (unsigned i=0;i<d.control_count;++i) {
            uint16_t node=d.controls[i].node;
            if ((*target && Husarz::equal(d.attr(node,"id"),target)) || (!*target && d.nodes[node].parent==clicked)) { clicked=node; break; }
        }
    }
    if(!browser_script_bypass){
        uint16_t target=clicked;
        if(!target)for(uint16_t i=1;i<d.node_count;++i){const auto &b=d.boxes[i];if(d.styles[i].display!=Husarz::Display::None && !d.nodes[i].detached && px>=b.x&&py>=b.y&&int64_t(px)<int64_t(b.x)+b.width&&int64_t(py)<int64_t(b.y)+b.height)target=i;}
        if(target&&d.nodes[target].is_text)target=d.nodes[target].parent;
        if(target)browser_script_event(d,target,"mousedown");
        if(target && browser_script_event(d,target,"click",1,0,0,e.czy_ctrl(),e.czy_shift(),e.czy_alt(),px,py)){e.obsluzone=true;return;}
    }
    if (auto *c=Husarz::control(d,clicked)) {
        if (d.has_attr(clicked,"disabled")) return;
        d.focused_control=clicked;
        if(Husarz::selectable(d,*c)) {
            Husarz::control_pointer(d,*c,browser_metrics,px,py,e.czy_shift());
            selection_node_=clicked;selection_generation_=d.generation;selection_revision_=d.revision;selection_tab_=uint32_t(aktywna_zakladka);
        } else c->cursor=c->anchor=c->length;
        if(!d.tag(clicked,"select")) d.open_select=0;
        if (Husarz::input_type(d,clicked,"checkbox") || Husarz::input_type(d,clicked,"radio")) Husarz::form_toggle(d,*c);
        else if (d.tag(clicked,"select")) d.open_select=clicked;
        else if (Husarz::input_type(d,clicked,"submit") || (d.tag(clicked,"button") && !Husarz::input_type(d,clicked,"button"))) { submit_browser_form(d,clicked); return; }
        przygotuj(); e.obsluzone=true; return;
    }
    d.focused_control=0; uniewaznij();
    uint16_t link=Husarz::hit_test(d,e.x-screen.x,e.y-screen.y+int(g_husarz.suwak_pionowy->wartosc()));
    if(link){struct LinkScratch{ParsedUrl base{};char url[URL_POJEMNOSC]{};};auto* scratch=new LinkScratch;
        if(!scratch)return;
        if(document_url_base(zakladki[aktywna_zakladka].url,&d,scratch->base)&&ustaw_url_z_location(scratch->url,sizeof(scratch->url),d.attr(link,"href"),scratch->base))g_husarz.przejdz_do_url(scratch->url);
        delete scratch;
    }
}

class PasekNarzedziHusarza : public Panel {
public:
    PasekNarzedziHusarza() : Panel(Orientacja::Pozioma) {}
    Rozmiar zmierz(Rozmiar dostepny) override {
        const SkalaUI s=skala_dpi();
        int h=(CzcionkaUI::pobierz_wysokosc(s)*100+s.procent()-1)/s.procent()+6;
        if(h<26)h=26;
        if(preferowany_rozmiar().szerokosc!=-1 || preferowany_rozmiar().wysokosc!=h+6)
            ustaw_preferowany_rozmiar(Rozmiar(-1,h+6));
        for(size_t i=0;i<liczba_dzieci();++i){
            auto* c=dziecko(i);
            if(c->preferowany_rozmiar().wysokosc!=h)
                c->ustaw_preferowany_rozmiar(Rozmiar(c->preferowany_rozmiar().szerokosc,h));
        }
        if(liczba_dzieci()>=2){
            int width=6*s+int(liczba_dzieci()-1)*odstep()*s;
            for(size_t i=0;i<liczba_dzieci();++i){
                auto* c=dziecko(i);
                if(i<2){width+=CzcionkaUI::zmierz_tekst(i?"Dalej":"Wstecz",s)+20*s;continue;}
                width+=c->wyrownanie_poziome()==WyrownaniePoziome::Rozciagnij
                    ? c->minimalny_rozmiar().szerokosc*s : c->zmierz(dostepny).szerokosc;
            }
            dziecko(0)->ustaw_tekst(width>dostepny.szerokosc?"‹":"Wstecz");
            dziecko(1)->ustaw_tekst(width>dostepny.szerokosc?"›":"Dalej");
        }
        return Panel::zmierz(dostepny);
    }
};

[[maybe_unused]] void HusarzApp::zbuduj_ui() {
    if (!okno) return;

    PanelDokowany* korzen = new PanelDokowany();
    korzen->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
    korzen->ustaw_odstep(0);
    okno->ustaw_kontrolke_glowna(korzen);

    // 1. Menu Glowne
    MenuGlowne& m = korzen->dodaj<MenuGlowne>(Dokowanie::Gora);
    menu = &m;

    Menu& m_plik = m.menu().dodaj_podmenu("Plik", 100);
    m_plik.dodaj_element("Nowa karta (Ctrl+T)", 101, [](ElementMenu&, void*) { g_husarz.nowa_zakladka_gui(); });
    m_plik.dodaj_element("Zamknij kartę (Ctrl+W)", 102, [](ElementMenu&, void*) { g_husarz.zamknij_zakladke_gui(aktywna_zakladka); });
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zakończ (Alt+F4)", 109, [](ElementMenu&, void*) { if (g_husarz.okno) g_husarz.okno->zamknij(); });

    Menu& m_edycja = m.menu().dodaj_podmenu("Edycja", 200);
    m_edycja.dodaj_element("Zaznacz adres (Ctrl+L)", 201, [](ElementMenu&, void*) {
        if (g_husarz.txt_adres && g_husarz.okno) {
            g_husarz.okno->ustaw_fokus(g_husarz.txt_adres);
            g_husarz.txt_adres->zaznacz_wszystko();
        }
    });
    m_edycja.dodaj_element("Kopiuj adres", 202, [](ElementMenu&, void*) { g_husarz.kopiuj_adres_do_schowka(); });
    m_edycja.dodaj_element("Wklej adres ze schowka", 203, [](ElementMenu&, void*) { g_husarz.wklej_adres_ze_schowka(); });

    Menu& m_widok = m.menu().dodaj_podmenu("Widok", 300);
    m_widok.dodaj_element("Odśwież stronę (F5)", 301, [](ElementMenu&, void*) { g_husarz.odswiez(); });
    m_widok.dodaj_separator();
    m_widok.dodaj_element("Przewiń na górę (Home)", 302, [](ElementMenu&, void*) { g_husarz.przewin_do(0); });
    m_widok.dodaj_element("Przewiń na dół (End)", 303, [](ElementMenu&, void*) { g_husarz.przewin_do(maksimum_przewijania()); });

    Menu& m_nawigacja = m.menu().dodaj_podmenu("Nawigacja", 400);
    m_nawigacja.dodaj_element("Wstecz (Alt+Lewo)", 401, [](ElementMenu&, void*) { g_husarz.wstecz(); });
    m_nawigacja.dodaj_element("Dalej (Alt+Prawo)", 402, [](ElementMenu&, void*) { g_husarz.dalej(); });
    m_nawigacja.dodaj_element("Strona domowa (Alt+Home)", 403, [](ElementMenu&, void*) { g_husarz.dom(); });
    m_nawigacja.dodaj_separator();
    m_nawigacja.dodaj_element("Przejdź do adresu (Enter)", 404, [](ElementMenu&, void*) {
        if (g_husarz.txt_adres) g_husarz.przejdz_do_url(g_husarz.txt_adres->tekst());
    });

    Menu& m_zakladki = m.menu().dodaj_podmenu("Zakładki", 500);
    m_zakladki.dodaj_element("Dodaj do zakładek (Ctrl+D)", 501, [](ElementMenu&, void*) { g_husarz.dodaj_obecna_do_ulubionych_akcja(); });
    m_zakladki.dodaj_separator();
    m_zakladki.dodaj_element("Bursztyn OS (http://example.com/)", 502, [](ElementMenu&, void*) { g_husarz.przejdz_do_url("http://example.com/"); });
    m_zakladki.dodaj_element("Wikipedia (http://wikipedia.org/)", 503, [](ElementMenu&, void*) { g_husarz.przejdz_do_url("http://wikipedia.org/"); });

    Menu& m_pomoc = m.menu().dodaj_podmenu("Pomoc", 600);
    m_pomoc.dodaj_element("O programie Husarz...", 601, [](ElementMenu&, void*) { g_husarz.pokaz_o_programie(); });

    // 2. Pasek Kart
    Panel& p_kart = korzen->dodaj<Panel>(Dokowanie::Gora, Orientacja::Pozioma);
    pasek_kart = &p_kart;
    p_kart.ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
    p_kart.ustaw_wypelnienie(Marginesy(2, 2, 2, 2));
    p_kart.ustaw_odstep(2);

    for (int i = 0; i < MAX_ZAKLADKI; ++i) {
        Przycisk& b_k = p_kart.dodaj<Przycisk>("Karta");
        btn_karty[i] = &b_k;
        b_k.ustaw_preferowany_rozmiar(Rozmiar(120, 24));
        b_k.przy_kliknieciu([](Przycisk*, void* d) {
            int idx = static_cast<int>(reinterpret_cast<intptr_t>(d));
            g_husarz.przelacz_zakladke(idx);
        }, reinterpret_cast<void*>(static_cast<intptr_t>(i)));

        Przycisk& b_z = p_kart.dodaj<Przycisk>("x");
        btn_zamknij_karty[i] = &b_z;
        b_z.ustaw_preferowany_rozmiar(Rozmiar(20, 24));
        b_z.przy_kliknieciu([](Przycisk*, void* d) {
            int idx = static_cast<int>(reinterpret_cast<intptr_t>(d));
            g_husarz.zamknij_zakladke_gui(idx);
        }, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }

    Przycisk& b_nowa = p_kart.dodaj<Przycisk>("+");
    btn_nowa_karta = &b_nowa;
    b_nowa.ustaw_preferowany_rozmiar(Rozmiar(24, 24));
    b_nowa.przy_kliknieciu([](Przycisk*, void*) { g_husarz.nowa_zakladka_gui(); });

    // 3. Pasek Narzedzi
    Panel& p_narz = korzen->dodaj<PasekNarzedziHusarza>(Dokowanie::Gora);
    pasek_narzedzi = &p_narz;
    p_narz.ustaw_preferowany_rozmiar(Rozmiar(-1, 32));
    p_narz.ustaw_wypelnienie(Marginesy(3, 3, 3, 3));
    p_narz.ustaw_odstep(4);

    Przycisk& b_wst = p_narz.dodaj<Przycisk>("Wstecz");
    btn_wstecz = &b_wst;
    b_wst.ustaw_podpowiedz("Wstecz (Alt+Lewo)");
    b_wst.ustaw_wypelnienie(Marginesy(0));
    b_wst.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    b_wst.przy_kliknieciu([](Przycisk*, void*) { g_husarz.wstecz(); });

    Przycisk& b_dal = p_narz.dodaj<Przycisk>("Dalej");
    btn_dalej = &b_dal;
    b_dal.ustaw_podpowiedz("Dalej (Alt+Prawo)");
    b_dal.ustaw_wypelnienie(Marginesy(0));
    b_dal.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    b_dal.przy_kliknieciu([](Przycisk*, void*) { g_husarz.dalej(); });

    Przycisk& b_ods = p_narz.dodaj<Przycisk>("Odśwież");
    btn_odswiez = &b_ods;
    b_ods.ustaw_wypelnienie(Marginesy(0));
    b_ods.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    b_ods.przy_kliknieciu([](Przycisk*, void*) { g_husarz.odswiez(); });

    Przycisk& b_zat = p_narz.dodaj<Przycisk>("Zatrzymaj");
    btn_zatrzymaj = &b_zat;
    b_zat.ustaw_wypelnienie(Marginesy(0));
    b_zat.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    b_zat.przy_kliknieciu([](Przycisk*, void*) { g_husarz.zatrzymaj(); });

    Przycisk& b_dom = p_narz.dodaj<Przycisk>("Dom");
    btn_dom = &b_dom;
    b_dom.ustaw_wypelnienie(Marginesy(0));
    b_dom.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    b_dom.przy_kliknieciu([](Przycisk*, void*) { g_husarz.dom(); });

    PoleTekstowe& t_adr = p_narz.dodaj<PoleTekstowe>("http://example.com/");
    txt_adres = &t_adr;
    t_adr.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    t_adr.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    t_adr.ustaw_minimalny_rozmiar(Rozmiar(120, 26));
    t_adr.ustaw_tekst_zastepczy("Wpisz adres URL...");
    t_adr.przy_zatwierdzeniu([](PoleTekstowe*, const char* t, void*) { g_husarz.przejdz_do_url(t); });

    Przycisk& b_idz = p_narz.dodaj<Przycisk>("Przejdź");
    btn_idz = &b_idz;
    b_idz.ustaw_wypelnienie(Marginesy(0));
    b_idz.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    b_idz.przy_kliknieciu([](Przycisk*, void*) {
        if (g_husarz.txt_adres) g_husarz.przejdz_do_url(g_husarz.txt_adres->tekst());
    });

    Przycisk& b_zak = p_narz.dodaj<Przycisk>("Zakładki");
    btn_zakladki = &b_zak;
    b_zak.ustaw_wypelnienie(Marginesy(0));
    b_zak.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    b_zak.przy_kliknieciu([](Przycisk*, void*) { g_husarz.pokaz_zakladki(); });

    // Chrome belongs exclusively to the client rectangle, never to the DOM.
    PasekStanu& sb = korzen->dodaj<PasekStanu>(Dokowanie::Dol);
    pasek_stanu = &sb;
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1,24));
    sb.dodaj_sekcje("Gotowy",200,RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("HTTP",70,RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Karta 1/1",90,RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("UTF-8",60,RozmiarSekcjiStanu::Stala);
    auto& content=korzen->dodaj<PanelDokowany>(Dokowanie::Wypelnij);
    content.ustaw_wypelnienie(Marginesy(0));content.ustaw_odstep(0);
    suwak_pionowy=&content.dodaj<SuwakStrony>(Dokowanie::Prawo);
    suwak_pionowy->ustaw_orientacje(Orientacja::Pionowa);
    suwak_pionowy->ustaw_preferowany_rozmiar(Rozmiar(18,-1));
    suwak_pionowy->przy_przewijaniu([](PasekPrzewijania*,int64_t,void*) {
        if(g_husarz.widok_strony)g_husarz.widok_strony->uniewaznij();
    });
    widok_strony=&content.dodaj<WidokStrony>(Dokowanie::Wypelnij);

    // 6. Menu Kontekstowe dla Widoku Strony
    menu_kontekstowe = new MenuKontekstowe();
    menu_kontekstowe->menu().dodaj_element("Wstecz", 1, [](ElementMenu&, void*) { g_husarz.wstecz(); });
    menu_kontekstowe->menu().dodaj_element("Dalej", 2, [](ElementMenu&, void*) { g_husarz.dalej(); });
    menu_kontekstowe->menu().dodaj_element("Odśwież", 3, [](ElementMenu&, void*) { g_husarz.odswiez(); });
    menu_kontekstowe->menu().dodaj_separator();
    menu_kontekstowe->menu().dodaj_element("Kopiuj adres", 4, [](ElementMenu&, void*) { g_husarz.kopiuj_adres_do_schowka(); });
    menu_kontekstowe->menu().dodaj_element("Dodaj do zakładek", 5, [](ElementMenu&, void*) { g_husarz.dodaj_obecna_do_ulubionych_akcja(); });
    widok_strony->ustaw_menu_kontekstowe(menu_kontekstowe);

    menu_zakladki_popup = new MenuKontekstowe();
    okno->przelicz_uklad();
}

void HusarzApp::aktualizuj_pasek_kart() {
    for (int i = 0; i < MAX_ZAKLADKI; ++i) {
        if (!btn_karty[i] || !btn_zamknij_karty[i]) continue;
        if (i < liczba_zakladek) {
            char tytul[32] = {};
            if (zakladki[i].url[0] != '\0') {
                const char* u = zakladki[i].url;
                if (u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p') {
                    if (u[4] == ':') u += 7;
                    else if (u[4] == 's' && u[5] == ':') u += 8;
                }
                size_t l = 0;
                if (i == aktywna_zakladka) {
                    tytul[l++] = '>';
                    tytul[l++] = ' ';
                }
                while (u[0] && u[0] != '/' && l < sizeof(tytul) - 1) {
                    tytul[l++] = *u++;
                }
                tytul[l] = '\0';
            } else {
                tytul[0] = 'K'; tytul[1] = 'a'; tytul[2] = 'r'; tytul[3] = 't'; tytul[4] = 'a';
                tytul[5] = ' '; tytul[6] = static_cast<char>('1' + i); tytul[7] = '\0';
            }
            btn_karty[i]->ustaw_tekst(tytul);
            btn_karty[i]->ustaw_widoczna(true);
            btn_zamknij_karty[i]->ustaw_widoczna(liczba_zakladek > 1);
        } else {
            btn_karty[i]->ustaw_widoczna(false);
            btn_zamknij_karty[i]->ustaw_widoczna(false);
        }
    }
    if (btn_nowa_karta) {
        btn_nowa_karta->ustaw_widoczna(liczba_zakladek < MAX_ZAKLADKI);
    }
    if (okno) {
        okno->przelicz_uklad();
        okno->uniewaznij();
    }
}

void HusarzApp::aktualizuj_pasek_stanu() {
    if (!pasek_stanu) return;
    pasek_stanu->ustaw_tekst(0, status_bufor[0] != '\0' ? status_bufor : "Gotowy");
    if (liczba_zakladek > 0) {
        bool https = (zakladki[aktywna_zakladka].url[0] == 'h' &&
                      zakladki[aktywna_zakladka].url[1] == 't' &&
                      zakladki[aktywna_zakladka].url[2] == 't' &&
                      zakladki[aktywna_zakladka].url[3] == 'p' &&
                      zakladki[aktywna_zakladka].url[4] == 's');
        pasek_stanu->ustaw_tekst(1, https ? "HTTPS" : "HTTP");

        char kbuf[32] = {};
        kbuf[0] = 'K'; kbuf[1] = 'a'; kbuf[2] = 'r'; kbuf[3] = 't'; kbuf[4] = 'a'; kbuf[5] = ' ';
        kbuf[6] = static_cast<char>('1' + aktywna_zakladka);
        kbuf[7] = '/';
        kbuf[8] = static_cast<char>('0' + liczba_zakladek);
        kbuf[9] = '\0';
        pasek_stanu->ustaw_tekst(2, kbuf);
    }
    pasek_stanu->ustaw_tekst(3, "UTF-8");
}

void HusarzApp::przelacz_zakladke(int idx) {
    if(nawigacja_trwa)return;
    if (idx < 0 || idx >= liczba_zakladek) return;
    aktywna_zakladka = idx;
    zresetuj_przewijanie();
    if (txt_adres) {
        txt_adres->ustaw_tekst(zakladki[idx].url);
    }
    ustaw_status("Przełączono kartę.");
    aktualizuj_pasek_kart();
    aktualizuj_pasek_stanu();
    if (widok_strony) {
        widok_strony->przygotuj();
    }
}

void HusarzApp::nowa_zakladka_gui() {
    if(nawigacja_trwa)return;
    if (nowa_zakladka()) {
        (void)kopiuj_limit(zakladki[aktywna_zakladka].url, sizeof(zakladki[aktywna_zakladka].url), "http://example.com/");
        if (txt_adres) {
            txt_adres->ustaw_tekst(zakladki[aktywna_zakladka].url);
        }
        aktualizuj_pasek_kart();
        aktualizuj_pasek_stanu();
        if (widok_strony) {
            widok_strony->przygotuj();
        }
    }
}

void HusarzApp::zamknij_zakladke_gui(int idx) {
    if(nawigacja_trwa)return;
    if(idx>=0 && idx<liczba_zakladek){cancel_async_tab(zakladki[idx]);Husarz::browser_storage.close(zakladki[idx].identity);}
    zamknij_zakladke(idx);
    if (txt_adres && liczba_zakladek > 0) {
        txt_adres->ustaw_tekst(zakladki[aktywna_zakladka].url);
    }
    aktualizuj_pasek_kart();
    aktualizuj_pasek_stanu();
    if (widok_strony) {
        widok_strony->przygotuj();
    }
}

void HusarzApp::przejdz_do_url(const char* url) {
    if(nawigacja_trwa)return;
    if (liczba_zakladek <= 0) return;
    if (!url || url[0] == '\0') {
        ustaw_status("Wpisz adres URL.");
        aktualizuj_pasek_stanu();
        return;
    }
    kopiuj_limit(zakladki[aktywna_zakladka].url, sizeof(zakladki[aktywna_zakladka].url), url);
    Nawiguj(true, true);
    if (txt_adres) {
        txt_adres->ustaw_tekst(zakladki[aktywna_zakladka].url);
    }
    aktualizuj_pasek_kart();
    aktualizuj_pasek_stanu();
    if (widok_strony) {
        widok_strony->przygotuj();
    }
}

void HusarzApp::wstecz() {
    if(nawigacja_trwa)return;
    if (historia_idx > 0) {
        --historia_idx;
        kopiuj_limit(zakladki[aktywna_zakladka].url, URL_POJEMNOSC, historia[historia_idx]);
        Nawiguj(false, false);
        if (txt_adres) txt_adres->ustaw_tekst(zakladki[aktywna_zakladka].url);
        aktualizuj_pasek_kart();
        aktualizuj_pasek_stanu();
        if (widok_strony) widok_strony->przygotuj();
    }
}

void HusarzApp::dalej() {
    if(nawigacja_trwa)return;
    if (historia_idx >= 0 && historia_idx < historia_max) {
        ++historia_idx;
        kopiuj_limit(zakladki[aktywna_zakladka].url, URL_POJEMNOSC, historia[historia_idx]);
        Nawiguj(false, false);
        if (txt_adres) txt_adres->ustaw_tekst(zakladki[aktywna_zakladka].url);
        aktualizuj_pasek_kart();
        aktualizuj_pasek_stanu();
        if (widok_strony) widok_strony->przygotuj();
    }
}

void HusarzApp::odswiez() {
    if(nawigacja_trwa)return;
    Nawiguj(false, false);
    aktualizuj_pasek_stanu();
    if (widok_strony) widok_strony->przygotuj();
}

void HusarzApp::zatrzymaj() {
    if(liczba_zakladek>0) {
        auto &z=zakladki[aktywna_zakladka];
        bool interrupted_script=z.javascript&&z.javascript->task.handle;
        cancel_async_tab(z);
        // A canceled script can leave committed mutations awaiting geometry.
        // Finish their cooperative publication without restarting scripts/jobs.
        if(z.dokument){
            if(interrupted_script)z.dokument->invalidate(0,8);
            if(z.dokument->styles_dirty||z.dokument->layout_revision!=z.dokument->revision||z.dokument->layout_width!=browser_viewport.width)browser_request_work(z);
        }
    }
    ustaw_status("Zatrzymano wczytywanie.");
    aktualizuj_pasek_stanu();
}

void HusarzApp::dom() {
    przejdz_do_url("http://example.com/");
}

void HusarzApp::dodaj_obecna_do_ulubionych_akcja() {
    dodaj_obecna_do_ulubionych();
    zapisz_ulubione();
    aktualizuj_pasek_stanu();
}

void HusarzApp::kopiuj_adres_do_schowka() {
    if (liczba_zakladek <= 0) return;
    const char* url = zakladki[aktywna_zakladka].url;
    size_t len = dlugosc_limit(url, URL_POJEMNOSC);
    if (len > 0) {
        clipboard_set("text/plain", url, len + 1);
        ustaw_status("Skopiowano adres do schowka.");
        aktualizuj_pasek_stanu();
    }
}

void HusarzApp::wklej_adres_ze_schowka() {
    auto* bufor=static_cast<char*>(gui_malloc(URL_POJEMNOSC));if(!bufor)return;
    uint64_t len=0;
    if(clipboard_read("text/plain",bufor,URL_POJEMNOSC-1,&len)&&len>0&&len<URL_POJEMNOSC){bufor[len]=0;przejdz_do_url(bufor);}
    gui_free(bufor);
}

void HusarzApp::przewin_do(int y){if(suwak_pionowy)suwak_pionowy->ustaw_wartosc(y);}


void HusarzApp::pokaz_o_programie() {
    if (okno) {
        Komunikat::informacja(okno->zarzadca_popupow(), "O programie Husarz",
            "Husarz - Nowoczesna Przeglądarka WWW\n"
            "Wersja 2.0 (BursztynUI Native)\n"
            "Bursztyn OS - Ring 3 GUI\n"
            "Obsługiwane protokoły: HTTP/1.1, HTTPS (TLS), DNS");
    }
}

void HusarzApp::pokaz_zakladki() {
    if (!okno || !btn_zakladki || !menu_zakladki_popup) return;
    menu_zakladki_popup->menu().wyczysc();
    menu_zakladki_popup->menu().dodaj_element("+ Dodaj bieżącą stronę", 10, [](ElementMenu&, void*) {
        g_husarz.dodaj_obecna_do_ulubionych_akcja();
    });
    if (ulubione_ilosc > 0) {
        menu_zakladki_popup->menu().dodaj_separator();
        for (int i = 0; i < ulubione_ilosc; ++i) {
            menu_zakladki_popup->menu().dodaj_element(ulubione[i], 100 + i, [](ElementMenu& e, void*) {
                g_husarz.przejdz_do_url(e.tekst());
            });
        }
    }
    menu_zakladki_popup->pokaz_pod_kontrolka(okno->zarzadca_popupow(), *btn_zakladki);
}

[[maybe_unused]] void inicjalizuj_stan() {
    liczba_zakladek = 0;
    aktywna_zakladka = 0;
    historia_idx = -1;
    historia_max = -1;
    ulubione_ilosc = 0;
    zresetuj_przewijanie();

    ustaw_status("Gotowy.");

    if (!nowa_zakladka()) {
        gui_zakoncz_aplikacje();
    }

    (void)kopiuj_limit(
        zakladki[0].url,
        sizeof(zakladki[0].url),
        "http://example.com/"
    );

    wczytaj_ulubione();
}

} // namespace

/* =========================================================================
 * 21. PUNKT WEJSCIA RING 3
 * ========================================================================= */

#ifndef HUSARZ_HOST_TEST
extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}

extern "C" [[noreturn]] void _start() {
#if defined(BSF_NATIVE) && !defined(HUSARZ_WORKER_ONLY)
    (void)BSF::enumerate();browser_font_generation=BSF::generation();Husarz::font_family_resolver=browser_family_resolver;
#endif

#if HUSARZ_DEBUG_RESOURCES
    Husarz::document_limit_log=wypisz;
#endif
#ifdef HUSARZ_WORKER_ONLY
    browser_worker_main();
#else
#if HUSARZ_PERF
    Husarz::perf_clock=gui_czas_monotoniczny_ns;
    Husarz::perf_log=[](const char *s){wypisz(s);};
#endif
    AplikacjaGUI app;
    g_husarz.app = &app;

    auto* okno=new OknoHusarz("Husarz - Przeglądarka WWW", DOMYSLNY_WIN_W, DOMYSLNY_WIN_H);
    if(!okno)bws_zakoncz_proces();
    g_husarz.okno = okno;
    app.ustaw_glowne_okno(okno);

    g_husarz.zbuduj_ui();

    inicjalizuj_stan();
    g_husarz.widok_strony->przygotuj();
    otworz_argument_startowy();
    app.uruchom(*okno);

    browser_flush_storage();
    zwolnij_wszystkie_zakladki();
    delete okno;
    delete resource_cache; resource_cache=nullptr;
    bws_zakoncz_proces();
#endif
}

#endif
