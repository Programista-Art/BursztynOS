// Mock-clock timing and real smoothed renderer; no emulator or real-time waits.
#define main suite_powitania_main
#include "test_powitanie.cpp"
#undef main
static bool center_has_ink(const std::vector<uint32_t>& p,int w,int h,unsigned dpi) {
    const auto r=Powitanie::centrum(w,h,dpi);
    for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)
        if(p[size_t(y)*w+x]!=Powitanie::TLO)return true;
    return false;
}
int main(){
    using namespace Powitanie;
    struct Sample {uint64_t ms;unsigned msg,next,blend,reveal;Stan state;};
    const Sample cases[]={
        {0,0,0,0,0,Stan::Widoczny},{1999,0,0,0,0,Stan::Widoczny},
        {2000,0,1,0,0,Stan::Widoczny},{2100,0,1,63,0,Stan::Widoczny},
        {2200,0,1,127,0,Stan::Widoczny},{2399,0,1,254,0,Stan::Widoczny},
        {2400,1,1,0,0,Stan::Widoczny},{4000,1,1,0,0,Stan::Widoczny},
        {4200,1,2,0,0,Stan::Widoczny},{4400,1,2,127,0,Stan::Widoczny},
        {4600,2,2,0,0,Stan::Widoczny},{6399,2,2,0,0,Stan::Widoczny},
        {6400,2,2,0,0,Stan::Zanikanie},{6700,2,2,0,127,Stan::Zanikanie},
        {6999,2,2,0,254,Stan::Zanikanie},{7000,2,2,0,255,Stan::Zakonczony}
    };
    for(unsigned dpi:{100U,125U}) {
        std::vector<uint32_t> pixels(1280*720);
        for(auto c:cases){Sekwencja s;s.rozpocznij(true,0);s.gotowy=true;s.aktualizuj(c.ms*1000);
            assert(s.komunikat==c.msg&&s.nastepny_komunikat==c.next);
            assert(s.przenikanie==c.blend&&s.odsloniecie==c.reveal&&s.stan==c.state);
            if(s.aktywny()){rysuj(pixels.data(),1280,720,1280,dpi,s,true);assert(center_has_ink(pixels,1280,720,dpi));}
        }
        // Every interpolation weight has actual non-background message pixels.
        // Clear + both glyph passes form one coherent surface, preserving heading.
        for(uint64_t boundary:{PIERWSZE_US,DRUGIE_US})for(unsigned step=0;step<=255;++step){
            Sekwencja s;s.rozpocznij(true,0);s.aktualizuj(boundary+PRZEJSCIE_US*step/255);
            rysuj(pixels.data(),1280,720,1280,dpi,s,true);assert(center_has_ink(pixels,1280,720,dpi));
            auto expected=pixels;
            rysuj(pixels.data(),1280,720,1280,dpi,s,false);assert(pixels==expected);
        }
    }
    // Sampled absolute times land directly on holds; no stale transition queue.
    Sekwencja s;s.rozpocznij(true,0);s.gotowy=true;
    for(auto t:{100000ULL,2500000ULL,5200000ULL,7500000ULL}){
        s.aktualizuj(t);Sekwencja direct;direct.rozpocznij(true,0);direct.gotowy=true;direct.aktualizuj(t);
        assert(s.komunikat==direct.komunikat&&s.przenikanie==direct.przenikanie&&s.stan==direct.stan&&s.odsloniecie==direct.odsloniecie);
    }
    assert(s.stan==Stan::Zakonczony&&s.fade==MINIMUM_US);
    // Real module schedules at most one dirty action per sample after any stall.
    width=1280;height=720;
    std::vector<uint32_t> scene(size_t(width)*height,0x234567);
    auto source=[&](bool whole=true){return reinterpret_cast<uint32_t*>(powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),width,height,width*4,0,0,whole?width:1,whole?height:1,100));};
    for(uint64_t stall:{500000ULL,1500000ULL,3000000ULL}) {
        now=0;powitanie_inicjalizuj(true);source();powitanie_pulpit_gotowy();advance(1900000);
        unsigned before=full+partial;advance(1900000+stall);
        assert(full+partial-before==1);
        Sekwencja expected;expected.rozpocznij(true,0);expected.aktualizuj(now);
        assert(sekwencja.komunikat==expected.komunikat&&sekwencja.przenikanie==expected.przenikanie);
        assert(source()!=scene.data());before=full+partial;powitanie_tick();assert(full+partial==before);
        powitanie_wycisz();assert(live==0);
    }
    // Completion still requires a whole coherent desktop transaction.
    now=0;powitanie_inicjalizuj(true);source();powitanie_pulpit_gotowy();
    for(auto t:{100000ULL,2500000ULL,5200000ULL}){advance(t);source();}
    advance(7500000);assert(zakonczenie_oczekuje&&powitanie_aktywne());
    assert(source(false)!=scene.data());assert(source()==scene.data());assert(live==0&&!powitanie_aktywne());
    // WM published readiness before the stall: validation on the delayed tick
    // must retain its event timestamp rather than start another exit fade.
    now=0;powitanie_inicjalizuj(true);source();
    root_layer={};root_layer.szerokosc=width;root_layer.wysokosc=height;root_visible=true;
    now=1000000;powitanie_pulpit_gotowy(false,2);advance(7500000);
    assert(sekwencja.fade==MINIMUM_US&&zakonczenie_oczekuje);
    assert(source()==scene.data()&&live==0);
    // Desktop arriving late gets a full exit fade from its ready timestamp.
    now=0;powitanie_inicjalizuj(true);source();advance(7500000);assert(sekwencja.stan==Stan::Widoczny);
    powitanie_pulpit_gotowy();advance(7600000);assert(sekwencja.fade==7500000&&sekwencja.odsloniecie==42);
    advance(8100000);assert(source()==scene.data());assert(live==0);
    // Diagnostic bypass remains immediate; host coverage does not claim Windows boot.
    powitanie_inicjalizuj(false);advance(10000000);assert(source()==scene.data());
#if BURSZTYNDIAGSPLASHTIMING
    logged_strings.clear();now=0;powitanie_inicjalizuj(true);source();powitanie_pulpit_gotowy();
    for(unsigned ms=0;ms<=7000;ms+=40){advance(uint64_t(ms)*1000);source();powitanie_diag_prezentacja();}
    assert(!powitanie_aktywne()&&live==0);
    // Sparse logging, including exact start, both transitions, readiness and end.
    assert(logged_strings.size()==9);
    auto logged=[&](const char* text){for(auto& m:logged_strings)if(m.find(text)!=std::string::npos)return true;return false;};
    assert(logged("start now_us=0")&&logged("message_0_to_1")&&logged("message_1_to_2"));
    assert(logged("desktop_ready")&&logged("fade_start now_us=6400000")&&logged("fade_end")&&logged("final_removal"));
    assert(logged("compositor_presents=50"));
#endif
    printf("Splash exact timing / nonempty crossfades / stalls / diagnostics: %u/%u PASS\n",checks,checks);
}
