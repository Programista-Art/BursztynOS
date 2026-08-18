#pragma once
#include <stdint.h>

constexpr uint8_t MYSZ_PRZYCISK_LEWY = 1U << 0;
constexpr uint8_t MYSZ_PRZYCISK_PRAWY = 1U << 1;
constexpr uint8_t MYSZ_PRZYCISK_SRODKOWY = 1U << 2;
constexpr uint8_t MYSZ_MASKA_PRZYCISKOW = MYSZ_PRZYCISK_LEWY |
    MYSZ_PRZYCISK_PRAWY | MYSZ_PRZYCISK_SRODKOWY;

extern "C" void zaktualizuj_mysze(int dx,int dy,uint8_t przyciski);
