#pragma once
static void desktop_selection_cases() {
    for(unsigned scale:{100U,125U})for(auto resolution:{std::pair<int,int>{1280,720},{1920,1080}}) {
        ui_skala_procent=scale;screen_w=resolution.first;screen_h=resolution.second;metryki_ekranu();rozmiesc_ikony_ekranu();
        files={{"/",true},{SCIEZKA_PULPIT,true}};
        for(auto name:{"i","WWWWWWWW","żółć","żółć i ąćęłńóśźż bardzo długa nazwa"})files[std::string(SCIEZKA_PULPIT)+"/"+name]=true;
        desktop_layout.count=liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
        CHECK(desktop_layout.count==4);
        int shortItem=desktop_find("i"),wideItem=desktop_find("WWWWWWWW"),polishItem=desktop_find("żółć");
        CHECK(shortItem>=0 && wideItem>=0 && polishItem>=0);
        desktop_click(shortItem);desktop_click(wideItem,true);desktop_click(polishItem,true);
        CHECK(desktop_layout.selected_count()==3);desktop_layout.items[desktop_find("żółć i ąćęłńóśźż bardzo długa nazwa")].selected=true;
        int pad=ui_skaluj(3,scale);CHECK(pad==(scale==100?3:4));
        auto metrics=pulpit_metryki(scale);
        for(int i=0;i<desktop_layout.count;++i) {
            auto& label=desktop_layout.items[i].label;auto box=wizualny_wpis_pulpitu(i),cell=desktop_layout.cell(i);
            auto geometry=pulpit_geometria_komorki(cell.x,cell.y,scale);
            CHECK(label.complete);int actualWidth=0;
            for(int line=0;line<label.count;++line) {
                std::string text(wpisy_pulpitu[i].nazwa+label.starts[line],label.ends[line]-label.starts[line]);
                CHECK((static_cast<unsigned char>(wpisy_pulpitu[i].nazwa[label.starts[line]])&192)!=128);
                CHECK((static_cast<unsigned char>(wpisy_pulpitu[i].nazwa[label.ends[line]])&192)!=128);
                int measured=oblicz_szerokosc_tekstu(text.c_str(),scale);CHECK(measured==label.widths[line]);
                if(measured>actualWidth)actualWidth=measured;
            }
            CHECK(actualWidth==label.width);
            // Pixel centers can differ by one on odd widths. Build the union
            // from the same centered label region used by the text renderer.
            int textX=geometry.etykieta_x+(geometry.etykieta_szer-actualWidth)/2;
            int left=geometry.ikona_x<textX?geometry.ikona_x:textX;
            int right=geometry.ikona_x+geometry.ikona_szer;
            if(textX+actualWidth>right)right=textX+actualWidth;
            CHECK(box.x==left-pad);CHECK(box.szerokosc==right-left+2*pad);
            CHECK(box.y==geometry.ikona_y-pad);
            CHECK(box.wysokosc==metrics.ikona_wys+metrics.odstep_etykiety+label.height+2*pad);
            CHECK(box.szerokosc<cell.szerokosc);CHECK(box.wysokosc<cell.wysokosc);
            CHECK(box.x>=0 && box.y>=0 && box.x+box.szerokosc<=screen_w && box.y+box.wysokosc<=screen_h-PASEK_WYS);
            // Comfortable click targets still occupy the full grid cell.
            CHECK(wpis_pulpitu_w_punkcie(cell.x+1,cell.y+1)==i);
        }
        auto narrow=wizualny_wpis_pulpitu(shortItem),wide=wizualny_wpis_pulpitu(wideItem);
        CHECK(desktop_layout.items[shortItem].label.count==1);CHECK(desktop_layout.items[shortItem].label.width<metrics.ikona_szer);
        CHECK(narrow.szerokosc==metrics.ikona_szer+2*pad);
        CHECK(desktop_layout.items[wideItem].label.width>metrics.ikona_szer);CHECK(wide.szerokosc>narrow.szerokosc);
        CHECK(desktop_layout.items[desktop_find("żółć i ąćęłńóśźż bardzo długa nazwa")].label.count>1);
        for(auto source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY,PLIK_GRAFIT,PLIK_BALTYCKA_NOC,PLIK_BURSZTYN_KLASYCZNY}) {
            BursztynMotyw theme{};CHECK(motyw_parsuj(source,strlen(source),&theme));test_ustaw_aktywny_motyw(&theme);
            test_paint_rects.clear();test_capture_paints=true;RysujWpisyPulpitu();test_capture_paints=false;
            CHECK(desktop_layout.selected_count()==4);
            for(int i=0;i<4;++i) {
                auto box=wizualny_wpis_pulpitu(i);bool fill=false,border=false;
                for(auto paint:test_paint_rects) {
                    if(paint.color==motyw_kolor(MOTYW_KOLOR_PULPIT_ZAZNACZENIE) && paint.x==box.x && paint.y==box.y && paint.w==box.szerokosc && paint.h==box.wysokosc)fill=true;
                    if(paint.color==motyw_kolor(MOTYW_KOLOR_PULPIT_RAMKA) && paint.x==box.x && paint.y==box.y && paint.w==box.szerokosc)border=true;
                }
                CHECK(fill);CHECK(border);CHECK(desktop_layout.items[i].selected);
            }
            CHECK(wizualny_wpis_pulpitu(shortItem).szerokosc==narrow.szerokosc);
            // Drag outlines use the same compact per-item geometry.
            desktop_gesture.dx=ui_skaluj(10,scale);desktop_gesture.dy=ui_skaluj(5,scale);
            test_paint_rects.clear();test_capture_paints=true;obrysy_grupy(false);test_capture_paints=false;
            for(int i=0;i<4;++i) {
                auto box=wizualny_wpis_pulpitu(i);bool found=false;
                for(auto paint:test_paint_rects)found|=paint.x==box.x+desktop_gesture.dx && paint.y==box.y+desktop_gesture.dy && paint.w==box.szerokosc;
                CHECK(found);
            }
            desktop_gesture.dx=desktop_gesture.dy=0;
        }
        test_ustaw_aktywny_motyw(nullptr);desktop_ctrl(false);
    }
}
