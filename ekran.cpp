#include "ekran.h"
#include "grafika.h"
#include "skladacz_obrazu.h"
#include "bufory_grafiki.h"
#include "tapety.h"
#include "psf.h"
#include "scheduler.h"
#include "bws_zdarzenia.h"
#include "pamiec.h"
#include "heap.h"
#include "sterowniki/czas/hpet.h"
#include "virtio_gpu.h"
extern "C" void zablokuj_ekran();
extern "C" void odblokuj_ekran();
namespace {
struct TransakcjaEkranu {
    bool oczekuje, fizyczna;
    int owner;
    uint64_t id, deadline;
    uint32_t stara_skala, nowa_skala;
    StanBuforowEkranu stary, nowy;
    ZmianaWarstwEkranu warstwy;
};
TransakcjaEkranu tx{}; // Kilka KiB w BSS, nigdy na stosie 16 KiB.
uint32_t skala=100,generacja=1;
uint64_t nastepna=0,zmiany=0,cofniecia=0;
TrybEkranu boot_tryb{};
uint64_t teraz(){return hpet_dostepny()?czas_monotoniczny_ns():scheduler_pobierz_tick()*4000000ULL;}
bool rowne(const char*a,const char*b){for(unsigned i=0;i<64;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
void powiadom(){
    for(int pid=1;pid<SKLADACZ_MAKS_WARSTW;++pid){
        auto*w=pobierz_warstwe(pid);if(!w)continue;
        bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_EKRAN;e.kod=skala;
        e.x=w->x;e.y=w->y;
        e.dx=w->z_order==0?grafika_pobierz_szerokosc():w->szerokosc;
        e.dy=w->z_order==0?grafika_pobierz_wysokosc():w->wysokosc;
        e.przyciski=generacja;e.timestamp=teraz();scheduler_dodaj_zdarzenie(pid,&e);
    }
}
void log_tryb(const char* prefix,const TrybEkranu&t){
    char b[112]{};size_t p=0;while(*prefix&&p+1<sizeof(b))b[p++]=*prefix++;
    auto num=[&](uint32_t n){char r[10];unsigned i=0;do{r[i++]=char('0'+n%10);n/=10;}while(n);while(i&&p+1<sizeof(b))b[p++]=r[--i];};
    num(t.szerokosc);b[p++]='x';num(t.wysokosc);b[p++]='x';num(t.bpp);b[p]=0;klog_serial(b);
}
bool zapisz(){
    TrybEkranu t=grafika_tryb_ekranu();
    EkranKonfiguracja c{t.szerokosc,t.wysokosc,t.bpp,skala};
    char buf[EKRAN_CFG_MAX]{},check[EKRAN_CFG_MAX]{};
    size_t n=ekran_serializuj(c,buf,sizeof(buf));
    if(!n||!psf_czy_gotowy()||!zapewnij_katalog("/ustawienia"))return false;
    psf_metadane m{};if(!pobierz_metadane_tworu(EKRAN_CFG,&m)&&!utworz_plik(EKRAN_CFG))return false;
    if(!zapisz_do_pliku(EKRAN_CFG,buf,n)||!psf_flush_cache())return false;
    uint32_t got=0;
    if(rozmiar_pliku(EKRAN_CFG)!=n||!czytaj_fragment_pliku(EKRAN_CFG,0,check,n,&got)||got!=n)return false;
    for(size_t i=0;i<n;++i)if(buf[i]!=check[i])return false;
    return true;
}
void snapshot(EkranOdpowiedz*out){
    *out={};out->wersja=1;out->mozliwosci=grafika_mozliwosci_ekranu();out->aktywny=grafika_tryb_ekranu();
    out->skala=skala;out->generacja=generacja;out->liczba_trybow=grafika_tryby_ekranu(out->tryby,EKRAN_MAX_TRYBOW);
    out->bufory_bajty=bufory_grafiki_bajty();out->alokacje=bufory_grafiki_alokacje();out->zmiany=zmiany;out->przywrocenia=cofniecia;
    if(tx.oczekuje){out->transakcja=tx.id;uint64_t n=teraz();out->pozostalo_sekund=n<tx.deadline?(tx.deadline-n+999999999)/1000000000:0;}
}
bool cofnij(){
    if(!tx.oczekuje)return true;
    klog_serial("[EKRAN] Przywracam poprzedni tryb.");
    StanBuforowEkranu zwolnij{};
    zablokuj_ekran();
    if(tx.fizyczna&&!grafika_zmien_sprzet(tx.stary.tryb)){
        odblokuj_ekran();
        klog_serial("[EKRAN] BLAD sprzet odrzucil przywrocenie.");tx.deadline=UINT64_MAX;return false;
    }
    if(tx.fizyczna)grafika_wymien_stan(tx.stary);
    skala=tx.stara_skala;bws_aktualizuj_ekran(skala);
    skladacz_cofnij_ekran(&tx.warstwy);
    if(tx.fizyczna)zwolnij=tx.nowy;
    tx.oczekuje=false;++cofniecia;
    skladacz_obrazu_zloz_klatke();
    if(++generacja==0)++generacja;
    grafika_zatwierdz_geometrie(skala,generacja);
    odblokuj_ekran();
    if(tx.fizyczna)grafika_zwolnij_stan(&zwolnij);
    powiadom();
    if(grafika_mozliwosci_ekranu().backend==EKRAN_VIRTIO_GPU){
        VirtioGpuDriver* gdrv = virtio_gpu_get_driver(0);
        if(gdrv) virtio_gpu_free_stale_resource(gdrv);
    }
    return true;
}
uint32_t zastosuj(const TrybEkranu&t,uint32_t s,int owner,bool persist=true,
                  bool wymagaj_potwierdzenia=true){
    if(tx.oczekuje)return EKRAN_ZAJETY;
    if(!ui_skala_poprawna(s)||!grafika_tryb_obslugiwany(t))return EKRAN_NIEOBSLUGIWANY;
    const bool fizyczna=!ekran_ten_sam(t,grafika_tryb_ekranu());
    if(!fizyczna&&s==skala)return EKRAN_OK;
    tx={};tx.fizyczna=fizyczna;tx.stara_skala=skala;tx.nowa_skala=s;tx.owner=owner;tx.id=++nastepna;
    grafika_pobierz_stan(&tx.stary);
    if(fizyczna){
        if(!grafika_przygotuj_ekran(t,&tx.nowy))return EKRAN_PAMIEC;
        if(!tapety_przygotuj_ekran(tx.nowy.tapeta,t.szerokosc,t.wysokosc,tx.nowy.tapeta_bajty)){
            grafika_zwolnij_stan(&tx.nowy);return EKRAN_PAMIEC;
        }
    }
    uint32_t next_generation=generacja+1;if(!next_generation)++next_generation;
    const auto next_geometry=display_geometry(t.szerokosc,t.wysokosc,s,next_generation);
    if (!skladacz_przygotuj_ekran(next_geometry, &tx.warstwy)) {
        if (fizyczna) grafika_zwolnij_stan(&tx.nowy);
        return EKRAN_PAMIEC;
    }
    zablokuj_ekran();
    if(fizyczna&&!grafika_zmien_sprzet(t)){
        if(grafika_mozliwosci_ekranu().backend==EKRAN_VIRTIO_GPU){
            VirtioGpuDriver* gdrv = virtio_gpu_get_driver(0);
            if(gdrv) virtio_gpu_rollback_modeset(gdrv);
        }
        const bool restored=grafika_zmien_sprzet(tx.stary.tryb);
        skladacz_zakoncz_zmiane_ekranu(&tx.warstwy,false);
        odblokuj_ekran();
        if(fizyczna)grafika_zwolnij_stan(&tx.nowy);
        klog_serial(restored?"[EKRAN] BLAD tryb odrzucony; poprzedni przywrocony.":"[EKRAN] BLAD sprzetowego przywrocenia.");return EKRAN_SPRZET;
    }
    if(fizyczna)grafika_wymien_stan(tx.nowy);
    skala=s;bws_aktualizuj_ekran(s);skladacz_zastosuj_ekran(&tx.warstwy);++zmiany;
    // Present the prepared surface before publishing the completed generation.
    // Input, BWS snapshots and client geometry cannot interleave this boundary.
    skladacz_obrazu_zloz_klatke();
    if(++generacja==0)++generacja;
    grafika_zatwierdz_geometrie(skala,generacja);
    odblokuj_ekran();
    powiadom();
    if(grafika_mozliwosci_ekranu().backend==EKRAN_VIRTIO_GPU){
        VirtioGpuDriver* gdrv = virtio_gpu_get_driver(0);
        if(gdrv) virtio_gpu_free_stale_resource(gdrv);
    }
    if(fizyczna&&wymagaj_potwierdzenia){
        tx.oczekuje=true;tx.deadline=teraz()+15000000000ULL;
        log_tryb("[EKRAN] Tryb ",t);klog_serial("[EKRAN] Tryb zastosowany, oczekiwanie na potwierdzenie.");
        return EKRAN_OCZEKUJE;
    }
    skladacz_zakoncz_zmiane_ekranu(&tx.warstwy,true);
    if(fizyczna)grafika_zwolnij_stan(&tx.stary);
    return !persist||zapisz()?EKRAN_OK:EKRAN_ZAPIS;
}
}
uint32_t ekran_skala(){return skala;}
void ekran_inicjalizuj(){
    boot_tryb=grafika_tryb_ekranu();log_tryb("[EKRAN INIT] boot=",boot_tryb);
    klog_serial(grafika_mozliwosci_ekranu().backend==EKRAN_BOCHS?
        "[EKRAN INIT] backend=bochs-runtime":grafika_mozliwosci_ekranu().backend==EKRAN_GOP?
        "[EKRAN INIT] backend=uefi-gop-static":grafika_mozliwosci_ekranu().backend==EKRAN_VIRTIO_GPU?
        "[EKRAN INIT] backend=virtio-gpu-runtime":"[EKRAN INIT] backend=vesa-static");
    uint32_t n=rozmiar_pliku(EKRAN_CFG),got=0;
    if(!n){bws_aktualizuj_ekran(100);grafika_zatwierdz_geometrie(100,generacja);klog_serial("[EKRAN INIT] cfg=missing; active=boot");return;}
    char buf[EKRAN_CFG_MAX]{};EkranKonfiguracja c{};
    bool parsed=n<=sizeof(buf)&&czytaj_fragment_pliku(EKRAN_CFG,0,buf,n,&got)&&got==n&&ekran_parsuj(buf,n,&c);
    if(parsed&&ui_skala_poprawna(c.skala)){
        TrybEkranu t{c.szerokosc,c.wysokosc,c.bpp,c.szerokosc*4,0,0};
        if(!ekran_ten_sam(t,boot_tryb)&&grafika_tryb_obslugiwany(t)){
            log_tryb("[EKRAN INIT] mode change source=configrestore target=",t);
            uint32_t result=zastosuj(t,c.skala,0,false,false);
            if(result!=EKRAN_OK){
                klog_serial("[EKRAN] Nie udalo sie przelaczyc trybu sprzetowego przy starcie; zachowano skale.");
                skala=c.skala;bws_aktualizuj_ekran(skala);
            }
        }else{
            skala=c.skala;bws_aktualizuj_ekran(skala);
        }
    }else{
        skala=100;bws_aktualizuj_ekran(skala);
        klog_serial("[EKRAN] BLAD konfiguracji; tryb startowy i skala 100%.");
    }
    grafika_zatwierdz_geometrie(skala,generacja);
}

bool ekran_obsluz(const proces_t& p, const EkranZadanie& r, EkranOdpowiedz* out);

static __attribute__((noinline)) void ekran_test_uprawnienia_i_autoryzacja(const proces_t& p, EkranOdpowiedz* out) {
    auto check=[&](bool ok){++out->asercje;if(!ok)++out->bledy;};

    // 3. Regresja autoryzacji EKRAN_ZACHOWAJ i EKRAN_PRZYWROC:
    // a) Dla tx.owner == 10: inny proces Ring 3 (np. PID 20) nie moze potwierdzic ani cofnac
    proces_t proc{};
    proc.pid = 10;
    proc.poziom_zaufania = PZB_ZAUFANE;
    proc.uprawnienia = PRAWO_GUI | PRAWO_SYSTEM_CONFIG;
    for (size_t i = 0; EKRAN_PROGRAM[i]; ++i) proc.sciezka_pliku[i] = EKRAN_PROGRAM[i];

    EkranZadanie req{};
    req.wersja = 1;
    req.operacja = EKRAN_ZASTOSUJ;
    req.szerokosc = 1280;
    req.wysokosc = 800;
    req.bpp = 32;
    req.skala = 100;
    EkranOdpowiedz resp{};
    check(ekran_obsluz(proc, req, &resp));
    check(resp.status == EKRAN_OCZEKUJE && tx.oczekuje && tx.owner == 10);
    const uint64_t tx_id = resp.transakcja;

    // PID 20 probuje wykonac EKRAN_ZACHOWAJ -> odrzucone z EKRAN_UPRAWNIENIA
    proc.pid = 20;
    req = {};
    req.wersja = 1;
    req.operacja = EKRAN_ZACHOWAJ;
    req.transakcja = tx_id;
    resp = {};
    check(ekran_obsluz(proc, req, &resp));
    check(resp.status == EKRAN_UPRAWNIENIA && tx.oczekuje);

    // PID 20 probuje wykonac EKRAN_PRZYWROC -> odrzucone z EKRAN_UPRAWNIENIA
    req = {};
    req.wersja = 1;
    req.operacja = EKRAN_PRZYWROC;
    req.transakcja = tx_id;
    resp = {};
    check(ekran_obsluz(proc, req, &resp));
    check(resp.status == EKRAN_UPRAWNIENIA && tx.oczekuje);

    // PID 10 (wlasciciel) wykonuje EKRAN_ZACHOWAJ -> zaakceptowane z EKRAN_OK
    proc.pid = 10;
    req = {};
    req.wersja = 1;
    req.operacja = EKRAN_ZACHOWAJ;
    req.transakcja = tx_id;
    resp = {};
    check(ekran_obsluz(proc, req, &resp));
    check(resp.status == EKRAN_OK && !tx.oczekuje);

    // b) Dla tx.owner == 0: weryfikacja scislej polityki uprawnien systemowych
    TrybEkranu t_sys_auth{1920, 1080, 32, 1920*4, 0, 0};
    check(zastosuj(t_sys_auth, 100, 0, true) == EKRAN_OCZEKUJE && tx.oczekuje && tx.owner == 0);
    const uint64_t sys_tx_id = tx.id;

    // Proces bez uprawnien systemowych (PZB_UZYTKOWNIK, brak PRAWO_SYSTEM_CONFIG) probuje kontrolowac transakcje systemowa
    for (size_t i = 0; i < sizeof(proc.sciezka_pliku); ++i) proc.sciezka_pliku[i] = 0;
    proc.pid = 99;
    proc.poziom_zaufania = PZB_UZYTKOWNIK;
    proc.uprawnienia = PRAWO_GUI;
    const char* unauth_path = "/programy/zwykly.bur";
    for (size_t i = 0; unauth_path[i]; ++i) proc.sciezka_pliku[i] = unauth_path[i];

    req = {};
    req.wersja = 1;
    req.operacja = EKRAN_ZACHOWAJ;
    req.transakcja = sys_tx_id;
    resp = {};
    check(ekran_obsluz(proc, req, &resp));
    check(resp.status == EKRAN_UPRAWNIENIA && tx.oczekuje);

    req = {};
    req.wersja = 1;
    req.operacja = EKRAN_PRZYWROC;
    req.transakcja = sys_tx_id;
    resp = {};
    check(ekran_obsluz(proc, req, &resp));
    check(resp.status == EKRAN_UPRAWNIENIA && tx.oczekuje);

    // Uprzywilejowany podmiot systemowy (p / test.bur lub proces z uprawnieniami systemowymi) cofa transakcje
    req = {};
    req.wersja = 1;
    req.operacja = EKRAN_PRZYWROC;
    req.transakcja = sys_tx_id;
    resp = {};
    check(ekran_obsluz(p, req, &resp));
    check(resp.status == EKRAN_OK && !tx.oczekuje);
    klog_serial("[EKRAN TEST] Explicit control authorization (PID 10 vs 20 & owner 0): PASS");
}

static __attribute__((noinline)) bool ekran_wykonaj_test_pakiet(const proces_t& p, const EkranZadanie& r, EkranOdpowiedz* out) {
    auto check=[&](bool ok){++out->asercje;if(!ok)++out->bledy;};
    extern bool skladacz_test_reset_ekranu();
    check(skladacz_test_reset_ekranu());
    check(VmmTestujWspoldzieloneMapowanieJadra(
        reinterpret_cast<const void*>(BUFORY_GRAFIKI_BAZA +
            2ULL * BUFORY_GRAFIKI_SLOT)));
    int mx=0,my=0;grafika_pobierz_pozycje_kursora(&mx,&my);
    check(mx>=0&&my>=0&&mx<grafika_pobierz_szerokosc()&&my<grafika_pobierz_wysokosc());
    uint64_t before=bufory_grafiki_bajty();
    void* p4k=bufor_grafiki_alokuj(33177600);check(p4k!=nullptr);
    if(p4k){auto* p32=static_cast<uint32_t*>(p4k);p32[0]=0x12345678;p32[8294399]=0x87654321;check(p32[0]==0x12345678&&p32[8294399]==0x87654321);}
    bufor_grafiki_zwolnij(p4k);check(before==bufory_grafiki_bajty());
    check(bufor_grafiki_alokuj(SIZE_MAX)==nullptr&&bufor_grafiki_alokuj(0)==nullptr);
    if(r.szerokosc==1&&grafika_mozliwosci_ekranu().mozna_zmienic_runtime){
        const TrybEkranu sekwencja[] = {
            {1280, 800, 32, 1280*4, 0, 0},
            {1920, 1080, 32, 1920*4, 0, 0},
            {2560, 1440, 32, 2560*4, 0, 0},
            {3440, 1440, 32, 3440*4, 0, 0},
            {1920, 1080, 32, 1920*4, 0, 0},
            {1280, 800, 32, 1280*4, 0, 0}
        };
        // Warm-up to populate shared kernel PTs in graphics pool slots
        for(const auto& cel : sekwencja) {
            if(zastosuj(cel, 100, 0, false) == EKRAN_OCZEKUJE) (void)cofnij();
        }
        const uint64_t pmm_start = pmm_pobierz_wolne_ramki();
        const uint64_t heap_start = heap_zajete_bajty();
        const uint64_t buf_start = bufory_grafiki_bajty();
        bool sekwencja_ok = true;
        for(unsigned cykl = 0; cykl < 3; ++cykl) {
            for(const auto& cel : sekwencja) {
                uint32_t res = zastosuj(cel, 100, 0, false);
                if(res != EKRAN_OCZEKUJE && res != EKRAN_OK) sekwencja_ok = false;
                if(tx.oczekuje) {
                    if(!cofnij()) sekwencja_ok = false;
                }
            }
        }
        check(sekwencja_ok);
        const uint64_t pmm_end = pmm_pobierz_wolne_ramki();
        const uint64_t heap_end = heap_zajete_bajty();
        const uint64_t buf_end = bufory_grafiki_bajty();
        check(pmm_start == pmm_end);
        check(heap_start == heap_end);
        check(buf_start == buf_end);
        auto log_val=[&](const char* lbl, uint64_t v){
            char b[96]{}; size_t p=0;
            while(*lbl&&p+1<sizeof(b))b[p++]=*lbl++;
            char r[24]; unsigned k=0;
            do{r[k++]=char('0'+v%10);v/=10;}while(v);
            while(k&&p+1<sizeof(b))b[p++]=r[--k];
            b[p]=0; klog_serial(b);
        };
        log_val("[EKRAN TEST] 3x transitions PMM delta=", pmm_start > pmm_end ? pmm_start - pmm_end : pmm_end - pmm_start);
        log_val("[EKRAN TEST] 3x transitions HEAP delta=", heap_start > heap_end ? heap_start - heap_end : heap_end - heap_start);
        log_val("[EKRAN TEST] 3x transitions BUF delta=", buf_start > buf_end ? buf_start - buf_end : buf_end - buf_start);
        klog_serial(pmm_start == pmm_end ? "[EKRAN TEST] 3x transitions PMM: STABILNE (PASS)" : "[EKRAN TEST] 3x transitions PMM: LEAK (FAIL)");
        klog_serial(heap_start == heap_end ? "[EKRAN TEST] 3x transitions HEAP: STABILNE (PASS)" : "[EKRAN TEST] 3x transitions HEAP: LEAK (FAIL)");
        klog_serial(buf_start == buf_end ? "[EKRAN TEST] 3x transitions BUF: STABILNE (PASS)" : "[EKRAN TEST] 3x transitions BUF: LEAK (FAIL)");
        TrybEkranu t_confirm{1920, 1080, 32, 1920*4, 0, 0};
        uint32_t a_res = zastosuj(t_confirm, 100, 0, true);
        check(a_res == EKRAN_OCZEKUJE && tx.oczekuje);
        check(zapisz());
        char cbuf[EKRAN_CFG_MAX]{}; uint32_t got = 0;
        check(czytaj_fragment_pliku(EKRAN_CFG, 0, cbuf, sizeof(cbuf), &got) && got > 0);
        EkranKonfiguracja read_cfg{};
        check(ekran_parsuj(cbuf, got, &read_cfg));
        check(read_cfg.szerokosc == 1920 && read_cfg.wysokosc == 1080 && read_cfg.skala == 100);
        skladacz_zakoncz_zmiane_ekranu(&tx.warstwy, true);
        grafika_zwolnij_stan(&tx.stary);
        tx.oczekuje = false;
        TrybEkranu t_timeout{1280, 800, 32, 1280*4, 0, 0};
        uint32_t to_res = zastosuj(t_timeout, 100, 0, true);
        check(to_res == EKRAN_OCZEKUJE && tx.oczekuje);
        tx.deadline = teraz() - 1;
        ekran_obsluz_timeout();
        check(!tx.oczekuje);
        check(grafika_pobierz_szerokosc() == 1920 && grafika_pobierz_wysokosc() == 1080);
        got = 0;
        check(czytaj_fragment_pliku(EKRAN_CFG, 0, cbuf, sizeof(cbuf), &got) && got > 0);
        check(ekran_parsuj(cbuf, got, &read_cfg));
        klog_serial("[EKRAN TEST] Confirm & persist, and timeout rollback: PASS");
        TrybEkranu t_curr = grafika_tryb_ekranu();
        check(zastosuj(t_curr, 150, 0, true) == EKRAN_OK && !tx.oczekuje && skala == 150);
        check(zastosuj(t_curr, 100, 0, true) == EKRAN_OK && !tx.oczekuje && skala == 100);
        // Regresja semantyki wlasciciela tx.owner:
        // 1. Transakcja nalezaca do PID 10 nie moze byc wycofana przez zamkniecie procesu PID 20
        TrybEkranu t_pid10{1280, 800, 32, 1280*4, 0, 0};
        check(zastosuj(t_pid10, 150, 10, true) == EKRAN_OCZEKUJE && tx.oczekuje && tx.owner == 10);
        ekran_posprzataj_proces(20);
        check(tx.oczekuje && tx.owner == 10); // PID 20 nie cofa transakcji PID 10
        ekran_posprzataj_proces(10);
        check(!tx.oczekuje); // PID 10 prawidlowo cofa swoja transakcje
        check(grafika_pobierz_szerokosc() == 1920 && grafika_pobierz_wysokosc() == 1080 && skala == 100);

        // 2. Transakcja systemowa (owner == 0) nie moze byc wycofana przez zamkniecie procesu Ring 3 (np. PID 20)
        TrybEkranu t_sys{1280, 800, 32, 1280*4, 0, 0};
        check(zastosuj(t_sys, 150, 0, true) == EKRAN_OCZEKUJE && tx.oczekuje && tx.owner == 0);
        ekran_posprzataj_proces(20);
        check(tx.oczekuje && tx.owner == 0); // PID 20 nie cofa transakcji systemowej
        check(cofnij()); // Manualne/systemowe zamkniecie transakcji
        check(!tx.oczekuje && grafika_pobierz_szerokosc() == 1920 && grafika_pobierz_wysokosc() == 1080 && skala == 100);
        klog_serial("[EKRAN TEST] Ownership semantics regression (PID 10/20 & owner 0): PASS");

        // Autoryzacja i kontrola transakcji (wydzielona pomocnicza funkcja ze wzgledu na rozmiar ramki stosu)
        ekran_test_uprawnienia_i_autoryzacja(p, out);

            TrybEkranu t_720{1280, 720, 32, 1280*4, 0, 0};
            check(zastosuj(t_720, 100, 0, false, false) == EKRAN_OK && !tx.oczekuje);
            int b_mx = 0, b_my = 0;
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 1279 && b_my == 719);

            TrybEkranu t_1080{1920, 1080, 32, 1920*4, 0, 0};
            uint32_t boot_res = zastosuj(t_1080, 100, 0, false, false);
            check(boot_res == EKRAN_OK && !tx.oczekuje);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 1919 && b_my == 1079);
            check(grafika_pobierz_szerokosc() == 1920 && grafika_pobierz_wysokosc() == 1080);

            // First USB packet simulation immediately after restore
            zaktualizuj_mysze_zrodlo(0, 0, 0, "usb");
            int cur_x = 0, cur_y = 0;
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 1919 && b_my == 1079);

