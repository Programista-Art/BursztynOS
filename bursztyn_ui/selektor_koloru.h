#pragma once

#include "pole_tekstowe.h"

namespace BursztynUI {

class SelektorKoloru : public Kontrolka {
public:
    static constexpr uint32_t WIDOCZNA_CZERN = 0x01000000U;
    static constexpr size_t LICZBA_PALETY = 16;
    using FunkcjaZmiany = void (*)(SelektorKoloru*, uint32_t, void*);

private:
    class PoleHex : public PoleTekstowe {
    public:
        explicit PoleHex(SelektorKoloru* w) : PoleTekstowe("#RRGGBB"), wlasciciel_(w) {
            ustaw_maksymalna_liczbe_znakow(7);
        }
        void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
            uint32_t k = e.kod_klawisza;
            bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
            uint32_t b = usb ? (k & 0xFFU) : (k & 0x7FU);
            if (wlasciciel_ && (k == 13U || k == 10U || (usb && b == 40U))) {
                wlasciciel_->zatwierdz_hex(); e.obsluzone = true; return;
            }
            if (wlasciciel_ && (k == 27U || (usb && b == 41U))) {
                wlasciciel_->synchronizuj_hex(); e.obsluzone = true; return;
            }
            PoleTekstowe::obsluz_wcisniecie_klawisza(e);
        }
    private:
        SelektorKoloru* wlasciciel_{nullptr};
    };

public:
    explicit SelektorKoloru(uint32_t kolor = 0x00FFBF00U) : kolor_(kolor) {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        pole_hex_ = new PoleHex(this);
        dodaj_kontrolke(pole_hex_);
        synchronizuj_hex();
        preferowany_rozmiar_ = Rozmiar(300, 112);
    }

    uint32_t pobierz_kolor() const { return kolor_; }
    uint32_t pobierzkolor() const { return pobierz_kolor(); }
    void ustaw_kolor(uint32_t kolor, bool powiadom = true) {
        if (kolor_ == kolor) return;
        int stara_paleta = wybrana_paleta_;
        kolor_ = kolor;
        wybrana_paleta_ = -1;
        for (size_t i = 0; i < LICZBA_PALETY; ++i) {
            if (PALETA[i] == kolor_) {
                wybrana_paleta_ = static_cast<int>(i);
                break;
            }
        }
        synchronizuj_hex();
        uniewaznij_obszar(prostokat_podgladu().suma(prostokat_palety()));
        uniewaznij_komorke(stara_paleta);
        uniewaznij_komorke(wybrana_paleta_);
        if (powiadom && cb_zmiana_) cb_zmiana_(this, kolor_, dane_zmiany_);
    }
    void ustawkolor(uint32_t kolor) { ustaw_kolor(kolor); }
    void przy_zmianie(FunkcjaZmiany cb, void* dane = nullptr) {
        cb_zmiana_ = cb; dane_zmiany_ = dane;
    }
    void przyzmianie(FunkcjaZmiany cb, void* dane = nullptr) { przy_zmianie(cb, dane); }

    bool ustaw_hex(const char* tekst, bool powiadom = true) {
        uint32_t rgb = 0;
        if (!parsuj_hex(tekst, &rgb)) return false;
        ustaw_kolor(rgb, powiadom);
        return true;
    }
    bool parsuj_hex(const char* tekst, uint32_t* wynik) const {
        if (!tekst || !wynik) return false;
        size_t i = tekst[0] == '#' ? 1 : 0;
        size_t start = i;
        uint32_t v = 0;
        for (; tekst[i] && i - start < 6; ++i) {
            int n = cyfra_hex(tekst[i]);
            if (n < 0) return false;
            v = (v << 4U) | static_cast<uint32_t>(n);
        }
        if (i - start != 6 || tekst[i] != '\0') return false;
        *wynik = v == 0 ? WIDOCZNA_CZERN : v;
        return true;
    }
    bool czy_hex_poprawny() const { uint32_t v = 0; return parsuj_hex(pole_hex_->tekst(), &v); }
    const char* tekst_hex() const { return pole_hex_->tekst(); }
    PoleTekstowe& pole_hex() { return *pole_hex_; }
    int wybrany_indeks_palety() const { return wybrana_paleta_; }

