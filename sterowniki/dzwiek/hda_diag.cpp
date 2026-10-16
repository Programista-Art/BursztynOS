/* Kernel-owned, resumable HDA discovery. No hardware polling loops, no codec writes. */
#include "hda_diag.h"
#include "hda_transport.h"
#include "hda_wewnetrzny.h"
#include "hda.h"
#include "../czas/hpet.h"
void* kmalloc(size_t);
void kfree(void*);

namespace {
HdaDiagSnapshot* bufor_a=nullptr;
HdaDiagSnapshot* bufor_b=nullptr;
HdaDiagSnapshot* skan_bufor=nullptr;
#define skan_docelowy (*skan_bufor)
HdaDiagSnapshot* opublikowana=nullptr;
uint32_t generacja_publikacji=0, licznik_generacji=0;
HdaDiagSkan stan_skan{};
bool blokada_skanu=false;
struct GuardSkan {
    bool ok;
    GuardSkan():ok(!__atomic_test_and_set(&blokada_skanu,__ATOMIC_ACQUIRE)){}
    ~GuardSkan(){if(ok)__atomic_clear(&blokada_skanu,__ATOMIC_RELEASE);}
};
struct SkanKontrolera {
    HdaDiagSkan stan{};
    uint8_t cad=0, param=0, fg_root=0, fg_idx=0, widget=0, conn=0;
    uint8_t sub_start=0, sub_count=0;
    bool transakcja=false;
    bool uzyte_nid[128]{};
};
SkanKontrolera skany[HDA_DIAG_MAKS_KONTROLEROW];
uint32_t nastepny=0;
size_t przygotowanie_offset=0;
uint32_t przygotowanie_wybrany=HDA_DIAG_WSZYSTKIE_KONTROLERY;
bool przygotowanie=false, anulowana_generacja=false;
uint64_t maks_serwis_ns=0;
HdaDiagStanTestu stan_test{};
uint64_t test_start_ms=0, test_czas_ms=0;
bool test_aktywny=false;
uint64_t teraz_ms_monotoniczny(){return hpet_dostepny()?czas_monotoniczny_ns()/1000000ULL:0;}
void zero(void* dst,size_t len){auto* p=static_cast<uint8_t*>(dst);for(size_t i=0;i<len;++i)p[i]=0;}
void skopiuj_snapshot(HdaDiagSnapshot* dst,const HdaDiagSnapshot* src){
    auto* d=reinterpret_cast<uint8_t*>(dst);const auto* s=reinterpret_cast<const uint8_t*>(src);
    for(size_t i=0;i<sizeof(*dst);++i)d[i]=s[i];
}
template<class T> void naglowek(T& v){v.wersja=HDA_DIAG_WERSJA_ABI;v.rozmiar=sizeof(v);}
void init_snapshot(HdaDiagSnapshot& s){zero(&s,sizeof(s));naglowek(s);}
void obetnij(){skan_docelowy.flagi|=HDA_DIAG_FLAGA_OBECIETA;}
void koniec_kontrolera(uint32_t k,uint32_t blad){
    // Finish bounded recovery after the final absent CAD as well. Otherwise a
    // healthy controller with no codecs would be published as uninitialized.
    HdaDiagKontroler info{};
    if(!hda_transport_stan(k,&info))return;
    if(!info.gotowy&&info.inicjalizacja!=HDA_INIT_BLAD){
        skany[k].stan.blad=blad;skany[k].stan.etap=HDA_DIAG_ETAP_PUBLIKACJA;
        hda_transport_odnow(k);return;
    }
    if(info.inicjalizacja==HDA_INIT_BLAD&&blad==HDA_DIAG_OK)blad=info.blad;
    auto& s=skany[k].stan;s.aktywny=0;s.gotowy=1;s.postep_0_1000=1000;
    s.blad=blad;s.etap=blad?HDA_DIAG_ETAP_BLAD:HDA_DIAG_ETAP_GOTOWY;
    auto& c=skan_docelowy.kontrolery[k];c.blad=blad;
    hda_transport_zapisz_maski(k,c.discovered_codec_mask,c.responsive_codec_mask);
    HdaDiagKontroler fresh{};
    if(hda_transport_stan(k,&fresh)){
        fresh.liczba_kodekow=c.liczba_kodekow;fresh.blad=blad;c=fresh;
    }
}
// Returns PENDING without touching the scan cursor, so the caller retries the SAME read.
HdaTransportStatus odczyt(uint32_t k,SkanKontrolera& s,uint8_t cad,uint8_t nid,
                         uint16_t verb,uint8_t payload,uint32_t* out){
    auto& status=s.stan;status.biezacy_cad=cad;status.biezacy_nid=nid;
    if(!s.transakcja){
        uint32_t cmd=0;
        if(!hda_zbuduj_verb12(cad,nid,verb,payload,&cmd))return HDA_TRANSPORT_INVALID_ARGUMENT;
        auto rc=hda_transport_zglos(k,HDA_WLASCICIEL_DIAG,cmd);
        if(rc==HDA_TRANSPORT_NOT_READY){
            HdaDiagKontroler info{};
            if(hda_transport_stan(k,&info)&&info.inicjalizacja!=HDA_INIT_BLAD){
                hda_transport_odnow(k);return HDA_TRANSPORT_PENDING;
            }
        }
        if(rc==HDA_TRANSPORT_BUSY)return HDA_TRANSPORT_PENDING;
        if(rc==HDA_TRANSPORT_PENDING){s.transakcja=true;status.slow_w_kroku=1;}
        return rc;
    }
    const auto rc=hda_transport_krok(k,HDA_WLASCICIEL_DIAG);
    if(rc==HDA_TRANSPORT_PENDING||rc==HDA_TRANSPORT_BUSY)return HDA_TRANSPORT_PENDING;
    const auto result=hda_transport_odbierz(k,HDA_WLASCICIEL_DIAG,out);
    s.transakcja=false;
    return result;
}
HdaTransportStatus parametr(uint32_t k,SkanKontrolera& s,uint8_t cad,uint8_t nid,
                           uint8_t p,uint32_t* out){return odczyt(k,s,cad,nid,0xf00,p,out);}
bool poprawna_odp(HdaTransportStatus rc,uint32_t r){return rc==HDA_TRANSPORT_SUCCESS&&r!=UINT32_MAX;}
void nastepny_kodek(uint32_t k,SkanKontrolera& s){
    ++s.stan.kodek_idx;s.param=0;s.fg_root=0;s.fg_idx=0;s.widget=0;
    for(bool& v:s.uzyte_nid)v=false;
    if(s.stan.kodek_idx>=skan_docelowy.kontrolery[k].liczba_kodekow){
        s.stan.kodek_idx=0;s.stan.fg_idx=0;s.stan.etap=HDA_DIAG_ETAP_BUDOWANIE_SCIEZEK;
    }else s.stan.etap=HDA_DIAG_ETAP_PARAMETRY_KORZENIA;
}
void nastepny_widget(SkanKontrolera& s){
    ++s.widget;s.param=0;s.conn=0;s.stan.widget_idx=s.widget;
    s.stan.etap=HDA_DIAG_ETAP_WIDGET_ODKRYCIE;
}
void krok_kontrolera(uint32_t k){
    auto& s=skany[k];auto& st=s.stan;auto& ctrl=skan_docelowy.kontrolery[k];
    st.slow_w_kroku=0;
    if(!st.aktywny)return;
    uint32_t r=0;HdaTransportStatus rc=HDA_TRANSPORT_PENDING;
    switch(st.etap){
    case HDA_DIAG_ETAP_KONTROLER:{
        HdaDiagKontroler info{};
        if(!hda_transport_stan(k,&info)){return;}
        ctrl=info;
        if(info.inicjalizacja==HDA_INIT_BLAD){koniec_kontrolera(k,info.blad);return;}
        if(!info.gotowy){hda_transport_init_krok(k);return;}
        ctrl.liczba_kodekow=0;ctrl.responsive_codec_mask=0;
        st.etap=HDA_DIAG_ETAP_SONDOWANIE_KODEKOW;st.postep_0_1000=20;return;
    }
    case HDA_DIAG_ETAP_SONDOWANIE_KODEKOW:
        rc=parametr(k,s,s.cad,0,HDA_PAR_VENDOR_ID,&r);if(rc==HDA_TRANSPORT_PENDING)return;
        if(poprawna_odp(rc,r)&&r&&uint16_t(r>>16)!=65535){
            ctrl.responsive_codec_mask|=uint16_t(1U<<s.cad);ctrl.discovered_codec_mask|=uint16_t(1U<<s.cad);
            if(ctrl.liczba_kodekow<HDA_DIAG_MAKS_KODEKOW){
                auto& c=skan_docelowy.kodeki[k][ctrl.liczba_kodekow++];naglowek(c);
                c.cad=s.cad;c.odpowiada=1;c.vendor_id=hda_vendor_id(r);c.device_id=hda_device_id(r);
            }else obetnij();
        }
        ++s.cad;st.postep_0_1000=20+uint32_t(s.cad)*180/15;
        if(s.cad==15){
            hda_transport_zapisz_maski(k,ctrl.discovered_codec_mask,ctrl.responsive_codec_mask);
            if(!ctrl.liczba_kodekow){koniec_kontrolera(k,HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA);return;}
            st.kodek_idx=0;s.param=0;st.etap=HDA_DIAG_ETAP_PARAMETRY_KORZENIA;
        }
        return;
    case HDA_DIAG_ETAP_PARAMETRY_KORZENIA:{
        auto& c=skan_docelowy.kodeki[k][st.kodek_idx];
        rc=parametr(k,s,c.cad,0,s.param==0?HDA_PAR_REV_ID:HDA_PAR_SUB_NODE_COUNT,&r);
        if(rc==HDA_TRANSPORT_PENDING)return;
        if(!poprawna_odp(rc,r)){c.blad=HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;nastepny_kodek(k,s);return;}
        if(s.param++==0){c.revision_id=r;return;}
        c.root_sub_count=r;s.sub_start=hda_sub_start(r);s.sub_count=hda_sub_count(r);
        if(!hda_zakres_wezlow_poprawny(s.sub_start,s.sub_count)||!s.sub_start){
            c.blad=HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW;nastepny_kodek(k,s);return;
        }
        s.fg_root=0;s.param=0;st.etap=HDA_DIAG_ETAP_GRUPY_FUNKCYJNE;return;
    }
    case HDA_DIAG_ETAP_GRUPY_FUNKCYJNE:{
        auto& c=skan_docelowy.kodeki[k][st.kodek_idx];
        if(s.fg_root>=s.sub_count){nastepny_kodek(k,s);return;}
        const uint8_t nid=uint8_t(s.sub_start+s.fg_root);
        rc=parametr(k,s,c.cad,nid,HDA_PAR_FGROUP_TYPE,&r);if(rc==HDA_TRANSPORT_PENDING)return;
        ++s.fg_root;
        if(!poprawna_odp(rc,r))return;
        if(c.liczba_fg>=HDA_DIAG_MAKS_FG){c.flagi|=1;obetnij();return;}
        s.fg_idx=c.liczba_fg++;st.fg_idx=s.fg_idx;
        auto& f=skan_docelowy.fg[k][st.kodek_idx][s.fg_idx];naglowek(f);
        f.nid=nid;f.typ=hda_fg_typ(r);f.audio=hda_fg_jest_audio(r);s.uzyte_nid[nid]=true;
        if(f.audio){s.param=0;st.etap=HDA_DIAG_ETAP_PARAMETRY_AFG;}
        return;
    }
    case HDA_DIAG_ETAP_PARAMETRY_AFG:{
        auto& c=skan_docelowy.kodeki[k][st.kodek_idx];auto& f=skan_docelowy.fg[k][st.kodek_idx][s.fg_idx];
        const uint8_t params[]={HDA_PAR_AFG_CAPS,HDA_PAR_SUB_NODE_COUNT,HDA_PAR_PCM_SIZE_RATE,
            HDA_PAR_STREAM_FORMATS,HDA_PAR_AMP_IN_CAP,HDA_PAR_AMP_OUT_CAP,HDA_PAR_POWER_STATES};
        rc=parametr(k,s,c.cad,f.nid,params[s.param],&r);if(rc==HDA_TRANSPORT_PENDING)return;
        if(poprawna_odp(rc,r))switch(s.param){
            case 0:f.fg_caps=r;break;
            case 1:
                f.sub_start=hda_sub_start(r);f.sub_count=hda_sub_count(r);
                if(hda_zakres_wezlow_poprawny(uint8_t(f.sub_start),uint8_t(f.sub_count))&&f.sub_start){
                    f.widget_start=f.sub_start;f.widget_count=f.sub_count;
                    if(f.widget_count>HDA_DIAG_MAKS_WIDGETOW){f.flagi|=1;obetnij();}
                }else f.blad=HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW;
                break;
            case 2:f.pcm_size_rate=r;break;case 3:f.stream_formats=r;break;
            case 4:f.in_amp_caps=r;break;case 5:f.out_amp_caps=r;break;case 6:f.power_states=r;break;
        }else if(s.param==1)f.blad=HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;
        if(++s.param==sizeof(params)){s.param=0;s.widget=0;st.widget_idx=0;st.etap=HDA_DIAG_ETAP_WIDGET_ODKRYCIE;}
        return;
    }
    case HDA_DIAG_ETAP_WIDGET_ODKRYCIE:{
        auto& c=skan_docelowy.kodeki[k][st.kodek_idx];auto& f=skan_docelowy.fg[k][st.kodek_idx][s.fg_idx];
        if(f.blad||s.widget>=f.widget_count||s.widget>=HDA_DIAG_MAKS_WIDGETOW){st.etap=HDA_DIAG_ETAP_GRUPY_FUNKCYJNE;return;}
        const uint8_t nid=uint8_t(f.widget_start+s.widget);
        if(!nid||nid>127||s.uzyte_nid[nid]){f.blad=HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW;st.etap=HDA_DIAG_ETAP_GRUPY_FUNKCYJNE;return;}
        rc=parametr(k,s,c.cad,nid,HDA_PAR_WIDGET_CAP,&r);if(rc==HDA_TRANSPORT_PENDING)return;
        s.uzyte_nid[nid]=true;
        auto& w=skan_docelowy.widgety[k][st.kodek_idx][s.fg_idx][s.widget];naglowek(w);w.nid=nid;
        if(!poprawna_odp(rc,r)){w.flagi|=2;w.blad=HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;nastepny_widget(s);return;}
        w.awcap=r;w.typ=hda_wcap_typ(r);s.param=0;st.etap=HDA_DIAG_ETAP_WIDGET_PARAMETRY;
        st.postep_0_1000=200+uint32_t(s.fg_root)*700/(s.sub_count?s.sub_count:1);return;
    }
    case HDA_DIAG_ETAP_WIDGET_PARAMETRY:{
        auto& c=skan_docelowy.kodeki[k][st.kodek_idx];auto& f=skan_docelowy.fg[k][st.kodek_idx][s.fg_idx];
        auto& w=skan_docelowy.widgety[k][st.kodek_idx][s.fg_idx][s.widget];
        // Each supported read is a separate resumable operation; skipped metadata does no I/O.
        uint16_t verb=0xf00;uint8_t p=0;uint32_t* dst=nullptr;bool query=true;
        switch(s.param){
        case 0:p=HDA_PAR_PIN_CAP;dst=&w.pin_caps;query=w.typ==HDA_WID_PIN;
               break;
        case 1:verb=0xf1c;dst=&w.default_config;query=w.typ==HDA_WID_PIN;
               break;
        case 2:p=HDA_PAR_PCM_SIZE_RATE;dst=&w.pcm_size_rate;query=hda_typ_wolno_pcm_caps(w.typ)&&(w.awcap&HDA_WCAP_FORMAT_OVRD);
               if(!query&&hda_typ_wolno_pcm_caps(w.typ))w.pcm_size_rate=f.pcm_size_rate;
               break;
        case 3:p=HDA_PAR_STREAM_FORMATS;dst=&w.stream_formats;query=hda_typ_wolno_pcm_caps(w.typ)&&(w.awcap&HDA_WCAP_FORMAT_OVRD);
               if(!query&&hda_typ_wolno_pcm_caps(w.typ))w.stream_formats=f.stream_formats;
               break;
        case 4:p=HDA_PAR_AMP_IN_CAP;dst=&w.in_amp_caps;query=(w.awcap&HDA_WCAP_IN_AMP)&&(w.awcap&HDA_WCAP_AMP_OVRD);
               if(!query&&(w.awcap&HDA_WCAP_IN_AMP))w.in_amp_caps=f.in_amp_caps;
               break;
        case 5:p=HDA_PAR_AMP_OUT_CAP;dst=&w.out_amp_caps;query=(w.awcap&HDA_WCAP_OUT_AMP)&&(w.awcap&HDA_WCAP_AMP_OVRD);
               if(!query&&(w.awcap&HDA_WCAP_OUT_AMP))w.out_amp_caps=f.out_amp_caps;
               break;
        case 6:p=HDA_PAR_POWER_STATES;dst=&w.power_states;query=(w.awcap&(1U<<10))!=0;
               break;
        case 7:verb=0xf05;query=(w.awcap&(1U<<10))!=0;
               break;
        case 8:p=HDA_PAR_CONNLIST_LEN;dst=&w.connlist_len_raw;query=hda_typ_wolno_conn_list(w.awcap);
               break;
        case 9:verb=0xf01;dst=&w.conn_select;query=hda_connlist_liczba(w.connlist_len_raw)>1&&w.typ!=HDA_WID_MIX;
               break;
        case 10:verb=0xf0c;dst=&w.eapd;query=w.typ==HDA_WID_PIN&&(w.pin_caps&HDA_PINCAP_EAPD);break;
        default:
            if(hda_connlist_liczba(w.connlist_len_raw)){s.conn=0;w.conn_long_form=hda_connlist_long_form(w.connlist_len_raw);st.etap=HDA_DIAG_ETAP_POLACZENIA;}
            else nastepny_widget(s);
            return;
        }
        if(query){
            rc=odczyt(k,s,c.cad,w.nid,verb,p,&r);if(rc==HDA_TRANSPORT_PENDING)return;
            if(poprawna_odp(rc,r)){if(dst)*dst=r;else w.power_current=uint8_t((r>>4)&15);}
            else {w.blad=HDA_DIAG_BLAD_ZLA_ODPOWIEDZ;w.flagi|=2;}
        }
        ++s.param;return;
    }
    case HDA_DIAG_ETAP_POLACZENIA:{
        auto& c=skan_docelowy.kodeki[k][st.kodek_idx];auto& f=skan_docelowy.fg[k][st.kodek_idx][s.fg_idx];
        auto& w=skan_docelowy.widgety[k][st.kodek_idx][s.fg_idx][s.widget];
        const uint32_t entries=hda_connlist_liczba(w.connlist_len_raw), per=w.conn_long_form?2:4;
        if(s.conn<entries){
            rc=odczyt(k,s,c.cad,w.nid,0xf02,s.conn,&r);if(rc==HDA_TRANSPORT_PENDING)return;
            if(!poprawna_odp(rc,r)){w.blad=HDA_DIAG_BLAD_LISTA_POLACZEN;nastepny_widget(s);return;}
            w.conn_slowa[w.liczba_slow_polaczen++]=r;s.conn=uint8_t(s.conn+per);return;
        }
        uint32_t count=0;bool truncated=false;
        if(!hda_rozszerz_liste_polaczen(w.conn_slowa,w.liczba_slow_polaczen,w.conn_long_form,uint8_t(entries),
            w.polaczenia,HDA_DIAG_MAKS_POLACZEN,&count,&truncated)) w.blad=HDA_DIAG_BLAD_LISTA_POLACZEN;
        else {
            w.liczba_polaczen=uint8_t(count);
            for(uint32_t ci=0;ci<count;++ci){
                const auto nid=w.polaczenia[ci];w.polaczenia_surowe[ci]=nid;
                if(nid<f.widget_start||nid>=f.widget_start+f.widget_count){w.blad=HDA_DIAG_BLAD_LISTA_POLACZEN;}
            }
        }
        if(truncated){w.flagi|=1;obetnij();}
        nastepny_widget(s);return;
    }
    case HDA_DIAG_ETAP_BUDOWANIE_SCIEZEK:{
        if(st.kodek_idx>=ctrl.liczba_kodekow){koniec_kontrolera(k,HDA_DIAG_OK);return;}
        auto& c=skan_docelowy.kodeki[k][st.kodek_idx];
        if(st.fg_idx>=c.liczba_fg){++st.kodek_idx;st.fg_idx=0;return;}
        auto& f=skan_docelowy.fg[k][st.kodek_idx][st.fg_idx];
        if(f.audio&&!f.blad){
            const uint32_t begin=skan_docelowy.liczba_sciezek;
            const uint32_t capacity=HDA_DIAG_MAKS_SCIEZEK-begin;
            uint32_t count=0;bool truncated=false;
            if(capacity){
                const uint32_t n=f.widget_count<HDA_DIAG_MAKS_WIDGETOW?f.widget_count:HDA_DIAG_MAKS_WIDGETOW;
                hda_zbuduj_sciezki(skan_docelowy.widgety[k][st.kodek_idx][st.fg_idx],n,
                    skan_docelowy.sciezki+begin,capacity,&count,&truncated);
                for(uint32_t j=begin;j<begin+count;++j){auto& route=skan_docelowy.sciezki[j];route.kontroler_idx=uint8_t(k);route.codec_cad=c.cad;route.fg_nid=f.nid;}
                skan_docelowy.liczba_sciezek+=count;
            }else truncated=true;
            if(truncated)obetnij();
        }
        ++st.fg_idx;st.postep_0_1000=950;return;
    }
    case HDA_DIAG_ETAP_PUBLIKACJA:koniec_kontrolera(k,st.blad);return;
    default:koniec_kontrolera(k,HDA_DIAG_BLAD_ZLA_ODPOWIEDZ);return;
    }
}
bool publikuj(){
    // Under the short scan/publication try-lock: readers cannot race buffer reuse.
    auto* dst=skan_bufor;
    skan_docelowy.generacja=stan_skan.generacja_skanu;
    skan_docelowy.flagi&=~HDA_DIAG_FLAGA_SKAN_W_TOKU;
    skan_docelowy.flagi|=HDA_DIAG_FLAGA_SKAN_GOTOWY;
    skan_docelowy.etap=HDA_DIAG_ETAP_GOTOWY;skan_docelowy.postep_0_1000=1000;
    skan_docelowy.test=stan_test;
    HdaWewSciezkaTestu path{};hda_wew_pobierz_sciezke_testu(&path);
    skan_docelowy.test.kontroler_idx=uint8_t(hda_wew_kontroler_odtwarzania());
    skan_docelowy.test.codec_cad=path.cad;skan_docelowy.test.konwerter_nid=path.konwerter_nid;
    skan_docelowy.test.pin_nid=path.pin_nid;skan_docelowy.test.routing_fizyczny=path.routing_fizyczny;
    hda_diag_pobierz_strumien(&skan_docelowy.strumien);
    auto* wolny=bufor_a;
    bufor_a=bufor_b;
    bufor_b=dst;
    __atomic_store_n(&opublikowana,dst,__ATOMIC_RELEASE);
    skan_bufor=wolny;
    generacja_publikacji=dst->generacja;stan_skan.generacja_opublikowana=generacja_publikacji;
    stan_skan.migawka_dostepna=1;stan_skan.aktywny=0;stan_skan.gotowy=1;
    stan_skan.etap=HDA_DIAG_ETAP_GOTOWY;stan_skan.postep_0_1000=1000;
    return true;
}
}

