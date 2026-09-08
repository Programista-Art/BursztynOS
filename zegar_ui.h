#pragma once

#include <stdint.h>
#include "ui_skala.h"

struct ZegarUiGeometria {
    int x;
    int y;
    int szer;
    int wys;
    int tekst_x;
    int tekst_y;
    int tekst_szer;
    int tekst_wys;
    int prawy_margines;
    int skala_fontu;
};

/* Zegar uzywa logicznie tej samej metryki 150x40 co historyczny pasek,
 * ale kazdy jej skladnik jest przeliczany kanonicznym ui_skaluj(). */
constexpr ZegarUiGeometria zegar_ui_geometria(int ekran_w, int ekran_h,
                                               uint32_t procent,
                                               int tekst_szer) {
    const int szer = ui_skaluj(150, procent);
    const int wys = ui_skaluj(40, procent);
    const int margines = ui_skaluj(10, procent);
    const int font = ui_skaluj(200, procent);
    const int tekst_wys = ui_skaluj(16, static_cast<uint32_t>(font));
    return {
        ekran_w - szer,
        ekran_h - wys,
        szer,
        wys,
        ekran_w - margines - tekst_szer,
        ekran_h - wys + (wys - tekst_wys) / 2,
        tekst_szer,
        tekst_wys,
        margines,
        font
    };
}
