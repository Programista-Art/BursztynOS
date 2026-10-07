#pragma once
#include <stdint.h>
#include "bursztyn_ui/czcionka.h"

namespace Powitanie {
enum class Tlo { Tapeta, Zastepcze }; // fallback is valid during loading or failure
enum class Stan { Nieaktywny, Widoczny, Zanikanie, Zakonczony, Wyciszanie, Zniszczony };
enum class Faza { Pierwszy, PierwszyDoDrugiego, Drugi, DrugiDoTrzeciego, Trzeci, Zanikanie, Zakonczony };
// 2 s first hold, 400 ms crossfades, 1.8 s later holds: 7 s including exit.
constexpr uint64_t PRZEJSCIE_US=400000, PIERWSZE_US=2000000, DRUGIE_US=4200000;
constexpr uint64_t MINIMUM_US=6400000, FADE_US=600000;
constexpr uint32_t TLO=0x101720;
inline uint32_t mieszaj(uint32_t a,uint32_t b,unsigned w) {
    uint32_t out=0;
    for(unsigned s=0;s<24;s+=8)
        out|=((((a>>s)&255)*(255-w)+((b>>s)&255)*w+127)/255)<<s;
    return out;
}
struct Sekwencja {
    Stan stan=Stan::Nieaktywny;
    uint64_t start=0, fade=0, ostatni=0, gotowy_od=0;
    bool gotowy=false;
    Tlo tlo=Tlo::Zastepcze;
    unsigned komunikat=0, nastepny_komunikat=0, przenikanie=0, odsloniecie=0;
    Faza faza=Faza::Pierwszy;
    unsigned alfa_starego() const { return 255-przenikanie; }
    unsigned alfa_nowego() const { return nastepny_komunikat==komunikat?0:przenikanie; }
    unsigned alfa_komunikatu(unsigned i) const {
        return (i==komunikat?alfa_starego():0)+(i==nastepny_komunikat?alfa_nowego():0);
    }
    void rozpocznij(bool wlacz,uint64_t teraz) {
        *this={};stan=wlacz?Stan::Widoczny:Stan::Nieaktywny;
        start=ostatni=teraz;
    }
    bool aktywny() const {return stan==Stan::Widoczny||stan==Stan::Zanikanie;}
    void aktualizuj(uint64_t teraz) {
        if(!aktywny())return;
        // The clock is monotonic; tolerate a regressing injected sample too.
        if(teraz<ostatni)teraz=ostatni;
        ostatni=teraz;
        const uint64_t t=teraz-start;
        komunikat=0;przenikanie=0;nastepny_komunikat=0;
        // Evaluate just the current absolute-time sample, never replay missed frames.
        if(t<PIERWSZE_US)faza=Faza::Pierwszy;
        else if(t<PIERWSZE_US+PRZEJSCIE_US) {
            faza=Faza::PierwszyDoDrugiego;nastepny_komunikat=1;
            przenikanie=unsigned((t-PIERWSZE_US)*255/PRZEJSCIE_US);
        } else if(t<DRUGIE_US) {
            faza=Faza::Drugi;komunikat=nastepny_komunikat=1;
        } else if(t<DRUGIE_US+PRZEJSCIE_US) {
            faza=Faza::DrugiDoTrzeciego;komunikat=1;nastepny_komunikat=2;
            przenikanie=unsigned((t-DRUGIE_US)*255/PRZEJSCIE_US);
        } else {
            faza=Faza::Trzeci;komunikat=nastepny_komunikat=2;
        }
        if(stan==Stan::Widoczny&&gotowy&&t>=MINIMUM_US){
            stan=Stan::Zanikanie;
            const uint64_t minimum=start+MINIMUM_US; // addition safe: t >= MINIMUM_US
            fade=gotowy_od>minimum?gotowy_od:minimum;
        }
        if(stan==Stan::Zanikanie){
            faza=Faza::Zanikanie;
            const uint64_t dt=teraz-fade;
            odsloniecie=dt>=FADE_US?255:unsigned(dt*255/FADE_US);
            if(dt>=FADE_US){stan=Stan::Zakonczony;faza=Faza::Zakonczony;}
        }
    }
};
inline const char* tekst(unsigned i) {
    static const char* const t[]={"Witamy w Bursztyn OS","Polski system operacyjny","Niezależny system"};
    return t[i<3?i:2];
}
struct Obszar {int x,y,w,h;};
constexpr int WYSOKOSC_TYTULU=56, WYSOKOSC_KOMUNIKATU=36;
inline uint32_t skala_tekstu(int wysokosc,uint32_t dpi) {
    if(!ui_skala_poprawna(dpi)) dpi=100;
    return uint32_t((uint64_t(wysokosc)*dpi+8)/16);
}
// The 16-row bitmap font has a fixed line cell. Row 13 is the common
// alphabetic baseline; glyph accents/descenders never alter the layout.
constexpr int ASCENT_KOMUNIKATU=13;
struct Geometria { Obszar tytul, komunikat, dirty; int baza_komunikatu; };
inline Geometria geometria(int w,int h,uint32_t dpi,unsigned msg) {
    using BursztynUI::CzcionkaUI;
    if(!ui_skala_poprawna(dpi)) dpi=100;
    const auto ts=skala_tekstu(WYSOKOSC_TYTULU,dpi);
    const auto ms=skala_tekstu(WYSOKOSC_KOMUNIKATU,dpi);
    const int th=ui_skaluj(16,ts),mh=ui_skaluj(16,ms);
    const int gap=ui_skaluj(24,dpi),top=h/2-(th+gap+mh)/2;
    const int tw=CzcionkaUI::zmierz_tekst_wygladzony("Bursztyn OS",ts);
    const int mw=CzcionkaUI::zmierz_tekst_wygladzony(tekst(msg),ms);
    return {{(w-tw)/2,top,tw,th},{(w-mw)/2,top+th+gap,mw,mh},
            {0,top,w,th+gap+mh},top+th+gap+ui_skaluj(ASCENT_KOMUNIKATU,ms)};
}
inline Obszar centrum(int w,int h,uint32_t dpi) {
    auto r=geometria(w,h,dpi,0).komunikat;
    for(unsigned i=1;i<3;++i){const auto m=geometria(w,h,dpi,i).komunikat;if(m.w>r.w)r=m;}
    return r;
}
inline void napis(uint32_t* dst,int w,int h,int pitch,const char* text,int y,
                  uint32_t kolor,uint32_t scale,Obszar clip,unsigned krycie=255) {
    using BursztynUI::CzcionkaUI;
    const int px=(w-CzcionkaUI::zmierz_tekst_wygladzony(text,scale))/2;
    CzcionkaUI::rysuj_tekst_wygladzony(text,px,y,scale,[&](int x,int yy,unsigned a) {
        if(x>=0&&x<w&&yy>=0&&yy<h&&x>=clip.x&&int64_t(x)<int64_t(clip.x)+clip.w&&
           yy>=clip.y&&int64_t(yy)<int64_t(clip.y)+clip.h) {
            const size_t i=size_t(yy)*pitch+x;dst[i]=mieszaj(dst[i],kolor,(a*krycie+127)/255);
        }
    });
}
inline void rysuj(uint32_t* dst,int w,int h,int pitch,uint32_t skala,
                  const Sekwencja& s,bool caly,Obszar clip={0,0,INT32_MAX,INT32_MAX}) {
    if(!dst||w<=0||h<=0||pitch<w) return;
    const auto g=geometria(w,h,skala,s.komunikat);
    (void)caly; // Every frame/clip is rebuilt from background, never old pixels.
    const int64_t bottom64=int64_t(clip.y)+clip.h,right64=int64_t(clip.x)+clip.w;
    const int top=clip.y>0?clip.y:0;
    const int bottom=bottom64<0?0:int(bottom64<h?bottom64:h);
    int left=clip.x>0?clip.x:0;
    int right=right64<0?0:int(right64<w?right64:w);
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)dst[size_t(y)*pitch+x]=TLO;
    napis(dst,w,h,pitch,"Bursztyn OS",g.tytul.y,0xE9BE76,
          skala_tekstu(WYSOKOSC_TYTULU,skala),clip);
    // Clear and compose both texts into this surface before the caller presents it.
    // Complementary coverage never creates an empty message frame.
    const auto ms=skala_tekstu(WYSOKOSC_KOMUNIKATU,skala);
    const int message_y=g.baza_komunikatu-ui_skaluj(ASCENT_KOMUNIKATU,ms);
    napis(dst,w,h,pitch,tekst(s.komunikat),message_y,0xE1E6ED,ms,clip,s.alfa_starego());
    if(s.alfa_nowego())napis(dst,w,h,pitch,tekst(s.nastepny_komunikat),message_y,
          0xE1E6ED,ms,clip,s.alfa_nowego());
}
}
#include "ekran_powitalny_api.h"
