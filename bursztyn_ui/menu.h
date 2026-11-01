#pragma once

#include <stdint.h>
#include <stddef.h>
#include "polecenie_ui.h"

namespace BursztynUI {

class IkonaPNG;
class Menu;
class ElementMenu;

enum class TypZaznaczeniaMenu { Brak, Pole, Radio };

using AkcjaMenu = void (*)(ElementMenu& element, void* dane_uzytkownika);

class ElementMenu : private OdbiorcaPolecenia {
public:
    ElementMenu() = default;
    ~ElementMenu();
    ElementMenu(const ElementMenu&) = delete;
    ElementMenu& operator=(const ElementMenu&) = delete;

    const char* tekst() const { return tekst_; }
    ElementMenu& ustaw_tekst(const char* t) { kopiuj(tekst_, sizeof(tekst_), t); powiadom(); return *this; }
    uint32_t identyfikator() const { return identyfikator_; }
    ElementMenu& ustaw_identyfikator(uint32_t id) { identyfikator_ = id; powiadom(); return *this; }
    bool czy_wlaczony() const { return wlaczony_; }
    ElementMenu& ustaw_wlaczony(bool v) { if (polecenie_) polecenie_->ustaw_wlaczona(v); else { wlaczony_ = v; powiadom(); } return *this; }
    bool czy_widoczny() const { return widoczny_; }
    ElementMenu& ustaw_widoczny(bool v) { if (polecenie_) polecenie_->ustaw_widoczna(v); else { widoczny_ = v; powiadom(); } return *this; }
    bool czy_separator() const { return separator_; }
    bool czy_zaznaczony() const { return zaznaczony_; }
    ElementMenu& ustaw_zaznaczony(bool v) { if (polecenie_) polecenie_->ustaw_zaznaczona(v); else { zaznaczony_ = v; powiadom(); } return *this; }
    TypZaznaczeniaMenu typ_zaznaczenia() const { return typ_zaznaczenia_; }
    ElementMenu& ustaw_typ_zaznaczenia(TypZaznaczeniaMenu t) { typ_zaznaczenia_ = t; powiadom(); return *this; }
    int grupa_radialna() const { return grupa_radialna_; }
    ElementMenu& ustaw_grupe_radialna(int grupa) { grupa_radialna_ = grupa; powiadom(); return *this; }
    const char* skrot() const { return skrot_; }
    ElementMenu& ustaw_skrot(const char* s) { kopiuj(skrot_, sizeof(skrot_), s); powiadom(); return *this; }
    uint32_t ikona() const { return ikona_; }
    ElementMenu& ustaw_ikone(uint32_t i) { ikona_ = i; powiadom(); return *this; }
    const IkonaPNG* obraz_ikony() const { return obraz_ikony_; }
    ElementMenu& ustaw_obraz_ikony(const IkonaPNG* p) { if(obraz_ikony_!=p){obraz_ikony_=p; powiadom();} return *this; }
    uint64_t znacznik() const { return znacznik_; }
    ElementMenu& ustaw_znacznik(uint64_t z) { znacznik_ = z; return *this; }
    Menu* podmenu() const { return podmenu_; }
    Menu& utworz_podmenu();
    ElementMenu& ustaw_akcje(AkcjaMenu cb, void* dane = nullptr) {
        akcja_ = cb; dane_akcji_ = dane; return *this;
    }
    PolecenieUI* polecenie() const { return polecenie_; }
    bool ustaw_polecenie(PolecenieUI* p) {
        if (polecenie_ == p) return true;
        if (polecenie_) polecenie_->odlacz(this);
        polecenie_ = nullptr;
        if (p && !p->dolacz(this)) return false;
        polecenie_ = p; return true;
    }
    bool ustaw_polecenie(PolecenieUI& p) { return ustaw_polecenie(&p); }
    void wykonaj() {
        if (!wlaczony_ || !widoczny_ || separator_) return;
        if (polecenie_) polecenie_->wykonaj();
        else if (akcja_) akcja_(*this, dane_akcji_);
    }

private:
    friend class Menu;
    static void kopiuj(char* dst, size_t cap, const char* src) {
        if (!src) src = "";
        size_t i = 0; for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i]; dst[i] = '\0';
    }
    void ustaw_separator() { separator_ = true; wlaczony_ = false; tekst_[0] = '\0'; powiadom(); }
    void powiadom();
    void wyzeruj();
    void przenies_z(ElementMenu& o) {
        if (polecenie_) polecenie_->odlacz(this);
        kopiuj(tekst_, sizeof(tekst_), o.tekst_); kopiuj(skrot_, sizeof(skrot_), o.skrot_);
        identyfikator_ = o.identyfikator_; wlaczony_ = o.wlaczony_; widoczny_ = o.widoczny_;
        separator_ = o.separator_; zaznaczony_ = o.zaznaczony_;
        typ_zaznaczenia_ = o.typ_zaznaczenia_; grupa_radialna_ = o.grupa_radialna_;
        obraz_ikony_ = o.obraz_ikony_; ikona_ = o.ikona_; znacznik_ = o.znacznik_; akcja_ = o.akcja_; dane_akcji_ = o.dane_akcji_;
        podmenu_ = o.podmenu_; o.podmenu_ = nullptr;
        polecenie_ = o.polecenie_;
        if (polecenie_) polecenie_->zastap_odbiorce(&o, this);
        o.polecenie_ = nullptr;
    }
    void odswiez_z_polecenia(PolecenieUI& p) override {
        identyfikator_ = p.identyfikator(); kopiuj(tekst_, sizeof(tekst_), p.tekst());
        kopiuj(skrot_, sizeof(skrot_), p.etykieta_skrotu()); ikona_ = p.ikona();
        wlaczony_ = p.czy_wlaczona(); widoczny_ = p.czy_widoczna();
        zaznaczony_ = p.czy_zaznaczona(); powiadom();
    }
    void polecenie_zniszczone(PolecenieUI& p) override { if (polecenie_ == &p) polecenie_ = nullptr; }

