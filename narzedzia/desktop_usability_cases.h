#pragma once
static bws_zdarzenie desktop_event(unsigned type,int x=0,int y=0) {
    bws_zdarzenie e{};e.typ=type;e.x=x;e.y=y;return e;
}
static void desktop_ctrl(bool on) {
    auto e=desktop_event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1);e.kod=0x1d;e.przyciski=on?BWS_MOD_CTRL:0;obsluz_gest_pulpitu(e);
}
static void desktop_click(int i,bool ctrl=false) {
    desktop_ctrl(ctrl);auto r=desktop_layout.cell(i);
    if(r.x+10<0 || r.y+10<0)r=wizualny_wpis_pulpitu(i);
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,r.x+10,r.y+10)));
    CHECK(test_mouse_capture);
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,r.x+10,r.y+10)));
    CHECK(!test_mouse_capture);
}
static void desktop_usability_cases() {
    for(unsigned scale:{100U,125U})for(auto resolution:{std::pair<int,int>{1280,720},{1920,1080}}) {
        screen_w=resolution.first;screen_h=resolution.second;ui_skala_procent=scale;metryki_ekranu();rozmiesc_ikony_ekranu();
        files={{"/",true},{"/uzytkownicy",true},{SCIEZKA_PULPIT,true}};
        for(auto name:{"a.txt","b folder","nowy test z pełną nazwą","żółć i ąćęłńóśźż bardzo długa nazwa"})
            files[std::string(SCIEZKA_PULPIT)+"/"+name]=strcmp(name,"a.txt")!=0;
        liczba_wpisow_pulpitu=0;desktop_layout.count=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        CHECK(desktop_layout.count==4+DesktopEntries::Count);
        for(int i=0;i<4;++i) {
            auto p=desktop_layout.items[i];auto cell=desktop_layout.cell(i),r=wizualny_wpis_pulpitu(i);
            CHECK(p.placed);CHECK(p.label.complete);CHECK(p.label.count>=1);
            CHECK(r.y>=0);CHECK(r.y+r.wysokosc<=screen_h-PASEK_WYS);CHECK(r.x>=0);CHECK(r.x+r.szerokosc<=screen_w);
            CHECK(r.wysokosc<cell.wysokosc);CHECK(r.szerokosc<cell.szerokosc);
            std::string joined;
            for(int line=0;line<p.label.count;++line) {
                CHECK(p.label.widths[line]<=pulpit_metryki(scale).etykieta_szer);
                CHECK((static_cast<unsigned char>(wpisy_pulpitu[i].nazwa[p.label.starts[line]])&192)!=128);
                CHECK((static_cast<unsigned char>(wpisy_pulpitu[i].nazwa[p.label.ends[line]])&192)!=128);
                joined.append(wpisy_pulpitu[i].nazwa+p.label.starts[line],p.label.ends[line]-p.label.starts[line]);
            }
            CHECK(joined==wpisy_pulpitu[i].nazwa);
            for(int j=i+1;j<4;++j)CHECK(!Desktop::intersects(r,wizualny_wpis_pulpitu(j)));
        }
        CHECK(desktop_layout.items[3].label.count>1);
        desktop_click(0);CHECK(desktop_layout.selected_count()==1);
        desktop_click(1,true);CHECK(desktop_layout.selected_count()==2);
        desktop_click(0,true);CHECK(desktop_layout.selected_count()==1);CHECK(!desktop_layout.items[0].selected);
        desktop_click(0,true);CHECK(desktop_layout.selected_count()==2);
        auto r=desktop_layout.cell(0);auto before=files;int rel=desktop_layout.items[1].col-desktop_layout.items[0].col;
        desktop_ctrl(false);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,r.x+10,r.y+10)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,r.x+11,r.y+11)));CHECK(!desktop_gesture.dragging);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,r.x+10,r.y+desktop_layout.ch+10)));CHECK(desktop_gesture.dragging);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,r.x+10,r.y+desktop_layout.ch+10)));
        CHECK(desktop_layout.selected_count()==2);CHECK(desktop_layout.items[1].col-desktop_layout.items[0].col==rel);CHECK(files==before);CHECK(!test_mouse_capture);
        for(auto edge:{std::pair<int,int>{-999,-999},{9999,-999},{-999,9999},{9999,9999}}) {
            r=wizualny_wpis_pulpitu(0);
            obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,r.x+10,r.y+10));
            test_paint_rects.clear();test_capture_paints=true;
            obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,edge.first,edge.second));
            test_capture_paints=false;
            for(auto paint:test_paint_rects)CHECK(paint.w<screen_w || paint.h<screen_h-PASEK_WYS);
            obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,edge.first,edge.second));
            for(int i:{0,1}) {auto c=wizualny_wpis_pulpitu(i);CHECK(c.x>=0);CHECK(c.y>=0);CHECK(c.prawy()<=screen_w);CHECK(c.dolny()<=screen_h-PASEK_WYS);}
            CHECK(desktop_layout.selected_count()==2);CHECK(files==before);
            for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)CHECK(!Desktop::intersects(wizualny_wpis_pulpitu(i),wizualny_wpis_pulpitu(j)));
        }
        // Preserve group on right click; single-target actions are disabled.
        r=desktop_layout.cell(0);auto right=desktop_event(BWS_ZDARZENIE_MYSZ_PRAWY_DOWN,r.x+10,r.y+10);
        CHECK(obsluz_prawy_klik_pulpitu(right));CHECK(desktop_layout.selected_count()==2);
        for(int index:{0,1,2,3,7})CHECK(!pulpit_menu->menu.menu().element(index)->czy_wlaczony());
        for(int index:{4,5,8})CHECK(pulpit_menu->menu.menu().element(index)->czy_wlaczony());
        wykonaj_pulpit_context(PulpitContextAction::COPY);CHECK(file_clip.zarezerwowane==2);CHECK(files==before);
        files["/dest"]=true;CHECK(PlikiUI::wklej("/dest")==PlikiUI::WynikWklejenia::OK);
        CHECK(files.count("/dest/a.txt"));CHECK(files.count("/dest/b folder"));
        wykonaj_pulpit_context(PulpitContextAction::CUT);CHECK(file_clip.operacja==BWS_SCHOWEK_CUT);CHECK(file_clip.zarezerwowane==2);
        files["/dest2"]=true;CHECK(PlikiUI::wklej("/dest2")==PlikiUI::WynikWklejenia::OK);CHECK(file_clip.operacja==BWS_SCHOWEK_PUSTY);
        CHECK(!files.count(std::string(SCIEZKA_PULPIT)+"/a.txt"));CHECK(!files.count(std::string(SCIEZKA_PULPIT)+"/b folder"));
        hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        desktop_layout.click(0,false);desktop_layout.items[1].selected=true;
        CHECK(sciezka_wpisu_pulpitu(0,pulpit_cel,sizeof(pulpit_cel)));
        int oldDeletes=deletes;wykonaj_pulpit_context(PulpitContextAction::REMOVE);
        CHECK(pulpit_modal_typ==PulpitModalTyp::REMOVE);CHECK(deletes==oldDeletes);CHECK(pulpit_batch_count==2);
        zatwierdz_pulpit_modal();CHECK(deletes==oldDeletes+2);CHECK(desktop_layout.count==0+DesktopEntries::Count);
        // Rebuild a predictable row for rubber-band and theme tests.
        files[std::string(SCIEZKA_PULPIT)+"/a"]=true;files[std::string(SCIEZKA_PULPIT)+"/b"]=false;
        hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        for(int i=0;i<desktop_layout.count;++i)desktop_layout.items[i]=Desktop::Position{};
        aktualizuj_layout_pulpitu();desktop_layout.clear_selection();
        // Isolate the two filesystem entries for this rubber-band test;
        // automatic placement now reserves the catalog before user items.
        for(int i=0;i<2;++i){auto& p=desktop_layout.items[i];p.col=i;p.row=3;p.offsetX=p.offsetY=0;p.placed=p.custom=true;}
        r=desktop_layout.cell(0);int y=wizualny_wpis_pulpitu(0).dolny()-1;
        obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,0,y));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,r.x+2*desktop_layout.cw,r.y)));
        CHECK(desktop_layout.selected_count()==2);CHECK(desktop_gesture.rubber);
        obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,r.x+2*desktop_layout.cw,r.y));CHECK(!test_mouse_capture);
        desktop_ctrl(true);obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,0,y));
        obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,5,y-10));CHECK(desktop_layout.selected_count()==2);
        obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,5,y-10));
        for(auto source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY,PLIK_BURSZTYN_KLASYCZNY,PLIK_GRAFIT,PLIK_BALTYCKA_NOC}) {
            BursztynMotyw theme{};CHECK(motyw_parsuj(source,strlen(source),&theme));
            test_ustaw_aktywny_motyw(&theme);test_paint_rects.clear();test_capture_paints=true;RysujWpisyPulpitu();test_capture_paints=false;
            CHECK(desktop_layout.selected_count()==2);
            bool bg=false;for(auto paint:test_paint_rects)bg|=paint.color==motyw_pulpit_zaznaczenie(theme);CHECK(bg);
            CHECK(motyw_pulpit_zaznaczenie(theme)!=theme.akcent);CHECK(motyw_pulpit_zaznaczenie(theme)!=0x008A5A00);
        }
        test_ustaw_aktywny_motyw(nullptr);desktop_ctrl(false);
        obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,0,y));CHECK(desktop_layout.selected_count()==0);
        obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,0,y));
        auto key=desktop_event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1);key.kod=0x1e;key.przyciski=BWS_MOD_CTRL;
        CHECK(obsluz_gest_pulpitu(key));CHECK(desktop_layout.selected_count()==2+DesktopEntries::Count);desktop_ctrl(false);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,0,0)));CHECK(desktop_layout.selected_count()==0);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,0,0)));CHECK(!test_mouse_capture);
        // Single file/folder drag is visual only and survives listing refresh.
        for(int i=0;i<2;++i) {
            desktop_click(i);r=desktop_layout.cell(i);before=files;
            obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,r.x+10,r.y+10));
            obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,r.x+desktop_layout.cw+10,r.y+10));
            obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,r.x+desktop_layout.cw+10,r.y+10));
            CHECK(files==before);CHECK(desktop_layout.items[i].custom);int col=desktop_layout.items[i].col;
            hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());CHECK(desktop_layout.items[i].col==col);CHECK(desktop_layout.items[i].selected);
        }
    }
    // Bounded storage handles the full entry limit; a packed grid restores a
    // group when no alternate translated arrangement is free.
    static Desktop::Layout<128> bounded;bounded.count=128;
    bounded.clear_selection();for(int i=0;i<128;++i)bounded.click(i,true);CHECK(bounded.selected_count()==0); // all unplaced
    static Desktop::Layout<4> packed;packed.count=4;packed.geometry(280,203,3,100);
    for(int i=0;i<4;++i){packed.items[i]={};packed.items[i].placed=true;packed.items[i].col=i%2;packed.items[i].row=i/2;}
    packed.items[0].selected=packed.items[3].selected=true;CHECK(packed.move_group(1,0));CHECK(packed.items[0].col==0);CHECK(packed.items[3].col==1);
    for(auto text:{std::string(55,'W'),std::string("żółćżółćżółćżółćżółćżółć"),std::string(255,'W')}) {
        auto l=Desktop::wrap(text.c_str(),112,16,2,16,100,oblicz_szerokosc_tekstu);
        CHECK(l.count<=16);CHECK(l.width<=112);CHECK(l.height<=16*18-2);
        if(text.size()<=55)CHECK(l.complete);else CHECK(!l.complete);
        for(int i=0;i<l.count;++i)CHECK((static_cast<unsigned char>(text[l.ends[i]])&192)!=128);
    }
    // Partial delete stops at the first denied object and reports completed count.
    screen_w=1280;screen_h=720;ui_skala_procent=100;metryki_ekranu();rozmiesc_ikony_ekranu();
    files={{"/",true},{"/uzytkownicy",true},{SCIEZKA_PULPIT,true},{std::string(SCIEZKA_PULPIT)+"/a",true},{std::string(SCIEZKA_PULPIT)+"/b",false}};
    desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
    desktop_layout.items[0].selected=desktop_layout.items[1].selected=true;zaznaczony_wpis_pulpitu=0;
    deny_path=std::string(SCIEZKA_PULPIT)+"/b";int beforeDelete=deletes;wykonaj_pulpit_context(PulpitContextAction::REMOVE);
    CHECK(deletes==beforeDelete);zatwierdz_pulpit_modal();CHECK(deletes==beforeDelete+1);CHECK(pulpit_modal_typ==PulpitModalTyp::NOTICE);
    CHECK(strstr(pulpit_batch_message,"1 / 2"));CHECK(files.count(deny_path));CHECK(!files.count(std::string(SCIEZKA_PULPIT)+"/a"));
    deny_path.clear();zamknij_pulpit_modal();
    // Partial CUT: successful prefix disappears from clipboard, failed and later
    // sources remain, and retry never attempts an already moved path.
    files={{"/",true},{"/src",true},{"/dest",true},{"/src/a",false},{"/src/b",true},{"/dest/b",true}};
    static char paths[2][512]={"/src/a","/src/b"};CHECK(ustaw_schowek_wielu_plikow(paths,2,BWS_SCHOWEK_CUT));
    PlikiUI::RaportWklejenia report;
    CHECK(PlikiUI::wklej("/dest",&report)==PlikiUI::WynikWklejenia::Blad);CHECK(report.wykonane==1);CHECK(report.wszystkie==2);
    CHECK(files.count("/dest/a"));CHECK(!files.count("/src/a"));CHECK(files.count("/src/b"));CHECK(file_clip.zarezerwowane==1);CHECK(!strcmp(file_clip.sciezka,"/src/b"));
    files.erase("/dest/b");CHECK(PlikiUI::wklej("/dest")==PlikiUI::WynikWklejenia::OK);CHECK(file_clip.operacja==BWS_SCHOWEK_PUSTY);
}
