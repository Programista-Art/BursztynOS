#pragma once
#include "motyw.h"
#include "ui_skala.h"

// Binary coverage matches the existing theme's integer rounded rectangles.
// The compositor owns final clipping: clients need no mask or corner erases.
struct WindowShape {
    int width, height, radius, bar, edge;
    WindowShape(int w,int h,bool maximized,uint32_t scale,const BursztynMotyw& theme)
        :width(w),height(h),radius(maximized?0:ui_skaluj(theme.rogokna,scale)),
         bar(ui_skaluj(28,scale)),edge(ui_skaluj(1,scale)) {
        // Keep the existing 20px controls at (right-26, top+4) wholly inside
        // coverage even for user themes requesting a very large radius.
        const int control_safe_radius=ui_skaluj(16,scale);
        if(radius>control_safe_radius)radius=control_safe_radius;
        if(radius>w/2)radius=w/2;
        if(radius>h/2)radius=h/2;
        if(radius<0)radius=0;
    }
    int inset(int y)const {
        int d=y<height-1-y?y:height-1-y;
        return d<radius?motyw_promien_inset(radius,d):0;
    }
    bool contains(int x,int y)const {
        if(x<0||y<0||x>=width||y>=height)return false;
        int i=inset(y);return x>=i&&x<width-i;
    }
    bool frame(int x,int y)const {
        if(!contains(x,y))return false;
        int i=inset(y);
        return y<bar||y>=height-edge||x<i+edge||x>=width-i-edge;
    }
    bool client(int x,int y)const {return contains(x,y)&&!frame(x,y);}
};
