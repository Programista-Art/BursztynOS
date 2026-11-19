#pragma once

#include "kontrolka.h"
#include "czcionka.h"
#include "motyw.h"

namespace BursztynUI {

enum class TypWykresu : uint8_t {
    Linia,
    Slupki,
    Obszar
};

class Wykres : public Kontrolka {
public:
    static constexpr size_t MAKS_PUNKTOW = 64;
    static constexpr size_t MAKS_SERII = 4;

    struct SeriaDanych {
        int punkty[MAKS_PUNKTOW]{};
        size_t liczba_punktow{0};
        char nazwa[32]{};
        uint32_t kolor{0};
        bool widoczna{true};
    };

    explicit Wykres(TypWykresu typ = TypWykresu::Linia)
        : typ_(typ) {
        zaktualizuj_motyw();
        wypelnienie_ = Marginesy(10, 10, 10, 10);
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
        preferowany_rozmiar_ = Rozmiar(280, 180);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        Kontrolka::zaktualizuj_motyw();
    }

    TypWykresu typ_wykresu() const { return typ_; }
    void ustaw_typ_wykresu(TypWykresu t) {
        if (typ_ != t) {
            typ_ = t;
            uniewaznij();
        }
    }

    bool czy_auto_skalowanie() const { return auto_skalowanie_; }
    void ustaw_auto_skalowanie(bool a) {
        if (auto_skalowanie_ != a) {
            auto_skalowanie_ = a;
            uniewaznij();
        }
    }

    void ustaw_zakres(int min_y, int max_y) {
        reczny_min_ = min_y;
        reczny_max_ = max_y > min_y ? max_y : min_y + 1;
        auto_skalowanie_ = false;
        uniewaznij();
    }

    bool czy_pokazuje_siatke() const { return pokazuj_siatke_; }
    void ustaw_pokazywanie_siatki(bool s) {
        if (pokazuj_siatke_ != s) {
            pokazuj_siatke_ = s;
            uniewaznij();
        }
    }

    const char* tytul() const { return tytul_; }
    void ustaw_tytul(const char* t) {
        if (!t) t = "";
        size_t i = 0;
        while (t[i] && i < sizeof(tytul_) - 1) {
            tytul_[i] = t[i];
            ++i;
        }
        tytul_[i] = '\0';
        uniewaznij();
    }

    bool ustaw_punkty(size_t seria_idx, const int* wartosci, size_t liczba,
                      const char* nazwa_serii = nullptr, uint32_t kolor = 0) {
        if (seria_idx >= MAKS_SERII) return false;
        const bool nowa_seria = seria_idx >= liczba_serii_;
        if (nowa_seria) liczba_serii_ = seria_idx + 1;

        SeriaDanych& s = serie_[seria_idx];
        if (liczba > MAKS_PUNKTOW) liczba = MAKS_PUNKTOW;
        // Te same dane (np. plaska seria po przesunieciu historii): bez uniewaznienia i ponownego rysowania.
        bool bez_zmian = !nowa_seria && s.liczba_punktow == liczba && (kolor == 0 || s.kolor == kolor);
        for (size_t i = 0; bez_zmian && i < liczba; ++i) bez_zmian = s.punkty[i] == (wartosci ? wartosci[i] : 0);
        if (bez_zmian && nazwa_serii) {
            size_t i = 0;
            while (bez_zmian && i < sizeof(s.nazwa) - 1 && (nazwa_serii[i] || s.nazwa[i])) { bez_zmian = nazwa_serii[i] == s.nazwa[i]; ++i; }
        }
        if (bez_zmian) return true;
        s.liczba_punktow = liczba;
        for (size_t i = 0; i < liczba; ++i) {
            s.punkty[i] = wartosci ? wartosci[i] : 0;
        }

        if (nazwa_serii) {
            size_t i = 0;
            while (nazwa_serii[i] && i < sizeof(s.nazwa) - 1) {
                s.nazwa[i] = nazwa_serii[i];
                ++i;
            }
            s.nazwa[i] = '\0';
        }
        if (kolor != 0) s.kolor = kolor;

        uniewaznij();
        return true;
    }

    size_t liczba_serii() const { return liczba_serii_; }
    const SeriaDanych* seria(size_t i) const { return i < liczba_serii_ ? &serie_[i] : nullptr; }

    void wyczysc() {
        liczba_serii_ = 0;
        uniewaznij();
    }

    Rozmiar zmierz(Rozmiar dostepny) override {
        SkalaUI s = skala_dpi_;
        int w = 280 * s;
        int h = 180 * s;
        if (preferowany_rozmiar_.szerokosc >= 0) w = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) h = preferowany_rozmiar_.wysokosc * s;
        (void)dostepny;
        return Rozmiar(w, h);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());

