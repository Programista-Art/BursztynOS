#pragma once

#include "kontrolka.h"
#include "kalendarz.h"
#include "czcionka.h"

namespace BursztynUI {

class PoleDaty : public Kontrolka {
public:
    using FunkcjaDaty = void (*)(PoleDaty* nadawca, DataKalendarza data, void* dane_uzytkownika);

    explicit PoleDaty(DataKalendarza data_poczatkowa = DataKalendarza{}) {
        data_ = data_poczatkowa.czy_poprawna()
            ? data_poczatkowa : DataKalendarza{};
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        preferowany_rozmiar_ = Rozmiar(150, 30);
        wypelnienie_ = Marginesy(6, 4, 6, 4);
    }

    ~PoleDaty() override = default;

    DataKalendarza pobierz_date() const { return data_; }

    void ustaw_date(DataKalendarza d, bool powiadom = true) {
        if (!d.czy_poprawna() || d == data_) return;
        data_ = d;
        uniewaznij_obszar(prostokat_tekstu());
        if (powiadom && cb_zmiana_) {
            cb_zmiana_(this, data_, dane_zmiany_);
        }
    }

    void przy_zmianie_daty(FunkcjaDaty cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
    }

    Prostokat prostokat_tekstu() const { return geometria().tekst; }
    Prostokat prostokat_przycisku_gora() const { return geometria().gora; }
    Prostokat prostokat_przycisku_dol() const { return geometria().dol; }
    int najechany_przycisk() const { return najechany_przycisk_; }
    int wcisniety_przycisk() const { return wcisniety_przycisk_; }
    Punkt pozycja_tekstu() const {
        char txt[16];
        formatuj_date(txt, sizeof(txt), data_);
        Prostokat r = prostokat_tekstu();
        const int skala = skala_dpi_ < 1 ? 1 : skala_dpi_;
        const int tw = CzcionkaUI::zmierz_tekst(txt, skala);
        const int th = CzcionkaUI::pobierz_wysokosc(skala);
        return Punkt(r.x + (r.szerokosc - tw) / 2,
                     r.y + (r.wysokosc - th) / 2);
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        int skala = skala_dpi_;
        return Rozmiar(150 * skala, 30 * skala);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        int skala = skala_dpi_ < 1 ? 1 : skala_dpi_;
        Geometria g = geometria();
        btn_up_ = g.gora;
        btn_down_ = g.dol;

        // Przycisk w górę (+)
        ctx.rysuj_zaokraglony_prostokat(btn_up_, kolor_przycisku(0), 2);
        ctx.rysuj_zaokraglona_ramke(btn_up_, Kolory::RamkaDomyslna, 2, 1);
        ctx.rysuj_tekst_wysrodkowany(btn_up_, "+", Kolory::TekstBialy, skala);

        // Przycisk w dół (-)
        ctx.rysuj_zaokraglony_prostokat(btn_down_, kolor_przycisku(1), 2);
        ctx.rysuj_zaokraglona_ramke(btn_down_, Kolory::RamkaDomyslna, 2, 1);
        ctx.rysuj_tekst_wysrodkowany(btn_down_, "-", Kolory::TekstBialy, skala);

        // Tekst daty
        char txt[16];
        formatuj_date(txt, sizeof(txt), data_);

        Punkt tekst_p = pozycja_tekstu();
        ctx.rysuj_tekst_obciety(tekst_p, txt, s.tekst,
                                g.tekst.prawy() - tekst_p.x, skala);
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        Geometria g = geometria();
        if (g.gora.zawiera(lx, ly)) {
            ustaw_wcisniety_przycisk(0);
            kolejny_dzien();
            e.obsluzone = true;
        } else if (g.dol.zawiera(lx, ly)) {
            ustaw_wcisniety_przycisk(1);
            poprzedni_dzien();
            e.obsluzone = true;
        }
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_zwolnienie_myszy(e);
        ustaw_wcisniety_przycisk(-1);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_ruch_myszy(e);
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        Geometria g = geometria();
        int nowy = g.gora.zawiera(lx, ly) ? 0
                  : g.dol.zawiera(lx, ly) ? 1 : -1;
        ustaw_najechany_przycisk(nowy);
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        ustaw_najechany_przycisk(-1);
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        Kontrolka::obsluz_rolke(e);
        if (!czy_wlaczona_) return;
        if (e.delta_y > 0) { kolejny_dzien(); e.obsluzone = true; }
        else if (e.delta_y < 0) { poprzedni_dzien(); e.obsluzone = true; }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        Kontrolka::obsluz_wcisniecie_klawisza(e);
        if (!czy_wlaczona_) return;

        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if ((usb && baza == 82U) || (e0 && baza == 0x48U)) { // Up
            kolejny_dzien();
            e.obsluzone = true;
        } else if ((usb && baza == 81U) || (e0 && baza == 0x50U)) { // Down
            poprzedni_dzien();
            e.obsluzone = true;
        }
    }

