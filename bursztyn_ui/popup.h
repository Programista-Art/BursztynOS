#pragma once

#include "kontrolka.h"
#include "rysowanie.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

enum class PowodZamknieciaPopup {
    Anulowanie,
    Zatwierdzenie,
    KlikPoza,
    Escape,
    WlascicielZamkniety,
    WlascicielZminimalizowany,
    ZmianaGeometrii
};

class ZarzadcaPopupow;

class PowierzchniaPopup : public Kontrolka {
public:
    using FunkcjaZamkniecia = void (*)(PowierzchniaPopup*, PowodZamknieciaPopup, void*);

    PowierzchniaPopup() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = Kolory::PowierzchniaTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = true;
    }

    ZarzadcaPopupow* zarzadca() const { return zarzadca_; }
    int poziom_popupu() const { return poziom_; }
    bool czy_otwarta() const { return zarzadca_ != nullptr; }

    void przy_zamknieciu(FunkcjaZamkniecia cb, void* dane = nullptr) {
        cb_zamkniecia_ = cb;
        dane_zamkniecia_ = dane;
    }

    virtual void po_otwarciu() {}
    virtual void po_zamknieciu(PowodZamknieciaPopup powod) {
        if (cb_zamkniecia_) {
            cb_zamkniecia_(this, powod, dane_zamkniecia_);
        }
    }

private:
    friend class ZarzadcaPopupow;
    ZarzadcaPopupow* zarzadca_{nullptr};
    int poziom_{-1};
    FunkcjaZamkniecia cb_zamkniecia_{nullptr};
    void* dane_zamkniecia_{nullptr};
};

class ZarzadcaPopupow {
public:
    static constexpr int MAKS_GLEBOKOSC = 8;

    ~ZarzadcaPopupow() {
        zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
    }

    void ustaw_geometrie_wlasciciela(Prostokat ekran, Prostokat praca) {
        wlasciciel_ = ekran;
        obszar_roboczy_ = praca;
    }

    void ustaw_skale_dpi(int skala) {
        skala_dpi_ = (skala < 1) ? 1 : skala;
    }

    bool czy_otwarty() const { return liczba_ > 0; }
    int liczba_powierzchni() const { return liczba_; }
    bool czy_ma_wpis_paska_zadan() const { return false; }
    bool czy_przechwytuje_mysz() const { return przechwycenie_ != nullptr; }
    Kontrolka* fokus_popupu() const { return fokus_popupu_; }
    Kontrolka* fokus_do_przywrocenia() const { return fokus_poprzedni_; }
    Prostokat prostokat_zarejestrowany_ekranu() const { return zarejestrowany_ekran_; }

    Prostokat umiesc_pod_kotwica(Prostokat kotwica_klienta, Rozmiar rozmiar,
                                  bool preferuj_dol = true) const {
        Prostokat k(wlasciciel_.x + kotwica_klienta.x, wlasciciel_.y + kotwica_klienta.y,
                    kotwica_klienta.szerokosc, kotwica_klienta.wysokosc);
        Prostokat d = obszar_roboczy_.przeciecie(wlasciciel_);
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        int dol = k.dolny();
        int gora = k.y - h;
        bool md = (dol + h <= d.dolny());
        bool mg = (gora >= d.y);
        int y = (preferuj_dol && md) || !mg ? dol : gora;
        int x = przytnij(k.x, d.x, d.prawy() - w);
        y = przytnij(y, d.y, d.dolny() - h);
        return Prostokat(x - wlasciciel_.x, y - wlasciciel_.y, w, h);
    }

    Prostokat umiesc_podmenu(Prostokat element_klienta, Rozmiar rozmiar) const {
        Prostokat e(wlasciciel_.x + element_klienta.x, wlasciciel_.y + element_klienta.y,
                    element_klienta.szerokosc, element_klienta.wysokosc);
        Prostokat d = obszar_roboczy_.przeciecie(wlasciciel_);
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        int x = (e.prawy() + w <= d.prawy()) ? e.prawy() : (e.x - w);
        x = przytnij(x, d.x, d.prawy() - w);
        int y = przytnij(e.y, d.y, d.dolny() - h);
        return Prostokat(x - wlasciciel_.x, y - wlasciciel_.y, w, h);
    }

    Prostokat umiesc_przy_punkcie_ekranu(int x, int y, Rozmiar rozmiar) const {
        Prostokat d = obszar_roboczy_.przeciecie(wlasciciel_);
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        x = przytnij(x, d.x, d.prawy() - w);
        y = przytnij(y, d.y, d.dolny() - h);
        return Prostokat(x - wlasciciel_.x, y - wlasciciel_.y, w, h);
    }

