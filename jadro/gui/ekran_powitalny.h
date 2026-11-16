#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "ui_skala.h"
#include "logo_maska_dane.h"
#include "napisy_powitania_dane.h"

// Ekran powitalny: czarne tlo, bursztynowy logotyp, tytul "Bursztyn OS",
// cienki bursztynowy separator i dwa podtytuly, odslaniane kolejno.
//
// Cala animacja jest funkcja czasu monotonicznego i geometrii ekranu. Renderer
// nie alokuje pamieci, nie uzywa FPU, nie dekoduje PNG i nie parsuje czcionek:
// maska logotypu (logo_maska_dane.h) i maski napisow zrasteryzowane z konturow
// czcionki OpenType (napisy_powitania_dane.h) sa wkompilowane w .rodata.
// Kazda klatka jest kompletna (tlo + wszystkie elementy), wiec powierzchnia
// wielokrotnego uzytku nie przenosi historii.
namespace Powitanie {
enum class Tlo { Tapeta, Zastepcze }; // fallback jest poprawny przy ladowaniu lub awarii
enum class Stan { Nieaktywny, Widoczny, Zanikanie, Zakonczony, Wyciszanie, Zniszczony };
enum class Faza { Logo, Tytul, Separator, Linia1, Linia2, Komplet, Zanikanie, Zakonczony };

// ---------------------------------------------------------------------------
// Paleta
// ---------------------------------------------------------------------------
constexpr uint32_t TLO=0x000000;              // czarne tlo marki
constexpr uint32_t AKCENT=0xFF8A00;           // bursztyn logotypu i separatora
constexpr uint32_t POSWIATA_KOLOR=0x8A4A00;
constexpr uint32_t TEKST=0xF2F4F7;            // tytul
constexpr uint32_t TEKST_PODTYTUL=0xC9CED6;   // "Polski system operacyjny"
constexpr uint32_t TEKST_PRZYGASZONY=0x8C949F;// "Niezalezny system"
constexpr unsigned SEPARATOR_KRYCIE=220;      // separator nieco cichszy od logotypu

// ---------------------------------------------------------------------------
// Etapy i czasy (sekwencjonowanie zalezne od prezentacji)
// ---------------------------------------------------------------------------
// Etapy: logo -> tytul -> separator -> "Polski system operacyjny" ->
// "Niezalezny system". Kolejny etap moze sie zaczac dopiero, gdy poprzedni
// zostal FAKTYCZNIE zaprezentowany na ekranie (potwierdz() po udanej publikacji
// klatki) co najmniej ODSTEP_ETAPOW_US temu i byl widoczny w pelni przez
// ODSTEP_ETAPOW_US-wejscie. Na jedna probke przypada najwyzej jeden nowy etap.
//
// Dawniej krycie kazdego elementu bylo czysta funkcja uplywu czasu. Kazda
// przerwa w odswiezaniu dluzsza niz ~1 s (synchroniczny start jadra, przestoj
// hosta) przekraczala progi kilku etapow naraz i oba podtytuly pojawialy sie
// w tej samej klatce. Teraz zalegly etap czeka na najblizsza prezentacje,
// a nastepny dopiero na potwierdzenie poprzedniego. Wejscie (fade-in) etapu
// liczy sie od jego pierwszej prezentacji, wiec nie "przepada" w przerwie.
enum Etap : unsigned { ETAP_LOGO=0, ETAP_TYTUL, ETAP_SEPARATOR, ETAP_LINIA1, ETAP_LINIA2, LICZBA_ETAPOW };
constexpr uint64_t ODSTEP_ETAPOW_US=1000000;    // od pierwszej prezentacji poprzedniego etapu
constexpr uint64_t WEJSCIE_TEKSTU_US=500000;     // fade-in tytulu i podtytulow
constexpr uint64_t WEJSCIE_SEPARATORA_US=400000; // fade-in + rozwiniecie separatora
constexpr uint64_t wejscie_etapu(unsigned e) {
    return e==ETAP_LOGO?0:(e==ETAP_SEPARATOR?WEJSCIE_SEPARATORA_US:WEJSCIE_TEKSTU_US);
}
// Kwantyzacja probki czasu (20 Hz). W holdzie obraz jest staly, wiec nie ma
// klatek; w trakcie wejsc 20 Hz (10 poziomow na 0,5 s) daje plynne przejscie.
constexpr uint64_t KROK_US=50000;
// Nominalna os czasu przy regularnych prezentacjach (co KROK_US): etap k
// zaczyna sie po k*ODSTEP_ETAPOW_US i jest pelny po wejsciu minus jeden krok
// (pierwsza prezentowana klatka pokazuje juz pierwszy krok wejscia).
constexpr uint64_t nominalny_start(unsigned e) {return uint64_t(e)*ODSTEP_ETAPOW_US;}
constexpr uint64_t nominalnie_pelny(unsigned e) {
    return e==ETAP_LOGO?0:nominalny_start(e)+wejscie_etapu(e)-KROK_US;
}
constexpr uint64_t SEKWENCJA_US=nominalnie_pelny(ETAP_LINIA2);
// Najkrotszy czas widocznosci ekranu (od pierwszej opublikowanej klatki) przed
// zanikaniem do gotowego pulpitu - tylko ochrona przed mignieciem. Gotowy pulpit
// NIE czeka na reszte sekwencji: niedokonczone etapy sa pomijane.
constexpr uint64_t MINIMUM_US=1500000, FADE_US=600000;

constexpr unsigned LOGO_ALFA=255;          // logo od pierwszej klatki (nigdy pusty ekran)
constexpr unsigned POSWIATA_UDZIAL=88;     // udzial poswiaty w pokryciu rdzenia

static_assert(WEJSCIE_TEKSTU_US<ODSTEP_ETAPOW_US&&WEJSCIE_SEPARATORA_US<ODSTEP_ETAPOW_US,
              "kazdy etap jest w pelni widoczny, zanim zacznie sie nastepny");
static_assert(WEJSCIE_TEKSTU_US%KROK_US==0&&WEJSCIE_SEPARATORA_US%KROK_US==0&&ODSTEP_ETAPOW_US%KROK_US==0,
              "granice wejsc wypadaja na probkach");
static_assert(MINIMUM_US<SEKWENCJA_US,"gotowy pulpit nie czeka na pelna sekwencje");

// ---------------------------------------------------------------------------
// Pomocnicze operacje
// ---------------------------------------------------------------------------
inline uint32_t mieszaj(uint32_t a,uint32_t b,unsigned w) {
    if(w>255)w=255;
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
constexpr const char* ZNAK_MARKI="Bursztyn OS";

// Nazwa etapu animacji do diagnostyki. Logujemy etapy, nie klatki, wiec
// diagnostyka pozostaje ograniczona nawet przy dlugim starcie.
inline const char* nazwa_fazy(Faza f) {
    switch(f) {
        case Faza::Logo: return "logo";
        case Faza::Tytul: return "title";
        case Faza::Separator: return "divider";
        case Faza::Linia1: return "subtitle";
        case Faza::Linia2: return "secondary_subtitle";
        case Faza::Komplet: return "complete_hold";
        case Faza::Zanikanie: return "fade_start";
        default: return "fade_end";
    }
}

// ---------------------------------------------------------------------------
// Uklad: wszystkie wymiary wyliczane z wysokosci ekranu i skali UI
// ---------------------------------------------------------------------------
// Wartosci bazowe dla 720p przy 100% (skala ukladu 1000 promili).
constexpr int LOGO_WYSOKOSC=96;        // rdzen logotypu (bylo 190: dominowal ekran)
constexpr int ODSTEP_LOGO_TYTUL=30;    // dol logotypu -> gora wersalikow tytulu
constexpr int ODSTEP_TYTUL_SEPARATOR=22; // linia bazowa tytulu -> separator
constexpr int ODSTEP_SEPARATOR_LINIA=22; // separator -> gora wersalikow podtytulu
constexpr int ODSTEP_LINII=14;         // linia bazowa podtytulu -> gora drugiego
constexpr int SEPARATOR_GRUBOSC=2;
constexpr int SEPARATOR_MIN=40;
constexpr unsigned SEPARATOR_UDZIAL=34; // % szerokosci tytulu
// Optyczny srodek: kompozycja lekko powyzej geometrycznego srodka ekranu.
constexpr unsigned UNIESIENIE_PROMILE=30; // promile wysokosci ekranu
constexpr int SKALA_MIN=500;           // promile; ponizej tekst bylby nieczytelny
constexpr int SKALA_MAX=NapisyPowitania::SKALA_MASEK; // maski sa tylko zmniejszane

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
// Prostokat wysrodkowany w poziomie.
inline Obszar wysrodkuj(int w,int y,int szer,int wys) {
    return {(w-szer)/2,y,szer,wys};
}
// Przeskalowanie wymiaru bazowego (720p) do skali ukladu w promilach.
constexpr int skaluj(int baza,int skala) {
    return int((int64_t(baza)*skala+500)/1000);
}
// Przeskalowanie wymiaru maski napisu (zapisanej przy SKALA_MASEK).
constexpr int skaluj_maske(int v,int skala) {
    return int((int64_t(v)*skala+NapisyPowitania::SKALA_MASEK/2)/NapisyPowitania::SKALA_MASEK);
}

// Napis ustawiony typograficznie: prostokat maski i linia bazowa.
struct Linia {
    Obszar r;          // docelowy prostokat atramentu (maska jest przycieta do atramentu)
    int baza;          // y linii bazowej
    int wersalik;      // wysokosc wersalikow nad linia bazowa
};

struct Miary {
    int skala;                         // promile wzgledem 720p/100%
    int logo_w,logo_h;
    int tytul_w,tytul_h,tytul_baza,tytul_wersalik;
    int linia1_w,linia1_h,linia1_baza,linia1_wersalik;
    int linia2_w,linia2_h,linia2_baza,linia2_wersalik;
    int separator_w,separator_h;
    int od_1,od_2,od_3,od_4;
    int blok_h;                        // od gory rdzenia logo do dolu drugiego podtytulu
};

inline Miary miary_dla_skali(int skala) {
    using namespace NapisyPowitania;
    Miary m{};
    m.skala=skala;
    m.logo_h=skaluj(LOGO_WYSOKOSC,skala);
    if(m.logo_h<16)m.logo_h=16;
    // Proporcje maski logotypu (294x320) sa zachowane: szerokosc wynika z wysokosci.
    m.logo_w=int((int64_t(m.logo_h)*BursztynLogo::SZEROKOSC+BursztynLogo::WYSOKOSC/2)/BursztynLogo::WYSOKOSC);
    if(m.logo_w<1)m.logo_w=1;
    auto napis=[&](const Maska& mk,int& w,int& h,int& baza,int& wersalik) {
        w=skaluj_maske(mk.w,skala); if(w<1)w=1;
        h=skaluj_maske(mk.h,skala); if(h<1)h=1;
        baza=skaluj_maske(mk.linia_bazowa,skala);
        wersalik=skaluj_maske(mk.wersalik,skala);
    };
    napis(TYTUL,m.tytul_w,m.tytul_h,m.tytul_baza,m.tytul_wersalik);
    napis(LINIA1,m.linia1_w,m.linia1_h,m.linia1_baza,m.linia1_wersalik);
    napis(LINIA2,m.linia2_w,m.linia2_h,m.linia2_baza,m.linia2_wersalik);
    m.separator_w=int(int64_t(m.tytul_w)*SEPARATOR_UDZIAL/100);
    const int sep_min=skaluj(SEPARATOR_MIN,skala);
    if(m.separator_w<sep_min)m.separator_w=sep_min;
    if((m.separator_w&1)!=(m.tytul_w&1))++m.separator_w; // ten sam srodek co tytul
    m.separator_h=skaluj(SEPARATOR_GRUBOSC,skala);
    if(m.separator_h<1)m.separator_h=1;
    m.od_1=skaluj(ODSTEP_LOGO_TYTUL,skala);
    m.od_2=skaluj(ODSTEP_TYTUL_SEPARATOR,skala);
    m.od_3=skaluj(ODSTEP_SEPARATOR_LINIA,skala);
    m.od_4=skaluj(ODSTEP_LINII,skala);
    // Pionowo wszystko liczy sie od linii bazowych i wysokosci wersalikow,
    // a nie od komorek: tak sklada sie tekst, a nie rozmieszcza prostokaty.
    const int tytul_baza_y=m.logo_h+m.od_1+m.tytul_wersalik;
    const int sep_y=tytul_baza_y+m.od_2;
    const int l1_baza_y=sep_y+m.separator_h+m.od_3+m.linia1_wersalik;
    const int l2_baza_y=l1_baza_y+m.od_4+m.linia2_wersalik;
    m.blok_h=l2_baza_y-m.linia2_baza+m.linia2_h;
    return m;
}

inline Miary miary(int w,int h,uint32_t dpi) {
    if(!ui_skala_poprawna(dpi))dpi=100;
    if(w<1)w=1;
    if(h<1)h=1;
    // Skala z wysokosci ekranu (720p -> 1000, 1080p -> 1500) i skali UI.
    int64_t s=(int64_t(h)*1000+360)/720;
    s=s*dpi/100;
    if(s>SKALA_MAX)s=SKALA_MAX;
    if(s<SKALA_MIN)s=SKALA_MIN;
    int skala=int(s);
    // Waski ekran: tytul i podtytul mieszcza sie w 86% szerokosci.
    const int64_t dostepne_w=int64_t(w)*86/100;
    const int64_t najszerszy=NapisyPowitania::TYTUL.w>NapisyPowitania::LINIA1.w?
        NapisyPowitania::TYTUL.w:NapisyPowitania::LINIA1.w;
    const int64_t z_szerokosci=dostepne_w*NapisyPowitania::SKALA_MASEK/najszerszy;
    if(skala>z_szerokosci)skala=int(z_szerokosci<1?1:z_szerokosci);
    Miary m=miary_dla_skali(skala);
    // Niski ekran: blok miesci sie w 88% wysokosci.
    const int dostepne_h=int(int64_t(h)*88/100);
    if(m.blok_h>dostepne_h&&m.blok_h>0) {
        const int64_t nowa=int64_t(skala)*dostepne_h/m.blok_h;
        m=miary_dla_skali(int(nowa<1?1:nowa));
    }
    return m;
}

struct Geometria {
    Obszar logo;       // obszar z poswiata (diagnostyka i testy)
    Obszar logo_rdzen; // wlasciwa maska logotypu
    Linia tytul, linia1, linia2;
    Obszar separator;  // pelna szerokosc separatora
    Obszar dirty;
    Miary m;
};

// Uklad jest staly dla wszystkich etapow: elementy pojawiaja sie na swoich
// docelowych miejscach, wiec nic nie przeskakuje ani nie "plywa".
inline Geometria geometria(int w,int h,uint32_t dpi) {
    Geometria g{};
    if(w<1)w=1;
    if(h<1)h=1;
    g.m=miary(w,h,dpi);
    const Miary& m=g.m;
    int gora=(h-m.blok_h)/2-int(int64_t(h)*UNIESIENIE_PROMILE/1000);
    if(gora<0)gora=0;
    // Atrament maski logotypu wypelnia ja w calosci (MASKA_ATRAMENT_* = pelne granice),
    // ale korygujemy srodek na wypadek asymetrii zrodla.
    const int atrament_srodek=int(int64_t(m.logo_w)*
        (BursztynLogo::MASKA_ATRAMENT_LEWY+BursztynLogo::MASKA_ATRAMENT_PRAWY)/
        (2*BursztynLogo::SZEROKOSC));
    const int logo_x=(w-m.logo_w)/2+(m.logo_w/2-atrament_srodek);
    g.logo_rdzen={logo_x,gora,m.logo_w,m.logo_h};
    const int poswiata_w=int(int64_t(m.logo_w)*
        (BursztynLogo::POSWIATA_DO_X-BursztynLogo::POSWIATA_OD_X)/1000);
    const int poswiata_h=int(int64_t(m.logo_h)*
        (BursztynLogo::POSWIATA_DO_Y-BursztynLogo::POSWIATA_OD_Y)/1000);
    const int poswiata_od_x=int(int64_t(m.logo_w)*BursztynLogo::POSWIATA_OD_X/1000);
    const int poswiata_od_y=int(int64_t(m.logo_h)*BursztynLogo::POSWIATA_OD_Y/1000);
    g.logo={logo_x+poswiata_od_x,gora+poswiata_od_y,
            poswiata_w>2?poswiata_w:2,poswiata_h>2?poswiata_h:2};

    const int tytul_baza=gora+m.logo_h+m.od_1+m.tytul_wersalik;
    g.tytul={wysrodkuj(w,tytul_baza-m.tytul_baza,m.tytul_w,m.tytul_h),tytul_baza,m.tytul_wersalik};
    const int sep_y=tytul_baza+m.od_2;
    g.separator=wysrodkuj(w,sep_y,m.separator_w,m.separator_h);
    const int l1_baza=sep_y+m.separator_h+m.od_3+m.linia1_wersalik;
    g.linia1={wysrodkuj(w,l1_baza-m.linia1_baza,m.linia1_w,m.linia1_h),l1_baza,m.linia1_wersalik};
    const int l2_baza=l1_baza+m.od_4+m.linia2_wersalik;
    g.linia2={wysrodkuj(w,l2_baza-m.linia2_baza,m.linia2_w,m.linia2_h),l2_baza,m.linia2_wersalik};
    g.dirty=polacz(g.logo,polacz(g.tytul.r,polacz(g.separator,polacz(g.linia1.r,g.linia2.r))));
    // Dirty zawsze w granicach ekranu.
    int x0,y0,x1,y1;
    if(obszar_w_ekranie(g.dirty,w,h,x0,y0,x1,y1))g.dirty={x0,y0,x1-x0,y1-y0};
    else g.dirty={0,0,0,0};
    return g;
}

// Obszar naglowka (logotyp + tytul) i podtytulow, niezalezne od fazy animacji,
// dzieki czemu diagnostyka i testy maja stabilne prostokaty.
inline Obszar obszar_znaku(int w,int h,uint32_t dpi) {
    const auto g=geometria(w,h,dpi);
    return polacz(g.logo,g.tytul.r);
}
inline Obszar centrum(int w,int h,uint32_t dpi) {
    const auto g=geometria(w,h,dpi);
    return polacz(g.linia1.r,g.linia2.r);
}

// ---------------------------------------------------------------------------
// Maszyna stanu: jedna probka czasu absolutnego opisuje cala widoczna tresc
// ---------------------------------------------------------------------------
struct Sekwencja {
    Stan stan=Stan::Nieaktywny;
    uint64_t start=0, fade=0, ostatni=0, gotowy_od=0;
    bool gotowy=false;
    Tlo tlo=Tlo::Zastepcze;
    uint64_t krok=0;       // indeks probki czasu od pierwszej klatki
    Faza faza=Faza::Logo;
    // Jawny stan etapow: ostatni rozpoczety etap oraz chwile pierwszej
    // potwierdzonej prezentacji (pokazany_od) i pierwszej prezentacji w pelni
    // (pelny_od) kazdego etapu. Obliczenie klatki nie zmienia tych pol.
    unsigned etap=ETAP_LOGO;
    bool pokazany[LICZBA_ETAPOW]{}, pelny[LICZBA_ETAPOW]{};
    uint64_t pokazany_od[LICZBA_ETAPOW]{}, pelny_od[LICZBA_ETAPOW]{};
    unsigned widoczne=0;   // maska etapow z kryciem > 0 w biezacym stanie
    unsigned pelne=0;      // maska etapow z pelnym kryciem w biezacym stanie
    unsigned alfa_logo=0, alfa_tytul=0, alfa_separator=0, alfa_linia1=0, alfa_linia2=0;
    unsigned rozwiniecie=0; // szerokosc separatora 0..1000
    unsigned odsloniecie=0;

    bool aktywny() const {return stan==Stan::Widoczny||stan==Stan::Zanikanie;}
    void rozpocznij(bool wlacz,uint64_t teraz) {
        *this={};stan=wlacz?Stan::Widoczny:Stan::Nieaktywny;
        start=ostatni=teraz;
        oblicz(teraz);
    }
    // Sygnatura opisuje cala widoczna tresc; kompozytor dzieki niej pomija
    // ponowne skladanie, gdy obraz sie nie zmienil.
    uint64_t sygnatura() const {
        uint64_t s=uint64_t(stan);
        s=s<<8|(odsloniecie&255);
        s=s<<10|(rozwiniecie&1023);
        s=s<<8|(alfa_logo&255);
        s=s<<8|(alfa_tytul&255);
        s=s<<8|(alfa_separator&255);
        s=s<<8|(alfa_linia1&255);
        s=s<<8|(alfa_linia2&255);
        return s;
    }
    // Etap widoczny w stanie, ale jeszcze nie potwierdzony na ekranie.
    bool oczekuje_potwierdzenia() const {
        for(unsigned e=0;e<LICZBA_ETAPOW;++e)
            if(((widoczne>>e)&1u)&&!pokazany[e])return true;
            else if(((pelne>>e)&1u)&&!pelny[e])return true;
        return false;
    }
    // Krycie etapu e (0..255) w chwili `chwila`. Etap niepotwierdzony stoi na
    // pierwszym kroku wejscia: przerwa w prezentacji nie zjada fade-in.
    unsigned krycie(unsigned e,uint64_t chwila) const {
        if(e>etap)return 0;
        const uint64_t wejscie=wejscie_etapu(e);
        if(!wejscie)return 255;
        uint64_t postep=KROK_US;
        if(pokazany[e]&&chwila>pokazany_od[e]) {
            const uint64_t dt=chwila-pokazany_od[e];
            postep=dt>=wejscie?wejscie:(dt/KROK_US)*KROK_US+KROK_US;
        }
        if(postep>wejscie)postep=wejscie;
        return unsigned(uint64_t(255)*wygladz(liniowo(postep,0,wejscie))/1000);
    }
    // Wylacznie obliczenie stanu klatki (bez przesuwania etapow).
    void oblicz(uint64_t chwila) {
        alfa_logo=LOGO_ALFA;
        alfa_tytul=krycie(ETAP_TYTUL,chwila);
        alfa_separator=krycie(ETAP_SEPARATOR,chwila);
        alfa_linia1=krycie(ETAP_LINIA1,chwila);
        alfa_linia2=krycie(ETAP_LINIA2,chwila);
        {
            const uint64_t w=wejscie_etapu(ETAP_SEPARATOR);
            uint64_t postep=0;
            if(etap>=ETAP_SEPARATOR) {
                postep=KROK_US;
                if(pokazany[ETAP_SEPARATOR]&&chwila>pokazany_od[ETAP_SEPARATOR]) {
                    const uint64_t dt=chwila-pokazany_od[ETAP_SEPARATOR];
                    postep=dt>=w?w:(dt/KROK_US)*KROK_US+KROK_US;
                }
                if(postep>w)postep=w;
            }
            rozwiniecie=wygladz(liniowo(postep,0,w));
        }
        const unsigned a[LICZBA_ETAPOW]={alfa_logo,alfa_tytul,alfa_separator,alfa_linia1,alfa_linia2};
        widoczne=pelne=0;
        for(unsigned e=0;e<LICZBA_ETAPOW;++e){
            if(a[e])widoczne|=1u<<e;
            if(a[e]>=255)pelne|=1u<<e;
        }
        static constexpr Faza fazy[LICZBA_ETAPOW]={Faza::Logo,Faza::Tytul,Faza::Separator,Faza::Linia1,Faza::Linia2};
        faza=(etap==ETAP_LINIA2&&alfa_linia2>=255)?Faza::Komplet:fazy[etap];
    }
    // Czy etap nastepny po biezacym moze sie zaczac w chwili `chwila`?
    bool nastepny_dozwolony(uint64_t chwila) const {
        if(etap+1>=LICZBA_ETAPOW||!pokazany[etap]||!pelny[etap])return false;
        if(chwila<pokazany_od[etap]||chwila-pokazany_od[etap]<ODSTEP_ETAPOW_US)return false;
        const uint64_t w_pelni=ODSTEP_ETAPOW_US-wejscie_etapu(etap);
        return chwila>=pelny_od[etap]&&chwila-pelny_od[etap]>=w_pelni;
    }
    // Potwierdzenie udanej prezentacji klatki wyrenderowanej ze stanu, ktorego
    // maski widocznych/pelnych etapow podano. Tylko to przesuwa "pokazane".
    void potwierdz(uint64_t teraz,unsigned maska_widocznych,unsigned maska_pelnych) {
        if(!aktywny())return;
        if(teraz<ostatni)teraz=ostatni;
        for(unsigned e=0;e<LICZBA_ETAPOW&&e<=etap;++e){
            if(((maska_widocznych>>e)&1u)&&!pokazany[e]){pokazany[e]=true;pokazany_od[e]=teraz;}
            if(((maska_pelnych>>e)&1u)&&!pelny[e]){pelny[e]=true;pelny_od[e]=teraz;}
        }
    }
    void potwierdz(uint64_t teraz){potwierdz(teraz,widoczne,pelne);}
    void aktualizuj(uint64_t teraz) {
        if(!aktywny())return;
        // Zegar jest monotoniczny; znosimy rowniez regresyjna probke.
        if(teraz<ostatni)teraz=ostatni;
        ostatni=teraz;
        const uint64_t t=teraz-start;
        krok=t/KROK_US;
        if(stan==Stan::Widoczny&&gotowy&&t>=MINIMUM_US){
            stan=Stan::Zanikanie;
            const uint64_t minimum=start+MINIMUM_US; // bezpieczne: t >= MINIMUM_US
            fade=gotowy_od>minimum?gotowy_od:minimum;
            if(fade>teraz)fade=teraz;
        }
        // Najwyzej jeden nowy etap na probke i tylko po potwierdzeniu poprzedniego.
        // W trakcie zanikania nic nowego sie nie zaczyna, a krycie jest zamrozone
        // w chwili jego poczatku.
        if(stan==Stan::Widoczny&&nastepny_dozwolony(teraz))++etap;
        oblicz(stan==Stan::Zanikanie?fade:teraz);
        if(stan==Stan::Zanikanie){
            faza=Faza::Zanikanie;
            const uint64_t dt=teraz-fade;
            odsloniecie=dt>=FADE_US?255:unsigned(dt*255/FADE_US);
            if(dt>=FADE_US){stan=Stan::Zakonczony;faza=Faza::Zakonczony;odsloniecie=255;}
        }
    }
};

// Idealny przebieg (prezentacja co KROK_US od chwili 0) do testow i podgladu:
// stan w chwili t, jakby kazda klatka trafila na ekran. Ograniczony liczba
// krokow do konca sekwencji, wiec dowolnie duze t jest tanie.
inline Sekwencja sekwencja_idealna(uint64_t t) {
    Sekwencja s;s.rozpocznij(true,0);s.potwierdz(0);
    for(uint64_t u=KROK_US;u<=t;u+=KROK_US){
        s.aktualizuj(u);s.potwierdz(u);
        if(s.faza==Faza::Komplet&&s.pelny[ETAP_LINIA2])break;
    }
    s.aktualizuj(t);
    return s;
}

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

// Maska pokrycia skalowana bilinear 16.16 (bez FPU). Uzywana tylko do
// powiekszania (rozmyta poswiata ma niska rozdzielczosc).
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

// Zmniejszanie maski filtrem obszarowym (box) z dokladnymi wagami brzegow
// w 1/256 piksela zrodla, bez FPU. Kazdy piksel docelowy to srednia pokrycia
// calego prostokata zrodla, ktory obejmuje - krawedzie sa gladkie przy kazdym
// wspolczynniku, w przeciwienstwie do bilinear, ktory przy >2x gubi probki.
// Gdy cel jest wiekszy od zrodla, uzywa interpolacji dwuliniowej.
template<class Emit>
inline void probkuj_obszarowo(const uint8_t* maska,int mw,int mh,int dw,int dh,unsigned alfa,Emit& emituj) {
    if(!maska||mw<1||mh<1||dw<1||dh<1||!alfa)return;
    if(dw>mw||dh>mh){probkuj_maske(maska,mw,mh,dw,dh,alfa,emituj);return;}
    for(int y=0;y<dh;++y) {
        const uint64_t ya=uint64_t(y)*mh*256/dh, yb=uint64_t(y+1)*mh*256/dh;
        const int sy0=int(ya>>8), sy1=int((yb-1)>>8);
        for(int x=0;x<dw;++x) {
            const uint64_t xa=uint64_t(x)*mw*256/dw, xb=uint64_t(x+1)*mw*256/dw;
            const int sx0=int(xa>>8), sx1=int((xb-1)>>8);
            uint64_t suma=0;
            for(int sy=sy0;sy<=sy1&&sy<mh;++sy) {
                const uint64_t r0=uint64_t(sy)<<8, r1=r0+256;
                const uint64_t wy=(yb<r1?yb:r1)-(ya>r0?ya:r0);
                const uint8_t* wiersz=maska+size_t(sy)*mw;
                uint64_t s=0;
                for(int sx=sx0;sx<=sx1&&sx<mw;++sx) {
                    const uint8_t v=wiersz[sx];
                    if(!v)continue;
                    const uint64_t c0=uint64_t(sx)<<8, c1=c0+256;
                    s+=uint64_t(v)*((xb<c1?xb:c1)-(xa>c0?xa:c0));
                }
                suma+=s*wy;
            }
            if(!suma)continue;
            const uint64_t pole=(xb-xa)*(yb-ya);
            const unsigned pokrycie=unsigned((suma+pole/2)/pole);
            if(!pokrycie)continue;
            emituj(x,y,unsigned((uint64_t(pokrycie>255?255:pokrycie)*alfa+127)/255));
        }
    }
}

// Granica rysowania (przyciecie klatki) respektowana na poziomie piksela.
struct Przyciecie {
    int x0,y0,x1,y1;
    Przyciecie(int w,int h,const Obszar& clip) {
        x0=clip.x>0?clip.x:0; y0=clip.y>0?clip.y:0;
        const int64_t cx=int64_t(clip.x)+clip.w, cy=int64_t(clip.y)+clip.h;
        x1=cx<0?0:int(cx<w?cx:w); y1=cy<0?0:int(cy<h?cy:h);
    }
    bool zawiera(int x,int y) const {return x>=x0&&x<x1&&y>=y0&&y<y1;}
};

// Maska napisu (TTF) w docelowym prostokacie, kolor z kryciem 0..255.
inline void rysuj_napis(uint32_t* dst,int pitch,const Przyciecie& p,
                        const NapisyPowitania::Maska& mk,const Obszar& r,
                        uint32_t kolor,unsigned krycie) {
    if(!krycie||pusty(r))return;
    auto pisz=[&](int lx,int ly,unsigned a) {
        const int64_t x=int64_t(r.x)+lx, y=int64_t(r.y)+ly;
        if(x<p.x0||x>=p.x1||y<p.y0||y>=p.y1)return;
        const size_t i=size_t(y)*pitch+size_t(x);
        dst[i]=naloz(dst[i],kolor,a);
    };
    probkuj_obszarowo(mk.piksele,mk.w,mk.h,r.w,r.h,krycie,pisz);
}

// Logotyp: najpierw miekka poswiata w obszarze z marginesem, potem ostry
// rdzen dokladnie w prostokacie maski (ten sam srodek, wiec nic nie ucieka).
inline void rysuj_logo(uint32_t* dst,int pitch,const Przyciecie& p,
                       const Obszar& rdzen,const Obszar& poswiata,unsigned alfa) {
    if(!alfa)return;
    auto poswiec=[&](int lx,int ly,unsigned a) {
        const int64_t x=int64_t(poswiata.x)+lx, y=int64_t(poswiata.y)+ly;
        if(x<p.x0||x>=p.x1||y<p.y0||y>=p.y1)return;
        const size_t i=size_t(y)*pitch+size_t(x);
        dst[i]=naloz(dst[i],POSWIATA_KOLOR,a);
    };
    probkuj_maske(BursztynLogo::POSWIATA,BursztynLogo::POSWIATA_SZEROKOSC,
                  BursztynLogo::POSWIATA_WYSOKOSC,poswiata.w,poswiata.h,
                  alfa*POSWIATA_UDZIAL/255,poswiec);
    auto pisz=[&](int lx,int ly,unsigned a) {
        const int64_t x=int64_t(rdzen.x)+lx, y=int64_t(rdzen.y)+ly;
        if(x<p.x0||x>=p.x1||y<p.y0||y>=p.y1)return;
        const size_t i=size_t(y)*pitch+size_t(x);
        dst[i]=naloz(dst[i],AKCENT,a);
    };
    probkuj_obszarowo(BursztynLogo::MASKA,BursztynLogo::SZEROKOSC,BursztynLogo::WYSOKOSC,
                      rdzen.w,rdzen.h,alfa,pisz);
}

// Separator: cienka bursztynowa linia, rozwijana od srodka, z lagodnie
// wygaszonymi koncami (bez twardych krawedzi przy 1-3 px grubosci).
inline void rysuj_separator(uint32_t* dst,int pitch,const Przyciecie& p,
                            const Obszar& pelny,unsigned alfa,unsigned rozwiniecie) {
    if(!alfa||pusty(pelny))return;
    if(rozwiniecie>1000)rozwiniecie=1000;
    // Od 40% do 100% szerokosci, symetrycznie wzgledem srodka pelnego separatora.
    int sw=int(int64_t(pelny.w)*(400+rozwiniecie*600/1000)/1000);
    if((sw&1)!=(pelny.w&1))++sw;
    if(sw>pelny.w)sw=pelny.w;
    if(sw<2)return;
    const int x0=pelny.x+(pelny.w-sw)/2;
    const int zanik=sw/4>1?sw/4:1;
    for(int y=pelny.y;y<pelny.y+pelny.h;++y) {
        if(y<p.y0||y>=p.y1)continue;
        for(int lx=0;lx<sw;++lx) {
            const int x=x0+lx;
            if(x<p.x0||x>=p.x1)continue;
            const int od_konca=lx<sw-1-lx?lx:sw-1-lx;
            unsigned a=255;
            if(od_konca<zanik)a=unsigned(uint64_t(255)*wygladz(unsigned((uint64_t(od_konca)*1000+500)/zanik))/1000);
            a=unsigned((uint64_t(a)*SEPARATOR_KRYCIE*alfa+255*255/2)/(255*255));
            if(!a)continue;
            const size_t i=size_t(y)*pitch+size_t(x);
            dst[i]=naloz(dst[i],AKCENT,a);
        }
    }
}

inline void rysuj(uint32_t* dst,int w,int h,int pitch,uint32_t dpi,
                  const Sekwencja& s,bool caly,Obszar clip={0,0,INT32_MAX,INT32_MAX}) {
    if(!dst||w<=0||h<=0||pitch<w)return;
    (void)caly; // Kazda klatka i wycinek sa odtwarzane od tla, nigdy z historii.
    const auto g=geometria(w,h,dpi);
    const Przyciecie p(w,h,clip);
    if(p.y1<=p.y0||p.x1<=p.x0)return;
    for(int y=p.y0;y<p.y1;++y)
        for(int x=p.x0;x<p.x1;++x)dst[size_t(y)*pitch+x]=TLO;
    rysuj_logo(dst,pitch,p,g.logo_rdzen,g.logo,s.alfa_logo);
    rysuj_napis(dst,pitch,p,NapisyPowitania::TYTUL,g.tytul.r,TEKST,s.alfa_tytul);
    rysuj_separator(dst,pitch,p,g.separator,s.alfa_separator,s.rozwiniecie);
    rysuj_napis(dst,pitch,p,NapisyPowitania::LINIA1,g.linia1.r,TEKST_PODTYTUL,s.alfa_linia1);
    rysuj_napis(dst,pitch,p,NapisyPowitania::LINIA2,g.linia2.r,TEKST_PRZYGASZONY,s.alfa_linia2);
}
}
#include "ekran_powitalny_api.h"
