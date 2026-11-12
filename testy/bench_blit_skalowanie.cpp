/*
 * Mikro-benchmark prymitywow skalowania (jadro/gui/skalowanie.h).
 *
 * Mierzy koszt na piksel docelowy dla trzech filtrow i dwoch kierunkow.
 * Nie jest to pomiar sprzetowy - to porownanie wzgledne algorytmow na tym
 * samym CPU i tym samym budzecie cache, wystarczajace do uzasadnienia
 * polityki skalowanie_filtr_dla().
 *
 * Wartosci sa deterministyczne dla danego hosta; w raporcie podajemy
 * zarowno ns/piksel, jak i stosunek do nearest.
 */
#include "jadro/gui/skalowanie.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr uint32_t SRC = 256U;
constexpr uint32_t DST = 256U;
constexpr uint32_t POWT = 40U;

std::vector<uint32_t> zrob_zrodlo() {
    std::vector<uint32_t> p(SRC * SRC);
    for (uint32_t y = 0; y < SRC; ++y) {
        for (uint32_t x = 0; x < SRC; ++x) {
            const uint32_t r = (x * 255U) / (SRC - 1);
            const uint32_t g = (y * 255U) / (SRC - 1);
            const uint32_t b = ((x ^ y) * 3U) & 0xFFU;
            p[y * SRC + x] = (r << 16) | (g << 8) | b;
        }
    }
    return p;
}

double mierz(uint32_t filtr, uint32_t dst_w, uint32_t dst_h,
             const SkalowanieZrodlo& z, std::vector<uint32_t>& out) {
    /* Rozgrzewka poza pomiarem. */
    for (uint32_t y = 0; y < dst_h; ++y)
        skalowanie_wiersz(out.data() + size_t(y) * dst_w, z, y, dst_w, dst_h, filtr);

    const auto start = std::chrono::steady_clock::now();
    for (uint32_t r = 0; r < POWT; ++r) {
        for (uint32_t y = 0; y < dst_h; ++y)
            skalowanie_wiersz(out.data() + size_t(y) * dst_w, z, y, dst_w, dst_h, filtr);
    }
    const auto stop = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return ns / (double(dst_w) * double(dst_h) * double(POWT));
}

}  // namespace

int main() {
    const auto piksele = zrob_zrodlo();
    SkalowanieZrodlo z{};
    z.piksele = piksele.data();
    z.szerokosc = SRC;
    z.wysokosc = SRC;

    /* 1:1 - sciezka kopii blokowej (bez matematyki probkowania). */
    std::vector<uint32_t> out(size_t(DST) * DST);
    const double kopia = mierz(SKALOWANIE_NEAREST, DST, DST, z, out);

    struct Przypadek { const char* nazwa; uint32_t filtr; uint32_t w; uint32_t h; };
    const Przypadek przypadki[] = {
        {"1:1     nearest ", SKALOWANIE_NEAREST, 256U, 256U},
        {"1:1     bilinear", SKALOWANIE_BILINEAR, 256U, 256U},
        {"2x      nearest ", SKALOWANIE_NEAREST, 512U, 512U},
        {"2x      bilinear", SKALOWANIE_BILINEAR, 512U, 512U},
        {"0.5x    box     ", SKALOWANIE_BOX, 128U, 128U},
        {"0.5x    bilinear", SKALOWANIE_BILINEAR, 128U, 128U},
        {"0.5x    nearest ", SKALOWANIE_NEAREST, 128U, 128U},
    };

    std::printf("[BENCH-SKALOWANIE] zrodlo=%ux%u powtorzen=%u\n", SRC, SRC, POWT);
    std::printf("[BENCH-SKALOWANIE] %s  ns/piksel  x_nearest\n", "przypadek");
    for (const auto& p : przypadki) {
        std::vector<uint32_t> buf(size_t(p.w) * p.h);
        const double ns = mierz(p.filtr, p.w, p.h, z, buf);
        const double rel = (p.filtr == SKALOWANIE_NEAREST && p.w == SRC)
                               ? 1.0
                               : ns / kopia;
        std::printf("[BENCH-SKALOWANIE] %s  %8.2f  %6.2f\n", p.nazwa, ns, rel);
    }

    /* Polityka: filtr wybrany automatycznie nie moze byc wolniejszy niz 2x nearest. */
    const uint32_t f2x = skalowanie_filtr_dla(SRC, SRC, 512U, 512U);
    const uint32_t f05 = skalowanie_filtr_dla(SRC, SRC, 128U, 128U);
    const uint32_t f11 = skalowanie_filtr_dla(SRC, SRC, 300U, 300U);
    std::printf("[BENCH-SKALOWANIE] polityka 2x=%u 0.5x=%u 1.17x=%u\n", f2x, f05, f11);
    if (f2x != SKALOWANIE_NEAREST || f05 != SKALOWANIE_BOX ||
        f11 != SKALOWANIE_BILINEAR) {
        std::printf("[BENCH-SKALOWANIE] FAIL: polityka filtrow niezgodna\n");
        return 1;
    }
    std::printf("[BENCH-SKALOWANIE] PASS\n");
    return 0;
}
