#include "file_operations_host_fixture.h"
#define _start desktop_test_entry
#include "../menedzer_okien.cpp"
#undef _start

// Rendering/WM BWS calls are observed only; no guest storage or launcher runs.
void gui_ustaw_przejecie_myszy(bool) {}
void gui_ustaw_system_overlay(bool,int,int,int,int) {}
void rysuj_tekst_wysrodkowany(int,int,int,int,int,uint32_t,const char*) {}
void gui_odswiez_pulpit() {}
uint32_t gui_pobierz_okna(GuiOknoInfo*,uint32_t) {return 0;}
bool gui_aktywuj_okno(uint64_t) {return true;}
int bws_utworz_warstwe(int,int,int,int,int) {return 1;}

int main() {
    g_test_bws_hook=explorer_bws;
    files={{"/",true},{"/uzytkownicy",true},{SCIEZKA_PULPIT,true}};
    pulpit_menu=new PlikiUI::MenuPlikowe();pulpit_dialog=new PlikiUI::DialogPlikowy();
    screen_w=640;screen_h=480;
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
    static_cast<PoleTekstowe*>(pulpit_dialog->dziecko(2))->ustaw_tekst("/dest");zatwierdz_pulpit_modal();
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
    delete pulpit_menu;delete pulpit_dialog;
    printf("Desktop production file actions: %d/%d PASS (RAM filesystem)\n",checks,checks);
}
