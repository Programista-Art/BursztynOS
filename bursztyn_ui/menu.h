#pragma once

#include <stdint.h>
#include <stddef.h>

namespace BursztynUI {

class Menu;
class ElementMenu;

enum class TypZaznaczeniaMenu { Brak, Pole, Radio };

using AkcjaMenu = void (*)(ElementMenu& element, void* dane_uzytkownika);

class ElementMenu {
public:
    ElementMenu() = default;
    ~ElementMenu();
    ElementMenu(const ElementMenu&) = delete;
    ElementMenu& operator=(const ElementMenu&) = delete;

    const char* tekst() const { return tekst_; }
    ElementMenu& ustaw_tekst(const char* t) { kopiuj(tekst_, sizeof(tekst_), t); return *this; }
    uint32_t identyfikator() const { return identyfikator_; }
    ElementMenu& ustaw_identyfikator(uint32_t id) { identyfikator_ = id; return *this; }
    bool czy_wlaczony() const { return wlaczony_; }
    ElementMenu& ustaw_wlaczony(bool v) { wlaczony_ = v; return *this; }
    bool czy_widoczny() const { return widoczny_; }
    ElementMenu& ustaw_widoczny(bool v) { widoczny_ = v; return *this; }
    bool czy_separator() const { return separator_; }
    bool czy_zaznaczony() const { return zaznaczony_; }
    ElementMenu& ustaw_zaznaczony(bool v) { zaznaczony_ = v; return *this; }
    TypZaznaczeniaMenu typ_zaznaczenia() const { return typ_zaznaczenia_; }
    ElementMenu& ustaw_typ_zaznaczenia(TypZaznaczeniaMenu t) { typ_zaznaczenia_ = t; return *this; }
    int grupa_radialna() const { return grupa_radialna_; }
    ElementMenu& ustaw_grupe_radialna(int grupa) { grupa_radialna_ = grupa; return *this; }
    const char* skrot() const { return skrot_; }
    ElementMenu& ustaw_skrot(const char* s) { kopiuj(skrot_, sizeof(skrot_), s); return *this; }
    uint32_t ikona() const { return ikona_; }
    ElementMenu& ustaw_ikone(uint32_t i) { ikona_ = i; return *this; }
    uint64_t znacznik() const { return znacznik_; }
    ElementMenu& ustaw_znacznik(uint64_t z) { znacznik_ = z; return *this; }
    Menu* podmenu() const { return podmenu_; }
    Menu& utworz_podmenu();
    ElementMenu& ustaw_akcje(AkcjaMenu cb, void* dane = nullptr) {
        akcja_ = cb; dane_akcji_ = dane; return *this;
    }
    void wykonaj() { if (wlaczony_ && !separator_ && akcja_) akcja_(*this, dane_akcji_); }

private:
    friend class Menu;
    static void kopiuj(char* dst, size_t cap, const char* src) {
        if (!src) src = "";
        size_t i = 0; for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i]; dst[i] = '\0';
    }
    void ustaw_separator() { separator_ = true; wlaczony_ = false; tekst_[0] = '\0'; }
    void przenies_z(ElementMenu& o) {
        kopiuj(tekst_, sizeof(tekst_), o.tekst_); kopiuj(skrot_, sizeof(skrot_), o.skrot_);
        identyfikator_ = o.identyfikator_; wlaczony_ = o.wlaczony_; widoczny_ = o.widoczny_;
        separator_ = o.separator_; zaznaczony_ = o.zaznaczony_;
        typ_zaznaczenia_ = o.typ_zaznaczenia_; grupa_radialna_ = o.grupa_radialna_;
        ikona_ = o.ikona_; znacznik_ = o.znacznik_; akcja_ = o.akcja_; dane_akcji_ = o.dane_akcji_;
        podmenu_ = o.podmenu_; o.podmenu_ = nullptr;
    }

    char tekst_[96]{};
    char skrot_[40]{};
    uint32_t identyfikator_{0}, ikona_{0};
    uint64_t znacznik_{0};
    bool wlaczony_{true}, widoczny_{true}, separator_{false}, zaznaczony_{false};
    TypZaznaczeniaMenu typ_zaznaczenia_{TypZaznaczeniaMenu::Brak};
    int grupa_radialna_{0};
    Menu* podmenu_{nullptr};
    AkcjaMenu akcja_{nullptr};
    void* dane_akcji_{nullptr};
};

class Menu {
public:
    Menu() = default;
    ~Menu() { delete[] elementy_; }
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    size_t liczba_elementow() const { return liczba_; }
    ElementMenu* element(size_t i) { return i < liczba_ ? &elementy_[i] : nullptr; }
    const ElementMenu* element(size_t i) const { return i < liczba_ ? &elementy_[i] : nullptr; }
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
        e.ustaw_tekst(tekst).ustaw_identyfikator(id).ustaw_akcje(akcja, dane);
        return e;
    }
    ElementMenu& wstaw_element(size_t indeks, const char* tekst, uint32_t id = 0) {
        if (indeks > liczba_) indeks = liczba_;
        zapewnij(liczba_ + 1);
        for (size_t i = liczba_; i > indeks; --i) elementy_[i].przenies_z(elementy_[i - 1]);
        ++liczba_;
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
        delete elementy_[indeks].podmenu_; elementy_[indeks].podmenu_ = nullptr;
        for (size_t i = indeks; i + 1 < liczba_; ++i) elementy_[i].przenies_z(elementy_[i + 1]);
        --liczba_; return true;
    }
    void wyczysc() {
        for (size_t i = 0; i < liczba_; ++i) {
            delete elementy_[i].podmenu_;
            elementy_[i].podmenu_ = nullptr;
            elementy_[i].tekst_[0] = '\0';
            elementy_[i].skrot_[0] = '\0';
            elementy_[i].identyfikator_ = 0;
            elementy_[i].ikona_ = 0;
            elementy_[i].znacznik_ = 0;
            elementy_[i].wlaczony_ = true;
            elementy_[i].widoczny_ = true;
            elementy_[i].separator_ = false;
            elementy_[i].zaznaczony_ = false;
            elementy_[i].typ_zaznaczenia_ = TypZaznaczeniaMenu::Brak;
            elementy_[i].grupa_radialna_ = 0;
            elementy_[i].akcja_ = nullptr;
            elementy_[i].dane_akcji_ = nullptr;
        }
        liczba_ = 0;
    }
    void zaznacz_radio(int grupa, uint32_t id) {
        for (size_t i = 0; i < liczba_; ++i) {
            ElementMenu& e = elementy_[i];
            if (e.typ_zaznaczenia_ == TypZaznaczeniaMenu::Radio && e.grupa_radialna_ == grupa)
                e.zaznaczony_ = e.identyfikator_ == id;
        }
    }

private:
    void zapewnij(size_t n) {
        if (n <= pojemnosc_) return;
        size_t nowa = pojemnosc_ ? pojemnosc_ * 2 : 8;
        while (nowa < n) nowa *= 2;
        ElementMenu* p = new ElementMenu[nowa];
        for (size_t i = 0; i < liczba_; ++i) p[i].przenies_z(elementy_[i]);
        delete[] elementy_; elementy_ = p; pojemnosc_ = nowa;
    }
    ElementMenu* elementy_{nullptr};
    size_t liczba_{0}, pojemnosc_{0};
};

inline Menu& ElementMenu::utworz_podmenu() {
    if (!podmenu_) podmenu_ = new Menu();
    return *podmenu_;
}

inline ElementMenu::~ElementMenu() { delete podmenu_; }

} // namespace BursztynUI
