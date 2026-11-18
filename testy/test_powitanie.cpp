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
static unsigned prezentacje_startu=0;
void PrzeniesNaEkran(){++prezentacje_startu;}
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

// Czy w obszarze nie ma zadnego atramentu (wylacznie tlo).
static bool tylko_tlo(const uint32_t* p,int w,int h,int pitch,Powitanie::Obszar r) {
    for(int y=r.y<0?0:r.y;y<r.y+r.h&&y<h;++y)
        for(int x=r.x<0?0:r.x;x<r.x+r.w&&x<w;++x)
            if(p[size_t(y)*pitch+x]!=Powitanie::TLO)return false;
    return true;
}
static Zakres atrament_w(const uint32_t* p,int w,int h,int pitch,Powitanie::Obszar r) {
    Zakres z{w,h,-1,-1};
    for(int y=r.y<0?0:r.y;y<r.y+r.h&&y<h;++y)
        for(int x=r.x<0?0:r.x;x<r.x+r.w&&x<w;++x)
            if(p[size_t(y)*pitch+x]!=Powitanie::TLO) {
                if(x<z.x0)z.x0=x;if(x>z.x1)z.x1=x;
                if(y<z.y0)z.y0=y;if(y>z.y1)z.y1=y;
            }
    return z;
}

