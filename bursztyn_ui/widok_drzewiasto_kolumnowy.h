#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

struct UchwytWezlaDrzewaKolumnowego {
    uint32_t indeks{0};
    uint32_t generacja{0};

    bool czy_poprawny() const { return generacja != 0; }
    bool operator==(const UchwytWezlaDrzewaKolumnowego& o) const {
        return indeks == o.indeks && generacja == o.generacja;
    }
    bool operator!=(const UchwytWezlaDrzewaKolumnowego& o) const {
        return !(*this == o);
    }
};

class WidokDrzewiastoKolumnowy : public Kontrolka {
public:
    static constexpr size_t MAKS_KOLUMN = 16;
    static constexpr size_t MAKS_WEZLOW = 1024;

    struct Kolumna {
        char nazwa[64]{};
        int szerokosc{120};
        int minimalna_szerokosc{30};
        WyrownaniePoziome wyrownanie{WyrownaniePoziome::Poczatek};
    };

    struct Wezel {
        char** komorki{nullptr};
        size_t liczba_komorek{0};
        uint64_t znacznik{0};
        int rodzic{-1};
        int pierwsze_dziecko{-1};
        int nastepne_rodzenstwo{-1};
        int poprzednie_rodzenstwo{-1};
        int liczba_dzieci{0};
        int glebokosc{0};
        bool rozwiniety{true};
        bool uzyty{false};
        uint32_t generacja{1};
    };

    using FunkcjaDrzewaKolumnowego = void (*)(WidokDrzewiastoKolumnowy* nadawca,
                                              UchwytWezlaDrzewaKolumnowego wezel,
                                              void* dane_uzytkownika);

    WidokDrzewiastoKolumnowy() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = Kolory::WejscieTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = true;
        preferowany_rozmiar_ = Rozmiar(360, 240);

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

        pionowy_->model().przy_zmianie([](ModelPrzewijania*, int64_t, void* dane) {
            WidokDrzewiastoKolumnowy* w = static_cast<WidokDrzewiastoKolumnowy*>(dane);
            if (w) w->uniewaznij();
        }, this);

