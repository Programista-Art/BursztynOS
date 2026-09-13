#pragma once

#include <stdint.h>

enum bws_typ_zdarzenia : uint32_t {
    BWS_ZDARZENIE_BRAK = 0,
    BWS_ZDARZENIE_MYSZ_RUCH = 1,
    BWS_ZDARZENIE_MYSZ_DOWN = 2,
    BWS_ZDARZENIE_MYSZ_UP = 3,
    BWS_ZDARZENIE_KLAWISZ = 4,
    BWS_ZDARZENIE_TIMER = 5,
    BWS_ZDARZENIE_FOCUS = 6,
    BWS_ZDARZENIE_BLUR = 7,
    BWS_ZDARZENIE_ZAMKNIJ = 8,
    BWS_ZDARZENIE_OKNO_UTWORZONE = 9,
    BWS_ZDARZENIE_OKNO_POKAZANE = 10,
    BWS_ZDARZENIE_OKNO_ZMINIMALIZOWANE = 11,
    BWS_ZDARZENIE_OKNO_PRZYWROCONE = 12,
    BWS_ZDARZENIE_OKNO_FOCUS = 13,
    BWS_ZDARZENIE_OKNO_ZAMKNIETE = 14,
    BWS_ZDARZENIE_OKNO_TYTUL = 15,
    /* Program skojarzony powinien ponownie pobrac argument startowy. */
    BWS_ZDARZENIE_OTWORZ_PLIK = 16,
    BWS_ZDARZENIE_DRAG_HOVER = 17,
    BWS_ZDARZENIE_DRAG_LEAVE = 18,
    BWS_ZDARZENIE_DRAG_DROP = 19,
    /* Addytywne typy: stare aplikacje nie pomyla PPM z lewym DOWN/UP. */
    BWS_ZDARZENIE_MYSZ_PRAWY_DOWN = 20,
    BWS_ZDARZENIE_MYSZ_PRAWY_UP = 21,
    BWS_ZDARZENIE_PLIKI_ZMIENIONE = 22,
    /* Addytywne eventy zachowuja 40-bajtowy ABI struktury ponizej. */
    BWS_ZDARZENIE_KLAWISZ_FIZYCZNY = 23,
    BWS_ZDARZENIE_TEXT_INPUT = 24,
    BWS_ZDARZENIE_MYSZ_KOLO = 25,
    BWS_ZDARZENIE_WSKAZNIK_PRECYZYJNY = 26,
    BWS_ZDARZENIE_FILE_WATCH = 27,
    BWS_ZDARZENIE_IPC_READY = 28,
    BWS_ZDARZENIE_EKRAN = 29,
    BWS_ZDARZENIE_MOTYW_ZMIENIONY = 30
};

enum bws_modyfikatory : uint32_t {
    BWS_MOD_SHIFT = 1U << 0,
    BWS_MOD_CTRL  = 1U << 1,
    BWS_MOD_ALT   = 1U << 2,
    BWS_MOD_SUPER = 1U << 3,
    BWS_MOD_CAPS  = 1U << 4,
    BWS_MOD_NUM   = 1U << 5
};

#define BWS_KEY_SOURCE_E0  (1U << 8)
#define BWS_KEY_SOURCE_USB (1U << 16)

struct bws_zdarzenie {
    uint32_t typ;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint32_t przyciski;
    uint32_t kod;
    uint64_t timestamp;
};

static_assert(sizeof(bws_zdarzenie) == 40,
              "ABI zdarzenia BWS musi pozostac stabilne");
