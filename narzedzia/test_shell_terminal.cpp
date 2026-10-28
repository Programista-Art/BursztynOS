// Host regression checks execute the production editor and viewport with a mocked BWS surface.
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#define _start shell_test_entry
#include "../shell.cpp"
#undef _start

static unsigned checks=0;
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
static std::vector<bws_zdarzenie> events;
static size_t event_index;
static std::vector<uint32_t> submitted;
static Prostokat submitted_rect;
static std::string copied;
static std::string sponsor_file;
static bool sponsor_exists=true, popup_open=false;
static Prostokat popup_rect;
static uint32_t theme_background=0x102030;
static unsigned popup_paints=0,client_paints=0,refreshes=0;
static bool expect_live_theme=false;
static std::vector<uint32_t> popup_pixels;
uint8_t motyw_cien_okna() { return 1; }
bool gui_ustaw_popup_aplikacji(bool open,int x,int y,int w,int h) { popup_open=open;popup_rect=Prostokat(x,y,w,h);return true; }
bool gui_blit_popup(int,int,int w,int h,const uint32_t* data) { assert(w>0 && h>0 && data);++popup_paints;popup_pixels.assign(data,data+w*h);return true; }
bool clipboard_size(const char*,uint64_t* n) { *n=copied.size()+1;return true; }
bool clipboard_read(const char*,void* out,uint64_t capacity,uint64_t* n) { if(capacity<copied.size()+1) return false;*n=copied.size()+1;memcpy(out,copied.c_str(),*n);return true; }
bool pobierz_rozmiar_pliku(const char* path,uint32_t* n) { assert(!strcmp(path,SHELL_SPONSOR_PATH));*n=sponsor_file.size();return sponsor_exists; }
bool gui_czekaj_na_zdarzenie(bws_zdarzenie* e) {
    assert(event_index<events.size());
    if(expect_live_theme && events[event_index].typ==BWS_ZDARZENIE_BLUR) {
        assert(popup_open && !popup_pixels.empty());
        assert(popup_paints>0);expect_live_theme=false;
    }
    *e=events[event_index++];return true;
}
void gui_odswiez() {++refreshes;}
uint32_t motyw_kolor(uint32_t id) { return id==MOTYW_KOLOR_OKNOTLO?theme_background:(0xeeddcc ^ (theme_background ^ 0x102030)); }
uint16_t motyw_promien(uint32_t) { return 0; }
uint8_t motyw_grubosc_ramki() { return 1; }
void motyw_odswiez_lokalny() {}
const BursztynMotyw* gui_pobierz_motyw() { return &g_domyslny_motyw_fallback; }
void gui_rysuj_prostokat(int,int,int,int,uint32_t) { assert(false); }
void gui_wypisz_tekst_kolor_skala(int,int,uint32_t,int,const char*) { assert(false); }
void gui_rysuj_standardowa_belke(int,int,int,const char*,bool) {}
bool gui_blit(int x,int y,int w,int h,const uint32_t* data) {
    ++client_paints;
    submitted_rect=Prostokat(x,y,w,h);submitted.assign(data,data+w*h);return true;
}
bool clipboard_set(const char* mime,const void* data,uint64_t n) {
    assert(!strcmp(mime,"text/plain"));assert(n);copied=static_cast<const char*>(data);return true;
}
void gui_ustaw_capture_myszy(bool) {}
bool gui_ustaw_geometrie_okna(int,int,int,int,int) { return true; }
void bws_przesun_warstwe(int,int) {}
bool gui_minimalizuj_okno() { return true; }
gui_akcja_belki gui_hit_test_belki(int,int,int,int,int) { return GUI_BELKA_BRAK; }
bool gui_pobierz_monitor(uint32_t,GuiMonitorInfo*) { return false; }
void gui_pobierz_rozdzielczosc(int* w,int* h) { *w=1280;*h=720; }
extern "C" void gui_zakoncz_aplikacje() { std::abort(); }
uint64_t bws_wywolaj(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t) { assert(false);return 0; }
bool utworz(const char*) { assert(false);return false; }
bool zapisz_plik(const char*,const char*,uint32_t) { assert(false);return false; }
bool czytaj_plik(const char* path,char* out,uint32_t n) { assert(!strcmp(path,SHELL_SPONSOR_PATH));assert(sponsor_file.size()<=n);memcpy(out,sponsor_file.data(),sponsor_file.size());return sponsor_exists; }
void bws_dzwiek_test(uint32_t,uint32_t) { assert(false); }
bool bws_siec_dns(const char*,uint8_t*) { assert(false);return false; }
bool bws_siec_pobierz_http(uint8_t*,const char*,const char*,char*,uint32_t) { assert(false);return false; }

