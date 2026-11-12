/*
 * Host oracle test for jadro/gui/skalowanie.h.
 *
 * Zakres:
 *   - mapowanie nearest / bilinear / box wzgledem niezaleznie policzonej
 *     matematyki zmiennoprzecinkowej (dopuszczalna tolerancja 1 jednostka),
 *   - zachowanie flag koloru (pusty, widoczna czern, alfa),
 *   - przyciecie i granice, brak zapisu poza buforem (kanarki),
 *   - 1:1 bit-identyczne z graphics_copy_pixels,
 *   - geometria dopasuj/wypelnij/rozciagnij.
 *
 * Kompilowany z -fsanitize=address,undefined, wiec kazde wyjscie poza bufor
 * oraz UB w arytmetyce staloprzecinkowej konczy test porazka.
 */
#include "jadro/gui/skalowanie.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool warunek, const char* opis) {
    ++g_checks;
    if (!warunek) {
        std::printf("FAIL: %s\n", opis);
        ++g_failures;
    }
}

void check_close(int a, int b, int tolerancja, const char* opis) {
    ++g_checks;
    const int roznica = a > b ? a - b : b - a;
    if (roznica > tolerancja) {
        std::printf("FAIL: %s (a=%d b=%d roznica=%d)\n", opis, a, b, roznica);
        ++g_failures;
    }
}

uint32_t zrob(uint32_t r, uint32_t g, uint32_t b) {
    return ((r & 0xFFU) << 16) | ((g & 0xFFU) << 8) | (b & 0xFFU);
}

SkalowanieZrodlo zrodlo_z(const std::vector<uint32_t>& piksele,
                          uint32_t w, uint32_t h) {
    SkalowanieZrodlo z{};
    z.piksele = piksele.data();
    z.szerokosc = w;
    z.wysokosc = h;
    return z;
}

/* ---- 1. Pozycja i indeks ---------------------------------------------- */

void test_pozycja() {
    /* 1:1 -> srodek kazdego piksela trafia w siebie. */
    for (uint32_t i = 0; i < 32U; ++i) {
        check(skalowanie_indeks_nearest(i, 32U, 32U) == i, "nearest 1:1");
        check_close(static_cast<int>(skalowanie_os(i, 32U, 32U).i0),
                    static_cast<int>(i), 0, "bilinear 1:1 i0");
        check(skalowanie_os(i, 32U, 32U).waga1 == 0U, "bilinear 1:1 waga");
    }

    /* 2x powiekszenie: 0,0,1,1,2,2,... */
    const uint32_t oczekiwane2x[8] = {0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U};
    for (uint32_t i = 0; i < 8U; ++i) {
        check(skalowanie_indeks_nearest(i, 8U, 4U) == oczekiwane2x[i],
              "nearest 2x");
    }

    /* Skrajne indeksy nigdy nie wychodza poza [0, src-1]. */
    for (uint32_t src = 1U; src <= 40U; ++src) {
        for (uint32_t dst = 1U; dst <= 40U; ++dst) {
            for (uint32_t i = 0; i < dst; ++i) {
                check(skalowanie_indeks_nearest(i, dst, src) < src,
                      "nearest w zakresie");
                const SkalowanieOs os = skalowanie_os(i, dst, src);
                check(os.i0 < src && os.i1 < src, "bilinear w zakresie");
                check(os.waga1 <= 65535U, "waga w zakresie");
            }
        }
    }

    /* Os x1 to i0+1 dla typowego przypadku bilinear (polowa odstepu). */
    const SkalowanieOs os = skalowanie_os(1U, 4U, 8U);
    check(os.i0 == 2U && os.i1 == 3U, "bilinear i0/i1");
    check_close(static_cast<int>(os.waga1), 32768, 0, "bilinear polowa wagi");
}

/* ---- 2. Nearest zgodny z niezaleznym oracle ---------------------------- */

