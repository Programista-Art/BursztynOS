#pragma once
#include <stdint.h>

// BWS57 mode 4: physical, transparency-preserving region replacements in one
// screen-locked operation. Modes 0..3 retain their original ABI.
constexpr uint32_t BWS_GUI_BATCH_MAX = 192;
constexpr uint64_t BWS_GUI_BATCH_PIXELS_MAX = 16ULL * 1024 * 1024;
struct BwsGuiRegion {
    int32_t x, y, width, height;
    uint64_t pixels;
};
struct BwsGuiBatch {
    uint32_t count, display_generation;
    uint64_t regions;
};
static_assert(sizeof(BwsGuiRegion)==24 && sizeof(BwsGuiBatch)==16,"BWS57 batch ABI");