        int pad_l = 36 * s; // Miejsce na etykiety osi Y
        int pad_t = tytul_[0] ? 24 * s : 10 * s;
        int pad_r = 16 * s;
        int pad_b = 20 * s; // Miejsce na oś X

        int plot_w = b.szerokosc - (pad_l + pad_r);
        int plot_h = b.wysokosc - (pad_t + pad_b);
        if (plot_w <= 10 || plot_h <= 10) return;

        // Tytuł
        if (tytul_[0]) {
            ctx.rysuj_tekst(Punkt(pad_l, 6 * s), tytul_, motyw_kolor(MOTYW_KOLOR_TEKST), s);
        }

        // Ustal min i max
        int min_val = 0;
        int max_val = 100;
        if (auto_skalowanie_) {
            bool first = true;
            for (size_t si = 0; si < liczba_serii_; ++si) {
                const SeriaDanych& ser = serie_[si];
                if (!ser.widoczna) continue;
                for (size_t pi = 0; pi < ser.liczba_punktow; ++pi) {
                    int v = ser.punkty[pi];
                    if (first) {
                        min_val = v; max_val = v; first = false;
                    } else {
                        if (v < min_val) min_val = v;
                        if (v > max_val) max_val = v;
                    }
                }
            }
            if (min_val > 0) min_val = 0;
            if (max_val <= min_val) max_val = min_val + 10;
        } else {
            min_val = reczny_min_;
            max_val = reczny_max_;
        }

        int range = max_val - min_val;
        if (range <= 0) range = 1;

