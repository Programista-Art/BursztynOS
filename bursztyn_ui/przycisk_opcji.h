#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class PrzyciskOpcji;

class GrupaOpcji {
public:
    static constexpr size_t MAKS_OPCJI = 64;

    bool dodaj(PrzyciskOpcji* opcja);
    bool usun(PrzyciskOpcji* opcja);
    void ustaw_dopuszczanie_braku(bool dopuszcza);
    bool czy_dopuszcza_brak() const { return dopuszcza_brak_; }
    PrzyciskOpcji* zaznaczona_opcja() const;
    bool wybierz(PrzyciskOpcji* opcja);
    bool wybierz_sasiednia(PrzyciskOpcji* od, int kierunek);

private:
    PrzyciskOpcji* opcje_[MAKS_OPCJI]{nullptr};
    size_t liczba_opcji_{0};
    bool dopuszcza_brak_{false};
};

class PrzyciskOpcji : public Kontrolka {
public:
    using FunkcjaZmiany = void (*)(PrzyciskOpcji* nadawca, bool zaznaczony,
                                   void* dane_uzytkownika);

    explicit PrzyciskOpcji(const char* tekst = "", int64_t wartosc = 0)
        : wartosc_(wartosc) {
        zaktualizuj_motyw();
        wypelnienie_ = Marginesy(2, 2, 2, 2);
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        ustaw_tekst(tekst);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_wyboru;
        styl_.normalny.tlo = 0;
        styl_.najechany.tlo = 0;
        styl_.fokus.tlo = 0;
        uniewaznij();
    }

    ~PrzyciskOpcji() override {
        if (grupa_) grupa_->usun(this);
    }

    bool czy_zaznaczony() const { return zaznaczony_; }
    void ustaw_zaznaczony(bool zaznaczony) {
        if (zaznaczony && grupa_) {
            grupa_->wybierz(this);
            return;
        }
        if (!zaznaczony && grupa_ && !grupa_->czy_dopuszcza_brak() &&
            grupa_->zaznaczona_opcja() == this) return;
        ustaw_zaznaczony_z_grupy(zaznaczony);
    }

    void ustaw_wartosc(int64_t wartosc) { wartosc_ = wartosc; }
    int64_t wartosc() const { return wartosc_; }

    void ustaw_grupe(GrupaOpcji* grupa) {
        if (grupa_ == grupa) return;
        if (grupa_) grupa_->usun(this);
        grupa_ = grupa;
        if (grupa_) grupa_->dodaj(this);
    }
    GrupaOpcji* grupa() const { return grupa_; }

    void przy_zmianie(FunkcjaZmiany cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI s = skala_dpi_;
        int znak = 18 * s;
        return Rozmiar(znak + 7 * s + CzcionkaUI::zmierz_tekst(tekst_, s) +
                       wypelnienie_.poziomo() * s,
                       (znak > CzcionkaUI::pobierz_wysokosc(s) ? znak : CzcionkaUI::pobierz_wysokosc(s)) +
                       wypelnienie_.pionowo() * s);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        rysuj_opcje(ctx);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Prostokat znak = prostokat_znacznika();
        Punkt p(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        bool nowy = czy_wlaczona_ && znak.zawiera(p);
        if (nowy != hover_znacznika_) {
            hover_znacznika_ = nowy;
            uniewaznij_obszar(znak);
        }
        Kontrolka::obsluz_ruch_myszy(e);
    }

    void obsluz_opuszczenie_myszy() override {
        if (hover_znacznika_) {
            hover_znacznika_ = false;
            uniewaznij_obszar(prostokat_znacznika());
        }
        Kontrolka::obsluz_opuszczenie_myszy();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        wcisniety_wewnatrz_ = true;
        czy_wcisnieta_ = true;
        uniewaznij_obszar(prostokat_znacznika());
        e.obsluzone = true;
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!e.czy_lewy_przycisk()) return;
        bool aktywuj = wcisniety_wewnatrz_ &&
                       obszar_ekranu_.zawiera(e.x, e.y) && czy_wlaczona_;
        wcisniety_wewnatrz_ = false;
        czy_wcisnieta_ = false;
        uniewaznij_obszar(prostokat_znacznika());
        if (aktywuj) ustaw_zaznaczony(true);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        uint32_t baza = (e.kod_klawisza & BWS_KEY_SOURCE_USB)
                      ? (e.kod_klawisza & 0xFFU) : (e.kod_klawisza & 0x7FU);
        if (e.kod_klawisza == ' ' || baza == 0x39U ||
            ((e.kod_klawisza & BWS_KEY_SOURCE_USB) && baza == 44U)) {
            ustaw_zaznaczony(true);
            e.obsluzone = true;
            return;
        }
        int kierunek = 0;
        if ((e.kod_klawisza & BWS_KEY_SOURCE_USB) != 0) {
            if (baza == 80U || baza == 82U) kierunek = -1;
            if (baza == 79U || baza == 81U) kierunek = 1;
        } else {
            if (baza == 0x4BU || baza == 0x48U) kierunek = -1;
            if (baza == 0x4DU || baza == 0x50U) kierunek = 1;
        }
        if (kierunek && grupa_ && grupa_->wybierz_sasiednia(this, kierunek)) {
            e.obsluzone = true;
            return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

protected:
    friend class GrupaOpcji;

    void ustaw_zaznaczony_z_grupy(bool zaznaczony) {
        if (zaznaczony_ == zaznaczony) return;
        zaznaczony_ = zaznaczony;
        uniewaznij_obszar(prostokat_znacznika());
        if (cb_zmiana_) cb_zmiana_(this, zaznaczony_, dane_zmiany_);
        if (cb_zmiana_wartosci_) cb_zmiana_wartosci_(this, dane_zmiany_wartosci_);
    }

    Prostokat prostokat_znacznika() const {
        SkalaUI s = skala_dpi_;
        int roz = 18 * s;
        int x = wypelnienie_.lewy * s;
        int y = (obszar_.wysokosc - roz) / 2;
        if (y < 0) y = 0;
        return Prostokat(x, y, roz, roz);
    }

public:
    // Rysowanie zdefiniowane osobno, aby cala etykieta pozostala neutralna na hover.
    void rysuj_opcje(KontekstRysowania& ctx) {
        SkalaUI s = skala_dpi_;
        Prostokat z = prostokat_znacznika();
        uint32_t ramka = !czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY)
                         : hover_znacznika_ ? motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY)
                                           : motyw_kolor(MOTYW_KOLOR_RAMKA);
        uint32_t tlo = !czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_PANEL)
                     : czy_wcisnieta_ ? motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY)
                                      : motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        ctx.rysuj_zaokraglony_prostokat(z, tlo, z.szerokosc / 2);
        ctx.rysuj_zaokraglona_ramke(z, ramka, z.szerokosc / 2, s);
        if (zaznaczony_) {
            Prostokat k = z.zmniejszony(Marginesy(5 * s));
            ctx.rysuj_zaokraglony_prostokat(k,
                czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY),
                k.szerokosc / 2);
        }
        int tx = z.prawy() + 7 * s;
        int ty = (obszar_.wysokosc - CzcionkaUI::pobierz_wysokosc(s)) / 2;
        ctx.rysuj_tekst(Punkt(tx, ty), tekst_,
                        czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_TEKST) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY), s);
        if (czy_ma_fokus_) ctx.rysuj_ramke(z.powiekszony(Marginesy(2 * s)),
                                           motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY), s);
    }

