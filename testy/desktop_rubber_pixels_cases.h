#pragma once
static unsigned desktop_blank_intermediates=0;
static void desktop_observe_live_write() {
    for(int i=0;i<desktop_layout.count;++i) {
        auto c=desktop_layout.cell(i);auto g=pulpit_geometria_komorki(c.x,c.y,ui_skala_procent);
        unsigned ink=0;
        for(int y=g.etykieta_y;y<g.etykieta_y+desktop_layout.items[i].label.height;++y)
            for(int x=g.etykieta_x;x<g.etykieta_x+g.etykieta_szer;++x) {
                if(x<0 || y<0 || x>=test_surface_width || y>=test_surface_height)continue;
                uint32_t p=test_surface[size_t(y)*test_surface_width+x];
                ink+=((p>>24)!=0 && (p&255)>180 && ((p>>8)&255)>180 && ((p>>16)&255)>180);
            }
        if(!ink)++desktop_blank_intermediates;
    }
}
static void desktop_rubber_pixels_cases() {
    screen_w=1280;screen_h=720;ui_skala_procent=100;
    desktop_settings=nullptr;desktop_layout={};desktop_layout.set_count(6);
    for(int i=0;i<6;++i){snprintf(wpisy_pulpitu[i].nazwa,sizeof(wpisy_pulpitu[i].nazwa),"Label żółć %d",i);wpisy_pulpitu[i].launcher=i;}
    auto* canvas=new KontekstRysowania({0,0,1280,720});
    std::vector<uint32_t> expected(1280*680);
    for(int px:{12,16,24})for(const char* family:{"DejaVu Sans","DejaVu Sans Mono"}) {
        auto config=BSF::configuration();config.size=px;
        config.family=BSF::resolve_family(family);CHECK(BSF::apply(config));
        metryki_ekranu();rozmiesc_ikony_ekranu();
        for(auto& item:desktop_layout.items)item.selected=false;
        zapamietaj_zaznaczenie_pulpitu();
        test_surface_width=1280;test_surface_height=680;test_surface.assign(1280*680,0);
        canvas->ustaw_cel(expected.data(),{0,0,1280,680});pulpit_canvas=canvas;
        std::fill(expected.begin(),expected.end(),0);RysujWpisyPulpitu();
        pulpit_canvas=nullptr;canvas->ustaw_cel(nullptr,{});
        gui_zastap_region(0,0,1280,680,expected.data());
        desktop_gesture={};desktop_modifiers=0;
        bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_MYSZ_DOWN;e.x=8;e.y=320;
        CHECK(obsluz_gest_pulpitu(e));CHECK(desktop_gesture.rubber);
        desktop_blank_intermediates=0;test_live_write_observer=desktop_observe_live_write;
        for(int y:{280,170,120,92,85,78,72,65,72,78,85,92,120,170,280}) {
            e.typ=BWS_ZDARZENIE_MYSZ_RUCH;e.x=430;e.y=y;
            auto rasterizations=BSF::statistics().rasterizations;
            auto labels=desktop_layout;
            CHECK(obsluz_gest_pulpitu(e));
            if(desktop_blank_intermediates)fprintf(stderr,"Blank labels at live-surface syscall boundaries: %u (font %s %d px, band top %d)\n",desktop_blank_intermediates,family,px,y);
            CHECK(desktop_blank_intermediates==0);
            CHECK(BSF::statistics().rasterizations==rasterizations);
            for(int i=0;i<desktop_layout.count;++i)CHECK(!memcmp(&labels.items[i].label,&desktop_layout.items[i].label,sizeof(labels.items[i].label)));
            auto generation=BSF::configuration().generation;
            canvas->ustaw_cel(expected.data(),{0,0,1280,680});pulpit_canvas=canvas;
            std::fill(expected.begin(),expected.end(),0);RysujWpisyPulpitu();
            rysuj_ramke_pulpitu(desktop_gesture.band,motyw_kolor(MOTYW_KOLOR_PULPIT_RAMKA));
            pulpit_canvas=nullptr;canvas->ustaw_cel(nullptr,{});
            if(test_surface!=expected) {
                for(bool reference:{false,true}) {
                    std::ofstream image(reference?".bsf-test/rubber-expected.ppm":".bsf-test/rubber-actual.ppm",std::ios::binary);
                    image<<"P6\n1280 680\n255\n";
                    for(auto c:reference?expected:test_surface){c=motyw_pulpit_skladaj(0x203040,c,true,gui_pobierz_motyw());char rgb[]={char(c>>16),char(c>>8),char(c)};image.write(rgb,3);}
                }
            }
            CHECK(test_surface==expected);
            CHECK(BSF::configuration().generation==generation);
        }
        e.typ=BWS_ZDARZENIE_MYSZ_UP;CHECK(obsluz_gest_pulpitu(e));
        test_live_write_observer=nullptr;
        test_surface_width=test_surface_height=0;test_surface.clear();
    }
    delete canvas;
#ifndef BURSZTYN_BASELINE
    printf("Desktop selection patches: metadata=%zu bytes, reused pixels=%llu bytes, generation=%llu\n",
        sizeof(DesktopSelectionPaint),static_cast<unsigned long long>(desktop_selection_paint->capacity()*4),
        static_cast<unsigned long long>(desktop_selection_generation));
#endif
}
