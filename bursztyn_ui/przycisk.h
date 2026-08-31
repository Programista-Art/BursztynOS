#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class Przycisk : public Kontrolka {
public:
    using FunkcjaKlikniecia = void (*)(Przycisk* nadawca, void* dane_uzytkownika);
    using AkcjaBezparametrowa = void (*)();

    Przycisk(const char* etykieta = "Przycisk") {
        styl_ = Motyw::pobierz_domyslny().styl_przycisku;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        czy_moze_miec_fokus_ = true;
        ustaw_tekst(etykieta);
    }

    const char* tekst() const { return tekst_; }
    void ustaw_tekst(const char* nowy_tekst) {
        if (!nowy_tekst) nowy_tekst = "";
        size_t i = 0;
        for (; i < sizeof(tekst_) - 1 && nowy_tekst[i] != '\0'; ++i) {
            tekst_[i] = nowy_tekst[i];
        }
        tekst_[i] = '\0';
        uniewaznij();
    }

    void przy_kliknieciu(FunkcjaKlikniecia cb, void* dane = nullptr) {
        na_klikniecie_ = cb;
        dane_uzytkownika_ = dane;
    }

    StylKontrolki& styl() { return styl_; }
    void ustaw_styl(const StylKontrolki& s) { styl_ = s; uniewaznij(); }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        int skala = skala_dpi_;
        int txt_w = oblicz_szerokosc_tekstu(tekst_, skala);
        int txt_h = gui_pobierz_wysokosc_fontu() * skala;

        int naturalna_wys = txt_h + wypelnienie_.pionowo() * skala + 8 * skala;
        if (naturalna_wys < 28 * skala) naturalna_wys = 28 * skala;
        int naturalna_szer = txt_w + wypelnienie_.poziomo() * skala + 16 * skala;

        Rozmiar zadany(naturalna_szer, naturalna_wys);

        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala, maksymalny_rozmiar_.szerokosc * skala);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala, maksymalny_rozmiar_.wysokosc * skala);

        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        int skala = skala_dpi_;
        const StylStanu* biezacy = &styl_.normalny;
        if (!czy_wlaczona_) {
            biezacy = &styl_.wylaczony;
        } else if (czy_wcisnieta_) {
            biezacy = &styl_.wcisniety;
        } else if (czy_najechana_) {
            biezacy = &styl_.najechany;
        } else if (czy_ma_fokus_) {
            biezacy = &styl_.fokus;
        }

        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        // Tło
        if (biezacy->tlo != 0) {
            if (biezacy->promien_zaokraglenia > 0) {
                ctx.rysuj_zaokraglony_prostokat(r, biezacy->tlo, biezacy->promien_zaokraglenia * skala);
            } else {
                ctx.rysuj_prostokat(r, biezacy->tlo);
            }
        }

        // Ramka
        if (biezacy->ramka != 0 && biezacy->grubosc_ramki > 0) {
            if (biezacy->promien_zaokraglenia > 0) {
                ctx.rysuj_zaokraglona_ramke(r, biezacy->ramka, biezacy->promien_zaokraglenia * skala, biezacy->grubosc_ramki * skala);
            } else {
                ctx.rysuj_ramke(r, biezacy->ramka, biezacy->grubosc_ramki * skala);
            }
        }

        // Tekst
        ctx.rysuj_tekst_wysrodkowany(r, tekst_, biezacy->tekst, skala);
    }

    void przy_najechaniu() override {
        czy_najechana_ = true;
        uniewaznij();
    }

    void przy_opuszczeniu() override {
        czy_najechana_ = false;
        czy_wcisnieta_ = false;
        uniewaznij();
    }

    void przy_wcisnieciu_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        czy_wcisnieta_ = true;
        uniewaznij();
        e.obsluzone = true;
    }

    void przy_zwolnieniu_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        if (czy_wcisnieta_) {
            czy_wcisnieta_ = false;
            uniewaznij();
            if (obszar_.zawiera(e.x, e.y) || obszar_ekranu_.zawiera(e.x, e.y)) {
                if (na_klikniecie_) {
                    na_klikniecie_(this, dane_uzytkownika_);
                }
            }
            e.obsluzone = true;
        }
    }

    void przy_wcisnieciu_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        // Spacja (32) lub Enter (13 / 10)
        if (e.kod_klawisza == 32 || e.kod_klawisza == 13 || e.kod_klawisza == 10) {
            if (na_klikniecie_) {
                na_klikniecie_(this, dane_uzytkownika_);
            }
            e.obsluzone = true;
        }
    }

private:
    char tekst_[64]{0};
    StylKontrolki styl_;
    FunkcjaKlikniecia na_klikniecie_{nullptr};
    void* dane_uzytkownika_{nullptr};
};

} // namespace BursztynUI
