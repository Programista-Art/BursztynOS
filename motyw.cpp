/*
 * Bursztyn OS - Podsystem Motywów
 *
 * Implementacja centralnego silnika motywów:
 * - struktura BursztynMotyw (ABI 144 B),
 * - parsowanie deklaratywne fail-closed,
 * - weryfikacja kontrastu i granic,
 * - wbudowany fallback,
 * - zarzadzanie transakcja podgladu (15s rollback) i obsluga BWS.
 */

#ifndef BURSZTYN_HOST_TEST
#include "pzb.h"
#include "skladacz_obrazu.h"
#include "psf.h"
#include "scheduler.h"
#include "bws_api.h"
#include "bws_zdarzenia.h"
#include "sterowniki/czas/hpet.h"

extern proces_t tablica_procesow[MAKS_PROCESOW];
extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* ptr, size_t rozmiar);
extern bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void* ptr, size_t rozmiar);
extern bool skopiuj_do_przestrzeni_uzytkownika(void* cel_uzytkownik, const void* zrodlo_jadro, size_t rozmiar);
extern bool skopiuj_z_przestrzeni_uzytkownika(void* cel_jadro, const void* zrodlo_uzytkownik, size_t rozmiar);
extern uint64_t pobierz_aktualny_czas_ms(void);
#endif

#include "motyw.h"

