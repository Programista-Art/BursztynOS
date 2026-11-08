#include "boot_phase_timing.h"
#include "ekran_powitalny.h"
#include "diagnostyka_startu.h"
#include "diagnostyka_klatek_startu.h"
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
bool pierwsza_klatka_gotowa=false, pierwsza_klatka_opublikowana=false;
bool animacja_rozpoczeta=false;
uint64_t poprzedni_krok=UINT64_MAX;
// Sygnatura ostatnio wyrenderowanej tresci. Brud wynika z tego, co widac,
// a nie z uplywu czasu: w holdzie ekran jest identyczny przez sekundy i nie
// wolno wtedy skladac pelnej klatki ani transferowac jej do GPU.
uint64_t poprzednia_sygnatura=UINT64_MAX;
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
#if BURSZTYNDIAGSPLASHFIRSTFRAME
    static uint64_t logged_generation=UINT64_MAX, logged_frame=UINT64_MAX;
    const auto& id=SplashFirstDiag::id();
    if(powitanie_aktywne()&&pierwsza_klatka_gotowa&&
       (id.generation!=logged_generation||id.frame!=logged_frame)) {
        logged_generation=id.generation;logged_frame=id.frame;
        SplashFirstDiag::event("first_compositor_commit",czas_monotoniczny_us());
    }
#endif
#if BURSZTYNDIAGSPLASHTIMING
    if(powitanie_aktywne())++prezentacje;
