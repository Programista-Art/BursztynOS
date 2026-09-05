#pragma once
#include <stdint.h>
#ifndef BURSZTYN_UI_DIAGNOSTYKA
#define BURSZTYN_UI_DIAGNOSTYKA 0
#endif
namespace BursztynUI {
struct PomiaryKontrolki {
    uint64_t dirty_calls{0}, dirty_pixels{0}, paint_calls{0}, layout_passes{0};
    uint64_t mouse_events{0}, identity_changes{0}, thumbnail_paints{0}, scrollbar_paints{0};
};
}
#if BURSZTYN_UI_DIAGNOSTYKA
#define BUI_POMIAR(pole, n) (pomiary_.pole += (n))
#else
#define BUI_POMIAR(pole, n) ((void)0)
#endif
