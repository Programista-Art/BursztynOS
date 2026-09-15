#pragma once

#include "kontrolka.h"
#include "czcionka.h"
#include "motyw.h"
#include "ikona_ui.h"

namespace BursztynUI {

enum class TypKomunikatu : uint8_t {
    Informacja,
    Ostrzezenie,
    Blad,
    Sukces
};

class PasekInformacyjny : public Kontrolka {
public:
    using FunkcjaAkcji = void (*)(PasekInformacyjny*, void*);

    explicit PasekInformacyjny(TypKomunikatu typ = TypKomunikatu::Informacja,
                               const char* tekst_glowny = "Informacja",
                               const char* tekst_drugi = nullptr)
        : typ_(typ) {
        zaktualizuj_motyw();
        wypelnienie_ = Marginesy(12, 8, 12, 8);
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        czy_moze_miec_fokus_ = false;

        ustaw_tekst(tekst_glowny);
        ustaw_tekst_drugorzedny(tekst_drugi);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        styl_.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        Kontrolka::zaktualizuj_motyw();
    }

    TypKomunikatu typ_komunikatu() const { return typ_; }
    void ustaw_typ_komunikatu(TypKomunikatu t) {
        if (typ_ != t) {
            typ_ = t;
            uniewaznij();
        }
    }

    const char* tekst_drugorzedny() const { return tekst_drugi_; }
    void ustaw_tekst_drugorzedny(const char* t) {
        if (!t) {
            tekst_drugi_[0] = '\0';
        } else {
            size_t i = 0;
            while (t[i] && i < sizeof(tekst_drugi_) - 1) {
                tekst_drugi_[i] = t[i];
                ++i;
            }
            tekst_drugi_[i] = '\0';
        }
        uniewaznij();
    }

    const char* etykieta_akcji() const { return akcja_tekst_; }
    void ustaw_akcje(const char* etykieta, FunkcjaAkcji cb = nullptr, void* dane = nullptr) {
        if (!etykieta) {
            akcja_tekst_[0] = '\0';
        } else {
            size_t i = 0;
            while (etykieta[i] && i < sizeof(akcja_tekst_) - 1) {
                akcja_tekst_[i] = etykieta[i];
                ++i;
            }
            akcja_tekst_[i] = '\0';
        }
        cb_akcja_ = cb;
        dane_akcji_ = dane;
        uniewaznij();
    }

    bool czy_ma_przycisk_zamkniecia() const { return pokaz_zamknij_; }
    void ustaw_przycisk_zamkniecia(bool p) {
        if (pokaz_zamknij_ != p) {
            pokaz_zamknij_ = p;
            uniewaznij();
        }
    }

    void przy_zamknieciu(FunkcjaAkcji cb, void* dane = nullptr) {
        cb_zamknij_ = cb;
        dane_zamknij_ = dane;
    }

    Rozmiar zmierz(Rozmiar dostepny) override {
        SkalaUI s = skala_dpi_;
        int pad_h = wypelnienie_.poziomo() * s;
        int pad_v = wypelnienie_.pionowo() * s;

        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, s);
        if (tekst_drugi_[0]) {
            txt_w += 8 * s + CzcionkaUI::zmierz_tekst(tekst_drugi_, s);
        }
        int btn_w = akcja_tekst_[0] ? (CzcionkaUI::zmierz_tekst(akcja_tekst_, s) + 16 * s) : 0;
        int close_w = pokaz_zamknij_ ? 24 * s : 0;

        int req_w = pad_h + 30 * s + txt_w + btn_w + close_w;
        int req_h = pad_v + CzcionkaUI::pobierz_wysokosc(s) + 10 * s;
        if (req_h < 38 * s) req_h = 38 * s;

        Rozmiar zadany(req_w, req_h);
        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * s;
        (void)dostepny;
        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        uint32_t kolor_stanu = pobierz_kolor_stanu();
        int promien = motyw_promien(MOTYW_ROG_PANEL) * s;

        // Tło
        ctx.rysuj_zaokraglony_prostokat(b, styl_.normalny.tlo, promien);

        // Ramka
        ctx.rysuj_zaokraglona_ramke(b, styl_.normalny.ramka, promien, styl_.normalny.grubosc_ramki * s);

        // Pionowy pasek akcentujący stan po lewej stronie
        int pasek_w = 4 * s;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(2 * s, 3 * s, pasek_w, b.wysokosc - 6 * s),
                                        kolor_stanu, pasek_w / 2);

