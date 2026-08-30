#pragma once

#include "widget.h"

namespace BursztynUI {

class TextBox : public Widget {
public:
    static constexpr size_t MAX_BUF = 256;
    using TextChangedCallback = void (*)(TextBox* sender, const char* text, void* user_data);

    TextBox(const char* placeholder = "Wpisz tekst...") {
        style_ = Theme::get_default().textbox_style;
        padding_ = style_.padding;
        h_align_ = HorizontalAlignment::Stretch;
        v_align_ = VerticalAlignment::Start;
        is_focusable_ = true;
        set_placeholder(placeholder);
    }

    const char* get_text() const { return buffer_; }
    void set_text(const char* new_text) {
        if (!new_text) new_text = "";
        size_t i = 0;
        for (; i < MAX_BUF - 1 && new_text[i] != '\0'; ++i) {
            buffer_[i] = new_text[i];
        }
        buffer_[i] = '\0';
        length_ = i;
        cursor_byte_ = length_;
        invalidate();
        notify_changed();
    }

    const char* get_placeholder() const { return placeholder_; }
    void set_placeholder(const char* ph) {
        if (!ph) ph = "";
        size_t i = 0;
        for (; i < sizeof(placeholder_) - 1 && ph[i] != '\0'; ++i) {
            placeholder_[i] = ph[i];
        }
        placeholder_[i] = '\0';
        invalidate();
    }

    using SubmitCallback = void (*)(TextBox* sender, const char* text, void* user_data);

    void set_on_text_changed(TextChangedCallback cb, void* user_data = nullptr) {
        on_text_changed_ = cb;
        user_data_ = user_data;
    }

    void set_on_submit(SubmitCallback cb, void* user_data = nullptr) {
        on_submit_ = cb;
        submit_user_data_ = user_data;
    }

    Size measure(Size available_size) override {
        (void)available_size;
        int scale = dpi_scale_;
        int font_h = gui_pobierz_wysokosc_fontu() * scale;
        int text_w = oblicz_szerokosc_tekstu(buffer_[0] ? buffer_ : placeholder_, scale);

        int min_w = 120 * scale;
        int desired_w = text_w + padding_.horizontal() * scale + 16 * scale;
        if (desired_w < min_w) desired_w = min_w;

        int natural_h = font_h + padding_.vertical() * scale + 6 * scale;
        if (natural_h < 26 * scale) natural_h = 26 * scale;

        Size desired(desired_w, natural_h);

        if (preferred_size_.width >= 0) desired.width = preferred_size_.width * scale;
        if (preferred_size_.height >= 0) desired.height = preferred_size_.height * scale;

        desired.width = clamp(desired.width, min_size_.width * scale, max_size_.width * scale);
        desired.height = clamp(desired.height, min_size_.height * scale, max_size_.height * scale);

        return desired;
    }

    void paint_self(DrawingContext& ctx) override {
        int scale = dpi_scale_;
        Rect r(0, 0, bounds_.width, bounds_.height);

        const StateStyle* cur = &style_.normal;
        if (!is_enabled_) {
            cur = &style_.disabled;
        } else if (is_focused_) {
            cur = &style_.focused;
        } else if (is_hovered_) {
            cur = &style_.hover;
        }

        // Tlo pola
        ctx.draw_rect(r, cur->background);
        // Ramka pola
        ctx.draw_border(r, cur->border, cur->border_thickness * scale);

        // Tekst lub Placeholder
        int pad_l = padding_.left * scale;
        int pad_t = padding_.top * scale;
        int pad_v = padding_.vertical() * scale;

        int client_x = pad_l;
        int font_h = gui_pobierz_wysokosc_fontu() * scale;
        int client_y = pad_t + (bounds_.height - pad_v - font_h) / 2;
        if (client_y < pad_t) client_y = pad_t;

        if (length_ == 0) {
            if (placeholder_[0] != '\0') {
                uint32_t ph_col = is_focused_ ? Colors::TextMuted : Colors::TextPlaceholder;
                ctx.draw_text(Point(client_x, client_y), placeholder_, ph_col, scale);
            }
        } else {
            uint32_t fg = is_enabled_ ? cur->foreground : Colors::DisabledFg;
            ctx.draw_text(Point(client_x, client_y), buffer_, fg, scale);
        }

        // Rysuj karetke kursora gdy focused
        if (is_focused_) {
            char temp[MAX_BUF];
            size_t c_len = (cursor_byte_ < length_) ? cursor_byte_ : length_;
            for (size_t i = 0; i < c_len; ++i) temp[i] = buffer_[i];
            temp[c_len] = '\0';

            int cursor_x = client_x + oblicz_szerokosc_tekstu(temp, scale);
            int cursor_h = font_h;
            Rect cursor_r(cursor_x, client_y, 2 * scale, cursor_h);
            ctx.draw_rect(cursor_r, Colors::AccentFocus);
        }
    }

    void on_mouse_down(MouseEvent& e) override {
        if (!is_enabled_ || !e.is_left_button()) return;
        cursor_byte_ = length_;
        invalidate();
        e.handled = true;
    }

    void on_text_input(TextInputEvent& e) override {
        if (!is_enabled_) return;
        if (e.codepoint >= 32) {
            char utf8_bytes[5]{0};
            size_t bytes = encode_utf8(e.codepoint, utf8_bytes);
            if (bytes > 0) {
                insert_bytes(utf8_bytes, bytes);
                e.handled = true;
            }
        }
    }

