#pragma once
// Trwalosc ukladu pulpitu po dodaniu skrotu "Ustawienia ekranu" (id
// "ustawienia-ekranu", dopisany na koncu DesktopEntries::Launchers).
// Prawdziwy Menedzer Okien (odswiez_wpisy_pulpitu / gest / zapis) oraz
// prawdziwy DesktopSettings::State, z izolowanym plikiem w pamieci
// (layout_test_io z desktop_persistence_cases.h) - bez dysku uzytkownika.
#include <array>
#include <map>
#include <set>

static void desktop_display_shortcut_cases() {
    using namespace DesktopSettings;
    const int NOWY=DesktopEntries::Count-1;
    CHECK(!strcmp(DesktopEntries::Launchers[NOWY].id,"ustawienia-ekranu"));
    CHECK(!strcmp(DesktopEntries::Launchers[NOWY].path,AppIdentity::DisplaySettings));
    int ustawienia_id=-1;
    for(int i=0;i<DesktopEntries::Count;++i)if(!strcmp(DesktopEntries::Launchers[i].id,"ustawienia"))ustawienia_id=i;
    CHECK(ustawienia_id>=0 && ustawienia_id!=NOWY);
    // Identyfikatory sa unikalne i zaden nie jest przedrostkiem dopasowania.
    for(int i=0;i<DesktopEntries::Count;++i)for(int j=0;j<i;++j)CHECK(!equal(DesktopEntries::Launchers[i].id,DesktopEntries::Launchers[j].id));
    CHECK(!equal("ustawienia","ustawienia-ekranu"));CHECK(!equal("ustawienia-ekranu","ustawienia"));

    g_test_bws_hook=layout_wm_io;
    auto reinit=[&]() {   // symulowany restart: nowy stan WM, ten sam plik
        anuluj_gest_pulpitu();desktop_ctrl(false);
        delete desktop_settings;desktop_settings=nullptr;desktop_layout={};liczba_wpisow_pulpitu=0;hash_pulpitu_wazny=false;
        CHECK(odswiez_wpisy_pulpitu());
    };
    auto ekran=[&](int w,int h,unsigned dpi){screen_w=w;screen_h=h;ui_skala_procent=dpi;metryki_ekranu();};
    auto indeks=[&](char type,const char* id)->int {
        for(int i=0;i<desktop_layout.count;++i){auto k=klucz_ukladu_pulpitu(i);if(k.type==type && !strcmp(k.id,id))return i;}
        return -1;
    };
    // Kazda widoczna ikona w calosci na ekranie, bez dwoch ikon w tej samej
    // komorce i bez nakladania sie obszarow.
    auto spojny=[&]() {
        std::set<std::pair<int,int>> komorki;
        for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed) {
            CHECK(desktop_layout.visible(i));
            auto c=desktop_layout.cell(i);CHECK(komorki.insert({c.x,c.y}).second);
            for(int j=0;j<i;++j)if(desktop_layout.items[j].placed)
                CHECK(!Desktop::intersects(desktop_layout.placement_bounds(i),desktop_layout.placement_bounds(j)));
        }
    };
    struct Kom {int col,row,ox,oy;};
    // Rzeczywiste polozenie komorki (kotwica moze byc col/row albo przesunieciem).
    auto komorka=[&](int i)->Kom{auto c=desktop_layout.cell(i);return {c.x,c.y,c.szerokosc,c.wysokosc};};
    auto w_siatce=[&](int col,int row)->Kom{return {desktop_layout.x0+col*desktop_layout.cw,desktop_layout.y0+row*desktop_layout.ch,desktop_layout.cw,desktop_layout.ch};};
    auto rowne_kom=[](Kom a,Kom b){return a.col==b.col && a.row==b.row && a.ox==b.ox && a.oy==b.oy;};
    auto rekord=[](char type,const std::string& id,int col,int row,int dpi=100) {
        return std::string(1,type)+"\t"+id+"\t0\t0\t0\t"+std::to_string(col)+"\t"+std::to_string(row)+"\t0\t0\t"+std::to_string(dpi)+"\n";
    };
    auto dokument=[&](const std::string& rekordy) {
        return layout_document("BursztynDesktopLayout=1\nanchors=1\ngrid=1\nwidth=1920\nheight=1040\ndpi=100\n"+rekordy);
    };
    auto zapisz_plik=[&](const std::string& tresc){layout_disk=tresc;layout_exists=true;};

    files={{"/",true},{"/uzytkownicy",true},{SCIEZKA_PULPIT,true},
           {std::string(SCIEZKA_PULPIT)+"/raport.txt",false},{std::string(SCIEZKA_PULPIT)+"/zdjęcia",true},
           {std::string(SCIEZKA_PULPIT)+"/notatki-żółte.txt",false}};
    ekran(1920,1080,100);

    // 1. Swiezy pulpit bez zapisanego ukladu: nowy skrot w domyslnym slocie
    //    (kolejnosc katalogu, kolumnami), nic nie jest zapisywane.
    layout_disk.clear();layout_exists=false;layout_writes=0;
    reinit();
    const int n0=indeks('L',"ustawienia-ekranu"),s0=indeks('L',"ustawienia");
    CHECK(n0>=0 && s0>=0 && n0!=s0);
    CHECK(desktop_layout.items[n0].placed && desktop_layout.items[n0].launcher);
    CHECK(wpisy_pulpitu[n0].launcher==NOWY && wpisy_pulpitu[s0].launcher==ustawienia_id);
    CHECK(!strcmp(wpisy_pulpitu[n0].nazwa,"Ustawienia ekranu") && !strcmp(wpisy_pulpitu[s0].nazwa,"Ustawienia"));
    {
        const int rows=desktop_layout.rows;CHECK(rows>0);
        CHECK(rowne_kom(komorka(n0),w_siatce(NOWY/rows,NOWY%rows)));
    }
    spojny();CHECK(layout_writes==0 && !layout_exists);
    const Kom domyslna_nowego=komorka(n0);

    // 2. Uklad zapisany PRZED dodaniem skrotu: wszystkie stare skroty z
    //    wlasnymi pozycjami (rozrzucone, z przerwami, przy prawej/dolnej
    //    krawedzi), plik zajmujacy domyslny slot nowego skrotu.
    const int cols=desktop_layout.cols,rows=desktop_layout.rows;
    CHECK(cols>=8 && rows>=6);
    const int dom_c=NOWY/rows,dom_r=NOWY%rows;   // domyslny slot nowego skrotu (kolumnami)
    std::map<std::string,std::pair<int,int>> stare;
    std::string rekordy;
    auto wolna=[&](int col,int row) {
        for(int proby=0;proby<cols*rows;++proby) {
            bool zajete=col==dom_c && row==dom_r;
            for(auto& kv:stare)zajete|=kv.second==std::make_pair(col,row);
            if(!zajete)return std::make_pair(col,row);
            if(++row==rows){row=0;col=(col+1)%cols;}
        }
        CHECK(false);return std::make_pair(0,0);
    };
    for(int i=0;i<NOWY;++i) {
        int col=(i*3)%cols,row=(i*2)%rows;            // przerwy miedzy zajetymi komorkami
        if(i==0){col=cols-1;row=rows-1;}               // prawy dolny rog
        if(i==1){col=cols-1;row=0;}                    // prawa krawedz
        if(i==2){col=0;row=rows-1;}                    // dolna krawedz
        auto k=wolna(col,row);
        stare["L"+std::string(DesktopEntries::Launchers[i].id)]=k;
        rekordy+=rekord('L',DesktopEntries::Launchers[i].id,k.first,k.second);
    }
    // Plik dokladnie w domyslnym slocie nowego skrotu.
    stare["Fraport.txt"]={dom_c,dom_r};
    rekordy+=rekord('F',"raport.txt",dom_c,dom_r);
    {auto k=wolna(cols-2,rows-1);stare["Fzdjęcia"]=k;rekordy+=rekord('F',"zdjęcia",k.first,k.second);}
    const std::string stary_plik=dokument(rekordy);
    CHECK(stary_plik.find("ustawienia-ekranu")==std::string::npos);
    zapisz_plik(stary_plik);layout_writes=0;
    reinit();
    CHECK(desktop_settings->count==NOWY+2);CHECK(desktop_settings->matchedLaunchers==NOWY);
    CHECK(desktop_settings->matchedFiles==2);CHECK(desktop_settings->stale==0);
    for(auto& kv:stare) {
        const int i=indeks(kv.first[0],kv.first.c_str()+1);CHECK(i>=0);
        CHECK(desktop_layout.items[i].placed);
        CHECK(rowne_kom(komorka(i),w_siatce(kv.second.first,kv.second.second)));
    }
    int n=indeks('L',"ustawienia-ekranu");
    CHECK(n>=0 && desktop_layout.items[n].placed);
    for(auto& kv:stare)CHECK(!rowne_kom(komorka(n),w_siatce(kv.second.first,kv.second.second)));
    spojny();
    // Nowy plik bez rekordu tez dostaje wolna komorke, a stare pozostaja.
    CHECK(desktop_layout.items[indeks('F',"notatki-żółte.txt")].placed);
    // Format bez zmian: stary plik nie jest przepisywany przy samym starcie.
    CHECK(layout_writes==0 && layout_disk==stary_plik);
    // Kolejny restart bez zapisu: nowy skrot deterministycznie w tej samej komorce.
    const Kom auto_nowy=komorka(n);
    reinit();n=indeks('L',"ustawienia-ekranu");CHECK(rowne_kom(komorka(n),auto_nowy));

    // 3. Przesuniecie nowego skrotu gestem i zapis; restart przywraca jego
    //    pozycje, pozostale pozycje bez zmian.
    {
        std::map<std::string,Kom> przed;
        for(int i=0;i<desktop_layout.count;++i){auto k=klucz_ukladu_pulpitu(i);przed[std::string(1,k.type)+k.id]=komorka(i);}
        int cel_c=-1,cel_r=-1;
        for(int c=cols-1;c>=0 && cel_c<0;--c)for(int r=rows-1;r>=0;--r)
            if(desktop_layout.fits(n,c,r) && (c!=desktop_layout.items[n].col || r!=desktop_layout.items[n].row)){cel_c=c;cel_r=r;break;}
        CHECK(cel_c>=0);
        desktop_layout.click(n,false);
        auto a=wizualny_wpis_pulpitu(n);
        const int dx=(cel_c-desktop_layout.items[n].col)*desktop_layout.cw,dy=(cel_r-desktop_layout.items[n].row)*desktop_layout.ch;
        const int zapisy=layout_writes;
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_DOWN,a.x+20,a.y+10)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,a.x+20+dx/2,a.y+10+dy/2)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_RUCH,a.x+20+dx,a.y+10+dy)));
        CHECK(obsluz_gest_pulpitu(desktop_event(BWS_ZDARZENIE_MYSZ_UP,a.x+20+dx,a.y+10+dy)));
        CHECK(layout_writes==zapisy+1);CHECK(!test_mouse_capture);
        const Kom po=komorka(n);CHECK(!rowne_kom(po,auto_nowy));
        CHECK(layout_disk.find("L\tustawienia-ekranu\t")!=std::string::npos);
        CHECK(layout_disk.find("L\tustawienia\t")!=std::string::npos);
        auto reload=std::make_unique<State>();
        memcpy(reload->buffer,layout_disk.data(),layout_disk.size());CHECK(reload->parse(layout_disk.size()));
        int nowe_rekordy=0;for(int i=0;i<reload->count;++i)nowe_rekordy+=equal(reload->records[i].id,"ustawienia-ekranu");
        CHECK(nowe_rekordy==1);
        reinit();n=indeks('L',"ustawienia-ekranu");
        CHECK(rowne_kom(komorka(n),po));
        for(int i=0;i<desktop_layout.count;++i)if(i!=n) {
            auto k=klucz_ukladu_pulpitu(i);CHECK(rowne_kom(komorka(i),przed[std::string(1,k.type)+k.id]));
        }
        spojny();CHECK(desktop_settings->matchedLaunchers==DesktopEntries::Count);
        // 4./5. Zmiana rozdzielczosci i skali: projekcja naprawiana w locie,
        //       nic nie jest nadpisywane; powrot przywraca zapisane komorki.
        const std::string zapisany=layout_disk;
        for(auto tryb:std::initializer_list<std::array<int,3>>{{1280,720,100},{1024,768,100},{1366,768,125},{1280,720,150},{1920,1080,200},{800,600,100}}) {
            ekran(tryb[0],tryb[1],unsigned(tryb[2]));reinit();
            spojny();CHECK(layout_disk==zapisany);
        }
        ekran(1920,1080,100);reinit();n=indeks('L',"ustawienia-ekranu");
        CHECK(rowne_kom(komorka(n),po));
        for(int i=0;i<desktop_layout.count;++i)if(i!=n){auto k=klucz_ukladu_pulpitu(i);CHECK(rowne_kom(komorka(i),przed[std::string(1,k.type)+k.id]));}
    }

    // 6. Bledne / spoza ekranu / zduplikowane / nieznane rekordy.
    {
        std::string r;
        r+=rekord('L',"ustawienia-ekranu",5000,5000);           // poza ekranem (w zakresie formatu)
        r+=rekord('L',"ustawienia",0,0);
        r+=rekord('L',"notatnik",0,0);                           // duplikat komorki ze starszym skrotem
        r+=rekord('L',"kalkulator",1,0);r+=rekord('L',"kalkulator",7,5);   // duplikat id: pierwszy wygrywa
        r+=rekord('L',"nieistniejacy-program",2,0);              // nieznany / przestarzaly identyfikator
        r+=rekord('L',"ustawienia-ekranu-stary",3,0);
        r+=rekord('F',"usuniety.txt",4,0);
        r+="L\tsnake\t0\t0\t0\t1\t1\t0\t0\t50\n";                // nieprawidlowe dpi kotwicy: rekord pominiety
        r+="L\thusarz\t0\t0\t0\t9999\t0\t0\t0\t100\n";            // kolumna poza zakresem formatu: pominiety
        r+="L\t../ustawienia\t0\t0\t0\t2\t2\t0\t0\t100\n";        // niedozwolony identyfikator
        zapisz_plik(dokument(r));layout_writes=0;reinit();
        CHECK(desktop_settings->count==7);CHECK(desktop_settings->matchedLaunchers==4);CHECK(desktop_settings->stale==3);
        const int s=indeks('L',"ustawienia"),nn=indeks('L',"ustawienia-ekranu"),no=indeks('L',"notatnik"),k=indeks('L',"kalkulator");
        // Wczesniejszy skrot katalogu (notatnik, indeks 0) zachowuje komorke 0,0.
        CHECK(rowne_kom(komorka(no),w_siatce(0,0)));
        CHECK(rowne_kom(komorka(k),w_siatce(1,0)));
        CHECK(desktop_layout.items[s].placed && !rowne_kom(komorka(s),w_siatce(0,0)));
        CHECK(desktop_layout.items[nn].placed && desktop_layout.visible(nn));
        CHECK(!rowne_kom(komorka(nn),komorka(s)));
        spojny();
        for(int i=0;i<desktop_layout.count;++i)CHECK(desktop_layout.items[i].placed);
    }
    // 7. Brak pliku, uszkodzona suma, obciety plik, zly naglowek, smieci:
    //    bezpieczny uklad domyslny (jak swiezy pulpit), bez zapisu przy starcie.
    {
        const std::string dobry=dokument(rekord('L',"ustawienia-ekranu",4,4));
        std::vector<std::string> zle={"",std::string(1,'\0'),"BursztynDesktopLayout=1\n",dobry.substr(0,dobry.size()/2),
                                      dobry.substr(0,dobry.size()-1),std::string(BufferCapacity-1,'z')};
        std::string suma=dobry;suma[suma.size()-20]^=1;zle.push_back(suma);
        std::string naglowek=dobry;CHECK(naglowek[22]=='1');naglowek[22]='2';zle.push_back(naglowek);
        for(size_t t=0;t<=zle.size();++t) {
            if(t==zle.size()){layout_disk.clear();layout_exists=false;}
            else zapisz_plik(zle[t]);
            layout_writes=0;reinit();
            CHECK(desktop_settings->count==0);
            const int i=indeks('L',"ustawienia-ekranu");
            CHECK(rowne_kom(komorka(i),domyslna_nowego));spojny();CHECK(layout_writes==0);
        }
        // Poprawny dokument z tym samym rekordem jest przyjmowany.
        zapisz_plik(dobry);reinit();const int i=indeks('L',"ustawienia-ekranu");
        CHECK(rowne_kom(komorka(i),w_siatce(4,4)));
    }
    // 8. Zapis tylko do /ustawienia/uklad-pulpitu.cfg przez BWS (PZB jak dotad).
    {
        CHECK(!strcmp(Path,"/ustawienia/uklad-pulpitu.cfg"));
        layout_disk.clear();layout_exists=false;layout_writes=0;reinit();
        desktop_settings->dirty=true;zapisz_uklad_pulpitu();
        CHECK(layout_exists && layout_writes==1);
        reinit();spojny();CHECK(desktop_settings->matchedLaunchers==DesktopEntries::Count);
        layout_write_fail=true;desktop_settings->dirty=true;const auto przed=layout_disk;
        zapisz_uklad_pulpitu();CHECK(layout_disk==przed);layout_write_fail=false;   // odmowa zapisu nie psuje pliku
    }
    g_test_bws_hook=explorer_bws;

    // 9. Pelna siatka: wszystkie komorki zajete przez zapisane pozycje -
    //    nowy skrot pozostaje nieumieszczony (nie nadpisuje innej ikony, bez
    //    wyjscia poza tablice); po zwolnieniu komorki trafia w wolne miejsce.
    {
        auto state=std::make_unique<State>();
        auto layout=std::make_unique<Desktop::Layout<Capacity>>();
        std::vector<std::string> nazwy(Capacity);
        auto m=pulpit_metryki(100);
        layout->geometry(1024,600,ui_skaluj(3,100),100);
        const int c=layout->cols,r=layout->rows,komorek=c*r;
        CHECK(komorek>NOWY && komorek<DesktopEntries::FileCapacity);
        const int liczba=komorek+DesktopEntries::Count>Capacity?Capacity:komorek+DesktopEntries::Count;
        auto key=[&](int i)->Key{return {i>=liczba-DesktopEntries::Count?'L':'F',nazwy[i].c_str()};};
        auto przygotuj=[&]() {
            *layout={};layout->geometry(1024,600,ui_skaluj(3,100),100);layout->count=liczba;
            for(int i=0;i<liczba;++i) {
                const int l=i-(liczba-DesktopEntries::Count);
                nazwy[i]=l>=0?DesktopEntries::Launchers[l].id:"plik-"+std::to_string(i);
                layout->items[i].launcher=l>=0;
                layout->items[i].label=Desktop::wrap(nazwy[i].c_str(),m.etykieta_szer,m.wysokosc_linii,m.odstep_linii,16,100,oblicz_szerokosc_tekstu);
                layout->items[i].col=l>=0?l/r:0;layout->items[i].row=l>=0?l%r:0;
            }
        };
        // Stare skroty + pliki wypelniaja wszystkie komorki.
        std::string rr;int zajete=0;
        for(int l=0;l<NOWY && zajete<komorek;++l,++zajete)rr+=rekord('L',DesktopEntries::Launchers[l].id,zajete/r,zajete%r);
        for(int f=0;zajete<komorek;++f,++zajete)rr+=rekord('F',"plik-"+std::to_string(f),zajete/r,zajete%r);
        const std::string doc=dokument(rr);
        CHECK(doc.size()<BufferCapacity);memcpy(state->buffer,doc.data(),doc.size());CHECK(state->parse(doc.size()));
        przygotuj();(void)state->apply(*layout,key);
        const int nowy=liczba-1;
        CHECK(!layout->items[nowy].placed);
        int umieszczone=0;
        for(int i=0;i<liczba;++i)if(layout->items[i].placed){++umieszczone;CHECK(layout->visible(i));
            for(int j=0;j<i;++j)if(layout->items[j].placed)CHECK(!Desktop::intersects(layout->placement_bounds(i),layout->placement_bounds(j)));}
        CHECK(umieszczone==komorek);
        // Zapis pomija nieumieszczony skrot (nie powstaje falszywa pozycja).
        const size_t rozmiar=state->serialize(*layout,key);CHECK(rozmiar>0);
        CHECK(std::string(state->buffer,rozmiar).find("ustawienia-ekranu")==std::string::npos);
        // Zwolnienie ostatniej komorki: nowy skrot ja zajmuje, reszta bez zmian.
        std::string mniej=rr.substr(0,rr.rfind("F\t"));
        const std::string doc2=dokument(mniej);
        memcpy(state->buffer,doc2.data(),doc2.size());CHECK(state->parse(doc2.size()));
        przygotuj();(void)state->apply(*layout,key);
        CHECK(layout->items[nowy].placed && layout->visible(nowy));
        const auto cz=layout->cell(nowy);
        CHECK(cz.x==layout->x0+((komorek-1)/r)*layout->cw && cz.y==layout->y0+((komorek-1)%r)*layout->ch);
    }
}