    bool otworz(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* poprzedni = nullptr,
                 Kontrolka* fokus = nullptr) {
        zamknij(PowodZamknieciaPopup::Anulowanie);
        fokus_poprzedni_ = poprzedni;
        return dodaj_wewnetrznie(p, obszar, fokus);
    }

    bool otworz_podmenu(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* fokus = nullptr) {
        return liczba_ < MAKS_GLEBOKOSC && dodaj_wewnetrznie(p, obszar, fokus);
    }

    void zamknij_od_poziomu(int poziom, PowodZamknieciaPopup powod = PowodZamknieciaPopup::Anulowanie) {
        if (poziom < 0) poziom = 0;
        if (poziom >= liczba_) return;
        for (int i = liczba_ - 1; i >= poziom; --i) {
            if (powierzchnie_[i]) {
                brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(powierzchnie_[i]->obszar_ekranu());
            }
        }
        if (przechwycenie_ && powierzchnia_dla(przechwycenie_) >= poziom &&
            przechwycenie_ != najechana_) {
            przechwycenie_->obsluz_opuszczenie_myszy();
        }
        if (najechana_ && powierzchnia_dla(najechana_) >= poziom) {
            najechana_->obsluz_opuszczenie_myszy();
        }
        for (int i = liczba_ - 1; i >= poziom; --i) {
            odlacz(i, powod);
        }
        liczba_ = poziom;
        najechana_ = nullptr;
        przechwycenie_ = nullptr;
        gui_ustaw_capture_myszy(false);
        aktualizuj_rejestracje();
        if (liczba_ > 0) {
            if (!fokus_popupu_) {
                ustaw_fokus_popupu(powierzchnie_[liczba_ - 1]);
            }
        }
    }

    void zamknij(PowodZamknieciaPopup powod = PowodZamknieciaPopup::Anulowanie) {
        if (!czy_otwarty()) return;
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(powierzchnie_[i]->obszar_ekranu());
            }
        }
        if (przechwycenie_ && przechwycenie_ != najechana_) {
            przechwycenie_->obsluz_opuszczenie_myszy();
        }
        if (najechana_) {
            najechana_->obsluz_opuszczenie_myszy();
        }
        najechana_ = nullptr;
        przechwycenie_ = nullptr;
        gui_ustaw_capture_myszy(false);
        for (int i = liczba_ - 1; i >= 0; --i) {
            odlacz(i, powod);
        }
        liczba_ = 0;
        zarejestrowany_ekran_ = Prostokat();
        fokus_poprzedni_ = nullptr;
    }

    bool obsluz_ruch(ZdarzenieMyszki& e) {
        if (!czy_otwarty()) return false;
        Kontrolka* hit = przechwycenie_ ? przechwycenie_ : trafienie(e.pozycja());
        if (!przechwycenie_ && hit != najechana_) {
            if (najechana_) najechana_->obsluz_opuszczenie_myszy();
            najechana_ = hit;
            if (najechana_) najechana_->obsluz_wejscie_myszy();
        }
        if (hit) hit->obsluz_ruch_myszy(e);
        e.obsluzone = (hit != nullptr);
        return e.obsluzone;
    }

    bool obsluz_wcisniecie(ZdarzenieMyszki& e) {
        if (!czy_otwarty()) return false;
        Kontrolka* hit = trafienie(e.pozycja());
        if (!hit) {
            zamknij(PowodZamknieciaPopup::KlikPoza);
            return false;
        }
        przechwycenie_ = hit;
        gui_ustaw_capture_myszy(true);
        if (hit->czy_moze_miec_fokus()) ustaw_fokus_popupu(hit);
        hit->obsluz_wcisniecie_myszy(e);
        e.obsluzone = true;
        return true;
    }

    bool obsluz_zwolnienie(ZdarzenieMyszki& e) {
        if (!czy_otwarty() && !przechwycenie_) return false;
        Kontrolka* hit = przechwycenie_ ? przechwycenie_ : trafienie(e.pozycja());
        przechwycenie_ = nullptr;
        gui_ustaw_capture_myszy(false);
        if (hit) hit->obsluz_zwolnienie_myszy(e);
        e.obsluzone = (hit != nullptr);
        return e.obsluzone;
    }

    bool obsluz_rolke(ZdarzenieRolki& e) {
        if (!czy_otwarty()) return false;
        for (Kontrolka* k = trafienie(e.pozycja()); k && !e.obsluzone; k = k->rodzic()) {
            k->obsluz_rolke(e);
        }
        return e.obsluzone;
    }

    bool obsluz_klawisz(ZdarzenieKlawiatury& e) {
        if (!czy_otwarty()) return false;
        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);
        if (kod == 27U || (usb && baza == 41U) || (!usb && baza == 0x01U)) {
            if (liczba_ > 1) {
                zamknij_od_poziomu(liczba_ - 1, PowodZamknieciaPopup::Escape);
            } else {
                zamknij(PowodZamknieciaPopup::Escape);
            }
            e.obsluzone = true;
            return true;
        }
        Kontrolka* cel = fokus_popupu_ ? fokus_popupu_ : powierzchnie_[liczba_ - 1];
        if (cel) cel->obsluz_wcisniecie_klawisza(e);
        return e.obsluzone;
    }

    bool obsluz_tekst(ZdarzenieWprowadzaniaTekstu& e) {
        if (!czy_otwarty() || !fokus_popupu_) return false;
        fokus_popupu_->obsluz_wprowadzenie_tekstu(e);
        return e.obsluzone;
    }

    bool czy_brudny() const {
        if (!brudny_po_zamknieciu_.czy_pusty()) return true;
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i] && powierzchnie_[i]->czy_brudna()) return true;
        }
        return false;
    }

    Prostokat brudny_obszar() const {
        Prostokat r = brudny_po_zamknieciu_;
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                r = r.suma(powierzchnie_[i]->pobierz_brudny_obszar());
            }
        }
        return r;
    }

    void rysuj(KontekstRysowania& ctx, bool wymus = false) {
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                powierzchnie_[i]->rysuj(ctx, wymus);
                powierzchnie_[i]->wyczysc_brudny_obszar();
            }
        }
        brudny_po_zamknieciu_ = Prostokat();
    }

