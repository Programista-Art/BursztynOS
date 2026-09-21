/*
 * Bursztyn OS - Abstrakcja Urzadzenia Wejsciowego (InputDevice / InputEvent API)
 *
 * Wspolny interfejs wejsciowy dla urzadzen wskazujacych i klawiatur w jadrze Bursztyn OS.
 * Zapewnia ujednolicona sciezke przetwarzania zdarzen dla:
 *   - myszy i klawiatur PS/2,
 *   - kontrolera USB xHCI (HID Boot Mouse i Boot Keyboard),
 *   - nowoczesnych urzadzen VirtIO-INPUT (klawiatury, myszy wzgledne, tablety bezwzgledne).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. STANDARDOWE TYPY I KODY ZDARZEN WEJSCIOWYCH (VirtIO / Linux Input ABI)
 * ========================================================================= */

#define INPUT_EV_SYN 0x00U
#define INPUT_EV_KEY 0x01U
#define INPUT_EV_REL 0x02U
#define INPUT_EV_ABS 0x03U
#define INPUT_EV_MSC 0x04U
#define INPUT_EV_SW  0x05U
#define INPUT_EV_LED 0x11U
#define INPUT_EV_SND 0x12U
#define INPUT_EV_REP 0x14U

#define INPUT_SYN_REPORT    0x00U
#define INPUT_SYN_CONFIG    0x01U
#define INPUT_SYN_MT_REPORT 0x02U
#define INPUT_SYN_DROPPED   0x03U

/* Osie wzgledne (EV_REL) */
#define INPUT_REL_X      0x00U
#define INPUT_REL_Y      0x01U
#define INPUT_REL_Z      0x02U
#define INPUT_REL_RX     0x03U
#define INPUT_REL_RY     0x04U
#define INPUT_REL_RZ     0x05U
#define INPUT_REL_HWHEEL 0x06U
#define INPUT_REL_DIAL   0x07U
#define INPUT_REL_WHEEL  0x08U
#define INPUT_REL_MISC   0x09U

/* Osie bezwzgledne (EV_ABS) */
#define INPUT_ABS_X 0x00U
#define INPUT_ABS_Y 0x01U
#define INPUT_ABS_Z 0x02U

/* Przyciski myszy i urzadzen wskazujacych (EV_KEY) */
#define INPUT_BTN_MISC   0x100U
#define INPUT_BTN_MOUSE  0x110U
#define INPUT_BTN_LEFT   0x110U
#define INPUT_BTN_RIGHT  0x111U
#define INPUT_BTN_MIDDLE 0x112U
#define INPUT_BTN_SIDE   0x113U
#define INPUT_BTN_EXTRA  0x114U
#define INPUT_BTN_TOUCH  0x14AU

/* Kody klawiszy klawiatury (Linux keycodes) */
#define INPUT_KEY_RESERVED   0U
#define INPUT_KEY_ESC        1U
#define INPUT_KEY_1          2U
#define INPUT_KEY_2          3U
#define INPUT_KEY_3          4U
#define INPUT_KEY_4          5U
#define INPUT_KEY_5          6U
#define INPUT_KEY_6          7U
#define INPUT_KEY_7          8U
#define INPUT_KEY_8          9U
#define INPUT_KEY_9          10U
#define INPUT_KEY_0          11U
#define INPUT_KEY_MINUS      12U
#define INPUT_KEY_EQUAL      13U
#define INPUT_KEY_BACKSPACE  14U
#define INPUT_KEY_TAB        15U
#define INPUT_KEY_Q          16U
#define INPUT_KEY_W          17U
#define INPUT_KEY_E          18U
#define INPUT_KEY_R          19U
#define INPUT_KEY_T          20U
#define INPUT_KEY_Y          21U
#define INPUT_KEY_U          22U
#define INPUT_KEY_I          23U
#define INPUT_KEY_O          24U
#define INPUT_KEY_P          25U
#define INPUT_KEY_LEFTBRACE  26U
#define INPUT_KEY_RIGHTBRACE 27U
#define INPUT_KEY_ENTER      28U
#define INPUT_KEY_LEFTCTRL   29U
#define INPUT_KEY_A          30U
#define INPUT_KEY_S          31U
#define INPUT_KEY_D          32U
#define INPUT_KEY_F          33U
#define INPUT_KEY_G          34U
#define INPUT_KEY_H          35U
#define INPUT_KEY_J          36U
#define INPUT_KEY_K          37U
#define INPUT_KEY_L          38U
#define INPUT_KEY_SEMICOLON  39U
#define INPUT_KEY_APOSTROPHE 40U
#define INPUT_KEY_GRAVE      41U
#define INPUT_KEY_LEFTSHIFT  42U
#define INPUT_KEY_BACKSLASH  43U
#define INPUT_KEY_Z          44U
#define INPUT_KEY_X          45U
#define INPUT_KEY_C          46U
#define INPUT_KEY_V          47U
#define INPUT_KEY_B          48U
#define INPUT_KEY_N          49U
#define INPUT_KEY_M          50U
#define INPUT_KEY_COMMA      51U
#define INPUT_KEY_DOT        52U
#define INPUT_KEY_SLASH      53U
#define INPUT_KEY_RIGHTSHIFT 54U
#define INPUT_KEY_KPASTERISK 55U
#define INPUT_KEY_LEFTALT    56U
#define INPUT_KEY_SPACE      57U
#define INPUT_KEY_CAPSLOCK   58U
#define INPUT_KEY_F1         59U
#define INPUT_KEY_F2         60U
#define INPUT_KEY_F3         61U
#define INPUT_KEY_F4         62U
#define INPUT_KEY_F5         63U
#define INPUT_KEY_F6         64U
#define INPUT_KEY_F7         65U
#define INPUT_KEY_F8         66U
#define INPUT_KEY_F9         67U
#define INPUT_KEY_F10        68U
#define INPUT_KEY_NUMLOCK    69U
#define INPUT_KEY_SCROLLLOCK 70U
#define INPUT_KEY_KP7        71U
#define INPUT_KEY_KP8        72U
#define INPUT_KEY_KP9        73U
#define INPUT_KEY_KPMINUS    74U
#define INPUT_KEY_KP4        75U
#define INPUT_KEY_KP5        76U
#define INPUT_KEY_KP6        77U
#define INPUT_KEY_KPPLUS     78U
#define INPUT_KEY_KP1        79U
#define INPUT_KEY_KP2        80U
#define INPUT_KEY_KP3        81U
#define INPUT_KEY_KP0        82U
#define INPUT_KEY_KPDOT      83U
#define INPUT_KEY_F11        87U
#define INPUT_KEY_F12        88U
#define INPUT_KEY_KPENTER    96U
#define INPUT_KEY_RIGHTCTRL  97U
#define INPUT_KEY_KPSLASH    98U
#define INPUT_KEY_RIGHTALT   100U
#define INPUT_KEY_HOME       102U
#define INPUT_KEY_UP         103U
#define INPUT_KEY_PAGEUP     104U
#define INPUT_KEY_LEFT       105U
#define INPUT_KEY_RIGHT      106U
#define INPUT_KEY_END        107U
#define INPUT_KEY_DOWN       108U
#define INPUT_KEY_PAGEDOWN   109U
#define INPUT_KEY_INSERT     110U
#define INPUT_KEY_DELETE     111U
#define INPUT_KEY_LEFTMETA   125U
#define INPUT_KEY_RIGHTMETA  126U

