/*
 * Bursztyn OS - Skladacz Obrazu
 *
 * Prosty kompozytor warstw Ring 3.
 *
 * Kontrakt:
 *
 *   - tablica_warstw[pid] przechowuje najwyzej jedna warstwe procesu,
 *   - bufor warstwy ma format 32-bit 0x00RRGGBB,
 *   - wartosc DOKLADNIE 0x00000000 oznacza przezroczystosc,
 *   - grafika.cpp moze uzyc gornego bajtu jako technicznego znacznika,
 *     aby narysowac wizualnie czarny piksel; kompozytor usuwa ten znacznik
 *     przed zapisem do backbufferu,
 *   - nizszy z_order jest skladany wczesniej,
 *   - przy jednakowym z_order stabilnym tie-breakerem jest PID,
 *   - zegar kernela jest rysowany nad warstwami, a kursor przez
 *     grafika_zakoncz_skladanie() nad gotowa klatka.
 *
 * Bezpieczenstwo:
 *
 *   - rozmiary powierzchni sa sprawdzane przed mnozeniem/alokacja,
 *   - pojedyncza warstwa i laczna pamiec warstw maja limity,
 *   - rozmiar bufora jest sledzony osobno od metadanych warstwy,
 *     co pozwala wykryc uszkodzone szerokosc/wysokosc przed odczytem,
 *   - tworzenie nowej warstwy nie niszczy starej, jezeli kmalloc() zawiedzie,
 *   - clipping uzywa int64_t, wiec x + szerokosc / y + wysokosc nie powoduje
 *     signed-overflow dla skrajnych wspolrzednych int,
 *   - aktywna warstwa jest publikowana dopiero po pelnej inicjalizacji,
 *   - kompozytor ma lekki guard przeciw reentrantnemu skladaniu.
 *
 * Synchronizacja:
 *
 * Aktualne BWS serializuje operacje GUI zewnetrzna blokada ekranu.
 * pobierz_warstwe() zwraca surowy wskaznik, wiec pelne SMP-safe lifetime
 * wymagaloby zmiany API (refcount/snapshot/lock trzymany przez czytelnika).
 * Ten plik wzmacnia publikacje acquire/release, ale nie udaje, ze samo to
 * rozwiazuje przyszly wielordzeniowy lifetime powierzchni.
 */

#include "skladacz_obrazu.h"
#include "sterowniki/czas/hpet.h"
#include "scheduler.h"

extern void wypisz_log(const char* tekst);
extern int grafika_pobierz_szerokosc();
extern int grafika_pobierz_wysokosc();
extern uint32_t* grafika_pobierz_wiersz_backbuffer(int y);
#include "heap.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. STALE I GLOBALNY STAN
 * ========================================================================= */

namespace {

constexpr int MAKS_WARSTW =
    16;

/*
 * Obecny heap kernela ma 16 MiB. Nie pozwalamy powierzchniom GUI zjesc
 * calej sterty, bo scheduler, loader, siec i system plikow rowniez
 * potrzebuja kmalloc().
 *
 * 12 MiB pozwala np. na kilka warstw 1024x768, pozostawiajac zapas
 * dla reszty jadra.
 *
 * Po przejsciu na dedykowany allocator powierzchni / wiekszy heap limit
 * powinien zostac przeniesiony do konfiguracji pamieci.
 */
constexpr uint64_t MAKS_PAMIEC_WARSTW =
    12ULL * 1024ULL * 1024ULL;

/*
 * Maksymalnie 8 MiB na pojedyncza powierzchnie.
 * 1920x1080x4 = 8 294 400 B, czyli nadal miesci sie w tym limicie.
 */
constexpr uint64_t MAKS_PAMIEC_JEDNEJ_WARSTWY =
    8ULL * 1024ULL * 1024ULL;

/*
 * Absolutny bezpiecznik metadanych. Faktyczny rozmiar warstwy jest dodatkowo
 * ograniczony do aktualnego rozmiaru ekranu.
 */
constexpr int MAKS_WYMIAR_WARSTWY =
    8192;

/*
 * Warstwy z absurdalnym z_order sa odrzucane.
 * To nie jest jeszcze pelny model focus/raise-to-front - ten powinien byc
 * zarzadzany przez menedzer okien/kernel, nie dowolna aplikacje.
 */
constexpr int MIN_Z_ORDER =
    -32768;

constexpr int MAX_Z_ORDER =
    32767;

/*
 * Dokladna liczba bajtow zaalokowana dla danego PID.
 *
 * Nie wyliczamy jej przy zwalnianiu z warstwa.szerokosc/wysokosc, bo
 * uszkodzone metadane moglyby doprowadzic do blednego accounting albo OOB.
 */
uint64_t rozmiar_alokacji_warstwy[
    MAKS_WARSTW
] = {};

bool widocznosc_warstwy[MAKS_WARSTW] = {};
uint32_t generacja_okna[MAKS_WARSTW] = {};
uint32_t stan_okna[MAKS_WARSTW] = {};
char tytul_okna[MAKS_WARSTW][48] = {};

/*
 * Steady-state budzet powierzchni.
 * Atomiki przygotowuja accounting pod przyszle wielordzeniowe wywolania.
 */
uint64_t zajete_bajty_warstw =
    0;

struct SystemOverlayState {
    bool otwarty;
    int pid;
    GuiDirtyRect rect;
};
SystemOverlayState system_overlay = {false, -1, {0, 0, 0, 0}};

/*
 * Guard chroni przed zagniezdzonym / rownoleglym rozpoczeciem skladania
 * tej samej klatki. Nie jest zamiennikiem lifetime-locka dla warstw.
 */
bool skladanie_w_toku =
    false;
GuiDirtyRect dirty_rects[SKLADACZ_MAKS_DIRTY_RECT] = {};
uint32_t dirty_count = 0;
bool cursor_pending=false;
GuiDirtyRect cursor_old{},cursor_new{};
struct PendingGeometry { bool pending; int x,y,old_x,old_y,width,height; };
PendingGeometry pending_geometry[MAKS_WARSTW]{};
uint64_t drag_moves_received=0,drag_moves_coalesced=0,drag_frames_presented=0;
uint64_t dirty_area_drag=0,cursor_fast_present_count=0;
uint64_t mouse_reports=0,mouse_moves=0;
bool drag_frame_waiting=false;
uint64_t dirty_generation = 1;
uint64_t nastepna_klatka_ns = 0;
constexpr uint64_t ODSTEP_KLATKI_NS = 16666667ULL;
uint64_t licznik_dirty=0,licznik_klatek=0,licznik_full=0,licznik_region=0;
uint64_t suma_compose_us=0,maks_compose_us=0,suma_present_us=0,maks_present_us=0;
uint64_t suma_dirty_px=0,suma_rect=0,maks_dirty_px=0;
[[maybe_unused]] uint64_t perf_deadline_ns=0,gui_perf_deadline_ns=0,perf_stare_irq=0,perf_stare_ctx=0,perf_stare_klatki=0,perf_stare_dirty=0;
[[maybe_unused]] uint64_t perf_stare_full=0,perf_stare_region=0,perf_stare_px=0,perf_stare_rect=0;

struct LayerRenderInfo { uint32_t* buffer; int x,y,width,height,z,pid; uint64_t pixels; };
struct DeferredBuffer { uint32_t* buffer; uint64_t bytes; };
DeferredBuffer deferred[SKLADACZ_MAKS_DIRTY_RECT] = {};
uint32_t deferred_count=0;
uint32_t render_readers=0;

uint64_t irq_off(){uint64_t f;asm volatile("pushfq; popq %0; cli":"=r"(f)::"memory","cc");return f;}
void irq_restore(uint64_t f){if(f&(1ULL<<9))asm volatile("sti":::"memory");}

void apply_pending_geometry(int only_pid){
    for(int pid=1;pid<MAKS_WARSTW;++pid){if(only_pid>0&&pid!=only_pid)continue;
        PendingGeometry p{};uint64_t f=irq_off();if(pending_geometry[pid].pending){p=pending_geometry[pid];pending_geometry[pid].pending=false;}irq_restore(f);if(!p.pending)continue;
        warstwa_obrazu* w=pobierz_warstwe(pid);if(!w)continue;w->x=p.x;w->y=p.y;
        skladacz_obrazu_oznacz_dirty_rect(p.old_x,p.old_y,p.width,p.height);skladacz_obrazu_oznacz_dirty_rect(p.x,p.y,p.width,p.height);
        drag_frame_waiting=true;
        dirty_area_drag+=static_cast<uint64_t>(p.width)*static_cast<uint64_t>(p.height)*2U;
    }
}

bool ma_pending_geometry(){
    uint64_t f=irq_off();bool wynik=false;
    for(int pid=1;pid<MAKS_WARSTW;++pid)if(pending_geometry[pid].pending){wynik=true;break;}
    irq_restore(f);return wynik;
}

bool rect_clip(GuiDirtyRect* r,int sw,int sh){
    if(!r||r->width<=0||r->height<=0||sw<=0||sh<=0)return false;
    int64_t x0=r->x,y0=r->y,x1=x0+r->width,y1=y0+r->height;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>sw)x1=sw;
    if(y1>sh)y1=sh;
    if(x0>=x1||y0>=y1)return false;
    r->x=(int)x0;r->y=(int)y0;r->width=(int)(x1-x0);r->height=(int)(y1-y0);return true;
}
uint64_t rect_area(const GuiDirtyRect&r){return(uint64_t)(uint32_t)r.width*(uint32_t)r.height;}
bool rect_przecina(const GuiDirtyRect&a,const GuiDirtyRect&b){
    return a.width>0&&a.height>0&&b.width>0&&b.height>0&&
        a.x<b.x+b.width&&b.x<a.x+a.width&&
        a.y<b.y+b.height&&b.y<a.y+a.height;
}
GuiDirtyRect rect_union(const GuiDirtyRect&a,const GuiDirtyRect&b){
    int x0=a.x<b.x?a.x:b.x,y0=a.y<b.y?a.y:b.y;
    int x1=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width;
    int y1=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;
    return{x0,y0,x1-x0,y1-y0};
}
bool rect_merge_ok(const GuiDirtyRect&a,const GuiDirtyRect&b){
    GuiDirtyRect u=rect_union(a,b);uint64_t ua=rect_area(u),sum=rect_area(a)+rect_area(b);
    bool touch=a.x<=b.x+b.width&&b.x<=a.x+a.width&&a.y<=b.y+b.height&&b.y<=a.y+a.height;
    return touch||ua<=sum+sum/2;
}