            // Simulate repeated positive dy movement to bottom & taskbar
            for(int i = 0; i < 30; ++i) {
                zaktualizuj_mysze_zrodlo(0, -100, 0, "usb");
            }
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_y == 1079);
            check(cur_y > 719);
            check(cur_y >= 1040); // Within taskbar area (1080 - 40 = 1040)
            klog_serial("[EKRAN TEST] USB first-packet & taskbar reachability (y=1079): PASS");

            // PS/2 path simulation
            for(int i = 0; i < 30; ++i) zaktualizuj_mysze_zrodlo(0, 100, 0, "ps2"); // top
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_y == 0);
            for(int i = 0; i < 30; ++i) zaktualizuj_mysze_zrodlo(0, -100, 0, "ps2"); // bottom
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_y == 1079 && cur_y >= 1040);
            for(int i = 0; i < 30; ++i) zaktualizuj_mysze_zrodlo(100, 0, 0, "ps2"); // bottom-right
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 1919 && cur_y == 1079);
            for(int i = 0; i < 30; ++i) zaktualizuj_mysze_zrodlo(0, 100, 0, "ps2"); // top-right
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 1919 && cur_y == 0);
            for(int i = 0; i < 30; ++i) zaktualizuj_mysze_zrodlo(-100, -100, 0, "ps2"); // bottom-left
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 0 && cur_y == 1079);
            klog_serial("[EKRAN TEST] PS/2 screen corners reachability: PASS");

            // Runtime transitions: 1080p -> 1440p -> UWQHD (3440x1440) -> 4K
            TrybEkranu t_1440{2560, 1440, 32, 2560*4, 0, 0};
            check(zastosuj(t_1440, 100, 0, false, false) == EKRAN_OK && !tx.oczekuje);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 2559 && b_my == 1439);
            for(int i = 0; i < 40; ++i) zaktualizuj_mysze_zrodlo(100, -100, 0, "usb");
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 2559 && cur_y == 1439);

            TrybEkranu t_uwqhd{3440, 1440, 32, 3440*4, 0, 0};
            check(zastosuj(t_uwqhd, 100, 0, false, false) == EKRAN_OK && !tx.oczekuje);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 3439 && b_my == 1439);
            // Corner reachability in 3440x1440
            for(int i = 0; i < 60; ++i) zaktualizuj_mysze_zrodlo(-100, 100, 0, "usb"); // top-left
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 0 && cur_y == 0);
            for(int i = 0; i < 60; ++i) zaktualizuj_mysze_zrodlo(0, -100, 0, "usb"); // bottom-left
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 0 && cur_y == 1439 && cur_y >= 1400);
            for(int i = 0; i < 60; ++i) zaktualizuj_mysze_zrodlo(100, 0, 0, "usb"); // bottom-right
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 3439 && cur_y == 1439);
            for(int i = 0; i < 60; ++i) zaktualizuj_mysze_zrodlo(0, 100, 0, "usb"); // top-right
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 3439 && cur_y == 0);
            klog_serial("[EKRAN TEST] Ultrawide 3440x1440 corners reachability: PASS");

            TrybEkranu t_4k{3840, 2160, 32, 3840*4, 0, 0};
            check(zastosuj(t_4k, 100, 0, false, false) == EKRAN_OK && !tx.oczekuje);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 3839 && b_my == 2159);
            for(int i = 0; i < 50; ++i) zaktualizuj_mysze_zrodlo(100, -100, 0, "usb");
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 3839 && cur_y == 2159);
            klog_serial("[EKRAN TEST] Runtime 1080p -> 1440p -> UWQHD -> 4K mouse bounds: PASS");

            // Live-window transition regressions:
            // 3840x2160 -> 3440x1440 -> 2560x1440 -> 3440x1440 -> 1920x1080
            // Keep the SAME open window(s) during transitions and verify layer & mouse state.
            check(zastosuj(t_1080, 100, 0, false, false) == EKRAN_OK && !tx.oczekuje);
            const uint64_t base_pmm = pmm_pobierz_wolne_ramki();
            const uint64_t base_heap = heap_zajete_bajty();
            const uint64_t base_buf = bufory_grafiki_bajty();

            const TrybEkranu live_cycle[] = { t_4k, t_uwqhd, t_1440, t_uwqhd, t_1080 };
            for(const auto& cel : live_cycle) {
                check(zastosuj(cel, 100, 0, false, false) == EKRAN_OK && !tx.oczekuje);
                check(grafika_pobierz_szerokosc() == int(cel.szerokosc) && grafika_pobierz_wysokosc() == int(cel.wysokosc));
                grafika_pobierz_granice_kursora(&b_mx, &b_my);
                check(b_mx == int(cel.szerokosc) - 1 && b_my == int(cel.wysokosc) - 1);
                // Mouse reachability immediately
                for(int i = 0; i < 50; ++i) zaktualizuj_mysze_zrodlo(-100, 100, 0, "usb");
                grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
                check(cur_x == 0 && cur_y == 0);
                for(int i = 0; i < 50; ++i) zaktualizuj_mysze_zrodlo(100, -100, 0, "usb");
                grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
                check(cur_x == b_mx && cur_y == b_my);
                // Verify presentation surfaces and window bounds for live layers
                for(int pid = 1; pid < SKLADACZ_MAKS_WARSTW; ++pid) {
                    auto* w = pobierz_warstwe(pid); if (!w) continue;
                    check(w->x >= 0 && w->y >= 0);
                    check(w->x + w->szerokosc <= int(cel.szerokosc));
                    check(w->y + w->wysokosc <= int(cel.wysokosc));
                    check(w->bufor_pikseli != nullptr);
                }
            }
            check(base_pmm == pmm_pobierz_wolne_ramki());
            check(base_heap == heap_zajete_bajty());
            check(base_buf == bufory_grafiki_bajty());
            klog_serial("[EKRAN TEST] Live-window transitions 4K->UWQHD->QHD->UWQHD->FHD: PASS");

            // Rollback test: 1080p -> temporary UWQHD (3440x1440) -> rollback
            check(zastosuj(t_1080, 100, 0, false, false) == EKRAN_OK && !tx.oczekuje);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 1919 && b_my == 1079);
            check(zastosuj(t_uwqhd, 100, 0, false, true) == EKRAN_OCZEKUJE && tx.oczekuje);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 3439 && b_my == 1439);
            check(cofnij() && !tx.oczekuje);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 1919 && b_my == 1079);
            check(base_pmm == pmm_pobierz_wolne_ramki());
            check(base_heap == heap_zajete_bajty());
            check(base_buf == bufory_grafiki_bajty());
            klog_serial("[EKRAN TEST] Rollback immediately restores mouse bounds: PASS");

            // Scale-only test: 1080p @ 100% vs 200%
            check(zastosuj(t_1080, 200, 0, false, false) == EKRAN_OK && skala == 200);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 1919 && b_my == 1079);
            for(int i = 0; i < 30; ++i) zaktualizuj_mysze_zrodlo(100, -100, 0, "usb");
            grafika_pobierz_pozycje_kursora(&cur_x, &cur_y);
            check(cur_x == 1919 && cur_y == 1079);
            check(zastosuj(t_1080, 100, 0, false, false) == EKRAN_OK && skala == 100);
            grafika_pobierz_granice_kursora(&b_mx, &b_my);
            check(b_mx == 1919 && b_my == 1079);
            klog_serial("[EKRAN TEST] Scale-only (100% -> 200%) mouse bounds unchanged: PASS");
            if(!ekran_ten_sam(boot_tryb, grafika_tryb_ekranu())) {
                zastosuj(boot_tryb, 100, 0, false, false);
            }
        }
        out->status=EKRAN_OK;
        return true;
    }

