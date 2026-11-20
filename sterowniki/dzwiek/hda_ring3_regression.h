#pragma once
#include "jadro/dzwiek/pcm_jadro.h"
#include "jadro/fs/loader.h"
#include "jadro/bws/scheduler.h"
#include "jadro/arch/diag_opoznienia_irq.h"
#include "hda.h"
extern void wypisz_log(const char*);
inline void hda_ring3_regression(bool requested) {
    static uint64_t start;
    static unsigned phase;
    static int producer_a;
    if (!requested || phase == 9) return;
    const uint64_t now = czas_monotoniczny_ns();
    auto log = [](const char* label, uint64_t value) {
        char text[128]; unsigned n=0;
        while(*label) text[n++]=*label++;
        char digits[20]; unsigned count=0;
        do { digits[count++]=char('0'+value%10); value/=10; } while(value);
        while(count) text[n++]=digits[--count];
        text[n]=0; wypisz_log(text);
    };
    auto mark = [&](const char* label) { log(label, now); };
    if (!phase) {
        start=now;
        char argument[32]; char digits[20]; unsigned n=0;
        uint64_t v=start;
        do { digits[n++]=char('0'+v%10); v/=10; } while(v);
        unsigned j=0; while(n) argument[j++]=digits[--n]; argument[j]=0;
        DiagOpoznieniaIrq::enable();
        if(!bws_uruchom_program_z_pliku_z_argumentem("/programy/pcm440.cebula/pcm440.bur",
            PZB_UZYTKOWNIK,PRAWO_GUI,false,argument)) { wypisz_log("[HDA-PCM] FAIL launch440"); phase=9; return; }
        producer_a=loader_ostatni_pid_uruchomiony();
        if(!bws_uruchom_program_z_pliku_z_argumentem("/programy/pcm660.cebula/pcm660.bur",
            PZB_UZYTKOWNIK,PRAWO_GUI,false,argument)) { wypisz_log("[HDA-PCM] FAIL launch660"); phase=9; return; }
        log("[HDA-PCM] PID_A=",producer_a);
        log("[HDA-PCM] PID_B=",loader_ostatni_pid_uruchomiony());
        phase=1; mark("[HDA-PCM] BOTH t_ns=");
    }
    const auto ms=(now-start)/1000000;
    if(phase==1 && ms>=4000) { phase=2; mark("[HDA-PCM] GAIN_A_25 t_ns="); }
    else if(phase==2 && ms>=6000) { phase=3; mark("[HDA-PCM] STREAM_MUTE t_ns="); }
    else if(phase==3 && ms>=8000) { phase=4; mark("[HDA-PCM] STREAM_RESTORE t_ns="); }
    else if(phase==4 && ms>=10000) {
        if(hda_wyciszenie_ustaw(true)!=HDA_GLOSNOSC_OK) return;
        phase=5; mark("[HDA-PCM] MASTER_MUTE t_ns=");
    } else if(phase==5 && ms>=12000) {
        if(hda_wyciszenie_ustaw(false)!=HDA_GLOSNOSC_OK) return;
        phase=6; mark("[HDA-PCM] MASTER_RESTORE t_ns=");
    } else if(phase==6 && ms>=14000) {
        if(scheduler_zazadaj_zakonczenia(producer_a)!=1) { wypisz_log("[HDA-PCM] FAIL terminate");phase=9;return; }
        phase=7; mark("[HDA-PCM] ONLY_B t_ns=");
    } else if(phase==7 && ms>=17000) { phase=8; mark("[HDA-PCM] SILENCE t_ns="); }
    else if(phase==8 && ms>=18000) {
        PcmJadro::Statystyki s{};
        if(!PcmJadro::statystyki(s)) return;
        log("[HDA-PCM] R3_OPENS=",s.otwarcia_aplikacji);
        log("[HDA-PCM] R3_CLEANED=",s.usuniete_przy_zakonczeniu);
        log("[HDA-PCM] R3_STARTS=",s.starty);
        log("[HDA-PCM] R3_RECOVERIES=",s.odzyskania);
        log("[HDA-PCM] R3_STARVED=",s.braki_ramek);
        log("[HDA-PCM] R3_SAFE_SILENCE=",s.bezpieczna_cisza);
        log("[HDA-PCM] R3_DELAYS=",s.opoznienia);
        log("[HDA-PCM] R3_MAX_IRQ_NS=",s.maks_irq_ns);
        log("[HDA-PCM] R3_MAX_GAP_NS=",s.maks_przerwa_ns);
        DiagOpoznieniaIrq::disable();
        phase=9; mark("[HDA-PCM] DONE t_ns=");
    }
}
