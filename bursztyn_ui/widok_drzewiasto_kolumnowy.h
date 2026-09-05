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
    // Pula rosnie geometrycznie dopiero wraz z modelem. Pusta kontrolka nie
    // zawiera wielusetkilobajtowej tablicy, a twardy limit pozostaje jawny.
    static constexpr size_t MAKS_WEZLOW = 4096;

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
        uint32_t ikona{0};
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
        delete[] wezly_;
        delete[] widoczne_wezly_;
        wezly_ = nullptr;
        widoczne_wezly_ = nullptr;
        pojemnosc_wezlow_ = 0;
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
        if (!w.czy_poprawny() || w.indeks >= pojemnosc_wezlow_) return false;
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
    UchwytWezlaDrzewaKolumnowego fokus_wezel() const { return fokus_; }
    size_t liczba_widocznych_wezlow() const { return liczba_widocznych_; }
    size_t liczba_ostatnio_narysowanych_wierszy() const { return ostatnio_narysowane_; }
    PasekPrzewijania& pasek_pionowy() { return *pionowy_; }
    PasekPrzewijania& pasek_poziomy() { return *poziomy_; }

    bool ustaw_wybrany_wezel(UchwytWezlaDrzewaKolumnowego wezel) {
        if (wezel.czy_poprawny() && !czy_uchwyt_poprawny(wezel)) return false;
        if (wybrany_ == wezel) return true;

        UchwytWezlaDrzewaKolumnowego stary = wybrany_;
        wybrany_ = wezel;
        if (wezel.czy_poprawny()) fokus_ = wezel;
        uniewaznij_wezel(stary);
        uniewaznij_wezel(wybrany_);
        if (cb_zmiana_zaznaczenia_) {
            cb_zmiana_zaznaczenia_(this, wybrany_, dane_zmiany_zaznaczenia_);
        }
        return true;
    }

    bool ustaw_fokus_wezel(UchwytWezlaDrzewaKolumnowego wezel) {
        if (wezel.czy_poprawny() && !czy_uchwyt_poprawny(wezel)) return false;
        if (fokus_ == wezel) return true;
        UchwytWezlaDrzewaKolumnowego stary = fokus_;
        fokus_ = wezel;
        uniewaznij_wezel(stary); uniewaznij_wezel(fokus_);
        przewin_do_fokusu();
        return true;
    }

    bool ustaw_ikone(UchwytWezlaDrzewaKolumnowego wezel, uint32_t ikona) {
        if (!czy_uchwyt_poprawny(wezel)) return false;
        if (wezly_[wezel.indeks].ikona == ikona) return true;
        wezly_[wezel.indeks].ikona = ikona; uniewaznij_wezel(wezel); return true;
    }
    uint32_t ikona(UchwytWezlaDrzewaKolumnowego wezel) const {
        return czy_uchwyt_poprawny(wezel) ? wezly_[wezel.indeks].ikona : 0;
    }

    const char* komorka(UchwytWezlaDrzewaKolumnowego wezel, size_t kolumna) const {
        if (!czy_uchwyt_poprawny(wezel)) return "";
        const Wezel& w = wezly_[wezel.indeks];
        if (kolumna >= w.liczba_komorek || !w.komorki[kolumna]) return "";
        return w.komorki[kolumna];
    }

    void wyczysc() {
        for (size_t i = 0; i < pojemnosc_wezlow_; ++i) {
            if (wezly_[i].uzyty) {
                zwolnij_komorki(wezly_[i]);
                wezly_[i].uzyty = false;
                ++wezly_[i].generacja;
                if (wezly_[i].generacja == 0) wezly_[i].generacja = 1;
            }
        }
        pierwszy_korzen_ = -1;
        liczba_uzytych_ = 0;
        wybrany_ = UchwytWezlaDrzewaKolumnowego{};
        fokus_ = UchwytWezlaDrzewaKolumnowego{};
        najechany_wezel_ = -1;
        przebuduj_widoczne();
        aktualizuj_przewijanie();
        uniewaznij();
    }

    size_t liczba_wezlow() const { return liczba_uzytych_; }

    void rozpocznij_aktualizacje() { Kontrolka::rozpocznij_aktualizacje(); }
    void zakoncz_aktualizacje() {
        przebuduj_widoczne();
        aktualizuj_przewijanie();
        Kontrolka::zakoncz_aktualizacje();
    }

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
        ostatnio_narysowane_ = 0;
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
                Punkt(cur_x + 6 * skala_dpi_,
                      1 + (h_naglowka - 16 * skala_dpi_) / 2),
                kolumny_[c].nazwa, 0x00FFD700,
                col_w - 12 * skala_dpi_, skala_dpi_);
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
            ++ostatnio_narysowane_;
            int node_idx = widoczne_wezly_[r];
            const Wezel& w = wezly_[node_idx];

            int row_y = rect_danych.y + (r * h_wiersza) - scroll_y;
            Prostokat row_rect(1, row_y, vp_w, h_wiersza);

            bool sel = (wybrany_.czy_poprawny() && wybrany_.indeks == static_cast<uint32_t>(node_idx));
            bool foc = (fokus_.czy_poprawny() && fokus_.indeks == static_cast<uint32_t>(node_idx));
            bool hov = (node_idx == najechany_wezel_ && czy_wlaczona_);

            if (sel) {
                ctx.rysuj_prostokat(row_rect, Kolory::ZaznaczenieTlo);
            } else if (hov) {
                ctx.rysuj_prostokat(row_rect, 0x003A2814);
            } else if (r % 2 == 1) {
                ctx.rysuj_prostokat(row_rect, 0x001F140A);
            }
            if (foc && czy_ma_fokus_)
                ctx.rysuj_ramke(Prostokat(row_rect.x + 1, row_rect.y + 1,
                                          row_rect.szerokosc - 2, row_rect.wysokosc - 2),
                                Kolory::AkcentFokus, skala_dpi_);

            // Kolumna 0: Wcięcie + Uchwyt rozwijania + Tekst węzła
            int indent = w.glebokosc * 16 * skala_dpi_;
            int tree_x = 1 - scroll_x + indent + 4 * skala_dpi_;

            if (w.liczba_dzieci > 0) {
                // Grot z geometrii jest symetryczny przy kazdej skali DPI.
                int cy = row_y + h_wiersza / 2;
                if (w.rozwiniety) {
                    for (int q = 0; q < 4 * skala_dpi_; ++q)
                        ctx.rysuj_prostokat(Prostokat(tree_x + q, cy - 2 * skala_dpi_ + q,
                                                     8 * skala_dpi_ - 2 * q, skala_dpi_), 0x00FFD700);
                } else {
                    for (int q = 0; q < 4 * skala_dpi_; ++q)
                        ctx.rysuj_prostokat(Prostokat(tree_x + q, cy - 4 * skala_dpi_ + q,
                                                     skala_dpi_, 8 * skala_dpi_ - 2 * q), 0x00FFD700);
                }
            }
            tree_x += 14 * skala_dpi_;

            const char* val0 = (w.liczba_komorek > 0 && w.komorki[0]) ? w.komorki[0] : "";
            uint32_t col_txt = sel ? Kolory::ZaznaczenieTekst : s.tekst;
            int col0_text_max_w = (1 - scroll_x + col0_w) - tree_x - 4 * skala_dpi_;
            if (col0_text_max_w > 0) {
                ctx.rysuj_tekst_obciety(
                    Punkt(tree_x, row_y + (h_wiersza - 16 * skala_dpi_) / 2), val0,
                    col_txt, col0_text_max_w, skala_dpi_);
            }

            ctx.rysuj_prostokat(Prostokat(1 - scroll_x + col0_w - 1, row_y, 1, h_wiersza), 0x002A1C0E);

            // Kolumny 1..N
            int cell_x = 1 - scroll_x + col0_w;
            for (size_t c = 1; c < liczba_kolumn_; ++c) {
                int col_w = kolumny_[c].szerokosc * skala_dpi_;
                const char* val = (c < w.liczba_komorek && w.komorki[c]) ? w.komorki[c] : "";

                int txt_x = cell_x + 6 * skala_dpi_;
                int txt_y = row_y + (h_wiersza - 16 * skala_dpi_) / 2;
                int txt_max_w = col_w - 12 * skala_dpi_;

                if (kolumny_[c].wyrownanie == WyrownaniePoziome::Srodek) {
                    int tw = CzcionkaUI::zmierz_tekst(val, skala_dpi_);
                    if (tw < txt_max_w) txt_x += (txt_max_w - tw) / 2;
                } else if (kolumny_[c].wyrownanie == WyrownaniePoziome::Koniec) {
                    int tw = CzcionkaUI::zmierz_tekst(val, skala_dpi_);
                    if (tw < txt_max_w) txt_x += (txt_max_w - tw);
                }

                ctx.rysuj_tekst_obciety(Punkt(txt_x, txt_y), val,
                                        col_txt, txt_max_w, skala_dpi_);
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

        if (stary != najechany_wezel_) {
            if (stary >= 0 && static_cast<size_t>(stary) < pojemnosc_wezlow_ && wezly_[stary].uzyty)
                uniewaznij_wezel({static_cast<uint32_t>(stary), wezly_[stary].generacja});
            if (najechany_wezel_ >= 0 && static_cast<size_t>(najechany_wezel_) < pojemnosc_wezlow_ &&
                wezly_[najechany_wezel_].uzyty)
                uniewaznij_wezel({static_cast<uint32_t>(najechany_wezel_),
                                  wezly_[najechany_wezel_].generacja});
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_wezel_ != -1) {
            int stary = najechany_wezel_;
            najechany_wezel_ = -1;
            if (static_cast<size_t>(stary) < pojemnosc_wezlow_ && wezly_[stary].uzyty)
                uniewaznij_wezel({static_cast<uint32_t>(stary), wezly_[stary].generacja});
        }
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_) return;
        if (pionowy_ && pionowy_->czy_widoczna() && e.delta_y != 0) {
            const int krok = 3 * wysokosc_wiersza_ * (skala_dpi_ < 1 ? 1 : skala_dpi_);
            pionowy_->przewin_o(e.delta_y > 0 ? -krok : krok);
            e.obsluzone = true;
        } else if (poziomy_ && poziomy_->czy_widoczna() &&
                   (e.delta_x != 0 || e.delta_y != 0)) {
            const int delta = e.delta_x != 0 ? e.delta_x : e.delta_y;
            poziomy_->przewin_o(delta > 0 ? -40 : 40);
            e.obsluzone = true;
        }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || liczba_widocznych_ == 0) return;
        uint32_t k = e.kod_klawisza; bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (k & BWS_KEY_SOURCE_E0) != 0; uint32_t b = usb ? k & 0xFFU : k & 0x7FU;
        int poz = pozycja_widocznego(fokus_.czy_poprawny() ? fokus_ : wybrany_);
        if (poz < 0) poz = 0;
        int cel = poz;
        if ((usb && b == 82U) || (e0 && b == 0x48U)) --cel;
        else if ((usb && b == 81U) || (e0 && b == 0x50U)) ++cel;
        else if ((usb && b == 75U) || (e0 && b == 0x49U)) cel -= widoczne_wiersze();
        else if ((usb && b == 78U) || (e0 && b == 0x51U)) cel += widoczne_wiersze();
        else if ((usb && b == 74U) || (e0 && b == 0x47U)) cel = 0;
        else if ((usb && b == 77U) || (e0 && b == 0x4FU)) cel = static_cast<int>(liczba_widocznych_) - 1;
        else if ((usb && b == 80U) || (e0 && b == 0x4BU)) {
            if (fokus_.czy_poprawny() && wezly_[fokus_.indeks].rozwiniety && wezly_[fokus_.indeks].liczba_dzieci > 0)
                zwin(fokus_);
            else if (fokus_.czy_poprawny() && wezly_[fokus_.indeks].rodzic >= 0) {
                int p = wezly_[fokus_.indeks].rodzic;
                ustaw_fokus_wezel({static_cast<uint32_t>(p), wezly_[p].generacja});
            }
            e.obsluzone = true; return;
        } else if ((usb && b == 79U) || (e0 && b == 0x4DU)) {
            if (fokus_.czy_poprawny() && wezly_[fokus_.indeks].liczba_dzieci > 0) {
                if (!wezly_[fokus_.indeks].rozwiniety) rozwin(fokus_);
                else { int d = wezly_[fokus_.indeks].pierwsze_dziecko; ustaw_fokus_wezel({static_cast<uint32_t>(d), wezly_[d].generacja}); }
            }
            e.obsluzone = true; return;
        } else if (k == 13U || k == 32U || (usb && (b == 40U || b == 44U))) {
            if (fokus_.czy_poprawny()) {
                ustaw_wybrany_wezel(fokus_);
                if (wezly_[fokus_.indeks].liczba_dzieci > 0) przelacz_rozwiniecie(fokus_);
                if (k == 13U && cb_aktywacja_) cb_aktywacja_(this, fokus_, dane_aktywacji_);
            }
            e.obsluzone = true; return;
        } else { Kontrolka::obsluz_wcisniecie_klawisza(e); return; }
        cel = przytnij(cel, 0, static_cast<int>(liczba_widocznych_) - 1);
        int idx = widoczne_wezly_[cel];
        ustaw_fokus_wezel({static_cast<uint32_t>(idx), wezly_[idx].generacja});
        e.obsluzone = true;
    }

