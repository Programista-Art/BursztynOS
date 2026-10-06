#pragma once
#include <memory>
static std::string layout_disk;
static bool layout_exists=false,layout_write_fail=false,layout_read_fail=false,layout_interrupt=false;
static int layout_writes=0,layout_io_captured=0;
static uint64_t layout_test_io(uint64_t nr,uint64_t a,uint64_t b,uint64_t c) {
    const char* path=reinterpret_cast<const char*>(a);
    if(nr==46)return 1;
    if(!path || strcmp(path,DesktopSettings::Path))return 0;
    if(nr==44)return layout_exists?layout_disk.size()+1:1;
    if(nr==2){layout_exists=true;return 1;}
    if(nr==3) {
        ++layout_writes;if(test_mouse_capture)++layout_io_captured;
        if(layout_write_fail)return 0;
        layout_disk.assign(reinterpret_cast<const char*>(b),c);
        if(layout_interrupt){layout_disk.resize(layout_disk.size()/2);return 0;}
        return 1;
    }
    if(nr==5) {
        if(layout_read_fail || !layout_exists || c!=layout_disk.size())return 0;
        memcpy(reinterpret_cast<void*>(b),layout_disk.data(),c);return 1;
    }
    return 0;
}
static uint64_t layout_wm_io(uint64_t nr,uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    if((nr==2||nr==3||nr==5||nr==44) && a && !strcmp(reinterpret_cast<const char*>(a),DesktopSettings::Path))return layout_test_io(nr,a,b,c);
    if(nr==46 && a && !strcmp(reinterpret_cast<const char*>(a),"/ustawienia"))return 1;
    return explorer_bws(nr,a,b,c,d);
}
static std::string layout_document(const std::string& body) {
    char end[32];snprintf(end,sizeof(end),"end=%08X\n",DesktopSettings::checksum(body.data(),body.size()));return body+end;
}
static void desktop_persistence_cases() {
    using namespace DesktopSettings;
    auto state=std::make_unique<State>();
    auto reload=std::make_unique<State>();
    auto layout=std::make_unique<Desktop::Layout<Capacity>>();
    std::vector<std::string> names(Capacity);
    auto key=[&](int i)->Key{return {i<DesktopEntries::Count?'L':'F',names[i].c_str()};};
    auto setup=[&](int n,int w,int h,unsigned dpi) {
        *layout={};layout->geometry(w,h,ui_skaluj(3,dpi),dpi);layout->count=n;
        auto m=pulpit_metryki(dpi);
        for(int i=0;i<n;++i) {
            names[i]=i<DesktopEntries::Count?DesktopEntries::Launchers[i].id:"zażółć-"+std::to_string(i);
            auto& p=layout->items[i];p.col=i%layout->cols;p.row=i/layout->cols;p.placed=true;
            p.label=Desktop::wrap(names[i].c_str(),m.etykieta_szer,m.wysokosc_linii,m.odstep_linii,16,dpi,oblicz_szerokosc_tekstu);
        }
        layout->reflow();
    };
    layout_exists=false;layout_disk.clear();layout_writes=0;
    CHECK(!state->load(layout_test_io));CHECK(state->loaded);CHECK(!state->dirty);
    setup(16,1920,1040,100);CHECK(state->save(*layout,key,layout_test_io));CHECK(!layout_exists);CHECK(!layout_writes);
    for(char type:{'L','F'}) {
        setup(1,1920,1040,100);layout->visual_position(0,300,200);
        auto singleKey=[&](int)->Key{return {type,type=='L'?"notatnik":"jeden-żółty.txt"};};
        state->dirty=true;CHECK(state->save(*layout,singleKey,layout_test_io));CHECK(reload->load(layout_test_io));
        setup(1,1920,1040,100);CHECK(reload->apply(*layout,singleKey)==0);
        CHECK(layout->visual_bounds(0).x==300 && layout->visual_bounds(0).y==200);
    }
    for(unsigned dpi:{100U,125U}) {
        setup(30,1920,1080-ui_skaluj(40,dpi),dpi);
        layout->visual_position(0,0,0);layout->visual_position(13,1700,800);
        layout->reflow();
        std::vector<std::pair<int,int>> original;
        for(int i=0;i<layout->count;++i){auto r=layout->visual_bounds(i);original.push_back({r.x,r.y});layout->items[i].selected=true;}
        state->dirty=true;int before=layout_writes;CHECK(state->save(*layout,key,layout_test_io));CHECK(layout_writes==before+1);CHECK(!state->dirty);
        CHECK(layout_disk.find("selected")==std::string::npos);CHECK(layout_disk.find("zażółć")!=std::string::npos);
        CHECK(reload->load(layout_test_io));CHECK(reload->count==30);
        setup(31,1920,1080-ui_skaluj(40,dpi),dpi);CHECK(reload->apply(*layout,key)==0);
        CHECK(reload->matchedLaunchers==DesktopEntries::Count);CHECK(reload->matchedFiles==17);CHECK(!reload->dirty);
        for(int i=0;i<30;++i) {
            auto r=layout->visual_bounds(i);CHECK(r.x==original[i].first && r.y==original[i].second);CHECK(!layout->items[i].selected);
        }
        CHECK(layout->items[30].placed); // new filesystem entry
        names[0]="new-launcher";names[13]="renamed-file";
        CHECK(reload->apply(*layout,key)>=0);CHECK(reload->stale==2);CHECK(layout->items[0].placed);CHECK(layout->items[13].placed);
        setup(30,1280,720-ui_skaluj(40,dpi),dpi);int corrected=reload->apply(*layout,key);CHECK(corrected>0);CHECK(reload->dirty);
        for(int i=0;i<30;++i)if(layout->items[i].placed) {
            CHECK(layout->visible(i));for(int j=0;j<i;++j)if(layout->items[j].placed)CHECK(!Desktop::intersects(layout->visual_bounds(i),layout->visual_bounds(j)));
        }
        CHECK(layout->visual_bounds(0).x==0 && layout->visual_bounds(0).y==0); // unaffected valid position
        CHECK(reload->save(*layout,key,layout_test_io));CHECK(state->load(layout_test_io));
        std::vector<std::pair<int,int>> small;
        for(int i=0;i<30;++i){auto r=layout->visual_bounds(i);small.push_back({r.x,r.y});}
        setup(30,1920,1080-ui_skaluj(40,dpi),dpi);CHECK(state->apply(*layout,key)==0);
        for(int i=0;i<30;++i){auto r=layout->visual_bounds(i);CHECK(r.x==small[i].first && r.y==small[i].second);}
    }
    // Exact visual boundaries, including wrapped labels, survive reload.
    for(unsigned dpi:{100U,125U})for(bool multiline:{false,true})for(int edge=0;edge<4;++edge) {
        setup(1,1280,720-ui_skaluj(40,dpi),dpi);
        if(multiline) {
            auto m=pulpit_metryki(dpi);
            layout->items[0].label=Desktop::wrap("bardzo długa nazwa zażółconego folderu",m.etykieta_szer,m.wysokosc_linii,m.odstep_linii,16,dpi,oblicz_szerokosc_tekstu);
            CHECK(layout->items[0].label.count>1);
        }
        auto bounds=layout->visual_bounds(0);
        int x=edge==0?0:edge==1?layout->workWidth-bounds.szerokosc:300;
        int y=edge==2?0:edge==3?layout->workBottom-bounds.wysokosc:200;
        layout->visual_position(0,x,y);state->dirty=true;CHECK(state->save(*layout,key,layout_test_io));CHECK(reload->load(layout_test_io));
        layout->visual_position(0,400,200);CHECK(reload->apply(*layout,key)==0);CHECK(layout->visual_bounds(0).x==x && layout->visual_bounds(0).y==y);CHECK(layout->visible(0));
    }
    // All launchers + full filesystem capacity, including UTF-8 IDs.
    setup(Capacity,3840,2120,125);state->dirty=true;CHECK(state->save(*layout,key,layout_test_io));CHECK(reload->load(layout_test_io));CHECK(reload->count==Capacity);
    setup(Capacity,3840,2120,125);CHECK(reload->apply(*layout,key)==0);
    for(int i=0;i<Capacity;++i){CHECK(layout->items[i].placed);CHECK(layout->visible(i));}
    // Scale changes use saved DPI; every resulting visual bound is rechecked.
    setup(Capacity,3840,2120,100);CHECK(reload->apply(*layout,key)>=0);
    for(int i=0;i<Capacity;++i)if(layout->items[i].placed)CHECK(layout->visible(i));
    auto parse=[&](std::string text) {CHECK(text.size()<BufferCapacity);memcpy(state->buffer,text.data(),text.size());return state->parse(text.size());};
    CHECK(!parse("bad\nend\n"));CHECK(!parse(layout_document("BursztynDesktopLayout=2\n")));
    CHECK(!parse("BursztynDesktopLayout=1\nF\tfoo\t0\t0\n"));
    CHECK(parse(layout_document("BursztynDesktopLayout=1\ndpi=125\nfuture=hello\nL\tghost\t0\t0\tfuture\nF\told\t100\t200\n")));
    CHECK(state->count==2);setup(16,1280,670,125);CHECK(state->apply(*layout,key)==0);CHECK(state->stale==2);
    for(const char* bad:{"-1","2147483647","99999999999999999999999","abc","1.2","+3",""}) {
        CHECK(parse(layout_document(std::string("BursztynDesktopLayout=1\nF\tbad\t")+bad+"\t0\nF\tok\t20\t40\n")));CHECK(state->count==1);CHECK(equal(state->records[0].id,"ok"));
    }
    CHECK(parse(layout_document("BursztynDesktopLayout=1\nF\ta\t12\t24\nF\ta\t500\t600\nF\tpercent%25name\t0\t0\tfuture\nF\tbad%00id\t0\t0\nF\t../escape\t0\t0\n")));
    CHECK(state->count==2);CHECK(state->records[0].x==12 && state->records[0].y==24);CHECK(equal(state->records[1].id,"percent%name"));
    CHECK(parse(layout_document("BursztynDesktopLayout=1\nL\tnotatnik\t1000000\t1000000\nL\tkalkulator\t10\t10\nL\thusarz\t10\t10\n")));
    setup(14,1280,680,100);CHECK(state->apply(*layout,key)>0);
    for(int i=0;i<layout->count;++i)if(layout->items[i].placed){CHECK(layout->visible(i));for(int j=0;j<i;++j)if(layout->items[j].placed)CHECK(!Desktop::intersects(layout->visual_bounds(i),layout->visual_bounds(j)));}
    layout_exists=true;layout_disk.assign(BufferCapacity,'x');CHECK(!reload->load(layout_test_io));CHECK(!reload->count);
    auto good=layout_document("BursztynDesktopLayout=1\nF\ta\t12\t24\n");
    for(size_t n=0;n<good.size();++n)CHECK(!parse(good.substr(0,n)));
    good[30]^=1;CHECK(!parse(good)); // checksum catches torn content even with old footer
    state->dirty=true;layout_write_fail=true;auto r=layout->visual_bounds(0);int before=layout_writes;
    CHECK(!state->save(*layout,key,layout_test_io));CHECK(layout_writes==before+1);CHECK(!state->dirty);CHECK(layout->visual_bounds(0).x==r.x);
    CHECK(state->save(*layout,key,layout_test_io));CHECK(layout_writes==before+1);layout_write_fail=false;
    state->dirty=true;layout_read_fail=true;CHECK(!state->save(*layout,key,layout_test_io));CHECK(!reload->load(layout_test_io));CHECK(reload->count==0);layout_read_fail=false;
    state->dirty=true;layout_interrupt=true;CHECK(!state->save(*layout,key,layout_test_io));CHECK(!reload->load(layout_test_io));layout_interrupt=false;
    // Production gesture lifecycle: multiple motions, one write after capture release.
    g_test_bws_hook=layout_wm_io;layout_disk.clear();layout_exists=false;layout_writes=layout_io_captured=0;
    delete desktop_settings;desktop_settings=nullptr;desktop_layout={};liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;
    files={{"/",true},{"/uzytkownicy",true},{SCIEZKA_PULPIT,true},{std::string(SCIEZKA_PULPIT)+"/plik-żółty",false},{std::string(SCIEZKA_PULPIT)+"/folder",true}};
    screen_w=1920;screen_h=1080;ui_skala_procent=100;metryki_ekranu();CHECK(odswiez_wpisy_pulpitu());CHECK(!layout_exists && layout_writes==0);
    int launch=-1,file=-1;
    for(int i=0;i<desktop_layout.count;++i){if(wpisy_pulpitu[i].launcher==0)launch=i;if(!strcmp(wpisy_pulpitu[i].nazwa,"plik-żółty"))file=i;}
    CHECK(launch>=0 && file>=0);
    for(bool group:{false,true}) {
        desktop_ctrl(false);desktop_layout.click(launch,false);if(group)desktop_layout.click(file,true);
        auto a=wizualny_wpis_pulpitu(launch);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+20,a.y+10)));
        int written=layout_writes;
        for(int step=1;step<=5;++step)CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,a.x+20+step*150,a.y+10+step*100)));
        CHECK(layout_writes==written);CHECK(test_mouse_capture);
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,a.x+770,a.y+510)));
        CHECK(!test_mouse_capture);CHECK(layout_writes==written+1);CHECK(layout_io_captured==0);
    }
    CHECK(reload->load(layout_test_io));CHECK(reload->matchedFiles==0); // fresh state until apply
    std::map<std::string,std::pair<int,int>> saved;
    for(int i=0;i<desktop_layout.count;++i){auto k=klucz_ukladu_pulpitu(i);auto a=wizualny_wpis_pulpitu(i);saved[std::string(1,k.type)+k.id]={a.x,a.y};}
    delete desktop_settings;desktop_settings=nullptr;desktop_layout={};liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;
    CHECK(odswiez_wpisy_pulpitu());
    for(int i=0;i<desktop_layout.count;++i){auto k=klucz_ukladu_pulpitu(i);auto a=wizualny_wpis_pulpitu(i);auto expected=saved[std::string(1,k.type)+k.id];CHECK(a.x==expected.first && a.y==expected.second);CHECK(!desktop_layout.items[i].selected);}
    // Paint/theme, selection, launch and unchanged gestures never write.
    int written=layout_writes;desktop_layout.click(launch,false);desktop_layout.click(file,true);
    for(auto* source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY}){BursztynMotyw theme{};CHECK(motyw_parsuj(source,strlen(source),&theme));test_ustaw_aktywny_motyw(&theme);RysujPulpit(true);}
    test_ustaw_aktywny_motyw(nullptr);otworz_wpis_pulpitu(launch);zapisz_uklad_pulpitu();CHECK(layout_writes==written);
    auto a=wizualny_wpis_pulpitu(launch);desktop_ctrl(false);
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+20,a.y+10)));
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,a.x+20,a.y+10)));CHECK(layout_writes==written);
    layout_write_fail=true;
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+20,a.y+10)));
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,-9999,-9999)));
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,-9999,-9999)));CHECK(!test_mouse_capture);CHECK(layout_writes==written+1);
    CHECK(wizualny_wpis_pulpitu(launch).x!=a.x || wizualny_wpis_pulpitu(launch).y!=a.y);zapisz_uklad_pulpitu();CHECK(layout_writes==written+1);layout_write_fail=false;
    // Runtime modeset reflows only invalid entries and persists once.
    screen_w=1280;screen_h=720;metryki_ekranu();rozmiesc_ikony_ekranu();CHECK(desktop_settings->dirty);
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed)CHECK(desktop_layout.visible(i));
    written=layout_writes;zapisz_uklad_pulpitu();CHECK(layout_writes==written+1);CHECK(reload->load(layout_test_io));CHECK(reload->apply(desktop_layout,klucz_ukladu_pulpitu)==0);
    // A filesystem drop must not save the departed item's old position.
    int folder=-1;file=-1;
    for(int i=0;i<desktop_layout.count;++i){if(!strcmp(wpisy_pulpitu[i].nazwa,"folder"))folder=i;if(!strcmp(wpisy_pulpitu[i].nazwa,"plik-żółty"))file=i;}
    CHECK(folder>=0 && file>=0);desktop_layout.click(file,false);
    auto from=wizualny_wpis_pulpitu(file),to=wizualny_wpis_pulpitu(folder);written=layout_writes;
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,from.x+20,from.y+10)));
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,to.x+20,to.y+10)));
    CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,to.x+20,to.y+10)));
    CHECK(layout_writes==written);CHECK(!test_mouse_capture);
    CHECK(!files.count(std::string(SCIEZKA_PULPIT)+"/plik-żółty"));CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/folder/plik-żółty"));
    delete desktop_settings;desktop_settings=nullptr;desktop_layout={};liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;
    CHECK(odswiez_wpisy_pulpitu());CHECK(desktop_settings->stale==1);
    for(int i=0;i<desktop_layout.count;++i)CHECK(strcmp(wpisy_pulpitu[i].nazwa,"plik-żółty"));
    desktop_settings->dirty=true;zapisz_uklad_pulpitu();CHECK(reload->load(layout_test_io));
    for(int i=0;i<reload->count;++i)CHECK(!equal(reload->records[i].id,"plik-żółty"));
    // Rename has a new relative identity; deleting an entry also leaves no match.
    CHECK(zmien_nazwe_uzytkownika((std::string(SCIEZKA_PULPIT)+"/folder").c_str(),"nowy-folder"));
    hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());CHECK(reload->apply(desktop_layout,klucz_ukladu_pulpitu)>=0);CHECK(reload->stale==1);
    CHECK(usun_twor_uzytkownika((std::string(SCIEZKA_PULPIT)+"/nowy-folder").c_str()));
    hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
    for(int i=0;i<desktop_layout.count;++i)CHECK(strcmp(wpisy_pulpitu[i].nazwa,"nowy-folder"));
    g_test_bws_hook=explorer_bws;
}