#ifdef __cplusplus
extern "C" {
#endif

static const char PLIK_BURSZTYN_NOCNY[] =
    "# Bursztyn OS - Motyw\n"
    "wersja = 2\n"
    "id = bursztynnocny\n"
    "nazwa = Bursztyn Nocny\n"
    "\n"
    "pulpit = #111315\n"
    "oknotlo = #191C20\n"
    "panel = #20242A\n"
    "podniesiony = #262B31\n"
    "ramka = #343A42\n"
    "tekst = #F2F3F5\n"
    "tekstdrugorzedny = #A7ADB5\n"
    "wylaczony = #6F7680\n"
    "akcent = #F2A900\n"
    "akcentnajechany = #FFBC32\n"
    "akcentaktywny = #D99100\n"
    "blad = #E85D5D\n"
    "sukces = #51B87A\n"
    "ostrzezenie = #D97706\n"
    "informacja = #58A6E7\n"
    "poletekstowe = #15181C\n"
    "zaznaczenie = #314A63\n"
    "przycisk = #262B31\n"
    "przycisknajechany = #343A42\n"
    "tekstakcentu = #111315\n"
    "\n"
    "rogokna = 10\n"
    "rogpanelu = 8\n"
    "rogprzycisku = 7\n"
    "rogpola = 6\n"
    "rogmenu = 8\n"
    "rogpodpowiedzi = 6\n"
    "gruboscramki = 1\n"
    "cienokna = 1\n";

static const char PLIK_BALTYCKA_NOC[] =
    "# Bursztyn OS - Motyw\n"
    "wersja = 2\n"
    "id = baltyckanoc\n"
    "nazwa = Bałtycka Noc\n"
    "\n"
    "pulpit = #0B1118\n"
    "oknotlo = #111A24\n"
    "panel = #162230\n"
    "podniesiony = #1C2B3A\n"
    "ramka = #294054\n"
    "tekst = #E8F1F7\n"
    "tekstdrugorzedny = #9DB0BF\n"
    "wylaczony = #556877\n"
    "akcent = #35B7C8\n"
    "akcentnajechany = #58D1DF\n"
    "akcentaktywny = #2899A8\n"
    "blad = #E85D5D\n"
    "sukces = #42BA85\n"
    "ostrzezenie = #ECA038\n"
    "informacja = #5EA6E8\n"
    "poletekstowe = #0E161F\n"
    "zaznaczenie = #24506A\n"
    "przycisk = #1C2B3A\n"
    "przycisknajechany = #294054\n"
    "tekstakcentu = #0B1118\n"
    "\n"
    "rogokna = 10\n"
    "rogpanelu = 8\n"
    "rogprzycisku = 7\n"
    "rogpola = 6\n"
    "rogmenu = 8\n"
    "rogpodpowiedzi = 6\n"
    "gruboscramki = 1\n"
    "cienokna = 1\n";

static const char PLIK_GRAFIT[] =
    "# Bursztyn OS - Motyw\n"
    "wersja = 2\n"
    "id = grafit\n"
    "nazwa = Grafit\n"
    "\n"
    "pulpit = #121212\n"
    "oknotlo = #191919\n"
    "panel = #202020\n"
    "podniesiony = #282828\n"
    "ramka = #3A3A3A\n"
    "tekst = #F0F0F0\n"
    "tekstdrugorzedny = #A8A8A8\n"
    "wylaczony = #666666\n"
    "akcent = #8FA3B8\n"
    "akcentnajechany = #A9BCCF\n"
    "akcentaktywny = #758A9E\n"
    "blad = #E85D5D\n"
    "sukces = #51B87A\n"
    "ostrzezenie = #D4A034\n"
    "informacja = #6FA4C7\n"
    "poletekstowe = #161616\n"
    "zaznaczenie = #3D5266\n"
    "przycisk = #282828\n"
    "przycisknajechany = #3A3A3A\n"
    "tekstakcentu = #121212\n"
    "\n"
    "rogokna = 8\n"
    "rogpanelu = 6\n"
    "rogprzycisku = 5\n"
    "rogpola = 5\n"
    "rogmenu = 6\n"
    "rogpodpowiedzi = 5\n"
    "gruboscramki = 1\n"
    "cienokna = 1\n";

static const char PLIK_BURSZTYN_KLASYCZNY[] =
    "# Bursztyn OS - Motyw\n"
    "wersja = 2\n"
    "id = bursztynklasyczny\n"
    "nazwa = Bursztyn Klasyczny\n"
    "\n"
    "pulpit = #170D07\n"
    "oknotlo = #21130B\n"
    "panel = #2B190E\n"
    "podniesiony = #362014\n"
    "ramka = #6B431D\n"
    "tekst = #F8F1E8\n"
    "tekstdrugorzedny = #C7B39C\n"
    "wylaczony = #7A6857\n"
    "akcent = #D98A00\n"
    "akcentnajechany = #F2A900\n"
    "akcentaktywny = #B66A00\n"
    "blad = #E85D5D\n"
    "sukces = #5BB36C\n"
    "ostrzezenie = #E69A17\n"
    "informacja = #599ED4\n"
    "poletekstowe = #1D1009\n"
    "zaznaczenie = #654126\n"
    "przycisk = #362014\n"
    "przycisknajechany = #4A2D1C\n"
    "tekstakcentu = #170D07\n"
    "\n"
    "rogokna = 10\n"
    "rogpanelu = 8\n"
    "rogprzycisku = 7\n"
    "rogpola = 6\n"
    "rogmenu = 8\n"
    "rogpodpowiedzi = 6\n"
    "gruboscramki = 1\n"
    "cienokna = 1\n";

static const char PLIK_BURSZTYN_JASNY[] =
    "# Bursztyn OS - Motyw\n"
    "wersja = 2\n"
    "id = bursztynjasny\n"
    "nazwa = Bursztyn Jasny\n"
    "\n"
    "pulpit = #EDEBE7\n"
    "oknotlo = #F7F6F3\n"
    "panel = #EEECE7\n"
    "podniesiony = #FFFFFF\n"
    "ramka = #C9C6BF\n"
    "tekst = #202124\n"
    "tekstdrugorzedny = #5F6368\n"
    "wylaczony = #8A8F98\n"
    "akcent = #B87200\n"
    "akcentnajechany = #D99100\n"
    "akcentaktywny = #9A5F00\n"
    "blad = #BA1A1A\n"
    "sukces = #1E7E34\n"
    "ostrzezenie = #995200\n"
    "informacja = #1864AB\n"
    "poletekstowe = #FFFFFF\n"
    "zaznaczenie = #FFE082\n"
    "przycisk = #E9E6DF\n"
    "przycisknajechany = #DDD9D0\n"
    "tekstakcentu = #FFFFFF\n"
    "\n"
    "rogokna = 10\n"
    "rogpanelu = 8\n"
    "rogprzycisku = 7\n"
    "rogpola = 6\n"
    "rogmenu = 8\n"
    "rogpodpowiedzi = 6\n"
    "gruboscramki = 1\n"
    "cienokna = 1\n";

/* =========================================================================
 * Pomocnicze funkcje tekstowe (bezpieczne, freestanding)
 * ========================================================================= */

static bool tekst_rowny(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static size_t tekst_dlugosc(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) ++len;
    return len;
}

static void tekst_kopiuj(char* cel, const char* zrodlo, size_t max_rozmiar) {
    if (!cel || max_rozmiar == 0) return;
    size_t i = 0;
    if (zrodlo) {
        while (i + 1 < max_rozmiar && zrodlo[i]) {
            cel[i] = zrodlo[i];
            ++i;
        }
    }
    cel[i] = '\0';
}

static bool czy_bialy_znak(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool parsuj_liczbe_u32(const char* s, uint32_t* out) {
    if (!s || !out || *s == '\0') return false;
    uint64_t val = 0;
    size_t len = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        val = val * 10 + (*s - '0');
        if (val > UINT32_MAX) return false;
        ++s;
        ++len;
    }
    if (len == 0) return false;
    *out = static_cast<uint32_t>(val);
    return true;
}

static int hex_wartosc(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool parsuj_kolor(const char* s, uint32_t* out) {
    if (!s || !out) return false;
    if (*s == '#') ++s;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    size_t len = 0;
    while (s[len]) ++len;
    if (len != 6 && len != 8) return false;

    uint32_t res = 0;
    for (size_t i = 0; i < len; ++i) {
        int v = hex_wartosc(s[i]);
        if (v < 0) return false;
        res = (res << 4) | static_cast<uint32_t>(v);
    }

    if (len == 6) {
        // RGB bez podanego alfa traktujemy jako pelny kolor RGB z alfa 0
        *out = res & 0x00FFFFFFU;
    } else {
        *out = res;
    }
    return true;
}

static inline uint32_t kolor_jasnosc(uint32_t c) {
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    return (299U * r + 587U * g + 114U * b) / 1000U;
}

bool motyw_waliduj_kontrast(const BursztynMotyw* m) {
    if (!m) return false;
    uint32_t y_tekst = kolor_jasnosc(m->tekst);
    uint32_t y_okno = kolor_jasnosc(m->oknotlo);
    uint32_t y_panel = kolor_jasnosc(m->panel);

    int diff_okno = static_cast<int>(y_tekst) - static_cast<int>(y_okno);
    if (diff_okno < 0) diff_okno = -diff_okno;

    int diff_panel = static_cast<int>(y_tekst) - static_cast<int>(y_panel);
    if (diff_panel < 0) diff_panel = -diff_panel;

    return (diff_okno >= 40 && diff_panel >= 40);
}

bool motyw_waliduj(const BursztynMotyw* m) {
    if (!m) return false;
    if (m->wersja != MOTYW_ABI_WERSJA) return false;
    if (m->rozmiar_struktury != sizeof(BursztynMotyw)) return false;
    if (m->id[0] == '\0' || m->nazwa[0] == '\0') return false;

    // Identyfikator tylko alfanumeryczny + '_'
    if (!motyw_waliduj_id(m->id)) return false;

    if (m->rogokna > 32 || m->rogpanelu > 32 || m->rogprzycisku > 32 ||
        m->rogpola > 32 || m->rogmenu > 32 || m->rogpodpowiedzi > 32) {
        return false;
    }

    if (m->gruboscramki > 8 || m->cienokna > 1) {
        return false;
    }

    return motyw_waliduj_kontrast(m);
}

/* =========================================================================
 * Parsowanie formatu .motyw (fail-closed)
 * ========================================================================= */

enum FlagiPol : uint32_t {
    POLE_WERSJA          = 1U << 0,
    POLE_ID              = 1U << 1,
    POLE_NAZWA           = 1U << 2,
    POLE_PULPIT          = 1U << 3,
    POLE_OKNOTLO         = 1U << 4,
    POLE_PANEL           = 1U << 5,
    POLE_PODNIESIONY     = 1U << 6,
    POLE_RAMKA           = 1U << 7,
    POLE_TEKST           = 1U << 8,
    POLE_TEKST_DRUGI     = 1U << 9,
    POLE_WYLACZONY       = 1U << 10,
    POLE_AKCENT          = 1U << 11,
    POLE_AKCENT_NAJECHANY= 1U << 12,
    POLE_AKCENT_AKTYWNY  = 1U << 13,
    POLE_BLAD            = 1U << 14,
    POLE_SUKCES          = 1U << 15,
    POLE_OSTRZEZENIE     = 1U << 16,
    POLE_INFORMACJA      = 1U << 17,
    POLE_POLETEKSTOWE    = 1U << 18,
    POLE_ZAZNACZENIE     = 1U << 19,
    POLE_PRZYCISK        = 1U << 20,
    POLE_PRZYCISK_NAJECHANY = 1U << 21,
    POLE_TEKST_AKCENTU   = 1U << 22,
    POLE_ROG_OKNA        = 1U << 23,
    POLE_ROG_PANELU      = 1U << 24,
    POLE_ROG_PRZYCISKU   = 1U << 25,
    POLE_ROG_POLA        = 1U << 26,
    POLE_ROG_MENU        = 1U << 27,
    POLE_ROG_PODPOWIEDZI = 1U << 28,
    POLE_GRUBOSC_RAMKI   = 1U << 29,
    POLE_CIEN_OKNA       = 1U << 30
};

constexpr uint32_t WSZYSTKIE_WYMAGANE_POLA = (1U << 31) - 1U;

bool motyw_parsuj(const char* tekst, size_t dlugosc, BursztynMotyw* out) {
    if (!tekst || !out || dlugosc == 0 || dlugosc > 4096) return false;

    BursztynMotyw m{};
    m.rozmiar_struktury = sizeof(BursztynMotyw);
    uint32_t flagi = 0;

    size_t pos = 0;
    while (pos < dlugosc) {
        // Pomiń białe znaki na początku linii
        while (pos < dlugosc && czy_bialy_znak(tekst[pos]) && tekst[pos] != '\n') ++pos;
        if (pos >= dlugosc) break;

        // Pusta linia
        if (tekst[pos] == '\n') {
            ++pos;
            continue;
        }

        // Komentarz (# lub ;) lub nagłówek sekcji ([...])
        if (tekst[pos] == '#' || tekst[pos] == ';' || tekst[pos] == '[') {
            while (pos < dlugosc && tekst[pos] != '\n') ++pos;
            if (pos < dlugosc && tekst[pos] == '\n') ++pos;
            continue;
        }

        // Czytamy klucz
        char klucz[40] = {};
        size_t klucz_len = 0;
        while (pos < dlugosc && tekst[pos] != '=' && tekst[pos] != ':' &&
               tekst[pos] != '\n' && !czy_bialy_znak(tekst[pos])) {
            if (klucz_len + 1 < sizeof(klucz)) {
                klucz[klucz_len++] = tekst[pos];
            } else {
                return false; // Za długi klucz
            }
            ++pos;
        }
        klucz[klucz_len] = '\0';

        // Pomiń spacje przed operatorem
        while (pos < dlugosc && czy_bialy_znak(tekst[pos]) && tekst[pos] != '\n') ++pos;
        if (pos >= dlugosc || (tekst[pos] != '=' && tekst[pos] != ':')) {
            return false; // Brak separatora '=' lub ':'
        }
        ++pos; // Pomiń '=' lub ':'

        // Pomiń spacje po separatorze
        while (pos < dlugosc && czy_bialy_znak(tekst[pos]) && tekst[pos] != '\n') ++pos;

        // Czytamy wartość do końca linii
        char wartosc[64] = {};
        size_t wartosc_len = 0;
        while (pos < dlugosc && tekst[pos] != '\n' && tekst[pos] != '\r') {
            if (wartosc_len + 1 < sizeof(wartosc)) {
                wartosc[wartosc_len++] = tekst[pos];
            } else {
                return false; // Za długa wartość
            }
            ++pos;
        }
        // Przytnij białe znaki na końcu wartości
        while (wartosc_len > 0 && czy_bialy_znak(wartosc[wartosc_len - 1])) {
            wartosc[--wartosc_len] = '\0';
        }
        wartosc[wartosc_len] = '\0';

        if (pos < dlugosc && tekst[pos] == '\r') ++pos;
        if (pos < dlugosc && tekst[pos] == '\n') ++pos;

        if (klucz_len == 0 || wartosc_len == 0) return false;

        // Dopasowanie klucza (fail-closed na duplikat i nieznany)
        if (tekst_rowny(klucz, "wersja")) {
            if (flagi & POLE_WERSJA) return false;
            uint32_t w = 0;
            if (!parsuj_liczbe_u32(wartosc, &w) || w != MOTYW_ABI_WERSJA) return false;
            m.wersja = w;
            flagi |= POLE_WERSJA;
        } else if (tekst_rowny(klucz, "id")) {
            if (flagi & POLE_ID) return false;
            if (wartosc_len >= sizeof(m.id) || !motyw_waliduj_id(wartosc)) return false;
            tekst_kopiuj(m.id, wartosc, sizeof(m.id));
            flagi |= POLE_ID;
        } else if (tekst_rowny(klucz, "nazwa")) {
            if (flagi & POLE_NAZWA) return false;
            if (wartosc_len >= sizeof(m.nazwa)) return false;
            tekst_kopiuj(m.nazwa, wartosc, sizeof(m.nazwa));
            flagi |= POLE_NAZWA;
        } else if (tekst_rowny(klucz, "autor")) {
            // Opcjonalne metadane
            continue;
        } else if (tekst_rowny(klucz, "pulpit") || tekst_rowny(klucz, "tlo")) {
            if (flagi & POLE_PULPIT) return false;
            if (!parsuj_kolor(wartosc, &m.pulpit)) return false;
            flagi |= POLE_PULPIT;
        } else if (tekst_rowny(klucz, "oknotlo") || tekst_rowny(klucz, "okno")) {
            if (flagi & POLE_OKNOTLO) return false;
            if (!parsuj_kolor(wartosc, &m.oknotlo)) return false;
            flagi |= POLE_OKNOTLO;
        } else if (tekst_rowny(klucz, "panel")) {
            if (flagi & POLE_PANEL) return false;
            if (!parsuj_kolor(wartosc, &m.panel)) return false;
            flagi |= POLE_PANEL;
        } else if (tekst_rowny(klucz, "podniesiony") || tekst_rowny(klucz, "panel_podniesiony")) {
            if (flagi & POLE_PODNIESIONY) return false;
            if (!parsuj_kolor(wartosc, &m.podniesiony)) return false;
            flagi |= POLE_PODNIESIONY;
        } else if (tekst_rowny(klucz, "ramka")) {
            if (flagi & POLE_RAMKA) return false;
            if (!parsuj_kolor(wartosc, &m.ramka)) return false;
            flagi |= POLE_RAMKA;
        } else if (tekst_rowny(klucz, "tekst")) {
            if (flagi & POLE_TEKST) return false;
            if (!parsuj_kolor(wartosc, &m.tekst)) return false;
            flagi |= POLE_TEKST;
        } else if (tekst_rowny(klucz, "tekstdrugorzedny") || tekst_rowny(klucz, "tekst_drugi") || tekst_rowny(klucz, "tekstdrugi")) {
            if (flagi & POLE_TEKST_DRUGI) return false;
            if (!parsuj_kolor(wartosc, &m.tekstdrugorzedny)) return false;
            flagi |= POLE_TEKST_DRUGI;
        } else if (tekst_rowny(klucz, "wylaczony") || tekst_rowny(klucz, "tekst_wylaczony") || tekst_rowny(klucz, "tekstwylaczony")) {
            if (flagi & POLE_WYLACZONY) return false;
            if (!parsuj_kolor(wartosc, &m.wylaczony)) return false;
            flagi |= POLE_WYLACZONY;
        } else if (tekst_rowny(klucz, "akcent")) {
            if (flagi & POLE_AKCENT) return false;
            if (!parsuj_kolor(wartosc, &m.akcent)) return false;
            flagi |= POLE_AKCENT;
        } else if (tekst_rowny(klucz, "akcentnajechany") || tekst_rowny(klucz, "akcent_hover") || tekst_rowny(klucz, "akcent_najechany")) {
            if (flagi & POLE_AKCENT_NAJECHANY) return false;
            if (!parsuj_kolor(wartosc, &m.akcentnajechany)) return false;
            flagi |= POLE_AKCENT_NAJECHANY;
        } else if (tekst_rowny(klucz, "akcentaktywny") || tekst_rowny(klucz, "akcent_aktywny")) {
            if (flagi & POLE_AKCENT_AKTYWNY) return false;
            if (!parsuj_kolor(wartosc, &m.akcentaktywny)) return false;
            flagi |= POLE_AKCENT_AKTYWNY;
        } else if (tekst_rowny(klucz, "blad")) {
            if (flagi & POLE_BLAD) return false;
            if (!parsuj_kolor(wartosc, &m.blad)) return false;
            flagi |= POLE_BLAD;
        } else if (tekst_rowny(klucz, "sukces")) {
            if (flagi & POLE_SUKCES) return false;
            if (!parsuj_kolor(wartosc, &m.sukces)) return false;
            flagi |= POLE_SUKCES;
        } else if (tekst_rowny(klucz, "ostrzezenie")) {
            if (flagi & POLE_OSTRZEZENIE) return false;
            if (!parsuj_kolor(wartosc, &m.ostrzezenie)) return false;
            flagi |= POLE_OSTRZEZENIE;
        } else if (tekst_rowny(klucz, "informacja")) {
            if (flagi & POLE_INFORMACJA) return false;
            if (!parsuj_kolor(wartosc, &m.informacja)) return false;
            flagi |= POLE_INFORMACJA;
        } else if (tekst_rowny(klucz, "poletekstowe") || tekst_rowny(klucz, "pole_tekstowe")) {
            if (flagi & POLE_POLETEKSTOWE) return false;
            if (!parsuj_kolor(wartosc, &m.poletekstowe)) return false;
            flagi |= POLE_POLETEKSTOWE;
        } else if (tekst_rowny(klucz, "zaznaczenie")) {
            if (flagi & POLE_ZAZNACZENIE) return false;
            if (!parsuj_kolor(wartosc, &m.zaznaczenie)) return false;
            flagi |= POLE_ZAZNACZENIE;
        } else if (tekst_rowny(klucz, "przycisk")) {
            if (flagi & POLE_PRZYCISK) return false;
            if (!parsuj_kolor(wartosc, &m.przycisk)) return false;
            flagi |= POLE_PRZYCISK;
        } else if (tekst_rowny(klucz, "przycisknajechany") || tekst_rowny(klucz, "przycisk_najechany") || tekst_rowny(klucz, "przycisk_hover")) {
            if (flagi & POLE_PRZYCISK_NAJECHANY) return false;
            if (!parsuj_kolor(wartosc, &m.przycisknajechany)) return false;
            flagi |= POLE_PRZYCISK_NAJECHANY;
        } else if (tekst_rowny(klucz, "tekstakcentu") || tekst_rowny(klucz, "tekst_akcentu")) {
            if (flagi & POLE_TEKST_AKCENTU) return false;
            if (!parsuj_kolor(wartosc, &m.tekstakcentu)) return false;
            flagi |= POLE_TEKST_AKCENTU;
        } else if (tekst_rowny(klucz, "rogokna") || tekst_rowny(klucz, "rog_okna") || tekst_rowny(klucz, "rog_okno")) {
            if (flagi & POLE_ROG_OKNA) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 32) return false;
            m.rogokna = static_cast<uint16_t>(val);
            flagi |= POLE_ROG_OKNA;
        } else if (tekst_rowny(klucz, "rogpanelu") || tekst_rowny(klucz, "rog_panelu") || tekst_rowny(klucz, "rog_panel")) {
            if (flagi & POLE_ROG_PANELU) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 32) return false;
            m.rogpanelu = static_cast<uint16_t>(val);
            flagi |= POLE_ROG_PANELU;
        } else if (tekst_rowny(klucz, "rogprzycisku") || tekst_rowny(klucz, "rog_przycisku") || tekst_rowny(klucz, "rog_przycisk")) {
            if (flagi & POLE_ROG_PRZYCISKU) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 32) return false;
            m.rogprzycisku = static_cast<uint16_t>(val);
            flagi |= POLE_ROG_PRZYCISKU;
        } else if (tekst_rowny(klucz, "rogpola") || tekst_rowny(klucz, "rog_pola") || tekst_rowny(klucz, "rog_pole")) {
            if (flagi & POLE_ROG_POLA) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 32) return false;
            m.rogpola = static_cast<uint16_t>(val);
            flagi |= POLE_ROG_POLA;
        } else if (tekst_rowny(klucz, "rogmenu") || tekst_rowny(klucz, "rog_menu")) {
            if (flagi & POLE_ROG_MENU) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 32) return false;
            m.rogmenu = static_cast<uint16_t>(val);
            flagi |= POLE_ROG_MENU;
        } else if (tekst_rowny(klucz, "rogpodpowiedzi") || tekst_rowny(klucz, "rog_podpowiedzi") || tekst_rowny(klucz, "rog_tooltip")) {
            if (flagi & POLE_ROG_PODPOWIEDZI) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 32) return false;
            m.rogpodpowiedzi = static_cast<uint16_t>(val);
            flagi |= POLE_ROG_PODPOWIEDZI;
        } else if (tekst_rowny(klucz, "gruboscramki") || tekst_rowny(klucz, "grubosc_ramki")) {
            if (flagi & POLE_GRUBOSC_RAMKI) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 8) return false;
            m.gruboscramki = static_cast<uint8_t>(val);
            flagi |= POLE_GRUBOSC_RAMKI;
        } else if (tekst_rowny(klucz, "cienokna") || tekst_rowny(klucz, "cien_okna") || tekst_rowny(klucz, "wlaczony")) {
            if (flagi & POLE_CIEN_OKNA) return false;
            uint32_t val = 0;
            if (!parsuj_liczbe_u32(wartosc, &val) || val > 1) return false;
            m.cienokna = static_cast<uint8_t>(val);
            flagi |= POLE_CIEN_OKNA;
        } else {
            // Nieznany klucz -> fail-closed
            return false;
        }
    }

    if (flagi != WSZYSTKIE_WYMAGANE_POLA) return false;
    if (!motyw_waliduj(&m)) return false;

    *out = m;
    return true;
}

