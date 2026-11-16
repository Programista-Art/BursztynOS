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
    auto krok=[](unsigned n){return n*KROK_US;};
    // Probki kluczowych etapow animacji.
    struct Sample {uint64_t us;const char* opis;};
    // Etapy tylko jako opis probki; faza jest wyliczana z kwantowanego czasu,
    // wiec test nie powtarza tej arytmetyki, a sprawdza spojnosc klatki.
    const Sample cases[]={
        {0,"start"},
        {300000,"logo fade-in"},
        {TYTUL_IN_US+KROK_US*4,"wejscie tytulu"},
        {TYTUL_PELNY_US+KROK_US,"tytul pelny"},
        {SEPARATOR_IN_US+KROK_US*4,"rozwijanie separatora"},
        {SEPARATOR_PELNY_US,"separator pelny"},
        {LINIA1_PELNA_US,"pierwszy podtytul"},
        {LINIA2_PELNA_US+KROK_US,"drugi podtytul"},
        {SEKWENCJA_US*3,"hold koncowy"}
    };
    for(unsigned dpi:{100U,125U}) {
        std::vector<uint32_t> piksele(1280*720);
        for(const auto& c:cases) {
            Sekwencja s;s.rozpocznij(true,0);s.przelicz(c.us);
            rysuj(piksele.data(),1280,720,1280,dpi,s,true);
            // Kazda klatka ma atrament logotypu - nigdy pusty ekran.
            const auto g=geometria(1280,720,dpi);
            assert(atrament_w(piksele,1280,720,g.logo));
            assert(s.alfa_logo==LOGO_ALFA);
            if(c.us>=TYTUL_PELNY_US)assert(s.alfa_tytul==255);
            if(c.us>=SEPARATOR_PELNY_US)assert(s.rozwiniecie==1000);
            // Element widoczny dokladnie wtedy, gdy jego krycie > 0.
            assert(atrament_w(piksele,1280,720,g.tytul.r)==(s.alfa_tytul>0));
            assert(atrament_w(piksele,1280,720,g.separator)==(s.alfa_separator>0));
            assert(atrament_w(piksele,1280,720,g.linia1.r)==(s.alfa_linia1>0));
            assert(atrament_w(piksele,1280,720,g.linia2.r)==(s.alfa_linia2>0));
        }
        // Separator rozwija sie symetrycznie od srodka i nigdy nie wychodzi poza swoj prostokat.
        int poprzednia=0;
        for(unsigned step=0;step<=20;++step) {
            const uint64_t t=SEPARATOR_IN_US+(SEPARATOR_PELNY_US-SEPARATOR_IN_US)*step/20;
            Sekwencja s;s.rozpocznij(true,0);s.przelicz(t);
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
        Sekwencja s;s.rozpocznij(true,0);s.przelicz(SEKWENCJA_US);
        std::vector<uint32_t> a,b;
        a.assign(1280*720,0xDEADBEEFu);b.assign(1280*720,0xC0FFEEu);
        rysuj(a.data(),1280,720,1280,dpi,s,true);
        rysuj(b.data(),1280,720,1280,dpi,s,true);
        assert(a==b);
        // Przyciecie ogranicza sie wylacznie do podanego prostokata: piksele
        // poza nim pozostaja nietkniete.
        auto clipped=b;
        rysuj(clipped.data(),1280,720,1280,dpi,s,false,{100,100,50,50});
        for(int y=0;y<720;++y)for(int x=0;x<1280;++x) {
            const bool wewnatrz=x>=100&&x<150&&y>=100&&y<150;
            if(!wewnatrz)assert(clipped[size_t(y)*1280+x]==b[size_t(y)*1280+x]);
        }
        // Ten fragment ukladu nie przecina przyciecia 100..150, wiec w srodku
        // musi byc czyste tlo.
        assert(clipped[size_t(120)*1280+120]==TLO);
    }
    // Absolutny czas: probka po dlugim przestoju pokazuje stan docelowy,
    // a nie odtwarza pominiete klatki po klatce.
    Sekwencja s;s.rozpocznij(true,0);
    for(uint64_t t:{100000ULL,2500000ULL,5200000ULL,7500000ULL}) {
        s.przelicz(t);
        Sekwencja direct;direct.rozpocznij(true,0);direct.przelicz(t);
        assert(s.sygnatura()==direct.sygnatura());
    }
    s.gotowy=true;s.aktualizuj(MINIMUM_US);assert(s.stan==Stan::Zanikanie);
    assert(s.fade==MINIMUM_US);

    // Prawdziwy modul: najwyzej jedna akcja brudna na probke, takze po przestoju.
    width=1280;height=720;
    std::vector<uint32_t> scene(size_t(width)*height,0x234567);
    auto source=[&](bool whole=true){
        auto* p=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
            reinterpret_cast<uint8_t*>(scene.data()),width,height,width*4,0,0,
            whole?width:1,whole?height:1,100));
        powitanie_publikacja();return p;};
    for(uint64_t stall:{500000ULL,1500000ULL,3000000ULL}) {
        now=0;powitanie_inicjalizuj(true);source();powitanie_tick();
        advance(1900000);
        unsigned przed=full+partial;
        advance(1900000+stall);
        assert(full+partial-przed<=1);
        Sekwencja expected;expected.rozpocznij(true,0);expected.rozpocznij_etapy(0);expected.aktualizuj(now);
        assert(sekwencja.sygnatura()==expected.sygnatura());
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
    // Dluga synchroniczna inicjalizacja po pierwszej klatce (bez taktow
    // kompozytora): etapy nie sa zalegle - pojawiaja sie kolejno od pierwszego
    // taktu, a nie wszystkie naraz (przyczyna problemu z ekranu referencyjnego).
    now=0;powitanie_inicjalizuj(true);source();
    now=10000000;assert(source()!=scene.data());
    assert(sekwencja.alfa_logo==LOGO_ALFA&&sekwencja.alfa_tytul==0&&sekwencja.alfa_linia2==0);
    advance(10000000);assert(sekwencja.etapy&&sekwencja.etapy_od==10000000);
    {
        unsigned widoczne_przy_pierwszym=0;source();
        widoczne_przy_pierwszym=(sekwencja.alfa_tytul>0)+(sekwencja.alfa_separator>0)+
                                (sekwencja.alfa_linia1>0)+(sekwencja.alfa_linia2>0);
        assert(widoczne_przy_pierwszym==0);
        unsigned poprzednie=0;
        for(uint64_t t=10000000;t<=10000000+SEKWENCJA_US;t+=KROK_US){
            advance(t);source();
            const unsigned teraz=(sekwencja.alfa_tytul==255)+(sekwencja.alfa_separator==255)+
                                 (sekwencja.alfa_linia1==255)+(sekwencja.alfa_linia2==255);
            assert(teraz>=poprzednie&&teraz<=poprzednie+1); // najwyzej jeden nowy element na probke
            poprzednie=teraz;
        }
        assert(poprzednie==4);
    }
    powitanie_wycisz();assert(live==0);
    // Punkty kontrolne synchronicznego startu (bez taktow kompozytora): etapy
    // pojawiaja sie kolejno, kazda zmiana to jedna prezentacja, nie czesciej niz
    // co POWITANIE_ODSTEP_STARTU_US, a w holdzie punkt nie robi nic.
    now=0;powitanie_inicjalizuj(true);
    assert(!powitanie_obsluz_start()); // przed pierwsza publikacja: nic
    source();
    {
        const unsigned p0=prezentacje_startu;
        unsigned zaprezentowane=0,poprzednie=0,etapy_widziane=0;
        for(uint64_t t=50000;t<=6000000;t+=50000){
            now=t;
            const bool p=powitanie_obsluz_start();
            if(p)++zaprezentowane;
            if(t==50000)assert(sekwencja.etapy&&sekwencja.etapy_od==50000);
            const unsigned pelne=(sekwencja.alfa_tytul==255)+(sekwencja.alfa_separator==255)+
                                 (sekwencja.alfa_linia1==255)+(sekwencja.alfa_linia2==255);
            assert(pelne>=poprzednie&&pelne<=poprzednie+1);
            if(pelne>poprzednie)++etapy_widziane;
            poprzednie=pelne;
        }
        assert(etapy_widziane==4&&poprzednie==4);
        assert(zaprezentowane==prezentacje_startu-p0);
        assert(zaprezentowane>=8&&zaprezentowane<=6000000/POWITANIE_ODSTEP_STARTU_US);
        // Limit czestotliwosci i brak pracy w holdzie.
        now=6000000+POWITANIE_ODSTEP_STARTU_US;assert(!powitanie_obsluz_start());
        now=6000000+POWITANIE_ODSTEP_STARTU_US*3;assert(!powitanie_obsluz_start());
        assert(prezentacje_startu-p0==zaprezentowane);
        printf("Punkty kontrolne startu: %u prezentacji na 6 s sekwencji\n",zaprezentowane);
    }
    powitanie_wycisz();assert(!powitanie_obsluz_start()&&live==0);
    // Pulpit gotowy wczesnie: zanikanie od MINIMUM_US, splash znika przed koncem
    // sekwencji (pulpit nie czeka na wszystkie etapy).
    now=0;powitanie_inicjalizuj(true);source();now=300000;powitanie_pulpit_gotowy();
    for(uint64_t t=KROK_US;t<MINIMUM_US+FADE_US;t+=KROK_US){advance(t);assert(source()!=scene.data());}
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
    printf("Splash timing / absolute sampling / stalls / centering: %u/%u PASS\n",checks,checks);
    return 0;
}