void test_nearest_oracle() {
    constexpr uint32_t SRC = 7U;
    std::vector<uint32_t> piksele(SRC * SRC);
    for (uint32_t y = 0; y < SRC; ++y)
        for (uint32_t x = 0; x < SRC; ++x)
            piksele[y * SRC + x] = zrob(x * 30U, y * 30U, 0x40U);
    const SkalowanieZrodlo z = zrodlo_z(piksele, SRC, SRC);

    for (uint32_t dst = 1U; dst <= 20U; ++dst) {
        for (uint32_t dy = 0; dy < dst; ++dy) {
            for (uint32_t dx = 0; dx < dst; ++dx) {
                /* Oracle: srodek piksela docelowego w ukladzie zrodla. */
                const double fx = (dx + 0.5) * SRC / dst;
                const double fy = (dy + 0.5) * SRC / dst;
                int ox = static_cast<int>(std::floor(fx));
                int oy = static_cast<int>(std::floor(fy));
                if (ox >= static_cast<int>(SRC)) ox = static_cast<int>(SRC) - 1;
                if (oy >= static_cast<int>(SRC)) oy = static_cast<int>(SRC) - 1;
                const uint32_t oczekiwany = piksele[oy * SRC + ox];
                check(skalowanie_piksel_nearest(z, dx, dy, dst, dst) == oczekiwany,
                      "nearest vs oracle");
            }
        }
    }
}

/* ---- 3. Bilinear zgodny z oracle (tolerancja 1) ------------------------ */

void test_bilinear_oracle() {
    constexpr uint32_t SRC = 5U;
    std::vector<uint32_t> piksele(SRC * SRC);
    for (uint32_t y = 0; y < SRC; ++y)
        for (uint32_t x = 0; x < SRC; ++x)
            piksele[y * SRC + x] = zrob(20U + x * 40U, 30U + y * 40U, 90U);
    const SkalowanieZrodlo z = zrodlo_z(piksele, SRC, SRC);

    for (uint32_t dst = 2U; dst <= 12U; ++dst) {
        for (uint32_t dy = 0; dy < dst; ++dy) {
            for (uint32_t dx = 0; dx < dst; ++dx) {
                const double fx = (dx + 0.5) * SRC / dst - 0.5;
                const double fy = (dy + 0.5) * SRC / dst - 0.5;
                double cx = fx < 0 ? 0 : fx;
                double cy = fy < 0 ? 0 : fy;
                if (cx > SRC - 1) cx = SRC - 1;
                if (cy > SRC - 1) cy = SRC - 1;

                int x0 = static_cast<int>(std::floor(cx));
                int y0 = static_cast<int>(std::floor(cy));
                int x1 = x0 + 1;
                int y1 = y0 + 1;
                if (x1 >= static_cast<int>(SRC)) x1 = static_cast<int>(SRC) - 1;
                if (y1 >= static_cast<int>(SRC)) y1 = static_cast<int>(SRC) - 1;
                const double wx = cx - x0;
                const double wy = cy - y0;

                double acc[3] = {0.0, 0.0, 0.0};
                const int xs[2] = {x0, x1};
                const int ys[2] = {y0, y1};
                const double wagi[2][2] = {
                    {(1.0 - wx) * (1.0 - wy), wx * (1.0 - wy)},
                    {(1.0 - wx) * wy, wx * wy}
                };
                for (int a = 0; a < 2; ++a) {
                    for (int b = 0; b < 2; ++b) {
                        const uint32_t p = piksele[ys[a] * SRC + xs[b]];
                        acc[0] += wagi[a][b] * ((p >> 16) & 0xFFU);
                        acc[1] += wagi[a][b] * ((p >> 8) & 0xFFU);
                        acc[2] += wagi[a][b] * (p & 0xFFU);
                    }
                }

                const uint32_t wynik = skalowanie_piksel_bilinear(z, dx, dy, dst, dst);
                check(wynik != 0U, "bilinear niepusty");
                check_close(static_cast<int>((wynik >> 16) & 0xFFU),
                            static_cast<int>(std::lround(acc[0])), 1, "bilinear R");
                check_close(static_cast<int>((wynik >> 8) & 0xFFU),
                            static_cast<int>(std::lround(acc[1])), 1, "bilinear G");
                check_close(static_cast<int>(wynik & 0xFFU),
                            static_cast<int>(std::lround(acc[2])), 1, "bilinear B");
                /* Wszystkie probki maja alfa 255 -> wynik nieprzezroczysty. */
                check((wynik >> 24) == 0U, "bilinear opaque ma gorny bajt 0");
            }
        }
    }
}

/* ---- 4. Box: jednorodny kolor pozostaje jednorodny -------------------- */

