#pragma once

// Explicit diagnostic boot option only. Called from the normal PID0 deferred
// loop AFTER its HDA service; this observer never supplies the missing service.
#include "hda.h"
#include "hda_diag.h"
#include "hda_transport.h"
#include "hda_wewnetrzny.h"
#include "../czas/hpet.h"

extern void wypisz_log(const char* tekst);

namespace HdaQemuRegression {
struct Line {
    char text[320] = {};
    uint32_t size = 0;
    void add(const char* value) {
        while (value && *value && size + 1 < sizeof(text)) text[size++] = *value++;
        text[size] = 0;
    }
    void number(uint64_t value, uint32_t base = 10, uint32_t width = 0) {
        char reverse[32]; uint32_t n = 0;
        do { const uint32_t digit = value % base;
            reverse[n++] = static_cast<char>(digit < 10 ? '0' + digit : 'A' + digit - 10);
            value /= base;
        } while (value && n < sizeof(reverse));
        while (n < width && n < sizeof(reverse)) reverse[n++] = '0';
        while (n && size + 1 < sizeof(text)) text[size++] = reverse[--n];
        text[size] = 0;
    }
    void field(const char* key, uint64_t value) { add(key); number(value); }
    void emit() { wypisz_log(text); }
};
inline void fail(const char* reason) {
    Line line; line.add("[HDA-QEMU-REGRESSION] FAIL "); line.add(reason); line.emit();
}
}

