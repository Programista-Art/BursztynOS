#pragma once

#include "popup.h"
#include "lista_wyboru.h"
#include "pole_tekstowe.h"

namespace BursztynUI {

class ListaRozwijana : public Kontrolka {
public:
    using FunkcjaWyboru = void (*)(ListaRozwijana*, int indeks, void* dane_uzytkownika);
    using FunkcjaZatwierdzenia = void (*)(ListaRozwijana*, int indeks,
                                          const char* tekst, void* dane_uzytkownika);

private:
    class PoleEdycji : public PoleTekstowe {
    public:
        explicit PoleEdycji(ListaRozwijana* wlasciciel)
            : PoleTekstowe("Wpisz lub wybierz..."), wlasciciel_(wlasciciel) {}
        void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
            if (wlasciciel_ && wlasciciel_->obsluz_klawisz_specjalny(e)) return;
            PoleTekstowe::obsluz_wcisniecie_klawisza(e);
        }
    private:
        ListaRozwijana* wlasciciel_{nullptr};
    };

    class PopupListy : public PowierzchniaPopup {
    public:
        explicit PopupListy(ListaRozwijana* wlasciciel) : wlasciciel_(wlasciciel) {
            lista_ = new ListaWyboru();
            lista_->ustaw_przewijanie_poziome(false);
            dodaj_kontrolke(lista_);
            lista_->przy_zmianie_wyboru(zmieniono, this);
            lista_->przy_aktywacji(aktywacja, this);
            przy_zamknieciu(zamknieto, this);
        }
        ListaWyboru& lista() { return *lista_; }
        bool czy_podaza_za_wlascicielem() const override { return true; }
        const ListaWyboru& lista() const { return *lista_; }
        void rozmiesc(Prostokat r) override {
            PowierzchniaPopup::rozmiesc(r);
            SkalaUI s = skala_dpi_;
            lista_->rozmiesc(Prostokat(s, s, r.szerokosc - 2 * s,
                                      r.wysokosc - 2 * s));
        }
        void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
            if (!wlasciciel_) return;
            if (wlasciciel_->czy_enter(e)) {
                wlasciciel_->zatwierdz_popup(); e.obsluzone = true; return;
            }
            wlasciciel_->nawigacja_popupu_ = true;
            lista_->obsluz_wcisniecie_klawisza(e);
            wlasciciel_->nawigacja_popupu_ = false;
        }
    private:
        static void zmieniono(ListaWyboru*, int indeks, void* dane) {
            PopupListy* p = static_cast<PopupListy*>(dane);
            if (p && p->wlasciciel_) p->wlasciciel_->wybrano_z_popupu(indeks);
        }
        static void aktywacja(ListaWyboru*, int, void* dane) {
            PopupListy* p = static_cast<PopupListy*>(dane);
            if (p && p->wlasciciel_) p->wlasciciel_->zatwierdz_popup();
        }
        static void zamknieto(PowierzchniaPopup*, PowodZamknieciaPopup powod, void* dane) {
            PopupListy* p = static_cast<PopupListy*>(dane);
            if (p && p->wlasciciel_) p->wlasciciel_->popup_zamkniety(powod);
        }
        ListaRozwijana* wlasciciel_{nullptr};
        ListaWyboru* lista_{nullptr};
    };

