#pragma once

// Geometryczne glify belki okna (minimize / maximize / restore / close).
// Wspolne dla kompozytora (pixel callback) i legacy GUI.
enum class WmIkonaBelki { Minimalizuj, Maksymalizuj, Przywroc, Zamknij };

template<class Pixel>
inline void wm_rysuj_ikone_belki(WmIkonaBelki rodzaj, int bx, int by, int size,
                                 uint32_t color, Pixel pixel) {
    if (size < 8) return;
    auto fill = [&](int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) return;
        for (int py = y; py < y + h; ++py)
            for (int px = x; px < x + w; ++px)
                pixel(px, py, color);
    };
    const int m = size / 5;
    const int t = size >= 16 ? (size / 10 > 1 ? size / 10 : 1) : 1;
    const int cx = bx + size / 2;
    const int cy = by + size / 2;

    switch (rodzaj) {
    case WmIkonaBelki::Minimalizuj: {
        const int w = size - 2 * m;
        fill(bx + m, cy - t / 2, w, t > 0 ? t : 1);
        break;
    }
    case WmIkonaBelki::Maksymalizuj: {
        const int s = size - 2 * m;
        fill(bx + m, by + m, s, t);                 // top
        fill(bx + m, by + m + s - t, s, t);         // bottom
        fill(bx + m, by + m, t, s);                 // left
        fill(bx + m + s - t, by + m, t, s);         // right
        break;
    }
    case WmIkonaBelki::Przywroc: {
        const int s = (size - 2 * m) * 3 / 4;
        const int ox = bx + m + (size - 2 * m - s) / 2 + t;
        const int oy = by + m;
        // Back square (offset up-right)
        fill(ox, oy, s, t);
        fill(ox, oy + s - t, s, t);
        fill(ox, oy, t, s);
        fill(ox + s - t, oy, t, s);
        // Front square
        const int fx = bx + m;
        const int fy = by + m + (size - 2 * m - s) / 2 + t;
        fill(fx, fy, s, t);
        fill(fx, fy + s - t, s, t);
        fill(fx, fy, t, s);
        fill(fx + s - t, fy, t, s);
        break;
    }
    case WmIkonaBelki::Zamknij: {
        const int x0 = bx + m, y0 = by + m;
        const int x1 = bx + size - m - 1, y1 = by + size - m - 1;
        const int steps = x1 - x0;
        for (int i = 0; i <= steps; ++i) {
            int x = x0 + i;
            int y_a = y0 + i;
            int y_b = y1 - i;
            for (int dy = 0; dy < t; ++dy) {
                pixel(x, y_a + dy - t / 2, color);
                pixel(x, y_b + dy - t / 2, color);
            }
        }
        break;
    }
    }
}
