#pragma once

#include <stdint.h>
#include <stddef.h>
#include "../bws_zdarzenia.h"

namespace BursztynUI {

enum class EventType {
    None = 0,
    MouseMove,
    MouseDown,
    MouseUp,
    MouseWheel,
    KeyDown,
    KeyUp,
    TextInput,
    Focus,
    Blur,
    Resize
};

struct MouseEvent {
    EventType type{EventType::None};
    int x{0};
    int y{0};
    int dx{0};
    int dy{0};
    uint32_t buttons{0};
    int wheel_delta{0};
    bool is_right{false};
    bool handled{false};

    constexpr bool is_left_button() const { return (buttons & 1) != 0; }
    constexpr bool is_right_button() const { return is_right || (buttons & 2) != 0; }
};

struct KeyEvent {
    EventType type{EventType::None};
    uint32_t keycode{0};
    uint32_t scancode{0};
    uint32_t modifiers{0};
    bool is_down{true};
    bool handled{false};

    constexpr bool has_shift() const { return (modifiers & BWS_MOD_SHIFT) != 0; }
    constexpr bool has_ctrl() const { return (modifiers & BWS_MOD_CTRL) != 0; }
    constexpr bool has_alt() const { return (modifiers & BWS_MOD_ALT) != 0; }
};

struct TextInputEvent {
    EventType type{EventType::TextInput};
    uint32_t codepoint{0};
    char utf8[5]{0, 0, 0, 0, 0};
    uint8_t utf8_len{0};
    bool handled{false};
};

struct FocusEvent {
    EventType type{EventType::Focus};
    bool gained{true};
    bool handled{false};
};

struct ResizeEvent {
    EventType type{EventType::Resize};
    int new_width{0};
    int new_height{0};
    bool handled{false};
};

} // namespace BursztynUI
