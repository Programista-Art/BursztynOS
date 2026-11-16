// Execute the real kernel terminal overlay and BWS frame commit. Hardware is mocked.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define __dso_handle boot_test_dso_handle
#define __cxa_atexit boot_test_cxa_atexit
#include "../sterowniki/grafika/grafika.cpp"
#include "../jadro/gui/ekran_powitalny.cpp"
// Test nie ma sterownika VirtIO-GPU: grafika.cpp (przygotuj_wlasnosc_sceny) pyta o niego tylko dla backendu VIRTIO.
VirtioGpuDriver* virtio_gpu_get_driver(uint32_t) { return nullptr; }
#undef __dso_handle
#undef __cxa_atexit
static unsigned checks=0,commits=0,invalidations=0,presents=0,shown=0;
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
static BursztynMotyw test_theme=g_domyslny_motyw_fallback;
static warstwa_obrazu layer{};
static bool presentable=false;
int aktualny_pid=0;
// Host nie ma sprzetu SMAP ani okien Ring 3: oba symbole pochodza z vmm.cpp,
// ktorego ten test nie linkuje. Stuby zachowuja semantyke "brak SMAP".
extern "C" bool vmm_smep_aktywny() {return false;}
extern "C" bool vmm_smap_aktywny() {return false;}
extern "C" int& vmm_okno_uzytkownika_glebokosc() {static int glebokosc=0;return glebokosc;}
warstwa_obrazu* pobierz_warstwe(int pid) {return layer.aktywna&&layer.pid==pid?&layer:nullptr;}
bool skladacz_obrazu_czy_widoczna(int pid) {return presentable&&pobierz_warstwe(pid);}
bool skladacz_obrazu_publikuj_klatke(int pid) {assert(pobierz_warstwe(pid));if(presentable)return false;++commits;presentable=true;return true;}
void skladacz_obrazu_oznacz_dirty_rect(int,int,int,int) {++invalidations;}
void skladacz_obrazu_oznacz_dirty_rect_dokladny(int,int,int,int) {++invalidations;}
void skladacz_obrazu_oznacz_dirty() {++invalidations;}
static bool test_zachowana_po_resize=false;
bool skladacz_obrazu_tresc_zachowana_po_zmianie_rozmiaru(int) {return test_zachowana_po_resize;}
void wyczysc_warstwe(int pid) {auto* w=pobierz_warstwe(pid);if(w)memset(w->bufor_pikseli,0,size_t(w->szerokosc)*w->wysokosc*4);++invalidations;}
void skladacz_obrazu_wznow() {}
void skladacz_obrazu_podnies_warstwe(int) {}
bool skladacz_obrazu_ustaw_popup_aplikacji(int,bool,int,int,int,int) {return true;}
const BursztynMotyw* motyw_pobierz_aktywny() {return &test_theme;}
uint32_t ekran_skala() {return 100;}
static bool test_clock=false;
static uint64_t test_now=0;
bool hpet_dostepny() {return test_clock;}
static uint64_t test_ns=0;
uint64_t czas_monotoniczny_ns() {return test_ns;}
uint64_t czas_monotoniczny_us() {return test_now;}
void kfree(void*) {}   // bws_gui_usun_stan_procesu zwalnia tez bufory pomocnicze Ring 0 (tu nie alokowane)
static bool test_alokacja_nie_dziala=false;
void* bufor_grafiki_alokuj(size_t n){return test_alokacja_nie_dziala?nullptr:malloc(n);}
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
    // BWS 19 (gui_odswiez_pulpit) = transakcja rysowania: ostatnia opublikowana klatka jest widoczna (cien) az do gui_odswiez,
    // nawet gdy bufor zywy zostal wyzerowany; po gui_odswiez kompozytor znow czyta bufor na zywo.
    {
        const size_t px=size_t(layer.szerokosc)*layer.wysokosc;
        for(size_t i=0;i<px;++i)layer.bufor_pikseli[i]=0x00ABCDEF;
        assert(!bws_gui_warstwa_w_trakcie_rysowania(2));
        bws_gui_odswiez_pulpit();
        assert(layer.bufor_pikseli[5]==0);                                  // bufor zywy wyzerowany ...
        assert(bws_gui_warstwa_w_trakcie_rysowania(2));                     // ... ale to trwa transakcja
        const uint32_t* cien=bws_gui_cien_warstwy(2,px*4);
        assert(cien&&cien!=layer.bufor_pikseli&&cien[5]==0x00ABCDEF&&cien[px-1]==0x00ABCDEF);   // kompozytor widzi ostatnia klatke
        assert(!bws_gui_cien_warstwy(2,px*4+4));                            // zly rozmiar => brak cienia (nigdy zly bufor)
        bws_gui_odswiez();
        assert(!bws_gui_warstwa_w_trakcie_rysowania(2)&&!bws_gui_cien_warstwy(2,px*4));
        // Po zmianie rozmiaru (stara tresc + kryjace rozszerzenie) bufor NIE jest zerowany
        for(size_t i=0;i<px;++i)layer.bufor_pikseli[i]=0x00FEDCBA;
        test_zachowana_po_resize=true;
        bws_gui_odswiez_pulpit();
        assert(layer.bufor_pikseli[7]==0x00FEDCBA&&bws_gui_warstwa_w_trakcie_rysowania(2));
        test_zachowana_po_resize=false;
        bws_gui_odswiez();
        // zwolnienie warstwy zwalnia cien (bez wyciekow przy ASan)
        bws_gui_odswiez_pulpit();assert(bws_gui_cien_warstwy(2,px*4));
        bws_gui_odswiez();
    }
    // BWS 19: blad alokacji cienia, timeout cienia, zakonczenie procesu, wymiana powierzchni i wielokrotna zmiana rozmiaru.
    {
        auto bytes_of=[&]{return size_t(layer.szerokosc)*layer.wysokosc*4;};
        // (a) brak pamieci na cien: odrysowanie dziala, brak cienia (kompozytor czyta bufor zywy), bez awarii
        bws_gui_usun_stan_procesu(2);
        test_alokacja_nie_dziala=true;
        for(size_t i=0;i<bytes_of()/4;++i)layer.bufor_pikseli[i]=0x00111111;
        bws_gui_odswiez_pulpit();
        assert(!bws_gui_cien_warstwy(2,bytes_of())&&bws_gui_warstwa_w_trakcie_rysowania(2)&&layer.bufor_pikseli[3]==0);
        test_alokacja_nie_dziala=false;
        bws_gui_odswiez();
        // (b) timeout: cien starszy niz 1 s jest ignorowany (aplikacja, ktora nie wola gui_odswiez, nie zamraza okna)
        test_clock=true;test_ns=5000;
        for(size_t i=0;i<bytes_of()/4;++i)layer.bufor_pikseli[i]=0x00222222;
        bws_gui_odswiez_pulpit();
        assert(bws_gui_cien_warstwy(2,bytes_of())&&bws_gui_cien_warstwy(2,bytes_of())[1]==0x00222222);
        test_ns=5000+999999999ULL;assert(bws_gui_cien_warstwy(2,bytes_of()));            // tuz przed limitem: jeszcze cien
        test_ns=5000+1000000001ULL;assert(!bws_gui_cien_warstwy(2,bytes_of()));          // po limicie: bufor zywy
        bws_gui_odswiez();test_clock=false;test_ns=0;
        // (c) zakonczenie procesu w trakcie transakcji: cien zwolniony i nieaktywny, ponowne uzycie PID startuje czysto
        for(size_t i=0;i<bytes_of()/4;++i)layer.bufor_pikseli[i]=0x00333333;
        bws_gui_odswiez_pulpit();assert(bws_gui_cien_warstwy(2,bytes_of()));
        bws_gui_usun_stan_procesu(2);
        assert(!bws_gui_cien_warstwy(2,bytes_of()));
        bws_gui_odswiez();
        // (d) wymiana powierzchni i wielokrotna zmiana rozmiaru podczas transakcji: cien zawsze o rozmiarze warstwy
        const int rozmiary[][2]={{120,90},{300,200},{80,60},{300,200},{1000,40},{100,100}};
        uint32_t* oryginal=layer.bufor_pikseli;const int ow=layer.szerokosc,oh=layer.wysokosc;
        for(auto& r:rozmiary){
            uint32_t* stary=layer.bufor_pikseli;
            layer.bufor_pikseli=static_cast<uint32_t*>(calloc(size_t(r[0])*r[1],4));assert(layer.bufor_pikseli);
            layer.szerokosc=r[0];layer.wysokosc=r[1];
            for(size_t i=0;i<bytes_of()/4;++i)layer.bufor_pikseli[i]=0x00444444+uint32_t(r[0]);
            bws_gui_odswiez_pulpit();
            const uint32_t* c=bws_gui_cien_warstwy(2,bytes_of());
            assert(c&&c!=layer.bufor_pikseli&&c[0]==0x00444444+uint32_t(r[0]));          // cien = ostatnia klatka w NOWYM rozmiarze
            assert(!bws_gui_cien_warstwy(2,bytes_of()+4)&&!bws_gui_cien_warstwy(2,bytes_of()-4));   // zadnego bufora o zlym rozmiarze
            bws_gui_odswiez();
            if(stary!=oryginal)free(stary);
        }
        free(layer.bufor_pikseli);layer.bufor_pikseli=oryginal;layer.szerokosc=ow;layer.wysokosc=oh;
        bws_gui_usun_stan_procesu(2);
        assert(!bws_gui_cien_warstwy(2,bytes_of()));
    }
    // The real dirty-region replacement copies transparency without clearing
    // or publishing the layer between preparation and the one BWS17 commit.
    const uint32_t replacement[]={0,0x123456,0,0x654321};
    const unsigned dirty_before=invalidations,frames_before=presents;
    assert(bws_gui_zastap_region(100,100,2,2,replacement));
    assert(layer.bufor_pikseli[0]==0&&layer.bufor_pikseli[1]==0x123456);
    assert(layer.bufor_pikseli[100]==0&&layer.bufor_pikseli[101]==0x654321);
    assert(invalidations==dirty_before&&presents==frames_before);
    bws_gui_odswiez();assert(invalidations==dirty_before+1&&presents==frames_before);
    assert(bws_gui_blit(100,100,2,2,replacement));
    bws_gui_odswiez();assert(layer.bufor_pikseli[1]==0x123456);
    BwsGuiRegion batch[]={{100,100,2,2,reinterpret_cast<uint64_t>(replacement)},
                          {199,199,2,2,reinterpret_cast<uint64_t>(replacement)}};
    const uint32_t before=layer.bufor_pikseli[0];
    assert(!bws_gui_zastap_regiony(batch,2,0));assert(layer.bufor_pikseli[0]==before);
    batch[1]={110,110,2,2,reinterpret_cast<uint64_t>(replacement)};
    assert(!bws_gui_zastap_regiony(batch,2,0xfffffff0));
    assert(bws_gui_zastap_regiony(batch,2,0));
    assert(layer.bufor_pikseli[0]==0 && layer.bufor_pikseli[10*100+11]==0x123456);
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
    // Each fresh first publication uses real graphics presentation entry points.
    // Initialization cannot present without a valid display; the previous valid
    // framebuffer remains untouched until a complete render becomes possible.
    for(auto delay:{0ULL,500000ULL,1500000ULL,3000000ULL}) {
        powitanie_wycisz();test_now=0;aktywny_ekran=nullptr;
        unsigned before=presents;
        powitanie_inicjalizuj(true);
        assert(!powitanie_pierwsza_klatka_gotowa()&&presents==before);
        test_now=delay;aktywny_ekran=&display;
        // A cursor-sized first dirty rect still publishes a complete frame once.
        PrzeniesFragmentNaEkran(0,0,1,1);
        assert(presents==before+1&&powitanie_pierwsza_klatka_gotowa());
        const auto g=Powitanie::geometria(1280,720,100);
        auto ink=[&](Powitanie::Obszar r){
            for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)
                if(display.scanout[y*1280+x]!=Powitanie::TLO)return true;
            return false;
        };
        assert(ink(g.logo));
    assert(sekwencja.alfa_linia1==0||ink(Powitanie::centrum(1280,720,100)));
        assert(display.scanout[0]==Powitanie::TLO&&display.scanout[1280*720-1]==Powitanie::TLO);
        assert(sekwencja.krok==0&&sekwencja.start==delay);
        powitanie_reset_ekranu();assert(!powitanie_pierwsza_klatka_gotowa());
        PrzeniesFragmentNaEkran(0,0,1,1);assert(presents==before+2);
    }
    // Power quiesce blocks both presentation entry points. Simulated graphics
    // reinit clears the gate under the same screen lock as production startup.
    unsigned before_quiesce=presents;
    powitanie_wycisz();grafika_wycisz();
    PrzeniesNaEkran();PrzeniesFragmentNaEkran(0,0,1,1);
    assert(presents==before_quiesce&&!powitanie_aktywne());
    grafika_reset_startu();assert(!grafika_wyciszona);
    grafika_zakoncz_wybor_backendu();
    test_now=0;powitanie_inicjalizuj(true);
    assert(sekwencja.krok==0&&sekwencja.start==0);
    powitanie_wycisz();
    powitanie_inicjalizuj(false);
    const GuiDirtyRect complete_desktop{0,0,1280,720};
    grafika_prezentuj_scene_z_kursorem(&complete_desktop,1,-100,-100);
    assert(display.scanout[0]==0x008A5A00);
    free(layer.bufor_pikseli);layer={};
    free(bufor_tapety);free(backbuffer);backbuffer=nullptr;aktywny_ekran=nullptr;
    printf("Production boot graphics/frame commit: %u/%u PASS\n",checks,checks);
}