        // Siatka i etykiety osi Y
        int linie_siatki = 4;
        uint32_t ramka_k = motyw_kolor(MOTYW_KOLOR_RAMKA);
        uint32_t tekst_k = motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI);

        for (int l = 0; l <= linie_siatki; ++l) {
            int gy = pad_t + (plot_h * l) / linie_siatki;
            if (pokazuj_siatke_) {
                ctx.rysuj_linie_pozioma(pad_l, gy, plot_w, ramka_k);
            }
            // Etykieta wartości
            int wartosc_l = max_val - (range * l) / linie_siatki;
            char lbl[16];
            formatuj_liczbe(lbl, sizeof(lbl), wartosc_l);
            int lw = CzcionkaUI::zmierz_tekst(lbl, s);
            ctx.rysuj_tekst(Punkt(pad_l - lw - 4 * s, gy - CzcionkaUI::pobierz_wysokosc(s) / 2),
                            lbl, tekst_k, s);
        }

        // Osie główne
        ctx.rysuj_linie_pionowa(pad_l, pad_t, plot_h, ramka_k);
        ctx.rysuj_linie_pozioma(pad_l, pad_t + plot_h, plot_w, ramka_k);

        // Znajdź maksymalną liczbę punktów w widocznych seriach
        size_t max_pts = 0;
        for (size_t si = 0; si < liczba_serii_; ++si) {
            if (serie_[si].widoczna && serie_[si].liczba_punktow > max_pts) {
                max_pts = serie_[si].liczba_punktow;
            }
        }
        if (max_pts < 2 && typ_ != TypWykresu::Slupki) return;

        // Rysuj serie
        static const uint32_t DOMYSLNE_KOLORY[MAKS_SERII] = {
            MOTYW_KOLOR_AKCENT,
            MOTYW_KOLOR_INFORMACJA,
            MOTYW_KOLOR_SUKCES,
            MOTYW_KOLOR_OSTRZEZENIE
        };

        for (size_t si = 0; si < liczba_serii_; ++si) {
            const SeriaDanych& ser = serie_[si];
            if (!ser.widoczna || ser.liczba_punktow == 0) continue;

            uint32_t c = ser.kolor != 0 ? ser.kolor : motyw_kolor(static_cast<MotywKolorRola>(DOMYSLNE_KOLORY[si % MAKS_SERII]));

            if (typ_ == TypWykresu::Slupki) {
                int total_slots = static_cast<int>(ser.liczba_punktow);
                int slot_w = plot_w / (total_slots > 0 ? total_slots : 1);
                int bar_w = (slot_w * 7) / 10;
                if (bar_w < 2) bar_w = 2;
                int bar_offset = (slot_w - bar_w) / 2;

                for (size_t pi = 0; pi < ser.liczba_punktow; ++pi) {
                    int val = ser.punkty[pi];
                    if (val < min_val) val = min_val;
                    if (val > max_val) val = max_val;

                    int bar_h = (static_cast<int64_t>(val - min_val) * plot_h) / range;
                    int bx = pad_l + static_cast<int>(pi) * slot_w + bar_offset;
                    int by = pad_t + plot_h - bar_h;

                    ctx.rysuj_prostokat(Prostokat(bx, by, bar_w, bar_h), c);
                    ctx.rysuj_ramke(Prostokat(bx, by, bar_w, bar_h), ramka_k, 1);
                }
            } else if (typ_ == TypWykresu::Obszar) {
                int prev_x = pad_l;
                int prev_y = pad_t + plot_h;

                for (size_t pi = 0; pi < ser.liczba_punktow; ++pi) {
                    int val = ser.punkty[pi];
                    if (val < min_val) val = min_val;
                    if (val > max_val) val = max_val;

                    int px = pad_l + (static_cast<int>(pi) * plot_w) / (static_cast<int>(ser.liczba_punktow - 1));
                    int py = pad_t + plot_h - (static_cast<int64_t>(val - min_val) * plot_h) / range;

                    if (pi > 0) {
                        // Wypełnij słupkami pionowymi od dołu wykresu
                        int x0 = prev_x;
                        int x1 = px;
                        for (int x = x0; x <= x1; ++x) {
                            int interp_y = prev_y + ((py - prev_y) * (x - x0)) / (x1 - x0 > 0 ? x1 - x0 : 1);
                            int strip_h = (pad_t + plot_h) - interp_y;
                            if (strip_h > 0) {
                                ctx.rysuj_linie_pionowa(x, interp_y, strip_h, c);
                            }
                        }
                    }
                    prev_x = px;
                    prev_y = py;
                }
                // Obrys linii na wierzchu obszaru
                for (size_t pi = 0; pi + 1 < ser.liczba_punktow; ++pi) {
                    int x0 = pad_l + (static_cast<int>(pi) * plot_w) / (static_cast<int>(ser.liczba_punktow - 1));
                    int y0 = pad_t + plot_h - (static_cast<int64_t>(ser.punkty[pi] - min_val) * plot_h) / range;
                    int x1 = pad_l + (static_cast<int>(pi + 1) * plot_w) / (static_cast<int>(ser.liczba_punktow - 1));
                    int y1 = pad_t + plot_h - (static_cast<int64_t>(ser.punkty[pi + 1] - min_val) * plot_h) / range;
                    rysuj_odcinek(ctx, x0, y0, x1, y1, motyw_kolor(MOTYW_KOLOR_TEKST));
                }
            } else { // TypWykresu::Linia
                for (size_t pi = 0; pi + 1 < ser.liczba_punktow; ++pi) {
                    int x0 = pad_l + (static_cast<int>(pi) * plot_w) / (static_cast<int>(ser.liczba_punktow - 1));
                    int y0 = pad_t + plot_h - (static_cast<int64_t>(ser.punkty[pi] - min_val) * plot_h) / range;
                    int x1 = pad_l + (static_cast<int>(pi + 1) * plot_w) / (static_cast<int>(ser.liczba_punktow - 1));
                    int y1 = pad_t + plot_h - (static_cast<int64_t>(ser.punkty[pi + 1] - min_val) * plot_h) / range;
                    rysuj_odcinek(ctx, x0, y0, x1, y1, c);
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(x0 - 2 * s, y0 - 2 * s, 5 * s, 5 * s), c, 2 * s);
                }
                if (ser.liczba_punktow > 0) {
                    size_t last = ser.liczba_punktow - 1;
                    int xn = pad_l + (static_cast<int>(last) * plot_w) / (static_cast<int>(ser.liczba_punktow - 1));
                    int yn = pad_t + plot_h - (static_cast<int64_t>(ser.punkty[last] - min_val) * plot_h) / range;
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(xn - 2 * s, yn - 2 * s, 5 * s, 5 * s), c, 2 * s);
                }
            }
        }
    }

private:
    static void formatuj_liczbe(char* b, size_t cap, int n) {
        if (!b || cap < 2) return;
        bool ujemna = n < 0;
        if (ujemna) n = -n;
        char tmp[16];
        size_t p = 0;
        do {
            tmp[p++] = static_cast<char>('0' + (n % 10));
            n /= 10;
        } while (n > 0 && p < sizeof(tmp));
        if (ujemna && p < sizeof(tmp)) tmp[p++] = '-';
        size_t o = 0;
        while (p > 0 && o + 1 < cap) b[o++] = tmp[--p];
        b[o] = '\0';
    }

    static void rysuj_odcinek(KontekstRysowania& ctx, int x0, int y0, int x1, int y1, uint32_t kolor) {
        // Algorytm Bresenhama całkowitoliczbowy
        int dx = x1 - x0; if (dx < 0) dx = -dx;
        int dy = y1 - y0; if (dy < 0) dy = -dy;
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            ctx.rysuj_prostokat(Prostokat(x0, y0, 2, 2), kolor);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    TypWykresu typ_{TypWykresu::Linia};
    bool auto_skalowanie_{true};
    bool pokazuj_siatke_{true};
    int reczny_min_{0};
    int reczny_max_{100};
    char tytul_[48]{};

    SeriaDanych serie_[MAKS_SERII]{};
    size_t liczba_serii_{0};
};

} // namespace BursztynUI