private:
    Kolumna kolumny_[MAKS_KOLUMN]{};
    size_t liczba_kolumn_{0};
    int calkowita_szerokosc_kolumn_{0};

    Wezel* wezly_{nullptr};
    size_t pojemnosc_wezlow_{0};
    size_t liczba_uzytych_{0};
    int pierwszy_korzen_{-1};

    int* widoczne_wezly_{nullptr};
    size_t liczba_widocznych_{0};

    UchwytWezlaDrzewaKolumnowego wybrany_{};
    UchwytWezlaDrzewaKolumnowego fokus_{};
    int najechany_wezel_{-1};
    size_t ostatnio_narysowane_{0};

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
        while (zrodlo[len] != '\0' && len < 255) ++len;
        char* buf = new char[len + 1];
        if (!buf) return nullptr;
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

    bool zapewnij_pojemnosc_wezlow(size_t wymagana) {
        if (wymagana <= pojemnosc_wezlow_) return true;
        if (wymagana > MAKS_WEZLOW) return false;
        size_t nowa = pojemnosc_wezlow_ ? pojemnosc_wezlow_ * 2U : 32U;
        if (nowa < wymagana) nowa = wymagana;
        if (nowa > MAKS_WEZLOW) nowa = MAKS_WEZLOW;
        Wezel* nowe_wezly = new Wezel[nowa];
        int* nowe_widoczne = new int[nowa];
        if (!nowe_wezly || !nowe_widoczne) {
            delete[] nowe_wezly;
            delete[] nowe_widoczne;
            return false;
        }
        for (size_t i = 0; i < nowa; ++i) nowe_widoczne[i] = -1;
        for (size_t i = 0; i < pojemnosc_wezlow_; ++i) nowe_wezly[i] = wezly_[i];
        for (size_t i = 0; i < liczba_widocznych_; ++i) nowe_widoczne[i] = widoczne_wezly_[i];
        delete[] wezly_;
        delete[] widoczne_wezly_;
        wezly_ = nowe_wezly;
        widoczne_wezly_ = nowe_widoczne;
        pojemnosc_wezlow_ = nowa;
        return true;
    }

    int znajdz_wolny_slot() {
        for (size_t i = 0; i < pojemnosc_wezlow_; ++i) {
            if (!wezly_[i].uzyty) return static_cast<int>(i);
        }
        size_t stara = pojemnosc_wezlow_;
        if (!zapewnij_pojemnosc_wezlow(stara + 1U)) return -1;
        return static_cast<int>(stara);
    }

    UchwytWezlaDrzewaKolumnowego dodaj_wezel_wewn(int rodzic_idx, const char* const* wartosci,
                                                 size_t ile_komorek, uint64_t znacznik) {
        int idx = znajdz_wolny_slot();
        if (idx < 0) return UchwytWezlaDrzewaKolumnowego{};

        Wezel& w = wezly_[idx];
        const uint32_t generacja = w.generacja ? w.generacja : 1U;
        w.znacznik = znacznik;
        w.rodzic = rodzic_idx;
        w.pierwsze_dziecko = -1;
        w.nastepne_rodzenstwo = -1;
        w.poprzednie_rodzenstwo = -1;
        w.liczba_dzieci = 0;
        w.rozwiniety = true;
        w.ikona = 0;
        w.glebokosc = (rodzic_idx >= 0) ? wezly_[rodzic_idx].glebokosc + 1 : 0;

        w.liczba_komorek = liczba_kolumn_ > 0 ? liczba_kolumn_ : ile_komorek;
        if (w.liczba_komorek == 0) w.liczba_komorek = 1;
        w.komorki = new char*[w.liczba_komorek];
        if (!w.komorki) {
            w = Wezel{};
            w.generacja = generacja;
            return UchwytWezlaDrzewaKolumnowego{};
        }
        for (size_t c = 0; c < w.liczba_komorek; ++c) w.komorki[c] = nullptr;
        for (size_t c = 0; c < w.liczba_komorek; ++c) {
            const char* val = (c < ile_komorek && wartosci && wartosci[c]) ? wartosci[c] : "";
            w.komorki[c] = kopiuj_tekst(val);
            if (!w.komorki[c]) {
                zwolnij_komorki(w);
                w = Wezel{};
                w.generacja = generacja;
                return UchwytWezlaDrzewaKolumnowego{};
            }
        }
        w.uzyty = true;
        w.generacja = generacja;

        if (rodzic_idx < 0) {
            if (pierwszy_korzen_ < 0) {
                pierwszy_korzen_ = idx;
            } else {
                int k = pierwszy_korzen_;
                size_t kroki = 0;
                while (wezly_[k].nastepne_rodzenstwo >= 0 &&
                       kroki++ < pojemnosc_wezlow_) {
                    k = wezly_[k].nastepne_rodzenstwo;
                }
                if (kroki > pojemnosc_wezlow_) {
                    zwolnij_komorki(w);
                    w = Wezel{};
                    w.generacja = generacja;
                    return UchwytWezlaDrzewaKolumnowego{};
                }
                wezly_[k].nastepne_rodzenstwo = idx;
                w.poprzednie_rodzenstwo = k;
            }
        } else {
            Wezel& r = wezly_[rodzic_idx];
            if (r.pierwsze_dziecko < 0) {
                r.pierwsze_dziecko = idx;
            } else {
                int d = r.pierwsze_dziecko;
                size_t kroki = 0;
                while (wezly_[d].nastepne_rodzenstwo >= 0 &&
                       kroki++ < pojemnosc_wezlow_) {
                    d = wezly_[d].nastepne_rodzenstwo;
                }
                if (kroki > pojemnosc_wezlow_) {
                    zwolnij_komorki(w);
                    w = Wezel{};
                    w.generacja = generacja;
                    return UchwytWezlaDrzewaKolumnowego{};
                }
                wezly_[d].nastepne_rodzenstwo = idx;
                w.poprzednie_rodzenstwo = d;
            }
            ++r.liczba_dzieci;
        }

        ++liczba_uzytych_;
        if (!czy_w_trakcie_aktualizacji()) {
            przebuduj_widoczne();
            aktualizuj_przewijanie();
            uniewaznij();
        }

        return UchwytWezlaDrzewaKolumnowego{static_cast<uint32_t>(idx), w.generacja};
    }

    void przebuduj_widoczne() {
        liczba_widocznych_ = 0;
        if (pierwszy_korzen_ < 0) return;
        // Iteracyjny pre-order wykorzystuje lacza rodzic/rodzenstwo, dlatego
        // glebokosc drzewa nie zuzywa ani stosu procesora, ani sterty.
        int idx = pierwszy_korzen_;
        size_t kroki = 0;
        while (idx >= 0 && liczba_widocznych_ < pojemnosc_wezlow_ &&
               kroki++ < MAKS_WEZLOW * 2U) {
            if (idx >= static_cast<int>(pojemnosc_wezlow_) || !wezly_[idx].uzyty) break;
            widoczne_wezly_[liczba_widocznych_++] = idx;
            if (wezly_[idx].rozwiniety && wezly_[idx].pierwsze_dziecko >= 0) {
                idx = wezly_[idx].pierwsze_dziecko;
                continue;
            }
            while (idx >= 0 && wezly_[idx].nastepne_rodzenstwo < 0) {
                idx = wezly_[idx].rodzic;
                if (kroki++ >= MAKS_WEZLOW * 2U) break;
            }
            if (idx >= 0) idx = wezly_[idx].nastepne_rodzenstwo;
        }
    }

    void przelicz_calkowita_szerokosc() {
        int64_t w = 0;
        for (size_t i = 0; i < liczba_kolumn_; ++i) {
            w += static_cast<int64_t>(kolumny_[i].szerokosc) * skala_dpi_;
            if (w > INT32_MAX) { w = INT32_MAX; break; }
        }
        calkowita_szerokosc_kolumn_ = static_cast<int>(w);
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
                pionowy_->rozmiesc(Prostokat(obszar_.szerokosc - sb_sz - 1, 1, sb_sz,
                                             obszar_.wysokosc - (potr_poz ? sb_sz : 0) - 2));
            }
        }
        if (poziomy_) {
            poziomy_->ustaw_widoczna(potr_poz);
            if (potr_poz) {
                poziomy_->rozmiesc(Prostokat(1, obszar_.wysokosc - sb_sz - 1,
                                             obszar_.szerokosc - (potr_pion ? sb_sz : 0) - 2, sb_sz));
            }
        }
    }

    void aktualizuj_przewijanie() {
        aktualizuj_geometrie_paskow();
        if (pionowy_) {
            uint64_t h64 = static_cast<uint64_t>(liczba_widocznych_) *
                           static_cast<uint64_t>(wysokosc_wiersza_) * static_cast<uint64_t>(skala_dpi_);
            int h_danych = h64 > static_cast<uint64_t>(INT32_MAX) ? INT32_MAX : static_cast<int>(h64);
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

    int pozycja_widocznego(UchwytWezlaDrzewaKolumnowego w) const {
        if (!czy_uchwyt_poprawny(w)) return -1;
        for (size_t i = 0; i < liczba_widocznych_; ++i)
            if (widoczne_wezly_[i] == static_cast<int>(w.indeks)) return static_cast<int>(i);
        return -1;
    }
    int widoczne_wiersze() const {
        int h = viewport_h() - wysokosc_naglowka_ * skala_dpi_;
        int rh = wysokosc_wiersza_ * skala_dpi_;
        return rh > 0 && h > 0 ? (h / rh > 0 ? h / rh : 1) : 1;
    }
    void przewin_do_fokusu() {
        int p = pozycja_widocznego(fokus_); if (p < 0 || !pionowy_) return;
        int rh = wysokosc_wiersza_ * skala_dpi_, y = p * rh;
        int cur = static_cast<int>(pionowy_->wartosc());
        int h = viewport_h() - wysokosc_naglowka_ * skala_dpi_;
        if (y < cur) pionowy_->ustaw_wartosc(y);
        else if (y + rh > cur + h) pionowy_->ustaw_wartosc(y + rh - h);
    }
    void uniewaznij_wezel(UchwytWezlaDrzewaKolumnowego w) {
        int p = pozycja_widocznego(w); if (p < 0) return;
        int rh = wysokosc_wiersza_ * skala_dpi_;
        int sy = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        int y = 1 + wysokosc_naglowka_ * skala_dpi_ + 1 + p * rh - sy;
        Prostokat r(1, y, viewport_w(), rh);
        if (!r.przeciecie(Prostokat(1, 1, viewport_w(), viewport_h())).czy_pusty())
            uniewaznij_obszar(r);
    }
};

} // namespace BursztynUI
