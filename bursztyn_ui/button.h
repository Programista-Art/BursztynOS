#pragma once

#include "widget.h"

namespace BursztynUI {

class Button : public Widget {
public:
    using ClickCallback = void (*)(Button* sender, void* user_data);

    Button(const char* label = "Przycisk") {
        style_ = Theme::get_default().button_style;
        padding_ = style_.padding;
        h_align_ = HorizontalAlignment::Stretch;
        v_align_ = VerticalAlignment::Start;
        is_focusable_ = true;
        set_text(label);
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

    void set_on_click(ClickCallback cb, void* user_data = nullptr) {
        on_click_ = cb;
        user_data_ = user_data;
    }

    WidgetStyle& get_style() { return style_; }
    void set_style(const WidgetStyle& s) { style_ = s; invalidate(); }

    Size measure(Size available_size) override {
        (void)available_size;
        int scale = dpi_scale_;
        int text_w = oblicz_szerokosc_tekstu(text_, scale);
        int text_h = gui_pobierz_wysokosc_fontu() * scale;

        int natural_h = text_h + padding_.vertical() * scale + 8 * scale;
        if (natural_h < 28 * scale) natural_h = 28 * scale;
        int natural_w = text_w + padding_.horizontal() * scale + 16 * scale;

        Size desired(natural_w, natural_h);

        if (preferred_size_.width >= 0) desired.width = preferred_size_.width * scale;
        if (preferred_size_.height >= 0) desired.height = preferred_size_.height * scale;

        desired.width = clamp(desired.width, min_size_.width * scale, max_size_.width * scale);
        desired.height = clamp(desired.height, min_size_.height * scale, max_size_.height * scale);

        return desired;
    }

    void paint_self(DrawingContext& ctx) override {
        int scale = dpi_scale_;
        const StateStyle* cur = &style_.normal;
        if (!is_enabled_) {
            cur = &style_.disabled;
        } else if (is_pressed_) {
            cur = &style_.pressed;
        } else if (is_hovered_) {
            cur = &style_.hover;
        } else if (is_focused_) {
            cur = &style_.focused;
        }

        Rect r(0, 0, bounds_.width, bounds_.height);

        // Tlo
        if (cur->background != 0) {
            if (cur->corner_radius > 0) {
                ctx.draw_rounded_rect(r, cur->background, cur->corner_radius * scale);
            } else {
                ctx.draw_rect(r, cur->background);
            }
        }

        // Ramka
        if (cur->border != 0 && cur->border_thickness > 0) {
            if (cur->corner_radius > 0) {
                ctx.draw_rounded_border(r, cur->border, cur->corner_radius * scale, cur->border_thickness * scale);
            } else {
                ctx.draw_border(r, cur->border, cur->border_thickness * scale);
            }
        }

        // Tekst
        ctx.draw_text_centered(r, text_, cur->foreground, scale);
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
            invalidate();
            if (bounds_.contains(e.x, e.y) || screen_bounds_.contains(e.x, e.y)) {
                if (on_click_) {
                    on_click_(this, user_data_);
                }
            }
            e.handled = true;
        }
    }

    void on_key_down(KeyEvent& e) override {
        if (!is_enabled_) return;
        // Space (32) lub Enter (13 / 10)
        if (e.keycode == 32 || e.keycode == 13 || e.keycode == 10) {
            if (on_click_) {
                on_click_(this, user_data_);
            }
            e.handled = true;
        }
    }

private:
    char text_[64]{0};
    WidgetStyle style_;
    ClickCallback on_click_{nullptr};
    void* user_data_{nullptr};
};

} // namespace BursztynUI
