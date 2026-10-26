#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <vector>
#define BURSZTYN_COMPOSITOR_HOST_TEST 1
#include "../skladacz_obrazu.cpp"
static unsigned compositor_checks=0;
#undef assert
#define assert(c) do {++compositor_checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
static BursztynMotyw compositor_theme=g_domyslny_motyw_fallback;
static int sw=1280,sh=720,lx=0,ly=0;
static uint64_t generation=1;
static uint32_t scale=100;
static std::vector<uint32_t> scene(1920*1080,0x123456);
static GuiDirtyRect overlay{};
static unsigned presents=0;
static bool mock_first_splash=false;
static uint32_t recorded_count=0;
static GuiDirtyRect recorded_first{};
static bool use_clock=false;
static uint64_t mock_now=1000000000;
int aktualny_pid=0;
int grafika_pobierz_szerokosc(){return sw;}
int grafika_pobierz_wysokosc(){return sh;}
uint32_t* grafika_pobierz_wiersz_backbuffer(int y){return scene.data()+y*sw;}
uint32_t ekran_skala(){return scale;}
bool hpet_dostepny(){return use_clock;}
uint64_t czas_monotoniczny_ns(){return mock_now;}
uint64_t czas_monotoniczny_us(){return mock_now/1000;}
void* bufor_grafiki_alokuj(size_t n){return malloc(n);}
void bufor_grafiki_zwolnij(void* p){free(p);}
void kfree(void* p){free(p);}
void wypisz_log(const char*){}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*){return true;}
void grafika_pobierz_pozycje_kursora(int* x,int* y){*x=lx;*y=ly;}
void grafika_pobierz_overlay_kursora(int* x,int* y,int* w,int* h,bool* visible){*x=overlay.x;*y=overlay.y;*w=overlay.width;*h=overlay.height;*visible=overlay.width>0;}
void grafika_rozpocznij_skladanie(){}
void grafika_zakoncz_scene(){}
void grafika_zakoncz_skladanie(){++presents;}
void grafika_odtworz_tlo_skladania(){for(int i=0;i<sw*sh;++i)scene[i]=0x123456;}
void grafika_odtworz_tlo_regionu(int x,int y,int w,int h){for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)scene[yy*sw+xx]=0x123456;}
void grafika_zapisz_surowy_piksel(int x,int y,uint32_t c){scene[y*sw+x]=c;}
void grafika_naloz_okno_terminala_region(int,int,int,int){}
const BursztynMotyw* motyw_pobierz_aktywny(){return &compositor_theme;}
void grafika_prezentuj_region(int,int,int,int){++presents;}
void grafika_prezentuj_scene_z_kursorem(const GuiDirtyRect* r,uint32_t count,int x,int y){
    recorded_count=count;if(count)recorded_first=r[0];
    uint32_t scratch[64*64]{};
    overlay=kursor_prezentuj_transakcje(scene.data(),sw,sw,sh,overlay,x,y,16,16,scratch,r,count,
        [](int,int,uint32_t& c){c=0xffffff;return true;},
        [](GuiDirtyRect rect){assert(rect.x>=0&&rect.y>=0&&rect.x+rect.width<=sw&&rect.y+rect.height<=sh);++presents;},[](int){});
}
void grafika_prezentuj_kursor_w(int x,int y){grafika_prezentuj_scene_z_kursorem(nullptr,0,x,y);}
static unsigned popup_checks=0;
static void popup_restoration_cases() {
    use_clock=false;skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
    int x=200,y=220,w=ui_skaluj(80,scale),h=ui_skaluj(60,scale),r=ui_skaluj(8,scale);
    std::vector<uint32_t> baseline(w*h),pixels(w*h,0);
    for(int py=0;py<h;++py)for(int px=0;px<w;++px)baseline[py*w+px]=scene[(y+py)*sw+x+px];
    for(int py=0;py<h;++py) {
        int edge=py<h-1-py?py:h-1-py;
        int inset=edge<r?motyw_promien_inset(r,edge):0;
        for(int px=inset;px<w-inset;++px)pixels[py*w+px]=0xff335577U;
    }
    assert(skladacz_obrazu_ustaw_popup_aplikacji(1,true,x,y,w,h));++popup_checks;
    assert(skladacz_obrazu_blit_popup(1,x,y,w,h,pixels.data()));++popup_checks;
    for(int py=0;py<h;++py)for(int px=0;px<w;++px) {
        uint32_t expected=pixels[py*w+px]?0x335577U:baseline[py*w+px];
        assert(scene[(y+py)*sw+x+px]==expected);++popup_checks;
    }
    // Replacing the surface with transparent pixels must erase old popup pixels.
    std::fill(pixels.begin(),pixels.end(),0);
    assert(skladacz_obrazu_blit_popup(1,x,y,w,h,pixels.data()));++popup_checks;
    for(int py=0;py<h;++py)for(int px=0;px<w;++px) {
        assert(scene[(y+py)*sw+x+px]==baseline[py*w+px]);++popup_checks;
    }
    assert(skladacz_obrazu_ustaw_popup_aplikacji(1,false,0,0,0,0));++popup_checks;
    for(int py=0;py<h;++py)for(int px=0;px<w;++px) {
        assert(scene[(y+py)*sw+x+px]==baseline[py*w+px]);++popup_checks;
    }
}
int main(){
    for(auto dpi:{100U,125U}){
        scale=dpi;sw=1280;sh=720;overlay={};
        assert(utworz_warstwe(1,0,0,sw,sh,0)==1);
        auto* desktop=pobierz_warstwe(1);
        assert(skladacz_obrazu_publikuj_klatke(1));
        for(uint32_t pixel:{0x20000000U,0x70ffffffU,0x01000000U,0x00abcdefU}) {
            desktop->bufor_pikseli[0]=pixel;scene[0]=0x123456;
            zloz_snapshot_region({desktop->bufor_pikseli,0,0,sw,sh,0,1,uint64_t(sw)*sh},{0,0,1,1});
            uint32_t expected=motyw_pulpit_skladaj(0x123456,pixel,true,motyw_pobierz_aktywny());assert(scene[0]==expected);
            scene[0]=0x123456;zloz_warstwe(1,sw,sh);assert(scene[0]==expected);
            scene[0]=0x123456;
            zloz_snapshot_region({desktop->bufor_pikseli,0,0,sw,sh,10,1,uint64_t(sw)*sh},{0,0,1,1});
            assert(scene[0]==(pixel&0xffffff)); // alpha tags are desktop-only
        }
        desktop->bufor_pikseli[0]=0;
        popup_restoration_cases();
        lx=1279;ly=719;skladacz_obrazu_oznacz_ruch_kursora(0,0,lx,ly);skladacz_obrazu_obsluz_dirty();
        sw=1920;sh=1080;++generation;skladacz_reset_ekranu();
        assert(dirty_count==1&&dirty_rects[0].width==1920&&dirty_rects[0].height==1080);
        lx=0;ly=0;skladacz_obrazu_oznacz_ruch_kursora(1279,719,lx,ly);presents=0;skladacz_obrazu_obsluz_dirty();assert(presents==1);
        lx=1919;ly=1079;skladacz_obrazu_oznacz_ruch_kursora(0,0,lx,ly);presents=0;skladacz_obrazu_obsluz_dirty();assert(presents==1);
        popup_restoration_cases();
        const auto gen=generation;const int oldx=lx,oldy=ly;
        assert(utworz_warstwe(2,300,200,700,450,10)==2); // Shell-equivalent surface
        assert(skladacz_obrazu_ustaw_tytul(2,"Powłoka Bursztynowa"));
        assert(sw==1920&&sh==1080&&lx==oldx&&ly==oldy&&generation==gen);
        for(int i=0;i<100;++i)skladacz_obrazu_oznacz_ruch_kursora(lx,ly,100+i,200+i);
        assert(cursor_pending&&cursor_new_x==199&&cursor_new_y==299);
        lx=199;ly=299;skladacz_obrazu_obsluz_dirty();
        assert(!cursor_pending&&overlay.x==199&&overlay.y==299);
        skladacz_obrazu_odswiez_dekoracje();skladacz_obrazu_oznacz_ruch_kursora(lx,ly,0,0);
        lx=0;ly=0;skladacz_obrazu_obsluz_dirty();assert(overlay.x==0&&overlay.y==0);
        skladacz_obrazu_oznacz_dirty();skladacz_obrazu_oznacz_ruch_kursora(0,0,1919,1079);
        lx=1919;ly=1079;presents=0;skladacz_obrazu_obsluz_dirty();assert(presents==1);
        use_clock=true;mock_now+=1000000000;
        skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
        mock_now+=1000000;skladacz_obrazu_oznacz_dirty();
        lx=100;ly=100;skladacz_obrazu_oznacz_ruch_kursora(1919,1079,lx,ly);
        presents=0;skladacz_obrazu_obsluz_dirty();
        assert(presents==1&&!cursor_pending&&dirty_count==1); // cursor bypasses scene pacing
        mock_now+=17000000;presents=0;skladacz_obrazu_obsluz_dirty();
        assert(presents==1&&dirty_count==0); // full scene reuses final cursor transaction
        use_clock=false;
        usun_warstwe(2);usun_warstwe(1);skladacz_reset_ekranu();
    }
    // Several initial element dirty regions produce a single full splash commit.
    skladacz_reset_ekranu();dirty_count=0;overlay={};mock_first_splash=true;
    PrezentacjaStartu::stan().pelna=true;
    skladacz_obrazu_oznacz_dirty_rect(0,0,10,10);
    skladacz_obrazu_oznacz_dirty_rect(500,300,20,20);
    skladacz_obrazu_oznacz_dirty_rect(800,500,20,20);
    presents=0;skladacz_obrazu_obsluz_dirty();
    assert(presents==1&&recorded_count==1);
    assert(recorded_first.x==0&&recorded_first.y==0&&recorded_first.width==sw&&recorded_first.height==sh);
    mock_first_splash=false;PrezentacjaStartu::stan().pelna=false;
    // Repeated power quiesce drops bounded pending work and prevents new work.
    for(unsigned cycle=0;cycle<100;++cycle) {
        skladacz_obrazu_wznow();
        skladacz_obrazu_oznacz_dirty();skladacz_obrazu_oznacz_ruch_kursora(0,0,10,10);
        assert(dirty_count>0&&cursor_pending);
        skladacz_obrazu_wycisz();
        assert(dirty_count==0&&!cursor_pending&&nastepna_klatka_ns==0);
        const auto before=presents;
        skladacz_obrazu_oznacz_dirty();skladacz_obrazu_oznacz_ruch_kursora(0,0,20,20);
        skladacz_obrazu_obsluz_dirty();skladacz_obrazu_zloz_klatke();
        assert(dirty_count==0&&!cursor_pending&&presents==before);
        skladacz_obrazu_wznow();skladacz_reset_ekranu();assert(dirty_count==1);
    }
    skladacz_obrazu_wznow();
    printf("Compositor lifecycle/scene: %u/%u PASS\n",compositor_checks,compositor_checks);
    printf("Popup compositor background restoration: %u/%u PASS\n",popup_checks,popup_checks);
    puts("Production compositor host PASS: modeset dirty geometry, Shell-equivalent layer preserves cursor/screen/generation, coalescing and theme/decor/full scene transactions");
    return 0;
}

void powitanie_reset_ekranu() {}
bool powitanie_pierwsza_publikacja() {return mock_first_splash;}
void powitanie_tick() {} // Dedicated presentation tests exercise the real runtime.