#ifndef BURSZTYN_DEBUG_PERF
#define BURSZTYN_DEBUG_PERF 0
#endif
#ifndef BURSZTYN_DEBUG_GUI_LAYERS
#define BURSZTYN_DEBUG_GUI_LAYERS 0
#endif
#ifndef BURSZTYN_DEBUG_GUI_PERF
#define BURSZTYN_DEBUG_GUI_PERF 0
#endif

#if BURSZTYN_DEBUG_GUI_PERF
uint64_t cursor_reoverlay_after_dirty=0;
#endif

#if BURSZTYN_DEBUG_PERF || BURSZTYN_DEBUG_GUI_LAYERS || BURSZTYN_DEBUG_GUI_PERF
void perf_num(char* b,size_t cap,size_t* n,uint64_t v){char t[24];size_t m=0;do{t[m++]=static_cast<char>('0'+v%10);v/=10;}while(v&&m<sizeof(t));while(m&&*n+1<cap)b[(*n)++]=t[--m];b[*n]='\0';}
void perf_txt(char*b,size_t cap,size_t*n,const char*s){while(s&&*s&&*n+1<cap)b[(*n)++]=*s++;b[*n]='\0';}
#endif
void perf_log(uint64_t teraz){
#if BURSZTYN_DEBUG_PERF
    if(perf_deadline_ns==0){perf_deadline_ns=teraz+5000000000ULL;return;}
    if(teraz<perf_deadline_ns) return;
    perf_deadline_ns=teraz+5000000000ULL;
    uint64_t irq=scheduler_liczba_irq_timera(),ctx=scheduler_liczba_przelaczen();
    uint64_t fr=licznik_klatek,dr=licznik_dirty,delta_fr=fr-perf_stare_klatki;
    uint64_t dp=suma_dirty_px,rc=suma_rect,screen=(uint64_t)grafika_pobierz_szerokosc()*grafika_pobierz_wysokosc();
    char b[384]={};size_t n=0;perf_txt(b,sizeof(b),&n,"[PERF] sched=");perf_num(b,sizeof(b),&n,(irq-perf_stare_irq)/5);perf_txt(b,sizeof(b),&n,"/s ctx=");perf_num(b,sizeof(b),&n,(ctx-perf_stare_ctx)/5);perf_txt(b,sizeof(b),&n,"/s frames=");perf_num(b,sizeof(b),&n,delta_fr/5);perf_txt(b,sizeof(b),&n,"/s dirty_req=");perf_num(b,sizeof(b),&n,(dr-perf_stare_dirty)/5);perf_txt(b,sizeof(b),&n,"/s avg_rects=");perf_num(b,sizeof(b),&n,delta_fr?rc/delta_fr:0);perf_txt(b,sizeof(b),&n," avg_dirty_px=");perf_num(b,sizeof(b),&n,delta_fr?dp/delta_fr:0);perf_txt(b,sizeof(b),&n," avg_dirty_pct=");perf_num(b,sizeof(b),&n,screen&&delta_fr?(dp*100)/(screen*delta_fr):0);perf_txt(b,sizeof(b),&n," max_dirty_pct=");perf_num(b,sizeof(b),&n,screen?(maks_dirty_px*100)/screen:0);perf_txt(b,sizeof(b),&n," compose_avg_us=");perf_num(b,sizeof(b),&n,delta_fr?suma_compose_us/delta_fr:0);perf_txt(b,sizeof(b),&n," compose_max_us=");perf_num(b,sizeof(b),&n,maks_compose_us);perf_txt(b,sizeof(b),&n," present_avg_us=");perf_num(b,sizeof(b),&n,delta_fr?suma_present_us/delta_fr:0);perf_txt(b,sizeof(b),&n," present_max_us=");perf_num(b,sizeof(b),&n,maks_present_us);perf_txt(b,sizeof(b),&n," full=");perf_num(b,sizeof(b),&n,(licznik_full-perf_stare_full)/5);perf_txt(b,sizeof(b),&n,"/s regions=");perf_num(b,sizeof(b),&n,(licznik_region-perf_stare_region)/5);perf_txt(b,sizeof(b),&n,"/s");wypisz_log(b);
    perf_stare_irq=irq;perf_stare_ctx=ctx;perf_stare_klatki=fr;perf_stare_dirty=dr;perf_stare_full=licznik_full;perf_stare_region=licznik_region;perf_stare_px=dp;perf_stare_rect=rc;suma_compose_us=0;maks_compose_us=0;suma_present_us=0;maks_present_us=0;suma_dirty_px=0;suma_rect=0;maks_dirty_px=0;
#else
    (void)teraz;
#endif
#if BURSZTYN_DEBUG_GUI_PERF
    if(gui_perf_deadline_ns==0){gui_perf_deadline_ns=teraz+5000000000ULL;return;}
    if(teraz<gui_perf_deadline_ns)return;
    gui_perf_deadline_ns=teraz+5000000000ULL;
    char g[512]={};size_t gn=0;
    perf_txt(g,sizeof(g),&gn,"[GUI-PERF] mouse_reports=");perf_num(g,sizeof(g),&gn,mouse_reports);
    perf_txt(g,sizeof(g),&gn," mouse_moves=");perf_num(g,sizeof(g),&gn,mouse_moves);
    perf_txt(g,sizeof(g),&gn," drag_moves_received=");perf_num(g,sizeof(g),&gn,drag_moves_received);
    perf_txt(g,sizeof(g),&gn," drag_moves_coalesced=");perf_num(g,sizeof(g),&gn,drag_moves_coalesced);
    perf_txt(g,sizeof(g),&gn," drag_frames_presented=");perf_num(g,sizeof(g),&gn,drag_frames_presented);
    perf_txt(g,sizeof(g),&gn," dirty_area_drag=");perf_num(g,sizeof(g),&gn,dirty_area_drag);
    perf_txt(g,sizeof(g),&gn," full_compose_count=");perf_num(g,sizeof(g),&gn,licznik_full);
    perf_txt(g,sizeof(g),&gn," partial_compose_count=");perf_num(g,sizeof(g),&gn,licznik_region);
    perf_txt(g,sizeof(g),&gn," cursor_fast_present_count=");perf_num(g,sizeof(g),&gn,cursor_fast_present_count);
    perf_txt(g,sizeof(g),&gn," cursor_reoverlay_after_dirty=");perf_num(g,sizeof(g),&gn,cursor_reoverlay_after_dirty);
    wypisz_log(g);
#endif
}

