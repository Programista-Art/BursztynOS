#pragma once

#include "popup.h"
#include "panel.h"
#include "przycisk.h"
#include "etykieta.h"
#include "motyw.h"

namespace BursztynUI {

enum class WynikDialogu : uint8_t {
    Brak,
    OK,
    Anuluj,
    Tak,
    Nie
};

enum class PrzyciskiDialogu : uint8_t {
    OkAnuluj,
    TakNie,
    TylkoOk
};

class OknoDialogowe : public PowierzchniaPopup {
public:
    using FunkcjaWyniku = void (*)(OknoDialogowe* dialog, WynikDialogu wynik, void* dane);

    explicit OknoDialogowe(const char* tytul = "Komunikat",
                           const char* wiadomosc = "",
                           PrzyciskiDialogu przyciski = PrzyciskiDialogu::OkAnuluj)
        : przyciski_typ_(przyciski) {
        ustaw_modalna(true);
        ustaw_zamykanie_na_klik_poza(false);
        ustaw_tekst(tytul);
        zaktualizuj_motyw();

        ustaw_wiadomosc(wiadomosc);
        zbuduj_interfejs();
    }

    ~OknoDialogowe() override {
        // Kontrolki podrzędne zostaną zwolnione przez destruktor Kontrolka
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_okna;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_OKNOTLO);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = 2;
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_OKNO);
        Kontrolka::zaktualizuj_motyw();
    }

    const char* wiadomosc() const { return wiadomosc_; }
    void ustaw_wiadomosc(const char* w) {
        if (!w) w = "";
        size_t i = 0;
        while (w[i] && i < sizeof(wiadomosc_) - 1) {
            wiadomosc_[i] = w[i];
            ++i;
        }
        wiadomosc_[i] = '\0';
        if (etykieta_wiadomosci_) {
            etykieta_wiadomosci_->ustaw_tekst(wiadomosc_);
        }
        uniewaznij();
    }

    WynikDialogu wynik() const { return wynik_; }

    void przy_wyniku(FunkcjaWyniku cb, void* dane = nullptr) {
        cb_wynik_ = cb;
        dane_wyniku_ = dane;
    }

    bool pokaz(ZarzadcaPopupow& z, Kontrolka* owner = nullptr) {
        SkalaUI s = skala_dpi_;
        int w = 360 * s;
        int h = 170 * s;

        int cx = 200;
        int cy = 150;
        if (owner) {
            Prostokat oe = owner->obszar_ekranu();
            cx = oe.x + (oe.szerokosc - w) / 2;
            cy = oe.y + (oe.wysokosc - h) / 2;
        }

        Prostokat gdzie = z.umiesc_przy_punkcie_ekranu(cx, cy, Rozmiar(w, h));

        wynik_ = WynikDialogu::Brak;
        z.zamknij_od_poziomu(0); // Zamknij ewentualne menu
        return z.dodaj_powierzchnie(*this, gdzie, przycisk_domyslny_);
    }

    void zamknij_z_wynikiem(WynikDialogu w) {
        wynik_ = w;
        if (cb_wynik_) {
            cb_wynik_(this, wynik_, dane_wyniku_);
        }
        if (zarzadca()) {
            zarzadca()->zamknij_powierzchnie(this,
                w == WynikDialogu::Anuluj || w == WynikDialogu::Nie
                    ? PowodZamknieciaPopup::Anulowanie
                    : PowodZamknieciaPopup::Zatwierdzenie);
        }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        // Escape: anuluj
        if (kod == 27U || (usb && baza == 41U) || (!usb && baza == 0x01U)) {
            if (przyciski_typ_ == PrzyciskiDialogu::TakNie) {
                zamknij_z_wynikiem(WynikDialogu::Nie);
            } else {
                zamknij_z_wynikiem(WynikDialogu::Anuluj);
            }
            e.obsluzone = true;
            return;
        }

        // Enter: wykonaj domyślny przycisk
        if (kod == 13U || kod == 10U || (usb && baza == 40U) || (!usb && baza == 0x1CU)) {
            if (przyciski_typ_ == PrzyciskiDialogu::TakNie) {
                zamknij_z_wynikiem(WynikDialogu::Tak);
            } else {
                zamknij_z_wynikiem(WynikDialogu::OK);
            }
            e.obsluzone = true;
            return;
        }

        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        // Tło okna dialogowego z zaokrąglonymi rogami i ramką
        int promien = motyw_promien(MOTYW_ROG_OKNO) * s;
        ctx.rysuj_zaokraglony_prostokat(b, styl_.normalny.tlo, promien);
        ctx.rysuj_zaokraglona_ramke(b, styl_.normalny.ramka, promien, 2 * s);

        // Belka nagłówkowa
        int h_belki = 30 * s;
        Prostokat belka(0, 0, obszar_.szerokosc, h_belki);
        ctx.rysuj_zaokraglony_prostokat(belka, motyw_kolor(MOTYW_KOLOR_PANEL), promien);
        ctx.rysuj_prostokat(Prostokat(0, h_belki - 6 * s, obszar_.szerokosc, 6 * s), motyw_kolor(MOTYW_KOLOR_PANEL));
        ctx.rysuj_linie_pozioma(0, h_belki, obszar_.szerokosc, motyw_kolor(MOTYW_KOLOR_RAMKA));

        // Tytuł w belce
        ctx.rysuj_tekst(Punkt(12 * s, (h_belki - CzcionkaUI::pobierz_wysokosc(s)) / 2),
                        tekst_, motyw_kolor(MOTYW_KOLOR_TEKST), s);
    }

