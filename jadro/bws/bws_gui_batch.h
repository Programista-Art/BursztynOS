#pragma once
#include <stdint.h>

/*
 * BWS57 mode 4: physical, transparency-preserving region replacements in one
 * screen-locked operation. Modes 0..3 retain their original ABI.
 */
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

/*
 * BWS57 mode 5: many scaled region replacements in one screen-locked call.
 *
 * Kazdy region opisuje ciagle zrodlo src_w x src_h i prostokat docelowy
 * w ekranie. Kontrakt jest identyczny z BWS98 (pojedynczy skalowany blit):
 * filtr i tryb dopasowania sa wspolne dla calego batchu, a puste piksele
 * sa pomijane albo nadpisywane zaleznie od flag regionu.
 *
 * Batch waliduje WSZYSTKIE regiony przed zapisem pierwszego piksela -
 * odrzucony batch nie zostawia polowicznie zaktualizowanej powierzchni.
 */
constexpr uint32_t BWS_GUI_BATCH_SKALOWANY_MAX = 64;
struct BwsGuiRegionSkalowany {
    int32_t x, y, width, height;
    uint32_t src_w, src_h;
    uint32_t flagi, zarezerwowane;
    uint64_t pixels;
};
struct BwsGuiBatchSkalowany {
    uint32_t count, display_generation;
    uint32_t filtr, tryb;
    uint64_t regions;
};
static_assert(sizeof(BwsGuiRegionSkalowany)==40 && sizeof(BwsGuiBatchSkalowany)==24,
              "BWS57 mode 5 ABI");
