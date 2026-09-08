#pragma once

#include "kontrolka.h"
#include "panel.h"
#include "pasek_przewijania.h"

namespace BursztynUI {

enum class TrybPaskaPrzewijania {
    Nigdy,
    Automatycznie,
    Zawsze
};

class WidokPrzewijany : public Kontrolka {
public:
    WidokPrzewijany() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = Kolory::PowierzchniaTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = false;
        preferowany_rozmiar_ = Rozmiar(200, 200);

        zawartosc_ = new Panel();
        zawartosc_->ustaw_kolor_tla(0);
        zawartosc_->ustaw_grubosc_ramki(0);
        zawartosc_->ustaw_wypelnienie(Marginesy(0));
        zawartosc_->ustaw_uklad_automatyczny(false);
        dodaj_kontrolke(zawartosc_);

        pasek_pionowy_ = new PasekPrzewijania();
        pasek_pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        pasek_pionowy_->pokaz_strzalki(true);
        dodaj_kontrolke(pasek_pionowy_);

        pasek_poziomy_ = new PasekPrzewijania();
        pasek_poziomy_->ustaw_orientacje(Orientacja::Pozioma);
        pasek_poziomy_->pokaz_strzalki(true);
        dodaj_kontrolke(pasek_poziomy_);

        pasek_pionowy_->model().przy_zmianie([](ModelPrzewijania*, int64_t, void* dane) {
            WidokPrzewijany* w = static_cast<WidokPrzewijany*>(dane);
            if (w) w->zaktualizuj_pozycje_zawartosci();
        }, this);

        pasek_poziomy_->model().przy_zmianie([](ModelPrzewijania*, int64_t, void* dane) {
            WidokPrzewijany* w = static_cast<WidokPrzewijany*>(dane);
            if (w) w->zaktualizuj_pozycje_zawartosci();
        }, this);
    }

    template <typename T, typename... Args>
    T& dodaj(Args&&... args) {
        return zawartosc_->dodaj<T>(static_cast<Args&&>(args)...);
    }

    bool dodaj_do_zawartosci(Kontrolka* k) {
        return zawartosc_->dodaj_kontrolke(k);
    }

    Panel& zawartosc() { return *zawartosc_; }
    const Panel& zawartosc() const { return *zawartosc_; }

    void ustaw_tryb_paska_poziomego(TrybPaskaPrzewijania tryb) {
        if (tryb_poziomy_ != tryb) {
            tryb_poziomy_ = tryb;
            przelicz_uklad();
            uniewaznij();
        }
    }
    TrybPaskaPrzewijania tryb_paska_poziomego() const { return tryb_poziomy_; }

    void ustaw_tryb_paska_pionowego(TrybPaskaPrzewijania tryb) {
        if (tryb_pionowy_ != tryb) {
            tryb_pionowy_ = tryb;
            przelicz_uklad();
            uniewaznij();
        }
    }
    TrybPaskaPrzewijania tryb_paska_pionowego() const { return tryb_pionowy_; }

    void ustaw_rozmiar_zawartosci(Rozmiar r) {
        rozmiar_zawartosci_ = r;
        auto_rozmiar_ = false;
        przelicz_uklad();
        uniewaznij();
    }
    Rozmiar rozmiar_zawartosci() const { return rozmiar_zawartosci_; }

    void ustaw_automatyczny_rozmiar_zawartosci(bool a) {
        if (auto_rozmiar_ != a) {
            auto_rozmiar_ = a;
            przelicz_uklad();
            uniewaznij();
        }
    }
    bool czy_automatyczny_rozmiar_zawartosci() const { return auto_rozmiar_; }

    int64_t offset_x() const { return pasek_poziomy_->wartosc(); }
    int64_t offset_y() const { return pasek_pionowy_->wartosc(); }

    void przewin_do(int64_t x, int64_t y) {
        pasek_poziomy_->ustaw_wartosc(x);
        pasek_pionowy_->ustaw_wartosc(y);
    }

