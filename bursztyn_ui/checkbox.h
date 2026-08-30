#pragma once

#include "widget.h"

namespace BursztynUI {

class CheckBox : public Widget {
public:
    using ToggleCallback = void (*)(CheckBox* sender, bool is_checked, void* user_data);

    CheckBox(const char* label = "Opcja", bool initial_checked = false)
        : is_checked_(initial_checked) {
        style_ = Theme::get_default().checkbox_style;
        padding_ = style_.padding;
        h_align_ = HorizontalAlignment::Start;
        v_align_ = VerticalAlignment::Start;
        is_focusable_ = true;
        set_text(label);
    }

    bool is_checked() const { return is_checked_; }
    void set_checked(bool c) {
        if (is_checked_ != c) {
            is_checked_ = c;
            invalidate();
            if (on_toggled_) {
                on_toggled_(this, is_checked_, user_data_);
            }
        }
    }

    const char* get_text() const { return text_; }
    void set_text(const char* new_text) {
        if (!new_text) new_text = "";
        size_t i = 0;
        for (; i < sizeof(text_) - 1 && new_text[i] != '\0'; ++i) {
            text_[i] = new_text[i];
        }
        text_[i] = '\0';
        invalidate();
    }

    void set_on_toggled(ToggleCallback cb, void* user_data = nullptr) {
        on_toggled_ = cb;
        user_data_ = user_data;
    }

    Size measure(Size available_size) override {
        (void)available_size;
        int scale = dpi_scale_;
        int box_sz = 16 * scale;
        int text_w = (text_[0] != '\0') ? (oblicz_szerokosc_tekstu(text_, scale) + 6 * scale) : 0;
        int text_h = gui_pobierz_wysokosc_fontu() * scale;

        int total_w = box_sz + text_w + padding_.horizontal() * scale;
        int total_h = (box_sz > text_h ? box_sz : text_h) + padding_.vertical() * scale;

        Size desired(total_w, total_h);

        if (preferred_size_.width >= 0) desired.width = preferred_size_.width * scale;
        if (preferred_size_.height >= 0) desired.height = preferred_size_.height * scale;

        desired.width = clamp(desired.width, min_size_.width * scale, max_size_.width * scale);
        desired.height = clamp(desired.height, min_size_.height * scale, max_size_.height * scale);

        return desired;
    }

    void paint_self(DrawingContext& ctx) override {
        int scale = dpi_scale_;
        int box_sz = 16 * scale;

        int pad_l = padding_.left * scale;
        int pad_t = padding_.top * scale;
        int pad_v = padding_.vertical() * scale;

        int box_x = pad_l;
        int box_y = pad_t + (bounds_.height - pad_v - box_sz) / 2;
        if (box_y < pad_t) box_y = pad_t;

        Rect box_r(box_x, box_y, box_sz, box_sz);

        const StateStyle* cur = &style_.normal;
        if (!is_enabled_) {
            cur = &style_.disabled;
        } else if (is_focused_) {
            cur = &style_.focused;
        } else if (is_hovered_) {
            cur = &style_.hover;
        }

        // Tlo kratki
        ctx.draw_rect(box_r, cur->background);
        // Ramka kratki
        ctx.draw_border(box_r, cur->border, cur->border_thickness * scale);

        // Znacznik zaznaczenia
        if (is_checked_) {
            int inner_pad = 3 * scale;
            Rect check_r(box_r.x + inner_pad, box_r.y + inner_pad,
                         box_r.width - 2 * inner_pad, box_r.height - 2 * inner_pad);
            if (!check_r.is_empty()) {
                uint32_t check_col = is_enabled_ ? Colors::CheckBoxCheck : Colors::DisabledFg;
                ctx.draw_rect(check_r, check_col);
            }
        }

        // Etykieta obok kratki
        if (text_[0] != '\0') {
            int text_x = box_x + box_sz + 6 * scale;
            int font_h = gui_pobierz_wysokosc_fontu() * scale;
            int text_y = pad_t + (bounds_.height - pad_v - font_h) / 2;
            if (text_y < pad_t) text_y = pad_t;

            uint32_t fg = is_enabled_ ? cur->foreground : Colors::DisabledFg;
            ctx.draw_text(Point(text_x, text_y), text_, fg, scale);
        }
    }

    void on_mouse_enter() override {
        is_hovered_ = true;
        invalidate();
    }

    void on_mouse_leave() override {
        is_hovered_ = false;
        is_pressed_ = false;
        invalidate();
    }

    void on_mouse_down(MouseEvent& e) override {
        if (!is_enabled_ || !e.is_left_button()) return;
        is_pressed_ = true;
        invalidate();
        e.handled = true;
    }

    void on_mouse_up(MouseEvent& e) override {
        if (!is_enabled_) return;
        if (is_pressed_) {
            is_pressed_ = false;
            set_checked(!is_checked_);
            e.handled = true;
        }
    }

    void on_key_down(KeyEvent& e) override {
        if (!is_enabled_) return;
        if (e.keycode == 32 || e.keycode == 13 || e.keycode == 10) { // Spacja / Enter
            set_checked(!is_checked_);
            e.handled = true;
        }
    }

private:
    char text_[64]{0};
    bool is_checked_{false};
    WidgetStyle style_;
    ToggleCallback on_toggled_{nullptr};
    void* user_data_{nullptr};
};

} // namespace BursztynUI
