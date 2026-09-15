#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class Przeplyw : public Kontrolka {
public:
    Przeplyw() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        wypelnienie_ = Marginesy(6, 6, 6, 6);
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        Kontrolka::zaktualizuj_motyw();
    }

    int odstep_poziomy() const { return odstep_x_; }
    int odstep_pionowy() const { return odstep_y_; }
    void ustaw_odstep(int ox, int oy) {
        odstep_x_ = ox >= 0 ? ox : 0;
        odstep_y_ = oy >= 0 ? oy : 0;
        uniewaznij();
    }
    void ustaw_odstep(int o) { ustaw_odstep(o, o); }

    Rozmiar zmierz(Rozmiar dostepny) override {
        SkalaUI s = skala_dpi_;
        int pad_h = wypelnienie_.poziomo() * s;
        int pad_v = wypelnienie_.pionowo() * s;

        int max_w = dostepny.szerokosc > pad_h ? dostepny.szerokosc - pad_h : 200 * s;

        int cur_x = 0;
        int cur_y = 0;
        int line_h = 0;
        int max_line_w = 0;

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            Kontrolka* child = dzieci_[i];
            if (!child || !child->czy_widoczna()) continue;

            Marginesy cm = child->margines();
            Rozmiar sz = child->zmierz(dostepny);
            int cw = (child->preferowany_rozmiar().czy_okreslona_szerokosc())
                     ? child->preferowany_rozmiar().szerokosc * s : sz.szerokosc;
            int ch = (child->preferowany_rozmiar().czy_okreslona_wysokosc())
                     ? child->preferowany_rozmiar().wysokosc * s : sz.wysokosc;

            int tw = cw + cm.poziomo() * s;
            int th = ch + cm.pionowo() * s;

            if (cur_x > 0 && cur_x + tw > max_w) {
                if (cur_x > max_line_w) max_line_w = cur_x;
                cur_y += line_h + odstep_y_ * s;
                cur_x = 0;
                line_h = 0;
            }

            cur_x += tw + odstep_x_ * s;
            if (th > line_h) line_h = th;
        }

        if (cur_x > max_line_w) max_line_w = cur_x;
        int req_w = max_line_w + pad_h;
        int req_h = cur_y + line_h + pad_v;

        Rozmiar zadany(req_w, req_h);
        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * s;
        return zadany;
    }

    void rozmiesc(Prostokat docelowy) override {
        bool rozmiar_zmieniony = (obszar_.szerokosc != docelowy.szerokosc || obszar_.wysokosc != docelowy.wysokosc);
        obszar_ = docelowy;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        SkalaUI s = skala_dpi_;
        int pad_l = wypelnienie_.lewy * s;
        int pad_t = wypelnienie_.gorny * s;
        int pad_r = wypelnienie_.prawy * s;

        int max_w = obszar_.szerokosc - (pad_l + pad_r);
        if (max_w < 1) max_w = 1;

        int cur_x = pad_l;
        int cur_y = pad_t;
        int line_h = 0;

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            Kontrolka* child = dzieci_[i];
            if (!child || !child->czy_widoczna()) continue;

            Marginesy cm = child->margines();
            int cml = cm.lewy * s;
            int cmt = cm.gorny * s;
            int cmr = cm.prawy * s;
            int cmb = cm.dolny * s;

            Rozmiar sz = child->zmierz(Rozmiar(max_w, obszar_.wysokosc));
            int cw = (child->preferowany_rozmiar().czy_okreslona_szerokosc())
                     ? child->preferowany_rozmiar().szerokosc * s : sz.szerokosc;
            int ch = (child->preferowany_rozmiar().czy_okreslona_wysokosc())
                     ? child->preferowany_rozmiar().wysokosc * s : sz.wysokosc;

            int tw = cw + cml + cmr;
            int th = ch + cmt + cmb;

            if (cur_x > pad_l && cur_x + tw > pad_l + max_w) {
                cur_y += line_h + odstep_y_ * s;
                cur_x = pad_l;
                line_h = 0;
            }

            child->rozmiesc(Prostokat(cur_x + cml, cur_y + cmt, cw, ch));
            cur_x += tw + odstep_x_ * s;
            if (th > line_h) line_h = th;
        }

        if (rozmiar_zmieniony) {
            obsluz_zmiane_rozmiaru();
        }
    }
private:
    int odstep_x_{6};
    int odstep_y_{6};
};

} // namespace BursztynUI