private:
    void zbuduj_interfejs() {
        wyczysc_kontrolki();

        // Główny panel wewnętrzny
        Panel* panel = new Panel(Orientacja::Pionowa);
        panel->ustaw_wypelnienie(Marginesy(16, 40, 16, 12));
        panel->ustaw_odstep(12);
        panel->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        panel->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        panel->ustaw_kolor_tla(0);
        panel->ustaw_kolor_ramki(0);
        panel->ustaw_grubosc_ramki(0);

        // Treść wiadomości
        etykieta_wiadomosci_ = &panel->dodaj<Etykieta>(wiadomosc_);
        etykieta_wiadomosci_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        etykieta_wiadomosci_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        etykieta_wiadomosci_->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));

        // Pasek przycisków na dole
        Panel& pasek_przyciskow = panel->dodaj<Panel>(Orientacja::Pozioma);
        pasek_przyciskow.ustaw_preferowany_rozmiar(Rozmiar(-1, 36));
        pasek_przyciskow.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        pasek_przyciskow.ustaw_odstep(10);
        pasek_przyciskow.ustaw_kolor_tla(0);
        pasek_przyciskow.ustaw_kolor_ramki(0);
        pasek_przyciskow.ustaw_grubosc_ramki(0);

        // Wypełniacz rozpychający przyciski do prawej
        Panel& spacja = pasek_przyciskow.dodaj<Panel>();
        spacja.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        spacja.ustaw_kolor_tla(0);
        spacja.ustaw_kolor_ramki(0);

        if (przyciski_typ_ == PrzyciskiDialogu::OkAnuluj) {
            przycisk_domyslny_ = &pasek_przyciskow.dodaj<Przycisk>("OK");
            przycisk_domyslny_->ustaw_preferowany_rozmiar(Rozmiar(80, 32));
            przycisk_domyslny_->przy_kliknieciu([](Przycisk*, void* d) {
                static_cast<OknoDialogowe*>(d)->zamknij_z_wynikiem(WynikDialogu::OK);
            }, this);

            przycisk_anuluj_ = &pasek_przyciskow.dodaj<Przycisk>("Anuluj");
            przycisk_anuluj_->ustaw_preferowany_rozmiar(Rozmiar(80, 32));
            przycisk_anuluj_->przy_kliknieciu([](Przycisk*, void* d) {
                static_cast<OknoDialogowe*>(d)->zamknij_z_wynikiem(WynikDialogu::Anuluj);
            }, this);
        } else if (przyciski_typ_ == PrzyciskiDialogu::TakNie) {
            przycisk_domyslny_ = &pasek_przyciskow.dodaj<Przycisk>("Tak");
            przycisk_domyslny_->ustaw_preferowany_rozmiar(Rozmiar(80, 32));
            przycisk_domyslny_->przy_kliknieciu([](Przycisk*, void* d) {
                static_cast<OknoDialogowe*>(d)->zamknij_z_wynikiem(WynikDialogu::Tak);
            }, this);

            przycisk_anuluj_ = &pasek_przyciskow.dodaj<Przycisk>("Nie");
            przycisk_anuluj_->ustaw_preferowany_rozmiar(Rozmiar(80, 32));
            przycisk_anuluj_->przy_kliknieciu([](Przycisk*, void* d) {
                static_cast<OknoDialogowe*>(d)->zamknij_z_wynikiem(WynikDialogu::Nie);
            }, this);
        } else { // TylkoOk
            przycisk_domyslny_ = &pasek_przyciskow.dodaj<Przycisk>("OK");
            przycisk_domyslny_->ustaw_preferowany_rozmiar(Rozmiar(80, 32));
            przycisk_domyslny_->przy_kliknieciu([](Przycisk*, void* d) {
                static_cast<OknoDialogowe*>(d)->zamknij_z_wynikiem(WynikDialogu::OK);
            }, this);
        }

        dodaj_kontrolke(panel);
    }

    PrzyciskiDialogu przyciski_typ_{PrzyciskiDialogu::OkAnuluj};
    char wiadomosc_[128]{};
    WynikDialogu wynik_{WynikDialogu::Brak};
    FunkcjaWyniku cb_wynik_{nullptr};
    void* dane_wyniku_{nullptr};

    Etykieta* etykieta_wiadomosci_{nullptr};
    Przycisk* przycisk_domyslny_{nullptr};
    Przycisk* przycisk_anuluj_{nullptr};
};

} // namespace BursztynUI
