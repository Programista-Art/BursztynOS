#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

class Tabela : public Kontrolka {
public:
    static constexpr size_t MAKS_KOLUMN = 16;
    static constexpr size_t MAKS_WIERSZY = 4096;

    struct Kolumna {
        char nazwa[64]{};
        int szerokosc{100};
        int minimalna_szerokosc{30};
        WyrownaniePoziome wyrownanie{WyrownaniePoziome::Poczatek};
    };

    struct Wiersz {
        char** komorki{nullptr};
        size_t liczba_komorek{0};
        uint64_t znacznik{0};
        bool zaznaczony{false};
    };

    using FunkcjaTabeli = void (*)(Tabela* nadawca, int wiersz, void* dane_uzytkownika);

    Tabela() {
        zaktualizuj_motyw();
        wypelnienie_ = Marginesy(1, 1, 1, 1);
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        preferowany_rozmiar_ = Rozmiar(320, 200);

        pionowy_ = new PasekPrzewijania();
        poziomy_ = new PasekPrzewijania();
        pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        poziomy_->ustaw_orientacje(Orientacja::Pozioma);
        pionowy_->pokaz_strzalki(true);
        poziomy_->pokaz_strzalki(true);
        pionowy_->ustaw_maly_krok(wysokosc_wiersza_);
        poziomy_->ustaw_maly_krok(20);
        dodaj_kontrolke(pionowy_);
        dodaj_kontrolke(poziomy_);

        pionowy_->przy_przewijaniu([](PasekPrzewijania*, int64_t, void* dane) {
            Tabela* t = static_cast<Tabela*>(dane);
            if (t) t->uniewaznij_obszar(t->prostokat_danych());
        }, this);

        poziomy_->przy_przewijaniu([](PasekPrzewijania*, int64_t, void* dane) {
            Tabela* t = static_cast<Tabela*>(dane);
            if (t) t->uniewaznij_obszar(t->prostokat_widoku());
        }, this);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = 1;
        if (pionowy_) pionowy_->zaktualizuj_motyw();
        if (poziomy_) poziomy_->zaktualizuj_motyw();
        uniewaznij();
    }

    ~Tabela() override {
        wyczysc_wiersze();
    }

    // =========================================================================
    // Zarządzanie kolumnami
    // =========================================================================

