#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "zdarzenia.h"
#include "motyw.h"
#include "rysowanie.h"

namespace BursztynUI {

class Kontrolka {
public:
    static constexpr size_t MAKS_DZIECI = 64;

    Kontrolka() = default;
    virtual ~Kontrolka() {
        wyczysc_kontrolki();
    }

    // Drzewo kontrolek
    Kontrolka* rodzic() const { return rodzic_; }
    size_t liczba_dzieci() const { return liczba_dzieci_; }
    Kontrolka* dziecko(size_t indeks) const {
        return (indeks < liczba_dzieci_) ? dzieci_[indeks] : nullptr;
    }

    bool dodaj_kontrolke(Kontrolka* kontrolka) {
        if (!kontrolka || kontrolka == this || liczba_dzieci_ >= MAKS_DZIECI) return false;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] == kontrolka) return true;
        }
        kontrolka->rodzic_ = this;
        kontrolka->ustaw_skale_dpi(skala_dpi_);
        dzieci_[liczba_dzieci_++] = kontrolka;
        uniewaznij();
        return true;
    }

    template <typename T, typename... Args>
    T& dodaj(Args&&... args) {
        T* k = new T(args...);
        dodaj_kontrolke(k);
        return *k;
    }

    bool usun_kontrolke(Kontrolka* kontrolka) {
        if (!kontrolka) return false;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] == kontrolka) {
                kontrolka->rodzic_ = nullptr;
                for (size_t j = i; j < liczba_dzieci_ - 1; ++j) {
                    dzieci_[j] = dzieci_[j + 1];
                }
                dzieci_[--liczba_dzieci_] = nullptr;
                uniewaznij();
                return true;
            }
        }
        return false;
    }

    void wyczysc_kontrolki() {
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i]) {
                dzieci_[i]->rodzic_ = nullptr;
                delete dzieci_[i];
                dzieci_[i] = nullptr;
            }
        }
        liczba_dzieci_ = 0;
        uniewaznij();
    }

    // Geometria i układ
    Prostokat obszar() const { return obszar_; }
    void ustaw_obszar(Prostokat r) {
        if (obszar_ != r) {
            obszar_ = r;
            uniewaznij();
        }
    }

    Prostokat obszar_ekranu() const { return obszar_ekranu_; }

    Rozmiar preferowany_rozmiar() const { return preferowany_rozmiar_; }
    void ustaw_preferowany_rozmiar(Rozmiar r) {
        preferowany_rozmiar_ = r;
        uniewaznij();
    }

    Rozmiar minimalny_rozmiar() const { return minimalny_rozmiar_; }
    void ustaw_minimalny_rozmiar(Rozmiar r) {
        minimalny_rozmiar_ = r;
        uniewaznij();
    }

    Rozmiar maksymalny_rozmiar() const { return maksymalny_rozmiar_; }
    void ustaw_maksymalny_rozmiar(Rozmiar r) {
        maksymalny_rozmiar_ = r;
        uniewaznij();
    }

    Marginesy margines() const { return margines_; }
    void ustaw_margines(Marginesy m) {
        margines_ = m;
        uniewaznij();
    }

    Marginesy wypelnienie() const { return wypelnienie_; }
    void ustaw_wypelnienie(Marginesy w) {
        wypelnienie_ = w;
        uniewaznij();
    }

    WyrownaniePoziome wyrownanie_poziome() const { return wyrownanie_poziome_; }
    void ustaw_wyrownanie_poziome(WyrownaniePoziome w) {
        wyrownanie_poziome_ = w;
        uniewaznij();
    }

    WyrownaniePionowe wyrownanie_pionowe() const { return wyrownanie_pionowe_; }
    void ustaw_wyrownanie_pionowe(WyrownaniePionowe w) {
        wyrownanie_pionowe_ = w;
        uniewaznij();
    }

    int skala_dpi() const { return skala_dpi_; }
    virtual void ustaw_skale_dpi(int s) {
        skala_dpi_ = (s < 1) ? 1 : s;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i]) dzieci_[i]->ustaw_skale_dpi(skala_dpi_);
        }
        uniewaznij();
    }

    // Stany
    bool czy_widoczna() const { return czy_widoczna_; }
    void ustaw_widoczna(bool v) {
        if (czy_widoczna_ != v) {
            czy_widoczna_ = v;
            uniewaznij();
        }
    }

    bool czy_wlaczona() const { return czy_wlaczona_; }
    void ustaw_wlaczona(bool e) {
        if (czy_wlaczona_ != e) {
            czy_wlaczona_ = e;
            uniewaznij();
        }
    }

    bool czy_najechana() const { return czy_najechana_; }
    bool czy_wcisnieta() const { return czy_wcisnieta_; }
    bool czy_ma_fokus() const { return czy_ma_fokus_; }
    bool czy_brudna() const { return czy_brudna_; }

    bool czy_moze_miec_fokus() const { return czy_moze_miec_fokus_; }
    void ustaw_mozliwosc_fokusu(bool f) { czy_moze_miec_fokus_ = f; }

    void uniewaznij() {
        czy_brudna_ = true;
        if (rodzic_) {
            rodzic_->uniewaznij();
        }
    }

    // Silnik Layoutu
    virtual Rozmiar zmierz(Rozmiar dostepny_rozmiar) {
        (void)dostepny_rozmiar;
        Rozmiar zadany = (preferowany_rozmiar_.szerokosc >= 0 && preferowany_rozmiar_.wysokosc >= 0)
                       ? Rozmiar(preferowany_rozmiar_.szerokosc * skala_dpi_, preferowany_rozmiar_.wysokosc * skala_dpi_)
                       : Rozmiar(0, 0);

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala_dpi_, maksymalny_rozmiar_.szerokosc * skala_dpi_);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala_dpi_, maksymalny_rozmiar_.wysokosc * skala_dpi_);
        return zadany;
    }

    virtual void rozmiesc(Prostokat docelowy_obszar) {
        obszar_ = docelowy_obszar;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz.x + obszar_.x, rodzic_poz.y + obszar_.y,
                                   obszar_.szerokosc, obszar_.wysokosc);

        Prostokat klient = Prostokat(wypelnienie_.lewy * skala_dpi_, wypelnienie_.gorny * skala_dpi_,
                                     obszar_.szerokosc - wypelnienie_.poziomo() * skala_dpi_ < 0 ? 0 : obszar_.szerokosc - wypelnienie_.poziomo() * skala_dpi_,
                                     obszar_.wysokosc - wypelnienie_.pionowo() * skala_dpi_ < 0 ? 0 : obszar_.wysokosc - wypelnienie_.pionowo() * skala_dpi_);

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] && dzieci_[i]->czy_widoczna()) {
                dzieci_[i]->rozmiesc(klient);
            }
        }
    }

    // Silnik Renderowania
    virtual void rysuj(KontekstRysowania& ctx, bool wymus = false) {
        if (!czy_widoczna_) return;
        bool nalezy_rysowac = czy_brudna_ || wymus;

        if (nalezy_rysowac) {
            ctx.dodaj_przesuniecie(Punkt(obszar_.x, obszar_.y));
            ctx.dodaj_przyciecie(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc));
            rysuj_siebie(ctx);
            ctx.zdejmij_przyciecie();
            ctx.zdejmij_przesuniecie();
        }

        ctx.dodaj_przesuniecie(Punkt(obszar_.x, obszar_.y));
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] && dzieci_[i]->czy_widoczna()) {
                dzieci_[i]->rysuj(ctx, nalezy_rysowac);
            }
        }
        ctx.zdejmij_przesuniecie();

        czy_brudna_ = false;
    }

    virtual void rysuj_siebie(KontekstRysowania& ctx) {
        (void)ctx;
    }

    // Sprawdzanie trafienia
    virtual Kontrolka* sprawdz_trafienie(Punkt pt) {
        if (!czy_widoczna_ || !obszar_.zawiera(pt)) return nullptr;

        Punkt lokalny(pt.x - obszar_.x, pt.y - obszar_.y);
        for (size_t i = liczba_dzieci_; i > 0; --i) {
            Kontrolka* child = dzieci_[i - 1];
            if (child && child->czy_widoczna()) {
                Kontrolka* hit = child->sprawdz_trafienie(lokalny);
                if (hit) return hit;
            }
        }
        return this;
    }

    // Obsługa Zdarzeń
    virtual void przy_najechaniu() { czy_najechana_ = true; uniewaznij(); }
    virtual void przy_opuszczeniu() { czy_najechana_ = false; czy_wcisnieta_ = false; uniewaznij(); }
    virtual void przy_wcisnieciu_myszy(ZdarzenieMyszki& e) { (void)e; }
    virtual void przy_zwolnieniu_myszy(ZdarzenieMyszki& e) { (void)e; }
    virtual void przy_ruchu_myszy(ZdarzenieMyszki& e) { (void)e; }
    virtual void przy_wcisnieciu_klawisza(ZdarzenieKlawiatury& e) { (void)e; }
    virtual void przy_zwolnieniu_klawisza(ZdarzenieKlawiatury& e) { (void)e; }
    virtual void przy_wprowadzeniu_tekstu(ZdarzenieWprowadzaniaTekstu& e) { (void)e; }
    virtual void przy_fokusie() { czy_ma_fokus_ = true; uniewaznij(); }
    virtual void przy_utracie_fokusu() { czy_ma_fokus_ = false; uniewaznij(); }

protected:
    Kontrolka* rodzic_{nullptr};
    Kontrolka* dzieci_[MAKS_DZIECI]{nullptr};
    size_t liczba_dzieci_{0};

    Prostokat obszar_{0, 0, 0, 0};
    Prostokat obszar_ekranu_{0, 0, 0, 0};

    Rozmiar minimalny_rozmiar_{0, 0};
    Rozmiar maksymalny_rozmiar_{10000, 10000};
    Rozmiar preferowany_rozmiar_{-1, -1};

    Marginesy margines_{0, 0, 0, 0};
    Marginesy wypelnienie_{0, 0, 0, 0};

    WyrownaniePoziome wyrownanie_poziome_{WyrownaniePoziome::Rozciagnij};
    WyrownaniePionowe wyrownanie_pionowe_{WyrownaniePionowe::Poczatek};

    bool czy_widoczna_{true};
    bool czy_wlaczona_{true};
    bool czy_najechana_{false};
    bool czy_wcisnieta_{false};
    bool czy_ma_fokus_{false};
    bool czy_brudna_{true};
    bool czy_moze_miec_fokus_{false};
    int skala_dpi_{1};
};

} // namespace BursztynUI
