#pragma once

#include "kontrolka.h"
#include "przycisk.h"
#include "menu_kontekstowe.h"
#include "czcionka.h"

namespace BursztynUI {

enum class TypElementuPaskaNarzedzi {
    PrzyciskAkcji,
    Przelacznik,
    Separator,
    KontrolkaWlasna
};

class PasekNarzedzi;

class ElementPaskaNarzedzi {
public:
    using AkcjaPrzycisku = void (*)(ElementPaskaNarzedzi* nadawca, void* dane_uzytkownika);
    using AkcjaPrzelacznika = void (*)(ElementPaskaNarzedzi* nadawca, bool stan, void* dane_uzytkownika);

    ElementPaskaNarzedzi() = default;

    TypElementuPaskaNarzedzi typ() const { return typ_; }
    const char* tekst() const { return tekst_; }
    void ustaw_tekst(const char* t) {
        if (!t) { tekst_[0] = '\0'; return; }
        size_t i = 0;
        while (t[i] != '\0' && i < sizeof(tekst_) - 1) {
            tekst_[i] = t[i];
            ++i;
        }
        tekst_[i] = '\0';
    }

    const char* podpowiedz() const { return podpowiedz_; }
    void ustaw_podpowiedz(const char* p) {
        if (!p) { podpowiedz_[0] = '\0'; return; }
        size_t i = 0;
        while (p[i] != '\0' && i < sizeof(podpowiedz_) - 1) {
            podpowiedz_[i] = p[i];
            ++i;
        }
        podpowiedz_[i] = '\0';
    }

    uint32_t ikona() const { return ikona_; }
    void ustaw_ikone(uint32_t i) { ikona_ = i; }

    bool czy_wlaczony() const { return wlaczony_; }
    void ustaw_wlaczony(bool w) { wlaczony_ = w; }

    bool czy_wcisniety() const { return wcisniety_; }
    void ustaw_wcisniety(bool w) {
        if (wcisniety_ != w) {
            wcisniety_ = w;
            if (cb_przelacznik_) cb_przelacznik_(this, wcisniety_, dane_przelacznika_);
        }
    }
    bool czy_przelaczony() const { return czy_wcisniety(); }
    void ustaw_przelaczony(bool p) { ustaw_wcisniety(p); }

    Kontrolka* kontrolka() const { return kontrolka_; }
    void ustaw_kontrolke(Kontrolka* k) { kontrolka_ = k; }

    void ustaw_akcje(AkcjaPrzycisku cb, void* dane = nullptr) {
        cb_akcja_ = cb; dane_akcji_ = dane;
    }
    void ustaw_akcje_przelacznika(AkcjaPrzelacznika cb, void* dane = nullptr) {
        cb_przelacznik_ = cb; dane_przelacznika_ = dane;
    }

    void wykonaj() {
        if (!wlaczony_) return;
        if (typ_ == TypElementuPaskaNarzedzi::Przelacznik) {
            ustaw_wcisniety(!wcisniety_);
        } else if (cb_akcja_) {
            cb_akcja_(this, dane_akcji_);
        }
    }

private:
    friend class PasekNarzedzi;
    TypElementuPaskaNarzedzi typ_{TypElementuPaskaNarzedzi::PrzyciskAkcji};
    char tekst_[48]{};
    char podpowiedz_[64]{};
    uint32_t ikona_{0};
    bool wlaczony_{true};
    bool wcisniety_{false};
    bool najechany_{false};
    Kontrolka* kontrolka_{nullptr};
    Prostokat prostokat_{};

    AkcjaPrzycisku cb_akcja_{nullptr};
    void* dane_akcji_{nullptr};
    AkcjaPrzelacznika cb_przelacznik_{nullptr};
    void* dane_przelacznika_{nullptr};
};

class PasekNarzedzi : public Kontrolka {
public:
    static constexpr size_t MAKS_ELEMENTOW = 32;

