#include "hda_host_model.h"
#include "../sterowniki/dzwiek/hda_quirks.h"
#include "../sterowniki/dzwiek/hda_diag.h"
#include "../sterowniki/dzwiek/hda_wewnetrzny.h"
#include <chrono>
#include <memory>
#include <time.h>

static unsigned checks=0,scenarios=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static HdaHostModel* active;
void* kmalloc(size_t n){return std::malloc(n);}
void kfree(void* p){std::free(p);}
bool hpet_dostepny(){return active&&active->clock_ok;}
uint64_t czas_monotoniczny_ns(){return HdaHostModel::clock(active);}
void hda_powiadom_reset_kontrolera(uint32_t k){if(active&&k<active->devices.size())++active->devices[k].reset_notifications;}
bool hda_wew_pobierz_stan_strumienia(HdaWewStanStrumienia* out){*out={};return true;}
void hda_wew_pobierz_stan_tonu(HdaWewStanTonu* out){*out={};}
void hda_wew_pobierz_sciezke_testu(HdaWewSciezkaTestu* out){*out={0,2,3,0};}
uint32_t hda_wew_kontroler_odtwarzania(){return 0;}
bool hda_wew_test_start(uint32_t,uint32_t,uint32_t,uint8_t){return false;}
void hda_wew_test_stop(){}
bool hda_wew_test_tick(uint64_t){return false;}
bool hda_wew_wyslij_immediate(uint32_t,uint32_t*){std::abort();} // BWS scanner must NEVER reach legacy polling.
uint32_t hda_wew_verb12(uint8_t c,uint8_t n,uint16_t v,uint8_t p){uint32_t cmd=0;hda_zbuduj_verb12(c,n,v,p,&cmd);return cmd;}
uint32_t hda_wew_verb4(uint8_t c,uint8_t n,uint8_t v,uint16_t p){uint32_t cmd=0;hda_zbuduj_verb4(c,n,v,p,&cmd);return cmd;}
struct Fixture {
    HdaHostModel m;
    Fixture(){hda_transport_host_reset();active=&m;}
    ~Fixture(){hda_transport_host_reset();active=nullptr;}
};
static HdaDiagKontroler info(uint32_t k=0){HdaDiagKontroler c{};CHECK(hda_transport_stan(k,&c));return c;}
static uint32_t command(uint8_t cad=0,uint8_t nid=0,uint8_t param=0){uint32_t c=0;CHECK(hda_zbuduj_get_param(cad,nid,param,&c));return c;}
static HdaTransportStatus complete(HdaHostModel& m,uint32_t k=0,uint32_t owner=1,uint32_t* out=nullptr){
    HdaTransportStatus rc=HDA_TRANSPORT_PENDING;
    for(unsigned j=0;j<128;++j){rc=hda_transport_krok(k,owner);if(rc!=HDA_TRANSPORT_PENDING)break;m.ns+=1000;}
    uint32_t r=0;const auto result=hda_transport_odbierz(k,owner,&r);if(out)*out=r;CHECK(result==rc);return rc;
}
static void recover(HdaHostModel& m,uint32_t k=0){
    for(unsigned j=0;j<128&&!info(k).gotowy;++j){hda_transport_odnow(k);m.ns+=100000;}
    CHECK(info(k).gotowy);CHECK(info(k).transport==2);CHECK(info(k).corb_run&&info(k).rirb_run);
}
static void discovery(){
    {++scenarios;Fixture f;f.m.add();f.m.add(0x10de,0xfb9,1,0,1);f.m.add(0xbeef,0xcafe,2,4,3);f.m.init_all();
        CHECK(hda_transport_liczba()==3);CHECK(info().device_id==0x2668);CHECK(info(1).bus==1);CHECK(info(1).func==1);CHECK(info(2).vendor_id==0xbeef);
        for(unsigned k=0;k<3;++k){CHECK(info(k).gotowy);CHECK((f.m.devices[k].command&0x106)==0x106);CHECK(info(k).initial_statests==1);CHECK(info(k).statests==0);CHECK(info(k).discovered_codec_mask==1);}
    }
    {++scenarios;Fixture f;f.m.add().class_revision=0x04010000;f.m.add(0x1234,0x5678,1).class_revision=0x03000000;f.m.init_all();CHECK(!hda_transport_liczba());}
    for(uint32_t lo:{0U,1U,0xffffffffU,0x10000002U,0x10000008U,0x10000010U}){
        ++scenarios;Fixture f;f.m.add().bar_lo=lo;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_BAR);CHECK(!info().gotowy);CHECK(!f.m.map_calls);
    }
    {++scenarios;Fixture f;auto& d=f.m.add(0x8086,0xa348,0,31,3);d.bar_lo=0x20000004;d.bar_hi=1;f.m.init_all();CHECK(info().bar0==0x120000000ULL);CHECK(info().bar_typ==64);CHECK(f.m.mappings[0][1]==0x120000000ULL);CHECK(info().gotowy);}
    {++scenarios;Fixture f;f.m.add().map_fails=true;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_MAPOWANIA);}
    {++scenarios;Fixture f;f.m.add().memory_fails=true;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_PCI_MEMORY);}
    {++scenarios;Fixture f;f.m.add().master_fails=true;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_PCI_MASTER);}
    {++scenarios;Fixture f;f.m.add().reset_zero_fails=true;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_RESET_ZERO);}
    {++scenarios;Fixture f;auto& d=f.m.add();d.reset_one_fails=true;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_RESET_JEDEN);}
    {++scenarios;Fixture f;f.m.add().set(3,1,2);f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_WERSJI);}
    {++scenarios;Fixture f;f.m.add().set(0,2,0);f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_GCAP);}
    {++scenarios;Fixture f;f.m.add();f.m.clock_ok=false;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_ZEGARA);}
    {++scenarios;Fixture f;for(unsigned j=0;j<6;++j)f.m.add(0x1234,0x5678,0,j);f.m.init_all();CHECK(hda_transport_liczba()==4);CHECK(hda_transport_rejestr_obciety());}
}
static void immediate(){
    ++scenarios;Fixture f;f.m.add();f.m.init_all();CHECK(hda_transport_host_immediate(0));
    auto& d=f.m.devices[0];d.set(0x68,2,2);d.set(0x64,4,0xdeadbeef);d.response_delay_ns=100000;
    CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
    CHECK(hda_transport_zglos(0,3,command())==HDA_TRANSPORT_BUSY);
    CHECK(hda_transport_krok(0,3)==HDA_TRANSPORT_BUSY);
    for(unsigned j=0;j<4;++j)CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);
    CHECK(d.verbs==1);CHECK(!(d.get(0x68,2)&2));
    for(unsigned j=0;j<100;++j){const auto reads=d.mmio_reads;CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);CHECK(d.mmio_reads-reads<=2);}
    f.m.ns+=100000;uint32_t r=0;CHECK(complete(f.m,0,1,&r)==HDA_TRANSPORT_SUCCESS);CHECK(r==0x1af40012);
    ++scenarios;d.set(0x68,2,1);CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
    CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);
    d.set(0x68,2,0);CHECK(complete(f.m)==HDA_TRANSPORT_SUCCESS);
    ++scenarios;d.irv_delay_ns=100000;CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
    for(unsigned j=0;j<6;++j){(void)hda_transport_krok(0,1);f.m.ns+=100000;}
    CHECK(complete(f.m)==HDA_TRANSPORT_SUCCESS);d.irv_delay_ns=0;
    ++scenarios;d.response_delay_ns=HDA_CZAS_VERBA_NS*2;CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
    for(unsigned j=0;j<4;++j)(void)hda_transport_krok(0,1);
    f.m.ns+=HDA_CZAS_VERBA_NS+1;CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_TIMEOUT);
    CHECK(hda_transport_odbierz(0,1,&r)==HDA_TRANSPORT_TIMEOUT);
    recover(f.m);CHECK(hda_transport_host_immediate(0));
    ++scenarios;d.response_delay_ns=10000;CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
    for(unsigned j=0;j<4;++j)(void)hda_transport_krok(0,1);
    hda_transport_anuluj(0,1);CHECK(hda_transport_zglos(0,1,command())!=HDA_TRANSPORT_PENDING);
    recover(f.m);
    CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);CHECK(complete(f.m)==HDA_TRANSPORT_SUCCESS);
}
static void qemu_regression(){
    ++scenarios;Fixture f;auto& d=f.m.add(0x8086,0x2668,0,2,0);
    d.set(0x0e,2,1);d.set(0x4e,1,0x40);d.set(0x5e,1,0x40);
    CHECK(!hda_pobierz_quirki_kontrolera(d.vendor,d.device).corbrp_self_clear);
    f.m.init_all();const auto c=info();
    CHECK(c.inicjalizacja==HDA_INIT_GOTOWY);CHECK(c.gotowy);CHECK(c.status_transportu==HDA_TRANSPORT_SUCCESS);
    CHECK(c.initial_statests==1&&c.discovered_codec_mask==1);CHECK(c.statests==0);
    CHECK(c.corb_rozmiar==256&&c.rirb_rozmiar==256);CHECK(c.corb_run&&c.rirb_run);CHECK(c.transport==2);
    CHECK(c.corb_ctl_before_start==0&&c.rirb_ctl_before_start==0);
    CHECK(c.corb_ctl_after_start==2&&c.rirb_ctl_after_start==3);CHECK(d.corbrp_set_reads>0);
    CHECK(d.get(0x20,4)==0);CHECK(d.get(0x5a,2)==1);CHECK(d.get(0x5c,1)==3);
    // Two commands catch the QEMU RINTCNT stall that a single verb misses.
    for(unsigned j=0;j<2;++j){uint32_t r=0;CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
        f.m.ns+=100000;CHECK(complete(f.m,0,1,&r)==HDA_TRANSPORT_SUCCESS);CHECK(r==0x1af40012);CHECK(d.rirb_count==0);}
    CHECK(hda_diag_inicjalizuj());CHECK(hda_diag_startuj_skan());
    for(unsigned j=0;j<30000;++j){const bool done=hda_diag_krok_skanu();f.m.ns+=100000;if(done)break;}
    auto snap=std::make_unique<HdaDiagSnapshot>();uint32_t bytes=0,generation=0;
    CHECK(hda_diag_pobierz_snapshot(snap.get(),sizeof(*snap),&bytes,&generation));
    CHECK(snap->kontrolery[0].liczba_kodekow==1);CHECK(snap->kodeki[0][0].cad==0);
    CHECK(snap->kodeki[0][0].vendor_id==0x1af4&&snap->kodeki[0][0].device_id==0x12);
    CHECK(info().gotowy&&info().corb_run&&info().rirb_run);CHECK(d.ic_commands==0);
    // Reproduce the old poison: QEMU keeps ICB=1 for an absent codec before
    // test availability is established. Recovery must reset and restart rings.
    ++scenarios;CHECK(!info().test_dostepny);CHECK(hda_transport_host_immediate(0));
    CHECK(hda_transport_zglos(0,1,command(1))==HDA_TRANSPORT_PENDING);
    for(unsigned j=0;j<4;++j)(void)hda_transport_krok(0,1);
    f.m.ns+=HDA_CZAS_VERBA_NS+1;CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_TIMEOUT);
    uint32_t r=0;CHECK(hda_transport_odbierz(0,1,&r)==HDA_TRANSPORT_TIMEOUT);CHECK(d.get(0x68,2)&1);
    CHECK(!hda_transport_host_immediate(0));CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_NOT_READY);
    const auto allocated=f.m.allocated.size();recover(f.m);CHECK(!(d.get(0x68,2)&1));CHECK(f.m.allocated.size()==allocated);
    CHECK(d.reset_notifications==2);CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
    CHECK(complete(f.m,0,1,&r)==HDA_TRANSPORT_SUCCESS);CHECK(r==0x1af40012);
    ++scenarios;CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);d.set(8,4,0);
    CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_RESET_REQUIRED);
    CHECK(hda_transport_odbierz(0,1,&r)==HDA_TRANSPORT_RESET_REQUIRED);
    CHECK(!hda_transport_host_immediate(0));recover(f.m);CHECK(d.reset_notifications==3);
}
static void controller_quirk_ordering(){
    for(bool nvidia_first:{true,false}){
        ++scenarios;Fixture f;
        for(unsigned k=0;k<2;++k){const bool nvidia=(k==0)==nvidia_first;
            f.m.add(nvidia?0x10de:0x8086,nvidia?0xfb9:0x2668,0,uint8_t(2+k));}
        f.m.init_all();
        for(unsigned k=0;k<2;++k){const auto& d=f.m.devices[k];
            CHECK(info(k).gotowy&&info(k).corb_run&&info(k).rirb_run);
            CHECK(hda_pobierz_quirki_kontrolera(d.vendor,d.device).corbrp_self_clear==(d.vendor==0x10de));
            CHECK((d.corbrp_set_reads>0)==(d.vendor==0x8086));
            uint32_t r=0;CHECK(hda_transport_zglos(k,1,command())==HDA_TRANSPORT_PENDING);
            CHECK(complete(f.m,k,1,&r)==HDA_TRANSPORT_SUCCESS);CHECK(r==0x1af40012);
        }
    }
}
static void rings(){
    for(uint8_t caps:{uint8_t(0x10),uint8_t(0x20),uint8_t(0x40),uint8_t(0x70)}){
        ++scenarios;Fixture f;auto& d=f.m.add(0x8086,0xa348);d.set(0x4e,1,caps);d.set(0x5e,1,caps);f.m.init_all();
        const auto c=info();CHECK(c.gotowy);CHECK(c.transport==2);CHECK(c.corb_rozmiar==(caps&0x40?256:caps&0x20?16:2));CHECK(c.rirb_rozmiar==c.corb_rozmiar);
        CHECK(c.corb_run&&c.rirb_run);CHECK(!d.get(0x4a,2));CHECK(!d.get(0x58,2));CHECK(d.get(0x40,4)==0x200000);CHECK(d.get(0x50,4)==0x201000);
        for(unsigned j=0;j<unsigned(c.corb_rozmiar)+3;++j){uint32_t r=0;CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);CHECK(complete(f.m,0,1,&r)==HDA_TRANSPORT_SUCCESS);CHECK(r==0x1af40012);}
    }
    for(bool corb:{true,false}){++scenarios;Fixture f;auto& d=f.m.add(0x8086,0xa348);d.set(corb?0x4e:0x5e,1,0);f.m.init_all();CHECK(info().etap_bledu==(corb?HDA_BLAD_CORB_ROZMIAR:HDA_BLAD_RIRB_ROZMIAR));}
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348);f.m.next_pa=0x120000000ULL;f.m.init_all();CHECK(info().gotowy);CHECK(f.m.devices[0].get(0x44,4)==1);CHECK(f.m.devices[0].get(0x54,4)==1);}
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348).set(0,2,0x4400);f.m.init_all();CHECK(info().gotowy);CHECK(f.m.devices[0].get(0x44,4)==0);}
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348).set(0,2,0x4400);f.m.force_high32=true;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_CORB_DMA);CHECK(f.m.allocated.empty());}
    for(bool corb:{true,false}){++scenarios;Fixture f;auto& d=f.m.add(0x8086,0xa348);d.corb_reset_fails=corb;d.rirb_reset_fails=!corb;d.set(0x58,2,9);f.m.init_all();CHECK(info().etap_bledu==(corb?HDA_BLAD_CORB_RESET:HDA_BLAD_RIRB_RESET));}
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348).start_fails=true;f.m.init_all();CHECK(info().etap_bledu==HDA_BLAD_RING_START);}
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348);f.m.init_all();auto& d=f.m.devices[0];
        CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
        CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);
        d.set(0x4a,2,1); // next WP=1 equals RP => full
        CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);f.m.ns+=HDA_CZAS_VERBA_NS+1;
        CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_CORB_FULL);uint32_t r;CHECK(hda_transport_odbierz(0,1,&r)==HDA_TRANSPORT_CORB_FULL);
        for(unsigned j=0;j<64;++j){hda_transport_odnow(0);f.m.ns+=100000;}CHECK(info().gotowy);
        CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);CHECK(complete(f.m)==HDA_TRANSPORT_SUCCESS);
    }
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348);f.m.init_all();
        CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
        for(unsigned j=0;j<3;++j)(void)hda_transport_krok(0,1);
        f.m.rirb(0,0xdeadcafe,0x10);f.m.rirb(0,0xbadbad,1);f.m.ns+=20000000;
        uint32_t r=0;CHECK(complete(f.m,0,1,&r)==HDA_TRANSPORT_SUCCESS);CHECK(r==0x1af40012);CHECK(info().odpowiedzi_niezamowione==1);CHECK(info().odpowiedzi_obce==1);
    }
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348);f.m.init_all();auto& d=f.m.devices[0];d.no_response=true;
        CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);for(unsigned j=0;j<3;++j)(void)hda_transport_krok(0,1);
        f.m.ns+=HDA_CZAS_VERBA_NS+1;CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_RIRB_NO_RESPONSE);
        uint32_t r;CHECK(hda_transport_odbierz(0,1,&r)==HDA_TRANSPORT_RIRB_NO_RESPONSE);
        for(unsigned j=0;j<64;++j){hda_transport_odnow(0);f.m.ns+=100000;}CHECK(info().gotowy);d.no_response=false;
        CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);CHECK(complete(f.m)==HDA_TRANSPORT_SUCCESS);
    }
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348);f.m.init_all();CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
        f.m.devices[0].set(0x58,2,255);f.m.devices[0].set(0x5d,1,4);CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_PENDING);
        CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_MALFORMED);
    }
    {++scenarios;Fixture f;f.m.add(0x8086,0xa348);f.m.init_all();CHECK(hda_transport_zglos(0,1,command())==HDA_TRANSPORT_PENDING);
        f.m.devices[0].set(8,4,0);CHECK(hda_transport_krok(0,1)==HDA_TRANSPORT_RESET_REQUIRED);CHECK(info().etap_bledu==HDA_BLAD_RESET_W_TRAKCIE);
    }
}
static void scan(){
    ++scenarios;Fixture f;f.m.add();f.m.add(0x8086,0xa348,0,31,3).codecs[0]=HdaHostModel::Codec::laptop(64,true);
    f.m.init_all();hda_transport_test_dostepny(0,true);CHECK(hda_diag_inicjalizuj());CHECK(hda_diag_startuj_skan());
    auto snap=std::make_unique<HdaDiagSnapshot>();uint32_t bytes=0,gen=0;
    CHECK(!hda_diag_pobierz_snapshot(snap.get(),sizeof(*snap),&bytes,&gen));
    uint64_t wall_max=0,cpu_max=0;unsigned turns=0,max_reads=0,max_writes=0;
    for(unsigned j=0;j<30000;++j){
        unsigned before=0,reads=0,writes=0;for(auto& d:f.m.devices){before+=d.verbs;reads+=d.mmio_reads;writes+=d.mmio_writes;}
        timespec cpu_start{},cpu_end{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_start);
        const auto start=std::chrono::steady_clock::now();const bool done=hda_diag_krok_skanu();
        clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_end);
        cpu_max=std::max(cpu_max,uint64_t((cpu_end.tv_sec-cpu_start.tv_sec)*1000000000LL+cpu_end.tv_nsec-cpu_start.tv_nsec));
        const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());wall_max=std::max(wall_max,elapsed);
        unsigned after=0,reads_after=0,writes_after=0;for(auto& d:f.m.devices){after+=d.verbs;reads_after+=d.mmio_reads;writes_after+=d.mmio_writes;}CHECK(after-before<=1);
        // Recovery start also captures four explicit control-register readbacks.
        max_reads=std::max(max_reads,reads_after-reads);max_writes=std::max(max_writes,writes_after-writes);CHECK(reads_after-reads<=12);CHECK(writes_after-writes<=8);
        ++turns;f.m.ns+=100000;if(done)break;
    }
    HdaDiagSkan state{};hda_diag_pobierz_stan(&state);CHECK(!state.aktywny);CHECK(state.gotowy);CHECK(turns>1000&&turns<30000);
    CHECK(hda_diag_pobierz_snapshot(snap.get(),sizeof(*snap),&bytes,&gen));CHECK(gen==1);CHECK(snap->liczba_kontrolerow==2);
    CHECK(snap->kodeki[0][0].vendor_id==0x1af4);CHECK(snap->kodeki[0][0].device_id==0x12);
    CHECK(snap->fg[0][0][0].nid==1);CHECK(snap->widgety[0][0][0][0].nid==2);CHECK(snap->widgety[0][0][0][1].nid==3);
    CHECK(snap->widgety[1][0][0][1].power_current==3);CHECK(snap->kodeki[1][0].vendor_id==0xface);CHECK(snap->kodeki[1][0].liczba_fg==2);CHECK(snap->fg[1][0][0].widget_count==64);
    bool qemu=false,speaker=false,hp=false,hdmi=false;
    for(unsigned j=0;j<snap->liczba_sciezek;++j){const auto& p=snap->sciezki[j];
        if(p.kontroler_idx==0){qemu=true;CHECK(p.konwerter_nid==2&&p.pin_nid==3&&p.cel==HDA_CEL_WYJSCIE_LINIOWE);}
        else{speaker|=p.cel==HDA_CEL_GLOSNIK;hp|=p.cel==HDA_CEL_SLUCHAWKI;hdmi|=p.cel==HDA_CEL_HDMI;if(p.cel==HDA_CEL_GLOSNIK){CHECK(p.dlugosc==3);CHECK(p.ocena==100);CHECK(p.selektory[1]==0&&p.selektory[2]==0);}}
    }
    CHECK(qemu&&speaker&&hp&&hdmi);CHECK(f.m.devices[0].codec_writes==0&&f.m.devices[1].codec_writes==0);
    std::vector<char> report(HDA_DIAG_MAKS_RAPORTU);HdaRaportBufor b{};hda_raport_init(&b,report.data(),report.size());
    CHECK(hda_sformatuj_raport(snap.get(),"host",&b));CHECK(!b.obciety);
    for(const char* text:{"CORB selected size","RIRB running","EAPD capability","Association","Rates [Hz]","Mic In","Speaker","Headphone Out","HDMI"})CHECK(std::strstr(report.data(),text));
    std::printf("Scan %u scheduler turns; deterministic maximum step %llu ns; measured host maximum %llu ns; report %u bytes\n",turns,(unsigned long long)hda_diag_maks_krok_ns(),(unsigned long long)wall_max,b.dlugosc);
    std::printf("Service maximum CPU %llu ns, MMIO reads %u, MMIO writes %u; no complete verb loop\n",(unsigned long long)cpu_max,max_reads,max_writes);
    CHECK(hda_diag_startuj_skan());for(unsigned j=0;j<64;++j){hda_diag_krok_skanu();f.m.ns+=100000;}
    CHECK(hda_diag_pobierz_snapshot(snap.get(),sizeof(*snap),&bytes,&gen));CHECK(gen==1);
    hda_diag_anuluj_skan();hda_diag_pobierz_stan(&state);CHECK(state.przerwany);CHECK(!state.aktywny);CHECK(gen==1);
    // Independent controller-zero codecs, one missing codec on controller 0 cannot block controller 1.
    ++scenarios;f.m.devices[0].no_response=true;CHECK(hda_diag_startuj_skan());
    for(unsigned j=0;j<30000;++j){const bool done=hda_diag_krok_skanu();f.m.ns+=100000;if(done)break;}
    CHECK(hda_diag_pobierz_snapshot(snap.get(),sizeof(*snap),&bytes,&gen));CHECK(gen==3);CHECK(snap->kontrolery[1].liczba_kodekow==1);
    CHECK(snap->kontrolery[0].liczba_kodekow==0);CHECK(snap->kontrolery[0].gotowy);CHECK(snap->kodeki[1][0].cad==0);
}
static void topology_limits(){
    ++scenarios;
    CHECK(hda_wcap_delay(0x000b0010)==11);CHECK(hda_wcap_delay(0x10)==0);
    CHECK(!hda_znajdz_quirk({0x10ec,0xffff,0}));
    uint8_t gain=255;CHECK(hda_konserwatywne_wzmocnienie(0x80004f4f,&gain));CHECK(gain==7);
    CHECK(!hda_konserwatywne_wzmocnienie(0,&gain));CHECK(!hda_konserwatywne_wzmocnienie(0x0000107f,&gain));
    const uint32_t short_range[]={0x00008502};uint8_t out[32]{};uint32_t n=0;bool clipped=false;
    CHECK(hda_rozszerz_liste_polaczen(short_range,1,false,2,out,32,&n,&clipped));CHECK(n==4&&out[0]==2&&out[3]==5);
    const uint32_t bad[]={0x00008105};CHECK(!hda_rozszerz_liste_polaczen(bad,1,false,2,out,32,&n,&clipped));
    const uint32_t badnid[]={0x00000180};CHECK(!hda_rozszerz_liste_polaczen(badnid,1,true,1,out,32,&n,&clipped));
    ++scenarios;Fixture f;auto& d=f.m.add(0x8086,0xa348);
    d.codecs.clear();d.codecs.emplace(2,HdaHostModel::Codec::laptop(110));
    auto& codec=d.codecs.at(2);codec.param(4,0xe,0x80|3);codec.verb(4,0xf02,0,0x80050002);codec.verb(4,0xf02,2,8);
    // A pin with 127 encoded connections exceeds the bounded expanded list.
    codec.param(5,0xe,127);for(unsigned i=0;i<127;i+=4)codec.verb(5,0xf02,i,0x03030303);
    f.m.init_all();CHECK(hda_diag_inicjalizuj());CHECK(hda_diag_startuj_skan());
    for(unsigned j=0;j<40000;++j){bool done=hda_diag_krok_skanu();f.m.ns+=100000;if(done)break;}
    auto snap=std::make_unique<HdaDiagSnapshot>();uint32_t size=0,generation=0;
    CHECK(hda_diag_pobierz_snapshot(snap.get(),sizeof(*snap),&size,&generation));
    CHECK(snap->kodeki[0][0].cad==2);CHECK(snap->fg[0][0][0].widget_count==110);
    CHECK(snap->flagi&HDA_DIAG_FLAGA_OBECIETA);CHECK(snap->fg[0][0][0].flagi&1);
    CHECK(snap->widgety[0][0][0][2].conn_long_form);CHECK(snap->widgety[0][0][0][2].liczba_polaczen==5);
    CHECK(snap->widgety[0][0][0][3].flagi&1);CHECK(snap->widgety[0][0][0][3].liczba_polaczen==32);
    CHECK(d.codec_writes==0);
}
int main(){discovery();immediate();qemu_regression();controller_quirk_ordering();rings();scan();topology_limits();std::printf("HDA production transport/discovery/scan: %u scenarios, %u checks PASS; ASan+UBSan\n",scenarios,checks);}
