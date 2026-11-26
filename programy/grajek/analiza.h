#pragma once
/*
 * Grajek - analiza sygnalu do wizualizacji: wskazniki poziomu stereo i widmo (FFT 512, okno Hanna).
 *
 * Zrodlem sa probki faktycznie wyslane do BWS 110 (po korektorze, przed glosnoscia strumienia) - wizualizacja
 * nie generuje losowych animacji. Probki sa jedynie kopiowane do pierscienia (tani koszt w goracej sciezce
 * audio); kosztowne FFT liczone jest tylko na zadanie interfejsu (<= kilkanascie razy na sekunde), gdy okno
 * jest widoczne i wizualizacja wlaczona. Arytmetyka calkowita (FFT radix-2 Q15 z blokowym skalowaniem).
 * Opoznienie: probki trafiaja tu w chwili zapisu do kolejki BWS 110, czyli do ~85 ms przed odtworzeniem.
 */
#include "tabele_dsp.h"
#include <stddef.h>
#include <stdint.h>

namespace Grajek {

constexpr int PASMA_WIDMA = 24;

class Analizator {
public:
    // Ostatnie probki stereo -> pierscien mono do FFT + szczyty kanalow od ostatniego odczytu.
    void dodaj(const int16_t* pcm, uint32_t ramki) {
        if (!wlaczony_ || !pcm) return;
        for (uint32_t i = 0; i < ramki; ++i) {
            const int32_t l = pcm[2 * size_t(i)], r = pcm[2 * size_t(i) + 1];
            const int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            if (al > szczyt_[0]) szczyt_[0] = al;
            if (ar > szczyt_[1]) szczyt_[1] = ar;
            suma_kw_[0] += uint64_t(int64_t(l) * l);
            suma_kw_[1] += uint64_t(int64_t(r) * r);
            ++liczba_;
            pierscien_[poz_] = int16_t((l + r) / 2);
            poz_ = (poz_ + 1) & (Tabele::FFT_N - 1);
        }
        nowe_ += ramki;
    }
    void ustaw_wlaczony(bool w) { wlaczony_ = w; if (!w) wyczysc(); }
    bool wlaczony() const { return wlaczony_; }
    void wyczysc() {
        for (auto& v : pierscien_) v = 0;
        szczyt_[0] = szczyt_[1] = 0; suma_kw_[0] = suma_kw_[1] = 0; liczba_ = 0; nowe_ = 0; poz_ = 0;
        for (auto& v : poziom_) v = 0;
        for (auto& v : widmo_) v = 0;
    }

    /*
     * Aktualizacja wartosci prezentowanych (0..100). Wolana z interfejsu. `opadanie` = punkty na odswiezenie
     * (plynny spadek). Gdy od ostatniego odczytu nie bylo nowych probek (pauza, cisza), wskazniki opadaja.
     */
    void odswiez(int opadanie = 6) {
        int cel[2] = {0, 0};
        for (int k = 0; k < 2; ++k) cel[k] = procent_z_amplitudy(szczyt_[k]);
        rms_[0] = liczba_ ? isqrt(suma_kw_[0] / liczba_) : 0;
        rms_[1] = liczba_ ? isqrt(suma_kw_[1] / liczba_) : 0;
        for (int k = 0; k < 2; ++k) {
            if (cel[k] >= poziom_[k]) poziom_[k] = cel[k];
            else poziom_[k] = poziom_[k] - opadanie > cel[k] ? poziom_[k] - opadanie : cel[k];
            szczyt_[k] = 0; suma_kw_[k] = 0;
        }
        liczba_ = 0;
        int nowe_widmo[PASMA_WIDMA] = {};
        if (nowe_) oblicz_widmo(nowe_widmo);
        for (int b = 0; b < PASMA_WIDMA; ++b) {
            if (nowe_widmo[b] >= widmo_[b]) widmo_[b] = nowe_widmo[b];
            else widmo_[b] = widmo_[b] - opadanie > nowe_widmo[b] ? widmo_[b] - opadanie : nowe_widmo[b];
        }
        nowe_ = 0;
    }

    int poziom(int kanal) const { return kanal == 0 || kanal == 1 ? poziom_[kanal] : 0; }
    int32_t rms(int kanal) const { return kanal == 0 || kanal == 1 ? rms_[kanal] : 0; }
    int widmo(int pasmo) const { return pasmo >= 0 && pasmo < PASMA_WIDMA ? widmo_[pasmo] : 0; }

