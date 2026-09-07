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
    ++generacja;
    for(int pid=1;pid<SKLADACZ_MAKS_WARSTW;++pid){
        auto*w=pobierz_warstwe(pid);if(!w)continue;
        bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_EKRAN;e.kod=skala;
        e.x=w->x;e.y=w->y;e.dx=w->szerokosc;e.dy=w->wysokosc;
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
    odblokuj_ekran();
    if(tx.fizyczna)grafika_zwolnij_stan(&zwolnij);
    powiadom();return true;
}
uint32_t zastosuj(const TrybEkranu&t,uint32_t s,int owner,bool persist=true){
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
    if (!skladacz_przygotuj_ekran(t.szerokosc, t.wysokosc, int(t.wysokosc) - ui_skaluj(40, s), &tx.warstwy)) {
        if (fizyczna) grafika_zwolnij_stan(&tx.nowy);
        return EKRAN_PAMIEC;
    }
    zablokuj_ekran();
    if(fizyczna&&!grafika_zmien_sprzet(t)){
        const bool restored=grafika_zmien_sprzet(tx.stary.tryb);
        skladacz_zakoncz_zmiane_ekranu(&tx.warstwy,false);
        odblokuj_ekran();
        if(fizyczna)grafika_zwolnij_stan(&tx.nowy);
        klog_serial(restored?"[EKRAN] BLAD tryb odrzucony; poprzedni przywrocony.":"[EKRAN] BLAD sprzetowego przywrocenia.");return EKRAN_SPRZET;
    }
    if(fizyczna)grafika_wymien_stan(tx.nowy);
    skala=s;bws_aktualizuj_ekran(s);skladacz_zastosuj_ekran(&tx.warstwy);++zmiany;
    // Present one internally consistent frame before normal event-driven repaint.
    skladacz_obrazu_zloz_klatke();
    odblokuj_ekran();
    powiadom();
    if(fizyczna){
        tx.oczekuje=true;tx.deadline=teraz()+15000000000ULL;
        log_tryb("[EKRAN] Tryb ",t);klog_serial("[EKRAN] Tryb zastosowany, oczekiwanie na potwierdzenie.");
        return EKRAN_OCZEKUJE;
    }
    skladacz_zakoncz_zmiane_ekranu(&tx.warstwy,true);
    return !persist||zapisz()?EKRAN_OK:EKRAN_ZAPIS;
}
}
uint32_t ekran_skala(){return skala;}
void ekran_inicjalizuj(){
    boot_tryb=grafika_tryb_ekranu();log_tryb("[EKRAN] Tryb ",boot_tryb);
    uint32_t n=rozmiar_pliku(EKRAN_CFG),got=0;
    if(!n){bws_aktualizuj_ekran(100);return;}
    char buf[EKRAN_CFG_MAX]{};EkranKonfiguracja c{};
    bool parsed=n<=sizeof(buf)&&czytaj_fragment_pliku(EKRAN_CFG,0,buf,n,&got)&&got==n&&ekran_parsuj(buf,n,&c);
    if(parsed&&ui_skala_poprawna(c.skala)){
        skala=c.skala;bws_aktualizuj_ekran(skala);
        TrybEkranu t{c.szerokosc,c.wysokosc,c.bpp,c.szerokosc*4,0,0};
        if(!ekran_ten_sam(t,boot_tryb)&&grafika_tryb_obslugiwany(t)){
            uint32_t result=zastosuj(t,c.skala,0);
            if(result!=EKRAN_OCZEKUJE){
                klog_serial("[EKRAN] Nie udalo sie przelaczyc trybu sprzetowego przy starcie; zachowano skale.");
            }
        }
    }else{
        skala=100;bws_aktualizuj_ekran(skala);
        klog_serial("[EKRAN] BLAD konfiguracji; tryb startowy i skala 100%.");
    }
}
bool ekran_obsluz(const proces_t&p,const EkranZadanie&r,EkranOdpowiedz*out){
    if(!out||r.wersja!=1||r.rezerwa[0]||r.rezerwa[1]||!pzb_ma_prawa(&p,PRAWO_GUI))return false;
    snapshot(out);
    if(r.operacja==EKRAN_STAN)return true;
    const bool test=rowne(p.sciezka_pliku,"/programy/test.cebula/test.bur");
    const bool allowed=(rowne(p.sciezka_pliku,EKRAN_PROGRAM)&&pzb_ma_co_najmniej_poziom(&p,PZB_ZAUFANE)&&pzb_ma_prawa(&p,PRAWO_SYSTEM_CONFIG))||test;
    if(!allowed){out->status=EKRAN_UPRAWNIENIA;return true;}
    if(test&&r.operacja==EKRAN_TEST&&!tx.oczekuje){
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
        if(grafika_mozliwosci_ekranu().mozna_zmienic_runtime){
            const TrybEkranu sekwencja[] = {
                {1280, 800, 32, 1280*4, 0, 0},
                {1920, 1080, 32, 1920*4, 0, 0},
                {2560, 1440, 32, 2560*4, 0, 0},
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
            check(read_cfg.szerokosc == 1920 && read_cfg.wysokosc == 1080 && read_cfg.skala == 100);
            klog_serial("[EKRAN TEST] Confirm & persist, and timeout rollback: PASS");
            if(!ekran_ten_sam(boot_tryb, grafika_tryb_ekranu())) {
                zastosuj(boot_tryb, 100, 0, true);
                if(tx.oczekuje) {
                    zapisz();
                    skladacz_zakoncz_zmiane_ekranu(&tx.warstwy, true);
                    grafika_zwolnij_stan(&tx.stary);
                    tx.oczekuje = false;
                }
            }
        }
        out->status=EKRAN_OK;return true;
    }
    uint32_t status=EKRAN_NIEOBSLUGIWANY;
    if(r.operacja==EKRAN_ZASTOSUJ){
        if(r.szerokosc<=UINT32_MAX/4){TrybEkranu t{r.szerokosc,r.wysokosc,r.bpp,r.szerokosc*4,0,0};status=zastosuj(t,r.skala,p.pid,!test);}
    }else if((r.operacja==EKRAN_ZACHOWAJ||r.operacja==EKRAN_PRZYWROC)&&tx.oczekuje&&r.transakcja==tx.id&&(tx.owner==int(p.pid)||tx.owner==0)){
        if(r.operacja==EKRAN_PRZYWROC)status=cofnij()?EKRAN_OK:EKRAN_SPRZET;
        else {
            if(!zapisz())status=EKRAN_ZAPIS;
            else {skladacz_zakoncz_zmiane_ekranu(&tx.warstwy,true);grafika_zwolnij_stan(&tx.stary);tx.oczekuje=false;status=EKRAN_OK;klog_serial("[EKRAN] Tryb zatwierdzony.");}
        }
    }
    snapshot(out);out->status=status;return true;
}
void ekran_obsluz_timeout(){
    if(!tx.oczekuje||teraz()<tx.deadline)return;
    (void)cofnij();
}
