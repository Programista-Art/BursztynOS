#pragma once

#include <stdint.h>
#include <stddef.h>
#include "types.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class DrawingContext {
public:
    DrawingContext() {
        clip_stack_[0] = Rect(0, 0, 4096, 4096);
        offset_stack_[0] = Point(0, 0);
    }

    void push_offset(Point pt) {
        if (offset_depth_ < MAX_STACK - 1) {
            offset_depth_++;
            offset_stack_[offset_depth_] = offset_stack_[offset_depth_ - 1] + pt;
        }
    }

    void pop_offset() {
        if (offset_depth_ > 0) {
            offset_depth_--;
        }
    }

    Point get_offset() const {
        return offset_stack_[offset_depth_];
    }

    void push_clip(Rect r) {
        if (clip_depth_ < MAX_STACK - 1) {
            clip_depth_++;
            Point cur_off = get_offset();
            Rect world_r(r.x + cur_off.x, r.y + cur_off.y, r.width, r.height);
            clip_stack_[clip_depth_] = clip_stack_[clip_depth_ - 1].intersection(world_r);
        }
    }

    void pop_clip() {
        if (clip_depth_ > 0) {
            clip_depth_--;
        }
    }

    Rect get_current_clip() const {
        return clip_stack_[clip_depth_];
    }

    void draw_rect(Rect r, uint32_t color) {
        if (r.is_empty()) return;
        Point off = get_offset();
        Rect world_r(r.x + off.x, r.y + off.y, r.width, r.height);
        Rect clipped = world_r.intersection(get_current_clip());
        if (!clipped.is_empty()) {
            gui_rysuj_prostokat(clipped.x, clipped.y, clipped.width, clipped.height, color);
        }
    }

    void draw_border(Rect r, uint32_t color, int thickness = 1) {
        if (thickness <= 0 || r.is_empty()) return;
        if (r.width <= 2 * thickness || r.height <= 2 * thickness) {
            draw_rect(r, color);
            return;
        }
        // Top
        draw_rect(Rect(r.x, r.y, r.width, thickness), color);
        // Bottom
        draw_rect(Rect(r.x, r.y + r.height - thickness, r.width, thickness), color);
        // Left
        draw_rect(Rect(r.x, r.y + thickness, thickness, r.height - 2 * thickness), color);
        // Right
        draw_rect(Rect(r.x + r.width - thickness, r.y + thickness, thickness, r.height - 2 * thickness), color);
    }

    void draw_rounded_rect(Rect r, uint32_t color, int radius = 2) {
        if (r.is_empty()) return;
        if (radius <= 0 || r.width < 6 || r.height < 6) {
            draw_rect(r, color);
            return;
        }
        // Subtelne zaokraglenie katow w oparciu o prostokaty wypelniajace
        // Czesc srodkowa
        draw_rect(Rect(r.x + radius, r.y, r.width - 2 * radius, r.height), color);
        // Boki
        draw_rect(Rect(r.x, r.y + radius, radius, r.height - 2 * radius), color);
        draw_rect(Rect(r.x + r.width - radius, r.y + radius, radius, r.height - 2 * radius), color);
    }

    void draw_rounded_border(Rect r, uint32_t color, int radius = 2, int thickness = 1) {
        if (r.is_empty()) return;
        if (radius <= 0 || r.width < 6 || r.height < 6) {
            draw_border(r, color, thickness);
            return;
        }
        // Gorna krawedz
        draw_rect(Rect(r.x + radius, r.y, r.width - 2 * radius, thickness), color);
        // Dolna krawedz
        draw_rect(Rect(r.x + radius, r.y + r.height - thickness, r.width - 2 * radius, thickness), color);
        // Lewa krawedz
        draw_rect(Rect(r.x, r.y + radius, thickness, r.height - 2 * radius), color);
        // Prawa krawedz
        draw_rect(Rect(r.x + r.width - thickness, r.y + radius, thickness, r.height - 2 * radius), color);
        // Narożniki
        if (radius > 1) {
            draw_rect(Rect(r.x + 1, r.y + 1, thickness, thickness), color);
            draw_rect(Rect(r.x + r.width - 1 - thickness, r.y + 1, thickness, thickness), color);
            draw_rect(Rect(r.x + 1, r.y + r.height - 1 - thickness, thickness, thickness), color);
            draw_rect(Rect(r.x + r.width - 1 - thickness, r.y + r.height - 1 - thickness, thickness, thickness), color);
        }
    }

    void draw_text(Point pt, const char* text, uint32_t color, int scale = 1) {
        if (!text || text[0] == '\0') return;
        Point off = get_offset();
        int tx = pt.x + off.x;
        int ty = pt.y + off.y;
        gui_wypisz_tekst_kolor_skala(tx, ty, color, scale, text);
    }

    void draw_text_centered(Rect r, const char* text, uint32_t color, int scale = 1) {
        if (!text || text[0] == '\0' || r.is_empty()) return;
        Size sz = measure_text(text, scale);
        int tx = r.x + (r.width - sz.width) / 2;
        int ty = r.y + (r.height - sz.height) / 2;
        if (tx < r.x) tx = r.x;
        if (ty < r.y) ty = r.y;
        draw_text(Point(tx, ty), text, color, scale);
    }

    Size measure_text(const char* text, int scale = 1) const {
        if (!text || text[0] == '\0') return {0, 0};
        int w = oblicz_szerokosc_tekstu(text, scale);
        int h = gui_pobierz_wysokosc_fontu() * scale;
        return {w, h};
    }

    int get_font_height(int scale = 1) const {
        return gui_pobierz_wysokosc_fontu() * scale;
    }

private:
    static constexpr size_t MAX_STACK = 16;
    Rect clip_stack_[MAX_STACK];
    Point offset_stack_[MAX_STACK];
    size_t clip_depth_{0};
    size_t offset_depth_{0};
};

} // namespace BursztynUI