bool hda_diag_inicjalizuj(){
    GuardSkan g;if(!g.ok)return false;
    for(uint32_t k=0;k<hda_transport_liczba();++k)hda_transport_anuluj(k,HDA_WLASCICIEL_DIAG);
    if(!bufor_a)bufor_a=static_cast<HdaDiagSnapshot*>(kmalloc(sizeof(HdaDiagSnapshot)));
    if(!bufor_b)bufor_b=static_cast<HdaDiagSnapshot*>(kmalloc(sizeof(HdaDiagSnapshot)));
    if(!skan_bufor)skan_bufor=static_cast<HdaDiagSnapshot*>(kmalloc(sizeof(HdaDiagSnapshot)));
    if(!bufor_a||!bufor_b||!skan_bufor){
        if(bufor_a)kfree(bufor_a);
        if(bufor_b)kfree(bufor_b);
        if(skan_bufor)kfree(skan_bufor);
        bufor_a=bufor_b=skan_bufor=nullptr;return false;
    }
    init_snapshot(*bufor_a);init_snapshot(*bufor_b);init_snapshot(skan_docelowy);
    opublikowana=nullptr;generacja_publikacji=licznik_generacji=0;
    stan_skan={};naglowek(stan_skan);stan_test={};naglowek(stan_test);
    for(auto& s:skany)s={};
    maks_serwis_ns=0;przygotowanie=false;return true;
}
bool hda_diag_startuj_skan(uint32_t wybrany){
    if(!skan_bufor&&!hda_diag_inicjalizuj())return false;
    GuardSkan g;if(!g.ok)return false;
    const uint32_t n=hda_transport_liczba();
    if(wybrany!=HDA_DIAG_WSZYSTKIE_KONTROLERY&&wybrany>=n)return false;
    if(stan_skan.aktywny)for(uint32_t k=0;k<n;++k)hda_transport_anuluj(k,HDA_WLASCICIEL_DIAG);
    // Refresh keeps the previous complete generation visible. Copy for a selected-controller refresh.
    anulowana_generacja=false;przygotowanie=true;przygotowanie_offset=0;przygotowanie_wybrany=wybrany;
    stan_skan={};naglowek(stan_skan);stan_skan.generacja_skanu=++licznik_generacji;
    if(!stan_skan.generacja_skanu)stan_skan.generacja_skanu=++licznik_generacji;
    stan_skan.generacja_opublikowana=generacja_publikacji;stan_skan.migawka_dostepna=opublikowana!=nullptr;
    stan_skan.aktywny=1;stan_skan.etap=HDA_DIAG_ETAP_KONTROLER;

    for(uint32_t k=0;k<n;++k){
        auto& s=skany[k];s={};naglowek(s.stan);s.stan.kontroler_idx=k;
        s.stan.generacja_skanu=stan_skan.generacja_skanu;s.stan.etap=HDA_DIAG_ETAP_KONTROLER;
        s.stan.aktywny=wybrany==HDA_DIAG_WSZYSTKIE_KONTROLERY||wybrany==k;
        if(s.stan.aktywny){
            hda_transport_generacja(k,stan_skan.generacja_skanu);
        }
    }
    nastepny=0;
    return true;
}
void hda_diag_anuluj_skan(uint32_t wybrany){
    GuardSkan g;if(!g.ok)return;
    for(uint32_t k=0;k<hda_transport_liczba();++k)if(wybrany==HDA_DIAG_WSZYSTKIE_KONTROLERY||wybrany==k){
        if(skany[k].stan.aktywny)anulowana_generacja=true;
        hda_transport_anuluj(k,HDA_WLASCICIEL_DIAG);skany[k].transakcja=false;skany[k].stan.aktywny=0;skany[k].stan.przerwany=1;
    }
    bool active=false;for(const auto& s:skany)active|=s.stan.aktywny;
    if(!active){przygotowanie=false;stan_skan.aktywny=0;stan_skan.przerwany=1;stan_skan.etap=HDA_DIAG_ETAP_BEZCZYNNY;}
}
bool hda_diag_krok_skanu(uint32_t wybrany){
    GuardSkan g;if(!g.ok)return false;
    if(!stan_skan.aktywny)return true;
    const uint64_t start=czas_monotoniczny_ns();
    struct Timing{uint64_t start;~Timing(){const uint64_t end=czas_monotoniczny_ns();if(end>=start&&end-start>maks_serwis_ns)maks_serwis_ns=end-start;}} timing{start};
    const uint32_t n=hda_transport_liczba();bool ran=false;
    if(przygotowanie){
        // Bounded RAM work as well as bounded MMIO work: at most 16 KiB per service turn.
        const size_t end=przygotowanie_offset+16384<sizeof(HdaDiagSnapshot)?przygotowanie_offset+16384:sizeof(HdaDiagSnapshot);
        auto* dst=reinterpret_cast<uint8_t*>(skan_bufor);
        const auto* src=reinterpret_cast<const uint8_t*>(opublikowana);
        const bool retain=src&&przygotowanie_wybrany!=HDA_DIAG_WSZYSTKIE_KONTROLERY;
        for(size_t i=przygotowanie_offset;i<end;++i)dst[i]=retain?src[i]:0;
        if(retain){
            const uint32_t k=przygotowanie_wybrany;
            const void* regions[]={skan_docelowy.kodeki[k],skan_docelowy.fg[k],skan_docelowy.widgety[k]};
            const size_t sizes[]={sizeof(skan_docelowy.kodeki[k]),sizeof(skan_docelowy.fg[k]),sizeof(skan_docelowy.widgety[k])};
            for(uint32_t j=0;j<3;++j){
                const size_t from=static_cast<const uint8_t*>(regions[j])-dst,to=from+sizes[j];
                const size_t lo=from>przygotowanie_offset?from:przygotowanie_offset,hi=to<end?to:end;
                if(hi>lo)zero(dst+lo,hi-lo);
            }
        }
        przygotowanie_offset=end;
        if(end==sizeof(HdaDiagSnapshot)){
            przygotowanie=false;naglowek(skan_docelowy);
            skan_docelowy.flagi&=~(HDA_DIAG_FLAGA_SKAN_GOTOWY|HDA_DIAG_FLAGA_SKAN_BLAD);
            skan_docelowy.flagi|=HDA_DIAG_FLAGA_SKAN_W_TOKU;
            skan_docelowy.liczba_kontrolerow=n;
            if(hda_transport_rejestr_obciety())obetnij();
            if(retain){
                uint32_t out=0;
                for(uint32_t j=0;j<skan_docelowy.liczba_sciezek&&j<HDA_DIAG_MAKS_SCIEZEK;++j)
                    if(skan_docelowy.sciezki[j].kontroler_idx!=przygotowanie_wybrany)skan_docelowy.sciezki[out++]=skan_docelowy.sciezki[j];
                skan_docelowy.liczba_sciezek=out;
            }
            if(!n){stan_skan.blad=HDA_DIAG_BLAD_BRAK_KONTROLERA;skan_docelowy.blad=stan_skan.blad;
                skan_docelowy.flagi|=HDA_DIAG_FLAGA_BRAK_KONTROLERA;return publikuj();}
        }
        return false;
    }
    for(uint32_t j=0;j<n;++j){
        const uint32_t k=(nastepny+j)%n;
        if((wybrany==HDA_DIAG_WSZYSTKIE_KONTROLERY||wybrany==k)&&skany[k].stan.aktywny){
            krok_kontrolera(k);nastepny=(k+1)%n;
            const auto generation=stan_skan.generacja_skanu;
            stan_skan=skany[k].stan;stan_skan.generacja_skanu=generation;ran=true;break;
        }
    }
    bool active=false;uint32_t progress=0;
    for(uint32_t k=0;k<n;++k){active|=skany[k].stan.aktywny;progress+=skany[k].stan.postep_0_1000;}
    stan_skan.aktywny=active;stan_skan.generacja_opublikowana=generacja_publikacji;stan_skan.migawka_dostepna=opublikowana!=nullptr;
    stan_skan.postep_0_1000=n?progress/n:1000;
    if(!active){
        if(anulowana_generacja){stan_skan.przerwany=1;stan_skan.gotowy=0;stan_skan.etap=HDA_DIAG_ETAP_BEZCZYNNY;return true;}
        return publikuj();
    }
    (void)ran;return false;
}
void hda_diag_pobierz_stan(HdaDiagSkan* out,uint32_t wybrany){
    if(!out)return;
    GuardSkan g;if(!g.ok){*out={};naglowek(*out);out->aktywny=1;out->blad=HDA_DIAG_BLAD_W_TRAKCIE;return;}
    *out=wybrany<hda_transport_liczba()?skany[wybrany].stan:stan_skan;
    naglowek(*out);out->migawka_dostepna=opublikowana!=nullptr;out->generacja_opublikowana=generacja_publikacji;
}
uint64_t hda_diag_maks_krok_ns(){return maks_serwis_ns;}
const HdaDiagSnapshot* hda_diag_pobierz_opublikowana(uint32_t* gen){
    if(gen)*gen=__atomic_load_n(&generacja_publikacji,__ATOMIC_ACQUIRE);
    return __atomic_load_n(&opublikowana,__ATOMIC_ACQUIRE);
}
bool hda_diag_pobierz_snapshot(HdaDiagSnapshot* out,uint32_t cap,uint32_t* written,uint32_t* gen){
    if(!out||!written||cap<sizeof(*out))return false;
    GuardSkan g;if(!g.ok||!opublikowana)return false;
    skopiuj_snapshot(out,opublikowana);*written=sizeof(*out);if(gen)*gen=generacja_publikacji;return true;
}
bool hda_diag_pobierz_strumien(HdaDiagStrumien* out) {
    if (!out) return false;
    *out={};
    {
        /* Probe live. */
        HdaWewStanStrumienia ws{};
        if (!hda_wew_pobierz_stan_strumienia(&ws)) return false;
        out->wersja = HDA_DIAG_WERSJA_ABI;
        out->rozmiar = static_cast<uint32_t>(sizeof(HdaDiagStrumien));
        out->numer = ws.numer;
        out->stream_tag = ws.stream_tag;
        out->format_raw = ws.format_raw;
        out->cbl = ws.cbl;
        out->lvi = ws.lvi;
        out->lpib = ws.lpib;
        out->ctl = ws.ctl;
        out->sts = ws.sts;
        out->bcis = ws.bcis;
        out->fifoe = ws.fifoe;
        out->dese = ws.dese;
        out->fifordy = ws.fifordy;
        out->run = ws.run;
        HdaWewStanTonu ton{};
        hda_wew_pobierz_stan_tonu(&ton);
        out->ton_deadline_ns = ton.deadline_ns;
        out->ton_stop_powod = ton.stop_reason;
        out->ton_aktywny = ton.active;
        out->run_po_stop = ton.run_after_stop;
        out->stan=ws.run?HDA_STRUMIEN_GRA:ws.ctl?HDA_STRUMIEN_SKONFIGUROWANY:HDA_STRUMIEN_BEZCZYNNY;
        out->bdl.wersja=HDA_DIAG_WERSJA_ABI;out->bdl.rozmiar=sizeof(HdaDiagBdlInfo);
        out->bdl.liczba_wpisow=ws.bdl_wpisow;out->bdl.maska_fizycznego=0;out->bdl.flagi=ws.bdl_uzywa_64;
        out->kanaly=uint8_t((ws.format_raw&15)+1);
        const uint8_t depth=(ws.format_raw>>4)&7;
        const uint16_t bits[]={8,16,20,24,32};out->bits=depth<5?bits[depth]:0;
        out->sample_rate=((ws.format_raw&(1U<<14))?44100U:48000U)*(((ws.format_raw>>11)&7)+1)/(((ws.format_raw>>8)&7)+1);
        return true;
    }
    return false;
}

