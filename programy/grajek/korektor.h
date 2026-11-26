#pragma once
/*
 * Grajek - 10-pasmowy korektor graficzny (31 Hz .. 16 kHz), filtry szczytowe RBJ w arytmetyce stalopozycyjnej.
 *
 * Kazde pasmo to biquad w postaci bezposredniej I (DF1): wspolczynniki Q28 z tabeli (tabele_dsp.h), probki
 * z 8 bitami czesci ulamkowej (Q8), akumulator 64-bit z zaokraglaniem - stabilne takze dla 31 Hz przy 48 kHz.
 * Zapas (headroom): przy dodatnich wzmocnieniach sygnal jest najpierw tlumiony o najwieksze wzmocnienie
 * (preamp), a wynik nasycany do S16 - bez przepelnien i zawijania.
 * Pasmo z 0 dB jest pomijane, a wylaczony korektor (albo wszystkie pasma 0 dB) to DOKLADNE przejscie bit w bit.
 * Obliczenia wylacznie w Ring 3, przed wyslaniem PCM do BWS 110 (nigdy w przerwaniu HDA).
 */
#include "tabele_dsp.h"
#include "przetwarzanie.h"

namespace Grajek {

enum class PresetKorektora : uint8_t { Plaski = 0, Basy, Rock, Pop, Wokal, Wlasny, Liczba };

inline const char* nazwa_presetu(PresetKorektora p) {
    switch (p) {
        case PresetKorektora::Plaski: return "Płaski";
        case PresetKorektora::Basy: return "Basy";
        case PresetKorektora::Rock: return "Rock";
        case PresetKorektora::Pop: return "Pop";
        case PresetKorektora::Wokal: return "Wokal";
        case PresetKorektora::Wlasny: return "Własne ustawienia";
        default: return "?";
    }
}

inline const int8_t* wzmocnienia_presetu(PresetKorektora p) {
    static constexpr int8_t T[5][Tabele::LICZBA_PASM] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},           // Plaski
        {6, 5, 4, 2, 0, 0, 0, 0, 0, 0},           // Basy
        {4, 3, 2, 0, -1, -1, 0, 2, 3, 4},         // Rock
        {-1, 0, 2, 3, 4, 3, 1, 0, -1, -1},        // Pop
        {-2, -2, -1, 0, 2, 4, 4, 3, 1, 0},        // Wokal
    };
    const int i = int(p);
    return i >= 0 && i < 5 ? T[i] : nullptr;
}

class Korektor {
public:
    static constexpr int PASMA = Tabele::LICZBA_PASM;

    Korektor() { reset(); }

    void ustaw_wlaczony(bool w) { if (w != wlaczony_) { wlaczony_ = w; reset(); } }
    bool wlaczony() const { return wlaczony_; }

    // Wzmocnienie pasma w dB (przycinane do -12..+12).
    void ustaw_wzmocnienie(int pasmo, int db) {
        if (pasmo < 0 || pasmo >= PASMA) return;
        if (db < Tabele::MIN_DB) db = Tabele::MIN_DB;
        if (db > Tabele::MAKS_DB) db = Tabele::MAKS_DB;
        db_[pasmo] = int8_t(db);
        przelicz_preamp();
    }
    int wzmocnienie(int pasmo) const { return pasmo >= 0 && pasmo < PASMA ? db_[pasmo] : 0; }

    void ustaw_preset(PresetKorektora p) {
        const int8_t* t = wzmocnienia_presetu(p);
        if (!t) return;
        for (int i = 0; i < PASMA; ++i) db_[i] = t[i];
        przelicz_preamp();
    }
    // Ktory preset odpowiada biezacym wzmocnieniom (Wlasny, gdy zaden).
    PresetKorektora rozpoznaj_preset() const {
        for (int p = 0; p < 5; ++p) {
            const int8_t* t = wzmocnienia_presetu(PresetKorektora(p));
            bool ok = true;
            for (int i = 0; i < PASMA; ++i) if (t[i] != db_[i]) { ok = false; break; }
            if (ok) return PresetKorektora(p);
        }
        return PresetKorektora::Wlasny;
    }

    bool plaski() const { for (int i = 0; i < PASMA; ++i) if (db_[i]) return false; return true; }
    bool aktywny() const { return wlaczony_ && !plaski(); }

    void reset() { for (auto& p : stan_) for (auto& k : p) for (auto& v : k) v = 0; }

    // Przetwarza w miejscu `ramki` ramek stereo S16.
    void przetworz(int16_t* pcm, uint32_t ramki) {
        if (!aktywny() || !pcm) return;
        for (uint32_t f = 0; f < ramki; ++f) {
            for (int k = 0; k < 2; ++k) {
                int64_t x = (int64_t(pcm[2 * size_t(f) + k]) * preamp_q15_) >> 7;   // Q8 po tlumieniu
                for (int b = 0; b < PASMA; ++b) {
                    if (!db_[b]) continue;
                    const int32_t* c = Tabele::KOREKTOR[b][db_[b] - Tabele::MIN_DB];
                    int64_t* s = stan_[b][k];   // x1, x2, y1, y2 (Q8), e = reszta kwantyzacji
                    // Sprzezenie zwrotne bledu kwantyzacji (1. rzedu): reszta przesuniecia >> 28 trafia do nastepnej
                    // probki. Bez tego bieguny 31 Hz (|1 + a1 + a2| ~ 2e-5) utrzymuja "martwa strefe" - stala
                    // skladowa do ~100 LSB po zaniku sygnalu.
                    const int64_t acc = int64_t(c[0]) * x + int64_t(c[1]) * s[0] + int64_t(c[2]) * s[1]
                                      - int64_t(c[3]) * s[2] - int64_t(c[4]) * s[3] + s[4];
                    int64_t y = acc >> 28;                              // podloga (przesuniecie arytmetyczne)
                    s[4] = acc - y * (int64_t(1) << 28);                // 0 <= reszta < 2^28 (bez przesuniecia ujemnej)
                    // ograniczenie stanu: wyjscie i tak jest nasycane; chroni akumulator przed narastaniem
                    const int64_t lim = int64_t(1) << 26;
                    if (y > lim) { y = lim; s[4] = 0; }
                    if (y < -lim) { y = -lim; s[4] = 0; }
                    s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y;
                    x = y;
                }
                pcm[2 * size_t(f) + k] = nasycenie16((x + 128) >> 8);
            }
        }
    }

private:
    void przelicz_preamp() {
        int maks = 0;
        for (int i = 0; i < PASMA; ++i) if (db_[i] > maks) maks = db_[i];
        preamp_q15_ = Tabele::PREAMP_Q15[maks];
    }
    bool wlaczony_ = false;
    int8_t db_[PASMA]{};
    int32_t preamp_q15_ = 32768;
    int64_t stan_[PASMA][2][5]{};
};

} // namespace Grajek
