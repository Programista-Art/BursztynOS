#pragma once

#include "kontrolka.h"
#include "czcionka.h"
#include "motyw.h"

namespace BursztynUI {

class DostawcaDanychListy {
public:
    virtual ~DostawcaDanychListy() = default;
    virtual size_t liczba_elementow() = 0;
    virtual const char* pobierz_tekst(size_t indeks, char* bufor, size_t rozmiar_bufora) = 0;
};

class ListaWirtualna : public Kontrolka {
public:
    using FunkcjaWyboru = void (*)(ListaWirtualna* nadawca, size_t indeks, void* dane);

    ListaWirtualna() {
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = true;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
        preferowany_rozmiar_ = Rozmiar(200, 200);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        Kontrolka::zaktualizuj_motyw();
    }

    void ustaw_dostawce(DostawcaDanychListy* d) {
        dostawca_ = d;
        wybrany_ = 0;
        przewiniecie_y_ = 0;
        liczba_zapytan_ = 0;
        uniewaznij();
    }
    DostawcaDanychListy* dostawca() const { return dostawca_; }

    size_t liczba_elementow() const {
        return dostawca_ ? dostawca_->liczba_elementow() : 0;
    }

    size_t wybrany_indeks() const { return wybrany_; }
    void ustaw_wybrany_indeks(size_t idx) {
        size_t n = liczba_elementow();
        if (n == 0) { wybrany_ = 0; return; }
        if (idx >= n) idx = n - 1;
        if (wybrany_ != idx) {
            wybrany_ = idx;
            zapewnij_widocznosc(wybrany_);
            uniewaznij();
            if (cb_wybor_) cb_wybor_(this, wybrany_, dane_wyboru_);
        }
    }

    void przy_zmianie_wyboru(FunkcjaWyboru cb, void* dane = nullptr) {
        cb_wybor_ = cb;
        dane_wyboru_ = dane;
    }

    uint64_t liczba_zapytan_dostawcy() const { return liczba_zapytan_; }
    size_t pierwszy_widoczny_indeks() const { return pierwszy_widoczny_; }
    size_t ostatni_widoczny_indeks() const { return ostatni_widoczny_; }

    int wysokosc_wiersza() const { return 24 * skala_dpi_; }

    void przewin_do(size_t idx) {
        zapewnij_widocznosc(idx);
        uniewaznij();
    }

    Rozmiar zmierz(Rozmiar dostepny) override {
        SkalaUI s = skala_dpi_;
        int w = 220 * s;
        int h = 200 * s;
        if (preferowany_rozmiar_.szerokosc >= 0) w = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) h = preferowany_rozmiar_.wysokosc * s;
        (void)dostepny;
        return Rozmiar(w, h);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());

        size_t n = liczba_elementow();
        if (!dostawca_ || n == 0) return;

        int row_h = wysokosc_wiersza();
        int font_h = CzcionkaUI::pobierz_wysokosc(s);
        int pad_l = 8 * s;
        int sb_w = 12 * s;
        int txt_max_w = obszar_.szerokosc - pad_l - sb_w - 4 * s;

        int widocznych_wierszy = (obszar_.wysokosc / (row_h > 0 ? row_h : 1)) + 2;
        size_t idx_start = przewiniecie_y_ / (row_h > 0 ? row_h : 1);
        if (idx_start >= n) idx_start = n > 0 ? n - 1 : 0;
        size_t idx_koniec = idx_start + widocznych_wierszy;
        if (idx_koniec > n) idx_koniec = n;

        pierwszy_widoczny_ = idx_start;
        ostatni_widoczny_ = idx_koniec > 0 ? idx_koniec - 1 : 0;

        int start_y = -(przewiniecie_y_ % (row_h > 0 ? row_h : 1));

        char bufor[128];
        for (size_t i = idx_start; i < idx_koniec; ++i) {
            int y = start_y + static_cast<int>(i - idx_start) * row_h;
            Prostokat r_wiersza(2 * s, y, obszar_.szerokosc - 4 * s - sb_w, row_h);

            bool zaznaczony = (i == wybrany_);
            if (zaznaczony) {
                ctx.rysuj_zaokraglony_prostokat(r_wiersza, motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE), 3 * s);
            }

            ++liczba_zapytan_;
            const char* txt = dostawca_->pobierz_tekst(i, bufor, sizeof(bufor));
            if (!txt) txt = "";

            uint32_t c_txt = zaznaczony ? motyw_kolor_tekstu_zaznaczenia()
                                        : motyw_kolor(MOTYW_KOLOR_TEKST);

