#pragma once
#include "skladacz_obrazu.h"

// Rendering does not add input objects. The existing owning layer receives
// both client and decoration events; popup dismissal keeps its modal contract.
template<class Layer, class Decoration>
int wm_warstwa_pod_punktem(int x, int y, int overlay, Layer layer, Decoration decoration) {
    if (overlay >= 0) return overlay;
    int result = -1, z = INT32_MIN;
    for (int pid=1; pid<SKLADACZ_MAKS_WARSTW; ++pid) {
        const warstwa_obrazu* w=layer(pid);
        if (!w || !w->aktywna || x<w->x || y<w->y ||
            int64_t(x)>=int64_t(w->x)+w->szerokosc ||
            int64_t(y)>=int64_t(w->y)+w->wysokosc) continue;
        const int lx=x-w->x, ly=y-w->y;
        if (!decoration(pid,lx,ly) && w->bufor_pikseli &&
            w->bufor_pikseli[uint64_t(ly)*w->szerokosc+lx]==0) continue;
        if (result<0 || w->z_order>z || (w->z_order==z && pid>result)) {
            result=pid; z=w->z_order;
        }
    }
    return result;
}
