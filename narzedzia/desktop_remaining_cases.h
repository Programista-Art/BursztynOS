#pragma once
static int desktop_find(const char* name) {
    for(int i=0;i<desktop_layout.count;++i)if(!strcmp(wpisy_pulpitu[i].nazwa,name))return i;
    return -1;
}
static void desktop_remaining_cases() {
    for(unsigned scale:{100U,125U}) {
        screen_w=1280;screen_h=1080;ui_skala_procent=scale;metryki_ekranu();rozmiesc_ikony_ekranu();
        for(auto kind:{PulpitModalTyp::FOLDER,PulpitModalTyp::PLIK}) {
            CHECK(otworz_pulpit_modal(kind));
            auto* field=static_cast<PoleTekstowe*>(pulpit_dialog->dziecko(2));field->ustaw_tekst("żółć.txt");
            auto box=pulpit_dialog->obszar(),input=field->obszar();
            for(auto xy:{std::pair<int,int>{box.x+input.x+4,box.y+input.y+4},{box.x+4,box.y+4},{0,0}}) {
                CHECK(obsluz_popupy_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_PRAWY_DOWN,xy.first,xy.second)));
                CHECK(pulpit_dialog->czy_otwarta());CHECK(pulpit_dialog->wynik()==0);
                CHECK(pulpit_dialog->events.popups.fokus_popupu()==field);CHECK(!strcmp(field->tekst(),"żółć.txt"));
                CHECK(obsluz_popupy_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_PRAWY_UP,xy.first,xy.second)));
            }
            zamknij_pulpit_modal();
        }
        files={{"/",true},{SCIEZKA_PULPIT,true}};
        std::string legal;for(int i=0;i<27;++i)legal+="ż";legal+='x';CHECK(legal.size()==55);
        CHECK(otworz_pulpit_modal(PulpitModalTyp::FOLDER));
        static_cast<PoleTekstowe*>(pulpit_dialog->dziecko(2))->ustaw_tekst(legal.c_str());
        auto input=desktop_event(BWS_ZDARZENIE_TEXT_INPUT);input.kod='y';CHECK(obsluz_popupy_pulpitu(input));
        CHECK(legal==pulpit_dialog->tekst());zatwierdz_pulpit_modal();
        CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/"+legal));
        CHECK(desktop_find(legal.c_str())>=0);CHECK(desktop_layout.items[desktop_find(legal.c_str())].label.complete);
        // Longer backend listings must not fail the entire desktop refresh.
        std::string longName;for(int i=0;i<40;++i)longName+="żółć";
        files={{"/",true},{SCIEZKA_PULPIT,true},{std::string(SCIEZKA_PULPIT)+"/"+longName,true}};
        desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        CHECK(desktop_layout.count==1);CHECK(longName==wpisy_pulpitu[0].nazwa);
        auto l=desktop_layout.items[0].label;CHECK(l.count>2);CHECK(l.count<=Desktop::MaxLines);
        for(int i=0;i<l.count;++i){CHECK((static_cast<unsigned char>(longName[l.ends[i]])&192)!=128);CHECK(l.widths[i]<=pulpit_metryki(scale).etykieta_szer);}
        CHECK(!PlikiUI::nazwa_poprawna(longName.c_str())); // PSF's on-disk limit is unchanged.
        for(auto source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY}) {
            BursztynMotyw theme{};CHECK(motyw_parsuj(source,strlen(source),&theme));test_ustaw_aktywny_motyw(&theme);
            desktop_layout.click(0,false);test_paint_rects.clear();test_capture_paints=true;RysujWpisPulpitu(0);test_capture_paints=false;
            auto visual=wizualny_wpis_pulpitu(0),cell=desktop_layout.cell(0);bool found=false;
            for(auto r:test_paint_rects)if(r.color==motyw_pulpit_zaznaczenie(theme)) {
                found=true;CHECK(r.x==visual.x && r.y==visual.y && r.w==visual.szerokosc && r.h==visual.wysokosc);
                CHECK(r.w<cell.szerokosc);CHECK(r.h<=cell.wysokosc);CHECK(r.color>>24==32);
                CHECK((r.color&0xffffff)==motyw_pulpit_odcien(theme,true,24));
            }
            CHECK(found);CHECK(motyw_pulpit_ramka(theme)>>24==112);
        }
        test_ustaw_aktywny_motyw(nullptr);
        for(bool folder:{false,true})for(int mode:{0,1,2,3}) {
            std::string root=SCIEZKA_PULPIT,src=root+"/source",dst=root+"/target";
            files={{"/",true},{root,true},{src,folder},{dst,true}};
            if(mode==2)files[dst+"/source"]=false;
            desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
            int from=desktop_find("source"),to=desktop_find("target");CHECK(from>=0 && to>=0);
            desktop_ctrl(false);desktop_layout.click(from,false);
            auto a=desktop_layout.cell(from),b=desktop_layout.cell(mode==1?from:to);
            deny_operation=mode==3;auto before=files;
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+10,a.y+10)));
            // A self-drop still requires crossing the drag threshold.
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,b.x+30,b.y+30)));
            if(mode==0)CHECK(pulpit_hover_folder==to);
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,b.x+30,b.y+30)));
            CHECK(!test_mouse_capture);CHECK(!desktop_gesture.candidate);CHECK(pulpit_hover_folder==-1);
            if(mode==0){CHECK(!files.count(src));CHECK(files.count(dst+"/source"));CHECK(desktop_find("source")==-1);}
            else {CHECK(files==before);if(folder || mode!=1)CHECK(pulpit_modal_typ==PulpitModalTyp::NOTICE);}
            deny_operation=false;zamknij_pulpit_modal();
        }
        files={{"/",true},{SCIEZKA_PULPIT,true},{std::string(SCIEZKA_PULPIT)+"/doc.TXT",false}};
        desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        CHECK(PlikiUI::tekstowy("doc.TXT",false));CHECK(!PlikiUI::tekstowy("doc.txt",true));
        test_paint_rects.clear();test_capture_paints=true;RysujWpisPulpitu(0);test_capture_paints=false;
        int paper=0,ink=0;for(auto r:test_paint_rects){CHECK(r.color!=0x004C9BE8);paper+=r.color==motyw_kolor(MOTYW_KOLOR_PANEL);ink+=r.color==motyw_kolor(MOTYW_KOLOR_TEKST);}
        CHECK(paper>=2);CHECK(ink>=8);
    }
    // Group drop: all selected sources move into one unselected folder.
    files={{"/",true},{SCIEZKA_PULPIT,true}};
    for(auto n:{"a.txt","b","target"})files[std::string(SCIEZKA_PULPIT)+"/"+n]=strcmp(n,"a.txt")!=0;
    desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
    int a=desktop_find("a.txt"),b=desktop_find("b"),target=desktop_find("target");
    desktop_layout.click(a,false);desktop_layout.items[b].selected=true;
    auto start=desktop_layout.cell(a),end=desktop_layout.cell(target);
    obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,start.x+10,start.y+10));
    CHECK(desktop_layout.selected_count()==2);
    obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,end.x+10,end.y+10));CHECK(pulpit_hover_folder==target);
    obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,end.x+10,end.y+10));
    CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/target/a.txt"));CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/target/b"));
    CHECK(desktop_layout.count==1);CHECK(!test_mouse_capture);
    // Partial failure is bounded; stop before attempting later sources and
    // report the completed prefix without modifying the clipboard.
    files={{"/",true},{SCIEZKA_PULPIT,true}};
    for(auto n:{"a","b","c","target"})files[std::string(SCIEZKA_PULPIT)+"/"+n]=strcmp(n,"target")==0;
    desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
    for(auto n:{"a","b","c"})desktop_layout.items[desktop_find(n)].selected=true;
    deny_path=std::string(SCIEZKA_PULPIT)+"/b";auto clipboardBefore=file_clip;
    CHECK(!wykonaj_drop_pulpitu(desktop_find("target")));
    CHECK(pulpit_modal_typ==PulpitModalTyp::NOTICE);CHECK(strstr(pulpit_batch_message,"1 / 3"));
    CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/target/a"));CHECK(files.count(deny_path));
    CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/c"));CHECK(!files.count(std::string(SCIEZKA_PULPIT)+"/target/c"));
    CHECK(!memcmp(&clipboardBefore,&file_clip,sizeof(file_clip)));deny_path.clear();zamknij_pulpit_modal();
    CHECK(!PlikiUI::cel_przeniesienia_poprawny("/a","/a"));
    CHECK(!PlikiUI::cel_przeniesienia_poprawny("/a","/a/b"));
    CHECK(PlikiUI::cel_przeniesienia_poprawny("/a","/ab"));
    CHECK(PlikiUI::cel_przeniesienia_poprawny("/a/b","/a"));
}