inline void hda_qemu_regression_krok(bool requested) {
    using namespace HdaQemuRegression;
    static uint8_t phase = 0;
    static uint64_t start_ns = 0, deadline_ns = 0;
    static uint64_t start_not_before_ns = 0;
    static bool before_seen = false;
    constexpr uint64_t tolerance_ns = 100000000ULL;
    if (!requested || phase == 3) return;

    if (phase == 0) {
        // Exercise an explicitly requested sound test in normal runtime, after
        // initial desktop construction. The first-busy-desktop trial is kept
        // separately: it exposed existing long Ring0 work delaying PID0.
        // This is an observation gate, never a wait loop or substitute service.
        const uint64_t now = czas_monotoniczny_ns();
        if (!start_not_before_ns) {
            if (!hpet_dostepny()) { phase = 3; fail("monotonic clock unavailable"); return; }
            start_not_before_ns = now + 3000000000ULL;
            wypisz_log("[HDA-QEMU-REGRESSION] WARMUP_NS=3000000000 production deferred service continues normally");
            return;
        }
        if (now < start_not_before_ns) return;
        phase = 3;
        const uint32_t playback_controller = hda_wew_kontroler_odtwarzania();
        const uint32_t controller = playback_controller < hda_transport_liczba() ? playback_controller : 0;
        HdaDiagKontroler live{};
        const HdaDiagSnapshot* snapshot = hda_diag_pobierz_opublikowana(nullptr);
        if (!hda_transport_stan(controller, &live)) { fail("controller unavailable"); return; }
        Line ctrl; ctrl.add("[HDA-QEMU-REGRESSION] CONTROLLER PCI=");
        ctrl.number(live.vendor_id, 16, 4); ctrl.add(":"); ctrl.number(live.device_id, 16, 4);
        ctrl.add(" STATESTS_INITIAL=0x"); ctrl.number(live.initial_statests, 16, 4);
        ctrl.add(" DETECTED_MASK=0x"); ctrl.number(live.discovered_codec_mask, 16, 4);
        ctrl.field(" TRANSPORT=", live.transport); ctrl.field(" READY=", live.gotowy);
        ctrl.field(" CORB_SIZE=", live.corb_rozmiar); ctrl.field(" RIRB_SIZE=", live.rirb_rozmiar);
        ctrl.field(" CORB_RUN=", live.corb_run); ctrl.field(" RIRB_RUN=", live.rirb_run);
        ctrl.field(" CODECS=", snapshot && controller < snapshot->liczba_kontrolerow ?
                   snapshot->kontrolery[controller].liczba_kodekow : 0); ctrl.emit();
        Line state; state.add("[HDA-QEMU-REGRESSION] STATE GCTL=0x"); state.number(live.gctl, 16, 8);
        state.field(" INIT=", live.inicjalizacja); state.field(" FAILURE_STAGE=", live.etap_bledu);
        state.field(" TRANSPORT_STATUS=", live.status_transportu); state.field(" RECOVERIES=", live.odzyskiwania); state.emit();
        if (!snapshot || controller >= snapshot->liczba_kontrolerow || controller >= HDA_DIAG_MAKS_KONTROLEROW) {
            fail("completed codec snapshot unavailable"); return;
        }
        const auto& saved = snapshot->kontrolery[controller];

        const HdaDiagKodek* codec = nullptr;
        uint32_t codec_index = 0;
        for (uint32_t c = 0; c < saved.liczba_kodekow && c < HDA_DIAG_MAKS_KODEKOW; ++c) {
            const auto& candidate = snapshot->kodeki[controller][c];
            if (candidate.odpowiada && candidate.cad == 0) { codec = &candidate; codec_index = c; break; }
        }
        if (!codec) { fail("CAD0 GETPARAMETER did not return a valid codec"); return; }
        Line cd; cd.add("[HDA-QEMU-REGRESSION] CODEC CAD=0 VENDOR=0x");
        cd.number(codec->vendor_id, 16, 4); cd.add(" DEVICE=0x"); cd.number(codec->device_id, 16, 4);
        cd.field(" RESPONDS=", codec->odpowiada); cd.field(" FUNCTION_GROUPS=", codec->liczba_fg); cd.emit();
        uint32_t audio_groups = 0, routes = 0;
        for (uint32_t f = 0; f < codec->liczba_fg && f < HDA_DIAG_MAKS_FG; ++f) {
            const auto& fg = snapshot->fg[controller][codec_index][f];
            if (fg.audio) {
                ++audio_groups;
                Line af; af.add("[HDA-QEMU-REGRESSION] AFG");
                af.field(" NID=", fg.nid); af.field(" TYPE=", fg.typ);
                af.field(" WIDGETS=", fg.widget_count); af.emit();
            }
        }
        for (uint32_t r = 0; r < snapshot->liczba_sciezek && r < HDA_DIAG_MAKS_SCIEZEK; ++r) {
            const auto& route = snapshot->sciezki[r];
            if (route.kontroler_idx == controller && route.codec_cad == 0) ++routes;
        }
        Line route; route.add("[HDA-QEMU-REGRESSION] ROUTE");
        route.field(" COUNT=", routes); route.field(" TEST_AVAILABLE=", live.test_dostepny); route.emit();
        // Neither the flag nor an Intel controller alone authorizes guessed
        // hardware playback: positively identify the supported QEMU codec.
        if (live.vendor_id != 0x8086 || live.device_id != 0x2668 ||
            codec->vendor_id != 0x1AF4 || (codec->device_id != 0x0012 && codec->device_id != 0x0022) ||
            !(live.initial_statests & 1) || !(live.discovered_codec_mask & 1) ||
            !live.gotowy || live.transport != 2 || !live.corb_run || !live.rirb_run ||
            live.corb_rozmiar != 256 || live.rirb_rozmiar != 256 || playback_controller != controller ||
            saved.liczba_kodekow != 1 || !audio_groups || !routes || !live.test_dostepny || !hpet_dostepny()) {
            fail("exact Intel/QEMU transport, codec and route prerequisites not met"); return;
        }
        if (!hda_test_ton(440, 500)) { fail("500 ms timed tone refused"); return; }
        HdaWewStanTonu tone{}; HdaWewStanStrumienia stream{};
        hda_wew_pobierz_stan_tonu(&tone); hda_wew_pobierz_stan_strumienia(&stream);
        start_ns = czas_monotoniczny_ns(); deadline_ns = tone.deadline_ns;
        Line start; start.add("[HDA-QEMU-REGRESSION] TONE_START");
        start.field(" RUN=", stream.run); start.field(" ACTIVE=", tone.active);
        start.field(" DEADLINE_NS=", deadline_ns); start.field(" NOW_NS=", start_ns); start.emit();
        if (!stream.run || !tone.active || deadline_ns <= start_ns) {
            hda_stop(); fail("start did not leave a timed running stream"); return;
        }
        phase = 1;
        return;
    }

    const uint64_t now = czas_monotoniczny_ns();
    HdaWewStanTonu tone{}; HdaWewStanStrumienia stream{};
    hda_wew_pobierz_stan_tonu(&tone); hda_wew_pobierz_stan_strumienia(&stream);
    if (!before_seen && now - start_ns >= 200000000ULL && now < deadline_ns) {
        Line before; before.add("[HDA-QEMU-REGRESSION] TONE_BEFORE_DEADLINE");
        before.field(" RUN=", stream.run); before.field(" ACTIVE=", tone.active);
        before.field(" NOW_NS=", now); before.emit();
        if (!stream.run || !tone.active) { phase = 3; hda_stop(); fail("tone stopped before deadline"); return; }
        before_seen = true;
    }
    if (now < deadline_ns) return;
    if (tone.active && now - deadline_ns <= tolerance_ns) return;
    phase = 3;
    Line stop; stop.add("[HDA-QEMU-REGRESSION] TONE_AFTER_DEADLINE");
    stop.field(" RUN=", stream.run); stop.field(" ACTIVE=", tone.active);
    stop.field(" RUN_AFTER_STOP=", tone.run_after_stop); stop.add(" STOP_REASON=");
    stop.add(hda_ton_stop_nazwa(tone.stop_reason));
    stop.field(" LATE_NS=", now - deadline_ns); stop.emit();
    if (!before_seen || tone.active || stream.run || tone.run_after_stop ||
        tone.stop_reason != HDA_TON_STOP_DEADLINE || now - deadline_ns > tolerance_ns) {
        hda_stop(); fail("production deferred deadline/readback regression"); return;
    }
    wypisz_log("[HDA-QEMU-REGRESSION] PASS transport codec AFG route 500ms tone production deferred stop RUN=0");
}
