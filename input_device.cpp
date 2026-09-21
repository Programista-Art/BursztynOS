/*
 * Bursztyn OS - Implementacja Abstrakcji Urzadzen Wejsciowych (InputDevice)
 *
 * Plik: input_device.cpp
 */

#include "input_device.h"
#include "mysz_input.h"

#ifdef BURSZTYN_VIRTIO_HOST_TEST
#include <cstring>
#include <cstdio>
#else
extern "C" void* memset(void* s, int c, size_t n);
extern "C" int strcmp(const char* s1, const char* s2);
extern "C" size_t strlen(const char* s);
#endif

/* Interfejsy z podsystemu grafiki i klawiatury Bursztyn OS */
extern "C" void zaktualizuj_mysze_zrodlo(int dx, int dy, uint8_t przyciski, const char* zrodlo);
extern "C" void zaktualizuj_kolo_myszy(int pion, int poziom, uint8_t przyciski);
extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x, int y, uint8_t przyciski, const char* zrodlo);
int grafika_pobierz_szerokosc(void);
int grafika_pobierz_wysokosc(void);
extern "C" void klawiatura_obsluz_klawisz(uint16_t keycode, uint32_t value);

namespace {

constexpr size_t MAX_INPUT_DEVICES = 16;
static InputDevice* s_devices[MAX_INPUT_DEVICES] = {};
static size_t s_device_count = 0;

static inline bool str_rowne(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a++ != *b++) return false;
    }
    return (*a == '\0' && *b == '\0');
}

} // namespace

extern "C" {

bool input_register_device(InputDevice* dev) {
    if (!dev || dev->registered) return false;
    if (s_device_count >= MAX_INPUT_DEVICES) return false;

    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] == nullptr) {
            s_devices[i] = dev;
            dev->registered = true;
            dev->rel_x = 0;
            dev->rel_y = 0;
            dev->rel_wheel = 0;
            dev->abs_val_x = 0;
            dev->abs_val_y = 0;
            dev->has_abs_x = false;
            dev->has_abs_y = false;
            dev->buttons_state = 0;
            dev->prev_buttons_state = 0;
            s_device_count++;
            return true;
        }
    }
    return false;
}

bool input_unregister_device(InputDevice* dev) {
    if (!dev || !dev->registered) return false;
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] == dev) {
            s_devices[i] = nullptr;
            dev->registered = false;
            if (s_device_count > 0) s_device_count--;
            return true;
        }
    }
    return false;
}

size_t input_get_device_count(void) {
    return s_device_count;
}

InputDevice* input_get_device(size_t index) {
    if (index >= MAX_INPUT_DEVICES) return nullptr;
    return s_devices[index];
}

InputDevice* input_find_device_by_name(const char* name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] && str_rowne(s_devices[i]->name, name)) {
            return s_devices[i];
        }
    }
    return nullptr;
}

