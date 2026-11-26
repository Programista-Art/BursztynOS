#pragma once
#include "app_identity.h"
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
           {"/dane/muzyka/Utwór ąę.mp3",false},{"/dane/muzyka/nagranie.wav",false},
           {"/dane/muzyka/Bursztyn OS2.MP3",false},{"/dane/muzyka/Electric Pulse.WAV",false},
           {"/dane/muzyka/Album",true},{"/dane/muzyka/Album/Ścieżka 1.mp3",false},{"/dane/muzyka/okładka.png",false}};
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
    // (6) Muzyka: pliki audio widoczne; dwuklik MP3/WAV (tez wielkie litery, spacje, UTF-8, podkatalog) otwiera
    //     Grajka z PELNA sciezka wybranego pliku - ten sam ogolny mechanizm (program + argument startowy)
    CHECK(click_quick(muz) && !strcmp(m->path(),"/dane/muzyka"));
    CHECK(visible(*m,"Utwór ąę.mp3",&t) && t==EntryType::FILE && visible(*m,"nagranie.wav"));
    for(const char* name:{"Utwór ąę.mp3","nagranie.wav","Bursztyn OS2.MP3","Electric Pulse.WAV"}) {
        select_name(*m,name);const int l0=launches;
        CHECK(m->activate()==Activation::STATUS && launches==l0+1);
        CHECK(launched_app==SkojarzeniaPlikow::GRAJEK && launched_file==std::string("/dane/muzyka/")+name);
    }
    select_name(*m,"okładka.png");CHECK(m->activate()==Activation::STATUS && launched_app==SkojarzeniaPlikow::PRZEGLADARKA_OBRAZOW);
    select_name(*m,"Album");CHECK(m->activate()==Activation::NAVIGATION);
    select_name(*m,"Ścieżka 1.mp3");CHECK(m->activate()==Activation::STATUS && launched_file=="/dane/muzyka/Album/Ścieżka 1.mp3");
    CHECK(m->back()!=Change::ERROR && !strcmp(m->path(),"/dane/muzyka"));
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
    CHECK(program_dla_pliku("/x/e.txt")==NOTATNIK && !program_dla_pliku("/x/png") && !program_dla_pliku("/x/f.flac"));
    CHECK(program_dla_pliku("/x/f.mp3")==GRAJEK && program_dla_pliku("/x/g.WAV")==GRAJEK && program_dla_pliku("/x/h.Mp3")==GRAJEK);
    CHECK(!program_dla_pliku("/x/mp3") && !program_dla_pliku("/x/.mp3/") && !program_dla_pliku("x.wav") && !program_dla_pliku("/x/a.mp3.txt.png.wav.ogg"));
    CHECK(!strcmp(GRAJEK,AppIdentity::MusicPlayer));
    CHECK(!program_dla_pliku("wzgledna.png") && !program_dla_pliku(nullptr) && !program_dla_pliku("/x.png/"));
    CHECK(!program_dla_pliku((std::string("/")+std::string(600,'a')+".png").c_str()));
    launches=0;associated_opens=0;launched_app.clear();launched_file.clear();files.clear();   // stan atrapy dla kolejnych testow
    std::printf("explorer quick access: Obrazy=/dane/zdjecia, Muzyka=/dane/muzyka, obrazy -> Przegladarka Obrazow OK\n");
}

