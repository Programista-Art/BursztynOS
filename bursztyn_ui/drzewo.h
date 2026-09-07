#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

struct UchwytWezlaDrzewa {
    uint32_t indeks{0};
    uint32_t generacja{0};

    bool czy_poprawny() const { return generacja != 0; }
    bool operator==(const UchwytWezlaDrzewa& o) const {
        return indeks == o.indeks && generacja == o.generacja;
    }
    bool operator!=(const UchwytWezlaDrzewa& o) const {
        return !(*this == o);
    }
};

class Drzewo : public Kontrolka {
public:
    static constexpr size_t MAKS_WEZLOW = 4096;
    static constexpr size_t MAKS_DZIECI_WEZLA = 64;
    using FunkcjaDrzewa = void (*)(Drzewo* nadawca, UchwytWezlaDrzewa wezel, void* dane_uzytkownika);

    Drzewo() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = Kolory::WejscieTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = true;
        preferowany_rozmiar_ = Rozmiar(220, 260);

        pasek_pionowy_ = new PasekPrzewijania();
        pasek_pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        pasek_pionowy_->pokaz_strzalki(true);
        dodaj_kontrolke(pasek_pionowy_);

        pasek_pionowy_->model().przy_zmianie([](ModelPrzewijania*, int64_t, void* dane) {
            Drzewo* d = static_cast<Drzewo*>(dane);
            if (d) d->uniewaznij();
        }, this);
    }

    UchwytWezlaDrzewa dodaj_korzen(const char* tekst, uint64_t tag = 0) {
        return wstaw_wezel_wewn(UchwytWezlaDrzewa{}, MAKS_WEZLOW, tekst, tag);
    }

    UchwytWezlaDrzewa dodaj_dziecko(UchwytWezlaDrzewa rodzic, const char* tekst, uint64_t tag = 0) {
        return wstaw_wezel_wewn(rodzic, MAKS_WEZLOW, tekst, tag);
    }

    UchwytWezlaDrzewa wstaw_dziecko(UchwytWezlaDrzewa rodzic, size_t indeks, const char* tekst, uint64_t tag = 0) {
        return wstaw_wezel_wewn(rodzic, indeks, tekst, tag);
    }

    bool usun_wezel(UchwytWezlaDrzewa wezel) {
        if (!czy_uchwyt_poprawny(wezel)) return false;
        usun_poddrzewo_rekurencyjnie(wezel.indeks);
        przebuduj_widoczne();
        uniewaznij();
        return true;
    }

    void wyczysc() {
        for (size_t i = 0; i < MAKS_WEZLOW; ++i) {
            if (wezly_[i].uzyty) {
                wezly_[i].uzyty = false;
                ++wezly_[i].generacja;
            }
        }
        pierwszy_korzen_ = -1;
        ostatni_korzen_ = -1;
        kolejny_wolny_ = 0;
        liczba_korzeni_ = 0;
        liczba_uzytych_ = 0;
        wybrany_ = UchwytWezlaDrzewa{};
        przebuduj_widoczne();
        uniewaznij();
    }

    void rozpocznij_aktualizacje() { ++blokada_aktualizacji_; }
    void zakoncz_aktualizacje() {
        if (blokada_aktualizacji_ > 0) {
            --blokada_aktualizacji_;
            if (blokada_aktualizacji_ == 0) {
                przebuduj_widoczne();
                uniewaznij();
            }
        }
    }

    size_t liczba_wezlow() const { return liczba_uzytych_; }
    uint64_t liczba_przebudowan_widocznych() const {
        return liczba_przebudowan_widocznych_;
    }

    bool czy_uchwyt_poprawny(UchwytWezlaDrzewa w) const {
        return w.czy_poprawny() && w.indeks < MAKS_WEZLOW &&
               wezly_[w.indeks].uzyty && !wezly_[w.indeks].emerytowany &&
               wezly_[w.indeks].generacja == w.generacja;
    }

    void _ustaw_generacje_slotu_dla_testu(size_t idx, uint32_t gen) {
        if (idx < MAKS_WEZLOW) {
            wezly_[idx].generacja = gen;
        }
    }

    bool _czy_slot_emerytowany_dla_testu(size_t idx) const {
        return idx < MAKS_WEZLOW && wezly_[idx].emerytowany;
    }

    UchwytWezlaDrzewa rodzic(UchwytWezlaDrzewa wezel) const {
        if (!czy_uchwyt_poprawny(wezel)) return UchwytWezlaDrzewa{};
        int p = wezly_[wezel.indeks].rodzic;
        if (p < 0 || !wezly_[p].uzyty) return UchwytWezlaDrzewa{};
        return UchwytWezlaDrzewa{static_cast<uint32_t>(p), wezly_[p].generacja};
    }

    size_t liczba_dzieci(UchwytWezlaDrzewa wezel) const {
        if (!czy_uchwyt_poprawny(wezel)) return 0;
        return wezly_[wezel.indeks].liczba_dzieci;
    }

    UchwytWezlaDrzewa dziecko(UchwytWezlaDrzewa wezel, size_t i) const {
        if (!czy_uchwyt_poprawny(wezel) || i >= wezly_[wezel.indeks].liczba_dzieci) {
            return UchwytWezlaDrzewa{};
        }
        int ch = wezly_[wezel.indeks].pierwsze_dziecko;
        for (size_t step = 0; step < i && ch >= 0; ++step) {
            ch = wezly_[ch].nastepne_rodzenstwo;
        }
        if (ch < 0 || !wezly_[ch].uzyty) return UchwytWezlaDrzewa{};
        return UchwytWezlaDrzewa{static_cast<uint32_t>(ch), wezly_[ch].generacja};
    }

    void ustaw_tekst(UchwytWezlaDrzewa wezel, const char* t) {
        if (!czy_uchwyt_poprawny(wezel)) return;
        kopiuj_str(wezly_[wezel.indeks].tekst, sizeof(wezly_[wezel.indeks].tekst), t);
        uniewaznij();
    }
    const char* tekst(UchwytWezlaDrzewa wezel) const {
        if (!czy_uchwyt_poprawny(wezel)) return "";
        return wezly_[wezel.indeks].tekst;
    }

    void ustaw_rozwiniety(UchwytWezlaDrzewa wezel, bool r) {
        if (!czy_uchwyt_poprawny(wezel)) return;
        if (wezly_[wezel.indeks].rozwiniety != r) {
            wezly_[wezel.indeks].rozwiniety = r;
            przebuduj_widoczne();
            uniewaznij();
            if (r && cb_rozwiniecie_) cb_rozwiniecie_(this, wezel, dane_rozwiniecia_);
            else if (!r && cb_zwiniecie_) cb_zwiniecie_(this, wezel, dane_zwiniecia_);
        }
    }
    bool czy_rozwiniety(UchwytWezlaDrzewa wezel) const {
        if (!czy_uchwyt_poprawny(wezel)) return false;
        return wezly_[wezel.indeks].rozwiniety;
    }

    void przelacz_rozwiniecie(UchwytWezlaDrzewa wezel) {
        if (czy_uchwyt_poprawny(wezel)) {
            ustaw_rozwiniety(wezel, !wezly_[wezel.indeks].rozwiniety);
        }
    }

    void ustaw_wybrany(UchwytWezlaDrzewa wezel) {
        if (wezel.czy_poprawny() && !czy_uchwyt_poprawny(wezel)) return;
        if (wybrany_ != wezel) {
            wybrany_ = wezel;
            if (wybrany_.czy_poprawny()) {
                rozwin_rodzicow(wybrany_);
                przewin_do_wezla(wybrany_);
            }
            uniewaznij();
            if (cb_zmiana_wyboru_) cb_zmiana_wyboru_(this, wybrany_, dane_zmiany_wyboru_);
        }
    }
    UchwytWezlaDrzewa wybrany_wezel() const { return wybrany_; }

    void ustaw_tag(UchwytWezlaDrzewa wezel, uint64_t t) {
        if (czy_uchwyt_poprawny(wezel)) wezly_[wezel.indeks].tag = t;
    }
    uint64_t tag(UchwytWezlaDrzewa wezel) const {
        if (!czy_uchwyt_poprawny(wezel)) return 0;
        return wezly_[wezel.indeks].tag;
    }

    void przewin_do_wezla(UchwytWezlaDrzewa wezel) {
        if (!czy_uchwyt_poprawny(wezel)) return;
        int poz = pozycja_widocznego(wezel);
        if (poz < 0) return;

        int row_h = wysokosc_wiersza();
        int h = obszar_ekranu_.wysokosc - 2;
        int visible_rows = (h > 0 && row_h > 0) ? h / row_h : 1;
        int first = static_cast<int>(pasek_pionowy_->wartosc());

        if (poz < first) {
            pasek_pionowy_->ustaw_wartosc(poz);
        } else if (poz >= first + visible_rows) {
            pasek_pionowy_->ustaw_wartosc(poz - visible_rows + 1);
        }
    }
    void przewin_do_wybranego() {
        if (wybrany_.czy_poprawny()) przewin_do_wezla(wybrany_);
    }

    void przy_zmianie_wyboru(FunkcjaDrzewa cb, void* dane = nullptr) {
        cb_zmiana_wyboru_ = cb; dane_zmiany_wyboru_ = dane;
    }
    void przy_rozwinieciu(FunkcjaDrzewa cb, void* dane = nullptr) {
        cb_rozwiniecie_ = cb; dane_rozwiniecia_ = dane;
    }
    void przy_zwinieciu(FunkcjaDrzewa cb, void* dane = nullptr) {
        cb_zwiniecie_ = cb; dane_zwiniecia_ = dane;
    }
    void przy_aktywacji(FunkcjaDrzewa cb, void* dane = nullptr) {
        cb_aktywacja_ = cb; dane_aktywacji_ = dane;
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        przelicz_uklad();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        int row_h = wysokosc_wiersza();
        int first = static_cast<int>(pasek_pionowy_->wartosc());
        int row_idx = first + (ly - 1) / row_h;

        if (row_idx >= 0 && row_idx < static_cast<int>(liczba_widocznych_)) {
            UchwytWezlaDrzewa w = widoczne_[row_idx].uchwyt;
            int depth = widoczne_[row_idx].glebokosc;
            int arrow_x = 4 + depth * wciecie_ - offset_x_;

            if (wezly_[w.indeks].liczba_dzieci > 0 && lx >= arrow_x && lx <= arrow_x + 14) {
                przelacz_rozwiniecie(w);
                e.obsluzone = true;
                return;
            }

            ustaw_wybrany(w);
            e.obsluzone = true;
        }
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0) return;
        pasek_pionowy_->przewin_o(e.delta_y > 0 ? -3 : 3);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if (kod == 13 || (usb && baza == 40U)) { // Enter
            if (wybrany_.czy_poprawny() && cb_aktywacja_) {
                cb_aktywacja_(this, wybrany_, dane_aktywacji_);
                e.obsluzone = true;
                return;
            }
        } else if (kod == 32 || (usb && baza == 44U)) { // Space
            if (wybrany_.czy_poprawny() && wezly_[wybrany_.indeks].liczba_dzieci > 0) {
                przelacz_rozwiniecie(wybrany_);
                e.obsluzone = true;
                return;
            }
        } else if ((usb && baza == 82U) || (e0 && baza == 0x48U)) { // Up
            nawiguj_wzglednie(-1);
            e.obsluzone = true;
        } else if ((usb && baza == 81U) || (e0 && baza == 0x50U)) { // Down
            nawiguj_wzglednie(1);
            e.obsluzone = true;
        } else if ((usb && baza == 80U) || (e0 && baza == 0x4BU)) { // Left
            if (wybrany_.czy_poprawny()) {
                if (wezly_[wybrany_.indeks].rozwiniety) {
                    ustaw_rozwiniety(wybrany_, false);
                } else {
                    UchwytWezlaDrzewa p = rodzic(wybrany_);
                    if (p.czy_poprawny()) ustaw_wybrany(p);
                }
                e.obsluzone = true;
            }
        } else if ((usb && baza == 79U) || (e0 && baza == 0x4DU)) { // Right
            if (wybrany_.czy_poprawny()) {
                if (wezly_[wybrany_.indeks].liczba_dzieci > 0) {
                    if (!wezly_[wybrany_.indeks].rozwiniety) {
                        ustaw_rozwiniety(wybrany_, true);
                    } else {
                        nawiguj_wzglednie(1);
                    }
                }
                e.obsluzone = true;
            }
        } else if ((usb && baza == 75U) || (e0 && baza == 0x49U)) { // Page Up
            nawiguj_wzglednie(-widoczne_wiersze());
            e.obsluzone = true;
        } else if ((usb && baza == 78U) || (e0 && baza == 0x51U)) { // Page Down
            nawiguj_wzglednie(widoczne_wiersze());
            e.obsluzone = true;
        } else if ((usb && baza == 74U) || (e0 && baza == 0x47U)) { // Home
            if (liczba_widocznych_ > 0) {
                ustaw_wybrany(widoczne_[0].uchwyt);
                e.obsluzone = true;
            }
        } else if ((usb && baza == 77U) || (e0 && baza == 0x4FU)) { // End
            if (liczba_widocznych_ > 0) {
                ustaw_wybrany(widoczne_[liczba_widocznych_ - 1].uchwyt);
                e.obsluzone = true;
            }
        }
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());

        int w = obszar_.szerokosc;
        int h = obszar_.wysokosc;
        if (w <= 2 || h <= 2) return;

        Prostokat client_rect(1, 1,
                              w - 2 - (pasek_pionowy_->czy_widoczna() ? 15 : 0), h - 2);
        ctx.dodaj_przyciecie(client_rect);

        int row_h = wysokosc_wiersza();
        int first_row = static_cast<int>(pasek_pionowy_->wartosc());
        int visible_count = (client_rect.wysokosc + row_h - 1) / row_h + 1;

        for (int i = 0; i < visible_count; ++i) {
            int row_idx = first_row + i;
            if (row_idx >= static_cast<int>(liczba_widocznych_)) break;

            UchwytWezlaDrzewa w = widoczne_[row_idx].uchwyt;
            int depth = widoczne_[row_idx].glebokosc;
            int y = client_rect.y + i * row_h;

            bool zaznaczony = (w == wybrany_);

            if (zaznaczony) {
                ctx.rysuj_prostokat(Prostokat(client_rect.x, y, client_rect.szerokosc, row_h),
                                    czy_ma_fokus_ ? Kolory::ZaznaczenieTlo : Kolory::PowierzchniaAktywna);
            }

            int arrow_x = client_rect.x + 4 + depth * wciecie_ - offset_x_;
            int arrow_y = y + (row_h - 10) / 2;
            if (wezly_[w.indeks].liczba_dzieci > 0) {
                uint32_t strzalka_kolor = zaznaczony ? Kolory::TekstBialy : Kolory::AkcentZloty;
                if (wezly_[w.indeks].rozwiniety) {
                    ctx.rysuj_linie_pozioma(arrow_x + 1, arrow_y + 2, 7, strzalka_kolor);
                    ctx.rysuj_linie_pozioma(arrow_x + 2, arrow_y + 3, 5, strzalka_kolor);
                    ctx.rysuj_linie_pozioma(arrow_x + 3, arrow_y + 4, 3, strzalka_kolor);
                    ctx.rysuj_prostokat(Prostokat(arrow_x + 4, arrow_y + 5, 1, 1), strzalka_kolor);
                } else {
                    ctx.rysuj_linie_pionowa(arrow_x + 2, arrow_y + 1, 7, strzalka_kolor);
                    ctx.rysuj_linie_pionowa(arrow_x + 3, arrow_y + 2, 5, strzalka_kolor);
                    ctx.rysuj_linie_pionowa(arrow_x + 4, arrow_y + 3, 3, strzalka_kolor);
                    ctx.rysuj_prostokat(Prostokat(arrow_x + 5, arrow_y + 4, 1, 1), strzalka_kolor);
                }
            }

            int text_x = arrow_x + 16;
            int text_y = y + (row_h - 16) / 2;
            uint32_t text_color = zaznaczony ? Kolory::TekstBialy : (wezly_[w.indeks].liczba_dzieci > 0 ? Kolory::AkcentZloty : Kolory::TekstBialy);
            ctx.rysuj_tekst(Punkt(text_x, text_y), wezly_[w.indeks].tekst, text_color, skala_dpi_);
        }

        ctx.zdejmij_przyciecie();
    }

