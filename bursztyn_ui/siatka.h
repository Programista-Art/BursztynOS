#pragma once

#include "kontrolka.h"

namespace BursztynUI {

enum class TypWymiaruSiatki : uint8_t {
    Piksele,
    Auto,
    Gwiazdka
};

struct DefinicjaWymiaru {
    TypWymiaruSiatki typ{TypWymiaruSiatki::Gwiazdka};
    int wartosc{1};

    constexpr DefinicjaWymiaru() = default;
    constexpr DefinicjaWymiaru(TypWymiaruSiatki t, int w = 1) : typ(t), wartosc(w) {}

    static constexpr DefinicjaWymiaru piksele(int px) {
        return DefinicjaWymiaru(TypWymiaruSiatki::Piksele, px);
    }
    static constexpr DefinicjaWymiaru automatyczny() {
        return DefinicjaWymiaru(TypWymiaruSiatki::Auto, 0);
    }
    static constexpr DefinicjaWymiaru gwiazdka(int waga = 1) {
        return DefinicjaWymiaru(TypWymiaruSiatki::Gwiazdka, waga);
    }
};

class Siatka : public Kontrolka {
public:
    static constexpr size_t MAKS_WIERSZY = 16;
    static constexpr size_t MAKS_KOLUMN = 16;
    static constexpr size_t MAKS_KOMOREK = Kontrolka::MAKS_DZIECI;

    struct Komorka {
        Kontrolka* kontrolka{nullptr};
        int wiersz{0};
        int kolumna{0};
        int span_wierszy{1};
        int span_kolumn{1};
    };

    Siatka() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        wypelnienie_ = Marginesy(4, 4, 4, 4);
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        Kontrolka::zaktualizuj_motyw();
    }

    void dodaj_wiersz(DefinicjaWymiaru def = DefinicjaWymiaru::gwiazdka(1)) {
        if (liczba_wierszy_ < MAKS_WIERSZY) {
            wiersze_[liczba_wierszy_++] = def;
            uniewaznij();
        }
    }

    void dodaj_kolumne(DefinicjaWymiaru def = DefinicjaWymiaru::gwiazdka(1)) {
        if (liczba_kolumn_ < MAKS_KOLUMN) {
            kolumny_[liczba_kolumn_++] = def;
            uniewaznij();
        }
    }

    void ustaw_wiersz(size_t idx, DefinicjaWymiaru def) {
        if (idx < liczba_wierszy_) {
            wiersze_[idx] = def;
            uniewaznij();
        }
    }

    void ustaw_kolumne(size_t idx, DefinicjaWymiaru def) {
        if (idx < liczba_kolumn_) {
            kolumny_[idx] = def;
            uniewaznij();
        }
    }

    void wyczysc_wiersze() { liczba_wierszy_ = 0; uniewaznij(); }
    void wyczysc_kolumny() { liczba_kolumn_ = 0; uniewaznij(); }

    size_t liczba_wierszy() const { return liczba_wierszy_; }
    size_t liczba_kolumn() const { return liczba_kolumn_; }

    DefinicjaWymiaru wiersz(size_t idx) const {
        return (idx < liczba_wierszy_) ? wiersze_[idx] : DefinicjaWymiaru::gwiazdka(1);
    }
    DefinicjaWymiaru kolumna(size_t idx) const {
        return (idx < liczba_kolumn_) ? kolumny_[idx] : DefinicjaWymiaru::gwiazdka(1);
    }

    int odstep_poziomy() const { return odstep_x_; }
    int odstep_pionowy() const { return odstep_y_; }
    void ustaw_odstep(int ox, int oy) {
        odstep_x_ = ox >= 0 ? ox : 0;
        odstep_y_ = oy >= 0 ? oy : 0;
        uniewaznij();
    }
    void ustaw_odstep(int o) { ustaw_odstep(o, o); }

    bool dodaj_do_siatki(Kontrolka* k, int w, int kol, int span_w = 1, int span_kol = 1) {
        if (!k || liczba_komorek_ >= MAKS_KOMOREK) return false;
        if (!dodaj_kontrolke(k)) return false;

        Komorka& kom = komorki_[liczba_komorek_++];
        kom.kontrolka = k;
        kom.wiersz = w >= 0 ? w : 0;
        kom.kolumna = kol >= 0 ? kol : 0;
        kom.span_wierszy = span_w >= 1 ? span_w : 1;
        kom.span_kolumn = span_kol >= 1 ? span_kol : 1;

        if (static_cast<size_t>(kom.wiersz + kom.span_wierszy) > liczba_wierszy_) {
            liczba_wierszy_ = static_cast<size_t>(kom.wiersz + kom.span_wierszy);
            if (liczba_wierszy_ > MAKS_WIERSZY) liczba_wierszy_ = MAKS_WIERSZY;
        }
        if (static_cast<size_t>(kom.kolumna + kom.span_kolumn) > liczba_kolumn_) {
            liczba_kolumn_ = static_cast<size_t>(kom.kolumna + kom.span_kolumn);
            if (liczba_kolumn_ > MAKS_KOLUMN) liczba_kolumn_ = MAKS_KOLUMN;
        }

        uniewaznij();
        return true;
    }

