#pragma once

#include "kontrolka.h"
#include "rysowanie.h"
#include "bursztyn_gui.h"

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
    virtual bool czy_podaza_za_wlascicielem() const { return false; }
    virtual void po_zamknieciu(PowodZamknieciaPopup powod) {
        if (cb_zamkniecia_) {
            cb_zamkniecia_(this, powod, dane_zamkniecia_);
        }
    }

    virtual uint16_t promien_zaokraglenia() const {
        return motyw_promien(MOTYW_ROG_MENU);
    }

    bool zawiera_ksztalt(Punkt pt) const {
        auto r=obszar();if(!r.zawiera(pt))return false;
        int radius=int(promien_zaokraglenia())*skala_dpi_;
        int maxRadius=(r.szerokosc<r.wysokosc?r.szerokosc:r.wysokosc)/2;
        if(radius>maxRadius)radius=maxRadius;
        int y=pt.y-r.y;
        int edge=y<r.wysokosc-1-y?y:r.wysokosc-1-y;
        int inset=radius>0 && edge<radius?motyw_promien_inset(radius,edge):0;
        return pt.x>=r.x+inset && pt.x<r.prawy()-inset;
    }
    // One radius controls the body, border and manager's translated shadow.
    void rysuj_zaokraglone_tlo(KontekstRysowania& ctx,
        uint32_t background=motyw_kolor(MOTYW_KOLOR_PANEL),
        uint32_t border=motyw_kolor(MOTYW_KOLOR_RAMKA),int thickness=1) {
        Prostokat r(0,0,szerokosc(),wysokosc());
        int radius=int(promien_zaokraglenia())*skala_dpi_;
        ctx.rysuj_zaokraglony_prostokat(r,background|0xff000000U,radius);
        rysuj_ramke_popupu(ctx,border,thickness);
    }
    void rysuj_ramke_popupu(KontekstRysowania& ctx,
        uint32_t border=motyw_kolor(MOTYW_KOLOR_RAMKA),int thickness=1) {
        ctx.rysuj_zaokraglona_ramke({0,0,szerokosc(),wysokosc()},border|0xff000000U,
            int(promien_zaokraglenia())*skala_dpi_,thickness*skala_dpi_);
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
        if (bufor_popupu_) {
            delete[] bufor_popupu_;
            bufor_popupu_ = nullptr;
            pojemnosc_popupu_ = 0;
        }
    }

    void ustaw_geometrie_wlasciciela(Prostokat ekran, Prostokat praca) {
        const int dx = ekran.x - wlasciciel_.x;
        const int dy = ekran.y - wlasciciel_.y;
        wlasciciel_ = ekran;
        obszar_roboczy_ = praca;
        if ((dx || dy) && liczba_) {
            const int cien = motyw_cien_okna() ? 2 * skala_dpi_ : 0;
            for (int i = 0; i < liczba_; ++i) {
                if (!powierzchnie_[i]->czy_podaza_za_wlascicielem()) continue;
                Prostokat r = powierzchnie_[i]->obszar();
                int max_x = praca.prawy() - r.szerokosc - cien;
                int max_y = praca.dolny() - r.wysokosc - cien;
                if (max_x < praca.x) max_x = praca.x;
                if (max_y < praca.y) max_y = praca.y;
                r.x = przytnij(r.x + dx, praca.x, max_x);
                r.y = przytnij(r.y + dy, praca.y, max_y);
                powierzchnie_[i]->rozmiesc(r);
            }
            aktualizuj_rejestracje(); // BWS damages both old and new rectangles.
            odrysuj_popupy(true);
        }
    }

    void ustaw_skale_dpi(SkalaUI skala) {
        skala_dpi_ = skala;
    }

    void zaktualizuj_motyw() {
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                powierzchnie_[i]->zaktualizuj_motyw();
            }
        }
        if (czy_otwarty()) {
            odrysuj_popupy(true);
        }
    }

    bool czy_otwarty() const { return liczba_ > 0; }
    bool czy_ma_modalna() const {
        for(int i=0;i<liczba_;++i)if(powierzchnie_[i] && powierzchnie_[i]->czy_modalna())return true;
        return false;
    }
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
        return Prostokat(x, y, w, h);
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
        return Prostokat(x, y, w, h);
    }

    Prostokat umiesc_podmenu(Prostokat element_klienta, Rozmiar rozmiar) const {
        return umiesc_podmenu_ekranu(element_klienta, rozmiar);
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
        return Prostokat(x, y, w, h);
    }

    // Popup globalny (np. toast): wynik jest w przestrzeni monitora, z marginesami i uwzglednieniem cienia.
    Prostokat umiesc_w_obszarze_roboczym_prawy_dol(Rozmiar rozmiar,
                                                   int margines_prawy,
                                                   int margines_dolny) const {
        Prostokat d = obszar_roboczy_;
        int cien = motyw_cien_okna() ? 2 * skala_dpi_ : 0;
        int w = rozmiar.szerokosc > d.szerokosc ? d.szerokosc : rozmiar.szerokosc;
        int h = rozmiar.wysokosc > d.wysokosc ? d.wysokosc : rozmiar.wysokosc;
        int x = d.prawy() - margines_prawy - w - cien;
        int y = d.dolny() - margines_dolny - h - cien;
        if (x < d.x) x = d.x;
        if (y < d.y) y = d.y;
        return Prostokat(x, y, w, h);
    }

    bool przemiesc_powierzchnie(PowierzchniaPopup& p, Prostokat obszar,
                                bool odswiez_rejestracje = true) {
        if (powierzchnia_dla(&p) < 0 || obszar.czy_pusty()) return false;
        if (p.obszar().pozycja() == obszar.pozycja() && p.obszar().rozmiar() == obszar.rozmiar()) return true;
        p.rozmiesc(obszar);
        if (odswiez_rejestracje) aktualizuj_rejestracje();
        return true;
    }

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
        ost_klik_kontrolka_ = nullptr;
        ost_klik_czas_ = 0;
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
        if (przechwycenie_ && przechwycenie_ != najechana_) {
            przechwycenie_->obsluz_opuszczenie_myszy();
        }
        if (najechana_) {
            najechana_->obsluz_opuszczenie_myszy();
        }
        najechana_ = nullptr;
        przechwycenie_ = nullptr;
        ost_klik_kontrolka_ = nullptr;
        ost_klik_czas_ = 0;
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
        // Podwojne klikniecie w popupie (np. wiersz tabeli w DialogPliku) -
        // ta sama semantyka co w Okno: drugie DOWN jest zwyklym DOWN, a potem
        // dodatkowo PodwojneKlikniecie. Tylko dla tej samej kontrolki i tylko
        // lewym przyciskiem; prawy przycisk zrywa sekwencje.
        const bool lewy = e.czy_lewy_przycisk();
        const bool podwojne = lewy && hit == ost_klik_kontrolka_ &&
                              czy_podwojne_klikniecie(e.timestamp, ost_klik_czas_, e.x, e.y,
                                                      ost_klik_x_, ost_klik_y_);
        hit->obsluz_wcisniecie_myszy(e);
        if (podwojne) {
            ost_klik_czas_ = 0;
            ost_klik_kontrolka_ = nullptr;
            // DOWN mogl zamknac powierzchnie kontrolki (np. pozycja menu):
            // zamkniecie zeruje przechwycenie, wtedy nie ma juz komu dostarczyc.
            if (przechwycenie_ == hit) {
                ZdarzenieMyszki dwa = e;
                dwa.typ = TypZdarzenia::PodwojneKlikniecie;
                dwa.liczba_klikniec = 2;
                dwa.obsluzone = false;
                hit->obsluz_podwojne_klikniecie(dwa);
            }
        } else {
            ost_klik_czas_ = lewy ? e.timestamp : 0;
            ost_klik_kontrolka_ = lewy ? hit : nullptr;
            ost_klik_x_ = e.x;
            ost_klik_y_ = e.y;
        }
        e.obsluzone = true;
        return true;
    }

    // Kryterium jak w Okno::obsluz_zdarzenie: < 400 ms (znaczniki w ns albo,
    // na hostach testowych, w ms) i przesuniecie < 5 px.
    static bool czy_podwojne_klikniecie(uint64_t teraz, uint64_t poprzedni, int x, int y,
                                        int px, int py) {
        if (teraz == 0 || poprzedni == 0 || teraz < poprzedni) return false;
        const uint64_t d = teraz - poprzedni;
        const int dx = x > px ? x - px : px - x;
        const int dy = y > py ? y - py : py - y;
        return (d < 400000000ULL || (d < 400 && teraz < 1000000000ULL)) && dx < 5 && dy < 5;
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
        PowierzchniaPopup* surf = powierzchnie_[liczba_ - 1];
        bool dotarto_do_powierzchni = false;
        for (Kontrolka* k = cel; k; k = k->rodzic()) {
            k->obsluz_wcisniecie_klawisza(e);
            if (e.obsluzone) return true;
            if (k == surf) {
                dotarto_do_powierzchni = true;
                break;
            }
        }
        if (surf && !dotarto_do_powierzchni) {
            surf->obsluz_wcisniecie_klawisza(e);
            if (e.obsluzone) return true;
        }
        uint32_t kod = e.polecenie();
        if (kod == 27U) {
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
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i] && powierzchnie_[i]->czy_brudna()) return true;
        }
        return false;
    }

    Prostokat brudny_obszar() const {
        Prostokat r;
        for (int i = 0; i < liczba_; ++i) {
            if (powierzchnie_[i]) {
                r = r.suma(powierzchnie_[i]->pobierz_brudny_obszar());
            }
        }
        return r;
    }

    bool odrysuj_popupy(bool wymus = false) {
        if (liczba_ == 0 || zarejestrowany_ekran_.czy_pusty() || !bufor_popupu_) return false;
        if(!wymus) {
            bool dirty=false;
            for(int i=0;i<liczba_;++i)dirty |= powierzchnie_[i] && powierzchnie_[i]->czy_brudna();
            if(!dirty)return false;
        }
        const int reg_w = zarejestrowany_ekran_.szerokosc;
        const int reg_h = zarejestrowany_ekran_.wysokosc;
        const size_t piksele = static_cast<size_t>(reg_w) * reg_h;
        if (piksele > pojemnosc_popupu_) return false;

        for (size_t i = 0; i < piksele; ++i) {
            bufor_popupu_[i] = 0x00000000U;
        }

        KontekstRysowania ctx;
        ctx.ustaw_cel(bufor_popupu_, zarejestrowany_ekran_);

        for (int i = 0; i < liczba_; ++i) {
            if (!powierzchnie_[i]) continue;
            PowierzchniaPopup* p = powierzchnie_[i];
            Prostokat r = p->obszar();

            if (motyw_cien_okna()) {
                int cien = 2 * skala_dpi_;
                int promien_cienia = int(p->promien_zaokraglenia()) * skala_dpi_;
                ctx.rysuj_zaokraglony_prostokat(
                    Prostokat(r.x + cien, r.y + cien, r.szerokosc, r.wysokosc),
                    motyw_kolor(MOTYW_KOLOR_RAMKA)|0xff000000U,
                    promien_cienia);
            }

            p->rysuj(ctx, wymus);
            p->wyczysc_brudny_obszar();
        }

        ctx.ustaw_cel(nullptr, Prostokat());

        gui_blit_popup(zarejestrowany_ekran_.x, zarejestrowany_ekran_.y,
                       zarejestrowany_ekran_.szerokosc, zarejestrowany_ekran_.wysokosc,
                       bufor_popupu_);
        return true;
    }

    void rysuj(KontekstRysowania& ctx, bool wymus = false) {
        (void)ctx;
        odrysuj_popupy(wymus);
    }

