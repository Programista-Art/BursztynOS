// Execute the real kernel terminal overlay and BWS frame commit. Hardware is mocked.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define __dso_handle boot_test_dso_handle
#define __cxa_atexit boot_test_cxa_atexit
#include "../grafika.cpp"
#include "../ekran_powitalny.cpp"
#undef __dso_handle
#undef __cxa_atexit
static unsigned checks=0,commits=0,invalidations=0,presents=0,shown=0;
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
static BursztynMotyw test_theme=g_domyslny_motyw_fallback;
static warstwa_obrazu layer{};
static bool presentable=false;
int aktualny_pid=0;
warstwa_obrazu* pobierz_warstwe(int pid) {return layer.aktywna&&layer.pid==pid?&layer:nullptr;}
bool skladacz_obrazu_czy_widoczna(int pid) {return presentable&&pobierz_warstwe(pid);}
bool skladacz_obrazu_publikuj_klatke(int pid) {assert(pobierz_warstwe(pid));if(presentable)return false;++commits;presentable=true;return true;}
void skladacz_obrazu_oznacz_dirty_rect(int,int,int,int) {++invalidations;}
void skladacz_obrazu_oznacz_dirty() {++invalidations;}
void skladacz_obrazu_wznow() {}
void skladacz_obrazu_podnies_warstwe(int) {}
bool skladacz_obrazu_ustaw_popup_aplikacji(int,bool,int,int,int,int) {return true;}
const BursztynMotyw* motyw_pobierz_aktywny() {return &test_theme;}
uint32_t ekran_skala() {return 100;}
static bool test_clock=false;
static uint64_t test_now=0;
bool hpet_dostepny() {return test_clock;}
uint64_t czas_monotoniczny_ns() {return 0;}
uint64_t czas_monotoniczny_us() {return test_now;}
void* bufor_grafiki_alokuj(size_t n){return malloc(n);}
void bufor_grafiki_zwolnij(void* p){free(p);}
uint64_t scheduler_pobierz_tick() {return 0;}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie* e) {if(e->typ==BWS_ZDARZENIE_OKNO_POKAZANE)++shown;return true;}
void pobierz_czas_rtc(czas_rtc* t) {*t={};}
void formatuj_czas_do_stringa(const czas_rtc*,char* out,size_t) {strcpy(out,"00:00:00");}
class HostDisplay final:public SterownikEkranu {
public:
    HostDisplay(){szerokosc=1280;wysokosc=720;pitch=szerokosc*4;bpp=32;}
    uint32_t* scanout=static_cast<uint32_t*>(calloc(1280*720,4));
    ~HostDisplay(){free(scanout);}
    bool Inicjalizuj(uint64_t,uint32_t,uint32_t,uint32_t,uint8_t) override {return true;}
    void KopiujNaEkran(uint8_t* p) override {++presents;memcpy(scanout,p,1280*720*4);}
    void KopiujFragmentNaEkran(uint8_t* p,int x,int y,int w,int h) override {
        ++presents;auto* src=reinterpret_cast<uint32_t*>(p);
        for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)scanout[yy*1280+xx]=src[yy*1280+xx];
    }
};
int main() {
    HostDisplay display;aktywny_ekran=&display;
    const size_t bytes=1280*720*4;
    backbuffer=static_cast<uint8_t*>(malloc(bytes));assert(backbuffer);
    memset(backbuffer,0x35,bytes);
    assert(!okna[0].widoczne);
    grafika_naloz_okno_terminala_region(0,0,1280,720);
    bool unchanged=true;for(size_t i=0;i<bytes;++i)unchanged &= backbuffer[i]==0x35;
    assert(unchanged);
    // Headless kernel output must not schedule a legacy frame.
    wypisz_na_ekranie("bootstrap diagnostics");
    boot_log_gui("bootstrap console");
    assert(invalidations==0&&presents==0);
    bws_gui_ustaw_przejecie_myszy(false);assert(!okna[0].widoczne);
    pid_przejmujacy_mysz=2;NaprawWlascicielaMyszyJesliProcesZniknal();
    assert(pid_przejmujacy_mysz==-1&&!okna[0].widoczne);
    bws_gui_zwolnij_mysz_procesu(2);assert(!okna[0].widoczne);
    // Clean modern background, then wallpaper; no legacy-colored fallback.
    test_theme.pulpit=0x071827;
    grafika_odtworz_tlo_skladania();
    bool themed=true;for(size_t i=0;i<bytes/4;++i)themed &= reinterpret_cast<uint32_t*>(backbuffer)[i]==test_theme.pulpit;
    assert(themed);
    bufor_tapety=static_cast<uint32_t*>(malloc(bytes));assert(bufor_tapety);
    for(size_t i=0;i<bytes/4;++i)bufor_tapety[i]=0x314253;
    tapeta_zaladowana=true;grafika_odtworz_tlo_regionu(0,0,1280,720);
    bool wallpaper=true;for(size_t i=0;i<bytes/4;++i)wallpaper &= reinterpret_cast<uint32_t*>(backbuffer)[i]==0x314253;
    assert(wallpaper);
    aktualny_pid=2;layer={2,10,100,100,100,100,nullptr,true};
    layer.bufor_pikseli=static_cast<uint32_t*>(calloc(10000,4));assert(layer.bufor_pikseli);
    bws_gui_odswiez();assert(commits==0&&!presentable);
    bws_gui_rysuj_prostokat(100,100,100,100,test_theme.oknotlo);
    assert(!presentable);bws_gui_odswiez();assert(commits==1&&presentable);
    assert(shown==1);
    bws_gui_odswiez();assert(commits==1 && shown==1);
    bws_gui_rysuj_prostokat(100,100,100,100,test_theme.oknotlo);
    bws_gui_odswiez();assert(commits==1 && shown==1);
    // Real production presentation entry points select the dedicated system
    // surface, including cursor-sized presentations. The scene is retained.
    test_clock=true;test_now=0;powitanie_inicjalizuj(true);
    assert(display.scanout[0]==Powitanie::TLO);
    reinterpret_cast<uint32_t*>(backbuffer)[0]=0x008A5A00;
    PrzeniesFragmentNaEkran(0,0,1,1);assert(display.scanout[0]==Powitanie::TLO);
    assert(reinterpret_cast<uint32_t*>(backbuffer)[0]==0x008A5A00);
    bws_zdarzenie input{};input.typ=BWS_ZDARZENIE_MYSZ_DOWN;
    assert(grafika_dostarcz_zdarzenie(2,&input));
    input.typ=BWS_ZDARZENIE_OKNO_POKAZANE;unsigned old_shown=shown;
    assert(grafika_dostarcz_zdarzenie(2,&input));assert(shown==old_shown+1);
    test_now=6400000;powitanie_tick();assert(powitanie_aktywne());
    test_now=6440000;powitanie_pulpit_gotowy(true);powitanie_tick();
    PrzeniesNaEkran();assert(display.scanout[0]==Powitanie::TLO);
    test_now=6740000;powitanie_tick();PrzeniesNaEkran();
    assert(display.scanout[0]==Powitanie::mieszaj(Powitanie::TLO,0x008A5A00,127));
    test_now=7040000;powitanie_tick();PrzeniesNaEkran();
    assert(!powitanie_aktywne());assert(display.scanout[0]==0x008A5A00);
    assert(!okna[0].widoczne);assert(presentable);
    // Power quiesce blocks both presentation entry points. Simulated graphics
    // reinit clears the gate under the same screen lock as production startup.
    unsigned before_quiesce=presents;
    powitanie_wycisz();grafika_wycisz();
    PrzeniesNaEkran();PrzeniesFragmentNaEkran(0,0,1,1);
    assert(presents==before_quiesce&&!powitanie_aktywne());
    grafika_reset_startu();assert(!grafika_wyciszona);
    test_now=0;powitanie_inicjalizuj(true);
    assert(sekwencja.komunikat==0&&sekwencja.start==0);
    powitanie_wycisz();
    powitanie_inicjalizuj(false);PrzeniesNaEkran();assert(display.scanout[0]==0x008A5A00);
    free(layer.bufor_pikseli);layer={};
    free(bufor_tapety);free(backbuffer);backbuffer=nullptr;aktywny_ekran=nullptr;
    printf("Production boot graphics/frame commit: %u/%u PASS\n",checks,checks);
}
