#pragma once
static void desktop_launcher_edge_cases() {
    for(unsigned scale:{100U,125U})for(auto resolution:{std::pair<int,int>{1280,720},{1920,1080}}) {
        screen_w=resolution.first;screen_h=resolution.second;ui_skala_procent=scale;metryki_ekranu();rozmiesc_ikony_ekranu();
        // Exercise the production visual geometry and placement algorithm with
        // an isolated item/group so another icon cannot occupy the tested edge.
        for(const char* name:{"i","zażółć bardzo długa nazwa folderu"})for(int edge=0;edge<4;++edge) {
            Desktop::Layout<4> layout;layout.geometry(screen_w,screen_h-PASEK_WYS,ui_skaluj(3,scale),scale);
            auto m=pulpit_metryki(scale);layout.count=1;
            auto& p=layout.items[0];p.col=2;p.row=2;p.placed=p.selected=true;
            p.label=Desktop::wrap(name,m.etykieta_szer,m.wysokosc_linii,m.odstep_linii,16,scale,oblicz_szerokosc_tekstu);
            CHECK(p.label.complete);CHECK(name[0]=='i'?p.label.count==1:p.label.count>1);
            int dx=edge==0?-9999:edge==1?9999:0,dy=edge==2?-9999:edge==3?9999:0;
            layout.clamp_delta(dx,dy);CHECK(layout.move_visual_group(dx,dy));auto r=layout.placement_bounds(0);
            CHECK(r.x>=0 && r.y>=0 && r.prawy()<=screen_w && r.dolny()<=screen_h-PASEK_WYS);
            if(edge==0)CHECK(r.x==0);if(edge==1)CHECK(r.prawy()==screen_w);
            if(edge==2)CHECK(r.y==0);if(edge==3)CHECK(r.dolny()==screen_h-PASEK_WYS);
            layout.count=2;layout.items[1]=layout.items[0];layout.items[1].offsetX+=layout.cw;
            // Return group to an interior location and preserve its geometry.
            layout.items[0].offsetX=layout.items[0].offsetY=0;
            layout.items[1].offsetX=layout.cw;layout.items[1].offsetY=0;
            auto a=layout.visual_bounds(0),b=layout.visual_bounds(1);
            dx=edge==0?-9999:edge==1?9999:0;dy=edge==2?-9999:edge==3?9999:0;
            layout.clamp_delta(dx,dy);CHECK(layout.move_visual_group(dx,dy));
            auto aa=layout.visual_bounds(0),bb=layout.visual_bounds(1),group=Desktop::unite(layout.placement_bounds(0),layout.placement_bounds(1));
            CHECK(bb.x-aa.x==b.x-a.x);CHECK(bb.y-aa.y==b.y-a.y);
            if(edge==0)CHECK(group.x==0);if(edge==1)CHECK(group.prawy()==screen_w);
            if(edge==2)CHECK(group.y==0);if(edge==3)CHECK(group.dolny()==screen_h-PASEK_WYS);
        }
        files={{"/",true},{SCIEZKA_PULPIT,true},{std::string(SCIEZKA_PULPIT)+"/file.txt",false}};
        for(auto& app:DesktopEntries::Launchers)files[app.path]=false;
        desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        CHECK(desktop_layout.count==1+DesktopEntries::Count);
        auto before=files;
        for(int id=0;id<DesktopEntries::Count;++id) {
            int i=desktop_find(DesktopEntries::Launchers[id].name);CHECK(i>=0);CHECK(wpisy_pulpitu[i].launcher==id);
            desktop_click(i);CHECK(desktop_layout.selected_count()==1);CHECK(desktop_layout.items[i].selected);
            auto r=wizualny_wpis_pulpitu(i);int x=r.x+r.szerokosc/2,y=r.y+10;
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,x,y)));
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,x,y+desktop_layout.ch*3)));
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,x,y+desktop_layout.ch*3)));
            CHECK(desktop_layout.items[i].custom);CHECK(files==before);
            int launchesBefore=launches;auto placed=wizualny_wpis_pulpitu(i);
            int last=-1;uint64_t stamp=0;auto click=desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,placed.x+placed.szerokosc/2,placed.y+10);
            click.timestamp=100;CHECK(obsluz_klik_wpisu_pulpitu(click,last,stamp));
            obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,click.x,click.y));
            click.timestamp=120;CHECK(obsluz_klik_wpisu_pulpitu(click,last,stamp));CHECK(!test_mouse_capture);
            CHECK(launches==launchesBefore+1);CHECK(launched_app==DesktopEntries::Launchers[id].path);
            CHECK(otworz_pulpit_context(x,y,i));CHECK(pulpit_menu->menu.menu().element(0)->czy_wlaczony());
            for(int action:{1,2,3,4,5,6,7,8})CHECK(!pulpit_menu->menu.menu().element(action)->czy_wlaczony());
            zamknij_pulpit_context();
            char path[512]{};CHECK(!sciezka_wpisu_pulpitu(i,path,sizeof(path)));
        }
        int first=desktop_find("Notatnik"),second=desktop_find("Kalkulator"),file=desktop_find("file.txt");
        desktop_click(first);desktop_click(second,true);desktop_click(file,true);desktop_ctrl(false);
        CHECK(desktop_layout.selected_count()==3);CHECK(!zapisz_batch_pulpitu());
        auto a=wizualny_wpis_pulpitu(first),b=wizualny_wpis_pulpitu(second),c=wizualny_wpis_pulpitu(file);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+a.szerokosc/2,a.y+10)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,screen_w+9999,screen_h+9999)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,screen_w+9999,screen_h+9999)));
        CHECK(!test_mouse_capture);CHECK(desktop_layout.selected_count()==3);
        auto aa=wizualny_wpis_pulpitu(first),bb=wizualny_wpis_pulpitu(second),cc=wizualny_wpis_pulpitu(file);
        CHECK(aa.x-a.x==bb.x-b.x && aa.x-a.x==cc.x-c.x);
        CHECK(aa.y-a.y==bb.y-b.y && aa.y-a.y==cc.y-c.y);CHECK(files==before);
        CHECK(otworz_pulpit_context(aa.x,aa.y,first));
        for(int action:{0,1,2,3,4,5,6,7,8})CHECK(!pulpit_menu->menu.menu().element(action)->czy_wlaczony());
        int deletesBefore=deletes;wykonaj_pulpit_context(PulpitContextAction::REMOVE);CHECK(deletes==deletesBefore);CHECK(files==before);
        desktop_layout.click(first,false);desktop_layout.click(second,true);
        a=wizualny_wpis_pulpitu(first);b=wizualny_wpis_pulpitu(second);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+a.szerokosc/2,a.y+10)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,-9999,screen_h+9999)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,-9999,screen_h+9999)));
        CHECK(desktop_layout.selected_count()==2);CHECK(files==before);
        auto onlyA=wizualny_wpis_pulpitu(first),onlyB=wizualny_wpis_pulpitu(second);
        CHECK(onlyB.x-onlyA.x==b.x-a.x);CHECK(onlyB.y-onlyA.y==b.y-a.y);
        aa=onlyA;
        // A complete rubber band selects the launchers and the filesystem item.
        desktop_ctrl(false);desktop_layout.clear_selection();
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,0,0)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,screen_w-1,screen_h-PASEK_WYS-1)));
        CHECK(desktop_layout.selected_count()==desktop_layout.count);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,screen_w-1,screen_h-PASEK_WYS-1)));
        // Switching palette repaints selected foreground without moving entries.
        for(auto source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY,PLIK_GRAFIT,PLIK_BALTYCKA_NOC,PLIK_BURSZTYN_KLASYCZNY}) {
            BursztynMotyw theme{};CHECK(motyw_parsuj(source,strlen(source),&theme));test_ustaw_aktywny_motyw(&theme);
            uint32_t text=motyw_kolor(MOTYW_KOLOR_PULPIT_TEKST_ZAZNACZONY);
            CHECK(motyw_jasnosc_rgb(text)>=230);CHECK(motyw_jasnosc_rgb(text)-motyw_jasnosc_rgb(KANONICZNY_KOLOR_CIENIA_PULPITU)>=210);
            label_paints.clear();capture_labels=true;RysujPulpit(true);capture_labels=false;
            int bright=0;for(auto paint:label_paints)if(paint.color==text)++bright;
            CHECK(bright>=desktop_layout.count);CHECK(desktop_layout.selected_count()==desktop_layout.count);
            auto current=wizualny_wpis_pulpitu(first);CHECK(current.x==aa.x && current.y==aa.y);CHECK(files==before);
        }
        test_ustaw_aktywny_motyw(nullptr);
        auto old=desktop_layout.cell(first);hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        first=desktop_find("Notatnik");auto current=desktop_layout.cell(first);CHECK(current.x==old.x && current.y==old.y);CHECK(desktop_layout.items[first].selected);
        desktop_layout.count=liczba_wpisow_pulpitu=0;
    }    // A first failed directory listing must not remove the built-in launchers.
    files={{"/",true}};desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;
    CHECK(odswiez_wpisy_pulpitu());CHECK(desktop_layout.count==DesktopEntries::Count);
    CHECK(files.size()==1);CHECK(!odswiez_wpisy_pulpitu());
    files[SCIEZKA_PULPIT]=true;files[std::string(SCIEZKA_PULPIT)+"/later.txt"]=false;
    CHECK(odswiez_wpisy_pulpitu());CHECK(desktop_layout.count==DesktopEntries::Count+1);

}
