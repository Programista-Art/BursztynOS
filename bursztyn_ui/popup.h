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
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = true;
    }
    ~PowierzchniaPopup() override;

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_menu;
        Kontrolka::zaktualizuj_motyw();
    }

    ZarzadcaPopupow* zarzadca() const { return zarzadca_; }
    int poziom_popupu() const { return poziom_; }
    bool czy_otwarta() const { return zarzadca_ != nullptr; }

    void przy_zamknieciu(FunkcjaZamkniecia cb, void* dane = nullptr) {
        cb_zamkniecia_ = cb;
        dane_zamkniecia_ = dane;
    }

    bool czy_modalna() const { return czy_modalna_; }
    void ustaw_modalna(bool m) { czy_modalna_ = m; }
    bool czy_zamyka_na_klik_poza() const { return czy_zamyka_na_klik_poza_; }
    void ustaw_zamykanie_na_klik_poza(bool z) { czy_zamyka_na_klik_poza_ = z; }

    virtual void po_otwarciu() {}
    virtual void po_zamknieciu(PowodZamknieciaPopup powod) {
        if (cb_zamkniecia_) {
            cb_zamkniecia_(this, powod, dane_zamkniecia_);
        }
    }

    virtual uint16_t promien_zaokraglenia() const {
        return motyw_promien(MOTYW_ROG_MENU);
    }