    bool dodaj_kolumne(const char* nazwa, int szerokosc = 100,
                       WyrownaniePoziome wyrownanie = WyrownaniePoziome::Poczatek) {
        if (liczba_kolumn_ >= MAKS_KOLUMN) return false;
        Kolumna& k = kolumny_[liczba_kolumn_++];
        if (!nazwa) nazwa = "";
        size_t i = 0;
        for (; i < sizeof(k.nazwa) - 1 && nazwa[i] != '\0'; ++i) {
            k.nazwa[i] = nazwa[i];
        }
        k.nazwa[i] = '\0';
        k.szerokosc = szerokosc < 30 ? 30 : szerokosc;
        k.minimalna_szerokosc = 30;
        k.wyrownanie = wyrownanie;

        przelicz_calkowita_szerokosc();
        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    size_t liczba_kolumn() const { return liczba_kolumn_; }

    const Kolumna* kolumna(size_t indeks) const {
        return indeks < liczba_kolumn_ ? &kolumny_[indeks] : nullptr;
    }

    void ustaw_szerokosc_kolumny(size_t indeks, int szerokosc) {
        if (indeks < liczba_kolumn_) {
            kolumny_[indeks].szerokosc = szerokosc < kolumny_[indeks].minimalna_szerokosc
                                       ? kolumny_[indeks].minimalna_szerokosc
                                       : szerokosc;
            przelicz_calkowita_szerokosc();
            aktualizuj_przewijanie();
            uniewaznij();
        }
    }

    // =========================================================================
    // Zarządzanie wierszami
    // =========================================================================

    bool dodaj_wiersz(const char* const* wartosci_komorek, size_t ile_komorek, uint64_t znacznik = 0) {
        if (liczba_wierszy_ >= MAKS_WIERSZY) return false;
        Wiersz& w = wiersze_[liczba_wierszy_++];
        w.liczba_komorek = liczba_kolumn_ > 0 ? liczba_kolumn_ : ile_komorek;
        w.znacznik = znacznik;
        w.zaznaczony = false;
        w.komorki = new char*[w.liczba_komorek];

        for (size_t c = 0; c < w.liczba_komorek; ++c) {
            const char* val = (c < ile_komorek && wartosci_komorek && wartosci_komorek[c])
                            ? wartosci_komorek[c] : "";
            w.komorki[c] = kopiuj_tekst(val);
        }

        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    bool dodaj_wiersz(const char* k0, const char* k1 = nullptr, const char* k2 = nullptr,
                      const char* k3 = nullptr, const char* k4 = nullptr) {
        const char* tab[5] = {k0, k1, k2, k3, k4};
        size_t ile = 1;
        if (k1) ile = 2;
        if (k2) ile = 3;
        if (k3) ile = 4;
        if (k4) ile = 5;
        return dodaj_wiersz(tab, ile, 0);
    }

    bool usun_wiersz(size_t indeks) {
        if (indeks >= liczba_wierszy_) return false;
        zwolnij_komorki(wiersze_[indeks]);

        for (size_t i = indeks; i + 1 < liczba_wierszy_; ++i) {
            wiersze_[i] = wiersze_[i + 1];
        }
        --liczba_wierszy_;
        wiersze_[liczba_wierszy_] = Wiersz{};

        if (zaznaczony_wiersz_ == static_cast<int>(indeks)) {
            zaznaczony_wiersz_ = -1;
        } else if (zaznaczony_wiersz_ > static_cast<int>(indeks)) {
            --zaznaczony_wiersz_;
        }

        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    void wyczysc_wiersze() {
        for (size_t i = 0; i < liczba_wierszy_; ++i) {
            zwolnij_komorki(wiersze_[i]);
        }
        liczba_wierszy_ = 0;
        zaznaczony_wiersz_ = -1;
        najechany_wiersz_ = -1;
        aktualizuj_przewijanie();
        uniewaznij();
    }

    size_t liczba_wierszy() const { return liczba_wierszy_; }

    PasekPrzewijania& pasek_pionowy() { return *pionowy_; }
    const PasekPrzewijania& pasek_pionowy() const { return *pionowy_; }
    PasekPrzewijania& pasek_poziomy() { return *poziomy_; }
    const PasekPrzewijania& pasek_poziomy() const { return *poziomy_; }
    size_t liczba_ostatnio_narysowanych_wierszy() const {
        return ostatnio_narysowane_wiersze_;
    }
    int najechany_wiersz() const { return najechany_wiersz_; }
    Prostokat prostokat_widoku() const {
        return Prostokat(1, 1, viewport_w(), viewport_h());
    }
    Prostokat prostokat_danych() const {
        const int naglowek = wysokosc_naglowka_ * skala_dpi_;
        const int h = viewport_h() - naglowek - 1;
        return h > 0 ? Prostokat(1, naglowek + 2, viewport_w(), h)
                     : Prostokat();
    }

    const char* komorka(size_t wiersz, size_t kolumna) const {
        if (wiersz >= liczba_wierszy_) return "";
        const Wiersz& w = wiersze_[wiersz];
        if (kolumna >= w.liczba_komorek || !w.komorki[kolumna]) return "";
        return w.komorki[kolumna];
    }

    bool ustaw_komorke(size_t wiersz, size_t kolumna, const char* tekst) {
        if (wiersz >= liczba_wierszy_) return false;
        Wiersz& w = wiersze_[wiersz];
        if (kolumna >= w.liczba_komorek) return false;

        if (w.komorki[kolumna]) {
            delete[] w.komorki[kolumna];
        }
        w.komorki[kolumna] = kopiuj_tekst(tekst ? tekst : "");
        uniewaznij();
        return true;
    }

    uint64_t znacznik_wiersza(size_t wiersz) const {
        return wiersz < liczba_wierszy_ ? wiersze_[wiersz].znacznik : 0;
    }

    void ustaw_znacznik_wiersza(size_t wiersz, uint64_t tag) {
        if (wiersz < liczba_wierszy_) wiersze_[wiersz].znacznik = tag;
    }

    int zaznaczony_wiersz() const { return zaznaczony_wiersz_; }

    bool ustaw_zaznaczony_wiersz(int wiersz) {
        if (wiersz < -1 || wiersz >= static_cast<int>(liczba_wierszy_)) return false;
        if (zaznaczony_wiersz_ == wiersz) return true;

        const int poprzedni = zaznaczony_wiersz_;
        if (zaznaczony_wiersz_ >= 0 && static_cast<size_t>(zaznaczony_wiersz_) < liczba_wierszy_) {
            wiersze_[zaznaczony_wiersz_].zaznaczony = false;
        }
        zaznaczony_wiersz_ = wiersz;
        if (zaznaczony_wiersz_ >= 0) {
            wiersze_[zaznaczony_wiersz_].zaznaczony = true;
            przewin_do_wiersza(zaznaczony_wiersz_);
        }

        uniewaznij_wiersz(poprzedni);
        uniewaznij_wiersz(zaznaczony_wiersz_);
        if (cb_zmiana_zaznaczenia_) {
            cb_zmiana_zaznaczenia_(this, zaznaczony_wiersz_, dane_zmiany_zaznaczenia_);
        }
        return true;
    }

    void przy_zmianie_zaznaczenia(FunkcjaTabeli cb, void* dane = nullptr) {
        cb_zmiana_zaznaczenia_ = cb;
        dane_zmiany_zaznaczenia_ = dane;
    }

    void przy_aktywacji(FunkcjaTabeli cb, void* dane = nullptr) {
        cb_aktywacja_ = cb;
        dane_aktywacji_ = dane;
    }

    int wysokosc_naglowka() const { return wysokosc_naglowka_; }
    void ustaw_wysokosc_naglowka(int h) {
        wysokosc_naglowka_ = h < 16 ? 16 : h;
        aktualizuj_przewijanie();
        uniewaznij();
    }

    int wysokosc_wiersza() const { return wysokosc_wiersza_; }
    void ustaw_wysokosc_wiersza(int h) {
        wysokosc_wiersza_ = h < 14 ? 14 : h;
        if (pionowy_) pionowy_->ustaw_maly_krok(wysokosc_wiersza_ * skala_dpi_);
        aktualizuj_przewijanie();
        uniewaznij();
    }

    void przewin_do_wiersza(int wiersz) {
        if (wiersz < 0 || wiersz >= static_cast<int>(liczba_wierszy_) || !pionowy_) return;
        int row_h = wysokosc_wiersza_ * skala_dpi_;
        int row_y = wiersz * row_h;
        int vp_h = viewport_h() - (wysokosc_naglowka_ * skala_dpi_);
        if (vp_h <= 0) return;
        int cur_scroll = static_cast<int>(pionowy_->wartosc());

        if (row_y < cur_scroll) {
            pionowy_->ustaw_wartosc(row_y);
        } else if (row_y + row_h > cur_scroll + vp_h) {
            pionowy_->ustaw_wartosc(row_y + row_h - vp_h);
        }
    }

    void ustaw_skale_dpi(SkalaUI s) override {
        Kontrolka::ustaw_skale_dpi(s);
        if (pionowy_) pionowy_->ustaw_maly_krok(wysokosc_wiersza_ * skala_dpi_);
        przelicz_calkowita_szerokosc();
        aktualizuj_przewijanie();
    }

    void rozmiesc(Prostokat docelowy_obszar) override {
        Kontrolka::rozmiesc(docelowy_obszar);
        aktualizuj_geometrie_paskow();
        aktualizuj_przewijanie();
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        int w = calkowita_szerokosc_kolumn_ > 0 ? calkowita_szerokosc_kolumn_ : 240;
        int h = (wysokosc_naglowka_ + static_cast<int>(liczba_wierszy_) * wysokosc_wiersza_) * skala_dpi_;
        return Rozmiar(w, h);
    }

    // =========================================================================
    // Rysowanie
    // =========================================================================

    void rysuj_siebie(KontekstRysowania& ctx) override {
        ostatnio_narysowane_wiersze_ = 0;
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        int vp_w = viewport_w();
        int vp_h = viewport_h();
        if (vp_w <= 0 || vp_h <= 0) return;

        int h_naglowka = wysokosc_naglowka_ * skala_dpi_;
        int h_wiersza = wysokosc_wiersza_ * skala_dpi_;
        int scroll_x = poziomy_ && poziomy_->czy_widoczna() ? static_cast<int>(poziomy_->wartosc()) : 0;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        Punkt off = ctx.pobierz_przesuniecie();
        Prostokat clip_swiat = ctx.pobierz_przyciecie();
        Prostokat clip_lokalny(clip_swiat.x - off.x, clip_swiat.y - off.y,
                               clip_swiat.szerokosc, clip_swiat.wysokosc);

        // 1. Nagłówek kolumn (przypięty na górze)
        Prostokat rect_naglowka(1, 1, vp_w, h_naglowka);
        if (rect_naglowka.przecina(clip_lokalny)) {
            ctx.dodaj_przyciecie(rect_naglowka);
            ctx.rysuj_prostokat(rect_naglowka, motyw_kolor(MOTYW_KOLOR_PANEL));

            int cur_x = 1 - scroll_x;
            for (size_t c = 0; c < liczba_kolumn_; ++c) {
                int col_w = kolumny_[c].szerokosc * skala_dpi_;
                ctx.rysuj_prostokat(Prostokat(cur_x + col_w - 1, 1, 1, h_naglowka), motyw_kolor(MOTYW_KOLOR_RAMKA));

                int txt_x = cur_x + 6;
                int txt_y = 1 + (h_naglowka - 16) / 2;
                int txt_max_w = col_w - 12;

                if (kolumny_[c].wyrownanie == WyrownaniePoziome::Srodek) {
                    int tw = CzcionkaUI::zmierz_tekst(kolumny_[c].nazwa, 1);
                    if (tw < txt_max_w) txt_x += (txt_max_w - tw) / 2;
                } else if (kolumny_[c].wyrownanie == WyrownaniePoziome::Koniec) {
                    int tw = CzcionkaUI::zmierz_tekst(kolumny_[c].nazwa, 1);
                    if (tw < txt_max_w) txt_x += (txt_max_w - tw);
                }

                ctx.rysuj_tekst_obciety(Punkt(txt_x, txt_y), kolumny_[c].nazwa,
                                        motyw_kolor(MOTYW_KOLOR_AKCENT), txt_max_w, 1);
                cur_x += col_w;
            }
            ctx.rysuj_prostokat(Prostokat(1, h_naglowka, vp_w, 1), motyw_kolor(MOTYW_KOLOR_RAMKA));
            ctx.zdejmij_przyciecie();
        }

        // 2. Wiersze danych (wirtualizowane)
        Prostokat rect_danych(1, 1 + h_naglowka + 1, vp_w, vp_h - h_naglowka - 1);
        Prostokat clip_danych = rect_danych.przeciecie(clip_lokalny);
        if (clip_danych.czy_pusty()) return;

        ctx.dodaj_przyciecie(rect_danych);

        int start_row = (scroll_y + clip_danych.y - rect_danych.y) / h_wiersza;
        if (start_row < 0) start_row = 0;
        int end_row = (scroll_y + clip_danych.dolny() - rect_danych.y - 1) /
                      h_wiersza + 1;
        if (end_row > static_cast<int>(liczba_wierszy_)) end_row = static_cast<int>(liczba_wierszy_);

        for (int r = start_row; r < end_row; ++r) {
            ++ostatnio_narysowane_wiersze_;
            int row_y = rect_danych.y + (r * h_wiersza) - scroll_y;
            Prostokat row_rect(1, row_y, vp_w, h_wiersza);

            bool sel = (r == zaznaczony_wiersz_);
            bool hov = (r == najechany_wiersz_ && czy_wlaczona_);

            if (sel) {
                ctx.rysuj_prostokat(row_rect, motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
            } else if (hov) {
                ctx.rysuj_prostokat(row_rect, motyw_kolor(MOTYW_KOLOR_PODNIESIONY));
            } else if (r % 2 == 1) {
                ctx.rysuj_prostokat(row_rect, motyw_kolor(MOTYW_KOLOR_PANEL));
            }

            const Wiersz& w = wiersze_[r];
            int cell_x = 1 - scroll_x;
            uint32_t col_txt = sel ? motyw_kolor_tekstu_zaznaczenia() : s.tekst;

            for (size_t c = 0; c < liczba_kolumn_; ++c) {
                int col_w = kolumny_[c].szerokosc * skala_dpi_;
                const char* val = (c < w.liczba_komorek && w.komorki[c]) ? w.komorki[c] : "";

                int txt_x = cell_x + 6;
                int txt_y = row_y + (h_wiersza - 16) / 2;
                int txt_max_w = col_w - 12;

                if (kolumny_[c].wyrownanie == WyrownaniePoziome::Srodek) {
                    int tw = CzcionkaUI::zmierz_tekst(val, 1);
                    if (tw < txt_max_w) txt_x += (txt_max_w - tw) / 2;
                } else if (kolumny_[c].wyrownanie == WyrownaniePoziome::Koniec) {
                    int tw = CzcionkaUI::zmierz_tekst(val, 1);
                    if (tw < txt_max_w) txt_x += (txt_max_w - tw);
                }

                ctx.rysuj_tekst_obciety(Punkt(txt_x, txt_y), val,
                                        col_txt, txt_max_w, 1);
                ctx.rysuj_prostokat(Prostokat(cell_x + col_w - 1, row_y, 1, h_wiersza), motyw_kolor(MOTYW_KOLOR_RAMKA));
                cell_x += col_w;
            }

            ctx.rysuj_prostokat(Prostokat(1, row_y + h_wiersza - 1, vp_w, 1), motyw_kolor(MOTYW_KOLOR_RAMKA));
        }

        ctx.zdejmij_przyciecie();
    }

    // =========================================================================
    // Obsługa wejścia
    // =========================================================================

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        int h_naglowka = wysokosc_naglowka_ * skala_dpi_;
        int h_wiersza = wysokosc_wiersza_ * skala_dpi_;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;

        if (ly > h_naglowka && lx < viewport_w() && ly < viewport_h()) {
            int rel_y = ly - h_naglowka - 1 + scroll_y;
            int r = rel_y / h_wiersza;
            if (r >= 0 && r < static_cast<int>(liczba_wierszy_)) {
                ustaw_zaznaczony_wiersz(r);
                e.obsluzone = true;
            }
        }
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_podwojne_klikniecie(e);
        if (zaznaczony_wiersz_ >= 0 && cb_aktywacja_) {
            cb_aktywacja_(this, zaznaczony_wiersz_, dane_aktywacji_);
            e.obsluzone = true;
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_ruch_myszy(e);
        if (!czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        int h_naglowka = wysokosc_naglowka_ * skala_dpi_;
        int h_wiersza = wysokosc_wiersza_ * skala_dpi_;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;

        int stary = najechany_wiersz_;
        if (ly > h_naglowka && lx < viewport_w() && ly < viewport_h()) {
            int rel_y = ly - h_naglowka - 1 + scroll_y;
            int r = rel_y / h_wiersza;
            najechany_wiersz_ = (r >= 0 && r < static_cast<int>(liczba_wierszy_)) ? r : -1;
        } else {
            najechany_wiersz_ = -1;
        }

        if (stary != najechany_wiersz_) {
            uniewaznij_wiersz(stary);
            uniewaznij_wiersz(najechany_wiersz_);
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_wiersz_ != -1) {
            const int poprzedni = najechany_wiersz_;
            najechany_wiersz_ = -1;
            uniewaznij_wiersz(poprzedni);
        }
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0 || !pionowy_ ||
            !pionowy_->czy_widoczna()) return;
        const int64_t stara = pionowy_->wartosc();
        const int64_t krok = static_cast<int64_t>(wysokosc_wiersza_) *
                             skala_dpi_ * 3;
        pionowy_->przewin_o(e.delta_y > 0 ? -krok : krok);
        if (pionowy_->wartosc() != stara) e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        Kontrolka::obsluz_wcisniecie_klawisza(e);
        if (!czy_wlaczona_ || liczba_wierszy_ == 0) return;

        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if ((usb && baza == 81U) || (e0 && baza == 0x50U)) { // Down
            if (zaznaczony_wiersz_ + 1 < static_cast<int>(liczba_wierszy_)) {
                ustaw_zaznaczony_wiersz(zaznaczony_wiersz_ + 1);
            }
            e.obsluzone = true;
        } else if ((usb && baza == 82U) || (e0 && baza == 0x48U)) { // Up
            if (zaznaczony_wiersz_ > 0) {
                ustaw_zaznaczony_wiersz(zaznaczony_wiersz_ - 1);
            }
            e.obsluzone = true;
        } else if ((usb && baza == 78U) || (e0 && baza == 0x51U)) { // Page Down
            int h_wiersza = wysokosc_wiersza_ * skala_dpi_;
            int page_rows = (viewport_h() - wysokosc_naglowka_ * skala_dpi_) / (h_wiersza > 0 ? h_wiersza : 1);
            int target = zaznaczony_wiersz_ + page_rows;
            if (target >= static_cast<int>(liczba_wierszy_)) target = static_cast<int>(liczba_wierszy_ - 1);
            ustaw_zaznaczony_wiersz(target);
            e.obsluzone = true;
        } else if ((usb && baza == 75U) || (e0 && baza == 0x49U)) { // Page Up
            int h_wiersza = wysokosc_wiersza_ * skala_dpi_;
            int page_rows = (viewport_h() - wysokosc_naglowka_ * skala_dpi_) / (h_wiersza > 0 ? h_wiersza : 1);
            int target = zaznaczony_wiersz_ - page_rows;
            if (target < 0) target = 0;
            ustaw_zaznaczony_wiersz(target);
            e.obsluzone = true;
        } else if ((usb && baza == 74U) || (e0 && baza == 0x47U)) { // Home
            ustaw_zaznaczony_wiersz(0);
            e.obsluzone = true;
        } else if ((usb && baza == 77U) || (e0 && baza == 0x4FU)) { // End
            ustaw_zaznaczony_wiersz(static_cast<int>(liczba_wierszy_ - 1));
            e.obsluzone = true;
        } else if (kod == 13 || (usb && baza == 40U)) { // Enter
            if (zaznaczony_wiersz_ >= 0 && cb_aktywacja_) {
                cb_aktywacja_(this, zaznaczony_wiersz_, dane_aktywacji_);
                e.obsluzone = true;
            }
        }
    }

private:
    Kolumna kolumny_[MAKS_KOLUMN]{};
    size_t liczba_kolumn_{0};
    int calkowita_szerokosc_kolumn_{0};

    Wiersz wiersze_[MAKS_WIERSZY]{};
    size_t liczba_wierszy_{0};

    int zaznaczony_wiersz_{-1};
    int najechany_wiersz_{-1};
    size_t ostatnio_narysowane_wiersze_{0};

    int wysokosc_naglowka_{24};
    int wysokosc_wiersza_{20};
    int szerokosc_paska_{14};

    PasekPrzewijania* pionowy_{nullptr};
    PasekPrzewijania* poziomy_{nullptr};

    FunkcjaTabeli cb_zmiana_zaznaczenia_{nullptr};
    void* dane_zmiany_zaznaczenia_{nullptr};

    FunkcjaTabeli cb_aktywacja_{nullptr};
    void* dane_aktywacji_{nullptr};

    static char* kopiuj_tekst(const char* zrodlo) {
        if (!zrodlo) zrodlo = "";
        size_t len = 0;
        while (zrodlo[len] != '\0') ++len;
        char* buf = new char[len + 1];
        for (size_t i = 0; i <= len; ++i) buf[i] = zrodlo[i];
        return buf;
    }

    void zwolnij_komorki(Wiersz& w) {
        if (w.komorki) {
            for (size_t i = 0; i < w.liczba_komorek; ++i) {
                if (w.komorki[i]) {
                    delete[] w.komorki[i];
                    w.komorki[i] = nullptr;
                }
            }
            delete[] w.komorki;
            w.komorki = nullptr;
        }
        w.liczba_komorek = 0;
    }

    void przelicz_calkowita_szerokosc() {
        int w = 0;
        for (size_t i = 0; i < liczba_kolumn_; ++i) {
            w += kolumny_[i].szerokosc * skala_dpi_;
        }
        calkowita_szerokosc_kolumn_ = w;
    }

    int viewport_w() const {
        int w = obszar_.szerokosc - 2;
        if (pionowy_ && pionowy_->czy_widoczna()) {
            w -= pionowy_->obszar().szerokosc;
        }
        return w > 0 ? w : 0;
    }

    int viewport_h() const {
        int h = obszar_.wysokosc - 2;
        if (poziomy_ && poziomy_->czy_widoczna()) {
            h -= poziomy_->obszar().wysokosc;
        }
        return h > 0 ? h : 0;
    }

    void aktualizuj_geometrie_paskow() {
        int sb_sz = szerokosc_paska_ * skala_dpi_;
        bool potr_pion = (static_cast<int>(liczba_wierszy_) * wysokosc_wiersza_ * skala_dpi_) >
                         (obszar_.wysokosc - wysokosc_naglowka_ * skala_dpi_);
        bool potr_poz = calkowita_szerokosc_kolumn_ > (obszar_.szerokosc - (potr_pion ? sb_sz : 0));

        if (pionowy_) {
            pionowy_->ustaw_widoczna(potr_pion);
            if (potr_pion) {
                pionowy_->rozmiesc(Prostokat(
                    obszar_.szerokosc - sb_sz - 1, 1, sb_sz,
                    obszar_.wysokosc - (potr_poz ? sb_sz : 0) - 2));
            }
        }
        if (poziomy_) {
            poziomy_->ustaw_widoczna(potr_poz);
            if (potr_poz) {
                poziomy_->rozmiesc(Prostokat(
                    1, obszar_.wysokosc - sb_sz - 1,
                    obszar_.szerokosc - (potr_pion ? sb_sz : 0) - 2, sb_sz));
            }
        }
    }

    void aktualizuj_przewijanie() {
        aktualizuj_geometrie_paskow();
        if (pionowy_) {
            int h_danych = static_cast<int>(liczba_wierszy_) * wysokosc_wiersza_ * skala_dpi_;
            int h_widoku = viewport_h() - (wysokosc_naglowka_ * skala_dpi_);
            int maks = h_danych - (h_widoku > 0 ? h_widoku : 0);
            if (maks < 0) maks = 0;
            pionowy_->ustaw_zakres(0, maks);
            pionowy_->ustaw_rozmiar_strony(h_widoku > 0 ? h_widoku : 1);
            pionowy_->ustaw_duzy_krok(h_widoku > 0 ? h_widoku : 1);
        }
        if (poziomy_) {
            int w_danych = calkowita_szerokosc_kolumn_;
            int w_widoku = viewport_w();
            int maks = w_danych - w_widoku;
            if (maks < 0) maks = 0;
            poziomy_->ustaw_zakres(0, maks);
            poziomy_->ustaw_rozmiar_strony(w_widoku > 0 ? w_widoku : 1);
            poziomy_->ustaw_duzy_krok(w_widoku > 0 ? w_widoku : 1);
        }
    }

    void uniewaznij_wiersz(int wiersz) {
        if (wiersz < 0 || wysokosc_wiersza_ <= 0) return;
        const int naglowek = wysokosc_naglowka_ * skala_dpi_;
        const int h = wysokosc_wiersza_ * skala_dpi_;
        const int scroll = pionowy_ && pionowy_->czy_widoczna()
            ? static_cast<int>(pionowy_->wartosc()) : 0;
        const int y = 2 + naglowek + wiersz * h - scroll;
        Prostokat r(1, y, viewport_w(), h);
        r = r.przeciecie(Prostokat(1, 1 + naglowek, viewport_w(),
                                   viewport_h() - naglowek));
        if (!r.czy_pusty()) uniewaznij_obszar(r);
    }
};

} // namespace BursztynUI
