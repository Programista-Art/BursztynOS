#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class Przycisk : public Kontrolka {
public:
    using FunkcjaKlikniecia = void (*)(Przycisk* nadawca, void* dane_uzytkownika);
    using AkcjaBezparametrowa = void (*)();

    Przycisk(const char* etykieta = "Przycisk") {
        styl_ = Motyw::pobierz_domyslny().styl_przycisku;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Poczatek;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = true;
        ustaw_tekst(etykieta);
    }

    uint32_t ikona_id() const { return ikona_id_; }
    void ustaw_ikone(uint32_t id) {
        ikona_id_ = id;
        uniewaznij();
    }

    void przy_kliknieciu(FunkcjaKlikniecia cb, void* dane = nullptr) {
        cb_klikniecie_przycisku_ = cb;
        dane_klikniecia_przycisku_ = dane;
    }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        SkalaUI skala = skala_dpi_;
        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala);
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala);

        int ikona_w = (ikona_id_ > 0) ? (16 * skala + 6 * skala) : 0;
        int naturalna_wys = txt_h + wypelnienie_.pionowo() * skala + 10 * skala;
        if (naturalna_wys < 30 * skala) naturalna_wys = 30 * skala;
        int naturalna_szer = txt_w + ikona_w + wypelnienie_.poziomo() * skala + 20 * skala;

        Rozmiar zadany(naturalna_szer, naturalna_wys);

        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala, maksymalny_rozmiar_.szerokosc * skala);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala, maksymalny_rozmiar_.wysokosc * skala);

        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI skala = skala_dpi_;
        StylStanu s = wyznacz_biezacy_styl_stanu();
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        rysuj_tlo_i_ramke(ctx, s);

        // Tekst
        ctx.rysuj_tekst_wysrodkowany(r, tekst_, s.tekst, skala);
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

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        bool wewnatrz = obszar_ekranu_.zawiera(e.x, e.y);
        if (czy_wcisnieta_) {
            if (czy_najechana_ != wewnatrz) {
                czy_najechana_ = wewnatrz;
                uniewaznij();
            }
        }
        if (cb_ruch_myszy_) cb_ruch_myszy_(this, e, dane_ruchu_myszy_);
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        bool byla_wcisnieta = czy_wcisnieta_;
        czy_wcisnieta_ = false;
        uniewaznij();

        bool wewnatrz = obszar_ekranu_.zawiera(e.x, e.y);
        if (byla_wcisnieta && wewnatrz) {
            wykonaj_akcje_klikniecia(e);
        }
        e.obsluzone = true;
        if (cb_zwolnienie_myszy_) cb_zwolnienie_myszy_(this, e, dane_zwolnienia_myszy_);
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        // Spacja (32) lub Enter (13 / 10)
        if (e.kod_klawisza == 32 || e.kod_klawisza == 13 || e.kod_klawisza == 10) {
            ZdarzenieMyszki me{};
            me.typ = TypZdarzenia::Klikniecie;
            me.x = obszar_ekranu_.x + obszar_ekranu_.szerokosc / 2;
            me.y = obszar_ekranu_.y + obszar_ekranu_.wysokosc / 2;
            wykonaj_akcje_klikniecia(me);
            e.obsluzone = true;
            return;
        }
        if (cb_klawisz_wcisniety_) cb_klawisz_wcisniety_(this, e, dane_klawisza_wcisnietego_);
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        wykonaj_akcje_klikniecia(e);
        if (cb_podwojne_klikniecie_) cb_podwojne_klikniecie_(this, dane_podwojnego_klikniecia_);
        e.obsluzone = true;
    }

private:
    void wykonaj_akcje_klikniecia(ZdarzenieMyszki& e) {
        if (cb_klikniecie_przycisku_) {
            cb_klikniecie_przycisku_(this, dane_klikniecia_przycisku_);
        }
        if (cb_klikniecie_) {
            cb_klikniecie_(this, dane_klikniecia_);
        }
        obsluz_klikniecie(e);
    }

    uint32_t ikona_id_{0};
    FunkcjaKlikniecia cb_klikniecie_przycisku_{nullptr};
    void* dane_klikniecia_przycisku_{nullptr};
};

} // namespace BursztynUI
