#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class Panel : public Kontrolka {
public:
    Panel(Orientacja orientacja = Orientacja::Pionowa)
        : orientacja_(orientacja) {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
    }

    Orientacja orientacja() const { return orientacja_; }
    void ustaw_orientacje(Orientacja o) {
        if (orientacja_ != o) {
            orientacja_ = o;
            uniewaznij();
        }
    }

    int odstep() const { return odstep_; }
    void ustaw_odstep(int o) {
        if (odstep_ != o) {
            odstep_ = (o < 0) ? 0 : o;
            uniewaznij();
        }
    }

    StylKontrolki& styl() { return styl_; }
    void ustaw_styl(const StylKontrolki& s) { styl_ = s; uniewaznij(); }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        int pad_h = wypelnienie_.poziomo() * skala_dpi_;
        int pad_v = wypelnienie_.pionowo() * skala_dpi_;
        int mar_h = margines_.poziomo() * skala_dpi_;
        int mar_v = margines_.pionowo() * skala_dpi_;

        int wewn_w = dostepny_rozmiar.szerokosc - mar_h - pad_h;
        int wewn_h = dostepny_rozmiar.wysokosc - mar_v - pad_v;
        if (wewn_w < 0) wewn_w = 0;
        if (wewn_h < 0) wewn_h = 0;

        int suma_glowna = 0;
        int maks_poprzeczna = 0;
        int liczba_widocznych = 0;

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            Kontrolka* child = dzieci_[i];
            if (!child || !child->czy_widoczna()) continue;

            liczba_widocznych++;
            Marginesy cm = child->margines();
            int cm_h = cm.poziomo() * skala_dpi_;
            int cm_v = cm.pionowo() * skala_dpi_;

            Rozmiar child_avail(wewn_w - cm_h > 0 ? wewn_w - cm_h : 0,
                                wewn_h - cm_v > 0 ? wewn_h - cm_v : 0);
            Rozmiar zmierzony = child->zmierz(child_avail);

            if (orientacja_ == Orientacja::Pionowa) {
                suma_glowna += zmierzony.wysokosc + cm_v;
                int child_w_with_margin = zmierzony.szerokosc + cm_h;
                if (child_w_with_margin > maks_poprzeczna) maks_poprzeczna = child_w_with_margin;
            } else {
                suma_glowna += zmierzony.szerokosc + cm_h;
                int child_h_with_margin = zmierzony.wysokosc + cm_v;
                if (child_h_with_margin > maks_poprzeczna) maks_poprzeczna = child_h_with_margin;
            }
        }

        if (liczba_widocznych > 1) {
            suma_glowna += (liczba_widocznych - 1) * odstep_ * skala_dpi_;
        }

        Rozmiar zadany;
        if (orientacja_ == Orientacja::Pionowa) {
            zadany.szerokosc = maks_poprzeczna + pad_h;
            zadany.wysokosc = suma_glowna + pad_v;
        } else {
            zadany.szerokosc = suma_glowna + pad_h;
            zadany.wysokosc = maks_poprzeczna + pad_v;
        }

        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala_dpi_;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala_dpi_;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala_dpi_, maksymalny_rozmiar_.szerokosc * skala_dpi_);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala_dpi_, maksymalny_rozmiar_.wysokosc * skala_dpi_);

        return zadany;
    }

    void rozmiesc(Prostokat docelowy_obszar) override {
        obszar_ = docelowy_obszar;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz.x + obszar_.x, rodzic_poz.y + obszar_.y,
                                   obszar_.szerokosc, obszar_.wysokosc);

        int pad_l = wypelnienie_.lewy * skala_dpi_;
        int pad_t = wypelnienie_.gorny * skala_dpi_;
        int pad_r = wypelnienie_.prawy * skala_dpi_;
        int pad_b = wypelnienie_.dolny * skala_dpi_;

        int klient_x = pad_l;
        int klient_y = pad_t;
        int klient_w = obszar_.szerokosc - (pad_l + pad_r);
        int klient_h = obszar_.wysokosc - (pad_t + pad_b);
        if (klient_w < 0) klient_w = 0;
        if (klient_h < 0) klient_h = 0;

        int liczba_widocznych = 0;
        int suma_stala_glowna = 0;
        int liczba_rozciagliwych = 0;

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            Kontrolka* child = dzieci_[i];
            if (!child || !child->czy_widoczna()) continue;
            liczba_widocznych++;

            Marginesy cm = child->margines();
            int cm_h = cm.poziomo() * skala_dpi_;
            int cm_v = cm.pionowo() * skala_dpi_;

            Rozmiar sz = child->zmierz(Rozmiar(klient_w - cm_h > 0 ? klient_w - cm_h : 0,
                                               klient_h - cm_v > 0 ? klient_h - cm_v : 0));

            if (orientacja_ == Orientacja::Pionowa) {
                if (child->wyrownanie_pionowe() == WyrownaniePionowe::Rozciagnij) {
                    liczba_rozciagliwych++;
                } else {
                    int h = sz.wysokosc;
                    if (child->preferowany_rozmiar().wysokosc >= 0) {
                        h = child->preferowany_rozmiar().wysokosc * skala_dpi_;
                    }
                    h = przytnij(h, child->minimalny_rozmiar().wysokosc * skala_dpi_, child->maksymalny_rozmiar().wysokosc * skala_dpi_);
                    suma_stala_glowna += h + cm_v;
                }
            } else {
                if (child->wyrownanie_poziome() == WyrownaniePoziome::Rozciagnij) {
                    liczba_rozciagliwych++;
                } else {
                    int w = sz.szerokosc;
                    if (child->preferowany_rozmiar().szerokosc >= 0) {
                        w = child->preferowany_rozmiar().szerokosc * skala_dpi_;
                    }
                    w = przytnij(w, child->minimalny_rozmiar().szerokosc * skala_dpi_, child->maksymalny_rozmiar().szerokosc * skala_dpi_);
                    suma_stala_glowna += w + cm_h;
                }
            }
        }

        int calkowity_odstep = (liczba_widocznych > 1) ? (liczba_widocznych - 1) * odstep_ * skala_dpi_ : 0;
        int dostepne_na_rozciagniecie = (orientacja_ == Orientacja::Pionowa)
                                       ? (klient_h - suma_stala_glowna - calkowity_odstep)
                                       : (klient_w - suma_stala_glowna - calkowity_odstep);
        if (dostepne_na_rozciagniecie < 0) dostepne_na_rozciagniecie = 0;

        int porcja_rozciagniecia = (liczba_rozciagliwych > 0) ? (dostepne_na_rozciagniecie / liczba_rozciagliwych) : 0;

        int biez_pozycja = (orientacja_ == Orientacja::Pionowa) ? klient_y : klient_x;

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            Kontrolka* child = dzieci_[i];
            if (!child || !child->czy_widoczna()) continue;

            Marginesy cm = child->margines();
            int cml = cm.lewy * skala_dpi_;
            int cmt = cm.gorny * skala_dpi_;
            int cmr = cm.prawy * skala_dpi_;
            int cmb = cm.dolny * skala_dpi_;
            int cm_h = cml + cmr;
            int cm_v = cmt + cmb;

            Rozmiar sz = child->zmierz(Rozmiar(klient_w - cm_h > 0 ? klient_w - cm_h : 0,
                                               klient_h - cm_v > 0 ? klient_h - cm_v : 0));

            Prostokat obszar_dziecka;
            if (orientacja_ == Orientacja::Pionowa) {
                int child_h = 0;
                if (child->wyrownanie_pionowe() == WyrownaniePionowe::Rozciagnij) {
                    child_h = porcja_rozciagniecia - cm_v;
                    if (child_h < 0) child_h = 0;
                } else {
                    child_h = sz.wysokosc;
                    if (child->preferowany_rozmiar().wysokosc >= 0) {
                        child_h = child->preferowany_rozmiar().wysokosc * skala_dpi_;
                    }
                }
                child_h = przytnij(child_h, child->minimalny_rozmiar().wysokosc * skala_dpi_, child->maksymalny_rozmiar().wysokosc * skala_dpi_);

                int child_w = 0;
                int child_x = klient_x + cml;
                int dostepne_w = klient_w - cm_h;
                if (dostepne_w < 0) dostepne_w = 0;

                if (child->wyrownanie_poziome() == WyrownaniePoziome::Rozciagnij) {
                    child_w = dostepne_w;
                } else {
                    child_w = sz.szerokosc;
                    if (child->preferowany_rozmiar().szerokosc >= 0) {
                        child_w = child->preferowany_rozmiar().szerokosc * skala_dpi_;
                    }
                    if (child_w > dostepne_w) child_w = dostepne_w;

                    if (child->wyrownanie_poziome() == WyrownaniePoziome::Srodek) {
                        child_x = klient_x + cml + (dostepne_w - child_w) / 2;
                    } else if (child->wyrownanie_poziome() == WyrownaniePoziome::Koniec) {
                        child_x = klient_x + klient_w - cmr - child_w;
                    }
                }
                child_w = przytnij(child_w, child->minimalny_rozmiar().szerokosc * skala_dpi_, child->maksymalny_rozmiar().szerokosc * skala_dpi_);

                obszar_dziecka = Prostokat(child_x, biez_pozycja + cmt, child_w, child_h);
                biez_pozycja += child_h + cm_v + odstep_ * skala_dpi_;
            } else {
                int child_w = 0;
                if (child->wyrownanie_poziome() == WyrownaniePoziome::Rozciagnij) {
                    child_w = porcja_rozciagniecia - cm_h;
                    if (child_w < 0) child_w = 0;
                } else {
                    child_w = sz.szerokosc;
                    if (child->preferowany_rozmiar().szerokosc >= 0) {
                        child_w = child->preferowany_rozmiar().szerokosc * skala_dpi_;
                    }
                }
                child_w = przytnij(child_w, child->minimalny_rozmiar().szerokosc * skala_dpi_, child->maksymalny_rozmiar().szerokosc * skala_dpi_);

                int child_h = 0;
                int child_y = klient_y + cmt;
                int dostepne_h = klient_h - cm_v;
                if (dostepne_h < 0) dostepne_h = 0;

                if (child->wyrownanie_pionowe() == WyrownaniePionowe::Rozciagnij) {
                    child_h = dostepne_h;
                } else {
                    child_h = sz.wysokosc;
                    if (child->preferowany_rozmiar().wysokosc >= 0) {
                        child_h = child->preferowany_rozmiar().wysokosc * skala_dpi_;
                    }
                    if (child_h > dostepne_h) child_h = dostepne_h;

                    if (child->wyrownanie_pionowe() == WyrownaniePionowe::Srodek) {
                        child_y = klient_y + cmt + (dostepne_h - child_h) / 2;
                    } else if (child->wyrownanie_pionowe() == WyrownaniePionowe::Koniec) {
                        child_y = klient_y + klient_h - cmb - child_h;
                    }
                }
                child_h = przytnij(child_h, child->minimalny_rozmiar().wysokosc * skala_dpi_, child->maksymalny_rozmiar().wysokosc * skala_dpi_);

                obszar_dziecka = Prostokat(biez_pozycja + cml, child_y, child_w, child_h);
                biez_pozycja += child_w + cm_h + odstep_ * skala_dpi_;
            }

            child->rozmiesc(obszar_dziecka);
        }
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        const StylStanu& s = czy_wlaczona_ ? styl_.normalny : styl_.wylaczony;
        if (s.tlo != 0) {
            if (s.promien_zaokraglenia > 0) {
                ctx.rysuj_zaokraglony_prostokat(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc), s.tlo, s.promien_zaokraglenia * skala_dpi_);
            } else {
                ctx.rysuj_prostokat(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc), s.tlo);
            }
        }
        if (s.ramka != 0 && s.grubosc_ramki > 0) {
            if (s.promien_zaokraglenia > 0) {
                ctx.rysuj_zaokraglona_ramke(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc), s.ramka, s.promien_zaokraglenia * skala_dpi_, s.grubosc_ramki * skala_dpi_);
            } else {
                ctx.rysuj_ramke(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc), s.ramka, s.grubosc_ramki * skala_dpi_);
            }
        }
    }

private:
    Orientacja orientacja_{Orientacja::Pionowa};
    int odstep_{6};
    StylKontrolki styl_;
};

class Wiersz : public Panel {
public:
    Wiersz() : Panel(Orientacja::Pozioma) {}
};

class Kolumna : public Panel {
public:
    Kolumna() : Panel(Orientacja::Pionowa) {}
};

} // namespace BursztynUI
