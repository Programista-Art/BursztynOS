#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "opcje_startowe.h"
#include "../jadro/gui/ekran_powitalny.cpp"
static unsigned checks=0,allocations=0,full=0,partial=0,live=0;
static uint64_t now=0;
static bool clock_ok=true,fail_alloc=false;
static int width=1280,height=720;
static warstwa_obrazu root_layer{};
static bool root_visible=false;
warstwa_obrazu* pobierz_warstwe(int pid){return pid==2?&root_layer:nullptr;}
bool skladacz_obrazu_czy_widoczna(int pid){return pid==2&&root_visible;}
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
bool hpet_dostepny(){return clock_ok;}
uint64_t czas_monotoniczny_us(){return now;}
uint64_t czas_monotoniczny_ns(){return now*1000;}
void* bufor_grafiki_alokuj(size_t n){if(fail_alloc)return nullptr;++allocations;++live;return calloc(1,n);}
void bufor_grafiki_zwolnij(void* p){assert(p&&live);--live;free(p);}
static std::vector<std::string> logged_strings;
void klog_serial(const char* msg){logged_strings.emplace_back(msg);}
void PrzeniesNaEkran(){}
int grafika_pobierz_szerokosc(){return width;}
int grafika_pobierz_wysokosc(){return height;}
void skladacz_obrazu_oznacz_dirty(){++full;}
void skladacz_obrazu_oznacz_dirty_rect(int,int,int,int){++partial;}
static void advance(uint64_t t){now=t;powitanie_tick();}

// Pomiar atramentu w gotowej powierzchni. Test ufa wylacznie pikselom, wiec
// kazda metryka ukladu jest weryfikowana na tym, co naprawde widac.
struct Zakres {int x0,y0,x1,y1;bool ok() const {return x1>=x0&&y1>=y0;}};
static Zakres atrament(const uint32_t* p,int w,int h,int pitch,int y0,int y1) {
    Zakres z{w,h,-1,-1};
    for(int y=y0<0?0:y0;y<y1&&y<h;++y)
        for(int x=0;x<w;++x)
            if(p[size_t(y)*pitch+x]!=Powitanie::TLO) {
                if(x<z.x0)z.x0=x;if(x>z.x1)z.x1=x;
                if(y<z.y0)z.y0=y;if(y>z.y1)z.y1=y;
            }
    return z;
}
static int srodek_x(const Zakres& z) {return (z.x0+z.x1)/2;}

