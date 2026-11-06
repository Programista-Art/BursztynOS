#pragma once
#include "display_geometry.h"

// Populate the already allocated replacement root surface. Desktop content
// retains physical coordinates until the WM reflows its canonical anchors.
// The retained taskbar is relocated/scaled as a separate strip immediately;
// its old pixels must never be copied into the new work area.
inline uint32_t desktop_modeset_pixel(const uint32_t* src,int width,int height,
                                     const DisplayGeometry& old,
                                     const DisplayGeometry& next,int x,int y) {
    if(y>=next.taskbar_y) {
        if(!src || old.taskbar_width<1 || old.taskbar_height<1)return 0;
        int sx=old.taskbar_x+int(int64_t(x-next.taskbar_x)*old.taskbar_width/next.taskbar_width);
        int sy=old.taskbar_y+int(int64_t(y-next.taskbar_y)*old.taskbar_height/next.taskbar_height);
        return sx>=0 && sy>=0 && sx<width && sy<height?src[size_t(sy)*width+sx]:0;
    }
    return src && x<width && y<height && y<old.taskbar_y?src[size_t(y)*width+x]:0;
}