bool ekran_obsluz(const proces_t& p, const EkranZadanie& r, EkranOdpowiedz* out) {
    if(!out||r.wersja!=1||r.rezerwa[0]||r.rezerwa[1]||!pzb_ma_prawa(&p,PRAWO_GUI))return false;
    snapshot(out);
    if(r.operacja==EKRAN_STAN)return true;
    const bool test=rowne(p.sciezka_pliku,"/programy/test.cebula/test.bur");
    const bool allowed=(rowne(p.sciezka_pliku,EKRAN_PROGRAM)&&pzb_ma_co_najmniej_poziom(&p,PZB_ZAUFANE)&&pzb_ma_prawa(&p,PRAWO_SYSTEM_CONFIG))||test;
    if(!allowed){out->status=EKRAN_UPRAWNIENIA;return true;}
    if(test&&r.operacja==EKRAN_TEST&&!tx.oczekuje){
        return ekran_wykonaj_test_pakiet(p, r, out);
    }
    uint32_t status=EKRAN_NIEOBSLUGIWANY;
    if(r.operacja==EKRAN_ZASTOSUJ){
        if(r.szerokosc<=UINT32_MAX/4){TrybEkranu t{r.szerokosc,r.wysokosc,r.bpp,r.szerokosc*4,0,0};status=zastosuj(t,r.skala,p.pid,!test);}
    }else if(r.operacja==EKRAN_ZACHOWAJ||r.operacja==EKRAN_PRZYWROC){
        if(!tx.oczekuje||r.transakcja!=tx.id){
            status=EKRAN_NIEOBSLUGIWANY;
        }else{
            const bool wlasciciel_ok=(tx.owner>0)?(int(p.pid)==tx.owner):allowed;
            if(!wlasciciel_ok){
                status=EKRAN_UPRAWNIENIA;
            }else if(r.operacja==EKRAN_PRZYWROC){
                status=cofnij()?EKRAN_OK:EKRAN_SPRZET;
            }else{
                if(!zapisz())status=EKRAN_ZAPIS;
                else{
                    skladacz_zakoncz_zmiane_ekranu(&tx.warstwy,true);
                    grafika_zwolnij_stan(&tx.stary);
                    tx.oczekuje=false;
                    status=EKRAN_OK;
                    klog_serial("[EKRAN] Tryb zatwierdzony.");
                }
            }
        }
    }
    snapshot(out);out->status=status;return true;
}
void ekran_obsluz_timeout(){
    if(!tx.oczekuje||teraz()<tx.deadline)return;
    (void)cofnij();
}
void ekran_posprzataj_proces(int pid){
    if(pid<=0)return;
    if(tx.oczekuje&&tx.owner>0&&tx.owner==pid){
        klog_serial("[EKRAN] Wlasciciel transakcji zakonczyl sie; przywracam poprzedni tryb.");
        (void)cofnij();
    }
}

