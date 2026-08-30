#pragma once

#include "widget.h"

namespace BursztynUI {

class Label : public Widget {
public:
    static constexpr size_t MAX_TEXT_LEN = 256;

    Label(const char* initial_text = "") {
        style_ = Theme::get_default().label_style;
        padding_ = style_.padding;
        h_align_ = HorizontalAlignment::Start;
        v_align_ = VerticalAlignment::Start;
        set_text(initial_text);
    }

    const char* get_text() const { return text_; }
    void set_text(const char* new_text) {
        if (!new_text) new_text = "";
        size_t i = 0;
        for (; i < MAX_TEXT_LEN - 1 && new_text[i] != '\0'; ++i) {
            text_[i] = new_text[i];
        }
        text_[i] = '\0';
        invalidate();
    }

    uint32_t get_text_color() const { return text_color_; }
    void set_text_color(uint32_t c) { text_color_ = c; invalidate(); }

    int get_font_scale() const { return font_scale_; }
    void set_font_scale(int s) { font_scale_ = s < 1 ? 1 : s; invalidate(); }

    HorizontalAlignment get_text_alignment() const { return text_align_; }
    void set_text_alignment(HorizontalAlignment a) { text_align_ = a; invalidate(); }

    Size measure(Size available_size) override {
        (void)available_size;
        int scale = font_scale_ * dpi_scale_;
        int text_w = oblicz_szerokosc_tekstu(text_, scale);
        int text_h = gui_pobierz_wysokosc_fontu() * scale;

        Size desired(text_w + padding_.horizontal() * dpi_scale_, text_h + padding_.vertical() * dpi_scale_);

        if (preferred_size_.width >= 0) desired.width = preferred_size_.width * dpi_scale_;
        if (preferred_size_.height >= 0) desired.height = preferred_size_.height * dpi_scale_;

        desired.width = clamp(desired.width, min_size_.width * dpi_scale_, max_size_.width * dpi_scale_);
        desired.height = clamp(desired.height, min_size_.height * dpi_scale_, max_size_.height * dpi_scale_);

        return desired;
    }

    void paint_self(DrawingContext& ctx) override {
        if (text_[0] == '\0') return;

        int scale = font_scale_ * dpi_scale_;
        uint32_t col = (text_color_ != 0) ? text_color_ :
                       (is_enabled_ ? style_.normal.foreground : style_.disabled.foreground);

        int text_w = oblicz_szerokosc_tekstu(text_, scale);
        int text_h = gui_pobierz_wysokosc_fontu() * scale;

        int pad_l = padding_.left * dpi_scale_;
        int pad_t = padding_.top * dpi_scale_;
        int pad_r = padding_.right * dpi_scale_;
        int pad_b = padding_.bottom * dpi_scale_;

        int client_w = bounds_.width - (pad_l + pad_r);
        int client_h = bounds_.height - (pad_t + pad_b);

        int tx = pad_l;
        if (text_align_ == HorizontalAlignment::Center) {
            tx = pad_l + (client_w - text_w) / 2;
        } else if (text_align_ == HorizontalAlignment::End) {
            tx = bounds_.width - pad_r - text_w;
        }
        if (tx < pad_l) tx = pad_l;

        int ty = pad_t + (client_h - text_h) / 2;
        if (ty < pad_t) ty = pad_t;

        ctx.draw_text(Point(tx, ty), text_, col, scale);
    }

private:
    char text_[MAX_TEXT_LEN]{0};
    uint32_t text_color_{0};
    int font_scale_{1};
    HorizontalAlignment text_align_{HorizontalAlignment::Start};
    WidgetStyle style_;
};

} // namespace BursztynUI
