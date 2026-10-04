#pragma once
static bool has_name(AktowkaModel& m,const char* name) {
    for(int i=0;i<m.visible_count();++i)if(!strcmp(m.visible_entry(i)->name,name))return true;
    return false;
}
static bws_zdarzenie physical(unsigned key,unsigned mods=0,bool usb=true) {
    auto e=event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,(usb?BWS_KEY_SOURCE_USB:0)|key);e.przyciski=mods;return e;
}
static void regression_cases() {
    using namespace PlikiUI;
    files={{"/",true},{"/test",true},{"/dest",true},{"/test/sub",true},{"/test/sub/child",true},{"/test/ordinary",false}};
    auto m=new AktowkaModel();CHECK(m->initialize());CHECK(m->navigate("/test")==Change::CHANGED);
    CHECK(m->create_folder("immediate"));CHECK(has_name(*m,"immediate"));
    CHECK(m->create_file("immediate.txt"));CHECK(has_name(*m,"immediate.txt"));
    files["/test/external.txt"]=false;
    CHECK(m->refresh()==Change::CHANGED);CHECK(has_name(*m,"external.txt"));
    select_name(*m,"immediate.txt");CHECK(m->rename_selected("nowa-żółć.txt"));
    CHECK(!has_name(*m,"immediate.txt"));CHECK(has_name(*m,"nowa-żółć.txt"));CHECK(!strcmp(m->entry(m->selected())->name,"nowa-żółć.txt"));
    CHECK(m->move_selected("/dest"));CHECK(!has_name(*m,"nowa-żółć.txt"));CHECK(m->selected()<0);
    CHECK(ustaw_schowek_plikow("/dest/nowa-żółć.txt",BWS_SCHOWEK_COPY));CHECK(m->paste_clipboard());CHECK(has_name(*m,"nowa-żółć.txt"));
    select_name(*m,"nowa-żółć.txt");CHECK(m->delete_selected());CHECK(!has_name(*m,"nowa-żółć.txt"));CHECK(m->selected()<0);
    CHECK(ustaw_schowek_plikow("/dest/nowa-żółć.txt",BWS_SCHOWEK_CUT));CHECK(m->paste_clipboard());CHECK(has_name(*m,"nowa-żółć.txt"));CHECK(!files.count("/dest/nowa-żółć.txt"));
    select_name(*m,"sub");CHECK(!m->move_selected("/test/sub"));CHECK(!m->move_selected("/test/sub/child"));CHECK(!m->move_selected("/test/ordinary"));
    files["/dest/sub"]=true;CHECK(!m->move_selected("/dest"));CHECK(files.count("/test/sub"));
    CHECK(m->set_filter("żółć"));m->set_sort(SortMode::ZA);select_name(*m,"nowa-żółć.txt");
    CHECK(m->create_file("inna-żółć.txt"));CHECK(!strcmp(m->path(),"/test"));CHECK(!strcmp(m->filter(),"żółć"));CHECK(m->sort_mode()==SortMode::ZA);CHECK(!strcmp(m->entry(m->selected())->name,"nowa-żółć.txt"));
    m->set_filter("");m->refresh();
    AktowkaWindow window;auto v=new AktowkaView(*m,window);v->draw_workspace();
    for(unsigned scale:{100U,125U}) {
        ui_skala_procent=scale;v->draw_workspace();
        test_capture_paints=true;test_paint_rects.clear();
        present_workspace_change(*v,m->navigate("/dest"));
        CHECK(!test_paint_rects.empty());auto l=v->layout();
        for(auto r:test_paint_rects) {
            CHECK(!(r.w>=window.w-4 && r.h>=window.h-TITLE_H-3));
            CHECK(!(r.x==l.toolbar.x && r.y==l.toolbar.y && r.w==l.toolbar.w && r.h==l.toolbar.h));
            CHECK(!(r.x==l.left.x && r.y==l.left.y && r.w==l.left.w && r.h==l.left.h));
            CHECK(r.y>=window.y+TITLE_H);
        }
        test_capture_paints=false;
        int rows=v->visible_rows();
        for(int total:{0,1,rows,rows+1,rows*4,2000}) {
            files={{"/",true}};
            for(int i=0;i<total;++i) {char name[40];snprintf(name,sizeof(name),"/row-%04d.txt",i);files[name]=false;}
            m->navigate("/");v->draw_directory_change();auto& bar=v->test_scrollbar();
            CHECK(m->visible_count()==total);CHECK(bar.czy_widoczna()==(total>rows));CHECK(bar.maksimum()==(total>rows?total-rows:0));
            if(total<=rows)continue;
            auto r=bar.obszar_ekranu();auto thumb=bar.prostokat_uchwytu();
            CHECK(thumb.wysokosc>0 && thumb.wysokosc<r.wysokosc);CHECK(thumb.y>=0 && thumb.dolny()<=r.wysokosc);
            CHECK(v->scrollbar_event(physical(78)));CHECK(m->scroll()==(total-rows<rows?total-rows:rows));
            CHECK(v->scrollbar_event(physical(75)));CHECK(m->scroll()==0);
            auto wheel=event(BWS_ZDARZENIE_MYSZ_KOLO,v->layout().list_body.x+10,v->layout().list_body.y+10);wheel.dy=-1;
            CHECK(v->scrollbar_event(wheel));CHECK(m->scroll()==(total-rows<3?total-rows:3));
            wheel.dy=1000;CHECK(v->scrollbar_event(wheel));CHECK(m->scroll()==0);
            thumb=bar.prostokat_uchwytu();int x=r.x+thumb.x+thumb.szerokosc/2,y=r.y+thumb.y+thumb.wysokosc/2;
            CHECK(v->scrollbar_event(event(BWS_ZDARZENIE_MYSZ_DOWN,x,y)));CHECK(bar.czy_przeciagany());
            CHECK(v->scrollbar_event(event(BWS_ZDARZENIE_MYSZ_RUCH,x,r.dolny()+50)));CHECK(m->scroll()==total-rows);CHECK(bar.wartosc()==m->scroll());
            CHECK(v->scrollbar_event(event(BWS_ZDARZENIE_MYSZ_UP,x,r.dolny()+50)));CHECK(!bar.czy_przeciagany());
            auto end=bar.prostokat_uchwytu();CHECK(end.y>=thumb.y && end.dolny()<=r.wysokosc);
            CHECK(m->scroll()+rows==total); // final row reachable, no overscroll
        }
        files={{"/",true},{"/test",true},{"/dest",true},{"/test/source.txt",false},{"/test/folder",true}};
        m->navigate("/test");select_name(*m,"source.txt");
        CHECK(v->open_modal(EditMode::MOVE));
        auto& move_field=v->test_chooser();move_field.events.popups.ustaw_fokus_popupu(move_field.pole_nazwy());
        move_field.pole_nazwy()->ustaw_tekst("aąbć");v->popup_event(physical(42));CHECK(!strcmp(move_field.pole_nazwy()->tekst(),"aąb"));
        v->popup_event(physical(74));v->popup_event(physical(76));CHECK(!strcmp(move_field.pole_nazwy()->tekst(),"ąb"));
        v->close_modal();
        auto native_table=new BursztynUI::Tabela();native_table->ustaw_skale_dpi(ui_skala());native_table->dodaj_kolumne("Nazwa",40);
        for(int i=0;i<10;++i) {const char* cells[]={"folder"};native_table->dodaj_wiersz(cells,1,i);}
        int exact=3+native_table->wysokosc_naglowka()*ui_skala()+10*native_table->wysokosc_wiersza()*ui_skala();
        native_table->rozmiesc({0,0,300,exact});CHECK(!native_table->pasek_pionowy().czy_widoczna());CHECK(!native_table->pasek_poziomy().czy_widoczna());
        native_table->rozmiesc({0,0,300,exact-1});CHECK(native_table->pasek_pionowy().czy_widoczna());CHECK(native_table->pasek_pionowy().maksimum()==1);
        native_table->rozmiesc({0,0,40*ui_skala()+1,exact});CHECK(native_table->pasek_poziomy().czy_widoczna());CHECK(native_table->pasek_pionowy().czy_widoczna());
        native_table->przewin_do_wiersza(9);CHECK(native_table->pasek_pionowy().wartosc()==native_table->pasek_pionowy().maksimum());delete native_table;
        for(auto mode:{EditMode::RENAME,EditMode::CREATE_FOLDER,EditMode::CREATE_FILE}) {
            CHECK(v->open_modal(mode));
            // Exercise the actual focused dialog field, not a separate editor.
            auto ctrl_a=physical(4,BWS_MOD_CTRL);CHECK(v->popup_event(ctrl_a));
            for(const unsigned char* c=reinterpret_cast<const unsigned char*>("aąbć");*c;++c)v->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,*c));
            CHECK(!strcmp(v->edit_text(),"aąbć"));
            CHECK(v->popup_event(physical(42)));CHECK(!strcmp(v->edit_text(),"aąb"));
            // Legacy echo of physical Backspace must not perform a second edit.
            CHECK(v->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,8)));CHECK(!strcmp(v->edit_text(),"aąb"));
            CHECK(v->popup_event(physical(74)));CHECK(v->popup_event(physical(76)));CHECK(!strcmp(v->edit_text(),"ąb"));
            CHECK(v->popup_event(physical(79)));CHECK(v->popup_event(physical(42)));CHECK(!strcmp(v->edit_text(),"b"));
            CHECK(v->popup_event(physical(77)));CHECK(v->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,8)));CHECK(!strcmp(v->edit_text(),""));
            CHECK(v->popup_event(physical(4,BWS_MOD_CTRL)));
            auto text=event(BWS_ZDARZENIE_TEXT_INPUT,0,0,0x17c);CHECK(v->popup_event(text));
            for(const unsigned char* c=reinterpret_cast<const unsigned char*>("ż");*c;++c)v->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,*c));
            CHECK(!strcmp(v->edit_text(),"ż"));
            CHECK(v->popup_event(physical(14,0,false)));CHECK(!strcmp(v->edit_text(),""));
            for(const unsigned char* c=reinterpret_cast<const unsigned char*>("aąb");*c;++c)v->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,*c));
            for(char c:std::string("\x1b[H\x1b[3~"))v->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,c));CHECK(!strcmp(v->edit_text(),"ąb"));
            for(char c:std::string("\x1b[F"))v->popup_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,c));
            v->popup_event(physical(0x0e,0,false));CHECK(!strcmp(v->edit_text(),"ą"));
            // Tab transfers focus to OK. Backspace must not edit the unfocused field.
            CHECK(v->popup_event(physical(43)));CHECK(v->popup_event(physical(42)));CHECK(!strcmp(v->edit_text(),"ą"));
            CHECK(v->popup_event(physical(41)));CHECK(!v->dialog_open());v->close_modal();
        }
    }
    ui_skala_procent=100;
    CHECK(v->open_modal(EditMode::MOVE));auto& chooser=v->test_chooser();
    CHECK(chooser.tabela_plikow()->liczba_wierszy()==1); // ordinary files excluded
    chooser.events.popups.ustaw_fokus_popupu(chooser.pole_nazwy());
    chooser.pole_nazwy()->ustaw_tekst("ąbć");v->popup_event(physical(42));CHECK(!strcmp(chooser.pole_nazwy()->tekst(),"ąb"));
    v->popup_event(physical(74));v->popup_event(physical(76));CHECK(!strcmp(chooser.pole_nazwy()->tekst(),"b"));
    chooser.pole_nazwy()->ustaw_tekst("source.txt");chooser.zatwierdz();CHECK(chooser.wynik_pliku()==BursztynUI::WynikDialoguPliku::Brak);
    CHECK(!chooser.przejdz_do_katalogu("/test/source.txt"));CHECK(chooser.przejdz_do_katalogu("/dest"));
    chooser.pole_nazwy()->ustaw_tekst("");chooser.zatwierdz();CHECK(v->modal_result()==1);submit_modal(*v,*m);
    CHECK(!has_name(*m,"source.txt"));CHECK(files.count("/dest/source.txt"));
    select_name(*m,"folder");CHECK(v->open_modal(EditMode::MOVE));CHECK(v->modal_result()==0);v->close_modal();
    CHECK(v->open_modal(EditMode::OPEN_WITH));CHECK(v->dialog_open());v->popup_event(physical(42));CHECK(v->modal_result()==0);v->close_modal();
    for(auto mode:{EditMode::PATH,EditMode::SEARCH}) {
        v->begin_edit(mode);v->active_field().ustaw_tekst("aąbć");
        CHECK(v->field_event(physical(42)));CHECK(!strcmp(v->edit_text(),"aąb"));
        CHECK(v->field_event(physical(74)));CHECK(v->field_event(physical(76)));CHECK(!strcmp(v->edit_text(),"ąb"));v->end_edit();
    }
    delete v;delete m;
}
