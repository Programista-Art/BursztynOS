#pragma once

#include "widget.h"
#include "panel.h"
#include "events.h"
#include "drawing.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class Window {
public:
    static constexpr int TITLE_BAR_HEIGHT = 28;

    Window(const char* title = "Okno BursztynUI", int width = 480, int height = 320)
        : width_(width), height_(height) {
        set_title(title);
        // Utworz domyslny panel glowny
        default_root_ = new Panel(Orientation::Vertical);
        root_widget_ = default_root_;
    }

    virtual ~Window() {
        if (default_root_) {
            delete default_root_;
            default_root_ = nullptr;
        }
    }

    const char* get_title() const { return title_; }
    void set_title(const char* new_title) {
        if (!new_title) new_title = "";
        size_t i = 0;
        for (; i < sizeof(title_) - 1 && new_title[i] != '\0'; ++i) {
            title_[i] = new_title[i];
        }
        title_[i] = '\0';
        gui_ustaw_tytul_okna(title_);
        is_dirty_ = true;
    }

    int get_x() const { return x_; }
    int get_y() const { return y_; }
    int get_width() const { return width_; }
    int get_height() const { return height_; }

    bool is_maximized() const { return is_maximized_; }
    bool is_minimized() const { return is_minimized_; }
    bool is_dragging() const { return is_dragging_; }

    void set_position(int x, int y) {
        x_ = x;
        y_ = y;
    }

    void set_size(int w, int h) {
        if (w < 100) w = 100;
        if (h < 60) h = 60;
        if (width_ != w || height_ != h) {
            width_ = w;
            height_ = h;
            relayout();
            is_dirty_ = true;
        }
    }

    Widget* get_root() const { return root_widget_; }
    void set_root(Widget* root) {
        if (root_widget_ != root) {
            root_widget_ = root;
            if (root_widget_) {
                root_widget_->set_dpi_scale(dpi_scale_);
            }
            relayout();
            is_dirty_ = true;
        }
    }

    Widget* get_focus() const { return focused_widget_; }
    void set_focus(Widget* w) {
        if (focused_widget_ != w) {
            if (focused_widget_) {
                focused_widget_->on_blur();
            }
            focused_widget_ = w;
            if (focused_widget_) {
                focused_widget_->on_focus();
            }
        }
    }

    Widget* get_mouse_capture() const { return captured_widget_; }
    void set_mouse_capture(Widget* w) { captured_widget_ = w; }
    void release_mouse_capture() { captured_widget_ = nullptr; }

    int get_dpi_scale() const { return dpi_scale_; }
    void set_dpi_scale(int scale) {
        dpi_scale_ = scale < 1 ? 1 : scale;
        if (root_widget_) {
            root_widget_->set_dpi_scale(dpi_scale_);
        }
        relayout();
        is_dirty_ = true;
    }

    void relayout() {
        if (!root_widget_) return;
        int title_h = TITLE_BAR_HEIGHT * dpi_scale_;
        int client_h = height_ - title_h;
        if (client_h < 0) client_h = 0;
        Rect client_area(0, title_h, width_, client_h);
        root_widget_->measure(client_area.size());
        root_widget_->arrange(client_area);
        root_widget_->invalidate();
    }

    void paint(DrawingContext& ctx, bool force = false) {
        if (is_minimized_) return;
        bool should_paint = is_dirty_ || force;

        if (should_paint) {
            // Narysuj standardowa belke tytulowa na pozycji okna
            gui_rysuj_standardowa_belke(x_, y_, width_, title_, is_maximized_);
            // Tlo okna ponizej belki
            int title_h = TITLE_BAR_HEIGHT * dpi_scale_;
            int client_h = height_ - title_h;
            if (client_h > 0) {
                ctx.push_offset(Point(x_, y_));
                ctx.draw_rect(Rect(0, title_h, width_, client_h), Colors::DarkAmberBg);
                ctx.pop_offset();
            }
            is_dirty_ = false;
        }

        // Renderuj drzewo widgetow
        if (root_widget_) {
            ctx.push_offset(Point(x_, y_));
            root_widget_->paint(ctx, should_paint);
            ctx.pop_offset();
        }
    }

    void minimize() {
        is_dragging_ = false;
        gui_ustaw_capture_myszy(false);
        is_minimized_ = gui_minimalizuj_okno();
    }

    void toggle_maximize() {
        is_dragging_ = false;
        gui_ustaw_capture_myszy(false);

        if (!is_maximized_) {
            // Zapisz biezaca geometrie okna
            saved_bounds_ = Rect(x_, y_, width_, height_);

            // Pobierz obszar roboczy (work area) biezacego monitora
            int target_x = 0;
            int target_y = 0;
            int target_w = 800;
            int target_h = 600;

            GuiMonitorInfo mon{};
            if (gui_pobierz_monitor(0, &mon) && mon.work_szer > 0 && mon.work_wys > 0) {
                target_x = mon.work_x;
                target_y = mon.work_y;
                target_w = mon.work_szer;
                target_h = mon.work_wys;
            } else {
                int sw = 800, sh = 600;
                gui_pobierz_rozdzielczosc(&sw, &sh);
                target_x = 0;
                target_y = 0;
                target_w = sw;
                target_h = (sh > 32) ? (sh - 32) : sh;
            }

            x_ = target_x;
            y_ = target_y;
            width_ = target_w;
            height_ = target_h;
            is_maximized_ = true;
        } else {
            // Przywroc dokladna zapisana geometrie
            x_ = saved_bounds_.x;
            y_ = saved_bounds_.y;
            width_ = saved_bounds_.width;
            height_ = saved_bounds_.height;
            is_maximized_ = false;
        }

        // Odtworz bufor warstwy i zaktualizuj pozycje/rozmiar w BWS
        bws_utworz_warstwe(x_, y_, width_, height_, 10);
        gui_ustaw_geometrie_okna(x_, y_, width_, height_);
        bws_przesun_warstwe(x_, y_);

        relayout();
        is_dirty_ = true;
    }

    void close() {
        is_dragging_ = false;
        gui_ustaw_capture_myszy(false);
        if (focused_widget_) {
            focused_widget_->on_blur();
            focused_widget_ = nullptr;
        }
        hovered_widget_ = nullptr;
        captured_widget_ = nullptr;
        gui_zakoncz_aplikacje();
    }

    void dispatch_event(const bws_zdarzenie& raw_e) {
        int title_h = TITLE_BAR_HEIGHT * dpi_scale_;
        int mx = raw_e.x;
        int my = raw_e.y;
        int local_x = mx - x_;
        int local_y = my - y_;

        switch (raw_e.typ) {
            case BWS_ZDARZENIE_MYSZ_RUCH: {
                if (is_dragging_ && (raw_e.przyciski & 1) != 0) {
                    int nx = mx - drag_off_x_;
                    int ny = my - drag_off_y_;

                    // Ograniczenie pozycji do obszaru ekranu / monitora
                    int max_w = 1280, max_h = 720;
                    GuiMonitorInfo mon{};
                    if (gui_pobierz_monitor(0, &mon) && mon.work_szer > 0 && mon.work_wys > 0) {
                        max_w = mon.work_x + mon.work_szer;
                        max_h = mon.work_y + mon.work_wys;
                    } else {
                        int sw = 1280, sh = 720;
                        gui_pobierz_rozdzielczosc(&sw, &sh);
                        max_w = sw;
                        max_h = (sh > 32) ? (sh - 32) : sh;
                    }

                    int max_pos_x = max_w - width_;
                    int max_pos_y = max_h - height_;
                    if (max_pos_x < 0) max_pos_x = 0;
                    if (max_pos_y < 0) max_pos_y = 0;

                    if (nx < 0) nx = 0;
                    if (ny < 0) ny = 0;
                    if (nx > max_pos_x) nx = max_pos_x;
                    if (ny > max_pos_y) ny = max_pos_y;

                    if (nx != x_ || ny != y_) {
                        x_ = nx;
                        y_ = ny;
                        bws_przesun_warstwe(x_, y_);
                        gui_odswiez();
                    }
                    return;
                } else if (is_dragging_) {
                    is_dragging_ = false;
                    gui_ustaw_capture_myszy(false);
                }

                if (captured_widget_) {
                    MouseEvent me;
                    me.type = EventType::MouseMove;
                    me.x = local_x;
                    me.y = local_y;
                    me.dx = raw_e.dx;
                    me.dy = raw_e.dy;
                    me.buttons = raw_e.przyciski;
                    captured_widget_->on_mouse_move(me);
                } else if (root_widget_) {
                    Widget* hit = root_widget_->hit_test(Point(local_x, local_y));
                    if (hit != hovered_widget_) {
                        if (hovered_widget_) hovered_widget_->on_mouse_leave();
                        hovered_widget_ = hit;
                        if (hovered_widget_) hovered_widget_->on_mouse_enter();
                    }
                    if (hovered_widget_) {
                        MouseEvent me;
                        me.type = EventType::MouseMove;
                        me.x = local_x;
                        me.y = local_y;
                        me.dx = raw_e.dx;
                        me.dy = raw_e.dy;
                        me.buttons = raw_e.przyciski;
                        hovered_widget_->on_mouse_move(me);
                    }
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_DOWN:
            case BWS_ZDARZENIE_MYSZ_PRAWY_DOWN: {
                // Sprawdz klikniecie w belke tytulowa
                if (my >= y_ && my < y_ + title_h && mx >= x_ && mx < x_ + width_) {
                    gui_akcja_belki akcja = gui_hit_test_belki(mx, my, x_, y_, width_);
                    if (akcja == GUI_BELKA_ZAMKNIJ) {
                        close();
                        return;
                    } else if (akcja == GUI_BELKA_MINIMALIZUJ) {
                        minimize();
                        return;
                    } else if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                        toggle_maximize();
                        return;
                    } else if (akcja == GUI_BELKA_DRAG && !is_maximized_) {
                        is_dragging_ = true;
                        drag_off_x_ = mx - x_;
                        drag_off_y_ = my - y_;
                        gui_ustaw_capture_myszy(true);
                        return;
                    }
                }

                if (root_widget_) {
                    Widget* hit = root_widget_->hit_test(Point(local_x, local_y));
                    if (hit && hit->is_focusable()) {
                        set_focus(hit);
                    } else if (!hit) {
                        set_focus(nullptr);
                    }

                    if (hit) {
                        captured_widget_ = hit;
                        MouseEvent me;
                        me.type = EventType::MouseDown;
                        me.x = local_x;
                        me.y = local_y;
                        me.buttons = raw_e.przyciski;
                        me.is_right = (raw_e.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN);
                        hit->on_mouse_down(me);
                    }
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_UP:
            case BWS_ZDARZENIE_MYSZ_PRAWY_UP: {
                if (is_dragging_) {
                    is_dragging_ = false;
                    gui_ustaw_capture_myszy(false);
                }

                Widget* target = captured_widget_;
                captured_widget_ = nullptr;

                if (!target && root_widget_) {
                    target = root_widget_->hit_test(Point(local_x, local_y));
                }

                if (target) {
                    MouseEvent me;
                    me.type = EventType::MouseUp;
                    me.x = local_x;
                    me.y = local_y;
                    me.buttons = raw_e.przyciski;
                    me.is_right = (raw_e.typ == BWS_ZDARZENIE_MYSZ_PRAWY_UP);
                    target->on_mouse_up(me);
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_KOLO: {
                int delta = raw_e.dy;
                Widget* target = hovered_widget_ ? hovered_widget_ : focused_widget_;
                if (target) {
                    MouseEvent me;
                    me.type = EventType::MouseWheel;
                    me.x = local_x;
                    me.y = local_y;
                    me.wheel_delta = delta;
                    target->on_mouse_wheel(me);
                }
                break;
            }

            case BWS_ZDARZENIE_TEXT_INPUT: {
                if (focused_widget_) {
                    TextInputEvent te;
                    te.codepoint = raw_e.kod;
                    focused_widget_->on_text_input(te);
                }
                break;
            }

            case BWS_ZDARZENIE_KLAWISZ:
            case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY: {
                // Tab (9 lub scancode 0x0F) - nawigacja focus
                if (raw_e.kod == 9 || raw_e.kod == 0x0F) {
                    bool shift = (raw_e.przyciski & BWS_MOD_SHIFT) != 0;
                    focus_next(shift);
                    break;
                }

                if (focused_widget_) {
                    KeyEvent ke;
                    ke.type = EventType::KeyDown;
                    ke.keycode = raw_e.kod;
                    ke.scancode = raw_e.kod;
                    ke.modifiers = raw_e.przyciski;
                    focused_widget_->on_key_down(ke);
                }
                break;
            }

            case BWS_ZDARZENIE_FOCUS: {
                is_active_ = true;
                if (is_minimized_) {
                    is_minimized_ = false;
                    relayout();
                }
                is_dirty_ = true;
                break;
            }

            case BWS_ZDARZENIE_BLUR: {
                is_active_ = false;
                is_dirty_ = true;
                break;
            }

            case BWS_ZDARZENIE_ZAMKNIJ: {
                close();
                break;
            }

            default:
                break;
        }
    }

    void focus_next(bool backward = false) {
        if (!root_widget_) return;
        Widget* list[32];
        size_t count = 0;
        collect_focusable(root_widget_, list, count);
        if (count == 0) return;

        int current_idx = -1;
        for (size_t i = 0; i < count; ++i) {
            if (list[i] == focused_widget_) {
                current_idx = static_cast<int>(i);
                break;
            }
        }

        int next_idx = 0;
        if (current_idx >= 0) {
            if (backward) {
                next_idx = (current_idx - 1 + static_cast<int>(count)) % static_cast<int>(count);
            } else {
                next_idx = (current_idx + 1) % static_cast<int>(count);
            }
        }
        set_focus(list[next_idx]);
    }

private:
    static void collect_focusable(Widget* w, Widget** out_list, size_t& count) {
        if (!w || !w->is_visible() || count >= 32) return;
        if (w->is_focusable()) {
            out_list[count++] = w;
        }
        for (size_t i = 0; i < w->get_child_count(); ++i) {
            collect_focusable(w->get_child(i), out_list, count);
        }
    }

    char title_[64]{0};
    int x_{100};
    int y_{100};
    int width_{480};
    int height_{320};
    int dpi_scale_{1};

    bool is_active_{true};
    bool is_maximized_{false};
    bool is_minimized_{false};
    bool is_dragging_{false};
    int drag_off_x_{0};
    int drag_off_y_{0};
    bool is_dirty_{true};
    Rect saved_bounds_{100, 100, 480, 320};

    Widget* root_widget_{nullptr};
    Panel* default_root_{nullptr};
    Widget* focused_widget_{nullptr};
    Widget* hovered_widget_{nullptr};
    Widget* captured_widget_{nullptr};
};

} // namespace BursztynUI
