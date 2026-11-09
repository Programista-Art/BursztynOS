#pragma once
#include <set>
struct DesktopPublication {uint64_t generation;std::set<std::string> ids;};
static std::vector<DesktopPublication> desktop_publications;
static std::set<std::string> visible_ids() {
    std::set<std::string> ids;
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed) {
        auto key=klucz_ukladu_pulpitu(i);CHECK(ids.insert(std::string(1,key.type)+key.id).second);
    }
    return ids;
}
static void observe_desktop(){desktop_publications.push_back({desktop_model_generation,visible_ids()});}
static void observe_empty_desktop(){desktop_publications.push_back({desktop_model_generation,{}});}
static void desktop_mutation_cases() {
    auto physical=[](uint32_t code){bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;e.x=1;e.kod=code;return e;};
    const char* names[]={"0","1","2","3","4","5","6","7","8","9","10","11","12","17","18","19","20","21","22","27","28","29",
        "70","71","72","78","79","80","81","82","88","89","90","91","98","99","1234567890",
        "folder 8","folder 9","folder 17","folder 21","folder 29","Folder 21 Żółty"};
    FILE* trace=fopen(".bursztyn-ui-test/desktop-publications.tsv","w");CHECK(trace);
    for(const char* name:names) {
        capacity_reset(3,100);desktop_pending_dirty={};CHECK(otworz_pulpit_modal(PulpitModalTyp::FOLDER));
        auto* focus=pulpit_dialog->events.popups.fokus_popupu();
        for(size_t i=0;name[i];) {
            uint32_t cp;size_t used=CzcionkaUI::dekoduj_utf8(name,i,&cp);CHECK(used);
            if(cp>='0' && cp<='9')CHECK(obsluz_popupy_pulpitu(physical(cp=='0'?11:cp-'0'+1)));
            bws_zdarzenie t{};t.typ=BWS_ZDARZENIE_TEXT_INPUT;t.kod=cp;CHECK(obsluz_popupy_pulpitu(t));
            CHECK(pulpit_dialog->wynik()==0);CHECK(pulpit_dialog->events.popups.fokus_popupu()==focus);i+=used;
        }
        CHECK(!strcmp(pulpit_dialog->tekst(),name));CHECK(!files.count(std::string(SCIEZKA_PULPIT)+"/"+name));
        auto old=visible_ids();const auto generation=desktop_model_generation;const int writes=layout_writes;
        std::map<std::string,std::pair<int,int>> positions;
        for(int i=0;i<desktop_layout.count;++i){auto r=wizualny_wpis_pulpitu(i);positions[wpisy_pulpitu[i].nazwa]={r.x,r.y};}
        desktop_publications={{generation,old}};test_publish_observer=observe_desktop;test_layer_clear_observer=observe_empty_desktop;
        test_paint_rects.clear();test_capture_paints=true;
        CHECK(obsluz_popupy_pulpitu(physical(28)));
        test_capture_paints=false;
        CHECK(files.count(std::string(SCIEZKA_PULPIT)+"/"+name));CHECK(desktop_model_generation==generation+1);
        auto expected=old;expected.insert(std::string("F")+name);
        CHECK(desktop_publications.size()==2);CHECK(desktop_publications[1].ids==expected);
        CHECK(layout_writes==writes+1);
        for(int i=0;i<desktop_layout.count;++i)if(positions.count(wpisy_pulpitu[i].nazwa)) {
            auto r=wizualny_wpis_pulpitu(i);CHECK(positions[wpisy_pulpitu[i].nazwa]==std::make_pair(r.x,r.y));
        }
        for(auto r:test_paint_rects)CHECK(r.w<screen_w || r.h<screen_h-PASEK_WYS);
        // Direct refresh plus repeated watcher notifications: no new model,
        // pixels, publication or persistence when the listing is unchanged.
        bws_zdarzenie watch{};watch.typ=BWS_ZDARZENIE_PLIKI_ZMIENIONE;
        for(int i=0;i<5;++i)CHECK(obsluz_zmiane_plikow_pulpitu(watch));
        CHECK(desktop_publications.size()==2);CHECK(layout_writes==writes+1);CHECK(desktop_model_generation==generation+1);
        for(auto& pub:desktop_publications) {
            fprintf(trace,"create:%s\t%llu\t",name,(unsigned long long)pub.generation);
            for(auto& id:pub.ids)fprintf(trace,"%s;",id.c_str());fprintf(trace,"\n");
        }
        test_publish_observer=nullptr;test_layer_clear_observer=nullptr;
    }
    // Delete a nested user directory on a full desktop, admit a waiting item
    // into the freed slot, retaining every other placed identity/coordinate.
    capacity_reset(128,100);desktop_pending_dirty={};
    int victim=-1;std::string waiting;
    for(int i=0;i<desktop_layout.count;++i)if(wpisy_pulpitu[i].launcher<0) {
        if(victim<0 && desktop_layout.items[i].placed)victim=i;
        if(waiting.empty() && !desktop_layout.items[i].placed)waiting=wpisy_pulpitu[i].nazwa;
    }
    CHECK(victim>=0 && !waiting.empty());std::string removed=wpisy_pulpitu[victim].nazwa;
    std::string path=std::string(SCIEZKA_PULPIT)+"/"+removed;files[path]=true;
    files[path+"/nested"]=true;files[path+"/nested/żółty.txt"]=false;
    hash_pulpitu_wazny=false;CHECK(odswiez_wpisy_pulpitu());desktop_pending_dirty={};
    auto old=visible_ids();const auto generation=desktop_model_generation;
    snprintf(pulpit_cel,sizeof(pulpit_cel),"%s",path.c_str());pulpit_batch_count=0;
    CHECK(otworz_pulpit_modal(PulpitModalTyp::REMOVE));
    pulpit_dialog->events.popups.ustaw_fokus_popupu(pulpit_dialog->dziecko(3));
    desktop_publications={{generation,old}};test_publish_observer=observe_desktop;test_layer_clear_observer=observe_empty_desktop;
    CHECK(obsluz_popupy_pulpitu(physical(28)));
    CHECK(!files.count(path));CHECK(!files.count(path+"/nested"));CHECK(!files.count(path+"/nested/żółty.txt"));
    auto expected=old;expected.erase("F"+removed);expected.insert("F"+waiting);
    CHECK(desktop_publications.size()==2);CHECK(desktop_publications[1].ids==expected);
    CHECK(desktop_model_generation==generation+1);capacity_invariants();
    for(auto& pub:desktop_publications) {
        fprintf(trace,"delete:%s\t%llu\t",removed.c_str(),(unsigned long long)pub.generation);
        for(auto& id:pub.ids)fprintf(trace,"%s;",id.c_str());fprintf(trace,"\n");
    }
    test_publish_observer=nullptr;test_layer_clear_observer=nullptr;
    // A watcher-only mutation is deferred while a modal owns the field; the
    // existing release/timer path then commits one complete model.
    capacity_reset(3);desktop_pending_dirty={};CHECK(otworz_pulpit_modal(PulpitModalTyp::FOLDER));
    auto before=visible_ids();const auto deferred_generation=desktop_model_generation;
    desktop_publications={{deferred_generation,before}};test_publish_observer=observe_desktop;test_layer_clear_observer=observe_empty_desktop;
    files[std::string(SCIEZKA_PULPIT)+"/watcher Żółty"]=true;
    bws_zdarzenie watch{};watch.typ=BWS_ZDARZENIE_PLIKI_ZMIENIONE;
    for(int i=0;i<20;++i)CHECK(obsluz_zmiane_plikow_pulpitu(watch));
    CHECK(desktop_publications.size()==1);CHECK(visible_ids()==before);CHECK(desktop_model_generation==deferred_generation);
    zamknij_pulpit_modal();odswiez_pulpit_natychmiast();
    auto after=before;after.insert("Fwatcher Żółty");CHECK(desktop_publications.size()==2);CHECK(desktop_publications.back().ids==after);
    for(int i=0;i<20;++i)CHECK(obsluz_zmiane_plikow_pulpitu(watch));CHECK(desktop_publications.size()==2);
    fclose(trace);test_publish_observer=nullptr;test_layer_clear_observer=nullptr;
}