    void przewin_o(int64_t dx, int64_t dy) {
        if (dx != 0) pasek_poziomy_->przewin_o(dx);
        if (dy != 0) pasek_pionowy_->przewin_o(dy);
    }

    void przewin_do_kontrolki(const Kontrolka* k) {
        if (!k || !zawartosc_) return;
        Prostokat ke = k->obszar_ekranu();
        Prostokat ze = zawartosc_->obszar_ekranu();
        int kx = ke.x - ze.x;
        int ky = ke.y - ze.y;

        int cur_x = static_cast<int>(offset_x());
        int cur_y = static_cast<int>(offset_y());

        if (kx < cur_x) cur_x = kx;
        else if (kx + ke.szerokosc > cur_x + widok_.szerokosc) {
            cur_x = kx + ke.szerokosc - widok_.szerokosc;
        }

        if (ky < cur_y) cur_y = ky;
        else if (ky + ke.wysokosc > cur_y + widok_.wysokosc) {
            cur_y = ky + ke.wysokosc - widok_.wysokosc;
        }

        przewin_do(cur_x, cur_y);
    }

    PasekPrzewijania& pasek_pionowy() { return *pasek_pionowy_; }
    const PasekPrzewijania& pasek_pionowy() const { return *pasek_pionowy_; }

    PasekPrzewijania& pasek_poziomy() { return *pasek_poziomy_; }
    const PasekPrzewijania& pasek_poziomy() const { return *pasek_poziomy_; }

    Prostokat prostokat_widoku() const { return widok_; }

    void ustaw_skale_dpi(SkalaUI s) override {
        szerokosc_paska_ = 14 * s;
        Kontrolka::ustaw_skale_dpi(s);
        przelicz_uklad();
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        przelicz_uklad();
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_) return;
        bool pion_widoczny = pasek_pionowy_->czy_widoczna();
        bool poziom_widoczny = pasek_poziomy_->czy_widoczna();