void input_dispatch_event(InputDevice* dev, const InputEvent* ev) {
    if (!dev || !ev) return;

    switch (ev->type) {
        case INPUT_EV_KEY: {
            const uint16_t code = ev->code;
            const uint32_t val = ev->value;

            // Przyciski myszy / urzadzenia wskazujacego
            if (code == INPUT_BTN_LEFT || code == INPUT_BTN_TOUCH) {
                if (val != 0) {
                    dev->buttons_state |= MYSZ_PRZYCISK_LEWY;
                } else {
                    dev->buttons_state &= ~MYSZ_PRZYCISK_LEWY;
                }
            } else if (code == INPUT_BTN_RIGHT) {
                if (val != 0) {
                    dev->buttons_state |= MYSZ_PRZYCISK_PRAWY;
                } else {
                    dev->buttons_state &= ~MYSZ_PRZYCISK_PRAWY;
                }
            } else if (code == INPUT_BTN_MIDDLE) {
                if (val != 0) {
                    dev->buttons_state |= MYSZ_PRZYCISK_SRODKOWY;
                } else {
                    dev->buttons_state &= ~MYSZ_PRZYCISK_SRODKOWY;
                }
            } else if (code >= INPUT_BTN_MISC && code < 0x200U) {
                // Inne przyciski rozszerzone
                if (val != 0) {
                    dev->buttons_state |= MYSZ_PRZYCISK_LEWY;
                } else {
                    dev->buttons_state &= ~MYSZ_PRZYCISK_LEWY;
                }
            } else {
                // Standardowe klawisze klawiatury
                klawiatura_obsluz_klawisz(code, val);
            }
            break;
        }

        case INPUT_EV_REL: {
            const int32_t delta = static_cast<int32_t>(ev->value);
            if (ev->code == INPUT_REL_X) {
                dev->rel_x += delta;
            } else if (ev->code == INPUT_REL_Y) {
                dev->rel_y += delta;
            } else if (ev->code == INPUT_REL_WHEEL) {
                dev->rel_wheel += delta;
            }
            break;
        }

        case INPUT_EV_ABS: {
            const int32_t coord = static_cast<int32_t>(ev->value);
            if (ev->code == INPUT_ABS_X) {
                dev->abs_val_x = coord;
                dev->has_abs_x = true;
            } else if (ev->code == INPUT_ABS_Y) {
                dev->abs_val_y = coord;
                dev->has_abs_y = true;
            }
            break;
        }

        case INPUT_EV_SYN: {
            if (ev->code == INPUT_SYN_REPORT) {
                const char* src_name = (dev->name[0] != '\0') ? dev->name : "input";

                // 1. Zdarzenia myszy wzglednej (ruch lub zmiana przyciskow)
                if (dev->rel_x != 0 || dev->rel_y != 0) {
                    /*
                     * W grafika.cpp kursor uaktualniany jest:
                     * nowy_x = mysz_x + dx;
                     * nowy_y = mysz_y - dy;
                     *
                     * Standard Linux EV_REL REL_Y ma wartosci dodatnie w dol ekranu.
                     * Aby ruch w dol zwiekszal wspolrzedna Y na ekranie, podajemy dy = -dev->rel_y.
                     */
                    zaktualizuj_mysze_zrodlo(dev->rel_x, -dev->rel_y, dev->buttons_state, src_name);
                    dev->rel_x = 0;
                    dev->rel_y = 0;
                } else if (dev->buttons_state != dev->prev_buttons_state) {
                    zaktualizuj_mysze_zrodlo(0, 0, dev->buttons_state, src_name);
                }

                // 2. Kolo myszy
                if (dev->rel_wheel != 0) {
                    zaktualizuj_kolo_myszy(dev->rel_wheel, 0, dev->buttons_state);
                    dev->rel_wheel = 0;
                }

                // 3. Pozycjonowanie bezwzgledne (tablet / ekran dotykowy)
                if (dev->has_abs_x || dev->has_abs_y) {
                    const int screen_w = grafika_pobierz_szerokosc();
                    const int screen_h = grafika_pobierz_wysokosc();

                    if (screen_w > 0 && screen_h > 0) {
                        int target_x = dev->abs_val_x;
                        int target_y = dev->abs_val_y;

                        // Skalowanie osi X z pelna ochrona przed overflow
                        if (dev->abs_x.max > dev->abs_x.min) {
                            if (target_x < dev->abs_x.min) target_x = dev->abs_x.min;
                            if (target_x > dev->abs_x.max) target_x = dev->abs_x.max;
                            const int64_t span_in = static_cast<int64_t>(dev->abs_x.max) - dev->abs_x.min;
                            const int64_t val_rel = static_cast<int64_t>(target_x) - dev->abs_x.min;
                            const int64_t max_out = screen_w - 1;
                            target_x = static_cast<int>((val_rel * max_out) / span_in);
                        }

                        // Skalowanie osi Y z pelna ochrona przed overflow
                        if (dev->abs_y.max > dev->abs_y.min) {
                            if (target_y < dev->abs_y.min) target_y = dev->abs_y.min;
                            if (target_y > dev->abs_y.max) target_y = dev->abs_y.max;
                            const int64_t span_in = static_cast<int64_t>(dev->abs_y.max) - dev->abs_y.min;
                            const int64_t val_rel = static_cast<int64_t>(target_y) - dev->abs_y.min;
                            const int64_t max_out = screen_h - 1;
                            target_y = static_cast<int>((val_rel * max_out) / span_in);
                        }

                        zaktualizuj_mysze_pozycja_zrodlo(target_x, target_y, dev->buttons_state, src_name);
                    }

                    dev->has_abs_x = false;
                    dev->has_abs_y = false;
                }

                dev->prev_buttons_state = dev->buttons_state;
            }
            break;
        }

        default:
            // Nieobslugiwane lub opcjonalne typy zdarzen (EV_MSC, EV_LED itp.) sa bezpiecznie ignorowane
            break;
    }
}

