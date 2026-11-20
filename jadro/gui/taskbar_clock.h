#pragma once
#include "bursztyn_ui/bursztyn_ui.h"
namespace TaskbarClock {
using namespace BursztynUI;
class Surface {
    uint32_t* pixels_{};size_t capacity_{};
    char published_[32]{};
    bool dirty_=true;
    bool active_=false;     // najechanie albo otwarty kalendarz paska: tlo przycisku (zegar jest klikalny)
    SkalaUI scale_{1};
public:
    Prostokat bounds{};
    ~Surface(){delete[] pixels_;}
    Surface()=default;Surface(const Surface&)=delete;Surface& operator=(const Surface&)=delete;
    void configure(int width,int height,int bar,uint32_t dpi){
        scale_=SkalaUI::procent(dpi);
        int digit=0;
        for(char c='0';c<='9';++c){char text[]={c,0};int w=CzcionkaUI::zmierz_tekst(text,scale_);if(w>digit)digit=w;}
        int w=6*digit+2*CzcionkaUI::zmierz_tekst(":",scale_)+24*scale_;
        if(w<150*scale_)w=150*scale_;
        if(w>width)w=width;
        bounds={width-w,height-bar,w,bar};dirty_=true;
    }
    bool needs_repaint()const{return dirty_;}
    bool active()const{return active_;}
    // Zmiana stanu = jedno przerysowanie zegara (nie calego paska); ten sam stan nic nie robi.
    bool set_active(bool a){if(a==active_)return false;active_=a;dirty_=true;return true;}
    bool contains(int x,int y)const{return bounds.zawiera(x,y);}
    template<class Publish> bool update(const char* text,bool force,Publish publish){
        if(!text || bounds.czy_pusty())return false;
        bool same=true;size_t length=0;
        for(;length<sizeof(published_)-1 && text[length];++length)if(text[length]!=published_[length])same=false;
        if(text[length] || published_[length])same=false;
        if(!force && same && !dirty_)return false;
        size_t n=size_t(bounds.szerokosc)*bounds.wysokosc;
        if(n>4U*1024*1024 || bounds.szerokosc>2048 || bounds.wysokosc>2048)return false;
        if(n>capacity_){auto* next=new uint32_t[n];if(!next)return false;delete[] pixels_;pixels_=next;capacity_=n;}
        Prostokat local{0,0,bounds.szerokosc,bounds.wysokosc};
        KontekstRysowania ctx(local);ctx.ustaw_cel(pixels_,local);
        ctx.rysuj_prostokat(local,0xff000000U|motyw_kolor(MOTYW_KOLOR_PODNIESIONY));
        if(active_)ctx.rysuj_zaokraglony_prostokat({3*scale_,4*scale_,local.szerokosc-6*scale_,local.wysokosc-8*scale_},
            0xff000000U|motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY),4*scale_);
        ctx.rysuj_linie_pozioma(0,0,local.szerokosc,0xff000000U|motyw_kolor(MOTYW_KOLOR_RAMKA));
        ctx.rysuj_linie_pionowa(0,0,local.wysokosc,0xff000000U|motyw_kolor(MOTYW_KOLOR_RAMKA));
        ctx.dodaj_przyciecie({1,1,local.szerokosc-1,local.wysokosc-1});
        ctx.rysuj_tekst_wysrodkowany(local,text,motyw_kolor(MOTYW_KOLOR_AKCENT),scale_);
        ctx.zdejmij_przyciecie();
        if(!publish(bounds,pixels_)){dirty_=true;return false;}
        for(size_t i=0;i<length;++i){published_[i]=text[i];}
        published_[length]=0;dirty_=false;return true;
    }
};
}
