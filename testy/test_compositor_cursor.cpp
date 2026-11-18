#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <vector>
#define BURSZTYN_COMPOSITOR_HOST_TEST 1
#include "../jadro/gui/skladacz_obrazu.cpp"
#include "../jadro/bws/popup_autoryzacja.h"
#include "../jadro/bws/uruchamianie_polityka.h"
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
static unsigned hardware_moves=0;
static bool mock_hardware_cursor=false;
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
bool grafika_czy_kursor_sprzetowy(){return mock_hardware_cursor;}
void grafika_pobierz_overlay_kursora(int* x,int* y,int* w,int* h,bool* visible){*x=overlay.x;*y=overlay.y;*w=overlay.width;*h=overlay.height;*visible=overlay.width>0;}
void grafika_pobierz_region_kursora(int nx,int ny,int* x,int* y,int* w,int* h){
    int x0=overlay.width>0?std::min(overlay.x,nx):nx;
    int y0=overlay.height>0?std::min(overlay.y,ny):ny;
    int x1=std::max(overlay.width>0?overlay.x+overlay.width:nx+16,nx+16);
    int y1=std::max(overlay.height>0?overlay.y+overlay.height:ny+16,ny+16);
    *x=x0;*y=y0;*w=x1-x0;*h=y1-y0;
}
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
    if(mock_hardware_cursor){
        for(uint32_t i=0;i<count;++i)++presents;
        overlay={x,y,16,16};
        ++hardware_moves;
        return;
    }
    recorded_count=count;if(count)recorded_first=r[0];
    uint32_t scratch[64*64]{};
    overlay=kursor_prezentuj_transakcje(scene.data(),sw,sw,sh,overlay,x,y,16,16,scratch,r,count,
        [](int,int,uint32_t& c){c=0xffffff;return true;},
        [](GuiDirtyRect rect){assert(rect.x>=0&&rect.y>=0&&rect.x+rect.width<=sw&&rect.y+rect.height<=sh);++presents;},[](int){});
}
void grafika_prezentuj_kursor_w(int x,int y){grafika_prezentuj_scene_z_kursorem(nullptr,0,x,y);}

