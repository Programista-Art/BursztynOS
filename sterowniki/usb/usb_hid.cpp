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
        s_usb_keyboard_dev.bus_type = INPUT_BUS_USB;
        s_usb_keyboard_dev.device_id = 0;
        input_register_device(&s_usb_keyboard_dev);
    }
}

InputDevice* usb_klawiatura_pobierz_urzadzenie(void) {
    usb_klawiatura_inicjalizuj_urzadzenie();
    return &s_usb_keyboard_dev;
}

void usb_hid_przetworz_raport(const uint8_t* raport) {
    usb_hid_keyboard_report(s_usb_keyboard_dev,s_prev_raport,raport,8);
}

} // extern "C"