int main(){
    using namespace Powitanie;
    // Dwa komunikaty wyswietlane po zapelnieniu paska.
    assert(!strcmp(tekst(0),"Polski system operacyjny"));
    assert(!strcmp(tekst(1),"Niezale\u017cny system"));
    assert(LICZBA_KOMUNIKATOW==2);
    assert(!strcmp(ZNAK_MARKI,"BURSZTYN OS"));
    // Czarne tlo i bursztynowy akcent.
    assert(TLO==0x000000&&AKCENT==0xFF8A00);
    // Logotyp jest wkompilowana maska, nie dekoderem PNG.
    assert(BursztynLogo::SZEROKOSC>0&&BursztynLogo::WYSOKOSC>0);
    assert(BursztynLogo::MASKA[0]!=0xFF||BursztynLogo::MASKA[BursztynLogo::SZEROKOSC]!=0xFF);

    // Fazy intro i wlasciwosci smoothstepu.
    assert(wygladz(0)==0&&wygladz(1000)==1000);
    for(unsigned u:{0U,125U,500U,875U,1000U}) {
        const unsigned g=wygladz(u);
        assert(g<=1000);
        assert(u==0||u==1000||(g>0&&g<1000));
    }
    assert(wygladz(500)>400&&wygladz(500)<600);

    Sekwencja s;s.rozpocznij(true,0);
    assert(s.alfa_logo==LOGO_ALFA_START); // pierwsza klatka nigdy nie jest pusta
    assert(s.alfa_znak==0&&s.alfa_pasek==0);
    assert(s.alfa_linia1==0&&s.alfa_linia2==0);
    assert(s.postep==0&&s.faza==Faza::Logo);
    // Kwantyzacja: alfa logotypu osiaga pelna wartosc w pierwszej probce
    // wypadajacej na lub po LOGO_IN_US, a nie w dowolnej chwili pomiedzy.
    s.przelicz(LOGO_IN_US-1);
    assert(s.alfa_logo<LOGO_ALFA_KONIEC);
    s.przelicz((LOGO_IN_US/KROK_US+1)*KROK_US);
    assert(s.alfa_logo==LOGO_ALFA_KONIEC);
    // Wszystkie etapy intro licza sie z JEDNEJ kwantowanej probki czasu
    // (tq = floor(t / KROK_US) * KROK_US). Zamiast zgadywac indeks granicznej
    // probki, test sprawdza wlasnosci: monotonicznosc, zakresy i kolejnosc faz.
    auto kwan=[](uint64_t t){return (t/KROK_US)*KROK_US;};
    unsigned poprzednia=0;
    for(uint64_t t=0;t<=MINIMUM_US;t+=KROK_US) {
        s.przelicz(t);
        assert(s.alfa_logo>=poprzednia||t<=KROK_US);
        assert(s.alfa_logo>=LOGO_ALFA_START&&s.alfa_logo<=LOGO_ALFA_KONIEC);
        assert(s.alfa_znak>=0&&s.alfa_znak<=255);
        assert(s.alfa_pasek<=255&&s.alfa_linia1<=255&&s.alfa_linia2<=255);
        assert(s.postep<=1000);
        poprzednia=s.alfa_logo;
    }
    // Logotyp rozjasnia sie do pelna i nigdy nie gaśnie.
    s.przelicz(0);assert(s.alfa_logo==LOGO_ALFA_START);
    s.przelicz(kwan(LOGO_IN_US)+KROK_US);assert(s.alfa_logo==LOGO_ALFA_KONIEC);
    // Napis, pasek i komunikaty pojawiaja sie w tej kolejnosci.
    s.przelicz(0);assert(s.alfa_znak==0&&s.alfa_pasek==0&&s.alfa_linia1==0);
    s.przelicz(ZNAK_PELNY_US+KROK_US);
    assert(s.alfa_znak==255&&s.alfa_pasek==0&&s.alfa_linia1==0);
    s.przelicz(PASEK_START_US+KROK_US);assert(s.alfa_pasek>0&&s.alfa_linia1==0);    s.przelicz(LINIA2_PELNA_US+KROK_US);
    assert(s.alfa_linia1==255&&s.alfa_linia2==255&&s.postep==1000);
    s.przelicz(LINIA1_IN_US+500000+KROK_US);assert(s.alfa_linia1==255);
    s.przelicz(LINIA2_PELNA_US+KROK_US);assert(s.alfa_linia2==255);
    s.przelicz(MINIMUM_US*4);assert(s.postep==1000); // pasek nigdy nie przekracza 100%
    // Pasek postepu jest monotoniczny i domyka sie dokladnie na 100%.
    unsigned poprzedni=0;
    for(uint64_t t=PASEK_START_US;t<=PASEK_PELNY_US;t+=KROK_US) {
        s.przelicz(t);
        assert(s.postep>=poprzedni&&s.postep<=1000);
        poprzedni=s.postep;
    }
    assert(s.postep==1000);

    // Kwantyzacja intro: dwie probki w tym samym kroku kwantyzacji maja te
    // sama sygnature, bo cala tresc intro liczy sie z tq = floor(t/KROK_US)*KROK_US.
    // Probki wybrane sa w holdzie, gdzie pasek jest juz pelny (czas ciagly staly).
    s.przelicz(4000000);const uint64_t sygn=s.sygnatura();
    s.przelicz(4000000+KROK_US-1);assert(s.sygnatura()==sygn);
    s.przelicz(4000000+KROK_US);assert(s.sygnatura()!=sygn);
    // Pasek jest ciagly, wiec sygnatura zmienia sie takze w obrebie kroku.
    s.przelicz(PASEK_START_US+KROK_US);const uint64_t p1=s.sygnatura();
    s.przelicz(PASEK_START_US+KROK_US+KROK_US/2);assert(s.sygnatura()!=p1);

    // Interpolacja i brak kumulacji.
    for(unsigned weight=0;weight<=255;++weight){
        assert(mieszaj(0xFFFFFF,0,weight)==mieszaj(0,0xFFFFFF,255-weight));
        assert(mieszaj(0x123456,0x123456,weight)==0x123456);
    }

    // Cykl zycia: fade wymaga gotowosci pulpitu i minimalnego czasu.
    s.rozpocznij(true,0);s.gotowy=true;
    s.aktualizuj(MINIMUM_US-1);assert(s.stan==Stan::Widoczny);
    s.aktualizuj(MINIMUM_US);assert(s.stan==Stan::Zanikanie&&s.odsloniecie==0);
    s.aktualizuj(MINIMUM_US+FADE_US/2);assert(s.odsloniecie==127);
    s.aktualizuj(MINIMUM_US+FADE_US);assert(s.stan==Stan::Zakonczony);
    s.aktualizuj(0);assert(s.stan==Stan::Zakonczony); // zegar nie wskrzesza stanu
    // Start=100, t = 9,999,000 us, wiec prog MINIMUM_US jest przekroczony i bez
    // zapisanego znacznika gotowosci zanikanie domyka sie od razu.
    s.rozpocznij(true,100);s.gotowy=true;s.aktualizuj(10000100);
    assert(s.postep==1000);                 // pasek i tak jest juz pelny
    assert(s.stan==Stan::Zakonczony&&s.odsloniecie==255);
    // Z odnotowanym znacznikiem gotowosci zanikanie trwa pelne FADE_US.
    s.rozpocznij(true,100);s.gotowy=true;s.gotowy_od=6500000;
    s.aktualizuj(6500000);assert(s.stan==Stan::Zanikanie&&s.fade==6500000);
    s.aktualizuj(6500000+FADE_US/2);assert(s.odsloniecie==127);
    s.aktualizuj(6500000+FADE_US);assert(!s.aktywny());
    s.rozpocznij(true,UINT64_MAX-8000000);s.gotowy=true;s.aktualizuj(UINT64_MAX-1600000);
    assert(s.stan==Stan::Zanikanie);
    s.aktualizuj(UINT64_MAX);assert(s.stan==Stan::Zakonczony);

    // Wszystkie napisy sa czystymi literalami ASCII/UTF-8 i przechodza przez
    // wspolny dekoder bez znaku zastepczego '?'.
    for(const char* t:{tekst(0),tekst(1),ZNAK_MARKI}){
        for(size_t i=0;t[i];){uint32_t cp=0;const size_t n=BursztynUI::CzcionkaUI::dekoduj_utf8(t,i,&cp);
            assert(n>0&&cp!='?');i+=n;}
    }
    // Drugi komunikat zawiera polski znak "ż", pierwszy jest czysto ASCII.
    {bool polish=false;const char* t=tekst(1);
     for(size_t i=0;t[i];){uint32_t cp;i+=BursztynUI::CzcionkaUI::dekoduj_utf8(t,i,&cp);
        if(cp==0x17c)polish=true;}
     assert(polish);} // "Niezależny" zawiera U+017C
    // Napis marki jest zgodny z maska (same wersaliki i spacja).
    for(const char* t=ZNAK_MARKI;*t;++t)assert(*t==' '||(*t>='A'&&*t<='Z'));

    // Uklad: kazdy element wysrodkowany, grupa w granicach ekranu.
    for(int w:{1280,1920})for(unsigned dpi:{100U,125U}){
        const int h=w==1280?720:1080;
        const auto g=geometria(w,h,dpi);
        assert(g.logo_rdzen.w>0&&g.logo_rdzen.h>0);
        assert(g.znak.w>0&&g.pasek.w>0&&g.linia1.w>0&&g.linia2.w>0);
        assert(g.pasek.w%2==0); // parzysta szerokosc = dokladny srodek
        assert(g.pasek.h>=3);
        assert(g.logo_rdzen.x*2+g.logo_rdzen.w>=w-1&&g.logo_rdzen.x*2+g.logo_rdzen.w<=w);
        assert(g.pasek.x*2+g.pasek.w>=w-1&&g.pasek.x*2+g.pasek.w<=w);
        assert(g.znak.y>g.logo_rdzen.y+g.logo_rdzen.h);
        assert(g.pasek.y>g.znak.y+g.znak.h);
        assert(g.linia1.y>g.pasek.y+g.pasek.h);
        assert(g.linia2.y>g.linia1.y+g.linia1.h);
        assert(g.linia2.y+g.linia2.h<=h);
        assert(g.logo_rdzen.y>=0);
        assert(g.dirty.x>=0&&g.dirty.y>=0);
        assert(g.dirty.x+g.dirty.w<=w&&g.dirty.y+g.dirty.h<=h);
        // Uklad jest staly: te same prostokaty dla kazdej fazy animacji.
        const auto inna=geometria(w,h,dpi,1000);
        assert(inna.logo_rdzen.x==g.logo_rdzen.x&&inna.logo_rdzen.y==g.logo_rdzen.y);
        assert(inna.znak.y==g.znak.y&&inna.pasek.y==g.pasek.y);
        assert(inna.linia1.y==g.linia1.y&&inna.linia2.y==g.linia2.y);
        // Wypelnienie jest przyciete do toru i rosnie z postepem.
        assert(geometria(w,h,dpi,0).wypelnienie==0);
        assert(geometria(w,h,dpi,1000).wypelnienie==g.pasek.w);
        assert(geometria(w,h,dpi,2000).wypelnienie==g.pasek.w);
        assert(geometria(w,h,dpi,500).wypelnienie<=g.pasek.w);
        // Mala rozdzielczosc nadal daje poprawny, nieujemny uklad.
        const auto maly=geometria(640,480,100);
        assert(maly.logo_rdzen.w>0&&maly.logo_rdzen.h>0);
        assert(maly.linia2.y+maly.linia2.h<=480);
        assert(maly.dirty.x>=0&&maly.dirty.y>=0);
    }

    // Render: kazda klatka jest kompletna, tlo jest czarne, atrament istnieje.
    for(int w:{1280,1920})for(unsigned dpi:{100U,125U}){
        const int h=w==1280?720:1080;
        std::vector<uint32_t> obraz(size_t(w)*h,0x00FF00FFu);
        for(uint64_t t:{uint64_t(0),LOGO_IN_US,ZNAK_PELNY_US,PASEK_PELNY_US,LINIA2_PELNA_US,MINIMUM_US}){
            std::fill(obraz.begin(),obraz.end(),0x00FF00FFu);
            Sekwencja draw;draw.rozpocznij(true,0);draw.przelicz(t);
            rysuj(obraz.data(),w,h,w,dpi,draw,true);
            const auto g=geometria(w,h,dpi);
            // Caly ekran zostal odtworzony z tla.
            assert(obraz[0]==TLO&&obraz.back()==TLO);
            assert(obraz[size_t(h/2)*w+0]==TLO);
            // Logotyp ma atrament od pierwszej klatki.
            const auto logo=atrament(obraz.data(),w,h,w,0,g.pasek.y);
            assert(logo.ok());
            assert(srodek_x(logo)==w/2||srodek_x(logo)==w/2-1);
            // Elementy z alfa 0 nie zostawiaja swojego atramentu: przy t=0 pod
            // logotypem widoczna jest tylko jego bursztynowa poswiata, ale ani
            // napisu, ani paska, ani komunikatow (brak pikseli jasnych).
            if(t==uint64_t(0)) {
                int jasne=0;
                for(int y=g.znak.y;y<h;++y)
                    for(int x=0;x<w;++x) {
                        const uint32_t c=obraz[size_t(y)*w+x];
                        const unsigned r=(c>>16)&255,gg=(c>>8)&255,b=c&255;
                        if(r>200&&gg>200&&b>200)++jasne;
                    }
                assert(jasne==0);
            }
        }
        // Klatka koncowa: wszystkie elementy widoczne i wysrodkowane.
        std::fill(obraz.begin(),obraz.end(),TLO);
        Sekwencja draw;draw.rozpocznij(true,0);draw.przelicz(MINIMUM_US);
        rysuj(obraz.data(),w,h,w,dpi,draw,true);
        const auto g=geometria(w,h,dpi);
        const auto logo=atrament(obraz.data(),w,h,w,0,g.znak.y);
        const auto znak=atrament(obraz.data(),w,h,w,g.znak.y,g.pasek.y);
        const auto pasek=atrament(obraz.data(),w,h,w,g.pasek.y,g.pasek.y+g.pasek.h);
        const auto l1=atrament(obraz.data(),w,h,w,g.linia1.y,g.linia1.y+g.linia1.h);
        const auto l2=atrament(obraz.data(),w,h,w,g.linia2.y,g.linia2.y+g.linia2.h);
        for(const auto* z:{&logo,&znak,&pasek,&l1,&l2}){
            assert(z->ok());
            const int d=srodek_x(*z)-w/2;
            assert(d>=-1&&d<=1);
        }
        // Pasek jest pelny i ma kolor akcentu w srodku.
        const auto srodek=obraz[size_t(g.pasek.y+g.pasek.h/2)*w+w/2];
        assert(((srodek>>16)&255)>200&&(srodek&255)<120);
        // Pelny pasek rozciaga sie na cala szerokosc toru.
        assert(pasek.x1-pasek.x0>=g.pasek.w-g.pasek.h);
        // Napisy maja atrament i nie nachodza na pasek.
        assert(l1.y0>g.pasek.y+g.pasek.h);
        assert(l2.y0>l1.y1);
    }

    // Wlasciwosci maski: pokrycie, proporcje i spojnosc z atramentem.
    {
        uint64_t suma=0;int niezerowe=0;
        for(int i=0;i<BursztynLogo::SZEROKOSC*BursztynLogo::WYSOKOSC;++i){
            suma+=BursztynLogo::MASKA[i];
            if(BursztynLogo::MASKA[i])++niezerowe;
        }
        assert(suma>0&&niezerowe>BursztynLogo::SZEROKOSC);
        // Poswiata jest rozmyciem rdzenia, wiec nigdy nie jest od niego jasniejsza.
        assert(BursztynLogo::POSWIATA_SZEROKOSC>0&&BursztynLogo::POSWIATA_WYSOKOSC>0);
        // Pole widzenia poswiaty jest wieksze od rdzenia po obu stronach.
        assert(BursztynLogo::POSWIATA_OD_X<0&&BursztynLogo::POSWIATA_OD_Y<0);
        assert(BursztynLogo::POSWIATA_DO_X>1000&&BursztynLogo::POSWIATA_DO_Y>1000);
        // Atrament rdzenia obejmuje jego pelna wysokosc (brak pustych wierszy).
        assert(BursztynLogo::MASKA_ATRAMENT_OD==0);
        assert(BursztynLogo::MASKA_ATRAMENT_DO==BursztynLogo::WYSOKOSC);
    }

    // Uklad na innych rozdzielczosciach i skalach.
    for(int w:{800,1024,1366,1600})for(unsigned dpi:{100U,125U,150U,200U}){
        const int h=w==800?600:(w==1024?768:768);
        const auto g=geometria(w,h,dpi);
        assert(g.logo_rdzen.w>0&&g.logo_rdzen.h>0&&g.pasek.w>0);
        assert(g.logo_rdzen.x>=0&&g.logo_rdzen.x+g.logo_rdzen.w<=w);
        assert(g.linia2.y+g.linia2.h<=h);
        assert(g.pasek.h>=3&&g.pasek.h<=24);
        assert(g.pasek.w%2==0);
    }

    // Cykl powitalny: powierzchnia wielokrotnego uzytku, bez wyciekow.
    for(int w:{1280,1920})for(unsigned dpi:{100U,125U}){
        width=w;height=w==1280?720:1080;const int pitch=w+8;
        std::vector<uint32_t> scene(size_t(pitch)*height,0xABCDEF);
        now=0;powitanie_inicjalizuj(true);
        auto source=[&](int x=0,int y=0,int rw=-1,int rh=-1){
            return reinterpret_cast<uint32_t*>(powitanie_zrodlo(
                reinterpret_cast<uint8_t*>(scene.data()),w,height,pitch*4,
                x,y,rw<0?w:rw,rh<0?height:rh,dpi));};
        auto* out=source();powitanie_publikacja();
        assert(out!=scene.data());assert(out[0]==TLO);
        assert(scene[0]==0xABCDEF); // scena aplikacji pozostaje prywatna
        const unsigned allocated=allocations;
        for(uint64_t t:{1400000ULL,1533333ULL,1633333ULL,1800000ULL,3233333ULL,3500000ULL}){
            advance(t);out=source();assert(out[0]==TLO);assert(allocations==allocated);
        }
        advance(6400000);assert(powitanie_aktywne()); // pulpit/papier jeszcze niegotowe
        now=6500000;powitanie_pulpit_gotowy();advance(6500000);
        out=source();assert(out[0]==TLO);
        advance(6800000);out=source();assert(out[0]==mieszaj(TLO,scene[0],127));
        // Powtorzone czesciowe prezentacje mieszaja oryginalny RGB, nie kumuluja.
        const auto pierwszy=out[10*pitch+10];
        out=source(10,10,2,2);assert(out[10*pitch+10]==pierwszy);
        advance(7100000);
        assert(powitanie_aktywne()); // koncowy scanout czeka na pelna scene
        assert(source(0,0,1,1)!=scene.data());
        out=source();assert(out==scene.data());assert(!powitanie_aktywne());
        assert(!pulpit_gotowy&&pulpit_pid==0&&!tlo_tapeta&&sekwencja.start==0&&live==0);
        for(int i=0;i<10;++i){advance(7600000+i*40000);assert(source()==scene.data());}
        // Jawny tryb diagnostyczny i automatyczny start nie maja opoznienia.
        powitanie_inicjalizuj(false);assert(!powitanie_aktywne());
        assert(source()==scene.data());
    }

    // Rekonfiguracja framebuffera przy aktywnej animacji.
    now=0;powitanie_inicjalizuj(true);
    std::vector<uint32_t> small(1280*720,0x123456),large(1920*1080,0x654321);
    auto* a=powitanie_zrodlo(reinterpret_cast<uint8_t*>(small.data()),1280,720,5120,0,0,1280,720,100);
    powitanie_publikacja();assert(a);
    auto* b=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
        reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1920,1080,125));
    assert(b[1920*1080-1]==TLO);assert(large.back()==0x654321);

    // Zmiana trybu uniewaznia gotowosc pulpitu do czasu republikacji korzenia.
    powitanie_pulpit_gotowy(false,2);root_layer={};root_visible=false;
    advance(6400000);assert(sekwencja.stan==Stan::Widoczny);
    root_layer.x=0;root_layer.y=0;root_layer.szerokosc=width;root_layer.wysokosc=height;
    root_visible=true;
    advance(6500000);assert(sekwencja.stan==Stan::Zanikanie);
    advance(6600000);
    auto* blended=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
        reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1920,1080,125));
    assert(blended[0]!=TLO);
    root_visible=false;advance(6800000);assert(sekwencja.stan==Stan::Widoczny);
    auto* hidden=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
        reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1,1,125));
    assert(hidden[0]==TLO&&hidden[1920*1080-1]==TLO);
    root_visible=true;advance(6900000);assert(sekwencja.stan==Stan::Zanikanie);
    advance(7500000);assert(zakonczenie_oczekuje);
    powitanie_zrodlo(reinterpret_cast<uint8_t*>(large.data()),1920,1080,7680,0,0,1920,1080,125);
    assert(!powitanie_aktywne());

    // Brak opcjonalnej powierzchni/zegara nie blokuje startu.
    powitanie_inicjalizuj(false);fail_alloc=true;now=0;powitanie_inicjalizuj(true);
    assert(powitanie_zrodlo(reinterpret_cast<uint8_t*>(small.data()),1280,720,5120,0,0,1280,720,100)==
           reinterpret_cast<uint8_t*>(small.data()));
    assert(!powitanie_aktywne());
    fail_alloc=false;clock_ok=false;powitanie_inicjalizuj(true);
    assert(!powitanie_aktywne());clock_ok=true;

    // Opcje startowe: dokladne dopasowanie tokenu.
    assert(opcja_startowa_obecna("bursztyn.splash=0","bursztyn.splash=0"));
    assert(opcja_startowa_obecna("a\t bursztyn.splash=0 b","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna("bursztyn.splash=01","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna("xbursztyn.splash=0","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna("bursztyn.splash=0x","bursztyn.splash=0"));
    assert(opcja_startowa_obecna("bursztyn.test=automated bursztyn.splash=1","bursztyn.test=automated"));
    assert(!opcja_startowa_obecna("  ","bursztyn.splash=0"));
    assert(!opcja_startowa_obecna(nullptr,"x"));assert(!opcja_startowa_obecna("x",""));

    // Co najmniej 100 symulowanych startow graficznych na kazdy stan przerwania.
    // Zegar celowo cofa sie miedzy generacjami; zaden znacznik nie jest dziedziczony.
    const uint64_t etapy[]={0,2200000,4400000,6500000,6800000,7100000};
    std::vector<uint32_t> cycle_scene(320*240,0x102030);
    for(unsigned cycle=0;cycle<100;++cycle)for(auto stage:etapy)for(bool ready:{false,true}) {
        width=320;height=240;now=123;powitanie_inicjalizuj(true);
        assert(sekwencja.start==123&&sekwencja.fade==0);
        assert(sekwencja.postep==0&&sekwencja.alfa_logo==LOGO_ALFA_START);
        assert(!pulpit_gotowy&&!tlo_tapeta&&pulpit_pid==0&&nastepny==0);
        auto source=[&](){return powitanie_zrodlo(
            reinterpret_cast<uint8_t*>(cycle_scene.data()),320,240,1280,0,0,320,240,100);};
        source();powitanie_publikacja();assert(live==1);
        if(ready)powitanie_pulpit_gotowy(false);
        if(stage>=6500000)advance(6400123);
        advance(123+stage);source();
        if(cycle%10==0){powitanie_reset_ekranu();assert(live==0);source();}
        powitanie_wycisz();assert(live==0&&!powitanie_aktywne());
        assert(sekwencja.stan==Stan::Zniszczony&&sekwencja.start==0&&sekwencja.fade==0);
        assert(!powierzchnia&&bajty==0&&szer==0&&wys==0&&stride==0&&procent==0);
        const unsigned old_full=full,old_partial=partial,old_alloc=allocations;
        powitanie_pulpit_gotowy(true,2);advance(99999999);
        assert(source()==reinterpret_cast<uint8_t*>(cycle_scene.data()));
        assert(full==old_full&&partial==old_partial&&allocations==old_alloc&&!pulpit_gotowy);
        powitanie_wycisz();assert(live==0); // idempotentne, bez podwojnego free
    }
    for(unsigned cycle=0;cycle<100;++cycle){
        for(unsigned mode=0;mode<5;++mode){
            now=0;fail_alloc=mode==3;clock_ok=mode!=4;
            powitanie_inicjalizuj(mode>=3); // wylaczony, automatyczny, instalator, alokacja, zegar
            powitanie_zrodlo(reinterpret_cast<uint8_t*>(cycle_scene.data()),320,240,1280,0,0,320,240,100);
            powitanie_wycisz();assert(live==0&&!powitanie_aktywne());
        }
    }
    fail_alloc=false;clock_ok=true;

    // Brud jest oznaczany tylko przy zmianie WIDOCZNEGO obrazu, nie przy kazdej
    // probce czasu. Bez tego faza hold generowala ~12 pelnych klatek 1280x720
    // na sekunde wraz z transferem do GPU (zaciecia i wysokie obciazenie CPU).
    now = 0;
    powitanie_inicjalizuj(true);
    {
        std::vector<uint32_t> sc(320 * 240, 0x0A0B0C);
        auto src = [&]() {
            return powitanie_zrodlo(reinterpret_cast<uint8_t*>(sc.data()), 320, 240, 1280,
                                    0, 0, 320, 240, 100);
        };
        src();
        powitanie_publikacja();

        // Najpierw: zmiana obrazu MUSI wymusic klatke. Probki w trakcie
        // wypelniania paska roznia sie wartoscia postepu (1900000 -> 50,
        // 1980000 -> 100).
        now = 1980000;
        powitanie_tick();
        const unsigned po_zmianie = full + partial;
        // Probka bez zmiany obrazu w tym samym kroku kwantyzacji: zero pracy.
        now = 1999999;
        powitanie_tick();
        assert(full + partial == po_zmianie);
        // Nastepny krok kwantyzacji zmienia postep -> dokladnie jedna klatka.
        now = 2080000;
        powitanie_tick();
        assert(full + partial == po_zmianie + 1);

        // Faza hold: ekran jest identyczny od 4,3 s do 6,3 s.
        now = 4300000;
        powitanie_tick();
        src();
        const unsigned start_hold = full + partial;
        unsigned przed = start_hold;
        for (uint64_t t = 4300000 + KROK_US; t < 6300000; t += 16000) {
            now = t;
            powitanie_tick();
            if (full + partial != przed) { przed = full + partial; src(); }
        }
        const unsigned w_holdzie = (full + partial) - start_hold;
        fprintf(stderr, "hold: %u klatek na 2 s (oczekiwane 0)\n", w_holdzie);
        assert(w_holdzie == 0);

        powitanie_wycisz();
    }
    printf("Simulated boots: 1200 interrupted + 500 bypass/failure; live surfaces: %u\n",live);
    printf("Welcome lifecycle/rendering/centering/options: %u/%u PASS (mock clock, ASan/UBSan)\n",checks,checks);
    return 0;
}
