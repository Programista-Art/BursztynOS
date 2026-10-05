// Reuse the established BursztynUI mock environment; exercise real Explorer
// code and shared desktop popup policy. All filesystem writes are in RAM.
#include "bursztyn_ui_host_fixture.h"
#include <map>
#include <string>
#define AKTOWKA_HOST_TEST 1
#include "../programy/eksplorator/eksplorator-plikow.cpp"

#include "file_operations_host_fixture.h"

static void select_name(AktowkaModel& m,const char* name) {
    for(int i=0;i<m.visible_count();++i)if(!strcmp(m.visible_entry(i)->name,name)) {
        bool sc=false;m.select_visible(i,5,&sc);return;
    }
    CHECK(false);
}
static bws_zdarzenie event(uint32_t type,int x=0,int y=0,uint32_t key=0) {
    bws_zdarzenie e{};e.typ=type;e.x=x;e.y=y;e.kod=key;return e;
}
static void input(PlikiUI::DialogPlikowy& d,const char* text) {
    for(const unsigned char* c=reinterpret_cast<const unsigned char*>(text);*c;++c)d.zdarzenie(event(BWS_ZDARZENIE_KLAWISZ,0,0,*c));
}
#include "explorer_regression_cases.h"
#include "text_context_cases.h"
#include "explorer_launcher_cases.h"
static void remaining_regressions() {
    files={{"/",true},{"/test",true},{"/test/nested",true},{"/test/żółć",true},{"/test/file.txt",false}};
    auto* m=new AktowkaModel();CHECK(m->initialize());CHECK(m->navigate("/test")!=Change::ERROR);
    AktowkaWindow w;auto* v=new AktowkaView(*m,w);v->draw_workspace();
    for(const char* path:{"/","/test","/test/nested","/test/żółć","/test/nested/","/test/./nested/../żółć"}) {
        v->begin_edit(EditMode::PATH);v->active_field().ustaw_tekst(path);
        CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,28)));
        char canonical[PATH_CAP];CHECK(BursztynUI::DialogPliku::normalizuj_sciezke(path,canonical,sizeof(canonical)));
        CHECK(!strcmp(m->path(),canonical));CHECK(v->edit_mode()==EditMode::NONE);
        CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,10)));CHECK(!v->dialog_open());
        v->begin_edit(EditMode::PATH);CHECK(!strcmp(v->edit_text(),canonical));v->end_edit();
    }
    for(const char* path:{"/missing","/test/file.txt","relative"}) {
        std::string before=m->path();v->begin_edit(EditMode::PATH);v->active_field().ustaw_tekst(path);
        CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,10)));CHECK(v->dialog_open());CHECK(before==m->path());v->close_modal();
    }
    v->begin_edit(EditMode::PATH);v->active_field().ustaw_tekst("/test/żółćXY");
    CHECK(v->erase_input());CHECK(v->move_caret_left());CHECK(v->delete_input());
    CHECK(!strcmp(v->edit_text(),"/test/żółć"));CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,13)));CHECK(!strcmp(m->path(),"/test/żółć"));
    v->begin_edit(EditMode::PATH);v->active_field().ustaw_tekst("/test/żółć");CHECK(v->erase_input());v->append_input(0xc4);CHECK(v->append_input(0x87));CHECK(v->submit_path());
    v->begin_edit(EditMode::PATH);v->active_field().ustaw_tekst("/test/żółć");v->active_field().ustaw_zaznaczenie(6,10);
    CHECK(v->open_path_context(v->layout().path.x+10,v->layout().path.y+10));CHECK(v->active_field().czy_ma_zaznaczenie());
    CHECK(v->context_open());CHECK(v->popup_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,1)));CHECK(!v->context_open());
    CHECK(v->edit_mode()==EditMode::PATH);v->active_field().ustaw_pozycje_kursora(strlen(v->edit_text()));
    CHECK(v->erase_input());v->append_input(0xc4);CHECK(v->append_input(0x87));CHECK(v->submit_path());CHECK(!strcmp(m->path(),"/test/żółć"));
    auto base=*gui_pobierz_motyw();
    for(bool black:{false,true}) {
        auto theme=base;theme.oknotlo=black?0:0x123456;theme.panel=black?0:0x234567;theme.poletekstowe=black?0:0x345678;
        test_ustaw_aktywny_motyw(&theme);test_capture_pixels=true;test_pixels.clear();v->theme_refresh();
        CHECK(test_pixels.at({w.x,w.y+TITLE_H})==(theme.oknotlo|0xff000000U));
        CHECK(test_pixels.at({w.x+w.w-1,w.y+w.h-1})==(theme.oknotlo|0xff000000U));
        bool panel=false,field=false,opaque=true;
        for(auto& pixel:test_pixels) {opaque &= (pixel.second>>24)==255;panel|=pixel.second==(theme.panel|0xff000000U);field|=pixel.second==(theme.poletekstowe|0xff000000U);}
        CHECK(opaque);CHECK(panel);CHECK(field);test_capture_pixels=false;
    }
    test_pixels.clear();test_ustaw_aktywny_motyw(nullptr);delete v;delete m;
}
int main() {
    g_test_bws_hook=explorer_bws;
    files={{"/",true},{"/test",true},{"/dest",true},{"/test/żółć.txt",false},{"/test/program.bur",false},{"/test/folder",true}};
    using namespace PlikiUI;
    CHECK(nazwa_poprawna("zażółć gęślą.txt"));CHECK(!nazwa_poprawna(""));CHECK(!nazwa_poprawna(".."));
    CHECK(!nazwa_poprawna("a/b"));CHECK(!nazwa_poprawna("a\\b"));CHECK(!nazwa_poprawna("\xc0\xaf"));
    CHECK(!nazwa_poprawna("\xed\xa0\x80"));CHECK(!nazwa_poprawna("\xf4\x90\x80\x80"));CHECK(!nazwa_poprawna("\xe2"));
    std::string longest(55,'a');CHECK(nazwa_poprawna(longest.c_str()));CHECK(!nazwa_poprawna((longest+"a").c_str()));
    CHECK(tekstowy("żółć.TXT",false));CHECK(!tekstowy("folder.txt",true));CHECK(!tekstowy("program.bur",false));
    CHECK(utworz_element("/test","folder2",true));CHECK(files["/test/folder2"]);
    CHECK(utworz_element("/test","nowy.txt",false));CHECK(!files["/test/nowy.txt"]);
    CHECK(!utworz_element("/test","nowy.txt",false));CHECK(!utworz_element("/test","../escape",false));
    CHECK(wklej("/dest")==WynikWklejenia::Pusty);
    CHECK(ustaw_schowek_plikow("/test/żółć.txt",BWS_SCHOWEK_COPY));CHECK(wklej("/dest")==WynikWklejenia::OK);
    CHECK(files.count("/test/żółć.txt"));CHECK(files.count("/dest/żółć.txt"));
    CHECK(wklej("/dest")==WynikWklejenia::Blad);CHECK(files.count("/test/żółć.txt"));
    CHECK(ustaw_schowek_plikow("/test/nowy.txt",BWS_SCHOWEK_CUT));deny_operation=true;
    CHECK(wklej("/dest")==WynikWklejenia::Blad);CHECK(files.count("/test/nowy.txt"));CHECK(file_clip.operacja==BWS_SCHOWEK_CUT);
    deny_operation=false;CHECK(wklej("/dest")==WynikWklejenia::OK);CHECK(!files.count("/test/nowy.txt"));CHECK(file_clip.operacja==BWS_SCHOWEK_PUSTY);
    CHECK(!notatnik("/test/folder",true));CHECK(!notatnik("/test/program.bur",false));CHECK(launches==0);
    CHECK(notatnik("/test/żółć.txt",false));CHECK(launched_file=="/test/żółć.txt");CHECK(launched_app=="/programy/notatnik.cebula/notatnik.bur");
    for(bool desktop:{false,true}) for(CelMenu target:{CelMenu{},CelMenu{true,false,true,false,false},CelMenu{true,true,false,false,false},CelMenu{true,false,false,true,false},CelMenu{false,false,false,false,true}}) {
        auto menu=new MenuPlikowe();menu->events.geometria({0,0,640,440},{0,0,640,440});menu->otworz(630,430,target,desktop);
        CHECK(menu->menu.czy_otwarte());
        for(size_t i=0;i<menu->menu.menu().liczba_elementow();++i) {
            auto* item=menu->menu.menu().element(i);if(static_cast<Akcja>(item->identyfikator())==Akcja::REMOVE)CHECK(!strcmp(item->tekst(),"Usuń"));CHECK(item->czy_wlaczony()==dostepna(static_cast<Akcja>(item->identyfikator()),target));
        }
        auto rect=menu->events.popups.prostokat_zarejestrowany_ekranu();CHECK(rect.x>=0 && rect.y>=0 && rect.prawy()<=640 && rect.dolny()<=440);
        CHECK(menu->events.zdarzenie(event(BWS_ZDARZENIE_MYSZ_PRAWY_DOWN,0,0)));CHECK(!menu->menu.czy_otwarte());CHECK(menu->pobierz_akcje()==Akcja::NONE);
        menu->otworz(300,100,target,desktop);
        CHECK(menu->events.zdarzenie(event(BWS_ZDARZENIE_MYSZ_DOWN,0,0)));CHECK(!menu->menu.czy_otwarte());
        CHECK(menu->events.zdarzenie(event(BWS_ZDARZENIE_MYSZ_UP,0,0)));CHECK(!menu->events.zdarzenie(event(BWS_ZDARZENIE_MYSZ_UP,0,0)));
        delete menu;
    }
    auto model=new AktowkaModel();CHECK(model->initialize());CHECK(model->navigate("/test")!=Change::ERROR);
    select_name(*model,"żółć.txt");CHECK(model->rename_selected("nowa-żółć.txt"));CHECK(files.count("/test/nowa-żółć.txt"));
    model->refresh();select_name(*model,"nowa-żółć.txt");CHECK(model->move_selected("/dest"));CHECK(!files.count("/test/nowa-żółć.txt"));
    model->refresh();select_name(*model,"program.bur");
    AktowkaWindow window;auto view=new AktowkaView(*model,window);view->draw_workspace();
    CHECK(view->open_modal(EditMode::CONFIRM_DELETE));view->close_modal();CHECK(deletes==0);CHECK(files.count("/test/program.bur"));
    CHECK(view->open_modal(EditMode::CONFIRM_DELETE));submit_modal(*view,*model);CHECK(deletes==1);CHECK(!files.count("/test/program.bur"));
    CHECK(model->create_file("żółć2.txt"));CHECK(model->create_folder("folder3"));model->refresh();select_name(*model,"żółć2.txt");
    CHECK(model->set_filter("żółć"));
    std::string path=model->path(),filter=model->filter();int selection=model->selected(),scroll=model->scroll();
    BursztynMotyw theme=*gui_pobierz_motyw();theme.panel^=0xffffff;theme.ramka=theme.wylaczony;test_ustaw_aktywny_motyw(&theme);
    view->theme_refresh();CHECK(view->toolbar_enabled(4));CHECK(view->toolbar_enabled(5));CHECK(path==model->path());CHECK(filter==model->filter());CHECK(selection==model->selected());CHECK(scroll==model->scroll());
    CHECK(view->open_modal(EditMode::OPEN_WITH));submit_modal(*view,*model);CHECK(launched_file=="/test/żółć2.txt");
    for(unsigned scale:{100U,125U}) {
        ui_skala_procent=scale;view->draw_workspace();auto l=view->layout();CHECK(l.path.w>0);CHECK(l.move.x+l.move.w<=l.toolbar.x+l.toolbar.w);CHECK(l.path.y+l.path.h<=l.toolbar.y+l.toolbar.h);
        auto d=new DialogPlikowy();CHECK(d->otworz("Nazwa","","",{500,320,140,120},{0,0,640,440}));
        auto r=d->events.popups.prostokat_zarejestrowany_ekranu();CHECK(r.x>=0 && r.y>=0 && r.prawy()<=640 && r.dolny()<=440);
        input(*d,"zażółć.txt");CHECK(!strcmp(d->tekst(),"zażółć.txt"));
        d->zdarzenie(event(BWS_ZDARZENIE_KLAWISZ,0,0,27));d->zdarzenie(event(BWS_ZDARZENIE_TIMER));CHECK(d->wynik()==2);d->zamknij();delete d;
    }
    ui_skala_procent=100;
    view->begin_edit(EditMode::SEARCH);for(const char* c=" nowy";*c;++c)view->append_input(*c);
    std::string editing=view->edit_text();view->theme_refresh();CHECK(editing==view->edit_text());view->end_edit();
    CHECK(model->create_file(longest.c_str()));CHECK(files.count("/test/"+longest));
    CHECK(ustaw_schowek_plikow("/test/żółć2.txt",BWS_SCHOWEK_CUT));files["/dest/żółć2.txt"]=false;
    CHECK(wklej("/dest")==WynikWklejenia::Blad);CHECK(files.count("/test/żółć2.txt"));CHECK(file_clip.operacja==BWS_SCHOWEK_CUT);
    model->refresh();select_name(*model,"żółć2.txt");
    CHECK(view->open_modal(EditMode::RENAME));view->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,'a'));
    editing=view->edit_text();view->theme_refresh();CHECK(editing==view->edit_text());CHECK(view->dialog_open());view->close_modal();
    auto menu=new MenuPlikowe();menu->events.geometria({0,0,640,440},{0,0,640,440});
    menu->otworz(100,100,{true,false,true,false,false});
    CHECK(menu->events.zdarzenie(event(BWS_ZDARZENIE_KLAWISZ,0,0,27)));
    CHECK(menu->events.zdarzenie(event(BWS_ZDARZENIE_KLAWISZ,0,0,'[')));
    CHECK(menu->events.zdarzenie(event(BWS_ZDARZENIE_KLAWISZ,0,0,'B')));CHECK(menu->menu.czy_otwarte());
    menu->events.zdarzenie(event(BWS_ZDARZENIE_KLAWISZ,0,0,27));menu->events.zdarzenie(event(BWS_ZDARZENIE_TIMER));CHECK(!menu->menu.czy_otwarte());delete menu;
    delete view;delete model;regression_cases();remaining_regressions();text_context_cases();explorer_launcher_cases();printf("Explorer/desktop shared behavior: %d/%d PASS (RAM filesystem)\n",checks,checks);
}
