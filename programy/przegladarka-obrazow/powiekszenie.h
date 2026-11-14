#pragma once
/*
 * Przegladarka Obrazow - prawdziwe, ciagle powiekszenie (10%..800%) obrazu w pamieci.
 *
 * Czysta logika bez alokacji i bez float: geometria widoku (offset, zakotwiczenie powiekszenia)
 * oraz renderer probkujacy zrodlo BGRA bezposrednio do bufora widoku (tylko widoczne piksele,
 * bez kopii calego powiekszonego obrazu). Wszystkie wymiary <= 8192, wspolczynnik <= 800%,
 * obliczenia posrednie w int64.
 *
 * Filtrowanie: pomniejszanie (<100%) usrednia siatke probek obejmujaca stopke piksela
 * (do 6x6, z uwzglednieniem alfa); powiekszenie 100..399% - interpolacja dwuliniowa;
 * >=400% oraz tryb "bez wygladzania" - najblizszy sasiad (ostre piksele).
 */
#include <stddef.h>
#include <stdint.h>

namespace Powiekszenie {

constexpr int ZOOM_MIN = 10;
constexpr int ZOOM_MAX = 800;
constexpr int ZOOM_FIT_MIN = 1;      // tryb dopasowania moze zejsc ponizej suwaka (duze zdjecia)
constexpr int MAKS_BOK = 8192;

struct Widok {
    int64_t szer_obr = 0, wys_obr = 0;   // rozmiar zrodla
    int zoom = 100;                      // procent
    int vw = 0, vh = 0;                  // rozmiar widoku w px
    int64_t ox = 0, oy = 0;              // lewy gorny rog widoku we wspolrzednych powiekszonego obrazu
};

constexpr int64_t klamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr int zakres_zoom(int z) { return z < ZOOM_MIN ? ZOOM_MIN : (z > ZOOM_MAX ? ZOOM_MAX : z); }

constexpr int64_t rozmiar_skalowany(int64_t wymiar, int zoom) {
    const int64_t r = wymiar * zoom / 100;
    return r < 1 ? 1 : r;
}

// Procent dopasowujacy caly obraz do widoku (zaokraglenie w dol, >= ZOOM_FIT_MIN, <= ZOOM_MAX).
constexpr int zoom_dopasuj(int64_t sw, int64_t sh, int vw, int vh) {
    if (sw <= 0 || sh <= 0 || vw <= 0 || vh <= 0) return 100;
    const int64_t zx = static_cast<int64_t>(vw) * 100 / sw, zy = static_cast<int64_t>(vh) * 100 / sh;
    int64_t z = zx < zy ? zx : zy;
    return static_cast<int>(klamp64(z, ZOOM_FIT_MIN, ZOOM_MAX));
}

// Offset: gdy obraz mniejszy od widoku - wysrodkowany (ujemny offset), inaczej w [0, rozmiar - widok].
inline void ogranicz_offset(Widok& w) {
    const int64_t sx = rozmiar_skalowany(w.szer_obr, w.zoom), sy = rozmiar_skalowany(w.wys_obr, w.zoom);
    w.ox = sx <= w.vw ? -(static_cast<int64_t>(w.vw) - sx) / 2 : klamp64(w.ox, 0, sx - w.vw);
    w.oy = sy <= w.vh ? -(static_cast<int64_t>(w.vh) - sy) / 2 : klamp64(w.oy, 0, sy - w.vh);
}

inline void wysrodkuj(Widok& w) { w.ox = 0; w.oy = 0; ogranicz_offset(w); }

// Zmiana powiekszenia z zachowaniem punktu obrazu pod (ax, ay) we wspolrzednych widoku
// (mysz: pozycja kursora; suwak: srodek widoku).
inline void zmien_zoom(Widok& w, int nowy_zoom, int ax, int ay) {
    nowy_zoom = static_cast<int>(klamp64(nowy_zoom, ZOOM_FIT_MIN, ZOOM_MAX));
    if (nowy_zoom == w.zoom) { ogranicz_offset(w); return; }
    // punkt obrazu pod kotwica w jednostkach zrodla*100 (zachowuje precyzje bez float)
    const int64_t px = (w.ox + ax) * 100 * 100 / (w.zoom ? w.zoom : 100);
    const int64_t py = (w.oy + ay) * 100 * 100 / (w.zoom ? w.zoom : 100);
    w.zoom = nowy_zoom;
    w.ox = px * nowy_zoom / (100 * 100) - ax;
    w.oy = py * nowy_zoom / (100 * 100) - ay;
    ogranicz_offset(w);
}

inline void przesun(Widok& w, int dx, int dy) { w.ox += dx; w.oy += dy; ogranicz_offset(w); }

// Kolejny/poprzedni poziom z drabinki typowych wartosci (przyciski +/-).
inline int zoom_krok(int zoom, int kierunek) {
    static const int drabina[] = {10, 15, 20, 25, 33, 50, 67, 75, 100, 125, 150, 175, 200, 250, 300, 400, 500, 600, 800};
    const int n = static_cast<int>(sizeof(drabina) / sizeof(drabina[0]));
    if (kierunek > 0) { for (int i = 0; i < n; ++i) if (drabina[i] > zoom) return drabina[i]; return ZOOM_MAX; }
    for (int i = n - 1; i >= 0; --i) if (drabina[i] < zoom) return drabina[i];
    return ZOOM_MIN;
}

namespace szczegoly {
inline uint32_t tlo_szachownicy(int x, int y, uint32_t jasny, uint32_t ciemny) {
    return (((x >> 3) ^ (y >> 3)) & 1) ? ciemny : jasny;
}
inline uint32_t nad(uint32_t tlo, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    const uint32_t tr = (tlo >> 16) & 255U, tg = (tlo >> 8) & 255U, tb = tlo & 255U;
    const uint32_t or_ = (r * a + tr * (255U - a) + 127U) / 255U;
    const uint32_t og = (g * a + tg * (255U - a) + 127U) / 255U;
    const uint32_t ob = (b * a + tb * (255U - a) + 127U) / 255U;
    return (or_ << 16) | (og << 8) | ob;
}
inline uint32_t nie_zero(uint32_t rgb) { return (rgb & 0xFFFFFFU) ? (rgb & 0xFFFFFFU) : 0x01000000U; }
} // namespace szczegoly

// Renderuje widok do dst (vw x vh, stride = vw). src: BGRA 0xAARRGGBB (alfa prosta).
// tlo: kolor tla poza obrazem (z motywu); przezroczystosc obrazu jest pokazywana na szachownicy.
// Zwraca false przy niepoprawnych argumentach (bez zapisu).
inline bool renderuj(const uint32_t* src, int sw, int sh, bool ma_alfa, uint32_t* dst, size_t pojemnosc,
                     const Widok& w, uint32_t tlo, bool gladko) {
    if (!src || !dst || sw <= 0 || sh <= 0 || sw > MAKS_BOK || sh > MAKS_BOK || w.vw <= 0 || w.vh <= 0 ||
        w.zoom < ZOOM_FIT_MIN || w.zoom > ZOOM_MAX || w.vw > 16384 || w.vh > 16384 ||
        static_cast<uint64_t>(w.vw) * static_cast<uint64_t>(w.vh) > pojemnosc) return false;
    const int64_t sx = rozmiar_skalowany(sw, w.zoom), sy = rozmiar_skalowany(sh, w.zoom);
    const int zoom = w.zoom;
    const uint32_t ttlo = szczegoly::nie_zero(tlo);
    const int n = zoom < 100 ? static_cast<int>((100 + zoom - 1) / zoom) : 1;
    const int nn = n > 6 ? 6 : n;
    const bool dwuliniowo = gladko && zoom >= 100 && zoom < 400;
    for (int y = 0; y < w.vh; ++y) {
        uint32_t* wiersz = dst + static_cast<size_t>(y) * static_cast<size_t>(w.vw);
        const int64_t Y = w.oy + y;
        for (int x = 0; x < w.vw; ++x) {
            const int64_t X = w.ox + x;
            if (X < 0 || Y < 0 || X >= sx || Y >= sy) { wiersz[x] = ttlo; continue; }
            uint32_t r = 0, g = 0, b = 0, a = 255;
            if (dwuliniowo) {
                // pozycja srodka piksela w zrodle, 16.16, minus polowa piksela zrodla
                int64_t fx = ((2 * X + 1) * 100 * 32768) / zoom - 32768;
                int64_t fy = ((2 * Y + 1) * 100 * 32768) / zoom - 32768;
                if (fx < 0) fx = 0;
                if (fy < 0) fy = 0;
                int64_t x0 = fx >> 16, y0 = fy >> 16;
                const uint32_t tx = static_cast<uint32_t>(fx & 0xFFFF), ty = static_cast<uint32_t>(fy & 0xFFFF);
                int64_t x1 = x0 + 1 < sw ? x0 + 1 : sw - 1, y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
                if (x0 >= sw) x0 = sw - 1;
                if (y0 >= sh) y0 = sh - 1;
                const uint32_t p00 = src[y0 * sw + x0], p10 = src[y0 * sw + x1], p01 = src[y1 * sw + x0], p11 = src[y1 * sw + x1];
                const uint64_t w00 = static_cast<uint64_t>(65536 - tx) * (65536 - ty), w10 = static_cast<uint64_t>(tx) * (65536 - ty);
                const uint64_t w01 = static_cast<uint64_t>(65536 - tx) * ty, w11 = static_cast<uint64_t>(tx) * ty;
                uint64_t sa = 0, sr = 0, sg = 0, sb = 0;
                const uint32_t pk[4] = {p00, p10, p01, p11};
                const uint64_t wk[4] = {w00, w10, w01, w11};
                for (int k = 0; k < 4; ++k) {
                    const uint64_t al = (pk[k] >> 24) & 255U;
                    sa += wk[k] * al;
                    sr += wk[k] * al * ((pk[k] >> 16) & 255U);
                    sg += wk[k] * al * ((pk[k] >> 8) & 255U);
                    sb += wk[k] * al * (pk[k] & 255U);
                }
                const uint64_t tot = 1ULL << 32;
                a = static_cast<uint32_t>((sa + tot / 2) / tot);
                if (sa) { r = static_cast<uint32_t>(sr / sa); g = static_cast<uint32_t>(sg / sa); b = static_cast<uint32_t>(sb / sa); }
            } else if (nn > 1 && gladko) {
                uint64_t sa = 0, sr = 0, sg = 0, sb = 0;
                for (int j = 0; j < nn; ++j) for (int i = 0; i < nn; ++i) {
                    int64_t ix = (X * 100 + (static_cast<int64_t>(i) * 100) / nn) / zoom;
                    int64_t iy = (Y * 100 + (static_cast<int64_t>(j) * 100) / nn) / zoom;
                    if (ix >= sw) ix = sw - 1;
                    if (iy >= sh) iy = sh - 1;
                    const uint32_t p = src[iy * sw + ix];
                    const uint64_t al = (p >> 24) & 255U;
                    sa += al; sr += al * ((p >> 16) & 255U); sg += al * ((p >> 8) & 255U); sb += al * (p & 255U);
                }
                a = static_cast<uint32_t>(sa / static_cast<uint64_t>(nn * nn));
                if (sa) { r = static_cast<uint32_t>(sr / sa); g = static_cast<uint32_t>(sg / sa); b = static_cast<uint32_t>(sb / sa); }
            } else {
                int64_t ix = X * 100 / zoom, iy = Y * 100 / zoom;
                if (ix >= sw) ix = sw - 1;
                if (iy >= sh) iy = sh - 1;
                const uint32_t p = src[iy * sw + ix];
                a = (p >> 24) & 255U; r = (p >> 16) & 255U; g = (p >> 8) & 255U; b = p & 255U;
            }
            if (!ma_alfa || a == 255U) wiersz[x] = szczegoly::nie_zero((r << 16) | (g << 8) | b);
            else wiersz[x] = szczegoly::nie_zero(szczegoly::nad(
                szczegoly::tlo_szachownicy(x, y, 0xCCCCCCU, 0x999999U), r, g, b, a));
        }
    }
    return true;
}

} // namespace Powiekszenie