int main(){
    using namespace Powitanie;
    // Napisy: tytul w zapisie mieszanym, dwa podtytuly z polskim znakiem (UTF-8).
    assert(!strcmp(tekst(0),"Polski system operacyjny"));
    assert(!strcmp(tekst(1),"Niezale\u017cny system"));
    assert(strstr(tekst(1),"\xC5\xBC")!=nullptr); // U+017C jako dwa bajty UTF-8
    assert(LICZBA_KOMUNIKATOW==2);
    assert(!strcmp(ZNAK_MARKI,"Bursztyn OS"));
    // Czarne tlo i bursztynowy akcent.
    assert(TLO==0x000000&&AKCENT==0xFF8A00);
    // Logotyp i napisy sa wkompilowanymi maskami, nie dekoderem PNG/TTF.
    assert(BursztynLogo::SZEROKOSC>0&&BursztynLogo::WYSOKOSC>0);
    assert(NapisyPowitania::TYTUL.w>0&&NapisyPowitania::LINIA1.w>0&&NapisyPowitania::LINIA2.w>0);

    // Smoothstep bez FPU.
    assert(wygladz(0)==0&&wygladz(1000)==1000);
    for(unsigned u:{0U,125U,500U,875U,1000U}) {
        const unsigned g=wygladz(u);
        assert(g<=1000);
        assert(u==0||u==1000||(g>0&&g<1000));
    }
    assert(wygladz(500)>400&&wygladz(500)<600);

    // Etapy: odstepy ~1 s miedzy kolejnymi elementami, krotkie wejscia.
    assert(ODSTEP_ETAPOW_US==1000000&&WEJSCIE_TEKSTU_US<=600000&&WEJSCIE_SEPARATORA_US<=600000);
    for(unsigned e=ETAP_TYTUL;e<LICZBA_ETAPOW;++e)
        assert(nominalny_start(e)-nominalny_start(e-1)==ODSTEP_ETAPOW_US);
    // Gotowy pulpit nie czeka na cala sekwencje (minimum to tylko ochrona przed mignieciem).
    assert(MINIMUM_US<=nominalnie_pelny(ETAP_TYTUL)+KROK_US&&MINIMUM_US<SEKWENCJA_US);

    Sekwencja s;s.rozpocznij(true,0);
    assert(s.alfa_logo==LOGO_ALFA&&LOGO_ALFA==255); // logo w pelni od pierwszej klatki
    assert(s.alfa_tytul==0&&s.alfa_separator==0&&s.alfa_linia1==0&&s.alfa_linia2==0);
    assert(s.rozwiniecie==0&&s.faza==Faza::Logo&&s.etap==ETAP_LOGO&&s.widoczne==1u);
    // Bez potwierdzenia prezentacji logo nic sie nie zaczyna, nawet po wielu sekundach.
    s.aktualizuj(10000000);assert(s.etap==ETAP_LOGO&&s.alfa_tytul==0);
    // Idealny przebieg (prezentacja co KROK_US): kolejnosc i nominalne chwile.
    {
        Sekwencja i=sekwencja_idealna(nominalny_start(ETAP_TYTUL)-KROK_US);
        assert(i.etap==ETAP_LOGO&&i.alfa_tytul==0&&i.faza==Faza::Logo);
        i=sekwencja_idealna(nominalny_start(ETAP_TYTUL)+KROK_US*3);
        assert(i.alfa_tytul>0&&i.alfa_tytul<255&&i.faza==Faza::Tytul&&i.alfa_separator==0);
        i=sekwencja_idealna(nominalnie_pelny(ETAP_TYTUL));
        assert(i.alfa_tytul==255&&i.alfa_separator==0&&i.alfa_linia1==0&&i.alfa_linia2==0);
        i=sekwencja_idealna(nominalny_start(ETAP_SEPARATOR)-KROK_US);assert(i.alfa_separator==0&&i.rozwiniecie==0);
        i=sekwencja_idealna(nominalny_start(ETAP_SEPARATOR)+KROK_US*3);
        assert(i.alfa_separator>0&&i.rozwiniecie>0&&i.rozwiniecie<1000&&i.faza==Faza::Separator);
        i=sekwencja_idealna(nominalnie_pelny(ETAP_SEPARATOR));
        assert(i.alfa_separator==255&&i.rozwiniecie==1000&&i.alfa_linia1==0&&i.alfa_linia2==0);
        i=sekwencja_idealna(nominalny_start(ETAP_LINIA1)+KROK_US*3);
        assert(i.alfa_linia1>0&&i.alfa_linia2==0&&i.faza==Faza::Linia1);
        i=sekwencja_idealna(nominalnie_pelny(ETAP_LINIA1));assert(i.alfa_linia1==255&&i.alfa_linia2==0);
        i=sekwencja_idealna(nominalny_start(ETAP_LINIA2)-KROK_US);assert(i.alfa_linia2==0);
        i=sekwencja_idealna(nominalny_start(ETAP_LINIA2)+KROK_US*3);
        assert(i.alfa_linia2>0&&i.alfa_linia2<255&&i.faza==Faza::Linia2);
        i=sekwencja_idealna(SEKWENCJA_US);assert(i.alfa_linia2==255&&i.faza==Faza::Komplet);
        i=sekwencja_idealna(UINT64_MAX/2);assert(i.alfa_linia2==255&&i.rozwiniecie==1000);
    }
    // Monotonicznosc i zakresy w calym idealnym przebiegu.
    {
        Sekwencja q;q.rozpocznij(true,0);q.potwierdz(0);Sekwencja p=q;
        for(uint64_t t=KROK_US/2;t<=SEKWENCJA_US+KROK_US*4;t+=KROK_US/2) {
            q.aktualizuj(t);q.potwierdz(t);
            assert(q.alfa_logo>=p.alfa_logo&&q.alfa_tytul>=p.alfa_tytul&&q.alfa_separator>=p.alfa_separator);
            assert(q.alfa_linia1>=p.alfa_linia1&&q.alfa_linia2>=p.alfa_linia2&&q.rozwiniecie>=p.rozwiniecie);
            assert(q.alfa_logo<=255&&q.alfa_tytul<=255&&q.alfa_linia2<=255&&q.rozwiniecie<=1000);
            assert(int(q.faza)>=int(p.faza)&&q.etap<=p.etap+1);
            p=q;
        }
    }

    // Kwantyzacja: probki w jednym kroku daja te sama sygnature, w holdzie
    // sygnatura jest stala przez wiele krokow (zero klatek).
    {
        Sekwencja k=sekwencja_idealna(nominalny_start(ETAP_TYTUL)+KROK_US*4);const uint64_t sygn=k.sygnatura();
        k.aktualizuj(nominalny_start(ETAP_TYTUL)+KROK_US*5-1);assert(k.sygnatura()==sygn);
        k.aktualizuj(nominalny_start(ETAP_TYTUL)+KROK_US*5);assert(k.sygnatura()!=sygn);
        Sekwencja h=sekwencja_idealna(SEKWENCJA_US+KROK_US);const uint64_t hold=h.sygnatura();
        h.aktualizuj(SEKWENCJA_US+KROK_US*50);assert(h.sygnatura()==hold);
    }

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
    // Gotowy pulpit: zanikanie startuje przed koncem sekwencji i ja zamraza -
    // separator nie zaczyna sie pojawiac w trakcie odchodzenia sceny.
    s.rozpocznij(true,0);s.potwierdz(0);s.gotowy=true;s.gotowy_od=200000;
    for(uint64_t t=KROK_US;t<MINIMUM_US;t+=KROK_US){s.aktualizuj(t);s.potwierdz(t);}
    s.aktualizuj(MINIMUM_US);s.potwierdz(MINIMUM_US);assert(s.stan==Stan::Zanikanie&&s.fade==MINIMUM_US);
    for(uint64_t t=MINIMUM_US+KROK_US;t<MINIMUM_US+FADE_US;t+=KROK_US){s.aktualizuj(t);s.potwierdz(t);}
    assert(s.stan==Stan::Zanikanie&&s.alfa_separator==0&&s.etap==ETAP_TYTUL&&s.alfa_tytul==255&&s.faza==Faza::Zanikanie);
    assert(MINIMUM_US+FADE_US-1>nominalny_start(ETAP_SEPARATOR));
    s.aktualizuj(MINIMUM_US+FADE_US);assert(!s.aktywny());
    // Bez znacznika gotowosci po dlugim czasie zanikanie domyka sie od razu.
    s.rozpocznij(true,100);s.gotowy=true;s.aktualizuj(10000100);
    assert(s.stan==Stan::Zakonczony&&s.odsloniecie==255);
    // Z odnotowanym znacznikiem gotowosci zanikanie trwa pelne FADE_US.
    s.rozpocznij(true,100);s.gotowy=true;s.gotowy_od=6500000;
    s.aktualizuj(6500000);assert(s.stan==Stan::Zanikanie&&s.fade==6500000);
    s.aktualizuj(6500000+FADE_US/2);assert(s.odsloniecie==127);
    s.aktualizuj(6500000+FADE_US);assert(!s.aktywny());
    s.rozpocznij(true,UINT64_MAX-3000000);s.gotowy=true;s.aktualizuj(UINT64_MAX-3000000+MINIMUM_US);
    assert(s.stan==Stan::Zanikanie);
    s.aktualizuj(UINT64_MAX);assert(s.stan==Stan::Zakonczony);

    // Filtr obszarowy: pelna maska daje pelne pokrycie w kazdym rozmiarze,
    // szachownica sredni sie do ~50%, zaden zapis nie wychodzi poza cel.
    {
        static uint8_t pelna[40*30],szach[40*30];
        for(int i=0;i<40*30;++i){pelna[i]=255;szach[i]=((i%40+i/40)&1)?255:0;}
        for(int dw:{1,7,13,20,39,40,57})for(int dh:{1,5,15,29,30,44}) {
            int n=0;bool ok=true;
            auto e=[&](int x,int y,unsigned a){++n;ok&=x>=0&&x<dw&&y>=0&&y<dh&&a==255;};
            probkuj_obszarowo(pelna,40,30,dw,dh,255,e);
            assert(ok&&n==dw*dh);
        }
        unsigned min=255,max=0;
        auto e=[&](int,int,unsigned a){if(a<min)min=a;if(a>max)max=a;};
        probkuj_obszarowo(szach,40,30,10,10,255,e);
        assert(min>=120&&max<=136);
        int zero=0;auto z=[&](int,int,unsigned){++zero;};
        probkuj_obszarowo(pelna,40,30,10,10,0,z);probkuj_obszarowo(pelna,40,30,0,10,255,z);
        probkuj_obszarowo(nullptr,40,30,10,10,255,z);assert(zero==0);
    }

    // Uklad: staly, wysrodkowany, proporcjonalny i w granicach ekranu.
    struct Ekran {int w,h;};
    for(Ekran e:{Ekran{1024,768},Ekran{1280,720},Ekran{1366,768},Ekran{1920,1080},Ekran{800,600},
                 Ekran{640,480},Ekran{2560,1440},Ekran{3840,2160},Ekran{1280,1024},Ekran{720,1280}})
    for(unsigned dpi:{100U,125U,150U,175U,200U,99U}) {
        const int w=e.w,h=e.h;
        const auto g=geometria(w,h,dpi);
        const auto& m=g.m;
        assert(m.skala>=1&&m.skala<=SKALA_MAX);
        // Logotyp: proporcje maski zachowane, maly wzgledem ekranu.
        assert(g.logo_rdzen.w>0&&g.logo_rdzen.h>0);
        const int64_t prop=int64_t(g.logo_rdzen.w)*BursztynLogo::WYSOKOSC-int64_t(g.logo_rdzen.h)*BursztynLogo::SZEROKOSC;
        assert(prop<=BursztynLogo::WYSOKOSC&&prop>=-BursztynLogo::WYSOKOSC);
        assert(g.logo_rdzen.h*100<=h*(dpi==100?17:30)); // wieksza skala UI to swiadomy wybor
        // Kazdy element wysrodkowany w poziomie (+-1 px).
        for(const Obszar* r:{&g.logo_rdzen,&g.tytul.r,&g.separator,&g.linia1.r,&g.linia2.r}) {
            assert(r->w>0&&r->h>0);
            assert(r->x*2+r->w>=w-1&&r->x*2+r->w<=w+1);
            assert(r->x>=0&&r->x+r->w<=w);
        }
        // Kolejnosc pionowa bez nachodzenia atramentu.
        assert(g.logo.y>=0);
        assert(g.logo_rdzen.y+g.logo_rdzen.h<g.tytul.r.y);
        assert(g.tytul.r.y+g.tytul.r.h<=g.separator.y);
        assert(g.separator.y+g.separator.h<g.linia1.r.y);
        assert(g.linia1.r.y+g.linia1.r.h<=g.linia2.r.y);
        assert(g.linia2.r.y+g.linia2.r.h<=h);
        assert(g.tytul.baza>g.tytul.r.y&&g.tytul.baza<=g.tytul.r.y+g.tytul.r.h);
        // Hierarchia: tytul > podtytul > drugi podtytul; separator cienki i krotki.
        assert(m.tytul_wersalik>m.linia1_wersalik&&m.linia1_wersalik>m.linia2_wersalik);
        assert(g.separator.w*2<g.tytul.r.w&&g.separator.h<=4);
        assert(g.tytul.r.w*100<=w*86+100);
        // Dirty obejmuje wszystko i lezy w ekranie.
        assert(g.dirty.x>=0&&g.dirty.y>=0&&g.dirty.x+g.dirty.w<=w&&g.dirty.y+g.dirty.h<=h);
        assert(g.dirty.y<=g.logo_rdzen.y&&g.dirty.y+g.dirty.h>=g.linia2.r.y+g.linia2.r.h);
    }
    // Typowe rozdzielczosci: logo wyraznie mniejsze niz dawne 190 px przy 720p,
    // tytul czytelny, ale nie przesadzony (wersaliki 3.5..5.5% wysokosci).
    for(Ekran e:{Ekran{1024,768},Ekran{1280,720},Ekran{1366,768},Ekran{1920,1080}}) {
        const auto g=geometria(e.w,e.h,100);
        assert(g.logo_rdzen.h*1000<=e.h*140);
        assert(g.m.tytul_wersalik*1000>=e.h*35&&g.m.tytul_wersalik*1000<=e.h*55);
        // Kompozycja wysrodkowana w pionie (z lekkim uniesieniem optycznym).
        const int srodek=(g.logo_rdzen.y+g.linia2.r.y+g.linia2.r.h)/2;
        assert(srodek<e.h/2&&srodek>e.h/2-e.h/20);
    }
    assert(geometria(1280,720,100).logo_rdzen.h<=100);

    // Render: kazdy etap pokazuje tylko swoje elementy, wszystko wysrodkowane.
    for(Ekran e:{Ekran{1280,720},Ekran{1920,1080},Ekran{1024,768},Ekran{1366,768}})for(unsigned dpi:{100U,125U}){
        const int w=e.w,h=e.h;
        std::vector<uint32_t> obraz(size_t(w)*h,0x00FF00FFu);
        const auto g=geometria(w,h,dpi);
        struct Probka {uint64_t t;bool tytul,separator,l1,l2;};
        for(Probka et:{Probka{0,false,false,false,false},Probka{nominalny_start(ETAP_TYTUL)-KROK_US,false,false,false,false},
                     Probka{nominalnie_pelny(ETAP_TYTUL),true,false,false,false},
                     Probka{nominalnie_pelny(ETAP_SEPARATOR),true,true,false,false},
                     Probka{nominalnie_pelny(ETAP_LINIA1),true,true,true,false},
                     Probka{nominalnie_pelny(ETAP_LINIA2),true,true,true,true}}){
            std::fill(obraz.begin(),obraz.end(),0x00FF00FFu);
            const Sekwencja draw=sekwencja_idealna(et.t);
            rysuj(obraz.data(),w,h,w,dpi,draw,true);
            // Caly ekran zostal odtworzony z tla.
            assert(obraz[0]==TLO&&obraz.back()==TLO);
            for(uint32_t px:obraz)assert(px!=0x00FF00FFu);
            // Logotyp ma atrament od pierwszej klatki i jest wysrodkowany.
            const auto logo=atrament_w(obraz.data(),w,h,w,g.logo);
            assert(logo.ok());
            assert(srodek_x(logo)>=w/2-1&&srodek_x(logo)<=w/2);
            assert(tylko_tlo(obraz.data(),w,h,w,g.tytul.r)==!et.tytul);
            assert(tylko_tlo(obraz.data(),w,h,w,g.separator)==!et.separator);
            assert(tylko_tlo(obraz.data(),w,h,w,g.linia1.r)==!et.l1);
            assert(tylko_tlo(obraz.data(),w,h,w,g.linia2.r)==!et.l2);
            // Poza prostokatem ukladu nic nie jest rysowane.
            for(int y=0;y<h;y+=7)for(int x=0;x<w;x+=5)
                if(x<g.dirty.x||x>=g.dirty.x+g.dirty.w||y<g.dirty.y||y>=g.dirty.y+g.dirty.h)
                    assert(obraz[size_t(y)*w+x]==TLO);
        }
        // Klatka koncowa: atrament kazdego elementu wysrodkowany na ekranie.
        const auto t=atrament_w(obraz.data(),w,h,w,g.tytul.r);
        const auto sep=atrament_w(obraz.data(),w,h,w,g.separator);
        const auto l1=atrament_w(obraz.data(),w,h,w,g.linia1.r);
        const auto l2=atrament_w(obraz.data(),w,h,w,g.linia2.r);
        for(const auto* z:{&t,&sep,&l1,&l2}){
            assert(z->ok());
            const int d=(z->x0+z->x1+1)-w; // podwojona odchylka srodka atramentu
            assert(d>=-3&&d<=3);
        }
        // Wygladzanie: tytul ma wiele posrednich odcieni, nie tylko 0/kolor.
        unsigned posrednie=0,pelne=0;
        for(int y=g.tytul.r.y;y<g.tytul.r.y+g.tytul.r.h;++y)
            for(int x=g.tytul.r.x;x<g.tytul.r.x+g.tytul.r.w;++x) {
                const uint32_t c=obraz[size_t(y)*w+x];
                if(c==TEKST)++pelne;else if(c!=TLO)++posrednie;
            }
        assert(pelne>0&&posrednie>unsigned(g.tytul.r.w));
        // Separator: bursztyn w srodku, wygaszone konce.
        const uint32_t sc=obraz[size_t(g.separator.y)*w+w/2];
        assert(((sc>>16)&255)>180&&((sc>>8)&255)>90&&(sc&255)<40);
        const uint32_t sk=obraz[size_t(g.separator.y)*w+g.separator.x];
        assert(((sk>>16)&255)<((sc>>16)&255)/2);
        // Kolory: tytul jasny, drugi podtytul przygaszony.
        unsigned max_l2=0;
        for(int y=l2.y0;y<=l2.y1;++y)for(int x=l2.x0;x<=l2.x1;++x){
            const unsigned v=(obraz[size_t(y)*w+x]>>8)&255;if(v>max_l2)max_l2=v;}
        assert(max_l2<=((TEKST_PRZYGASZONY>>8)&255)&&max_l2>=0x60);
    }

    // Wlasciwosci maski logotypu.
    {
        uint64_t suma=0;int niezerowe=0;
        for(int i=0;i<BursztynLogo::SZEROKOSC*BursztynLogo::WYSOKOSC;++i){
            suma+=BursztynLogo::MASKA[i];
            if(BursztynLogo::MASKA[i])++niezerowe;
        }
        assert(suma>0&&niezerowe>BursztynLogo::SZEROKOSC);
        assert(BursztynLogo::POSWIATA_SZEROKOSC>0&&BursztynLogo::POSWIATA_WYSOKOSC>0);
        assert(BursztynLogo::POSWIATA_OD_X<0&&BursztynLogo::POSWIATA_OD_Y<0);
        assert(BursztynLogo::POSWIATA_DO_X>1000&&BursztynLogo::POSWIATA_DO_Y>1000);
        assert(BursztynLogo::MASKA_ATRAMENT_OD==0);
        assert(BursztynLogo::MASKA_ATRAMENT_DO==BursztynLogo::WYSOKOSC);
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
        assert(sekwencja.rozwiniecie==0&&sekwencja.alfa_tytul==0&&sekwencja.alfa_logo==LOGO_ALFA&&sekwencja.etap==ETAP_LOGO);
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
        // Kompozytor: klatka trafia na ekran (render + udana publikacja).
        auto pokaz = [&]() { src(); powitanie_publikacja(); };
        pokaz();
        // Regularny kompozytor (~60 Hz probek): pelna klatka tylko, gdy oznaczono brud.
        unsigned klatki = 0;
        for (uint64_t t = 16000; t <= SEKWENCJA_US + 500000; t += 16000) {
            now = t;
            const unsigned p = full + partial;
            powitanie_tick();
            if (full + partial != p) { pokaz(); ++klatki; }
        }
        assert(sekwencja.faza == Faza::Komplet && sekwencja.pelny[ETAP_LINIA2]);
        // Klatki tylko podczas wejsc (najwyzej jedna na krok) i potwierdzen.
        fprintf(stderr, "sekwencja: %u klatek na %llu ms\n", klatki,
                (unsigned long long)((SEKWENCJA_US + 500000) / 1000));
        assert(klatki >= 4 && klatki <= SEKWENCJA_US / KROK_US + 12);

        // Faza hold: po wejsciu drugiego podtytulu ekran jest staly.
        now = SEKWENCJA_US + 600000;
        powitanie_tick();
        pokaz();
        const unsigned start_hold = full + partial;
        unsigned przed = start_hold;
        for (uint64_t t = SEKWENCJA_US + 600000 + KROK_US; t < SEKWENCJA_US + 2600000; t += 16000) {
            now = t;
            powitanie_tick();
            if (full + partial != przed) { przed = full + partial; pokaz(); }
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
