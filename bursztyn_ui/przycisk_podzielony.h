#pragma once

#include "kontrolka.h"
#include "menu_kontekstowe.h"
#include "czcionka.h"

namespace BursztynUI {

enum class CzescPrzyciskuPodzielonego : uint8_t { Brak, Glowna, Strzalka };

class PrzyciskPodzielony : public Kontrolka {
public:
    using FunkcjaAkcji = void (*)(PrzyciskPodzielony*, void*);

    explicit PrzyciskPodzielony(const char* tekst = "Wykonaj") {
        styl_ = Motyw::pobierz_domyslny().styl_przycisku;
        wypelnienie_ = styl_.wypelnienie;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        ustaw_tekst(tekst);
        preferowany_rozmiar_ = Rozmiar(150, 32);
    }

    Menu& menu() { return menu_.menu(); }
    const Menu& menu() const { return menu_.menu(); }
    bool czy_menu_otwarte() const { return menu_.czy_otwarte(); }
    CzescPrzyciskuPodzielonego najechana_czesc() const { return najechana_czesc_; }
    CzescPrzyciskuPodzielonego wcisnieta_czesc() const { return wcisnieta_czesc_; }
    int szerokosc_czesci_strzalki() const { return szerokosc_strzalki_ * skala_dpi_; }

    void przy_kliknieciu(FunkcjaAkcji cb, void* dane = nullptr) {
        cb_glowna_ = cb; dane_glownej_ = dane;
    }
    void przy_otwarciu_menu(FunkcjaAkcji cb, void* dane = nullptr) {
        cb_menu_ = cb; dane_menu_ = dane;
    }

    Rozmiar zmierz(Rozmiar) override {
        const int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int w = CzcionkaUI::zmierz_tekst(tekst_, s) + 48 * s;
        if (w < 100 * s) w = 100 * s;
        int h = 32 * s;
        if (preferowany_rozmiar_.szerokosc >= 0) w = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) h = preferowany_rozmiar_.wysokosc * s;
        return Rozmiar(w, h);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu baza = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, baza);
        const int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        const int aw = szerokosc_czesci_strzalki();
        const int podzial = obszar_.szerokosc - aw;
        Prostokat glowna(1, 1, podzial - 1, obszar_.wysokosc - 2);
        Prostokat strzalka(podzial, 1, aw - 1, obszar_.wysokosc - 2);
        if (czy_wlaczona_) {
            if (najechana_czesc_ == CzescPrzyciskuPodzielonego::Glowna)
                ctx.rysuj_prostokat(glowna, styl_.najechany.tlo);
            if (najechana_czesc_ == CzescPrzyciskuPodzielonego::Strzalka)
                ctx.rysuj_prostokat(strzalka, styl_.najechany.tlo);
            if (wcisnieta_czesc_ == CzescPrzyciskuPodzielonego::Glowna)
                ctx.rysuj_prostokat(glowna, styl_.wcisniety.tlo);
            if (wcisnieta_czesc_ == CzescPrzyciskuPodzielonego::Strzalka)
                ctx.rysuj_prostokat(strzalka, styl_.wcisniety.tlo);
        }
        ctx.rysuj_prostokat(Prostokat(podzial, 2 * s, s,
                            obszar_.wysokosc - 4 * s), Kolory::RamkaDomyslna);
        ctx.rysuj_tekst_wysrodkowany(glowna, tekst_, baza.tekst, s);

