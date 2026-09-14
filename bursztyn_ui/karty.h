#pragma once

#include "kontrolka.h"
#include "panel.h"
#include "czcionka.h"

namespace BursztynUI {

class Karty;

class StronaKarty {
public:
    StronaKarty() = default;

    const char* tytul() const { return tytul_; }
    void ustaw_tytul(const char* t) {
        if (!t) { tytul_[0] = '\0'; return; }
        size_t i = 0;
        while (t[i] != '\0' && i < sizeof(tytul_) - 1) {
            tytul_[i] = t[i];
            ++i;
        }
        tytul_[i] = '\0';
    }

    bool czy_wlaczona() const { return wlaczona_; }
    void ustaw_wlaczona(bool w) { wlaczona_ = w; }

    bool czy_zamykalna() const { return zamykalna_; }
    void ustaw_zamykalna(bool z) { zamykalna_ = z; }

    Kontrolka* zawartosc() const { return zawartosc_; }
    void ustaw_zawartosc(Kontrolka* k) { zawartosc_ = k; }

    uint64_t tag() const { return tag_; }
    void ustaw_tag(uint64_t t) { tag_ = t; }

private:
    friend class Karty;
    char tytul_[48]{};
    bool wlaczona_{true};
    bool zamykalna_{false};
    Kontrolka* zawartosc_{nullptr};
    uint64_t tag_{0};
    Prostokat prostokat_naglowka_{};
};

class Karty : public Kontrolka {
public:
    static constexpr int MAKS_KART = 32;
    using FunkcjaKarty = void (*)(Karty* nadawca, int indeks_karty, void* dane_uzytkownika);

    Karty() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        czy_moze_miec_fokus_ = true;
        preferowany_rozmiar_ = Rozmiar(-1, -1);
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;

        obszar_stron_ = new Panel();
        obszar_stron_->ustaw_kolor_tla(0);
        obszar_stron_->ustaw_grubosc_ramki(0);
        obszar_stron_->ustaw_wypelnienie(Marginesy(0));
        obszar_stron_->ustaw_uklad_automatyczny(false);
        dodaj_kontrolke(obszar_stron_);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        uniewaznij();
        Kontrolka::zaktualizuj_motyw();
    }

    int dodaj_karte(const char* tytul, Kontrolka* zawartosc = nullptr) {
        return wstaw_karte(liczba_kart_, tytul, zawartosc);
    }

    int wstaw_karte(int indeks, const char* tytul, Kontrolka* zawartosc = nullptr) {
        if (liczba_kart_ >= MAKS_KART) return -1;
        if (indeks < 0) indeks = 0;
        if (indeks > liczba_kart_) indeks = liczba_kart_;

        for (int i = liczba_kart_; i > indeks; --i) {
            strony_[i] = strony_[i - 1];
        }

        strony_[indeks] = StronaKarty();
        strony_[indeks].ustaw_tytul(tytul);

        if (!zawartosc) {
            Panel* domyslny_panel = new Panel();
            domyslny_panel->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
            zawartosc = domyslny_panel;
        }
        strony_[indeks].ustaw_zawartosc(zawartosc);
        obszar_stron_->dodaj_kontrolke(zawartosc);

        ++liczba_kart_;

        if (aktywna_karta_ < 0) {
            ustaw_aktywna_karte(indeks);
        } else if (aktywna_karta_ >= indeks) {
            ustaw_aktywna_karte(aktywna_karta_ + 1);
        } else {
            zawartosc->ustaw_widoczna(false);
        }

        przelicz_naglowki();
        rozmiesc_strone(zawartosc);
        uniewaznij();
        return indeks;
    }

    bool usun_karte(int indeks) {
        if (indeks < 0 || indeks >= liczba_kart_) return false;

        Kontrolka* k = strony_[indeks].zawartosc();
        if (k) {
            k->ustaw_widoczna(false);
        }

        for (int i = indeks; i < liczba_kart_ - 1; ++i) {
            strony_[i] = strony_[i + 1];
        }
        --liczba_kart_;
        strony_[liczba_kart_] = StronaKarty();

        if (liczba_kart_ == 0) {
            aktywna_karta_ = -1;
        } else if (aktywna_karta_ >= liczba_kart_) {
            ustaw_aktywna_karte(liczba_kart_ - 1);
        } else if (aktywna_karta_ == indeks) {
            ustaw_aktywna_karte(indeks < liczba_kart_ ? indeks : liczba_kart_ - 1);
        }

        przelicz_naglowki();
        uniewaznij();
        return true;
    }