    PasekNarzedzi() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = Kolory::PowierzchniaAlternatywna;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = false;
        preferowany_rozmiar_ = Rozmiar(300, 32);
    }

    ElementPaskaNarzedzi& dodaj_przycisk(const char* tekst, uint32_t ikona = 0,
                                         ElementPaskaNarzedzi::AkcjaPrzycisku akcja = nullptr,
                                         void* dane = nullptr) {
        ElementPaskaNarzedzi& el = dodaj_pusty_element(TypElementuPaskaNarzedzi::PrzyciskAkcji);
        el.ustaw_tekst(tekst);
        el.ustaw_ikone(ikona);
        el.ustaw_akcje(akcja, dane);
        przelicz_elementy();
        uniewaznij();
        return el;
    }

    ElementPaskaNarzedzi& dodaj_przelacznik(const char* tekst, bool wlaczony = false,
                                             ElementPaskaNarzedzi::AkcjaPrzelacznika akcja = nullptr,
                                             void* dane = nullptr) {
        ElementPaskaNarzedzi& el = dodaj_pusty_element(TypElementuPaskaNarzedzi::Przelacznik);
        el.ustaw_tekst(tekst);
        el.wcisniety_ = wlaczony;
        el.ustaw_akcje_przelacznika(akcja, dane);
        przelicz_elementy();
        uniewaznij();
        return el;
    }

    ElementPaskaNarzedzi& dodaj_separator() {
        ElementPaskaNarzedzi& el = dodaj_pusty_element(TypElementuPaskaNarzedzi::Separator);
        przelicz_elementy();
        uniewaznij();
        return el;
    }

    ElementPaskaNarzedzi& dodaj_kontrolke(Kontrolka* k) {
        ElementPaskaNarzedzi& el = dodaj_pusty_element(TypElementuPaskaNarzedzi::KontrolkaWlasna);
        el.ustaw_kontrolke(k);
        if (k) Kontrolka::dodaj_kontrolke(k);
        przelicz_elementy();
        uniewaznij();
        return el;
    }

    size_t liczba_elementow() const { return liczba_elementow_; }
    ElementPaskaNarzedzi* element(size_t i) {
        return (i < liczba_elementow_) ? &elementy_[i] : nullptr;
    }
    const ElementPaskaNarzedzi* element(size_t i) const {
        return (i < liczba_elementow_) ? &elementy_[i] : nullptr;
    }

    void ustaw_pokazywanie_tekstu(bool p) {
        if (pokazuj_tekst_ != p) {
            pokazuj_tekst_ = p;
            przelicz_elementy();
            uniewaznij();
        }
    }
    bool czy_pokazuje_tekst() const { return pokazuj_tekst_; }
    bool czy_jest_nadmiar() const { return czy_jest_nadmiar_; }

    void pokaz_menu_nadmiaru() {
        if (!zarzadca_popupow_) return;
        menu_nadmiaru_.menu().wyczysc();
        for (size_t i = liczba_widocznych_elementow_; i < liczba_elementow_; ++i) {
            ElementPaskaNarzedzi& el = elementy_[i];
            if (el.typ() == TypElementuPaskaNarzedzi::Separator) {
                menu_nadmiaru_.menu().dodaj_separator();
            } else if (el.typ() == TypElementuPaskaNarzedzi::KontrolkaWlasna) {
                continue;
            } else {
                ElementMenu& mi = menu_nadmiaru_.menu().dodaj_element(el.tekst(), static_cast<uint32_t>(i + 1),
                    [](ElementMenu&, void* dane) {
                        ElementPaskaNarzedzi* pel = static_cast<ElementPaskaNarzedzi*>(dane);
                        if (pel) pel->wykonaj();
                    }, &el);
                mi.ustaw_wlaczony(el.czy_wlaczony());
                if (el.typ() == TypElementuPaskaNarzedzi::Przelacznik) {
                    mi.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole);
                    mi.ustaw_zaznaczony(el.czy_przelaczony());
                }
            }
        }
        int pop_x = obszar_ekranu_.x + przycisk_nadmiaru_.x;
        int pop_y = obszar_ekranu_.y + przycisk_nadmiaru_.dolny();
        menu_nadmiaru_.pokaz(*zarzadca_popupow_, pop_x, pop_y);
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        przelicz_elementy();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        if (czy_jest_nadmiar_ && przycisk_nadmiaru_.zawiera(lx, ly)) {
            pokaz_menu_nadmiaru();
            e.obsluzone = true;
            return;
        }

