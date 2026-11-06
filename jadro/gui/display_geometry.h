#pragma once
#include <stdint.h>
#include "ui_skala.h"

// All rectangles and pointer coordinates are physical framebuffer pixels.
// Scale is used only when deriving UI metrics, never pointer motion/range.
struct DisplayGeometry {
    uint32_t version=1,generation=0;
    int32_t width=0,height=0;
    uint32_t scale=100;
    int32_t work_x=0,work_y=0,work_width=0,work_height=0;
    int32_t taskbar_x=0,taskbar_y=0,taskbar_width=0,taskbar_height=0;
    int32_t cursor_max_x=0,cursor_max_y=0;
    uint32_t desktop_generation=0;
};
inline DisplayGeometry display_geometry(int width,int height,uint32_t scale,uint32_t generation) {
    DisplayGeometry g{};
    if(width<1 || height<1 || width>8192 || height>8192 || !ui_skala_poprawna(scale))return g;
    g.width=width;g.height=height;g.scale=scale;g.generation=generation;
    g.work_width=g.taskbar_width=width;
    g.taskbar_height=ui_skaluj(40,scale);
    if(g.taskbar_height>=height)g.taskbar_height=height-1;
    g.work_height=g.taskbar_y=height-g.taskbar_height;
    g.cursor_max_x=width-1;g.cursor_max_y=height-1;
    return g;
}
inline int display_clamp(int value,int max) {return value<0?0:value>max?max:value;}
static_assert(sizeof(DisplayGeometry)==64,"Bounded display snapshot ABI");
