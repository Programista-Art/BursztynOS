#include <cassert>
#include <cstdio>
#include <vector>
#include <climits>
#include "kursor_prezentacja.h"
#define BURSZTYNDIAGCURSORPERF 1
#include "kursor_diag.h"
#include <string>
static uint64_t mock_ns=1000000;
static std::string anomaly_log;
bool hpet_dostepny(){return true;}
uint64_t czas_monotoniczny_ns(){return mock_ns;}
void klog_serial(const char* s){anomaly_log+=s;}
int grafika_pobierz_szerokosc(){return 320;}
int grafika_pobierz_wysokosc(){return 240;}

struct Harness {
    static constexpr int w=320,h=240,stride=324;
    std::vector<uint32_t> scene=std::vector<uint32_t>(stride*h,0x123456);
    std::vector<uint32_t> fb=scene;
    uint32_t scratch[64*64]{};
    GuiDirtyRect old{};
    unsigned presents=0;
    std::vector<GuiDirtyRect> presented;
    void move(int x,int y,const std::vector<GuiDirtyRect>& dirty={}) {
        presents=0;presented.clear();
        const auto clean=scene;
        old=kursor_prezentuj_transakcje(scene.data(),stride,w,h,old,x,y,16,16,
            scratch,dirty.data(),dirty.size(),
            [](int cx,int cy,uint32_t& c){c=0xffffff;return cx<=cy;},
            [&](GuiDirtyRect r){
                assert(r.x>=0&&r.y>=0&&r.width>0&&r.height>0);
                assert(r.x+r.width<=w&&r.y+r.height<=h);
                ++presents;presented.push_back(r);
                for(int yy=r.y;yy<r.y+r.height;++yy)for(int xx=r.x;xx<r.x+r.width;++xx)
                    fb[yy*stride+xx]=scene[yy*stride+xx];
                // Every presented transaction involving new cursor contains it.
                if(x>=0&&y>=0&&x<w&&y<h&&kursor_przecina(r,{x,y,16,16}))
                    assert(fb[y*stride+x]==0xffffff);
            },[](int){});
        assert(scene==clean); // temporary cache cannot persist stale scene
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx) {
            uint32_t expected=scene[yy*stride+xx];
            if(xx>=x&&xx<x+16&&yy>=y&&yy<y+16&&xx-x<=yy-y)expected=0xffffff;
            assert(fb[yy*stride+xx]==expected); // includes all old cursor pixels
        }
    }
};
int main() {
    Harness t;
    t.move(160,120);assert(t.presents==1);
    t.move(230,120);assert(t.presents==1);
    assert(t.presented[0].x==160&&t.presented[0].width==86);
    t.move(160,120);t.move(160,200);assert(t.presents==1);
    assert(t.presented[0].y==120&&t.presented[0].height==96);
    KursorLatest q;
    for(int i=0;i<100;++i)q.queue(50+i,60+i);
    assert(q.pending&&q.coalesced==99);
    int x=0,y=0;assert(q.take(x,y)&&x==149&&y==159);
    assert(!q.take(x,y));t.move(x,y);assert(t.presents==1);
    q.queue(20,30);assert(q.take(x,y));q.queue(40,50);
    t.move(x,y);assert(q.pending&&q.x==40&&q.y==50);
    q.retry(1,2);assert(q.x==40&&q.y==50);
    assert(q.take(x,y));t.move(x,y);
    q.retry(60,70);assert(q.take(x,y)&&x==60&&y==70);t.move(x,y);
    for(auto p:std::vector<GuiDirtyRect>{{0,0,0,0},{319,0,0,0},{0,239,0,0},{319,239,0,0},
                                      {-5,-5,0,0},{315,235,0,0},{-100,-100,0,0}}) {
        t.move(p.x,p.y);assert(t.presents<=1);
    }
    assert(kursor_clip({INT_MAX,INT_MAX,64,64},320,240).width==0);
    assert(kursor_clip({INT_MIN,INT_MIN,64,64},320,240).width==0);
    // Full scene and theme changes beneath a stationary/moving cursor.
    for(int kind=0;kind<3;++kind) {
        t.move(160,120);
        GuiDirtyRect r=kind==1?GuiDirtyRect{140,100,60,30}:GuiDirtyRect{0,0,320,240};
        for(int yy=r.y;yy<r.y+r.height;++yy)for(int xx=r.x;xx<r.x+r.width;++xx)
            t.scene[yy*t.stride+xx]=0x654320+kind;
        t.move(kind==0?180:160,120,{r});assert(t.presents==1);
        t.move(250,180);assert(t.presents==1); // refreshed background restored
    }
    // Separate old/new scene dirty rectangles join exactly one cursor transfer.
    t.move(50,50);
    t.move(180,180,{{50,50,16,16},{180,180,16,16}});assert(t.presents==1);
    // Transitive intersecting dirty rectangles cannot erase a new overlay.
    t.move(160,120,{{199,120,20,20},{180,120,20,20},{170,120,20,20}});
    assert(t.presents==1);
    // Independent scene region stays a separate small present.
    t.move(170,120,{{0,0,10,10}});assert(t.presents==2);
    // Physical hotspot and raster clipping are independent at both DPI values.
    for(unsigned scale:{100U,125U}) {
        for(auto mode:std::vector<GuiDirtyRect>{{0,0,1280,720},{0,0,1920,1080}}) {
            std::vector<uint32_t> scene(mode.width*mode.height,0x123456);
            uint32_t scratch[64*64]{};GuiDirtyRect old{};
            int bitmap=(16*scale+50)/100,hotspot=bitmap/2;
            for(int edge=0;edge<4;++edge)for(int i=0;i<=16;++i) {
                int x=edge==0?0:(edge==1?mode.width-1:i*(mode.width-1)/16);
                int y=edge==2?0:(edge==3?mode.height-1:i*(mode.height-1)/16);
                const int logical_x=x,logical_y=y;unsigned presents=0;
                old=kursor_prezentuj_transakcje(scene.data(),mode.width,mode.width,mode.height,
                    old,x-hotspot,y-hotspot,bitmap,bitmap,scratch,nullptr,0,
                    [](int,int,uint32_t& c){c=0xffffff;return true;},
                    [&](GuiDirtyRect r){++presents;assert(r.x>=0&&r.y>=0&&r.x+r.width<=mode.width&&r.y+r.height<=mode.height);},[](int){});
                assert(x==logical_x&&y==logical_y&&presents==1);
                if(x==0)assert(old.x==0);
                if(y==0)assert(old.y==0);
            }
        }
    }
    // Enabled diagnostics distinguish queue/compose/present and stay silent
    // for normal frames. These are mock clock measurements, not runtime data.
    kursor_diag_input();mock_ns+=1000000;kursor_diag_logical();
    kursor_diag_wm();kursor_diag_queue(false);kursor_diag_consume(nullptr,0);
    for(int stage=0;stage<4;++stage){mock_ns+=1000000;kursor_diag_stage(stage);}
    kursor_diag_finish();assert(anomaly_log.empty());
    mock_ns=2000000000;kursor_diag_input();kursor_diag_logical();kursor_diag_queue(false);
    mock_ns+=1000000;kursor_diag_input();kursor_diag_logical();kursor_diag_wm();kursor_diag_queue(true);
    GuiDirtyRect full{0,0,320,240};kursor_diag_consume(&full,1);kursor_diag_compose();kursor_diag_decor();
    kursor_diag_stage(0);mock_ns+=10000000;kursor_diag_stage(1);kursor_diag_stage(2);
    kursor_diag_present(full);mock_ns+=30000000;kursor_diag_stage(3);kursor_diag_finish();
    assert(anomaly_log.find("coalesced_total=1")!=std::string::npos);
    assert(anomaly_log.find("full_pending=1 composing=1 decor=1 scene=1")!=std::string::npos);
    assert(anomaly_log.find("presents=1 present_area=76800")!=std::string::npos);
    puts("Cursor diagnostics host PASS: default fast frames silent; anomaly timestamps, dirty metrics and coalescing logged");
    puts("Cursor host PASS: clipping, union, 100-position coalescing, interleaved producer, clean background, scene/theme/decor redraw, final-only presents and no stale pixels");
}