            ctx.rysuj_tekst_obciety(Punkt(r_wiersza.x + pad_l, y + (row_h - font_h) / 2),
                                    txt, c_txt, txt_max_w, s);
        }

        // Pasek przewijania
        rysuj_pasek_przewijania(ctx, n, row_h, sb_w);
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        int ly = e.y - obszar_ekranu_.y;
        int lx = e.x - obszar_ekranu_.x;
        SkalaUI s = skala_dpi_;
        int sb_w = 12 * s;

        // Kliknięcie w suwak przewijania
        if (lx >= obszar_.szerokosc - sb_w) {
            int total_h = static_cast<int>(liczba_elementow()) * wysokosc_wiersza();
            if (total_h > obszar_.wysokosc) {
                przewiniecie_y_ = (ly * (total_h - obszar_.wysokosc)) / (obszar_.wysokosc > 0 ? obszar_.wysokosc : 1);
                normalizuj_przewiniecie();
                uniewaznij();
                e.obsluzone = true;
                return;
            }
        }

        // Kliknięcie w wiersz
        int klik_idx = (przewiniecie_y_ + ly) / (wysokosc_wiersza() > 0 ? wysokosc_wiersza() : 1);
        if (klik_idx >= 0 && static_cast<size_t>(klik_idx) < liczba_elementow()) {
            ustaw_wybrany_indeks(static_cast<size_t>(klik_idx));
        }

        Kontrolka::obsluz_wcisniecie_myszy(e);
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_) return;
        int delta = (e.delta_y != 0) ? -e.delta_y : 3;
        przewiniecie_y_ += delta * wysokosc_wiersza();
        normalizuj_przewiniecie();
        uniewaznij();
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || liczba_elementow() == 0) return;
        uint32_t k = e.kod_klawisza;
        bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        uint32_t b = usb ? (k & 0xFFU) : (k & 0x7FU);

        // Strzałka w górę
        if ((usb && b == 82U) || (!usb && b == 0x48U)) {
            if (wybrany_ > 0) ustaw_wybrany_indeks(wybrany_ - 1);
            e.obsluzone = true;
            return;
        }
        // Strzałka w dół
        if ((usb && b == 81U) || (!usb && b == 0x50U)) {
            if (wybrany_ + 1 < liczba_elementow()) ustaw_wybrany_indeks(wybrany_ + 1);
            e.obsluzone = true;
            return;
        }
        // Page Up
        if ((usb && b == 75U) || (!usb && b == 0x49U)) {
            int page = obszar_.wysokosc / (wysokosc_wiersza() > 0 ? wysokosc_wiersza() : 1);
            if (wybrany_ > static_cast<size_t>(page)) ustaw_wybrany_indeks(wybrany_ - page);
            else ustaw_wybrany_indeks(0);
            e.obsluzone = true;
            return;
        }
        // Page Down
        if ((usb && b == 78U) || (!usb && b == 0x51U)) {
            int page = obszar_.wysokosc / (wysokosc_wiersza() > 0 ? wysokosc_wiersza() : 1);
            ustaw_wybrany_indeks(wybrany_ + page);
            e.obsluzone = true;
            return;
        }
        // Home
        if ((usb && b == 74U) || (!usb && b == 0x47U)) {
            ustaw_wybrany_indeks(0);
            e.obsluzone = true;
            return;
        }
        // End
        if ((usb && b == 77U) || (!usb && b == 0x4FU)) {
            ustaw_wybrany_indeks(liczba_elementow() - 1);
            e.obsluzone = true;
            return;
        }

        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    void zapewnij_widocznosc(size_t idx) {
        int row_h = wysokosc_wiersza();
        int y0 = static_cast<int>(idx) * row_h;
        int y1 = y0 + row_h;

        if (y0 < przewiniecie_y_) {
            przewiniecie_y_ = y0;
        } else if (y1 > przewiniecie_y_ + obszar_.wysokosc) {
            przewiniecie_y_ = y1 - obszar_.wysokosc;
        }
        normalizuj_przewiniecie();
    }

    void normalizuj_przewiniecie() {
        int total_h = static_cast<int>(liczba_elementow()) * wysokosc_wiersza();
        int max_scroll = total_h - obszar_.wysokosc;
        if (max_scroll < 0) max_scroll = 0;
        if (przewiniecie_y_ > max_scroll) przewiniecie_y_ = max_scroll;
        if (przewiniecie_y_ < 0) przewiniecie_y_ = 0;
    }

    void rysuj_pasek_przewijania(KontekstRysowania& ctx, size_t n, int row_h, int sb_w) {
        int total_h = static_cast<int>(n) * row_h;
        if (total_h <= obszar_.wysokosc || obszar_.wysokosc <= 0) return;

        SkalaUI s = skala_dpi_;
        int sb_x = obszar_.szerokosc - sb_w - 2 * s;
        int sb_h = obszar_.wysokosc - 4 * s;

        // Tor suwaka
        ctx.rysuj_prostokat(Prostokat(sb_x, 2 * s, sb_w, sb_h), motyw_kolor(MOTYW_KOLOR_PANEL));

        // Uchwyt (thumb)
        int thumb_h = (obszar_.wysokosc * sb_h) / total_h;
        if (thumb_h < 16 * s) thumb_h = 16 * s;
        int travel = sb_h - thumb_h;
        int max_scroll = total_h - obszar_.wysokosc;
        int thumb_y = 2 * s + (max_scroll > 0 ? (przewiniecie_y_ * travel) / max_scroll : 0);

        ctx.rysuj_zaokraglony_prostokat(Prostokat(sb_x + 2 * s, thumb_y, sb_w - 4 * s, thumb_h),
                                        motyw_kolor(MOTYW_KOLOR_RAMKA), 3 * s);
    }

    DostawcaDanychListy* dostawca_{nullptr};
    size_t wybrany_{0};
    int przewiniecie_y_{0};
    uint64_t liczba_zapytan_{0};
    size_t pierwszy_widoczny_{0};
    size_t ostatni_widoczny_{0};

    FunkcjaWyboru cb_wybor_{nullptr};
    void* dane_wyboru_{nullptr};
};

} // namespace BursztynUI
