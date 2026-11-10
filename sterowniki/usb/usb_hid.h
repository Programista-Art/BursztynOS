/*
 * Bursztyn OS - Dekoder Raportow USB HID Boot Keyboard (xHCI/HID)
 *
 * Plik: sterowniki/usb/usb_hid.h
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "input_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Przetwarzanie 8-bajtowego raportu USB HID Boot Keyboard w warstwie xHCI/HID */
void usb_hid_przetworz_raport(const uint8_t* raport);

/* Pobranie wskaznika do struktury InputDevice klawiatury USB */
InputDevice* usb_klawiatura_pobierz_urzadzenie(void);

/* Reset stanu poprzedniego raportu USB (uzywany przy restarcie i w testach) */
void usb_klawiatura_zresetuj_stan(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#if defined(BURSZTYN_VIRTIO_HOST_TEST)
extern uint64_t host_czas_monotoniczny_ns() __attribute__((weak));
inline uint64_t usb_hid_repeat_now(){return host_czas_monotoniczny_ns?host_czas_monotoniczny_ns():0;}
#else
#include "sterowniki/czas/hpet.h"
extern uint64_t scheduler_pobierz_tick();
inline uint64_t usb_hid_repeat_now(){return hpet_dostepny()?czas_monotoniczny_ns():scheduler_pobierz_tick()*4000000ULL;}
#endif
constexpr uint64_t UsbRepeatDelay=400000000ULL,UsbRepeatInterval=33000000ULL;
inline bool usb_hid_keyboard_register(InputDevice& device) {
    if(device.registered)return true;
    device.keyboard_repeat_usage=0;device.keyboard_repeat_sent=false;
    const char* name="usb-keyboard";
    for(size_t i=0;name[i]&&i+1<sizeof(device.name);++i)device.name[i]=name[i];
    device.kinds=INPUT_DEVICE_KIND_KEYBOARD;device.bus_type=INPUT_BUS_USB;
    return input_register_device(&device);
}
inline void usb_hid_keyboard_report(InputDevice& device, uint8_t* previous, const uint8_t* raport, size_t length) {
    if (!raport || !previous || length < 8) return;
    for (unsigned i=2;i<8;++i) if (raport[i]>=1 && raport[i]<=3) return;

    if(!usb_hid_keyboard_register(device))return;

    const uint64_t now=usb_hid_repeat_now();
    const uint8_t raw_mod = raport[0];
    const uint8_t prev_mod = previous[0];

    InputEvent evs[32];
    size_t n = 0;

    // 1. Zmiany modyfikatorow (bity 0..7)
    for (uint8_t bit = 0; bit < 8; ++bit) {
        const bool was = (prev_mod & (1U << bit)) != 0;
        const bool now = (raw_mod & (1U << bit)) != 0;
        if (was != now) {
            const uint16_t linux_kc = input_usb_hid_mod_to_linux(bit);
            if (linux_kc != 0 && n < 31) {
                evs[n++] = { INPUT_EV_KEY, linux_kc, now ? 1U : 0U };
            }
        }
    }

    // 2. Klawisze zwolnione (obecne w prev, nieobecne w current)
    for (uint8_t i = 0; i < 6; ++i) {
        const uint8_t old_u = previous[2 + i];
        if (old_u < 4) continue;
        bool nadal = false;
        for (uint8_t j = 0; j < 6; ++j) {
            if (raport[2 + j] == old_u) {
                nadal = true;
                break;
            }
        }
        if (!nadal) {
            if(device.keyboard_repeat_usage==old_u){device.keyboard_repeat_usage=0;device.keyboard_repeat_sent=false;}
            const uint16_t linux_kc = input_usb_hid_usage_to_linux(old_u);
            if (linux_kc != 0 && n < 31) {
                evs[n++] = { INPUT_EV_KEY, linux_kc, 0U };
            }
        }
    }

    // 3. Sprawdzenie czy caly raport jest identyczny (potencjalny auto-repeat)
    bool identyczny = (raw_mod == prev_mod);
    if (identyczny) {
        for (uint8_t i = 0; i < 6; ++i) {
            if (raport[2 + i] != previous[2 + i]) {
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
            if (previous[2 + j] == u) {
                byl = true;
                break;
            }
        }
        const uint16_t linux_kc = input_usb_hid_usage_to_linux(u);
        if (linux_kc == 0) continue;

        if (!byl) {
            // New press starts a bounded typematic delay for the latest key.
            device.keyboard_repeat_usage=u;device.keyboard_repeat_started=now;device.keyboard_repeat_sent=false;
            if (n < 31) {
                evs[n++] = { INPUT_EV_KEY, linux_kc, 1U };
            }
        } else if (identyczny) {
            if(!device.keyboard_repeat_usage){device.keyboard_repeat_usage=u;device.keyboard_repeat_started=now;device.keyboard_repeat_sent=false;}
            if(device.keyboard_repeat_usage==u && now-device.keyboard_repeat_started>=UsbRepeatDelay &&
               (!device.keyboard_repeat_sent || now-device.keyboard_repeat_last>=UsbRepeatInterval) && n<31){
                evs[n++]={INPUT_EV_KEY,linux_kc,2U};device.keyboard_repeat_last=now;device.keyboard_repeat_sent=true;
            }
        }
    }

    for (uint8_t i = 0; i < 8; ++i) {
        previous[i] = raport[i];
    }

    if (n > 0) {
        evs[n++] = { INPUT_EV_SYN, INPUT_SYN_REPORT, 0 };
        input_dispatch_events(&device, evs, n);
    }
}

#endif