        poziomy_->model().przy_zmianie([](ModelPrzewijania*, int64_t, void* dane) {
            WidokDrzewiastoKolumnowy* w = static_cast<WidokDrzewiastoKolumnowy*>(dane);
            if (w) w->uniewaznij();
        }, this);
    }

    ~WidokDrzewiastoKolumnowy() override {
        wyczysc();
    }

    // =========================================================================
    // Kolumny
    // =========================================================================

    bool dodaj_kolumne(const char* nazwa, int szerokosc = 120,
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

    // =========================================================================
    // Węzły
    // =========================================================================

    UchwytWezlaDrzewaKolumnowego dodaj_korzen(const char* k0, const char* k1 = nullptr,
                                             const char* k2 = nullptr, const char* k3 = nullptr,
                                             uint64_t znacznik = 0) {
        const char* tab[4] = {k0, k1, k2, k3};
        size_t ile = 1;
        if (k1) ile = 2;
        if (k2) ile = 3;
        if (k3) ile = 4;
        return dodaj_wezel_wewn(-1, tab, ile, znacznik);
    }

    UchwytWezlaDrzewaKolumnowego dodaj_dziecko(UchwytWezlaDrzewaKolumnowego rodzic,
                                              const char* k0, const char* k1 = nullptr,
                                              const char* k2 = nullptr, const char* k3 = nullptr,
                                              uint64_t znacznik = 0) {
        if (!czy_uchwyt_poprawny(rodzic)) return UchwytWezlaDrzewaKolumnowego{};
        const char* tab[4] = {k0, k1, k2, k3};
        size_t ile = 1;
        if (k1) ile = 2;
        if (k2) ile = 3;
        if (k3) ile = 4;
        return dodaj_wezel_wewn(static_cast<int>(rodzic.indeks), tab, ile, znacznik);
    }

    bool czy_uchwyt_poprawny(UchwytWezlaDrzewaKolumnowego w) const {
        if (!w.czy_poprawny() || w.indeks >= MAKS_WEZLOW) return false;
        return wezly_[w.indeks].uzyty && wezly_[w.indeks].generacja == w.generacja;
    }

    bool rozwin(UchwytWezlaDrzewaKolumnowego wezel) {
        if (!czy_uchwyt_poprawny(wezel)) return false;
        if (!wezly_[wezel.indeks].rozwiniety) {
            wezly_[wezel.indeks].rozwiniety = true;
            przebuduj_widoczne();
            aktualizuj_przewijanie();
            uniewaznij();
        }
        return true;
    }

    bool zwin(UchwytWezlaDrzewaKolumnowego wezel) {
        if (!czy_uchwyt_poprawny(wezel)) return false;
        if (wezly_[wezel.indeks].rozwiniety) {
            wezly_[wezel.indeks].rozwiniety = false;
            przebuduj_widoczne();
            aktualizuj_przewijanie();
            uniewaznij();
        }
        return true;
    }

    bool przelacz_rozwiniecie(UchwytWezlaDrzewaKolumnowego wezel) {
        if (!czy_uchwyt_poprawny(wezel)) return false;
        wezly_[wezel.indeks].rozwiniety = !wezly_[wezel.indeks].rozwiniety;
        przebuduj_widoczne();
        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    bool czy_rozwiniety(UchwytWezlaDrzewaKolumnowego wezel) const {
        if (!czy_uchwyt_poprawny(wezel)) return false;
        return wezly_[wezel.indeks].rozwiniety;
    }

    UchwytWezlaDrzewaKolumnowego wybrany_wezel() const { return wybrany_; }

    bool ustaw_wybrany_wezel(UchwytWezlaDrzewaKolumnowego wezel) {
        if (wezel.czy_poprawny() && !czy_uchwyt_poprawny(wezel)) return false;
        if (wybrany_ == wezel) return true;

        wybrany_ = wezel;
        uniewaznij();
        if (cb_zmiana_zaznaczenia_) {
            cb_zmiana_zaznaczenia_(this, wybrany_, dane_zmiany_zaznaczenia_);
        }
        return true;
    }

    const char* komorka(UchwytWezlaDrzewaKolumnowego wezel, size_t kolumna) const {
        if (!czy_uchwyt_poprawny(wezel)) return "";
        const Wezel& w = wezly_[wezel.indeks];
        if (kolumna >= w.liczba_komorek || !w.komorki[kolumna]) return "";
        return w.komorki[kolumna];
    }

    void wyczysc() {
        for (size_t i = 0; i < MAKS_WEZLOW; ++i) {
            if (wezly_[i].uzyty) {
                zwolnij_komorki(wezly_[i]);
                wezly_[i].uzyty = false;
                ++wezly_[i].generacja;
            }
        }
        pierwszy_korzen_ = -1;
        liczba_uzytych_ = 0;
        wybrany_ = UchwytWezlaDrzewaKolumnowego{};
        najechany_wezel_ = -1;
        przebuduj_widoczne();
        aktualizuj_przewijanie();
        uniewaznij();
    }

    size_t liczba_wezlow() const { return liczba_uzytych_; }

    void przy_zmianie_zaznaczenia(FunkcjaDrzewaKolumnowego cb, void* dane = nullptr) {
        cb_zmiana_zaznaczenia_ = cb;
        dane_zmiany_zaznaczenia_ = dane;
    }

    void przy_aktywacji(FunkcjaDrzewaKolumnowego cb, void* dane = nullptr) {
        cb_aktywacja_ = cb;
        dane_aktywacji_ = dane;
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        int w = calkowita_szerokosc_kolumn_ > 0 ? calkowita_szerokosc_kolumn_ : 300;
        int h = (wysokosc_naglowka_ + static_cast<int>(liczba_widocznych_) * wysokosc_wiersza_) * skala_dpi_;
        return Rozmiar(w, h);
    }

    void rozmiesc(Prostokat docelowy_obszar) override {
        Kontrolka::rozmiesc(docelowy_obszar);
        aktualizuj_geometrie_paskow();
        aktualizuj_przewijanie();
    }

    // =========================================================================
    // Rysowanie
    // =========================================================================

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        int vp_w = viewport_w();
        int vp_h = viewport_h();
        if (vp_w <= 0 || vp_h <= 0) return;

        int h_naglowka = wysokosc_naglowka_ * skala_dpi_;
        int h_wiersza = wysokosc_wiersza_ * skala_dpi_;
        int scroll_x = poziomy_ && poziomy_->czy_widoczna() ? static_cast<int>(poziomy_->wartosc()) : 0;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;

        // 1. Nagłówek kolumn
        Prostokat rect_naglowka(1, 1, vp_w, h_naglowka);
        ctx.dodaj_przyciecie(rect_naglowka);
        ctx.rysuj_prostokat(rect_naglowka, 0x002B1D0E);

        int cur_x = 1 - scroll_x;
        for (size_t c = 0; c < liczba_kolumn_; ++c) {
            int col_w = kolumny_[c].szerokosc * skala_dpi_;
            ctx.rysuj_prostokat(Prostokat(cur_x + col_w - 1, 1, 1, h_naglowka), Kolory::RamkaDomyslna);
            ctx.rysuj_tekst_obciety(
                Punkt(cur_x + 6, 1 + (h_naglowka - 16) / 2),
                kolumny_[c].nazwa, 0x00FFD700, col_w - 12, 1);
            cur_x += col_w;
        }
        ctx.rysuj_prostokat(Prostokat(1, h_naglowka, vp_w, 1), Kolory::RamkaDomyslna);
        ctx.zdejmij_przyciecie();

        // 2. Wiersze drzewa (wirtualizowane)
        Prostokat rect_danych(1, 1 + h_naglowka + 1, vp_w, vp_h - h_naglowka - 1);
        if (rect_danych.czy_pusty()) return;

        ctx.dodaj_przyciecie(rect_danych);

        int start_row = scroll_y / h_wiersza;
        if (start_row < 0) start_row = 0;
        int end_row = (scroll_y + rect_danych.wysokosc) / h_wiersza + 1;
        if (end_row > static_cast<int>(liczba_widocznych_)) end_row = static_cast<int>(liczba_widocznych_);

        int col0_w = liczba_kolumn_ > 0 ? kolumny_[0].szerokosc * skala_dpi_ : 120;

        for (int r = start_row; r < end_row; ++r) {
            int node_idx = widoczne_wezly_[r];
            const Wezel& w = wezly_[node_idx];

            int row_y = rect_danych.y + (r * h_wiersza) - scroll_y;
            Prostokat row_rect(1, row_y, vp_w, h_wiersza);

            bool sel = (wybrany_.czy_poprawny() && wybrany_.indeks == static_cast<uint32_t>(node_idx));
            bool hov = (node_idx == najechany_wezel_ && czy_wlaczona_);

            if (sel) {
                ctx.rysuj_prostokat(row_rect, Kolory::ZaznaczenieTlo);
            } else if (hov) {
                ctx.rysuj_prostokat(row_rect, 0x003A2814);
            } else if (r % 2 == 1) {
                ctx.rysuj_prostokat(row_rect, 0x001F140A);
            }

            // Kolumna 0: Wcięcie + Uchwyt rozwijania + Tekst węzła
            int indent = w.glebokosc * 16 * skala_dpi_;
            int tree_x = 1 - scroll_x + indent + 4 * skala_dpi_;

            if (w.liczba_dzieci > 0) {
                const char* chev = w.rozwiniety ? "v" : ">";
                ctx.rysuj_tekst_obciety(
                    Punkt(tree_x, row_y + (h_wiersza - 16) / 2), chev,
                    0x00FFD700, 12, 1);
            }
            tree_x += 14 * skala_dpi_;

            const char* val0 = (w.liczba_komorek > 0 && w.komorki[0]) ? w.komorki[0] : "";
            uint32_t col_txt = sel ? Kolory::ZaznaczenieTekst : s.tekst;
            int col0_text_max_w = (1 - scroll_x + col0_w) - tree_x - 4;
            if (col0_text_max_w > 0) {
                ctx.rysuj_tekst_obciety(
                    Punkt(tree_x, row_y + (h_wiersza - 16) / 2), val0,
                    col_txt, col0_text_max_w, 1);
            }

            ctx.rysuj_prostokat(Prostokat(1 - scroll_x + col0_w - 1, row_y, 1, h_wiersza), 0x002A1C0E);

            // Kolumny 1..N
            int cell_x = 1 - scroll_x + col0_w;
            for (size_t c = 1; c < liczba_kolumn_; ++c) {
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
                ctx.rysuj_prostokat(Prostokat(cell_x + col_w - 1, row_y, 1, h_wiersza), 0x002A1C0E);
                cell_x += col_w;
            }

            ctx.rysuj_prostokat(Prostokat(1, row_y + h_wiersza - 1, vp_w, 1), 0x002A1C0E);
        }

        ctx.zdejmij_przyciecie();
    }

    // =========================================================================
    // Wejście
    // =========================================================================

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        int h_naglowka = wysokosc_naglowka_ * skala_dpi_;
        int h_wiersza = wysokosc_wiersza_ * skala_dpi_;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        int scroll_x = poziomy_ && poziomy_->czy_widoczna() ? static_cast<int>(poziomy_->wartosc()) : 0;

        if (ly > h_naglowka && lx < viewport_w() && ly < viewport_h()) {
            int rel_y = ly - h_naglowka - 1 + scroll_y;
            int r = rel_y / h_wiersza;
            if (r >= 0 && r < static_cast<int>(liczba_widocznych_)) {
                int node_idx = widoczne_wezly_[r];
                Wezel& w = wezly_[node_idx];

                int indent = w.glebokosc * 16 * skala_dpi_;
                int chev_x = 1 - scroll_x + indent + 4 * skala_dpi_;
                if (w.liczba_dzieci > 0 && lx >= chev_x && lx <= chev_x + 16 * skala_dpi_) {
                    przelacz_rozwiniecie(UchwytWezlaDrzewaKolumnowego{static_cast<uint32_t>(node_idx), w.generacja});
                } else {
                    ustaw_wybrany_wezel(UchwytWezlaDrzewaKolumnowego{static_cast<uint32_t>(node_idx), w.generacja});
                }
                e.obsluzone = true;
            }
        }
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_podwojne_klikniecie(e);
        if (wybrany_.czy_poprawny()) {
            if (wezly_[wybrany_.indeks].liczba_dzieci > 0) {
                przelacz_rozwiniecie(wybrany_);
            }
            if (cb_aktywacja_) {
                cb_aktywacja_(this, wybrany_, dane_aktywacji_);
            }
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

        int stary = najechany_wezel_;
        if (ly > h_naglowka && lx < viewport_w() && ly < viewport_h()) {
            int rel_y = ly - h_naglowka - 1 + scroll_y;
            int r = rel_y / h_wiersza;
            najechany_wezel_ = (r >= 0 && r < static_cast<int>(liczba_widocznych_)) ? widoczne_wezly_[r] : -1;
        } else {
            najechany_wezel_ = -1;
        }

        if (stary != najechany_wezel_) uniewaznij();
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_wezel_ != -1) {
            najechany_wezel_ = -1;
            uniewaznij();
        }
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (pionowy_ && pionowy_->czy_widoczna()) {
            pionowy_->obsluz_rolke(e);
        } else if (poziomy_ && poziomy_->czy_widoczna()) {
            poziomy_->obsluz_rolke(e);
        }
    }