    template <typename T, typename... Args>
    T& dodaj_komorke(int w, int kol, int span_w, int span_kol, Args&&... args) {
        T* k = new T(args...);
        dodaj_do_siatki(k, w, kol, span_w, span_kol);
        return *k;
    }

    template <typename T, typename... Args>
    T& dodaj_komorke(int w, int kol, Args&&... args) {
        return dodaj_komorke<T>(w, kol, 1, 1, args...);
    }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        SkalaUI s = skala_dpi_;
        int pad_h = wypelnienie_.poziomo() * s;
        int pad_v = wypelnienie_.pionowo() * s;

        int rows = liczba_wierszy_ > 0 ? static_cast<int>(liczba_wierszy_) : 1;
        int cols = liczba_kolumn_ > 0 ? static_cast<int>(liczba_kolumn_) : 1;

        int total_gap_x = (cols > 1) ? (cols - 1) * odstep_x_ * s : 0;
        int total_gap_y = (rows > 1) ? (rows - 1) * odstep_y_ * s : 0;

        int col_w[MAKS_KOLUMN]{};
        int row_h[MAKS_WIERSZY]{};

        // Oblicz wstępne wymiary wierszy i kolumn
        for (int c = 0; c < cols; ++c) {
            DefinicjaWymiaru def = (c < static_cast<int>(liczba_kolumn_)) ? kolumny_[c] : DefinicjaWymiaru::gwiazdka(1);
            if (def.typ == TypWymiaruSiatki::Piksele) col_w[c] = def.wartosc * s;
        }
        for (int r = 0; r < rows; ++r) {
            DefinicjaWymiaru def = (r < static_cast<int>(liczba_wierszy_)) ? wiersze_[r] : DefinicjaWymiaru::gwiazdka(1);
            if (def.typ == TypWymiaruSiatki::Piksele) row_h[r] = def.wartosc * s;
        }

        // Zmierz kontrolki dla wymiarów Auto
        for (size_t i = 0; i < liczba_komorek_; ++i) {
            const Komorka& k = komorki_[i];
            if (!k.kontrolka || !k.kontrolka->czy_widoczna()) continue;

            Rozmiar sz = k.kontrolka->zmierz(dostepny_rozmiar);
            Marginesy cm = k.kontrolka->margines();
            int mw = sz.szerokosc + cm.poziomo() * s;
            int mh = sz.wysokosc + cm.pionowo() * s;

            if (k.span_kolumn == 1 && k.kolumna < cols) {
                DefinicjaWymiaru def = (k.kolumna < static_cast<int>(liczba_kolumn_)) ? kolumny_[k.kolumna] : DefinicjaWymiaru::gwiazdka(1);
                if (def.typ == TypWymiaruSiatki::Auto && mw > col_w[k.kolumna]) {
                    col_w[k.kolumna] = mw;
                }
            }
            if (k.span_wierszy == 1 && k.wiersz < rows) {
                DefinicjaWymiaru def = (k.wiersz < static_cast<int>(liczba_wierszy_)) ? wiersze_[k.wiersz] : DefinicjaWymiaru::gwiazdka(1);
                if (def.typ == TypWymiaruSiatki::Auto && mh > row_h[k.wiersz]) {
                    row_h[k.wiersz] = mh;
                }
            }
        }

        int sum_w = pad_h + total_gap_x;
        int sum_h = pad_v + total_gap_y;
        for (int c = 0; c < cols; ++c) sum_w += col_w[c];
        for (int r = 0; r < rows; ++r) sum_h += row_h[r];

        Rozmiar zadany(sum_w, sum_h);
        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * s;

