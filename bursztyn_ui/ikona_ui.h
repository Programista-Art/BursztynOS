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
    // Native semantic power-menu glyphs; numeric IDs keep existing icon ABI.
    if (identyfikator >= 100 && identyfikator <= 103) {
        const int u = r.szerokosc / 7 > 0 ? r.szerokosc / 7 : 1;
        const int cx = r.x+r.szerokosc/2, cy=r.y+r.wysokosc/2;
        if (identyfikator == 100) { // lock
            ctx.rysuj_zaokraglona_ramke({cx-2*u,r.y,4*u,5*u},kolor,2*u,u);
            ctx.rysuj_prostokat({cx-3*u,cy,6*u,3*u},kolor);
        } else if (identyfikator == 101) { // moon, open crescent
            ctx.rysuj_linie_pionowa(r.x+u,r.y+2*u,3*u,kolor);
            ctx.rysuj_linie_pionowa(r.x+2*u,r.y+u,u,kolor);
            ctx.rysuj_linie_pozioma(r.x+2*u,r.y+5*u,4*u,kolor);
            ctx.rysuj_linie_pozioma(r.x+3*u,r.y+u,2*u,kolor);
            ctx.rysuj_linie_pionowa(r.x+5*u,r.y+4*u,u,kolor);
        } else { // power / restart
            ctx.rysuj_zaokraglona_ramke({r.x+u,r.y+u,5*u,5*u},kolor,2*u,u);
            if(identyfikator==102)ctx.rysuj_linie_pionowa(cx,r.y,3*u,kolor);
            else {ctx.rysuj_linie_pozioma(cx,r.y+u,3*u,kolor);ctx.rysuj_linie_pionowa(r.x+6*u,r.y+u,2*u,kolor);}
        }
        return;
    }
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
