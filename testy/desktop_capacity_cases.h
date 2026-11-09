#pragma once
// Exercise the actual WM admission, selection, file-action and persistence paths.
static bool capacity_reverse_listing=false;
static uint64_t capacity_wm_io(uint64_t nr,uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    if(nr==6 && capacity_reverse_listing && !strcmp(reinterpret_cast<const char*>(a),SCIEZKA_PULPIT)) {
        std::string listing,prefix=std::string(SCIEZKA_PULPIT)+"/";
        for(auto it=files.rbegin();it!=files.rend();++it)if(it->first.compare(0,prefix.size(),prefix)==0) {
            auto name=it->first.substr(prefix.size());if(name.empty() || name.find('/')!=std::string::npos)continue;
            listing+=(it->second?"[KAT]  ":"[PLIK] ")+name+"\n";
        }
        if(listing.size()+1>c)return 0;
        memcpy(reinterpret_cast<void*>(b),listing.c_str(),listing.size()+1);return 1;
    }
    return layout_wm_io(nr,a,b,c,d);
}
static int capacity_visible() {int n=0;for(int i=0;i<desktop_layout.count;++i)n+=desktop_layout.items[i].placed;return n;}
static void capacity_invariants() {
    CHECK(desktop_layout.count==liczba_wpisow_pulpitu);
    CHECK(desktop_layout.count<=PULPIT_MAX_WPISOW);
    CHECK(desktop_layout.searchIterations<=uint64_t(desktop_layout.count)*desktop_layout.candidate_limit());
    int launchers=0;
    for(int i=0;i<desktop_layout.count;++i) {
        launchers+=wpisy_pulpitu[i].launcher>=0;
        auto& p=desktop_layout.items[i];if(!p.placed){CHECK(!p.selected);continue;}
        CHECK(desktop_layout.visible(i));
        for(int j=0;j<i;++j)if(desktop_layout.items[j].placed)CHECK(!Desktop::intersects(wizualny_wpis_pulpitu(i),wizualny_wpis_pulpitu(j)));
    }
    CHECK(launchers==DesktopEntries::Count);
}
static void capacity_reset(int n,unsigned dpi=100) {
    capacity_reverse_listing=false;g_test_bws_hook=capacity_wm_io;
    delete desktop_settings;desktop_settings=nullptr;desktop_layout={};liczba_wpisow_pulpitu=0;
    hash_pulpitu_wazny=false;hash_pulpitu=hash_modelu_pulpitu=0;pulpit_nadmiar_modelu=0;
    layout_disk.clear();layout_exists=false;layout_writes=0;
    deny_operation=false;deny_path.clear();anuluj_gest_pulpitu();zamknij_pulpit_modal();desktop_ctrl(false);
    files={{"/",true},{SCIEZKA_PULPIT,true},{"/dest",true}};
    for(int i=0;i<n;++i){char name[32];snprintf(name,sizeof(name),"f%03d",i);files[std::string(SCIEZKA_PULPIT)+"/"+name]=false;}
    screen_w=1280;screen_h=720;ui_skala_procent=dpi;metryki_ekranu();rozmiesc_ikony_ekranu();CHECK(odswiez_wpisy_pulpitu());capacity_invariants();
}
static void desktop_capacity_cases() {
    for(unsigned dpi:{100U,125U}) {
        capacity_reset(128,dpi);const int visual=capacity_visible();
        printf("Production capacity 1280x720 dpi=%u: %d visible (%d launchers), grid=%d, candidates=%llu\n",dpi,visual,DesktopEntries::Count,desktop_layout.cols*desktop_layout.rows,(unsigned long long)desktop_layout.candidate_limit());
        for(auto& launcher:DesktopEntries::Launchers)CHECK(desktop_layout.items[desktop_find(launcher.name)].placed);
        for(int user:{0,1,visual-DesktopEntries::Count-1,visual-DesktopEntries::Count,visual-DesktopEntries::Count+1,128,129,180}) {
            capacity_reset(user,dpi);CHECK(desktop_layout.count==DesktopEntries::Count+(user<128?user:128));CHECK(pulpit_nadmiar_modelu==(user>128?user-128:0));
            CHECK(capacity_visible()==(user+DesktopEntries::Count<visual?user+DesktopEntries::Count:visual));
            CHECK(files.size()==size_t(user+3));CHECK(!odswiez_wpisy_pulpitu());capacity_invariants();
        }
        capacity_reset(128,dpi);std::vector<std::string> waiting;
        std::map<std::string,std::pair<int,int>> saved;
        for(int i=0;i<desktop_layout.count;++i)if(!desktop_layout.items[i].placed)waiting.push_back(wpisy_pulpitu[i].nazwa);
        CHECK(waiting.size()>3);
        for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed){auto k=klucz_ukladu_pulpitu(i);auto r=wizualny_wpis_pulpitu(i);saved[std::string(1,k.type)+k.id]={r.x,r.y};}
        // Stable model order independent of the kernel's enumeration order.
        capacity_reverse_listing=true;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());CHECK(!desktop_settings->dirty);
        for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed){auto k=klucz_ukladu_pulpitu(i);auto r=wizualny_wpis_pulpitu(i);CHECK(saved[std::string(1,k.type)+k.id]==std::make_pair(r.x,r.y));}
        capacity_reverse_listing=false;
        // Ctrl+A and rubber band ignore logical overflow; empty hit regions
        // never resolve to a waiting item.
        auto e=desktop_event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1);e.kod=0x1e;e.przyciski=BWS_MOD_CTRL;CHECK(obsluz_gest_pulpitu(e));CHECK(desktop_layout.selected_count()==visual);
        for(int i=0;i<desktop_layout.count;++i)if(!desktop_layout.items[i].placed)CHECK(!desktop_layout.items[i].selected);
        desktop_ctrl(false);desktop_layout.clear_selection();
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,0,0)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,screen_w-1,screen_h-PASEK_WYS-1)));
        CHECK(desktop_layout.selected_count()==visual);CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,screen_w-1,screen_h-PASEK_WYS-1)));
        for(int y=0;y<screen_h-PASEK_WYS;y+=19)for(int x=0;x<screen_w;x+=23){int hit=wpis_pulpitu_w_punkcie(x,y);CHECK(hit<0 || desktop_layout.items[hit].placed);}
        desktop_layout.clear_selection();
        // Requested occupied group destination commits one common translation
        // or a clean no-op; other placed icons and filesystem objects stay put.
        for(bool builtin:{false,true}) {
            int first=builtin?desktop_find("Notatnik"):desktop_find("f000");
            int second=builtin?desktop_find("Kalkulator"):desktop_find("f001");
            desktop_layout.click(first,false);desktop_layout.click(second,true);
            auto beforeMove=desktop_layout;auto fsBefore=files;auto a=wizualny_wpis_pulpitu(first),b=wizualny_wpis_pulpitu(second);
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+a.szerokosc/2,a.y+10)));
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,screen_w+9999,screen_h+9999)));
            CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,screen_w+9999,screen_h+9999)));
            auto aa=wizualny_wpis_pulpitu(first),bb=wizualny_wpis_pulpitu(second);
            CHECK(aa.x-a.x==bb.x-b.x && aa.y-a.y==bb.y-b.y);CHECK(files==fsBefore);CHECK(!test_mouse_capture);
            CHECK(desktop_layout.searchIterations>0);capacity_invariants();
            for(int i=0;i<desktop_layout.count;++i)if(beforeMove.items[i].placed && i!=first && i!=second){auto a=beforeMove.visual_bounds(i),b=desktop_layout.visual_bounds(i);CHECK(a.x==b.x && a.y==b.y);}
        }
        desktop_layout.clear_selection();
        // Use the updated valid positions when checking preservation on delete.
        saved.clear();for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed){auto k=klucz_ukladu_pulpitu(i);auto r=wizualny_wpis_pulpitu(i);saved[std::string(1,k.type)+k.id]={r.x,r.y};}
        // Real filesystem delete opens one slot. Preserve all other placements.
        int victim=-1;for(int i=0;i<desktop_layout.count;++i)if(wpisy_pulpitu[i].launcher<0 && desktop_layout.items[i].placed){victim=i;break;}
        CHECK(victim>=0);std::string departed=wpisy_pulpitu[victim].nazwa;
        int written=layout_writes;
        CHECK(usun_twor_uzytkownika((std::string(SCIEZKA_PULPIT)+"/"+departed).c_str()));odswiez_pulpit_natychmiast();
        CHECK(desktop_layout.items[desktop_find(waiting[0].c_str())].placed);CHECK(capacity_visible()==visual);CHECK(layout_writes==written+1);capacity_invariants();
        for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed && std::string(wpisy_pulpitu[i].nazwa)!=waiting[0]){auto k=klucz_ukladu_pulpitu(i);auto r=wizualny_wpis_pulpitu(i);CHECK(saved[std::string(1,k.type)+k.id]==std::make_pair(r.x,r.y));}
        // Reboot with overflow, old v1 format, and deterministic save/load/save.
        std::string disk=layout_disk;int visible=capacity_visible();delete desktop_settings;desktop_settings=nullptr;desktop_layout={};liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;
        CHECK(odswiez_wpisy_pulpitu());CHECK(capacity_visible()==visible);desktop_settings->dirty=true;zapisz_uklad_pulpitu();CHECK(layout_disk==disk);capacity_invariants();
        // Resolution and actual DPI update paths retain valid old positions and
        // admit waiting files only into free geometry.
        auto before=desktop_layout;screen_w=1920;screen_h=1080;metryki_ekranu();rozmiesc_ikony_ekranu();CHECK(capacity_visible()>visual);
        for(int i=0;i<before.count;++i)if(before.items[i].placed){auto a=before.visual_bounds(i),b=desktop_layout.visual_bounds(i);CHECK(a.x==b.x && a.y==b.y);}capacity_invariants();
        screen_w=1280;screen_h=720;ui_skala_procent=125;metryki_ekranu();rozmiesc_ikony_ekranu();capacity_invariants();int high=capacity_visible();
        ui_skala_procent=100;metryki_ekranu();rozmiesc_ikony_ekranu();capacity_invariants();CHECK(capacity_visible()>=high);
    }
    // Folder targets and all file mutations stay independent from visual space.
    capacity_reset(128);files[std::string(SCIEZKA_PULPIT)+"/f000"]=true;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());
    int folder=desktop_find("f000"),source=desktop_find("f001");CHECK(desktop_layout.items[folder].placed && desktop_layout.items[source].placed);
    desktop_layout.click(source,false);CHECK(wykonaj_drop_pulpitu(folder));CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/f000/f001"));capacity_invariants();
    // User creation through the production modal at a full visual desktop.
    CHECK(otworz_pulpit_modal(PulpitModalTyp::FOLDER));static_cast<PoleTekstowe*>(pulpit_dialog->dziecko(2))->ustaw_tekst("zz-folder");zatwierdz_pulpit_modal();
    CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/zz-folder"));CHECK(!desktop_layout.items[desktop_find("zz-folder")].placed);capacity_invariants();
    files["/dest/zz-copy"]=false;files["/dest/zz-cut"]=false;
    for(auto op:{BWS_SCHOWEK_COPY,BWS_SCHOWEK_CUT}) {
        const char* path=op==BWS_SCHOWEK_COPY?"/dest/zz-copy":"/dest/zz-cut";CHECK(ustaw_schowek_plikow(path,op));
        deny_operation=true;CHECK(PlikiUI::wklej(SCIEZKA_PULPIT)==PlikiUI::WynikWklejenia::Blad);CHECK(files.count(path));CHECK(file_clip.operacja==op);
        deny_operation=false;CHECK(PlikiUI::wklej(SCIEZKA_PULPIT)==PlikiUI::WynikWklejenia::OK);odswiez_pulpit_natychmiast();
        CHECK(files.count(std::string(SCIEZKA_PULPIT)+(op==BWS_SCHOWEK_COPY?"/zz-copy":"/zz-cut")));CHECK(bool(files.count(path))==(op==BWS_SCHOWEK_COPY));capacity_invariants();
    }
    CHECK(pulpit_nadmiar_modelu==2);CHECK(desktop_layout.count==PULPIT_MAX_WPISOW);
    // A lexically earlier newcomer must not evict a valid existing icon when
    // the logical model is full, including after reboot with saved identities.
    desktop_settings->dirty=true;zapisz_uklad_pulpitu();
    std::map<std::string,std::pair<int,int>> visibleBefore;
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed){auto k=klucz_ukladu_pulpitu(i);auto r=wizualny_wpis_pulpitu(i);visibleBefore[std::string(1,k.type)+k.id]={r.x,r.y};}
    files[std::string(SCIEZKA_PULPIT)+"/aaa-new"]=false;odswiez_pulpit_natychmiast();CHECK(desktop_find("aaa-new")<0);CHECK(pulpit_nadmiar_modelu==3);
    for(bool reboot:{false,true}) {
        if(reboot){delete desktop_settings;desktop_settings=nullptr;desktop_layout={};liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());}
        for(auto saved:visibleBefore) {
            int found=-1;for(int i=0;i<desktop_layout.count;++i){auto k=klucz_ukladu_pulpitu(i);if(std::string(1,k.type)+k.id==saved.first)found=i;}
            CHECK(found>=0 && desktop_layout.items[found].placed);auto r=wizualny_wpis_pulpitu(found);CHECK(saved.second==std::make_pair(r.x,r.y));
        }
        capacity_invariants();
    }
    // Similar long UTF-8 identities remain exact, never compared by labels.
    capacity_reset(0);std::string prefix;for(int i=0;i<12;++i)prefix+="żółć";
    for(auto suffix:{"a","b"})files[std::string(SCIEZKA_PULPIT)+"/"+prefix+suffix]=false;
    files[std::string(SCIEZKA_PULPIT)+"/Notatnik"]=false;hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());CHECK(desktop_find((prefix+"a").c_str())>=0 && desktop_find((prefix+"b").c_str())>=0);
    CHECK(desktop_layout.count==DesktopEntries::Count+3);capacity_invariants();
    g_test_bws_hook=explorer_bws;
}
