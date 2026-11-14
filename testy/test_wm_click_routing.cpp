#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "bursztyn_ui_host_fixture.h"
#include "wm_input.h"
#include "wm_belka_hit.h"
#include "wm_dekoracje.h"
#define _start wm_test_entry
#include "../programy/menedzer_okien/menedzer_okien.cpp"
#undef _start
static std::string launched;
struct PowerIntercept { uint64_t syscall; };
static uint64_t launch_hook(uint64_t nr,uint64_t a,uint64_t,uint64_t,uint64_t) {
    if(nr==25 || nr==26)throw PowerIntercept{nr};
    if(nr==10)launched=reinterpret_cast<const char*>(a);
    return 1;
}
void gui_ustaw_przejecie_myszy(bool) {}
void gui_ustaw_system_overlay(bool,int,int,int,int) {}
void rysuj_tekst_wysrodkowany(int,int,int,int,int,uint32_t,const char*) {}
void gui_odswiez_pulpit() {}
int bws_utworz_warstwe(int,int,int,int,int) {return 1;}
bool czytaj_fragment_pliku(const char*, uint64_t, char*, uint32_t, uint32_t*) { return false; }
static unsigned checks=0;
#undef assert
#define assert(condition) do {++checks;if(!(condition)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition);abort();}} while(0)
int main() {
    assert(wm_cel_ruchu(2,-1,1,true)==1); // outside focused Shell -> desktop
    assert(wm_cel_ruchu(2,-1,3,true)==3); // another hovered window
    assert(wm_cel_ruchu(2,2,1,true)==2); // selection/scrollbar capture
    assert(wm_cel_ruchu(2,-1,1,false)==2); // focus/button edge semantics
    int previous=2;
    assert(wm_opuszczony_cel_ruchu(previous,1,-1)==2);
    for(int i=0;i<100;++i)assert(wm_opuszczony_cel_ruchu(previous,1,-1)==-1);
    assert(wm_opuszczony_cel_ruchu(previous,2,2)==-1&&previous==1);
    assert(wm_cel_ruchu(2,-1,-1,true)==-1); // no hit -> no app wakeup

    screen_w=1280;screen_h=720;metryki_ekranu();
    g_test_bws_hook=launch_hook;
    {   // pasek zadan jako powierzchnia kompozytora + "Zawsze na wierzchu takze nad paskiem zadan"
        RysujTaskbar();
        assert(test_pasek_otwarty && test_pasek_rect[2]==desktop_geometry.taskbar_width &&
               test_pasek_rect[1]==desktop_geometry.taskbar_y && test_pasek_rect[3]==desktop_geometry.taskbar_height);
        liczba_okien_taskbara=2;okna_taskbara[0]={};okna_taskbara[1]={};
        okna_taskbara[0].window_id=0x100000005ULL;okna_taskbara[1].window_id=0x200000007ULL;
        strcpy(okna_taskbara[0].tytul,"Okno A");strcpy(okna_taskbara[1].tytul,"Okno B");
        const int y0=screen_h-ui_skaluj(36,ui_skala_procent)+2;
        const int bx1=TASK_BUTTON_X+2,bx2=TASK_BUTTON_X+TASK_BUTTON_W+TASK_BUTTON_GAP+2;
        assert(!czy_nad_paskiem(okna_taskbara[0].window_id));                        // domyslnie wylaczone
        assert(obsluz_prawy_klik_paska(bx1,y0) && czy_nad_paskiem(okna_taskbara[0].window_id));
        assert(test_nad_paskiem[okna_taskbara[0].window_id]==true && !czy_nad_paskiem(okna_taskbara[1].window_id));
        assert(obsluz_prawy_klik_paska(bx2,y0) && czy_nad_paskiem(okna_taskbara[1].window_id));
        assert(obsluz_prawy_klik_paska(bx1,y0) && !czy_nad_paskiem(okna_taskbara[0].window_id));   // cofniecie
        assert(test_nad_paskiem[okna_taskbara[0].window_id]==false);
        assert(!obsluz_prawy_klik_paska(5,5));                                        // poza przyciskiem: bez efektu
        okna_taskbara[1]={};liczba_okien_taskbara=1;                                 // okno zamkniete
        wyczysc_nad_paskiem_zamknietych(okna_taskbara,liczba_okien_taskbara);
        assert(!czy_nad_paskiem(0x200000007ULL) && nad_paskiem_liczba==0);
        for(int i=0;i<MAKS_NAD_PASKIEM+3;++i){                                        // limit liczby wpisow
            uint64_t id=0x300000000ULL+i;bool ok=przelacz_nad_paskiem(id);
            assert(ok==(i<MAKS_NAD_PASKIEM));
        }
        nad_paskiem_liczba=0;
        assert(!przelacz_nad_paskiem(0));                                             // zerowy id odrzucony
    }
    auto menu_click=[](const char* query) {
        obsluz_klikniecie(PRZYCISK_MENU_X+2,przycisk_menu_y()+2);
        assert(menu_start_otwarte);
        for(const char* at=query;*at;++at){bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_TEXT_INPUT;e.kod=*at;assert(start_menu->events.zdarzenie(e));}
        bws_zdarzenie enter{};enter.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;enter.kod=BWS_KEY_SOURCE_USB|40;enter.x=1;
        assert(start_menu->events.zdarzenie(enter));int action=start_menu->take_action();assert(action>=0);
        zamknij_menu_i_odtworz_pulpit();obsluz_element_menu(action);
        assert(!menu_start_otwarte && !start_menu->query()[0]);
    };
    menu_click("shell");assert(launched==SCIEZKA_POWLOKA);
    menu_click("notat");assert(launched==SCIEZKA_NOTATNIK);
    menu_click("diag");assert(launched=="/programy/diagnostyka.cebula/diagnostyka.bur");
    for(auto& launcher:DesktopEntries::Launchers) {
        launched.clear();uruchom_lub_aktywuj(launcher.path,launcher.title);
        assert(launched==launcher.path);
    }
    for(uint32_t scale:{100U,125U}) {
        ui_skala_procent=scale;metryki_ekranu();launched.clear();
        assert(!menu_start_otwarte);
        menu_click("shell");
        assert(launched==SCIEZKA_POWLOKA && !menu_start_otwarte);
    }
    // Exercise both popup activation and actual trusted WM dispatch. The BWS
    // mock throws before a power syscall can reach any host hardware path.
    for(int action:{19,18}) {
        start_menu->open({0,0,1280,680},10);start_menu->open_power();
        ZdarzenieKlawiatury power_key{};
        if(action==18){power_key.kod_klawisza=0x48;start_menu->events.popups.obsluz_klawisz(power_key);}
        power_key={};power_key.kod_klawisza=13;
        assert(start_menu->events.popups.obsluz_klawisz(power_key));
        assert(start_menu->take_action()==action);
        bool intercepted=false;
        try { obsluz_element_menu(action); }
        catch(const PowerIntercept& call) { assert(call.syscall==(action==18?25U:26U));intercepted=true; }
        assert(intercepted);
    }
    // Exact executable identity ignores localized/window-document titles.
    char path[128]{};strcpy(path,AppIdentity::DisplaySettings);
    assert(AppIdentity::equal(path,AppIdentity::DisplaySettings));assert(!AppIdentity::equal(path,AppIdentity::Settings));
    memset(path,'x',sizeof(path));assert(!AppIdentity::equal(path,AppIdentity::Settings));
    test_running_title="Ustawienia ekranu";test_running_executable=AppIdentity::DisplaySettings;
    launched.clear();test_activated_window=0;uruchom_lub_aktywuj(AppIdentity::Settings,"Ustawienia");
    assert(launched==AppIdentity::Settings && test_activated_window==0);
    launched.clear();uruchom_lub_aktywuj(AppIdentity::DisplaySettings,"Ustawienia ekranu");
    assert(launched.empty() && test_activated_window==42);
    test_running_title="Ustawienia";test_running_executable=AppIdentity::Settings;
    launched.clear();test_activated_window=0;uruchom_lub_aktywuj(AppIdentity::DisplaySettings,"Ustawienia ekranu");
    assert(launched==AppIdentity::DisplaySettings && test_activated_window==0);
    launched.clear();uruchom_lub_aktywuj(AppIdentity::Settings,"Ustawienia");assert(launched.empty()&&test_activated_window==42);
    test_running_title="Changed title";launched.clear();uruchom_lub_aktywuj(AppIdentity::Settings,"Ustawienia");assert(launched.empty());
    test_running_title=nullptr;test_running_executable=nullptr;
    // Use the production kernel selection, with retained client buffers.
    for(uint32_t scale:{100U,125U}) {
        warstwa_obrazu layers[SKLADACZ_MAKS_WARSTW]{};
        std::vector<uint32_t> desktop(1280*720,1),app(400*300,0),top(400*300,0);
        auto set=[&](int pid,int z,int x,int y,uint32_t* buffer) {
            auto& w=layers[pid];w.aktywna=true;w.z_order=z;w.x=x;w.y=y;
            w.szerokosc=pid==1?1280:400;w.wysokosc=pid==1?720:300;w.bufor_pikseli=buffer;
        };
        set(1,0,0,0,desktop.data());set(2,100,100,100,app.data());set(3,101,100,100,top.data());
        bool decorated[SKLADACZ_MAKS_WARSTW]{};decorated[2]=true;
        // A full-screen transparent visual surface must never hide root input.
        std::vector<uint32_t> transparent(1280*720,0);
        layers[4]=layers[1];layers[4].z_order=300;layers[4].bufor_pikseli=transparent.data();
        auto hit=[&](int x,int y,int overlay=-1) {
            return wm_warstwa_pod_punktem(x,y,overlay,
                [&](int pid){return &layers[pid];},
                [&](int pid,int lx,int ly){return decorated[pid]&&wm_dekoracja_w_punkcie(400,300,lx,ly,scale);});
        };
        std::fill(desktop.begin(),desktop.end(),0);
        assert(hit(900,500)==1); // empty transparent desktop receives right click
        assert(hit(35,699)==1); // transparent top object cannot block Start
        assert(hit(150,110)==2); // visible compositor titlebar, zero retained pixel
        decorated[2]=false;assert(hit(150,110)==1); // reproduce the old pixel-only rejection
        decorated[2]=true;
        assert(hit(485,110)==2);
        assert(wm_hit_test_belki(485,110,100,100,400,scale,0)==GUI_BELKA_ZAMKNIJ);
        assert(wm_hit_test_belki(150,110,100,100,400,scale,0)==GUI_BELKA_DRAG);
        assert(wm_hit_test_belki(150,160,100,100,400,scale,0)==GUI_BELKA_BRAK); // close button remains owned by the same window
        assert(hit(150,160)==1); // transparent client passes through
        app[60*400+50]=1;assert(hit(150,160)==2); // normal client targets app PID
        top[60*400+50]=1;assert(hit(150,160)==3); // topmost interactive object
        layers[3].z_order=100;assert(hit(150,160)==3); // stable PID tie breaker
        layers[3].aktywna=false;assert(hit(150,160)==2);
        assert(hit(150,160,1)==1); // modal outside click delivered once to popup owner
        const auto geometry=layers[2];
        BursztynMotyw theme=g_domyslny_motyw_fallback;
        std::vector<uint32_t> scene(400*300,0);
        for(int redraw=0;redraw<2;++redraw) {
            theme.panel^=0xffffff;theme.rogokna=redraw?24:0;
            wm_rysuj_dekoracje(400,300,"Notatnik",false,scale,theme,
                [&](int x,int y,uint32_t c){scene[y*400+x]=c;});
            assert(!memcmp(&geometry,&layers[2],sizeof(geometry)));
            assert(hit(35,699)==1 && hit(150,160)==2 && hit(150,110)==2);
            assert(scene[60*400+50]==0); // no full-client decoration coverage
        }
    }
    printf("WM routing %u/%u PASS: ",checks,checks);puts("WM click routing passed: desktop, Start, launch, client PID, titlebar, close region, transparency, z-order, popup owner, theme and redraw");
}
