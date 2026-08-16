/*
 * Bursztyn OS - UEFI GOP framebuffer
 *
 * Publiczny interfejs pomocniczej warstwy framebuffera.
 *
 * Implementacja:
 *   uefi_gop.cpp
 *
 * WAZNE:
 * Ten modul nie komunikuje sie bezposrednio z firmware UEFI. Otrzymuje
 * juz przygotowany i dostepny dla CPU adres framebuffera wraz z geometria.
 *
 * Aktualna implementacja obsluguje:
 *   - framebuffer 32 bpp,
 *   - pitch podany w bajtach,
 *   - bezposredni zapis pojedynczego piksela,
 *   - bezpieczne odrzucanie wspolrzednych poza ekranem.
 *
 * Symbole zachowuja linkage C++, zgodnie z aktualnym uefi_gop.cpp.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. PUBLICZNE STALE
 * ========================================================================= */

/*
 * Aktualny GOP_PostawPiksel zapisuje jeden uint32_t na piksel.
 */
#define GOP_API_BPP UINT32_C(32)

/*
 * 32 bity / 8 = 4 bajty na piksel.
 */
#define GOP_API_BAJTOW_NA_PIKSEL UINT32_C(4)

/* =========================================================================
 * 2. PUBLICZNY OPIS FRAMEBUFFERA
 * ========================================================================= */

/*
 * Parametry aktywnego framebuffera.
 *
 * framebuffer:
 *   adres dostepny dla CPU. W obecnej architekturze powinien byc juz
 *   poprawnie zmapowany przez warstwe pamieci/grafiki.
 *
 * szerokosc / wysokosc:
 *   rozdzielczosc w pikselach.
 *
 * pitch:
 *   liczba BAJTOW pomiedzy poczatkami dwoch kolejnych wierszy.
 *   Nie wolno zakladac pitch == szerokosc * 4.
 *
 * bpp:
 *   liczba bitow na piksel. Poprawiony uefi_gop.cpp akceptuje tylko 32.
 */
struct GOP_Zmienne {
    uint32_t* framebuffer;

    uint32_t szerokosc;
    uint32_t wysokosc;

    uint32_t pitch;
    uint32_t bpp;
};

/*
 * Globalny, tylko-do-odczytu z punktu widzenia innych modulow opis ekranu.
 *
 * Nie nalezy modyfikowac pol bezposrednio. Stan powinien byc ustawiany
 * tylko przez InicjalizujGOP(), poniewaz implementacja utrzymuje dodatkowa
 * prywatna walidacje i flage gotowosci.
 *
 * Typ pozostaje nie-const dla zgodnosci ABI z istniejacym kodem.
 */
extern GOP_Zmienne gop_ekran;

/* =========================================================================
 * 3. PUBLICZNE API
 * ========================================================================= */

/*
 * Inicjalizuje opis framebuffera.
 *
 * adres_fb:
 *   adres framebuffera dostepny dla CPU.
 *
 * szer / wys:
 *   rozdzielczosc w pikselach; obie wartosci musza byc > 0.
 *
 * pitch:
 *   bajty na wiersz; musi byc >= szer * 4 i podzielny przez 4.
 *
 * bpp:
 *   obecnie musi byc rowne GOP_API_BPP (32).
 *
 * Funkcja zachowuje ABI void.
 * Przy blednych parametrach stan GOP zostaje wyzerowany fail-closed.
 */
void InicjalizujGOP(
    uint64_t adres_fb,
    uint32_t szer,
    uint32_t wys,
    uint32_t pitch,
    uint32_t bpp
);

/*
 * Zapisuje pojedynczy piksel.
 *
 * Wspolrzedne poza:
 *
 *   0 <= x < szerokosc
 *   0 <= y < wysokosc
 *
 * sa bezpiecznie ignorowane.
 *
 * kolor:
 *   surowa 32-bitowa wartosc piksela w formacie oczekiwanym przez
 *   aktualny framebuffer. Ten modul nie wykonuje konwersji RGB/BGR.
 *
 * Funkcja nic nie robi, jezeli GOP nie zostal poprawnie zainicjalizowany.
 */