        if (e.czy_shift() && poziom_widoczny && e.delta_y != 0) {
            int64_t stary = pasek_poziomy_->wartosc();
            pasek_poziomy_->przewin_o(-e.delta_y * 24);
            if (pasek_poziomy_->wartosc() != stary) e.obsluzone = true;
        } else if (pion_widoczny && e.delta_y != 0) {
            int64_t stary = pasek_pionowy_->wartosc();
            pasek_pionowy_->przewin_o(-e.delta_y * 24);
            if (pasek_pionowy_->wartosc() != stary) e.obsluzone = true;
        } else if (poziom_widoczny && e.delta_x != 0) {
            int64_t stary = pasek_poziomy_->wartosc();
            pasek_poziomy_->przewin_o(-e.delta_x * 24);
            if (pasek_poziomy_->wartosc() != stary) e.obsluzone = true;
        }
    }

    Kontrolka* sprawdz_trafienie(Punkt pt) override {
        if (!czy_widoczna_ || !obszar_.zawiera(pt)) return nullptr;

        Punkt lokalny(pt.x - obszar_.x, pt.y - obszar_.y);

        if (pasek_pionowy_->czy_widoczna() && pasek_pionowy_->obszar().zawiera(lokalny)) {
            return pasek_pionowy_->sprawdz_trafienie(lokalny);
        }
        if (pasek_poziomy_->czy_widoczna() && pasek_poziomy_->obszar().zawiera(lokalny)) {
            return pasek_poziomy_->sprawdz_trafienie(lokalny);
        }

        if (widok_.zawiera(lokalny)) {
            Kontrolka* k = zawartosc_->sprawdz_trafienie(lokalny);
            return k ? k : this;
        }
        return this;
    }

    void uniewaznij_dziecko(Kontrolka* child, Prostokat r_ekranu) override {
        if (blokada_aktualizacji_ > 0) return;
        if (child == zawartosc_) {
            Prostokat r_widok = r_ekranu.przeciecie(widok_ekranu_);
            if (r_widok.czy_pusty()) return;
            czy_brudna_ = true;
            brudny_obszar_ = brudny_obszar_.suma(r_widok);
            brudne_regiony_.dodaj(r_widok);
            if (rodzic_) {
                rodzic_->uniewaznij_dziecko(this, r_widok);
            }
        } else {
            Kontrolka::uniewaznij_dziecko(child, r_ekranu);
        }
    }

    void rysuj(KontekstRysowania& ctx, bool wymus = false) override {
        if (!czy_widoczna_) return;

        Punkt przesuniecie_rodzica = ctx.pobierz_przesuniecie();
        Prostokat obszar_renderowania(przesuniecie_rodzica.x + obszar_.x,
                                      przesuniecie_rodzica.y + obszar_.y,
                                      obszar_.szerokosc, obszar_.wysokosc);
        Prostokat clip = ctx.pobierz_przyciecie();
        if (!obszar_renderowania.przecina(clip) && !wymus) {
            return;
        }

        ctx.dodaj_przesuniecie(Punkt(obszar_.x, obszar_.y));
        ctx.dodaj_przyciecie(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc));

        // Rysuj tło i ramkę WidokuPrzewijanego
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        uint32_t tlo = styl_.normalny.tlo;
        uint32_t ramka = styl_.normalny.ramka;
        if (tlo != 0) ctx.rysuj_prostokat(b, tlo);
        int gr = grubosc_ramki_geometrii();
        if (ramka != 0 && gr > 0) ctx.rysuj_ramke(b, ramka, gr);

        // Ogranicz do widoku i narysuj zawartość
        ctx.dodaj_przyciecie(widok_);
        zawartosc_->rysuj(ctx, wymus);
        ctx.zdejmij_przyciecie();

        // Narysuj paski przewijania
        if (pasek_pionowy_->czy_widoczna()) {
            pasek_pionowy_->rysuj(ctx, wymus);
        }
        if (pasek_poziomy_->czy_widoczna()) {
            pasek_poziomy_->rysuj(ctx, wymus);
        }

        // Róg
        if (pasek_pionowy_->czy_widoczna() && pasek_poziomy_->czy_widoczna()) {
            Prostokat rog(widok_.prawy(), widok_.dolny(),
                          szerokosc_paska_, szerokosc_paska_);
            ctx.rysuj_prostokat(rog, Kolory::PowierzchniaAlternatywna);
        }

        ctx.zdejmij_przyciecie();
        ctx.zdejmij_przesuniecie();

        wyczysc_brudny_obszar();
        czy_brudna_ = false;
    }

