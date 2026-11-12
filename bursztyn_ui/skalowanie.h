#pragma once
#include <stdint.h>
#include <stdbool.h>

/*
 * Bursztyn OS - helpery skalowania dla aplikacji Ring 3.
 *
 * Ten naglowek celowo powiela minimalna czesc matematyki z
 * jadro/gui/skalowanie.h. Powod: biblioteki Ring 3 nie moga includowac
 * naglowkow jadra (inne flagi, inna warstwa ABI). Zasada jest jednak ta sama:
 *
 *   - zero float/double (Ring 3 tez jest budowane z -mno-sse/-mno-sse2),
 *   - zero alokacji,
 *   - mapowanie w stalym punkcie 16.16 na int64_t,
 *   - semantyka koloru identyczna z kompozytorem.
 *
 * Wszystko jest inline, wiec w kodzie aplikacji zostaje tylko to, co naprawde
 * uzyte.
 */

enum BursztynSkalowanieFiltr : uint32_t {
    BURSZTYN_SKAL_NEAREST = 0U,
    BURSZTYN_SKAL_BILINEAR = 1U,
    BURSZTYN_SKAL_BOX = 2U
};

struct BursztynSkalowanieOs {
    uint32_t i0;
    uint32_t i1;
    uint32_t waga1;   /* 0..65535 */
};

inline uint32_t bursztyn_skal_indeks(uint32_t i, uint32_t dst, uint32_t src) {
    if (src == 0U) return 0U;
    const int64_t num = (static_cast<int64_t>(2) * i + 1) * static_cast<int64_t>(src) * 65536;
    const int64_t den = static_cast<int64_t>(2) * static_cast<int64_t>(dst);
    int64_t pos = num / den;
    if (pos <= 0) return 0U;
    const uint32_t idx = static_cast<uint32_t>(pos >> 16);
    return idx >= src ? src - 1U : idx;
}

inline BursztynSkalowanieOs bursztyn_skal_os(uint32_t i, uint32_t dst, uint32_t src) {
    BursztynSkalowanieOs os{0U, 0U, 0U};
    if (src == 0U || dst == 0U) return os;
    const int64_t num = (static_cast<int64_t>(2) * i + 1) * static_cast<int64_t>(src) * 65536;
    const int64_t den = static_cast<int64_t>(2) * static_cast<int64_t>(dst);
    const int64_t pos = num / den - 32768;
    if (pos <= 0) return os;
    const int64_t ostatni = (static_cast<int64_t>(src) - 1) * 65536;
    if (pos >= ostatni) {
        os.i0 = os.i1 = src - 1U;
        return os;
    }
    os.i0 = static_cast<uint32_t>(pos >> 16);
    os.waga1 = static_cast<uint32_t>(pos & 0xFFFF);
    os.i1 = os.i0 + 1U;
    if (os.i1 >= src) {
        os.i1 = src - 1U;
        os.waga1 = 0U;
    }
    return os;
}

inline void bursztyn_skal_rozloz(uint32_t piksel, uint32_t& a, uint32_t& r,
                                 uint32_t& g, uint32_t& b) {
    if (piksel == 0U) {
        a = r = g = b = 0U;
        return;
    }
    uint32_t alfa = (piksel >> 24) & 0xFFU;
    if (alfa == 0U || piksel == 0x01000000U) alfa = 255U;
    a = alfa;
    r = (piksel >> 16) & 0xFFU;
    g = (piksel >> 8) & 0xFFU;
    b = piksel & 0xFFU;
}