        for (size_t i = 0; i < liczba_widocznych_elementow_; ++i) {
            ElementPaskaNarzedzi& el = elementy_[i];
            if (!el.czy_wlaczony()) continue;
            if (el.typ() == TypElementuPaskaNarzedzi::Separator ||
                el.typ() == TypElementuPaskaNarzedzi::KontrolkaWlasna) continue;

            if (el.prostokat_.zawiera(lx, ly)) {
                wcisniety_indeks_ = static_cast<int>(i);
                el.wykonaj();
                uniewaznij();
                e.obsluzone = true;
                return;
            }
        }
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (wcisniety_indeks_ >= 0) {
            wcisniety_indeks_ = -1;
            uniewaznij();
            e.obsluzone = true;
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        bool zmieniono = false;
        for (size_t i = 0; i < liczba_widocznych_elementow_; ++i) {
            ElementPaskaNarzedzi& el = elementy_[i];
            if (el.typ() == TypElementuPaskaNarzedzi::Separator) continue;
            bool hit = el.prostokat_.zawiera(lx, ly);
            if (el.najechany_ != hit) {
                el.najechany_ = hit;
                zmieniono = true;
            }
        }
        bool hit_ov = czy_jest_nadmiar_ && przycisk_nadmiaru_.zawiera(lx, ly);
        if (najechany_nadmiar_ != hit_ov) {
            najechany_nadmiar_ = hit_ov;
            zmieniono = true;
        }
        if (zmieniono) uniewaznij();
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        bool zmieniono = false;
        for (size_t i = 0; i < liczba_elementow_; ++i) {
            if (elementy_[i].najechany_) {
                elementy_[i].najechany_ = false;
                zmieniono = true;
            }
        }
        if (najechany_nadmiar_) {
            najechany_nadmiar_ = false;
            zmieniono = true;
        }
        if (zmieniono) uniewaznij();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        ctx.rysuj_prostokat(b, styl_.normalny.tlo);
        ctx.rysuj_ramke(b, styl_.normalny.ramka, 1);

        for (size_t i = 0; i < liczba_widocznych_elementow_; ++i) {
            ElementPaskaNarzedzi& el = elementy_[i];
            Prostokat r = el.prostokat_;

            if (el.typ() == TypElementuPaskaNarzedzi::Separator) {
                int cx = r.x + r.szerokosc / 2;
                ctx.rysuj_linie_pionowa(cx, r.y + 4, r.wysokosc - 8, Kolory::RamkaDomyslna);
            } else if (el.typ() == TypElementuPaskaNarzedzi::KontrolkaWlasna) {
                // Kontrolka rysuje się sama
            } else {
                bool aktywne_wcisniecie = (static_cast<int>(i) == wcisniety_indeks_);
                bool przelaczony = el.czy_wcisniety();
                bool najechany = el.najechany_;
                bool wlaczony = el.czy_wlaczony();

                uint32_t tlo = (przelaczony || aktywne_wcisniecie) ? Kolory::AkcentWcisniety :
                               (najechany ? Kolory::PowierzchniaNajechana : 0);
                uint32_t ramka = najechany ? Kolory::RamkaNajechana :
                                 (przelaczony ? Kolory::RamkaFokus : 0);
                uint32_t tekst_k = !wlaczony ? Kolory::WylaczonyTekst :
                                   ((przelaczony || aktywne_wcisniecie) ? Kolory::TekstCiemny : Kolory::TekstBialy);

                if (tlo != 0) ctx.rysuj_prostokat(r, tlo);
                if (ramka != 0) ctx.rysuj_ramke(r, ramka, 1);

                ctx.rysuj_tekst_wysrodkowany(r, el.tekst(), tekst_k, skala_dpi_);
            }
        }

        if (czy_jest_nadmiar_) {
            uint32_t bg_ov = najechany_nadmiar_ ? Kolory::PowierzchniaNajechana : 0;
            if (bg_ov != 0) ctx.rysuj_prostokat(przycisk_nadmiaru_, bg_ov);
            ctx.rysuj_ramke(przycisk_nadmiaru_, Kolory::RamkaDomyslna, 1);
            ctx.rysuj_tekst_wysrodkowany(przycisk_nadmiaru_, ">>", Kolory::TekstBialy, skala_dpi_);
        }
    }

private:
    ElementPaskaNarzedzi& dodaj_pusty_element(TypElementuPaskaNarzedzi typ) {
        if (liczba_elementow_ >= MAKS_ELEMENTOW) return elementy_[MAKS_ELEMENTOW - 1];
        ElementPaskaNarzedzi& el = elementy_[liczba_elementow_++];
        el = ElementPaskaNarzedzi();
        el.typ_ = typ;
        return el;
    }

