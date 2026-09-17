#pragma once

#include "widok_menu_popup.h"

namespace BursztynUI {

class MenuKontekstowe : private ObslugaWidokuMenu {
public:
    MenuKontekstowe() {
        for (int i = 0; i < ZarzadcaPopupow::MAKS_GLEBOKOSC; ++i)
            widoki_[i].ustaw(nullptr, this);
    }
    ~MenuKontekstowe() {
        if (widoki_[0].zarzadca())
            widoki_[0].zarzadca()->zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
    }
    Menu& menu() { return menu_; }
    const Menu& menu() const { return menu_; }
    bool czy_otwarte() const { return widoki_[0].czy_otwarta(); }
    Kontrolka* kontrolka_wywolujaca() const { return kontrolka_wywolujaca_; }

    template <typename TOkno>
    bool pokaz(TOkno& okno, int x_ekranu, int y_ekranu) {
        return pokaz(okno.zarzadca_popupow(), x_ekranu, y_ekranu, okno.skala_dpi());
    }
    bool pokaz(ZarzadcaPopupow& z, int x_ekranu, int y_ekranu, int skala_dpi = 1,
               Kontrolka* wywolujaca = nullptr) {
        kontrolka_wywolujaca_ = wywolujaca;
        widoki_[0].zaktualizuj_motyw();
        widoki_[0].ustaw(&menu_, this);
        Rozmiar rz = widoki_[0].wymagany_rozmiar(skala_dpi);
        Prostokat gdzie = z.umiesc_przy_punkcie_ekranu(x_ekranu, y_ekranu, rz);
        return z.otworz(widoki_[0], gdzie, wywolujaca, &widoki_[0]);
    }
    bool pokaz_dla(Kontrolka& wywolujaca, int x_ekranu, int y_ekranu) {
        ZarzadcaPopupow* z = wywolujaca.zarzadca_popupow();
        if (!z) return false;
        kontrolka_wywolujaca_ = &wywolujaca;
        wywolujaca.przygotuj_menu_kontekstowe(*this);
        return pokaz(*z, x_ekranu, y_ekranu, wywolujaca.skala_dpi(), &wywolujaca);
    }
    bool pokaz_pod_kontrolka(ZarzadcaPopupow& z, Kontrolka& owner,
                             Kontrolka* kotwica = nullptr) {
        kontrolka_wywolujaca_ = &owner;
        owner.przygotuj_menu_kontekstowe(*this);
        widoki_[0].zaktualizuj_motyw();
        widoki_[0].ustaw(&menu_, this);
        Kontrolka& kontrolka_kotwicy = kotwica ? *kotwica : owner;
        Prostokat kotwica_ekranu = kontrolka_kotwicy.prostokat_do_ekranu(
            Prostokat(0, 0, kontrolka_kotwicy.szerokosc(), kontrolka_kotwicy.wysokosc()));
        return z.otworz(widoki_[0], z.umiesc_pod_kotwica_ekranu(kotwica_ekranu,
            widoki_[0].wymagany_rozmiar(owner.skala_dpi())), &owner, &widoki_[0]);
    }
    void ukryj() {
        if (widoki_[0].zarzadca()) widoki_[0].zarzadca()->zamknij();
    }

private:
    void menu_aktywuj(WidokMenuPopup& widok, ElementMenu& e) override {
        if (e.typ_zaznaczenia() == TypZaznaczeniaMenu::Pole)
            e.ustaw_zaznaczony(!e.czy_zaznaczony());
        else if (e.typ_zaznaczenia() == TypZaznaczeniaMenu::Radio && widok.menu())
            widok.menu()->zaznacz_radio(e.grupa_radialna(), e.identyfikator());
        ZarzadcaPopupow* z = widok.zarzadca();
        if (z) z->zamknij(PowodZamknieciaPopup::Zatwierdzenie);
        e.wykonaj();
    }
    void menu_otworz_podmenu(WidokMenuPopup& widok, ElementMenu& e,
                              Prostokat element) override {
        if (!e.podmenu() || !widok.zarzadca()) return;
        int poziom = widok.poziom_popupu() + 1;
        if (poziom >= ZarzadcaPopupow::MAKS_GLEBOKOSC) return;
        ZarzadcaPopupow* z = widok.zarzadca();
        z->zamknij_od_poziomu(poziom);
        widoki_[poziom].zaktualizuj_motyw();
        widoki_[poziom].ustaw(e.podmenu(), this);
        Prostokat gdzie = z->umiesc_podmenu(element,
            widoki_[poziom].wymagany_rozmiar(widok.skala_dpi()));
        (void)z->otworz_podmenu(widoki_[poziom], gdzie, &widoki_[poziom]);
    }
    void menu_kierunek_poziomy(WidokMenuPopup& widok, int kierunek) override {
        if (kierunek < 0 && widok.poziom_popupu() > 0 && widok.zarzadca())
            widok.zarzadca()->zamknij_od_poziomu(widok.poziom_popupu());
    }

    Menu menu_;
    WidokMenuPopup widoki_[ZarzadcaPopupow::MAKS_GLEBOKOSC];
    Kontrolka* kontrolka_wywolujaca_{nullptr};
};

} // namespace BursztynUI
