/*
 * BRB - ograniczona konwersja UTF-8 -> UCS-2 (CHAR16) dla konsoli tekstowej UEFI.
 *
 * Polityka (jedna, dla calego BRB):
 *   - dekodowanie scisle wg RFC 3629: odrzucane sa sekwencje nadmiarowe (overlong), surogaty
 *     U+D800..U+DFFF, wartosci > U+10FFFF, bajty 0xC0, 0xC1, 0xF5..0xFF, samotne bajty kontynuacji
 *     i sekwencje urwane (takze przez koniec bufora albo NUL),
 *   - kazda bledna sekwencja daje jeden znak zastepczy '?' (U+003F); dekoder przesuwa sie o 1 bajt
 *     (bajt wiodacy) albo za ostatni poprawny bajt kontynuacji - nigdy nie "zjada" nastepnego znaku,
 *   - poprawne znaki spoza BMP (U+10000..U+10FFFF) tez daja '?' - konsola UEFI jest UCS-2 (bez par surogatow),
 *   - znaki sterujace C0 poza \n i \t oraz DEL sa zamieniane na '?' (zabezpieczenie konsoli); \n -> CR LF,
 *   - wynik ZAWSZE konczy sie NUL, o ile pojemnosc > 0; przy braku miejsca konwersja jest przerywana
 *     na granicy calego znaku (nigdy "pol" CR LF) i ustawiana jest flaga obciete.
 * Nie jest to biblioteka Unicode: brak normalizacji, kategorii i par surogatow - celowo.
 */
#ifndef BRB_UTF8_H
#define BRB_UTF8_H

#include <stddef.h>
#include <stdint.h>

namespace brb {

constexpr char16_t ZNAK_ZASTEPCZY = u'?';

struct WynikUtf8 {
    size_t   zapisane;         // liczba jednostek CHAR16 bez koncowego NUL
    uint32_t bledne;           // liczba blednych sekwencji (zamienionych na '?')
    uint32_t poza_bmp;         // poprawne znaki spoza BMP (zamienione na '?')
    uint32_t sterujace;        // znaki sterujace zamienione na '?'
    bool     obciete;          // zabraklo miejsca w buforze docelowym
};

// zrodlo: tekst UTF-8 o dlugosci dlugosc bajtow (konwersja konczy sie takze na pierwszym bajcie 0).
// cel/pojemnosc: bufor CHAR16 i jego pojemnosc w jednostkach (lacznie z NUL). cel == nullptr albo
// pojemnosc == 0: nic nie jest zapisywane, wynik.obciete = true (o ile zrodlo niepuste).
WynikUtf8 utf8_na_ucs2(const char* zrodlo, size_t dlugosc, char16_t* cel, size_t pojemnosc);

// Transliteracja UCS-2 do ASCII "w miejscu" dla konsol bez polskich glifow:
// ą->a ć->c ę->e ł->l ń->n ó->o ś->s ź->z ż->z (i wielkie litery), pozostale > 0x7E -> '?'.
// Zwraca liczbe zmienionych znakow.
size_t transliteruj_ascii(char16_t* tekst, size_t pojemnosc);

size_t dlugosc_tekstu(const char* tekst, size_t maksimum);

}  // namespace brb

#endif