/* =========================================================================
 * Stan i podsystem jądra Bursztyn OS
 * ========================================================================= */

static BursztynMotyw g_aktywny_motyw = g_domyslny_motyw_fallback;
static BursztynMotyw g_motyw_przed_podgladem = g_domyslny_motyw_fallback;
static bool g_podglad_aktywny = false;
static int g_podglad_pid = 0;
static uint64_t g_podglad_deadline_ms = 0;

static uint64_t pobierz_czas_systemowy_ms() {
#ifndef BURSZTYN_HOST_TEST
    if (hpet_dostepny()) {
        return czas_monotoniczny_ms();
    }
    return scheduler_pobierz_tick() * 4ULL; // ~250 Hz -> 4 ms na tick
#else
    return 0;
#endif
}

void motyw_rozglos_zmiane(void) {
#ifndef BURSZTYN_HOST_TEST
    skladacz_obrazu_odswiez_dekoracje();
    bws_zdarzenie e{};
    e.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY;
    e.timestamp = pobierz_czas_systemowy_ms();
    for (int pid = 1; pid < static_cast<int>(MAKS_PROCESOW); ++pid) {
        if (tablica_procesow[pid].stan != PROCES_PUSTY) {
            (void)scheduler_dodaj_zdarzenie(pid, &e);
        }
    }
#endif
}

