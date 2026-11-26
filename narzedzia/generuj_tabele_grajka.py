#!/usr/bin/env python3
"""Generuje programy/grajek/tabele_dsp.h - stale tabele DSP Grajka (bez liczb zmiennoprzecinkowych w Ring 3).

Zawartosc:
  * wspolczynniki 10-pasmowego korektora (RBJ "peaking EQ", Q = 1.41, fs = 48000 Hz) dla wzmocnien
    -12..+12 dB co 1 dB, znormalizowane (a0 = 1), stalopozycyjne Q28: b0, b1, b2, a1, a2,
  * tlumienie wstepne (preamp) Q15 dla 0..12 dB,
  * sinus Q15 dla FFT N = 512 i okno Hanna Q15 (N = 512),
  * progi amplitudy dla 0..-72 dB (wskazniki poziomu i widmo).
Uzycie: python3 narzedzia/generuj_tabele_grajka.py [plik_wyjsciowy]
Narzedzie deterministyczne (te same dane wejsciowe -> ten sam plik); nie nadpisuje innych plikow.
"""
import math
import sys

FS = 48000.0
PASMA = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
Q = 1.41
WZMOCNIENIA = range(-12, 13)
Q28 = 1 << 28


def biquad(f0, gain_db):
    a = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * f0 / FS
    alpha = math.sin(w0) / (2 * Q)
    b0 = 1 + alpha * a
    b1 = -2 * math.cos(w0)
    b2 = 1 - alpha * a
    a0 = 1 + alpha / a
    a1 = -2 * math.cos(w0)
    a2 = 1 - alpha / a
    return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0]


def q28(x):
    v = int(round(x * Q28))
    assert -(1 << 31) <= v < (1 << 31), x
    return v


def main(out):
    w = []
    w.append('#pragma once')
    w.append('/* WYGENEROWANE przez narzedzia/generuj_tabele_grajka.py - nie edytowac recznie. */')
    w.append('#include <stdint.h>')
    w.append('namespace Grajek::Tabele {')
    w.append('constexpr int LICZBA_PASM = %d;' % len(PASMA))
    w.append('constexpr int MIN_DB = -12, MAKS_DB = 12;')
    w.append('constexpr int CZESTOTLIWOSCI_PASM[LICZBA_PASM] = {%s};' % ', '.join(str(p) for p in PASMA))
    w.append('// [pasmo][dB + 12][b0, b1, b2, a1, a2] w Q28 (a0 = 1)')
    w.append('constexpr int32_t KOREKTOR[LICZBA_PASM][25][5] = {')
    for f in PASMA:
        wiersze = []
        for g in WZMOCNIENIA:
            wiersze.append('{%s}' % ', '.join(str(q28(c)) for c in biquad(f, g)))
        w.append('  {' + ', '.join(wiersze) + '},')
    w.append('};')
    w.append('// tlumienie wstepne 10^(-dB/20) w Q15 dla dB = 0..12')
    w.append('constexpr int32_t PREAMP_Q15[13] = {%s};' % ', '.join(str(int(round(32768 * 10 ** (-d / 20.0)))) for d in range(13)))
    n = 512
    w.append('constexpr int FFT_N = %d;' % n)
    w.append('// sin(2*pi*k/N) w Q15')
    w.append('constexpr int16_t SINUS_Q15[FFT_N] = {%s};' % ', '.join(str(max(-32767, min(32767, int(round(32767 * math.sin(2 * math.pi * k / n)))))) for k in range(n)))
    w.append('// okno Hanna w Q15')
    w.append('constexpr int16_t HANN_Q15[FFT_N] = {%s};' % ', '.join(str(int(round(32767 * (0.5 - 0.5 * math.cos(2 * math.pi * k / (n - 1)))))) for k in range(n)))
    w.append('// prog amplitudy (pelna skala 32767) dla 0, -1, ..., -72 dB')
    w.append('constexpr int32_t PROG_DB[73] = {%s};' % ', '.join(str(int(round(32767 * 10 ** (-d / 20.0)))) for d in range(73)))
    w.append('} // namespace Grajek::Tabele')
    tekst = '\n'.join(w) + '\n'
    with open(out, 'w', encoding='utf-8') as f:
        f.write(tekst)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'programy/grajek/tabele_dsp.h')