static void bytes(const char* text) {
    for(size_t i=0;text[i];++i) { bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ;e.kod=static_cast<unsigned char>(text[i]);events.push_back(e); }
}
static void key(uint32_t usage,const char* ansi) {
    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;e.x=1;e.kod=BWS_KEY_SOURCE_USB|usage;events.push_back(e);bytes(ansi);
}
static void reset() {
    events.clear();event_index=0;g_liczba_linii=1;g_pierwsza_linia=0;g_scroll_linia=0;g_auto_scroll=true;
    g_linie[0]={};g_output_selection=false;historia_ilosc=0;g_rows_dirty=true;
}
int main() {
    g_linie=new ShellLine[MAXSCROLLBACKLINES]{};g_pasek_przewijania=new PasekPrzewijania();
    char input[SHELL_INPUT_CAPACITY];
    // Execute the production first client paint with two resolved theme palettes.
    for(unsigned scale:{100U,125U}) for(uint32_t background:{0x00191C20U,0x00EDF1F5U}) {
        ui_skala_procent=scale;shell_w=ui_skaluj(700,scale);shell_h=ui_skaluj(450,scale);
        theme_background=background;reset();submitted.clear();unsigned before=refreshes;
        shell_rysuj_terminal();
        assert(refreshes==before+1 && !submitted.empty());
        assert(submitted_rect.x==shell_obszar_klienta().x && submitted_rect.y==shell_obszar_klienta().y);
        assert(submitted.front()==background);
        assert(background!=0x001A0B00U && background!=0x00280F00U);
    }
    theme_background=0x102030;shell_w=700;shell_h=450;
    // Feed the real blocking event loop 250 ticks and 100 irrelevant outside
    // moves, then return one key without editor-side repaint.
    for(unsigned scale:{100U,125U}) {
        ui_skala_procent=scale;reset();
        bws_zdarzenie idle{};idle.typ=BWS_ZDARZENIE_TIMER;
        for(int i=0;i<250;++i)events.push_back(idle);
        idle.typ=BWS_ZDARZENIE_MYSZ_RUCH;idle.x=1919;idle.y=0;
        for(int i=0;i<100;++i)events.push_back(idle);
        idle.typ=BWS_ZDARZENIE_KLAWISZ;idle.kod='a';events.push_back(idle);
        unsigned paints_before=client_paints,refresh_before=refreshes,popup_before=popup_paints;
        int sx=shell_x,sy=shell_y,sw=shell_w,sh=shell_h;
        assert(pobierz_znak_blokujaco()=='a');
        assert(client_paints==paints_before&&refreshes==refresh_before&&popup_paints==popup_before);
        assert(shell_x==sx&&shell_y==sy&&shell_w==sw&&shell_h==sh);
    }
    ui_skala_procent=100;
    reset();bytes("\n\n \t\nabcąę");key(80,"\x1b[D");bytes("X");bytes("\b");key(80,"\x1b[D");key(76,"\x1b[3~");bytes("\n");
    assert(pobierz_linie(input,sizeof(input)));assert(!strcmp(input,"abcę"));assert(g_liczba_linii==2);assert(historia_ilosc==0);
    reset();dodaj_do_historii("pisz stary");dodaj_do_historii("system");bytes("roboczeą");key(82,"\x1b[A");key(82,"\x1b[A");key(81,"\x1b[B");key(81,"\x1b[B");bytes("\n");
    assert(pobierz_linie(input,sizeof(input)));assert(!strcmp(input,"roboczeą"));
    reset();bytes("zażółć");
    bws_zdarzenie mouse{};mouse.typ=BWS_ZDARZENIE_MYSZ_DOWN;
    mouse.x=shell_edit_rect().x+CzcionkaUI::zmierz_tekst("za",ui_skala());mouse.y=shell_input_rect().y+8;events.push_back(mouse);
    mouse.typ=BWS_ZDARZENIE_MYSZ_RUCH;mouse.x=shell_edit_rect().x+CzcionkaUI::zmierz_tekst("zażółć",ui_skala())+1;events.push_back(mouse);
    mouse.typ=BWS_ZDARZENIE_MYSZ_UP;events.push_back(mouse);
    bws_zdarzenie copy{};copy.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;copy.x=1;copy.kod=BWS_KEY_SOURCE_USB|6;copy.przyciski=BWS_MOD_CTRL;events.push_back(copy);bytes("c");
    copy.x=0;copy.przyciski=0;events.push_back(copy);bytes("Q\n");
    assert(pobierz_linie(input,sizeof(input)));assert(!strcmp(input,"zaQ"));assert(copied=="żółć");
    reset();char system_command[]="system";Argumenty arguments{};arguments.dane[0]=system_command;arguments.liczba=1;wykonaj_polecenie(arguments);
    assert(g_liczba_linii==int(SystemIdentity::PropertyCount)+2);
    assert(!strcmp(shell_line(0).tekst,"OS: " BURSZTYN_NAZWA_WERSJI));
    for(size_t i=0;i<SystemIdentity::PropertyCount;++i){
        std::string expected=std::string(SystemIdentity::Properties[i].label)+": "+SystemIdentity::Properties[i].value;
        assert(expected==shell_line(i+1).tekst);
    }
    for(uint32_t scale:{100U,125U}) {
        reset();ui_skala_procent=scale;shell_w=ui_skaluj(700,scale);shell_h=ui_skaluj(450,scale);
        memset(input,'W',sizeof(input)-1);input[sizeof(input)-1]=0;g_obecny_bufor_wejscia=input;g_caret=g_input_anchor=strlen(input);
        shell_odswiez_linie_wejsciowa();assert(g_input_scroll_px>0);
        assert(submitted_rect.prawy()<=shell_scrollbar_rect().x);
        assert(shell_edit_rect().x+shell_prefix_width(input,g_caret)-g_input_scroll_px<shell_edit_rect().prawy());
        strcpy(input,"krótko");g_caret=g_input_anchor=strlen(input);shell_odswiez_linie_wejsciowa();assert(g_input_scroll_px==0);
        strcpy(input,"zażółć");g_caret=strlen(input);g_input_anchor=2;shell_delete_input_selection(input);assert(!strcmp(input,"za"));
        strcpy(input,"zażółć");g_caret=strlen(input);g_input_anchor=2;shell_copy_selection();assert(copied=="żółć");
        g_obecny_bufor_wejscia=nullptr;g_caret=g_input_anchor=0;
        char longline[2048];memset(longline,'W',sizeof(longline)-1);longline[2047]=0;shell_wypisz(longline);
        assert(g_liczba_linii==1);assert(g_total_rows>1);assert(shell_line(0).len==2047);
        for(int i=0;i<600;++i) shell_wypisz("\nwiersz ąęż");
        assert(g_liczba_linii==512);
        for(int i=0;i<100;++i) { shell_set_scroll(i%2?-999:99999);shell_rysuj_terminal();assert(g_scroll_linia>=0 && g_scroll_linia<=shell_max_scroll());assert(g_visible_count>0);assert(g_pasek_przewijania->wartosc()==g_scroll_linia); }
        shell_set_scroll(0);int old=g_scroll_linia;shell_wypisz("\nnowe");assert(g_scroll_linia==old && !g_auto_scroll);
        reset();shell_wypisz_wiersz_pomocy("    zmiennazwe <ścieżka> <nazwa> <długi-dodatkowy-argument>","Zmienia nazwę pliku lub katalogu.");
        assert(shell_line(1).opis_x>0);assert(shell_line(g_liczba_linii-1).len==0);shell_wypisz("powłoka> system\n");assert(shell_line(1).opis[0]);
        g_output_selection=true;g_selection_anchor={0,0};g_selection_end={1,strlen(shell_line(1).opis)+1};shell_copy_selection();assert(copied.find("Zmienia nazwę")!=std::string::npos);
    }
    for(int scale:{100,125}) {
        ui_skala_procent=scale;shell_screen_w=1280;shell_screen_h=720;reset();g_obecny_bufor_wejscia=input;g_input_capacity=sizeof(input);
        strcpy(input,"Zażółć gęślą jaźń");g_input_anchor=0;g_caret=strlen(input);
        shell_context_open(shell_input_rect().x+5,shell_input_rect().y+5);
        int longest=0;
        for(size_t i=0;i<g_context_menu->menu().liczba_elementow();++i) {
            int width=CzcionkaUI::zmierz_tekst(g_context_menu->menu().element(i)->tekst(),ui_skala());
            if(width>longest) longest=width;
        }
        // Native popup includes a 2 logical-pixel shadow; scale it before rounding.
        assert(popup_rect.szerokosc==longest+2*ui_skala()+16*ui_skala()+2*ui_skala());
        assert(popup_open);g_context_menu->menu().element(0)->wykonaj();assert(copied==input);
        g_context_menu->menu().element(3)->wykonaj();assert(!input[0]);
        g_context_menu->menu().element(2)->wykonaj();assert(!strcmp(input,"Zażółć gęślą jaźń"));
        strcpy(input,"abcd");g_caret=g_input_anchor=2;copied="ą";shell_context_paste();assert(!strcmp(input,"abącd"));
        g_input_anchor=2;g_caret=4;copied="ę";shell_context_paste();assert(!strcmp(input,"abęcd"));
        g_context_menu->menu().element(4)->wykonaj(); // collapsed selection: no deletion
        g_input_anchor=2;g_caret=4;g_context_menu->menu().element(4)->wykonaj();assert(!strcmp(input,"abcd"));
        g_context_menu->menu().element(5)->wykonaj();assert(!input[0]);
        strcpy(input,"ASCII");g_caret=5;g_input_anchor=0;assert(shell_copy_selection());assert(copied=="ASCII");
        memset(input,'a',sizeof(input)-2);input[sizeof(input)-2]=0;g_caret=g_input_anchor=strlen(input);copied="ż";shell_context_paste();assert(strlen(input)==sizeof(input)-2);
        input[0]=0;g_caret=g_input_anchor=0;copied=std::string(4096,'a');shell_context_paste();assert(strlen(input)==2047);
        input[0]=0;g_caret=g_input_anchor=0;copied="Aliasą";
        shell_context_open(shell_input_rect().x+5,shell_input_rect().y+5);
        auto* clicked_view=static_cast<WidokMenuPopup*>(g_context_popups->fokus_popupu());
        Prostokat clicked=clicked_view->prostokat_elementu_klienta(1);
        bws_zdarzenie click{};click.typ=BWS_ZDARZENIE_MYSZ_DOWN;click.x=clicked.x+5;click.y=clicked.y+5;
        assert(shell_context_event(click));click.typ=BWS_ZDARZENIE_MYSZ_UP;assert(shell_context_event(click));
        assert(!popup_open && !strcmp(input,"Aliasą"));
        input[0]=0;g_caret=g_input_anchor=0;
        shell_context_open(shell_input_rect().x+5,shell_input_rect().y+5);
        bws_zdarzenie enter{};enter.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;enter.x=1;enter.kod=BWS_KEY_SOURCE_USB|40;
        bws_zdarzenie down=enter;down.kod=BWS_KEY_SOURCE_USB|81;assert(shell_context_event(down));
        assert(shell_context_event(enter));assert(!popup_open && !strcmp(input,"Aliasą"));
        enter.typ=BWS_ZDARZENIE_KLAWISZ;enter.kod='\n';assert(shell_context_event(enter));
        for(auto point:std::vector<Punkt>{{0,0},{1279,0},{0,719},{1279,719},{500,350}}) {
            shell_context_open(point.x,point.y);assert(popup_open);
            assert(popup_rect.x>=0 && popup_rect.y>=0 && popup_rect.prawy()<=1280 && popup_rect.dolny()<=720);
            auto* view=static_cast<WidokMenuPopup*>(g_context_popups->fokus_popupu());
            assert(popup_rect.wysokosc==view->wymagany_rozmiar(ui_skala()).wysokosc+2*ui_skala());
            for(int i=0;i<6;++i) { Prostokat r=view->prostokat_elementu_klienta(i);assert(r.szerokosc>0 && r.wysokosc>0); }
            bws_zdarzenie outside{};outside.typ=BWS_ZDARZENIE_MYSZ_DOWN;outside.x=-1;outside.y=-1;
            assert(shell_context_event(outside));assert(!popup_open);outside.typ=BWS_ZDARZENIE_MYSZ_UP;assert(shell_context_event(outside));
            // Dismissal consumes this DOWN/UP pair only; subsequent clicks route normally.
            outside.typ=BWS_ZDARZENIE_MYSZ_DOWN;assert(!shell_context_event(outside));
        }
        reset();shell_wypisz("Nie usuwaj ąę\n");g_output_selection=true;g_selection_anchor={0,0};g_selection_end={0,strlen(shell_line(0).tekst)};
        shell_context_open(shell_output_rect().x+5,shell_output_rect().y+5);
        for(int i=1;i<6;++i) { assert(!g_context_menu->menu().element(i)->czy_wlaczony());g_context_menu->menu().element(i)->wykonaj(); }
        auto* readonly_view=static_cast<WidokMenuPopup*>(g_context_popups->fokus_popupu());
        Prostokat disabled=readonly_view->prostokat_elementu_klienta(3);
        click.typ=BWS_ZDARZENIE_MYSZ_DOWN;click.x=disabled.x+5;click.y=disabled.y+5;assert(shell_context_event(click));
        click.typ=BWS_ZDARZENIE_MYSZ_UP;assert(shell_context_event(click));assert(popup_open);
        assert(!strcmp(shell_line(0).tekst,"Nie usuwaj ąę"));g_context_menu->menu().element(0)->wykonaj();assert(copied=="Nie usuwaj ąę");
        bws_zdarzenie escape{};escape.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;escape.x=1;escape.kod=BWS_KEY_SOURCE_USB|41;
        assert(shell_context_event(escape));assert(!popup_open);
        escape.typ=BWS_ZDARZENIE_KLAWISZ;escape.kod=27;assert(shell_context_event(escape));
        shell_context_open(500,350);escape.typ=BWS_ZDARZENIE_BLUR;shell_context_event(escape);assert(!popup_open);
        reset();sponsor_file="Jan Kowalski\r\n\nNick123\nFundacja Przykład\n";shell_sponsors();
        assert(!strcmp(shell_line(0).tekst,"Sponsorzy projektu"));assert(!strcmp(shell_line(3).tekst,"Fundacja Przykład"));
        reset();sponsor_exists=false;shell_sponsors();assert(!strcmp(shell_line(0).tekst,"Brak pliku z listą sponsorów projektu."));sponsor_exists=true;
        for(const char* empty:{"","\n\r\n"}) { reset();sponsor_file=empty;shell_sponsors();assert(!strcmp(shell_line(0).tekst,"Brak sponsorów do wyświetlenia.")); }
        reset();sponsor_file=std::string(4096,'a');shell_sponsors();assert(!strcmp(shell_line(0).tekst,"Wiersz listy sponsorów jest zbyt długi."));
        reset();sponsor_file=std::string(16385,'a');shell_sponsors();assert(!strcmp(shell_line(0).tekst,"Plik sponsorów jest zbyt duży."));
        reset();sponsor_file=std::string(3000,'W')+"\n";shell_sponsors();assert(g_total_rows>2);
        reset();sponsor_file="";for(int i=0;i<100;++i) sponsor_file+="Sponsor ąęż\n";shell_sponsors();assert(shell_max_scroll()>0);
        reset();char version_command[]="wersja";arguments.dane[0]=version_command;wykonaj_polecenie(arguments);assert(!strcmp(shell_line(0).tekst,BURSZTYN_NAZWA_WERSJI));
    }
    reset();sponsor_file="Test wsparcia ąę\n";
    char support_command[]="wsparcie";Argumenty support_args{};
    assert(parse_argumenty(support_command,&support_args));wykonaj_polecenie(support_args);
    assert(!strcmp(shell_line(1).tekst,"Test wsparcia ąę"));
    reset();wypisz_pomoc();bool support_help=false;
    for(int i=0;i<g_liczba_linii;++i) {
        assert(!strstr(shell_line(i).tekst,"sponsorzy"));
        if(!strcmp(shell_line(i).tekst,"wsparcie")) {
            assert(!strcmp(shell_line(i).opis,"Wyświetla listę wspierających Bursztyn OS."));support_help=true;
        }
    }
    assert(support_help);
    for(uint32_t scale:{100U,125U}) {
        reset();ui_skala_procent=scale;shell_w=ui_skaluj(700,scale);shell_h=ui_skaluj(450,scale);
        for(int i=0;i<70;++i) shell_wypisz("wiersz ąę\n");
        shell_set_scroll(3);dodaj_do_historii("pisz historia");
        memset(input,'W',sizeof(input)-1);input[sizeof(input)-1]=0;
        g_obecny_bufor_wejscia=input;g_input_capacity=sizeof(input);g_caret=strlen(input);g_input_anchor=2;
        shell_rysuj_terminal();
        g_output_selection=true;g_selection_anchor={4,0};g_selection_end={5,3};
        const int scroll=g_scroll_linia,hscroll=g_input_scroll_px,lines=g_liczba_linii;
        ShellLine* buffer=g_linie;std::string text=input,clip=copied;
        shell_context_open(shell_input_rect().x+5,shell_input_rect().y+5);
        auto before=submitted,menu_before=popup_pixels;unsigned paints=popup_paints;
        theme_background=theme_background==0x102030?0x405060:0x102030;
        bws_zdarzenie theme{};theme.typ=BWS_ZDARZENIE_MOTYW_ZMIENIONY;
        events.push_back(theme);expect_live_theme=true;
        bws_zdarzenie blur{};blur.typ=BWS_ZDARZENIE_BLUR;events.push_back(blur);bytes("x");assert(pobierz_znak_blokujaco()=='x');
        assert(submitted!=before && popup_pixels!=menu_before && popup_paints>paints && !expect_live_theme);
        assert(submitted_rect.x==shell_obszar_klienta().x && submitted_rect.y==shell_obszar_klienta().y);
        assert(g_linie==buffer && g_liczba_linii==lines && text==input && copied==clip);
        assert(g_scroll_linia==scroll && g_input_scroll_px==hscroll && !g_auto_scroll);
        assert(g_caret==text.size() && g_input_anchor==2 && historia_ilosc==1);
        assert(g_output_selection && g_selection_anchor.line==4 && g_selection_end.line==5);
        assert(ui_skala_procent==scale);shell_context_close();
    }
    for(uint32_t scale:{100U,125U}) {
        Menu menu;auto& item=menu.dodaj_element("Zażółć",1);
        WidokMenuPopup view;view.ustaw(&menu,nullptr);SkalaUI s=SkalaUI::procent(scale);
        const int text=CzcionkaUI::zmierz_tekst(item.tekst(),s);
        assert(view.tekst_x(s)==1*s+8*s);
        assert(view.wymagany_rozmiar(s).szerokosc==2*s+16*s+text);
        item.ustaw_ikone(1).ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole).ustaw_skrot("Ctrl+C");
        assert(view.tekst_x(s)==1*s+8*s+18*s+20*s);
        assert(view.wymagany_rozmiar(s).szerokosc==2*s+16*s+18*s+20*s+text+12*s+CzcionkaUI::zmierz_tekst("Ctrl+C",s));
        item.ustaw_widoczny(false);
        assert(view.tekst_x(s)==1*s+8*s);
    }
    reset();sponsor_file=std::string(SHELL_SPONSOR_LINE_LIMIT,'a')+"\r\n";shell_sponsors();
    assert(!strcmp(shell_line(0).tekst,"Sponsorzy projektu"));
    for(const std::string& invalid:std::vector<std::string>{"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", std::string("A\0B",3)}) {
        reset();sponsor_file=invalid;shell_sponsors();assert(!strcmp(shell_line(0).tekst,"Nieprawidłowy tekst UTF-8 w pliku sponsorów."));
    }
    reset();sponsor_file=std::string(SHELL_SPONSOR_LIMIT,'\n');shell_sponsors();assert(!strcmp(shell_line(0).tekst,"Brak sponsorów do wyświetlenia."));
    strcpy(input,"abc");g_obecny_bufor_wejscia=input;g_context_input=true;g_input_capacity=sizeof(input);
    g_caret=SIZE_MAX;g_input_anchor=0;copied="x";shell_context_paste();assert(!strcmp(input,"abc"));assert(!shell_copy_selection());
    shell_delete_range(input,2,SIZE_MAX);assert(!strcmp(input,"abc"));g_caret=g_input_anchor=3;
    shell_context_open(500,350);bws_zdarzenie close{};close.typ=BWS_ZDARZENIE_ZAMKNIJ;shell_context_event(close);assert(!popup_open);
    shell_context_open(500,350);close.typ=BWS_ZDARZENIE_KLAWISZ;close.kod=27;assert(shell_context_event(close));assert(!popup_open);
    printf("Shell host: %u/%u PASS\n",checks,checks);
    puts("Host checks passed: editor, popup, clipboard, sponsors and version (100%, 125%); not OS runtime validation.");
}