public:
    ListaRozwijana() : popup_(this) {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        czy_moze_miec_fokus_ = true;
        edycja_ = new PoleEdycji(this);
        edycja_->ustaw_widoczna(false);
        dodaj_kontrolke(edycja_);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        popup_.lista().zaktualizuj_motyw();
        if (edycja_) edycja_->zaktualizuj_motyw();
        uniewaznij();
    }

    bool dodaj_element(const char* tekst, uint64_t znacznik = 0) {
        return popup_.lista().dodaj_element(tekst, znacznik);
    }
    bool wstaw_element(int indeks, const char* tekst, uint64_t znacznik = 0) {
        return popup_.lista().wstaw_element(indeks, tekst, znacznik);
    }
    bool usun_element(int indeks) {
        bool wynik = popup_.lista().usun_element(indeks);
        if (wynik) synchronizuj_tekst();
        return wynik;
    }
    void wyczysc() { popup_.lista().wyczysc(); synchronizuj_tekst(); }
    size_t liczba_elementow() const { return popup_.lista().liczba_elementow(); }
    const char* element(size_t indeks) const { return popup_.lista().element(indeks); }
    uint64_t znacznik_elementu(size_t indeks) const {
        return popup_.lista().znacznik_elementu(indeks);
    }

    bool ustaw_wybrany_indeks(int indeks) {
        if (!popup_.lista().ustaw_wybrany_indeks(indeks)) return false;
        synchronizuj_tekst();
        uniewaznij();
        return true;
    }
    int wybrany_indeks() const { return popup_.lista().wybrany_indeks(); }
    const char* wybrany_tekst() const {
        if (edytowalna_) return edycja_->tekst();
        const char* t = popup_.lista().wybrany_element();
        return t ? t : "";
    }

    void ustaw_edytowalna(bool edytowalna) {
        if (edytowalna_ == edytowalna) return;
        edytowalna_ = edytowalna;
        edycja_->ustaw_widoczna(edytowalna_);
        synchronizuj_tekst(); rozmiesc(obszar_); uniewaznij();
    }
    bool czy_edytowalna() const { return edytowalna_; }
    PoleTekstowe& pole_edycji() { return *edycja_; }
    ListaWyboru& lista_popupu() { return popup_.lista(); }
    const ListaWyboru& lista_popupu() const { return popup_.lista(); }
    Prostokat prostokat_popupu() const { return popup_.obszar_ekranu(); }

    void ustaw_maks_widocznych_elementow(int maks) {
        maks_widocznych_ = maks < 1 ? 1 : maks;
    }
    int maks_widocznych_elementow() const { return maks_widocznych_; }

    bool rozwin() {
        ZarzadcaPopupow* z = zarzadca_popupow();
        if (!z || czy_rozwinieta()) return z != nullptr;
        int liczba = static_cast<int>(liczba_elementow());
        int wiersze = liczba < 1 ? 1 : (liczba < maks_widocznych_ ? liczba : maks_widocznych_);
        SkalaUI s = skala_dpi_;
        popup_.ustaw_skale_dpi(s);
        int h = popup_.lista().wysokosc_dla_wierszy(wiersze) + 2 * s;
        int w = obszar_.szerokosc;
        Prostokat kotwica = prostokat_do_ekranu(
            Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc));
        Prostokat gdzie = z->umiesc_pod_kotwica_ekranu(kotwica, Rozmiar(w, h), true);
        wybor_przed_otwarciem_ = wybrany_indeks();
        anulowanie_ = false;
        podglad_zgloszony_ = false;
        if (!z->otworz(popup_, gdzie, edytowalna_ ? static_cast<Kontrolka*>(edycja_)
                                                   : static_cast<Kontrolka*>(this),
                       &popup_)) return false;
        popup_.lista().przewin_do_elementu(wybrany_indeks());
        uniewaznij();
        return true;
    }
    void zwin() {
        if (popup_.zarzadca()) popup_.zarzadca()->zamknij(PowodZamknieciaPopup::Anulowanie);
    }
    bool czy_rozwinieta() const { return popup_.czy_otwarta(); }

    void przy_zmianie_wyboru(FunkcjaWyboru cb, void* dane = nullptr) {
        cb_zmiana_ = cb; dane_zmiany_ = dane;
    }
    void przy_zatwierdzeniu(FunkcjaZatwierdzenia cb, void* dane = nullptr) {
        cb_zatwierdzenie_ = cb; dane_zatwierdzenia_ = dane;
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI s = skala_dpi_;
        int height=CzcionkaUI::pobierz_wysokosc(s)+12*s;
        if(height<30*s)height=30*s;
        return Rozmiar(220 * s, height);
    }
    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        SkalaUI s = skala_dpi_;
        int przycisk = 28 * s;
        if (edytowalna_)
            edycja_->rozmiesc(Prostokat(1 * s, 1 * s,
                r.szerokosc - przycisk - 2 * s, r.wysokosc - 2 * s));
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu st = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, st);
        SkalaUI s = skala_dpi_;
        int bw = 28 * s;
        Prostokat przycisk(obszar_.szerokosc - bw, s, bw - s, obszar_.wysokosc - 2 * s);
        ctx.rysuj_prostokat(przycisk, czy_rozwinieta() ? motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY)
                                                       : motyw_kolor(MOTYW_KOLOR_PANEL));
        ctx.rysuj_ramke(przycisk, motyw_kolor(MOTYW_KOLOR_RAMKA), s);
        int cx = przycisk.x + przycisk.szerokosc / 2;
        int cy = przycisk.y + przycisk.wysokosc / 2;
        uint32_t arrow_col = czy_rozwinieta() ? motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU)
                                              : (czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_TEKST) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY));
        for (int i = 0; i < 4 * s; ++i)
            ctx.rysuj_linie_pozioma(cx - i, cy - 2 * s + i, 2 * i + 1, arrow_col);
        if (!edytowalna_) {
            const char* t = wybrany_tekst();
            ctx.dodaj_przyciecie(Prostokat(6 * s, 2 * s,
                                           obszar_.szerokosc - bw - 10 * s,
                                           obszar_.wysokosc - 4 * s));
            ctx.rysuj_tekst(Punkt(6 * s,
                (obszar_.wysokosc - CzcionkaUI::pobierz_wysokosc(s)) / 2),
                t[0] ? t : "Wybierz...", t[0] ? st.tekst : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY), s);
            ctx.zdejmij_przyciecie();
        }
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int lokalny_x = e.x - obszar_ekranu_.x;
        int bw = 28 * (skala_dpi_);
        if (!edytowalna_ || lokalny_x >= obszar_.szerokosc - bw) {
            czy_rozwinieta() ? zwin() : (void)rozwin();
            e.obsluzone = true;
        }
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (obsluz_klawisz_specjalny(e)) return;
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }
    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0 || liczba_elementow() == 0) return;
        int n = wybrany_indeks(); if (n < 0) n = 0;
        n += e.delta_y > 0 ? -1 : 1;
        if (n < 0) n = 0;
        if (n >= static_cast<int>(liczba_elementow())) n = static_cast<int>(liczba_elementow()) - 1;
        if (ustaw_wybrany_indeks(n) && cb_zmiana_) cb_zmiana_(this, n, dane_zmiany_);
        e.obsluzone = true;
    }