private:
    struct Wezel {
        char tekst[64]{};
        uint64_t tag{0};
        uint32_t generacja{1};
        int rodzic{-1};
        int pierwsze_dziecko{-1};
        int ostatnie_dziecko{-1};
        int nastepne_rodzenstwo{-1};
        int poprzednie_rodzenstwo{-1};
        size_t liczba_dzieci{0};
        bool uzyty{false};
        bool rozwiniety{false};
        bool emerytowany{false};
    };

    struct WidocznyWpis {
        UchwytWezlaDrzewa uchwyt;
        int glebokosc{0};
    };

    Wezel* wezel(UchwytWezlaDrzewa w) {
        if (!czy_uchwyt_poprawny(w)) return nullptr;
        return &wezly_[w.indeks];
    }
    const Wezel* wezel(UchwytWezlaDrzewa w) const {
        if (!czy_uchwyt_poprawny(w)) return nullptr;
        return &wezly_[w.indeks];
    }

    int alokuj_wezel() {
        if (liczba_uzytych_ >= MAKS_WEZLOW) return -1;
        for (size_t i = 0; i < MAKS_WEZLOW; ++i) {
            if (!wezly_[i].uzyty && !wezly_[i].emerytowany) {
                uint32_t gen = wezly_[i].generacja;
                if (gen == 0) gen = 1;
                wezly_[i] = Wezel();
                wezly_[i].generacja = gen;
                wezly_[i].uzyty = true;
                wezly_[i].emerytowany = false;
                ++liczba_uzytych_;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void zwolnij_wezel_slot(size_t idx) {
        if (idx >= MAKS_WEZLOW || !wezly_[idx].uzyty) return;
        wezly_[idx].uzyty = false;
        if (wezly_[idx].generacja >= 0xFFFFFFFFU) {
            wezly_[idx].emerytowany = true;
        } else {
            ++wezly_[idx].generacja;
        }
        --liczba_uzytych_;
    }

    UchwytWezlaDrzewa wstaw_wezel_wewn(UchwytWezlaDrzewa rodzic, size_t indeks_w_dzieciach, const char* tekst, uint64_t tag) {
        if (rodzic.czy_poprawny() && !czy_uchwyt_poprawny(rodzic)) {
            return UchwytWezlaDrzewa{};
        }

        int idx = alokuj_wezel();
        if (idx < 0) return UchwytWezlaDrzewa{};

        kopiuj_str(wezly_[idx].tekst, sizeof(wezly_[idx].tekst), tekst);
        wezly_[idx].tag = tag;

        if (!rodzic.czy_poprawny()) {
            wezly_[idx].rodzic = -1;
            if (indeks_w_dzieciach >= liczba_korzeni_ || pierwszy_korzen_ < 0) {
                if (ostatni_korzen_ >= 0) {
                    wezly_[ostatni_korzen_].nastepne_rodzenstwo = idx;
                    wezly_[idx].poprzednie_rodzenstwo = ostatni_korzen_;
                    ostatni_korzen_ = idx;
                } else {
                    pierwszy_korzen_ = ostatni_korzen_ = idx;
                }
            } else {
                int cur = pierwszy_korzen_;
                for (size_t i = 0; i < indeks_w_dzieciach && cur >= 0; ++i) {
                    cur = wezly_[cur].nastepne_rodzenstwo;
                }
                if (cur >= 0) {
                    int prev = wezly_[cur].poprzednie_rodzenstwo;
                    wezly_[idx].nastepne_rodzenstwo = cur;
                    wezly_[idx].poprzednie_rodzenstwo = prev;
                    wezly_[cur].poprzednie_rodzenstwo = idx;
                    if (prev >= 0) wezly_[prev].nastepne_rodzenstwo = idx;
                    else pierwszy_korzen_ = idx;
                }
            }
            ++liczba_korzeni_;
        } else {
            int r_idx = static_cast<int>(rodzic.indeks);
            wezly_[idx].rodzic = r_idx;
            Wezel& rw = wezly_[r_idx];
            if (indeks_w_dzieciach >= rw.liczba_dzieci || rw.pierwsze_dziecko < 0) {
                if (rw.ostatnie_dziecko >= 0) {
                    wezly_[rw.ostatnie_dziecko].nastepne_rodzenstwo = idx;
                    wezly_[idx].poprzednie_rodzenstwo = rw.ostatnie_dziecko;
                    rw.ostatnie_dziecko = idx;
                } else {
                    rw.pierwsze_dziecko = rw.ostatnie_dziecko = idx;
                }
            } else {
                int cur = rw.pierwsze_dziecko;
                for (size_t i = 0; i < indeks_w_dzieciach && cur >= 0; ++i) {
                    cur = wezly_[cur].nastepne_rodzenstwo;
                }
                if (cur >= 0) {
                    int prev = wezly_[cur].poprzednie_rodzenstwo;
                    wezly_[idx].nastepne_rodzenstwo = cur;
                    wezly_[idx].poprzednie_rodzenstwo = prev;
                    wezly_[cur].poprzednie_rodzenstwo = idx;
                    if (prev >= 0) wezly_[prev].nastepne_rodzenstwo = idx;
                    else rw.pierwsze_dziecko = idx;
                }
            }
            ++rw.liczba_dzieci;
        }

        if (blokada_aktualizacji_ == 0) {
            przebuduj_widoczne();
            uniewaznij();
        }
        return UchwytWezlaDrzewa{static_cast<uint32_t>(idx), wezly_[idx].generacja};
    }

    void usun_poddrzewo_rekurencyjnie(size_t idx) {
        if (idx >= MAKS_WEZLOW || !wezly_[idx].uzyty) return;

        int ch = wezly_[idx].pierwsze_dziecko;
        while (ch >= 0) {
            int next_ch = wezly_[ch].nastepne_rodzenstwo;
            usun_poddrzewo_rekurencyjnie(static_cast<size_t>(ch));
            ch = next_ch;
        }

        int p = wezly_[idx].rodzic;
        int prev = wezly_[idx].poprzednie_rodzenstwo;
        int next = wezly_[idx].nastepne_rodzenstwo;

        if (prev >= 0) wezly_[prev].nastepne_rodzenstwo = next;
        if (next >= 0) wezly_[next].poprzednie_rodzenstwo = prev;

        if (p >= 0 && wezly_[p].uzyty) {
            Wezel& pw = wezly_[p];
            if (pw.pierwsze_dziecko == static_cast<int>(idx)) pw.pierwsze_dziecko = next;
            if (pw.ostatnie_dziecko == static_cast<int>(idx)) pw.ostatnie_dziecko = prev;
            if (pw.liczba_dzieci > 0) --pw.liczba_dzieci;
        } else {
            if (pierwszy_korzen_ == static_cast<int>(idx)) pierwszy_korzen_ = next;
            if (ostatni_korzen_ == static_cast<int>(idx)) ostatni_korzen_ = prev;
            if (liczba_korzeni_ > 0) --liczba_korzeni_;
        }

        if (wybrany_.indeks == idx) {
            wybrany_ = UchwytWezlaDrzewa{};
        }

        zwolnij_wezel_slot(idx);
    }

    void przebuduj_widoczne() {
        ++liczba_przebudowan_widocznych_;
        liczba_widocznych_ = 0;
        int cur = pierwszy_korzen_;
        while (cur >= 0) {
            dodaj_widoczne_rekurencyjnie(cur, 0);
            cur = wezly_[cur].nastepne_rodzenstwo;
        }
        przelicz_uklad();
    }

    void dodaj_widoczne_rekurencyjnie(int idx, int glebokosc) {
        if (idx < 0 || idx >= static_cast<int>(MAKS_WEZLOW) || !wezly_[idx].uzyty) return;
        if (liczba_widocznych_ < MAKS_WEZLOW) {
            widoczne_[liczba_widocznych_++] = WidocznyWpis{
                UchwytWezlaDrzewa{static_cast<uint32_t>(idx), wezly_[idx].generacja},
                glebokosc
            };
        }
        if (wezly_[idx].rozwiniety) {
            int ch = wezly_[idx].pierwsze_dziecko;
            while (ch >= 0) {
                dodaj_widoczne_rekurencyjnie(ch, glebokosc + 1);
                ch = wezly_[ch].nastepne_rodzenstwo;
            }
        }
    }

    int pozycja_widocznego(UchwytWezlaDrzewa w) const {
        for (size_t i = 0; i < liczba_widocznych_; ++i) {
            if (widoczne_[i].uchwyt == w) return static_cast<int>(i);
        }
        return -1;
    }

    void rozwin_rodzicow(UchwytWezlaDrzewa w) {
        UchwytWezlaDrzewa p = rodzic(w);
        bool zmieniono = false;
        while (p.czy_poprawny()) {
            if (!wezly_[p.indeks].rozwiniety) {
                wezly_[p.indeks].rozwiniety = true;
                zmieniono = true;
            }
            p = rodzic(p);
        }
        if (zmieniono) przebuduj_widoczne();
    }

    void nawiguj_wzglednie(int delta) {
        if (liczba_widocznych_ == 0) return;
        int cur = pozycja_widocznego(wybrany_);
        int nowa = (cur < 0) ? 0 : cur + delta;
        if (nowa < 0) nowa = 0;
        if (nowa >= static_cast<int>(liczba_widocznych_)) {
            nowa = static_cast<int>(liczba_widocznych_) - 1;
        }
        ustaw_wybrany(widoczne_[nowa].uchwyt);
    }

    void przelicz_uklad() {
        int w = obszar_.szerokosc;
        int h = obszar_.wysokosc;
        if (w <= 2 || h <= 2) return;

        int row_h = wysokosc_wiersza();
        int visible_rows = (h - 2) / row_h;
        if (visible_rows < 1) visible_rows = 1;

        bool scroll_needed = (liczba_widocznych_ > static_cast<size_t>(visible_rows));
        pasek_pionowy_->ustaw_widoczna(scroll_needed);

        if (scroll_needed) {
            pasek_pionowy_->rozmiesc(Prostokat(w - 15, 1, 14, h - 2));
            pasek_pionowy_->ustaw_zakres(0, liczba_widocznych_ > static_cast<size_t>(visible_rows) ? liczba_widocznych_ - visible_rows : 0);
            pasek_pionowy_->ustaw_rozmiar_strony(visible_rows);
        }
    }

    int wysokosc_wiersza() const { return 20 * (skala_dpi_); }
    int widoczne_wiersze() const {
        int h = obszar_.wysokosc - 2;
        int rh = wysokosc_wiersza();
        return (h > 0 && rh > 0) ? (h / rh) : 1;
    }

    static void kopiuj_str(char* cel, size_t max_cel, const char* zr) {
        if (!cel || max_cel == 0) return;
        if (!zr) { cel[0] = '\0'; return; }
        size_t i = 0;
        while (zr[i] != '\0' && i + 1 < max_cel) {
            cel[i] = zr[i];
            ++i;
        }
        cel[i] = '\0';
    }

    Wezel wezly_[MAKS_WEZLOW];
    int pierwszy_korzen_{-1};
    int ostatni_korzen_{-1};
    size_t liczba_korzeni_{0};
    size_t liczba_uzytych_{0};
    size_t kolejny_wolny_{0};
    int blokada_aktualizacji_{0};

    WidocznyWpis widoczne_[MAKS_WEZLOW];
    size_t liczba_widocznych_{0};
    uint64_t liczba_przebudowan_widocznych_{0};

    UchwytWezlaDrzewa wybrany_{};
    PasekPrzewijania* pasek_pionowy_{nullptr};

    int wciecie_{16};
    int offset_x_{0};

    FunkcjaDrzewa cb_zmiana_wyboru_{nullptr};
    void* dane_zmiany_wyboru_{nullptr};
    FunkcjaDrzewa cb_rozwiniecie_{nullptr};
    void* dane_rozwiniecia_{nullptr};
    FunkcjaDrzewa cb_zwiniecie_{nullptr};
    void* dane_zwiniecia_{nullptr};
    FunkcjaDrzewa cb_aktywacja_{nullptr};
    void* dane_aktywacji_{nullptr};
};

} // namespace BursztynUI
