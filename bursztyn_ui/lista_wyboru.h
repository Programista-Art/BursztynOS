#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

class ListaWyboru : public Kontrolka {
public:
    using FunkcjaIndeksu = void (*)(ListaWyboru* nadawca, int indeks,
                                    void* dane_uzytkownika);
    using FunkcjaRysowaniaElementu = void (*)(ListaWyboru* lista,
                                              KontekstRysowania& ctx,
                                              int indeks, Prostokat obszar,
                                              bool zaznaczony, bool fokus,
                                              void* dane_uzytkownika);

    ListaWyboru() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        wypelnienie_ = Marginesy(4, 4, 4, 4);
        czy_moze_miec_fokus_ = true;
        pionowy_ = new PasekPrzewijania();
        poziomy_ = new PasekPrzewijania();
        pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        poziomy_->ustaw_orientacje(Orientacja::Pozioma);
        pionowy_->ustaw_maly_krok(1);
        poziomy_->ustaw_maly_krok(16);
        dodaj_kontrolke(pionowy_);
        dodaj_kontrolke(poziomy_);
        pionowy_->przy_przewijaniu(przewinieto, this);
        poziomy_->przy_przewijaniu(przewinieto, this);
    }

    void zaktualizuj_motyw() override {
        bool wlasne_tlo = styl_.normalny.wlasne_tlo;
        bool wlasny_tekst = styl_.normalny.wlasny_tekst;
        bool wlasna_ramka = styl_.normalny.wlasna_ramka;
        StylKontrolki s = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        if (wlasne_tlo) { s.normalny.tlo = styl_.normalny.tlo; s.normalny.wlasne_tlo = true; }
        if (wlasny_tekst) { s.normalny.tekst = styl_.normalny.tekst; s.normalny.wlasny_tekst = true; }
        if (wlasna_ramka) { s.normalny.ramka = styl_.normalny.ramka; s.normalny.wlasna_ramka = true; }
        styl_ = s;
        if (pionowy_) pionowy_->zaktualizuj_motyw();
        if (poziomy_) poziomy_->zaktualizuj_motyw();
        uniewaznij();
        Kontrolka::zaktualizuj_motyw();
    }

    ~ListaWyboru() override {
        zwolnij_elementy();
    }

    bool dodaj_element(const char* tekst, uint64_t znacznik = 0) {
        return wstaw_element(static_cast<int>(liczba_), tekst, znacznik);
    }

    bool wstaw_element(int indeks, const char* tekst, uint64_t znacznik = 0) {
        if (indeks < 0) indeks = 0;
        if (indeks > static_cast<int>(liczba_)) indeks = static_cast<int>(liczba_);
        if (!zapewnij_pojemnosc(liczba_ + 1)) return false;
        char* kopia = kopiuj_tekst(tekst ? tekst : "");
        if (!kopia) return false;
        for (size_t i = liczba_; i > static_cast<size_t>(indeks); --i)
            elementy_[i] = elementy_[i - 1];
        elementy_[indeks].tekst = kopia;
        elementy_[indeks].znacznik = znacznik;
        elementy_[indeks].zaznaczony = false;
        ++liczba_;
        if (wybrany_indeks_ >= indeks) ++wybrany_indeks_;
        if (indeks_fokusu_ >= indeks) ++indeks_fokusu_;
        przelicz_szerokosc();
        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    bool usun_element(int indeks) {
        if (indeks < 0 || indeks >= static_cast<int>(liczba_)) return false;
        delete[] elementy_[indeks].tekst;
        for (size_t i = static_cast<size_t>(indeks); i + 1 < liczba_; ++i)
            elementy_[i] = elementy_[i + 1];
        --liczba_;
        elementy_[liczba_] = Element();
        if (wybrany_indeks_ == indeks) wybrany_indeks_ = -1;
        else if (wybrany_indeks_ > indeks) --wybrany_indeks_;
        if (indeks_fokusu_ >= static_cast<int>(liczba_)) indeks_fokusu_ = liczba_ ? static_cast<int>(liczba_ - 1) : -1;
        przelicz_szerokosc();
        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    void wyczysc() {
        zwolnij_elementy();
        wybrany_indeks_ = -1;
        indeks_fokusu_ = -1;
        maks_szerokosc_ = 0;
        aktualizuj_przewijanie();
        uniewaznij();
    }

    size_t liczba_elementow() const { return liczba_; }
    const char* element(size_t indeks) const {
        return indeks < liczba_ ? elementy_[indeks].tekst : nullptr;
    }
    uint64_t znacznik_elementu(size_t indeks) const {
        return indeks < liczba_ ? elementy_[indeks].znacznik : 0;
    }
    void ustaw_znacznik_elementu(size_t indeks, uint64_t znacznik) {
        if (indeks < liczba_) elementy_[indeks].znacznik = znacznik;
    }

    int wybrany_indeks() const { return wybrany_indeks_; }
    const char* wybrany_element() const {
        return wybrany_indeks_ >= 0 ? element(static_cast<size_t>(wybrany_indeks_)) : nullptr;
    }
    int indeks_fokusu() const { return indeks_fokusu_; }

    bool ustaw_wybrany_indeks(int indeks) {
        if (indeks < -1 || indeks >= static_cast<int>(liczba_)) return false;
        int stary = wybrany_indeks_;
        if (!wielokrotny_) {
            for (size_t i = 0; i < liczba_; ++i) elementy_[i].zaznaczony = false;
        }
        wybrany_indeks_ = indeks;
        indeks_fokusu_ = indeks;
        if (indeks >= 0) elementy_[indeks].zaznaczony = true;
        if (stary == indeks) return true;
        przewin_do_indeksu(indeks);
        uniewaznij_wiersz(stary);
        uniewaznij_wiersz(indeks);
        if (cb_zmiana_) cb_zmiana_(this, indeks, dane_zmiany_);
        return true;
    }

    void ustaw_wielokrotny_wybor(bool wlaczony) { wielokrotny_ = wlaczony; }
    bool czy_wielokrotny_wybor() const { return wielokrotny_; }
    bool czy_element_zaznaczony(size_t indeks) const {
        return indeks < liczba_ && elementy_[indeks].zaznaczony;
    }

    void ustaw_wysokosc_wiersza(int wysokosc) {
        wysokosc_wiersza_ = wysokosc < 1 ? 1 : wysokosc;
        aktualizuj_przewijanie();
        uniewaznij();
    }
    int wysokosc_wiersza() const { return wysokosc_wiersza_; }
    int wysokosc_wiersza_piksele() const { return wysokosc_piks(); }
    int wysokosc_dla_wierszy(int rows) const {
        return rows*wysokosc_piks()+wypelnienie_.pionowo()*skala_dpi_+2*grubosc_ramki_geometrii();
    }
    void ustaw_przewijanie_poziome(bool enabled) { przewijanie_poziome_=enabled; aktualizuj_przewijanie(); uniewaznij(); }

    void przy_zmianie_wyboru(FunkcjaIndeksu cb, void* dane = nullptr) {
        cb_zmiana_ = cb; dane_zmiany_ = dane;
    }
    void przy_aktywacji(FunkcjaIndeksu cb, void* dane = nullptr) {
        cb_aktywacja_ = cb; dane_aktywacji_ = dane;
    }
    void przy_zatwierdzeniu(FunkcjaIndeksu cb, void* dane = nullptr) {
        przy_aktywacji(cb, dane);
    }
    void przy_podwojnym_kliknieciu_elementu(FunkcjaIndeksu cb, void* dane = nullptr) {
        cb_podwojne_ = cb; dane_podwojnego_ = dane;
    }
    void ustaw_rysowanie_elementu(FunkcjaRysowaniaElementu cb, void* dane = nullptr) {
        cb_rysowanie_elementu_ = cb; dane_rysowania_elementu_ = dane;
        uniewaznij();
    }

    /* Przerysowanie jednego wiersza (np. zmienila sie wartosc rysowana przez ustaw_rysowanie_elementu). */
    void odswiez_element(int indeks) { uniewaznij_wiersz(indeks); }

    size_t liczba_ostatnio_narysowanych_wierszy() const { return ostatnio_narysowane_; }
    int pierwszy_widoczny_indeks() const {
        return static_cast<int>(pionowy_->wartosc());
    }
    void przewin_do_elementu(int indeks) { przewin_do_indeksu(indeks); }
    Prostokat prostokat_widoku() const { return geometria().widok; }
    Prostokat prostokat_paska_pionowego() const { return geometria().pasek_pionowy; }
    Prostokat prostokat_paska_poziomego() const { return geometria().pasek_poziomy; }
    Prostokat prostokat_rogu_paskow() const { return geometria().rog; }
    PasekPrzewijania& pasek_pionowy() { return *pionowy_; }
    PasekPrzewijania& pasek_poziomy() { return *poziomy_; }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI s = skala_dpi_;
        return Rozmiar(220 * s, 160 * s);
    }

    void rozmiesc(Prostokat docelowy) override {
        Kontrolka::rozmiesc(docelowy);
        aktualizuj_przewijanie();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);
        Geometria g = geometria();
        ostatnio_narysowane_ = 0;
        if (g.widok.czy_pusty() || liczba_ == 0) return;
        ctx.dodaj_przyciecie(g.widok);
        SkalaUI skala = skala_dpi_;
        int wys = wysokosc_piks();
        int pierwszy = static_cast<int>(pionowy_->wartosc());
        int widoczne = g.widok.wysokosc / wys + 1;
        int koniec = pierwszy + widoczne;
        if (koniec > static_cast<int>(liczba_)) koniec = static_cast<int>(liczba_);
        int przes_x = static_cast<int>(poziomy_->wartosc());
        for (int i = pierwszy; i < koniec; ++i) {
            Prostokat r(g.widok.x, g.widok.y + (i - pierwszy) * wys,
                        g.widok.szerokosc, wys);
            bool zaz = elementy_[i].zaznaczony;
            bool fokus = i == indeks_fokusu_ && czy_ma_fokus_;
            if (cb_rysowanie_elementu_) {
                cb_rysowanie_elementu_(this, ctx, i, r, zaz, fokus,
                                        dane_rysowania_elementu_);
            } else {
                if (zaz) ctx.rysuj_prostokat(r, motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
                else if (fokus) ctx.rysuj_prostokat(r, motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY));
                ctx.rysuj_tekst(Punkt(r.x + 4 * skala - przes_x,
                                     r.y + (wys - CzcionkaUI::pobierz_wysokosc(skala)) / 2),
                                elementy_[i].tekst,
                                zaz ? motyw_kolor_tekstu_zaznaczenia()
                                    : (czy_wlaczona_ ? s.tekst : motyw_kolor(MOTYW_KOLOR_WYLACZONY)),
                                skala);
                if (fokus) ctx.rysuj_ramke(r, motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY), skala);
            }
            ++ostatnio_narysowane_;
        }
        ctx.zdejmij_przyciecie();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int indeks = indeks_z_punktu(e.x - obszar_ekranu_.x,
                                     e.y - obszar_ekranu_.y);
        if (indeks >= 0) {
            if (wielokrotny_ && e.czy_ctrl()) {
                elementy_[indeks].zaznaczony = !elementy_[indeks].zaznaczony;
                indeks_fokusu_ = indeks;
                wybrany_indeks_ = indeks;
                uniewaznij_wiersz(indeks);
                if (cb_zmiana_) cb_zmiana_(this, indeks, dane_zmiany_);
            } else ustaw_wybrany_indeks(indeks);
            e.obsluzone = true;
        }
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int indeks = indeks_z_punktu(e.x - obszar_ekranu_.x,
                                     e.y - obszar_ekranu_.y);
        if (indeks >= 0) {
            ustaw_wybrany_indeks(indeks);
            if (cb_podwojne_) cb_podwojne_(this, indeks, dane_podwojnego_);
            if (cb_aktywacja_) cb_aktywacja_(this, indeks, dane_aktywacji_);
            e.obsluzone = true;
        }
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0) return;
        pionowy_->przewin_o(e.delta_y > 0 ? -3 : 3);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        int nowy = indeks_fokusu_;
        if (nowy < 0 && liczba_ > 0) nowy = 0;
        uint32_t kod = e.polecenie();
        int strona = liczba_widocznych_wierszy();
        bool obsl = true;
        if (kod == 0x48U) --nowy;
        else if (kod == 0x50U) ++nowy;
        else if (kod == 0x47U) nowy = 0;
        else if (kod == 0x4FU) nowy = static_cast<int>(liczba_) - 1;
        else if (kod == 0x49U) nowy -= strona;
        else if (kod == 0x51U) nowy += strona;
        else if (kod == 13) {
            if (indeks_fokusu_ >= 0 && cb_aktywacja_)
                cb_aktywacja_(this, indeks_fokusu_, dane_aktywacji_);
        } else obsl = false;
        if (obsl) {
            if (liczba_ > 0 && !(kod == 13)) {
                if (nowy < 0) nowy = 0;
                if (nowy >= static_cast<int>(liczba_)) nowy = static_cast<int>(liczba_) - 1;
                ustaw_wybrany_indeks(nowy);
            }
            e.obsluzone = true;
            return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    struct Element {
        char* tekst{nullptr};
        uint64_t znacznik{0};
        bool zaznaczony{false};
    };
    struct Geometria {
        Prostokat widok;
        Prostokat pasek_pionowy;
        Prostokat pasek_poziomy;
        Prostokat rog;
    };

    static void przewinieto(PasekPrzewijania*, int64_t, void* dane) {
        ListaWyboru* lista = static_cast<ListaWyboru*>(dane);
        if (lista) lista->uniewaznij_obszar(lista->geometria().widok);
    }

    int wysokosc_piks() const {
        SkalaUI s = skala_dpi_;
        int min = CzcionkaUI::pobierz_wysokosc(s) + 6 * s;
        int zad = wysokosc_wiersza_ * s;
        return zad > min ? zad : min;
    }

    int grubosc_paska() const { return 18 * (skala_dpi_); }

    Prostokat obszar_po_ramce_i_wypelnieniu() const {
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        r = r.zmniejszony(Marginesy(grubosc_ramki_geometrii()));
        SkalaUI s = skala_dpi_;
        return r.zmniejszony(Marginesy(wypelnienie_.lewy * s,
                                      wypelnienie_.gorny * s,
                                      wypelnienie_.prawy * s,
                                      wypelnienie_.dolny * s));
    }

    Geometria geometria() const {
        Prostokat baza = obszar_po_ramce_i_wypelnieniu();
        int gp = grubosc_paska();
        bool pv = pionowy_->czy_widoczna();
        bool ph = poziomy_->czy_widoczna();
        int vw = baza.szerokosc - (pv ? gp : 0); if (vw < 0) vw = 0;
        int vh = baza.wysokosc - (ph ? gp : 0); if (vh < 0) vh = 0;
        Geometria g;
        g.widok = Prostokat(baza.x, baza.y, vw, vh);
        g.pasek_pionowy = Prostokat(baza.x + vw, baza.y, pv ? gp : 0, vh);
        g.pasek_poziomy = Prostokat(baza.x, baza.y + vh, vw, ph ? gp : 0);
        g.rog = (pv && ph)
              ? Prostokat(baza.x + vw, baza.y + vh, gp, gp)
              : Prostokat();
        return g;
    }

    int liczba_widocznych_wierszy() const {
        int wys = wysokosc_piks();
        int n = wys > 0 ? geometria().widok.wysokosc / wys : 0;
        return n > 0 ? n : 1;
    }

    void aktualizuj_przewijanie() {
        SkalaUI s = skala_dpi_;
        Prostokat stary = geometria().widok;
        Prostokat baza = obszar_po_ramce_i_wypelnieniu();
        int gp = grubosc_paska();
        bool pv = false, ph = false;
        for (int iteracja = 0; iteracja < 3; ++iteracja) {
            int vw = baza.szerokosc - (pv ? gp : 0); if (vw < 0) vw = 0;
            int vh = baza.wysokosc - (ph ? gp : 0); if (vh < 0) vh = 0;
            bool potrzebny_p = static_cast<int64_t>(liczba_) * wysokosc_piks() > vh;
            bool potrzebny_h = przewijanie_poziome_ && maks_szerokosc_ + 8 * s > vw;
            bool nowy_p = pv || potrzebny_p;
            bool nowy_h = ph || potrzebny_h;
            if (nowy_p == pv && nowy_h == ph) break;
            pv = nowy_p; ph = nowy_h;
        }
        pionowy_->ustaw_widoczna(pv);
        poziomy_->ustaw_widoczna(ph);
        int wid = liczba_widocznych_wierszy();
        int max_pierwszy = static_cast<int>(liczba_) - wid;
        if (max_pierwszy < 0) max_pierwszy = 0;
        pionowy_->ustaw_zakres(0, max_pierwszy);
        pionowy_->ustaw_rozmiar_strony(wid);
        pionowy_->ustaw_duzy_krok(wid);
        int wid_w = geometria().widok.szerokosc;
        int max_x = maks_szerokosc_ + 8 * s - wid_w;
        if (max_x < 0) max_x = 0;
        poziomy_->ustaw_zakres(0, max_x);
        poziomy_->ustaw_rozmiar_strony(wid_w);
        poziomy_->ustaw_duzy_krok(wid_w > 16 ? wid_w - 16 : 16);
        Geometria g = geometria();
        pionowy_->rozmiesc(g.pasek_pionowy);
        poziomy_->rozmiesc(g.pasek_poziomy);
        if (stary != g.widok) uniewaznij_obszar(stary.suma(g.widok).suma(g.rog));
    }

    int indeks_z_punktu(int x, int y) const {
        Geometria g = geometria();
        if (!g.widok.zawiera(x, y)) return -1;
        int i = static_cast<int>(pionowy_->wartosc()) + (y - g.widok.y) / wysokosc_piks();
        return i >= 0 && i < static_cast<int>(liczba_) ? i : -1;
    }

    void przewin_do_indeksu(int indeks) {
        if (indeks < 0) return;
        int pierwszy = static_cast<int>(pionowy_->wartosc());
        int wid = liczba_widocznych_wierszy();
        if (indeks < pierwszy) pionowy_->ustaw_wartosc(indeks);
        else if (indeks >= pierwszy + wid) pionowy_->ustaw_wartosc(indeks - wid + 1);
    }

    void uniewaznij_wiersz(int indeks) {
        if (indeks < 0) return;
        int pierwszy = static_cast<int>(pionowy_->wartosc());
        int rel = indeks - pierwszy;
        if (rel < 0 || rel > liczba_widocznych_wierszy()) return;
        Geometria g = geometria();
        uniewaznij_obszar(Prostokat(g.widok.x, g.widok.y + rel * wysokosc_piks(),
                                    g.widok.szerokosc, wysokosc_piks()));
    }

    static char* kopiuj_tekst(const char* tekst) {
        size_t n = 0; while (tekst[n]) ++n;
        char* wynik = new char[n + 1];
        if (!wynik) return nullptr;
        for (size_t i = 0; i <= n; ++i) wynik[i] = tekst[i];
        return wynik;
    }

    bool zapewnij_pojemnosc(size_t potrzebna) {
        if (potrzebna <= pojemnosc_) return true;
        size_t nowa = pojemnosc_ ? pojemnosc_ * 2 : 16;
        while (nowa < potrzebna) nowa *= 2;
        Element* e = new Element[nowa];
        if (!e) return false;
        for (size_t i = 0; i < liczba_; ++i) e[i] = elementy_[i];
        delete[] elementy_;
        elementy_ = e;
        pojemnosc_ = nowa;
        return true;
    }

    void zwolnij_elementy() {
        for (size_t i = 0; i < liczba_; ++i) delete[] elementy_[i].tekst;
        delete[] elementy_;
        elementy_ = nullptr;
        liczba_ = pojemnosc_ = 0;
    }

    void przelicz_szerokosc() {
        maks_szerokosc_ = 0;
        for (size_t i = 0; i < liczba_; ++i) {
            int w = CzcionkaUI::zmierz_tekst(elementy_[i].tekst, skala_dpi_);
            if (w > maks_szerokosc_) maks_szerokosc_ = w;
        }
    }

    Element* elementy_{nullptr};
    size_t liczba_{0};
    size_t pojemnosc_{0};
    int wybrany_indeks_{-1};
    int indeks_fokusu_{-1};
    int wysokosc_wiersza_{22};
    int maks_szerokosc_{0};
    bool wielokrotny_{false};
    bool przewijanie_poziome_{true};
    PasekPrzewijania* pionowy_{nullptr};
    PasekPrzewijania* poziomy_{nullptr};
    mutable size_t ostatnio_narysowane_{0};
    FunkcjaIndeksu cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
    FunkcjaIndeksu cb_aktywacja_{nullptr};
    void* dane_aktywacji_{nullptr};
    FunkcjaIndeksu cb_podwojne_{nullptr};
    void* dane_podwojnego_{nullptr};
    FunkcjaRysowaniaElementu cb_rysowanie_elementu_{nullptr};
    void* dane_rysowania_elementu_{nullptr};
};

} // namespace BursztynUI