        // Znak/ikona stanu
        int icon_x = 14 * s;
        int icon_y = (b.wysokosc - 16 * s) / 2;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(icon_x, icon_y, 16 * s, 16 * s), kolor_stanu, 8 * s);
        const char* symbol = "?";
        switch (typ_) {
            case TypKomunikatu::Sukces: symbol = "+"; break;
            case TypKomunikatu::Ostrzezenie: symbol = "!"; break;
            case TypKomunikatu::Blad: symbol = "x"; break;
            case TypKomunikatu::Informacja:
            default: symbol = "i"; break;
        }
        ctx.rysuj_tekst_wysrodkowany(Prostokat(icon_x, icon_y, 16 * s, 16 * s), symbol,
                                     motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU), s);

        // Tekst główny
        int txt_x = icon_x + 22 * s;
        int font_h = CzcionkaUI::pobierz_wysokosc(s);
        int txt_y = (b.wysokosc - font_h) / 2;
        ctx.rysuj_tekst(Punkt(txt_x, txt_y), tekst_, motyw_kolor(MOTYW_KOLOR_TEKST), s);

        // Tekst drugorzędny
        int main_w = CzcionkaUI::zmierz_tekst(tekst_, s);
        if (tekst_drugi_[0]) {
            int sub_x = txt_x + main_w + 10 * s;
            ctx.rysuj_tekst(Punkt(sub_x, txt_y), tekst_drugi_, motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);
        }

        // Przycisk akcji
        int right_margin = 8 * s;
        if (pokaz_zamknij_) {
            przycisk_zamknij_r_ = Prostokat(b.prawy() - 24 * s - right_margin, (b.wysokosc - 20 * s) / 2, 20 * s, 20 * s);
            if (najechany_zamknij_) {
                ctx.rysuj_zaokraglony_prostokat(przycisk_zamknij_r_, motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY), 4 * s);
            }
            ctx.rysuj_tekst_wysrodkowany(przycisk_zamknij_r_, "X", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);
            right_margin += 26 * s;
        }

        if (akcja_tekst_[0]) {
            int bw = CzcionkaUI::zmierz_tekst(akcja_tekst_, s) + 16 * s;
            int bh = 24 * s;
            przycisk_akcji_r_ = Prostokat(b.prawy() - bw - right_margin, (b.wysokosc - bh) / 2, bw, bh);

            uint32_t btn_bg = najechany_akcja_ ? motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY)
                                               : motyw_kolor(MOTYW_KOLOR_PRZYCISK);
            ctx.rysuj_zaokraglony_prostokat(przycisk_akcji_r_, btn_bg, 4 * s);
            ctx.rysuj_zaokraglona_ramke(przycisk_akcji_r_, motyw_kolor(MOTYW_KOLOR_RAMKA), 4 * s, 1);
            ctx.rysuj_tekst_wysrodkowany(przycisk_akcji_r_, akcja_tekst_, kolor_stanu, s);
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        bool na_akcji = akcja_tekst_[0] && przycisk_akcji_r_.zawiera(lx, ly);
        bool na_zamknij = pokaz_zamknij_ && przycisk_zamknij_r_.zawiera(lx, ly);

        if (na_akcji != najechany_akcja_ || na_zamknij != najechany_zamknij_) {
            najechany_akcja_ = na_akcji;
            najechany_zamknij_ = na_zamknij;
            uniewaznij();
        }
        Kontrolka::obsluz_ruch_myszy(e);
    }

    void obsluz_opuszczenie_myszy() override {
        if (najechany_akcja_ || najechany_zamknij_) {
            najechany_akcja_ = false;
            najechany_zamknij_ = false;
            uniewaznij();
        }
        Kontrolka::obsluz_opuszczenie_myszy();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        if (pokaz_zamknij_ && przycisk_zamknij_r_.zawiera(lx, ly)) {
            ukryj();
            if (cb_zamknij_) cb_zamknij_(this, dane_zamknij_);
            e.obsluzone = true;
            return;
        }

        if (akcja_tekst_[0] && przycisk_akcji_r_.zawiera(lx, ly)) {
            if (cb_akcja_) cb_akcja_(this, dane_akcji_);
            e.obsluzone = true;
            return;
        }

        Kontrolka::obsluz_wcisniecie_myszy(e);
    }

private:
    uint32_t pobierz_kolor_stanu() const {
        switch (typ_) {
            case TypKomunikatu::Sukces: return motyw_kolor(MOTYW_KOLOR_SUKCES);
            case TypKomunikatu::Ostrzezenie: return motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE);
            case TypKomunikatu::Blad: return motyw_kolor(MOTYW_KOLOR_BLAD);
            case TypKomunikatu::Informacja:
            default: return motyw_kolor(MOTYW_KOLOR_INFORMACJA);
        }
    }

    TypKomunikatu typ_{TypKomunikatu::Informacja};
    char tekst_drugi_[128]{};
    char akcja_tekst_[48]{};
    bool pokaz_zamknij_{true};

    bool najechany_akcja_{false};
    bool najechany_zamknij_{false};
    Prostokat przycisk_akcji_r_{};
    Prostokat przycisk_zamknij_r_{};

    FunkcjaAkcji cb_akcja_{nullptr};
    void* dane_akcji_{nullptr};
    FunkcjaAkcji cb_zamknij_{nullptr};
    void* dane_zamknij_{nullptr};
};

} // namespace BursztynUI
