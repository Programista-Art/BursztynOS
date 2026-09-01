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
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
    }

    Orientacja orientacja() const { return orientacja_; }
    void ustaw_orientacje(Orientacja o) {
        if (orientacja_ != o) {
            orientacja_ = o;
            uniewaznij();
        }
    }

    bool czy_uklad_automatyczny() const { return uklad_automatyczny_; }
    void ustaw_uklad_automatyczny(bool a) {
        if (uklad_automatyczny_ != a) {
            uklad_automatyczny_ = a;
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

    Punkt przesuniecie_przewijania() const { return przesuniecie_przewijania_; }
    void ustaw_przesuniecie_przewijania(Punkt p) {
        if (przesuniecie_przewijania_ != p) {
            przesuniecie_przewijania_ = p;
            uniewaznij();
        }
    }

    Rozmiar rozmiar_zawartosci() const { return rozmiar_zawartosci_; }

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
                int child_h = (child->preferowany_rozmiar().czy_okreslona_wysokosc()) ? (child->preferowany_rozmiar().wysokosc * skala_dpi_) : zmierzony.wysokosc;
                child_h = przytnij(child_h, child->minimalny_rozmiar().wysokosc * skala_dpi_, child->maksymalny_rozmiar().wysokosc * skala_dpi_);
                suma_glowna += child_h + cm_v;

                int child_w_with_margin = ((child->preferowany_rozmiar().czy_okreslona_szerokosc()) ? (child->preferowany_rozmiar().szerokosc * skala_dpi_) : zmierzony.szerokosc) + cm_h;
                if (child_w_with_margin > maks_poprzeczna) maks_poprzeczna = child_w_with_margin;
            } else {
                int child_w = (child->preferowany_rozmiar().czy_okreslona_szerokosc()) ? (child->preferowany_rozmiar().szerokosc * skala_dpi_) : zmierzony.szerokosc;
                child_w = przytnij(child_w, child->minimalny_rozmiar().szerokosc * skala_dpi_, child->maksymalny_rozmiar().szerokosc * skala_dpi_);
                suma_glowna += child_w + cm_h;

                int child_h_with_margin = ((child->preferowany_rozmiar().czy_okreslona_wysokosc()) ? (child->preferowany_rozmiar().wysokosc * skala_dpi_) : zmierzony.wysokosc) + cm_v;
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
            rozmiar_zawartosci_ = zadany;
        } else {
            zadany.szerokosc = suma_glowna + pad_h;
            zadany.wysokosc = maks_poprzeczna + pad_v;
            rozmiar_zawartosci_ = zadany;
        }

        if (preferowany_rozmiar_.czy_okreslona_szerokosc()) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala_dpi_;
        if (preferowany_rozmiar_.czy_okreslona_wysokosc()) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala_dpi_;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala_dpi_, maksymalny_rozmiar_.szerokosc * skala_dpi_);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala_dpi_, maksymalny_rozmiar_.wysokosc * skala_dpi_);

        return zadany;
    }

    void rozmiesc(Prostokat docelowy_obszar) override {
        bool rozmiar_zmieniony = (obszar_.szerokosc != docelowy_obszar.szerokosc || obszar_.wysokosc != docelowy_obszar.wysokosc);
        obszar_ = docelowy_obszar;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        if (!uklad_automatyczny_) {
            for (size_t i = 0; i < liczba_dzieci_; ++i) {
                if (dzieci_[i]) {
                    dzieci_[i]->zaktualizuj_pozycje_ekranowa();
                }
            }
            if (rozmiar_zmieniony) {
                obsluz_zmiane_rozmiaru();
            }
            return;
        }

        int pad_l = wypelnienie_.lewy * skala_dpi_;
        int pad_t = wypelnienie_.gorny * skala_dpi_;
        int pad_r = wypelnienie_.prawy * skala_dpi_;
        int pad_b = wypelnienie_.dolny * skala_dpi_;

        int klient_x = pad_l - przesuniecie_przewijania_.x;
        int klient_y = pad_t - przesuniecie_przewijania_.y;
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
                if (child->wyrownanie_pionowe() == WyrownaniePionowe::Rozciagnij && child->preferowany_rozmiar().czy_nieokreslona_wysokosc()) {
                    liczba_rozciagliwych++;
                } else {
                    int h = (child->preferowany_rozmiar().czy_okreslona_wysokosc())
                            ? (child->preferowany_rozmiar().wysokosc * skala_dpi_)
                            : sz.wysokosc;
                    h = przytnij(h, child->minimalny_rozmiar().wysokosc * skala_dpi_, child->maksymalny_rozmiar().wysokosc * skala_dpi_);
                    suma_stala_glowna += h + cm_v;
                }
            } else {
                if (child->wyrownanie_poziome() == WyrownaniePoziome::Rozciagnij && child->preferowany_rozmiar().czy_nieokreslona_szerokosc()) {
                    liczba_rozciagliwych++;
                } else {
                    int w = (child->preferowany_rozmiar().czy_okreslona_szerokosc())
                            ? (child->preferowany_rozmiar().szerokosc * skala_dpi_)
                            : sz.szerokosc;
                    w = przytnij(w, child->minimalny_rozmiar().szerokosc * skala_dpi_, child->maksymalny_rozmiar().szerokosc * skala_dpi_);
                    suma_stala_glowna += w + cm_h;
                }
            }
        }

        int calkowity_odstep = (liczba_widocznych > 1) ? ((liczba_widocznych - 1) * odstep_ * skala_dpi_) : 0;
        int wolna_przestrzen = (orientacja_ == Orientacja::Pionowa ? klient_h : klient_w) - suma_stala_glowna - calkowity_odstep;
        if (wolna_przestrzen < 0) wolna_przestrzen = 0;

        int porcja_rozciagniecia = (liczba_rozciagliwych > 0) ? (wolna_przestrzen / liczba_rozciagliwych) : 0;
        int reszta_rozciagniecia = (liczba_rozciagliwych > 0) ? (wolna_przestrzen % liczba_rozciagliwych) : 0;
        int idx_rozciagliwy = 0;

        int biez_pozycja = (orientacja_ == Orientacja::Pionowa) ? klient_y : klient_x;

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            Kontrolka* child = dzieci_[i];
            if (!child || !child->czy_widoczna()) continue;

            Marginesy cm = child->margines();
            int cml = cm.lewy * skala_dpi_;
            int cmt = cm.gorny * skala_dpi_;
            int cmr = cm.prawy * skala_dpi_;
            int cmb = cm.dolny * skala_dpi_;
            int cm_h = (cm.lewy + cm.prawy) * skala_dpi_;
            int cm_v = (cm.gorny + cm.dolny) * skala_dpi_;

            Rozmiar sz = child->zmierz(Rozmiar(klient_w - cm_h > 0 ? klient_w - cm_h : 0,
                                               klient_h - cm_v > 0 ? klient_h - cm_v : 0));

            Prostokat obszar_dziecka;
            if (orientacja_ == Orientacja::Pionowa) {
                int child_h = 0;
                if (child->wyrownanie_pionowe() == WyrownaniePionowe::Rozciagnij && child->preferowany_rozmiar().czy_nieokreslona_wysokosc()) {
                    int extra = (idx_rozciagliwy < reszta_rozciagniecia) ? 1 : 0;
                    ++idx_rozciagliwy;
                    child_h = porcja_rozciagniecia + extra - cm_v;
                    if (child_h < 0) child_h = 0;
                } else {
                    child_h = (child->preferowany_rozmiar().czy_okreslona_wysokosc())
                              ? (child->preferowany_rozmiar().wysokosc * skala_dpi_)
                              : sz.wysokosc;
                }
                child_h = przytnij(child_h, child->minimalny_rozmiar().wysokosc * skala_dpi_, child->maksymalny_rozmiar().wysokosc * skala_dpi_);

                int child_w = 0;
                int child_x = klient_x + cml;
                int dostepne_w = klient_w - cm_h;
                if (dostepne_w < 0) dostepne_w = 0;

                if (child->wyrownanie_poziome() == WyrownaniePoziome::Rozciagnij) {
                    child_w = dostepne_w;
                } else {
                    child_w = (child->preferowany_rozmiar().czy_okreslona_szerokosc())
                              ? (child->preferowany_rozmiar().szerokosc * skala_dpi_)
                              : sz.szerokosc;
                    if (child_w > dostepne_w) child_w = dostepne_w;

                    if (child->wyrownanie_poziome() == WyrownaniePoziome::Srodek) {
                        child_x = klient_x + cml + (dostepne_w - child_w) / 2;
                    } else if (child->wyrownanie_poziome() == WyrownaniePoziome::Koniec) {
                        child_x = klient_x + klient_w - cmr - child_w;
                    }
                }
                child_w = przytnij(child_w, child->minimalny_rozmiar().szerokosc * skala_dpi_, child->maksymalny_rozmiar().szerokosc * skala_dpi_);

                obszar_dziecka = Prostokat(child_x, biez_pozycja + cmt, child_w, child_h);
                child->rozmiesc(obszar_dziecka);
                biez_pozycja += child_h + cm_v + odstep_ * skala_dpi_;
            } else {
                int child_w = 0;
                if (child->wyrownanie_poziome() == WyrownaniePoziome::Rozciagnij && child->preferowany_rozmiar().czy_nieokreslona_szerokosc()) {
                    int extra = (idx_rozciagliwy < reszta_rozciagniecia) ? 1 : 0;
                    ++idx_rozciagliwy;
                    child_w = porcja_rozciagniecia + extra - cm_h;
                    if (child_w < 0) child_w = 0;
                } else {
                    child_w = (child->preferowany_rozmiar().czy_okreslona_szerokosc())
                              ? (child->preferowany_rozmiar().szerokosc * skala_dpi_)
                              : sz.szerokosc;
                }
                child_w = przytnij(child_w, child->minimalny_rozmiar().szerokosc * skala_dpi_, child->maksymalny_rozmiar().szerokosc * skala_dpi_);

                int child_h = 0;
                int child_y = klient_y + cmt;
                int dostepne_h = klient_h - cm_v;
                if (dostepne_h < 0) dostepne_h = 0;

                if (child->wyrownanie_pionowe() == WyrownaniePionowe::Rozciagnij) {
                    child_h = dostepne_h;
                } else {
                    child_h = (child->preferowany_rozmiar().czy_okreslona_wysokosc())
                              ? (child->preferowany_rozmiar().wysokosc * skala_dpi_)
                              : sz.wysokosc;
                    if (child_h > dostepne_h) child_h = dostepne_h;

                    if (child->wyrownanie_pionowe() == WyrownaniePionowe::Srodek) {
                        child_y = klient_y + cmt + (dostepne_h - child_h) / 2;
                    } else if (child->wyrownanie_pionowe() == WyrownaniePionowe::Koniec) {
                        child_y = klient_y + klient_h - cmb - child_h;
                    }
                }
                child_h = przytnij(child_h, child->minimalny_rozmiar().wysokosc * skala_dpi_, child->maksymalny_rozmiar().wysokosc * skala_dpi_);

                obszar_dziecka = Prostokat(biez_pozycja + cml, child_y, child_w, child_h);
                child->rozmiesc(obszar_dziecka);
                biez_pozycja += child_w + cm_h + odstep_ * skala_dpi_;
            }
        }

        // Walidacja braku nakładania się dzieci wzdłuż głównej osi
        waliduj_brak_nakladania();

        if (rozmiar_zmieniony) {
            obsluz_zmiane_rozmiaru();
        }
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);
    }

