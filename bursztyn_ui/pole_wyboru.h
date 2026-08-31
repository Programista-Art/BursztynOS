#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class PoleWyboru : public Kontrolka {
public:
    using FunkcjaPrzelaczenia = void (*)(PoleWyboru* nadawca, bool czy_zaznaczone, void* dane_uzytkownika);

    PoleWyboru(const char* etykieta = "Opcja", bool poczatkowo_zaznaczone = false)
        : czy_zaznaczone_(poczatkowo_zaznaczone) {
        styl_ = Motyw::pobierz_domyslny().styl_pola_wyboru;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Poczatek;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        czy_moze_miec_fokus_ = true;
        ustaw_tekst(etykieta);
    }

    bool czy_zaznaczone() const { return czy_zaznaczone_; }
    void ustaw_zaznaczone(bool z) {
        if (czy_zaznaczone_ != z) {
            czy_zaznaczone_ = z;
            uniewaznij();
            if (na_przelaczenie_) {
                na_przelaczenie_(this, czy_zaznaczone_, dane_uzytkownika_);
            }
        }
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

    void przy_przelaczeniu(FunkcjaPrzelaczenia cb, void* dane = nullptr) {
        na_przelaczenie_ = cb;
        dane_uzytkownika_ = dane;
    }

    StylKontrolki& styl() { return styl_; }
    void ustaw_styl(const StylKontrolki& s) { styl_ = s; uniewaznij(); }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        int skala = skala_dpi_;
        int wymiar_kratki = 16 * skala;
        int txt_w = (tekst_[0] != '\0') ? (oblicz_szerokosc_tekstu(tekst_, skala) + 6 * skala) : 0;
        int txt_h = gui_pobierz_wysokosc_fontu() * skala;

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

        const StylStanu* biezacy = &styl_.normalny;
        if (!czy_wlaczona_) {
            biezacy = &styl_.wylaczony;
        } else if (czy_ma_fokus_) {
            biezacy = &styl_.fokus;
        } else if (czy_najechana_) {
            biezacy = &styl_.najechany;
        }

        // Tło kratki
        ctx.rysuj_prostokat(kratka_r, biezacy->tlo);
        // Ramka kratki
        ctx.rysuj_ramke(kratka_r, biezacy->ramka, biezacy->grubosc_ramki * skala);

        // Znacznik zaznaczenia
        if (czy_zaznaczone_) {
            int wewn_pad = 3 * skala;
            Prostokat znacznik_r(kratka_r.x + wewn_pad, kratka_r.y + wewn_pad,
                                 kratka_r.szerokosc - 2 * wewn_pad, kratka_r.wysokosc - 2 * wewn_pad);
            if (!znacznik_r.czy_pusty()) {
                uint32_t check_col = czy_wlaczona_ ? Kolory::PoleWyboruZaznaczenie : Kolory::WylaczonyTekst;
                ctx.rysuj_prostokat(znacznik_r, check_col);
            }
        }

        // Etykieta obok kratki
        if (tekst_[0] != '\0') {
            int txt_x = kratka_x + wymiar_kratki + 6 * skala;
            int txt_h = gui_pobierz_wysokosc_fontu() * skala;
            int txt_y = pad_t + (obszar_.wysokosc - pad_v - txt_h) / 2;
            if (txt_y < pad_t) txt_y = pad_t;

            uint32_t fg = czy_wlaczona_ ? biezacy->tekst : Kolory::WylaczonyTekst;
            ctx.rysuj_tekst(Punkt(txt_x, txt_y), tekst_, fg, skala);
        }
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
            ustaw_zaznaczone(!czy_zaznaczone_);
            e.obsluzone = true;
        }
    }

    void przy_wcisnieciu_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        if (e.kod_klawisza == 32 || e.kod_klawisza == 13 || e.kod_klawisza == 10) { // Spacja / Enter
            ustaw_zaznaczone(!czy_zaznaczone_);
            e.obsluzone = true;
        }
    }

private:
    char tekst_[64]{0};
    bool czy_zaznaczone_{false};
    StylKontrolki styl_;
    FunkcjaPrzelaczenia na_przelaczenie_{nullptr};
    void* dane_uzytkownika_{nullptr};
};

} // namespace BursztynUI
