#pragma once

#include "rysowanie.h"
#include "motyw.h"

namespace BursztynUI {

// Renderer istniejacego numerycznego pola ikony. Nie przechowuje zasobow ani
// kolorow; wyglad pobiera przy kazdym rysowaniu z aktywnego BursztynMotyw.
inline void rysuj_ikone_ui(KontekstRysowania& ctx, Prostokat r,
                           uint32_t identyfikator, bool wlaczona = true,
                           SkalaUI skala = SkalaUI(1)) {
    if (identyfikator == 0 || r.szerokosc <= 2 || r.wysokosc <= 2) return;
    uint32_t kolor = wlaczona ? motyw_kolor(MOTYW_KOLOR_AKCENT)
                              : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);
    int promien = motyw_promien(MOTYW_ROG_PRZYCISK) * skala / 3;
    if (promien < 1) promien = 1;
    ctx.rysuj_zaokraglona_ramke(r, kolor, promien, skala);
    int margines = 3 * skala;
    int cx = r.x + r.szerokosc / 2;
    int cy = r.y + r.wysokosc / 2;
    if ((identyfikator & 1U) != 0 && r.wysokosc > 2 * margines)
        ctx.rysuj_linie_pionowa(cx, r.y + margines, r.wysokosc - 2 * margines, kolor);
    if (((identyfikator & 2U) != 0 || identyfikator == 1U) && r.szerokosc > 2 * margines)
        ctx.rysuj_linie_pozioma(r.x + margines, cy, r.szerokosc - 2 * margines, kolor);
}

} // namespace BursztynUI
