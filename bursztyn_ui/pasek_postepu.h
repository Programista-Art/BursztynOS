#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class PasekPostepu : public Kontrolka {
public:
    using FunkcjaPostepu = void (*)(PasekPostepu*, int64_t, void*);

    explicit PasekPostepu(int64_t min = 0, int64_t max = 100)
        : minimum_(min), maksimum_(max), wartosc_(min) {
        normalizuj_zakres();
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = false;
        preferowany_rozmiar_ = Rozmiar(160, 22);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = 1;
        styl_.normalny.promien_zaokraglenia = 2;
        uniewaznij();
    }

    void ustaw_zakres(int64_t minimum, int64_t maksimum) {
        minimum_ = minimum;
        maksimum_ = maksimum;
        normalizuj_zakres();
        ustaw_wartosc_wewnetrzna(wartosc_, false);
        uniewaznij();
    }

    int64_t minimum() const { return minimum_; }
    int64_t maksimum() const { return maksimum_; }

    void ustaw_wartosc(int64_t w) {
        ustaw_wartosc_wewnetrzna(w, true);
    }

    int64_t wartosc() const { return wartosc_; }

    void ustaw_krok(int64_t k) {
        krok_ = (k < 1) ? 1 : k;
    }
    int64_t krok() const { return krok_; }

    void wykonaj_krok() {
        zwieksz_o(krok_);
    }

    void zwieksz_o(int64_t delta) {
        int64_t nowa = wartosc_ + delta;
        if (delta > 0 && nowa < wartosc_) nowa = maksimum_; // overflow guard
        if (delta < 0 && nowa > wartosc_) nowa = minimum_; // underflow guard
        if (nowa > maksimum_) nowa = maksimum_;
        if (nowa < minimum_) nowa = minimum_;
        ustaw_wartosc(nowa);
    }

    void ustaw_orientacje(Orientacja o) {
        if (orientacja_ != o) {
            orientacja_ = o;
            if (orientacja_ == Orientacja::Pionowa && preferowany_rozmiar_.szerokosc > preferowany_rozmiar_.wysokosc) {
                int tmp = preferowany_rozmiar_.szerokosc;
                preferowany_rozmiar_.szerokosc = preferowany_rozmiar_.wysokosc;
                preferowany_rozmiar_.wysokosc = tmp;
            }
            uniewaznij();
        }
    }
    Orientacja orientacja() const { return orientacja_; }

    void ustaw_pokazywanie_procentu(bool p) {
        if (pokazuj_procent_ != p) {
            pokazuj_procent_ = p;
            uniewaznij();
        }
    }
    bool czy_pokazuje_procent() const { return pokazuj_procent_; }

    void ustaw_tekst(const char* t) {
        if (!t) {
            tekst_[0] = '\0';
        } else {
            size_t i = 0;
            while (t[i] != '\0' && i < sizeof(tekst_) - 1) {
                tekst_[i] = t[i];
                ++i;
            }
            tekst_[i] = '\0';
        }
        uniewaznij();
    }
    const char* tekst() const { return tekst_; }

    void ustaw_pokazywanie_tekstu(bool t) {
        if (pokazuj_tekst_ != t) {
            pokazuj_tekst_ = t;
            uniewaznij();
        }
    }
    bool czy_pokazuje_tekst() const { return pokazuj_tekst_; }

    void ustaw_tryb_nieokreslony(bool n) {
        if (nieokreslony_ != n) {
            nieokreslony_ = n;
            pozycja_animacji_ = 0;
            kierunek_animacji_ = 1;
            uniewaznij();
        }
    }
    bool czy_tryb_nieokreslony() const { return nieokreslony_; }

    void ustaw_predkosc_animacji(int predkosc) {
        predkosc_animacji_ = (predkosc < 1) ? 1 : predkosc;
    }
    int predkosc_animacji() const { return predkosc_animacji_; }
    uint64_t liczba_krokow_animacji() const { return liczba_krokow_animacji_; }

    bool czy_wymaga_animacji() const override {
        return nieokreslony_ && czy_efektywnie_widoczna();
    }

    void krok_animacji(uint64_t teraz_ns = 0) override {
        if (!nieokreslony_ || !czy_efektywnie_widoczna()) {
            ostatni_krok_ns_ = 0;
            return;
        }
        if (teraz_ns != 0) {
            if (ostatni_krok_ns_ != 0 && (teraz_ns - ostatni_krok_ns_ < 33000000ULL)) {
                return;
            }
            ostatni_krok_ns_ = teraz_ns;
        }
        int max_pos = 100;
        pozycja_animacji_ += kierunek_animacji_ * predkosc_animacji_;
        if (pozycja_animacji_ >= max_pos) {
            pozycja_animacji_ = max_pos;
            kierunek_animacji_ = -1;
        } else if (pozycja_animacji_ <= 0) {
            pozycja_animacji_ = 0;
            kierunek_animacji_ = 1;
        }
        ++liczba_krokow_animacji_;
        uniewaznij();
    }

    int procent() const {
        if (maksimum_ == minimum_) return 100;
        int64_t val = wartosc_ - minimum_;
        int64_t den = maksimum_ - minimum_;
        if (den <= 0) return 100;
        if (val <= 0) return 0;
        if (val >= den) return 100;
        if (val < (0x7FFFFFFFFFFFFFFFLL / 100LL)) {
            return static_cast<int>((val * 100LL) / den);
        } else {
            int64_t den_100 = den / 100LL;
            return den_100 > 0 ? static_cast<int>(val / den_100) : 100;
        }
    }

    void przy_zmianie_wartosci(FunkcjaPostepu cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        uint32_t kolor_tla = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        uint32_t kolor_ramki = czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_RAMKA) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);
        uint32_t kolor_wypelnienia = czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);
        uint32_t kolor_tekstu = czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_TEKST) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);

        // Tor
        ctx.rysuj_prostokat(b, kolor_tla);

        int grubosc_ramki = 1;
        Prostokat wew(grubosc_ramki, grubosc_ramki,
                      b.szerokosc > 2 * grubosc_ramki ? b.szerokosc - 2 * grubosc_ramki : 0,
                      b.wysokosc > 2 * grubosc_ramki ? b.wysokosc - 2 * grubosc_ramki : 0);
        if (wew.szerokosc > 0 && wew.wysokosc > 0) {
            if (nieokreslony_) {
                if (orientacja_ == Orientacja::Pozioma) {
                    int seg_w = (wew.szerokosc * 30) / 100;
                    if (seg_w < 12) seg_w = 12;
                    int travel = wew.szerokosc - seg_w;
                    int x = (travel > 0) ? wew.x + (travel * pozycja_animacji_) / 100 : wew.x;
                    Prostokat seg(x, wew.y, seg_w, wew.wysokosc);
                    ctx.rysuj_prostokat(seg.przeciecie(wew), kolor_wypelnienia);
                } else {
                    int seg_h = (wew.wysokosc * 30) / 100;
                    if (seg_h < 12) seg_h = 12;
                    int travel = wew.wysokosc - seg_h;
                    int y = (travel > 0) ? wew.y + travel - (travel * pozycja_animacji_) / 100 : wew.y;
                    Prostokat seg(wew.x, y, wew.szerokosc, seg_h);
                    ctx.rysuj_prostokat(seg.przeciecie(wew), kolor_wypelnienia);
                }
            } else {
                int64_t zakres = maksimum_ - minimum_;
                if (zakres <= 0) {
                    ctx.rysuj_prostokat(wew, kolor_wypelnienia);
                } else {
                    int64_t d = wartosc_ - minimum_;
                    if (d < 0) d = 0;
                    if (d > zakres) d = zakres;
                    if (orientacja_ == Orientacja::Pozioma) {
                        int fill_w = 0;
                        if (d < (0x7FFFFFFFFFFFFFFFLL / static_cast<int64_t>(wew.szerokosc))) {
                            fill_w = static_cast<int>((d * static_cast<int64_t>(wew.szerokosc)) / zakres);
                        } else {
                            int64_t z_div = zakres / static_cast<int64_t>(wew.szerokosc);
                            fill_w = z_div > 0 ? static_cast<int>(d / z_div) : wew.szerokosc;
                        }
                        if (fill_w > 0) {
                            if (fill_w > wew.szerokosc) fill_w = wew.szerokosc;
                            Prostokat wyp(wew.x, wew.y, fill_w, wew.wysokosc);
                            ctx.rysuj_prostokat(wyp, kolor_wypelnienia);
                        }
                    } else {
                        int fill_h = 0;
                        if (d < (0x7FFFFFFFFFFFFFFFLL / static_cast<int64_t>(wew.wysokosc))) {
                            fill_h = static_cast<int>((d * static_cast<int64_t>(wew.wysokosc)) / zakres);
                        } else {
                            int64_t z_div = zakres / static_cast<int64_t>(wew.wysokosc);
                            fill_h = z_div > 0 ? static_cast<int>(d / z_div) : wew.wysokosc;
                        }
                        if (fill_h > 0) {
                            if (fill_h > wew.wysokosc) fill_h = wew.wysokosc;
                            Prostokat wyp(wew.x, wew.dolny() - fill_h, wew.szerokosc, fill_h);
                            ctx.rysuj_prostokat(wyp, kolor_wypelnienia);
                        }
                    }
                }
            }
        }

        // Tekst / Procent
        if (!nieokreslony_ && (pokazuj_procent_ || pokazuj_tekst_ || tekst_[0] != '\0')) {
            char buf[64];
            const char* do_wyswietlenia = tekst_;
            if (pokazuj_procent_ && tekst_[0] == '\0') {
                formatuj_procent(buf, sizeof(buf), procent());
                do_wyswietlenia = buf;
            }
            if (do_wyswietlenia[0] != '\0') {
                ctx.rysuj_tekst_wysrodkowany(b, do_wyswietlenia, kolor_tekstu, skala_dpi_);
            }
        }

        // Ramka
        ctx.rysuj_ramke(b, kolor_ramki, grubosc_ramki);
    }

