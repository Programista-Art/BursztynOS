#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include "../opcje_startowe.h"
#include "../ekran_powitalny.cpp"
static unsigned checks=0,allocations=0,full=0,partial=0,live=0;
static uint64_t now=0;
static bool clock_ok=true,fail_alloc=false;
static int width=1280,height=720;
static warstwa_obrazu root_layer{};
static bool root_visible=false;
warstwa_obrazu* pobierz_warstwe(int pid){return pid==2?&root_layer:nullptr;}
bool skladacz_obrazu_czy_widoczna(int pid){return pid==2&&root_visible;}
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
bool hpet_dostepny(){return clock_ok;}
uint64_t czas_monotoniczny_us(){return now;}
void* bufor_grafiki_alokuj(size_t n){if(fail_alloc)return nullptr;++allocations;++live;return calloc(1,n);}
void bufor_grafiki_zwolnij(void* p){assert(p&&live);--live;free(p);}
static std::vector<std::string> logged_strings;
void klog_serial(const char* msg){logged_strings.emplace_back(msg);}
void PrzeniesNaEkran(){}
int grafika_pobierz_szerokosc(){return width;}
int grafika_pobierz_wysokosc(){return height;}
void skladacz_obrazu_oznacz_dirty(){++full;}
void skladacz_obrazu_oznacz_dirty_rect(int,int,int,int){++partial;}
static void advance(uint64_t t){now=t;powitanie_tick();}
int main(){
    using namespace Powitanie;
    assert(!strcmp(tekst(0),"Witamy w Bursztyn OS"));
    assert(!strcmp(tekst(1),"Polski system operacyjny"));
    assert(!strcmp(tekst(2),"Niezależny system"));
    Sekwencja s;s.rozpocznij(true,0);s.gotowy=true;
    for(auto t:{0ULL,1999999ULL,2000000ULL,2200000ULL,2400000ULL,4199999ULL,4200000ULL,4400000ULL,4600000ULL,6399999ULL}){
        s.aktualizuj(t);assert(s.stan==Stan::Widoczny);
        assert(s.komunikat==(t<2400000?0U:(t<4600000?1U:2U)));assert(s.przenikanie<=255);
    }
    s.aktualizuj(MINIMUM_US);assert(s.stan==Stan::Zanikanie&&s.odsloniecie==0);
    s.aktualizuj(MINIMUM_US+FADE_US/2);assert(s.odsloniecie==127);
    s.aktualizuj(MINIMUM_US+FADE_US);assert(s.stan==Stan::Zakonczony);
    s.aktualizuj(0);assert(s.stan==Stan::Zakonczony);
    s.rozpocznij(true,100);s.aktualizuj(10000100);assert(s.stan==Stan::Widoczny&&s.komunikat==2);
    s.gotowy=true;s.gotowy_od=10000100;s.aktualizuj(10000100);assert(s.stan==Stan::Zanikanie);
    s.aktualizuj(1);assert(s.odsloniecie==0);
    s.aktualizuj(10600100);assert(!s.aktywny());
    s.rozpocznij(true,UINT64_MAX-8000000);s.gotowy=true;s.aktualizuj(UINT64_MAX-1600000);assert(s.stan==Stan::Zanikanie);
    s.aktualizuj(UINT64_MAX);assert(s.stan==Stan::Zakonczony);
    for(unsigned weight=0;weight<=255;++weight){assert(mieszaj(0xFFFFFF,0,weight)==mieszaj(0,0xFFFFFF,255-weight));assert(mieszaj(0x123456,0x123456,weight)==0x123456);}
    for(const char* t:{tekst(0),tekst(1),tekst(2)}){
        int measured=0;bool polish=false;
        for(size_t i=0;t[i];){uint32_t cp;i+=BursztynUI::CzcionkaUI::dekoduj_utf8(t,i,&cp);assert(cp!='?');measured+=BursztynUI::CzcionkaUI::postep_znaku(cp);if(cp==0x17c)polish=true;}
        assert(measured==BursztynUI::CzcionkaUI::zmierz_tekst(t));
        if(t==tekst(2))assert(polish);
    }
    for(int w:{1280,1920})for(unsigned dpi:{100U,125U}){
        width=w;height=w==1280?720:1080;const int pitch=w+8;
        std::vector<uint32_t> scene(size_t(pitch)*height,0xABCDEF);
        now=0;powitanie_inicjalizuj(true);
        auto source=[&](int x=0,int y=0,int rw=-1,int rh=-1){return reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),w,height,pitch*4,x,y,rw<0?w:rw,rh<0?height:rh,dpi));};
        auto* out=source();assert(out!=scene.data());assert(out[0]==TLO);
        assert(scene[0]==0xABCDEF); // normal app/window scene remains private
        unsigned allocated=allocations;
        for(auto t:{1400000ULL,1533333ULL,1633333ULL,1800000ULL,3233333ULL,3500000ULL}){
            advance(t);out=source();assert(out[0]==TLO);assert(allocations==allocated);
        }
        const auto scale=SkalaUI::procent(skala_tekstu(WYSOKOSC_KOMUNIKATU,dpi));
        for(unsigned msg=0;msg<3;++msg){
            const int tw=BursztynUI::CzcionkaUI::zmierz_tekst(tekst(msg),scale);
            assert(((w-tw)/2)*2+tw>=w-1&&((w-tw)/2)*2+tw<=w);
            std::vector<uint32_t> expected(scene.size(),0xBADBAD);
            Sekwencja draw;draw.rozpocznij(true,0);draw.komunikat=msg;
            rysuj(expected.data(),w,height,pitch,dpi,draw,true);
            int minx=w,maxx=-1;const int yy=geometria(w,height,dpi,msg).komunikat.y;
            for(int y=yy;y<yy+16*scale;++y)for(int x=0;x<w;++x)
                if(expected[y*pitch+x]!=TLO){if(x<minx)minx=x;if(x>maxx)maxx=x;}
            assert(minx>=(w-tw)/2&&maxx<(w-tw)/2+tw);
            assert(maxx>=minx);
        }
        advance(6400000);assert(powitanie_aktywne()); // delayed desktop / wallpaper failure
        now=6500000;powitanie_pulpit_gotowy();advance(6500000);out=source();assert(out[0]==TLO);
        advance(6800000);out=source();assert(out[0]==mieszaj(TLO,scene[0],127));
        // Partial repeated presentations must blend original RGB, never accumulate.
        auto first=out[10*pitch+10];out=source(10,10,2,2);assert(out[10*pitch+10]==first);
        advance(7100000);
        assert(powitanie_aktywne()); // final scanout waits for a whole composed scene
        assert(source(0,0,1,1)!=scene.data());
        out=source();assert(out==scene.data());assert(!powitanie_aktywne());
        assert(!pulpit_gotowy&&pulpit_pid==0&&!tlo_tapeta&&sekwencja.start==0&&live==0);
        for(int i=0;i<10;++i){advance(7600000+i*40000);assert(source()==scene.data());}
        // Explicit diagnostic and automated boot have no delay.
        powitanie_inicjalizuj(false);assert(!powitanie_aktywne());assert(source()==scene.data());
    }
    // Geometry, actual grayscale pixels and affected-band restoration.
    for(int w:{1280,1920})for(unsigned dpi:{100U,125U})for(unsigned msg=0;msg<3;++msg) {
        int h=w==1280?720:1080;auto g=geometria(w,h,dpi,msg);
        assert(g.tytul.h==ui_skaluj(56,dpi));assert(g.komunikat.h==ui_skaluj(36,dpi));
        assert(g.tytul.h>g.komunikat.h&&g.komunikat.h>=28);
        assert(g.komunikat.y-g.tytul.y-g.tytul.h==ui_skaluj(24,dpi));
        assert(g.tytul.x>0&&g.komunikat.x>0&&g.dirty.y>=0&&g.dirty.y+g.dirty.h<h);
        assert(g.tytul.x*2+g.tytul.w>=w-1&&g.tytul.x*2+g.tytul.w<=w);
        assert(g.komunikat.x*2+g.komunikat.w>=w-1&&g.komunikat.x*2+g.komunikat.w<=w);
        std::vector<uint32_t> image(size_t(w)*h,0x123456);
        Sekwencja draw;draw.rozpocznij(true,0);draw.komunikat=msg;
        rysuj(image.data(),w,h,w,dpi,draw,false);
        unsigned smooth=0;const auto affected=centrum(w,h,dpi);bool outside=true;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const auto c=image[size_t(y)*w+x];
            if(y<affected.y||y>=affected.y+affected.h||x<affected.x||x>=affected.x+affected.w)outside&=c==0x123456;
            else if(c!=TLO&&c!=0xE9BE76&&c!=0xE1E6ED)++smooth;
        }
        assert(smooth>0&&outside);
        rysuj(image.data(),w,h,w,dpi,draw,true);
        auto ink_height=[&](Obszar r){int lo=h,hi=-1;
            for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)
                if(image[size_t(y)*w+x]!=TLO){if(y<lo)lo=y;if(y>hi)hi=y;}
            return hi-lo+1;
        };
        int title_ink=ink_height(g.tytul),message_ink=ink_height(g.komunikat);
        assert(title_ink>=ui_skaluj(40,dpi)&&title_ink<=ui_skaluj(52,dpi));
        assert(message_ink>=ui_skaluj(24,dpi)&&message_ink<=ui_skaluj(32,dpi));
        assert(title_ink>message_ink);
        auto old_title=image[size_t(g.tytul.y+12)*w+g.tytul.x+8];
        draw.przenikanie=127;draw.nastepny_komunikat=(msg+1)%3;rysuj(image.data(),w,h,w,dpi,draw,false);
        assert(image[size_t(g.tytul.y+12)*w+g.tytul.x+8]==old_title);
        // Extreme clip additions must not overflow under UBSan.
        rysuj(image.data(),w,h,w,dpi,draw,false,{INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX});
        printf("geometry %dx%d %u%% message %u: heading %d,%d %dx%d; message %d,%d %dx%d\n",
               w,h,dpi,msg,g.tytul.x,g.tytul.y,g.tytul.w,g.tytul.h,g.komunikat.x,g.komunikat.y,g.komunikat.w,g.komunikat.h);
    }
    // Reconfigure framebuffer while active; no old geometry or app pixels escape.
    now=0;powitanie_inicjalizuj(true);
    std::vector<uint32_t> small(1280*720,0x123456),large(1920*1080,0x654321);
    auto* a=powitanie_zrodlo(reinterpret_cast<uint8_t*>(small.data()),1280,720,5120,0,0,1280,720,100);assert(a);
    auto* b=reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1920,1080,125));assert(b[1920*1080-1]==TLO);
    assert(large.back()==0x654321);
    // Modeset invalidates desktop readiness until WM republishes its root.
    powitanie_pulpit_gotowy(false,2);root_layer={};root_visible=false;
    advance(6400000);assert(sekwencja.stan==Stan::Widoczny);
    root_layer.x=0;root_layer.y=0;root_layer.szerokosc=width;root_layer.wysokosc=height;root_visible=true;
    advance(6500000);assert(sekwencja.stan==Stan::Zanikanie);
    advance(6600000);
    auto* blended=reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1920,1080,125));
    assert(blended[0]!=TLO);
    root_visible=false;advance(6800000);assert(sekwencja.stan==Stan::Widoczny);
    auto* hidden=reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1,1,125));
    assert(hidden[0]==TLO&&hidden[1920*1080-1]==TLO);
    root_visible=true;advance(6900000);assert(sekwencja.stan==Stan::Zanikanie);
    advance(7500000);assert(zakonczenie_oczekuje);
    powitanie_zrodlo(reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1920,1080,125);
    assert(!powitanie_aktywne());
    // Missing optional surface/clock cannot hang boot or resurrect terminal.
    powitanie_inicjalizuj(false);fail_alloc=true;now=0;powitanie_inicjalizuj(true);
    assert(powitanie_zrodlo(reinterpret_cast<uint8_t*>(small.data()),1280,720,5120,0,0,1280,720,100)==reinterpret_cast<uint8_t*>(small.data()));assert(!powitanie_aktywne());
    fail_alloc=false;clock_ok=false;powitanie_inicjalizuj(true);assert(!powitanie_aktywne());clock_ok=true;
    assert(opcja_startowa_obecna("bursztyn.splash=0","bursztyn.splash=0"));
    assert(opcja_startowa_obecna("a\t bursztyn.splash=0 b","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna("bursztyn.splash=01","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna("xbursztyn.splash=0","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna("bursztyn.splash=0x","bursztyn.splash=0"));
    assert(opcja_startowa_obecna("bursztyn.test=automated bursztyn.splash=1","bursztyn.test=automated"));
    assert(!opcja_startowa_obecna("  ","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna(nullptr,"x"));assert(!opcja_startowa_obecna("x",""));
    // At least 100 simulated graphical boots per interruption state. The clock
    // deliberately goes backwards between generations; no inherited timestamp.
    const uint64_t stages[]={0,2200000,4400000,6500000,6800000,7100000};
    std::vector<uint32_t> cycle_scene(320*240,0x102030);
    for(unsigned cycle=0;cycle<100;++cycle)for(auto stage:stages)for(bool ready:{false,true}) {
        width=320;height=240;now=123;powitanie_inicjalizuj(true);
        assert(sekwencja.start==123&&sekwencja.fade==0&&sekwencja.komunikat==0);
        assert(!pulpit_gotowy&&!tlo_tapeta&&pulpit_pid==0&&nastepny==0);
        auto source=[&](){return powitanie_zrodlo(reinterpret_cast<uint8_t*>(cycle_scene.data()),320,240,1280,0,0,320,240,100);};
        source();assert(live==1);
        if(ready)powitanie_pulpit_gotowy(false); // async wallpaper pending / valid fallback
        if(stage>=6500000)advance(6400123);
        advance(123+stage);source();
        if(cycle%10==0){powitanie_reset_ekranu();assert(live==0);source();}
        powitanie_wycisz();assert(live==0&&!powitanie_aktywne());
        assert(sekwencja.stan==Stan::Zniszczony&&sekwencja.start==0&&sekwencja.fade==0&&sekwencja.ostatni==0);
        assert(!powierzchnia&&bajty==0&&szer==0&&wys==0&&stride==0&&procent==0);
        unsigned old_full=full,old_partial=partial,old_alloc=allocations;
        powitanie_pulpit_gotowy(true,2);advance(99999999);assert(source()==reinterpret_cast<uint8_t*>(cycle_scene.data()));
        assert(full==old_full&&partial==old_partial&&allocations==old_alloc&&!pulpit_gotowy);
        powitanie_wycisz();assert(live==0); // idempotent; no double-free
    }
    for(unsigned cycle=0;cycle<100;++cycle){
        for(unsigned mode=0;mode<5;++mode){
            now=0;fail_alloc=mode==3;clock_ok=mode!=4;
            powitanie_inicjalizuj(mode>=3); // disabled, automated, installer, allocation, clock
            powitanie_zrodlo(reinterpret_cast<uint8_t*>(cycle_scene.data()),320,240,1280,0,0,320,240,100);
            powitanie_wycisz();assert(live==0&&!powitanie_aktywne());
        }
    }
    fail_alloc=false;clock_ok=true;
    printf("Simulated boots: 1200 interrupted + 500 bypass/failure; live surfaces: %u\n",live);
    printf("Welcome lifecycle/rendering/options: %u/%u PASS (mock clock, ASan/UBSan)\n",checks,checks);
    return 0;
}