private:
    void waliduj_brak_nakladania() {
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (!dzieci_[i] || !dzieci_[i]->czy_widoczna()) continue;
            for (size_t j = i + 1; j < liczba_dzieci_; ++j) {
                if (!dzieci_[j] || !dzieci_[j]->czy_widoczna()) continue;
                Prostokat ri = dzieci_[i]->obszar();
                Prostokat rj = dzieci_[j]->obszar();
                Prostokat p = ri.przeciecie(rj);
                if (!p.czy_pusty() && p.szerokosc > 0 && p.wysokosc > 0) {
                    // Wykryto przecięcie obszarów kontrolek podrzędnych
                }
            }
        }
    }

    Orientacja orientacja_{Orientacja::Pionowa};
    bool uklad_automatyczny_{true};
    int odstep_{6};
    Punkt przesuniecie_przewijania_{0, 0};
    Rozmiar rozmiar_zawartosci_{0, 0};
};

class Wiersz : public Panel {
public:
    Wiersz() : Panel(Orientacja::Pozioma) {
        styl_.normalny.tlo = 0;
        styl_.normalny.ramka = 0;
        styl_.normalny.grubosc_ramki = 0;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
    }
};

class Kolumna : public Panel {
public:
    Kolumna() : Panel(Orientacja::Pionowa) {
        styl_.normalny.tlo = 0;
        styl_.normalny.ramka = 0;
        styl_.normalny.grubosc_ramki = 0;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
    }
};

} // namespace BursztynUI
