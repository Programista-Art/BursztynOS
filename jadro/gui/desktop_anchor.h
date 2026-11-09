#pragma once
#include <stdint.h>

namespace Desktop {
// Persistent grid ownership and manual offset. Offset is kept at the DPI of
// the last intentional placement; rendering never feeds rounded pixels back.
struct Anchor {
    int col=0,row=0,offsetX=0,offsetY=0;
    uint32_t dpi=0;
};
inline int floor_div(int n,int d) {int q=n/d;return q-(n%d<0);}
inline int transform_offset(int offset,uint32_t source,uint32_t target) {
    return source?int(int64_t(offset)*target/source):offset;
}
inline Anchor capture_anchor(int x,int y,int x0,int y0,int cw,int ch,uint32_t dpi) {
    int col=floor_div(x-x0,cw),row=floor_div(y-y0,ch);
    return {col,row,x-x0-col*cw,y-y0-row*ch,dpi};
}
}
