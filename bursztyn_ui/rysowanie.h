#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "czcionka.h"
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
        int max_p = (r.szerokosc < r.wysokosc ? r.szerokosc : r.wysokosc) / 2;
        if (promien > max_p) promien = max_p;
        if (promien <= 0 || r.szerokosc < 4 || r.wysokosc < 4) {
            rysuj_prostokat(r, kolor);
            return;
        }

        int mid_w = r.szerokosc - 2 * promien;
        int mid_h = r.wysokosc - 2 * promien;

        // Środkowa kolumna (pełna wysokość)
        if (mid_w > 0) {
            rysuj_prostokat(Prostokat(r.x + promien, r.y, mid_w, r.wysokosc), kolor);
        }
        // Lewy i prawy bok
        if (mid_h > 0) {
            rysuj_prostokat(Prostokat(r.x, r.y + promien, promien, mid_h), kolor);
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - promien, r.y + promien, promien, mid_h), kolor);
        }
        // Zaokrąglone narożniki / zaokrąglone kapsułki
        for (int dy = 0; dy < promien; ++dy) {
            int inset = promien - 1 - dy;
            if (inset < 0) inset = 0;
            int line_w = r.szerokosc - 2 * inset;
            if (line_w > 0) {
                // Górna linia
                rysuj_prostokat(Prostokat(r.x + inset, r.y + dy, line_w, 1), kolor);
                // Dolna linia
                rysuj_prostokat(Prostokat(r.x + inset, r.y + r.wysokosc - 1 - dy, line_w, 1), kolor);
            }
        }
    }

    void rysuj_zaokraglona_ramke(Prostokat r, uint32_t kolor, int promien = 2, int grubosc = 1) {
        if (r.czy_pusty() || grubosc <= 0) return;
        int max_p = (r.szerokosc < r.wysokosc ? r.szerokosc : r.wysokosc) / 2;
        if (promien > max_p) promien = max_p;
        if (promien <= 0 || r.szerokosc < 4 || r.wysokosc < 4) {
            rysuj_ramke(r, kolor, grubosc);
            return;
        }

        int mid_w = r.szerokosc - 2 * promien;
        int mid_h = r.wysokosc - 2 * promien;

        // Górna i dolna krawędź
        if (mid_w > 0) {
            rysuj_prostokat(Prostokat(r.x + promien, r.y, mid_w, grubosc), kolor);
            rysuj_prostokat(Prostokat(r.x + promien, r.y + r.wysokosc - grubosc, mid_w, grubosc), kolor);
        }
        // Lewa i prawa krawędź
        if (mid_h > 0) {
            rysuj_prostokat(Prostokat(r.x, r.y + promien, grubosc, mid_h), kolor);
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - grubosc, r.y + promien, grubosc, mid_h), kolor);
        }
        // Zaokrąglone narożniki
        for (int dy = 0; dy < promien; ++dy) {
            int inset = promien - 1 - dy;
            if (inset < 0) inset = 0;
            // Lewy róg (góra/dół)
            rysuj_prostokat(Prostokat(r.x + inset, r.y + dy, grubosc, 1), kolor);
            rysuj_prostokat(Prostokat(r.x + inset, r.y + r.wysokosc - 1 - dy, grubosc, 1), kolor);
            // Prawy róg (góra/dół)
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - inset - grubosc, r.y + dy, grubosc, 1), kolor);
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - inset - grubosc, r.y + r.wysokosc - 1 - dy, grubosc, 1), kolor);
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

    void rysuj_linie_pozioma(int x, int y, int szer, uint32_t kolor) {
        if (szer <= 0) return;
        rysuj_prostokat(Prostokat(x, y, szer, 1), kolor);
    }

    void rysuj_linie_pionowa(int x, int y, int wys, uint32_t kolor) {
        if (wys <= 0) return;
        rysuj_prostokat(Prostokat(x, y, 1, wys), kolor);
    }

    void rysuj_zaznaczenie(Prostokat r, uint32_t kolor) {
        rysuj_prostokat(r, kolor);
    }

    void rysuj_tekst_obciety(Punkt pt, const char* tekst, uint32_t kolor, int max_szerokosc, int skala = 1) {
        if (!tekst || tekst[0] == '\0' || max_szerokosc <= 0) return;
        dodaj_przyciecie(Prostokat(pt.x, pt.y, max_szerokosc, pobierz_wysokosc_czcionki(skala) + 4));
        rysuj_tekst(pt, tekst, kolor, skala);
        zdejmij_przyciecie();
    }

    Rozmiar zmierz_tekst(const char* tekst, int skala = 1) const {
        return CzcionkaUI::zmierz_rozmiar(tekst, skala);
    }

    int pobierz_wysokosc_czcionki(int skala = 1) const {
        return CzcionkaUI::pobierz_wysokosc(skala);
    }

private:
    static constexpr size_t MAKS_STOS = 16;
    Prostokat stos_przyciecia_[MAKS_STOS];
    Punkt stos_przesuniecia_[MAKS_STOS];
    size_t glebokosc_przyciecia_{0};
    size_t glebokosc_przesuniecia_{0};
};

} // namespace BursztynUI
