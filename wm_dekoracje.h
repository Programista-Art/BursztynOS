#pragma once
#include "motyw.h"
#include "ui_skala.h"
#include "bursztyn_ui/czcionka.h"

inline bool wm_dekoracja_w_punkcie(int w,int h,int x,int y,uint32_t scale) {
    if(x<0 || y<0 || x>=w || y>=h) return false;
    const int bar=ui_skaluj(28,scale),edge=ui_skaluj(1,scale);
    return y<bar || x<edge || x>=w-edge || y>=h-edge;
}

// Compositor-owned chrome. The callback clips to the current dirty region.
// No application buffers or window metadata are modified.
template<class Pixel>
void wm_rysuj_dekoracje(int w, int h, const char* title, bool maximized,
                       uint32_t scale, const BursztynMotyw& theme, Pixel pixel) {
    using BursztynUI::CzcionkaUI;
    SkalaUI s = SkalaUI::procent(scale);
    const int bar=ui_skaluj(28,scale), edge=ui_skaluj(1,scale);
    int radius=maximized?0:ui_skaluj(theme.rogokna,scale);
    if(radius>bar) radius=bar;
    if(radius>w/2) radius=w/2;
    auto rect=[&](int x,int y,int width,int height,uint32_t color) {
        for(int py=y;py<y+height && py<h;++py)
            for(int px=x;px<x+width && px<w;++px)
                if(px>=0 && py>=0) pixel(px,py,color);
    };
    for(int y=0;y<bar && y<h;++y) {
        int inset=y<radius?motyw_promien_inset(radius,y):0;
        rect(inset,y,w-2*inset,1,y<ui_skaluj(2,scale)?theme.akcent:theme.panel);
    }
    rect(0,bar,edge,h-bar,theme.ramka);
    rect(w-edge,bar,edge,h-bar,theme.ramka);
    rect(0,h-edge,w,edge,theme.ramka);
    auto text=[&](const char* str,int x,int y,int right,uint32_t color) {
        for(size_t i=0;str && str[i];) {
            uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(str,i,&cp);if(!n) break;i+=n;
            int width=CzcionkaUI::szerokosc_znaku(cp);
            if(x+width*s>right) break;
            const uint16_t* glyph=CzcionkaUI::bitmapa_znaku(cp);
            for(int gy=0;gy<16;++gy) for(int gx=0;gx<width;++gx)
                if(glyph[gy]&(1U<<(15-gx)))
                    rect(x+gx*s,y+gy*s,(gx+1)*s-gx*s,(gy+1)*s-gy*s,color);
            x+=CzcionkaUI::postep_znaku(cp,s);
        }
    };
    text(title,ui_skaluj(8,scale),ui_skaluj(7,scale),w-ui_skaluj(80,scale),theme.tekst);
    const int offsets[]={74,50,26};const char* labels[]={"-",maximized?"v":"^","X"};
    for(int i=0;i<3;++i) {
        int x=w-ui_skaluj(offsets[i],scale),y=ui_skaluj(4,scale),size=ui_skaluj(20,scale);
        rect(x,y,size,size,i==2?theme.blad:theme.akcent);
        int tx=x+(size-CzcionkaUI::zmierz_tekst(labels[i],s))/2;
        int ty=y+(size-CzcionkaUI::pobierz_wysokosc(s))/2;
        text(labels[i],tx,ty,x+size,theme.tekstakcentu);
    }
}