private:
    int64_t wartosc_{0};
    bool zaznaczony_{false};
    bool hover_znacznika_{false};
    bool wcisniety_wewnatrz_{false};
    GrupaOpcji* grupa_{nullptr};
    FunkcjaZmiany cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
};

inline bool GrupaOpcji::dodaj(PrzyciskOpcji* opcja) {
    if (!opcja || liczba_opcji_ >= MAKS_OPCJI) return false;
    for (size_t i = 0; i < liczba_opcji_; ++i) if (opcje_[i] == opcja) return true;
    opcje_[liczba_opcji_++] = opcja;
    opcja->grupa_ = this;
    if (!dopuszcza_brak_ && !zaznaczona_opcja()) wybierz(opcja);
    else if (opcja->czy_zaznaczony()) wybierz(opcja);
    return true;
}

inline bool GrupaOpcji::usun(PrzyciskOpcji* opcja) {
    for (size_t i = 0; i < liczba_opcji_; ++i) {
        if (opcje_[i] != opcja) continue;
        bool byla = opcja->czy_zaznaczony();
        opcja->grupa_ = nullptr;
        for (size_t j = i; j + 1 < liczba_opcji_; ++j) opcje_[j] = opcje_[j + 1];
        opcje_[--liczba_opcji_] = nullptr;
        if (byla && !dopuszcza_brak_ && liczba_opcji_ > 0) wybierz(opcje_[0]);
        return true;
    }
    return false;
}

inline void GrupaOpcji::ustaw_dopuszczanie_braku(bool dopuszcza) {
    dopuszcza_brak_ = dopuszcza;
    if (!dopuszcza_brak_ && !zaznaczona_opcja() && liczba_opcji_ > 0) wybierz(opcje_[0]);
}

inline PrzyciskOpcji* GrupaOpcji::zaznaczona_opcja() const {
    for (size_t i = 0; i < liczba_opcji_; ++i)
        if (opcje_[i] && opcje_[i]->czy_zaznaczony()) return opcje_[i];
    return nullptr;
}

inline bool GrupaOpcji::wybierz(PrzyciskOpcji* opcja) {
    bool znaleziona = false;
    for (size_t i = 0; i < liczba_opcji_; ++i) if (opcje_[i] == opcja) znaleziona = true;
    if (!znaleziona) return false;
    for (size_t i = 0; i < liczba_opcji_; ++i)
        opcje_[i]->ustaw_zaznaczony_z_grupy(opcje_[i] == opcja);
    return true;
}

inline bool GrupaOpcji::wybierz_sasiednia(PrzyciskOpcji* od, int kierunek) {
    if (!od || liczba_opcji_ == 0) return false;
    size_t start = liczba_opcji_;
    for (size_t i = 0; i < liczba_opcji_; ++i) if (opcje_[i] == od) start = i;
    if (start == liczba_opcji_) return false;
    for (size_t krok = 1; krok <= liczba_opcji_; ++krok) {
        size_t i = kierunek < 0
                 ? (start + liczba_opcji_ - (krok % liczba_opcji_)) % liczba_opcji_
                 : (start + krok) % liczba_opcji_;
        if (opcje_[i] && opcje_[i]->czy_wlaczona_w_drzewie() && opcje_[i]->czy_widoczna_w_drzewie()) {
            wybierz(opcje_[i]);
            return true;
        }
    }
    return false;
}

} // namespace BursztynUI
