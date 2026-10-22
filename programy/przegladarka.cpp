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

#include "../bursztyn_gui.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "http_kody.h"
#include "husarz/pipeline.h"
#include "husarz/work_clock.h"
#include "husarz/resource.h"
#include "husarz/async.h"
#include "../obrazy/dekoder_obrazu.h"
#include "husarz/script.h"

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
 *   TEXT    off 0x1000  VA 0x1001000  region 0x60000
 *   DATA    off 0x61000 VA 0x1061000  memory 0x80000
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

__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},

    reinterpret_cast<uint64_t>(
        &_start
    ),

    0x1000ULL,
    0x60000ULL,
    0x1001000ULL,

    0x61000ULL,
    0x80000ULL,
    0x1061000ULL
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
    struct Item{Husarz::ResourceEntry *entry{};uint16_t node{};} items[8]{};
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
    bool dokument_brudny;
    bool work_pending{};uint32_t resource_revision{};uint64_t navigation_started{};bool navigation_measured{};PendingCss *pending_css{};

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

    if(zwolnij_tresc){browser_cancel_work(z.identity);delete z.pending_css;z.pending_css=nullptr;}
    if (zwolnij_tresc) delete z.dokument;
    z.identity=z.navigation=0;
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

    browser_cancel_work(zakladki[indeks].identity);delete zakladki[indeks].pending_css;zakladki[indeks].pending_css=nullptr;
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
    zakladki[liczba_zakladek].dokument = nullptr;
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

    // A colon before any path/query separator denotes an unsupported scheme.
    // In particular, web links/forms cannot turn file:, data: or javascript:
    // into native operations or accidental relative HTTP requests.
    for(size_t i=0;location[i] && location[i]!='/' && location[i]!='?' && location[i]!='#';++i)
        if(location[i]==':') return false;

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

    if (location[0]=='?' || location[0]=='#') {
        for(size_t i=0;obecny.sciezka[i] && obecny.sciezka[i]!='#' &&
            (location[0]!='?' || obecny.sciezka[i]!='?');++i) {
            if(p+1>=sizeof(wynik))return false;
            wynik[p++]=obecny.sciezka[i];
        }
        wynik[p]=0;
        return append(location) && kopiuj_limit(cel,pojemnosc,wynik);
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
const Husarz::Metrics browser_metrics{nullptr,advance_font,height_font};
const Husarz::Glyphs browser_glyphs{nullptr,bitmap_font,width_font};

void przewin_kolem(PasekPrzewijania& bar,ZdarzenieRolki& e){
    if(!e.delta_y)return;
    int64_t units=e.delta_y;if(units>=120||units<=-120)units/=120;
    bar.przewin_o(-units*bar.maly_krok());e.obsluzone=true;
}
class SuwakStrony : public PasekPrzewijania {
public:void obsluz_rolke(ZdarzenieRolki& e)override{przewin_kolem(*this,e);}
};
void submit_browser_form(Husarz::Document&,uint16_t);
Husarz::ViewportRequest browser_viewport{};
void browser_request_work(Zakladka&);
bool browser_has_work();
bool browser_active_busy();
void browser_document_painted();
void browser_step_work();
void append_decimal(char *out,size_t &position,uint64_t value);
class WidokStrony : public Kontrolka {
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
        if(!e.delta_y||!g_husarz.suwak_pionowy)return;
        przewin_kolem(*g_husarz.suwak_pionowy,e);
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e)override{
        auto* bar=g_husarz.suwak_pionowy;if(!bar)return;
        auto *d=liczba_zakladek>0?zakladki[aktywna_zakladka].dokument:nullptr;
        unsigned key=e.polecenie();
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
            d->open_select=0;
            int current=-1;
            for (unsigned i=0;i<d->control_count;++i) if (d->controls[i].node==d->focused_control) current=int(i);
            for (unsigned step=0;step<d->control_count;++step) {
                current=(current+(e.czy_shift()?-1:1)+int(d->control_count))%int(d->control_count);
                uint16_t node=d->controls[current].node;
                if (!d->has_attr(node,"disabled") && d->styles[node].display!=Husarz::Display::None) { d->focused_control=node; break; }
            }
            uniewaznij(); e.obsluzone=true; return;
        }
        if (d && d->focused_control) {
            auto *c=Husarz::control(*d,d->focused_control);
            if (c && Husarz::form_key(*d,key)) { przygotuj(); e.obsluzone=true; return; }
            if (c && d->tag(c->node,"select") && (key==0x48 || key==0x50 || key==32 || key==13)) {
                if (key==13 && d->open_select) d->open_select=0;
                else if (key==32 || key==13) d->open_select=c->node;
                else Husarz::form_select(*d,*c,key==0x48?-1:1);
                przygotuj(); e.obsluzone=true; return;
            }
            if (c && (key==32 || key==13) && (Husarz::input_type(*d,c->node,"checkbox") || Husarz::input_type(*d,c->node,"radio"))) {
                Husarz::form_toggle(*d,*c); przygotuj(); e.obsluzone=true; return;
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
    void obsluz_wprowadzenie_tekstu(ZdarzenieWprowadzaniaTekstu& e) override {
        auto *d=liczba_zakladek>0?zakladki[aktywna_zakladka].dokument:nullptr;
        if (d && Husarz::form_insert(*d,e.punkt_kodowy)) { przygotuj(); e.obsluzone=true; }
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e)override;
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
                bool ctrl = (ev.przyciski & BWS_MOD_CTRL) != 0;
                bool alt = (ev.przyciski & BWS_MOD_ALT) != 0;

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
                format==FormatObrazu::JPEG ? sprawdz_jpeg(entry.raw,entry.size,&w,&h) : BladObrazu::Format;
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
bool resource_mime_is(const char *value,const char *expected) {
    size_t n=0;while(value[n] && value[n]!=';' && !Husarz::space(value[n]))++n;
    return Husarz::equal_ci(value,n,expected);
}
// HTTP responses must use the binary-safe count, never strlen on image data.
Husarz::ResourceEntry *fetch_browser_resource(const char *url,Husarz::ResourceType type) {
    struct Address { char normalized[URL_POJEMNOSC]{}; };
    struct AddressOwner { Address *value{new Address}; ~AddressOwner(){delete value;} } address;
    if (!address.value || !Husarz::normalize_resource_url(url,address.value->normalized,sizeof(address.value->normalized))) return nullptr;
    url=address.value->normalized;
    auto *cache=cache_resources(); if (!cache) return nullptr;
    if (auto *hit=cache->find(url,type,gui_czas_monotoniczny_ns())) return hit;
    auto *parsed=new ParsedUrl{}; if (!parsed) return nullptr;
    if (!parsuj_url(url,parsed)) { delete parsed; return nullptr; }
    uint8_t ip[4]{};
    if (!adres_ipv4_literal(parsed->domena,ip) && !bws_siec_dns(parsed->domena,ip)) { delete parsed; return nullptr; }
    uint32_t count=bws_siec_pobierz_bajty(parsed->https,ip,parsed->domena,parsed->sciezka,siec_bufor,sizeof(siec_bufor));
    delete parsed;
    OdpowiedzHttp response{};
    if (!count || !parsuj_odpowiedz_http(siec_bufor,count,&response) || !http_kod_sukces(response.kod)) return nullptr;
    size_t header_size=size_t(response.body-siec_bufor);
    char mime[96]{}, directive[128]{}, content_length[32]{};
    if (!znajdz_header(siec_bufor,header_size,"content-type",mime,sizeof(mime))) return nullptr;
    if (type==Husarz::ResourceType::Css && !resource_mime_is(mime,"text/css")) return nullptr;
    if (type==Husarz::ResourceType::Image && !resource_mime_is(mime,"image/png") && !resource_mime_is(mime,"image/jpeg")) return nullptr;
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
        if (body && dekoduj_chunked(response.body,response.body_len,body,Husarz::MaxResourceBytes+1,&size))
            entry=cache->insert(url,type,reinterpret_cast<uint8_t*>(body),size);
        if (body) gui_free(body);
    } else entry=cache->insert(url,type,reinterpret_cast<const uint8_t*>(response.body),response.body_len);
    if (entry) {
        kopiuj_limit(entry->content_type,sizeof(entry->content_type),mime);
        znajdz_header(siec_bufor,header_size,"etag",entry->etag,sizeof(entry->etag));
        znajdz_header(siec_bufor,header_size,"last-modified",entry->modified,sizeof(entry->modified));
        entry->transient=transient;
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
#include "husarz/worker.inc"
#include "husarz/async_runtime.inc"
#include "husarz/pipeline_runtime.inc"

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
            for (uint16_t i=1;i<next->node_count && stylesheets<8;++i) {
                if (!next->tag(i,"link") || !Husarz::equal_ci(next->attr(i,"rel"),Husarz::length(next->attr(i,"rel"),64),"stylesheet")) continue;
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
    if(liczba_zakladek<=0||!zakladki[aktywna_zakladka].dokument)return;
    auto& d=*zakladki[aktywna_zakladka].dokument;
    auto screen=obszar_ekranu();
    int px=e.x-screen.x, py=e.y-screen.y+int(g_husarz.suwak_pionowy->wartosc());
    if (d.open_select) {
        auto *popup=new Husarz::SelectPopup(Husarz::select_popup(d,d.open_select,int(g_husarz.suwak_pionowy->wartosc()),obszar_.szerokosc,obszar_.wysokosc));
        int local_y=e.y-screen.y;
        uint16_t open=d.open_select; d.open_select=0;
        if(!popup)return;
        if (px>=popup->x && px<popup->x+popup->width && local_y>=popup->y && local_y<popup->y+popup->height) {
            if(auto *c=Husarz::control(d,open)) Husarz::choose_option(d,*c,popup->start+unsigned((local_y-popup->y)/popup->row));
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
    if (auto *c=Husarz::control(d,clicked)) {
        if (d.has_attr(clicked,"disabled")) return;
        d.focused_control=clicked; c->cursor=c->length;
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
    m_plik.dodaj_element("Zakończ (Alt+F4)", 109, [](ElementMenu&, void*) { if (g_husarz.app) g_husarz.app->zakoncz(); });

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
    Panel& p_narz = korzen->dodaj<Panel>(Dokowanie::Gora, Orientacja::Pozioma);
    pasek_narzedzi = &p_narz;
    p_narz.ustaw_preferowany_rozmiar(Rozmiar(-1, 32));
    p_narz.ustaw_wypelnienie(Marginesy(3, 3, 3, 3));
    p_narz.ustaw_odstep(4);

    Przycisk& b_wst = p_narz.dodaj<Przycisk>("Wstecz");
    btn_wstecz = &b_wst;
    b_wst.ustaw_preferowany_rozmiar(Rozmiar(56, 26));
    b_wst.przy_kliknieciu([](Przycisk*, void*) { g_husarz.wstecz(); });

    Przycisk& b_dal = p_narz.dodaj<Przycisk>("Dalej");
    btn_dalej = &b_dal;
    b_dal.ustaw_preferowany_rozmiar(Rozmiar(52, 26));
    b_dal.przy_kliknieciu([](Przycisk*, void*) { g_husarz.dalej(); });

    Przycisk& b_ods = p_narz.dodaj<Przycisk>("Odśwież");
    btn_odswiez = &b_ods;
    b_ods.ustaw_preferowany_rozmiar(Rozmiar(64, 26));
    b_ods.przy_kliknieciu([](Przycisk*, void*) { g_husarz.odswiez(); });

    Przycisk& b_zat = p_narz.dodaj<Przycisk>("Zatrzymaj");
    btn_zatrzymaj = &b_zat;
    b_zat.ustaw_preferowany_rozmiar(Rozmiar(70, 26));
    b_zat.przy_kliknieciu([](Przycisk*, void*) { g_husarz.zatrzymaj(); });

    Przycisk& b_dom = p_narz.dodaj<Przycisk>("Dom");
    btn_dom = &b_dom;
    b_dom.ustaw_preferowany_rozmiar(Rozmiar(48, 26));
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
    b_idz.ustaw_preferowany_rozmiar(Rozmiar(62, 26));
    b_idz.przy_kliknieciu([](Przycisk*, void*) {
        if (g_husarz.txt_adres) g_husarz.przejdz_do_url(g_husarz.txt_adres->tekst());
    });

    Przycisk& b_zak = p_narz.dodaj<Przycisk>("Zakładki");
    btn_zakladki = &b_zak;
    b_zak.ustaw_preferowany_rozmiar(Rozmiar(72, 26));
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
    if(idx>=0 && idx<liczba_zakladek) cancel_async_tab(zakladki[idx]);
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
    if(liczba_zakladek>0) cancel_async_tab(zakladki[aktywna_zakladka]);
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

    zwolnij_wszystkie_zakladki();
    delete okno;
    delete resource_cache; resource_cache=nullptr;
    bws_zakoncz_proces();
#endif
}

#endif