    void wyczysc() {
        for (int i = 0; i < liczba_kart_; ++i) {
            if (strony_[i].zawartosc()) strony_[i].zawartosc()->ustaw_widoczna(false);
            strony_[i] = StronaKarty();
        }
        liczba_kart_ = 0;
        aktywna_karta_ = -1;
        przelicz_naglowki();
        uniewaznij();
    }

    int liczba_kart() const { return liczba_kart_; }
    int aktywna_karta() const { return aktywna_karta_; }
    Prostokat prostokat_obszaru_strony() const {
        const int y = wysokosc_paska_kart_;
        return Prostokat(1, y,
                         obszar_.szerokosc > 2 ? obszar_.szerokosc - 2 : 0,
                         obszar_.wysokosc > y + 2 ? obszar_.wysokosc - y - 2 : 0);
    }

    bool ustaw_aktywna_karte(int indeks) {
        if (indeks < 0 || indeks >= liczba_kart_ || !strony_[indeks].czy_wlaczona()) return false;
        if (aktywna_karta_ == indeks) return true;

        const int stara_karta = aktywna_karta_;
        if (stara_karta >= 0 && stara_karta < liczba_kart_) {
            if (strony_[stara_karta].zawartosc()) {
                strony_[stara_karta].zawartosc()->ustaw_widoczna(false);
            }
        }

        aktywna_karta_ = indeks;
        if (strony_[aktywna_karta_].zawartosc()) {
            strony_[aktywna_karta_].zawartosc()->ustaw_widoczna(true);
        }

        dopasuj_przewiniecie_do_aktywnej();
        if (stara_karta >= 0 && stara_karta < liczba_kart_)
            uniewaznij_obszar(strony_[stara_karta].prostokat_naglowka_);
        uniewaznij_obszar(strony_[aktywna_karta_].prostokat_naglowka_);

        if (cb_zmiana_) {
            cb_zmiana_(this, aktywna_karta_, dane_zmiany_);
        }
        return true;
    }

    StronaKarty* strona(int indeks) {
        return (indeks >= 0 && indeks < liczba_kart_) ? &strony_[indeks] : nullptr;
    }
    const StronaKarty* strona(int indeks) const {
        return (indeks >= 0 && indeks < liczba_kart_) ? &strony_[indeks] : nullptr;
    }

    void ustaw_tekst_karty(int indeks, const char* t) {
        if (indeks >= 0 && indeks < liczba_kart_) {
            strony_[indeks].ustaw_tytul(t);
            przelicz_naglowki();
            uniewaznij();
        }
    }
    const char* tekst_karty(int indeks) const {
        return (indeks >= 0 && indeks < liczba_kart_) ? strony_[indeks].tytul() : "";
    }

    void ustaw_wlaczona_karte(int indeks, bool w) {
        if (indeks >= 0 && indeks < liczba_kart_) {
            strony_[indeks].ustaw_wlaczona(w);
            if (!w && aktywna_karta_ == indeks) {
                przelacz_na_kolejna();
            }
            uniewaznij();
        }
    }
    bool czy_karta_wlaczona(int indeks) const {
        return (indeks >= 0 && indeks < liczba_kart_) && strony_[indeks].czy_wlaczona();
    }

    void ustaw_zamykalna(int indeks, bool z) {
        if (indeks >= 0 && indeks < liczba_kart_) {
            strony_[indeks].ustaw_zamykalna(z);
            przelicz_naglowki();
            uniewaznij();
        }
    }

    void przy_zmianie_karty(FunkcjaKarty cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
    }

