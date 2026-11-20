#pragma once
/*
 * Bursztyn OS - glosnosc systemowa HDA (czysta logika: sterownik + testy hosta).
 *
 * Backend 1 (preferowany): wzmacniacz wyjsciowy kodeka na aktywnej trasie (SET_AMP_GAIN_MUTE, verb 0x3). Poziom
 * 100 % = wzmocnienie, ktore sterownik i tak ustawial przy konfiguracji trasy (konserwatywne -18 dB albo 0x4A
 * dla potwierdzonego kodeka QEMU) - glosnosc domyslna nie zmienia sie wzgledem dotychczasowego odtwarzania.
 * 0..100 % mapuje sie liniowo na kroki 0..gain_ref (kroki wzmacniacza HDA sa w dB). Wyciszenie = bit Mute
 * (tylko gdy Amp Caps ma Mute Capable); poziom 0 z Mute Capable takze ustawia Mute.
 *
 * Backend 2 (gdy trasa nie ma regulowanego wzmacniacza): programowe wzmocnienie gotowego okresu PCM
 * przed publikacja dla DMA - skaluj_probke() z nasyceniem do int16. Przy backendzie 1 skalowanie
 * programowe jest tozsamoscia (bez podwojnego tlumienia).
 */
#include <stdint.h>

namespace HdaGlosnosc {

inline constexpr uint32_t AMP_MUTE_CAPABLE = 1U << 31;
inline constexpr uint16_t AMP_OUT_LR = (1U << 15) | (1U << 13) | (1U << 12);   // SET_OUTPUT | LEFT | RIGHT
inline constexpr uint16_t AMP_MUTE = 1U << 7;
/* Amp Caps kodeka QEMU hda-output/duplex: Mute, Offset 0x4A, NumSteps 0x4A, StepSize 3. */
inline constexpr uint32_t QEMU_AMP_CAPS = AMP_MUTE_CAPABLE | (3U << 16) | (0x4AU << 8) | 0x4AU;

inline constexpr uint8_t kroki(uint32_t caps) { return uint8_t((caps >> 8) & 0x7F); }

struct Wzmacniacz {
    bool jest = false;
    uint8_t cad = 0, nid = 0;
    uint8_t gain_ref = 0;      // krok zapisany przy konfiguracji trasy = 100 %
    uint32_t caps = 0;
    bool regulowany() const { return jest && kroki(caps) > 0 && gain_ref > 0; }
    bool wyciszanie() const { return jest && (caps & AMP_MUTE_CAPABLE) != 0; }
};

struct Stan {
    uint32_t procent = 100;
    bool wyciszony = false;
    uint32_t generacja = 0;
};

inline constexpr uint32_t ogranicz(uint64_t procent) { return procent > 100 ? 100U : uint32_t(procent); }

/* Krok wzmacniacza dla poziomu (zaokraglenie do najblizszego; 100 % = gain_ref). */
inline uint8_t krok(const Wzmacniacz& w, uint32_t procent) {
    procent = ogranicz(procent);
    uint32_t g = (uint32_t(w.gain_ref) * procent + 50U) / 100U;
    const uint32_t maks = kroki(w.caps);
    if (g > maks) g = maks;
    return uint8_t(g & 0x7F);
}

/* Payload SET_AMP_GAIN_MUTE (wyjscie, L+P) dla stanu. */
inline uint16_t payload(const Wzmacniacz& w, const Stan& s) {
    const bool mute = w.wyciszanie() && (s.wyciszony || ogranicz(s.procent) == 0);
    return uint16_t(AMP_OUT_LR | (mute ? AMP_MUTE : 0) | krok(w, s.procent));
}

/* Payload dla wzmacniacza, ktory nie jest kontrolka glosnosci (pozostale wezly trasy): bez zmian wzgledem dotad. */
inline uint16_t payload_staly(uint8_t gain) { return uint16_t(AMP_OUT_LR | (gain & 0x7F)); }

/* Programowe wzmocnienie probki (backend PCM): nasycenie do int16, wyciszenie = cisza. */
inline int16_t skaluj_probke(int32_t v, const Stan& s, bool programowe) {
    if (!programowe) {
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        return int16_t(v);
    }
    if (s.wyciszony) return 0;
    const int64_t r = int64_t(v) * int64_t(ogranicz(s.procent)) / 100;
    if (r > 32767) return 32767;
    if (r < -32768) return -32768;
    return int16_t(r);
}

}  // namespace HdaGlosnosc