        // Symetryczny grot z geometrii, bez glifu fontu.
        const int cx = strzalka.x + strzalka.szerokosc / 2;
        const int cy = strzalka.y + strzalka.wysokosc / 2;
        for (int y = 0; y < 4 * s; ++y) {
            int pol = y;
            ctx.rysuj_prostokat(Prostokat(cx - pol, cy - 2 * s + y,
                                          2 * pol + s, s), baza.tekst);
        }
        if (czy_ma_fokus_)
            ctx.rysuj_ramke(Prostokat(2 * s, 2 * s,
                obszar_.szerokosc - 4 * s, obszar_.wysokosc - 4 * s),
                Kolory::AkcentFokus, s);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override { BUI_POMIAR(mouse_events, 1);
        CzescPrzyciskuPodzielonego nowa = czesc_w_punkcie(e.x, e.y);
        if (nowa != najechana_czesc_) {
            CzescPrzyciskuPodzielonego stara = najechana_czesc_;
            najechana_czesc_ = nowa;
            uniewaznij_czesc(stara);
            uniewaznij_czesc(nowa);
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechana_czesc_ != CzescPrzyciskuPodzielonego::Brak) {
            CzescPrzyciskuPodzielonego stara = najechana_czesc_;
            najechana_czesc_ = CzescPrzyciskuPodzielonego::Brak;
            uniewaznij_czesc(stara);
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        wcisnieta_czesc_ = czesc_w_punkcie(e.x, e.y);
        uniewaznij_czesc(wcisnieta_czesc_);
        e.obsluzone = wcisnieta_czesc_ != CzescPrzyciskuPodzielonego::Brak;
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        CzescPrzyciskuPodzielonego stara = wcisnieta_czesc_;
        CzescPrzyciskuPodzielonego cel = czesc_w_punkcie(e.x, e.y);
        wcisnieta_czesc_ = CzescPrzyciskuPodzielonego::Brak;
        uniewaznij_czesc(stara);
        if (stara == cel && cel == CzescPrzyciskuPodzielonego::Glowna) wykonaj_glowna();
        else if (stara == cel && cel == CzescPrzyciskuPodzielonego::Strzalka) otworz_menu();
        e.obsluzone = stara != CzescPrzyciskuPodzielonego::Brak;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        uint32_t k = e.kod_klawisza;
        bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        uint32_t b = usb ? (k & 0xFFU) : (k & 0x7FU);
        if (k == 13U || k == 10U || k == 32U || (usb && (b == 40U || b == 44U))) {
            wykonaj_glowna(); e.obsluzone = true; return;
        }
        if ((e.czy_alt() && ((usb && b == 81U) || (k & BWS_KEY_SOURCE_E0))) ||
            (usb && b == 81U)) {
            otworz_menu(); e.obsluzone = true; return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    CzescPrzyciskuPodzielonego czesc_w_punkcie(int x, int y) const {
        if (!obszar_ekranu_.zawiera(x, y)) return CzescPrzyciskuPodzielonego::Brak;
        return x >= obszar_ekranu_.prawy() - szerokosc_czesci_strzalki()
            ? CzescPrzyciskuPodzielonego::Strzalka
            : CzescPrzyciskuPodzielonego::Glowna;
    }
    void uniewaznij_czesc(CzescPrzyciskuPodzielonego c) {
        if (c == CzescPrzyciskuPodzielonego::Brak) return;
        int aw = szerokosc_czesci_strzalki();
        if (c == CzescPrzyciskuPodzielonego::Strzalka)
            uniewaznij_obszar(Prostokat(obszar_.szerokosc - aw, 0, aw, obszar_.wysokosc));
        else
            uniewaznij_obszar(Prostokat(0, 0, obszar_.szerokosc - aw, obszar_.wysokosc));
    }
    void wykonaj_glowna() {
        if (cb_glowna_) cb_glowna_(this, dane_glownej_);
    }
    void otworz_menu() {
        if (cb_menu_) cb_menu_(this, dane_menu_);
        if (!zarzadca_popupow_ || menu_.menu().liczba_elementow() == 0) return;
        (void)menu_.pokaz_pod_kontrolka(*zarzadca_popupow_, *this);
    }

    int szerokosc_strzalki_{28};
    CzescPrzyciskuPodzielonego najechana_czesc_{CzescPrzyciskuPodzielonego::Brak};
    CzescPrzyciskuPodzielonego wcisnieta_czesc_{CzescPrzyciskuPodzielonego::Brak};
    MenuKontekstowe menu_{};
    FunkcjaAkcji cb_glowna_{nullptr};
    void* dane_glownej_{nullptr};
    FunkcjaAkcji cb_menu_{nullptr};
    void* dane_menu_{nullptr};
};

} // namespace BursztynUI
