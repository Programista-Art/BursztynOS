#pragma once
#include <stdint.h>
#include "bursztyn_ui/czcionka.h"
#include "logo_maska_dane.h"

// Ekran powitalny: czarne tlo, logotyp Bursztyn OS, napis z rozstrzeleniem,
// pasek postepu i dwa komunikaty po jego zapelnieniu.
//
// Cala animacja jest funkcja czasu absolutnego i geometrii ekranu. Renderer nie
// alokuje pamieci, nie uzywa FPU i nie dekoduje PNG: maska logotypu jest
// wkompilowana w .rodata, a kazda klatka jest kompletna (tlo + wszystkie
// elementy), wiec powierzchnia wielokrotnego uzytku nie przenosi historii.
namespace Powitanie {
enum class Tlo { Tapeta, Zastepcze }; // fallback jest poprawny przy ladowaniu lub awarii
enum class Stan { Nieaktywny, Widoczny, Zanikanie, Zakonczony, Wyciszanie, Zniszczony };
enum class Faza { Logo, Znak, Pasek, Linie, Zanikanie, Zakonczony };

// ---------------------------------------------------------------------------
// Paleta i czasy
// ---------------------------------------------------------------------------
constexpr uint32_t TLO=0x000000;          // czarne tlo marki
constexpr uint32_t AKCENT=0xFF8A00;       // bursztyn logotypu
constexpr uint32_t AKCENT_JASNY=0xFFC24D; // czolo paska postepu
constexpr uint32_t POSWIATA_KOLOR=0x8A4A00;
constexpr uint32_t TEKST=0xEDF0F4;        // napis pod logotypem
constexpr uint32_t TEKST_PRZYGASZONY=0x98A1AC;
constexpr uint32_t TOR=0x2A2F38;          // tor paska (widoczny na czerni)
constexpr uint32_t TOR_OBRAMOWANIE=0x3D444F;

// 6.4 s sekwencji + 0.6 s przejscia do pulpitu (bez zmian wobec poprzedniej wersji).
constexpr uint64_t MINIMUM_US=6400000, FADE_US=600000;

// Kwantyzacja stanu intro. 12.5 Hz jest celowo wolniejsze od kompozytora,
// dzieki czemu faza logo/napisow nie wymusza pelnego renderu co klatke.
constexpr uint64_t KROK_US=80000;

constexpr uint64_t LOGO_IN_US=650000;      // rozjasnianie logotypu
constexpr uint64_t ZNAK_IN_US=650000;      // wejscie napisu pod logotypem
constexpr uint64_t ZNAK_PELNY_US=1350000;  // napis osiaga pelna jasnosc
constexpr uint64_t PASEK_IN_US=1350000;    // pojawienie sie toru paska
constexpr uint64_t PASEK_START_US=1800000; // pasek zaczyna sie wypelniac
constexpr uint64_t PASEK_PELNY_US=3400000; // pasek osiaga 100%
constexpr uint64_t LINIA1_IN_US=3400000;   // pierwszy komunikat
constexpr uint64_t LINIA2_IN_US=3650000;   // drugi komunikat
constexpr uint64_t LINIA2_PELNA_US=4150000;

constexpr unsigned LOGO_ALFA_START=96;     // pierwsza klatka nigdy nie jest pusta
constexpr unsigned LOGO_ALFA_KONIEC=255;
constexpr unsigned POSWIATA_UDZIAL=88;     // udzial poswiaty w pokryciu rdzenia

// ---------------------------------------------------------------------------
// Pomocnicze operacje
// ---------------------------------------------------------------------------
inline uint32_t mieszaj(uint32_t a,uint32_t b,unsigned w) {
    uint32_t out=0;
    for(unsigned s=0;s<24;s+=8)
        out|=((((a>>s)&255)*(255-w)+((b>>s)&255)*w+127)/255)<<s;
    return out;
}
// Postep 0..1000 miedzy dwoma znacznikami czasu; poza zakresem domyka.
inline unsigned liniowo(uint64_t t,uint64_t od,uint64_t do_) {
    if(t<=od)return 0;
    if(t>=do_)return 1000;
    return unsigned((t-od)*1000/(do_-od));
}
// Smoothstep 0..1000 -> 0..1000 bez FPU (3x^2-2x^3).
inline unsigned wygladz(unsigned u) {
    if(u==0)return 0;
    if(u>=1000)return 1000;
    const uint64_t x=u;
    return unsigned((x*x*(3000-2*x))/1000000);
}
// Nakladanie koloru o podanym pokryciu 0..255 na piksel.
inline uint32_t naloz(uint32_t dst,uint32_t kolor,unsigned a) {
    return a>=255?kolor:mieszaj(dst,kolor,a);
}

inline const char* tekst(unsigned i) {
    static const char* const t[]={"Polski system operacyjny","Niezale\u017cny system"};
    return t[i==0?0:1];
}
constexpr unsigned LICZBA_KOMUNIKATOW=2;
constexpr const char* ZNAK_MARKI="BURSZTYN OS";

// Nazwa etapu animacji do diagnostyki. Logujemy etapy, nie klatki, wiec
// diagnostyka pozostaje ograniczona nawet przy dlugim starcie.
inline const char* nazwa_fazy(Faza f) {
    switch(f) {
        case Faza::Logo: return "logo_hold";
        case Faza::Znak: return "wordmark_hold";
        case Faza::Pasek: return "bar_progress";
        case Faza::Linie: return "lines_hold";
        case Faza::Zanikanie: return "fade_start";
        default: return "fade_end";
    }
}

// ---------------------------------------------------------------------------
// Uklad: wszystkie wymiary wyliczane z wysokosci ekranu i skali DPI
// ---------------------------------------------------------------------------
constexpr int LOGO_PREFEROWANA=190;   // wysokosc rdzenia maski przy 720p i 100%
constexpr int LOGO_MINIMALNA=24;
constexpr int ODSTEP_LOGO_ZNAK=38;
constexpr int ODSTEP_ZNAK_PASEK=34;
constexpr int ODSTEP_PASEK_LINIA=30;
constexpr int ODSTEP_LINII=10;
constexpr int PASEK_SZEROKOSC=320;
constexpr int PASEK_WYSOKOSC=5;

struct Obszar {int x,y,w,h;};

inline bool pusty(Obszar r) {return r.w<=0||r.h<=0;}
// Przyciecie prostokata do ekranu; false, gdy nie ma czego rysowac.
inline bool obszar_w_ekranie(const Obszar& r,int w,int h,int& x0,int& y0,int& x1,int& y1) {
    x0=r.x>0?r.x:0; y0=r.y>0?r.y:0;
    const int64_t rx=int64_t(r.x)+r.w, ry=int64_t(r.y)+r.h;
    x1=rx<w?int(rx):w; y1=ry<h?int(ry):h;
    return x1>x0&&y1>y0;
}
inline Obszar polacz(Obszar a,Obszar b) {
    if(pusty(a))return b;
    if(pusty(b))return a;
    const int x0=a.x<b.x?a.x:b.x, y0=a.y<b.y?a.y:b.y;
    const int x1=(a.x+a.w)>(b.x+b.w)?(a.x+a.w):(b.x+b.w);
    const int y1=(a.y+a.h)>(b.y+b.h)?(a.y+a.h):(b.y+b.h);
    return {x0,y0,x1-x0,y1-y0};
}
inline Obszar przesun(Obszar r,int dy) {r.y+=dy;return r;}
// Prostokat wysrodkowany w poziomie. Parzysta szerokosc daje rowna liczbe
// pikseli po obu stronach, wiec srodek atramentu wypada dokladnie w srodku.
inline Obszar wysrodkuj(int w,int y,int szer,int wys) {
    return {(w-szer)/2,y,szer,wys};
}inline Obszar powieksz(Obszar r,int m) {return {r.x-m,r.y-m,r.w+2*m,r.h+2*m};}

// Margines poswiaty wokol rdzenia maski (symetryczny, wiec srodek sie nie zmienia).
inline int margines_logo(int rdzen_h) {
    const int m=rdzen_h/28;
    return m<2?2:m;
}

// Szerokosc tekstu z rozstrzeleniem. Spacja dostaje wieksza przerwe, zeby
// wyrazy nie zlewaly sie. Ta funkcja jest jedynym zrodlem metryki ukladu.
inline int postep_rozstrzelony(uint32_t cp,uint32_t p,int odstep) {
    using BursztynUI::CzcionkaUI;
    // Spacja nie dostaje rozstrzelenia: jej krok pisma jest juz przerwa
    // miedzywyrazowa, wiec slowa nie rozjezdzaja sie o podwojny odstep.
    // Przy odstep == 0 wynik jest identyczny z rendererem tekstu.
    const int krok=ui_skaluj(CzcionkaUI::szerokosc_znaku(cp)+1,p);
    return cp==' '?krok:krok+odstep;
}
inline int szerokosc_rozstrzelona(const char* s,uint32_t p,int odstep) {
    if(!s)return 0;
    using BursztynUI::CzcionkaUI;
    int szer=0;
    int ostatni=0;
    for(size_t i=0;s[i];) {
        uint32_t cp=0;
        const size_t krok=CzcionkaUI::dekoduj_utf8(s,i,&cp);
        if(!krok)break;
        i+=krok;
        ostatni=postep_rozstrzelony(cp,p,odstep);
        szer+=ostatni;
    }
    // Ostatni znak nie wnosi prawej przerwy - inaczej tekst bylby przesuniety.
    if(szer>0&&s[0])szer-=odstep;
    return szer;}
// Glif zajmuje NAJWYZSZE bity 16-bitowego wiersza (bit 15 = kolumna 0).
// Ta maska jest jedynym zrodlem wiedzy o ukladzie bitow dla metryk i renderu.
inline uint16_t maska_glifu(int gw) {
    if(gw<1)return 0;
    if(gw>16)gw=16;
    return uint16_t(0xFFFFu<<(16-gw));
}

// Granice atramentu calego napisu wzgledem poczatku rysowania. Renderer
// centruje po atramencie, a nie po szerokosci kroku pisma, wiec marginesy
// boczne glifow nie przesuwaja tekstu w lewo.
struct Granice {int lewy,prawy;};
inline Granice granice_atramentu(const char* s,uint32_t p,int odstep) {
    using BursztynUI::CzcionkaUI;
    Granice g{-1,-1};
    if(!s||!s[0])return {0,0};
    int x=0;
    for(size_t i=0;s[i];) {
        uint32_t cp=0;
        const size_t krok=CzcionkaUI::dekoduj_utf8(s,i,&cp);
        if(!krok)break;
        i+=krok;
        const int gw=CzcionkaUI::szerokosc_znaku(cp);
        const uint16_t* glif=CzcionkaUI::bitmapa_znaku(cp);
        if(glif&&gw>0&&cp!=' ') {
            int minc=-1,maxc=-1;
            const uint16_t maska=maska_glifu(gw);
            for(int r=0;r<16;++r) {
                const uint16_t wiersz=uint16_t(glif[r]&maska);
                if(!wiersz)continue;
                for(int c=0;c<gw;++c)
                    if(wiersz&(0x8000u>>c)){if(minc<0||c<minc)minc=c;if(c>maxc)maxc=c;}
            }
            if(minc>=0) {
                if(g.lewy<0)g.lewy=x+ui_skaluj(minc,p);
                const int prawy=x+ui_skaluj(maxc+1,p);
                if(prawy>g.prawy)g.prawy=prawy;
            }
        }
        x+=postep_rozstrzelony(cp,p,odstep);
    }
    if(g.lewy<0)return {0,0};
    return g;
}
// Pozycja X rysowania taka, ze atrament napisu jest wysrodkowany na ekranie.
inline int x_dla_atramentu(const char* s,int w,uint32_t p,int odstep) {
    const Granice g=granice_atramentu(s,p,odstep);
    return w/2-(g.lewy+g.prawy)/2;
}

// Najwieksza skala pisma nie wieksza od preferowanej, ktora miesci sie w maxw.
// Renderer grayscale nie schodzi ponizej 100%, wiec to jest dolna granica.
inline uint32_t dopasuj_pismo(const char* s,uint32_t preferowane,int maxw,int odstep_bazowy) {
    if(maxw<=0||!s)return preferowane;
    uint32_t p=preferowane;
    for(unsigned i=0;i<8;++i) {
        const int odstep=int(uint64_t(odstep_bazowy)*p/100);
        const int szer=szerokosc_rozstrzelona(s,p,odstep);
        if(szer<=maxw||szer<=0)break;
        uint32_t nastepne=uint32_t(uint64_t(p)*uint32_t(maxw)/uint32_t(szer));
        if(nastepne>=p)nastepne=p>4?p-4:1;
        if(nastepne<100)nastepne=100;
        if(nastepne==p)break;
        p=nastepne;
    }
    return p;
}

// Wiersze atramentu w komorce pisma (0..15). Dzieki temu uklad pionowy liczy
// sie na faktycznie widocznych pikselach, a nie na pelnych komorkach linii.
inline void wiersze_atramentu(const char* s,bool* wiersze) {
    using BursztynUI::CzcionkaUI;
    for(int r=0;r<16;++r)wiersze[r]=false;
    if(!s)return;
    for(size_t i=0;s[i];) {
        uint32_t cp=0;
        const size_t krok=CzcionkaUI::dekoduj_utf8(s,i,&cp);
        if(!krok)break;
        i+=krok;
        const uint16_t* glif=CzcionkaUI::bitmapa_znaku(cp);
        const int gw=CzcionkaUI::szerokosc_znaku(cp);
        if(!glif||gw<1)continue;
        const uint16_t maska=maska_glifu(gw);
        for(int r=0;r<16;++r)
            if(glif[r]&maska)wiersze[r]=true;
    }
}
// Granice atramentu jednej linii wzgledem gornej krawedzi jej komorki.
struct Atrament {int gora,dol;};
inline Atrament atrament_linii(const char* s,uint32_t p) {
    bool w[16];
    wiersze_atramentu(s,w);
    int pierwszy=-1,ostatni=-1;
    for(int r=0;r<16;++r)if(w[r]){if(pierwszy<0)pierwszy=r;ostatni=r;}
    if(pierwszy<0)return {0,ui_skaluj(16,p)};
    return {ui_skaluj(pierwszy,p),ui_skaluj(ostatni+1,p)};
}

struct Miary {
    uint32_t p_baz,p_znak,p_linia1,p_linia2;
    int logo_h,logo_w,logo_margines;
    int logo_atrament_gora,logo_atrament_dol; // widoczny atrament logo wzgledem rdzenia
    int znak_h,znak_w,znak_odstep;
    int pasek_w,pasek_h;
    int linia1_h,linia1_w,linia2_h,linia2_w;
    Granice gran_znak,gran_linia1,gran_linia2;
    Atrament in_znak,in_linia1,in_linia2;
    int od_1,od_2,od_3,od_4;
    int blok_h,blok_w;
};

inline Miary miary(int w,int h,uint32_t dpi) {
    if(!ui_skala_poprawna(dpi))dpi=100;
    if(w<1)w=1;
    if(h<1)h=1;
    Miary m{};
    // Skala bazowa z wysokosci ekranu: 720p -> 100%, 1080p -> 150%, maks. 200%.
    int pb=int((int64_t(h)*100+360)/720);
    if(pb<100)pb=100;
    if(pb>200)pb=200;
    m.p_baz=uint32_t(pb);
    const int dostepne_w=int(uint64_t(w)*92/100);
    const int dostepne_h=int(uint64_t(h)*86/100);
    auto pismo=[&](int baza)->uint32_t {
        int64_t v=int64_t(baza)*pb*dpi/10000;
        if(v<100)v=100;
        if(v>800)v=800;
        return uint32_t(v);
    };
    m.p_znak=dopasuj_pismo(ZNAK_MARKI,pismo(212),dostepne_w,int(uint64_t(212)*pb*dpi/10000/3));
    m.p_linia1=dopasuj_pismo(tekst(0),pismo(137),dostepne_w,0);
    m.p_linia2=dopasuj_pismo(tekst(1),pismo(112),dostepne_w,0);

    m.od_1=ui_skaluj(ODSTEP_LOGO_ZNAK,pb);
    m.od_2=ui_skaluj(ODSTEP_ZNAK_PASEK,pb);
    m.od_3=ui_skaluj(ODSTEP_PASEK_LINIA,pb);
    m.od_4=ui_skaluj(ODSTEP_LINII,pb);
    m.znak_h=ui_skaluj(16,m.p_znak);
    m.linia1_h=ui_skaluj(16,m.p_linia1);
    m.linia2_h=ui_skaluj(16,m.p_linia2);
    m.znak_odstep=int(uint64_t(m.p_znak)*pb*dpi/10000/3);
    m.znak_w=szerokosc_rozstrzelona(ZNAK_MARKI,m.p_znak,m.znak_odstep);
    // Na bardzo waskim ekranie rozstrzelenie jest pierwszym elementem, ktory ustępuje.
    if(m.znak_w>dostepne_w) {
        m.znak_w=szerokosc_rozstrzelona(ZNAK_MARKI,m.p_znak,0);
        m.znak_odstep=0;
    }
    if(m.znak_w>dostepne_w)m.znak_w=dostepne_w; // tekst jest wtedy przycinany
    m.linia1_w=BursztynUI::CzcionkaUI::zmierz_tekst_wygladzony(tekst(0),m.p_linia1);
    m.linia2_w=BursztynUI::CzcionkaUI::zmierz_tekst_wygladzony(tekst(1),m.p_linia2);
    m.in_znak=atrament_linii(ZNAK_MARKI,m.p_znak);
    m.in_linia1=atrament_linii(tekst(0),m.p_linia1);
    m.in_linia2=atrament_linii(tekst(1),m.p_linia2);
    m.gran_znak=granice_atramentu(ZNAK_MARKI,m.p_znak,m.znak_odstep);
    m.gran_linia1=granice_atramentu(tekst(0),m.p_linia1,0);
    m.gran_linia2=granice_atramentu(tekst(1),m.p_linia2,0);

    // Pasek ma szerokosc kolumny napisu (maksymalnie 72% ekranu), dzieki czemu
    // cala grupa czyta sie jako jedna, wysrodkowana os.
    const int pasek_max=int(uint64_t(w)*72/100);
    int pasek=m.znak_w;
    if(pasek>pasek_max)pasek=pasek_max;
    if(pasek<48)pasek=48;
    m.pasek_w=pasek;
    // Parzysta szerokosc: srodek paska wypada dokladnie w srodku ekranu.
    if(m.pasek_w&1)--m.pasek_w;
    if(m.pasek_w<48)m.pasek_w=48;
    m.pasek_h=ui_skaluj(PASEK_WYSOKOSC,pb);
    if(m.pasek_h<3)m.pasek_h=3;
    // Parzysta wysokosc toru: kapsula ma wtedy taki sam promien u gory i u dolu.
    if(m.pasek_h&1)++m.pasek_h;

    const int stale=m.od_1+m.znak_h+m.od_2+m.pasek_h+m.od_3+m.linia1_h+m.od_4+m.linia2_h;
    int rdzen=dostepne_h-stale;
    const int preferowana=ui_skaluj(LOGO_PREFEROWANA,pb);
    if(rdzen>preferowana)rdzen=preferowana;
    if(rdzen<LOGO_MINIMALNA)rdzen=LOGO_MINIMALNA;
    m.logo_h=rdzen;
    // Maska zachowuje wlasne proporcje (294x320), wiec szerokosc wynika z wysokosci.
    m.logo_w=int(int64_t(rdzen)*BursztynLogo::SZEROKOSC/BursztynLogo::WYSOKOSC);
    if(m.logo_w<1)m.logo_w=1;
    if(m.logo_w>dostepne_w&&dostepne_w>0) {
        m.logo_w=dostepne_w;
        const int nowa_h=int(int64_t(dostepne_w)*BursztynLogo::WYSOKOSC/BursztynLogo::SZEROKOSC);
        if(nowa_h>0&&nowa_h<m.logo_h)m.logo_h=nowa_h;
    }
    m.logo_margines=margines_logo(m.logo_h);
    // Margines ukladu musi pokryc poswiate, inaczej blok bylby za niski.
    const int margines_poswiaty=int(int64_t(m.logo_h)*BursztynLogo::POSWIATA_OD_Y/1000);
    if(margines_poswiaty>m.logo_margines)m.logo_margines=margines_poswiaty;
    // Widoczny atrament logo: rdzen plus ewentualna poswiata ponad niego,
    // przeliczony na piksele docelowe. Dzieki temu uklad centruje realny obraz,
    // a nie prostokat rezerwacji.
    m.logo_atrament_gora=0;
    m.logo_atrament_dol=m.logo_h;
    if(BursztynLogo::POSWIATA_ATRAMENT_OD<BursztynLogo::POSWIATA_WYSOKOSC) {
        const int od=int(int64_t(m.logo_h)*BursztynLogo::POSWIATA_OD_Y/1000);
        const int wysokosc_poswiaty=int(int64_t(m.logo_h)*
            (BursztynLogo::POSWIATA_DO_Y-BursztynLogo::POSWIATA_OD_Y)/1000);
        const int gora=od+int(int64_t(wysokosc_poswiaty)*
            BursztynLogo::POSWIATA_ATRAMENT_OD/BursztynLogo::POSWIATA_WYSOKOSC);
        const int dol=od+int(int64_t(wysokosc_poswiaty)*
            BursztynLogo::POSWIATA_ATRAMENT_DO/BursztynLogo::POSWIATA_WYSOKOSC);
        if(gora<m.logo_atrament_gora)m.logo_atrament_gora=gora;
        if(dol>m.logo_atrament_dol)m.logo_atrament_dol=dol;
    }
    m.blok_h=stale+m.logo_h+2*m.logo_margines;
    m.blok_w=m.logo_w+2*m.logo_margines;
    if(m.blok_w<m.znak_w)m.blok_w=m.znak_w;
    if(m.blok_w<m.pasek_w)m.blok_w=m.pasek_w;
    if(m.blok_w<m.linia1_w)m.blok_w=m.linia1_w;
    if(m.blok_w<m.linia2_w)m.blok_w=m.linia2_w;
    return m;
}

// Na starcie widoczny jest tylko logotyp, wiec centruje sie sam; wraz z
// pojawianiem sie kolejnych elementow grupa plynnie przesuwa sie tak, aby
// zawsze jej widoczna czesc byla wysrodkowana. Wagi to krycie elementow
// w skali 0..255, wiec przejscia sa ciagle i nie ma skokow ukladu.
struct Wagi { unsigned znak=0,pasek=0,linia1=0,linia2=0; };
inline Wagi wagi_pelne(){return {255,255,255,255};}

// Wysokosc atramentu linii tekstu - dokladnie tyle pikseli widac.
inline int wysokosc_atramentu(const Atrament& a) {return a.dol>a.gora?a.dol-a.gora:1;}

// Uklad grupy zalezy od tego, co jest aktualnie widoczne: na starcie
// wysrodkowany jest sam logotyp, a gdy pod nim pojawiaja sie napis, pasek i
// komunikaty, cala grupa plynnie osiada w srodku ekranu. Wagi to krycie
// elementow (0..255), wiec ruch jest ciagly i nie ma skokow ukladu.
inline int wysokosc_grupy(const Miary& m,const Wagi& w) {
    int h=m.logo_atrament_dol-m.logo_atrament_gora;
    if(h<1)h=m.logo_h;
    h+=int(int64_t(w.znak)*(m.od_1+wysokosc_atramentu(m.in_znak))/255);
    h+=int(int64_t(w.pasek)*(m.od_2+m.pasek_h)/255);
    h+=int(int64_t(w.linia1)*(m.od_3+wysokosc_atramentu(m.in_linia1))/255);
    h+=int(int64_t(w.linia2)*(m.od_4+wysokosc_atramentu(m.in_linia2))/255);
    return h;
}

struct Geometria {
    Obszar logo;       // obszar z poswiata (diagnostyka i testy)
    Obszar logo_rdzen; // wlasciwa maska logotypu
    Obszar znak;
    Obszar pasek;
    Obszar linia1;
    Obszar linia2;
    Obszar dirty;
    int wypelnienie;   // px od lewej krawedzi toru
    int przesuniecie;
    Miary m;
};

inline Geometria geometria(int w,int h,uint32_t dpi,unsigned postep=0,
                           const Wagi& wagi=wagi_pelne()) {
    Geometria g{};
    g.m=miary(w,h,dpi);
    const Miary& m=g.m;
    // Grupa jest wysrodkowana na podstawie tego, co jest aktualnie widoczne,
    // wiec logotyp nie stoi nad pustka w pierwszej fazie animacji.
    const int widoczna=wysokosc_grupy(m,wagi);
    const int gora=h/2-widoczna/2;
    g.przesuniecie=gora-(h/2-m.blok_h/2);
    // Kazdy element jest wysrodkowany na ekranie niezaleznie, a blok wyznacza
    // tylko wspolna os pionowa. Margines poswiaty nie przesuwa rdzenia: on go
    // otacza symetrycznie, wiec srodek rdzenia to zawsze srodek ekranu.
    // Maski maja nieparzysta szerokosc, wiec srodek rdzenia wypada w polpikselu.
    // Korygujemy pozycje tak, aby atrament logotypu byl rowno wysrodkowany.
    const int atrament_srodek=int(int64_t(m.logo_w)*
        (BursztynLogo::MASKA_ATRAMENT_LEWY+BursztynLogo::MASKA_ATRAMENT_PRAWY)/
        (2*BursztynLogo::SZEROKOSC));
    const int logo_x=(w-m.logo_w)/2+(m.logo_w/2-atrament_srodek);
    // Rdzen ustawiamy tak, aby widoczny atrament (rdzen + poswiata) zaczynal sie
    // dokladnie w gornej krawedzi wyliczonej grupy.
    const int logo_y=gora-m.logo_atrament_gora;
    g.logo_rdzen={logo_x,logo_y,m.logo_w,m.logo_h};
    // Prostokat poswiaty liczony jest od tego samego srodka i ma wieksze pole
    // widzenia (promile w naglowku maski), wiec rozmycie nie wychyla kompozycji.
    const int poswiata_w=int(int64_t(m.logo_w)*
        (BursztynLogo::POSWIATA_DO_X-BursztynLogo::POSWIATA_OD_X)/1000);
    const int poswiata_h=int(int64_t(m.logo_h)*
        (BursztynLogo::POSWIATA_DO_Y-BursztynLogo::POSWIATA_OD_Y)/1000);
    const int poswiata_od_x=int(int64_t(m.logo_w)*BursztynLogo::POSWIATA_OD_X/1000);
    const int poswiata_od_y=int(int64_t(m.logo_h)*BursztynLogo::POSWIATA_OD_Y/1000);
    g.logo={logo_x+poswiata_od_x,logo_y+poswiata_od_y,
            poswiata_w>2?poswiata_w:2,poswiata_h>2?poswiata_h:2};
    // Odstepy liczy sie miedzy atramentem, nie miedzy komorkami pisma, wiec
    // grupa ma rowne przerwy i zadnego ukrytego pustego pasa.
    int y=g.logo_rdzen.y+m.logo_atrament_dol+m.od_1-m.in_znak.gora;    g.znak={x_dla_atramentu(ZNAK_MARKI,w,m.p_znak,m.znak_odstep),y,
            szerokosc_rozstrzelona(ZNAK_MARKI,m.p_znak,m.znak_odstep),m.znak_h};
    y=g.znak.y+m.in_znak.dol+m.od_2;
    g.pasek=wysrodkuj(w,y,m.pasek_w,m.pasek_h);    y=g.pasek.y+m.pasek_h+m.od_3-m.in_linia1.gora;
    g.linia1={x_dla_atramentu(tekst(0),w,m.p_linia1,0),y,
              szerokosc_rozstrzelona(tekst(0),m.p_linia1,0),m.linia1_h};
    y=g.linia1.y+m.in_linia1.dol+m.od_4-m.in_linia2.gora;
    g.linia2={x_dla_atramentu(tekst(1),w,m.p_linia2,0),y,
              szerokosc_rozstrzelona(tekst(1),m.p_linia2,0),m.linia2_h};
    g.dirty=polacz(g.logo,polacz(g.pasek,polacz(g.linia1,g.linia2)));
    const unsigned p=postep>1000?1000:postep;
    g.wypelnienie=int(int64_t(m.pasek_w)*p/1000);
    return g;
}

// Obszar naglowka (logotyp + napis) i komunikatow, niezalezne od fazy animacji,
// dzieki czemu diagnostyka i testy maja stabilne prostokaty.
inline Obszar obszar_znaku(int w,int h,uint32_t dpi) {
    const auto g=geometria(w,h,dpi,0);
    return polacz(g.logo,g.znak);
}
inline Obszar centrum(int w,int h,uint32_t dpi) {
    const auto g=geometria(w,h,dpi,0);
    return polacz(g.linia1,g.linia2);
}

// ---------------------------------------------------------------------------
// Maszyna stanu: jedna probka czasu absolutnego opisuje cala widoczna tresc
// ---------------------------------------------------------------------------
struct Sekwencja {
    Stan stan=Stan::Nieaktywny;
    uint64_t start=0, fade=0, ostatni=0, gotowy_od=0;
    bool gotowy=false;
    Tlo tlo=Tlo::Zastepcze;
    uint64_t krok=0;       // indeks probki czasu intro
    Faza faza=Faza::Logo;
    unsigned alfa_logo=0, alfa_znak=0, alfa_pasek=0, alfa_linia1=0, alfa_linia2=0;
    unsigned postep=0, odsloniecie=0;