bool pid_poprawny(
    int pid
) {
    return
        pid >= 0 &&
        pid < MAKS_WARSTW;
}

bool z_order_poprawny(
    int z_order
) {
    return
        z_order >= MIN_Z_ORDER &&
        z_order <= MAX_Z_ORDER;
}

bool oblicz_rozmiar_powierzchni(
    int szer,
    int wys,
    uint64_t* liczba_pikseli,
    uint64_t* liczba_bajtow
) {
    if (!liczba_pikseli ||
        !liczba_bajtow) {

        return false;
    }

    if (szer <= 0 ||
        wys <= 0 ||
        szer > MAKS_WYMIAR_WARSTWY ||
        wys > MAKS_WYMIAR_WARSTWY) {

        return false;
    }

    const uint64_t w =
        static_cast<uint64_t>(
            static_cast<uint32_t>(
                szer
            )
        );

    const uint64_t h =
        static_cast<uint64_t>(
            static_cast<uint32_t>(
                wys
            )
        );

    if (w >
        UINT64_MAX / h) {

        return false;
    }

    const uint64_t piksele =
        w * h;

    if (piksele >
        UINT64_MAX /
            sizeof(uint32_t)) {

        return false;
    }

    const uint64_t bajty =
        piksele *
        sizeof(uint32_t);

    if (bajty >
            static_cast<uint64_t>(
                SIZE_MAX) ||
        bajty >
            MAKS_PAMIEC_JEDNEJ_WARSTWY) {

        return false;
    }

    *liczba_pikseli =
        piksele;

    *liczba_bajtow =
        bajty;

    return true;
}

bool wymiary_mieszcza_sie_na_ekranie(
    int szer,
    int wys
);

/* =========================================================================
 * 2. ACCOUNTING PAMIECI WARSTW
 * ========================================================================= */

bool zarezerwuj_dodatkowe_bajty(
    uint64_t delta
) {
    if (delta == 0) {
        return true;
    }

    uint64_t stare =
        __atomic_load_n(
            &zajete_bajty_warstw,
            __ATOMIC_RELAXED
        );

    for (;;) {
        if (stare >
            MAKS_PAMIEC_WARSTW) {

            return false;
        }

        if (delta >
            MAKS_PAMIEC_WARSTW -
                stare) {

            return false;
        }

        const uint64_t nowe =
            stare + delta;

        if (__atomic_compare_exchange_n(
                &zajete_bajty_warstw,
                &stare,
                nowe,
                false,
                __ATOMIC_ACQ_REL,
                __ATOMIC_RELAXED)) {

            return true;
        }

        /*
         * Przy nieudanym CAS "stare" zostaje uzupelnione aktualna wartoscia.
         */
    }
}

void oddaj_bajty(
    uint64_t bajty
) {
    if (bajty == 0) {
        return;
    }

    const uint64_t stare =
        __atomic_fetch_sub(
            &zajete_bajty_warstw,
            bajty,
            __ATOMIC_ACQ_REL
        );

    /*
     * Fail-safe accounting. Przy logicznym underflow nie zostawiamy
     * ogromnej wartosci UINT64_MAX, ktora zablokowalaby wszystkie
     * przyszle warstwy.
     *
     * Nie zwalnia to ani nie naprawia pamieci; jedynie ogranicza skutki
     * uszkodzenia licznika.
     */
    if (stare < bajty) {
        __atomic_store_n(
            &zajete_bajty_warstw,
            0ULL,
            __ATOMIC_RELEASE
        );
    }
}

void zwolnij_lub_odrocz(uint32_t* buffer,uint64_t bytes){
    if(!buffer)return;
    uint64_t f=irq_off();
    if(render_readers&&deferred_count<SKLADACZ_MAKS_DIRTY_RECT)deferred[deferred_count++]={buffer,bytes};
    else { irq_restore(f);kfree(buffer);oddaj_bajty(bytes);return; }
    irq_restore(f);
}

void oproznij_odroczone(){
    DeferredBuffer local[SKLADACZ_MAKS_DIRTY_RECT];uint32_t n=0;uint64_t f=irq_off();
    if(!render_readers){n=deferred_count;for(uint32_t i=0;i<n;++i)local[i]=deferred[i];deferred_count=0;}
    irq_restore(f);for(uint32_t i=0;i<n;++i){kfree(local[i].buffer);oddaj_bajty(local[i].bytes);}
}

/* =========================================================================
 * 3. WALIDACJA METADANYCH WARSTWY
 * ========================================================================= */

bool warstwa_bufor_spojny(
    int indeks
) {
    if (!pid_poprawny(
            indeks)) {

        return false;
    }

    const warstwa_obrazu& w =
        tablica_warstw[indeks];

    if (!__atomic_load_n(
            &w.aktywna,
            __ATOMIC_ACQUIRE)) {

        return false;
    }

    if (!w.bufor_pikseli ||
        w.szerokosc <= 0 ||
        w.wysokosc <= 0) {

        return false;
    }

    uint64_t piksele = 0;
    uint64_t bajty = 0;

    if (!oblicz_rozmiar_powierzchni(
            w.szerokosc,
            w.wysokosc,
            &piksele,
            &bajty)) {

        return false;
    }

    (void)piksele;

    /*
     * Najwazniejsza kontrola OOB: metadane musza opisywac DOKLADNIE
     * rozmiar rzeczywistej alokacji tego slotu.
     */
    return
        bajty ==
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[indeks],
            __ATOMIC_ACQUIRE
        );
}

bool warstwa_ma_spojny_bufor(int indeks) {
    return pid_poprawny(indeks) && widocznosc_warstwy[indeks] &&
           warstwa_bufor_spojny(indeks);
}

/* =========================================================================
 * 4. GUARD SKLADANIA
 * ========================================================================= */

bool rozpocznij_guard_skladania() {
    return
        !__atomic_test_and_set(
            &skladanie_w_toku,
            __ATOMIC_ACQUIRE
        );
}

void zakoncz_guard_skladania() {
    __atomic_clear(
        &skladanie_w_toku,
        __ATOMIC_RELEASE
    );
}

class GuardSkladania {
public:
    GuardSkladania()
        : aktywny_(
              rozpocznij_guard_skladania()
          ) {
    }

    ~GuardSkladania() {
        if (aktywny_) {
            zakoncz_guard_skladania();
        }
    }

    bool aktywny() const {
        return aktywny_;
    }

    GuardSkladania(
        const GuardSkladania&
    ) = delete;

    GuardSkladania& operator=(
        const GuardSkladania&
    ) = delete;

private:
    bool aktywny_;
};

/* =========================================================================
 * 5. CLIPPING
 * ========================================================================= */

int64_t max_i64(
    int64_t a,
    int64_t b
) {
    return
        a > b
            ? a
            : b;
}

int64_t min_i64(
    int64_t a,
    int64_t b
) {
    return
        a < b
            ? a
            : b;
}

struct ProstokatWidoczny {
    int src_x;
    int src_y;

    int dst_x;
    int dst_y;

    int szer;
    int wys;
};

