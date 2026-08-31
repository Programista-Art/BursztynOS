#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class Etykieta : public Kontrolka {
public:
    static constexpr size_t MAKS_DLUGOSC_TEKSTU = 256;

    Etykieta(const char* poczatkowy_tekst = "") {
        styl_ = Motyw::pobierz_domyslny().styl_etykiety;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Poczatek;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        ustaw_tekst(poczatkowy_tekst);
    }

    const char* tekst() const { return tekst_; }
    void ustaw_tekst(const char* nowy_tekst) {
        if (!nowy_tekst) nowy_tekst = "";
        size_t i = 0;
        for (; i < MAKS_DLUGOSC_TEKSTU - 1 && nowy_tekst[i] != '\0'; ++i) {
            tekst_[i] = nowy_tekst[i];
        }
        tekst_[i] = '\0';
        uniewaznij();
    }

    uint32_t kolor_tekstu() const { return kolor_tekstu_; }
    void ustaw_kolor_tekstu(uint32_t c) { kolor_tekstu_ = c; uniewaznij(); }

    int skala_fontu() const { return skala_fontu_; }
    void ustaw_skale_fontu(int s) { skala_fontu_ = (s < 1) ? 1 : s; uniewaznij(); }

    WyrownaniePoziome wyrownanie_tekstu() const { return wyrownanie_tekstu_; }
    void ustaw_wyrownanie_tekstu(WyrownaniePoziome a) { wyrownanie_tekstu_ = a; uniewaznij(); }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        int skala = skala_fontu_ * skala_dpi_;
        int txt_w = oblicz_szerokosc_tekstu(tekst_, skala);
        int txt_h = gui_pobierz_wysokosc_fontu() * skala;

        Rozmiar zadany(txt_w + wypelnienie_.poziomo() * skala_dpi_,
                       txt_h + wypelnienie_.pionowo() * skala_dpi_);

        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala_dpi_;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala_dpi_;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala_dpi_, maksymalny_rozmiar_.szerokosc * skala_dpi_);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala_dpi_, maksymalny_rozmiar_.wysokosc * skala_dpi_);

        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        if (tekst_[0] == '\0') return;

        int skala = skala_fontu_ * skala_dpi_;
        uint32_t col = (kolor_tekstu_ != 0) ? kolor_tekstu_ :
                       (czy_wlaczona_ ? styl_.normalny.tekst : styl_.wylaczony.tekst);

        int txt_w = oblicz_szerokosc_tekstu(tekst_, skala);
        int txt_h = gui_pobierz_wysokosc_fontu() * skala;

        int pad_l = wypelnienie_.lewy * skala_dpi_;
        int pad_t = wypelnienie_.gorny * skala_dpi_;
        int pad_r = wypelnienie_.prawy * skala_dpi_;
        int pad_b = wypelnienie_.dolny * skala_dpi_;

        int klient_w = obszar_.szerokosc - (pad_l + pad_r);
        int klient_h = obszar_.wysokosc - (pad_t + pad_b);

        int tx = pad_l;
        if (wyrownanie_tekstu_ == WyrownaniePoziome::Srodek) {
            tx = pad_l + (klient_w - txt_w) / 2;
        } else if (wyrownanie_tekstu_ == WyrownaniePoziome::Koniec) {
            tx = obszar_.szerokosc - pad_r - txt_w;
        }
        if (tx < pad_l) tx = pad_l;

        int ty = pad_t + (klient_h - txt_h) / 2;
        if (ty < pad_t) ty = pad_t;

        ctx.rysuj_tekst(Punkt(tx, ty), tekst_, col, skala);
    }

private:
    char tekst_[MAKS_DLUGOSC_TEKSTU]{0};
    uint32_t kolor_tekstu_{0};
    int skala_fontu_{1};
    WyrownaniePoziome wyrownanie_tekstu_{WyrownaniePoziome::Poczatek};
    StylKontrolki styl_;
};

} // namespace BursztynUI