    // Wagi ukladu wynikaja wprost z krycia elementow: grupa centruje sie na
    // tym, co widac, wiec nie ma sztucznego stanu "osiadania".
    Wagi wagi() const {return {alfa_znak,alfa_pasek,alfa_linia1,alfa_linia2};}

    bool aktywny() const {return stan==Stan::Widoczny||stan==Stan::Zanikanie;}
    void rozpocznij(bool wlacz,uint64_t teraz) {
        *this={};stan=wlacz?Stan::Widoczny:Stan::Nieaktywny;
        start=ostatni=teraz;
        przelicz(0);
    }
    // Sygnatura opisuje cala widoczna tresc; kompozytor dzieki niej pomija
    // ponowne skladanie, gdy obraz sie nie zmienil. Postep paska jest w niej
    // obecny, wiec plynne wypelnianie nadal wymusza klatki.
    uint64_t sygnatura() const {
        uint64_t s=uint64_t(stan);
        s=s<<8|(odsloniecie&255);
        s=s<<11|(postep&2047);
        s=s<<8|(alfa_logo&255);
        s=s<<8|(alfa_znak&255);
        s=s<<8|(alfa_pasek&255);
        s=s<<8|(alfa_linia1&255);
        s=s<<8|(alfa_linia2&255);
        return s;
    }
    // Intro liczy sie z jednej kwantowanej probki (12.5 Hz), a pasek postepu
    // i zanikanie z czasu ciaglego, aby ich ruch byl plynny.
    void przelicz(uint64_t t) {
        krok=t/KROK_US;
    const uint64_t tq=krok*KROK_US;
    const unsigned wejscie=wygladz(liniowo(tq,0,LOGO_IN_US));
    alfa_logo=unsigned(LOGO_ALFA_START+
        uint64_t(LOGO_ALFA_KONIEC-LOGO_ALFA_START)*wejscie/1000);
    if(wejscie>=1000)alfa_logo=LOGO_ALFA_KONIEC;
        alfa_znak=unsigned(uint64_t(255)*wygladz(liniowo(tq,ZNAK_IN_US,ZNAK_PELNY_US))/1000);
        alfa_pasek=unsigned(uint64_t(255)*wygladz(liniowo(tq,PASEK_IN_US,PASEK_START_US))/1000);
        // Postep paska jest czasem ciaglym, ale kwantyzujemy WARTOSC do poziomow
    // wynikajacych z KROK_US. Inaczej kazda probka kompozytora dawalaby nowa
    // sygnature i pasek wymuszalby ~140 pelnych klatek 1280x720 na sekunde
    // zamiast ~12 - widoczne jako zaciecia i wysokie obciazenie CPU.
    // Kwantyzujemy wartosc, a nie czas, zeby pasek domknal sie dokladnie na
    // PASEK_PELNY_US (kwantyzacja czasu zostawialaby 97% na koncu).
    constexpr unsigned POZIOMOW_PASKA=
        unsigned((PASEK_PELNY_US-PASEK_START_US)/KROK_US);
    const unsigned u_paska=liniowo(t,PASEK_START_US,PASEK_PELNY_US);
    if(u_paska>=1000)postep=1000;
    else postep=((u_paska*POZIOMOW_PASKA+500)/1000)*1000/POZIOMOW_PASKA;
        alfa_linia1=unsigned(uint64_t(255)*wygladz(liniowo(tq,LINIA1_IN_US,LINIA1_IN_US+500000))/1000);
        alfa_linia2=unsigned(uint64_t(255)*wygladz(liniowo(tq,LINIA2_IN_US,LINIA2_PELNA_US))/1000);
        if(tq<ZNAK_IN_US)faza=Faza::Logo;
        else if(tq<PASEK_IN_US)faza=Faza::Znak;
        else if(tq<PASEK_PELNY_US)faza=Faza::Pasek;
        else faza=Faza::Linie;
    }
    void aktualizuj(uint64_t teraz) {
        if(!aktywny())return;
        // Zegar jest monotoniczny; znosimy rowniez regresyjna probke.
        if(teraz<ostatni)teraz=ostatni;
        ostatni=teraz;
        const uint64_t t=teraz-start;
        przelicz(t);
        if(stan==Stan::Widoczny&&gotowy&&t>=MINIMUM_US){
            stan=Stan::Zanikanie;
            const uint64_t minimum=start+MINIMUM_US; // bezpieczne: t >= MINIMUM_US
            fade=gotowy_od>minimum?gotowy_od:minimum;
        }
        if(stan==Stan::Zanikanie){
            faza=Faza::Zanikanie;
            const uint64_t dt=teraz-fade;
            odsloniecie=dt>=FADE_US?255:unsigned(dt*255/FADE_US);
            if(dt>=FADE_US){stan=Stan::Zakonczony;faza=Faza::Zakonczony;odsloniecie=255;}
        }
    }
};

// ---------------------------------------------------------------------------
// Rysowanie
// ---------------------------------------------------------------------------
inline void czysc(uint32_t* dst,int w,int pitch,Obszar clip) {
    const int x0=clip.x>0?clip.x:0, y0=clip.y>0?clip.y:0;
    const int64_t x1=int64_t(clip.x)+clip.w, y1=int64_t(clip.y)+clip.h;
    const int px1=x1>w?w:int(x1), py1=y1<0?0:int(y1);
    for(int y=y0;y<py1;++y)
        for(int x=x0;x<px1;++x)dst[size_t(y)*pitch+x]=TLO;
}

// Maska pokrycia skalowana bilinear 16.16 (bez FPU) w obszarze docelowym.
// Wspolczynnik alfa < 255 dodatkowo wygasza cala maske (wejscie logotypu).
template<class Emit>
inline void probkuj_maske(const uint8_t* maska,int mw,int mh,int dw,int dh,unsigned alfa,Emit& emituj) {
    if(!maska||mw<1||mh<1||dw<1||dh<1||!alfa)return;
    const uint32_t x_step=dw>1?uint32_t(((uint64_t(mw)-1)<<16)/(dw-1)):0;
    const uint32_t y_step=dh>1?uint32_t(((uint64_t(mh)-1)<<16)/(dh-1)):0;
    for(int y=0;y<dh;++y) {
        const uint32_t fy=y_step*uint32_t(y);
        const int sy0=int(fy>>16);
        int sy1=sy0+1;
        if(sy1>=mh)sy1=mh-1;
        const unsigned wy=fy&0xffffU;
        const uint8_t* r0=maska+size_t(sy0)*mw;
        const uint8_t* r1=maska+size_t(sy1)*mw;
        for(int x=0;x<dw;++x) {
            const uint32_t fx=x_step*uint32_t(x);
            const int sx0=int(fx>>16);
            int sx1=sx0+1;
            if(sx1>=mw)sx1=mw-1;
            const unsigned wx=fx&0xffffU;
            const uint32_t gora=uint32_t(r0[sx0])*(65536-wx)+uint32_t(r0[sx1])*wx;
            const uint32_t dol=uint32_t(r1[sx0])*(65536-wx)+uint32_t(r1[sx1])*wx;
            const uint32_t pokrycie=((gora>>16)*(65536-wy)+(dol>>16)*wy)>>16;
            if(!pokrycie)continue;
            emituj(x,y,unsigned((uint64_t(pokrycie)*alfa)/255));
        }
    }
}

// Najmniejszy pierwiastek calkowity; argumenty sa male (<= 8*8+2*10*10).
inline uint32_t isqrt32(uint32_t v) {
    uint32_t r=0;
    while((r+1)*(r+1)<=v)++r;
    return r;
}
// Pokrycie piksela kapsula o zaokraglonych koncach.
// Wszystkie odleglosci liczone sa w polpikselach: szerokosc 2*szer, wysokosc
// 2*wys, promien = wysokosc w polpikselach, srodek piksela (2x+1,2y+1).
inline unsigned pokrycie_kapsuly(int x,int y,int szer,int wys) {
    if(x<0||y<0||x>=szer||y>=wys||szer<1||wys<1)return 0;
    const int r=wys;                 // promien = polowa wysokosci w polpikselach
    const int w2=szer*2;             // pelna szerokosc w polpikselach
    const int cx=2*x+1, cy=2*y+1;
    // Odleglosc pozioma od odcinka [r, w2-r].
    int dx=r-cx;
    const int prawy=cx-(w2-r);
    if(prawy>dx)dx=prawy;
    if(dx<0)dx=0;
    const int dy=cy>r?cy-r:r-cy;
    const uint32_t dist=isqrt32(uint32_t(dx*dx+dy*dy));
    // Wygladzenie na 1 polpikselu krawedzi kapsuly.
    if(dist>=uint32_t(r)+1u)return 0;
    if(dist+1u<=uint32_t(r))return 255;
    return unsigned((uint32_t(r)+1u-dist)*255u/2u);
}

// Napis wycentrowany w poziomie z opcjonalnym rozstrzeleniem miedzy znakami.
inline void napis(uint32_t* dst,int w,int h,int pitch,const char* zdanie,int y,
                  uint32_t kolor,uint32_t skala,Obszar clip,unsigned krycie=255,int odstep=0) {
    using BursztynUI::CzcionkaUI;
    if(!zdanie||!krycie)return;
    auto pisz=[&](const char* jeden,int px) {
        CzcionkaUI::rysuj_tekst_wygladzony(jeden,px,y,skala,[&](int x,int yy,unsigned a) {
            if(x<0||x>=w||yy<0||yy>=h)return;
            if(x<clip.x||int64_t(x)>=int64_t(clip.x)+clip.w)return;
            if(yy<clip.y||int64_t(yy)>=int64_t(clip.y)+clip.h)return;
            const size_t i=size_t(yy)*pitch+x;
            dst[i]=mieszaj(dst[i],kolor,(a*krycie+127)/255);
        });
    };
    if(!odstep) {
        pisz(zdanie,x_dla_atramentu(zdanie,w,skala,0));
        return;
    }
    // Rozstrzelenie rysuje pojedyncze znaki, ale metryki i glify pochodza
    // z tego samego wspolnego renderera Unicode. Pozycja startowa wynika
    // z atramentu, wiec tekst jest wysrodkowany na widocznych pikselach.
    int x=x_dla_atramentu(zdanie,w,skala,odstep);
    for(size_t i=0;zdanie[i];) {
        uint32_t cp=0;i+=CzcionkaUI::dekoduj_utf8(zdanie,i,&cp);
        if(cp!=' ') {
            char jeden[5];
            if(!CzcionkaUI::zakoduj_utf8(cp,jeden))break;
            pisz(jeden,x);
        }
        x+=postep_rozstrzelony(cp,skala,odstep);
    }
}

// Logotyp: najpierw miekka poswiata w obszarze z marginesem, potem ostry
// rdzen dokladnie w prostokacie maski (ten sam srodek, wiec nic nie ucieka).
// Prostokaty poswiaty i rdzenia wylicza wylacznie geometria, a granica rysowania
// (obszar przyciecia klatki) jest respektowana na poziomie piksela.
inline void rysuj_logo(uint32_t* dst,int w,int h,int pitch,
                       const Obszar& rdzen,const Obszar& poswiata,unsigned alfa,
                       const Obszar& granica={0,0,INT32_MAX,INT32_MAX}) {
    if(!alfa)return;
    const int64_t gr_x=int64_t(granica.x)+granica.w, gr_y=int64_t(granica.y)+granica.h;
    auto dozwolony=[&](int x,int y) {
        return x>=granica.x&&int64_t(x)<gr_x&&y>=granica.y&&int64_t(y)<gr_y;
    };
    int gx0,gy0,gx1,gy1;
    if(obszar_w_ekranie(poswiata,w,h,gx0,gy0,gx1,gy1)) {
        auto pisz=[&](int lx,int ly,unsigned a) {
            const int x=poswiata.x+lx, y=poswiata.y+ly;
            if(x<gx0||x>=gx1||y<gy0||y>=gy1||!dozwolony(x,y))return;
            const size_t i=size_t(y)*pitch+x;
            dst[i]=naloz(dst[i],POSWIATA_KOLOR,a);
        };
        probkuj_maske(BursztynLogo::POSWIATA,BursztynLogo::POSWIATA_SZEROKOSC,
                      BursztynLogo::POSWIATA_WYSOKOSC,poswiata.w,poswiata.h,
                      alfa*POSWIATA_UDZIAL/255,pisz);
    }
    int cx0,cy0,cx1,cy1;
    if(!obszar_w_ekranie(rdzen,w,h,cx0,cy0,cx1,cy1))return;
    auto pisz=[&](int lx,int ly,unsigned a) {
        const int x=rdzen.x+lx, y=rdzen.y+ly;
        if(x<cx0||x>=cx1||y<cy0||y>=cy1||!dozwolony(x,y))return;
        const size_t i=size_t(y)*pitch+x;
        dst[i]=naloz(dst[i],AKCENT,a);
    };
    probkuj_maske(BursztynLogo::MASKA,BursztynLogo::SZEROKOSC,BursztynLogo::WYSOKOSC,
                  rdzen.w,rdzen.h,alfa,pisz);
}

// Tor i wypelnienie paska. Wypelnienie jest przycinane do toru, wiec zadna
// wartosc postepu nie moze wyjsc poza geometrie klatki.
inline void rysuj_pasek(uint32_t* dst,int w,int h,int pitch,const Obszar& r,
                        unsigned alfa,int wypelnienie,
                        const Obszar& granica={0,0,INT32_MAX,INT32_MAX}) {
    if(pusty(r)||!alfa)return;
    const int x0=r.x>0?r.x:0, y0=r.y>0?r.y:0;
    const int x1=r.x+r.w<w?r.x+r.w:w;
    const int y1=r.y+r.h<h?r.y+r.h:h;
    if(x1<=x0||y1<=y0)return;
    const int64_t gr_x=int64_t(granica.x)+granica.w, gr_y=int64_t(granica.y)+granica.h;
    const int tor=wypelnienie<0?0:(wypelnienie>r.w?r.w:wypelnienie);
    // Wewnetrzne wypelnienie ma 1 px oddechu od obramowania toru.
    const int wciecie=r.h>=5?1:0;
    const int fw=tor>0?tor:0, fh=r.h-2*wciecie;
    const int fx=r.x, fy=r.y+wciecie;
    for(int y=y0;y<y1;++y) {
        const int ly=y-r.y;
        for(int x=x0;x<x1;++x) {
            const int lx=x-r.x;
            if(x<granica.x||int64_t(x)>=gr_x||y<granica.y||int64_t(y)>=gr_y)continue;
            unsigned pokrycie=pokrycie_kapsuly(lx,ly,r.w,r.h);
            if(!pokrycie)continue;
            const size_t i=size_t(y)*pitch+x;
            const bool krawedz=ly==0||ly==r.h-1||lx==0||lx==r.w-1;
            uint32_t kolor=krawedz?TOR_OBRAMOWANIE:TOR;
            unsigned a=(pokrycie*alfa+127)/255;
            if(fw>0&&fh>0&&y>=fy&&y<fy+fh&&x<fx+fw) {
                // Wypelnienie: symetryczny gradient (jasniejszy srodek, ciemniejsze
                // konce). Jednolity kolor na calym pasku wygladal na przesuniety,
                // gdy jasny koniec wypadal tylko po jednej stronie.
                const int flx=x-fx;
                const int srodek=fw/2;
                const unsigned odleglosc=unsigned(flx<srodek?srodek-flx:flx-srodek);
                const unsigned udzial=srodek>0?unsigned(uint64_t(odleglosc)*255/srodek):0u;
                const unsigned fp=pokrycie_kapsuly(flx,y-fy,fw,fh);
                if(fp) {
                    kolor=mieszaj(AKCENT_JASNY,AKCENT,udzial>255?255:udzial);
                    a=(fp*alfa+127)/255;
                }
            }
            dst[i]=naloz(dst[i],kolor,a);
        }
    }
}

inline void rysuj(uint32_t* dst,int w,int h,int pitch,uint32_t dpi,
                  const Sekwencja& s,bool caly,Obszar clip={0,0,INT32_MAX,INT32_MAX}) {
    if(!dst||w<=0||h<=0||pitch<w)return;
    (void)caly; // Kazda klatka i wycinek sa odtwarzane od tla, nigdy z historii.
    const auto g=geometria(w,h,dpi,s.postep,s.wagi());
    // Zapisy poza przycieciem oraz poza ekranem sa odrzucane.
    const int64_t koniec_y=int64_t(clip.y)+clip.h;
    const int y0=clip.y>0?clip.y:0;
    const int y1=koniec_y<0?0:int(koniec_y<h?koniec_y:h);
    const int64_t koniec_x=int64_t(clip.x)+clip.w;
    const int x0=clip.x>0?clip.x:0;
    const int x1=koniec_x<0?0:int(koniec_x<w?koniec_x:w);
    if(y1<=y0||x1<=x0)return;
    for(int y=y0;y<y1;++y)
        for(int x=x0;x<x1;++x)dst[size_t(y)*pitch+x]=TLO;

    rysuj_logo(dst,w,h,pitch,g.logo_rdzen,g.logo,s.alfa_logo,clip);
    napis(dst,w,h,pitch,ZNAK_MARKI,g.znak.y,TEKST,
          g.m.p_znak,clip,s.alfa_znak,g.m.znak_odstep);
    rysuj_pasek(dst,w,h,pitch,g.pasek,s.alfa_pasek,g.wypelnienie,clip);
    napis(dst,w,h,pitch,tekst(0),g.linia1.y,TEKST,
          g.m.p_linia1,clip,s.alfa_linia1);
    napis(dst,w,h,pitch,tekst(1),g.linia2.y,TEKST_PRZYGASZONY,
          g.m.p_linia2,clip,s.alfa_linia2);
}
}
#include "ekran_powitalny_api.h"