private:
    Prostokat prostokat_wizualny_popupu(const PowierzchniaPopup* p) const {
        if (!p) return Prostokat();
        Prostokat r = p->obszar();
        int cien = motyw_cien_okna() ? 2 * skala_dpi_ : 0;
        return Prostokat(r.x, r.y, r.szerokosc + cien, r.wysokosc + cien);
    }

    bool dodaj_wewnetrznie(PowierzchniaPopup& p, Prostokat obszar, Kontrolka* fokus) {
        if (liczba_ >= MAKS_GLEBOKOSC || obszar.czy_pusty()) return false;
        powierzchnie_[liczba_] = &p;
        p.zarzadca_ = this;
        p.poziom_ = liczba_;
        // Retained surfaces may have missed theme broadcasts while detached.
        // Refresh the whole control tree before its first layout and paint.
        p.zaktualizuj_motyw();
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

    Kontrolka* trafienie(Punkt pt) const {
        for (int i = liczba_ - 1; i >= 0; --i) {
            if (powierzchnie_[i] && powierzchnie_[i]->zawiera_ksztalt(pt)) {
                Kontrolka* hit = powierzchnie_[i]->sprawdz_trafienie(pt);
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
                suma = suma.suma(prostokat_wizualny_popupu(powierzchnie_[i]));
            }
        }
        if (suma.czy_pusty()) {
            zarejestrowany_ekran_ = Prostokat();
            (void)gui_ustaw_popup_aplikacji(false, 0, 0, 0, 0);
            return;
        }

        const bool rozmiar_zmieniony = (suma.szerokosc != zarejestrowany_ekran_.szerokosc ||
                                       suma.wysokosc != zarejestrowany_ekran_.wysokosc);
        const bool pozycja_zmieniona = (suma.x != zarejestrowany_ekran_.x ||
                                       suma.y != zarejestrowany_ekran_.y);

        if (!rozmiar_zmieniony && !pozycja_zmieniona) {
            return;
        }

        zarejestrowany_ekran_ = suma;

        size_t potrzebne = static_cast<size_t>(suma.szerokosc) * suma.wysokosc;
        if (potrzebne > pojemnosc_popupu_) {
            delete[] bufor_popupu_;
            size_t nowa_poj = (potrzebne + 65535) & ~65535;
            bufor_popupu_ = new uint32_t[nowa_poj];
            pojemnosc_popupu_ = nowa_poj;
        }

        (void)gui_ustaw_popup_aplikacji(true, zarejestrowany_ekran_.x,
                                        zarejestrowany_ekran_.y,
                                        zarejestrowany_ekran_.szerokosc,
                                        zarejestrowany_ekran_.wysokosc);

        if (rozmiar_zmieniony) {
            odrysuj_popupy(true);
        }
    }

    PowierzchniaPopup* powierzchnie_[MAKS_GLEBOKOSC]{};
    int liczba_{0};
    SkalaUI skala_dpi_{1};
    Prostokat wlasciciel_{};
    Prostokat obszar_roboczy_{};
    Prostokat zarejestrowany_ekran_{};
    uint32_t* bufor_popupu_{nullptr};
    size_t pojemnosc_popupu_{0};
    Kontrolka* fokus_poprzedni_{nullptr};
    Kontrolka* fokus_popupu_{nullptr};
    Kontrolka* najechana_{nullptr};
    Kontrolka* przechwycenie_{nullptr};
    // Sekwencja podwojnego klikniecia; wskaznik tylko porownywany, nigdy dereferencjonowany.
    Kontrolka* ost_klik_kontrolka_{nullptr};
    uint64_t ost_klik_czas_{0};
    int ost_klik_x_{0};
    int ost_klik_y_{0};
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
        if (z->powierzchnia_dla(const_cast<Kontrolka*>(this)) >= 0) {
            return w;
        }
        Prostokat geom = z->geometria_wlasciciela();
        return Punkt(geom.x + w.x, geom.y + w.y);
    }
    return w;
}

inline Prostokat Kontrolka::prostokat_do_ekranu(Prostokat r_lokalny) const {
    Prostokat w = prostokat_do_okna(r_lokalny);
    ZarzadcaPopupow* z = zarzadca_popupow();
    if (z) {
        if (z->powierzchnia_dla(const_cast<Kontrolka*>(this)) >= 0) {
            return w;
        }
        Prostokat geom = z->geometria_wlasciciela();
        return Prostokat(geom.x + w.x, geom.y + w.y, w.szerokosc, w.wysokosc);
    }
    return w;
}

} // namespace BursztynUI