bool wyznacz_widoczny_fragment(
    const warstwa_obrazu& w,
    int ekran_szer,
    int ekran_wys,
    ProstokatWidoczny* wynik
) {
    if (!wynik ||
        ekran_szer <= 0 ||
        ekran_wys <= 0 ||
        w.szerokosc <= 0 ||
        w.wysokosc <= 0) {

        return false;
    }

    /*
     * int64_t eliminuje UB typu:
     *
     *   INT_MAX + szerokosc
     */
    const int64_t lewo =
        static_cast<int64_t>(
            w.x
        );

    const int64_t gora =
        static_cast<int64_t>(
            w.y
        );

    const int64_t prawo =
        lewo +
        static_cast<int64_t>(
            w.szerokosc
        );

    const int64_t dol =
        gora +
        static_cast<int64_t>(
            w.wysokosc
        );

    const int64_t clip_lewo =
        max_i64(
            lewo,
            0
        );

    const int64_t clip_gora =
        max_i64(
            gora,
            0
        );

    const int64_t clip_prawo =
        min_i64(
            prawo,
            static_cast<int64_t>(
                ekran_szer
            )
        );

    const int64_t clip_dol =
        min_i64(
            dol,
            static_cast<int64_t>(
                ekran_wys
            )
        );

    if (clip_lewo >=
            clip_prawo ||
        clip_gora >=
            clip_dol) {

        return false;
    }

    const int64_t src_x =
        clip_lewo -
        lewo;

    const int64_t src_y =
        clip_gora -
        gora;

    const int64_t szer =
        clip_prawo -
        clip_lewo;

    const int64_t wys =
        clip_dol -
        clip_gora;

    /*
     * Wszystkie wartosci sa ograniczone rozmiarem ekranu/warstwy, wiec
     * po poprzedniej walidacji bezpiecznie mieszcza sie w int.
     */
    wynik->src_x =
        static_cast<int>(
            src_x
        );

    wynik->src_y =
        static_cast<int>(
            src_y
        );

    wynik->dst_x =
        static_cast<int>(
            clip_lewo
        );

    wynik->dst_y =
        static_cast<int>(
            clip_gora
        );

    wynik->szer =
        static_cast<int>(
            szer
        );

    wynik->wys =
        static_cast<int>(
            wys
        );

    return
        wynik->szer > 0 &&
        wynik->wys > 0;
}

/* =========================================================================
 * 6. KOLEJNOSC Z
 * ========================================================================= */

bool warstwa_przed(
    int a,
    int b
) {
    if (!pid_poprawny(a)) {
        return false;
    }

    if (!pid_poprawny(b)) {
        return true;
    }

    const warstwa_obrazu& wa =
        tablica_warstw[a];

    const warstwa_obrazu& wb =
        tablica_warstw[b];

    if (wa.z_order <
        wb.z_order) {

        return true;
    }

    if (wa.z_order >
        wb.z_order) {

        return false;
    }

    /*
     * Stabilny tie-breaker.
     */
    return
        a < b;
}

} // namespace

/* =========================================================================
 * 7. GLOBALNA TABLICA WARSTW
 * ========================================================================= */

warstwa_obrazu tablica_warstw[
    16
] = {};

/* =========================================================================
 * 8. SUROWE API GRAFIKI
 * ========================================================================= */

/*
 * Operacje te omijaja przekierowanie PostawPiksel() do aktywnej warstwy.
 */
extern void grafika_rozpocznij_skladanie();
extern void grafika_odtworz_tlo_skladania();
extern void grafika_odtworz_tlo_regionu(int,int,int,int);
extern void grafika_zapisz_surowy_piksel(
    int x,
    int y,
    uint32_t kolor
);
extern void grafika_zakoncz_skladanie();
extern void grafika_zakoncz_skladanie_regionu(int,int,int,int);
extern void grafika_prezentuj_region(int,int,int,int);
extern void grafika_prezentuj_kursor();
extern void grafika_prezentuj_kursor_w(int,int);
extern void grafika_pobierz_pozycje_kursora(int*,int*);
extern void grafika_pobierz_overlay_kursora(int*,int*,bool*);
extern void grafika_zakoncz_scene();
extern "C" void grafika_naloz_okno_terminala_region(int x, int y,
                                                       int szer, int wys);

extern int grafika_pobierz_szerokosc();
extern int grafika_pobierz_wysokosc();

extern void rysuj_zegar_rtc();

namespace {

bool wymiary_mieszcza_sie_na_ekranie(
    int szer,
    int wys
) {
    const int ekran_szer =
        grafika_pobierz_szerokosc();

    const int ekran_wys =
        grafika_pobierz_wysokosc();

    if (ekran_szer <= 0 ||
        ekran_wys <= 0) {

        return false;
    }

    /*
     * Powierzchnia wieksza od fizycznego ekranu nie wnosi obecnie wartosci,
     * a moze bardzo latwo wyczerpac 16-MiB heap kernela.
     *
     * Po wprowadzeniu scrollowalnych/offscreen surfaces polityke mozna
     * rozszerzyc razem z dedykowanym allocatorem grafiki.
     */
    return
        szer <= ekran_szer &&
        wys <= ekran_wys;
}

void wyczysc_bufor_pikseli(
    uint32_t* bufor,
    uint64_t liczba_pikseli
) {
    if (!bufor) {
        return;
    }

    for (uint64_t i = 0;
         i < liczba_pikseli;
         ++i) {

        bufor[i] =
            0x00000000U;
    }
}

} // namespace

/* =========================================================================
 * 9. DOSTEP DO WARSTWY
 * ========================================================================= */

warstwa_obrazu* pobierz_warstwe(
    int pid
) {
    if (!pid_poprawny(
            pid)) {

        return nullptr;
    }

    warstwa_obrazu& warstwa =
        tablica_warstw[pid];

    /*
     * Acquire paruje sie z release przy publikacji aktywnej warstwy.
     */
    if (!__atomic_load_n(
            &warstwa.aktywna,
            __ATOMIC_ACQUIRE)) {

        return nullptr;
    }

    /*
     * Nie zwracamy uszkodzonej powierzchni do grafika.cpp.
     */
    if (!warstwa_ma_spojny_bufor(
            pid)) {

        return nullptr;
    }

    return &warstwa;
}

/* =========================================================================
 * 10. TWORZENIE WARSTWY
 * ========================================================================= */

