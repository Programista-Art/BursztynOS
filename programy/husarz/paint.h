#pragma once
#include "layout.h"
namespace Husarz {
struct Surface {
    uint32_t *pixels{};
    int width{}, height{};
    uint32_t budget{16000000};
    bool truncated{};
    void pixel(int x, int y, uint32_t color, unsigned alpha) {
        if (!budget) {
            truncated = true;
            return;
        }
        --budget;
        if (x < 0 || y < 0 || x >= width || y >= height)
            return;
        uint32_t &dest = pixels[size_t(y) * width + x];
        if (alpha >= 255)
            dest = 0xff000000 | color;
        else {
            unsigned inverse = 255 - alpha;
            uint32_t r = (((color >> 16) & 255) * alpha + ((dest >> 16) & 255) * inverse + 127) / 255,
                     g = (((color >> 8) & 255) * alpha + ((dest >> 8) & 255) * inverse + 127) / 255,
                     b = ((color & 255) * alpha + (dest & 255) * inverse + 127) / 255;
            dest = 0xff000000 | (r << 16) | (g << 8) | b;
        }
    }
    void clear(uint32_t color) {
        for (size_t i = 0; i < size_t(width) * height; ++i)
            pixels[i] = 0xff000000 | color;
    }
    static bool rounded(int x, int y, int w, int h, int radius) {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return false;
        if (radius > w / 2)
            radius = w / 2;
        if (radius > h / 2)
            radius = h / 2;
        if (radius <= 0)
            return true;
        int dx = x < radius        ? radius - 1 - x
                 : x >= w - radius ? x - (w - radius)
                                   : 0,
            dy = y < radius        ? radius - 1 - y
                 : y >= h - radius ? y - (h - radius)
                                   : 0;
        return int64_t(dx) * dx + int64_t(dy) * dy < int64_t(radius) * radius;
    }
    void rect(int x, int y, int w, int h, uint32_t color, unsigned alpha, Clip clip, int radius = 0) {
        Clip c = intersect({x, y, w, h}, intersect(clip, {0, 0, width, height}));
        for (int yy = c.y; yy < c.y + c.height && budget; ++yy)
            for (int xx = c.x; xx < c.x + c.width && budget; ++xx) {
                --budget;
                if (rounded(xx - x, yy - y, w, h, radius) && budget)
                    pixel(xx, yy, color, alpha);
            }
        if (!budget)
            truncated = true;
    }
};
struct Glyphs {
    void *user{};
    const uint16_t *(*bitmap)(void *, uint32_t){};
    int (*width)(void *, uint32_t){};
};
inline Clip ancestor_clip(const Document &d, uint16_t id, int scroll, int vw, int vh) {
    Clip clip{0, 0, vw, vh};
    for (uint16_t p = d.nodes[id].parent; p; p = d.nodes[p].parent) {
        const Style &s = d.styles[p];
        const auto &b = d.boxes[p];
        Clip c = clip;
        if (s.overflow_x != Overflow::Visible) {
            c.x = b.x;
            c.width = b.width;
        }
        if (s.overflow_y != Overflow::Visible) {
            c.y = b.y - scroll;
            c.height = b.height;
        }
        clip = intersect(clip, c);
    }
    return clip;
}
inline void paint(Document &d, Surface &target, int scroll, const Metrics &metrics, const Glyphs &glyphs,
                  uint32_t background) {
    target.clear(background);
    uint32_t fi = 0;
    for (uint16_t id = 0; id < d.node_count; ++id) {
        const Style &s = d.styles[id];
        const Box &b = d.boxes[id];
        if (s.display == Display::None)
            continue;
        Clip clip = ancestor_clip(d, id, scroll, target.width, target.height);
        if (s.visible && s.paint_opacity && b.height && b.width) {
            if (s.has_background)
                target.rect(b.x, b.y - scroll, b.width, b.height, s.background, s.paint_opacity, clip,
                            s.radius);
            if (s.border_style) {
                int y = b.y - scroll;
                Clip c = intersect({b.x, y, b.width, b.height}, clip);
                int innerw = b.width - s.border[1] - s.border[3],
                    innerh = b.height - s.border[0] - s.border[2];
                int r = s.radius - s.border[0];
                if (r < 0)
                    r = 0;
                for (int yy = c.y; yy < c.y + c.height && target.budget; ++yy)
                    for (int xx = c.x; xx < c.x + c.width && target.budget; ++xx) {
                        --target.budget;
                        int dx = xx - b.x, dy = yy - y;
                        if (!Surface::rounded(dx, dy, b.width, b.height, s.radius) ||
                            Surface::rounded(dx - s.border[3], dy - s.border[0], innerw, innerh, r))
                            continue;
                        if (s.border_style > 1) {
                            int step = s.border_style == 2 ? 6 : 2;
                            if (((dy < s.border[0] || dy >= b.height - s.border[2] ? dx : dy) / step) & 1)
                                continue;
                        }
                        target.pixel(xx, yy, s.border_color, s.paint_opacity);
                    }
            }
        }
        while (fi < d.fragment_count && d.fragments[fi].node < id)
            ++fi;
        while (fi < d.fragment_count && d.fragments[fi].node == id) {
            const Fragment &f = d.fragments[fi++];
            if (!s.visible || !s.paint_opacity)
                continue;
            Clip fc = intersect({f.clip.x, f.clip.y - scroll, f.clip.width, f.clip.height},
                                {0, 0, target.width, target.height});
            if (f.y - scroll >= target.height || f.y - scroll + f.height <= 0)
                continue;
            int x = f.x, y = f.y - scroll;
            size_t p = 0;
            const char *text = d.str(f.text);
            while (p < f.length && target.budget) {
                size_t used;
                uint32_t cp = utf8(text + p, f.length - p, used);
                int adv = advance(metrics, cp, s);
                int gw = glyphs.width ? glyphs.width(glyphs.user, cp) : 8;
                const uint16_t *bitmap = glyphs.bitmap ? glyphs.bitmap(glyphs.user, cp) : nullptr;
                int dw = clamp64((int64_t(gw) * s.font + 8) / 16, 1, 256), dh = font_height(metrics, s);
                if (bitmap)
                    for (int py = 0; py < dh && target.budget; ++py) {
                        int sy = py * 16 / dh, skew = s.italic ? (dh - 1 - py) / 5 : 0;
                        for (int px = 0; px < dw && target.budget; ++px) {
                            --target.budget;
                            int sx = px * gw / dw;
                            if (sx > 15)
                                sx = 15;
                            if (!(bitmap[sy] & (1U << (15 - sx))))
                                continue;
                            int tx = x + px + skew, ty = y + py;
                            if (tx >= fc.x && tx < int64_t(fc.x) + fc.width && ty >= fc.y &&
                                ty < int64_t(fc.y) + fc.height) {
                                target.pixel(tx, ty, s.color, s.paint_opacity);
                                if (s.bold && tx + 1 < int64_t(fc.x) + fc.width)
                                    target.pixel(tx + 1, ty, s.color, s.paint_opacity);
                            }
                        }
                    }
                x = clamp64(int64_t(x) + adv, -MaxDimension);
                p += used;
            }
            if (s.underline)
                target.rect(f.x, y + f.height - 1, f.width, 1, s.color, s.paint_opacity, fc);
        }
    }
    if (!target.budget)
        target.truncated = true;
    if (target.truncated)
        d.truncated = true;
}
} // namespace Husarz