    // 0 dB -> 100, -60 dB i mniej -> 0 (liniowo w dB).
    static int procent_z_amplitudy(int32_t a) {
        if (a <= 0) return 0;
        for (int d = 0; d <= 60; ++d) if (a >= Tabele::PROG_DB[d]) return 100 - (d * 100) / 60;
        return 0;
    }
    static int32_t isqrt(uint64_t v) {
        uint64_t r = 0, bit = 1ULL << 62;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
            bit >>= 2;
        }
        return int32_t(r);
    }

    // Widmo z ostatnich FFT_N probek pierscienia; wynik 0..100 na pasmo (logarytmiczne pasma 40 Hz..20 kHz).
    void oblicz_widmo(int* wynik) const {
        constexpr int N = Tabele::FFT_N;
        int32_t re[N], im[N];
        for (int i = 0; i < N; ++i) {
            const int32_t s = pierscien_[(poz_ + i) & (N - 1)];
            re[i] = (s * Tabele::HANN_Q15[i]) >> 15;
            im[i] = 0;
        }
        int wykladnik = fft(re, im);
        // granice pasm (indeksy binow) - rozklad logarytmiczny; bin = f * N / 48000
        static constexpr int16_t GRANICE[PASMA_WIDMA + 1] = {1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16, 19, 23, 28, 34, 41, 50, 61, 74, 90, 110, 134, 163, 199, 256};
        for (int b = 0; b < PASMA_WIDMA; ++b) {
            int64_t maks = 0;
            for (int k = GRANICE[b]; k < GRANICE[b + 1] && k < N / 2; ++k) {
                const int64_t m = int64_t(re[k]) * re[k] + int64_t(im[k]) * im[k];
                if (m > maks) maks = m;
            }
            // amplituda bin-u przeskalowana do pelnej skali probki: |X| * 2^wykladnik * 2 / (N/2) (okno Hanna ~ 1/2)
            int64_t amp = isqrt(uint64_t(maks));
            amp = wykladnik >= 0 ? amp << wykladnik : amp >> (-wykladnik);
            amp = amp * 4 / N;
            wynik[b] = procent_z_amplitudy(int32_t(amp > 0x7FFFFFFF ? 0x7FFFFFFF : amp));
        }
    }

private:
    // FFT radix-2 w miejscu, Q15, skalowanie 1/2 na etapie (zwraca laczny wykladnik skali = log2 N).
    static int fft(int32_t* re, int32_t* im) {
        constexpr int N = Tabele::FFT_N;
        for (int i = 1, j = 0; i < N; ++i) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { int32_t t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
        }
        int skala = 0;
        for (int len = 2; len <= N; len <<= 1) {
            const int krok = N / len;
            for (int i = 0; i < N; i += len) {
                for (int k = 0; k < len / 2; ++k) {
                    const int32_t wr = Tabele::SINUS_Q15[(k * krok + N / 4) & (N - 1)];   // cos
                    const int32_t wi = -Tabele::SINUS_Q15[(k * krok) & (N - 1)];          // -sin
                    const int32_t ar = re[i + k + len / 2], ai = im[i + k + len / 2];
                    const int32_t tr = int32_t((int64_t(ar) * wr - int64_t(ai) * wi) >> 15);
                    const int32_t ti = int32_t((int64_t(ar) * wi + int64_t(ai) * wr) >> 15);
                    const int32_t ur = re[i + k], ui = im[i + k];
                    re[i + k] = (ur + tr) >> 1; im[i + k] = (ui + ti) >> 1;
                    re[i + k + len / 2] = (ur - tr) >> 1; im[i + k + len / 2] = (ui - ti) >> 1;
                }
            }
            ++skala;
        }
        return skala;
    }

    bool wlaczony_ = true;
    int16_t pierscien_[Tabele::FFT_N]{};
    uint32_t poz_ = 0;
    int32_t szczyt_[2]{};
    uint64_t suma_kw_[2]{};
    uint64_t liczba_ = 0, nowe_ = 0;
    int poziom_[2]{};
    int32_t rms_[2]{};
    int widmo_[PASMA_WIDMA]{};
};

} // namespace Grajek
