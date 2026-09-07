#pragma once

#include "pole_tekstowe.h"

namespace BursztynUI {

class PoleWyszukiwania : public Kontrolka {
public:
    using FunkcjaTekstu = void (*)(PoleWyszukiwania*, const char*, void*);

private:
    class PoleWewnetrzne : public PoleTekstowe {
    public:
        explicit PoleWewnetrzne(PoleWyszukiwania* w)
            : PoleTekstowe("Szukaj…"), wlasciciel_(w) {}
        void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
            if (wlasciciel_ && wlasciciel_->obsluz_klawisz_specjalny(e)) return;
            PoleTekstowe::obsluz_wcisniecie_klawisza(e);
        }
    private:
        PoleWyszukiwania* wlasciciel_{nullptr};
    };

public:
    explicit PoleWyszukiwania(const char* placeholder = "Szukaj…") {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        czy_reaguje_wizualnie_na_najechanie_ = true;
        pole_ = new PoleWewnetrzne(this);
        pole_->ustaw_tekst_zastepczy(placeholder);
        pole_->przy_zmianie(zmieniono_pole, this);
        dodaj_kontrolke(pole_);
        preferowany_rozmiar_ = Rozmiar(240, 32);
    }

    const char* pobierz_tekst() const { return pole_->tekst(); }
    const char* tekst() const override { return pole_->tekst(); }
    const char* placeholder() const { return pole_->tekst_zastepczy(); }

    void ustaw_tekst(const char* tekst) override {
        if (tekst_rowny(pole_->tekst(), tekst ? tekst : "")) return;
        pole_->ustaw_tekst(tekst);
    }
    void ustawtekst(const char* tekst) { ustaw_tekst(tekst); }
    const char* pobierztekst() const { return pobierz_tekst(); }
    void ustaw_placeholder(const char* tekst) { pole_->ustaw_tekst_zastepczy(tekst); }
    void ustawplaceholder(const char* tekst) { ustaw_placeholder(tekst); }

    void wyczysc() {
        if (pole_->tekst()[0] == '\0') return;
        pole_->ustaw_tekst("");
    }

    void przy_zmianie(FunkcjaTekstu cb, void* dane = nullptr) {
        cb_zmiana_ = cb; dane_zmiany_ = dane;
    }
    void przyzmianie(FunkcjaTekstu cb, void* dane = nullptr) { przy_zmianie(cb, dane); }
    void przy_wyszukaniu(FunkcjaTekstu cb, void* dane = nullptr) {
        cb_wyszukanie_ = cb; dane_wyszukania_ = dane;
    }
    void przywyszukaniu(FunkcjaTekstu cb, void* dane = nullptr) { przy_wyszukaniu(cb, dane); }

    PoleTekstowe& pole_tekstowe() { return *pole_; }
    const PoleTekstowe& pole_tekstowe() const { return *pole_; }
    bool czy_przycisk_czyszczenia_widoczny() const { return pole_->tekst()[0] != '\0'; }

    void ustaw_wlaczona(bool wlaczona) {
        Kontrolka::ustaw_wlaczona(wlaczona);
        pole_->ustaw_wlaczona(wlaczona);
    }
    void ustaw_tylko_do_odczytu(bool tylko) {
        Kontrolka::ustaw_tylko_do_odczytu(tylko);
        pole_->ustaw_tylko_do_odczytu(tylko);
    }

    Rozmiar zmierz(Rozmiar) override {
        SkalaUI s = skala_dpi_;
        int w = preferowany_rozmiar_.szerokosc >= 0 ? preferowany_rozmiar_.szerokosc * s : 240 * s;
        int h = preferowany_rozmiar_.wysokosc >= 0 ? preferowany_rozmiar_.wysokosc * s : 32 * s;
        return Rozmiar(w, h);
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        SkalaUI s = skala_dpi_;
        int ikona = 28 * s;
        int prawa = czy_przycisk_czyszczenia_widoczny() ? 28 * s : 5 * s;
        int w = r.szerokosc - ikona - prawa;
        if (w < 0) w = 0;
        pole_->rozmiesc(Prostokat(ikona, 1 * s, w, r.wysokosc - 2 * s));
    }

