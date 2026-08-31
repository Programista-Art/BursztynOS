#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class Etykieta : public Kontrolka {
public:
    Etykieta(const char* poczatkowy_tekst = "") {
        styl_ = Motyw::pobierz_domyslny().styl_etykiety;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        ustaw_tekst(poczatkowy_tekst);
    }

    int skala_czcionki() const { return skala_czcionki_; }
    void ustaw_skale_czcionki(int s) {
        skala_czcionki_ = (s < 1) ? 1 : s;
        uniewaznij();
    }

    WyrownaniePoziome wyrownanie_tekstu() const { return wyrownanie_tekstu_; }
    void ustaw_wyrownanie_tekstu(WyrownaniePoziome a) {
        wyrownanie_tekstu_ = a;
        uniewaznij();
    }

    WyrownaniePionowe wyrownanie_pionowe_tekstu() const { return wyrownanie_pionowe_tekstu_; }
    void ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe a) {
        wyrownanie_pionowe_tekstu_ = a;
        uniewaznij();
    }

    bool czy_autodopasowanie() const { return czy_autodopasowanie_; }
    void ustaw_autodopasowanie(bool a) {
        czy_autodopasowanie_ = a;
        uniewaznij();
    }

    int maksymalna_szerokosc() const { return maksymalna_szerokosc_; }
    void ustaw_maksymalna_szerokosc(int mw) {
        maksymalna_szerokosc_ = mw;
        uniewaznij();
    }

    bool czy_zawijanie() const { return czy_zawijanie_; }
    void ustaw_zawijanie(bool z) {
        czy_zawijanie_ = z;
        uniewaznij();
    }

    bool czy_zaznaczalna() const { return czy_zaznaczalna_; }
    void ustaw_zaznaczalna(bool s) {
        czy_zaznaczalna_ = s;
        ustaw_mozliwosc_fokusu(s);
    }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        int skala = skala_czcionki_ * skala_dpi_;
        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala);
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala);

        if (maksymalna_szerokosc_ > 0 && txt_w > maksymalna_szerokosc_ * skala_dpi_) {
            txt_w = maksymalna_szerokosc_ * skala_dpi_;
        }

        Rozmiar zadany(txt_w + wypelnienie_.poziomo() * skala_dpi_,
                       txt_h + wypelnienie_.pionowo() * skala_dpi_);

        if (!czy_autodopasowanie_) {
            if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala_dpi_;
            if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala_dpi_;
        }

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala_dpi_, maksymalny_rozmiar_.szerokosc * skala_dpi_);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala_dpi_, maksymalny_rozmiar_.wysokosc * skala_dpi_);

        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        if (tekst_[0] == '\0') return;

        int skala = skala_czcionki_ * skala_dpi_;
        uint32_t col = s.tekst;

        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala);
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala);

        int pad_l = wypelnienie_.lewy * skala_dpi_;
        int pad_t = wypelnienie_.gorny * skala_dpi_;
        int pad_r = wypelnienie_.prawy * skala_dpi_;
        int pad_b = wypelnienie_.dolny * skala_dpi_;

        int klient_w = obszar_.szerokosc - (pad_l + pad_r);
        int klient_h = obszar_.wysokosc - (pad_t + pad_b);
        if (klient_w <= 0 || klient_h <= 0) return;

        int tx = pad_l;
        if (wyrownanie_tekstu_ == WyrownaniePoziome::Srodek) {
            tx = pad_l + (klient_w - txt_w) / 2;
        } else if (wyrownanie_tekstu_ == WyrownaniePoziome::Koniec) {
            tx = obszar_.szerokosc - pad_r - txt_w;
        }
        if (tx < pad_l) tx = pad_l;

        int ty = pad_t;
        if (wyrownanie_pionowe_tekstu_ == WyrownaniePionowe::Srodek) {
            ty = pad_t + (klient_h - txt_h) / 2;
        } else if (wyrownanie_pionowe_tekstu_ == WyrownaniePionowe::Koniec) {
            ty = obszar_.wysokosc - pad_b - txt_h;
        }
        if (ty < pad_t) ty = pad_t;

        ctx.rysuj_tekst_obciety(Punkt(tx, ty), tekst_, col, klient_w, skala);
    }

private:
    int skala_czcionki_{1};
    WyrownaniePoziome wyrownanie_tekstu_{WyrownaniePoziome::Poczatek};
    WyrownaniePionowe wyrownanie_pionowe_tekstu_{WyrownaniePionowe::Srodek};
    bool czy_autodopasowanie_{true};
    int maksymalna_szerokosc_{0};
    bool czy_zawijanie_{false};
    bool czy_zaznaczalna_{false};
};

} // namespace BursztynUI