int utworz_warstwe(
    int pid,
    int x,
    int y,
    int szer,
    int wys,
    int z_order
) {
    if (!pid_poprawny(
            pid) ||
        !z_order_poprawny(
            z_order)) {

        return -1;
    }

    if (!wymiary_mieszcza_sie_na_ekranie(
            szer,
            wys)) {

        return -1;
    }

    uint64_t liczba_pikseli = 0;
    uint64_t nowe_bajty = 0;

    if (!oblicz_rozmiar_powierzchni(
            szer,
            wys,
            &liczba_pikseli,
            &nowe_bajty)) {

        return -1;
    }

    warstwa_obrazu& warstwa =
        tablica_warstw[pid];
    {
        const uint64_t f=irq_off();
        pending_geometry[pid]={};
        irq_restore(f);
    }
    const bool nowe_okno = !warstwa_bufor_spojny(pid);

    const uint64_t stare_bajty =
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[pid],
            __ATOMIC_ACQUIRE
        );

    uint32_t* stary_bufor =
        warstwa.bufor_pikseli;
    const int stary_x=warstwa.x,stary_y=warstwa.y;
    const int stary_szer=warstwa.szerokosc,stary_wys=warstwa.wysokosc;

    /*
     * Najczestszy przypadek przy ponownym tworzeniu tej samej powierzchni:
     * wykorzystujemy istniejaca alokacje zamiast wymagac chwilowo dwoch
     * duzych buforow z malego heapu.
     */
    if (stary_bufor &&
        stare_bajty ==
            nowe_bajty &&
        warstwa.szerokosc ==
            szer &&
        warstwa.wysokosc ==
            wys) {

        __atomic_store_n(
            &warstwa.aktywna,
            false,
            __ATOMIC_RELEASE
        );

        wyczysc_bufor_pikseli(
            stary_bufor,
            liczba_pikseli
        );

        warstwa.pid =
            pid;

        warstwa.z_order =
            z_order;

        warstwa.x =
            x;

        warstwa.y =
            y;

        warstwa.szerokosc =
            szer;

        warstwa.wysokosc =
            wys;

        warstwa.bufor_pikseli =
            stary_bufor;

        __atomic_store_n(
            &warstwa.aktywna,
            true,
            __ATOMIC_RELEASE
        );

        widocznosc_warstwy[pid] = true;
        if (nowe_okno) {
            if (++generacja_okna[pid] == 0) ++generacja_okna[pid];
            stan_okna[pid] = GUI_OKNO_NORMALNE;
        }

        if(stary_bufor)skladacz_obrazu_oznacz_dirty_rect(stary_x,stary_y,stary_szer,stary_wys);
        skladacz_obrazu_oznacz_dirty_rect(x,y,szer,wys);
        skladacz_obrazu_debug_warstwy("create/reuse");
        return pid;
    }

    /*
     * Accounting dotyczy stanu po udanej wymianie.
     *
     * Przy powiekszaniu rezerwujemy tylko roznice. kmalloc() nadal moze
     * odmowic z powodu chwilowej potrzeby posiadania starego i nowego
     * bufora jednoczesnie - wtedy stara warstwa pozostaje nietknieta.
     */
    uint64_t zarezerwowane_delta =
        0;

    if (nowe_bajty >
        stare_bajty) {

        zarezerwowane_delta =
            nowe_bajty -
            stare_bajty;

        if (!zarezerwuj_dodatkowe_bajty(
                zarezerwowane_delta)) {

            return -1;
        }
    }

    uint32_t* nowy_bufor =
        static_cast<uint32_t*>(
            kmalloc(
                nowe_bajty
            )
        );

    if (!nowy_bufor) {
        if (zarezerwowane_delta != 0) {
            oddaj_bajty(
                zarezerwowane_delta
            );
        }

        return -1;
    }

    wyczysc_bufor_pikseli(
        nowy_bufor,
        liczba_pikseli
    );

    /*
     * Dopiero teraz odpublikowujemy stara warstwe.
     */
    __atomic_store_n(
        &warstwa.aktywna,
        false,
        __ATOMIC_RELEASE
    );

    /*
     * Publikujemy wszystkie metadane przed aktywna=true.
     */
    warstwa.pid =
        pid;

    warstwa.z_order =
        z_order;

    warstwa.x =
        x;

    warstwa.y =
        y;

    warstwa.szerokosc =
        szer;

    warstwa.wysokosc =
        wys;

    warstwa.bufor_pikseli =
        nowy_bufor;

    __atomic_store_n(
        &rozmiar_alokacji_warstwy[pid],
        nowe_bajty,
        __ATOMIC_RELEASE
    );

    __atomic_store_n(
        &warstwa.aktywna,
        true,
        __ATOMIC_RELEASE
    );

    widocznosc_warstwy[pid] = true;
    if (nowe_okno) {
        if (++generacja_okna[pid] == 0) ++generacja_okna[pid];
        stan_okna[pid] = GUI_OKNO_NORMALNE;
    }

    /*
     * Stary bufor nie jest juz osiagalny przez nowa publikacje.
     */
    if (stary_bufor &&
        stary_bufor !=
            nowy_bufor) {

        zwolnij_lub_odrocz(stary_bufor, 0);
    }

    if (stare_bajty >
        nowe_bajty) {

        oddaj_bajty(
            stare_bajty -
            nowe_bajty
        );
    }

    if(stary_bufor)skladacz_obrazu_oznacz_dirty_rect(stary_x,stary_y,stary_szer,stary_wys);
    skladacz_obrazu_oznacz_dirty_rect(x,y,szer,wys);
    skladacz_obrazu_debug_warstwy("create");
    return pid;
}

/* =========================================================================
 * 11. PRZESUWANIE WARSTWY
 * ========================================================================= */

void zaktualizuj_pozycje_warstwy(
    int pid,
    int nowy_x,
    int nowy_y
) {
    warstwa_obrazu* warstwa =
        pobierz_warstwe(
            pid
        );

    if (!warstwa) {
        return;
    }

    /*
     * Nie wykonujemy x+szer tutaj, wiec same skrajne wartosci int sa
     * bezpieczne. Kompozytor uzyje int64_t podczas clippingu.
     */
    ++drag_moves_received;
    uint64_t f=irq_off();PendingGeometry& p=pending_geometry[pid];
    if(p.pending)++drag_moves_coalesced;
    else{p.old_x=warstwa->x;p.old_y=warstwa->y;p.width=warstwa->szerokosc;p.height=warstwa->wysokosc;p.pending=true;}
    p.x=nowy_x;p.y=nowy_y;irq_restore(f);
}

void skladacz_obrazu_zastosuj_pending_geometrii(int pid){apply_pending_geometry(pid);}

void skladacz_obrazu_zarejestruj_raport_myszy(bool ruch){
    __atomic_fetch_add(&mouse_reports,1ULL,__ATOMIC_RELAXED);
    if(ruch)__atomic_fetch_add(&mouse_moves,1ULL,__ATOMIC_RELAXED);
}

/* =========================================================================
 * 12. CZYSZCZENIE WARSTWY
 * ========================================================================= */

void wyczysc_warstwe(
    int pid
) {
    warstwa_obrazu* warstwa =
        pobierz_warstwe(
            pid
        );

    if (!warstwa ||
        !warstwa->bufor_pikseli) {

        return;
    }

    const uint64_t bajty =
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[pid],
            __ATOMIC_ACQUIRE
        );

    if (bajty == 0 ||
        (bajty %
         sizeof(uint32_t)) != 0) {

        return;
    }

    const uint64_t liczba_pikseli =
        bajty /
        sizeof(uint32_t);

    wyczysc_bufor_pikseli(
        warstwa->bufor_pikseli,
        liczba_pikseli
    );
    skladacz_obrazu_oznacz_dirty_warstwy(pid);
}

/* =========================================================================
 * 13. USUWANIE WARSTWY
 * ========================================================================= */

void usun_warstwe(
    int pid
) {
    if (!pid_poprawny(
            pid)) {

        return;
    }

    warstwa_obrazu& warstwa =
        tablica_warstw[pid];
    {
        const uint64_t f=irq_off();
        pending_geometry[pid]={};
        irq_restore(f);
    }
    if (system_overlay.otwarty && system_overlay.pid == pid)
        system_overlay = {false, -1, {0, 0, 0, 0}};
    const int stare_x=warstwa.x, stare_y=warstwa.y;
    const int stare_szer=warstwa.szerokosc, stare_wys=warstwa.wysokosc;

    /*
     * Najpierw przestajemy publikowac powierzchnie nowym czytelnikom.
     */
    __atomic_store_n(
        &warstwa.aktywna,
        false,
        __ATOMIC_RELEASE
    );
    widocznosc_warstwy[pid] = false;
    stan_okna[pid] = GUI_OKNO_NORMALNE;
    tytul_okna[pid][0] = '\0';

    uint32_t* bufor =
        warstwa.bufor_pikseli;

    const uint64_t bajty =
        __atomic_exchange_n(
            &rozmiar_alokacji_warstwy[pid],
            0ULL,
            __ATOMIC_ACQ_REL
        );

    /*
     * Zerujemy metadane przed kfree(), aby nowy pobierz_warstwe() nie mogl
     * znalezc wskaznika do zwalnianej pamieci.
     */
    warstwa = {};
    warstwa.pid =
        pid;

    if (bufor) zwolnij_lub_odrocz(bufor, bajty);
    skladacz_obrazu_oznacz_dirty_rect(stare_x, stare_y, stare_szer, stare_wys);
    skladacz_obrazu_debug_warstwy("remove");
}

/* =========================================================================
 * 14. SKLADANIE POJEDYNCZEJ WARSTWY
 * ========================================================================= */

namespace {

void zloz_warstwe(
    int indeks,
    int ekran_szer,
    int ekran_wys
) {
    if (!warstwa_ma_spojny_bufor(
            indeks)) {

        return;
    }

    const warstwa_obrazu& w =
        tablica_warstw[indeks];

    ProstokatWidoczny clip{};

    if (!wyznacz_widoczny_fragment(
            w,
            ekran_szer,
            ekran_wys,
            &clip)) {

        return;
    }

    const uint64_t szerokosc_warstwy =
        static_cast<uint64_t>(
            static_cast<uint32_t>(
                w.szerokosc
            )
        );

    const uint64_t zaalokowane_piksele =
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[indeks],
            __ATOMIC_ACQUIRE
        ) /
        sizeof(uint32_t);

