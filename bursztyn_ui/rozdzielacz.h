#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class Rozdzielacz : public Kontrolka {
public:
    using FunkcjaRozdzielacza = void (*)(Rozdzielacz* nadawca, int nowa_pozycja, void* dane_uzytkownika);

    explicit Rozdzielacz(Orientacja orientacja = Orientacja::Pozioma)
        : orientacja_(orientacja) {
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        preferowany_rozmiar_ = Rozmiar(-1, -1);
    }

    ~Rozdzielacz() override = default;

    Orientacja orientacja() const { return orientacja_; }
    void ustaw_orientacje(Orientacja o) {
        if (orientacja_ != o) {
            orientacja_ = o;
            uniewaznij();
        }
    }

    void ustaw_pierwszy(Kontrolka* k) {
        if (pierwszy_ && pierwszy_ != k) {
            usun_kontrolke(pierwszy_);
        }
        pierwszy_ = k;
        if (pierwszy_) {
            dodaj_kontrolke(pierwszy_);
        }
        uniewaznij();
    }

    void ustaw_drugi(Kontrolka* k) {
        if (drugi_ && drugi_ != k) {
            usun_kontrolke(drugi_);
        }
        drugi_ = k;
        if (drugi_) {
            dodaj_kontrolke(drugi_);
        }
        uniewaznij();
    }

    Kontrolka* pierwszy() const { return pierwszy_; }
    Kontrolka* drugi() const { return drugi_; }

    int pozycja_podzialu() const { return pozycja_podzialu_; }
    void ustaw_pozycje_podzialu(int poz) {
        int nowa = poz;
        int calkowity = orientacja_ == Orientacja::Pozioma
                      ? obszar_.szerokosc : obszar_.wysokosc;
        if (calkowity > 0)
            ogranicze_pozycje_podzialu_helper(
                nowa, calkowity, grubosc_paska_ * skala_dpi_);
        if (pozycja_podzialu_ == nowa) return;
        pozycja_podzialu_ = nowa;
        if (obszar_.szerokosc > 0 && obszar_.wysokosc > 0) {
            rozmiesc(obszar_);
        }
        uniewaznij();
    }

    void ustaw_minimalne_rozmiary(int min_pierwszy, int min_drugi) {
        min_pierwszy_ = min_pierwszy < 10 ? 10 : min_pierwszy;
        min_drugi_ = min_drugi < 10 ? 10 : min_drugi;
        ogranicz_pozycje_podzialu();
        uniewaznij();
    }

    int minimalny_rozmiar_pierwszego() const { return min_pierwszy_; }
    int minimalny_rozmiar_drugiego() const { return min_drugi_; }
    bool czy_przeciagany() const { return przeciganie_; }
    bool czy_pasek_najechany() const { return najechany_pasek_; }

    int grubosc_paska() const { return grubosc_paska_; }
    void ustaw_grubosc_paska(int g) {
        grubosc_paska_ = g < 2 ? 2 : g;
        uniewaznij();
    }

    void przy_zmianie_podzialu(FunkcjaRozdzielacza cb, void* dane = nullptr) {
        cb_zmiana_podzialu_ = cb;
        dane_zmiany_podzialu_ = dane;
    }

    // Geometria paska rozdzielacza w układzie lokalnym
    Prostokat obszar_paska() const {
        int g = grubosc_paska_ * skala_dpi_;
        int poz = pozycja_podzialu_ >= 0 ? pozycja_podzialu_ : domyslna_pozycja_podzialu();
        if (orientacja_ == Orientacja::Pozioma) {
            return Prostokat(poz, 0, g, obszar_.wysokosc);
        } else {
            return Prostokat(0, poz, obszar_.szerokosc, g);
        }
    }

    void rozmiesc(Prostokat docelowy_obszar) override {
        bool rozmiar_zmieniony =
            obszar_.szerokosc != docelowy_obszar.szerokosc ||
            obszar_.wysokosc != docelowy_obszar.wysokosc;
        obszar_ = docelowy_obszar;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        if (pozycja_podzialu_ < 0) {
            pozycja_podzialu_ = domyslna_pozycja_podzialu();
        }
        ogranicz_pozycje_podzialu();

        int g = grubosc_paska_ * skala_dpi_;
        if (orientacja_ == Orientacja::Pozioma) {
            Prostokat r1(0, 0, pozycja_podzialu_, obszar_.wysokosc);
            int r2_x = pozycja_podzialu_ + g;
            int r2_w = obszar_.szerokosc - r2_x;
            if (r2_w < 0) r2_w = 0;
            Prostokat r2(r2_x, 0, r2_w, obszar_.wysokosc);

            if (pierwszy_ && pierwszy_->czy_widoczna()) pierwszy_->rozmiesc(r1);
            if (drugi_ && drugi_->czy_widoczna()) drugi_->rozmiesc(r2);
        } else {
            Prostokat r1(0, 0, obszar_.szerokosc, pozycja_podzialu_);
            int r2_y = pozycja_podzialu_ + g;
            int r2_h = obszar_.wysokosc - r2_y;
            if (r2_h < 0) r2_h = 0;
            Prostokat r2(0, r2_y, obszar_.szerokosc, r2_h);

            if (pierwszy_ && pierwszy_->czy_widoczna()) pierwszy_->rozmiesc(r1);
            if (drugi_ && drugi_->czy_widoczna()) drugi_->rozmiesc(r2);
        }

        if (rozmiar_zmieniony) obsluz_zmiane_rozmiaru();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat pasek = obszar_paska();
        if (pasek.czy_pusty()) return;

        // Tło paska
        uint32_t tlo_paska = przeciganie_ ? 0x00E58A00 : (najechany_pasek_ ? 0x00503010 : 0x002B1D0E);
        ctx.rysuj_prostokat(pasek, tlo_paska);
        ctx.rysuj_ramke(pasek, Kolory::RamkaDomyslna);

        // Uchwyt w centrum paska (3 punkty)
        int cx = pasek.x + pasek.szerokosc / 2;
        int cy = pasek.y + pasek.wysokosc / 2;
        uint32_t kol_uchwytu = przeciganie_ ? 0x00FFFFFF : (najechany_pasek_ ? 0x00FFD700 : 0x008A5E2A);

        if (orientacja_ == Orientacja::Pozioma) {
            for (int d = -6; d <= 6; d += 6) {
                ctx.rysuj_prostokat(Prostokat(cx - 1, cy + d - 1, 3, 3), kol_uchwytu);
            }
        } else {
            for (int d = -6; d <= 6; d += 6) {
                ctx.rysuj_prostokat(Prostokat(cx + d - 1, cy - 1, 3, 3), kol_uchwytu);
            }
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        Prostokat pasek = obszar_paska();

        if (pasek.zawiera(lx, ly)) {
            przeciganie_ = true;
            mysz_start_ = (orientacja_ == Orientacja::Pozioma) ? lx : ly;
            podzial_start_ = pozycja_podzialu_;
            e.obsluzone = true;
            uniewaznij_obszar(pasek);
        }
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_zwolnienie_myszy(e);
        if (przeciganie_) {
            Prostokat pasek = obszar_paska();
            przeciganie_ = false;
            e.obsluzone = true;
            uniewaznij_obszar(pasek);
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_ruch_myszy(e);
        if (!czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        if (przeciganie_) {
            int delta = ((orientacja_ == Orientacja::Pozioma) ? lx : ly) - mysz_start_;
            int nowa_poz = podzial_start_ + delta;
            int stara_poz = pozycja_podzialu_;
            pozycja_podzialu_ = nowa_poz;
            ogranicz_pozycje_podzialu();

            if (pozycja_podzialu_ != stara_poz) {
                rozmiesc(obszar_);
                uniewaznij();
                if (cb_zmiana_podzialu_) {
                    cb_zmiana_podzialu_(this, pozycja_podzialu_, dane_zmiany_podzialu_);
                }
            }
            e.obsluzone = true;
        } else {
            bool hit = obszar_paska().zawiera(lx, ly);
            if (hit != najechany_pasek_) {
                najechany_pasek_ = hit;
                uniewaznij_obszar(obszar_paska());
            }
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_pasek_ && !przeciganie_) {
            Prostokat pasek = obszar_paska();
            najechany_pasek_ = false;
            uniewaznij_obszar(pasek);
        }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        Kontrolka::obsluz_wcisniecie_klawisza(e);
        if (!czy_wlaczona_) return;

        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        int krok = 10 * skala_dpi_;
        int stara_poz = pozycja_podzialu_;

        if (orientacja_ == Orientacja::Pozioma) {
            if ((usb && baza == 80U) || (e0 && baza == 0x4BU)) pozycja_podzialu_ -= krok; // Left
            else if ((usb && baza == 79U) || (e0 && baza == 0x4DU)) pozycja_podzialu_ += krok; // Right
        } else {
            if ((usb && baza == 82U) || (e0 && baza == 0x48U)) pozycja_podzialu_ -= krok; // Up
            else if ((usb && baza == 81U) || (e0 && baza == 0x50U)) pozycja_podzialu_ += krok; // Down
        }

        ogranicz_pozycje_podzialu();
        if (pozycja_podzialu_ != stara_poz) {
            rozmiesc(obszar_);
            uniewaznij();
            if (cb_zmiana_podzialu_) {
                cb_zmiana_podzialu_(this, pozycja_podzialu_, dane_zmiany_podzialu_);
            }
            e.obsluzone = true;
        }
    }

private:
    Orientacja orientacja_{Orientacja::Pozioma};
    Kontrolka* pierwszy_{nullptr};
    Kontrolka* drugi_{nullptr};

    int pozycja_podzialu_{-1};
    int min_pierwszy_{40};
    int min_drugi_{40};
    int grubosc_paska_{6};

    bool przeciganie_{false};
    bool najechany_pasek_{false};
    int mysz_start_{0};
    int podzial_start_{0};

    FunkcjaRozdzielacza cb_zmiana_podzialu_{nullptr};
    void* dane_zmiany_podzialu_{nullptr};

    int domyslna_pozycja_podzialu() const {
        int calkowity = (orientacja_ == Orientacja::Pozioma) ? obszar_.szerokosc : obszar_.wysokosc;
        int g = grubosc_paska_ * skala_dpi_;
        int poz = (calkowity - g) / 2;
        return poz > 0 ? poz : 0;
    }

    void ogranicze_pozycje_podzialu_helper(int& poz, int calkowity, int g) const {
        int min1 = min_pierwszy_ * skala_dpi_;
        int min2 = min_drugi_ * skala_dpi_;
        int max_poz = calkowity - g - min2;
        if (max_poz < min1) max_poz = min1;
        if (poz < min1) poz = min1;
        if (poz > max_poz) poz = max_poz;
    }

    void ogranicz_pozycje_podzialu() {
        int calkowity = (orientacja_ == Orientacja::Pozioma) ? obszar_.szerokosc : obszar_.wysokosc;
        int g = grubosc_paska_ * skala_dpi_;
        if (calkowity <= 0) return;
        ogranicze_pozycje_podzialu_helper(pozycja_podzialu_, calkowity, g);
    }
};

} // namespace BursztynUI