bool hda_diag_pobierz_test(HdaDiagStanTestu* out) {
    if (!out) return false;
    *out = stan_test;
    HdaWewStanTonu ton{};
    hda_wew_pobierz_stan_tonu(&ton);
    if (test_aktywny && ton.active) {
        const uint64_t t = teraz_ms_monotoniczny();
        const uint64_t uplynelo = t >= test_start_ms ? t - test_start_ms : 0;
        const uint64_t pozostalo =
            uplynelo < test_czas_ms ? test_czas_ms - uplynelo : 0;
        out->pozostalo_ms = static_cast<uint32_t>(pozostalo);
        out->aktywny = 1;
    } else {
        out->pozostalo_ms = 0;
        out->aktywny = 0;
    }
    return true;
}

bool hda_diag_test_start(
    uint32_t tryb,
    uint32_t czestotliwosc_hz,
    uint32_t czas_ms,
    uint8_t kanaly
) {
    hda_diag_test_stop();

    if (tryb == HDA_TEST_BRAK) return false;
    if (czas_ms == 0 || czas_ms > HDA_API_MAX_CZAS_MS) return false;
    if (tryb == HDA_TEST_TON || tryb == HDA_TEST_LEWY ||
        tryb == HDA_TEST_PRAWY || tryb == HDA_TEST_OBA) {
        if (!hda_api_czestotliwosc_poprawna(czestotliwosc_hz)) return false;
    }
    if (tryb == HDA_TEST_SWEEP) {
        czestotliwosc_hz = HDA_TEST_SWEEP_MIN_HZ;
    }
    if (kanaly == 0) kanaly = 3;

    uint8_t kan = kanaly;
    if (tryb == HDA_TEST_LEWY) kan = 1;
    if (tryb == HDA_TEST_PRAWY) kan = 2;
    if (tryb == HDA_TEST_OBA) kan = 3;

    if (!hda_wew_test_start(tryb, czestotliwosc_hz, czas_ms, kan)) {
        stan_test.ostatni_blad = HDA_DIAG_BLAD_STRUMIEN;
        return false;
    }

    stan_test.wersja = HDA_DIAG_WERSJA_ABI;
    stan_test.rozmiar = static_cast<uint32_t>(sizeof(HdaDiagStanTestu));
    stan_test.tryb = tryb;
    stan_test.czestotliwosc_hz = czestotliwosc_hz;
    stan_test.czas_ms = czas_ms;
    stan_test.pozostalo_ms = czas_ms;
    stan_test.kanaly = kan;
    stan_test.aktywny = 1;
    stan_test.ostatni_blad = HDA_DIAG_OK;

    HdaWewSciezkaTestu sp{};
    hda_wew_pobierz_sciezke_testu(&sp);
    stan_test.kontroler_idx = static_cast<uint8_t>(hda_wew_kontroler_odtwarzania());
    stan_test.codec_cad = sp.cad;
    stan_test.konwerter_nid = sp.konwerter_nid;
    stan_test.pin_nid = sp.pin_nid;
    stan_test.routing_fizyczny = sp.routing_fizyczny;

    test_aktywny = true;
    test_start_ms = teraz_ms_monotoniczny();
    test_czas_ms = czas_ms;
    return true;
}

