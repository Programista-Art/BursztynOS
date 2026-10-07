// Real splash lifecycle/Unicode renderer with deterministic compositor/GPU recorder.
#define main existing_welcome_main
#include "test_powitanie.cpp"
#undef main
struct Published {uint64_t generation,frame;int w,h;unsigned message;std::vector<uint32_t> pixels;};
static std::vector<Published> published;
static bool ink(const std::vector<uint32_t>& p,int w,int h,Powitanie::Obszar r) {
    for(int y=r.y<0?0:r.y;y<h&&y<r.y+r.h;++y)
        for(int x=r.x<0?0:r.x;x<w&&x<r.x+r.w;++x)
            if(p[size_t(y)*w+x]!=Powitanie::TLO)return true;
    return false;
}
static void compositor_tick() {
    // This fake must retain its valid old framebuffer while splash is pending.
    if(!powitanie_pierwsza_klatka_gotowa())return;
    if(!powitanie_pierwsza_publikacja())return;
    published.push_back({generacja,SplashFirstDiag::id().frame,width,height,sekwencja.komunikat,
        std::vector<uint32_t>(reinterpret_cast<uint32_t*>(powierzchnia),reinterpret_cast<uint32_t*>(powierzchnia)+size_t(width)*height)});
    powitanie_publikacja();
}
static void render(unsigned dpi=100) {
    std::vector<uint32_t> scene(size_t(width)*height,0xABCDEF);
    powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),width,height,width*4,0,0,width,height,dpi);
}
static void validate(const Published& p,unsigned dpi) {
    auto g=Powitanie::geometria(p.w,p.h,dpi,p.message);
    assert(p.pixels[0]==Powitanie::TLO&&p.pixels.back()==Powitanie::TLO);
    assert(ink(p.pixels,p.w,p.h,g.tytul));
    assert(ink(p.pixels,p.w,p.h,Powitanie::centrum(p.w,p.h,dpi)));
    // Pixel-for-pixel equality with the correct absolute-time complete frame.
    std::vector<uint32_t> expected(p.pixels.size(),0);
    Powitanie::rysuj(expected.data(),p.w,p.h,p.w,dpi,sekwencja,true);
    assert(expected==p.pixels);
}
int main() {
    for(auto delay:{0ULL,500000ULL,1500000ULL,3000000ULL}) {
        width=1280;height=720;now=0;published.clear();logged_strings.clear();
        powitanie_inicjalizuj(true);
        assert(powitanie_aktywne()&&!powitanie_pierwsza_klatka_gotowa());
        compositor_tick();assert(published.empty());
        now=delay;compositor_tick();assert(published.empty());
        render();assert(powitanie_pierwsza_klatka_gotowa());assert(published.empty());
        compositor_tick();assert(published.size()==1);
        assert(published[0].message==0&&sekwencja.start==delay);
        validate(published[0],100);
        compositor_tick();assert(published.size()==1);
        auto logged=[&](const char* event){for(size_t i=0;i<logged_strings.size();++i)if(logged_strings[i].find(event)!=std::string::npos)return i;return logged_strings.size();};
        assert(logged("splash_created")<logged("render_begin"));
        assert(logged("render_begin")<logged("render_end"));
        assert(logged("render_end")<logged("first_frame_ready"));
        assert(logged("first_frame_ready")<logged("first_visible_commit"));
        assert(SplashFirstDiag::id().title&&SplashFirstDiag::id().message);
        powitanie_wycisz();assert(!powitanie_pierwsza_klatka_gotowa()&&live==0);
    }
    // First compositor tick delayed after a complete off-screen time-zero render.
    width=1280;height=720;now=0;published.clear();powitanie_inicjalizuj(true);render();
    now=500000;compositor_tick();assert(published.size()==1);validate(published[0],100);
    powitanie_wycisz();
    // Resolution invalidation before publication, then DPI invalidation.
    now=0;powitanie_inicjalizuj(true);render();
    auto old_frame=SplashFirstDiag::id().frame;
    powitanie_reset_ekranu();width=1920;height=1080;published.clear();
    assert(!powitanie_pierwsza_klatka_gotowa());compositor_tick();assert(published.empty());
    render();assert(SplashFirstDiag::id().frame>old_frame);
    // A caller detecting a DPI change must rebuild before publication.
    render(125);compositor_tick();assert(published.size()==1);validate(published[0],125);
    auto old_gen=published[0].generation;
    powitanie_wycisz();now=0;powitanie_inicjalizuj(true);published.clear();
    assert(generacja>old_gen&&!powitanie_pierwsza_klatka_gotowa());
    compositor_tick();assert(published.empty());render(125);compositor_tick();
    assert(published.size()==1&&published[0].generation!=old_gen);validate(published[0],125);
    powitanie_wycisz();assert(live==0);
    printf("First splash frame ordering / pixels / delayed render / resolution / DPI / generations: %u/%u PASS\n",checks,checks);
}
