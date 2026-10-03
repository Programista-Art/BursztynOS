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

static std::vector<bws_zdarzenie> events;
static size_t event_index;
static std::vector<uint32_t> submitted;
static Prostokat submitted_rect;
static std::string copied;
bool gui_czekaj_na_zdarzenie(bws_zdarzenie* e) {
    assert(event_index<events.size());*e=events[event_index++];return true;
}
void gui_odswiez() {}
uint32_t motyw_kolor(uint32_t id) { return id==MOTYW_KOLOR_OKNOTLO?0x102030:0xeeddcc; }
uint16_t motyw_promien(uint32_t) { return 0; }
uint8_t motyw_grubosc_ramki() { return 1; }
void motyw_odswiez_lokalny() {}
const BursztynMotyw* gui_pobierz_motyw() { return &g_domyslny_motyw_fallback; }
void gui_rysuj_prostokat(int,int,int,int,uint32_t) { assert(false); }
void gui_wypisz_tekst_kolor_skala(int,int,uint32_t,int,const char*) { assert(false); }
void gui_rysuj_standardowa_belke(int,int,int,const char*,bool) {}
bool gui_blit(int x,int y,int w,int h,const uint32_t* data) {
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
bool czytaj_plik(const char*,char*,uint32_t) { assert(false);return false; }
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
    assert(g_liczba_linii==9);
    assert(!strcmp(shell_line(0).tekst,"OS: Bursztyn OS x86_64"));
    assert(!strcmp(shell_line(1).tekst,"Autor: Dymitr Wygowski"));
    assert(!strcmp(shell_line(2).tekst,"E-mail: programista.art@gmail.com"));
    assert(!strncmp(shell_line(3).tekst,"Jądro:",7));assert(!strncmp(shell_line(4).tekst,"Procesy:",8));
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
    puts("Shell editor, UTF-8, history, clipping, wrapping, ring, selection, clipboard and scroll checks passed (100%, 125%).");
}
