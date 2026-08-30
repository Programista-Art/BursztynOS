#pragma once

#include "widget.h"

namespace BursztynUI {

class Panel : public Widget {
public:
    Panel(Orientation orientation = Orientation::Vertical)
        : orientation_(orientation) {
        style_ = Theme::get_default().panel_style;
        padding_ = style_.padding;
        h_align_ = HorizontalAlignment::Stretch;
        v_align_ = VerticalAlignment::Stretch;
    }

    Orientation get_orientation() const { return orientation_; }
    void set_orientation(Orientation o) {
        if (orientation_ != o) {
            orientation_ = o;
            invalidate();
        }
    }

    int get_spacing() const { return spacing_; }
    void set_spacing(int s) {
        if (spacing_ != s) {
            spacing_ = s < 0 ? 0 : s;
            invalidate();
        }
    }

    WidgetStyle& get_style() { return style_; }
    void set_style(const WidgetStyle& s) { style_ = s; invalidate(); }

    Size measure(Size available_size) override {
        int pad_h = padding_.horizontal() * dpi_scale_;
        int pad_v = padding_.vertical() * dpi_scale_;
        int mar_h = margin_.horizontal() * dpi_scale_;
        int mar_v = margin_.vertical() * dpi_scale_;

        int inner_w = available_size.width - mar_h - pad_h;
        int inner_h = available_size.height - mar_v - pad_v;
        if (inner_w < 0) inner_w = 0;
        if (inner_h < 0) inner_h = 0;

        int total_primary = 0;
        int max_cross = 0;
        int visible_count = 0;

        for (size_t i = 0; i < child_count_; ++i) {
            Widget* child = children_[i];
            if (!child || !child->is_visible()) continue;

            visible_count++;
            Thickness cm = child->get_margin();
            int cm_h = cm.horizontal() * dpi_scale_;
            int cm_v = cm.vertical() * dpi_scale_;

            Size child_avail(inner_w - cm_h > 0 ? inner_w - cm_h : 0,
                             inner_h - cm_v > 0 ? inner_h - cm_v : 0);
            Size measured = child->measure(child_avail);

            if (orientation_ == Orientation::Vertical) {
                total_primary += measured.height + cm_v;
                int child_w_with_margin = measured.width + cm_h;
                if (child_w_with_margin > max_cross) max_cross = child_w_with_margin;
            } else {
                total_primary += measured.width + cm_h;
                int child_h_with_margin = measured.height + cm_v;
                if (child_h_with_margin > max_cross) max_cross = child_h_with_margin;
            }
        }

        if (visible_count > 1) {
            total_primary += (visible_count - 1) * spacing_ * dpi_scale_;
        }

        Size desired;
        if (orientation_ == Orientation::Vertical) {
            desired.width = max_cross + pad_h;
            desired.height = total_primary + pad_v;
        } else {
            desired.width = total_primary + pad_h;
            desired.height = max_cross + pad_v;
        }

        if (preferred_size_.width >= 0) desired.width = preferred_size_.width * dpi_scale_;
        if (preferred_size_.height >= 0) desired.height = preferred_size_.height * dpi_scale_;

        desired.width = clamp(desired.width, min_size_.width * dpi_scale_, max_size_.width * dpi_scale_);
        desired.height = clamp(desired.height, min_size_.height * dpi_scale_, max_size_.height * dpi_scale_);

        desired.width += mar_h;
        desired.height += mar_v;
        return desired;
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        Point parent_pos = parent_ ? parent_->get_screen_bounds().position() : Point(0, 0);
        screen_bounds_ = Rect(parent_pos.x + bounds_.x, parent_pos.y + bounds_.y,
                              bounds_.width, bounds_.height);

        int pad_l = padding_.left * dpi_scale_;
        int pad_t = padding_.top * dpi_scale_;
        int pad_r = padding_.right * dpi_scale_;
        int pad_b = padding_.bottom * dpi_scale_;

        int client_x = pad_l;
        int client_y = pad_t;
        int client_w = bounds_.width - (pad_l + pad_r);
        int client_h = bounds_.height - (pad_t + pad_b);
        if (client_w < 0) client_w = 0;
        if (client_h < 0) client_h = 0;

        int num_visible = 0;
        int fixed_primary = 0;
        int stretch_count = 0;

        for (size_t i = 0; i < child_count_; ++i) {
            Widget* child = children_[i];
            if (!child || !child->is_visible()) continue;
            num_visible++;

            Thickness cm = child->get_margin();
            int cm_h = cm.horizontal() * dpi_scale_;
            int cm_v = cm.vertical() * dpi_scale_;

            Size sz = child->measure(Size(client_w - cm_h > 0 ? client_w - cm_h : 0,
                                          client_h - cm_v > 0 ? client_h - cm_v : 0));

            if (orientation_ == Orientation::Vertical) {
                if (child->get_vertical_alignment() == VerticalAlignment::Stretch) {
                    stretch_count++;
                } else {
                    int h = sz.height;
                    if (child->get_preferred_size().height >= 0) {
                        h = child->get_preferred_size().height * dpi_scale_;
                    }
                    h = clamp(h, child->get_min_size().height * dpi_scale_, child->get_max_size().height * dpi_scale_);
                    fixed_primary += h + cm_v;
                }
            } else {
                if (child->get_horizontal_alignment() == HorizontalAlignment::Stretch) {
                    stretch_count++;
                } else {
                    int w = sz.width;
                    if (child->get_preferred_size().width >= 0) {
                        w = child->get_preferred_size().width * dpi_scale_;
                    }
                    w = clamp(w, child->get_min_size().width * dpi_scale_, child->get_max_size().width * dpi_scale_);
                    fixed_primary += w + cm_h;
                }
            }
        }

        int total_spacing = (num_visible > 1) ? (num_visible - 1) * spacing_ * dpi_scale_ : 0;
        int available_for_stretch = (orientation_ == Orientation::Vertical)
                                   ? (client_h - fixed_primary - total_spacing)
                                   : (client_w - fixed_primary - total_spacing);
        if (available_for_stretch < 0) available_for_stretch = 0;

        int stretch_share = (stretch_count > 0) ? (available_for_stretch / stretch_count) : 0;

        int cur_pos = (orientation_ == Orientation::Vertical) ? client_y : client_x;

        for (size_t i = 0; i < child_count_; ++i) {
            Widget* child = children_[i];
            if (!child || !child->is_visible()) continue;

            Thickness cm = child->get_margin();
            int cml = cm.left * dpi_scale_;
            int cmt = cm.top * dpi_scale_;
            int cmr = cm.right * dpi_scale_;
            int cmb = cm.bottom * dpi_scale_;
            int cm_h = cml + cmr;
            int cm_v = cmt + cmb;

            Size sz = child->measure(Size(client_w - cm_h > 0 ? client_w - cm_h : 0,
                                          client_h - cm_v > 0 ? client_h - cm_v : 0));

            Rect child_rect;
            if (orientation_ == Orientation::Vertical) {
                int child_h = 0;
                if (child->get_vertical_alignment() == VerticalAlignment::Stretch) {
                    child_h = stretch_share - cm_v;
                    if (child_h < 0) child_h = 0;
                } else {
                    child_h = sz.height;
                    if (child->get_preferred_size().height >= 0) {
                        child_h = child->get_preferred_size().height * dpi_scale_;
                    }
                }
                child_h = clamp(child_h, child->get_min_size().height * dpi_scale_, child->get_max_size().height * dpi_scale_);

                int child_w = 0;
                int child_x = client_x + cml;
                int avail_w = client_w - cm_h;
                if (avail_w < 0) avail_w = 0;

                if (child->get_horizontal_alignment() == HorizontalAlignment::Stretch) {
                    child_w = avail_w;
                } else {
                    child_w = sz.width;
                    if (child->get_preferred_size().width >= 0) {
                        child_w = child->get_preferred_size().width * dpi_scale_;
                    }
                    if (child_w > avail_w) child_w = avail_w;

                    if (child->get_horizontal_alignment() == HorizontalAlignment::Center) {
                        child_x = client_x + cml + (avail_w - child_w) / 2;
                    } else if (child->get_horizontal_alignment() == HorizontalAlignment::End) {
                        child_x = client_x + client_w - cmr - child_w;
                    }
                }
                child_w = clamp(child_w, child->get_min_size().width * dpi_scale_, child->get_max_size().width * dpi_scale_);

                child_rect = Rect(child_x, cur_pos + cmt, child_w, child_h);
                cur_pos += child_h + cm_v + spacing_ * dpi_scale_;
            } else {
                int child_w = 0;
                if (child->get_horizontal_alignment() == HorizontalAlignment::Stretch) {
                    child_w = stretch_share - cm_h;
                    if (child_w < 0) child_w = 0;
                } else {
                    child_w = sz.width;
                    if (child->get_preferred_size().width >= 0) {
                        child_w = child->get_preferred_size().width * dpi_scale_;
                    }
                }
                child_w = clamp(child_w, child->get_min_size().width * dpi_scale_, child->get_max_size().width * dpi_scale_);

                int child_h = 0;
                int child_y = client_y + cmt;
                int avail_h = client_h - cm_v;
                if (avail_h < 0) avail_h = 0;

                if (child->get_vertical_alignment() == VerticalAlignment::Stretch) {
                    child_h = avail_h;
                } else {
                    child_h = sz.height;
                    if (child->get_preferred_size().height >= 0) {
                        child_h = child->get_preferred_size().height * dpi_scale_;
                    }
                    if (child_h > avail_h) child_h = avail_h;

                    if (child->get_vertical_alignment() == VerticalAlignment::Center) {
                        child_y = client_y + cmt + (avail_h - child_h) / 2;
                    } else if (child->get_vertical_alignment() == VerticalAlignment::End) {
                        child_y = client_y + client_h - cmb - child_h;
                    }
                }
                child_h = clamp(child_h, child->get_min_size().height * dpi_scale_, child->get_max_size().height * dpi_scale_);

                child_rect = Rect(cur_pos + cml, child_y, child_w, child_h);
                cur_pos += child_w + cm_h + spacing_ * dpi_scale_;
            }

            child->arrange(child_rect);
        }
    }

    void paint_self(DrawingContext& ctx) override {
        const StateStyle& s = is_enabled_ ? style_.normal : style_.disabled;
        if (s.background != 0) {
            if (s.corner_radius > 0) {
                ctx.draw_rounded_rect(Rect(0, 0, bounds_.width, bounds_.height), s.background, s.corner_radius * dpi_scale_);
            } else {
                ctx.draw_rect(Rect(0, 0, bounds_.width, bounds_.height), s.background);
            }
        }
        if (s.border != 0 && s.border_thickness > 0) {
            if (s.corner_radius > 0) {
                ctx.draw_rounded_border(Rect(0, 0, bounds_.width, bounds_.height), s.border, s.corner_radius * dpi_scale_, s.border_thickness * dpi_scale_);
            } else {
                ctx.draw_border(Rect(0, 0, bounds_.width, bounds_.height), s.border, s.border_thickness * dpi_scale_);
            }
        }
    }

private:
    Orientation orientation_{Orientation::Vertical};
    int spacing_{6};
    WidgetStyle style_;
};

class Row : public Panel {
public:
    Row() : Panel(Orientation::Horizontal) {}
};

class Column : public Panel {
public:
    Column() : Panel(Orientation::Vertical) {}
};

} // namespace BursztynUI