    void on_key_down(KeyEvent& e) override {
        if (!is_enabled_) return;

        // Strzalka w lewo (kod 0x4B lub 2 / scancode E0 4B)
        if (e.keycode == 0x4B || e.scancode == 0x4B || e.keycode == 2) {
            move_cursor_left();
            e.handled = true;
            return;
        }
        // Strzalka w prawo (kod 0x4D lub 3 / scancode E0 4D)
        if (e.keycode == 0x4D || e.scancode == 0x4D || e.keycode == 3) {
            move_cursor_right();
            e.handled = true;
            return;
        }
        // Home (0x47 lub scancode 0x47)
        if (e.keycode == 0x47 || e.scancode == 0x47) {
            cursor_byte_ = 0;
            invalidate();
            e.handled = true;
            return;
        }
        // End (0x4F lub scancode 0x4F)
        if (e.keycode == 0x4F || e.scancode == 0x4F) {
            cursor_byte_ = length_;
            invalidate();
            e.handled = true;
            return;
        }
        // Backspace (8)
        if (e.keycode == 8) {
            delete_backspace();
            e.handled = true;
            return;
        }
        // Delete (127 lub 0x53)
        if (e.keycode == 127 || e.keycode == 0x53 || e.scancode == 0x53) {
            delete_forward();
            e.handled = true;
            return;
        }

        // Enter (13, 10 lub scancode 0x1C)
        if (e.keycode == 13 || e.keycode == 10 || e.scancode == 0x1C) {
            if (on_submit_) {
                on_submit_(this, buffer_, submit_user_data_);
            }
            e.handled = true;
            return;
        }
    }

private:
    static size_t encode_utf8(uint32_t cp, char* out) {
        if (cp <= 0x7F) {
            out[0] = static_cast<char>(cp);
            out[1] = '\0';
            return 1;
        } else if (cp <= 0x7FF) {
            out[0] = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
            out[1] = static_cast<char>(0x80 | (cp & 0x3F));
            out[2] = '\0';
            return 2;
        } else if (cp <= 0xFFFF) {
            out[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
            out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[2] = static_cast<char>(0x80 | (cp & 0x3F));
            out[3] = '\0';
            return 3;
        } else if (cp <= 0x10FFFF) {
            out[0] = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
            out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (cp & 0x3F));
            out[4] = '\0';
            return 4;
        }
        return 0;
    }

    void insert_bytes(const char* bytes, size_t count) {
        if (length_ + count >= MAX_BUF) return;
        for (size_t i = length_ + count; i > cursor_byte_ + count - 1; --i) {
            buffer_[i] = buffer_[i - count];
        }
        for (size_t i = 0; i < count; ++i) {
            buffer_[cursor_byte_ + i] = bytes[i];
        }
        length_ += count;
        cursor_byte_ += count;
        buffer_[length_] = '\0';
        invalidate();
        notify_changed();
    }

    void delete_backspace() {
        if (cursor_byte_ == 0 || length_ == 0) return;
        size_t prev_byte = cursor_byte_ - 1;
        while (prev_byte > 0 && (static_cast<uint8_t>(buffer_[prev_byte]) & 0xC0) == 0x80) {
            prev_byte--;
        }
        size_t char_len = cursor_byte_ - prev_byte;
        for (size_t i = prev_byte; i + char_len <= length_; ++i) {
            buffer_[i] = buffer_[i + char_len];
        }
        length_ -= char_len;
        cursor_byte_ = prev_byte;
        buffer_[length_] = '\0';
        invalidate();
        notify_changed();
    }

    void delete_forward() {
        if (cursor_byte_ >= length_) return;
        size_t next_byte = cursor_byte_ + 1;
        while (next_byte < length_ && (static_cast<uint8_t>(buffer_[next_byte]) & 0xC0) == 0x80) {
            next_byte++;
        }
        size_t char_len = next_byte - cursor_byte_;
        for (size_t i = cursor_byte_; i + char_len <= length_; ++i) {
            buffer_[i] = buffer_[i + char_len];
        }
        length_ -= char_len;
        buffer_[length_] = '\0';
        invalidate();
        notify_changed();
    }

    void move_cursor_left() {
        if (cursor_byte_ == 0) return;
        cursor_byte_--;
        while (cursor_byte_ > 0 && (static_cast<uint8_t>(buffer_[cursor_byte_]) & 0xC0) == 0x80) {
            cursor_byte_--;
        }
        invalidate();
    }

    void move_cursor_right() {
        if (cursor_byte_ >= length_) return;
        cursor_byte_++;
        while (cursor_byte_ < length_ && (static_cast<uint8_t>(buffer_[cursor_byte_]) & 0xC0) == 0x80) {
            cursor_byte_++;
        }
        invalidate();
    }

    void notify_changed() {
        if (on_text_changed_) {
            on_text_changed_(this, buffer_, user_data_);
        }
    }

    char buffer_[MAX_BUF]{0};
    char placeholder_[64]{0};
    size_t length_{0};
    size_t cursor_byte_{0};

    WidgetStyle style_;
    TextChangedCallback on_text_changed_{nullptr};
    void* user_data_{nullptr};
    SubmitCallback on_submit_{nullptr};
    void* submit_user_data_{nullptr};
};

} // namespace BursztynUI