#endif
}
namespace {
void zwolnij_powierzchnie() {
    if(powierzchnia) {bufor_grafiki_zwolnij(powierzchnia);diag_start_reboot("[STARTUP] splash surface released");}
    pierwsza_klatka_gotowa=false;pierwsza_klatka_opublikowana=false;
    KlatkiStartuDiag::info().ready=false;
    powierzchnia=nullptr;bajty=0;szer=wys=stride=0;procent=0;
}
void zakoncz_powitanie() {
    animacja_rozpoczeta=false;
    KlatkiStartuDiag::info().active=false;
    zwolnij_powierzchnie();
    zakonczenie_oczekuje=false;pulpit_gotowy=false;gotowosc_utracona=false;tlo_tapeta=false;pulpit_pid=0;
    sekwencja={};sekwencja.stan=Powitanie::Stan::Zakonczony;
    nastepny=0;pelne_rysowanie=false;poprzedni_krok=UINT64_MAX;
}
}
void powitanie_wycisz() {
    animacja_rozpoczeta=false;
    KlatkiStartuDiag::info().active=false;
    diag_start_reboot("[STARTUP] splash quiesce begin");
    sekwencja.stan=Powitanie::Stan::Wyciszanie;
    zakonczenie_oczekuje=false;pulpit_gotowy=false;gotowosc_utracona=false;tlo_tapeta=false;pulpit_pid=0;
    zwolnij_powierzchnie();
    sekwencja={};sekwencja.stan=Powitanie::Stan::Zniszczony;
    nastepny=0;pelne_rysowanie=false;poprzedni_krok=UINT64_MAX;
}
void powitanie_reset_ekranu() {
    PrezentacjaStartu::uniewaznij();
    const bool aktywne=powitanie_aktywne();
    zwolnij_powierzchnie();pulpit_gotowy=false;gotowosc_utracona=false;tlo_tapeta=false;pulpit_pid=0;
    zakonczenie_oczekuje=false;pelne_rysowanie=true;nastepny=0;
    poprzedni_krok=UINT64_MAX;
    if(aktywne) {sekwencja.stan=Powitanie::Stan::Widoczny;sekwencja.gotowy=false;sekwencja.fade=0;sekwencja.odsloniecie=0;}
}
bool powitanie_pierwsza_klatka_gotowa(){return pierwsza_klatka_gotowa;}
bool powitanie_pierwsza_publikacja(){return powitanie_aktywne()&&!pierwsza_klatka_opublikowana;}
void powitanie_publikacja() {
    if(powitanie_aktywne()&&pierwsza_klatka_gotowa&&!pierwsza_klatka_opublikowana){
        if(!animacja_rozpoczeta) {
            // Allocation and backend probing can outlast the first hold. Its
            // absolute-time origin is the completed first visible welcome,
            // never creation of an off-screen surface. Later mode resets keep it.
            sekwencja.start=sekwencja.ostatni=czas_monotoniczny_us();
            animacja_rozpoczeta=true;nastepny=0;
            BootTiming::splash_started();
            diag_czas("start",sekwencja.start);
        }
        SplashFirstDiag::event("first_visible_commit",czas_monotoniczny_us());
        pierwsza_klatka_opublikowana=true;
    }
}
bool powitanie_aktywne(){return sekwencja.aktywny()||zakonczenie_oczekuje;}
bool powitanie_pulpit_potwierdzony(){return __atomic_load_n(&pulpit_gotowy,__ATOMIC_ACQUIRE);}
void powitanie_inicjalizuj(bool wlacz){
    BootTiming::splash_expected=wlacz;
    powitanie_wycisz();
    PrezentacjaStartu::uniewaznij();
    PrezentacjaStartu::wybierz();
    ++generacja;pelne_rysowanie=true;
    SplashFirstDiag::id()={generacja,0,false,false};
    SplashFirstDiag::event("splash_created",czas_monotoniczny_us());
#if BURSZTYNDIAGSTARTUPREBOOT
    char msg[96]="[STARTUP] splash init generation=";unsigned n=sizeof("[STARTUP] splash init generation=")-1;
    char digits[24];unsigned count=0;uint64_t value=generacja;
    do{digits[count++]=char('0'+value%10);value/=10;}while(value);
    while(count)msg[n++]=digits[--count];
    msg[n]=0;klog_serial(msg);
#endif
    sekwencja.rozpocznij(wlacz&&hpet_dostepny(),czas_monotoniczny_us());
    KlatkiStartuDiag::info().active=sekwencja.aktywny();
#if BURSZTYNDIAGSPLASHTIMING
    prezentacje=0;
#endif
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
static void powitanie_aktualizuj_stan(bool pacing){
    if(!powitanie_aktywne()||!animacja_rozpoczeta)return;
    const uint64_t now=czas_monotoniczny_us();
    // Watchdog: after desktop handshake a stuck fade/hold must not leave the
    // pointer without a visible cursor forever (full-scene commit required).
    if(__atomic_load_n(&pulpit_gotowy,__ATOMIC_ACQUIRE) && sekwencja.gotowy_od &&
       now>sekwencja.gotowy_od && now-sekwencja.gotowy_od>10000000ULL) {
        zakoncz_powitanie();
        skladacz_obrazu_oznacz_dirty();
        return;
    }
    if(pacing&&now<nastepny)return;
    if(pacing)nastepny=now>UINT64_MAX-33333?UINT64_MAX:now+33333;
    const auto stan=sekwencja.stan;
    const unsigned poprzednie_odsloniecie=sekwencja.odsloniecie;
    const uint64_t krok=sekwencja.krok;
    Powitanie::Faza faza=sekwencja.faza;
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
    // Diagnostyka emituje ETAPY, nie klatki: probka 12.5 Hz nie moze generowac
    // jednego wpisu na krok, bo seria startowa mialaby dziesiatki linii.
    if(krok!=sekwencja.krok&&faza!=sekwencja.faza)diag_czas(Powitanie::nazwa_fazy(sekwencja.faza),now);
    faza=sekwencja.faza;
    if(stan==Powitanie::Stan::Widoczny&&sekwencja.stan!=stan)diag_czas("fade_start",now);
    if(stan!=Powitanie::Stan::Zakonczony&&sekwencja.stan==Powitanie::Stan::Zakonczony)diag_czas("fade_end",now);
    if(stan!=Powitanie::Stan::Zanikanie&&sekwencja.stan==Powitanie::Stan::Zanikanie)diag_start_reboot("[STARTUP] fade started");
    if(stan==Powitanie::Stan::Zanikanie||sekwencja.stan==Powitanie::Stan::Zanikanie||!sekwencja.aktywny())
        skladacz_obrazu_oznacz_dirty();
    else if(sekwencja.sygnatura()!=poprzednia_sygnatura){
        // Brud tylko wtedy, gdy zmienil sie WIDOCZNY obraz. Wczesniej warunkiem
        // byla zmiana probki czasu, wiec faza hold (ekran staly przez 2,2 s)
        // generowala ~12 pelnych klatek 1280x720 na sekunde wraz z transferem
        // do GPU - to bylo widoczne jako zaciecie i wysokie obciazenie CPU.
        // Animacja pozostaje jedna kompletna transakcja: przy zmianie geometrii
        // lub DPI cala klatka jest przebudowywana od tla.
        //
        // Znacznik ustawiamy JUZ TUTAJ, a nie po renderze: powitanie_zrodlo()
        // wolajace to samo aktualizuj_stan(false) oznaczyloby ten sam stan
        // drugi raz i kazda zmiana kosztowalaby dwie pelne klatki.
        poprzednia_sygnatura=sekwencja.sygnatura();
        skladacz_obrazu_oznacz_dirty();
    }
    if(sekwencja.stan==Powitanie::Stan::Zakonczony&&!zakonczenie_oczekuje){
        // Pacing may defer the full composition. Cursor-only presentation must
        // keep the previous welcome frame until a complete scene is committed.
        zakonczenie_oczekuje=true;
        sekwencja.odsloniecie=poprzednie_odsloniecie<255?poprzednie_odsloniecie:254;
    }
}
void powitanie_tick(){powitanie_aktualizuj_stan(true);}
uint8_t* powitanie_zrodlo(uint8_t* scena,int w,int h,int pitch,int x,int y,int rw,int rh,uint32_t skala){
    if(!powitanie_aktywne()||w<=0||h<=0||int64_t(pitch)<int64_t(w)*4)return scena;
    // Pacing limits scheduled work, never the state of an actual presentation.
    powitanie_aktualizuj_stan(false);
    if(zakonczenie_oczekuje&&x==0&&y==0&&rw==w&&rh==h){
        diag_czas("final_removal",czas_monotoniczny_us());
        zakoncz_powitanie();diag_start_reboot("[STARTUP] splash finished");
        return scena;
    }
    if(size_t(h)>SIZE_MAX/size_t(pitch))return scena;
    const size_t potrzebne=size_t(pitch)*h;
    bool rebuild=pelne_rysowanie||w!=szer||h!=wys||pitch!=stride||skala!=procent;
    if(rebuild){pierwsza_klatka_gotowa=false;pierwsza_klatka_opublikowana=false;PrezentacjaStartu::uniewaznij();}
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
    // Allocation/mapping can consume time too; sample after preparation and
    // before freezing the complete frame instead of replaying its old state.
    powitanie_aktualizuj_stan(false);
    auto* dst=reinterpret_cast<uint32_t*>(powierzchnia);
    {
        if(rebuild) {
            // The first visible welcome stays at time zero until publication;
            // subsequent geometry changes use the current absolute state.
            SplashFirstDiag::id().message_index=unsigned(sekwencja.krok);
            SplashFirstDiag::event("render_begin",czas_monotoniczny_us());
        }
        // Freeze one absolute-time state for both glyph passes. Always rebuild
        // background and title too; the reusable allocation carries no history.
        const auto frame=sekwencja;
        Powitanie::rysuj(dst,w,h,pitch/4,skala,frame,true);
        ++SplashFirstDiag::id().frame;
        poprzedni_krok=sekwencja.krok;poprzednia_sygnatura=sekwencja.sygnatura();pelne_rysowanie=false;
    }
    if(sekwencja.stan==Powitanie::Stan::Zanikanie||zakonczenie_oczekuje){
        // Blend a newly reconstructed complete frame, never the last blend.
        auto* src=reinterpret_cast<const uint32_t*>(scena);
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){size_t i=size_t(yy)*(pitch/4)+xx;dst[i]=Powitanie::mieszaj(dst[i],src[i],sekwencja.odsloniecie);}
    }
    if(rebuild) {
#if BURSZTYNDIAGSPLASHFIRSTFRAME
        auto ink=[&](Powitanie::Obszar r){
            for(int yy=r.y<0?0:r.y;yy<h&&yy<r.y+r.h;++yy)
                for(int xx=r.x<0?0:r.x;xx<w&&xx<r.x+r.w;++xx)
                    if(dst[size_t(yy)*(pitch/4)+xx]!=Powitanie::TLO)return true;
            return false;
        };
        const auto znak=Powitanie::obszar_znaku(w,h,skala);
        SplashFirstDiag::id().title=ink(znak);
        // Komunikaty pojawiaja sie dopiero po zapelnieniu paska, wiec pierwsza
        // klatka sprawdza gotowosc ich obszaru, a nie atrament (go jeszcze nie ma).
        SplashFirstDiag::id().message=1;
#endif
        SplashFirstDiag::event("render_end",czas_monotoniczny_us());
        // The surface becomes publishable only after all CPU rendering.
        pierwsza_klatka_gotowa=true;
        SplashFirstDiag::event("first_frame_ready",czas_monotoniczny_us());
    }
    // Hardware preparation may invalidate publication without changing this
    // allocation/geometry (for example a rejected startup modeset). Every
    // completed full render, not only a surface rebuild, restores readiness.
    PrezentacjaStartu::przygotowana();
#if BURSZTYNDIAGSPLASHRUNTIMEFRAMES
    const auto znak=Powitanie::obszar_znaku(w,h,skala);
    const auto napisy=Powitanie::centrum(w,h,skala);
    KlatkiStartuDiag::info()={SplashFirstDiag::id().frame,true,pierwsza_klatka_gotowa,
        {znak.x,znak.y,znak.w,znak.h},
        {napisy.x,napisy.y,napisy.w,napisy.h},Powitanie::TLO};
    KlatkiStartuDiag::info().message_index=unsigned(sekwencja.krok);
#endif
    return powierzchnia;
}
