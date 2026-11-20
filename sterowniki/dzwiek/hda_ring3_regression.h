#pragma once
#include "jadro/dzwiek/pcm_write_diag.h"
#include "jadro/dzwiek/pcm_sched_diag.h"
#include "jadro/dzwiek/pcm_jadro.h"
#include "jadro/fs/loader.h"
#include "jadro/bws/scheduler.h"
#include "jadro/arch/diag_opoznienia_irq.h"
#include "hda.h"
#include "jadro/dzwiek/pcm_aplikacji.h"
extern void wypisz_log(const char*);
inline void hda_ring3_regression(bool requested) {
    static uint64_t start;
    static unsigned phase;
    static int producer_a, producer_b;
    static uint64_t generation_a, generation_b;
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
        PcmSchedDiag::enable_timeline();
        if(!bws_uruchom_program_z_pliku_z_argumentem("/programy/pcm440.cebula/pcm440.bur",
            PZB_UZYTKOWNIK,PRAWO_GUI,false,argument)) { wypisz_log("[HDA-PCM] FAIL launch440"); phase=9; return; }
        producer_a=loader_ostatni_pid_uruchomiony();
        generation_a=scheduler_generacja_procesu(producer_a);
        if(!bws_uruchom_program_z_pliku_z_argumentem("/programy/pcm660.cebula/pcm660.bur",
            PZB_UZYTKOWNIK,PRAWO_GUI,false,argument)) { wypisz_log("[HDA-PCM] FAIL launch660"); phase=9; return; }
        producer_b=loader_ostatni_pid_uruchomiony();
        generation_b=scheduler_generacja_procesu(producer_b);
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
        DiagOpoznieniaIrq::disable();
        PcmSchedDiag::disable_timeline();
        log("[HDA-PCM] R3_WRITE_CALLS=",PcmWriteDiag::read(PcmWriteDiag::calls));
        log("[HDA-PCM] R3_WRITE_SMALL=",PcmWriteDiag::read(PcmWriteDiag::small));
        log("[HDA-PCM] R3_WRITE_LARGE=",PcmWriteDiag::read(PcmWriteDiag::large));
        log("[HDA-PCM] R3_WRITE_ACCEPTED=",PcmWriteDiag::read(PcmWriteDiag::accepted));
        log("[HDA-PCM] R3_WRITE_INVALID=",PcmWriteDiag::read(PcmWriteDiag::errors[1]));
        log("[HDA-PCM] R3_WRITE_FULL=",PcmWriteDiag::read(PcmWriteDiag::errors[5]));
        log("[HDA-PCM] R3_WRITE_BUSY=",PcmWriteDiag::read(PcmWriteDiag::errors[6]));
        log("[HDA-PCM] R3_WRITE_MEMORY=",PcmWriteDiag::read(PcmWriteDiag::errors[8]));
        log("[HDA-PCM] R3_QUERY_CALLS=",PcmWriteDiag::read(PcmWriteDiag::query_calls));
        log("[HDA-PCM] R3_QUERY_BUSY=",PcmWriteDiag::read(PcmWriteDiag::query_busy));
        log("[HDA-PCM] R3_QUERY_ERRORS=",PcmWriteDiag::read(PcmWriteDiag::query_errors));
        log("[HDA-PCM] R3_OPENS=",s.otwarcia_aplikacji);
        log("[HDA-PCM] R3_CLEANED=",s.usuniete_przy_zakonczeniu);
        log("[HDA-PCM] R3_STARTUP_TIMEOUTS=",s.start_przedwczesny);
        log("[HDA-PCM] R3_STARTS=",s.starty);
        log("[HDA-PCM] R3_RECOVERIES=",s.odzyskania);
        log("[HDA-PCM] R3_STARVED=",s.braki_ramek);
        log("[HDA-PCM] R3_SAFE_SILENCE=",s.bezpieczna_cisza);
        log("[HDA-PCM] R3_DELAYS=",s.opoznienia);
        log("[HDA-PCM] R3_MAX_IRQ_NS=",s.maks_irq_ns);
        log("[HDA-PCM] R3_MAX_GAP_NS=",s.maks_przerwa_ns);
        log("[HDA-PCM] R3_WORK_WAIT_MAX=",s.maks_czekanie_ns);
        log("[HDA-PCM] R3_WORK_WAIT_SUM=",s.suma_czekania_ns);
        log("[HDA-PCM] R3_WORK_COUNT=",s.paczki_pracy);
        log("[HDA-PCM] R3_LOCK_BUSY=",s.blokady_pracy);
        log("[HDA-PCM] R3_MAX_MIX_NS=",s.maks_miks_ns);
        log("[HDA-PCM] R3_FIFO=",s.fifo_error);
        log("[HDA-PCM] R3_BDL_ERROR=",s.descriptor_error);
        auto stream = [&](int pid, uint64_t generation) {
            BwsPcmStats q{};
            if (!pcm_diag_stream(pid,generation,q)) return;
            log("[PCM-STREAM] pid=",pid);
            log("[PCM-STREAM] writes=",q.writes);
            log("[PCM-STREAM] requested=",q.requested);
            log("[PCM-STREAM] accepted=",q.accepted);
            log("[PCM-STREAM] partial=",q.partial);
            log("[PCM-STREAM] full=",q.full);
            log("[PCM-STREAM] rejected=",q.rejected);
            log("[PCM-STREAM] queued=",q.queued);
            log("[PCM-STREAM] starved=",q.starved);
            log("[PCM-STREAM] muted_missing=",q.muted_missing);
            log("[PCM-STREAM] min_queued=",q.min_queued);
            log("[PCM-STREAM] max_queued=",q.max_queued);
            log("[PCM-STREAM] max_write_gap_ns=",q.max_write_gap_ns);
            log("[PCM-STREAM] max_copy_ns=",q.max_copy_ns);
            log("[PCM-STREAM] max_runnable_wait_ns=",q.max_runnable_wait_ns);
            log("[PCM-STREAM] max_voluntary_wait_ns=",q.max_voluntary_wait_ns);
            log("[PCM-STREAM] max_exec_gap_ns=",q.max_exec_gap_ns);
            log("[PCM-STREAM] consumed=",q.consumed);
        };
        stream(producer_a,generation_a);
        stream(producer_b,generation_b);
        auto sched_dump = [&](int pid) {
            PcmSchedDiag::PidStats st{};
            if (!PcmSchedDiag::snapshot_pid(static_cast<uint32_t>(pid), st)) return;
            log("[PCM-SCHED] pid=",pid);
            log("[PCM-SCHED] scheduled=",st.scheduled_count);
            log("[PCM-SCHED] runnable_waits=",st.runnable_waits);
            log("[PCM-SCHED] max_runnable_wait_ns=",st.max_runnable_wait_ns);
            log("[PCM-SCHED] total_runnable_wait_ns=",st.total_runnable_wait_ns);
            log("[PCM-SCHED] voluntary_waits=",st.voluntary_waits);
            log("[PCM-SCHED] max_voluntary_wait_ns=",st.max_voluntary_wait_ns);
            log("[PCM-SCHED] total_voluntary_wait_ns=",st.total_voluntary_wait_ns);
            log("[PCM-SCHED] max_exec_gap_ns=",st.max_exec_gap_ns);
        };
        sched_dump(producer_a);
        sched_dump(producer_b);
        for (uint32_t i=0;;++i) {
            PcmSchedDiag::Event ev{};
            if (!PcmSchedDiag::event(i, ev)) break;
            log("[PCM-TIMELINE] ns=",ev.ns);
            log("[PCM-TIMELINE] kind=",ev.kind);
            log("[PCM-TIMELINE] pid=",ev.pid);
            log("[PCM-TIMELINE] arg0=",ev.arg0);
            log("[PCM-TIMELINE] arg1=",ev.arg1);
            log("[PCM-TIMELINE] flags=",ev.flags);
        }
        for (uint32_t i=0;;++i) {
            Pcm::Starvation e{};
            if (!pcm_diag_starvation(i,e)) break;
            log("[PCM-STARVE] pid=",e.owner.pid);
            log("[PCM-STARVE] ns=",e.ns);
            log("[PCM-STARVE] last_write_ns=",e.last_write_ns);
            log("[PCM-STARVE] missing=",e.missing);
            log("[PCM-STARVE] queued=",e.queued);
        }
        for (uint32_t i=0;i<s.zapisane_zdarzenia;++i) {
            PcmJadro::Zdarzenie e{};
            if (!PcmJadro::zdarzenie(i,e)) break;
            log("[PCM-RECOVERY] ns=",e.ns);
            log("[PCM-RECOVERY] irq_gap_ns=",e.irq_ns);
            log("[PCM-RECOVERY] pending_ns=",e.oczekuje_od_ns);
            log("[PCM-RECOVERY] cause=",e.przyczyna);
            log("[PCM-RECOVERY] lpib=",e.lpib);
            log("[PCM-RECOVERY] trusted_ns=",e.ostatni_zaufany_ns);
        }
        for (uint32_t i=0;i<DiagOpoznieniaIrq::gap_count;++i) {
            const auto& e=DiagOpoznieniaIrq::gaps[i];
            log("[PCM-IRQ] begin_ns=",e.previous);
            log("[PCM-IRQ] end_ns=",e.delivered);
            log("[PCM-IRQ] pid=",e.pid);
            log("[PCM-IRQ] ring0=",e.ring0);
        }
        for (uint32_t i=0;;++i) {
            PcmJadro::SladPcm e{};
            if (!PcmJadro::slad_pcm(i,e)) break;
            log("[PCM-SHORTFALL] ns=",e.ns);
            log("[PCM-SHORTFALL] kind=",static_cast<unsigned>(e.rodzaj));
            log("[PCM-SHORTFALL] period=",e.okres);
            log("[PCM-SHORTFALL] dma_bytes=",e.bajty_dma);
            log("[PCM-SHORTFALL] pending_ns=",e.oczekuje_od_ns);
            log("[PCM-SHORTFALL] min_queue=",e.minimum_kolejki);
            log("[PCM-SHORTFALL] max_queue=",e.maksimum_kolejki);
        }
        phase=9; mark("[HDA-PCM] DONE t_ns=");
    }
}
