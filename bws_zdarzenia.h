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
    BWS_ZDARZENIE_ZAMKNIJ = 8
};

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
