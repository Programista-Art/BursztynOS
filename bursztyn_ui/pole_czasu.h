#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

struct CzasDnia {
    int godzina{12};  // 0..23
    int minuta{0};    // 0..59
    int sekunda{0};   // 0..59

    bool czy_poprawny() const {
        return godzina >= 0 && godzina <= 23 &&
               minuta >= 0 && minuta <= 59 &&
               sekunda >= 0 && sekunda <= 59;
    }

    bool operator==(const CzasDnia& o) const {
        return godzina == o.godzina && minuta == o.minuta && sekunda == o.sekunda;
    }
    bool operator!=(const CzasDnia& o) const { return !(*this == o); }
};

class PoleCzasu : public Kontrolka {
public:
    using FunkcjaCzasu = void (*)(PoleCzasu* nadawca, CzasDnia czas, void* dane_uzytkownika);

    explicit PoleCzasu(CzasDnia czas_poczatkowy = CzasDnia{}) {
        czas_ = czas_poczatkowy.czy_poprawny()
            ? czas_poczatkowy : CzasDnia{};
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        preferowany_rozmiar_ = Rozmiar(150, 30);
        wypelnienie_ = Marginesy(6, 4, 6, 4);
    }

    ~PoleCzasu() override = default;

    CzasDnia pobierz_czas() const { return czas_; }

    struct GeometriaStrzalki {
        Punkt srodek;
        int promien{0};
    };

    Prostokat prostokat_przycisku_gora() const { return geometria().gora; }
    Prostokat prostokat_przycisku_dol() const { return geometria().dol; }
    GeometriaStrzalki geometria_strzalki_gora() const {
        return geometria_strzalki(geometria().gora);
    }
    GeometriaStrzalki geometria_strzalki_dol() const {
        return geometria_strzalki(geometria().dol);
    }

    void ustaw_czas(CzasDnia c) {
        if (!c.czy_poprawny() || c == czas_) return;
        const CzasDnia stary = czas_;
        czas_ = c;
        if (stary.godzina != c.godzina) uniewaznij_segment(0);
        if (stary.minuta != c.minuta) uniewaznij_segment(1);
        if (stary.sekunda != c.sekunda) uniewaznij_segment(2);
        if (cb_zmiana_) {
            cb_zmiana_(this, czas_, dane_zmiany_);
        }
    }

