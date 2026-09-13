#pragma once

#include "widok_menu_popup.h"

namespace BursztynUI {

class MenuGlowne : public Kontrolka, private ObslugaWidokuMenu {
public:
    MenuGlowne() {
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = true;
        for (int i = 0; i < ZarzadcaPopupow::MAKS_GLEBOKOSC; ++i)
            widoki_[i].ustaw(nullptr, this);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.grubosc_ramki = 0;
        styl_.normalny.ramka = 0;
        for (int i = 0; i < ZarzadcaPopupow::MAKS_GLEBOKOSC; ++i)
            widoki_[i].zaktualizuj_motyw();
        uniewaznij();
    }
    ~MenuGlowne() override {
        if (widoki_[0].zarzadca())
            widoki_[0].zarzadca()->zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
    }
    Menu& menu() { return menu_; }
    const Menu& menu() const { return menu_; }
    bool czy_otwarte() const { return widoki_[0].czy_otwarta(); }

    Kontrolka* kontrolka_aktywacji_alt() override {
        aktywny_glowny_ = nastepny_glowny(-1, 1); uniewaznij(); return this;
    }
    Rozmiar oblicz_naturalny_rozmiar() const override {
        return Rozmiar(300 * skala_dpi_, 28 * skala_dpi_);
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());
        for (int i = 0; i < static_cast<int>(menu_.liczba_elementow()); ++i) {
            ElementMenu* e = menu_.element(i); if (!e || !e->czy_widoczny()) continue;
            Prostokat r = prostokat_glowny(i);
            if (i == aktywny_glowny_ && (czy_ma_fokus_ || czy_otwarte()))
                ctx.rysuj_prostokat(r, motyw_kolor(MOTYW_KOLOR_PODNIESIONY));
            SkalaUI s = skala_dpi_;
            ctx.rysuj_tekst(Punkt(r.x + 10 * s,
                r.y + (r.wysokosc - CzcionkaUI::pobierz_wysokosc(s)) / 2),
                e->tekst(), e->czy_wlaczony() ? motyw_kolor(MOTYW_KOLOR_TEKST) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY), s);
        }
    }
    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        int i = indeks_glowny_z_x(e.x - obszar_ekranu_.x);
        if (i >= 0 && i != aktywny_glowny_) {
            int stary = aktywny_glowny_; aktywny_glowny_ = i;
            uniewaznij_obszar(prostokat_glowny(stary)); uniewaznij_obszar(prostokat_glowny(i));
            if (czy_otwarte()) otworz_glowny(i);
        }
        e.obsluzone = i >= 0;
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!e.czy_lewy_przycisk()) return;
        int i = indeks_glowny_z_x(e.x - obszar_ekranu_.x);
        if (i >= 0) { aktywny_glowny_ = i; otworz_glowny(i); e.obsluzone = true; }
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        uint32_t k = e.kod_klawisza; bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        uint32_t b = usb ? k & 0xFFU : k & 0x7FU;
        if ((usb && b == 79U) || (!usb && b == 0x4DU)) aktywny_glowny_ = nastepny_glowny(aktywny_glowny_, 1);
        else if ((usb && b == 80U) || (!usb && b == 0x4BU)) aktywny_glowny_ = nastepny_glowny(aktywny_glowny_, -1);
        else if ((usb && b == 81U) || (!usb && b == 0x50U) || k == 13U || b == 0x1CU)
            otworz_glowny(aktywny_glowny_ < 0 ? nastepny_glowny(-1, 1) : aktywny_glowny_);
        else return;
        uniewaznij(); e.obsluzone = true;
    }

