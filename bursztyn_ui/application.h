#pragma once

#include "types.h"
#include "window.h"
#include "drawing.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class GuiApplication {
public:
    GuiApplication() {
        init();
    }

    virtual ~GuiApplication() = default;

    int get_dpi_scale() const { return dpi_scale_; }

    void set_main_window(Window* win) {
        main_window_ = win;
        if (main_window_) {
            main_window_->set_dpi_scale(dpi_scale_);
        }
    }

    Window* get_main_window() const { return main_window_; }

    void quit() {
        running_ = false;
        gui_zakoncz_aplikacje();
    }

    int run() {
        if (!main_window_) return -1;
        running_ = true;

        // Utworz warstwe okna i relayout
        int wx = main_window_->get_x();
        int wy = main_window_->get_y();
        int ww = main_window_->get_width();
        int wh = main_window_->get_height();

        bws_utworz_warstwe(wx, wy, ww, wh, 10);
        gui_ustaw_tytul_okna(main_window_->get_title());
        gui_ustaw_geometrie_okna(wx, wy, ww, wh);
        main_window_->relayout();

        // Pierwszy pelny paint
        DrawingContext ctx;
        main_window_->paint(ctx, true);
        gui_odswiez();

        bws_zdarzenie ev{};
        while (running_) {
            if (gui_czekaj_na_zdarzenie(&ev)) {
                main_window_->dispatch_event(ev);

                // Sprawdz czy sa dodatkowe zdarzenia w kolejce
                while (gui_pobierz_zdarzenie(&ev)) {
                    main_window_->dispatch_event(ev);
                }

                if (!main_window_->is_minimized()) {
                    // Przerysuj tylko brudne widgety
                    main_window_->paint(ctx, false);
                    gui_odswiez();
                }
            }
        }

        return 0;
    }

private:
    void init() {
        GuiMonitorInfo mon{};
        if (gui_pobierz_monitor(0, &mon) && mon.scale_x1000 >= 1000) {
            dpi_scale_ = static_cast<int>((mon.scale_x1000 + 500) / 1000);
            if (dpi_scale_ < 1) dpi_scale_ = 1;
        } else {
            dpi_scale_ = 1;
        }
    }

    int dpi_scale_{1};
    Window* main_window_{nullptr};
    bool running_{false};
};

} // namespace BursztynUI
