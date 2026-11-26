#pragma once
#include "jadro/dzwiek/pcm_jadro.h"
#include "jadro/dzwiek/mikser_pcm.h"
#include "sterowniki/czas/hpet.h"
#include "hda.h"
#include "jadro/arch/diag_opoznienia_irq.h"
#include "jadro/dzwiek/punkt_kontrolny_audio.h"

extern "C" uint32_t timer_scheduler_hz();

// Runs only with the explicit boot token, once the desktop is ready.
inline void hda_pcm_regression_krok(bool requested) {
    using namespace PcmJadro;
    extern void wypisz_log(const char*);
    struct Producer {
        Uchwyt handle;
        uint32_t hz, phase;
        bool feed(bool& retry) {
            if (!handle) return true;
            constexpr uint32_t frames = 512;
            int16_t samples[frames * 2];
            // Catch up one whole source queue even after a long PID0 gap.
            // A full queue and a partially accepted block both end this visit.
            for (uint32_t batch = 0; batch < Pcm::CAPACITY_FRAMES / frames; ++batch) {
                uint32_t p = phase;
                for (uint32_t i = 0; i < frames; ++i) {
                    const int32_t a = static_cast<int32_t>(p * 4);
                    const int32_t tri = a < 48000 ? a : (a < 144000 ? 96000 - a : a - 192000);
                    samples[2 * i] = samples[2 * i + 1] = static_cast<int16_t>(tri / 6);
                    p = (p + hz) % 48000;
                }
                uint32_t accepted = 0;
                const Wynik rc = zapisz(handle, samples, frames, accepted);
                phase = (phase + accepted * hz) % 48000;
                if (rc == Wynik::Zajete) { retry = true; return true; }
                if (rc == Wynik::Pelny) return true;
                if (rc != Wynik::Ok) return false;
                if (accepted != frames) return true;
            }
            return true;
        }
    };
    static Producer a{0, 440, 0}, b{0, 660, 0};
    static uint64_t start, feed_after;
    static uint64_t last_feed, max_feed_gap, feed_visits, busy_visits;
    struct FeedGap { uint64_t from, until; };
    static FeedGap feed_gaps[32];
    static uint32_t feed_gap_count;
    static unsigned phase;
    static bool master_changed;
    static HdaGlosnoscInfo master{};
    if (!requested || phase >= 8) return;

    auto fail = [&] {
        DiagOpoznieniaIrq::disable();
        wypisz_log("[HDA-PCM] FAIL");
        if (master_changed) (void)hda_wyciszenie_ustaw(master.wyciszony);
        if (a.handle) (void)zamknij(a.handle);
        if (b.handle) (void)zamknij(b.handle);
        phase = 8;
    };
    const uint64_t now = czas_monotoniczny_ns();
    auto mark = [now](const char* label) {
        char text[80];
        unsigned n = 0;
        while (*label) text[n++] = *label++;
        const char suffix[] = " t_ns=";
        for (unsigned i = 0; i < sizeof(suffix) - 1; ++i) text[n++] = suffix[i];
        char digits[20];
        unsigned count = 0;
        for (uint64_t t = now;; t /= 10) {
            digits[count++] = char('0' + t % 10);
            if (t < 10) break;
        }
        while (count) text[n++] = digits[--count];
        text[n] = 0;
        wypisz_log(text);
    };
    const bool opening = !phase;
    if (opening) {
        DiagOpoznieniaIrq::enable();
        hda_glosnosc_info(&master);
        if (!master.urzadzenie || master.wyciszony || !master.procent ||
            otworz(a.handle) != Wynik::Ok || otworz(b.handle) != Wynik::Ok) {
            fail();
            return;
        }
        start = now;
        phase = 1;
    }
    if (b.handle && now >= feed_after) {
        ++feed_visits;
        if (last_feed) {
            const uint64_t gap = now - last_feed;
            if (gap > max_feed_gap) max_feed_gap = gap;
            if (gap >= 24000000 && feed_gap_count < sizeof(feed_gaps) / sizeof(feed_gaps[0]))
                feed_gaps[feed_gap_count++] = {last_feed, now};
        }
        last_feed = now;
        bool retry = false;
        if (!a.feed(retry) || !b.feed(retry)) { fail(); return; }
        if (retry) ++busy_visits;
        else feed_after = now + 4000000ULL;
    }
    if (opening) mark("[HDA-PCM] BOTH");
    const uint64_t elapsed_ms = (now - start) / 1000000;
    if (phase == 1 && elapsed_ms >= 4000) {
        if (glosnosc(a.handle, 25, false) != Wynik::Ok) { fail(); return; }
        phase = 2;
        mark("[HDA-PCM] GAIN_A_25");
    } else if (phase == 2 && elapsed_ms >= 6500) {
        if (hda_wyciszenie_ustaw(true) != HDA_GLOSNOSC_OK) { fail(); return; }
        master_changed = true;
        phase = 3;
        mark("[HDA-PCM] MASTER_MUTE");
    } else if (phase == 3 && elapsed_ms >= 8500) {
        if (hda_wyciszenie_ustaw(false) != HDA_GLOSNOSC_OK) { fail(); return; }
        master_changed = false;
        phase = 4;
        mark("[HDA-PCM] MASTER_RESTORE");
    } else if (phase == 4 && elapsed_ms >= 11000) {
        if (zamknij(a.handle) != Wynik::Ok) { fail(); return; }
        a.handle = 0;
        phase = 5;
        mark("[HDA-PCM] ONLY_B");
    } else if (phase == 5 && elapsed_ms >= 14000) {
        if (zamknij(b.handle) != Wynik::Ok) { fail(); return; }
        b.handle = 0;
        phase = 6;
        mark("[HDA-PCM] SILENCE");
    } else if (phase == 6 && elapsed_ms >= 15000) {
        DiagOpoznieniaIrq::disable();
        Statystyki s{};
        if (!statystyki(s)) { fail(); return; }
        char message[512];
        unsigned size = 0;
        auto str = [&](const char* p) {
            while (*p && size + 1 < sizeof(message)) message[size++] = *p++;
        };
        auto field = [&](const char* name, uint64_t value) {
            str(name);
            char digits[20];
            unsigned n = 0;
            do { digits[n++] = char('0' + value % 10); value /= 10; } while (value);
            while (n && size + 1 < sizeof(message)) message[size++] = digits[--n];
        };
        str("[HDA-PCM] STATS");
        field(" starts=", s.starty);
        field(" periods=", s.okresy);
        field(" starved_frames=", s.braki_ramek);
        field(" delays=", s.opoznienia);
        field(" dma_errors=", s.bledy_dma);
        field(" recoveries=", s.odzyskania);
        field(" max_gap_ns=", s.maks_przerwa_ns);
        field(" mix_ns=", s.czas_miksowania_ns);
        field(" max_mix_ns=", s.maks_miks_ns);
        message[size] = 0;
        wypisz_log(message);
        size = 0;
        str("[HDA-PCM] TIMING");
        field(" irq=", s.przerwania);
        field(" max_irq_ns=", s.maks_irq_ns);
        field(" max_irq_pid=", s.maks_irq_pid);
        field(" max_irq_ring0=", s.maks_irq_ring0);
        field(" work=", s.paczki_pracy);
        field(" work_wait_ns=", s.suma_czekania_ns);
        field(" max_work_wait_ns=", s.maks_czekanie_ns);
        field(" lock_busy=", s.blokady);
        field(" worker_lock_busy=", s.blokady_pracy);
        field(" max_lock_ns=", s.maks_blokada_ns);
        field(" max_recovery_ns=", s.maks_odzyskanie_ns);
        field(" max_lpib_delta=", s.maks_skok_lpib);
        field(" max_late_ns=", s.maks_spoznienie_ns);
        field(" missed_periods=", s.utracone_okresy);
        field(" max_lead_frames=", s.maks_wyprzedzenie_ramek);
        field(" mix_periods=", s.okresy_miksowania);
        field(" irq_during_mix=", s.irq_w_miksie);
        field(" priority_returns=", s.pilne_powroty);
        field(" watchdog_extensions=", s.przedluzenia_watchdoga);
        message[size] = 0;
        wypisz_log(message);
        size = 0;
        str("[HDA-PCM] CAUSES");
        const char* fields[] = {" none=", " watchdog=", " ambiguous=", " position=", " stalled=",
                                " ownership=", " device=", " clock=", " start_failed="};
        for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
            field(fields[i], s.przyczyny[i]);
        message[size] = 0;
        wypisz_log(message);
        {   // Punkty kontrolne audio w dlugich petlach jadra (sterta, VirtIO-BLK, AHCI): ile razy zastaly prace,
            // ile wykonaly, ile odroczyly i dlaczego; prawdziwe niedobory to missed_periods / safe_silence powyzej.
            const auto pk = PunktKontrolnyAudio::statystyki();
            size = 0;
            str("[HDA-PCM] CHECKPOINTS");
            field(" requested=", pk.zgloszenia);
            field(" executed=", pk.wykonane);
            field(" deferred_if0=", pk.odroczone_przerwania);
            field(" nested_rejected=", pk.odrzucone_zagniezdzone);
            field(" deferred_lock=", pk.odroczone_blokada);
            field(" device_deadline_ext_ns=", pk.przedluzenia_terminu_ns);
            message[size] = 0;
            wypisz_log(message);
        }
        for (uint32_t i = 0; i < s.zapisane_zdarzenia; ++i) {
            Zdarzenie e{};
            if (!zdarzenie(i, e)) break;
            size = 0;
            str("[HDA-PCM] RECOVERY");
            field(" cause=", e.przyczyna);
            field(" observed_ns=", e.ns);
            field(" deadline_ns=", e.termin_ns);
            field(" irq_gap_ns=", e.irq_ns);
            field(" pending_ns=", e.oczekuje_od_ns);
            field(" lpib=", e.lpib);
            field(" last_trusted_ns=", e.ostatni_zaufany_ns);
            field(" watchdog_lpib=", e.lpib_watchdoga);
            message[size] = 0;
            wypisz_log(message);
        }
        size = 0;
        str("[HDA-PCM] SILENCE_COUNTS");
        field(" mixer_zero_periods=", s.cisza_miksera);
        field(" master_muted_periods=", s.okresy_master_mute);
        field(" safety_silent_periods=", s.bezpieczna_cisza);
        message[size] = 0;
        wypisz_log(message);
        size = 0;
        str("[HDA-PCM] DMA_STATUS");
        field(" watchdog_rechecks=", s.proby_watchdoga);
        field(" fifo_errors=", s.fifo_error);
        field(" descriptor_errors=", s.descriptor_error);
        message[size] = 0;
        wypisz_log(message);
        for (uint32_t i = 0;; ++i) {
            SladPcm e{};
            if (!slad_pcm(i, e)) break;
            size = 0;
            str("[HDA-PCM] SHORTFALL");
            field(" kind=", static_cast<uint32_t>(e.rodzaj));
            field(" at_ns=", e.ns);
            field(" previous_ns=", e.poprzedni_odczyt_ns);
            field(" pending_ns=", e.oczekuje_od_ns);
            field(" irq_gap_ns=", e.przerwa_irq_ns);
            field(" period=", e.okres);
            field(" dma_bytes=", e.bajty_dma);
            field(" previous_bytes=", e.poprzednie_bajty_dma);
            field(" lpib=", e.lpib);
            field(" min_frames=", e.minimum_kolejki);
            field(" max_frames=", e.maksimum_kolejki);
            field(" sources=", e.oczekujace_zrodla);
            field(" missing_frames=", e.brakujace_ramki);
            field(" prime=", e.start);
            field(" muted=", e.master_mute);
            field(" staged=", e.przygotowano);
            field(" pcm_available=", e.pcm_dostepne);
            message[size] = 0;
            wypisz_log(message);
        }
        size = 0;
        str("[HDA-PCM] FEED_STATS");
        field(" visits=", feed_visits);
        field(" busy=", busy_visits);
        field(" max_gap_ns=", max_feed_gap);
        message[size] = 0;
        wypisz_log(message);
        for (uint32_t i = 0; i < feed_gap_count; ++i) {
            size = 0;
            str("[HDA-PCM] FEED_GAP");
            field(" begin_ns=", feed_gaps[i].from);
            field(" end_ns=", feed_gaps[i].until);
            message[size] = 0;
            wypisz_log(message);
        }
        size = 0;
        str("[HDA-PCM] IRQ_TIMING");
        field(" lapic_hz=", timer_scheduler_hz());
        field(" intervals=", DiagOpoznieniaIrq::intervals);
        field(" interval_sum_ns=", DiagOpoznieniaIrq::interval_sum);
        field(" gaps_12ms=", DiagOpoznieniaIrq::late_gaps);
        field(" eoi=", DiagOpoznieniaIrq::eois);
        field(" missing_eoi=", DiagOpoznieniaIrq::missing_eois);
        field(" max_eoi_ns=", DiagOpoznieniaIrq::max_eoi_ns);
        message[size] = 0;
        wypisz_log(message);
        auto gap = [&](const char* label, const DiagOpoznieniaIrq::Gap& g) {
            if (!g.delivered) return;
            size = 0;
            str(label);
            field(" begin_ns=", g.previous);
            field(" end_ns=", g.delivered);
            field(" pid=", g.pid);
            field(" ring0=", g.ring0);
            message[size] = 0;
            wypisz_log(message);
        };
        gap("[HDA-PCM] IRQ_MAX_GAP", DiagOpoznieniaIrq::longest_gap);
        for (uint32_t i = 0; i < DiagOpoznieniaIrq::gap_count; ++i)
            gap("[HDA-PCM] IRQ_GAP", DiagOpoznieniaIrq::gaps[i]);
        auto span = [&](const char* label, const DiagOpoznieniaIrq::Span& x) {
            if (!x.end) return;
            size = 0;
            str(label);
            field(" site=", x.site);
            field(" begin_ns=", x.begin);
            field(" end_ns=", x.end);
            message[size] = 0;
            wypisz_log(message);
        };
        for (const auto& x : DiagOpoznieniaIrq::longest)
            span("[HDA-PCM] MASK_MAX", x);
        for (uint32_t i = 0; i < DiagOpoznieniaIrq::span_count; ++i)
            span("[HDA-PCM] MASK_LONG", DiagOpoznieniaIrq::spans[i]);
        phase = 8;
        mark("[HDA-PCM] DONE");
    }
}