private:
    void przelicz_uklad() {
        int w = obszar_.szerokosc;
        int h = obszar_.wysokosc;
        if (w <= 2 || h <= 2) return;

        int ramka_gr = grubosc_ramki_geometrii();
        int max_w = w - 2 * ramka_gr;
        int max_h = h - 2 * ramka_gr;

        if (auto_rozmiar_ && zawartosc_) {
            Rozmiar zm = zawartosc_->zmierz(Rozmiar(max_w, 0));
            int cw = zm.szerokosc;
            int ch = zm.wysokosc;
            for (size_t i = 0; i < zawartosc_->liczba_dzieci(); ++i) {
                Kontrolka* d = zawartosc_->dziecko(i);
                if (d && d->czy_widoczna()) {
                    Prostokat dr = d->obszar();
                    if (dr.prawy() > cw) cw = dr.prawy();
                    if (dr.dolny() > ch) ch = dr.dolny();
                }
            }
            if (cw < max_w) cw = max_w;
            if (ch < max_h) ch = max_h;
            rozmiar_zawartosci_ = Rozmiar(cw, ch);
        }

        bool pion_wymagany = (tryb_pionowy_ == TrybPaskaPrzewijania::Zawsze) ||
                             (tryb_pionowy_ == TrybPaskaPrzewijania::Automatycznie && rozmiar_zawartosci_.wysokosc > max_h);
        int avail_w = pion_wymagany ? max_w - szerokosc_paska_ : max_w;

        bool poziom_wymagany = (tryb_poziomy_ == TrybPaskaPrzewijania::Zawsze) ||
                               (tryb_poziomy_ == TrybPaskaPrzewijania::Automatycznie && rozmiar_zawartosci_.szerokosc > avail_w);
        int avail_h = poziom_wymagany ? max_h - szerokosc_paska_ : max_h;

        if (!pion_wymagany && tryb_pionowy_ == TrybPaskaPrzewijania::Automatycznie && rozmiar_zawartosci_.wysokosc > avail_h) {
            pion_wymagany = true;
            avail_w = max_w - szerokosc_paska_;
        }

        widok_ = Prostokat(ramka_gr, ramka_gr, avail_w, avail_h);
        widok_ekranu_ = Prostokat(obszar_ekranu_.x + widok_.x, obszar_ekranu_.y + widok_.y,
                                  widok_.szerokosc, widok_.wysokosc);

        pasek_pionowy_->ustaw_widoczna(pion_wymagany);
        if (pion_wymagany) {
            pasek_pionowy_->rozmiesc(Prostokat(ramka_gr + avail_w,
                                              ramka_gr,
                                              szerokosc_paska_, avail_h));
            int64_t max_scroll = rozmiar_zawartosci_.wysokosc > avail_h ? rozmiar_zawartosci_.wysokosc - avail_h : 0;
            pasek_pionowy_->ustaw_zakres(0, max_scroll);
            pasek_pionowy_->ustaw_rozmiar_strony(avail_h);
            if (pasek_pionowy_->wartosc() > max_scroll) {
                pasek_pionowy_->ustaw_wartosc(max_scroll);
            }
        } else {
            pasek_pionowy_->ustaw_wartosc(0);
            pasek_pionowy_->ustaw_zakres(0, 0);
        }

        pasek_poziomy_->ustaw_widoczna(poziom_wymagany);
        if (poziom_wymagany) {
            pasek_poziomy_->rozmiesc(Prostokat(ramka_gr,
                                              ramka_gr + avail_h,
                                              avail_w, szerokosc_paska_));
            int64_t max_scroll = rozmiar_zawartosci_.szerokosc > avail_w ? rozmiar_zawartosci_.szerokosc - avail_w : 0;
            pasek_poziomy_->ustaw_zakres(0, max_scroll);
            pasek_poziomy_->ustaw_rozmiar_strony(avail_w);
            if (pasek_poziomy_->wartosc() > max_scroll) {
                pasek_poziomy_->ustaw_wartosc(max_scroll);
            }
        } else {
            pasek_poziomy_->ustaw_wartosc(0);
            pasek_poziomy_->ustaw_zakres(0, 0);
        }

        zaktualizuj_pozycje_zawartosci();
    }

    void zaktualizuj_pozycje_zawartosci() {
        if (!zawartosc_) return;
        int ox = static_cast<int>(pasek_poziomy_->wartosc());
        int oy = static_cast<int>(pasek_pionowy_->wartosc());

        int zx = widok_.x - ox;
        int zy = widok_.y - oy;
        int zw = rozmiar_zawartosci_.szerokosc > widok_.szerokosc ? rozmiar_zawartosci_.szerokosc : widok_.szerokosc;
        int zh = rozmiar_zawartosci_.wysokosc > widok_.wysokosc ? rozmiar_zawartosci_.wysokosc : widok_.wysokosc;

        zawartosc_->rozmiesc(Prostokat(zx, zy, zw, zh));
        uniewaznij_obszar(widok_);
    }

    Panel* zawartosc_{nullptr};
    PasekPrzewijania* pasek_pionowy_{nullptr};
    PasekPrzewijania* pasek_poziomy_{nullptr};

    TrybPaskaPrzewijania tryb_poziomy_{TrybPaskaPrzewijania::Automatycznie};
    TrybPaskaPrzewijania tryb_pionowy_{TrybPaskaPrzewijania::Automatycznie};

    Rozmiar rozmiar_zawartosci_{300, 300};
    bool auto_rozmiar_{true};
    int szerokosc_paska_{14};

    Prostokat widok_;
    Prostokat widok_ekranu_;
};

} // namespace BursztynUI