void input_dispatch_events(InputDevice* dev, const InputEvent* evs, size_t count) {
    if (!dev || !evs || count == 0) return;
    for (size_t i = 0; i < count; ++i) {
        input_dispatch_event(dev, &evs[i]);
    }
}

void input_service_all(void) {
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i] && s_devices[i]->poll_fn) {
            s_devices[i]->poll_fn(s_devices[i]);
        }
    }
}

void input_reset_all(void) {
    for (size_t i = 0; i < MAX_INPUT_DEVICES; ++i) {
        if (s_devices[i]) {
            s_devices[i]->registered = false;
            s_devices[i] = nullptr;
        }
    }
    s_device_count = 0;
}

uint16_t input_ps2_scancode_to_linux(bool is_e0, uint8_t scancode) {
    const uint8_t code = scancode & 0x7FU;
    if (is_e0) {
        switch (code) {
            case 0x38: return INPUT_KEY_RIGHTALT;   // 100
            case 0x1D: return INPUT_KEY_RIGHTCTRL;  // 97
            case 0x5B: return INPUT_KEY_LEFTMETA;   // 125
            case 0x5C: return INPUT_KEY_RIGHTMETA;  // 126
            case 0x48: return INPUT_KEY_UP;         // 103
            case 0x50: return INPUT_KEY_DOWN;       // 108
            case 0x4B: return INPUT_KEY_LEFT;       // 105
            case 0x4D: return INPUT_KEY_RIGHT;      // 106
            case 0x47: return INPUT_KEY_HOME;       // 102
            case 0x4F: return INPUT_KEY_END;        // 107
            case 0x49: return INPUT_KEY_PAGEUP;     // 104
            case 0x51: return INPUT_KEY_PAGEDOWN;   // 109
            case 0x52: return INPUT_KEY_INSERT;     // 110
            case 0x53: return INPUT_KEY_DELETE;     // 111
            case 0x1C: return INPUT_KEY_KPENTER;    // 96
            case 0x35: return INPUT_KEY_KPSLASH;    // 98
            default:   return 0;
        }
    } else {
        switch (code) {
            case 0x2A: return INPUT_KEY_LEFTSHIFT;  // 42
            case 0x36: return INPUT_KEY_RIGHTSHIFT; // 54
            case 0x1D: return INPUT_KEY_LEFTCTRL;   // 29
            case 0x38: return INPUT_KEY_LEFTALT;    // 56
            case 0x3A: return INPUT_KEY_CAPSLOCK;   // 58
            case 0x45: return INPUT_KEY_NUMLOCK;    // 69
            default:
                if (code > 0 && code < 128) {
                    return static_cast<uint16_t>(code); // Scan Code Set 1 kody 1..88 odpowiadają Linux keycodes
                }
                return 0;
        }
    }
}

uint16_t input_usb_hid_mod_to_linux(uint8_t bit_index) {
    static const uint16_t s_mod_map[8] = {
        INPUT_KEY_LEFTCTRL,   // bit 0
        INPUT_KEY_LEFTSHIFT,  // bit 1
        INPUT_KEY_LEFTALT,    // bit 2
        INPUT_KEY_LEFTMETA,   // bit 3
        INPUT_KEY_RIGHTCTRL,  // bit 4
        INPUT_KEY_RIGHTSHIFT, // bit 5
        INPUT_KEY_RIGHTALT,   // bit 6 (AltGr)
        INPUT_KEY_RIGHTMETA   // bit 7
    };
    if (bit_index < 8) return s_mod_map[bit_index];
    return 0;
}