    char tekst_[96]{};
    char skrot_[40]{};
    uint32_t identyfikator_{0}, ikona_{0};
    const IkonaPNG* obraz_ikony_{nullptr};
    uint64_t znacznik_{0};
    bool wlaczony_{true}, widoczny_{true}, separator_{false}, zaznaczony_{false};
    TypZaznaczeniaMenu typ_zaznaczenia_{TypZaznaczeniaMenu::Brak};
    int grupa_radialna_{0};
    Menu* podmenu_{nullptr};
    AkcjaMenu akcja_{nullptr};
    void* dane_akcji_{nullptr};
    PolecenieUI* polecenie_{nullptr};
    Menu* wlasciciel_{nullptr};
};

class Menu {
public:
    using FunkcjaZmiany = void (*)(Menu&, void*);
    Menu() = default;
    ~Menu() { delete[] elementy_; }
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    size_t liczba_elementow() const { return liczba_; }
    ElementMenu* element(size_t i) { return i < liczba_ ? &elementy_[i] : nullptr; }
    const ElementMenu* element(size_t i) const { return i < liczba_ ? &elementy_[i] : nullptr; }
    void przy_zmianie(FunkcjaZmiany cb, void* dane = nullptr) { zmiana_ = cb; dane_zmiany_ = dane; }
    ElementMenu* znajdz(uint32_t id) {
        for (size_t i = 0; i < liczba_; ++i) {
            if (elementy_[i].identyfikator() == id) return &elementy_[i];
            if (elementy_[i].podmenu()) {
                ElementMenu* wynik = elementy_[i].podmenu()->znajdz(id);
                if (wynik) return wynik;
            }
        }
        return nullptr;
    }

