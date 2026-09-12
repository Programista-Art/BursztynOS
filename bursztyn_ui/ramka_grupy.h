#pragma once

#include "panel.h"
#include "przycisk_opcji.h"
#include "czcionka.h"

namespace BursztynUI {

enum class WyrownanieTytuluGrupy { Poczatek, Srodek, Koniec };

class RamkaGrupy : public Panel {
public:
    explicit RamkaGrupy(const char* tytul = "") : Panel(Orientacja::Pionowa) {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = Kolory::PowierzchniaTlo;
        styl_.normalny.promien_zaokraglenia = 4;
        ustaw_wypelnienie(Marginesy(12, 22, 12, 10));
        ustaw_tekst(tytul);
    }

    const char* tytul() const { return tekst_; }
    void ustaw_tytul(const char* tytul) { ustaw_tekst(tytul); }

    void ustaw_wyrownanie_tytulu(WyrownanieTytuluGrupy w) {
        if (wyrownanie_tytulu_ == w) return;
        wyrownanie_tytulu_ = w;
        uniewaznij();
    }
    WyrownanieTytuluGrupy wyrownanie_tytulu() const { return wyrownanie_tytulu_; }

    void ustaw_zaokraglenie(int promien) { ustaw_promien_zaokraglenia(promien); }

    GrupaOpcji& grupa_opcji() { return grupa_opcji_; }
    const GrupaOpcji& grupa_opcji() const { return grupa_opcji_; }

    PrzyciskOpcji& dodaj_opcje(const char* tekst, int64_t wartosc = 0) {
        PrzyciskOpcji& opcja = dodaj<PrzyciskOpcji>(tekst, wartosc);
        opcja.ustaw_grupe(&grupa_opcji_);
        return opcja;
    }

    void ustaw_wlaczona(bool wlaczona) {
        Kontrolka::ustaw_wlaczona(wlaczona);
        for (size_t i = 0; i < liczba_dzieci_; ++i)
            if (dzieci_[i]) dzieci_[i]->ustaw_wlaczona(wlaczona);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat caly(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        if (caly.czy_pusty()) return;
        StylStanu st = wyznacz_biezacy_styl_stanu();
        if (st.tlo) ctx.rysuj_zaokraglony_prostokat(caly, st.tlo,
                                                    st.promien_zaokraglenia * s);
        int font_h = CzcionkaUI::pobierz_wysokosc(s);
        int y_ramki = font_h / 2;
        Prostokat ramka(0, y_ramki, caly.szerokosc,
                        caly.wysokosc > y_ramki ? caly.wysokosc - y_ramki : 0);
        if (st.ramka && st.grubosc_ramki > 0)
            ctx.rysuj_zaokraglona_ramke(ramka, st.ramka,
                                        st.promien_zaokraglenia * s,
                                        st.grubosc_ramki * s);
        if (tekst_[0]) {
            int szer = CzcionkaUI::zmierz_tekst(tekst_, s);
            int marg = 10 * s;
            int tx = marg;
            if (wyrownanie_tytulu_ == WyrownanieTytuluGrupy::Srodek)
                tx = (caly.szerokosc - szer) / 2;
            else if (wyrownanie_tytulu_ == WyrownanieTytuluGrupy::Koniec)
                tx = caly.szerokosc - marg - szer;
            if (tx < marg) tx = marg;
            Prostokat przerwa(tx - 4 * s, 0, szer + 8 * s, font_h + 2 * s);
            ctx.rysuj_prostokat(przerwa, st.tlo ? st.tlo : Kolory::PowierzchniaTlo);
            ctx.rysuj_tekst(Punkt(tx, 0), tekst_,
                            czy_wlaczona_ ? st.tekst : Kolory::WylaczonyTekst, s);
        }
    }

private:
    WyrownanieTytuluGrupy wyrownanie_tytulu_{WyrownanieTytuluGrupy::Poczatek};
    GrupaOpcji grupa_opcji_;
};

using Grupa = RamkaGrupy;

} // namespace BursztynUI