        return zadany;
    }

    void rozmiesc(Prostokat docelowy) override {
        bool rozmiar_zmieniony = (obszar_.szerokosc != docelowy.szerokosc || obszar_.wysokosc != docelowy.wysokosc);
        obszar_ = docelowy;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        SkalaUI s = skala_dpi_;
        int pad_l = wypelnienie_.lewy * s;
        int pad_t = wypelnienie_.gorny * s;
        int pad_r = wypelnienie_.prawy * s;
        int pad_b = wypelnienie_.dolny * s;

        int klient_w = obszar_.szerokosc - (pad_l + pad_r);
        int klient_h = obszar_.wysokosc - (pad_t + pad_b);
        if (klient_w < 0) klient_w = 0;
        if (klient_h < 0) klient_h = 0;

        int rows = liczba_wierszy_ > 0 ? static_cast<int>(liczba_wierszy_) : 1;
        int cols = liczba_kolumn_ > 0 ? static_cast<int>(liczba_kolumn_) : 1;

        int total_gap_x = (cols > 1) ? (cols - 1) * odstep_x_ * s : 0;
        int total_gap_y = (rows > 1) ? (rows - 1) * odstep_y_ * s : 0;

        int col_w[MAKS_KOLUMN]{};
        int row_h[MAKS_WIERSZY]{};
        int col_stars = 0;
        int row_stars = 0;

        // 1. Ustal wymiary stałe (Piksele) oraz zbierz wagi gwiazdek
        for (int c = 0; c < cols; ++c) {
            DefinicjaWymiaru def = (c < static_cast<int>(liczba_kolumn_)) ? kolumny_[c] : DefinicjaWymiaru::gwiazdka(1);
            if (def.typ == TypWymiaruSiatki::Piksele) {
                col_w[c] = def.wartosc * s;
            } else if (def.typ == TypWymiaruSiatki::Gwiazdka) {
                col_stars += def.wartosc > 0 ? def.wartosc : 1;
            }
        }
        for (int r = 0; r < rows; ++r) {
            DefinicjaWymiaru def = (r < static_cast<int>(liczba_wierszy_)) ? wiersze_[r] : DefinicjaWymiaru::gwiazdka(1);
            if (def.typ == TypWymiaruSiatki::Piksele) {
                row_h[r] = def.wartosc * s;
            } else if (def.typ == TypWymiaruSiatki::Gwiazdka) {
                row_stars += def.wartosc > 0 ? def.wartosc : 1;
            }
        }

        // 2. Oblicz wymiary Auto z kontrolek
        for (size_t i = 0; i < liczba_komorek_; ++i) {
            const Komorka& k = komorki_[i];
            if (!k.kontrolka || !k.kontrolka->czy_widoczna()) continue;

            Rozmiar sz = k.kontrolka->zmierz(Rozmiar(klient_w, klient_h));
            Marginesy cm = k.kontrolka->margines();
            int mw = sz.szerokosc + cm.poziomo() * s;
            int mh = sz.wysokosc + cm.pionowo() * s;

            if (k.span_kolumn == 1 && k.kolumna < cols) {
                DefinicjaWymiaru def = (k.kolumna < static_cast<int>(liczba_kolumn_)) ? kolumny_[k.kolumna] : DefinicjaWymiaru::gwiazdka(1);
                if (def.typ == TypWymiaruSiatki::Auto && mw > col_w[k.kolumna]) {
                    col_w[k.kolumna] = mw;
                }
            }
            if (k.span_wierszy == 1 && k.wiersz < rows) {
                DefinicjaWymiaru def = (k.wiersz < static_cast<int>(liczba_wierszy_)) ? wiersze_[k.wiersz] : DefinicjaWymiaru::gwiazdka(1);
                if (def.typ == TypWymiaruSiatki::Auto && mh > row_h[k.wiersz]) {
                    row_h[k.wiersz] = mh;
                }
            }
        }

        // 3. Rozdziel pozostałą przestrzeń na kolumny i wiersze Gwiazdka
        int uzyta_szer = total_gap_x;
        for (int c = 0; c < cols; ++c) uzyta_szer += col_w[c];
        int wolna_szer = klient_w - uzyta_szer;
        if (wolna_szer > 0 && col_stars > 0) {
            for (int c = 0; c < cols; ++c) {
                DefinicjaWymiaru def = (c < static_cast<int>(liczba_kolumn_)) ? kolumny_[c] : DefinicjaWymiaru::gwiazdka(1);
                if (def.typ == TypWymiaruSiatki::Gwiazdka) {
                    int waga = def.wartosc > 0 ? def.wartosc : 1;
                    col_w[c] = (wolna_szer * waga) / col_stars;
                }
            }
        }

        int uzyta_wys = total_gap_y;
        for (int r = 0; r < rows; ++r) uzyta_wys += row_h[r];
        int wolna_wys = klient_h - uzyta_wys;
        if (wolna_wys > 0 && row_stars > 0) {
            for (int r = 0; r < rows; ++r) {
                DefinicjaWymiaru def = (r < static_cast<int>(liczba_wierszy_)) ? wiersze_[r] : DefinicjaWymiaru::gwiazdka(1);
                if (def.typ == TypWymiaruSiatki::Gwiazdka) {
                    int waga = def.wartosc > 0 ? def.wartosc : 1;
                    row_h[r] = (wolna_wys * waga) / row_stars;
                }
            }
        }

        // 4. Pozycje początkowe kolumn i wierszy
        int col_x[MAKS_KOLUMN + 1]{};
        int row_y[MAKS_WIERSZY + 1]{};
        int cur_x = pad_l;
        for (int c = 0; c < cols; ++c) {
            col_x[c] = cur_x;
            cur_x += col_w[c] + odstep_x_ * s;
        }
        col_x[cols] = cur_x;

        int cur_y = pad_t;
        for (int r = 0; r < rows; ++r) {
            row_y[r] = cur_y;
            cur_y += row_h[r] + odstep_y_ * s;
        }
        row_y[rows] = cur_y;

        // 5. Rozmieść kontrolki w komórkach
        for (size_t i = 0; i < liczba_komorek_; ++i) {
            const Komorka& k = komorki_[i];
            if (!k.kontrolka || !k.kontrolka->czy_widoczna()) continue;

            int c0 = k.kolumna < cols ? k.kolumna : cols - 1;
            int c1 = (k.kolumna + k.span_kolumn <= cols) ? k.kolumna + k.span_kolumn : cols;
            int r0 = k.wiersz < rows ? k.wiersz : rows - 1;
            int r1 = (k.wiersz + k.span_wierszy <= rows) ? k.wiersz + k.span_wierszy : rows;

            int cell_x = col_x[c0];
            int cell_y = row_y[r0];
            int cell_w = 0;
            for (int c = c0; c < c1; ++c) cell_w += col_w[c];
            if (c1 > c0 + 1) cell_w += (c1 - c0 - 1) * odstep_x_ * s;

            int cell_h = 0;
            for (int r = r0; r < r1; ++r) cell_h += row_h[r];
            if (r1 > r0 + 1) cell_h += (r1 - r0 - 1) * odstep_y_ * s;

            Marginesy cm = k.kontrolka->margines();
            int cml = cm.lewy * s;
            int cmt = cm.gorny * s;
            int cmr = cm.prawy * s;
            int cmb = cm.dolny * s;

            int av_w = cell_w - (cml + cmr); if (av_w < 0) av_w = 0;
            int av_h = cell_h - (cmt + cmb); if (av_h < 0) av_h = 0;

            Rozmiar sz = k.kontrolka->zmierz(Rozmiar(av_w, av_h));

            int child_w = (k.kontrolka->wyrownanie_poziome() == WyrownaniePoziome::Rozciagnij)
                          ? av_w : sz.szerokosc;
            if (child_w > av_w) child_w = av_w;

            int child_h = (k.kontrolka->wyrownanie_pionowe() == WyrownaniePionowe::Rozciagnij)
                          ? av_h : sz.wysokosc;
            if (child_h > av_h) child_h = av_h;

            int child_x = cell_x + cml;
            if (k.kontrolka->wyrownanie_poziome() == WyrownaniePoziome::Srodek) {
                child_x += (av_w - child_w) / 2;
            } else if (k.kontrolka->wyrownanie_poziome() == WyrownaniePoziome::Koniec) {
                child_x += av_w - child_w;
            }

            int child_y = cell_y + cmt;
            if (k.kontrolka->wyrownanie_pionowe() == WyrownaniePionowe::Srodek) {
                child_y += (av_h - child_h) / 2;
            } else if (k.kontrolka->wyrownanie_pionowe() == WyrownaniePionowe::Koniec) {
                child_y += av_h - child_h;
            }

            k.kontrolka->rozmiesc(Prostokat(child_x, child_y, child_w, child_h));
        }

        if (rozmiar_zmieniony) {
            obsluz_zmiane_rozmiaru();
        }
    }

private:
    DefinicjaWymiaru wiersze_[MAKS_WIERSZY]{};
    DefinicjaWymiaru kolumny_[MAKS_KOLUMN]{};
    size_t liczba_wierszy_{0};
    size_t liczba_kolumn_{0};

    Komorka komorki_[MAKS_KOMOREK]{};
    size_t liczba_komorek_{0};

    int odstep_x_{4};
    int odstep_y_{4};
};

} // namespace BursztynUI
