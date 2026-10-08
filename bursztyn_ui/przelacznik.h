#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class Przelacznik : public Kontrolka {
public:
    using FunkcjaZmiany = void (*)(Przelacznik*, bool, void*);

    explicit Przelacznik(bool wartosc = false) : wartosc_(wartosc) {
        styl_ = Motyw::pobierz_domyslny().styl_przycisku;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = true;
        czy_reaguje_wizualnie_na_wcisniecie_ = true;
        preferowany_rozmiar_ = Rozmiar(52, 28);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_przycisku;
        uniewaznij();
    }

    bool pobierz_wartosc() const { return wartosc_; }
    bool wartosc() const { return wartosc_; }

    void ustaw_wartosc(bool wartosc, bool powiadom = true) {
        if (wartosc_ == wartosc) return;
        wartosc_ = wartosc;
        BUI_POMIAR(identity_changes, 1);
        uniewaznij_obszar(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc));
        if (powiadom && cb_zmiana_) cb_zmiana_(this, wartosc_, dane_zmiany_);
    }

    void przy_zmianie(FunkcjaZmiany cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        return Rozmiar(52 * skala_dpi_, 28 * skala_dpi_);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        const SkalaUI s = skala_dpi_;
        Prostokat tor(1 * s, 4 * s, obszar_.szerokosc - 2 * s,
                      obszar_.wysokosc - 8 * s);
        if (tor.szerokosc < 8 * s || tor.wysokosc < 8 * s) return;
        uint32_t tlo = wartosc_ ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_PANEL);
        if (!czy_wlaczona_) tlo = motyw_kolor(MOTYW_KOLOR_PANEL);
        else if (czy_tylko_do_odczytu_) tlo = wartosc_ ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_PANEL);
        else if (czy_wcisnieta_) tlo = wartosc_ ? motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY) : motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
        else if (czy_najechana_) tlo = wartosc_ ? motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY) : motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
        const int promien = tor.wysokosc / 2;
        ctx.rysuj_zaokraglony_prostokat(tor, tlo, promien);
        ctx.rysuj_zaokraglona_ramke(tor,
            czy_ma_fokus_ ? motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY) : motyw_kolor(MOTYW_KOLOR_RAMKA),
            promien, czy_ma_fokus_ ? 2 * s : 1 * s);

        const int margines = 3 * s;
        int d = tor.wysokosc - 2 * margines;
        if (d < 2 * s) d = 2 * s;
        int x = wartosc_ ? tor.prawy() - margines - d : tor.x + margines;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(x, tor.y + margines, d, d),
                                       czy_wlaczona_ ? 0x00FFFFFF : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY),
                                       d / 2);
        ctx.rysuj_zaokraglona_ramke(Prostokat(x, tor.y + margines, d, d),
                                    motyw_kolor(MOTYW_KOLOR_RAMKA), d / 2, 1);
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ || !e.czy_lewy_przycisk()) return;
        czy_wcisnieta_ = true;
        uniewaznij();
        e.obsluzone = true;
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ || !e.czy_lewy_przycisk()) return;
        const bool aktywuj = czy_wcisnieta_ && obszar_ekranu_.zawiera(e.pozycja());
        if (czy_wcisnieta_) {
            czy_wcisnieta_ = false;
            if (!aktywuj) uniewaznij();
        }
        if (aktywuj) ustaw_wartosc(!wartosc_);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_) return;
        const uint32_t kod = e.polecenie();
        if (kod == 32U) {
            ustaw_wartosc(!wartosc_);
            e.obsluzone = true;
            return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    bool wartosc_{false};
    FunkcjaZmiany cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
};

} // namespace BursztynUI
