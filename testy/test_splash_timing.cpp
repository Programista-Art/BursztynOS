// Mock-clock timing i prawdziwy renderer; bez emulatora i bez rzeczywistego czekania.
#define main suite_powitania_main
#include "test_powitanie.cpp"
#undef main
// Czy w obszarze jest atrament (cokolwiek poza tlem).
static bool atrament_w(const std::vector<uint32_t>& p,int w,int h,Powitanie::Obszar r) {
    for(int y=r.y<0?0:r.y;y<h&&y<r.y+r.h;++y)
        for(int x=r.x<0?0:r.x;x<w&&x<r.x+r.w;++x)
            if(p[size_t(y)*w+x]!=Powitanie::TLO)return true;
    return false;
}
int main(){
    using namespace Powitanie;
    // Probki kluczowych etapow idealnego przebiegu (prezentacja co KROK_US).
    struct Sample {uint64_t us;const char* opis;};
    const Sample cases[]={
        {0,"start"},
        {300000,"samo logo"},
        {nominalny_start(ETAP_TYTUL)+KROK_US*4,"wejscie tytulu"},
        {nominalnie_pelny(ETAP_TYTUL)+KROK_US,"tytul pelny"},
        {nominalny_start(ETAP_SEPARATOR)+KROK_US*4,"rozwijanie separatora"},
        {nominalnie_pelny(ETAP_SEPARATOR),"separator pelny"},
        {nominalnie_pelny(ETAP_LINIA1),"pierwszy podtytul"},
        {SEKWENCJA_US+KROK_US,"drugi podtytul"},
        {SEKWENCJA_US*3,"hold koncowy"}
    };
    for(unsigned dpi:{100U,125U}) {
        std::vector<uint32_t> piksele(1280*720);
        for(const auto& c:cases) {
            const Sekwencja s=sekwencja_idealna(c.us);
            rysuj(piksele.data(),1280,720,1280,dpi,s,true);
            // Kazda klatka ma atrament logotypu - nigdy pusty ekran.
            const auto g=geometria(1280,720,dpi);
            assert(atrament_w(piksele,1280,720,g.logo));
            assert(s.alfa_logo==LOGO_ALFA);
            if(c.us>=nominalnie_pelny(ETAP_TYTUL))assert(s.alfa_tytul==255);
            if(c.us>=nominalnie_pelny(ETAP_SEPARATOR))assert(s.rozwiniecie==1000);
            // Element widoczny dokladnie wtedy, gdy jego krycie > 0.
            assert(atrament_w(piksele,1280,720,g.tytul.r)==(s.alfa_tytul>0));
            assert(atrament_w(piksele,1280,720,g.separator)==(s.alfa_separator>0));
            assert(atrament_w(piksele,1280,720,g.linia1.r)==(s.alfa_linia1>0));
            assert(atrament_w(piksele,1280,720,g.linia2.r)==(s.alfa_linia2>0));
        }
        // Separator rozwija sie symetrycznie od srodka i nigdy nie wychodzi poza swoj prostokat.
        int poprzednia=0;
        for(unsigned step=0;step<=20;++step) {
            const uint64_t t=nominalny_start(ETAP_SEPARATOR)+WEJSCIE_SEPARATORA_US*step/20;
            const Sekwencja s=sekwencja_idealna(t);
            std::fill(piksele.begin(),piksele.end(),0u);
            rysuj(piksele.data(),1280,720,1280,dpi,s,true);
            const auto g=geometria(1280,720,dpi);
            int x0=1280,x1=-1;
            for(int y=g.separator.y-2;y<g.separator.y+g.separator.h+2;++y)
                for(int x=0;x<1280;++x)if(piksele[size_t(y)*1280+x]!=TLO){
                    assert(y>=g.separator.y&&y<g.separator.y+g.separator.h);
                    assert(x>=g.separator.x&&x<g.separator.x+g.separator.w);
                    if(x<x0)x0=x;if(x>x1)x1=x;}
            if(x1>=0) {
                assert(x0+x1+1>=1280-3&&x0+x1+1<=1280+3);
                assert(x1-x0+1>=poprzednia);poprzednia=x1-x0+1;
            }
        }
        // Pelny render jest idempotentny wzgledem tego samego stanu.
        const Sekwencja s=sekwencja_idealna(SEKWENCJA_US);
        std::vector<uint32_t> a,b;
        a.assign(1280*720,0xDEADBEEFu);b.assign(1280*720,0xC0FFEEu);
        rysuj(a.data(),1280,720,1280,dpi,s,true);
        rysuj(b.data(),1280,720,1280,dpi,s,true);
        assert(a==b);
        // Przyciecie ogranicza sie wylacznie do podanego prostokata.
        auto clipped=b;
        rysuj(clipped.data(),1280,720,1280,dpi,s,false,{100,100,50,50});
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x) {
            const bool wewnatrz=x>=100&&x<150&&y>=100&&y<150;
            if(!wewnatrz)assert(clipped[size_t(y)*1280+x]==b[size_t(y)*1280+x]);
        }
        assert(clipped[size_t(120)*1280+120]==TLO);
    }
    {
        Sekwencja s;s.rozpocznij(true,0);s.potwierdz(0);
        s.gotowy=true;s.aktualizuj(MINIMUM_US);assert(s.stan==Stan::Zanikanie);
        assert(s.fade==MINIMUM_US);
    }

    // Prawdziwy modul: source() = render + udana publikacja (jedna prezentacja).
    width=1280;height=720;
    std::vector<uint32_t> scene(size_t(width)*height,0x234567);
    auto source=[&](bool whole=true){
        auto* p=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
            reinterpret_cast<uint8_t*>(scene.data()),width,height,width*4,0,0,
            whole?width:1,whole?height:1,100));
        powitanie_publikacja();return p;};
    // Przestoj: najwyzej jedna akcja brudna na probke i najwyzej jeden nowy etap.
    for(uint64_t stall:{500000ULL,1500000ULL,3000000ULL}) {
        now=0;powitanie_inicjalizuj(true);source();
        for(uint64_t t=KROK_US;t<=1900000;t+=KROK_US){advance(t);source();}
        const unsigned etap_przed=sekwencja.etap;
        unsigned przed=full+partial;
        advance(1900000+stall);
        assert(full+partial-przed<=1);
        assert(sekwencja.etap<=etap_przed+1);
        assert(source()!=scene.data());
        przed=full+partial;powitanie_tick();
        assert(full+partial-przed<=1);
        powitanie_wycisz();assert(live==0);
    }
    // Zakonczenie nadal wymaga pelnej, spojnej transakcji pulpitu.
    now=0;powitanie_inicjalizuj(true);source();powitanie_pulpit_gotowy();
    for(uint64_t t:{100000ULL,700000ULL,1400000ULL}){advance(t);source();assert(powitanie_aktywne());}
    advance(7500000);assert(zakonczenie_oczekuje&&powitanie_aktywne());
    assert(source(false)!=scene.data());
    assert(source()==scene.data());
    assert(live==0&&!powitanie_aktywne());
    // Pulpit gotowy przed przestojem: zachowany znacznik, bez drugiego zanikania.
    now=0;powitanie_inicjalizuj(true);source();
    root_layer={};root_layer.szerokosc=width;root_layer.wysokosc=height;root_visible=true;
    now=1000000;powitanie_pulpit_gotowy(false,2);advance(8000000);
    assert(sekwencja.fade==MINIMUM_US&&zakonczenie_oczekuje);
    assert(source()==scene.data()&&live==0);
    // Pulpit spozniony dostaje pelne zanikanie od znacznika gotowosci.
    now=0;powitanie_inicjalizuj(true);source();advance(8000000);
    assert(sekwencja.stan==Stan::Widoczny);
    powitanie_pulpit_gotowy();advance(8100000);
    assert(sekwencja.fade==8000000&&sekwencja.odsloniecie==42);
    advance(8600000);assert(source()==scene.data()&&live==0);
    // Dluga synchroniczna inicjalizacja po pierwszej klatce (bez prezentacji):
    // etapy nie sa zalegle - po wznowieniu wchodza kolejno, najwyzej jeden na klatke.
    now=0;powitanie_inicjalizuj(true);source();
    {
        unsigned poprzednie_widoczne=sekwencja.widoczne,nowe_etapy=0;
        for(uint64_t t=10000000;t<=10000000+SEKWENCJA_US+KROK_US;t+=KROK_US){
            advance(t);source();
            const unsigned nowe=sekwencja.widoczne&~poprzednie_widoczne;
            assert(!(nowe&(nowe-1))); // najwyzej jeden nowy etap w prezentowanej klatce
            if(nowe)++nowe_etapy;
            if(t==10000000)assert(nowe==(1u<<ETAP_TYTUL)&&sekwencja.alfa_tytul<255);
            poprzednie_widoczne|=sekwencja.widoczne;
        }
        assert(nowe_etapy==4&&sekwencja.pelny[ETAP_LINIA2]);
        assert(sekwencja.pokazany_od[ETAP_TYTUL]==10000000);
        assert(sekwencja.pokazany_od[ETAP_LINIA2]-sekwencja.pokazany_od[ETAP_LINIA1]>=ODSTEP_ETAPOW_US);
    }
    powitanie_wycisz();assert(live==0);
    // Punkty kontrolne synchronicznego startu (bez taktow kompozytora): etapy
    // pojawiaja sie kolejno, kazda zmiana to jedna prezentacja, nie czesciej niz
    // co POWITANIE_ODSTEP_STARTU_US, a w holdzie punkt nie robi nic. Atrapa
    // PrzeniesNaEkran nie publikuje, wiec publikacje symuluje test.
    now=0;powitanie_inicjalizuj(true);
    assert(!powitanie_obsluz_start()); // przed pierwsza publikacja: nic
    source();
    {
        const unsigned p0=prezentacje_startu;
        unsigned zaprezentowane=0,poprzednie_widoczne=sekwencja.widoczne,etapy_widziane=0;
        for(uint64_t t=50000;t<=7000000;t+=50000){
            now=t;
            const bool p=powitanie_obsluz_start();
            if(p){++zaprezentowane;source();} // PrzeniesNaEkran: render + publikacja
            const unsigned nowe=sekwencja.widoczne&~poprzednie_widoczne;
            assert(!(nowe&(nowe-1)));
            if(nowe)++etapy_widziane;
            poprzednie_widoczne|=sekwencja.widoczne;
        }
        assert(etapy_widziane==4&&sekwencja.pelny[ETAP_LINIA2]);
        assert(zaprezentowane==prezentacje_startu-p0);
        assert(zaprezentowane>=8&&zaprezentowane<=7000000/POWITANIE_ODSTEP_STARTU_US);
        // Limit czestotliwosci i brak pracy w holdzie.
        now=7000000+POWITANIE_ODSTEP_STARTU_US;assert(!powitanie_obsluz_start());
        now=7000000+POWITANIE_ODSTEP_STARTU_US*3;assert(!powitanie_obsluz_start());
        assert(prezentacje_startu-p0==zaprezentowane);
        printf("Punkty kontrolne startu: %u prezentacji na 7 s sekwencji\n",zaprezentowane);
    }
    powitanie_wycisz();assert(!powitanie_obsluz_start()&&live==0);
    // Pulpit gotowy wczesnie: zanikanie od MINIMUM_US, splash znika przed koncem
    // sekwencji (pulpit nie czeka na wszystkie etapy).
    now=0;powitanie_inicjalizuj(true);source();now=300000;powitanie_pulpit_gotowy();
    for(uint64_t t=KROK_US;t<MINIMUM_US+FADE_US;t+=KROK_US){advance(t);assert(source()!=scene.data());}
    assert(sekwencja.etap<=ETAP_TYTUL); // separator i podtytuly pominiete
    advance(MINIMUM_US+FADE_US);
    assert(zakonczenie_oczekuje);assert(source()==scene.data()&&live==0);
    assert(MINIMUM_US+FADE_US<SEKWENCJA_US);
    // Jawne wylaczenie jest natychmiastowe.
    powitanie_inicjalizuj(false);advance(10000000);
    assert(source()==scene.data());
#if BURSZTYNDIAGSPLASHTIMING
    logged_strings.clear();now=0;powitanie_inicjalizuj(true);source();powitanie_pulpit_gotowy();
    for(unsigned ms=0;ms<=7000;ms+=40){advance(uint64_t(ms)*1000);source();powitanie_diag_prezentacja();}
    assert(!powitanie_aktywne()&&live==0);
    auto logged=[&](const char* text){for(auto& m:logged_strings)if(m.find(text)!=std::string::npos)return true;return false;};
    assert(logged("start now_us=0"));
    // Diagnostyka loguje etapy (start, fazy, gotowosc, zanikanie, usuniecie),
    // a nie klatki: liczba wpisow rosnie z liczba etapow, nie z FPS.
    assert(logged_strings.size()<=32);
    printf("Wpisy diagnostyczne: %zu przy 176 probkach\n",logged_strings.size());
    (void)logged;
#endif
    printf("Splash timing / presentation-aware stages / stalls / centering: %u/%u PASS\n",checks,checks);
    return 0;
}