    void przy_zmianie_czasu(FunkcjaCzasu cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
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

        // Spinner
        ctx.rysuj_zaokraglony_prostokat(btn_up_, kolor_przycisku(0), 2);
        ctx.rysuj_zaokraglona_ramke(btn_up_, Kolory::RamkaDomyslna, 2, 1);
        rysuj_strzalke(ctx, btn_up_, true,
                       czy_wlaczona_ ? Kolory::TekstBialy : Kolory::WylaczonyTekst);

        ctx.rysuj_zaokraglony_prostokat(btn_down_, kolor_przycisku(1), 2);
        ctx.rysuj_zaokraglona_ramke(btn_down_, Kolory::RamkaDomyslna, 2, 1);
        rysuj_strzalke(ctx, btn_down_, false,
                       czy_wlaczona_ ? Kolory::TekstBialy : Kolory::WylaczonyTekst);

        // Segmenty [HH] : [MM] : [SS]
        int cy = (obszar_.wysokosc - CzcionkaUI::pobierz_wysokosc(skala)) / 2;

        char buf_h[4], buf_m[4], buf_s[4];
        formatuj_2cyfry(buf_h, czas_.godzina);
        formatuj_2cyfry(buf_m, czas_.minuta);
        formatuj_2cyfry(buf_s, czas_.sekunda);

        // Godzina
        rect_hh_ = g.hh;
        if (aktywny_segment_ == 0 && czy_ma_fokus_) {
            ctx.rysuj_zaokraglony_prostokat(rect_hh_, Kolory::ZaznaczenieTlo, 2);
            ctx.rysuj_tekst_wysrodkowany(rect_hh_, buf_h, Kolory::TekstBialy, skala);
        } else {
            ctx.rysuj_tekst_wysrodkowany(rect_hh_, buf_h, s.tekst, skala);
        }
        // :
        ctx.rysuj_tekst_obciety(Punkt(g.dwukropek1.x, cy), ":",
                                Kolory::TekstWyciszony,
                                g.dwukropek1.szerokosc, skala);

        // Minuta
        rect_mm_ = g.mm;
        if (aktywny_segment_ == 1 && czy_ma_fokus_) {
            ctx.rysuj_zaokraglony_prostokat(rect_mm_, Kolory::ZaznaczenieTlo, 2);
            ctx.rysuj_tekst_wysrodkowany(rect_mm_, buf_m, Kolory::TekstBialy, skala);
        } else {
            ctx.rysuj_tekst_wysrodkowany(rect_mm_, buf_m, s.tekst, skala);
        }
        // :
        ctx.rysuj_tekst_obciety(Punkt(g.dwukropek2.x, cy), ":",
                                Kolory::TekstWyciszony,
                                g.dwukropek2.szerokosc, skala);

        // Sekunda
        rect_ss_ = g.ss;
        if (aktywny_segment_ == 2 && czy_ma_fokus_) {
            ctx.rysuj_zaokraglony_prostokat(rect_ss_, Kolory::ZaznaczenieTlo, 2);
            ctx.rysuj_tekst_wysrodkowany(rect_ss_, buf_s, Kolory::TekstBialy, skala);
        } else {
            ctx.rysuj_tekst_wysrodkowany(rect_ss_, buf_s, s.tekst, skala);
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        Geometria g = geometria();
        if (g.gora.zawiera(lx, ly)) {
            ustaw_wcisniety_przycisk(0);
            zwieksz_aktywny_segment();
            e.obsluzone = true;
            return;
        }
        if (g.dol.zawiera(lx, ly)) {
            ustaw_wcisniety_przycisk(1);
            zmniejsz_aktywny_segment();
            e.obsluzone = true;
            return;
        }

        if (g.hh.zawiera(lx, ly)) {
            ustaw_aktywny_segment(0);
            e.obsluzone = true;
        } else if (g.mm.zawiera(lx, ly)) {
            ustaw_aktywny_segment(1);
            e.obsluzone = true;
        } else if (g.ss.zawiera(lx, ly)) {
            ustaw_aktywny_segment(2);
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
        if (e.delta_y > 0) { zwieksz_aktywny_segment(); e.obsluzone = true; }
        else if (e.delta_y < 0) { zmniejsz_aktywny_segment(); e.obsluzone = true; }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        Kontrolka::obsluz_wcisniecie_klawisza(e);
        if (!czy_wlaczona_) return;

        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if ((usb && baza == 82U) || (e0 && baza == 0x48U)) { // Up
            zwieksz_aktywny_segment();
            e.obsluzone = true;
        } else if ((usb && baza == 81U) || (e0 && baza == 0x50U)) { // Down
            zmniejsz_aktywny_segment();
            e.obsluzone = true;
        } else if ((usb && baza == 79U) || (e0 && baza == 0x4DU)) { // Right
            if (aktywny_segment_ < 2) {
                ustaw_aktywny_segment(aktywny_segment_ + 1);
            }
            e.obsluzone = true;
        } else if ((usb && baza == 80U) || (e0 && baza == 0x4BU)) { // Left
            if (aktywny_segment_ > 0) {
                ustaw_aktywny_segment(aktywny_segment_ - 1);
            }
            e.obsluzone = true;
        }
    }

private:
    CzasDnia czas_{};
    int aktywny_segment_{0}; // 0 = HH, 1 = MM, 2 = SS

    Prostokat btn_up_{};
    Prostokat btn_down_{};
    Prostokat rect_hh_{};
    Prostokat rect_mm_{};
    Prostokat rect_ss_{};
    int najechany_przycisk_{-1};
    int wcisniety_przycisk_{-1};

    FunkcjaCzasu cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};

    struct Geometria {
        Prostokat gora;
        Prostokat dol;
        Prostokat hh;
        Prostokat dwukropek1;
        Prostokat mm;
        Prostokat dwukropek2;
        Prostokat ss;
    };

    Geometria geometria() const {
        const int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        const int btn_w = 20 * s;
        const int btn_h = (obszar_.wysokosc - 4 * s) / 2;
        const int seg_w = 20 * s;
        const int sep_w = 10 * s;
        int x = 6 * s;
        Geometria g;
        g.gora = Prostokat(obszar_.szerokosc - btn_w - 2 * s, 2 * s,
                           btn_w, btn_h);
        g.dol = Prostokat(g.gora.x, g.gora.y + btn_h, btn_w, btn_h);
        g.hh = Prostokat(x, 2 * s, seg_w, obszar_.wysokosc - 4 * s);
        x += seg_w;
        g.dwukropek1 = Prostokat(x, 2 * s, sep_w, obszar_.wysokosc - 4 * s);
        x += sep_w;
        g.mm = Prostokat(x, 2 * s, seg_w, obszar_.wysokosc - 4 * s);
        x += seg_w;
        g.dwukropek2 = Prostokat(x, 2 * s, sep_w, obszar_.wysokosc - 4 * s);
        x += sep_w;
        g.ss = Prostokat(x, 2 * s, seg_w, obszar_.wysokosc - 4 * s);
        return g;
    }

    static GeometriaStrzalki geometria_strzalki(Prostokat r) {
        GeometriaStrzalki g;
        g.srodek = Punkt(r.x + r.szerokosc / 2,
                         r.y + r.wysokosc / 2);
        int najmniejszy = r.szerokosc < r.wysokosc
                        ? r.szerokosc : r.wysokosc;
        g.promien = najmniejszy / 5;
        if (g.promien < 1) g.promien = 1;
        return g;
    }

    static void rysuj_strzalke(KontekstRysowania& ctx, Prostokat r,
                               bool gora, uint32_t kolor) {
        GeometriaStrzalki g = geometria_strzalki(r);
        for (int d = 0; d <= 2 * g.promien; ++d) {
            int polowa = d / 2;
            int y = gora ? g.srodek.y - g.promien + d
                         : g.srodek.y + g.promien - d;
            ctx.rysuj_linie_pozioma(g.srodek.x - polowa, y,
                                    2 * polowa + 1, kolor);
        }
    }

    Prostokat prostokat_segmentu(int segment) const {
        Geometria g = geometria();
        return segment == 0 ? g.hh : segment == 1 ? g.mm : g.ss;
    }

    void uniewaznij_segment(int segment) {
        if (segment < 0 || segment > 2) return;
        uniewaznij_obszar(prostokat_segmentu(segment));
    }

    void ustaw_aktywny_segment(int nowy) {
        if (nowy < 0 || nowy > 2 || nowy == aktywny_segment_) return;
        int stary = aktywny_segment_;
        aktywny_segment_ = nowy;
        uniewaznij_segment(stary);
        uniewaznij_segment(nowy);
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

    void zwieksz_aktywny_segment() {
        CzasDnia c = czas_;
        if (aktywny_segment_ == 0) {
            c.godzina = (c.godzina + 1) % 24;
        } else if (aktywny_segment_ == 1) {
            c.minuta = (c.minuta + 1) % 60;
        } else {
            c.sekunda = (c.sekunda + 1) % 60;
        }
        ustaw_czas(c);
    }

    void zmniejsz_aktywny_segment() {
        CzasDnia c = czas_;
        if (aktywny_segment_ == 0) {
            c.godzina = (c.godzina + 23) % 24;
        } else if (aktywny_segment_ == 1) {
            c.minuta = (c.minuta + 59) % 60;
        } else {
            c.sekunda = (c.sekunda + 59) % 60;
        }
        ustaw_czas(c);
    }

    static void formatuj_2cyfry(char* buf, int n) {
        if (n < 0) n = 0;
        buf[0] = static_cast<char>('0' + (n / 10) % 10);
        buf[1] = static_cast<char>('0' + (n % 10));
        buf[2] = '\0';
    }
};

} // namespace BursztynUI
