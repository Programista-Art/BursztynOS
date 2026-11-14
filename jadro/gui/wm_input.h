#pragma once
#include "skladacz_obrazu.h"

// Rendering does not add input objects. The existing owning layer receives
// both client and decoration events; popup dismissal keeps its modal contract.
struct WmPelnePokrycie {bool operator()(int,int,int)const{return true;}};
// Pasek zadan jest wyzej niz zwykle okna, a nizej niz okna z jawnym zezwoleniem "nad paskiem"
// (ta sama kolejnosc co kompozycja: skladacz_obrazu.cpp / zloz_pasek_zadan). Wyzszy z_order
// zwyklego okna NIE daje mu priorytetu nad paskiem.
struct WmPasek {
    bool aktywny=false;int pid=-1;int x=0,y=0,w=0,h=0;
    bool zawiera(int px,int py)const{return aktywny&&px>=x&&py>=y&&int64_t(px)<int64_t(x)+w&&int64_t(py)<int64_t(y)+h;}
};
struct WmBrakNadPaskiem {bool operator()(int)const{return false;}};
template<class Layer, class Decoration, class Coverage=WmPelnePokrycie, class NadPaskiem=WmBrakNadPaskiem>
int wm_warstwa_pod_punktem(int x, int y, int overlay, Layer layer, Decoration decoration,Coverage coverage={},
                           WmPasek pasek={}, NadPaskiem nad_paskiem={}) {
    if (overlay >= 0) return overlay;
    const bool w_pasku = pasek.zawiera(x,y);
    int result = -1, z = INT32_MIN;
    for (int pid=1; pid<SKLADACZ_MAKS_WARSTW; ++pid) {
        // W obszarze paska liczy sie tylko okno z zezwoleniem "nad paskiem" (inaczej wygrywa pasek).
        if (w_pasku && pid != pasek.pid && !nad_paskiem(pid)) continue;
        if (w_pasku && pid == pasek.pid) continue;
        const warstwa_obrazu* w=layer(pid);
        if (!w || !w->aktywna || x<w->x || y<w->y ||
            int64_t(x)>=int64_t(w->x)+w->szerokosc ||
            int64_t(y)>=int64_t(w->y)+w->wysokosc) continue;
        const int lx=x-w->x, ly=y-w->y;
        if(!coverage(pid,lx,ly))continue;
        // The root desktop is transparent over the wallpaper but owns background input.
        if (w->z_order != 0 && !decoration(pid,lx,ly) && w->bufor_pikseli &&
            w->bufor_pikseli[uint64_t(ly)*w->szerokosc+lx]==0) continue;
        if (result<0 || w->z_order>z || (w->z_order==z && pid>result)) {
            result=pid; z=w->z_order;
        }
    }
    if (w_pasku && result < 0) return pasek.pid;
    return result;
}

// Motion follows the hit window; capture keeps selection/scrollbar/window drags.
inline int wm_cel_ruchu(int focus,int capture,int hit,bool motion_only) {
    return capture>0?capture:(motion_only?hit:focus);
}

inline int wm_opuszczony_cel_ruchu(int& previous,int current,int capture) {
    if(capture>0)return -1;
    const int old=previous;previous=current;
    return old>0&&old!=current?old:-1;
}
