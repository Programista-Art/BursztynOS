// Prawdziwy cykl zycia ekranu powitalnego z deterministycznym rejestratorem
// kompozytora/GPU. Sprawdza pierwsza klatke, kolejnosc i wycentrowanie.
#define main existing_welcome_main
#include "test_powitanie.cpp"
#undef main
struct Published {uint64_t generation,frame;int w,h;unsigned krok;std::vector<uint32_t> pixels;};
static std::vector<Published> published;
static bool atrament(const std::vector<uint32_t>& p,int w,int h,Powitanie::Obszar r) {
    for(int y=r.y<0?0:r.y;y<h&&y<r.y+r.h;++y)
        for(int x=r.x<0?0:r.x;x<w&&x<r.x+r.w;++x)
            if(p[size_t(y)*w+x]!=Powitanie::TLO)return true;
    return false;
}
static void compositor_tick() {
    // Fałszywy kompozytor zachowuje swoj poprzedni bufor do publikacji.
    if(!powitanie_pierwsza_klatka_gotowa())return;
    if(!powitanie_pierwsza_publikacja())return;
    published.push_back({generacja,SplashFirstDiag::id().frame,width,height,
        unsigned(sekwencja.krok),
        std::vector<uint32_t>(reinterpret_cast<uint32_t*>(powierzchnia),
            reinterpret_cast<uint32_t*>(powierzchnia)+size_t(width)*height)});
    powitanie_publikacja();
}
static void render(unsigned dpi=100) {
    std::vector<uint32_t> scene(size_t(width)*height,0xABCDEF);
    powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),width,height,width*4,
        0,0,width,height,dpi);
}
static void validate(const Published& p,unsigned dpi,uint64_t czas) {
    const auto g=Powitanie::geometria(p.w,p.h,dpi);
    // Tlo jest czarne w calym oknie, takze na przecieciach.
    assert(p.pixels[0]==Powitanie::TLO&&p.pixels.back()==Powitanie::TLO);
    // Logotyp ma atrament od pierwszej klatki (alfa startowa > 0).
    assert(atrament(p.pixels,p.w,p.h,g.logo));
    // Komunikaty sa niewidoczne na starcie animacji.
    if(czas< Powitanie::LINIA1_IN_US) {
        for(int y=g.linia1.y;y<p.h;++y)
            for(int x=0;x<p.w;++x)
                assert(p.pixels[size_t(y)*p.w+x]==Powitanie::TLO);
    }
    // Klatka jest kompletna i zgodna z rendererem dla tego samego stanu.
    std::vector<uint32_t> expected(p.pixels.size(),0);
    Powitanie::Sekwencja s;s.rozpocznij(true,0);s.przelicz(czas);
    Powitanie::rysuj(expected.data(),p.w,p.h,p.w,dpi,s,true);
    assert(expected==p.pixels);
}
int main() {
    for(uint64_t delay:{0ULL,500000ULL,1500000ULL,3000000ULL}) {
        width=1280;height=720;now=0;published.clear();logged_strings.clear();
        powitanie_inicjalizuj(true);
        assert(powitanie_aktywne()&&!powitanie_pierwsza_klatka_gotowa());
        compositor_tick();assert(published.empty());
        now=delay;compositor_tick();assert(published.empty());
        render();assert(powitanie_pierwsza_klatka_gotowa());assert(published.empty());
        compositor_tick();assert(published.size()==1);
        assert(published[0].krok==0&&sekwencja.start==delay);
        validate(published[0],100,0);
        compositor_tick();assert(published.size()==1); // dokladnie jedna publikacja
        auto logged=[&](const char* event){for(size_t i=0;i<logged_strings.size();++i)
            if(logged_strings[i].find(event)!=std::string::npos)return i;return logged_strings.size();};
        assert(logged("splash_created")<logged("render_begin"));
        assert(logged("render_begin")<logged("render_end"));
        assert(logged("render_end")<logged("first_frame_ready"));
        assert(logged("first_frame_ready")<logged("first_visible_commit"));
        assert(SplashFirstDiag::id().title&&SplashFirstDiag::id().message);
        powitanie_wycisz();assert(!powitanie_pierwsza_klatka_gotowa()&&live==0);
    }
    // Pierwsza klatka kompozytora opozniona po kompletnym renderze czasu zero.
    width=1280;height=720;now=0;published.clear();powitanie_inicjalizuj(true);render();
    now=500000;compositor_tick();assert(published.size()==1);validate(published[0],100,0);
    powitanie_wycisz();
    // Uniewaznienie przy zmianie rozdzielczosci, potem przy zmianie DPI.
    now=0;powitanie_inicjalizuj(true);render();
    const auto old_frame=SplashFirstDiag::id().frame;
    powitanie_reset_ekranu();width=1920;height=1080;published.clear();
    assert(!powitanie_pierwsza_klatka_gotowa());compositor_tick();assert(published.empty());
    render();assert(SplashFirstDiag::id().frame>old_frame);
    // Wykrycie zmiany DPI wymaga przebudowy przed publikacja.
    render(125);compositor_tick();assert(published.size()==1);validate(published[0],125,0);
    const auto old_gen=published[0].generation;
    powitanie_wycisz();now=0;powitanie_inicjalizuj(true);published.clear();
    assert(generacja>old_gen&&!powitanie_pierwsza_klatka_gotowa());
    compositor_tick();assert(published.empty());render(125);compositor_tick();
    assert(published.size()==1&&published[0].generation!=old_gen);
    validate(published[0],125,0);
    powitanie_wycisz();assert(live==0);
    printf("First splash frame ordering / pixels / delayed render / resolution / DPI / generations: %u/%u PASS\n",
        checks,checks);
    return 0;
}
