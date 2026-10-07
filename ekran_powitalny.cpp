#include "ekran_powitalny.h"
#include "diagnostyka_startu.h"
#include "bufory_grafiki.h"
#include "grafika.h"
#include "skladacz_obrazu.h"
#include "sterowniki/czas/hpet.h"
namespace {
Powitanie::Sekwencja sekwencja;
uint64_t generacja=0;
uint8_t* powierzchnia=nullptr;
size_t bajty=0;
int szer=0,wys=0,stride=0;
uint32_t procent=0;
uint64_t nastepny=0;
bool pulpit_gotowy=false;
bool gotowosc_utracona=false;
bool tlo_tapeta=false;
int pulpit_pid=0;
bool zakonczenie_oczekuje=false;
bool pelne_rysowanie=false;
unsigned poprzedni_tekst=99,poprzednie_przenikanie=999;
#if BURSZTYNDIAGSPLASHTIMING
uint64_t prezentacje=0;
void diag_czas(const char* event,uint64_t now) {
    char msg[160];unsigned n=0;
    auto txt=[&](const char* s){while(*s&&n<sizeof(msg)-1)msg[n++]=*s++;};
    auto num=[&](uint64_t v){char digits[24];unsigned k=0;do{digits[k++]=char('0'+v%10);v/=10;}while(v);while(k&&n<sizeof(msg)-1)msg[n++]=digits[--k];};
    txt("[SPLASH TIMING] ");txt(event);txt(" now_us=");num(now);
    txt(" elapsed_us=");num(now>=sekwencja.start?now-sekwencja.start:0);
    txt(" compositor_presents=");num(prezentacje);msg[n]=0;klog_serial(msg);
}
#else
void diag_czas(const char*,uint64_t) {}
#endif
}
void powitanie_diag_prezentacja() {
#if BURSZTYNDIAGSPLASHTIMING
    if(powitanie_aktywne())++prezentacje;
#endif
}
namespace {
void zwolnij_powierzchnie() {
    if(powierzchnia) {bufor_grafiki_zwolnij(powierzchnia);diag_start_reboot("[STARTUP] splash surface released");}
    powierzchnia=nullptr;bajty=0;szer=wys=stride=0;procent=0;
}
void zakoncz_powitanie() {
    zwolnij_powierzchnie();
    zakonczenie_oczekuje=false;pulpit_gotowy=false;gotowosc_utracona=false;tlo_tapeta=false;pulpit_pid=0;
    sekwencja={};sekwencja.stan=Powitanie::Stan::Zakonczony;
    nastepny=0;pelne_rysowanie=false;poprzedni_tekst=99;poprzednie_przenikanie=999;
}
}
void powitanie_wycisz() {
    diag_start_reboot("[STARTUP] splash quiesce begin");
    sekwencja.stan=Powitanie::Stan::Wyciszanie;
    zakonczenie_oczekuje=false;pulpit_gotowy=false;gotowosc_utracona=false;tlo_tapeta=false;pulpit_pid=0;
    zwolnij_powierzchnie();
    sekwencja={};sekwencja.stan=Powitanie::Stan::Zniszczony;
    nastepny=0;pelne_rysowanie=false;poprzedni_tekst=99;poprzednie_przenikanie=999;
}
void powitanie_reset_ekranu() {
    const bool aktywne=powitanie_aktywne();
    zwolnij_powierzchnie();pulpit_gotowy=false;gotowosc_utracona=false;tlo_tapeta=false;pulpit_pid=0;
    zakonczenie_oczekuje=false;pelne_rysowanie=true;nastepny=0;
    poprzedni_tekst=99;poprzednie_przenikanie=999;
    if(aktywne) {sekwencja.stan=Powitanie::Stan::Widoczny;sekwencja.gotowy=false;sekwencja.fade=0;sekwencja.odsloniecie=0;}
}
bool powitanie_aktywne(){return sekwencja.aktywny()||zakonczenie_oczekuje;}
void powitanie_inicjalizuj(bool wlacz){
    powitanie_wycisz();
    ++generacja;pelne_rysowanie=true;
#if BURSZTYNDIAGSTARTUPREBOOT
    char msg[96]="[STARTUP] splash init generation=";unsigned n=sizeof("[STARTUP] splash init generation=")-1;
    char digits[24];unsigned count=0;uint64_t value=generacja;
    do{digits[count++]=char('0'+value%10);value/=10;}while(value);
    while(count)msg[n++]=digits[--count];
    msg[n]=0;klog_serial(msg);
#endif
    sekwencja.rozpocznij(wlacz&&hpet_dostepny(),czas_monotoniczny_us());
#if BURSZTYNDIAGSPLASHTIMING
    prezentacje=0;
#endif
    if(sekwencja.aktywny())diag_czas("start",sekwencja.start);
    if(wlacz&&!hpet_dostepny())klog_serial("[WELCOME] No monotonic clock; clean startup without presentation delay.");
    if(sekwencja.aktywny()){diag_start_reboot("[STARTUP] splash active");PrzeniesNaEkran();}
}
void powitanie_pulpit_gotowy(bool tapeta,int pid){
    if(!sekwencja.aktywny())return;
    if(!pulpit_gotowy){
        sekwencja.gotowy_od=czas_monotoniczny_us();
        diag_start_reboot("[STARTUP] desktop ready");diag_czas("desktop_ready",sekwencja.gotowy_od);
    }
    tlo_tapeta=tapeta;pulpit_pid=pid;
    __atomic_store_n(&pulpit_gotowy,true,__ATOMIC_RELEASE);
}
void powitanie_tick(){
    if(!powitanie_aktywne())return;
    const uint64_t now=czas_monotoniczny_us();
    if(now<nastepny)return;
    nastepny=now>UINT64_MAX-33333?UINT64_MAX:now+33333;
    const auto stan=sekwencja.stan;
    const unsigned poprzednie_odsloniecie=sekwencja.odsloniecie;
    const unsigned tekst=sekwencja.komunikat,przenikanie=sekwencja.przenikanie;
    const unsigned kolejny=sekwencja.nastepny_komunikat;
    sekwencja.gotowy=__atomic_load_n(&pulpit_gotowy,__ATOMIC_ACQUIRE);
    if(sekwencja.gotowy&&pulpit_pid>0){
        auto* w=pobierz_warstwe(pulpit_pid);
        sekwencja.gotowy=w&&skladacz_obrazu_czy_widoczna(pulpit_pid)&&
            w->x==0&&w->y==0&&w->szerokosc>=grafika_pobierz_szerokosc()&&
            w->wysokosc>=grafika_pobierz_wysokosc();
        // Preserve the WM's actual ready timestamp across a scheduling stall.
        // Only a previously observed invalid root needs a fresh fade origin.
        if(!sekwencja.gotowy)gotowosc_utracona=true;
        else if(gotowosc_utracona){sekwencja.gotowy_od=now;gotowosc_utracona=false;}
    }
    if(!sekwencja.gotowy&&(stan==Powitanie::Stan::Zanikanie||zakonczenie_oczekuje)){
        sekwencja.stan=Powitanie::Stan::Widoczny;
        sekwencja.odsloniecie=0;zakonczenie_oczekuje=false;pelne_rysowanie=true;
        skladacz_obrazu_oznacz_dirty();
    }
    if(sekwencja.gotowy)sekwencja.tlo=tlo_tapeta?Powitanie::Tlo::Tapeta:Powitanie::Tlo::Zastepcze;
    sekwencja.aktualizuj(now);
    // Log sampled transitions only. A stall may jump straight to a later hold/end.
    if(tekst!=sekwencja.komunikat)diag_czas(sekwencja.komunikat==1?"message_1_hold":"message_2_hold",now);
    else if(kolejny!=sekwencja.nastepny_komunikat&&sekwencja.nastepny_komunikat!=tekst)diag_czas(tekst==0?"message_0_to_1":"message_1_to_2",now);
    if(stan==Powitanie::Stan::Widoczny&&sekwencja.stan!=stan)diag_czas("fade_start",now);
    if(stan!=Powitanie::Stan::Zakonczony&&sekwencja.stan==Powitanie::Stan::Zakonczony)diag_czas("fade_end",now);
    if(stan!=Powitanie::Stan::Zanikanie&&sekwencja.stan==Powitanie::Stan::Zanikanie)diag_start_reboot("[STARTUP] fade started");
    if(stan==Powitanie::Stan::Zanikanie||sekwencja.stan==Powitanie::Stan::Zanikanie||!sekwencja.aktywny())
        skladacz_obrazu_oznacz_dirty();
    else if(tekst!=sekwencja.komunikat||przenikanie!=sekwencja.przenikanie){
        const auto r=Powitanie::centrum(grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),procent?procent:100);
        skladacz_obrazu_oznacz_dirty_rect(r.x,r.y,r.w,r.h);
    }
    if(sekwencja.stan==Powitanie::Stan::Zakonczony&&!zakonczenie_oczekuje){
        // Pacing may defer the full composition. Cursor-only presentation must
        // keep the previous welcome frame until a complete scene is committed.
        zakonczenie_oczekuje=true;
        sekwencja.odsloniecie=poprzednie_odsloniecie<255?poprzednie_odsloniecie:254;
    }
}
uint8_t* powitanie_zrodlo(uint8_t* scena,int w,int h,int pitch,int x,int y,int rw,int rh,uint32_t skala){
    if(!powitanie_aktywne()||w<=0||h<=0||int64_t(pitch)<int64_t(w)*4)return scena;
    if(zakonczenie_oczekuje&&x==0&&y==0&&rw==w&&rh==h){
        diag_czas("final_removal",czas_monotoniczny_us());
        zakoncz_powitanie();diag_start_reboot("[STARTUP] splash finished");
        return scena;
    }
    if(size_t(h)>SIZE_MAX/size_t(pitch))return scena;
    const size_t potrzebne=size_t(pitch)*h;
    bool rebuild=pelne_rysowanie||w!=szer||h!=wys||pitch!=stride||skala!=procent;
    if(potrzebne>bajty){
        auto* nowa=static_cast<uint8_t*>(bufor_grafiki_alokuj(potrzebne));
        if(!nowa){
            // Optional visual resource failure does not stop system boot.
            zakoncz_powitanie();
            klog_serial("[WELCOME] Surface unavailable; clean startup fallback.");
            skladacz_obrazu_oznacz_dirty();return scena;
        }
        if(powierzchnia)bufor_grafiki_zwolnij(powierzchnia);
        powierzchnia=nowa;bajty=potrzebne;rebuild=true;
        diag_start_reboot("[STARTUP] splash surface allocated");
    }
    szer=w;wys=h;stride=pitch;procent=skala;
    auto* dst=reinterpret_cast<uint32_t*>(powierzchnia);
    if(rebuild||poprzedni_tekst!=sekwencja.komunikat||poprzednie_przenikanie!=sekwencja.przenikanie){
        Powitanie::rysuj(dst,w,h,pitch/4,skala,sekwencja,rebuild);
        poprzedni_tekst=sekwencja.komunikat;poprzednie_przenikanie=sekwencja.przenikanie;pelne_rysowanie=false;
    }
    if(sekwencja.stan==Powitanie::Stan::Zanikanie||zakonczenie_oczekuje){
        // Recreate only the presented region; never blend an already blended pixel.
        // Full-screen work is limited to the final 600 ms at existing frame pacing.
        int x0=x<0?0:x,y0=y<0?0:y;
        const int64_t right=int64_t(x)+rw,bottom=int64_t(y)+rh;
        int x1=right>w?w:(right<0?0:int(right)),y1=bottom>h?h:(bottom<0?0:int(bottom));
        if(x0<x1&&y0<y1)Powitanie::rysuj(dst,w,h,pitch/4,skala,sekwencja,true,{x0,y0,x1-x0,y1-y0});
        auto* src=reinterpret_cast<const uint32_t*>(scena);
        for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx){size_t i=size_t(yy)*(pitch/4)+xx;dst[i]=Powitanie::mieszaj(dst[i],src[i],sekwencja.odsloniecie);}
    }
    return powierzchnia;
}
