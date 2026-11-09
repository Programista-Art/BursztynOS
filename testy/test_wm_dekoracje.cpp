#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "wm_dekoracje.h"
static unsigned checks=0;
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
int main() {
    for(uint32_t scale:{100U,125U}) for(bool maximized:{false,true}) {
        int w=ui_skaluj(700,scale),h=ui_skaluj(450,scale);
        std::vector<uint32_t> client(w*h,0xabcdef),scene=client;
        BursztynMotyw theme=g_domyslny_motyw_fallback;
        auto draw=[&]() {
            wm_rysuj_dekoracje(w,h,"Powłoka ąęż",maximized,scale,theme,
                [&](int x,int y,uint32_t color) { assert(x>=0 && y>=0 && x<w && y<h);scene[y*w+x]=color; });
        };
        draw();auto previous=scene;theme.panel^=0xffffff;theme.ramka^=0xffffff;
        theme.akcent^=0xffffff;theme.tekst^=0xffffff;theme.blad^=0xffffff;theme.tekstakcentu^=0xffffff;
        draw();assert(scene!=previous);
        assert(scene[ui_skaluj(26,scale)*w+w/2]==theme.panel);
        assert(scene[(h-1)*w+w/2]==theme.ramka);
        for(int y=ui_skaluj(28,scale);y<h-ui_skaluj(1,scale);++y)
            for(int x=ui_skaluj(1,scale);x<w-ui_skaluj(1,scale);++x)
                if(WindowShape(w,h,maximized,scale,theme).client(x,y))assert(scene[y*w+x]==client[y*w+x]);
        // A dirty-region callback must leave all pixels outside that region intact.
        scene.assign(w*h,0xabcdef);
        wm_rysuj_dekoracje(w,h,"ąęż",maximized,scale,theme,[&](int x,int y,uint32_t color) {
            if(x>=100 && x<120 && y>=5 && y<15) scene[y*w+x]=color;
        });
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            if(x<100 || x>=120 || y<5 || y>=15) assert(scene[y*w+x]==0xabcdef);
    }
    for(uint32_t dpi:{100U,125U,150U,200U}){
        auto theme=g_domyslny_motyw_fallback;theme.rogokna=32;
        WindowShape shape(800,600,false,dpi,theme);
        for(int y=ui_skaluj(4,dpi);y<ui_skaluj(24,dpi);++y)
            for(int x=800-ui_skaluj(26,dpi);x<800-ui_skaluj(6,dpi);++x)assert(shape.contains(x,y));
    }
    printf("WM decoration renderer: %u/%u PASS\n",checks,checks);
    puts("WM decoration renderer host checks passed (100%, 125%); not runtime validation.");
}