/* Klasyfikacja urzadzen wejsciowych (maska bitowa) */
enum InputDeviceKind : uint32_t {
    INPUT_DEVICE_KIND_UNKNOWN  = 0U,
    INPUT_DEVICE_KIND_KEYBOARD = 1U << 0,
    INPUT_DEVICE_KIND_MOUSE    = 1U << 1,
    INPUT_DEVICE_KIND_TABLET   = 1U << 2,
};

/* =========================================================================
 * 2. STRUKTURY DANYCH WEJSCIA
 * ========================================================================= */

#pragma pack(push, 1)
struct InputEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};
#pragma pack(pop)
static_assert(sizeof(InputEvent) == 8, "InputEvent rozmiar != 8 B");

struct InputAbsInfo {
    int32_t min;
    int32_t max;
    int32_t fuzz;
    int32_t flat;
    int32_t res;
};

struct InputDevice {
    char         name[64];
    uint32_t     kinds;
    InputAbsInfo abs_x;
    InputAbsInfo abs_y;
    void*        driver_data;
    void       (*poll_fn)(InputDevice* dev);

    /* Stan akumulowany pomiedzy raportami synchronizacji (SYN_REPORT) */
    int32_t      rel_x;
    int32_t      rel_y;
    int32_t      rel_wheel;
    int32_t      abs_val_x;
    int32_t      abs_val_y;
    bool         has_abs_x;
    bool         has_abs_y;
    uint8_t      buttons_state;
    uint8_t      prev_buttons_state;
    bool         registered;
};

/* =========================================================================
 * 3. PUBLICZNE API DYSPOZYTORA I REJESTRU URZADZEN WEJSCIOWYCH
 * ========================================================================= */

bool input_register_device(InputDevice* dev);
bool input_unregister_device(InputDevice* dev);
size_t input_get_device_count(void);
InputDevice* input_get_device(size_t index);
InputDevice* input_find_device_by_name(const char* name);

/* Dyspozycja pojedynczego lub serii zdarzen z urzadzenia wejsciowego */
void input_dispatch_event(InputDevice* dev, const InputEvent* ev);
void input_dispatch_events(InputDevice* dev, const InputEvent* evs, size_t count);

/* Polling / obsluga zarejestrowanych urzadzen spoza kontekstu twardego IRQ */
void input_service_all(void);

/* Reset rejestru urzadzen (uzywany przy restarcie i w testach) */
void input_reset_all(void);

/* =========================================================================
 * 4. STANDARDOWE TLUMACZENIE KODOW WEJSCIOWYCH ZE STEROWNIKOW SPRZETOWYCH
 * ========================================================================= */

/* Tlumaczenie kodu Scan Code Set 1 (PS/2) na kod Linux INPUT_KEY_* */
uint16_t input_ps2_scancode_to_linux(bool is_e0, uint8_t scancode);

/* Tlumaczenie USB HID Usage ID (Usage Page 0x07) na kod Linux INPUT_KEY_* */
uint16_t input_usb_hid_usage_to_linux(uint8_t usage);

/* Tlumaczenie indeksu bitu modyfikatora USB (0..7) na kod Linux INPUT_KEY_* */
uint16_t input_usb_hid_mod_to_linux(uint8_t bit_index);

#ifdef __cplusplus
}
#endif