private:
    bool obsluz_klawisz_specjalny(ZdarzenieKlawiatury& e) {
        uint32_t k = e.polecenie();
        bool dol = k == 0x50U;
        bool gora = k == 0x48U;
        if (czy_enter(e)) {
            if (czy_rozwinieta()) zatwierdz_popup(); else rozwin();
            e.obsluzone = true; return true;
        }
        if (dol || gora) {
            if (czy_rozwinieta()) {
                nawigacja_popupu_ = true; popup_.lista().obsluz_wcisniecie_klawisza(e);
                nawigacja_popupu_ = false;
            } else if (liczba_elementow() > 0) {
                int n = wybrany_indeks(); if (n < 0) n = dol ? 0 : static_cast<int>(liczba_elementow()) - 1;
                else n += dol ? 1 : -1;
                if (n < 0) n = 0;
                if (n >= static_cast<int>(liczba_elementow())) n = static_cast<int>(liczba_elementow()) - 1;
                if (ustaw_wybrany_indeks(n) && cb_zmiana_) cb_zmiana_(this, n, dane_zmiany_);
            }
            e.obsluzone = true; return true;
        }
        return false;
    }
    static bool czy_enter(const ZdarzenieKlawiatury& e) {
        uint32_t k = e.polecenie();
        return k == 13U || k == 10U || ((k & 0x7FU) == 0x1CU);
    }
    void wybrano_z_popupu(int indeks) {
        synchronizuj_tekst();
        if (!anulowanie_ && cb_zmiana_) { cb_zmiana_(this, indeks, dane_zmiany_); podglad_zgloszony_ = czy_rozwinieta(); }
        if (czy_rozwinieta() && !nawigacja_popupu_ && !otwieranie_) zatwierdz_popup();
        uniewaznij();
    }
    void zatwierdz_popup() {
        int indeks = wybrany_indeks();
        if (popup_.zarzadca()) popup_.zarzadca()->zamknij(PowodZamknieciaPopup::Zatwierdzenie);
        if (cb_zatwierdzenie_) cb_zatwierdzenie_(this, indeks, wybrany_tekst(), dane_zatwierdzenia_);
    }
    // Anulowanie (Esc, klik poza lista, zamkniecie wlasciciela) przywraca wybor sprzed otwarcia. Nawigacja strzalkami
    // w otwartej liscie zglasza zmiane na biezaco (podglad, np. preset korektora slychac od razu), wiec przywrocenie
    // tez musi byc zgloszone - inaczej aplikacja zostawala przy podgladanym elemencie, a lista pokazywala poprzedni.
    void popup_zamkniety(PowodZamknieciaPopup powod) {
        if (powod != PowodZamknieciaPopup::Zatwierdzenie && wybor_przed_otwarciem_ != wybrany_indeks()) {
            anulowanie_ = true;
            popup_.lista().ustaw_wybrany_indeks(wybor_przed_otwarciem_);
            anulowanie_ = false;
            synchronizuj_tekst();
            if (podglad_zgloszony_ && cb_zmiana_) cb_zmiana_(this, wybor_przed_otwarciem_, dane_zmiany_);
        }
        podglad_zgloszony_ = false;
        uniewaznij();
    }
    void synchronizuj_tekst() {
        if (!edytowalna_) return;
        const char* t = popup_.lista().wybrany_element();
        edycja_->ustaw_tekst(t ? t : "");
    }

    PoleEdycji* edycja_{nullptr};
    PopupListy popup_;
    bool edytowalna_{false}, nawigacja_popupu_{false}, otwieranie_{false}, anulowanie_{false}, podglad_zgloszony_{false};
    int maks_widocznych_{8}, wybor_przed_otwarciem_{-1};
    FunkcjaWyboru cb_zmiana_{nullptr}; void* dane_zmiany_{nullptr};
    FunkcjaZatwierdzenia cb_zatwierdzenie_{nullptr}; void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