const BursztynMotyw* motyw_pobierz_aktywny(void) {
    uint64_t teraz = pobierz_czas_systemowy_ms();
    motyw_sprawdz_timeout_podgladu(teraz);
    return &g_aktywny_motyw;
}

bool motyw_ustaw_aktywny(const BursztynMotyw* m, bool trwaly) {
    if (!m || !motyw_waliduj(m)) return false;
    g_aktywny_motyw = *m;
    if (trwaly) {
#ifndef BURSZTYN_HOST_TEST
        zapewnij_katalog("/system");
        zapewnij_katalog("/system/konfig");
        char linia[64] = {};
        tekst_kopiuj(linia, "id = ", sizeof(linia));
        size_t l = tekst_dlugosc(linia);
        tekst_kopiuj(linia + l, m->id, sizeof(linia) - l);
        l = tekst_dlugosc(linia);
        if (l + 2 < sizeof(linia)) {
            linia[l] = '\n';
            linia[l + 1] = '\0';
            ++l;
        }
        (void)utworz_plik(MOTYW_SCIEZKA_KONFIG);
        (void)zapisz_do_pliku(MOTYW_SCIEZKA_KONFIG, linia, static_cast<uint32_t>(l));
        (void)psf_flush_cache();
#endif
    }
    motyw_rozglos_zmiane();
    return true;
}