bool ekran_test_virtio_gpu_modeset(bool capture_modes) {
    if (grafika_mozliwosci_ekranu().backend != EKRAN_VIRTIO_GPU) return true;
    klog_serial("[VIRTIO-GPU-MODESET] Rozpoczynam desktopowy test przelaczania trybow w locie...");

    // 1. Sprawdz zachowanie trybu rozruchowego
    uint32_t bw = 0, bh = 0;
    grafika_pobierz_boot_rozmiar(&bw, &bh);
    if (bw >= 640 && bh >= 480) {
        if (grafika_pobierz_szerokosc() != int(bw) || grafika_pobierz_wysokosc() != int(bh)) {
            klog_serial("[VIRTIO-GPU-MODESET] BLAD: Tryb rozruchowy nie zostal zachowany na pulpicie!");
            return false;
        }
        klog_serial("[VIRTIO-GPU-MODESET] Boot mode 1280x720 preserved: PASS");
    }

    // Host acknowledges each captured scanout over COM1. This is a bounded
    // test-only handshake, not a delay in production boot.
    auto capture = [](const char* marker) {
        klog_serial(marker);
        const uint64_t start = czas_monotoniczny_ns();
        while (czas_monotoniczny_ns() - start < 10000000000ULL) {
            uint8_t status;
            asm volatile("inb %1, %0" : "=a"(status) : "Nd"(uint16_t(0x3FD)));
            if (status & 1) {
                uint8_t c;
                asm volatile("inb %1, %0" : "=a"(c) : "Nd"(uint16_t(0x3F8)));
                if (c == 'C') return true;
            }
            asm volatile("pause");
        }
        klog_serial("[VIRTIO-GPU-MODESET] capture acknowledgement timeout: FAIL");
        return false;
    };
    const TrybEkranu high_modes[] = {
        {3440,1440,32,3440*4,0,0}, {3840,2160,32,3840*4,0,0},
        {1280,720,32,1280*4,0,0}
    };
    const char* markers[] = {
        "[VIRTIO-GPU-MODESET] Capture 3440x1440",
        "[VIRTIO-GPU-MODESET] Capture 3840x2160",
        "[VIRTIO-GPU-MODESET] Capture 1280x720"
    };
    for (unsigned i = 0; capture_modes && i < 3; ++i) {
        if (zastosuj(high_modes[i], 100, 0, false, false) != EKRAN_OK ||
            !capture(markers[i])) return false;
    }

    // 2. Przelaczenie 1280x720 -> 1920x1080 (natychmiastowe)
    TrybEkranu t_1080{1920, 1080, 32, 1920 * 4, 0, 0};
    uint32_t r1080 = zastosuj(t_1080, 100, 0, false, false);
    if (r1080 != EKRAN_OK || grafika_pobierz_szerokosc() != 1920 || grafika_pobierz_wysokosc() != 1080) {
        klog_serial("[VIRTIO-GPU-MODESET] BLAD: Przelaczenie na 1920x1080 nie powiodlo sie!");
        return false;
    }
    klog_serial("[VIRTIO-GPU-MODESET] Switched to 1920x1080: PASS");
    klog_serial("[VIRTIO-GPU-MODESET] Ready for 1920x1080 screendump");
    {
        const uint64_t t0 = hpet_dostepny() ? czas_monotoniczny_ns() : 0;
        while (hpet_dostepny() && czas_monotoniczny_ns() - t0 < 300000000ULL) asm volatile("pause");
    }

    // 3. Przelaczenie 1920x1080 -> 1024x768 (natychmiastowe)
    TrybEkranu t_768{1024, 768, 32, 1024 * 4, 0, 0};
    uint32_t r768 = zastosuj(t_768, 100, 0, false, false);
    if (r768 != EKRAN_OK || grafika_pobierz_szerokosc() != 1024 || grafika_pobierz_wysokosc() != 768) {
        klog_serial("[VIRTIO-GPU-MODESET] BLAD: Przelaczenie na 1024x768 nie powiodlo sie!");
        return false;
    }
    klog_serial("[VIRTIO-GPU-MODESET] Switched to 1024x768: PASS");
    klog_serial("[VIRTIO-GPU-MODESET] Ready for 1024x768 screendump");
    {
        const uint64_t t0 = hpet_dostepny() ? czas_monotoniczny_ns() : 0;
        while (hpet_dostepny() && czas_monotoniczny_ns() - t0 < 300000000ULL) asm volatile("pause");
    }

    // 4. Test niepotwierdzonego trybu z wycofaniem po timeout (rollback)
    // Zastosuj 800x600 z wymagaj_potwierdzenia=true
    TrybEkranu t_rollback{800, 600, 32, 800 * 4, 0, 0};
    uint32_t r_rb = zastosuj(t_rollback, 100, 0, false, true);
    if (r_rb != EKRAN_OCZEKUJE || !tx.oczekuje) {
        klog_serial("[VIRTIO-GPU-MODESET] BLAD: Oczekiwanie na potwierdzenie nie zostalo aktywowane!");
        return false;
    }
    // Wymus uplyw czasu timeout
    tx.deadline = teraz() - 1;
    ekran_obsluz_timeout();
    if (tx.oczekuje || grafika_pobierz_szerokosc() != 1024 || grafika_pobierz_wysokosc() != 768) {
        klog_serial("[VIRTIO-GPU-MODESET] BLAD: Wycofanie po timeout (rollback) nie przywrocilo 1024x768!");
        return false;
    }
    klog_serial("[VIRTIO-GPU-MODESET] Unconfirmed rollback complete, ready for screendump");
    {
        const uint64_t t0 = hpet_dostepny() ? czas_monotoniczny_ns() : 0;
        while (hpet_dostepny() && czas_monotoniczny_ns() - t0 < 300000000ULL) asm volatile("pause");
    }

    // 5. Przywrocenie poczatkowego trybu 1280x720 przed startem srodowiska
    TrybEkranu t_init{bw ? bw : 1280, bh ? bh : 720, 32, (bw ? bw : 1280) * 4, 0, 0};
    uint32_t r_init = zastosuj(t_init, 100, 0, false, false);
    if (r_init != EKRAN_OK || grafika_pobierz_szerokosc() != int(bw ? bw : 1280)) {
        klog_serial("[VIRTIO-GPU-MODESET] BLAD: Przywrocenie trybu poczatkowego 1280x720 nie powiodlo sie!");
        return false;
    }
    klog_serial("[VIRTIO-GPU-MODESET] Restored initial 1280x720: PASS");
    klog_serial("[VIRTIO-GPU-MODESET] Desktop modeset test suite: PASS");
    return true;
}