inline uint32_t bursztyn_skal_zloz(uint32_t a, uint32_t r, uint32_t g, uint32_t b) {
    if (a == 0U) return 0U;
    if (r > 255U) r = 255U;
    if (g > 255U) g = 255U;
    if (b > 255U) b = 255U;
    if (a >= 255U) return (r << 16) | (g << 8) | b;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline uint32_t bursztyn_skal_piksel_nearest(const uint32_t* src, uint32_t sw, uint32_t sh,
                                             uint32_t dx, uint32_t dy,
                                             uint32_t dw, uint32_t dh) {
    const uint32_t sx = bursztyn_skal_indeks(dx, dw, sw);
    const uint32_t sy = bursztyn_skal_indeks(dy, dh, sh);
    return src[static_cast<size_t>(sy) * sw + sx];
}

inline uint32_t bursztyn_skal_piksel_bilinear(const uint32_t* src, uint32_t sw, uint32_t sh,
                                              uint32_t dx, uint32_t dy,
                                              uint32_t dw, uint32_t dh) {
    const BursztynSkalowanieOs ox = bursztyn_skal_os(dx, dw, sw);
    const BursztynSkalowanieOs oy = bursztyn_skal_os(dy, dh, sh);
    const uint32_t w0x = 65535U - ox.waga1, w0y = 65535U - oy.waga1;
    const uint64_t w00 = static_cast<uint64_t>(w0x) * w0y;
    const uint64_t w10 = static_cast<uint64_t>(ox.waga1) * w0y;
    const uint64_t w01 = static_cast<uint64_t>(w0x) * oy.waga1;
    const uint64_t w11 = static_cast<uint64_t>(ox.waga1) * oy.waga1;
    const size_t r0 = static_cast<size_t>(oy.i0) * sw;
    const size_t r1 = static_cast<size_t>(oy.i1) * sw;
    uint32_t a00, r00, g00, b00, a10, r10, g10, b10;
    uint32_t a01, r01, g01, b01, a11, r11, g11, b11;
    bursztyn_skal_rozloz(src[r0 + ox.i0], a00, r00, g00, b00);
    bursztyn_skal_rozloz(src[r0 + ox.i1], a10, r10, g10, b10);
    bursztyn_skal_rozloz(src[r1 + ox.i0], a01, r01, g01, b01);
    bursztyn_skal_rozloz(src[r1 + ox.i1], a11, r11, g11, b11);
    const uint64_t sa = w00 * a00 + w10 * a10 + w01 * a01 + w11 * a11;
    if (sa == 0U) return 0U;
    const uint64_t sr = w00 * a00 * r00 + w10 * a10 * r10 + w01 * a01 * r01 + w11 * a11 * r11;
    const uint64_t sg = w00 * a00 * g00 + w10 * a10 * g10 + w01 * a01 * g01 + w11 * a11 * g11;
    const uint64_t sb = w00 * a00 * b00 + w10 * a10 * b10 + w01 * a01 * b01 + w11 * a11 * b11;
    const uint64_t maks = 65535ULL * 65535ULL;
    return bursztyn_skal_zloz(static_cast<uint32_t>((sa + maks / 2U) / maks),
                              static_cast<uint32_t>((sr + sa / 2U) / sa),
                              static_cast<uint32_t>((sg + sa / 2U) / sa),
                              static_cast<uint32_t>((sb + sa / 2U) / sa));
}

/*
 * Wybor filtra - ta sama polityka co w kompozytorze, zeby aplikacja
 * i jadro nie roznily sie w decyzji o jakosci.
 */
inline uint32_t bursztyn_skal_filtr(uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh) {
    if (dw == 0U || dh == 0U) return BURSZTYN_SKAL_NEAREST;
    if (dw >= sw * 2U && dh >= sh * 2U) return BURSZTYN_SKAL_NEAREST;
    if (sw >= dw * 2U && sh >= dh * 2U) return BURSZTYN_SKAL_BOX;
    return BURSZTYN_SKAL_BILINEAR;
}

/* Wypelnia jeden wiersz docelowy. Widoczne tu tylko dla zgodnosci API. */
inline void bursztyn_skal_wiersz(uint32_t* dst, const uint32_t* src,
                                 uint32_t sw, uint32_t sh,
                                 uint32_t dy, uint32_t dw, uint32_t dh,
                                 uint32_t filtr) {
    if (!dst || !src || sw == 0U || sh == 0U || dw == 0U || dh == 0U) return;
    for (uint32_t x = 0; x < dw; ++x) {
        dst[x] = (filtr == BURSZTYN_SKAL_BILINEAR)
                     ? bursztyn_skal_piksel_bilinear(src, sw, sh, x, dy, dw, dh)
                     : bursztyn_skal_piksel_nearest(src, sw, sh, x, dy, dw, dh);
    }
}
