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

    bool pobierz_wartosc() const { return wartosc_; }
    bool wartosc() const { return wartosc_; }

    void ustaw_wartosc(bool wartosc, bool powiadom = true) {
        if (wartosc_ == wartosc) return;
        wartosc_ = wartosc;
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
        const int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        Prostokat tor(1 * s, 4 * s, obszar_.szerokosc - 2 * s,
                      obszar_.wysokosc - 8 * s);
        if (tor.szerokosc < 8 * s || tor.wysokosc < 8 * s) return;
        uint32_t tlo = wartosc_ ? Kolory::AkcentZloty : 0x004A3A2A;
        if (!czy_wlaczona_) tlo = 0x00352A20;
        else if (czy_tylko_do_odczytu_) tlo = wartosc_ ? 0x00806A32 : 0x0040362C;
        else if (czy_wcisnieta_) tlo = wartosc_ ? 0x00C47700 : 0x005A4632;
        else if (czy_najechana_) tlo = wartosc_ ? 0x00FFBF00 : 0x00604A34;
        const int promien = tor.wysokosc / 2;
        ctx.rysuj_zaokraglony_prostokat(tor, tlo, promien);
        ctx.rysuj_zaokraglona_ramke(tor,
            czy_ma_fokus_ ? Kolory::AkcentFokus : Kolory::RamkaDomyslna,
            promien, czy_ma_fokus_ ? 2 * s : s);

        const int margines = 3 * s;
        int d = tor.wysokosc - 2 * margines;
        if (d < 2 * s) d = 2 * s;
        int x = wartosc_ ? tor.prawy() - margines - d : tor.x + margines;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(x, tor.y + margines, d, d),
                                       czy_wlaczona_ ? 0x00FFFFFF : 0x00808080,
                                       d / 2);
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
            uniewaznij();
        }
        if (aktywuj) ustaw_wartosc(!wartosc_);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_) return;
        const uint32_t kod = e.kod_klawisza;
        const bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        const uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);
        if (kod == 32U || (usb && baza == 44U)) {
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