    void kolejny_dzien() {
        DataKalendarza d = data_;
        int max_dni = DataKalendarza::dni_w_miesiacu(d.rok, d.miesiac);
        if (d.dzien < max_dni) {
            ++d.dzien;
        } else {
            d.dzien = 1;
            if (d.miesiac < 12) ++d.miesiac;
            else { d.miesiac = 1; ++d.rok; }
        }
        ustaw_date(d);
    }

    void poprzedni_dzien() {
        DataKalendarza d = data_;
        if (d.dzien > 1) {
            --d.dzien;
        } else {
            if (d.miesiac > 1) --d.miesiac;
            else { d.miesiac = 12; --d.rok; }
            d.dzien = DataKalendarza::dni_w_miesiacu(d.rok, d.miesiac);
        }
        ustaw_date(d);
    }

private:
    DataKalendarza data_{};
    Prostokat btn_up_{};
    Prostokat btn_down_{};
    int najechany_przycisk_{-1};
    int wcisniety_przycisk_{-1};

    FunkcjaDaty cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};

    struct Geometria {
        Prostokat tekst;
        Prostokat gora;
        Prostokat dol;
    };

    Geometria geometria() const {
        const int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        const int btn_w = 22 * s;
        Geometria g;
        g.dol = Prostokat(obszar_.szerokosc - btn_w - 2 * s, 2 * s,
                          btn_w, obszar_.wysokosc - 4 * s);
        g.gora = Prostokat(obszar_.szerokosc - 2 * btn_w - 4 * s, 2 * s,
                           btn_w, obszar_.wysokosc - 4 * s);
        g.tekst = Prostokat(6 * s, 2 * s,
                            obszar_.szerokosc - 2 * btn_w - 12 * s,
                            obszar_.wysokosc - 4 * s);
        return g;
    }

    uint32_t kolor_przycisku(int indeks) const {
        if (!czy_wlaczona_) return Kolory::WylaczonyTlo;
        if (indeks == wcisniety_przycisk_) return Kolory::PowierzchniaAktywna;
        if (indeks == najechany_przycisk_) return Kolory::PowierzchniaNajechana;
        return Kolory::PowierzchniaAlternatywna;
    }

    void uniewaznij_przycisk(int indeks) {
        if (indeks < 0 || indeks > 1) return;
        Geometria g = geometria();
        uniewaznij_obszar(indeks == 0 ? g.gora : g.dol);
    }

    void ustaw_najechany_przycisk(int nowy) {
        if (nowy == najechany_przycisk_) return;
        int stary = najechany_przycisk_;
        najechany_przycisk_ = nowy;
        uniewaznij_przycisk(stary);
        uniewaznij_przycisk(nowy);
    }

    void ustaw_wcisniety_przycisk(int nowy) {
        if (nowy == wcisniety_przycisk_) return;
        int stary = wcisniety_przycisk_;
        wcisniety_przycisk_ = nowy;
        uniewaznij_przycisk(stary);
        uniewaznij_przycisk(nowy);
    }

    static void formatuj_date(char* buf, size_t cap, DataKalendarza d) {
        if (cap < 11) return;
        buf[0] = static_cast<char>('0' + (d.rok / 1000) % 10);
        buf[1] = static_cast<char>('0' + (d.rok / 100) % 10);
        buf[2] = static_cast<char>('0' + (d.rok / 10) % 10);
        buf[3] = static_cast<char>('0' + (d.rok % 10));
        buf[4] = '-';
        buf[5] = static_cast<char>('0' + (d.miesiac / 10) % 10);
        buf[6] = static_cast<char>('0' + (d.miesiac % 10));
        buf[7] = '-';
        buf[8] = static_cast<char>('0' + (d.dzien / 10) % 10);
        buf[9] = static_cast<char>('0' + (d.dzien % 10));
        buf[10] = '\0';
    }
};

} // namespace BursztynUI
