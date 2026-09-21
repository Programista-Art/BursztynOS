/*
 * Bursztyn OS - Dekoder Raportow USB HID Boot Keyboard (xHCI/HID)
 *
 * Plik: sterowniki/usb/usb_hid.cpp
 */

#include "sterowniki/usb/usb_hid.h"
#include "input_device.h"

namespace {

static InputDevice s_usb_keyboard_dev{};
static uint8_t s_prev_raport[8] = {};

} // namespace

extern "C" {

void usb_klawiatura_zresetuj_stan(void) {
    for (size_t i = 0; i < 8; ++i) {
        s_prev_raport[i] = 0;
    }
}

static void usb_klawiatura_inicjalizuj_urzadzenie(void) {
    if (!s_usb_keyboard_dev.registered) {
        for (size_t i = 0; i < sizeof(s_usb_keyboard_dev.name) - 1 && "usb-keyboard"[i]; ++i) {
            s_usb_keyboard_dev.name[i] = "usb-keyboard"[i];
        }
        s_usb_keyboard_dev.kinds = INPUT_DEVICE_KIND_KEYBOARD;
        input_register_device(&s_usb_keyboard_dev);
    }
}

InputDevice* usb_klawiatura_pobierz_urzadzenie(void) {
    usb_klawiatura_inicjalizuj_urzadzenie();
    return &s_usb_keyboard_dev;
}

void usb_hid_przetworz_raport(const uint8_t* raport) {
    if (!raport) return;

    usb_klawiatura_inicjalizuj_urzadzenie();

    const uint8_t raw_mod = raport[0];
    const uint8_t prev_mod = s_prev_raport[0];

    InputEvent evs[16];
    size_t n = 0;

    // 1. Zmiany modyfikatorow (bity 0..7)
    for (uint8_t bit = 0; bit < 8; ++bit) {
        const bool was = (prev_mod & (1U << bit)) != 0;
        const bool now = (raw_mod & (1U << bit)) != 0;
        if (was != now) {
            const uint16_t linux_kc = input_usb_hid_mod_to_linux(bit);
            if (linux_kc != 0 && n < 15) {
                evs[n++] = { INPUT_EV_KEY, linux_kc, now ? 1U : 0U };
            }
        }
    }

    // 2. Klawisze zwolnione (obecne w prev, nieobecne w current)
    for (uint8_t i = 0; i < 6; ++i) {
        const uint8_t old_u = s_prev_raport[2 + i];
        if (old_u < 4) continue;
        bool nadal = false;
        for (uint8_t j = 0; j < 6; ++j) {
            if (raport[2 + j] == old_u) {
                nadal = true;
                break;
            }
        }
        if (!nadal) {
            const uint16_t linux_kc = input_usb_hid_usage_to_linux(old_u);
            if (linux_kc != 0 && n < 15) {
                evs[n++] = { INPUT_EV_KEY, linux_kc, 0U };
            }
        }
    }

    // 3. Sprawdzenie czy caly raport jest identyczny (potencjalny auto-repeat)
    bool identyczny = (raw_mod == prev_mod);
    if (identyczny) {
        for (uint8_t i = 0; i < 6; ++i) {
            if (raport[2 + i] != s_prev_raport[2 + i]) {
                identyczny = false;
                break;
            }
        }
    }

    // 4. Klawisze wcisniete lub powtorzone
    for (uint8_t i = 0; i < 6; ++i) {
        const uint8_t u = raport[2 + i];
        if (u < 4) continue;
        bool byl = false;
        for (uint8_t j = 0; j < 6; ++j) {
            if (s_prev_raport[2 + j] == u) {
                byl = true;
                break;
            }
        }
        const uint16_t linux_kc = input_usb_hid_usage_to_linux(u);
        if (linux_kc == 0) continue;

        if (!byl) {
            // Nowe wcisniecie
            if (n < 15) {
                evs[n++] = { INPUT_EV_KEY, linux_kc, 1U };
            }
        } else if (identyczny) {
            // Raport przyszedl ponownie bez zmian ze stabilnie trzymanym klawiszem -> repeat
            if (n < 15) {
                evs[n++] = { INPUT_EV_KEY, linux_kc, 2U };
            }
        }
    }

    for (uint8_t i = 0; i < 8; ++i) {
        s_prev_raport[i] = raport[i];
    }

    if (n > 0) {
        evs[n++] = { INPUT_EV_SYN, INPUT_SYN_REPORT, 0 };
        input_dispatch_events(&s_usb_keyboard_dev, evs, n);
    }
}

} // extern "C"
