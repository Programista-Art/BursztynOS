#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class KontekstRysowania {
public:
    KontekstRysowania() {
        stos_przyciecia_[0] = Prostokat(0, 0, 4096, 4096);
        stos_przesuniecia_[0] = Punkt(0, 0);
    }

    void dodaj_przesuniecie(Punkt pt) {
        if (glebokosc_przesuniecia_ < MAKS_STOS - 1) {
            glebokosc_przesuniecia_++;
            stos_przesuniecia_[glebokosc_przesuniecia_] =
                stos_przesuniecia_[glebokosc_przesuniecia_ - 1] + pt;
        }
    }

    void zdejmij_przesuniecie() {
        if (glebokosc_przesuniecia_ > 0) {
            glebokosc_przesuniecia_--;
        }
    }

    Punkt pobierz_przesuniecie() const {
        return stos_przesuniecia_[glebokosc_przesuniecia_];
    }

    void dodaj_przyciecie(Prostokat r) {
        if (glebokosc_przyciecia_ < MAKS_STOS - 1) {
            glebokosc_przyciecia_++;
            Punkt biez_przes = pobierz_przesuniecie();
            Prostokat swiat_r(r.x + biez_przes.x, r.y + biez_przes.y, r.szerokosc, r.wysokosc);
            stos_przyciecia_[glebokosc_przyciecia_] =
                stos_przyciecia_[glebokosc_przyciecia_ - 1].przeciecie(swiat_r);
        }
    }

    void zdejmij_przyciecie() {
        if (glebokosc_przyciecia_ > 0) {
            glebokosc_przyciecia_--;
        }
    }

    Prostokat pobierz_przyciecie() const {
        return stos_przyciecia_[glebokosc_przyciecia_];
    }

    void rysuj_prostokat(Prostokat r, uint32_t kolor) {
        if (r.czy_pusty()) return;
        Punkt off = pobierz_przesuniecie();
        Prostokat swiat_r(r.x + off.x, r.y + off.y, r.szerokosc, r.wysokosc);
        Prostokat przyciety = swiat_r.przeciecie(pobierz_przyciecie());
        if (!przyciety.czy_pusty()) {
            gui_rysuj_prostokat(przyciety.x, przyciety.y,
                                przyciety.szerokosc, przyciety.wysokosc, kolor);
        }
    }

    void rysuj_ramke(Prostokat r, uint32_t kolor, int grubosc = 1) {
        if (grubosc <= 0 || r.czy_pusty()) return;
        if (r.szerokosc <= 2 * grubosc || r.wysokosc <= 2 * grubosc) {
            rysuj_prostokat(r, kolor);
            return;
        }
        // Góra
        rysuj_prostokat(Prostokat(r.x, r.y, r.szerokosc, grubosc), kolor);
        // Dół
        rysuj_prostokat(Prostokat(r.x, r.y + r.wysokosc - grubosc, r.szerokosc, grubosc), kolor);
        // Lewo
        rysuj_prostokat(Prostokat(r.x, r.y + grubosc, grubosc, r.wysokosc - 2 * grubosc), kolor);
        // Prawo
        rysuj_prostokat(Prostokat(r.x + r.szerokosc - grubosc, r.y + grubosc, grubosc, r.wysokosc - 2 * grubosc), kolor);
    }

    void rysuj_zaokraglony_prostokat(Prostokat r, uint32_t kolor, int promien = 2) {
        if (r.czy_pusty()) return;
        if (promien <= 0 || r.szerokosc < 6 || r.wysokosc < 6) {
            rysuj_prostokat(r, kolor);
            return;
        }
        // Środek
        rysuj_prostokat(Prostokat(r.x + promien, r.y, r.szerokosc - 2 * promien, r.wysokosc), kolor);
        // Boki
        rysuj_prostokat(Prostokat(r.x, r.y + promien, promien, r.wysokosc - 2 * promien), kolor);
        rysuj_prostokat(Prostokat(r.x + r.szerokosc - promien, r.y + promien, promien, r.wysokosc - 2 * promien), kolor);
    }

    void rysuj_zaokraglona_ramke(Prostokat r, uint32_t kolor, int promien = 2, int grubosc = 1) {
        if (r.czy_pusty()) return;
        if (promien <= 0 || r.szerokosc < 6 || r.wysokosc < 6) {
            rysuj_ramke(r, kolor, grubosc);
            return;
        }
        // Górna krawędź
        rysuj_prostokat(Prostokat(r.x + promien, r.y, r.szerokosc - 2 * promien, grubosc), kolor);
        // Dolna krawędź
        rysuj_prostokat(Prostokat(r.x + promien, r.y + r.wysokosc - grubosc, r.szerokosc - 2 * promien, grubosc), kolor);
        // Lewa krawędź
        rysuj_prostokat(Prostokat(r.x, r.y + promien, grubosc, r.wysokosc - 2 * promien), kolor);
        // Prawa krawędź
        rysuj_prostokat(Prostokat(r.x + r.szerokosc - grubosc, r.y + promien, grubosc, r.wysokosc - 2 * promien), kolor);
        // Narożniki
        if (promien > 1) {
            rysuj_prostokat(Prostokat(r.x + 1, r.y + 1, grubosc, grubosc), kolor);
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - 1 - grubosc, r.y + 1, grubosc, grubosc), kolor);
            rysuj_prostokat(Prostokat(r.x + 1, r.y + r.wysokosc - 1 - grubosc, grubosc, grubosc), kolor);
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - 1 - grubosc, r.y + r.wysokosc - 1 - grubosc, grubosc, grubosc), kolor);
        }
    }

    void rysuj_tekst(Punkt pt, const char* tekst, uint32_t kolor, int skala = 1) {
        if (!tekst || tekst[0] == '\0') return;
        Punkt off = pobierz_przesuniecie();
        int tx = pt.x + off.x;
        int ty = pt.y + off.y;
        gui_wypisz_tekst_kolor_skala(tx, ty, kolor, skala, tekst);
    }

    void rysuj_tekst_wysrodkowany(Prostokat r, const char* tekst, uint32_t kolor, int skala = 1) {
        if (!tekst || tekst[0] == '\0' || r.czy_pusty()) return;
        Rozmiar rz = zmierz_tekst(tekst, skala);
        int tx = r.x + (r.szerokosc - rz.szerokosc) / 2;
        int ty = r.y + (r.wysokosc - rz.wysokosc) / 2;
        if (tx < r.x) tx = r.x;
        if (ty < r.y) ty = r.y;
        rysuj_tekst(Punkt(tx, ty), tekst, kolor, skala);
    }

    Rozmiar zmierz_tekst(const char* tekst, int skala = 1) const {
        if (!tekst || tekst[0] == '\0') return {0, 0};
        int w = oblicz_szerokosc_tekstu(tekst, skala);
        int h = gui_pobierz_wysokosc_fontu() * skala;
        return {w, h};
    }

    int pobierz_wysokosc_fontu(int skala = 1) const {
        return gui_pobierz_wysokosc_fontu() * skala;
    }

private:
    static constexpr size_t MAKS_STOS = 16;
    Prostokat stos_przyciecia_[MAKS_STOS];
    Punkt stos_przesuniecia_[MAKS_STOS];
    size_t glebokosc_przyciecia_{0};
    size_t glebokosc_przesuniecia_{0};
};

} // namespace BursztynUI
