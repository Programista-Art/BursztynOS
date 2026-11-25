#pragma once
// Szybki dostep "Obrazy" (/dane/zdjecia) i "Muzyka" (/dane/muzyka), reczna sciezka, widocznosc zaimportowanych
// plikow (bez filtrow rozszerzen), UTF-8 i dlugie nazwy, odswiezanie, odmowa listowania oraz otwieranie obrazow
// dwuklikiem w Przegladarce Obrazow (prawdziwy plik, ogolny mechanizm programu z argumentem).
static int quick_index(const AktowkaModel& m,const char* label) {
    for(int i=0;i<m.quick_count();++i)if(m.quick(i)&&!strcmp(m.quick(i)->label,label))return i;
    return -1;
}
static bool visible(const AktowkaModel& m,const char* name,EntryType* type=nullptr) {
    for(int i=0;i<m.visible_count();++i)if(!strcmp(m.visible_entry(i)->name,name)){if(type)*type=m.visible_entry(i)->type;return true;}
    return false;
}
static void submit_typed_path(AktowkaView& v,const char* path) {
    v.begin_edit(EditMode::PATH);v.active_field().ustaw_tekst(path);
    CHECK(v.field_event(event(BWS_ZDARZENIE_KLAWISZ_FIZYCZNY,1,0,28)));
}
static void explorer_quick_access_cases() {
    const std::string dlugi=std::string("zdjęcie-")+std::string(55-strlen("zdjęcie-")-4,'x')+".png";   // 55 B UTF-8
    CHECK(PlikiUI::nazwa_poprawna(dlugi.c_str()) && dlugi.size()==55);
    files={{"/",true},{"/uzytkownicy",true},{"/uzytkownicy/Pulpit",true},{"/uzytkownicy/domyslny",true},
           {"/uzytkownicy/domyslny/Dokumenty",true},{"/programy",true},{"/system",true},
           {"/dane",true},{"/dane/zdjecia",true},{"/dane/muzyka",true},
           {"/dane/zdjecia/lightsterowanieobrazy.png",false},{"/dane/zdjecia/Wakacje żółć.JPG",false},
           {"/dane/zdjecia/portret.jpeg",false},{"/dane/zdjecia/stary.bmp",false},{"/dane/zdjecia/tapeta.btp",false},
           {"/dane/zdjecia/opis zdjęć.txt",false},{"/dane/zdjecia/bez_rozszerzenia",false},{"/dane/zdjecia/"+dlugi,false},
           {"/dane/zdjecia/Album",true},
           {"/dane/muzyka/Utwór ąę.mp3",false},{"/dane/muzyka/nagranie.wav",false}};
    const auto before=files;
    auto* m=new AktowkaModel();CHECK(m->initialize());
    // (1) wpisy: dokladne sciezki, po Dokumentach, nieprzypiete; poprzednie wpisy zachowane w tej samej kolejnosci
    const int dok=quick_index(*m,"Dokumenty"),obr=quick_index(*m,"Obrazy"),muz=quick_index(*m,"Muzyka");
    CHECK(dok>=0 && obr==dok+1 && muz==dok+2);
    CHECK(!strcmp(m->quick(obr)->path,"/dane/zdjecia") && !strcmp(m->quick(muz)->path,"/dane/muzyka"));
    CHECK(!m->quick(obr)->pinned && !m->quick(muz)->pinned);
    CHECK(quick_index(*m,"Pulpit")==0 && quick_index(*m,"Użytkownicy")==1 && quick_index(*m,"Programy")==2);
    CHECK(quick_index(*m,"System")>muz && m->quick(quick_index(*m,"System"))->pinned);
    CHECK(files==before);                                       // skroty nie tworza katalogow
    // (2) klikniecie wpisu w panelu (ta sama droga co petla zdarzen: quick_at -> navigate)
    AktowkaWindow w;auto* v=new AktowkaView(*m,w);v->draw_workspace();
    auto click_quick=[&](int index){
        const auto l=v->layout();
        for(int y=l.left.y;y<l.left.y+l.left.h;++y)if(v->quick_at(l.left.x+20,y)==index){
            present_workspace_change(*v,m->navigate(m->quick(index)->path));return true;}
        return false;
    };
    CHECK(click_quick(obr) && !strcmp(m->path(),"/dane/zdjecia"));
    // (3) wszystkie prawdziwe pliki widoczne - bez filtrow rozszerzen; obrazy rozpoznane, UTF-8 i 55 B
    EntryType t{};
    CHECK(visible(*m,"lightsterowanieobrazy.png",&t) && t==EntryType::IMAGE && !strcmp(entry_type_name(t),"Obraz"));
    CHECK(visible(*m,"Wakacje żółć.JPG",&t) && t==EntryType::IMAGE);
    CHECK(visible(*m,"portret.jpeg",&t) && t==EntryType::IMAGE);
    CHECK(visible(*m,"tapeta.btp",&t) && t==EntryType::IMAGE);
    CHECK(visible(*m,"stary.bmp",&t) && t==EntryType::BMP);
    CHECK(visible(*m,"opis zdjęć.txt",&t) && t==EntryType::TXT);
    CHECK(visible(*m,"bez_rozszerzenia",&t) && t==EntryType::FILE);
    CHECK(visible(*m,dlugi.c_str(),&t) && t==EntryType::IMAGE);
    CHECK(visible(*m,"Album",&t) && t==EntryType::FOLDER);
    int plikow=0;for(int i=0;i<m->visible_count();++i)plikow+=m->visible_entry(i)->type!=EntryType::PARENT;
    CHECK(plikow==9);
    // sortowanie A-Z / Z-A dotyczy wszystkich wpisow (kolejnosc odwrocona, ten sam zbior)
    std::vector<std::string> az,za;
    for(int i=0;i<m->visible_count();++i)az.push_back(m->visible_entry(i)->name);
    m->set_sort(SortMode::ZA);for(int i=0;i<m->visible_count();++i)za.push_back(m->visible_entry(i)->name);
    CHECK(az.size()==za.size() && std::is_permutation(az.begin(),az.end(),za.begin()) && az!=za);
    m->set_sort(SortMode::AZ);
    // (4) dwuklik obrazu: Przegladarka Obrazow z PELNA sciezka wybranego pliku (nie obraz demonstracyjny)
    for(const char* name:{"lightsterowanieobrazy.png","Wakacje żółć.JPG","portret.jpeg",dlugi.c_str()}) {
        select_name(*m,name);const int l0=launches,o0=associated_opens;
        CHECK(m->activate()==Activation::STATUS && launches==l0+1 && associated_opens==o0+1);
        CHECK(launched_app==SkojarzeniaPlikow::PRZEGLADARKA_OBRAZOW && launched_file==std::string("/dane/zdjecia/")+name);
        CHECK(!strcmp(m->status(),"Plik przekazano do skojarzonej aplikacji."));
    }
    select_name(*m,"opis zdjęć.txt");CHECK(m->activate()==Activation::STATUS && launched_app==SkojarzeniaPlikow::NOTATNIK);
    {   // plik bez skojarzenia: brak uruchomienia, czytelny komunikat
        select_name(*m,"bez_rozszerzenia");const int l0=launches;
        CHECK(m->activate()==Activation::STATUS && launches==l0 && !strcmp(m->status(),"Brak skojarzenia dla tego typu pliku."));
    }
    select_name(*m,"Album");CHECK(m->activate()==Activation::NAVIGATION && !strcmp(m->path(),"/dane/zdjecia/Album"));
    // (5) reczna sciezka (pole adresu): /dane/zdjecia i /dane/muzyka, takze z ukosnikiem na koncu
    for(auto [typed,canonical]:{std::pair<const char*,const char*>{"/dane/zdjecia","/dane/zdjecia"},{"/dane/muzyka/","/dane/muzyka"},
                                {"/dane/./zdjecia/../muzyka","/dane/muzyka"},{"/dane/zdjecia/","/dane/zdjecia"}}) {
        submit_typed_path(*v,typed);CHECK(!strcmp(m->path(),canonical) && v->edit_mode()==EditMode::NONE);
    }
    CHECK(visible(*m,"lightsterowanieobrazy.png"));
    // (6) Muzyka: pliki audio widoczne (bez skojarzenia - odtwarzanie przez Grajka)
    CHECK(click_quick(muz) && !strcmp(m->path(),"/dane/muzyka"));
    CHECK(visible(*m,"Utwór ąę.mp3",&t) && t==EntryType::FILE && visible(*m,"nagranie.wav"));
    // (7) odswiezenie: nowo zaimportowany plik pojawia sie po refresh
    CHECK(click_quick(obr));
    files["/dane/zdjecia/nowy import.png"]=false;
    CHECK(!visible(*m,"nowy import.png"));CHECK(m->refresh()!=Change::ERROR);CHECK(visible(*m,"nowy import.png"));
    // (8) odmowa listowania (np. PZB / brak katalogu): nawigacja nieudana, sciezka bez zmian, nic nie tworzone
    files.erase("/dane/muzyka/Utwór ąę.mp3");files.erase("/dane/muzyka/nagranie.wav");files.erase("/dane/muzyka");
    const auto przed=files;const std::string sciezka=m->path();
    CHECK(m->navigate("/dane/muzyka")==Change::ERROR && sciezka==m->path() && files==przed);
    delete v;delete m;
    // (9) bez katalogu danych wpis jest pomijany (bez tworzenia); pozostale wpisy bez zmian
    files={{"/",true},{"/uzytkownicy",true},{"/uzytkownicy/Pulpit",true},{"/dane",true},{"/dane/muzyka",true}};
    const auto bez=files;
    m=new AktowkaModel();CHECK(m->initialize());
    CHECK(quick_index(*m,"Obrazy")<0 && quick_index(*m,"Muzyka")>=0 && quick_index(*m,"Pulpit")==0 && files==bez);
    delete m;
    // (10) mapowanie skojarzen (wspolne z systemem): wielkosc liter, sciezki niepoprawne
    using namespace SkojarzeniaPlikow;
    CHECK(program_dla_pliku("/dane/zdjecia/a.PNG")==PRZEGLADARKA_OBRAZOW && program_dla_pliku("/x/b.Jpeg")==PRZEGLADARKA_OBRAZOW);
    CHECK(program_dla_pliku("/x/c.bmp")==PRZEGLADARKA_OBRAZOW && program_dla_pliku("/x/d.btp")==PRZEGLADARKA_OBRAZOW);
    CHECK(program_dla_pliku("/x/e.txt")==NOTATNIK && !program_dla_pliku("/x/f.mp3") && !program_dla_pliku("/x/png"));
    CHECK(!program_dla_pliku("wzgledna.png") && !program_dla_pliku(nullptr) && !program_dla_pliku("/x.png/"));
    CHECK(!program_dla_pliku((std::string("/")+std::string(600,'a')+".png").c_str()));
    launches=0;associated_opens=0;launched_app.clear();launched_file.clear();files.clear();   // stan atrapy dla kolejnych testow
    std::printf("explorer quick access: Obrazy=/dane/zdjecia, Muzyka=/dane/muzyka, obrazy -> Przegladarka Obrazow OK\n");
}
