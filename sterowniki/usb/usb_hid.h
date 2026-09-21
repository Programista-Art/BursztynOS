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