static uint64_t g_podglad_token = 0;
static uint64_t g_ostatni_token = 100;

bool motyw_czy_uprawniony_do_mutacji(const proces_t* p) {
    if (!p) return false;
#ifndef BURSZTYN_HOST_TEST
    return pzb_ma_co_najmniej_poziom(p, PZB_ZAUFANE) &&
           pzb_ma_prawa(p, PRAWO_SYSTEM_CONFIG);
#else
    return (p->poziom_zaufania <= PZB_ZAUFANE) &&
           ((p->uprawnienia & PRAWO_SYSTEM_CONFIG) == PRAWO_SYSTEM_CONFIG);
#endif
}

bool motyw_zaladuj_z_pliku(const char* id, BursztynMotyw* out) {
    if (!id || !out || id[0] == '\0') return false;

    char czyste_id[MOTYW_MAX_DLUGOSC_ID] = {};
    size_t in_len = tekst_dlugosc(id);

    // Odrzucamy ścieżki i prefiksy katalogowe
    for (size_t i = 0; i < in_len; ++i) {
        if (id[i] == '/' || id[i] == '\\') return false;
    }

    if (in_len >= 6 && id[in_len - 6] == '.' && id[in_len - 5] == 'm' && id[in_len - 4] == 'o' &&
        id[in_len - 3] == 't' && id[in_len - 2] == 'y' && id[in_len - 1] == 'w') {
        size_t copy_len = in_len - 6;
        if (copy_len >= sizeof(czyste_id)) return false;
        for (size_t i = 0; i < copy_len; ++i) czyste_id[i] = id[i];
        czyste_id[copy_len] = '\0';
    } else {
        if (in_len >= sizeof(czyste_id)) return false;
        tekst_kopiuj(czyste_id, id, sizeof(czyste_id));
    }

    // Ścisła walidacja czystego ID: wyłącznie [a-z0-9_]
    if (!motyw_waliduj_id(czyste_id)) {
        return false;
    }

#ifndef BURSZTYN_HOST_TEST
    char sciezka[96] = {};
    tekst_kopiuj(sciezka, MOTYW_SCIEZKA_KATALOG "/", sizeof(sciezka));
    size_t l = tekst_dlugosc(sciezka);
    tekst_kopiuj(sciezka + l, czyste_id, sizeof(sciezka) - l);
    l = tekst_dlugosc(sciezka);
    tekst_kopiuj(sciezka + l, ".motyw", sizeof(sciezka) - l);

    uint32_t rozmiar = rozmiar_pliku(sciezka);
    if (rozmiar == 0) {
        // Próba z alternatywnym aliasem (np. bursztynnocny <-> bursztyn_nocny)
        const char* alias = nullptr;
        if (tekst_rowny(czyste_id, "bursztynnocny")) alias = "bursztyn_nocny";
        else if (tekst_rowny(czyste_id, "bursztyn_nocny")) alias = "bursztynnocny";
        else if (tekst_rowny(czyste_id, "baltyckanoc")) alias = "baltycka_noc";
        else if (tekst_rowny(czyste_id, "baltycka_noc")) alias = "baltyckanoc";
        else if (tekst_rowny(czyste_id, "bursztynklasyczny")) alias = "bursztyn_klasyczny";
        else if (tekst_rowny(czyste_id, "bursztyn_klasyczny")) alias = "bursztynklasyczny";
        else if (tekst_rowny(czyste_id, "bursztynjasny")) alias = "bursztyn_jasny";
        else if (tekst_rowny(czyste_id, "bursztyn_jasny")) alias = "bursztynjasny";

        if (alias) {
            tekst_kopiuj(sciezka, MOTYW_SCIEZKA_KATALOG "/", sizeof(sciezka));
            l = tekst_dlugosc(sciezka);
            tekst_kopiuj(sciezka + l, alias, sizeof(sciezka) - l);
            l = tekst_dlugosc(sciezka);
            tekst_kopiuj(sciezka + l, ".motyw", sizeof(sciezka) - l);
            rozmiar = rozmiar_pliku(sciezka);
        }
    }

    if (rozmiar == 0 || rozmiar > 4096) return false;

    char bufor[4096] = {};
    if (!czytaj_z_pliku(sciezka, bufor, rozmiar)) return false;

    return motyw_parsuj(bufor, rozmiar, out);
#else
    // Host mock / test fallback
    if (tekst_rowny(czyste_id, "bursztynnocny") || tekst_rowny(czyste_id, "bursztyn_nocny"))
        return motyw_parsuj(PLIK_BURSZTYN_NOCNY, sizeof(PLIK_BURSZTYN_NOCNY) - 1, out);
    if (tekst_rowny(czyste_id, "baltyckanoc") || tekst_rowny(czyste_id, "baltycka_noc"))
        return motyw_parsuj(PLIK_BALTYCKA_NOC, sizeof(PLIK_BALTYCKA_NOC) - 1, out);
    if (tekst_rowny(czyste_id, "grafit"))
        return motyw_parsuj(PLIK_GRAFIT, sizeof(PLIK_GRAFIT) - 1, out);
    if (tekst_rowny(czyste_id, "bursztynklasyczny") || tekst_rowny(czyste_id, "bursztyn_klasyczny"))
        return motyw_parsuj(PLIK_BURSZTYN_KLASYCZNY, sizeof(PLIK_BURSZTYN_KLASYCZNY) - 1, out);
    if (tekst_rowny(czyste_id, "bursztynjasny") || tekst_rowny(czyste_id, "bursztyn_jasny"))
        return motyw_parsuj(PLIK_BURSZTYN_JASNY, sizeof(PLIK_BURSZTYN_JASNY) - 1, out);
    return false;
#endif
}

