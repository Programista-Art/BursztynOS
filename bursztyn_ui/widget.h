#pragma once

#include <stdint.h>
#include <stddef.h>
#include "types.h"
#include "theme.h"
#include "events.h"
#include "drawing.h"

namespace BursztynUI {

class Widget {
public:
    static constexpr size_t MAX_CHILDREN = 32;

    Widget() = default;
    virtual ~Widget() {
        for (size_t i = 0; i < child_count_; ++i) {
            if (children_[i]) {
                children_[i]->parent_ = nullptr;
            }
        }
    }

    // Drzewo widgetów
    Widget* get_parent() const { return parent_; }
    size_t get_child_count() const { return child_count_; }
    Widget* get_child(size_t index) const {
        return (index < child_count_) ? children_[index] : nullptr;
    }

    bool add_child(Widget* child) {
        if (!child || child == this || child_count_ >= MAX_CHILDREN) return false;
        // Unikaj duplikatow
        for (size_t i = 0; i < child_count_; ++i) {
            if (children_[i] == child) return true;
        }
        child->parent_ = this;
        child->set_dpi_scale(dpi_scale_);
        children_[child_count_++] = child;
        invalidate();
        return true;
    }

    bool remove_child(Widget* child) {
        if (!child) return false;
        for (size_t i = 0; i < child_count_; ++i) {
            if (children_[i] == child) {
                child->parent_ = nullptr;
                for (size_t j = i; j < child_count_ - 1; ++j) {
                    children_[j] = children_[j + 1];
                }
                children_[--child_count_] = nullptr;
                invalidate();
                return true;
            }
        }
        return false;
    }

    // Geometria
    Rect get_bounds() const { return bounds_; }
    Rect get_screen_bounds() const { return screen_bounds_; }

    Size get_min_size() const { return min_size_; }
    void set_min_size(Size s) { min_size_ = s; invalidate(); }

    Size get_max_size() const { return max_size_; }
    void set_max_size(Size s) { max_size_ = s; invalidate(); }

    Size get_preferred_size() const { return preferred_size_; }
    void set_preferred_size(Size s) { preferred_size_ = s; invalidate(); }

    Thickness get_margin() const { return margin_; }
    void set_margin(Thickness m) { margin_ = m; invalidate(); }

    Thickness get_padding() const { return padding_; }
    void set_padding(Thickness p) { padding_ = p; invalidate(); }

    HorizontalAlignment get_horizontal_alignment() const { return h_align_; }
    void set_horizontal_alignment(HorizontalAlignment a) { h_align_ = a; invalidate(); }

    VerticalAlignment get_vertical_alignment() const { return v_align_; }
    void set_vertical_alignment(VerticalAlignment a) { v_align_ = a; invalidate(); }

    // Stany
    bool is_visible() const { return is_visible_; }
    void set_visible(bool v) {
        if (is_visible_ != v) {
            is_visible_ = v;
            invalidate();
        }
    }

    bool is_enabled() const { return is_enabled_; }
    void set_enabled(bool e) {
        if (is_enabled_ != e) {
            is_enabled_ = e;
            invalidate();
        }
    }

    bool is_hovered() const { return is_hovered_; }
    bool is_pressed() const { return is_pressed_; }
    bool is_focused() const { return is_focused_; }
    bool is_dirty() const { return is_dirty_; }
    bool is_focusable() const { return is_focusable_ && is_visible_ && is_enabled_; }

    void set_focusable(bool f) { is_focusable_ = f; }

    int get_dpi_scale() const { return dpi_scale_; }
    virtual void set_dpi_scale(int scale) {
        dpi_scale_ = scale < 1 ? 1 : scale;
        for (size_t i = 0; i < child_count_; ++i) {
            if (children_[i]) children_[i]->set_dpi_scale(dpi_scale_);
        }
        invalidate();
    }

    void invalidate() {
        is_dirty_ = true;
        if (parent_) {
            parent_->invalidate();
        }
    }

    // Silnik Layoutu
    virtual Size measure(Size available_size) {
        (void)available_size;
        // Domyślna implementacja mierzy preferred/min/max
        Size desired = (preferred_size_.width >= 0 && preferred_size_.height >= 0)
                       ? preferred_size_ : Size(0, 0);
        desired.width = (desired.width < min_size_.width) ? min_size_.width : desired.width;
        desired.height = (desired.height < min_size_.height) ? min_size_.height : desired.height;

        desired.width = (desired.width > max_size_.width) ? max_size_.width : desired.width;
        desired.height = (desired.height > max_size_.height) ? max_size_.height : desired.height;

        desired.width += margin_.horizontal();
        desired.height += margin_.vertical();
        return desired;
    }