    void przelicz_elementy() {
        int w = obszar_ekranu_.szerokosc;
        int h = obszar_ekranu_.wysokosc;
        if (w <= 0 || h <= 0) return;

        int item_h = h - 6;
        int x = 4;
        liczba_widocznych_elementow_ = 0;
        czy_jest_nadmiar_ = false;

        int ov_w = 20;
        int max_w = w - ov_w - 4;

        for (size_t i = 0; i < liczba_elementow_; ++i) {
            ElementPaskaNarzedzi& el = elementy_[i];
            int item_w = 0;

            if (el.typ() == TypElementuPaskaNarzedzi::Separator) {
                item_w = 8;
            } else if (el.typ() == TypElementuPaskaNarzedzi::KontrolkaWlasna) {
                item_w = el.kontrolka() ? el.kontrolka()->preferowany_rozmiar().szerokosc : 60;
            } else {
                item_w = CzcionkaUI::zmierz_tekst(el.tekst(), skala_dpi_) + 16;
                if (item_w < 28) item_w = 28;
            }

            if (x + item_w > max_w && i + 1 < liczba_elementow_) {
                czy_jest_nadmiar_ = true;
                break;
            }

            el.prostokat_ = Prostokat(x, 3, item_w, item_h);
            if (el.typ() == TypElementuPaskaNarzedzi::KontrolkaWlasna && el.kontrolka()) {
                el.kontrolka()->rozmiesc(Prostokat(obszar_ekranu_.x + x, obszar_ekranu_.y + 3, item_w, item_h));
                el.kontrolka()->ustaw_widoczna(true);
            }
            x += item_w + 4;
            ++liczba_widocznych_elementow_;
        }

        if (czy_jest_nadmiar_) {
            przycisk_nadmiaru_ = Prostokat(w - ov_w - 2, 3, ov_w, item_h);
            for (size_t i = liczba_widocznych_elementow_; i < liczba_elementow_; ++i) {
                if (elementy_[i].typ() == TypElementuPaskaNarzedzi::KontrolkaWlasna && elementy_[i].kontrolka()) {
                    elementy_[i].kontrolka()->ustaw_widoczna(false);
                }
            }
        }
    }

    ElementPaskaNarzedzi elementy_[MAKS_ELEMENTOW];
    size_t liczba_elementow_{0};
    size_t liczba_widocznych_elementow_{0};
    bool pokazuj_tekst_{true};
    bool czy_jest_nadmiar_{false};
    bool najechany_nadmiar_{false};
    int wcisniety_indeks_{-1};

    MenuKontekstowe menu_nadmiaru_;
    Prostokat przycisk_nadmiaru_;
};

} // namespace BursztynUI
