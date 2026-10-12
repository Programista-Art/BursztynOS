#include "bws_czcionka.h"
#include "pzb.h"
#include "czcionki/extronic16Bunicode.h"

extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void*, size_t);
extern bool skopiuj_do_przestrzeni_uzytkownika(void*, const void*, size_t);

uint64_t bws_czcionka_kopiuj(uint64_t prawa, uint64_t polecenie,
                           uint64_t bufor, uint64_t rozmiar) {
    if (!(prawa & PRAWO_GUI)) return 0;
    if (!polecenie) return 16; // unchanged BWS 51 ABI
    if (polecenie != 1 || !bufor || rozmiar != sizeof(BwsCzcionkaSystemowa) ||
        bufor > UINT64_MAX - rozmiar) return 0;
    auto* p = reinterpret_cast<uint8_t*>(bufor);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(p, rozmiar)) return 0;
    static_assert(sizeof(nowa_czcionka_szerokosci) == BWS_CZCIONKA_GLIFY);
    static_assert(sizeof(nowa_czcionka_16x16) == BWS_CZCIONKA_GLIFY * 32);
    const uint32_t header[] = {BWS_CZCIONKA_WERSJA, sizeof(BwsCzcionkaSystemowa),
                              BWS_CZCIONKA_GLIFY, 16};
    // Publish the header last; partial copies are never accepted by Ring 3.
    if (!skopiuj_do_przestrzeni_uzytkownika(
            p + offsetof(BwsCzcionkaSystemowa, szerokosci),
            nowa_czcionka_szerokosci, sizeof(nowa_czcionka_szerokosci)) ||
        !skopiuj_do_przestrzeni_uzytkownika(
            p + offsetof(BwsCzcionkaSystemowa, bitmapy),
            nowa_czcionka_16x16, sizeof(nowa_czcionka_16x16)) ||
        !skopiuj_do_przestrzeni_uzytkownika(p, header, sizeof(header))) return 0;
    return 1;
}
