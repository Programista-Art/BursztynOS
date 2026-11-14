#pragma once
/*
 * Centralne kursory zmiany rozmiaru okna.
 *
 * Aplikacje (BursztynUI, Eksplorator, Menedzer Okien, aplikacje natywne) wybieraja
 * tylko TYP kursora (GUI_CURSOR_RESIZE_*); wyglad zapewnia jadro. Gdy podczas
 * budowania istnieje ikonki/kursor_rozciagania.png, narzedzia/generuj_kursor_rozciagania.py
 * zamienia je na male, ograniczone mapy bitowe (bez dekodera PNG w Ring 0).
 * Gdy pliku brak, KURSOR_ROZCIAGANIA_DOSTEPNY == 0 i uzywane sa ksztalty wbudowane.
 */
#include <stdint.h>
#include "kursor_rozciagania.generated.h"

struct OpisKursoraRozciagania {
    uint16_t szer, wys, hotspot_x, hotspot_y;
    const uint32_t* piksele;   // ARGB, alfa 0 = przezroczysty
};

// typ: wartosci GUI_CURSOR_RESIZE_NS=3, EW=4, NWSE=5, NESW=6. nullptr = brak
// zasobu (uzyj ksztaltu wbudowanego).
inline const OpisKursoraRozciagania* kursor_rozciagania_opis(uint32_t typ) {
#if KURSOR_ROZCIAGANIA_DOSTEPNY
    static const OpisKursoraRozciagania tablica[4] = {
        {kKursorRozciaganiaNSSzer, kKursorRozciaganiaNSWys,
         uint16_t(kKursorRozciaganiaNSSzer / 2), uint16_t(kKursorRozciaganiaNSWys / 2), kKursorRozciaganiaNS},
        {kKursorRozciaganiaEWSzer, kKursorRozciaganiaEWWys,
         uint16_t(kKursorRozciaganiaEWSzer / 2), uint16_t(kKursorRozciaganiaEWWys / 2), kKursorRozciaganiaEW},
        {kKursorRozciaganiaNWSESzer, kKursorRozciaganiaNWSEWys,
         uint16_t(kKursorRozciaganiaNWSESzer / 2), uint16_t(kKursorRozciaganiaNWSEWys / 2), kKursorRozciaganiaNWSE},
        {kKursorRozciaganiaNESWSzer, kKursorRozciaganiaNESWWys,
         uint16_t(kKursorRozciaganiaNESWSzer / 2), uint16_t(kKursorRozciaganiaNESWWys / 2), kKursorRozciaganiaNESW},
    };
    if (typ < 3 || typ > 6) return nullptr;
    return &tablica[typ - 3];
#else
    (void)typ;
    return nullptr;
#endif
}

inline bool kursor_rozciagania_piksel(const OpisKursoraRozciagania* o, int x, int y, uint32_t* kolor) {
    if (!o || !kolor || x < 0 || y < 0 || x >= o->szer || y >= o->wys) return false;
    const uint32_t p = o->piksele[static_cast<uint32_t>(y) * o->szer + static_cast<uint32_t>(x)];
    if ((p >> 24U) == 0) return false;
    *kolor = p & 0x00FFFFFFU;
    return true;
}
