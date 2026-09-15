#pragma once

#include "kontrolka.h"
#include "rysowanie.h"

namespace BursztynUI {

class PowierzchniaRysunkowa : public Kontrolka {
public:
    using FunkcjaRysowaniaWlasnego = void (*)(PowierzchniaRysunkowa* nadawca, KontekstRysowania& ctx, void* dane);
    using FunkcjaMyszyWlasna = void (*)(PowierzchniaRysunkowa* nadawca, const ZdarzenieMyszki& e, void* dane);
    using FunkcjaRolkiWlasna = void (*)(PowierzchniaRysunkowa* nadawca, const ZdarzenieRolki& e, void* dane);

    PowierzchniaRysunkowa() {
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = true;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        Kontrolka::zaktualizuj_motyw();
    }

    void przy_rysowaniu(FunkcjaRysowaniaWlasnego cb, void* dane = nullptr) {
        cb_rysowanie_ = cb;
        dane_rysowania_ = dane;
        uniewaznij();
    }

    void przy_wcisnieciu(FunkcjaMyszyWlasna cb, void* dane = nullptr) {
        cb_wcisniecie_ = cb;
        dane_wcisniecia_ = dane;
    }

    void przy_zwolnieniu(FunkcjaMyszyWlasna cb, void* dane = nullptr) {
        cb_zwolnienie_ = cb;
        dane_zwolnienia_ = dane;
    }

    void przy_ruchu(FunkcjaMyszyWlasna cb, void* dane = nullptr) {
        cb_ruch_ = cb;
        dane_ruchu_ = dane;
    }

    void przy_rolce(FunkcjaRolkiWlasna cb, void* dane = nullptr) {
        cb_rolka_wlasna_ = cb;
        dane_rolki_wlasna_ = dane;
    }

    Rozmiar zmierz(Rozmiar dostepny) override {
        SkalaUI s = skala_dpi_;
        int w = 200 * s;
        int h = 150 * s;
        if (preferowany_rozmiar_.szerokosc >= 0) w = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) h = preferowany_rozmiar_.wysokosc * s;
        (void)dostepny;
        return Rozmiar(w, h);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        // Tło i ramka
        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());

        // Własne rysowanie wywołujemy w lokalnym układzie współrzędnych kontrolki
        if (cb_rysowanie_) {
            cb_rysowanie_(this, ctx, dane_rysowania_);
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        if (cb_wcisniecie_) {
            cb_wcisniecie_(this, e, dane_wcisniecia_);
        }
        Kontrolka::obsluz_wcisniecie_myszy(e);
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        if (cb_zwolnienie_) {
            cb_zwolnienie_(this, e, dane_zwolnienia_);
        }
        Kontrolka::obsluz_zwolnienie_myszy(e);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        if (cb_ruch_) {
            cb_ruch_(this, e, dane_ruchu_);
        }
        Kontrolka::obsluz_ruch_myszy(e);
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_) return;
        if (cb_rolka_wlasna_) {
            cb_rolka_wlasna_(this, e, dane_rolki_wlasna_);
        }
        Kontrolka::obsluz_rolke(e);
    }

private:
    FunkcjaRysowaniaWlasnego cb_rysowanie_{nullptr};
    void* dane_rysowania_{nullptr};
    FunkcjaMyszyWlasna cb_wcisniecie_{nullptr};
    void* dane_wcisniecia_{nullptr};
    FunkcjaMyszyWlasna cb_zwolnienie_{nullptr};
    void* dane_zwolnienia_{nullptr};
    FunkcjaMyszyWlasna cb_ruch_{nullptr};
    void* dane_ruchu_{nullptr};
    FunkcjaRolkiWlasna cb_rolka_wlasna_{nullptr};
    void* dane_rolki_wlasna_{nullptr};
};

} // namespace BursztynUI
