#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "czcionka.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class KontekstRysowania {
public:
    static inline bool g_tlum_rysowanie_globalne = false;

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

    // A packed, owner-allocated dirty-region target. BWS57 submits it only
    // after the complete subtree and its popup overlays have been painted.
    void ustaw_cel(uint32_t* pixels, Prostokat bounds) { cel_ = pixels; granice_celu_ = bounds; }
    bool przecina_klip(Prostokat r) const {
        Punkt p = pobierz_przesuniecie();
        return Prostokat(r.x+p.x, r.y+p.y, r.szerokosc, r.wysokosc).przecina(pobierz_przyciecie());
    }
    Prostokat lokalny_klip() const {
        Prostokat r = pobierz_przyciecie(); Punkt p = pobierz_przesuniecie();
        r.x -= p.x; r.y -= p.y; return r;
    }
    // Opt-in for opaque app surfaces; popup cutouts retain their zero-pixel contract.
    void ustaw_nieprzezroczyste_kolory(bool enabled) { nieprzezroczyste_kolory_ = enabled; }
    void rysuj_prostokat(Prostokat r, uint32_t kolor) {
        if (nieprzezroczyste_kolory_) kolor |= 0xFF000000U;
        if (r.czy_pusty()) return;
        Punkt off = pobierz_przesuniecie();
        Prostokat swiat_r(r.x + off.x, r.y + off.y, r.szerokosc, r.wysokosc);
        Prostokat przyciety = swiat_r.przeciecie(pobierz_przyciecie());
        if (cel_) {
            przyciety = przyciety.przeciecie(granice_celu_);
            for (int y = przyciety.y; !przyciety.czy_pusty() && y < przyciety.dolny(); ++y) {
                uint32_t* row = cel_ + static_cast<size_t>(y-granice_celu_.y)*granice_celu_.szerokosc;
                for (int x = przyciety.x; x < przyciety.prawy(); ++x) row[x-granice_celu_.x] = kolor;
            }
            return;
        }
        if (!przyciety.czy_pusty()) {
            if (!g_tlum_rysowanie_globalne) {
                gui_rysuj_prostokat(przyciety.x, przyciety.y,
                                    przyciety.szerokosc, przyciety.wysokosc, kolor);
            }
        }
    }

    void rysuj_wiersz_ekranu(int x, int y, int w, const uint32_t* pixels) {
        if (cel_) {
            Prostokat r = Prostokat(x,y,w,1).przeciecie(granice_celu_).przeciecie(pobierz_przyciecie());
            if (r.czy_pusty()) return;
            uint32_t* out = cel_ + static_cast<size_t>(y-granice_celu_.y)*granice_celu_.szerokosc;
            for (int px=r.x;px<r.prawy();++px) if (pixels[px-x]) out[px-granice_celu_.x]=pixels[px-x];
        } else if (!g_tlum_rysowanie_globalne) gui_blit(x,y,w,1,pixels);
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
        // Zaokrąglone narożniki (kolista arytmetyka calkowitoliczbowa)
        for (int dy = 0; dy < promien; ++dy) {
            int inset = motyw_promien_inset(promien, dy);
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
            int inset = motyw_promien_inset(promien, dy);
            // Lewy róg (góra/dół)
            rysuj_prostokat(Prostokat(r.x + inset, r.y + dy, grubosc, 1), kolor);
            rysuj_prostokat(Prostokat(r.x + inset, r.y + r.wysokosc - 1 - dy, grubosc, 1), kolor);
            // Prawy róg (góra/dół)
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - inset - grubosc, r.y + dy, grubosc, 1), kolor);
            rysuj_prostokat(Prostokat(r.x + r.szerokosc - inset - grubosc, r.y + r.wysokosc - 1 - dy, grubosc, 1), kolor);
        }
    }

    void rysuj_tekst(Punkt pt, const char* tekst, uint32_t kolor, SkalaUI skala = 1) {
        if (!tekst || tekst[0] == '\0') return;
        if (skala < 1) skala = 1;
        Punkt off = pobierz_przesuniecie();
        int tx = pt.x + off.x;
        int ty = pt.y + off.y;
        Prostokat granice(tx, ty, CzcionkaUI::zmierz_tekst(tekst, skala),
                          CzcionkaUI::pobierz_wysokosc(skala));
        Prostokat clip = pobierz_przyciecie();
        Prostokat widoczny = granice.przeciecie(clip);
        if (widoczny.czy_pusty()) return;

        // Zwykly tekst, ktory w calosci miesci sie w klipie, zachowuje szybki
        // syscall renderera. Tylko tekst przecinajacy krawedz wymaga lokalnego
        // renderowania bitmapy; syscall 20 nie ma parametru przyciecia.
        if (widoczny == granice && !cel_ && skala.procent()%100==0) {
            if (!g_tlum_rysowanie_globalne) {
                gui_wypisz_tekst_kolor_skala(tx, ty, kolor, skala, tekst);
            }
            dodaj_pomiar_tekstu(widoczny);
            return;
        }

        int x_znaku = pt.x;
        size_t i = 0;
        while (tekst[i] != '\0') {
            uint32_t cp = 0;
            size_t zuzyte = CzcionkaUI::dekoduj_utf8(tekst, i, &cp);
            if (zuzyte == 0) break;
            int szerokosc = CzcionkaUI::szerokosc_znaku(cp);
            int swiat_x = x_znaku + off.x;
            int prawy_glifu = swiat_x + szerokosc * skala;
            if (prawy_glifu > clip.x && swiat_x < clip.prawy()) {
                const uint16_t* bitmapa = CzcionkaUI::bitmapa_znaku(cp);
                for (int y = 0; y < CzcionkaUI::WYSOKOSC; ++y) {
                    int poczatek = -1;
                    for (int x = 0; x <= szerokosc; ++x) {
                        bool piksel = x < szerokosc &&
                            (bitmapa[y] & (1U << (15 - x))) != 0;
                        if (piksel && poczatek < 0) poczatek = x;
                        if (!piksel && poczatek >= 0) {
                            Prostokat seria(x_znaku + poczatek * skala,
                                             pt.y + y * skala,
                                             x * skala - poczatek * skala, (y+1)*skala-y*skala);
                            Prostokat seria_swiat(seria.x + off.x,
                                                  seria.y + off.y,
                                                  seria.szerokosc,
                                                  seria.wysokosc);
                            Prostokat seria_clip = seria_swiat.przeciecie(clip);
                            if (!seria_clip.czy_pusty()) {
                                rysuj_prostokat(seria, kolor);
                                dodaj_pomiar_tekstu(seria_clip);
                            }
                            poczatek = -1;
                        }
                    }
                }
            }
            x_znaku += CzcionkaUI::postep_znaku(cp, skala);
            if (x_znaku + off.x >= clip.prawy()) break;
            i += zuzyte;
        }
    }

    void wyczysc_pomiar_tekstu() { obszar_narysowanego_tekstu_ = Prostokat(); }
    Prostokat obszar_narysowanego_tekstu() const {
        return obszar_narysowanego_tekstu_;
    }

    void rysuj_tekst_wysrodkowany(Prostokat r, const char* tekst, uint32_t kolor, SkalaUI skala = 1) {
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

    void rysuj_tekst_obciety(Punkt pt, const char* tekst, uint32_t kolor, int max_szerokosc, SkalaUI skala = 1) {
        if (!tekst || tekst[0] == '\0' || max_szerokosc <= 0) return;
        dodaj_przyciecie(Prostokat(pt.x, pt.y, max_szerokosc, pobierz_wysokosc_czcionki(skala) + 4));
        rysuj_tekst(pt, tekst, kolor, skala);
        zdejmij_przyciecie();
    }

    Rozmiar zmierz_tekst(const char* tekst, SkalaUI skala = 1) const {
        return CzcionkaUI::zmierz_rozmiar(tekst, skala);
    }

    int pobierz_wysokosc_czcionki(SkalaUI skala = 1) const {
        return CzcionkaUI::pobierz_wysokosc(skala);
    }

private:
    void dodaj_pomiar_tekstu(Prostokat r) {
        obszar_narysowanego_tekstu_ = obszar_narysowanego_tekstu_.suma(r);
    }

    static constexpr size_t MAKS_STOS = 16;
    bool nieprzezroczyste_kolory_{false};
    uint32_t* cel_{nullptr};
    Prostokat granice_celu_{};
    Prostokat stos_przyciecia_[MAKS_STOS];
    Punkt stos_przesuniecia_[MAKS_STOS];
    size_t glebokosc_przyciecia_{0};
    size_t glebokosc_przesuniecia_{0};
    Prostokat obszar_narysowanego_tekstu_{};
};

} // namespace BursztynUI