// Przewijany panel szybkiego dostepu: widocznosc paska, kolko, tor i przeciaganie suwaka, klawiatura (Tab, strzalki,
// Home/End, Enter, Esc), automatyczne przewijanie do fokusu, minimalna wysokosc, zmiana rozmiaru w stanie
// przewinietym, trafienia po przewinieciu, odrysowanie tylko panelu, zachowanie historii nawigacji.
static void sidebar_invariants(AktowkaView& v,AktowkaModel& m) {
    const auto view=v.quick_view();const auto bar=v.quick_bar_rect();const auto l=v.layout();
    CHECK(v.quick_first()>=0 && v.quick_first()<=v.quick_max_first());
    int last_bottom=view.y,visible=0;
    for(int i=0;i<m.quick_count();++i) {
        Rect r{};if(!v.quick_row_rect(i,&r))continue;++visible;
        CHECK(i>=v.quick_first());
        CHECK(r.y>=last_bottom && r.x>=view.x && r.y>=view.y && r.y+r.h<=view.y+view.h && r.x+r.w<=view.x+view.w);  // w panelu
        CHECK(bar.w==0 || r.x+r.w<=bar.x);                                   // nie pod paskiem
        last_bottom=r.y+r.h;
        CHECK(v.quick_at(r.x+r.w/2,r.y+r.h/2)==i && v.quick_at(r.x+1,r.y)==i && v.quick_at(r.x+r.w-1,r.y+r.h-1)==i);
    }
    CHECK(visible==v.quick_visible_count());
    CHECK(v.quick_at(l.left.x+5,l.left.y+2)==-1);                           // tytul panelu
    CHECK(v.quick_at(l.center.x+5,view.y+5)==-1 && v.quick_at(l.left.x-1,view.y+5)==-1);   // poza panelem
    if(bar.w>0)for(int y=bar.y;y<bar.y+bar.h;y+=5)CHECK(v.quick_at(bar.x+bar.w/2,y)==-1);    // pasek nie aktywuje
    CHECK((bar.w>0)==v.test_quick_scrollbar().czy_widoczna());
    if(bar.w>0)CHECK(v.test_quick_scrollbar().maksimum()==v.quick_max_first() && v.test_quick_scrollbar().wartosc()==v.quick_first());
    // kazda pozycja osiagalna przy jakims przewinieciu (zadna nie jest utracona)
    if(view.h>=ROW_H+AktowkaView::PINNED_HEADER_H) {
        std::vector<bool> reach(m.quick_count(),false);const int keep=v.quick_first();
        for(int f=0;f<=v.quick_max_first();++f){v.scroll_quick_by(f-v.quick_first());for(int i=0;i<m.quick_count();++i)if(v.quick_row_rect(i,nullptr))reach[i]=true;}
        for(bool b:reach)CHECK(b);
        v.scroll_quick_by(keep-v.quick_first());
    }
}
static void explorer_sidebar_scroll_cases() {
    files={{"/",true},{"/uzytkownicy",true},{"/uzytkownicy/Pulpit",true},{"/uzytkownicy/domyslny",true},
           {"/uzytkownicy/domyslny/Dokumenty",true},{"/programy",true},{"/gry",true},{"/kosz",true},{"/system",true},
           {"/logi",true},{"/tymczasowe",true},{"/dane",true},{"/dane/zdjecia",true},{"/dane/muzyka",true}};
    auto* m=new AktowkaModel();CHECK(m->initialize());CHECK(m->quick_count()==11);
    AktowkaWindow w;w.h=900;auto* v=new AktowkaView(*m,w);v->draw_workspace();
    // (1) wszystko sie miesci: brak paska, brak przewijania, kolko nic nie zmienia
    CHECK(v->quick_max_first()==0 && v->quick_bar_rect().w==0 && v->quick_visible_count()==11);
    sidebar_invariants(*v,*m);
    auto wheel=[&](int dy){auto e=event(BWS_ZDARZENIE_MYSZ_KOLO,v->layout().left.x+10,v->quick_view().y+10);e.dy=dy;return v->quick_event(e,nullptr);};
    CHECK(wheel(-1)==AktowkaView::QuickInput::HANDLED && v->quick_first()==0);
    // (2) minimalna wysokosc okna i mniejsze: pasek widoczny, pozycje tylko w calosci, wszystkie osiagalne
    for(int h:{MIN_H,330,300}) {
        w.h=h;v->draw_workspace();sidebar_invariants(*v,*m);
    }
    CHECK(v->quick_max_first()>0 && v->quick_bar_rect().w>0 && v->quick_visible_count()<11);
    CHECK(v->layout().list_body.w==calculate_layout(w,m->visible_count()).list_body.w);   // lista plikow bez zmian
    // (3) kolko nad panelem: o jedna pozycje, z ograniczeniem do zakresu; kolko nad lista nie rusza panelu
    CHECK(wheel(-1)==AktowkaView::QuickInput::HANDLED && v->quick_first()==1);sidebar_invariants(*v,*m);
    CHECK(wheel(-1000)==AktowkaView::QuickInput::HANDLED && v->quick_first()==v->quick_max_first());sidebar_invariants(*v,*m);
    CHECK(wheel(1000)==AktowkaView::QuickInput::HANDLED && v->quick_first()==0);
    {auto e=event(BWS_ZDARZENIE_MYSZ_KOLO,v->layout().list_body.x+10,v->layout().list_body.y+10);e.dy=-1;
     CHECK(v->quick_event(e,nullptr)==AktowkaView::QuickInput::NONE && v->quick_first()==0);}
    // (4) klik po przewinieciu aktywuje dokladnie pozycje pod kursorem
    CHECK(wheel(-2)==AktowkaView::QuickInput::HANDLED && v->quick_first()==2);
    {Rect r{};CHECK(v->quick_row_rect(2,&r));const int q=v->quick_at(r.x+5,r.y+5);CHECK(q==2);
     CHECK(m->navigate(m->quick(q)->path)!=Change::ERROR && !strcmp(m->path(),m->quick(2)->path));}
    // (5) odrysowanie tylko panelu (bez pelnego odrysowania okna)
    test_paint_rects.clear();test_capture_paints=true;CHECK(wheel(-1)==AktowkaView::QuickInput::HANDLED);test_capture_paints=false;
    {const auto left=v->layout().left;CHECK(!test_paint_rects.empty());
     for(auto& p:test_paint_rects)CHECK(p.x>=left.x && p.y>=left.y && p.x+p.w<=left.x+left.w && p.y+p.h<=left.y+left.h);}
    // (6) pasek: klik w tor (strona), przeciaganie suwaka do konca i z powrotem; klik w pasek nie nawiguje
    {
        CHECK(wheel(1000)==AktowkaView::QuickInput::HANDLED && v->quick_first()==0);
        auto& bar=v->test_quick_scrollbar();const auto r=bar.obszar_ekranu();
        const std::string before=m->path();
        CHECK(v->quick_event(event(BWS_ZDARZENIE_MYSZ_DOWN,r.x+r.szerokosc/2,r.dolny()-3),nullptr)==AktowkaView::QuickInput::HANDLED);
        CHECK(v->quick_event(event(BWS_ZDARZENIE_MYSZ_UP,r.x+r.szerokosc/2,r.dolny()-3),nullptr)==AktowkaView::QuickInput::HANDLED);
        CHECK(v->quick_first()>0 && before==m->path());sidebar_invariants(*v,*m);
        CHECK(wheel(1000)==AktowkaView::QuickInput::HANDLED);
        auto thumb=bar.prostokat_uchwytu();CHECK(thumb.wysokosc>0 && thumb.wysokosc<r.wysokosc);
        const int x=r.x+thumb.x+thumb.szerokosc/2,y=r.y+thumb.y+thumb.wysokosc/2;
        CHECK(v->quick_event(event(BWS_ZDARZENIE_MYSZ_DOWN,x,y),nullptr)==AktowkaView::QuickInput::HANDLED && bar.czy_przeciagany());
        CHECK(v->quick_event(event(BWS_ZDARZENIE_MYSZ_RUCH,x+300,r.dolny()+80),nullptr)==AktowkaView::QuickInput::HANDLED);  // poza paskiem: przechwycenie
        CHECK(v->quick_first()==v->quick_max_first() && bar.wartosc()==v->quick_first());
        CHECK(v->quick_event(event(BWS_ZDARZENIE_MYSZ_RUCH,x,r.y-80),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_first()==0);
        CHECK(v->quick_event(event(BWS_ZDARZENIE_MYSZ_UP,x,r.y-80),nullptr)==AktowkaView::QuickInput::HANDLED && !bar.czy_przeciagany());
        CHECK(v->quick_event(event(BWS_ZDARZENIE_MYSZ_RUCH,v->layout().center.x+20,v->layout().center.y+20),nullptr)==AktowkaView::QuickInput::NONE);
        CHECK(before==m->path());
    }
    // (7) klawiatura: Tab -> fokus na pozycji biezacego katalogu; strzalki/Home/End z przewijaniem do fokusu;
    //     Enter otwiera; Esc i Tab wracaja do listy; poza fokusem panelu strzalki naleza do listy
    {
        const int tab=43,esc=41,enter=40,up=82,down=81,home=74,end=77;
        CHECK(v->quick_event(physical(down),nullptr)==AktowkaView::QuickInput::NONE);
        CHECK(m->navigate("/dane/zdjecia")!=Change::ERROR);
        CHECK(v->quick_event(physical(tab),nullptr)==AktowkaView::QuickInput::HANDLED);
        CHECK(v->quick_focus()>=0 && !strcmp(m->quick(v->quick_focus())->path,"/dane/zdjecia"));
        CHECK(v->quick_event(physical(end),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==10);
        CHECK(v->quick_row_rect(10,nullptr));sidebar_invariants(*v,*m);           // automatycznie przewinieto
        CHECK(v->quick_event(physical(down),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==10);   // granica
        CHECK(v->quick_event(physical(home),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==0 && v->quick_first()==0);
        for(int i=1;i<11;++i){CHECK(v->quick_event(physical(down),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==i);
                              CHECK(v->quick_row_rect(i,nullptr));}
        CHECK(v->quick_event(physical(up),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==9 && v->quick_row_rect(9,nullptr));
        int act=-1;CHECK(v->quick_event(physical(enter),&act)==AktowkaView::QuickInput::ACTIVATE && act==9);
        CHECK(m->navigate(m->quick(act)->path)!=Change::ERROR && !strcmp(m->path(),m->quick(9)->path));
        CHECK(v->quick_event(physical(esc),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==-1);
        CHECK(v->quick_event(physical(tab),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==9);
        CHECK(v->quick_event(physical(tab),nullptr)==AktowkaView::QuickInput::HANDLED && v->quick_focus()==-1);
        CHECK(v->quick_event(physical(up),nullptr)==AktowkaView::QuickInput::NONE);
        // w trybie edycji sciezki Tab nie przejmuje fokusu
        v->begin_edit(EditMode::PATH);CHECK(v->quick_event(physical(tab),nullptr)==AktowkaView::QuickInput::NONE);v->end_edit();
    }
    // (8) zmiana rozmiaru w stanie przewinietym: zakres przyciety, po powiekszeniu wszystko widac i pasek znika
    {
        w.h=300;v->draw_workspace();CHECK(wheel(-1000)==AktowkaView::QuickInput::HANDLED);const int f=v->quick_first();CHECK(f>0);
        w.h=MIN_H;v->draw_workspace();sidebar_invariants(*v,*m);CHECK(v->quick_first()<=f);
        w.h=900;v->draw_workspace();CHECK(v->quick_first()==0 && v->quick_bar_rect().w==0);sidebar_invariants(*v,*m);
        w.h=120;v->draw_workspace();sidebar_invariants(*v,*m);                    // bardzo male okno: bez bledow
        w.h=1;v->draw_workspace();CHECK(v->quick_visible_count()==0);CHECK(v->quick_at(v->layout().left.x+5,v->layout().left.y)==-1);
        (void)wheel(-5);CHECK(v->quick_first()==0);                                   // panel o zerowej wysokosci: brak przewijania
        w.h=MIN_H;v->draw_workspace();sidebar_invariants(*v,*m);
    }
    // (9) historia nawigacji zachowana mimo przewijania i fokusu panelu
    {
        CHECK(m->navigate("/system")!=Change::ERROR);CHECK(m->navigate("/dane/muzyka")!=Change::ERROR);
        CHECK(wheel(-3)==AktowkaView::QuickInput::HANDLED);
        CHECK(m->back()!=Change::ERROR && !strcmp(m->path(),"/system"));
        CHECK(m->forward()!=Change::ERROR && !strcmp(m->path(),"/dane/muzyka"));
    }
    delete v;delete m;files.clear();
    std::printf("explorer sidebar: przewijanie (kolko, pasek, klawiatura, rozmiar) OK\n");
}