bool motyw_ustaw_z_pliku(const char* id, bool trwaly) {
    BursztynMotyw m{};
    if (!motyw_zaladuj_z_pliku(id, &m)) return false;
    return motyw_ustaw_aktywny(&m, trwaly);
}

bool motyw_rozpocznij_podglad(const char* id, int pid, uint64_t czas_teraz) {
    // Inny proces nie może przejąć ani zresetować aktywnej transakcji podglądu
    if (g_podglad_aktywny && pid > 0 && g_podglad_pid != 0 && g_podglad_pid != pid) {
        return false;
    }

    BursztynMotyw m{};
    if (!motyw_zaladuj_z_pliku(id, &m)) return false;

    if (!g_podglad_aktywny) {
        g_motyw_przed_podgladem = g_aktywny_motyw;
    }
    g_aktywny_motyw = m;
    g_podglad_aktywny = true;
    g_podglad_pid = pid;
    g_podglad_token = ++g_ostatni_token;
    // 15 sekund limitu podglądu (15 000 ms)
    g_podglad_deadline_ms = czas_teraz + 15000ULL;

    motyw_rozglos_zmiane();
    return true;
}

bool motyw_zatwierdz_podglad_token(int pid, uint64_t token) {
    if (!g_podglad_aktywny) return false;
    if (pid > 0 && g_podglad_pid != 0 && g_podglad_pid != pid) return false;
    if (token != 0 && token != g_podglad_token) return false;

    g_podglad_aktywny = false;
    g_podglad_pid = 0;
    g_podglad_token = 0;
    g_podglad_deadline_ms = 0;

    // Zapisz na stałe
    return motyw_ustaw_aktywny(&g_aktywny_motyw, true);
}

bool motyw_zatwierdz_podglad(int pid) {
    return motyw_zatwierdz_podglad_token(pid, 0);
}

bool motyw_przywroc_podglad_token(int pid, uint64_t token) {
    if (!g_podglad_aktywny) return false;
    if (pid > 0 && g_podglad_pid != 0 && g_podglad_pid != pid) return false;
    if (token != 0 && token != g_podglad_token) return false;

    g_aktywny_motyw = g_motyw_przed_podgladem;
    g_podglad_aktywny = false;
    g_podglad_pid = 0;
    g_podglad_token = 0;
    g_podglad_deadline_ms = 0;

    motyw_rozglos_zmiane();
    return true;
}

bool motyw_przywroc_podglad(int pid) {
    return motyw_przywroc_podglad_token(pid, 0);
}

void motyw_sprawdz_timeout_podgladu(uint64_t czas_teraz) {
    if (g_podglad_aktywny && g_podglad_deadline_ms > 0 && czas_teraz >= g_podglad_deadline_ms) {
        (void)motyw_przywroc_podglad(0);
    }
}