    void przy_zamknieciu_karty(FunkcjaKarty cb, void* dane = nullptr) {
        cb_zamkniecie_ = cb;
        dane_zamkniecia_ = dane;
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI s = skala_dpi_;
        return Rozmiar(320 * s, 200 * s);
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        przelicz_naglowki();
        rozmiesc_wszystkie_strony();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        if (ly < wysokosc_paska_kart_) {
            // Sprawdź przyciski przewijania paska kart
            if (czy_przewijany_pasek_) {
                if (przycisk_lewo_.zawiera(lx, ly)) {
                    przewin_pasek(-30);
                    e.obsluzone = true;
                    return;
                }
                if (przycisk_prawo_.zawiera(lx, ly)) {
                    przewin_pasek(30);
                    e.obsluzone = true;
                    return;
                }
            }

            for (int i = 0; i < liczba_kart_; ++i) {
                if (!strony_[i].czy_wlaczona()) continue;
                Prostokat nr = strony_[i].prostokat_naglowka_;
                if (nr.zawiera(lx, ly)) {
                    // Sprawdź klik w przycisk zamknięcia 'x'
                    if (strony_[i].czy_zamykalna()) {
                        Prostokat x_btn(nr.prawy() - 18, nr.y + (nr.wysokosc - 14) / 2, 14, 14);
                        if (x_btn.zawiera(lx, ly)) {
                            if (cb_zamkniecie_) cb_zamkniecie_(this, i, dane_zamkniecia_);
                            usun_karte(i);
                            e.obsluzone = true;
                            return;
                        }
                    }
                    ustaw_aktywna_karte(i);
                    e.obsluzone = true;
                    return;
                }
            }
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        int nowy_hover = -1;
        if (ly < wysokosc_paska_kart_) {
            for (int i = 0; i < liczba_kart_; ++i) {
                if (strony_[i].prostokat_naglowka_.zawiera(lx, ly)) {
                    nowy_hover = i;
                    break;
                }
            }
        }
        if (nowy_hover != najechana_karta_) {
            int stary_hover = najechana_karta_;
            najechana_karta_ = nowy_hover;
            if (stary_hover >= 0 && stary_hover < liczba_kart_)
                uniewaznij_obszar(strony_[stary_hover].prostokat_naglowka_);
            if (nowy_hover >= 0 && nowy_hover < liczba_kart_)
                uniewaznij_obszar(strony_[nowy_hover].prostokat_naglowka_);
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechana_karta_ != -1) {
            int stary_hover = najechana_karta_;
            najechana_karta_ = -1;
            if (stary_hover >= 0 && stary_hover < liczba_kart_)
                uniewaznij_obszar(strony_[stary_hover].prostokat_naglowka_);
        }
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0) return;
        if (e.pozycja().y < obszar_ekranu_.y + wysokosc_paska_kart_ && czy_przewijany_pasek_) {
            przewin_pasek(e.delta_y > 0 ? -30 : 30);
            e.obsluzone = true;
        }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        // Ctrl+Tab / Ctrl+Shift+Tab
        if (e.czy_ctrl() && (kod == 9 || (usb && baza == 43U))) {
            if (e.czy_shift()) przelacz_na_poprzednia();
            else przelacz_na_kolejna();
            e.obsluzone = true;
            return;
        }

        // Left / Right
        if ((!usb && e0 && baza == 0x4BU) || (usb && baza == 80U)) {
            przelacz_na_poprzednia();
            e.obsluzone = true;
            return;
        }
        if ((!usb && e0 && baza == 0x4DU) || (usb && baza == 79U)) {
            przelacz_na_kolejna();
            e.obsluzone = true;
            return;
        }

        // Home / End
        if ((!usb && e0 && baza == 0x47U) || (usb && baza == 74U)) {
            for (int i = 0; i < liczba_kart_; ++i) {
                if (strony_[i].czy_wlaczona()) { ustaw_aktywna_karte(i); break; }
            }
            e.obsluzone = true;
            return;
        }
        if ((!usb && e0 && baza == 0x4FU) || (usb && baza == 77U)) {
            for (int i = liczba_kart_ - 1; i >= 0; --i) {
                if (strony_[i].czy_wlaczona()) { ustaw_aktywna_karte(i); break; }
            }
            e.obsluzone = true;
            return;
        }
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        uint32_t ramka = czy_ma_fokus_ ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_RAMKA);

