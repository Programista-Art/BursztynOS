#pragma once
#include <stdint.h>

// Existing boolean APIs keep 0/1. Explicit status requests use these values.
enum class WynikPlikowy : uint32_t {
    Blad=0, Ok=1, Istnieje=2, BrakUprawnien=3, Niepusty=4,
    Brak=5, NieprawidlowaNazwa=6, BrakMiejsca=7, BladIO=8,
    Chroniony=9, Czesciowo=10, Uszkodzony=11, Limit=12, BrakPamieci=13
};
constexpr uint64_t BWS_USUN_DRZEWO = 1;
constexpr uint64_t BWS_STATUS_PLIKOWY = 2;
constexpr uint32_t PSF_USUN_MAX_WEZLOW = 4096;
constexpr uint32_t PSF_USUN_MAX_GLEBOKOSC = 32;
