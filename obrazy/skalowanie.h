#pragma once
#include <stdint.h>
#include <stddef.h>

// Wspolna geometria i nearest-neighbor dla obrazow XRGB. Bez alokacji/float.
// Wszystkie osie <=8192; iloczyny posrednie liczone w uint64_t.
enum class TrybSkalowaniaObrazu : uint32_t { Dopasuj = 0, Wypelnij = 1, Wysrodkuj = 2, Rozciagnij = 3 };
struct GeometriaObrazu {
    uint32_t src_x, src_y, src_w, src_h;
    uint32_t dst_x, dst_y, dst_w, dst_h;
};
inline bool geometria_obrazu(uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
                             TrybSkalowaniaObrazu tryb, GeometriaObrazu* out) {
    if (!out || !sw || !sh || !dw || !dh || sw > 8192 || sh > 8192 ||
        dw > 8192 || dh > 8192 || static_cast<uint32_t>(tryb) > 3) return false;
    GeometriaObrazu g{0, 0, sw, sh, 0, 0, dw, dh};
    if (tryb == TrybSkalowaniaObrazu::Dopasuj) {
        if (static_cast<uint64_t>(sw) * dh > static_cast<uint64_t>(sh) * dw)
            g.dst_h = static_cast<uint32_t>(static_cast<uint64_t>(dw) * sh / sw);
        else g.dst_w = static_cast<uint32_t>(static_cast<uint64_t>(dh) * sw / sh);
        if (!g.dst_w) g.dst_w = 1;
        if (!g.dst_h) g.dst_h = 1;
        g.dst_x = (dw - g.dst_w) / 2;
        g.dst_y = (dh - g.dst_h) / 2;
    } else if (tryb == TrybSkalowaniaObrazu::Wypelnij) {
        if (static_cast<uint64_t>(sw) * dh > static_cast<uint64_t>(sh) * dw)
            g.src_w = static_cast<uint32_t>(static_cast<uint64_t>(sh) * dw / dh);
        else g.src_h = static_cast<uint32_t>(static_cast<uint64_t>(sw) * dh / dw);
        if (!g.src_w) g.src_w = 1;
        if (!g.src_h) g.src_h = 1;
        g.src_x = (sw - g.src_w) / 2;
        g.src_y = (sh - g.src_h) / 2;
    } else if (tryb == TrybSkalowaniaObrazu::Wysrodkuj) {
        g.src_w = g.dst_w = sw < dw ? sw : dw;
        g.src_h = g.dst_h = sh < dh ? sh : dh;
        g.src_x = (sw - g.src_w) / 2; g.src_y = (sh - g.src_h) / 2;
        g.dst_x = (dw - g.dst_w) / 2; g.dst_y = (dh - g.dst_h) / 2;
    }
    *out = g;
    return true;
}

// Uzyte tez przez Obraz: indeks w probkowaniu nearest-neighbor, bez float.
inline uint32_t probka_obrazu(uint32_t pozycja, uint32_t zrodlo, uint32_t cel) {
    if (!zrodlo || !cel) return 0;
    const uint64_t wynik = static_cast<uint64_t>(pozycja) * zrodlo / cel;
    return wynik < zrodlo ? static_cast<uint32_t>(wynik) : zrodlo - 1;
}

inline bool skaluj_obraz(const uint32_t* src, uint32_t sw, uint32_t sh, size_t src_bytes,
                         uint32_t* dst, uint32_t dw, uint32_t dh, uint32_t stride,
                         size_t dst_bytes, TrybSkalowaniaObrazu tryb, uint32_t tlo) {
    GeometriaObrazu g{};
    if (!src || !dst || !geometria_obrazu(sw, sh, dw, dh, tryb, &g) || stride < dw ||
        static_cast<uint64_t>(sw) * sh * 4 > src_bytes ||
        static_cast<uint64_t>(stride) * dh * 4 > dst_bytes) return false;
    // Kontrakt: poprawnie zmapowane, wyrownane, rozlaczne bufory o podanej pojemnosci.
    // Wszystkie kontrole zakonczone przed pierwszym zapisem.
    for (uint32_t y = 0; y < dh; ++y)
        for (uint32_t x = 0; x < dw; ++x) dst[static_cast<size_t>(y) * stride + x] = tlo;
    for (uint32_t y = 0; y < g.dst_h; ++y) {
        const uint32_t sy = g.src_y + probka_obrazu(y, g.src_h, g.dst_h);
        const uint32_t* row = src + static_cast<size_t>(sy) * sw + g.src_x;
        uint32_t* out = dst + static_cast<size_t>(y + g.dst_y) * stride + g.dst_x;
        for (uint32_t x = 0; x < g.dst_w; ++x) {
            const uint32_t c = row[probka_obrazu(x, g.src_w, g.dst_w)];
            if (c != 0) out[x] = c; // Exact zero, bez mieszania alfa.
        }
    }
    return true;
}