    for (int y = 0;
         y < clip.wys;
         ++y) {

        const uint64_t src_y =
            static_cast<uint64_t>(
                static_cast<uint32_t>(
                    clip.src_y + y
                )
            );

        const uint64_t pierwszy =
            src_y *
                szerokosc_warstwy +
            static_cast<uint64_t>(
                static_cast<uint32_t>(
                    clip.src_x
                )
            );

        const uint64_t dlugosc =
            static_cast<uint64_t>(
                static_cast<uint32_t>(
                    clip.szer
                )
            );

        /*
         * Metadane byly juz sprawdzone, ale ten check jest celowo tuz przed
         * dereferencja - chroni przed ewentualnym uszkodzeniem indeksu.
         */
        if (pierwszy >
                zaalokowane_piksele ||
            dlugosc >
                zaalokowane_piksele -
                    pierwszy) {

            return;
        }

        const uint32_t* zrodlo =
            w.bufor_pikseli +
            pierwszy;

        const int dst_y =
            clip.dst_y +
            y;

        for (int x = 0;
             x < clip.szer;
             ++x) {

            uint32_t kolor =
                zrodlo[x];

            /*
             * DOKLADNE zero jest przezroczyste.
             */
            if (kolor ==
                0x00000000U) {

                continue;
            }

            /*
             * Gorny bajt jest technicznym markerem uzywanym m.in. przez
             * grafika.cpp do rozroznienia:
             *
             *   0x00000000 = przezroczysty
             *   0x01000000 = widoczna czern
             *
             * Framebuffer Bursztyna uzywa 0x00RRGGBB, wiec marker usuwamy.
             */
            kolor &=
                0x00FFFFFFU;

            grafika_zapisz_surowy_piksel(
                clip.dst_x + x,
                dst_y,
                kolor
            );
        }
    }
}

void zloz_snapshot_region(const LayerRenderInfo& w,const GuiDirtyRect& r) {
    int x0=w.x>r.x?w.x:r.x, y0=w.y>r.y?w.y:r.y;
    int x1=w.x+w.width<r.x+r.width?w.x+w.width:r.x+r.width;
    int y1=w.y+w.height<r.y+r.height?w.y+w.height:r.y+r.height;
    if(x0>=x1||y0>=y1||!w.buffer)return;
    for(int py=y0;py<y1;++py){
        uint64_t first=(uint64_t)(py-w.y)*(uint32_t)w.width+(uint32_t)(x0-w.x);
        uint64_t len=(uint32_t)(x1-x0);if(first>w.pixels||len>w.pixels-first)return;
        const uint32_t* src=w.buffer+first;uint32_t* dst=grafika_pobierz_wiersz_backbuffer(py)+x0;
        for(int px=x0;px<x1;++px){uint32_t c=*src++;if(c)*dst=c&SKLADACZ_MASKA_KOLORU_RGB;++dst;}
    }
}

int snapshot_warstw(LayerRenderInfo* out) {
    uint64_t f=irq_off();++render_readers;int n=0;
    for(int i=0;i<MAKS_WARSTW;++i)if(warstwa_ma_spojny_bufor(i)){
        const warstwa_obrazu&w=tablica_warstw[i];
        out[n++]={w.bufor_pikseli,w.x,w.y,w.szerokosc,w.wysokosc,w.z_order,w.pid,
            __atomic_load_n(&rozmiar_alokacji_warstwy[i],__ATOMIC_RELAXED)/sizeof(uint32_t)};
    }
    irq_restore(f);
    for(int i=1;i<n;++i){LayerRenderInfo v=out[i];int j=i-1;while(j>=0&&(out[j].z>v.z||(out[j].z==v.z&&out[j].pid>v.pid))){out[j+1]=out[j];--j;}out[j+1]=v;}
    return n;
}

void zakoncz_snapshot(){uint64_t f=irq_off();if(render_readers)--render_readers;irq_restore(f);oproznij_odroczone();}

} // namespace

/* =========================================================================
 * 15. KOMPOZYCJA CALEJ KLATKI
 * ========================================================================= */

void skladacz_obrazu_zloz_klatke() {
    GuardSkladania guard;

    if (!guard.aktywny()) {
        /*
         * Klatka jest juz skladana. Pomijamy zagniezdzone zadanie zamiast
         * modyfikowac backbuffer rownolegle.
         */
        return;
    }

    const int ekran_szer =
        grafika_pobierz_szerokosc();

    const int ekran_wys =
        grafika_pobierz_wysokosc();

    if (ekran_szer <= 0 ||
        ekran_wys <= 0) {

        return;
    }

    /*
     * Najpierw przechodzimy w surowy tryb backbufferu. Od tego momentu
     * PostawPiksel() nie moze przypadkowo zapisac do warstwy aktualnego PID.
     */
    grafika_rozpocznij_skladanie();

    /*
     * Kazda pelna klatka zaczyna sie od tapety/koloru pulpitu. Usuwa to
     * slady po przesunietych lub zamknietych oknach.
     */
    grafika_odtworz_tlo_skladania();

    /*
     * Maksymalnie 16 wpisow, wiec prosty selection-sort jest szybszy
     * organizacyjnie i nie potrzebuje dynamicznej pamieci.
     */
    bool uzyta[
        MAKS_WARSTW
    ] = {};

    for (int numer = 0;
         numer < MAKS_WARSTW;
         ++numer) {

        int wybrana =
            -1;

        for (int i = 0;
             i < MAKS_WARSTW;
             ++i) {

            if (uzyta[i] ||
                !warstwa_ma_spojny_bufor(
                    i)) {

                continue;
            }

            if (wybrana < 0 ||
                warstwa_przed(
                    i,
                    wybrana)) {

                wybrana =
                    i;
            }
        }

        if (wybrana < 0) {
            break;
        }

        uzyta[wybrana] =
            true;

        zloz_warstwe(
            wybrana,
            ekran_szer,
            ekran_wys
        );
    }

    if (system_overlay.otwarty &&
        warstwa_ma_spojny_bufor(system_overlay.pid)) {
        const warstwa_obrazu& ow = tablica_warstw[system_overlay.pid];
        LayerRenderInfo oi{ow.bufor_pikseli, ow.x, ow.y, ow.szerokosc,
                           ow.wysokosc, ow.z_order, ow.pid,
            __atomic_load_n(&rozmiar_alokacji_warstwy[system_overlay.pid],
                            __ATOMIC_RELAXED) / sizeof(uint32_t)};
        zloz_snapshot_region(oi, system_overlay.rect);
    }

    /*
     * Zegar jest nakladka kernela, nie powierzchnia dowolnego procesu.
     */
    rysuj_zegar_rtc();

    /*
     * grafika_zakoncz_skladanie():
     *   - rysuje kursor,
     *   - przenosi backbuffer do framebufferu,
     *   - opuszcza surowy tryb skladania.
     */
    grafika_zakoncz_skladanie();
}

void skladacz_obrazu_oznacz_dirty() {
    skladacz_obrazu_oznacz_dirty_rect(0,0,grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc());
}

void skladacz_obrazu_oznacz_dirty_rect(int x,int y,int width,int height){
    GuiDirtyRect r{x,y,width,height};const int sw=grafika_pobierz_szerokosc(),sh=grafika_pobierz_wysokosc();
    if(!rect_clip(&r,sw,sh))return;
    uint64_t f=irq_off();++licznik_dirty;++dirty_generation;
    /* Scalaj przechodnio tylko regiony, dla ktorych bounding box jest
       rzeczywiscie oplacalny. Usuniecie przez swap nie pomija wpisu. */
    uint32_t i=0;
    while(i<dirty_count){
        if(rect_merge_ok(dirty_rects[i],r)){
            r=rect_union(dirty_rects[i],r);
            dirty_rects[i]=dirty_rects[--dirty_count];
            i=0;
        } else ++i;
    }
    if(dirty_count<SKLADACZ_MAKS_DIRTY_RECT)dirty_rects[dirty_count++]=r;
    else {GuiDirtyRect all=r;for(uint32_t k=0;k<dirty_count;++k)all=rect_union(all,dirty_rects[k]);dirty_rects[0]=all;dirty_count=1;}
    irq_restore(f);
}