uint16_t input_usb_hid_usage_to_linux(uint8_t usage) {
    if (usage >= 4 && usage <= 29) {
        static const uint16_t az[26] = {
            INPUT_KEY_A, INPUT_KEY_B, INPUT_KEY_C, INPUT_KEY_D, INPUT_KEY_E,
            INPUT_KEY_F, INPUT_KEY_G, INPUT_KEY_H, INPUT_KEY_I, INPUT_KEY_J,
            INPUT_KEY_K, INPUT_KEY_L, INPUT_KEY_M, INPUT_KEY_N, INPUT_KEY_O,
            INPUT_KEY_P, INPUT_KEY_Q, INPUT_KEY_R, INPUT_KEY_S, INPUT_KEY_T,
            INPUT_KEY_U, INPUT_KEY_V, INPUT_KEY_W, INPUT_KEY_X, INPUT_KEY_Y,
            INPUT_KEY_Z
        };
        return az[usage - 4];
    }
    if (usage >= 30 && usage <= 38) {
        return static_cast<uint16_t>(INPUT_KEY_1 + (usage - 30));
    }
    if (usage == 39) return INPUT_KEY_0;
    if (usage == 40) return INPUT_KEY_ENTER;
    if (usage == 41) return INPUT_KEY_ESC;
    if (usage == 42) return INPUT_KEY_BACKSPACE;
    if (usage == 43) return INPUT_KEY_TAB;
    if (usage == 44) return INPUT_KEY_SPACE;
    if (usage == 45) return INPUT_KEY_MINUS;
    if (usage == 46) return INPUT_KEY_EQUAL;
    if (usage == 47) return INPUT_KEY_LEFTBRACE;
    if (usage == 48) return INPUT_KEY_RIGHTBRACE;
    if (usage == 49) return INPUT_KEY_BACKSLASH;
    if (usage == 51) return INPUT_KEY_SEMICOLON;
    if (usage == 52) return INPUT_KEY_APOSTROPHE;
    if (usage == 53) return INPUT_KEY_GRAVE;
    if (usage == 54) return INPUT_KEY_COMMA;
    if (usage == 55) return INPUT_KEY_DOT;
    if (usage == 56) return INPUT_KEY_SLASH;
    if (usage == 57) return INPUT_KEY_CAPSLOCK;
    if (usage >= 58 && usage <= 67) {
        return static_cast<uint16_t>(INPUT_KEY_F1 + (usage - 58));
    }
    if (usage == 68) return INPUT_KEY_F11;
    if (usage == 69) return INPUT_KEY_F12;
    if (usage == 73) return INPUT_KEY_INSERT;
    if (usage == 74) return INPUT_KEY_HOME;
    if (usage == 75) return INPUT_KEY_PAGEUP;
    if (usage == 76) return INPUT_KEY_DELETE;
    if (usage == 77) return INPUT_KEY_END;
    if (usage == 78) return INPUT_KEY_PAGEDOWN;
    if (usage == 79) return INPUT_KEY_RIGHT;
    if (usage == 80) return INPUT_KEY_LEFT;
    if (usage == 81) return INPUT_KEY_DOWN;
    if (usage == 82) return INPUT_KEY_UP;
    if (usage == 83) return INPUT_KEY_NUMLOCK;
    if (usage == 84) return INPUT_KEY_KPSLASH;
    if (usage == 85) return INPUT_KEY_KPASTERISK;
    if (usage == 86) return INPUT_KEY_KPMINUS;
    if (usage == 87) return INPUT_KEY_KPPLUS;
    if (usage == 88) return INPUT_KEY_KPENTER;
    if (usage >= 89 && usage <= 97) {
        static const uint16_t kp_digits[9] = {
            INPUT_KEY_KP1, INPUT_KEY_KP2, INPUT_KEY_KP3,
            INPUT_KEY_KP4, INPUT_KEY_KP5, INPUT_KEY_KP6,
            INPUT_KEY_KP7, INPUT_KEY_KP8, INPUT_KEY_KP9
        };
        return kp_digits[usage - 89];
    }
    if (usage == 98) return INPUT_KEY_KP0;
    if (usage == 99) return INPUT_KEY_KPDOT;
    return 0;
}

} // extern "C"