void motyw_posprzataj_proces(int pid) {
    if (g_podglad_aktywny && pid > 0 && g_podglad_pid == pid) {
        (void)motyw_przywroc_podglad(0);
    }
}

void motyw_zainstaluj_domyslne_pliki(void) {
#ifndef BURSZTYN_HOST_TEST
    zapewnij_katalog("/system");
    zapewnij_katalog("/system/motywy");
    zapewnij_katalog("/system/konfig");

    struct PlikWbudowany {
        const char* nazwa;
        const char* tresc;
        size_t dlugosc;
    } pliki[] = {
        {"/system/motywy/bursztynnocny.motyw", PLIK_BURSZTYN_NOCNY, sizeof(PLIK_BURSZTYN_NOCNY) - 1},
        {"/system/motywy/bursztyn_nocny.motyw", PLIK_BURSZTYN_NOCNY, sizeof(PLIK_BURSZTYN_NOCNY) - 1},
        {"/system/motywy/baltyckanoc.motyw", PLIK_BALTYCKA_NOC, sizeof(PLIK_BALTYCKA_NOC) - 1},
        {"/system/motywy/baltycka_noc.motyw", PLIK_BALTYCKA_NOC, sizeof(PLIK_BALTYCKA_NOC) - 1},
        {"/system/motywy/grafit.motyw", PLIK_GRAFIT, sizeof(PLIK_GRAFIT) - 1},
        {"/system/motywy/bursztynklasyczny.motyw", PLIK_BURSZTYN_KLASYCZNY, sizeof(PLIK_BURSZTYN_KLASYCZNY) - 1},
        {"/system/motywy/bursztyn_klasyczny.motyw", PLIK_BURSZTYN_KLASYCZNY, sizeof(PLIK_BURSZTYN_KLASYCZNY) - 1},
        {"/system/motywy/bursztynjasny.motyw", PLIK_BURSZTYN_JASNY, sizeof(PLIK_BURSZTYN_JASNY) - 1},
        {"/system/motywy/bursztyn_jasny.motyw", PLIK_BURSZTYN_JASNY, sizeof(PLIK_BURSZTYN_JASNY) - 1}
    };

    for (size_t i = 0; i < sizeof(pliki)/sizeof(pliki[0]); ++i) {
        uint32_t istniejacy = rozmiar_pliku(pliki[i].nazwa);
        if (istniejacy == 0) {
            (void)utworz_plik(pliki[i].nazwa);
            (void)zapisz_do_pliku(pliki[i].nazwa, pliki[i].tresc, static_cast<uint32_t>(pliki[i].dlugosc));
        }
    }
    (void)psf_flush_cache();
#endif
}

void motyw_inicjalizuj(void) {
    g_aktywny_motyw = g_domyslny_motyw_fallback;
    g_motyw_przed_podgladem = g_domyslny_motyw_fallback;
    g_podglad_aktywny = false;
    g_podglad_pid = 0;
    g_podglad_token = 0;
    g_podglad_deadline_ms = 0;

#ifndef BURSZTYN_HOST_TEST
    motyw_zainstaluj_domyslne_pliki();

    // Sprawdzamy czy istnieje konfiguracja
    uint32_t r = rozmiar_pliku(MOTYW_SCIEZKA_KONFIG);
    if (r > 0 && r <= 256) {
        char bufor[256] = {};
        if (czytaj_z_pliku(MOTYW_SCIEZKA_KONFIG, bufor, r)) {
            // Szukamy 'id = ' lub 'id='
            char zadane_id[32] = {};
            size_t id_len = 0;
            const char* p = bufor;
            while (*p) {
                while (*p && czy_bialy_znak(*p)) ++p;
                if (p[0] == 'i' && p[1] == 'd') {
                    p += 2;
                    while (*p && czy_bialy_znak(*p)) ++p;
                    if (*p == '=') {
                        ++p;
                        while (*p && czy_bialy_znak(*p)) ++p;
                        while (*p && !czy_bialy_znak(*p) && id_len + 1 < sizeof(zadane_id)) {
                            zadane_id[id_len++] = *p++;
                        }
                        zadane_id[id_len] = '\0';
                        break;
                    }
                }
                while (*p && *p != '\n') ++p;
                if (*p == '\n') ++p;
            }

            if (id_len > 0) {
                BursztynMotyw z_pliku{};
                if (motyw_zaladuj_z_pliku(zadane_id, &z_pliku)) {
                    g_aktywny_motyw = z_pliku;
                    g_motyw_przed_podgladem = z_pliku;
                    return;
                }
            }
        }
    }

    // Jeśli brak konfiguracji, zapisujemy domyślny bursztynnocny
    (void)utworz_plik(MOTYW_SCIEZKA_KONFIG);
    (void)zapisz_do_pliku(MOTYW_SCIEZKA_KONFIG, "id = bursztynnocny\n", 19);
    (void)psf_flush_cache();
#endif
}

/* =========================================================================
 * BWS 86 i 87 (Obsługa wywołań systemowych)
 * ========================================================================= */

