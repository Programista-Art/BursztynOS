#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <vector>
#define BURSZTYN_COMPOSITOR_HOST_TEST 1
#include "../skladacz_obrazu.cpp"
static int sw=1280,sh=720,lx=0,ly=0;
static uint64_t generation=1;
static uint32_t scale=100;
static std::vector<uint32_t> scene(1920*1080,0x123456);
static GuiDirtyRect overlay{};
static unsigned presents=0;
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
void wypisz_log(const char*){}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*){return true;}
void grafika_pobierz_pozycje_kursora(int* x,int* y){*x=lx;*y=ly;}
void grafika_pobierz_overlay_kursora(int* x,int* y,int* w,int* h,bool* visible){*x=overlay.x;*y=overlay.y;*w=overlay.width;*h=overlay.height;*visible=overlay.width>0;}
void grafika_rozpocznij_skladanie(){}
void grafika_zakoncz_scene(){}
void grafika_odtworz_tlo_regionu(int x,int y,int w,int h){for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)scene[yy*sw+xx]=0x123456;}
void grafika_zapisz_surowy_piksel(int x,int y,uint32_t c){scene[y*sw+x]=c;}
void grafika_naloz_okno_terminala_region(int,int,int,int){}
const BursztynMotyw* motyw_pobierz_aktywny(){return &g_domyslny_motyw_fallback;}
void grafika_prezentuj_region(int,int,int,int){++presents;}
void grafika_prezentuj_scene_z_kursorem(const GuiDirtyRect* r,uint32_t count,int x,int y){
    uint32_t scratch[64*64]{};
    overlay=kursor_prezentuj_transakcje(scene.data(),sw,sw,sh,overlay,x,y,16,16,scratch,r,count,
        [](int,int,uint32_t& c){c=0xffffff;return true;},
        [](GuiDirtyRect rect){assert(rect.x>=0&&rect.y>=0&&rect.x+rect.width<=sw&&rect.y+rect.height<=sh);++presents;},[](int){});
}
void grafika_prezentuj_kursor_w(int x,int y){grafika_prezentuj_scene_z_kursorem(nullptr,0,x,y);}
int main(){
    for(auto dpi:{100U,125U}){
        scale=dpi;sw=1280;sh=720;overlay={};
        assert(utworz_warstwe(1,0,0,sw,sh,0)==1);
        lx=1279;ly=719;skladacz_obrazu_oznacz_ruch_kursora(0,0,lx,ly);skladacz_obrazu_obsluz_dirty();
        sw=1920;sh=1080;++generation;skladacz_reset_ekranu();
        assert(dirty_count==1&&dirty_rects[0].width==1920&&dirty_rects[0].height==1080);
        lx=0;ly=0;skladacz_obrazu_oznacz_ruch_kursora(1279,719,lx,ly);presents=0;skladacz_obrazu_obsluz_dirty();assert(presents==1);
        lx=1919;ly=1079;skladacz_obrazu_oznacz_ruch_kursora(0,0,lx,ly);presents=0;skladacz_obrazu_obsluz_dirty();assert(presents==1);
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
    puts("Production compositor host PASS: modeset dirty geometry, Shell-equivalent layer preserves cursor/screen/generation, coalescing and theme/decor/full scene transactions");
}