void test_box() {
    constexpr uint32_t SRC = 8U;
    std::vector<uint32_t> piksele(SRC * SRC, zrob(10U, 20U, 30U));
    const SkalowanieZrodlo z = zrodlo_z(piksele, SRC, SRC);
    for (uint32_t dst = 1U; dst <= 8U; ++dst) {
        for (uint32_t dy = 0; dy < dst; ++dy) {
            for (uint32_t dx = 0; dx < dst; ++dx) {
                check(skalowanie_piksel_box(z, dx, dy, dst, dst) == zrob(10U, 20U, 30U),
                      "box jednorodny");
            }
        }
    }

    /* Kolumny naprzemienne: okno sredniej obszarowej zawsze obejmuje oba kolory. */
    for (uint32_t y = 0; y < SRC; ++y)
        for (uint32_t x = 0; x < SRC; ++x)
            piksele[y * SRC + x] = (x & 1U) ? zrob(255U, 0U, 0U) : zrob(0U, 0U, 255U);
    const SkalowanieZrodlo z2 = zrodlo_z(piksele, SRC, SRC);
    const uint32_t srodek = skalowanie_piksel_box(z2, 0U, 0U, 4U, 4U);
    check((srodek >> 16 & 0xFFU) > 0U && (srodek & 0xFFU) > 0U, "box mieszanka");

    /* Degradacja do nearest przy bardzo duzym oknie probkowania. */
    std::vector<uint32_t> duze(512U * 512U, zrob(1U, 2U, 3U));
    const SkalowanieZrodlo z3 = zrodlo_z(duze, 512U, 512U);
    check(skalowanie_piksel_box(z3, 0U, 0U, 1U, 1U) == skalowanie_piksel_nearest(z3, 0U, 0U, 1U, 1U),
          "box degraduje do nearest");
}

/* ---- 5. Piksele puste i alfa ----------------------------------------- */

void test_alfa() {
    std::vector<uint32_t> piksele(4U * 4U, 0U);
    const SkalowanieZrodlo z = zrodlo_z(piksele, 4U, 4U);
    check(skalowanie_piksel_nearest(z, 1U, 1U, 4U, 4U) == 0U, "pusty nearest");
    check(skalowanie_piksel_bilinear(z, 1U, 1U, 4U, 4U) == 0U, "pusty bilinear");
    check(skalowanie_piksel_box(z, 1U, 1U, 4U, 4U) == 0U, "pusty box");
    check(skalowanie_zloz(0U, 200U, 100U, 50U) == 0U, "zloz coverage 0");

    /* Marker widocznej czerni musi pozostac nieprzezroczysty. */
    std::vector<uint32_t> czern(1U, 0x01000000U);
    const SkalowanieZrodlo zc = zrodlo_z(czern, 1U, 1U);
    check(skalowanie_piksel_nearest(zc, 0U, 0U, 1U, 1U) == 0x01000000U,
          "marker czerni zachowany");

    /* Alfa 128 na jednolitym polu daje alfe ~128 (nie 255). */
    std::vector<uint32_t> alfa(4U * 4U, (128U << 24) | zrob(200U, 100U, 50U));
    const SkalowanieZrodlo za = zrodlo_z(alfa, 4U, 4U);
    const uint32_t w = skalowanie_piksel_bilinear(za, 1U, 1U, 4U, 4U);
    check_close(static_cast<int>(w >> 24), 128, 2, "alfa zachowana");
    check_close(static_cast<int>((w >> 16) & 0xFFU), 200, 1, "alfa R");
    /* Piksel o coverage pelnym nie ma gornego bajtu. */
    check((skalowanie_zloz(255U, 1U, 2U, 3U) >> 24) == 0U, "coverage 255 bez alfy");
}

/* ---- 6. Kanarki: brak zapisu poza wiersz ----------------------------- */

void test_kanarki() {
    constexpr uint32_t SRC = 9U;
    std::vector<uint32_t> piksele(SRC * SRC);
    for (size_t i = 0; i < piksele.size(); ++i) piksele[i] = zrob(i & 0xFFU, 0x10U, 0x20U);
    const SkalowanieZrodlo z = zrodlo_z(piksele, SRC, SRC);

    constexpr uint32_t DST = 13U;
    constexpr uint32_t KANAREK = 0xDEADBEEFU;
    std::vector<uint32_t> bufor(DST + 8U, KANAREK);
    for (uint32_t f = 0; f <= SKALOWANIE_BOX; ++f) {
        for (uint32_t y = 0; y < DST; ++y) {
            skalowanie_wiersz(bufor.data() + 4U, z, y, DST, DST, f);
            for (uint32_t i = 0; i < 4U; ++i) check(bufor[i] == KANAREK, "kanarek lewy");
            for (uint32_t i = 0; i < 4U; ++i)
                check(bufor[4U + DST + i] == KANAREK, "kanarek prawy");
        }
    }

    /* dst_w == 0 i puste zrodlo nie moga niczego zapisac. */
    std::vector<uint32_t> pusty(4U, KANAREK);
    SkalowanieZrodlo zle{};
    skalowanie_wiersz(pusty.data(), zle, 0U, 4U, 4U, SKALOWANIE_BILINEAR);
    for (uint32_t i = 0; i < 4U; ++i) check(pusty[i] == KANAREK, "puste zrodlo nie pisze");
    skalowanie_wiersz(pusty.data(), z, 0U, 0U, 4U, SKALOWANIE_BILINEAR);
    for (uint32_t i = 0; i < 4U; ++i) check(pusty[i] == KANAREK, "dst_w 0 nie pisze");
}