void GOP_PostawPiksel(
    int x,
    int y,
    uint32_t kolor
);

/* =========================================================================
 * 4. HELPERY HEADER-ONLY
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Minimalna liczba bajtow w wierszu dla zadanej szerokosci.
 *
 * Zwraca 0 przy overflow uint32_t.
 */
inline constexpr uint32_t gop_api_minimalny_pitch(
    uint32_t szerokosc
) noexcept {
    return
        szerokosc <=
                UINT32_MAX /
                GOP_API_BAJTOW_NA_PIKSEL
            ? szerokosc *
                GOP_API_BAJTOW_NA_PIKSEL
            : 0U;
}

/*
 * Podstawowa walidacja parametrow niezalezna od adresu framebuffera.
 *
 * Wlasciwa implementacja uefi_gop.cpp wykonuje dodatkowo kontrole:
 *   - adres_fb != 0,
 *   - alignment adresu,
 *   - overflow pitch * wysokosc,
 *   - overflow koncowego zakresu adresowego.
 */
inline constexpr bool gop_api_geometria_poprawna(
    uint32_t szerokosc,
    uint32_t wysokosc,
    uint32_t pitch,
    uint32_t bpp
) noexcept {
    const uint32_t minimum =
        gop_api_minimalny_pitch(
            szerokosc
        );

    return
        szerokosc != 0 &&
        wysokosc != 0 &&
        bpp == GOP_API_BPP &&
        minimum != 0 &&
        pitch >= minimum &&
        (pitch &
         (GOP_API_BAJTOW_NA_PIKSEL - 1U)) == 0;
}

/*
 * Sprawdza tylko granice wspolrzednych.
 */
inline constexpr bool gop_api_wspolrzedne_poprawne(
    int x,
    int y,
    uint32_t szerokosc,
    uint32_t wysokosc
) noexcept {
    return
        x >= 0 &&
        y >= 0 &&
        static_cast<uint32_t>(x) <
            szerokosc &&
        static_cast<uint32_t>(y) <
            wysokosc;
}

/* =========================================================================
 * 5. KONTROLA ABI / LAYOUTU
 * ========================================================================= */

static_assert(
    sizeof(uint32_t) == 4,
    "GOP wymaga 32-bitowego uint32_t"
);

static_assert(
    sizeof(uint64_t) == 8,
    "GOP wymaga 64-bitowego uint64_t"
);

static_assert(
    GOP_API_BPP == 32U,
    "Aktualny GOP_PostawPiksel wymaga 32 bpp"
);

static_assert(
    GOP_API_BAJTOW_NA_PIKSEL == 4U,
    "32 bpp musi odpowiadac 4 bajtom na piksel"
);

/*
 * x86_64 ABI:
 *
 *   framebuffer  offset 0
 *   szerokosc    offset 8
 *   wysokosc     offset 12
 *   pitch        offset 16
 *   bpp          offset 20
 *   sizeof              24
 */
static_assert(
    offsetof(
        GOP_Zmienne,
        framebuffer
    ) == 0U,
    "Nieprawidlowy offset GOP_Zmienne::framebuffer"
);

static_assert(
    offsetof(
        GOP_Zmienne,
        szerokosc
    ) == 8U,
    "Nieprawidlowy offset GOP_Zmienne::szerokosc"
);

static_assert(
    offsetof(
        GOP_Zmienne,
        wysokosc
    ) == 12U,
    "Nieprawidlowy offset GOP_Zmienne::wysokosc"
);

static_assert(
    offsetof(
        GOP_Zmienne,
        pitch
    ) == 16U,
    "Nieprawidlowy offset GOP_Zmienne::pitch"
);

static_assert(
    offsetof(
        GOP_Zmienne,
        bpp
    ) == 20U,
    "Nieprawidlowy offset GOP_Zmienne::bpp"
);

static_assert(
    sizeof(GOP_Zmienne) == 24U,
    "Nieprawidlowy rozmiar GOP_Zmienne dla x86_64"
);

static_assert(
    alignof(GOP_Zmienne) == 8U,
    "Nieprawidlowe wyrownanie GOP_Zmienne dla x86_64"
);

#endif /* __cplusplus */
