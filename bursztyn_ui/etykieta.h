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

    void zaktualizuj_motyw() override {
        bool wlasne_tlo = styl_.normalny.wlasne_tlo;
        bool wlasny_tekst = styl_.normalny.wlasny_tekst;
        bool wlasna_ramka = styl_.normalny.wlasna_ramka;
        StylKontrolki s = Motyw::pobierz_domyslny().styl_etykiety;
        if (wlasne_tlo) { s.normalny.tlo = styl_.normalny.tlo; s.normalny.wlasne_tlo = true; }
        if (wlasny_tekst) { s.normalny.tekst = styl_.normalny.tekst; s.normalny.wlasny_tekst = true; }
        if (wlasna_ramka) { s.normalny.ramka = styl_.normalny.ramka; s.normalny.wlasna_ramka = true; }
        styl_ = s;
        Kontrolka::zaktualizuj_motyw();
    }

    int skala_czcionki() const { return skala_czcionki_; }
    void ustaw_skale_czcionki(int s) {
        skala_czcionki_ = s;
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
        SkalaUI skala = SkalaUI::procent(skala_dpi_.procent() * uint32_t(skala_czcionki_));
        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala);
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala);

        if (maksymalna_szerokosc_ > 0 && txt_w > maksymalna_szerokosc_ * skala_dpi_) {
            txt_w = maksymalna_szerokosc_ * skala_dpi_;
        }

        if(czy_zawijanie_ && dostepny_rozmiar.szerokosc>0){
            int limit=dostepny_rozmiar.szerokosc-wypelnienie_.poziomo()*skala_dpi_;
            if(limit<1)limit=1;
            int rows=0, widest=0;
            linie(limit,skala,[&](const char* line){++rows;int w=CzcionkaUI::zmierz_tekst(line,skala);if(w>widest)widest=w;});
            txt_w=widest;txt_h=rows*CzcionkaUI::pobierz_wysokosc(skala);
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

        SkalaUI skala = SkalaUI::procent(skala_dpi_.procent() * uint32_t(skala_czcionki_));
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

        if(czy_zawijanie_){
            int y=pad_t;
            linie(klient_w,skala,[&](const char* line){
                if(y+txt_h<=pad_t+klient_h)ctx.rysuj_tekst_obciety({pad_l,y},line,col,klient_w,skala);
                y+=txt_h;
            });
        }else ctx.rysuj_tekst_obciety(Punkt(tx, ty), tekst_, col, klient_w, skala);
    }

private:
    template<class Line> void linie(int width,SkalaUI scale,Line line)const {
        size_t at=0;
        while(tekst_[at]){
            while(tekst_[at]==' ')++at;
            if(!tekst_[at])break;
            char part[MAKS_DLUGOSC_TEKSTU]{};size_t used=0,start=at,last_space=0;
            while(tekst_[at] && tekst_[at]!='\n'){
                uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(tekst_,at,&cp);
                if(!n || used+n>=sizeof(part))break;
                for(size_t j=0;j<n;++j)part[used+j]=tekst_[at+j];
                part[used+n]=0;
                if(used && CzcionkaUI::zmierz_tekst(part,scale)>width){part[used]=0;break;}
                if(cp==' ')last_space=used;
                used+=n;at+=n;
            }
            if(tekst_[at] && tekst_[at]!='\n' && last_space){used=last_space;at=start+used;}
            while(used && part[used-1]==' ')--used;
            part[used]=0;line(part);
            if(tekst_[at]=='\n')++at;
            if(at==start)++at; // Defensive progress for corrupt internal text.
        }
    }
    int skala_czcionki_{1};
    WyrownaniePoziome wyrownanie_tekstu_{WyrownaniePoziome::Poczatek};
    WyrownaniePionowe wyrownanie_pionowe_tekstu_{WyrownaniePionowe::Srodek};
    bool czy_autodopasowanie_{true};
    int maksymalna_szerokosc_{0};
    bool czy_zawijanie_{false};
    bool czy_zaznaczalna_{false};
};

} // namespace BursztynUI