        // Pasek kart (tło)
        Prostokat pasek_r(0, 0, b.szerokosc, wysokosc_paska_kart_);
        ctx.rysuj_prostokat(pasek_r, motyw_kolor(MOTYW_KOLOR_PANEL));

        // Obszar strony (tło i ramka)
        Prostokat strona_r(0, wysokosc_paska_kart_ - 1, b.szerokosc, b.wysokosc - wysokosc_paska_kart_ + 1);
        ctx.rysuj_prostokat(strona_r, motyw_kolor(MOTYW_KOLOR_OKNOTLO));
        ctx.rysuj_ramke(strona_r, ramka, 1);

        // Rysuj nagłówki kart
        Prostokat pasek_clip(0, 0, czy_przewijany_pasek_ ? b.szerokosc - 36 : b.szerokosc, wysokosc_paska_kart_);
        ctx.dodaj_przyciecie(pasek_clip);

        for (int i = 0; i < liczba_kart_; ++i) {
            Prostokat nr = strony_[i].prostokat_naglowka_;
            if (nr.szerokosc <= 0) continue;
            bool aktywna = (i == aktywna_karta_);
            bool najechana = (i == najechana_karta_);
            bool wlaczona = strony_[i].czy_wlaczona();

            uint32_t tlo_karty = aktywna ? motyw_kolor(MOTYW_KOLOR_OKNOTLO) : (najechana ? motyw_kolor(MOTYW_KOLOR_PODNIESIONY) : motyw_kolor(MOTYW_KOLOR_PANEL));
            uint32_t tekst_karty = !wlaczona ? motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY) : (aktywna ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_TEKST));
            uint32_t ramka_karty = aktywna ? ramka : motyw_kolor(MOTYW_KOLOR_RAMKA);

            ctx.rysuj_prostokat(nr, tlo_karty);
            ctx.rysuj_ramke(nr, ramka_karty, 1);

            // Linia łącząca aktywną kartę ze stroną (usuwa dolną ramkę nagłówka)
            if (aktywna) {
                ctx.rysuj_linie_pozioma(nr.x + 1, nr.dolny() - 1, nr.szerokosc - 2, motyw_kolor(MOTYW_KOLOR_OKNOTLO));
            }

            // Tekst
            int text_pad_right = strony_[i].czy_zamykalna() ? 24 : 8;
            Prostokat tekst_r(nr.x + 8, nr.y + 4, nr.szerokosc > 8 + text_pad_right ? nr.szerokosc - 8 - text_pad_right : 0, nr.wysokosc - 8);
            ctx.rysuj_tekst_wysrodkowany(tekst_r, strony_[i].tytul(), tekst_karty, skala_dpi_);

            // Przycisk 'x'
            if (strony_[i].czy_zamykalna()) {
                Prostokat x_btn(nr.prawy() - 18, nr.y + (nr.wysokosc - 14) / 2, 14, 14);
                ctx.rysuj_tekst(Punkt(x_btn.x + 3, x_btn.y - 1), "x", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), skala_dpi_);
            }
        }
        ctx.zdejmij_przyciecie();

        // Przyciski przewijania paska
        if (czy_przewijany_pasek_) {
            ctx.rysuj_prostokat(przycisk_lewo_, motyw_kolor(MOTYW_KOLOR_PANEL));
            ctx.rysuj_ramke(przycisk_lewo_, motyw_kolor(MOTYW_KOLOR_RAMKA), 1);
            ctx.rysuj_tekst(Punkt(przycisk_lewo_.x + 4, przycisk_lewo_.y + 4), "<", motyw_kolor(MOTYW_KOLOR_TEKST), skala_dpi_);

            ctx.rysuj_prostokat(przycisk_prawo_, motyw_kolor(MOTYW_KOLOR_PANEL));
            ctx.rysuj_ramke(przycisk_prawo_, motyw_kolor(MOTYW_KOLOR_RAMKA), 1);
            ctx.rysuj_tekst(Punkt(przycisk_prawo_.x + 4, przycisk_prawo_.y + 4), ">", motyw_kolor(MOTYW_KOLOR_TEKST), skala_dpi_);
        }
    }