private:
    Prostokat prostokat_glowny(int indeks) const {
        SkalaUI s = skala_dpi_; int x = 0;
        for (int i = 0; i < static_cast<int>(menu_.liczba_elementow()); ++i) {
            const ElementMenu* e = menu_.element(i); if (!e || !e->czy_widoczny()) continue;
            int w = CzcionkaUI::zmierz_tekst(e->tekst(), s) + 20 * s;
            if (i == indeks) return Prostokat(x, 0, w, obszar_.wysokosc);
            x += w;
        }
        return Prostokat();
    }
    int indeks_glowny_z_x(int x) const {
        for (int i = 0; i < static_cast<int>(menu_.liczba_elementow()); ++i) {
            const ElementMenu* e = menu_.element(i);
            if (e && e->czy_widoczny() && prostokat_glowny(i).zawiera(x, obszar_.wysokosc / 2)) return i;
        }
        return -1;
    }
    int nastepny_glowny(int od, int kierunek) const {
        int n = static_cast<int>(menu_.liczba_elementow()); if (!n) return -1;
        for (int k = 0; k < n; ++k) {
            od = (od + kierunek + n) % n;
            const ElementMenu* e = menu_.element(od);
            if (e && e->czy_widoczny() && e->czy_wlaczony() && !e->czy_separator()) return od;
        }
        return -1;
    }
    void otworz_glowny(int indeks) {
        ElementMenu* e = indeks >= 0 ? menu_.element(indeks) : nullptr;
        ZarzadcaPopupow* z = zarzadca_popupow();
        if (!e || !e->czy_wlaczony() || !z) return;
        aktywny_glowny_ = indeks;
        if (!e->podmenu()) { e->wykonaj(); return; }
        widoki_[0].ustaw(e->podmenu(), this);
        Prostokat kotwica = prostokat_glowny(indeks);
        kotwica.x += obszar_ekranu_.x; kotwica.y += obszar_ekranu_.y;
        Prostokat gdzie = z->umiesc_pod_kotwica(kotwica,
            widoki_[0].wymagany_rozmiar(skala_dpi_), true);
        (void)z->otworz(widoki_[0], gdzie, this, &widoki_[0]);
        uniewaznij_obszar(prostokat_glowny(indeks));
    }
    void menu_aktywuj(WidokMenuPopup& widok, ElementMenu& e) override {
        if (e.typ_zaznaczenia() == TypZaznaczeniaMenu::Pole) e.ustaw_zaznaczony(!e.czy_zaznaczony());
        else if (e.typ_zaznaczenia() == TypZaznaczeniaMenu::Radio && widok.menu())
            widok.menu()->zaznacz_radio(e.grupa_radialna(), e.identyfikator());
        ZarzadcaPopupow* z = widok.zarzadca(); if (z) z->zamknij(PowodZamknieciaPopup::Zatwierdzenie);
        e.wykonaj(); uniewaznij();
    }
    void menu_otworz_podmenu(WidokMenuPopup& widok, ElementMenu& e, Prostokat element) override {
        if (!e.podmenu() || !widok.zarzadca()) return;
        int poziom = widok.poziom_popupu() + 1;
        if (poziom >= ZarzadcaPopupow::MAKS_GLEBOKOSC) return;
        ZarzadcaPopupow* z = widok.zarzadca(); z->zamknij_od_poziomu(poziom);
        widoki_[poziom].ustaw(e.podmenu(), this);
        Prostokat gdzie = z->umiesc_podmenu(element,
            widoki_[poziom].wymagany_rozmiar(skala_dpi_));
        (void)z->otworz_podmenu(widoki_[poziom], gdzie, &widoki_[poziom]);
    }
    void menu_kierunek_poziomy(WidokMenuPopup& widok, int kierunek) override {
        if (widok.poziom_popupu() > 0 && kierunek < 0 && widok.zarzadca()) {
            widok.zarzadca()->zamknij_od_poziomu(widok.poziom_popupu()); return;
        }
        int n = nastepny_glowny(aktywny_glowny_, kierunek); if (n >= 0) otworz_glowny(n);
    }

    Menu menu_;
    WidokMenuPopup widoki_[ZarzadcaPopupow::MAKS_GLEBOKOSC];
    int aktywny_glowny_{-1};
};

} // namespace BursztynUI