private:
    friend class ZarzadcaPopupow;
    ZarzadcaPopupow* zarzadca_{nullptr};
    int poziom_{-1};
    bool czy_modalna_{false};
    bool czy_zamyka_na_klik_poza_{true};
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

    void ustaw_skale_dpi(SkalaUI skala) {
        skala_dpi_ = skala;
    }

    void zaktualizuj_motyw() {
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(
                    obszar_z_cieniem(powierzchnie_[i]));
                powierzchnie_[i]->zaktualizuj_motyw();
            }
        }
    }

    bool czy_otwarty() const { return liczba_ > 0; }
    int liczba_powierzchni() const { return liczba_; }
    bool czy_ma_wpis_paska_zadan() const { return false; }
    bool czy_przechwytuje_mysz() const { return przechwycenie_ != nullptr; }
    Kontrolka* fokus_popupu() const { return fokus_popupu_; }
    Kontrolka* fokus_do_przywrocenia() const { return fokus_poprzedni_; }
    Prostokat prostokat_zarejestrowany_ekranu() const { return zarejestrowany_ekran_; }
    Prostokat obszar_roboczy_ekranu() const { return obszar_roboczy_; }
    Prostokat geometria_wlasciciela() const { return wlasciciel_; }
    Prostokat obszar_wlasciciela() const { return wlasciciel_; }
    SkalaUI skala_dpi() const { return skala_dpi_; }

    Prostokat umiesc_pod_kotwica_ekranu(Prostokat kotwica_ekranu, Rozmiar rozmiar,
                                         bool preferuj_dol = true) const {
        Prostokat k = kotwica_ekranu;
        Prostokat d = obszar_roboczy_;
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        int cien = motyw_cien_okna() ? 2 * skala_dpi_ : 0;
        int dol = k.dolny();
        int gora = k.y - h;
        bool md = (dol + h + cien <= d.dolny());
        bool mg = (gora >= d.y);
        int y = (preferuj_dol && md) || !mg ? dol : gora;
        int max_x = d.prawy() - w - cien; if (max_x < d.x) max_x = d.x;
        int max_y = d.dolny() - h - cien; if (max_y < d.y) max_y = d.y;
        int x = przytnij(k.x, d.x, max_x);
        y = przytnij(y, d.y, max_y);
        return Prostokat(x - wlasciciel_.x, y - wlasciciel_.y, w, h);
    }

    Prostokat umiesc_pod_kotwica(Prostokat kotwica_klienta, Rozmiar rozmiar,
                                  bool preferuj_dol = true) const {
        return umiesc_pod_kotwica_ekranu(
            Prostokat(wlasciciel_.x + kotwica_klienta.x, wlasciciel_.y + kotwica_klienta.y,
                      kotwica_klienta.szerokosc, kotwica_klienta.wysokosc),
            rozmiar, preferuj_dol);
    }

    Prostokat umiesc_podmenu_ekranu(Prostokat element_ekranu, Rozmiar rozmiar) const {
        Prostokat e = element_ekranu;
        Prostokat d = obszar_roboczy_;
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        int cien = motyw_cien_okna() ? 2 * skala_dpi_ : 0;
        int max_x = d.prawy() - w - cien; if (max_x < d.x) max_x = d.x;
        int max_y = d.dolny() - h - cien; if (max_y < d.y) max_y = d.y;
        int x = (e.prawy() + w + cien <= d.prawy()) ? e.prawy() : (e.x - w - cien);
        x = przytnij(x, d.x, max_x);
        int y = przytnij(e.y, d.y, max_y);
        return Prostokat(x - wlasciciel_.x, y - wlasciciel_.y, w, h);
    }

    Prostokat umiesc_podmenu(Prostokat element_klienta, Rozmiar rozmiar) const {
        return umiesc_podmenu_ekranu(
            Prostokat(wlasciciel_.x + element_klienta.x, wlasciciel_.y + element_klienta.y,
                      element_klienta.szerokosc, element_klienta.wysokosc),
            rozmiar);
    }

    Prostokat umiesc_przy_punkcie_ekranu(int x, int y, Rozmiar rozmiar) const {
        Prostokat d = obszar_roboczy_;
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        int cien = motyw_cien_okna() ? 2 * skala_dpi_ : 0;
        int max_x = d.prawy() - w - cien; if (max_x < d.x) max_x = d.x;
        int max_y = d.dolny() - h - cien; if (max_y < d.y) max_y = d.y;
        x = przytnij(x, d.x, max_x);
        y = przytnij(y, d.y, max_y);
        return Prostokat(x - wlasciciel_.x, y - wlasciciel_.y, w, h);
    }

    // Popup globalny (np. toast): wynik pozostaje lokalny wzgledem okna
    // wlasciciela, lecz ograniczenie odbywa sie w przestrzeni monitora.
    Prostokat umiesc_w_obszarze_roboczym_prawy_dol(Rozmiar rozmiar,
                                                   int margines_prawy,
                                                   int margines_dolny) const {
        Prostokat d = obszar_roboczy_;
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        int x = d.prawy() - margines_prawy - w;
        int y = d.dolny() - margines_dolny - h;
        if (x < d.x) x = d.x;
        if (y < d.y) y = d.y;
        return Prostokat(x - wlasciciel_.x, y - wlasciciel_.y, w, h);
    }

    bool przemiesc_powierzchnie(PowierzchniaPopup& p, Prostokat obszar,
                                bool odswiez_rejestracje = true) {
        if (powierzchnia_dla(&p) < 0 || obszar.czy_pusty()) return false;
        brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(obszar_z_cieniem(&p));
        p.rozmiesc(obszar);
        brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(obszar_z_cieniem(&p));
        if (odswiez_rejestracje) aktualizuj_rejestracje();
        p.uniewaznij();
        return true;
    }

    // Przy zmianie polozenia kilku popupow rejestracja musi nastapic raz,
    // po ustaleniu calej geometrii. Kazda rejestracja tworzy nowa globalna
    // powierzchnie kompozytora, wiec aktualizacja posrednia tracilaby tresc.
    void odswiez_rejestracje_popupow() { aktualizuj_rejestracje(); }

    bool otworz(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* poprzedni = nullptr,
                 Kontrolka* fokus = nullptr) {
        zamknij(PowodZamknieciaPopup::Anulowanie);
        fokus_poprzedni_ = poprzedni;
        return dodaj_wewnetrznie(p, obszar, fokus);
    }

    bool otworz_podmenu(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* fokus = nullptr) {
        return liczba_ < MAKS_GLEBOKOSC && dodaj_wewnetrznie(p, obszar, fokus);
    }

    bool dodaj_powierzchnie(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* fokus = nullptr) {
        return liczba_ < MAKS_GLEBOKOSC && dodaj_wewnetrznie(p, obszar, fokus);
    }

    bool zamknij_powierzchnie(PowierzchniaPopup* p, PowodZamknieciaPopup powod = PowodZamknieciaPopup::Anulowanie) {
        if (!p) return false;
        int idx = -1;
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i] == p) { idx = i; break; }
        }
        if (idx < 0) return false;
        brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(obszar_z_cieniem(powierzchnie_[idx]));
        if (przechwycenie_ && czy_potomkiem(przechwycenie_, p)) {
            przechwycenie_->obsluz_opuszczenie_myszy();
            przechwycenie_ = nullptr;
            gui_ustaw_capture_myszy(false);
        }
        if (najechana_ && czy_potomkiem(najechana_, p)) {
            najechana_->obsluz_opuszczenie_myszy();
            najechana_ = nullptr;
        }
        odlacz(idx, powod);
        for (int i = idx; i + 1 < liczba_; ++i) {
            powierzchnie_[i] = powierzchnie_[i + 1];
            if (powierzchnie_[i]) powierzchnie_[i]->poziom_ = i;
        }
        powierzchnie_[--liczba_] = nullptr;
        aktualizuj_rejestracje();
        if (liczba_ > 0 && !fokus_popupu_) {
            ustaw_fokus_popupu(powierzchnie_[liczba_ - 1]);
        }
        return true;
    }

    void zamknij_od_poziomu(int poziom, PowodZamknieciaPopup powod = PowodZamknieciaPopup::Anulowanie) {
        if (poziom < 0) poziom = 0;
        if (poziom >= liczba_) return;
        for (int i = liczba_ - 1; i >= poziom; --i) {
            if (powierzchnie_[i]) {
                brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(obszar_z_cieniem(powierzchnie_[i]));
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
                brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(obszar_z_cieniem(powierzchnie_[i]));
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
        aktualizuj_rejestracje();
        fokus_poprzedni_ = nullptr;
    }

    void ustaw_fokus_popupu(Kontrolka* k) {
        if (fokus_popupu_ == k) return;
        if (fokus_popupu_) fokus_popupu_->przy_utracie_fokusu();
        fokus_popupu_ = k;
        if (fokus_popupu_) fokus_popupu_->przy_fokusie();
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

    bool czy_wymaga_animacji() const {
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i] && powierzchnie_[i]->czy_wymaga_animacji()) return true;
        }
        return false;
    }

    void krok_animacji(uint64_t teraz_ns = 0) {
        // Powierzchnia moze zamknac sie w kroku i skrocic tablice.
        for (int i = liczba_ - 1; i >= 0; --i) {
            if (powierzchnie_[i] && powierzchnie_[i]->czy_wymaga_animacji())
                powierzchnie_[i]->krok_animacji(teraz_ns);
        }
    }

    bool obsluz_wcisniecie(ZdarzenieMyszki& e) {
        if (!czy_otwarty()) return false;
        Kontrolka* hit = trafienie(e.pozycja());
        if (!hit) {
            bool ma_modalny = false;
            for (int i = liczba_ - 1; i >= 0; --i) {
                if (powierzchnie_[i] && powierzchnie_[i]->czy_modalna()) {
                    ma_modalny = true;
                    break;
                }
            }
            if (ma_modalny) {
                e.obsluzone = true;
                return true;
            }
            int zamknij_od = -1;
            for (int i = liczba_ - 1; i >= 0; --i) {
                if (powierzchnie_[i] && powierzchnie_[i]->czy_zamyka_na_klik_poza()) {
                    zamknij_od = i;
                } else if (zamknij_od >= 0) {
                    break;
                }
            }
            if (zamknij_od >= 0) {
                zamknij_od_poziomu(zamknij_od, PowodZamknieciaPopup::KlikPoza);
                // Klik zamykajacy popup nie moze przejsc do kontrolki lezacej
                // pod powierzchnia (szczegolnie nie moze otworzyc jej menu).
                e.obsluzone = true;
                return true;
            }
            // Niemodalne powierzchnie, ktore nie zamykaja sie po kliknieciu
            // poza nimi (toasty), nie moga blokowac kontrolek wlasciciela.
            e.obsluzone = false;
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
        Kontrolka* cel = fokus_popupu_ ? fokus_popupu_ : powierzchnie_[liczba_ - 1];
        if (cel) {
            cel->obsluz_wcisniecie_klawisza(e);
            if (e.obsluzone) return true;
        }
        PowierzchniaPopup* surf = powierzchnie_[liczba_ - 1];
        if (surf && cel != surf) {
            surf->obsluz_wcisniecie_klawisza(e);
            if (e.obsluzone) return true;
        }
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
                if (motyw_cien_okna()) {
                    Prostokat r = powierzchnie_[i]->obszar_ekranu();
                    int cien = 2 * skala_dpi_;
                    int promien_cienia = powierzchnie_[i]->promien_zaokraglenia() * skala_dpi_;
                    ctx.rysuj_zaokraglony_prostokat(
                        Prostokat(r.x + cien, r.y + cien, r.szerokosc, r.wysokosc),
                        motyw_kolor(MOTYW_KOLOR_RAMKA),
                        promien_cienia);
                }
                powierzchnie_[i]->rysuj(ctx, wymus);
                powierzchnie_[i]->wyczysc_brudny_obszar();
            }
        }
        brudny_po_zamknieciu_ = Prostokat();
    }

