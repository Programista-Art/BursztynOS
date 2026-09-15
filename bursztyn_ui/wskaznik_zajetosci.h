#pragma once

#include "kontrolka.h"
#include "motyw.h"

namespace BursztynUI {

class WskaznikZajetosci : public Kontrolka {
public:
    explicit WskaznikZajetosci(bool aktywny = false)
        : aktywny_(aktywny) {
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = false;
        preferowany_rozmiar_ = Rozmiar(32, 32);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = 0;
        styl_.normalny.ramka = 0;
        styl_.normalny.grubosc_ramki = 0;
        Kontrolka::zaktualizuj_motyw();
    }

    bool czy_aktywny() const { return aktywny_; }
    void start() {
        if (!aktywny_) {
            aktywny_ = true;
            uniewaznij();
        }
    }
    void stop() {
        if (aktywny_) {
            aktywny_ = false;
            uniewaznij();
        }
    }
    void reset() {
        faza_ = 0;
        uniewaznij();
    }

    bool czy_wymaga_animacji() const override {
        return aktywny_ && czy_efektywnie_widoczna();
    }

    void krok_animacji(uint64_t teraz_ns = 0) override {
        if (!aktywny_ || !czy_efektywnie_widoczna()) {
            ostatni_krok_ns_ = 0;
            return;
        }
        if (teraz_ns != 0) {
            // ~20-25 klatek na sekundę (co ok. 40-50 ms)
            if (ostatni_krok_ns_ != 0 && (teraz_ns - ostatni_krok_ns_ < 45000000ULL)) {
                return;
            }
            ostatni_krok_ns_ = teraz_ns;
        }
        faza_ = (faza_ + 1) % LICZBA_KROKOW;
        ++liczba_krokow_;
        uniewaznij();
    }

    uint64_t liczba_krokow() const { return liczba_krokow_; }

    Rozmiar zmierz(Rozmiar) override {
        SkalaUI s = skala_dpi_;
        int sz = 32 * s;
        if (preferowany_rozmiar_.szerokosc >= 0) sz = preferowany_rozmiar_.szerokosc * s;
        return Rozmiar(sz, sz);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        int cx = obszar_.szerokosc / 2;
        int cy = obszar_.wysokosc / 2;
        int r = (obszar_.szerokosc < obszar_.wysokosc ? obszar_.szerokosc : obszar_.wysokosc) / 2 - 4 * s;
        if (r < 4 * s) r = 4 * s;

        uint32_t kolor_baza = motyw_kolor(MOTYW_KOLOR_AKCENT);

        // 8 punktów na okręgu z predefiniowanymi trygonometrycznymi stałymi (całkowitoliczbowe)
        // Kąty: 0, 45, 90, 135, 180, 225, 270, 315 stopni
        // cos / sin przeskalowane o 1000:
        static const int cos1000[8] = { 1000,  707,    0, -707, -1000, -707,    0,  707 };
        static const int sin1000[8] = {    0,  707, 1000,  707,     0, -707, -1000, -707 };

        for (int i = 0; i < LICZBA_KROKOW; ++i) {
            int px = cx + (r * cos1000[i]) / 1000;
            int py = cy + (r * sin1000[i]) / 1000;

            // Faza określa jasność/wielkość kropki
            int od_fazy = (i - faza_ + LICZBA_KROKOW) % LICZBA_KROKOW;
            int kropka_r = (aktywny_ ? (2 + (od_fazy * 3) / LICZBA_KROKOW) : 2) * s;
            if (kropka_r < 1) kropka_r = 1;

            uint32_t c = kolor_baza;
            if (aktywny_) {
                // Skaluj jasność kropki
                uint8_t factor = static_cast<uint8_t>(60 + (195 * od_fazy) / LICZBA_KROKOW);
                uint32_t red = (((c >> 16) & 0xFF) * factor) / 255;
                uint32_t green = (((c >> 8) & 0xFF) * factor) / 255;
                uint32_t blue = ((c & 0xFF) * factor) / 255;
                c = (red << 16) | (green << 8) | blue;
            } else {
                c = motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);
            }

            ctx.rysuj_zaokraglony_prostokat(
                Prostokat(px - kropka_r / 2, py - kropka_r / 2, kropka_r, kropka_r),
                c, kropka_r / 2);
        }
    }

private:
    static constexpr int LICZBA_KROKOW = 8;
    bool aktywny_{false};
    int faza_{0};
    uint64_t ostatni_krok_ns_{0};
    uint64_t liczba_krokow_{0};
};

} // namespace BursztynUI
