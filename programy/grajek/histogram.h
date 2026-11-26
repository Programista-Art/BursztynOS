#pragma once
/*
 * Grajek - histogram opoznien o stalej pamieci (diagnostyka ciaglosci dzwieku).
 *
 * Kubelki logarytmiczne o podstawie 2 w mikrosekundach: kubelek k obejmuje [2^k, 2^(k+1)) us (k = 0 obejmuje
 * takze 0 us), ostatni (LICZBA_KUBELKOW - 1) zbiera wszystko powyzej ~33,5 s. Percentyl jest zwracany jako GORNA
 * granica kubelka (zawyzenie najwyzej 2x, nigdy zanizenie), a minimum, maksimum i srednia sa dokladne.
 * Dodanie probki: kilka porownan i dodawan - bez alokacji, bez dzielenia 128-bit (Ring 3 bez libgcc).
 */
#include <stdint.h>

namespace Grajek {

class Histogram {
public:
    static constexpr int LICZBA_KUBELKOW = 26;

    void dodaj(uint64_t ns) {
        const uint64_t us = ns / 1000ULL;
        int k = 0;
        while (k < LICZBA_KUBELKOW - 1 && (us >> (k + 1)) != 0) ++k;
        ++kubelki_[k];
        ++liczba_;
        suma_ns_ = ns <= UINT64_MAX - suma_ns_ ? suma_ns_ + ns : UINT64_MAX;
        if (ns > maks_ns_) maks_ns_ = ns;
        if (liczba_ == 1 || ns < min_ns_) min_ns_ = ns;
    }
    void wyczysc() { *this = Histogram{}; }

    uint64_t liczba() const { return liczba_; }
    uint64_t min_ns() const { return liczba_ ? min_ns_ : 0; }
    uint64_t maks_ns() const { return maks_ns_; }
    uint64_t srednia_ns() const { return liczba_ ? suma_ns_ / liczba_ : 0; }
    // p w promilach (500 = mediana, 950 = p95, 990 = p99). Gorna granica kubelka w ns, ograniczona przez maksimum.
    uint64_t percentyl_ns(uint32_t promile) const {
        if (!liczba_) return 0;
        if (promile > 1000) promile = 1000;
        const uint64_t prog = (liczba_ * promile + 999) / 1000;   // najmniejsza liczba probek >= p
        uint64_t narast = 0;
        for (int k = 0; k < LICZBA_KUBELKOW; ++k) {
            narast += kubelki_[k];
            if (narast >= (prog ? prog : 1)) {
                const uint64_t gorna = k == LICZBA_KUBELKOW - 1 ? maks_ns_ : (uint64_t(2) << k) * 1000ULL;
                return gorna < maks_ns_ ? gorna : maks_ns_;
            }
        }
        return maks_ns_;
    }

private:
    uint32_t kubelki_[LICZBA_KUBELKOW]{};
    uint64_t liczba_ = 0, suma_ns_ = 0, maks_ns_ = 0, min_ns_ = 0;
};

} // namespace Grajek
