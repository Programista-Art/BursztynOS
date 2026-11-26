#pragma once
/*
 * Grajek - konwersja do formatu wyjscia BWS 110: 48000 Hz, stereo, S16 przeplatane.
 *
 * Mono -> stereo: ta sama probka w obu kanalach. Zmiana czestotliwosci: interpolacja Hermite'a (Catmull-Rom,
 * 4 punkty) w arytmetyce calkowitej (pozycja 32.32, wspolczynniki Q16, akumulator 64-bit, nasycenie do S16).
 * Dla 48000 Hz potok jest DOKLADNYM przejsciem (bit w bit), bez interpolacji. Stan miedzy wywolaniami jest
 * zachowany, wiec granice blokow nie powoduja duplikatow ani pominiec probek; reset() przy przewijaniu
 * i zmianie utworu usuwa historie (zadna probka poprzedniego fragmentu nie trafia do nowego).
 *
 * Ograniczenie: przy zmniejszaniu czestotliwosci (np. 96 kHz -> 48 kHz) nie ma filtru antyaliasingowego.
 */
#include <stddef.h>
#include <stdint.h>

namespace Grajek {

constexpr uint32_t CZESTOTLIWOSC_WYJSCIA = 48000;

inline int16_t nasycenie16(int64_t v) { return v > 32767 ? int16_t(32767) : v < -32768 ? int16_t(-32768) : int16_t(v); }

class Resampler {
public:
    // false dla niepoprawnych parametrow (czestotliwosc 1..384000, kanaly 1..2).
    bool ustaw(uint32_t czestotliwosc_wejscia, uint16_t kanaly) {
        if (!czestotliwosc_wejscia || czestotliwosc_wejscia > 384000 || kanaly < 1 || kanaly > 2) return false;
        we_ = czestotliwosc_wejscia;
        kanaly_ = kanaly;
        krok_ = (uint64_t(we_) << 32) / CZESTOTLIWOSC_WYJSCIA;
        reset();
        return true;
    }
    void reset() {
        t_ = 0;
        wypelniony_ = false;
        for (auto& k : h_) for (auto& v : k) v = 0;
    }
    bool przejscie() const { return we_ == CZESTOTLIWOSC_WYJSCIA; }
    uint32_t czestotliwosc_wejscia() const { return we_; }
    uint16_t kanaly() const { return kanaly_; }

    /*
     * Przetwarza wejscie (przeplatane, `kanaly` kanalow) do wyjscia stereo. Zatrzymuje sie, gdy wyjscie jest pelne
     * albo wejscie sie skonczylo. *zuzyte = ramki wejscia pobrane (pozostale nalezy podac w nastepnym wywolaniu).
     * Zwraca liczbe ramek wyjsciowych.
     */
    uint32_t przetworz(const int16_t* we, uint32_t ramki_we, uint32_t* zuzyte, int16_t* wy, uint32_t pojemnosc_wy) {
        uint32_t i = 0, o = 0;
        if (!we_ || !we || !wy) { if (zuzyte) *zuzyte = 0; return 0; }
        if (przejscie()) {
            const uint32_t n = ramki_we < pojemnosc_wy ? ramki_we : pojemnosc_wy;
            for (uint32_t k = 0; k < n; ++k) {
                const int16_t l = we[size_t(k) * kanaly_];
                wy[2 * size_t(k)] = l;
                wy[2 * size_t(k) + 1] = kanaly_ == 2 ? we[size_t(k) * 2 + 1] : l;
            }
            if (zuzyte) *zuzyte = n;
            return n;
        }
        constexpr uint64_t JEDEN = 1ULL << 32;
        while (o < pojemnosc_wy) {
            if (!wypelniony_) {
                if (i >= ramki_we) break;
                for (int k = 0; k < 2; ++k) {
                    const int16_t s = we[size_t(i) * kanaly_ + (kanaly_ == 2 ? k : 0)];
                    h_[k][0] = h_[k][1] = h_[k][2] = h_[k][3] = s;
                }
                ++i;
                t_ = 2 * JEDEN;            // potrzebne jeszcze dwie probki, aby h1 = x0, h2 = x1, h3 = x2
                wypelniony_ = true;
            }
            bool brak = false;
            while (t_ >= JEDEN) {
                if (i >= ramki_we) { brak = true; break; }
                for (int k = 0; k < 2; ++k) {
                    h_[k][0] = h_[k][1]; h_[k][1] = h_[k][2]; h_[k][2] = h_[k][3];
                    h_[k][3] = we[size_t(i) * kanaly_ + (kanaly_ == 2 ? k : 0)];
                }
                ++i;
                t_ -= JEDEN;
            }
            if (brak) break;
            const int64_t f = int64_t(t_ >> 16);   // Q16
            for (int k = 0; k < 2; ++k) {
                const int64_t a = h_[k][0], b = h_[k][1], c = h_[k][2], d = h_[k][3];
                const int64_t c3 = (d - a) + 3 * (b - c);       // *2
                const int64_t c2 = 2 * a - 5 * b + 4 * c - d;   // *2
                const int64_t c1 = c - a;                       // *2
                int64_t acc = c3;
                acc = ((acc * f) >> 16) + c2;
                acc = ((acc * f) >> 16) + c1;
                acc = (acc * f) >> 16;
                wy[2 * size_t(o) + k] = nasycenie16(b + (acc >= 0 ? (acc + 1) / 2 : -((-acc + 1) / 2)));
            }
            ++o;
            t_ += krok_;
        }
        if (zuzyte) *zuzyte = i;
        return o;
    }

    // Liczba ramek wyjsciowych odpowiadajaca `ramki` ramkom wejscia (pozycja w utworze -> czas wyjscia).
    static uint64_t ramki_wyjscia(uint64_t ramki, uint32_t czestotliwosc) {
        if (!czestotliwosc) return 0;
        return ramki * CZESTOTLIWOSC_WYJSCIA / czestotliwosc;
    }

private:
    uint32_t we_ = 0;
    uint16_t kanaly_ = 0;
    uint64_t krok_ = 0, t_ = 0;
    bool wypelniony_ = false;
    int32_t h_[2][4]{};
};

} // namespace Grajek
