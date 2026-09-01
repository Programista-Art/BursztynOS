#pragma once

#include <stdint.h>
#include "typy.h"

namespace BursztynUI {

// Niezalezny od renderowania model przesuniecia. Maksimum oznacza najwieksze
// dozwolone przesuniecie, a rozmiar_obszaru - widoczny fragment zawartosci.
class ModelPrzewijania {
public:
    using FunkcjaZmiany = void (*)(ModelPrzewijania* model, int64_t wartosc,
                                   void* dane_uzytkownika);

    void ustaw_zakres(int64_t minimum, int64_t maksimum) {
        if (minimum > maksimum) {
            int64_t tmp = minimum;
            minimum = maksimum;
            maksimum = tmp;
        }
        if (minimum_ == minimum && maksimum_ == maksimum) return;
        minimum_ = minimum;
        maksimum_ = maksimum;
        ustaw_wartosc_wewnetrznie(wartosc_);
        powiadom_o_geometrii();
    }

    int64_t minimum() const { return minimum_; }
    int64_t maksimum() const { return maksimum_; }

    bool ustaw_wartosc(int64_t wartosc) {
        return ustaw_wartosc_wewnetrznie(wartosc);
    }

    int64_t wartosc() const { return wartosc_; }

    void ustaw_rozmiar_obszaru(int64_t rozmiar) {
        if (rozmiar < 0) rozmiar = 0;
        if (rozmiar_obszaru_ == rozmiar) return;
        rozmiar_obszaru_ = rozmiar;
        powiadom_o_geometrii();
    }

    int64_t rozmiar_obszaru() const { return rozmiar_obszaru_; }

    void ustaw_maly_krok(int64_t krok) {
        maly_krok_ = bezpieczna_wartosc_bezwzgledna(krok, 1);
    }

    int64_t maly_krok() const { return maly_krok_; }

    void ustaw_duzy_krok(int64_t krok) {
        duzy_krok_ = bezpieczna_wartosc_bezwzgledna(krok, 1);
    }

    int64_t duzy_krok() const { return duzy_krok_; }

    bool przewin_o(int64_t delta) {
        return ustaw_wartosc_wewnetrznie(dodaj_nasycajaco(wartosc_, delta));
    }

    bool przewin_maly_krok(int kierunek) {
        return przewin_o(kierunek < 0 ? -maly_krok_ : maly_krok_);
    }

    bool przewin_strone(int kierunek) {
        return przewin_o(kierunek < 0 ? -duzy_krok_ : duzy_krok_);
    }

    void przy_zmianie(FunkcjaZmiany cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
    }

    // Osobne powiadomienie pozwala widokom przeliczyc uchwyt bez udawania,
    // ze zmienilo sie przesuniecie.
    void przy_zmianie_geometrii(FunkcjaZmiany cb, void* dane = nullptr) {
        cb_geometria_ = cb;
        dane_geometrii_ = dane;
    }

    static int64_t dodaj_nasycajaco(int64_t a, int64_t b) {
        SzerokaLiczba suma = static_cast<SzerokaLiczba>(a) + static_cast<SzerokaLiczba>(b);
        if (suma > static_cast<SzerokaLiczba>(INT64_MAX)) return INT64_MAX;
        if (suma < static_cast<SzerokaLiczba>(INT64_MIN)) return INT64_MIN;
        return static_cast<int64_t>(suma);
    }

private:
    static int64_t bezpieczna_wartosc_bezwzgledna(int64_t n, int64_t gdy_zero) {
        if (n == 0) return gdy_zero;
        if (n == INT64_MIN) return INT64_MAX;
        return n < 0 ? -n : n;
    }

    int64_t ogranicz(int64_t wartosc) const {
        if (wartosc <= minimum_) return minimum_;
        if (wartosc >= maksimum_) return maksimum_;
        return wartosc;
    }

    bool ustaw_wartosc_wewnetrznie(int64_t wartosc) {
        int64_t nowa = ogranicz(wartosc);
        if (nowa == wartosc_) return false;
        wartosc_ = nowa;
        if (cb_zmiana_) cb_zmiana_(this, wartosc_, dane_zmiany_);
        return true;
    }

    void powiadom_o_geometrii() {
        if (cb_geometria_) cb_geometria_(this, wartosc_, dane_geometrii_);
    }

    int64_t minimum_{0};
    int64_t maksimum_{100};
    int64_t wartosc_{0};
    int64_t rozmiar_obszaru_{10};
    int64_t maly_krok_{1};
    int64_t duzy_krok_{10};
    FunkcjaZmiany cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
    FunkcjaZmiany cb_geometria_{nullptr};
    void* dane_geometrii_{nullptr};
};

} // namespace BursztynUI