void skladacz_obrazu_oznacz_dirty_warstwy(int pid){warstwa_obrazu*w=pobierz_warstwe(pid);if(w)skladacz_obrazu_oznacz_dirty_rect(w->x,w->y,w->szerokosc,w->wysokosc);}

void skladacz_obrazu_oznacz_ruch_kursora(int old_x,int old_y,int new_x,int new_y){
    GuiDirtyRect oldr{old_x,old_y,16,16},newr{new_x,new_y,16,16};
    const int sw=grafika_pobierz_szerokosc(),sh=grafika_pobierz_wysokosc();
    const bool old_ok=rect_clip(&oldr,sw,sh),new_ok=rect_clip(&newr,sw,sh);
    if(!old_ok&&!new_ok)return;
    if(!old_ok)oldr=newr;
    if(!new_ok)newr=oldr;
    uint64_t f=irq_off();++licznik_dirty;++dirty_generation;
    if(!cursor_pending)cursor_old=oldr;
    cursor_new=newr;cursor_pending=true;
    irq_restore(f);
}

void skladacz_obrazu_obsluz_dirty() {
    uint64_t f=irq_off();bool ma_dirty=dirty_count!=0,ma_cursor=cursor_pending;irq_restore(f);
    const bool ma_geometrie=ma_pending_geometry();
    if(!ma_dirty&&!ma_cursor&&!ma_geometrie)return;

    uint64_t now=hpet_dostepny()?czas_monotoniczny_ns():0;
    const bool klatka_potrzebna=ma_dirty||ma_geometrie;
    if(klatka_potrzebna&&hpet_dostepny()&&nastepna_klatka_ns&&now<nastepna_klatka_ns){
        /* Kursor nie czeka na pacing duzych warstw. Restore i overlay nadal
           wykonuje petla PID 0, nigdy IRQ myszy. */
        GuiDirtyRect nowy{};bool ruch=false;
        f=irq_off();if(cursor_pending){nowy=cursor_new;cursor_pending=false;ruch=true;}irq_restore(f);
        if(ruch){grafika_prezentuj_kursor_w(nowy.x,nowy.y);++cursor_fast_present_count;}
        if(hpet_dostepny())perf_log(now);
        return;
    }
    if(!klatka_potrzebna){
        GuiDirtyRect nowy{};bool ruch=false;
        f=irq_off();if(cursor_pending){nowy=cursor_new;cursor_pending=false;ruch=true;}irq_restore(f);
        if(ruch){grafika_prezentuj_kursor_w(nowy.x,nowy.y);++cursor_fast_present_count;}
        if(hpet_dostepny())perf_log(now);
        return;
    }

    if(hpet_dostepny())nastepna_klatka_ns=now>UINT64_MAX-ODSTEP_KLATKI_NS?UINT64_MAX:now+ODSTEP_KLATKI_NS;
    /* Geometria jest konsumowana dopiero przez klatke, dzieki czemu wiele
       MOVE pomiedzy dwiema klatkami zastępuje poprzednia pozycje. */
    apply_pending_geometry(-1);
    bool drag_frame_work=false;
    f=irq_off();drag_frame_work=drag_frame_waiting;drag_frame_waiting=false;irq_restore(f);
    GuiDirtyRect work[SKLADACZ_MAKS_DIRTY_RECT];uint32_t count=0;uint64_t generation_start;bool cursor_work=false;GuiDirtyRect old_cursor{},new_cursor{};
    f=irq_off();count=dirty_count;for(uint32_t i=0;i<count;++i)work[i]=dirty_rects[i];dirty_count=0;cursor_work=cursor_pending;old_cursor=cursor_old;new_cursor=cursor_new;cursor_pending=false;generation_start=dirty_generation;irq_restore(f);
    GuardSkladania guard;if(!guard.aktywny()){for(uint32_t i=0;i<count;++i)skladacz_obrazu_oznacz_dirty_rect(work[i].x,work[i].y,work[i].width,work[i].height);if(cursor_work)skladacz_obrazu_oznacz_ruch_kursora(old_cursor.x,old_cursor.y,new_cursor.x,new_cursor.y);if(drag_frame_work){f=irq_off();drag_frame_waiting=true;irq_restore(f);}return;}
    const uint64_t start=hpet_dostepny()?czas_monotoniczny_us():0;LayerRenderInfo layers[MAKS_WARSTW];int layer_count=0;
    if(count)layer_count=snapshot_warstw(layers);
    int overlay_x=0,overlay_y=0;bool overlay_widoczny=false;
    grafika_pobierz_overlay_kursora(&overlay_x,&overlay_y,&overlay_widoczny);
    const GuiDirtyRect overlay_rect{overlay_x,overlay_y,16,16};
    bool dirty_przecina_kursor=false;
    if(overlay_widoczny)for(uint32_t ri=0;ri<count;++ri){
        if(rect_przecina(work[ri],overlay_rect)){dirty_przecina_kursor=true;break;}
    }
    grafika_rozpocznij_skladanie();uint64_t pixels=0;
    /* Faza A: wszystkie regiony trafiaja w calosci do scene backbufferu.
       Framebuffer nie jest dotykany podczas kosztownego compositingu. */
    const int sw=grafika_pobierz_szerokosc(),sh=grafika_pobierz_wysokosc();
    for(uint32_t ri=0;ri<count;++ri){const GuiDirtyRect&r=work[ri];pixels+=rect_area(r);grafika_odtworz_tlo_regionu(r.x,r.y,r.width,r.height);for(int li=0;li<layer_count;++li)zloz_snapshot_region(layers[li],r);if(system_overlay.otwarty){for(int li=0;li<layer_count;++li)if(layers[li].pid==system_overlay.pid){GuiDirtyRect o=system_overlay.rect;int x0=o.x>r.x?o.x:r.x,y0=o.y>r.y?o.y:r.y,x1=o.x+o.width<r.x+r.width?o.x+o.width:r.x+r.width,y1=o.y+o.height<r.y+r.height?o.y+o.height:r.y+r.height;if(x0<x1&&y0<y1){GuiDirtyRect clip{x0,y0,x1-x0,y1-y0};zloz_snapshot_region(layers[li],clip);}break;}}if(r.x+r.width>sw-150&&r.y+r.height>sh-40)rysuj_zegar_rtc();}
    const uint64_t compose_end=hpet_dostepny()?czas_monotoniczny_us():0;
    /* Faza B: prezentujemy dopiero gotowe wiersze, a kursor nakladamy raz,
       jako ostatni overlay. Scene backbuffer pozostaje bez kursora. */
    for(uint32_t ri=0;ri<count;++ri){const GuiDirtyRect&r=work[ri];grafika_prezentuj_region(r.x,r.y,r.width,r.height);}
    int cursor_x=overlay_x,cursor_y=overlay_y;
    if(cursor_work){cursor_x=new_cursor.x;cursor_y=new_cursor.y;pixels+=rect_area(old_cursor);}
    else if(!overlay_widoczny)grafika_pobierz_pozycje_kursora(&cursor_x,&cursor_y);
    /* Scene nigdy nie zawiera kursora. Bez ruchu myszy overlay pozostaje
       nietkniety, dopoki dirty faktycznie nie przecina jego 16x16. */
    if(cursor_work||dirty_przecina_kursor||!overlay_widoczny){
        grafika_prezentuj_kursor_w(cursor_x,cursor_y);++cursor_fast_present_count;
#if BURSZTYN_DEBUG_GUI_PERF
        if(dirty_przecina_kursor)++cursor_reoverlay_after_dirty;
#endif
    }
    grafika_zakoncz_scene();
    if(drag_frame_work)++drag_frames_presented;
    const uint64_t present_end=hpet_dostepny()?czas_monotoniczny_us():0;
    if(count) zakoncz_snapshot();
    ++licznik_klatek;
    suma_dirty_px+=pixels;
    suma_rect+=count+(cursor_work?2U:0U);
    if(pixels>maks_dirty_px)maks_dirty_px=pixels;
    const bool pelny_compose=count==1U&&work[0].x==0&&work[0].y==0&&
        work[0].width>=sw&&work[0].height>=sh;
    if(pelny_compose)++licznik_full;else if(count!=0U)++licznik_region;
    /* Nowe dirty powstale podczas klatki pozostaly w kolejce. Generation
       sluzy jako jawna kontrola, ze nie wolno ich skasowac na koncu. */
    f=irq_off();bool newer=dirty_generation!=generation_start;irq_restore(f);(void)newer;
    if(hpet_dostepny()){uint64_t compose=compose_end>=start?compose_end-start:0;uint64_t present=present_end>=compose_end?present_end-compose_end:0;suma_compose_us+=compose;suma_present_us+=present;if(compose>maks_compose_us)maks_compose_us=compose;if(present>maks_present_us)maks_present_us=present;perf_log(present_end*1000ULL);}
}

