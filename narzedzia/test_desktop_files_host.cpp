#include "file_operations_host_fixture.h"
#define _start desktop_test_entry
#include "../menedzer_okien.cpp"
#undef _start

// Rendering/WM BWS calls are observed only; no guest storage or launcher runs.
void gui_ustaw_przejecie_myszy(bool) {}
void gui_ustaw_system_overlay(bool,int,int,int,int) {}
struct LabelPaint {uint32_t color;std::string text;};
static std::vector<LabelPaint> label_paints;
static bool capture_labels=false;
void rysuj_tekst_wysrodkowany(int,int,int,int,int,uint32_t color,const char* text) {if(capture_labels)label_paints.push_back({color,text});}
void gui_odswiez_pulpit() {}
int bws_utworz_warstwe(int,int,int,int,int) {return 1;}

#include "desktop_usability_cases.h"
#include "desktop_remaining_cases.h"
#include "desktop_selection_cases.h"
#include "desktop_launcher_edge_cases.h"
#include "desktop_persistence_cases.h"
#include "desktop_capacity_cases.h"

int main() {
    g_test_bws_hook=explorer_bws;
    files={{"/",true},{"/uzytkownicy",true},{SCIEZKA_PULPIT,true}};
    pulpit_menu=new PlikiUI::MenuPlikowe();pulpit_dialog=new PlikiUI::DialogPlikowy();
    screen_w=1280;screen_h=720;
    CHECK(otworz_pulpit_context(638,438,-1));
    CHECK(!pulpit_menu->menu.menu().element(0)->czy_wlaczony());
    CHECK(!pulpit_menu->menu.menu().element(2)->czy_wlaczony());
    bws_zdarzenie click{};click.typ=BWS_ZDARZENIE_MYSZ_DOWN;click.x=0;click.y=0;
    CHECK(obsluz_popupy_pulpitu(click));CHECK(!pulpit_context_open);CHECK(launches==0);
    click.typ=BWS_ZDARZENIE_MYSZ_UP;CHECK(obsluz_popupy_pulpitu(click));CHECK(launches==0);
    CHECK(!obsluz_popupy_pulpitu(click));
    CHECK(otworz_pulpit_modal(PulpitModalTyp::PLIK));zatwierdz_pulpit_modal();
    CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/Nowy plik.txt"));
    snprintf(pulpit_cel,sizeof(pulpit_cel),"%s/Nowy plik.txt",SCIEZKA_PULPIT);
    CHECK(otworz_pulpit_modal(PulpitModalTyp::REMOVE));zamknij_pulpit_modal();CHECK(deletes==0);CHECK(files.count(pulpit_cel));
    CHECK(otworz_pulpit_modal(PulpitModalTyp::OPEN_WITH));zatwierdz_pulpit_modal();CHECK(launched_file==pulpit_cel);
    CHECK(otworz_pulpit_modal(PulpitModalTyp::REMOVE));zatwierdz_pulpit_modal();CHECK(deletes==1);CHECK(!files.count(pulpit_cel));
    CHECK(otworz_pulpit_modal(PulpitModalTyp::FOLDER));
    static_cast<PoleTekstowe*>(pulpit_dialog->dziecko(2))->ustaw_tekst("zażółć");zatwierdz_pulpit_modal();
    CHECK(files[std::string(SCIEZKA_PULPIT)+"/zażółć"]);
    CHECK(otworz_pulpit_context(630,430,0));CHECK(pulpit_cel_folder);
    CHECK(!pulpit_menu->menu.menu().element(2)->czy_wlaczony());zamknij_pulpit_context();
    CHECK(otworz_pulpit_modal(PulpitModalTyp::RENAME));
    static_cast<PoleTekstowe*>(pulpit_dialog->dziecko(2))->ustaw_tekst("folder2");zatwierdz_pulpit_modal();
    CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/folder2"));
    files["/dest"]=true;snprintf(pulpit_cel,sizeof(pulpit_cel),"%s/folder2",SCIEZKA_PULPIT);
    CHECK(otworz_pulpit_modal(PulpitModalTyp::MOVE));
    CHECK(pulpit_wybor->przejdz_do_katalogu("/dest"));pulpit_wybor->zatwierdz();zatwierdz_pulpit_modal();
    CHECK(files.count("/dest/folder2"));CHECK(!files.count(pulpit_cel));
    for(auto operation:{BWS_SCHOWEK_COPY,BWS_SCHOWEK_CUT}) {
        CHECK(ustaw_schowek_plikow("/dest/folder2",operation));
        CHECK(otworz_pulpit_context(630,430,-1));CHECK(pulpit_menu->menu.menu().element(6)->czy_wlaczony());
        CHECK(!pulpit_menu->menu.menu().element(4)->czy_wlaczony());zamknij_pulpit_context();
    }
    files[std::string(SCIEZKA_PULPIT)+"/folder2"]=false;
    CHECK(otworz_pulpit_modal(PulpitModalTyp::PLIK));
    static_cast<PoleTekstowe*>(pulpit_dialog->dziecko(2))->ustaw_tekst("folder2");zatwierdz_pulpit_modal();
    CHECK(pulpit_dialog->czy_otwarta()); // failed create explains collision in a native dialog
    zamknij_pulpit_modal();
    zamknij_pulpit_modal();
    for(unsigned scale:{100U,125U}) {
        ui_skala_procent=scale;
        bws_zdarzenie right{};right.typ=BWS_ZDARZENIE_MYSZ_PRAWY_DOWN;right.x=620;right.y=300;
        CHECK(obsluz_prawy_klik_pulpitu(right));CHECK(pulpit_context_open);CHECK(zaznaczony_wpis_pulpitu==-1);
        for(size_t i=0;i<pulpit_menu->menu.menu().liczba_elementow();++i) {
            auto* a=pulpit_menu->menu.menu().element(i);auto id=static_cast<PlikiUI::Akcja>(a->identyfikator());
            if(id==PlikiUI::Akcja::NEW_FOLDER || id==PlikiUI::Akcja::NEW_FILE)CHECK(a->czy_wlaczony());
            if(id==PlikiUI::Akcja::COPY || id==PlikiUI::Akcja::CUT || id==PlikiUI::Akcja::REMOVE || id==PlikiUI::Akcja::OPEN_WITH || id==PlikiUI::Akcja::NOTEPAD)CHECK(!a->czy_wlaczony());
            if(id==PlikiUI::Akcja::REMOVE)CHECK(!strcmp(a->tekst(),"Usuń"));
        }
        zamknij_pulpit_context();
        for(bool folder:{false,true}) {
            files[std::string(SCIEZKA_PULPIT)+"/target.txt"]=folder;hash_pulpitu_wazny=false;odswiez_wpisy_pulpitu();
            int target=-1;for(int i=0;i<liczba_wpisow_pulpitu;++i)if(!strcmp(wpisy_pulpitu[i].nazwa,"target.txt"))target=i;
            CHECK(target>=0);prostokat_wpisu_pulpitu(target,&right.x,&right.y);++right.x;++right.y;
            CHECK(obsluz_prawy_klik_pulpitu(right));CHECK(zaznaczony_wpis_pulpitu==target);
            CHECK(pulpit_menu->menu.menu().element(4)->czy_wlaczony());CHECK(pulpit_menu->menu.menu().element(5)->czy_wlaczony());
            CHECK(pulpit_menu->menu.menu().element(8)->czy_wlaczony());CHECK(pulpit_menu->menu.menu().element(1)->czy_wlaczony()==!folder);
            auto t=*gui_pobierz_motyw();t.panel^=0xffffff;test_ustaw_aktywny_motyw(&t);pulpit_menu->events.popups.zaktualizuj_motyw();CHECK(pulpit_menu->menu.czy_otwarte());
            click.typ=BWS_ZDARZENIE_MYSZ_DOWN;click.x=0;click.y=0;int before=launches;CHECK(obsluz_popupy_pulpitu(click));
            click.typ=BWS_ZDARZENIE_MYSZ_UP;CHECK(obsluz_popupy_pulpitu(click));CHECK(launches==before);
            test_ustaw_aktywny_motyw(nullptr);
        }
    }
    // Failed directory move opens one notice; idle timers do not republish it.
    snprintf(pulpit_cel,sizeof(pulpit_cel),"%s/target.txt",SCIEZKA_PULPIT);
    files[pulpit_cel]=true;CHECK(otworz_pulpit_modal(PulpitModalTyp::MOVE));CHECK(pulpit_wybor->przejdz_do_katalogu("/dest"));
    deny_operation=true;zatwierdz_pulpit_modal();deny_operation=false;
    CHECK(pulpit_modal_typ==PulpitModalTyp::NOTICE);CHECK(files.count(pulpit_cel));CHECK(!files.count("/dest/target.txt"));
    bws_zdarzenie timer{};timer.typ=BWS_ZDARZENIE_TIMER;obsluz_popupy_pulpitu(timer);
    int blits=test_popup_blits,refresh=test_refreshes;
    for(int i=0;i<100;++i)CHECK(obsluz_popupy_pulpitu(timer));
    CHECK(test_popup_blits==blits);CHECK(test_refreshes==refresh);zatwierdz_pulpit_modal();
    CHECK(test_refreshes==refresh);CHECK(files.count(pulpit_cel));
    auto base=*gui_pobierz_motyw();
    for(bool folder:{false,true}) {
        files[pulpit_cel]=folder;hash_pulpitu_wazny=false;odswiez_wpisy_pulpitu();
        int selected=-1;for(int i=0;i<liczba_wpisow_pulpitu;++i)if(!strcmp(wpisy_pulpitu[i].nazwa,"target.txt"))selected=i;
        CHECK(selected>=0);zaznaczony_wpis_pulpitu=selected;desktop_layout.click(selected,false);
        for(uint32_t color:{0x112233U,0xabcdefU,0x456789U}) {
            auto theme=base;theme.zaznaczenie=color;theme.akcent=color^0x555555;test_ustaw_aktywny_motyw(&theme);
            test_paint_rects.clear();test_capture_paints=true;RysujWpisyPulpitu();test_capture_paints=false;
            bool background=false,border=false;for(auto r:test_paint_rects){background|=r.color==motyw_pulpit_zaznaczenie(theme);border|=r.color==motyw_pulpit_ramka(theme);}
            CHECK(background);CHECK(border);CHECK(zaznaczony_wpis_pulpitu==selected);CHECK(wpisy_pulpitu[selected].folder==folder);
        }
    }
    test_ustaw_aktywny_motyw(nullptr);
    desktop_usability_cases();
    desktop_remaining_cases();
    desktop_selection_cases();
    desktop_launcher_edge_cases();
    desktop_persistence_cases();
    desktop_capacity_cases();
    delete pulpit_menu;delete pulpit_dialog;delete pulpit_wybor;
    printf("Desktop production file actions: %d/%d PASS (RAM filesystem)\n",checks,checks);
}
