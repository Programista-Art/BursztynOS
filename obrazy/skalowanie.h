#pragma once
#include <stdint.h>
#include <stddef.h>

// Wspolna geometria i nearest-neighbor dla obrazow XRGB. Bez alokacji/float.
// Wszystkie osie <=8192; iloczyny posrednie liczone w uint64_t.
enum class TrybSkalowaniaObrazu : uint32_t { Dopasuj = 0, Wypelnij = 1, Wysrodkuj = 2 };
struct GeometriaObrazu {
    uint32_t src_x, src_y, src_w, src_h;
    uint32_t dst_x, dst_y, dst_w, dst_h;
};
inline bool geometria_obrazu(uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
                             TrybSkalowaniaObrazu tryb, GeometriaObrazu* out) {
    if (!out || !sw || !sh || !dw || !dh || sw > 8192 || sh > 8192 ||
        dw > 8192 || dh > 8192 || static_cast<uint32_t>(tryb) > 2) return false;
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
    } else {
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
