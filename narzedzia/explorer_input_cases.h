#pragma once
static void explorer_input_cases() {
    const char* names[]={"0","1","2","3","4","5","6","7","8","9","10","11","12","17","18","19","20","21","22","27","28","29",
        "70","71","72","78","79","80","81","82","88","89","90","91","98","99","1234567890",
        "folder 8","folder 9","folder 17","folder 21","folder 29","Folder 21 Żółty"};
    for(auto mode:{EditMode::CREATE_FOLDER,EditMode::CREATE_FILE,EditMode::RENAME})for(const char* name:names) {
        files={{"/",true},{"/test",true},{"/test/original",false}};
        auto* m=new AktowkaModel();CHECK(m->initialize());CHECK(m->navigate("/test")!=Change::ERROR);
        select_name(*m,"original");AktowkaWindow w;auto* v=new AktowkaView(*m,w);CHECK(v->open_modal(mode));
        auto ctrl=event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,0x1e);ctrl.przyciski=BWS_MOD_CTRL;CHECK(v->popup_event(ctrl));
        for(size_t i=0;name[i];) {
            uint32_t cp;size_t used=CzcionkaUI::dekoduj_utf8(name,i,&cp);CHECK(used);
            if(cp>='0' && cp<='9')CHECK(v->popup_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,cp=='0'?11:cp-'0'+1)));
            CHECK(v->popup_event(event(BWS_ZDARZENIE_TEXT_INPUT,0,0,cp)));CHECK(v->modal_result()==0);i+=used;
        }
        CHECK(!strcmp(v->edit_text(),name));CHECK(!files.count(std::string("/test/")+name));
        CHECK(v->popup_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,28)));CHECK(v->modal_result()==1);
        submit_modal(*v,*m);CHECK(files.count(std::string("/test/")+name));CHECK(!v->dialog_open());
        if(mode==EditMode::RENAME)CHECK(!files.count("/test/original"));
        delete v;delete m;
    }
    // Inline path/search use the same command normalization and ignore echoes.
    files={{"/",true},{"/test",true},{"/test/21",true},{"/test/Folder 21 Żółty",true}};
    auto* m=new AktowkaModel();CHECK(m->initialize());CHECK(m->navigate("/test")!=Change::ERROR);
    AktowkaWindow w;auto* v=new AktowkaView(*m,w);v->begin_edit(EditMode::PATH);v->active_field().ustaw_tekst("/test/");
    for(uint32_t cp:{uint32_t('2'),uint32_t('1')}) {
        CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,cp-'0'+1)));
        CHECK(v->field_event(event(BWS_ZDARZENIE_TEXT_INPUT,0,0,cp)));
        CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,cp)));
    }
    CHECK(!strcmp(v->edit_text(),"/test/21"));CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,28)));
    CHECK(!strcmp(m->path(),"/test/21"));
    CHECK(obsluz_nawigacje_fizyczna(*v,*m,event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,14)));
    CHECK(!strcmp(m->path(),"/test"));CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,8)));
    CHECK(!strcmp(m->path(),"/test"));select_name(*m,"21");
    CHECK(obsluz_nawigacje_fizyczna(*v,*m,event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,28)));
    CHECK(!strcmp(m->path(),"/test/21"));CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,10)));
    CHECK(!strcmp(m->path(),"/test/21"));
    for(auto mode:{EditMode::PATH,EditMode::SEARCH}) {
        v->begin_edit(mode);CHECK(v->edit_mode()==mode);
        CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,1)));
        CHECK(v->edit_mode()==EditMode::NONE);CHECK(!strcmp(m->path(),"/test/21"));
        CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,27)));
        CHECK(v->edit_mode()==EditMode::NONE);
    }
    CHECK(m->navigate("/test")!=Change::ERROR);select_name(*m,"21");const int before=m->selected();
    CHECK(obsluz_nawigacje_fizyczna(*v,*m,event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,BWS_KEY_SOURCE_E0|0x50)));
    CHECK(m->selected()!=before);const int after=m->selected();
    for(uint32_t c:{27U,91U,66U})CHECK(v->field_event(event(BWS_ZDARZENIE_KLAWISZ,0,0,c)));
    CHECK(m->selected()==after);
    CHECK(obsluz_nawigacje_fizyczna(*v,*m,event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,BWS_KEY_SOURCE_E0|0x48)));
    CHECK(m->selected()==before);delete v;delete m;
}
