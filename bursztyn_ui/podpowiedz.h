#pragma once

#include "popup.h"
#include "czcionka.h"

namespace BursztynUI {

class Podpowiedz : public PowierzchniaPopup {
public:
    static constexpr size_t MAKS_TEKSTU = 256;
    static constexpr size_t MAKS_LINII = 16;

    Podpowiedz() {
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = false;
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_podpowiedzi;
        uniewaznij();
    }

    void ustaw_tekst(const char* t) {
        kopiuj_str(tekst_, sizeof(tekst_), t);
        przelicz_rozmiar();
        uniewaznij();
    }
    const char* tekst() const { return tekst_; }

    void ustaw_tytul(const char* t) {
        kopiuj_str(tytul_, sizeof(tytul_), t);
        przelicz_rozmiar();
        uniewaznij();
    }
    const char* tytul() const { return tytul_; }

    void ustaw_maksymalna_szerokosc(int max_w) {
        max_szerokosc_ = max_w > 60 ? max_w : 60;
        przelicz_rozmiar();
        uniewaznij();
    }
    int maksymalna_szerokosc() const { return max_szerokosc_; }

    Rozmiar obliczony_rozmiar() const { return obliczony_rozmiar_; }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        ctx.rysuj_prostokat(b, styl_.normalny.tlo);
        ctx.rysuj_ramke(b, styl_.normalny.ramka, 1);

        SkalaUI s = skala_dpi_;
        int y = 4 * s;

        // Tytuł jeśli obecny
        if (tytul_[0] != '\0') {
            ctx.rysuj_tekst(Punkt(6 * s, y), tytul_, motyw_kolor(MOTYW_KOLOR_AKCENT), s);
            y += 18 * s;
            ctx.rysuj_linie_pozioma(4 * s, y - 2 * s, b.szerokosc - 8 * s, motyw_kolor(MOTYW_KOLOR_RAMKA));
        }

        // Linie tekstu
        for (size_t i = 0; i < liczba_linii_; ++i) {
            char linia_buf[64];
            size_t len = linie_[i].dlugosc;
            if (len >= sizeof(linia_buf)) len = sizeof(linia_buf) - 1;
            for (size_t j = 0; j < len; ++j) {
                linia_buf[j] = tekst_[linie_[i].start + j];
            }
            linia_buf[len] = '\0';

            ctx.rysuj_tekst(Punkt(6 * s, y), linia_buf, motyw_kolor(MOTYW_KOLOR_TEKST), s);
            y += 16 * s;
        }
    }

private:
    struct ZakresLinii {
        size_t start{0};
        size_t dlugosc{0};
    };

    void przelicz_rozmiar() {
        SkalaUI s = skala_dpi_;
        liczba_linii_ = 0;
        int max_w = 0;

        if (tytul_[0] != '\0') {
            max_w = CzcionkaUI::zmierz_tekst(tytul_, s);
        }

        // Zawijanie tekstu
        size_t p = 0;
        size_t len = 0;
        while (tekst_[len] != '\0') ++len;

        int limit_px = max_szerokosc_ * s - 12 * s;

        while (p < len && liczba_linii_ < MAKS_LINII) {
            size_t line_start = p;
            size_t last_space = p;
            int cur_w = 0;

            while (p < len && tekst_[p] != '\n') {
                if (tekst_[p] == ' ') last_space = p;
                cur_w += 8 * s;
                if (cur_w > limit_px && last_space > line_start) {
                    p = last_space;
                    break;
                }
                ++p;
            }

            linie_[liczba_linii_].start = line_start;
            linie_[liczba_linii_].dlugosc = p - line_start;
            int lw = static_cast<int>(linie_[liczba_linii_].dlugosc) * 8 * s;
            if (lw > max_w) max_w = lw;
            ++liczba_linii_;

            if (p < len && (tekst_[p] == ' ' || tekst_[p] == '\n')) ++p;
        }

        int h = (tytul_[0] != '\0' ? 22 * s : 0) + static_cast<int>(liczba_linii_) * 16 * s + 8 * s;
        int w = max_w + 12 * s;
        if (w < 40 * s) w = 40 * s;
        obliczony_rozmiar_ = Rozmiar(w, h);
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

    char tekst_[MAKS_TEKSTU]{};
    char tytul_[64]{};
    int max_szerokosc_{240};
    ZakresLinii linie_[MAKS_LINII];
    size_t liczba_linii_{0};
    Rozmiar obliczony_rozmiar_{60, 24};
};

class ZarzadcaPodpowiedzi {
public:
    ZarzadcaPodpowiedzi() = default;
    ~ZarzadcaPodpowiedzi() {
        ukryj();
    }