uint64_t motyw_bws_pobierz(const proces_t* proces, uint64_t uzytkownik_bufor) {
#ifndef BURSZTYN_HOST_TEST
    if (!proces || uzytkownik_bufor == 0) return 0;

    uint64_t teraz = pobierz_czas_systemowy_ms();
    motyw_sprawdz_timeout_podgladu(teraz);

    BursztynMotyw m = g_aktywny_motyw;
    m.wersja = MOTYW_ABI_WERSJA;
    m.rozmiar_struktury = sizeof(BursztynMotyw);
    for (size_t i = 0; i < sizeof(m.rezerwa); ++i) m.rezerwa[i] = 0;

    // Obsługa zgodności odczytu ze strukturą ABI v1 (144 B)
    uint32_t user_ver = 0;
    if (czy_bezpieczny_zakres_uzytkownika_do_odczytu(reinterpret_cast<const void*>(uzytkownik_bufor), sizeof(uint32_t))) {
        (void)skopiuj_z_przestrzeni_uzytkownika(&user_ver, reinterpret_cast<const void*>(uzytkownik_bufor), sizeof(uint32_t));
    }

    if (user_ver == 1 && czy_bezpieczny_zakres_uzytkownika_do_zapisu(reinterpret_cast<void*>(uzytkownik_bufor), sizeof(BursztynMotywV1))) {
        BursztynMotywV1 v1{};
        v1.wersja = 1;
        v1.pulpit = m.pulpit;
        v1.oknotlo = m.oknotlo;
        v1.panel = m.panel;
        v1.podniesiony = m.podniesiony;
        v1.ramka = m.ramka;
        v1.tekst = m.tekst;
        v1.tekstdrugorzedny = m.tekstdrugorzedny;
        v1.wylaczony = m.wylaczony;
        v1.akcent = m.akcent;
        v1.akcentnajechany = m.akcentnajechany;
        v1.akcentaktywny = m.akcentaktywny;
        v1.blad = m.blad;
        v1.sukces = m.sukces;
        v1.ostrzezenie = m.ostrzezenie;
        v1.informacja = m.informacja;
        v1.rogokna = m.rogokna;
        v1.rogpanelu = m.rogpanelu;
        v1.rogprzycisku = m.rogprzycisku;
        v1.rogpola = m.rogpola;
        v1.rogmenu = m.rogmenu;
        v1.rogpodpowiedzi = m.rogpodpowiedzi;
        v1.gruboscramki = m.gruboscramki;
        v1.cienokna = m.cienokna;
        v1.rezerwa[0] = 0; v1.rezerwa[1] = 0;
        tekst_kopiuj(v1.nazwa, m.nazwa, sizeof(v1.nazwa));
        tekst_kopiuj(v1.id, m.id, sizeof(v1.id));
        if (!skopiuj_do_przestrzeni_uzytkownika(reinterpret_cast<void*>(uzytkownik_bufor), &v1, sizeof(BursztynMotywV1))) {
            return 0;
        }
        return 1;
    }

    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(reinterpret_cast<void*>(uzytkownik_bufor), sizeof(BursztynMotyw))) {
        return 0;
    }

    if (!skopiuj_do_przestrzeni_uzytkownika(reinterpret_cast<void*>(uzytkownik_bufor), &m, sizeof(BursztynMotyw))) {
        return 0;
    }
    return 1;
#else
    (void)proces;
    if (!uzytkownik_bufor) return 0;
    BursztynMotyw m = g_aktywny_motyw;
    m.wersja = MOTYW_ABI_WERSJA;
    m.rozmiar_struktury = sizeof(BursztynMotyw);
    for (size_t i = 0; i < sizeof(m.rezerwa); ++i) m.rezerwa[i] = 0;
    *reinterpret_cast<BursztynMotyw*>(uzytkownik_bufor) = m;
    return 1;
#endif
}

uint64_t motyw_bws_polecenie(const proces_t* proces, uint32_t polecenie, uint64_t arg1, uint64_t arg2) {
    if (!proces) return 0;
    uint64_t teraz = pobierz_czas_systemowy_ms();
    motyw_sprawdz_timeout_podgladu(teraz);

    switch (polecenie) {
        case MOTYW_CMD_USTAW: {
            if (!motyw_czy_uprawniony_do_mutacji(proces)) {
                return 0;
            }
            if (arg1 == 0) return 0;
#ifndef BURSZTYN_HOST_TEST
            if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(reinterpret_cast<const void*>(arg1), MOTYW_MAX_DLUGOSC_ID)) {
                return 0;
            }
            char id[MOTYW_MAX_DLUGOSC_ID] = {};
            if (!skopiuj_z_przestrzeni_uzytkownika(id, reinterpret_cast<const void*>(arg1), sizeof(id) - 1)) {
                return 0;
            }
            id[sizeof(id) - 1] = '\0';
#else
            const char* id = reinterpret_cast<const char*>(arg1);
#endif
            if (!motyw_waliduj_id(id)) return 0;
            return motyw_ustaw_z_pliku(id, true) ? 1 : 0;
        }

        case MOTYW_CMD_PODGLAD: {
            if (!motyw_czy_uprawniony_do_mutacji(proces)) {
                return 0;
            }
            if (arg1 == 0) return 0;
            // Sprawdzenie własności transakcji: inny proces nie może przejąć aktywnego podglądu
            if (g_podglad_aktywny && g_podglad_pid != 0 && static_cast<uint64_t>(g_podglad_pid) != proces->pid) {
                return 0;
            }
#ifndef BURSZTYN_HOST_TEST
            if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(reinterpret_cast<const void*>(arg1), MOTYW_MAX_DLUGOSC_ID)) {
                return 0;
            }
            char id[MOTYW_MAX_DLUGOSC_ID] = {};
            if (!skopiuj_z_przestrzeni_uzytkownika(id, reinterpret_cast<const void*>(arg1), sizeof(id) - 1)) {
                return 0;
            }
            id[sizeof(id) - 1] = '\0';
#else
            const char* id = reinterpret_cast<const char*>(arg1);
#endif
            if (!motyw_waliduj_id(id)) return 0;
            if (!motyw_rozpocznij_podglad(id, static_cast<int>(proces->pid), teraz)) return 0;
            return g_podglad_token > 0 ? g_podglad_token : 1;
        }

        case MOTYW_CMD_ZATWIERDZ: {
            if (!motyw_czy_uprawniony_do_mutacji(proces)) {
                return 0;
            }
            if (!g_podglad_aktywny) return 0;
            if (proces->pid > 0 && g_podglad_pid != 0 && static_cast<uint64_t>(g_podglad_pid) != proces->pid) return 0;
            if (arg1 != 0 && arg1 != g_podglad_token) return 0;
            return motyw_zatwierdz_podglad_token(static_cast<int>(proces->pid), arg1) ? 1 : 0;
        }

        case MOTYW_CMD_PRZYWROC: {
            if (!motyw_czy_uprawniony_do_mutacji(proces)) {
                return 0;
            }
            if (!g_podglad_aktywny) return 0;
            if (proces->pid > 0 && g_podglad_pid != 0 && static_cast<uint64_t>(g_podglad_pid) != proces->pid) return 0;
            if (arg1 != 0 && arg1 != g_podglad_token) return 0;
            return motyw_przywroc_podglad_token(static_cast<int>(proces->pid), arg1) ? 1 : 0;
        }

        case MOTYW_CMD_LISTUJ: {
            // Bezpieczny odczyt listy dostępnych motywów
            (void)arg1; (void)arg2;
            return 5;
        }

        default:
            return 0;
    }
}

#ifdef __cplusplus
}
#endif