private:
    Kolumna kolumny_[MAKS_KOLUMN]{};
    size_t liczba_kolumn_{0};
    int calkowita_szerokosc_kolumn_{0};

    Wezel wezly_[MAKS_WEZLOW]{};
    size_t liczba_uzytych_{0};
    int pierwszy_korzen_{-1};

    int widoczne_wezly_[MAKS_WEZLOW]{};
    size_t liczba_widocznych_{0};

    UchwytWezlaDrzewaKolumnowego wybrany_{};
    int najechany_wezel_{-1};

    int wysokosc_naglowka_{24};
    int wysokosc_wiersza_{20};
    int szerokosc_paska_{14};

    PasekPrzewijania* pionowy_{nullptr};
    PasekPrzewijania* poziomy_{nullptr};

    FunkcjaDrzewaKolumnowego cb_zmiana_zaznaczenia_{nullptr};
    void* dane_zmiany_zaznaczenia_{nullptr};

    FunkcjaDrzewaKolumnowego cb_aktywacja_{nullptr};
    void* dane_aktywacji_{nullptr};

    static char* kopiuj_tekst(const char* zrodlo) {
        if (!zrodlo) zrodlo = "";
        size_t len = 0;
        while (zrodlo[len] != '\0') ++len;
        char* buf = new char[len + 1];
        for (size_t i = 0; i <= len; ++i) buf[i] = zrodlo[i];
        return buf;
    }

    void zwolnij_komorki(Wezel& w) {
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

    int znajdz_wolny_slot() {
        for (size_t i = 0; i < MAKS_WEZLOW; ++i) {
            if (!wezly_[i].uzyty) return static_cast<int>(i);
        }
        return -1;
    }

    UchwytWezlaDrzewaKolumnowego dodaj_wezel_wewn(int rodzic_idx, const char* const* wartosci,
                                                 size_t ile_komorek, uint64_t znacznik) {
        int idx = znajdz_wolny_slot();
        if (idx < 0) return UchwytWezlaDrzewaKolumnowego{};

        Wezel& w = wezly_[idx];
        w.uzyty = true;
        w.znacznik = znacznik;
        w.rodzic = rodzic_idx;
        w.pierwsze_dziecko = -1;
        w.nastepne_rodzenstwo = -1;
        w.poprzednie_rodzenstwo = -1;
        w.liczba_dzieci = 0;
        w.rozwiniety = true;
        w.glebokosc = (rodzic_idx >= 0) ? wezly_[rodzic_idx].glebokosc + 1 : 0;

        w.liczba_komorek = liczba_kolumn_ > 0 ? liczba_kolumn_ : ile_komorek;
        if (w.liczba_komorek == 0) w.liczba_komorek = 1;
        w.komorki = new char*[w.liczba_komorek];
        for (size_t c = 0; c < w.liczba_komorek; ++c) {
            const char* val = (c < ile_komorek && wartosci && wartosci[c]) ? wartosci[c] : "";
            w.komorki[c] = kopiuj_tekst(val);
        }

        if (rodzic_idx < 0) {
            if (pierwszy_korzen_ < 0) {
                pierwszy_korzen_ = idx;
            } else {
                int k = pierwszy_korzen_;
                while (wezly_[k].nastepne_rodzenstwo >= 0) {
                    k = wezly_[k].nastepne_rodzenstwo;
                }
                wezly_[k].nastepne_rodzenstwo = idx;
                w.poprzednie_rodzenstwo = k;
            }
        } else {
            Wezel& r = wezly_[rodzic_idx];
            ++r.liczba_dzieci;
            if (r.pierwsze_dziecko < 0) {
                r.pierwsze_dziecko = idx;
            } else {
                int d = r.pierwsze_dziecko;
                while (wezly_[d].nastepne_rodzenstwo >= 0) {
                    d = wezly_[d].nastepne_rodzenstwo;
                }
                wezly_[d].nastepne_rodzenstwo = idx;
                w.poprzednie_rodzenstwo = d;
            }
        }

        ++liczba_uzytych_;
        przebuduj_widoczne();
        aktualizuj_przewijanie();
        uniewaznij();

        return UchwytWezlaDrzewaKolumnowego{static_cast<uint32_t>(idx), w.generacja};
    }

    void przebuduj_widoczne() {
        liczba_widocznych_ = 0;
        int k = pierwszy_korzen_;
        while (k >= 0) {
            dodaj_widoczne_rekurencyjnie(k);
            k = wezly_[k].nastepne_rodzenstwo;
        }
    }

    void dodaj_widoczne_rekurencyjnie(int idx) {
        if (idx < 0 || liczba_widocznych_ >= MAKS_WEZLOW) return;
        widoczne_wezly_[liczba_widocznych_++] = idx;
        const Wezel& w = wezly_[idx];
        if (w.rozwiniety && w.pierwsze_dziecko >= 0) {
            int d = w.pierwsze_dziecko;
            while (d >= 0) {
                dodaj_widoczne_rekurencyjnie(d);
                d = wezly_[d].nastepne_rodzenstwo;
            }
        }
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
        if (pionowy_ && pionowy_->czy_widoczna()) w -= pionowy_->obszar().szerokosc;
        return w > 0 ? w : 0;
    }

    int viewport_h() const {
        int h = obszar_.wysokosc - 2;
        if (poziomy_ && poziomy_->czy_widoczna()) h -= poziomy_->obszar().wysokosc;
        return h > 0 ? h : 0;
    }

    void aktualizuj_geometrie_paskow() {
        int sb_sz = szerokosc_paska_ * skala_dpi_;
        bool potr_pion = (static_cast<int>(liczba_widocznych_) * wysokosc_wiersza_ * skala_dpi_) >
                         (obszar_.wysokosc - wysokosc_naglowka_ * skala_dpi_);
        bool potr_poz = calkowita_szerokosc_kolumn_ > (obszar_.szerokosc - (potr_pion ? sb_sz : 0));

        if (pionowy_) {
            pionowy_->ustaw_widoczna(potr_pion);
            if (potr_pion) {
                pionowy_->ustaw_geometrie(obszar_.szerokosc - sb_sz - 1, 1, sb_sz,
                                          obszar_.wysokosc - (potr_poz ? sb_sz : 0) - 2);
            }
        }
        if (poziomy_) {
            poziomy_->ustaw_widoczna(potr_poz);
            if (potr_poz) {
                poziomy_->ustaw_geometrie(1, obszar_.wysokosc - sb_sz - 1,
                                          obszar_.szerokosc - (potr_pion ? sb_sz : 0) - 2, sb_sz);
            }
        }
    }

    void aktualizuj_przewijanie() {
        aktualizuj_geometrie_paskow();
        if (pionowy_) {
            int h_danych = static_cast<int>(liczba_widocznych_) * wysokosc_wiersza_ * skala_dpi_;
            int h_widoku = viewport_h() - (wysokosc_naglowka_ * skala_dpi_);
            pionowy_->ustaw_zakres(0, h_danych);
            pionowy_->ustaw_rozmiar_strony(h_widoku > 0 ? h_widoku : 1);
        }
        if (poziomy_) {
            int w_danych = calkowita_szerokosc_kolumn_;
            int w_widoku = viewport_w();
            poziomy_->ustaw_zakres(0, w_danych);
            poziomy_->ustaw_rozmiar_strony(w_widoku > 0 ? w_widoku : 1);
        }
    }
};

} // namespace BursztynUI
