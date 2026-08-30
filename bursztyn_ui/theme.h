#pragma once

#include <stdint.h>
#include "types.h"

namespace BursztynUI {

namespace Colors {
    // Paleta Bursztynowa (Amber Gold Theme)
    inline constexpr uint32_t DarkAmberBg     = 0x001A0B00U;
    inline constexpr uint32_t SurfaceBg       = 0x00280F00U;
    inline constexpr uint32_t SurfaceAlt      = 0x00391B02U;
    inline constexpr uint32_t SurfaceHover    = 0x004A2605U;
    inline constexpr uint32_t SurfaceActive   = 0x00603800U;
    inline constexpr uint32_t InputBg         = 0x00120700U;

    inline constexpr uint32_t AccentGold      = 0x00FFBF00U;
    inline constexpr uint32_t AccentHover     = 0x00FFCF40U;
    inline constexpr uint32_t AccentPressed   = 0x00E58A00U;
    inline constexpr uint32_t AccentBorder    = 0x00794400U;
    inline constexpr uint32_t AccentFocus     = 0x00FFBF00U;

    inline constexpr uint32_t TextWhite       = 0x00FFFFFFU;
    inline constexpr uint32_t TextMuted       = 0x00D1D5DBU;
    inline constexpr uint32_t TextDark        = 0x001A0B00U;
    inline constexpr uint32_t TextPlaceholder = 0x008A7060U;

    inline constexpr uint32_t BorderDefault   = 0x00794400U;
    inline constexpr uint32_t BorderHover     = 0x00A05A00U;
    inline constexpr uint32_t BorderFocus     = 0x00FFBF00U;

    inline constexpr uint32_t CheckBoxCheck   = 0x00FFBF00U;
    inline constexpr uint32_t CloseRed        = 0x00AA0000U;
    inline constexpr uint32_t CloseRedHover   = 0x00CC2222U;

    inline constexpr uint32_t DisabledBg      = 0x00222222U;
    inline constexpr uint32_t DisabledFg      = 0x00666666U;
    inline constexpr uint32_t DisabledBorder  = 0x00444444U;
}

struct StateStyle {
    uint32_t background{Colors::SurfaceBg};
    uint32_t foreground{Colors::TextWhite};
    uint32_t border{Colors::BorderDefault};
    int border_thickness{1};
    int corner_radius{0};
    int font_scale{1};
};

struct WidgetStyle {
    StateStyle normal;
    StateStyle hover;
    StateStyle pressed;
    StateStyle focused;
    StateStyle disabled;
    Thickness padding{4, 4, 4, 4};
};

class Theme {
public:
    static Theme& get_default() {
        static Theme instance;
        return instance;
    }

    Theme() {
        init_default();
    }

    WidgetStyle window_style;
    WidgetStyle panel_style;
    WidgetStyle label_style;
    WidgetStyle button_style;
    WidgetStyle checkbox_style;
    WidgetStyle textbox_style;

private:
    void init_default() {
        // Window style
        window_style.normal.background = Colors::DarkAmberBg;
        window_style.normal.foreground = Colors::TextWhite;
        window_style.normal.border = Colors::BorderDefault;
        window_style.normal.border_thickness = 1;

        // Panel style
        panel_style.normal.background = Colors::SurfaceBg;
        panel_style.normal.foreground = Colors::TextWhite;
        panel_style.normal.border = Colors::BorderDefault;
        panel_style.normal.border_thickness = 1;
        panel_style.padding = Thickness(8, 8, 8, 8);

        // Label style
        label_style.normal.background = 0; // Przezroczysty
        label_style.normal.foreground = Colors::TextWhite;
        label_style.normal.border = 0;
        label_style.normal.border_thickness = 0;
        label_style.padding = Thickness(2, 2, 2, 2);

        // Button style
        button_style.normal.background = Colors::AccentPressed; // 0x00E58A00
        button_style.normal.foreground = Colors::TextDark;      // 0x001A0B00
        button_style.normal.border = Colors::BorderDefault;
        button_style.normal.border_thickness = 1;
        button_style.normal.corner_radius = 2;

        button_style.hover.background = Colors::AccentHover;
        button_style.hover.foreground = Colors::TextDark;
        button_style.hover.border = Colors::BorderHover;
        button_style.hover.border_thickness = 1;
        button_style.hover.corner_radius = 2;

        button_style.pressed.background = Colors::AccentGold;
        button_style.pressed.foreground = Colors::TextDark;
        button_style.pressed.border = Colors::BorderFocus;
        button_style.pressed.border_thickness = 1;
        button_style.pressed.corner_radius = 2;

        button_style.focused.background = Colors::AccentPressed;
        button_style.focused.foreground = Colors::TextDark;
        button_style.focused.border = Colors::BorderFocus;
        button_style.focused.border_thickness = 2;
        button_style.focused.corner_radius = 2;

        button_style.disabled.background = Colors::DisabledBg;
        button_style.disabled.foreground = Colors::DisabledFg;
        button_style.disabled.border = Colors::DisabledBorder;
        button_style.padding = Thickness(10, 6, 10, 6);

        // CheckBox style
        checkbox_style.normal.background = Colors::InputBg;
        checkbox_style.normal.foreground = Colors::TextWhite;
        checkbox_style.normal.border = Colors::BorderDefault;
        checkbox_style.normal.border_thickness = 1;

        checkbox_style.hover.background = Colors::SurfaceHover;
        checkbox_style.hover.foreground = Colors::TextWhite;
        checkbox_style.hover.border = Colors::BorderHover;
        checkbox_style.hover.border_thickness = 1;

        checkbox_style.focused.background = Colors::InputBg;
        checkbox_style.focused.foreground = Colors::TextWhite;
        checkbox_style.focused.border = Colors::BorderFocus;
        checkbox_style.focused.border_thickness = 2;
        checkbox_style.padding = Thickness(4, 4, 4, 4);

        // TextBox style
        textbox_style.normal.background = Colors::InputBg;
        textbox_style.normal.foreground = Colors::TextWhite;
        textbox_style.normal.border = Colors::BorderDefault;
        textbox_style.normal.border_thickness = 1;

        textbox_style.hover.background = Colors::InputBg;
        textbox_style.hover.foreground = Colors::TextWhite;
        textbox_style.hover.border = Colors::BorderHover;
        textbox_style.hover.border_thickness = 1;

        textbox_style.focused.background = Colors::InputBg;
        textbox_style.focused.foreground = Colors::TextWhite;
        textbox_style.focused.border = Colors::BorderFocus;
        textbox_style.focused.border_thickness = 2;

        textbox_style.disabled.background = Colors::DisabledBg;
        textbox_style.disabled.foreground = Colors::DisabledFg;
        textbox_style.disabled.border = Colors::DisabledBorder;
        textbox_style.padding = Thickness(6, 4, 6, 4);
    }
};

} // namespace BursztynUI
