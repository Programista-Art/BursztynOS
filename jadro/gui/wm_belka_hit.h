#pragma once
#include "bursztyn_gui.h"
#include "motyw.h"
#include "ui_skala.h"

inline gui_akcja_belki wm_hit_test_belki(int mx,int my,int x,int y,int width,
                                        uint32_t scale,int radius) {
    if(width<ui_skaluj(90,scale) || mx<x || int64_t(mx)>=int64_t(x)+width ||
       my<y || int64_t(my)>=int64_t(y)+ui_skaluj(28,scale)) return GUI_BELKA_BRAK;
    const int dy=my-y;
    if(dy<radius) {
        const int inset=motyw_promien_inset(radius,dy);
        if(mx<x+inset || int64_t(mx)>=int64_t(x)+width-inset) return GUI_BELKA_BRAK;
    }
    if(int64_t(mx)>=int64_t(x)+width-ui_skaluj(26,scale)) return GUI_BELKA_ZAMKNIJ;
    if(int64_t(mx)>=int64_t(x)+width-ui_skaluj(50,scale)) return GUI_BELKA_MAKSYMALIZUJ;
    if(int64_t(mx)>=int64_t(x)+width-ui_skaluj(74,scale)) return GUI_BELKA_MINIMALIZUJ;
    return GUI_BELKA_DRAG;
}