void hda_diag_test_stop() {
    hda_wew_test_stop();
    test_aktywny = false;
    stan_test.aktywny = 0;
    stan_test.pozostalo_ms = 0;
    stan_test.tryb = HDA_TEST_BRAK;
}

bool hda_diag_test_tick(uint64_t teraz_ms) {
    if (!hda_wew_test_tick(teraz_ms)) {
        if (test_aktywny) {
            hda_diag_test_stop();
        }
        return false;
    }
    if (!test_aktywny) return false;
    const uint64_t uplynelo =
        teraz_ms >= test_start_ms ? teraz_ms - test_start_ms : 0;
    if (uplynelo >= test_czas_ms) {
        hda_diag_test_stop();
        return false;
    }
    stan_test.pozostalo_ms =
        static_cast<uint32_t>(test_czas_ms - uplynelo);
    return true;
}

void hda_diag_pobierz_sciezke_testu(
    uint8_t* kontroler,
    uint8_t* cad,
    uint8_t* konwerter,
    uint8_t* pin,
    uint8_t* routing_fizyczny
) {
    HdaWewSciezkaTestu sp{};
    hda_wew_pobierz_sciezke_testu(&sp);
    if (kontroler) *kontroler = static_cast<uint8_t>(hda_wew_kontroler_odtwarzania());
    if (cad) *cad = sp.cad;
    if (konwerter) *konwerter = sp.konwerter_nid;
    if (pin) *pin = sp.pin_nid;
    if (routing_fizyczny) *routing_fizyczny = sp.routing_fizyczny;
}

bool hda_diag_wyslij_verb(uint32_t komenda, uint32_t* odpowiedz) {
    return hda_wew_wyslij_immediate(komenda, odpowiedz);
}

uint32_t hda_diag_verb12(uint8_t cad, uint8_t nid, uint16_t verb, uint8_t payload) {
    return hda_wew_verb12(cad, nid, verb, payload);
}

uint32_t hda_diag_verb4(uint8_t cad, uint8_t nid, uint8_t verb, uint16_t payload) {
    return hda_wew_verb4(cad, nid, verb, payload);
}