    Kontrolka* sprawdz_trafienie(Punkt pt) override {
        if (!czy_widoczna_ || !obszar_.zawiera(pt)) return nullptr;
        Punkt lokalny(pt.x - obszar_.x, pt.y - obszar_.y);
        if (czy_przycisk_czyszczenia_widoczny() && prostokat_czyszczenia().zawiera(lokalny)) return this;
        Kontrolka* hit = pole_->sprawdz_trafienie(lokalny);
        return hit ? hit : this;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu st = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, st);
        SkalaUI s = skala_dpi_;
        // Lupa rysowana geometrycznie.
        int cx = 11 * s, cy = obszar_.wysokosc / 2 - 2 * s, r = 5 * s;
        ctx.rysuj_zaokraglona_ramke(Prostokat(cx - r, cy - r, 2 * r, 2 * r),
                                   st.tekst, r, s);
        ctx.rysuj_prostokat(Prostokat(cx + 3 * s, cy + 3 * s, 6 * s, 2 * s), st.tekst);
        if (czy_przycisk_czyszczenia_widoczny()) {
            Prostokat c = prostokat_czyszczenia();
            if (najechany_clear_ && czy_wlaczona_)
                ctx.rysuj_zaokraglony_prostokat(c.zmniejszony(Marginesy(3 * s)),
                                               0x00503A22, 4 * s);
            int mx = c.x + c.szerokosc / 2;
            int my = c.y + c.wysokosc / 2;
            for (int i = -3 * s; i <= 3 * s; ++i) {
                ctx.rysuj_prostokat(Prostokat(mx + i, my + i, s, s), st.tekst);
                ctx.rysuj_prostokat(Prostokat(mx + i, my - i, s, s), st.tekst);
            }
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        bool n = czy_przycisk_czyszczenia_widoczny() &&
                 prostokat_czyszczenia_ekran().zawiera(e.pozycja());
        if (n != najechany_clear_) {
            najechany_clear_ = n;
            uniewaznij_obszar(prostokat_czyszczenia());
        }
    }
    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_clear_) { najechany_clear_ = false; uniewaznij_obszar(prostokat_czyszczenia()); }
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ || !e.czy_lewy_przycisk()) return;
        if (prostokat_czyszczenia_ekran().zawiera(e.pozycja())) {
            clear_wcisniety_ = true; e.obsluzone = true;
            uniewaznij_obszar(prostokat_czyszczenia());
        }
    }
    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!clear_wcisniety_) return;
        bool hit = prostokat_czyszczenia_ekran().zawiera(e.pozycja());
        clear_wcisniety_ = false;
        if (hit) wyczysc();
        e.obsluzone = true;
    }

protected:
    virtual bool obsluz_klawisz_specjalny(ZdarzenieKlawiatury& e) {
        uint32_t k = e.kod_klawisza;
        bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        uint32_t b = usb ? (k & 0xFFU) : (k & 0x7FU);
        if (k == 13U || k == 10U || (usb && b == 40U)) {
            if (cb_wyszukanie_) cb_wyszukanie_(this, pole_->tekst(), dane_wyszukania_);
            e.obsluzone = true; return true;
        }
        if (k == 27U || (usb && b == 41U) || (!usb && b == 1U)) {
            if (pole_->tekst()[0] != '\0' && !czy_tylko_do_odczytu_) wyczysc();
            e.obsluzone = true; return true;
        }
        return false;
    }

private:
    static bool tekst_rowny(const char* a, const char* b) {
        if (!a || !b) return a == b;
        size_t i = 0;
        while (a[i] && b[i] && a[i] == b[i]) ++i;
        return a[i] == b[i];
    }
    static void zmieniono_pole(PoleTekstowe*, const char* tekst, void* dane) {
        PoleWyszukiwania* w = static_cast<PoleWyszukiwania*>(dane);
        if (!w) return;
        const bool ma_tekst = tekst && tekst[0] != '\0';
        // Szerokosc edytora zmienia sie tylko przy pojawieniu/zniknieciu X.
        // Kolejne znaki nie uruchamiaja ponownie ukladu calej kontrolki.
        if (w->ma_tekst_ != ma_tekst) {
            w->ma_tekst_ = ma_tekst;
            w->rozmiesc(w->obszar_);
            w->uniewaznij_obszar(w->prostokat_czyszczenia());
        }
        if (w->cb_zmiana_) w->cb_zmiana_(w, tekst, w->dane_zmiany_);
    }
    Prostokat prostokat_czyszczenia() const {
        int w = 28 * (skala_dpi_);
        return Prostokat(obszar_.szerokosc - w, 0, w, obszar_.wysokosc);
    }
    Prostokat prostokat_czyszczenia_ekran() const {
        Prostokat r = prostokat_czyszczenia();
        return Prostokat(obszar_ekranu_.x + r.x, obszar_ekranu_.y + r.y, r.szerokosc, r.wysokosc);
    }

    PoleWewnetrzne* pole_{nullptr};
    bool najechany_clear_{false};
    bool clear_wcisniety_{false};
    bool ma_tekst_{false};
    FunkcjaTekstu cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
    FunkcjaTekstu cb_wyszukanie_{nullptr};
    void* dane_wyszukania_{nullptr};
};

} // namespace BursztynUI