    ElementMenu& dodaj_element(const char* tekst, uint32_t id = 0,
                               AkcjaMenu akcja = nullptr, void* dane = nullptr) {
        zapewnij(liczba_ + 1);
        ElementMenu& e = elementy_[liczba_++];
        e.wyzeruj();
        e.wlasciciel_ = this;
        e.ustaw_tekst(tekst).ustaw_identyfikator(id).ustaw_akcje(akcja, dane);
        return e;
    }
    ElementMenu& dodaj_polecenie(PolecenieUI& polecenie) {
        ElementMenu& e = dodaj_element(polecenie.tekst(), polecenie.identyfikator());
        (void)e.ustaw_polecenie(polecenie);
        return e;
    }
    ElementMenu& wstaw_element(size_t indeks, const char* tekst, uint32_t id = 0) {
        if (indeks > liczba_) indeks = liczba_;
        zapewnij(liczba_ + 1);
        for (size_t i = liczba_; i > indeks; --i) elementy_[i].przenies_z(elementy_[i - 1]);
        ++liczba_;
        elementy_[indeks].wyzeruj();
        elementy_[indeks].wlasciciel_ = this;
        return elementy_[indeks].ustaw_tekst(tekst).ustaw_identyfikator(id);
    }
    ElementMenu& dodaj_separator() {
        zapewnij(liczba_ + 1); ElementMenu& e = elementy_[liczba_++]; e.ustaw_separator(); return e;
    }
    Menu& dodaj_podmenu(const char* tekst, uint32_t id = 0) {
        return dodaj_element(tekst, id).utworz_podmenu();
    }
    bool usun_element(size_t indeks) {
        if (indeks >= liczba_) return false;
        elementy_[indeks].wyzeruj();
        for (size_t i = indeks; i + 1 < liczba_; ++i) elementy_[i].przenies_z(elementy_[i + 1]);
        --liczba_; powiadom(); return true;
    }
    void wyczysc() {
        for (size_t i = 0; i < liczba_; ++i) {
            elementy_[i].wyzeruj();
        }
        liczba_ = 0; powiadom();
    }
    void zaznacz_radio(int grupa, uint32_t id) {
        for (size_t i = 0; i < liczba_; ++i) {
            ElementMenu& e = elementy_[i];
            if (e.typ_zaznaczenia_ == TypZaznaczeniaMenu::Radio && e.grupa_radialna_ == grupa)
                e.ustaw_zaznaczony(e.identyfikator_ == id);
        }
        powiadom();
    }

    void powiadom() { if (zmiana_) zmiana_(*this, dane_zmiany_); }

private:
    void zapewnij(size_t n) {
        if (n <= pojemnosc_) return;
        size_t nowa = pojemnosc_ ? pojemnosc_ * 2 : 8;
        while (nowa < n) nowa *= 2;
        ElementMenu* p = new ElementMenu[nowa];
        for (size_t i = 0; i < nowa; ++i) p[i].wlasciciel_ = this;
        for (size_t i = 0; i < liczba_; ++i) p[i].przenies_z(elementy_[i]);
        delete[] elementy_; elementy_ = p; pojemnosc_ = nowa;
    }
    ElementMenu* elementy_{nullptr};
    size_t liczba_{0}, pojemnosc_{0};
    FunkcjaZmiany zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
};

inline Menu& ElementMenu::utworz_podmenu() {
    if (!podmenu_) podmenu_ = new Menu();
    return *podmenu_;
}

inline void ElementMenu::powiadom() { if (wlasciciel_) wlasciciel_->powiadom(); }

inline void ElementMenu::wyzeruj() {
    if (polecenie_) polecenie_->odlacz(this);
    polecenie_ = nullptr;
    delete podmenu_; podmenu_ = nullptr;
    obraz_ikony_ = nullptr; tekst_[0] = '\0'; skrot_[0] = '\0'; identyfikator_ = 0; ikona_ = 0; znacznik_ = 0;
    wlaczony_ = true; widoczny_ = true; separator_ = false; zaznaczony_ = false;
    typ_zaznaczenia_ = TypZaznaczeniaMenu::Brak; grupa_radialna_ = 0;
    akcja_ = nullptr; dane_akcji_ = nullptr;
}

inline ElementMenu::~ElementMenu() { if (polecenie_) polecenie_->odlacz(this); delete podmenu_; }

} // namespace BursztynUI