void skladacz_obrazu_podnies_warstwe(int pid) {
    warstwa_obrazu* cel = pobierz_warstwe(pid);
    if (!cel || cel->z_order <= 0) return; /* pulpit pozostaje na dole */
    int maks = cel->z_order;
    for (int i=1;i<MAKS_WARSTW;++i) {
        warstwa_obrazu* w=pobierz_warstwe(i);
        if (w && w->z_order>0 && w->z_order>maks) maks=w->z_order;
    }
    if (maks < 30000) cel->z_order=maks+1;
    else {
        int kolejny=100;
        bool uzyte[MAKS_WARSTW]={};
        for (int n=0;n<MAKS_WARSTW;++n) {
            int best=-1;
            for(int i=1;i<MAKS_WARSTW;++i) {
                warstwa_obrazu* w=pobierz_warstwe(i);
                if(!uzyte[i]&&w&&w->z_order>0&&
                   (best<0||w->z_order<tablica_warstw[best].z_order))
                    best=i;
            }
            if(best<0) break;
            uzyte[best]=true;
            tablica_warstw[best].z_order=kolejny++;
        }
        cel->z_order=kolejny;
    }
    skladacz_obrazu_oznacz_dirty_rect(cel->x,cel->y,cel->szerokosc,cel->wysokosc);
}

bool skladacz_obrazu_ustaw_tytul(int pid, const char* tytul) {
    if (!pid_poprawny(pid) || !tytul || !warstwa_bufor_spojny(pid)) return false;
    bool zmieniony = false;
    uint32_t i = 0;
    for (; i + 1U < sizeof(tytul_okna[pid]) && tytul[i] != '\0'; ++i) {
        if (tytul_okna[pid][i] != tytul[i]) zmieniony = true;
        tytul_okna[pid][i] = tytul[i];
    }
    if (tytul_okna[pid][i] != '\0') zmieniony = true;
    tytul_okna[pid][i] = '\0';
    return zmieniony;
}

uint64_t skladacz_obrazu_id_okna(int pid) {
    if (!pid_poprawny(pid) || !warstwa_bufor_spojny(pid)) return 0;
    return (static_cast<uint64_t>(generacja_okna[pid]) << 32U) |
           static_cast<uint32_t>(pid);
}

bool skladacz_obrazu_czy_widoczna(int pid) {
    return pid_poprawny(pid) && widocznosc_warstwy[pid] &&
           warstwa_bufor_spojny(pid);
}

bool skladacz_obrazu_minimalizuj(int pid) {
    if (!pid_poprawny(pid) || !warstwa_bufor_spojny(pid) ||
        !widocznosc_warstwy[pid]) return false;
    const warstwa_obrazu& w = tablica_warstw[pid];
    widocznosc_warstwy[pid] = false;
    stan_okna[pid] = GUI_OKNO_ZMINIMALIZOWANE;
    skladacz_obrazu_oznacz_dirty_rect(w.x, w.y, w.szerokosc, w.wysokosc);
    return true;
}

bool skladacz_obrazu_przywroc(uint64_t window_id) {
    const int pid = static_cast<int>(static_cast<uint32_t>(window_id));
    const uint32_t generation = static_cast<uint32_t>(window_id >> 32U);
    if (!pid_poprawny(pid) || generation == 0 ||
        generacja_okna[pid] != generation || !warstwa_bufor_spojny(pid))
        return false;
    widocznosc_warstwy[pid] = true;
    if (stan_okna[pid] == GUI_OKNO_ZMINIMALIZOWANE)
        stan_okna[pid] = GUI_OKNO_NORMALNE;
    const warstwa_obrazu& w = tablica_warstw[pid];
    skladacz_obrazu_oznacz_dirty_rect(w.x, w.y, w.szerokosc, w.wysokosc);
    return true;
}

uint32_t skladacz_obrazu_snapshot_okien(GuiOknoInfo* out, uint32_t max,
                                        int aktywny_pid) {
    if (!out || max == 0) return 0;
    uint32_t count = 0;
    for (int pid = 1; pid < MAKS_WARSTW && count < max; ++pid) {
        if (!warstwa_bufor_spojny(pid)) continue;
        const warstwa_obrazu& w = tablica_warstw[pid];
        GuiOknoInfo& o = out[count++];
        o = {};
        o.window_id = skladacz_obrazu_id_okna(pid);
        o.pid = pid;
        o.generation = generacja_okna[pid];
        o.stan = stan_okna[pid];
        o.widoczne = widocznosc_warstwy[pid] ? 1U : 0U;
        o.aktywne = pid == aktywny_pid ? 1U : 0U;
        o.x = w.x; o.y = w.y; o.szerokosc = w.szerokosc; o.wysokosc = w.wysokosc;
        for (uint32_t i = 0; i < sizeof(o.tytul); ++i) o.tytul[i] = tytul_okna[pid][i];
    }
    return count;
}

void skladacz_obrazu_ustaw_overlay(int pid, bool otwarty,
                                   int x, int y, int szer, int wys) {
    const GuiDirtyRect stary = system_overlay.rect;
    const bool byl = system_overlay.otwarty;
    if (!otwarty) {
        system_overlay = {false, -1, {0, 0, 0, 0}};
    } else if (pid_poprawny(pid) && warstwa_ma_spojny_bufor(pid) &&
               szer > 0 && wys > 0) {
        system_overlay = {true, pid, {x, y, szer, wys}};
    } else {
        return;
    }
    if (byl) skladacz_obrazu_oznacz_dirty_rect(stary.x, stary.y,
                                                stary.width, stary.height);
    if (system_overlay.otwarty)
        skladacz_obrazu_oznacz_dirty_rect(x, y, szer, wys);
}

int skladacz_obrazu_overlay_pod_punktem(int x, int y) {
    if (!system_overlay.otwarty) return -1;
    (void)x;
    (void)y;
    /* Otwarty popup jest modalny dla pierwszego MOUSE_DOWN. Dzieki temu
       klik poza nim zamyka popup i nie jest dostarczany drugi raz aplikacji. */
    return system_overlay.pid;
}

void skladacz_obrazu_debug_warstwy(const char* powod) {
#if BURSZTYN_DEBUG_GUI_LAYERS
    char b[192];size_t n=0;perf_txt(b,sizeof(b),&n,"[GUI-LAYERS] ");perf_txt(b,sizeof(b),&n,powod?powod:"");wypisz_log(b);
    for(int i=0;i<MAKS_WARSTW;++i){warstwa_obrazu*w=pobierz_warstwe(i);if(!w)continue;n=0;b[0]='\0';
        perf_txt(b,sizeof(b),&n,"pid=");perf_num(b,sizeof(b),&n,w->pid);perf_txt(b,sizeof(b),&n," x=");perf_num(b,sizeof(b),&n,(uint32_t)w->x);perf_txt(b,sizeof(b),&n," y=");perf_num(b,sizeof(b),&n,(uint32_t)w->y);perf_txt(b,sizeof(b),&n," w=");perf_num(b,sizeof(b),&n,w->szerokosc);perf_txt(b,sizeof(b),&n," h=");perf_num(b,sizeof(b),&n,w->wysokosc);perf_txt(b,sizeof(b),&n," z=");perf_num(b,sizeof(b),&n,(uint32_t)w->z_order);perf_txt(b,sizeof(b),&n," visible=1");wypisz_log(b);}
#else
    (void)powod;
#endif
}