private:
    void normalizuj_zakres() {
        if (maksimum_ < minimum_) {
            int64_t tmp = minimum_;
            minimum_ = maksimum_;
            maksimum_ = tmp;
        }
    }

    void ustaw_wartosc_wewnetrzna(int64_t w, bool zglos_callback) {
        if (w < minimum_) w = minimum_;
        if (w > maksimum_) w = maksimum_;
        if (wartosc_ != w) {
            wartosc_ = w;
            uniewaznij();
            if (zglos_callback && cb_zmiana_) {
                cb_zmiana_(this, wartosc_, dane_zmiany_);
            }
        }
    }

    static void formatuj_procent(char* buf, size_t max_len, int pct) {
        if (max_len < 6) return;
        size_t p = 0;
        if (pct >= 100) {
            buf[p++] = '1'; buf[p++] = '0'; buf[p++] = '0';
        } else if (pct >= 10) {
            buf[p++] = static_cast<char>('0' + (pct / 10));
            buf[p++] = static_cast<char>('0' + (pct % 10));
        } else {
            buf[p++] = static_cast<char>('0' + pct);
        }
        buf[p++] = '%';
        buf[p] = '\0';
    }

    int64_t minimum_{0};
    int64_t maksimum_{100};
    int64_t wartosc_{0};
    int64_t krok_{10};
    Orientacja orientacja_{Orientacja::Pozioma};
    bool pokazuj_procent_{false};
    bool pokazuj_tekst_{false};
    bool nieokreslony_{false};
    int pozycja_animacji_{0};
    int kierunek_animacji_{1};
    int predkosc_animacji_{4};
    uint64_t ostatni_krok_ns_{0};
    char tekst_[64]{};

    FunkcjaPostepu cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
    uint64_t liczba_krokow_animacji_{0};
};

} // namespace BursztynUI