private:
    bool dodaj_wewnetrznie(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* fokus) {
        if (liczba_ >= MAKS_GLEBOKOSC || obszar.czy_pusty()) return false;
        powierzchnie_[liczba_] = &p;
        p.zarzadca_ = this;
        p.poziom_ = liczba_;
        p.ustaw_skale_dpi(skala_dpi_);
        p.rozmiesc(obszar);
        ++liczba_;
        aktualizuj_rejestracje();
        ustaw_fokus_popupu(fokus ? fokus : &p);
        p.po_otwarciu();
        p.uniewaznij();
        return true;
    }

    void odlacz(int i, PowodZamknieciaPopup powod) {
        PowierzchniaPopup* p = powierzchnie_[i];
        powierzchnie_[i] = nullptr;
        if (!p) return;
        if (fokus_popupu_ && (fokus_popupu_ == p || czy_potomkiem(fokus_popupu_, p))) {
            fokus_popupu_->przy_utracie_fokusu();
            fokus_popupu_ = nullptr;
        }
        p->zarzadca_ = nullptr;
        p->poziom_ = -1;
        p->po_zamknieciu(powod);
    }

    static bool czy_potomkiem(Kontrolka* k, Kontrolka* korzen) {
        for (; k; k = k->rodzic()) {
            if (k == korzen) return true;
        }
        return false;
    }

    int powierzchnia_dla(Kontrolka* k) const {
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i] && czy_potomkiem(k, powierzchnie_[i])) return i;
        }
        return -1;
    }

    void ustaw_fokus_popupu(Kontrolka* k) {
        if (fokus_popupu_ == k) return;
        if (fokus_popupu_) fokus_popupu_->przy_utracie_fokusu();
        fokus_popupu_ = k;
        if (fokus_popupu_) fokus_popupu_->przy_fokusie();
    }

    Kontrolka* trafienie(Punkt klient) const {
        for (int i = liczba_ - 1; i >= 0; --i) {
            if (powierzchnie_[i]) {
                Kontrolka* hit = powierzchnie_[i]->sprawdz_trafienie(klient);
                if (hit) return hit;
            }
        }
        return nullptr;
    }

    void aktualizuj_rejestracje() {
        if (liczba_ == 0) {
            zarejestrowany_ekran_ = Prostokat();
            return;
        }
        Prostokat suma;
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                suma = suma.suma(powierzchnie_[i]->obszar_ekranu());
            }
        }
        zarejestrowany_ekran_ = Prostokat(wlasciciel_.x + suma.x, wlasciciel_.y + suma.y,
                                          suma.szerokosc, suma.wysokosc);
    }

    PowierzchniaPopup* powierzchnie_[MAKS_GLEBOKOSC]{};
    int liczba_{0};
    int skala_dpi_{1};
    Prostokat wlasciciel_{};
    Prostokat obszar_roboczy_{};
    Prostokat zarejestrowany_ekran_{};
    Prostokat brudny_po_zamknieciu_{};
    Kontrolka* fokus_poprzedni_{nullptr};
    Kontrolka* fokus_popupu_{nullptr};
    Kontrolka* najechana_{nullptr};
    Kontrolka* przechwycenie_{nullptr};
};

} // namespace BursztynUI