    void ustaw_opoznienie_pokazania(uint64_t ms) { opoznienie_ms_ = ms; }
    uint64_t opoznienie_pokazania() const { return opoznienie_ms_; }

    void ustaw_czas_wyswietlania(uint64_t ms) { czas_wyswietlania_ms_ = ms; }
    uint64_t czas_wyswietlania() const { return czas_wyswietlania_ms_; }

    bool czy_widoczna() const { return podpowiedz_.czy_otwarta(); }
    Podpowiedz& podpowiedz() { return podpowiedz_; }

    void obsluz_ruch(ZarzadcaPopupow& popupy, Kontrolka* najechana, int kursor_ekran_x, int kursor_ekran_y, uint64_t timestamp = 0) {
        if (najechana != ostatnia_kontrolka_) {
            ukryj(&popupy);
            ostatnia_kontrolka_ = najechana;
            czas_wejscia_ = timestamp;
            kursor_x_ = kursor_ekran_x;
            kursor_y_ = kursor_ekran_y;
            if (opoznienie_ms_ != 0) return;
        }

        kursor_x_ = kursor_ekran_x;
        kursor_y_ = kursor_ekran_y;

        if (najechana && !podpowiedz_.czy_otwarta()) {
            const char* tip = najechana->podpowiedz();
            if (tip && tip[0] != '\0') {
                if (timestamp >= czas_wejscia_ + opoznienie_ms_ || opoznienie_ms_ == 0) {
                    pokaz(popupy, tip, kursor_ekran_x, kursor_ekran_y);
                }
            }
        }
    }

    void pokaz(ZarzadcaPopupow& popupy, const char* tekst, int kursor_ekran_x, int kursor_ekran_y, const char* tytul = nullptr) {
        if (!tekst || tekst[0] == '\0') return;
        ukryj(&popupy);
        podpowiedz_.ustaw_tytul(tytul);
        podpowiedz_.ustaw_tekst(tekst);
        Rozmiar sz = podpowiedz_.obliczony_rozmiar();

        Prostokat rect = popupy.umiesc_przy_punkcie_ekranu(kursor_ekran_x + 12, kursor_ekran_y + 18, sz);
        popupy.otworz(podpowiedz_, rect);
    }

    void ukryj(ZarzadcaPopupow* popupy = nullptr) {
        if (podpowiedz_.czy_otwarta()) {
            if (podpowiedz_.zarzadca()) {
                podpowiedz_.zarzadca()->zamknij(PowodZamknieciaPopup::Anulowanie);
            } else if (popupy) {
                popupy->zamknij(PowodZamknieciaPopup::Anulowanie);
            }
        }
    }

    void odrejestruj_kontrolke(Kontrolka* k) {
        if (ostatnia_kontrolka_ == k) {
            ukryj();
            reset();
        }
    }

    void odrejestruj_zarzadce(ZarzadcaPopupow& popupy) {
        if (podpowiedz_.zarzadca() == &popupy) {
            ukryj(&popupy);
        }
        reset();
    }

    void reset() {
        ostatnia_kontrolka_ = nullptr;
        czas_wejscia_ = 0;
    }

private:
    Podpowiedz podpowiedz_;
    Kontrolka* ostatnia_kontrolka_{nullptr};
    uint64_t czas_wejscia_{0};
    uint64_t opoznienie_ms_{500};
    uint64_t czas_wyswietlania_ms_{5000};
    int kursor_x_{0};
    int kursor_y_{0};
};

} // namespace BursztynUI
