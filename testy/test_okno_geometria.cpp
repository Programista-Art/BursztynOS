// Host test: jadro/gui/okno_geometria.h (resize zones, anchored resize, off-screen moves).
#include <stdio.h>
#include <stdint.h>
#include <random>
#include "../jadro/gui/okno_geometria.h"
using namespace OknoGeometria;
static int fails=0,checks=0;
#define CHECK(c) do{++checks;if(!(c)){++fails;printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);}}while(0)
static int64_t R(const Prost&p){return int64_t(p.x)+p.w;}
static int64_t B(const Prost&p){return int64_t(p.y)+p.h;}
int main(){
    // ---- hit test: eight zones, centre and outside
    const int W=400,H=300,G=5,C=12;
    CHECK(krawedz_w_punkcie(0,150,W,H,G,C)==KRAWEDZ_LEWA);
    CHECK(krawedz_w_punkcie(W-1,150,W,H,G,C)==KRAWEDZ_PRAWA);
    CHECK(krawedz_w_punkcie(200,0,W,H,G,C)==KRAWEDZ_GORNA);
    CHECK(krawedz_w_punkcie(200,H-1,W,H,G,C)==KRAWEDZ_DOLNA);
    CHECK(krawedz_w_punkcie(0,0,W,H,G,C)==(KRAWEDZ_LEWA|KRAWEDZ_GORNA));
    CHECK(krawedz_w_punkcie(W-1,0,W,H,G,C)==(KRAWEDZ_PRAWA|KRAWEDZ_GORNA));
    CHECK(krawedz_w_punkcie(0,H-1,W,H,G,C)==(KRAWEDZ_LEWA|KRAWEDZ_DOLNA));
    CHECK(krawedz_w_punkcie(W-1,H-1,W,H,G,C)==(KRAWEDZ_PRAWA|KRAWEDZ_DOLNA));
    CHECK(krawedz_w_punkcie(8,2,W,H,G,C)==(KRAWEDZ_LEWA|KRAWEDZ_GORNA));      // corner arm along the top edge
    CHECK(krawedz_w_punkcie(2,8,W,H,G,C)==(KRAWEDZ_LEWA|KRAWEDZ_GORNA));      // corner arm along the left edge
    CHECK(krawedz_w_punkcie(8,150,W,H,G,C)==KRAWEDZ_BRAK);                   // client area untouched
    CHECK(krawedz_w_punkcie(200,150,W,H,G,C)==KRAWEDZ_BRAK);
    CHECK(krawedz_w_punkcie(30,30,W,H,G,C)==KRAWEDZ_BRAK);
    CHECK(krawedz_w_punkcie(-1,10,W,H,G,C)==KRAWEDZ_BRAK);
    CHECK(krawedz_w_punkcie(W,10,W,H,G,C)==KRAWEDZ_BRAK);
    CHECK(krawedz_w_punkcie(10,H,W,H,G,C)==KRAWEDZ_BRAK);
    CHECK(krawedz_w_punkcie(0,0,0,0,G,C)==KRAWEDZ_BRAK);
    CHECK(krawedz_w_punkcie(0,0,-5,10,G,C)==KRAWEDZ_BRAK);
    // tiny windows: every border pixel resolves to something, the centre never does when large enough
    for(int w=1;w<=24;++w)for(int h=1;h<=24;++h){
        for(int x=0;x<w;++x)for(int y=0;y<h;++y){
            const uint8_t m=krawedz_w_punkcie(x,y,w,h,G,C);
            const bool border=x<G||y<G||x>=w-G||y>=h-G;
            CHECK(border==(m!=KRAWEDZ_BRAK));
        }
    }
    // ---- cursors
    CHECK(kursor_dla_krawedzi(KRAWEDZ_LEWA)==KURSOR_EW && kursor_dla_krawedzi(KRAWEDZ_PRAWA)==KURSOR_EW);
    CHECK(kursor_dla_krawedzi(KRAWEDZ_GORNA)==KURSOR_NS && kursor_dla_krawedzi(KRAWEDZ_DOLNA)==KURSOR_NS);
    CHECK(kursor_dla_krawedzi(KRAWEDZ_LEWA|KRAWEDZ_GORNA)==KURSOR_NWSE);
    CHECK(kursor_dla_krawedzi(KRAWEDZ_PRAWA|KRAWEDZ_DOLNA)==KURSOR_NWSE);
    CHECK(kursor_dla_krawedzi(KRAWEDZ_PRAWA|KRAWEDZ_GORNA)==KURSOR_NESW);
    CHECK(kursor_dla_krawedzi(KRAWEDZ_LEWA|KRAWEDZ_DOLNA)==KURSOR_NESW);
    CHECK(kursor_dla_krawedzi(0)==KURSOR_STRZALKA);
    // ---- resize anchoring
    const Prost s{100,80,400,300};const Ograniczenia lim{200,150,2000,1500};
    Prost r=zmien_rozmiar(s,KRAWEDZ_PRAWA,50,0,lim);CHECK(r==(Prost{100,80,450,300}));
    r=zmien_rozmiar(s,KRAWEDZ_DOLNA,0,-40,lim);CHECK(r==(Prost{100,80,400,260}));
    r=zmien_rozmiar(s,KRAWEDZ_LEWA,-30,0,lim);CHECK(r==(Prost{70,80,430,300}) && R(r)==R(s));
    r=zmien_rozmiar(s,KRAWEDZ_GORNA,0,-30,lim);CHECK(r==(Prost{100,50,400,330}) && B(r)==B(s));
    r=zmien_rozmiar(s,KRAWEDZ_LEWA|KRAWEDZ_GORNA,20,25,lim);CHECK(r==(Prost{120,105,380,275}) && R(r)==R(s) && B(r)==B(s));
    r=zmien_rozmiar(s,KRAWEDZ_PRAWA|KRAWEDZ_DOLNA,-1000,-1000,lim);CHECK(r.w==200 && r.h==150 && r.x==100 && r.y==80);
    r=zmien_rozmiar(s,KRAWEDZ_LEWA|KRAWEDZ_GORNA,1000,1000,lim);CHECK(r.w==200 && r.h==150 && R(r)==R(s) && B(r)==B(s));
    r=zmien_rozmiar(s,KRAWEDZ_PRAWA,100000,0,lim);CHECK(r.w==2000);
    // extreme pointer deltas never overflow or go negative
    const int32_t ex[]={INT32_MIN,INT32_MIN+1,-1,0,1,INT32_MAX-1,INT32_MAX};
    for(uint8_t m=1;m<16;++m)for(int32_t dx:ex)for(int32_t dy:ex){
        Prost q=zmien_rozmiar({INT32_MAX-10,INT32_MIN+10,300,200},m,dx,dy,lim);
        CHECK(q.w>=lim.min_w&&q.w<=lim.max_w&&q.h>=lim.min_h&&q.h<=lim.max_h);
        Prost z=zmien_rozmiar({0,0,300,200},m,dx,dy,lim);
        CHECK(z.w>=lim.min_w&&z.w<=lim.max_w&&z.h>=lim.min_h&&z.h<=lim.max_h);
    }
    // degenerate limits are repaired
    r=zmien_rozmiar(s,KRAWEDZ_PRAWA,10,0,{0,0,-5,-5});CHECK(r.w>=1&&r.h>=1);
    // ---- moves off-screen at all supported resolutions
    const int res[][2]={{800,600},{1024,768},{1280,720},{1920,1080},{2560,1440}};
    for(const auto&rs:res){
        const Prost work{0,0,rs[0],rs[1]-40};   // taskbar excluded
        const int belka=28,uchwyt=96,nad=14;
        const Prost win{50,50,640,480};
        const LimityRuchu l=limity_ruchu(win,work,uchwyt,belka,nad);
        // left/right/bottom overflow allowed, title strip reachable
        CHECK(l.min_x<0 && l.min_x==-win.w+uchwyt);
        CHECK(l.max_x==work.w-uchwyt && l.max_x+win.w>work.w);     // window extends past the right edge
        CHECK(l.max_y==work.h-belka && l.max_y+win.h>work.h);       // and past the bottom
        CHECK(l.min_y==-nad);                                       // partly above the desktop
        Prost a=przytnij_ruch({-100000,-100000,640,480},work,uchwyt,belka,nad);
        CHECK(a.x==l.min_x && a.y==l.min_y && a.w==640 && a.h==480);
        Prost b=przytnij_ruch({100000,100000,640,480},work,uchwyt,belka,nad);
        CHECK(b.x==l.max_x && b.y==l.max_y);
        CHECK(pasek_uchwytny(a,work,uchwyt,belka,nad) && pasek_uchwytny(b,work,uchwyt,belka,nad));
        // title strip: >= `uchwyt` px of the title bar horizontally visible at the extremes
        CHECK(a.x+a.w-work.x>=uchwyt && work.x+work.w-b.x>=uchwyt);
        // title bar never fully below / above the work area
        CHECK(b.y<work.y+work.h && a.y+belka>work.y);
        // a window larger than the desktop is still recoverable
        const Prost big{0,0,rs[0]+500,rs[1]+500};
        Prost c=przytnij_ruch({INT32_MIN,INT32_MIN,big.w,big.h},work,uchwyt,belka,nad);
        CHECK(pasek_uchwytny(c,work,uchwyt,belka,nad));
        Prost d=przytnij_ruch({INT32_MAX,INT32_MAX,big.w,big.h},work,uchwyt,belka,nad);
        CHECK(pasek_uchwytny(d,work,uchwyt,belka,nad));
        // a window that was fine stays fine (idempotent)
        CHECK(przytnij_ruch(a,work,uchwyt,belka,nad)==a && przytnij_ruch(b,work,uchwyt,belka,nad)==b);
        // after a resolution drop an off-limit window becomes grabbable again
        const Prost small{0,0,800,560};
        Prost far{1900,1000,640,480};
        CHECK(!pasek_uchwytny(far,small,uchwyt,belka,nad));
        CHECK(pasek_uchwytny(przytnij_ruch(far,small,uchwyt,belka,nad),small,uchwyt,belka,nad));
    }
    // ---- randomised invariants
    std::mt19937 rng(12345);
    auto rnd=[&](int lo,int hi){return int(lo+rng()%uint32_t(hi-lo+1));};
    for(int i=0;i<200000;++i){
        Prost st{rnd(-3000,3000),rnd(-3000,3000),rnd(1,3000),rnd(1,3000)};
        Ograniczenia lm{rnd(1,400),rnd(1,300),rnd(401,4096),rnd(301,4096)};
        if(st.w<lm.min_w)st.w=lm.min_w;if(st.h<lm.min_h)st.h=lm.min_h;
        if(st.w>lm.max_w)st.w=lm.max_w;if(st.h>lm.max_h)st.h=lm.max_h;
        uint8_t m=uint8_t(rnd(1,15));
        Prost q=zmien_rozmiar(st,m,rnd(-5000,5000),rnd(-5000,5000),lm);
        CHECK(q.w>=lm.min_w&&q.w<=lm.max_w&&q.h>=lm.min_h&&q.h<=lm.max_h);
        if(m&KRAWEDZ_LEWA)CHECK(R(q)==R(st)); else CHECK(q.x==st.x);
        if(m&KRAWEDZ_GORNA)CHECK(B(q)==B(st)); else CHECK(q.y==st.y);
        if(!(m&(KRAWEDZ_LEWA|KRAWEDZ_PRAWA)))CHECK(q.w==st.w);
        if(!(m&(KRAWEDZ_GORNA|KRAWEDZ_DOLNA)))CHECK(q.h==st.h);
    }
    printf("okno_geometria: %d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