/* ---- 7. 1:1 bit-identyczne ------------------------------------------- */

void test_1do1() {
    constexpr uint32_t N = 257U;   /* nieparzyste, z ogonem kopii blokowej */
    std::vector<uint32_t> src(N), dst(N, 0xFFFFFFFFU);
    for (uint32_t i = 0; i < N; ++i) src[i] = 0x11223300U ^ (i * 2654435761U);
    skalowanie_kopia_1do1(dst.data(), src.data(), N);
    check(std::memcmp(src.data(), dst.data(), N * sizeof(uint32_t)) == 0,
          "kopia 1:1 bit-identyczna");
}

/* ---- 8. Geometria ---------------------------------------------------- */

void test_geometria() {
    /* 16:9 w kwadracie -> letterbox na dole i gorze. */
    SkalowanieProstokat p = skalowanie_dopasuj(1920U, 1080U, 400U, 400U, SKALOWANIE_DOPASUJ);
    check(p.szerokosc == 400 && p.wysokosc == 225, "dopasuj wymiary");
    check(p.y == 87 && p.x == 0, "dopasuj przesuniecie");

    /* To samo w trybie wypelnij -> pionowe pasy. */
    p = skalowanie_dopasuj(1920U, 1080U, 400U, 400U, SKALOWANIE_WYPELNIJ);
    check(p.szerokosc == 400 && p.wysokosc == 225, "wypelnij wymiary");

    /* Rozciagnij ignoruje proporcje. */
    p = skalowanie_dopasuj(1920U, 1080U, 400U, 300U, SKALOWANIE_ROZCIAGNIJ);
    check(p.szerokosc == 400 && p.wysokosc == 300 && p.x == 0 && p.y == 0,
          "rozciagnij");

    /* Pionowe zrodlo w poziomym obszarze. */
    p = skalowanie_dopasuj(1080U, 1920U, 400U, 400U, SKALOWANIE_DOPASUJ);
    check(p.wysokosc == 400 && p.szerokosc == 225, "dopasuj pion");
    check(p.x == 87 && p.y == 0, "dopasuj pion przesuniecie");

    /* Idealny kwadrat wypelnia bez marginesow. */
    p = skalowanie_dopasuj(100U, 100U, 400U, 400U, SKALOWANIE_DOPASUJ);
    check(p.szerokosc == 400 && p.wysokosc == 400 && p.x == 0 && p.y == 0,
          "kwadrat");

    /* Zdegenerowane wejscie zwraca zero wymiarow. */
    p = skalowanie_dopasuj(0U, 100U, 400U, 400U, SKALOWANIE_DOPASUJ);
    check(p.szerokosc == 0 && p.wysokosc == 0, "zero zrodla");
}

/* ---- 9. Wybor filtra ------------------------------------------------- */

void test_wybor_filtra() {
    check(skalowanie_filtr_dla(100U, 100U, 400U, 400U) == SKALOWANIE_NEAREST,
          "powiekszenie 2x+ -> nearest");
    check(skalowanie_filtr_dla(400U, 400U, 100U, 100U) == SKALOWANIE_BOX,
          "zmniejszenie 2x+ -> box");
    check(skalowanie_filtr_dla(400U, 400U, 350U, 350U) == SKALOWANIE_BILINEAR,
          "male skalowanie -> bilinear");
    check(skalowanie_filtr_dla(400U, 400U, 401U, 401U) == SKALOWANIE_BILINEAR,
          "male powiekszenie -> bilinear");
}

}  // namespace

int main() {
    test_pozycja();
    test_nearest_oracle();
    test_bilinear_oracle();
    test_box();
    test_alfa();
    test_kanarki();
    test_1do1();
    test_geometria();
    test_wybor_filtra();

    if (g_failures != 0) {
        std::printf("[BLIT-SKALOWANIE-HOST] FAIL %d/%d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("[BLIT-SKALOWANIE-HOST] PASS %d asercji\n", g_checks);
    return 0;
}