    virtual void arrange(Rect final_rect) {
        bounds_ = final_rect;
        Point parent_pos = parent_ ? parent_->screen_bounds_.position() : Point(0, 0);
        screen_bounds_ = Rect(parent_pos.x + bounds_.x, parent_pos.y + bounds_.y,
                              bounds_.width, bounds_.height);

        // Uporzadkuj dzieci domyslnie w calym screen_bounds
        Rect client = Rect(padding_.left, padding_.top,
                           bounds_.width - padding_.horizontal() < 0 ? 0 : bounds_.width - padding_.horizontal(),
                           bounds_.height - padding_.vertical() < 0 ? 0 : bounds_.height - padding_.vertical());

        for (size_t i = 0; i < child_count_; ++i) {
            if (children_[i] && children_[i]->is_visible()) {
                children_[i]->arrange(client);
            }
        }
    }

    // Silnik Renderowania
    virtual void paint(DrawingContext& ctx, bool force = false) {
        if (!is_visible_) return;
        bool should_paint = is_dirty_ || force;

        if (should_paint) {
            // Narysuj wlasne tlo/elementy
            ctx.push_offset(Point(bounds_.x, bounds_.y));
            ctx.push_clip(Rect(0, 0, bounds_.width, bounds_.height));
            paint_self(ctx);
            ctx.pop_clip();
            ctx.pop_offset();
        }

        // Narysuj dzieci
        ctx.push_offset(Point(bounds_.x, bounds_.y));
        for (size_t i = 0; i < child_count_; ++i) {
            if (children_[i] && children_[i]->is_visible()) {
                children_[i]->paint(ctx, should_paint);
            }
        }
        ctx.pop_offset();

        is_dirty_ = false;
    }

    virtual void paint_self(DrawingContext& ctx) {
        (void)ctx;
        // Domyślnie brak tła dla czystego widgetu
    }

    // Hit Testing (od góry do dołu)
    virtual Widget* hit_test(Point pt) {
        if (!is_visible_ || !bounds_.contains(pt)) return nullptr;

        Point local_pt(pt.x - bounds_.x, pt.y - bounds_.y);
        // Sprawdz dzieci od ostatniego (na wierzchu) do pierwszego
        for (size_t i = child_count_; i > 0; --i) {
            Widget* child = children_[i - 1];
            if (child && child->is_visible()) {
                Widget* hit = child->hit_test(local_pt);
                if (hit) return hit;
            }
        }
        return this;
    }

    // Zdarzenia myszy
    virtual void on_mouse_enter() {
        is_hovered_ = true;
        invalidate();
    }

    virtual void on_mouse_leave() {
        is_hovered_ = false;
        is_pressed_ = false;
        invalidate();
    }

    virtual void on_mouse_move(MouseEvent& e) { (void)e; }
    virtual void on_mouse_down(MouseEvent& e) {
        (void)e;
        is_pressed_ = true;
        invalidate();
    }

    virtual void on_mouse_up(MouseEvent& e) {
        (void)e;
        is_pressed_ = false;
        invalidate();
    }

    virtual void on_mouse_wheel(MouseEvent& e) { (void)e; }

    // Zdarzenia klawiatury
    virtual void on_key_down(KeyEvent& e) { (void)e; }
    virtual void on_key_up(KeyEvent& e) { (void)e; }
    virtual void on_text_input(TextInputEvent& e) { (void)e; }

    // Focus
    virtual void on_focus() {
        is_focused_ = true;
        invalidate();
    }

    virtual void on_blur() {
        is_focused_ = false;
        invalidate();
    }

protected:
    Widget* parent_{nullptr};
    Widget* children_[MAX_CHILDREN]{nullptr};
    size_t child_count_{0};

    Rect bounds_{0, 0, 0, 0};
    Rect screen_bounds_{0, 0, 0, 0};

    Size min_size_{0, 0};
    Size max_size_{10000, 10000};
    Size preferred_size_{-1, -1};

    Thickness margin_{0, 0, 0, 0};
    Thickness padding_{0, 0, 0, 0};

    HorizontalAlignment h_align_{HorizontalAlignment::Stretch};
    VerticalAlignment v_align_{VerticalAlignment::Start};

    bool is_visible_{true};
    bool is_enabled_{true};
    bool is_hovered_{false};
    bool is_pressed_{false};
    bool is_focused_{false};
    bool is_dirty_{true};
    bool is_focusable_{false};
    int dpi_scale_{1};
};

} // namespace BursztynUI
