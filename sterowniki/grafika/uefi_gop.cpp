/*
 * Bursztyn OS - UEFI GOP framebuffer helper
 *
 * Ten modul NIE inicjalizuje firmware UEFI i nie wywoluje EFI GOP.
 * Otrzymuje juz przygotowany framebuffer od warstwy boot/grafika i
 * przechowuje jego geometrie dla prostych operacji pikselowych.
 *
 * Wazne:
 *   - adres_fb musi byc adresem dostepnym dla CPU (w obecnym grafika.cpp
 *     jest to juz zmapowany adres wirtualny framebuffera),
 *   - GOP_PostawPiksel obsluguje wylacznie format 32 bpp,
 *   - pitch jest liczony w BAJTACH, nie pikselach,
 *   - zapis do framebuffera jest wykonywany przez volatile,
 *   - bledne parametry zeruja stan zamiast pozostawiac czesciowo
 *     zainicjalizowany ekran.
 */

#include "uefi_gop.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. PUBLICZNY STAN GOP
 * ========================================================================= */

/*
 * Zachowujemy symbol i layout wymagany przez pozostale moduly grafiki.
 *
 * Nie dodajemy pol do GOP_Zmienne, aby nie zmieniac ABI naglowka.
 */
GOP_Zmienne gop_ekran = {
    nullptr,
    0,
    0,
    0,
    0
};

/* =========================================================================
 * 2. STALE I PRYWATNY STAN
 * ========================================================================= */

namespace {

constexpr uint32_t GOP_WYMAGANE_BPP =
    32U;

constexpr uint32_t GOP_BAJTOW_NA_PIKSEL =
    GOP_WYMAGANE_BPP /
    8U;

/*
 * Rozmiar faktycznie dostepnego obszaru framebuffera wynikajacy z:
 *
 *   pitch * wysokosc
 *
 * Nie jest czescia publicznego ABI.
 */
uint64_t gop_rozmiar_framebuffer =
    0;

/*
 * Flaga jest publikowana dopiero po zapisaniu kompletnego stanu.
 *
 * GOP jest inicjalizowany przed normalna praca GUI; flaga dodatkowo
 * zabezpiecza przypadkowe wywolanie GOP_PostawPiksel przed inicjalizacja.
 */
bool gop_gotowy =
    false;

/* =========================================================================
 * 3. HELPERY
 * ========================================================================= */

bool mnozenie_u64_bez_overflow(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) {
        return false;
    }

    if (a != 0 &&
        b >
            UINT64_MAX /
            a) {

        return false;
    }

    *wynik =
        a *
        b;

    return true;
}

bool dodawanie_u64_bez_overflow(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) {
        return false;
    }

    if (a >
        UINT64_MAX -
        b) {

        return false;
    }

    *wynik =
        a +
        b;

    return true;
}

void wyzeruj_stan_gop() {
    /*
     * Najpierw uniewazniamy stan. GOP_PostawPiksel nie powinien wtedy
     * korzystac z zadnego z ponizszych pol.
     */
    __atomic_store_n(
        &gop_gotowy,
        false,
        __ATOMIC_RELEASE
    );

    gop_ekran.framebuffer =
        nullptr;

    gop_ekran.szerokosc =
        0;

    gop_ekran.wysokosc =
        0;

    gop_ekran.pitch =
        0;

    gop_ekran.bpp =
        0;

    gop_rozmiar_framebuffer =
        0;
}

bool parametry_gop_poprawne(
    uint64_t adres_fb,
    uint32_t szerokosc,
    uint32_t wysokosc,
    uint32_t pitch,
    uint32_t bpp,
    uint64_t* rozmiar_wynik
) {
    if (!rozmiar_wynik) {
        return false;
    }

    if (adres_fb == 0 ||
        szerokosc == 0 ||
        wysokosc == 0 ||
        pitch == 0) {

        return false;
    }

    /*
     * GOP_PostawPiksel wykonuje jeden 32-bitowy zapis.
     * Nie udajemy obslugi 15/16/24 bpp.
     */
    if (bpp !=
        GOP_WYMAGANE_BPP) {

        return false;
    }

    /*
     * Dla 32-bitowych pikseli framebuffer oraz poczatek kazdego wiersza
     * musza zachowac przynajmniej 4-bajtowe wyrownanie.
     */
    if ((adres_fb &
         (GOP_BAJTOW_NA_PIKSEL - 1U)) != 0 ||
        (pitch &
         (GOP_BAJTOW_NA_PIKSEL - 1U)) != 0) {

        return false;
    }

    /*
     * Minimalny pitch:
     *
     *   width * 4
     *
     * Obliczamy w 64 bitach, aby szerokosc nie mogla zawinac uint32_t.
     */
    uint64_t minimalny_pitch =
        0;

    if (!mnozenie_u64_bez_overflow(
            static_cast<uint64_t>(
                szerokosc
            ),
            GOP_BAJTOW_NA_PIKSEL,
            &minimalny_pitch)) {

        return false;
    }

    if (static_cast<uint64_t>(
            pitch) <
        minimalny_pitch) {

        return false;
    }

    /*
     * Caly dostepny obszar:
     *
     *   pitch * height
     */
    uint64_t rozmiar =
        0;

    if (!mnozenie_u64_bez_overflow(
            static_cast<uint64_t>(
                pitch
            ),
            static_cast<uint64_t>(
                wysokosc
            ),
            &rozmiar)) {

        return false;
    }

    if (rozmiar <
        GOP_BAJTOW_NA_PIKSEL) {

        return false;
    }

    /*
     * Sprawdzamy rowniez overflow samego zakresu adresowego:
     *
     *   [adres_fb, adres_fb + rozmiar)
     */
    uint64_t koniec =
        0;

    if (!dodawanie_u64_bez_overflow(
            adres_fb,
            rozmiar,
            &koniec)) {

        return false;
    }

    /*
     * koniec == 0 moze nastapic tylko przy overflow, ktory zostal juz
     * odrzucony. Ten test utrzymuje jawna semantyke niepustego zakresu.
     */
    if (koniec <=
        adres_fb) {

        return false;
    }

    *rozmiar_wynik =
        rozmiar;

    return true;
}

} // namespace