private:
    Prostokat obszar_z_cieniem(const PowierzchniaPopup* p) const {
        if (!p) return Prostokat();
        Prostokat r = p->obszar_ekranu();
        if (!motyw_cien_okna()) return r;
        int cien = 2 * skala_dpi_;
        return r.suma(Prostokat(r.x + cien, r.y + cien, r.szerokosc, r.wysokosc));
    }

    bool dodaj_wewnetrznie(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* fokus) {
        if (liczba_ >= MAKS_GLEBOKOSC || obszar.czy_pusty()) return false;
        powierzchnie_[liczba_] = &p;
        p.zarzadca_ = this;
        p.poziom_ = liczba_;
        p.ustaw_skale_dpi(skala_dpi_);
        p.rozmiesc(obszar);
        ++liczba_;
        brudny_po_zamknieciu_ = brudny_po_zamknieciu_.suma(obszar_z_cieniem(&p));
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
            (void)gui_ustaw_popup_aplikacji(false, 0, 0, 0, 0);
            return;
        }
        Prostokat suma;
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                suma = suma.suma(obszar_z_cieniem(powierzchnie_[i]));
            }
        }
        zarejestrowany_ekran_ = Prostokat(wlasciciel_.x + suma.x, wlasciciel_.y + suma.y,
                                          suma.szerokosc, suma.wysokosc);
        int sw = 0, sh = 0;
        gui_pobierz_rozdzielczosc(&sw, &sh);
        if (sw > 0 && sh > 0) {
            zarejestrowany_ekran_ = zarejestrowany_ekran_.przeciecie(Prostokat(0, 0, sw, sh));
        }
        (void)gui_ustaw_popup_aplikacji(true, zarejestrowany_ekran_.x,
                                        zarejestrowany_ekran_.y,
                                        zarejestrowany_ekran_.szerokosc,
                                        zarejestrowany_ekran_.wysokosc);
    }

    PowierzchniaPopup* powierzchnie_[MAKS_GLEBOKOSC]{};
    int liczba_{0};
    SkalaUI skala_dpi_{1};
    Prostokat wlasciciel_{};
    Prostokat obszar_roboczy_{};
    Prostokat zarejestrowany_ekran_{};
    Prostokat brudny_po_zamknieciu_{};
    Kontrolka* fokus_poprzedni_{nullptr};
    Kontrolka* fokus_popupu_{nullptr};
    Kontrolka* najechana_{nullptr};
    Kontrolka* przechwycenie_{nullptr};
};
 
inline PowierzchniaPopup::~PowierzchniaPopup() {
    if (zarzadca_) {
        zarzadca_->zamknij_powierzchnie(this, PowodZamknieciaPopup::WlascicielZamkniety);
    }
}

inline Punkt Kontrolka::punkt_do_ekranu(Punkt p_lokalny) const {
    Punkt w = punkt_do_okna(p_lokalny);
    ZarzadcaPopupow* z = zarzadca_popupow();
    if (z) {
        Prostokat geom = z->geometria_wlasciciela();
        return Punkt(geom.x + w.x, geom.y + w.y);
    }
    return w;
}

inline Prostokat Kontrolka::prostokat_do_ekranu(Prostokat r_lokalny) const {
    Prostokat w = prostokat_do_okna(r_lokalny);
    ZarzadcaPopupow* z = zarzadca_popupow();
    if (z) {
        Prostokat geom = z->geometria_wlasciciela();
        return Prostokat(geom.x + w.x, geom.y + w.y, w.szerokosc, w.wysokosc);
    }
    return w;
}

} // namespace BursztynUI