    Rozmiar oblicz_naturalny_rozmiar() const override { return Rozmiar(300, 112); }
    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int w = r.szerokosc - 62 * s;
        if (w < 0) w = 0;
        pole_hex_->rozmiesc(Prostokat(56 * s, 6 * s, w, 30 * s));
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        Prostokat pod = prostokat_podgladu();
        // Szachownica pokazuje przezroczystosc; czern palety ma marker 0x01.
        for (int y = 0; y < pod.wysokosc; y += 4 * s)
            for (int x = 0; x < pod.szerokosc; x += 4 * s)
                ctx.rysuj_prostokat(Prostokat(pod.x + x, pod.y + y, 4 * s, 4 * s),
                    (((x / (4 * s)) + (y / (4 * s))) & 1) ? 0x00B0B0B0 : 0x00606060);
        if (kolor_ != 0) ctx.rysuj_prostokat(pod, kolor_);
        ctx.rysuj_ramke(pod, Kolory::RamkaDomyslna, s);

        Prostokat pal = prostokat_palety();
        int gap = 3 * s;
        int cell = (pal.szerokosc - 7 * gap) / 8;
        if (cell < 4 * s) cell = 4 * s;
        for (size_t i = 0; i < LICZBA_PALETY; ++i) {
            int col = static_cast<int>(i % 8), row = static_cast<int>(i / 8);
            Prostokat c(pal.x + col * (cell + gap), pal.y + row * (cell + gap), cell, cell);
            if (PALETA[i] == 0) {
                ctx.rysuj_prostokat(c, 0x00B0B0B0);
                ctx.rysuj_prostokat(Prostokat(c.x, c.y + c.wysokosc / 2, c.szerokosc, 2 * s), 0x00CC2020);
            } else ctx.rysuj_prostokat(c, PALETA[i]);
            uint32_t ramka = static_cast<int>(i) == wybrana_paleta_ ? Kolory::AkcentFokus : 0x00806030;
            int grubosc = static_cast<int>(i) == wybrana_paleta_ ? 2 * s : s;
            if (czy_wlaczona_ && static_cast<int>(i) == najechana_paleta_) {
                ramka = 0x00FFFFFF;
                grubosc = 2 * s;
            }
            if (czy_wlaczona_ && static_cast<int>(i) == wcisnieta_paleta_)
                ctx.rysuj_ramke(c.zmniejszony(Marginesy(2 * s)), 0x00FFD700, s);
            ctx.rysuj_ramke(c, ramka, grubosc);
        }
        if (czy_ma_fokus_) ctx.rysuj_ramke(Prostokat(1, 1, obszar_.szerokosc - 2, obszar_.wysokosc - 2),
                                            Kolory::AkcentFokus, s);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        int nowa = czy_wlaczona_
            ? indeks_palety(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y)
            : -1;
        if (nowa == najechana_paleta_) return;
        int stara = najechana_paleta_;
        najechana_paleta_ = nowa;
        uniewaznij_komorke(stara);
        uniewaznij_komorke(nowa);
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechana_paleta_ < 0) return;
        int stara = najechana_paleta_;
        najechana_paleta_ = -1;
        uniewaznij_komorke(stara);
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ || !e.czy_lewy_przycisk()) return;
        int idx = indeks_palety(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        if (idx >= 0) {
            wcisnieta_paleta_ = idx;
            uniewaznij_komorke(idx);
            e.obsluzone = true;
        }
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!e.czy_lewy_przycisk() || wcisnieta_paleta_ < 0) return;
        int stara = wcisnieta_paleta_;
        int idx = indeks_palety(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        wcisnieta_paleta_ = -1;
        uniewaznij_komorke(stara);
        if (czy_wlaczona_ && !czy_tylko_do_odczytu_ && idx == stara)
            ustaw_kolor(PALETA[idx]);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_) return;
        uint32_t k = e.kod_klawisza; bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (k & BWS_KEY_SOURCE_E0) != 0; uint32_t b = usb ? k & 0xFFU : k & 0x7FU;
        int delta = 0;
        if ((usb && b == 79U) || (e0 && b == 0x4DU)) delta = 1;
        if ((usb && b == 80U) || (e0 && b == 0x4BU)) delta = -1;
        if ((usb && b == 81U) || (e0 && b == 0x50U)) delta = 8;
        if ((usb && b == 82U) || (e0 && b == 0x48U)) delta = -8;
        if (delta) {
            int n = wybrana_paleta_ < 0 ? 0 : przytnij(wybrana_paleta_ + delta, 0, 15);
            int stary = wybrana_paleta_; wybrana_paleta_ = n;
            uniewaznij_komorke(stary); uniewaznij_komorke(n); e.obsluzone = true; return;
        }
        if (k == 13U || k == 32U || (usb && (b == 40U || b == 44U))) {
            if (wybrana_paleta_ >= 0) ustaw_kolor(PALETA[wybrana_paleta_]);
            e.obsluzone = true; return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    static constexpr uint32_t PALETA[LICZBA_PALETY] = {
        WIDOCZNA_CZERN, 0x00404040, 0x00808080, 0x00FFFFFF,
        0x00CC2020, 0x00E67E22, 0x00FFBF00, 0x0020A040,
        0x002070D0, 0x006040C0, 0x00C030A0, 0x00794400,
        0x001A0B00, 0x00391B02, 0x00F2E2C4, 0x00000000
    };
    static int cyfra_hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    void synchronizuj_hex() {
        char b[8] = "#000000";
        uint32_t v = kolor_ & 0x00FFFFFFU;
        static const char h[] = "0123456789ABCDEF";
        for (int i = 0; i < 6; ++i) b[1 + i] = h[(v >> ((5 - i) * 4)) & 0xFU];
        if (pole_hex_ && !rowne(pole_hex_->tekst(), b)) pole_hex_->ustaw_tekst(b);
    }
    void zatwierdz_hex() {
        if (!ustaw_hex(pole_hex_->tekst())) synchronizuj_hex();
    }
    static bool rowne(const char* a, const char* b) {
        size_t i = 0; while (a && b && a[i] && b[i] && a[i] == b[i]) ++i;
        return a && b && a[i] == b[i];
    }
    Prostokat prostokat_podgladu() const {
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        return Prostokat(7 * s, 7 * s, 40 * s, 28 * s);
    }
    Prostokat prostokat_palety() const {
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        return Prostokat(7 * s, 45 * s, obszar_.szerokosc - 14 * s, obszar_.wysokosc - 52 * s);
    }
    int indeks_palety(int x, int y) const {
        Prostokat p = prostokat_palety(); if (!p.zawiera(x, y)) return -1;
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_; int gap = 3 * s;
        int cell = (p.szerokosc - 7 * gap) / 8; if (cell <= 0) return -1;
        int col = (x - p.x) / (cell + gap), row = (y - p.y) / (cell + gap);
        if (col < 0 || col >= 8 || row < 0 || row >= 2) return -1;
        int cx = p.x + col * (cell + gap), cy = p.y + row * (cell + gap);
        if (x >= cx + cell || y >= cy + cell) return -1;
        return row * 8 + col;
    }
    void uniewaznij_komorke(int i) {
        if (i < 0 || i >= 16) return;
        Prostokat p = prostokat_palety(); int s = skala_dpi_ < 1 ? 1 : skala_dpi_; int gap = 3 * s;
        int cell = (p.szerokosc - 7 * gap) / 8;
        uniewaznij_obszar(Prostokat(p.x + (i % 8) * (cell + gap), p.y + (i / 8) * (cell + gap), cell, cell));
    }

    uint32_t kolor_{0x00FFBF00U};
    int wybrana_paleta_{6};
    int najechana_paleta_{-1};
    int wcisnieta_paleta_{-1};
    PoleHex* pole_hex_{nullptr};
    FunkcjaZmiany cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
};

} // namespace BursztynUI