/* =========================================================================
 * 4. INICJALIZACJA
 * ========================================================================= */

void InicjalizujGOP(
    uint64_t adres_fb,
    uint32_t szer,
    uint32_t wys,
    uint32_t pitch,
    uint32_t bpp
) {
    /*
     * Fail-closed:
     * nigdy nie pozostawiamy starego framebuffera aktywnego po probie
     * inicjalizacji z niepoprawnymi parametrami.
     */
    wyzeruj_stan_gop();

    uint64_t rozmiar =
        0;

    if (!parametry_gop_poprawne(
            adres_fb,
            szer,
            wys,
            pitch,
            bpp,
            &rozmiar)) {

        return;
    }

    /*
     * Publikujemy kompletny stan dopiero po przejsciu wszystkich kontroli.
     *
     * GOP_Zmienne pozostaje zwyklym publicznym obiektem dla zgodnosci
     * z istniejacym grafika.cpp.
     */
    gop_ekran.framebuffer =
        reinterpret_cast<uint32_t*>(
            adres_fb
        );

    gop_ekran.szerokosc =
        szer;

    gop_ekran.wysokosc =
        wys;

    gop_ekran.pitch =
        pitch;

    gop_ekran.bpp =
        bpp;

    gop_rozmiar_framebuffer =
        rozmiar;

    /*
     * Release zapewnia, ze GOP_PostawPiksel, ktory zobaczy true przez
     * acquire, zobaczy tez komplet danych zapisanych wyzej.
     */
    __atomic_store_n(
        &gop_gotowy,
        true,
        __ATOMIC_RELEASE
    );
}

/* =========================================================================
 * 5. ZAPIS PIKSELA
 * ========================================================================= */

void GOP_PostawPiksel(
    int x,
    int y,
    uint32_t kolor
) {
    if (!__atomic_load_n(
            &gop_gotowy,
            __ATOMIC_ACQUIRE)) {

        return;
    }

    /*
     * Najpierw odrzucamy wartosci ujemne. Dopiero pozniej wykonujemy
     * konwersje do unsigned.
     */
    if (x < 0 ||
        y < 0) {

        return;
    }

    const uint32_t ux =
        static_cast<uint32_t>(
            x
        );

    const uint32_t uy =
        static_cast<uint32_t>(
            y
        );

    if (ux >=
            gop_ekran.szerokosc ||
        uy >=
            gop_ekran.wysokosc) {

        return;
    }

    if (!gop_ekran.framebuffer ||
        gop_ekran.bpp !=
            GOP_WYMAGANE_BPP ||
        gop_ekran.pitch == 0) {

        return;
    }

    /*
     * Offset liczymy w bajtach i w 64 bitach:
     *
     *   y * pitch + x * 4
     *
     * Stara wersja skladala indeks do uint32_t, co przy duzym pitch/ekranie
     * moglo zawinac i zapisac poza framebufferem.
     */
    uint64_t offset_wiersza =
        0;

    if (!mnozenie_u64_bez_overflow(
            static_cast<uint64_t>(
                uy
            ),
            static_cast<uint64_t>(
                gop_ekran.pitch
            ),
            &offset_wiersza)) {

        return;
    }

    uint64_t offset_x =
        0;

    if (!mnozenie_u64_bez_overflow(
            static_cast<uint64_t>(
                ux
            ),
            GOP_BAJTOW_NA_PIKSEL,
            &offset_x)) {

        return;
    }

    uint64_t offset =
        0;

    if (!dodawanie_u64_bez_overflow(
            offset_wiersza,
            offset_x,
            &offset)) {

        return;
    }

    /*
     * Sprawdzamy caly 4-bajtowy zapis, a nie tylko pierwszy bajt.
     */
    if (gop_rozmiar_framebuffer <
            GOP_BAJTOW_NA_PIKSEL ||
        offset >
            gop_rozmiar_framebuffer -
            GOP_BAJTOW_NA_PIKSEL) {

        return;
    }

    /*
     * Framebuffer jest MMIO / write-combined memory, nie zwyklym RAM-em.
     * volatile zapobiega usunieciu lub niepozadanemu scaleniu samego
     * zapisu piksela przez kompilator.
     *
     * Adres oraz pitch zostaly sprawdzone pod katem 4-byte alignment.
     */
    volatile uint8_t* baza =
        reinterpret_cast<volatile uint8_t*>(
            gop_ekran.framebuffer
        );

    volatile uint32_t* piksel =
        reinterpret_cast<volatile uint32_t*>(
            baza +
            offset
        );

    *piksel =
        kolor;
}