// Bilinear (fixed-point 16.16), bez FPU. Kanaly RGBA interpolowane NIEZALEZNIE (bez premnozenia alfy), wiec
// wolno go uzywac tylko dla obrazow nieprzezroczystych (np. okladka w Grajku). Ikony z alfa: skaluj_ikone_alfa.
// Tlo wypelniane przed zapisem jak w skaluj_obraz.
inline bool skaluj_obraz_bilinear(const uint32_t* src, uint32_t sw, uint32_t sh, size_t src_bytes,
                                  uint32_t* dst, uint32_t dw, uint32_t dh, uint32_t stride,
                                  size_t dst_bytes, TrybSkalowaniaObrazu tryb, uint32_t tlo) {
    GeometriaObrazu g{};
    if (!src || !dst || !geometria_obrazu(sw, sh, dw, dh, tryb, &g) || stride < dw ||
        !g.dst_w || !g.dst_h || !g.src_w || !g.src_h ||
        static_cast<uint64_t>(sw) * sh * 4 > src_bytes ||
        static_cast<uint64_t>(stride) * dh * 4 > dst_bytes) return false;
    for (uint32_t y = 0; y < dh; ++y)
        for (uint32_t x = 0; x < dw; ++x) dst[static_cast<size_t>(y) * stride + x] = tlo;
    const uint32_t x_step = g.dst_w > 1
        ? static_cast<uint32_t>(((uint64_t(g.src_w) - 1U) << 16) / (g.dst_w - 1U)) : 0;
    const uint32_t y_step = g.dst_h > 1
        ? static_cast<uint32_t>(((uint64_t(g.src_h) - 1U) << 16) / (g.dst_h - 1U)) : 0;
    for (uint32_t y = 0; y < g.dst_h; ++y) {
        const uint32_t fy = y_step * y;
        const uint32_t sy0 = g.src_y + (fy >> 16);
        uint32_t sy1 = sy0 + 1;
        if (sy1 >= g.src_y + g.src_h) sy1 = g.src_y + g.src_h - 1;
        const uint32_t wy = fy & 0xffffU;
        const uint32_t* row0 = src + static_cast<size_t>(sy0) * sw;
        const uint32_t* row1 = src + static_cast<size_t>(sy1) * sw;
        uint32_t* out = dst + static_cast<size_t>(y + g.dst_y) * stride + g.dst_x;
        for (uint32_t x = 0; x < g.dst_w; ++x) {
            const uint32_t fx = x_step * x;
            const uint32_t sx0 = g.src_x + (fx >> 16);
            uint32_t sx1 = sx0 + 1;
            if (sx1 >= g.src_x + g.src_w) sx1 = g.src_x + g.src_w - 1;
            const uint32_t wx = fx & 0xffffU;
            const uint32_t c00 = row0[sx0], c10 = row0[sx1], c01 = row1[sx0], c11 = row1[sx1];
            uint32_t out_c = 0;
            for (int shift = 0; shift <= 24; shift += 8) {
                const uint32_t p00 = (c00 >> shift) & 0xffU;
                const uint32_t p10 = (c10 >> shift) & 0xffU;
                const uint32_t p01 = (c01 >> shift) & 0xffU;
                const uint32_t p11 = (c11 >> shift) & 0xffU;
                const uint32_t top = p00 * (65536U - wx) + p10 * wx;
                const uint32_t bot = p01 * (65536U - wx) + p11 * wx;
                const uint32_t mix = ((top >> 16) * (65536U - wy) + (bot >> 16) * wy) >> 16;
                out_c |= (mix & 0xffU) << shift;
            }
            if (out_c != 0) out[x] = out_c;
        }
    }
    return true;
}

