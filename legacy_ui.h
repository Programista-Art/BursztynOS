#pragma once

#include <stdint.h>
#include "ui_skala.h"

struct GuiLegacyProstokat {
    int x, y, szer, wys;
};

/* Czysta, bezalokacyjna czesc adaptera legacy. Wspolrzedne wejsciowe sa
 * logiczne (100%), wynik jest fizyczny i ograniczony do BWS62 work area. */
constexpr GuiLegacyProstokat gui_legacy_geometria_fizyczna(
    GuiLegacyProstokat logiczna, GuiLegacyProstokat obszar_roboczy,
    uint32_t procent) {
    int w = ui_skaluj(logiczna.szer, procent);
    int h = ui_skaluj(logiczna.wys, procent);
    if (w > obszar_roboczy.szer) w = obszar_roboczy.szer;
    if (h > obszar_roboczy.wys) h = obszar_roboczy.wys;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int x = ui_skaluj(logiczna.x, procent);
    int y = ui_skaluj(logiczna.y, procent);
    const int max_x = obszar_roboczy.x + obszar_roboczy.szer - w;
    const int max_y = obszar_roboczy.y + obszar_roboczy.wys - h;
    if (x < obszar_roboczy.x) x = obszar_roboczy.x;
    if (y < obszar_roboczy.y) y = obszar_roboczy.y;
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;
    return {x, y, w, h};
}

