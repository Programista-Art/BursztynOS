#include <cassert>
#include <cstdio>
#include <vector>
#include "../wm_dekoracje.h"
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
                assert(scene[y*w+x]==client[y*w+x]);
        // A dirty-region callback must leave all pixels outside that region intact.
        scene.assign(w*h,0xabcdef);
        wm_rysuj_dekoracje(w,h,"ąęż",maximized,scale,theme,[&](int x,int y,uint32_t color) {
            if(x>=100 && x<120 && y>=5 && y<15) scene[y*w+x]=color;
        });
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            if(x<100 || x>=120 || y<5 || y>=15) assert(scene[y*w+x]==0xabcdef);
    }
    puts("WM decoration renderer host checks passed (100%, 125%); not runtime validation.");
}