// --- Ikony z kanalem alfa -----------------------------------------------------------------------------------
// Wejscie i wyjscie: proste (nieprzemnozone) ARGB; obliczenia na alfie premnozonej, wiec kolor pikseli w pelni
// przezroczystych (np. bialy RGB pod A=0 z edytora grafiki) NIE wplywa na wynik - bez bialych/ciemnych obwodek.
// Pomniejszanie: srednia obszarowa z dokladnym (ulamkowym) pokryciem pikseli zrodla - bez aliasingu 128 -> 16 px.
// Powiekszanie: interpolacja liniowa (namiot). Ten sam rozmiar: kopia 1:1 (piksel A=0 zapisywany jako 0).
// Geometria Dopasuj: proporcje zachowane, przezroczyste marginesy (0). Bez alokacji i bez float.
namespace skalowanie_alfa {
// Wagi jednej osi dla piksela celu d: f(indeks_zrodla, waga); zwraca sume wag (mianownik).
template <class F> inline uint64_t wagi_osi(uint32_t d, uint32_t zrodlo, uint32_t cel, F f) {
    if (zrodlo >= cel) {
        // piksel celu d pokrywa [d*zrodlo, (d+1)*zrodlo), piksel zrodla i: [i*cel, (i+1)*cel) (jednostki 1/cel)
        const uint64_t a = uint64_t(d) * zrodlo, b = a + zrodlo;
        for (uint64_t i = a / cel; i < zrodlo && i * cel < b; ++i) {
            const uint64_t lo = i * cel > a ? i * cel : a, hi = (i + 1) * cel < b ? (i + 1) * cel : b;
            if (hi > lo) f(static_cast<uint32_t>(i), hi - lo);
        }
        return zrodlo;
    }
    // srodek piksela celu w ukladzie zrodla: (d + 0.5) * zrodlo / cel - 0.5, w 1/65536 piksela
    int64_t c = static_cast<int64_t>((uint64_t(2 * d + 1) * zrodlo * 65536U) / (2U * uint64_t(cel))) - 32768;
    if (c < 0) c = 0;
    const uint32_t i0 = static_cast<uint32_t>(c >> 16), w1 = static_cast<uint32_t>(c & 0xFFFF);
    const uint32_t i1 = i0 + 1 < zrodlo ? i0 + 1 : i0;
    f(i0, 65536U - w1);
    if (w1) f(i1, w1);
    return 65536U;
}
}  // namespace skalowanie_alfa

inline bool skaluj_ikone_alfa(const uint32_t* src, uint32_t sw, uint32_t sh, size_t src_bytes,
                              uint32_t* dst, uint32_t dw, uint32_t dh, uint32_t stride, size_t dst_bytes) {
    GeometriaObrazu g{};
    if (!src || !dst || !geometria_obrazu(sw, sh, dw, dh, TrybSkalowaniaObrazu::Dopasuj, &g) || stride < dw ||
        static_cast<uint64_t>(sw) * sh * 4 > src_bytes ||
        static_cast<uint64_t>(stride) * dh * 4 > dst_bytes) return false;
    for (uint32_t y = 0; y < dh; ++y)
        for (uint32_t x = 0; x < dw; ++x) dst[static_cast<size_t>(y) * stride + x] = 0;
    const uint64_t mian = (sw >= g.dst_w ? uint64_t(sw) : 65536U) * (sh >= g.dst_h ? uint64_t(sh) : 65536U);
    for (uint32_t y = 0; y < g.dst_h; ++y) {
        uint32_t* out = dst + static_cast<size_t>(y + g.dst_y) * stride + g.dst_x;
        for (uint32_t x = 0; x < g.dst_w; ++x) {
            uint64_t A = 0, R = 0, G = 0, B = 0;     // <= 2^32 (wagi) * 255 * 255 < 2^48: miesci sie w uint64
            skalowanie_alfa::wagi_osi(y, sh, g.dst_h, [&](uint32_t sy, uint64_t wy) {
                const uint32_t* wiersz = src + static_cast<size_t>(sy) * sw;
                skalowanie_alfa::wagi_osi(x, sw, g.dst_w, [&](uint32_t sx, uint64_t wx) {
                    const uint32_t c = wiersz[sx];
                    const uint64_t wa = wx * wy * (c >> 24);
                    if (!wa) return;
                    A += wa; R += wa * ((c >> 16) & 0xFFU); G += wa * ((c >> 8) & 0xFFU); B += wa * (c & 0xFFU);
                });
            });
            uint64_t a = (A + mian / 2) / mian;
            if (a > 255) a = 255;
            out[x] = a ? static_cast<uint32_t>((a << 24) | (((R + A / 2) / A) << 16) | (((G + A / 2) / A) << 8) | ((B + A / 2) / A)) : 0U;
        }
    }
    return true;
}