// --- VirtIO GPU: fixture hostowy nie ma sprzetu ---
// Kompozytor odpytuje sterownik o geometrie hosta (GET_DISPLAYINFO). Bez
// urzadzenia musi to byc bezpieczne "brak kierowcy / brak zmiany", a nie
// niezdefiniowana referencja na etapie linkowania.
static VirtioGpuDriver fixture_gpu{};
static bool fixture_gpu_obecny=false;
static uint32_t fixture_host_w=0,fixture_host_h=0;
static unsigned fixture_gpu_odczyty=0;
VirtioGpuDriver* virtio_gpu_get_driver(uint32_t index){
    return index==0&&fixture_gpu_obecny?&fixture_gpu:nullptr;
}
uint32_t virtio_gpu_get_driver_count(){return fixture_gpu_obecny?1u:0u;}
bool virtio_gpu_pobierz_geometrie_hosta(VirtioGpuDriver* drv,uint32_t* out_w,uint32_t* out_h){
    ++fixture_gpu_odczyty;
    if(!drv||!drv->initialized||drv->has_error||!fixture_host_w||!fixture_host_h)return false;
    if(out_w)*out_w=fixture_host_w;
    if(out_h)*out_h=fixture_host_h;
    return true;
}
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
    // Dymek paska zadan (bez wejscia): widoczny, ale nie przechwytuje myszy.
    assert(skladacz_obrazu_overlay_pod_punktem(x+1,y+1)==-1);++popup_checks;
    assert(skladacz_obrazu_ustaw_popup_aplikacji(1,true,x,y,w,h,true));++popup_checks;
    std::fill(pixels.begin(),pixels.end(),0xff335577U);
    assert(skladacz_obrazu_blit_popup(1,x,y,w,h,pixels.data()));++popup_checks;
    assert(scene[(y+1)*sw+x+w/2]==0x335577U);++popup_checks;
    assert(skladacz_obrazu_overlay_pod_punktem(x+1,y+1)==-1);++popup_checks;
    // Ten sam rozmiar, zwykly popup: znow modalny; i z powrotem.
    assert(skladacz_obrazu_ustaw_popup_aplikacji(1,true,x,y,w,h,false));++popup_checks;
    assert(skladacz_obrazu_overlay_pod_punktem(x+1,y+1)==1);++popup_checks;
    assert(skladacz_obrazu_ustaw_popup_aplikacji(1,true,x,y,w,h,true));++popup_checks;
    assert(skladacz_obrazu_overlay_pod_punktem(x+1,y+1)==-1);++popup_checks;
    // Interaktywny popup innej aplikacji zastepuje dymek; dymek nie zastepuje popupu.
    assert(utworz_warstwe(2,0,0,320,240,10)==2);assert(skladacz_obrazu_publikuj_klatke(2));
    assert(skladacz_obrazu_ustaw_popup_aplikacji(2,true,10,10,40,30,false));++popup_checks;
    assert(skladacz_obrazu_overlay_pod_punktem(0,0)==2);++popup_checks;
    assert(!skladacz_obrazu_ustaw_popup_aplikacji(1,true,x,y,w,h,true));++popup_checks;
    assert(!skladacz_obrazu_ustaw_popup_aplikacji(1,false,0,0,0,0));++popup_checks;   // nie jego popup
    assert(skladacz_obrazu_ustaw_popup_aplikacji(2,false,0,0,0,0));++popup_checks;
    assert(skladacz_obrazu_overlay_pod_punktem(0,0)==-1);++popup_checks;
    for(int py=0;py<h;++py)for(int px=0;px<w;++px) {
        assert(scene[(y+py)*sw+x+px]==baseline[py*w+px]);++popup_checks;            // dymek usuniety z ekranu
    }
    usun_warstwe(2);
}
static void rounded_window_cases(){
    for(unsigned dpi:{100U,125U,150U,200U})for(uint32_t background:{0xffeeeeeeU,0xff141414U,0xff427abcU}){
        scale=dpi;sw=1280;sh=720;overlay={};use_clock=false;
        assert(utworz_warstwe(1,0,0,sw,sh,0)==1);auto* lower=pobierz_warstwe(1);
        std::fill(lower->bufor_pikseli,lower->bufor_pikseli+sw*sh,background);assert(skladacz_obrazu_publikuj_klatke(1));
        if(background==0xff427abcU){
            assert(utworz_warstwe(3,0,0,sw,sh,5)==3);auto* lower_window=pobierz_warstwe(3);
            assert(skladacz_obrazu_ustaw_tytul(3,"Notatnik"));
            std::fill(lower_window->bufor_pikseli,lower_window->bufor_pikseli+sw*sh,background);
            assert(skladacz_obrazu_publikuj_klatke(3));
        }
        assert(utworz_warstwe(2,180,120,400,300,10)==2);assert(skladacz_obrazu_ustaw_tytul(2,"Ustawienia"));
        for(int stage=0;stage<6;++stage){
            int x=180+stage*7,y=120+stage*9,w=400+stage*12,h=300+stage*8;
            assert(zmien_geometrie_warstwy(2,x,y,w,h));auto* window=pobierz_warstwe(2);
            // Hostile rectangular client writes may never escape WM coverage.
            std::fill(window->bufor_pikseli,window->bufor_pikseli+w*h,0xffff00ffU);
            stan_okna[2]=(stage==3)?GUI_OKNO_ZMAKSYMALIZOWANE:GUI_OKNO_NORMALNE;
            compositor_theme.rogokna=(stage==5)?16:10;
            assert(skladacz_obrazu_publikuj_klatke(2));
            skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
            auto full=scene;WindowShape shape(w,h,stage==3,scale,compositor_theme);
            for(int py=0;py<h;++py)for(int px=0;px<w;++px){
                auto pixel=scene[(y+py)*sw+x+px];
                if(!shape.contains(px,py))assert(pixel==(background&0xffffff));
                else if(shape.client(px,py))assert(pixel==0xff00ff);
            }
            assert(stage==3?shape.contains(0,0):!shape.contains(0,0));
            // Partial repaint and full-layer paths must reproduce the same mask.
            for(int cy:{0,h-20})for(int cx:{0,w-20}){
                skladacz_obrazu_oznacz_dirty_rect(x+cx,y+cy,20,20);skladacz_obrazu_obsluz_dirty();
            }
            assert(scene==full);
            grafika_odtworz_tlo_skladania();zloz_warstwe(1,sw,sh);if(background==0xff427abcU)zloz_warstwe(3,sw,sh);zloz_warstwe(2,sw,sh);assert(scene==full);
            assert(skladacz_obrazu_minimalizuj(2));skladacz_obrazu_obsluz_dirty();
            assert(scene[y*sw+x]==(background&0xffffff));
            assert(skladacz_obrazu_przywroc(skladacz_obrazu_id_okna(2)));skladacz_obrazu_obsluz_dirty();
            if(stage!=3)assert(scene==full);
            int old_radius=compositor_theme.rogokna;compositor_theme.rogokna=24;
            skladacz_obrazu_odswiez_dekoracje();skladacz_obrazu_obsluz_dirty();
            WindowShape changed(w,h,false,scale,compositor_theme);
            for(int py=h-ui_skaluj(28,scale);py<h;++py)for(int px=0;px<w;++px)
                if(!changed.contains(px,py))assert(scene[(y+py)*sw+x+px]==(background&0xffffff));
            compositor_theme.rogokna=old_radius;skladacz_obrazu_odswiez_dekoracje();skladacz_obrazu_obsluz_dirty();

        }
        usun_warstwe(2);if(background==0xff427abcU)usun_warstwe(3);usun_warstwe(1);skladacz_reset_ekranu();
    }
    compositor_theme=g_domyslny_motyw_fallback;
}
/* Fast-path: nieprzezroczyste okno, pulpit z alfa, częściowy dirty — te same piksele. */
static void opaque_fastpath_cases(){
    scale=100;sw=640;sh=480;overlay={};use_clock=false;scene.assign(sw*sh,0x123456);
    assert(utworz_warstwe(1,0,0,sw,sh,0)==1);
    auto* desktop=pobierz_warstwe(1);
    std::fill(desktop->bufor_pikseli,desktop->bufor_pikseli+sw*sh,0xff203040U);
    desktop->bufor_pikseli[10+20*sw]=0x70ffffffU; // selection alpha tag
    desktop->bufor_pikseli[11+20*sw]=0x20000000U;
    assert(skladacz_obrazu_publikuj_klatke(1));

    assert(utworz_warstwe(2,100,80,200,120,10)==2); // bez tytułu → bez dekoracji
    auto* window=pobierz_warstwe(2);
    std::fill(window->bufor_pikseli,window->bufor_pikseli+200*120,0x00aabbccU);
    window->bufor_pikseli[0]=0; // transparent corner
    window->bufor_pikseli[1]=0x01000000U; // visible black marker
    assert(skladacz_obrazu_publikuj_klatke(2));

    grafika_odtworz_tlo_skladania();
    zloz_snapshot_region({desktop->bufor_pikseli,0,0,sw,sh,0,1,uint64_t(sw)*sh},{0,0,sw,sh});
    zloz_snapshot_region({window->bufor_pikseli,100,80,200,120,10,2,200ull*120},{0,0,sw,sh});
    auto via_snapshot=scene;

    grafika_odtworz_tlo_skladania();
    zloz_warstwe(1,sw,sh);zloz_warstwe(2,sw,sh);
    assert(scene==via_snapshot);

    const uint32_t desk_rgb=motyw_pulpit_skladaj(0x123456,0xff203040U,true);
    const uint32_t sel_rgb=motyw_pulpit_skladaj(0x123456,0x70ffffffU,true);
    assert(via_snapshot[0]==desk_rgb);
    assert(via_snapshot[20*sw+10]==sel_rgb);
    assert(via_snapshot[80*sw+100]==desk_rgb); // transparent window pixel keeps desktop
    assert(via_snapshot[80*sw+101]==0); // visible-black marker → RGB 0
    assert(via_snapshot[80*sw+102]==0xaabbcc);

    // Partial dirty must match a full compose for that rect.
    grafika_odtworz_tlo_skladania();
    GuiDirtyRect partial{110,90,40,30};
    zloz_snapshot_region({desktop->bufor_pikseli,0,0,sw,sh,0,1,uint64_t(sw)*sh},partial);
    zloz_snapshot_region({window->bufor_pikseli,100,80,200,120,10,2,200ull*120},partial);
    for(int py=partial.y;py<partial.y+partial.height;++py)
        for(int px=partial.x;px<partial.x+partial.width;++px)
            assert(scene[py*sw+px]==via_snapshot[py*sw+px]);

    usun_warstwe(2);usun_warstwe(1);skladacz_reset_ekranu();
}
// ---------------------------------------------------------------------------
// BWS 56: autoryzacja popupu bez wejscia. Prawdziwa obsluga wywolania
// (PopupAutoryzacja::wykonaj - ta sama funkcja, ktora wola dyspozytor
// syscalli) na prawdziwym kompozytorze. Po kazdej odmowie caly stan popupu,
// piksele bufora, scena, kolejka dirty i routing myszy musza byc bez zmian.
// ---------------------------------------------------------------------------
static unsigned bws56_checks=0;
static unsigned wywolania_kompozytora=0;
static bool ustaw_popup_licznik(int pid,bool o,int x,int y,int w,int h,bool b){
    ++wywolania_kompozytora;return skladacz_obrazu_ustaw_popup_aplikacji(pid,o,x,y,w,h,b);
}
static uint64_t para(int a,int b){return (uint64_t(uint32_t(a))<<32)|uint32_t(b);}
struct ZrzutPopupu {
    ApplicationPopupState s{};std::vector<uint32_t> tlo,scena;uint32_t dirty=0;int pod=-1;uint64_t gen=0;
};
static ZrzutPopupu zrzut_popupu(){
    ZrzutPopupu z;z.s=application_popup;z.scena=scene;z.dirty=dirty_count;
    z.pod=skladacz_obrazu_overlay_pod_punktem(0,0);
    if(application_popup.tlo)z.tlo.assign(application_popup.tlo,application_popup.tlo+application_popup.piksele);
    if(application_popup.pid>0&&application_popup.pid<MAKS_WARSTW)z.gen=generacja_okna[application_popup.pid];
    return z;
}
static bool zrzuty_rowne(const ZrzutPopupu& a,const ZrzutPopupu& b){
    return a.s.otwarty==b.s.otwarty&&a.s.pid==b.s.pid&&a.s.generacja==b.s.generacja&&
           a.s.rect.x==b.s.rect.x&&a.s.rect.y==b.s.rect.y&&a.s.rect.width==b.s.rect.width&&
           a.s.rect.height==b.s.rect.height&&a.s.tlo==b.s.tlo&&a.s.piksele==b.s.piksele&&
           a.s.bez_wejscia==b.s.bez_wejscia&&a.tlo==b.tlo&&a.scena==b.scena&&a.dirty==b.dirty&&
           a.pod==b.pod&&a.gen==b.gen;
}
struct ProcesTestowy { proces_t p{}; bool fokus=false; };
static ProcesTestowy proces_testowy(int pid,const char* sciezka,uint8_t poziom,uint64_t prawa,bool fokus=false){
    ProcesTestowy t;t.p.pid=uint64_t(pid);t.p.poziom_zaufania=poziom;t.p.uprawnienia=prawa;t.fokus=fokus;
    size_t i=0;for(;sciezka&&sciezka[i]&&i+1<sizeof(t.p.sciezka_pliku);++i)t.p.sciezka_pliku[i]=sciezka[i];
    t.p.sciezka_pliku[i]='\0';return t;
}
// Odpowiednik case 56 w syscalls.cpp (te same pola jadra -> Wywolujacy).
static uint64_t bws56(const ProcesTestowy& t,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4){
    const PopupAutoryzacja::Wywolujacy w{t.p.uprawnienia&uint64_t(PRAWA_ZNANE),t.p.poziom_zaufania,
                                         t.p.sciezka_pliku,sizeof(t.p.sciezka_pliku),t.fokus};
    return PopupAutoryzacja::wykonaj(w,int(t.p.pid),a1,a2,a3,a4,ustaw_popup_licznik);
}
// Odmowa: wynik 0, kompozytor nie wywolany (gdy odrzuca autoryzacja) i stan bez zmian.
static void odmowa(const ProcesTestowy& t,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,bool przez_autoryzacje=true){
    const ZrzutPopupu przed=zrzut_popupu();const unsigned wywolania=wywolania_kompozytora;
    assert(bws56(t,a1,a2,a3,a4)==0);++bws56_checks;
    if(przez_autoryzacje){assert(wywolania_kompozytora==wywolania);++bws56_checks;}
    assert(zrzuty_rowne(przed,zrzut_popupu()));++bws56_checks;
}
static void bws56_security_cases(){
    using namespace PopupAutoryzacja;
    use_clock=false;skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
    constexpr uint64_t PRAWA_WM=PRAWO_GUI|PRAWO_URUCHOM_PROGRAM|PRAWO_SYSTEM_CONFIG|PRAWO_PLIKI_CZYTAJ|
                                PRAWO_PLIKI_ZAPISZ|PRAWO_SIEC;   // PRAWA_MENEDZERA_OKIEN (kernel.cpp)
    constexpr uint64_t PRAWA_APLIKACJI=PRAWO_GUI|PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ|PRAWO_USTAWIENIA_APLIKACJI|
                                       PRAWO_OKNO_PODRZEDNE;
    // pid 1 = Menedzer Okien (warstwa pulpitu juz istnieje), pid 2/3 = aplikacje.
    const auto wm=proces_testowy(1,"/menedzer_okien.bur",PZB_USLUGI,PRAWA_WM);
    assert(utworz_warstwe(2,40,40,320,240,10)==2);assert(skladacz_obrazu_publikuj_klatke(2));
    assert(utworz_warstwe(3,400,40,320,240,11)==3);assert(skladacz_obrazu_publikuj_klatke(3));
    const uint64_t poz=para(200,300),roz=para(120,26);
    const ZrzutPopupu pusty=zrzut_popupu();

    // Kazdy proces poza Menedzerem Okien: z fokusem i bez, flaga sama i z
    // nieznanymi bitami, otwarcie i zamkniecie -> odmowa bez zmian.
    const auto* ustawienia=uruchamianie::znajdz_aplikacje_systemowa("/programy/ustawienia.cebula/ustawienia.bur");
    const auto* aktualizacje=uruchamianie::znajdz_aplikacje_systemowa("/programy/aktualizacje.cebula/aktualizacje.bur");
    assert(ustawienia&&aktualizacje);
    const ProcesTestowy obcy[]={
        proces_testowy(2,"/programy/notatnik.cebula/notatnik.bur",PZB_UZYTKOWNIK,PRAWA_APLIKACJI),
        proces_testowy(2,"/gry/snake.cebula/snake.bur",PZB_UZYTKOWNIK,PRAWA_APLIKACJI),
        proces_testowy(2,"/programy/husarz.cebula/husarz.bur",PZB_UZYTKOWNIK,PRAWA_APLIKACJI),
        proces_testowy(2,"/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur",PZB_UZYTKOWNIK,PRAWA_APLIKACJI),
        // Okno podrzedne (BWS 101: PZB_UZYTKOWNIK, tylko PRAWO_GUI) - takze osierocone.
        proces_testowy(2,"/programy/okno-informacyjne.cebula/okno-informacyjne.bur",PZB_UZYTKOWNIK,PRAWO_GUI),
        // Aplikacje systemowe z PRAWO_SYSTEM_CONFIG: prawa nie wystarczaja bez tozsamosci WM.
        proces_testowy(2,ustawienia->sciezka,ustawienia->poziom,ustawienia->prawa),
        proces_testowy(2,aktualizacje->sciezka,aktualizacje->poziom,aktualizacje->prawa),
        proces_testowy(2,"/programy/ekran.cebula/ekran.bur",PZB_ZAUFANE,PRAWO_GUI|PRAWO_SYSTEM_CONFIG),
        // Wyzszy poziom niz wymagany i pelne prawa, ale inna tozsamosc.
        proces_testowy(2,"/shell.bur",PZB_JADRO,PRAWA_ZNANE),
        // Sciezka WM, ale poziom/prawa zwyklej aplikacji (np. kopia uruchomiona z Ring 3).
        proces_testowy(1,"/menedzer_okien.bur",PZB_UZYTKOWNIK,PRAWA_WM&~PRAWO_SYSTEM_CONFIG),
        proces_testowy(1,"/menedzer_okien.bur",PZB_UZYTKOWNIK,PRAWA_WM),
        proces_testowy(1,"/menedzer_okien.bur",PZB_PIASKOWNICA,PRAWA_WM),
        proces_testowy(1,"/menedzer_okien.bur",PZB_ZAUFANE,PRAWA_WM&~PRAWO_SYSTEM_CONFIG),
        proces_testowy(1,"/menedzer_okien.bur",6,PRAWA_WM),
        proces_testowy(1,"/menedzer_okien.bur",0xFF,PRAWA_WM),
        // Podrobione sciezki (przedrostek, przyrostek, wielkosc liter, ../, pusta).
        proces_testowy(1,"/menedzer_okien.bu",PZB_USLUGI,PRAWA_WM),
        proces_testowy(1,"/menedzer_okien.bur.bak",PZB_USLUGI,PRAWA_WM),
        proces_testowy(1,"/MENEDZER_OKIEN.BUR",PZB_USLUGI,PRAWA_WM),
        proces_testowy(1,"//menedzer_okien.bur",PZB_USLUGI,PRAWA_WM),
        proces_testowy(1,"/programy/../menedzer_okien.bur",PZB_USLUGI,PRAWA_WM),
        proces_testowy(1,"/programy/menedzer_okien.cebula/menedzer_okien.bur/",PZB_USLUGI,PRAWA_WM),
        proces_testowy(1,"/home/menedzer_okien.bur",PZB_USLUGI,PRAWA_WM),
        proces_testowy(1,"",PZB_USLUGI,PRAWA_WM),
    };
    for(const auto& o0:obcy)for(bool fokus:{false,true})for(uint64_t a1:std::initializer_list<uint64_t>{1,0,2,~0ULL}){
        auto o=o0;o.fokus=fokus;
        for(uint64_t a4:std::initializer_list<uint64_t>{BWS_POPUP_BEZ_WEJSCIA,BWS_POPUP_BEZ_WEJSCIA|2ULL,BWS_POPUP_BEZ_WEJSCIA|(1ULL<<63),~0ULL}){
            odmowa(o,a1,poz,roz,a4);
            const Wywolujacy w{o.p.uprawnienia&uint64_t(PRAWA_ZNANE),o.p.poziom_zaufania,o.p.sciezka_pliku,
                               sizeof(o.p.sciezka_pliku),fokus};
            assert(autoryzuj(w,a1!=0,a4)!=Decyzja::Dozwolone);++bws56_checks;
        }
    }
    assert(zrzuty_rowne(pusty,zrzut_popupu()));++bws56_checks;
    // Konkretne przyczyny odmowy (sciezka decyzji, nie tylko "false").
    {
        auto w_of=[](const ProcesTestowy& t,bool f){return Wywolujacy{t.p.uprawnienia&uint64_t(PRAWA_ZNANE),
            t.p.poziom_zaufania,t.p.sciezka_pliku,sizeof(t.p.sciezka_pliku),f};};
        assert(autoryzuj(w_of(obcy[0],true),true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::BrakUprawnienSystemowych);
        assert(autoryzuj(w_of(obcy[5],true),true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::NieMenedzerOkien);   // Ustawienia
        assert(autoryzuj(w_of(obcy[6],true),true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::NieMenedzerOkien);   // Aktualizacje
        assert(autoryzuj(w_of(obcy[7],true),true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::NieMenedzerOkien);   // Ustawienia ekranu
        assert(autoryzuj(w_of(wm,false),true,2)==Decyzja::NieznaneFlagi);
        assert(autoryzuj(w_of(obcy[0],false),true,0)==Decyzja::BrakFokusu);
        assert(autoryzuj(w_of(obcy[0],false),false,0)==Decyzja::Dozwolone);    // zamkniecie wlasnego - jak dotad
        auto bez_gui=wm;bez_gui.p.uprawnienia&=~PRAWO_GUI;
        assert(autoryzuj(w_of(bez_gui,false),true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::BrakPrawaGui);
        // Nieznane bity procesu (stara maska 0xFFFFFFFF) nie daja nowych praw.
        auto stara_maska=proces_testowy(2,"/programy/notatnik.cebula/notatnik.bur",PZB_UZYTKOWNIK,0xFFFFFFFFULL&~PRAWO_SYSTEM_CONFIG);
        assert(autoryzuj(w_of(stara_maska,true),true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::BrakUprawnienSystemowych);
        // Bufor sciezki bez NUL: pelne 64 bajty zaczynajace sie od sciezki WM.
        ProcesTestowy bez_nul=wm;for(size_t i=0;i<sizeof(bez_nul.p.sciezka_pliku);++i)bez_nul.p.sciezka_pliku[i]='a';
        const char wzor[]="/menedzer_okien.bur";for(size_t i=0;i+1<sizeof(wzor);++i)bez_nul.p.sciezka_pliku[i]=wzor[i];
        assert(!menedzer_okien(w_of(bez_nul,false)));
        assert(!rowna_sciezka("/menedzer_okien.bur",5,"/menedzer_okien.bur"));       // pojemnosc za mala
        assert(!rowna_sciezka(nullptr,64,"/menedzer_okien.bur"));
        Wywolujacy pusta{};pusta.prawa=PRAWA_WM;pusta.poziom=PZB_USLUGI;
        assert(autoryzuj(pusta,true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::NieMenedzerOkien);  // brak sciezki
        bws56_checks+=13;
    }
    // Druga zaufana sciezka WM (jak BWS 40) i wyzszy poziom zaufania.
    {
        auto wm2=proces_testowy(1,"/programy/menedzer_okien.cebula/menedzer_okien.bur",PZB_ZAUFANE,PRAWA_WM);
        auto wm0=proces_testowy(1,"/menedzer_okien.bur",PZB_JADRO,PRAWA_WM);
        for(const auto& t:{wm2,wm0}){
            const Wywolujacy w{t.p.uprawnienia,t.p.poziom_zaufania,t.p.sciezka_pliku,sizeof(t.p.sciezka_pliku),false};
            assert(autoryzuj(w,true,BWS_POPUP_BEZ_WEJSCIA)==Decyzja::Dozwolone);++bws56_checks;
        }
    }

    // Autoryzowany WM bez fokusu: dymek widoczny, nie przechwytuje myszy.
    assert(bws56(wm,1,poz,roz,BWS_POPUP_BEZ_WEJSCIA)==1);++bws56_checks;
    std::vector<uint32_t> px(120*26,0xff446688U);
    assert(skladacz_obrazu_blit_popup(1,200,300,120,26,px.data()));
    assert(application_popup.otwarty&&application_popup.pid==1&&application_popup.bez_wejscia);
    assert(scene[310*sw+250]==0x446688U);
    assert(skladacz_obrazu_overlay_pod_punktem(250,310)==-1);bws56_checks+=3;
    // Ten sam WM bez fokusu nie moze zamienic dymka w popup interaktywny (modalny).
    odmowa(wm,1,poz,roz,0);
    // Nieznane bity od WM tez sa odrzucane (bez reinterpretacji jako zaufane).
    for(uint64_t a4:std::initializer_list<uint64_t>{2,3,1ULL<<32,1ULL<<63,~0ULL})odmowa(wm,1,poz,roz,a4);
    // Bledne wymiary/polozenie od WM: odrzuca kompozytor, dymek bez zmian.
    const uint64_t zle_rozmiary[]={para(0,26),para(120,0),para(-1,26),para(120,-1),para(2049,26),para(120,2049),
                                   para(INT32_MIN,INT32_MIN),para(INT32_MAX,INT32_MAX),~0ULL,0ULL};
    for(uint64_t r:zle_rozmiary)odmowa(wm,1,poz,r,BWS_POPUP_BEZ_WEJSCIA,false);
    const uint64_t zle_polozenia[]={para(-2049,0),para(0,-2049),para(4097,0),para(0,4097),para(INT32_MIN,0),
                                    para(0,INT32_MAX),para(INT32_MAX,INT32_MIN)};   // (-1,-1) jest dozwolone (czesciowo poza ekranem)
    for(uint64_t p:zle_polozenia)odmowa(wm,1,p,roz,BWS_POPUP_BEZ_WEJSCIA,false);
    // Obcy proces (nawet z fokusem) nie zamknie ani nie przejmie dymka flaga.
    auto notatnik=proces_testowy(2,"/programy/notatnik.cebula/notatnik.bur",PZB_UZYTKOWNIK,PRAWA_APLIKACJI,true);
    odmowa(notatnik,0,0,0,BWS_POPUP_BEZ_WEJSCIA);
    odmowa(notatnik,0,0,0,0,false);                           // zamkniecie cudzego popupu
    // Focused app z blednym rozmiarem nie moze usunac dymka (wypieranie dopiero po walidacji).
    for(uint64_t r:zle_rozmiary)odmowa(notatnik,1,para(50,50),r,0,false);
    for(uint64_t p:zle_polozenia)odmowa(notatnik,1,p,para(40,30),0,false);
    // Powtarzane odrzucone proby: zero efektow ubocznych, zero alokacji (ASan wykryje wyciek).
    {
        const ZrzutPopupu przed=zrzut_popupu();
        for(int i=0;i<2000;++i){
            (void)bws56(obcy[i%int(sizeof(obcy)/sizeof(obcy[0]))],1,poz,roz,BWS_POPUP_BEZ_WEJSCIA);
            (void)bws56(notatnik,1,para(50,50),para(0,0),0);
        }
        assert(zrzuty_rowne(przed,zrzut_popupu()));++bws56_checks;
    }
    // Poprawny interaktywny popup aplikacji z fokusem wypiera dymek (model bez zmian).
    assert(bws56(notatnik,1,para(50,50),para(40,30),0)==1);
    assert(application_popup.pid==2&&!application_popup.bez_wejscia);
    assert(skladacz_obrazu_overlay_pod_punktem(0,0)==2);
    assert(scene[310*sw+250]!=0x446688U);bws56_checks+=3;               // dymek zniknal z ekranu
    // Teraz WM nie moze nadpisac ani zamknac cudzego popupu interaktywnego.
    odmowa(wm,1,poz,roz,BWS_POPUP_BEZ_WEJSCIA,false);
    odmowa(wm,0,0,0,BWS_POPUP_BEZ_WEJSCIA,false);
    odmowa(wm,0,0,0,0,false);
    // Inna aplikacja z fokusem nie przejmie popupu pid 2.
    auto snake=proces_testowy(3,"/gry/snake.cebula/snake.bur",PZB_UZYTKOWNIK,PRAWA_APLIKACJI,true);
    odmowa(snake,1,para(400,60),para(40,30),0,false);
    odmowa(snake,0,0,0,0,false);
    // Aplikacja nie zamieni wlasnego popupu w bez-wejscia ani go nim nie zamknie.
    odmowa(notatnik,1,para(50,50),para(40,30),BWS_POPUP_BEZ_WEJSCIA);
    odmowa(notatnik,0,0,0,BWS_POPUP_BEZ_WEJSCIA);
    assert(bws56(notatnik,0,0,0,0)==1);assert(!application_popup.otwarty);bws56_checks+=2;

    // Zakonczenie WM przy widocznym dymku: dymek znika, bufor zwolniony.
    assert(bws56(wm,1,poz,roz,BWS_POPUP_BEZ_WEJSCIA)==1);
    assert(skladacz_obrazu_blit_popup(1,200,300,120,26,px.data()));
    assert(scene[310*sw+250]==0x446688U);
    usun_warstwe(1);skladacz_obrazu_obsluz_dirty();
    assert(!application_popup.otwarty&&application_popup.pid==-1&&!application_popup.tlo);
    assert(scene[310*sw+250]!=0x446688U);bws56_checks+=3;
    // Ponowne uzycie pid 1 przez zwykly proces: dziedziczy numer, nie prawa.
    assert(utworz_warstwe(1,0,0,sw,sh,0)==1);assert(skladacz_obrazu_publikuj_klatke(1));
    auto nastepca=proces_testowy(1,"/programy/notatnik.cebula/notatnik.bur",PZB_UZYTKOWNIK,PRAWA_APLIKACJI,false);
    odmowa(nastepca,1,poz,roz,BWS_POPUP_BEZ_WEJSCIA);
    nastepca.fokus=true;odmowa(nastepca,1,poz,roz,BWS_POPUP_BEZ_WEJSCIA);
    // Stara generacja: dymek zapisany przed zmiana generacji nie jest skladany.
    assert(bws56(wm,1,poz,roz,BWS_POPUP_BEZ_WEJSCIA)==1);
    assert(skladacz_obrazu_blit_popup(1,200,300,120,26,px.data()));
    if(++generacja_okna[1]==0)++generacja_okna[1];
    assert(!popup_aplikacji_spojny());
    assert(skladacz_obrazu_overlay_pod_punktem(0,0)==-1);bws56_checks+=2;
    usun_warstwe(1);skladacz_obrazu_obsluz_dirty();
    assert(!application_popup.otwarty);++bws56_checks;
    // Minimalizacja/zamkniecie wlasciciela innego popupu tez sprzata.
    assert(bws56(notatnik,1,para(50,50),para(40,30),0)==1);
    usun_warstwe(2);assert(!application_popup.otwarty);++bws56_checks;
    usun_warstwe(3);
    // Przywroc warstwe pulpitu pid 1 dla dalszych testow.
    assert(utworz_warstwe(1,0,0,sw,sh,0)==1);assert(skladacz_obrazu_publikuj_klatke(1));
    skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
}
int main(){
    // Compare packed rows against the pixel contract, including transparent
    // holes, black markers, high bytes and every short/unaligned tail.
    uint32_t rng=0x19460829U;
    for(int width=0;width<=257;++width)for(int offset=0;offset<4;++offset){
        std::vector<uint32_t> src(width+8),dst(width+8,0x12345678),expected=dst;
        for(auto& c:src){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;c=(rng%5)?rng:0;}
        for(int i=0;i<width;++i)if(src[offset+i])expected[offset+i]=src[offset+i]&0xffffffU;
        zloz_wiersz_nieprzezroczysty(dst.data()+offset,src.data()+offset,width);
        assert(dst==expected);
        graphics_copy_pixels(dst.data()+offset,src.data()+offset,width);
        for(int i=0;i<width;++i)expected[offset+i]=src[offset+i];
        assert(dst==expected);
    }
    rounded_window_cases();
    opaque_fastpath_cases();
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
        if(dpi==100)bws56_security_cases();
        // A stationary pointer must still get its first visible transaction
        // after the desktop handshake queues the current position.
        overlay={};dirty_count=0;cursor_pending=false;lx=320;ly=240;
        skladacz_obrazu_oznacz_ruch_kursora(lx,ly,lx,ly);
        presents=0;skladacz_obrazu_obsluz_dirty();
        assert(presents==1&&overlay.x==lx&&overlay.y==ly);
        // A repaint away from the pointer must not invalidate its visible
        // position or require another mouse packet.
        skladacz_obrazu_oznacz_dirty_rect(0,0,10,10);
        presents=0;skladacz_obrazu_obsluz_dirty();
        assert(presents==1&&overlay.x==lx&&overlay.y==ly);
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
        assert(presents==0&&cursor_pending&&dirty_count==1); // queued scene work blocks stale cursor-only publication
        mock_now+=17000000;presents=0;skladacz_obrazu_obsluz_dirty();
        assert(presents==1&&!cursor_pending&&dirty_count==0); // full scene reuses final cursor transaction
        mock_hardware_cursor=true;
        skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
        mock_now+=1000000;
        skladacz_obrazu_oznacz_dirty_rect(10,10,20,20);
        skladacz_obrazu_oznacz_ruch_kursora(lx,ly,120,130);
        lx=120;ly=130;presents=0;hardware_moves=0;
        skladacz_obrazu_obsluz_dirty();
        assert(presents==0&&hardware_moves==1&&!cursor_pending&&dirty_count>0);
        mock_hardware_cursor=false;
        use_clock=false;
        lx=150;ly=140;
        skladacz_obrazu_oznacz_ruch_kursora(120,130,lx,ly);
        presents=0;skladacz_obrazu_obsluz_dirty();
        assert(presents>=1&&!cursor_pending&&overlay.x==lx&&overlay.y==ly);
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
    // Exact scene regions stay separate for reconstruction, but nearby
    // regions share one present transfer when the backbuffer gap is cheap.
    dirty_count=0;cursor_pending=false;overlay={};recorded_count=0;
    skladacz_obrazu_oznacz_dirty_rect_dokladny(10,10,10,10);
    skladacz_obrazu_oznacz_dirty_rect_dokladny(25,10,10,10);
    skladacz_obrazu_obsluz_dirty();
    assert(recorded_count==1&&recorded_first.x==10&&recorded_first.width==25);
    // A rubber-band outline is four thin strips, not its bounding box.
    dirty_count=0;
    skladacz_obrazu_oznacz_dirty_rect_dokladny(10,10,500,1);
    skladacz_obrazu_oznacz_dirty_rect_dokladny(10,309,500,1);
    skladacz_obrazu_oznacz_dirty_rect_dokladny(10,10,1,300);
    skladacz_obrazu_oznacz_dirty_rect_dokladny(509,10,1,300);
    assert(dirty_count==4);
    uint64_t damaged=0;for(uint32_t i=0;i<dirty_count;++i)damaged+=rect_area(dirty_rects[i]);
    assert(damaged==1600);
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
    printf("BWS 56 popup authorization (security): %u/%u PASS\n",bws56_checks,bws56_checks);
    puts("Production compositor host PASS: modeset dirty geometry, Shell-equivalent layer preserves cursor/screen/generation, coalescing and theme/decor/full scene transactions");
    return 0;
}

void powitanie_reset_ekranu() {}
bool powitanie_pierwsza_publikacja() {return mock_first_splash;}
void powitanie_tick() {} // Dedicated presentation tests exercise the real runtime.
