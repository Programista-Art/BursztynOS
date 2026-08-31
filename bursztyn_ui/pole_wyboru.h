#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class PoleWyboru : public Kontrolka {
public:
    using FunkcjaPrzelaczenia = void (*)(PoleWyboru* nadawca, bool czy_zaznaczone, void* dane_uzytkownika);
    using FunkcjaStanu = void (*)(PoleWyboru* nadawca, StanWyboru stan, void* dane_uzytkownika);

    PoleWyboru(const char* etykieta = "Opcja", bool poczatkowo_zaznaczone = false)
        : stan_wyboru_(poczatkowo_zaznaczone ? StanWyboru::Zaznaczone : StanWyboru::Niezaznaczone) {
        styl_ = Motyw::pobierz_domyslny().styl_pola_wyboru;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Poczatek;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = true;
        ustaw_tekst(etykieta);
    }

    bool czy_zaznaczone() const { return stan_wyboru_ == StanWyboru::Zaznaczone; }
    void ustaw_zaznaczone(bool z) {
        ustaw_stan_wyboru(z ? StanWyboru::Zaznaczone : StanWyboru::Niezaznaczone);
    }

    StanWyboru stan_wyboru() const { return stan_wyboru_; }
    void ustaw_stan_wyboru(StanWyboru nowy_stan) {
        if (stan_wyboru_ != nowy_stan) {
            stan_wyboru_ = nowy_stan;
            uniewaznij();
            if (cb_przelaczenia_) {
                cb_przelaczenia_(this, czy_zaznaczone(), dane_przelaczenia_);
            }
            if (cb_stanu_) {
                cb_stanu_(this, stan_wyboru_, dane_stanu_);
            }
            if (cb_zmiana_wartosci_) {
                cb_zmiana_wartosci_(this, dane_zmiany_wartosci_);
            }
        }
    }

    bool czy_trojstanowe() const { return czy_trojstanowe_; }
    void ustaw_trojstanowe(bool t) { czy_trojstanowe_ = t; }

    bool czy_nieokreslone() const { return stan_wyboru_ == StanWyboru::Nieokreslone; }

    void przy_przelaczeniu(FunkcjaPrzelaczenia cb, void* dane = nullptr) {
        cb_przelaczenia_ = cb;
        dane_przelaczenia_ = dane;
    }

    void przy_zmianie_stanu(FunkcjaStanu cb, void* dane = nullptr) {
        cb_stanu_ = cb;
        dane_stanu_ = dane;
    }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        int skala = skala_dpi_;
        int wymiar_kratki = 16 * skala;
        int txt_w = (tekst_[0] != '\0') ? (CzcionkaUI::zmierz_tekst(tekst_, skala) + 8 * skala) : 0;
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala);

        int calkowita_szer = wymiar_kratki + txt_w + wypelnienie_.poziomo() * skala;
        int calkowita_wys = (wymiar_kratki > txt_h ? wymiar_kratki : txt_h) + wypelnienie_.pionowo() * skala;

        Rozmiar zadany(calkowita_szer, calkowita_wys);

        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala, maksymalny_rozmiar_.szerokosc * skala);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala, maksymalny_rozmiar_.wysokosc * skala);

        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        int skala = skala_dpi_;
        int wymiar_kratki = 16 * skala;

        int pad_l = wypelnienie_.lewy * skala;
        int pad_t = wypelnienie_.gorny * skala;
        int pad_v = wypelnienie_.pionowo() * skala;

        int kratka_x = pad_l;
        int kratka_y = pad_t + (obszar_.wysokosc - pad_v - wymiar_kratki) / 2;
        if (kratka_y < pad_t) kratka_y = pad_t;

        Prostokat kratka_r(kratka_x, kratka_y, wymiar_kratki, wymiar_kratki);

        StylStanu s = wyznacz_biezacy_styl_stanu();

        // Tło kratki
        ctx.rysuj_prostokat(kratka_r, s.tlo);
        // Ramka kratki
        ctx.rysuj_ramke(kratka_r, s.ramka, s.grubosc_ramki * skala);

        // Znacznik stanu
        if (stan_wyboru_ == StanWyboru::Zaznaczone) {
            int wewn_pad = 3 * skala;
            Prostokat znacznik_r(kratka_r.x + wewn_pad, kratka_r.y + wewn_pad,
                                 kratka_r.szerokosc - 2 * wewn_pad, kratka_r.wysokosc - 2 * wewn_pad);
            if (!znacznik_r.czy_pusty()) {
                uint32_t check_col = czy_wlaczona_ ? Kolory::PoleWyboruZaznaczenie : Kolory::WylaczonyTekst;
                ctx.rysuj_prostokat(znacznik_r, check_col);
            }
        } else if (stan_wyboru_ == StanWyboru::Nieokreslone) {
            int wewn_pad = 4 * skala;
            int wysokosc_belki = 4 * skala;
            Prostokat belka_r(kratka_r.x + wewn_pad, kratka_r.y + (kratka_r.wysokosc - wysokosc_belki) / 2,
                              kratka_r.szerokosc - 2 * wewn_pad, wysokosc_belki);
            if (!belka_r.czy_pusty()) {
                uint32_t indef_col = czy_wlaczona_ ? Kolory::NieokreslonyKolor : Kolory::WylaczonyTekst;
                ctx.rysuj_prostokat(belka_r, indef_col);
            }
        }

        // Etykieta obok kratki
        if (tekst_[0] != '\0') {
            int txt_x = kratka_x + wymiar_kratki + 8 * skala;
            int txt_h = CzcionkaUI::pobierz_wysokosc(skala);
            int txt_y = pad_t + (obszar_.wysokosc - pad_v - txt_h) / 2;
            if (txt_y < pad_t) txt_y = pad_t;

            uint32_t fg = czy_wlaczona_ ? s.tekst : Kolory::WylaczonyTekst;
            ctx.rysuj_tekst(Punkt(txt_x, txt_y), tekst_, fg, skala);
        }
    }

    void obsluz_wejscie_myszy() override {
        Kontrolka::obsluz_wejscie_myszy();
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        czy_wcisnieta_ = true;
        uniewaznij();
        e.obsluzone = true;
        if (cb_wcisniecie_myszy_) cb_wcisniecie_myszy_(this, e, dane_wcisniecia_myszy_);
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        if (czy_wcisnieta_) {
            czy_wcisnieta_ = false;
            if (obszar_ekranu_.zawiera(e.x, e.y)) {
                przelacz_kolejny_stan();
            }
            uniewaznij();
            e.obsluzone = true;
        }
        if (cb_zwolnienie_myszy_) cb_zwolnienie_myszy_(this, e, dane_zwolnienia_myszy_);
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        if (e.kod_klawisza == 32 || e.kod_klawisza == 13 || e.kod_klawisza == 10) { // Spacja / Enter
            przelacz_kolejny_stan();
            e.obsluzone = true;
            return;
        }
        if (cb_klawisz_wcisniety_) cb_klawisz_wcisniety_(this, e, dane_klawisza_wcisnietego_);
    }

private:
    void przelacz_kolejny_stan() {
        if (czy_trojstanowe_) {
            if (stan_wyboru_ == StanWyboru::Niezaznaczone) {
                ustaw_stan_wyboru(StanWyboru::Zaznaczone);
            } else if (stan_wyboru_ == StanWyboru::Zaznaczone) {
                ustaw_stan_wyboru(StanWyboru::Nieokreslone);
            } else {
                ustaw_stan_wyboru(StanWyboru::Niezaznaczone);
            }
        } else {
            ustaw_stan_wyboru((stan_wyboru_ == StanWyboru::Zaznaczone) ? StanWyboru::Niezaznaczone : StanWyboru::Zaznaczone);
        }
    }

    StanWyboru stan_wyboru_{StanWyboru::Niezaznaczone};
    bool czy_trojstanowe_{false};

    FunkcjaPrzelaczenia cb_przelaczenia_{nullptr};
    void* dane_przelaczenia_{nullptr};
    FunkcjaStanu cb_stanu_{nullptr};
    void* dane_stanu_{nullptr};
};

} // namespace BursztynUI