private:
    void przelicz_naglowki() {
        int w = obszar_.szerokosc;
        if (w <= 0) return;

        int tab_h = wysokosc_paska_kart_;
        int x = 2 - offset_paska_;

        int laczna_szerokosc = 2;
        for (int i = 0; i < liczba_kart_; ++i) {
            int tw = CzcionkaUI::zmierz_tekst(strony_[i].tytul(), skala_dpi_) + 24;
            if (strony_[i].czy_zamykalna()) tw += 16;
            if (tw < 50) tw = 50;

            strony_[i].prostokat_naglowka_ = Prostokat(x, 2, tw, tab_h - 2);
            x += tw + 2;
            laczna_szerokosc += tw + 2;
        }

        czy_przewijany_pasek_ = (laczna_szerokosc > w);
        if (czy_przewijany_pasek_) {
            przycisk_lewo_ = Prostokat(w - 36, 2, 16, tab_h - 4);
            przycisk_prawo_ = Prostokat(w - 18, 2, 16, tab_h - 4);
            maks_offset_paska_ = laczna_szerokosc - (w - 38);
            if (maks_offset_paska_ < 0) maks_offset_paska_ = 0;
        } else {
            offset_paska_ = 0;
            maks_offset_paska_ = 0;
        }
    }

    void rozmiesc_strone(Kontrolka* k) {
        if (!k) return;
        Prostokat r = prostokat_obszaru_strony();
        k->rozmiesc(Prostokat(0, 0, r.szerokosc, r.wysokosc));
    }

    void rozmiesc_wszystkie_strony() {
        if (!obszar_stron_) return;
        Prostokat r = prostokat_obszaru_strony();
        obszar_stron_->rozmiesc(r);

        for (int i = 0; i < liczba_kart_; ++i) {
            Kontrolka* k = strony_[i].zawartosc();
            if (k) {
                k->ustaw_widoczna(i == aktywna_karta_);
                rozmiesc_strone(k);
            }
        }
    }

    void dopasuj_przewiniecie_do_aktywnej() {
        if (aktywna_karta_ < 0 || aktywna_karta_ >= liczba_kart_ || !czy_przewijany_pasek_) return;
        Prostokat nr = strony_[aktywna_karta_].prostokat_naglowka_;
        int avail_w = obszar_.szerokosc - 38;
        if (nr.x < 2) {
            przewin_pasek(nr.x - 2);
        } else if (nr.prawy() > avail_w) {
            przewin_pasek(nr.prawy() - avail_w);
        }
    }

    void przewin_pasek(int delta) {
        int nowy = offset_paska_ + delta;
        if (nowy < 0) nowy = 0;
        if (nowy > maks_offset_paska_) nowy = maks_offset_paska_;
        if (nowy == offset_paska_) return;
        offset_paska_ = nowy;
        przelicz_naglowki();
        uniewaznij_obszar(Prostokat(0, 0, obszar_.szerokosc,
                                    wysokosc_paska_kart_));
    }

    void przelacz_na_kolejna() {
        for (int step = 1; step < liczba_kart_; ++step) {
            int next = (aktywna_karta_ + step) % liczba_kart_;
            if (strony_[next].czy_wlaczona()) {
                ustaw_aktywna_karte(next);
                return;
            }
        }
    }

    void przelacz_na_poprzednia() {
        for (int step = 1; step < liczba_kart_; ++step) {
            int prev = (aktywna_karta_ - step + liczba_kart_) % liczba_kart_;
            if (strony_[prev].czy_wlaczona()) {
                ustaw_aktywna_karte(prev);
                return;
            }
        }
    }

    StronaKarty strony_[MAKS_KART];
    int liczba_kart_{0};
    int aktywna_karta_{-1};
    int najechana_karta_{-1};
    int wysokosc_paska_kart_{28};
    int offset_paska_{0};
    int maks_offset_paska_{0};
    bool czy_przewijany_pasek_{false};

    Prostokat przycisk_lewo_;
    Prostokat przycisk_prawo_;
    Panel* obszar_stron_{nullptr};

    FunkcjaKarty cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
    FunkcjaKarty cb_zamkniecie_{nullptr};
    void* dane_zamkniecia_{nullptr};
};

} // namespace BursztynUI
