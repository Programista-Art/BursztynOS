#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../wm_input.h"
#include "../wm_belka_hit.h"
#include "../wm_dekoracje.h"
#define _start wm_test_entry
#include "../menedzer_okien.cpp"
#undef _start
static std::string launched;
uint64_t bws_wywolaj(uint64_t nr,uint64_t a,uint64_t,uint64_t,uint64_t) {
    if(nr==10) launched=reinterpret_cast<const char*>(a);
    return 1;
}
uint32_t gui_pobierz_okna(GuiOknoInfo*,uint32_t) {return 0;}
void gui_ustaw_przejecie_myszy(bool) {}
bool gui_aktywuj_okno(uint64_t) {return true;}
void gui_odswiez() {}
void gui_ustaw_system_overlay(bool,int,int,int,int) {}
void rysuj_tekst_wysrodkowany(int,int,int,int,int,uint32_t,const char*) {}
int oblicz_szerokosc_tekstu(const char* s,int scale) {return int(strlen(s))*8*scale;}
bool gui_rejestruj_cele_drop(const BwsCelDrop*,uint32_t) {return true;}
void gui_rysuj_prostokat(int,int,int,int,uint32_t) {}
uint32_t motyw_kolor(uint32_t) {return 0x123456;}
bool gui_blit(int,int,int,int,const uint32_t*) {return true;}
void gui_odswiez_pulpit() {}
void gui_wypisz_tekst_kolor(int,int,uint32_t,const char*) {}
void wypisz(const char*) {}
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
    obsluz_klikniecie(35,699); assert(menu_start_otwarte);
    obsluz_klikniecie(100,232); assert(launched==SCIEZKA_POWLOKA);assert(!menu_start_otwarte);
    obsluz_klikniecie(35,699);obsluz_klikniecie(100,282);assert(launched==SCIEZKA_NOTATNIK);
    launched.clear();obsluz_klikniecie(APLIKACJE_PULPITU[0].x+5,APLIKACJE_PULPITU[0].y+5);
    assert(launched==APLIKACJE_PULPITU[0].sciezka);
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
    puts("WM click routing passed: desktop, Start, launch, client PID, titlebar, close region, transparency, z-order, popup owner, theme and redraw");
}
