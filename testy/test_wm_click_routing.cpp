#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "bursztyn_ui_host_fixture.h"
#include "wm_input.h"
#include "wm_belka_hit.h"
#include "wm_dekoracje.h"
#define _start wm_test_entry
#include "../programy/menedzer_okien/menedzer_okien.cpp"
#undef _start
static std::string launched;
struct PowerIntercept { uint64_t syscall; };
// Prawdziwe pliki ikon z repozytorium (/ikonki/x.png -> ikonki/x.png); brakujaca
// ikona symulowana przez test_brak_ikony.
static bool test_czytaj_ikony=false;
static std::string test_brak_ikony;
static std::vector<char> plik_ikony(const char* p) {
    std::vector<char> d;if(!test_czytaj_ikony||!p||strncmp(p,"/ikonki/",8)||test_brak_ikony==p)return d;
    FILE* f=fopen(p+1,"rb");if(!f)return d;char b[4096];size_t n;while((n=fread(b,1,sizeof b,f))>0)d.insert(d.end(),b,b+n);fclose(f);return d;
}
static uint64_t launch_hook(uint64_t nr,uint64_t a,uint64_t a2,uint64_t,uint64_t) {
    if(nr==4&&test_czytaj_ikony){auto d=plik_ikony(reinterpret_cast<const char*>(a));if(d.empty())return 0;
        *reinterpret_cast<uint32_t*>(a2)=uint32_t(d.size());return 1;}
    if(nr==25 || nr==26)throw PowerIntercept{nr};
    if(nr==10)launched=reinterpret_cast<const char*>(a);
    return 1;
}
void gui_ustaw_przejecie_myszy(bool) {}
void gui_ustaw_system_overlay(bool,int,int,int,int) {}
void rysuj_tekst_wysrodkowany(int,int,int,int,int,uint32_t,const char*) {}
void gui_odswiez_pulpit() {}
int bws_utworz_warstwe(int,int,int,int,int) {return 1;}
bool czytaj_fragment_pliku(const char* p, uint64_t off, char* out, uint32_t n, uint32_t* got) {
    auto d=plik_ikony(p);if(d.empty()||off>d.size()||!out)return false;
    const uint32_t k=uint32_t(std::min<uint64_t>(n,d.size()-off));memcpy(out,d.data()+off,k);if(got)*got=k;return true;
}
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
    // Pasek zadan z ikonami aplikacji: tozsamosc programu (nie tytul), okna
    // podrzedne, ponowne uzycie pid, brak ikony, stany, motywy, wiele okien, skala.
    {
        ui_skala_procent=100;metryki_ekranu();test_czytaj_ikony=true;
        test_surface_width=1280;test_surface_height=720;test_surface.assign(1280*720,0);
        const int iN=AppIcons::indeks_programu(SCIEZKA_NOTATNIK),iE=AppIcons::indeks_programu(SCIEZKA_EKSPLORATOR);
        const int iD=AppIcons::indeks_programu(AppIdentity::DisplaySettings),iS=AppIcons::indeks_programu(AppIdentity::Settings);
        const int iK=AppIcons::indeks_programu(SCIEZKA_KALKULATOR),iH=AppIcons::indeks_programu(SCIEZKA_HUSSAR);
        const int iSn=AppIcons::indeks_programu(SCIEZKA_SNAKE),iO=AppIcons::indeks_programu(SCIEZKA_PRZEGLADARKA_OBRAZOW);
        assert(iN>=0&&iE>=0&&iD>=0&&iS>=0&&iK>=0&&iH>=0&&iSn>=0&&iO>=0&&iD!=iS);
        // Te same pliki co skroty pulpitu i menu Start; Ustawienia ekranu = nowa ikona, nie ikona Ustawien.
        for(int i=0;i<DesktopEntries::Count;++i)assert(!strcmp(AppIcons::icon_png(i),DesktopEntries::Launchers[i].icon_png));
        assert(!strcmp(AppIcons::icon_png(iD),"/ikonki/app_ustawienia_ekranu.png"));
        assert(!strcmp(AppIcons::icon_png(iS),"/ikonki/app_ustawienia.png"));
        for(const auto& app:StartMenu::apps)if(app.action==12)assert(!strcmp(app.icon_png,AppIcons::icon_png(iD)));
        for(const auto& app:StartMenu::apps)if(app.action==2)assert(!strcmp(app.icon_png,AppIcons::icon_png(iN)));
        for(const auto& app:StartMenu::apps)if(app.action==20)assert(!strcmp(app.icon_png,AppIcons::icon_png(iS)));
        for(const auto& app:StartMenu::apps)if(app.icon_png){bool znana=false;for(int i=0;i<AppIcons::Count;++i)znana|=!strcmp(app.icon_png,AppIcons::icon_png(i));assert(znana);}
        assert(AppIcons::indeks_programu("/programy/nieznany.bur")==-1&&AppIcons::indeks_programu(nullptr)==-1);
        assert(AppIcons::indeks_programu("/programy/notatnik.cebula/notatnik.bu")==-1);
        // Okna: Notatnik (aktywny), Eksplorator (zminimalizowany), Ustawienia ekranu,
        // nieznany program, okno informacyjne Notatnika (BWS 101), osierocone okno informacyjne.
        auto okno=[](int i,uint64_t id,int pid,const char* t,bool akt,uint32_t stan){
            okna_taskbara[i]={};okna_taskbara[i].window_id=id;okna_taskbara[i].pid=pid;okna_taskbara[i].aktywne=akt;
            okna_taskbara[i].stan=stan;strcpy(okna_taskbara[i].tytul,t);};
        okno(0,0x100000005ULL,5,"Notatnik - Zażółć gęślą jaźń.txt",true,0);
        okno(1,0x100000006ULL,6,"Eksplorator Plików",false,GUI_OKNO_ZMINIMALIZOWANE);
        okno(2,0x100000007ULL,7,"Ustawienia ekranu",false,0);
        okno(3,0x100000008ULL,8,"Instalator",false,0);
        okno(4,0x100000009ULL,9,"O Notatniku",false,0);
        okno(5,0x10000000AULL,10,"Okno informacyjne",false,0);
        liczba_okien_taskbara=6;
        test_okna_aplikacji={{0x100000005ULL,SCIEZKA_NOTATNIK},{0x100000006ULL,SCIEZKA_EKSPLORATOR},
            {0x100000007ULL,AppIdentity::DisplaySettings},{0x100000009ULL,SCIEZKA_NOTATNIK}};   // dziecko -> wlasciciel
        przypisz_ikony_okien();
        assert(ikona_okna_taskbara[0]==iN&&ikona_okna_taskbara[1]==iE&&ikona_okna_taskbara[2]==iD);
        assert(ikona_okna_taskbara[3]==-1&&ikona_okna_taskbara[4]==iN&&ikona_okna_taskbara[5]==-1);
        // Zmiana tytulu (nowy dokument) nie zmienia ikony i nie wymaga ponownego zapytania.
        unsigned q=test_zapytania_aplikacji;
        strcpy(okna_taskbara[0].tytul,"Notatnik - Inny dokument.txt *");okna_taskbara[0].aktywne=false;przypisz_ikony_okien();
        assert(test_zapytania_aplikacji==q&&ikona_okna_taskbara[0]==iN);okna_taskbara[0].aktywne=true;
        // Ponowne uzycie pid (nowa generacja okna) przez inny program: nowa ikona.
        okna_taskbara[0].window_id=0x200000005ULL;test_okna_aplikacji[0]={0x200000005ULL,SCIEZKA_KALKULATOR};
        przypisz_ikony_okien();assert(test_zapytania_aplikacji>q&&ikona_okna_taskbara[0]==iK);
        okna_taskbara[0].window_id=0x100000005ULL;test_okna_aplikacji[0]={0x100000005ULL,SCIEZKA_NOTATNIK};
        przypisz_ikony_okien();assert(ikona_okna_taskbara[0]==iN);
        // Rysowanie: ikony z prawdziwych PNG, bez tytulu, stany aktywne/zminimalizowane.
        auto px=[](int x,int y){return test_surface[size_t(y)*1280+x]&0xFFFFFFU;};
        auto rysuj=[&](){test_surface.assign(1280*720,0xABCDEF);RysujTaskbar();};
        auto opaque_match=[&](int i)->double{   // udzial w pelni kryjacych pikseli ikony narysowanych 1:1
            int bx,by;assert(prostokat_przycisku_paska(i,&bx,&by));const auto* ik=ikona_paska(ikona_okna_taskbara[i]);assert(ik);
            const int bok=ik->bok(),ix=bx+(TASK_BUTTON_W-bok)/2,iy=by+(SKROT_H-bok)/2-ui_skaluj(1,ui_skala_procent);int all=0,ok=0;
            for(int y=0;y<bok;++y)for(int x=0;x<bok;++x){uint32_t c=ik->piksele()[y*bok+x];if((c>>24)!=255)continue;++all;ok+=px(ix+x,iy+y)==(c&0xFFFFFF);}
            return all?double(ok)/all:0.0;};
        for(const BursztynMotyw* theme:{&g_domyslny_motyw_fallback,(const BursztynMotyw*)nullptr}){
            static BursztynMotyw jasny=g_domyslny_motyw_fallback;
            if(!theme){jasny.panel=0xF2F2F2;jasny.podniesiony=0xFFFFFF;jasny.ramka=0xC8C8C8;jasny.tekst=0x202020;jasny.przycisk=0xE6E6E6;theme=&jasny;}
            g_aktywny_test_motyw=theme;rysuj();
            const uint32_t akcent=motyw_kolor(MOTYW_KOLOR_AKCENT)&0xFFFFFF,panel=motyw_kolor(MOTYW_KOLOR_PANEL)&0xFFFFFF;
            const uint32_t podn=motyw_kolor(MOTYW_KOLOR_PODNIESIONY)&0xFFFFFF,ramka=motyw_kolor(MOTYW_KOLOR_RAMKA)&0xFFFFFF;
            for(int i:{0,2,4})assert(opaque_match(i)>0.95);                      // ikony bez znieksztalcen
            int bx,by;
            assert(prostokat_przycisku_paska(0,&bx,&by));                         // aktywny: tlo + pomaranczowy pasek
            assert(px(bx+TASK_BUTTON_W/2,by+SKROT_H-1)==akcent&&px(bx+2,by+2)==podn);
            assert(prostokat_przycisku_paska(1,&bx,&by));                         // zminimalizowany: przygaszony + krotki pasek
            assert(px(bx+TASK_BUTTON_W/2,by+SKROT_H-1)==ramka&&px(bx+2,by+2)==panel&&opaque_match(1)<0.5);
            assert(prostokat_przycisku_paska(2,&bx,&by));                         // nieaktywny: bez paska i tla
            assert(px(bx+TASK_BUTTON_W/2,by+SKROT_H-1)==panel&&px(bx+2,by+2)==panel);
            assert(prostokat_przycisku_paska(3,&bx,&by));                         // ikona ogolna (belka w kolorze akcentu)
            {const int bok=ui_skaluj(24,ui_skala_procent),ix=bx+(TASK_BUTTON_W-bok)/2,iy=by+(SKROT_H-bok)/2-1;
             assert(px(ix+bok/2,iy+4)==akcent&&px(ix+bok/2,iy+bok/2+2)==(motyw_kolor(MOTYW_KOLOR_PRZYCISK)&0xFFFFFF));}
            // Nic poza paskiem zadan.
            for(int y=0;y<desktop_geometry.taskbar_y;y+=7)for(int x=0;x<1280;x+=7)assert(px(x,y)==0xABCDEF);
            for(const auto& r:test_batch_regions)assert(r.y>=desktop_geometry.taskbar_y&&r.y+r.h<=720&&r.x>=0&&r.x+r.w<=1280);
        }
        g_aktywny_test_motyw=&g_domyslny_motyw_fallback;
        // Brak pliku ikony: ikona ogolna zamiast zaslepki.
        {int bx,by;test_brak_ikony="/ikonki/app_snake.png";okno(2,0x10000000BULL,11,"Snake",false,0);
         test_okna_aplikacji.push_back({0x10000000BULL,SCIEZKA_SNAKE});przypisz_ikony_okien();assert(ikona_okna_taskbara[2]==iSn);
         assert(!ikona_paska(iSn));rysuj();assert(prostokat_przycisku_paska(2,&bx,&by));
         const int bok=ui_skaluj(24,ui_skala_procent),ix=bx+(TASK_BUTTON_W-bok)/2,iy=by+(SKROT_H-bok)/2-1;
         assert(px(ix+bok/2,iy+4)==(motyw_kolor(MOTYW_KOLOR_AKCENT)&0xFFFFFF));test_brak_ikony.clear();
         okno(2,0x100000007ULL,7,"Ustawienia ekranu",false,0);przypisz_ikony_okien();}
        // Hover: tylko przy zmianie przycisku; klik aktywuje/przywraca; prawy przycisk = znacznik "nad paskiem".
        {int bx,by;assert(prostokat_przycisku_paska(1,&bx,&by));
         assert(aktualizuj_hover_paska(bx+5,by+5)&&!aktualizuj_hover_paska(bx+6,by+6)&&hover_okna_paska==okna_taskbara[1].window_id);
         rysuj();assert(px(bx+2,by+2)==(motyw_kolor(MOTYW_KOLOR_PODNIESIONY)&0xFFFFFF));
         assert(aktualizuj_hover_paska(5,5)&&hover_okna_paska==0);
         test_activated_window=0;obsluz_klikniecie(bx+TASK_BUTTON_W/2,by+SKROT_H/2);assert(test_activated_window==okna_taskbara[1].window_id);
         assert(prostokat_przycisku_paska(2,&bx,&by));test_activated_window=0;obsluz_klikniecie(bx+1,by+1);
         assert(test_activated_window==okna_taskbara[2].window_id);
         assert(obsluz_prawy_klik_paska(bx+2,by+2)&&czy_nad_paskiem(okna_taskbara[2].window_id));
         rysuj();const int z=ui_skaluj(5,ui_skala_procent);assert(px(bx+TASK_BUTTON_W-z,by+3)==(motyw_kolor(MOTYW_KOLOR_AKCENT)&0xFFFFFF));
         assert(obsluz_prawy_klik_paska(bx+2,by+2)&&!czy_nad_paskiem(okna_taskbara[2].window_id));
         test_activated_window=0;obsluz_klikniecie(TASK_BUTTON_X-2,by+2);assert(test_activated_window==0);}
        // Zamkniecie okna: wpis znika, kolejne przesuwaja sie, bez starej ikony.
        okna_taskbara[1]=okna_taskbara[2];okna_taskbara[2]=okna_taskbara[3];liczba_okien_taskbara=3;przypisz_ikony_okien();
        assert(ikona_okna_taskbara[0]==iN&&ikona_okna_taskbara[1]==iD&&ikona_okna_taskbara[2]==-1&&ikona_okna_taskbara[3]==-1);
        {int bx,by;assert(!prostokat_przycisku_paska(3,&bx,&by));}
        // Wiele okien: przyciski rowno rozmieszczone, nie nachodza i nie wchodza na zegar.
        {liczba_okien_taskbara=SKLADACZ_MAKS_WARSTW;test_okna_aplikacji.clear();
         const char* progi[]={SCIEZKA_NOTATNIK,SCIEZKA_HUSSAR,SCIEZKA_SNAKE,SCIEZKA_PRZEGLADARKA_OBRAZOW,SCIEZKA_EKSPLORATOR};
         for(uint32_t i=0;i<liczba_okien_taskbara;++i){okno(int(i),0x500000000ULL+i,20+int(i),"Okno",i==3,0);test_okna_aplikacji.push_back({0x500000000ULL+i,progi[i%5]});}
         for(int szer:{1280,640}){screen_w=szer;metryki_ekranu();
          przypisz_ikony_okien();rysuj();int poprz=-1,widoczne=0;
          for(uint32_t i=0;i<liczba_okien_taskbara;++i){int bx,by;if(!prostokat_przycisku_paska(i,&bx,&by))continue;++widoczne;
             assert(bx+TASK_BUTTON_W<=poczatek_zegara()&&(poprz<0||bx==poprz+TASK_BUTTON_W+TASK_BUTTON_GAP));poprz=bx;}
          // 1280: wszystkie 16 ikon (tytuly zajmowaly ~9); 640: tylko mieszczace sie przed zegarem.
          if(szer==1280)assert(widoczne==int(liczba_okien_taskbara));else assert(widoczne>5&&widoczne<int(liczba_okien_taskbara));
          for(int y=0;y<desktop_geometry.taskbar_y;y+=11)for(int x=0;x<1280;x+=11)assert(px(x,y)==0xABCDEF);}
         screen_w=1280;metryki_ekranu();rysuj();
         assert(ikona_okna_taskbara[1]==iH&&ikona_okna_taskbara[2]==iSn&&ikona_okna_taskbara[3]==iO);
        }
        // Skala 125%: ikony przeskalowane z oryginalu, przycisk szerszy.
        ui_skala_procent=125;metryki_ekranu();liczba_okien_taskbara=2;przypisz_ikony_okien();rysuj();
        assert(ikona_paska(ikona_okna_taskbara[0])&&ikona_paska(ikona_okna_taskbara[0])->bok()==ui_skaluj(24,125));
        assert(TASK_BUTTON_W==ui_skaluj(44,125)&&opaque_match(0)>0.95);
        ui_skala_procent=100;metryki_ekranu();
        liczba_okien_taskbara=0;przypisz_ikony_okien();test_okna_aplikacji.clear();test_czytaj_ikony=false;
        test_surface_width=test_surface_height=0;test_surface.clear();hover_okna_paska=0;
    }
    printf("WM routing %u/%u PASS: ",checks,checks);puts("WM click routing passed: desktop, Start, launch, client PID, titlebar, close region, transparency, z-order, popup owner, theme and redraw");
}
