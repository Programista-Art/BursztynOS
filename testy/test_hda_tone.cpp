// Actual playback driver + same deferred entry as production PID0. PCI/codec
// transport and MMIO hardware effects are modeled; no diagnostic test tick.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "../sterowniki/dzwiek/hda.cpp"
#include "../sterowniki/dzwiek/hda_praca_odroczona.h"
#include "../jadro/bws/pcm_jadro_api.h"

namespace {
alignas(4096) uint8_t registers[16384];
alignas(4096) uint8_t bdl_page[4096];
alignas(4096) uint8_t sample_pages[HDA_MAKS_BUFOROW_DMA][4096];
uint64_t now_ns;
bool clock_ready;
bool refuse_stop;
bool refuse_start;
unsigned clock_reads_until_pause;
uint32_t verbs;
uint32_t ostatni_verb;
bool transport_zajety;
uint32_t amp_verbs;
unsigned starts, stops;
unsigned checks, scenarios;
constexpr uint32_t SD = 0x100; // ISS=4: first output descriptor, never SD0
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
void put(uint32_t offset, uint32_t value, uint8_t size) {
    std::memcpy(registers + offset, &value, size);
}
uint32_t get(uint32_t offset, uint8_t size) {
    uint32_t value = 0;
    std::memcpy(&value, registers + offset, size);
    return value;
}
void reset() {
    ++scenarios;
    std::memset(registers, 0, sizeof(registers));
    now_ns = 1000000000ULL;
    clock_ready = true;
    refuse_stop = false;
    refuse_start = false;
    clock_reads_until_pause = 0;
    verbs = 0;
    ostatni_verb = 0; transport_zajety = false; amp_verbs = 0;
    s_wzmacniacz = HdaGlosnosc::Wzmacniacz{};
    s_glosnosc = HdaGlosnosc::Stan{};
    starts = stops = 0;
    pcm_mixer.cleanup(pcm_internal);
    pcm_mixer.cleanup(pcm_legacy);
    pcm_clients = 0;
    pcm_tone = {};
    pcm_running = pcm_retry = pcm_shutdown = false;
    pcm_idle_since = pcm_retry_ns = pcm_watchdog_ns = 0;
    pcm_watchdog_fired = false;
    pcm_stats = {};
    pcm_shortfall_count = 0;
    DiagOpoznieniaIrq::disable();
    hda_mmio_base = reinterpret_cast<uint64_t>(registers);
    hda_stream_base = hda_mmio_base + SD;
    hda_kontroler_odtwarzania = 0;
    hda_gcap = HDA_GCAP_64OK;
    hda_codec_addr = 0;
    hda_gotowy = true;
    hda_blokada = false;
    pcm_interest = false;
    pcm_pending_ns = pcm_last_priority_request = 0;
    pcm_guard_misses = pcm_guard_max_ns = 0;
    pcm_irq_timing = {};
    pcm_recovery_since = pcm_watchdog_time = pcm_worker_request = 0;
    s_sciezka_skonfigurowana = false;
    s_legacy_qemu_potwierdzony = true;
    s_ton_aktywny = false;
    s_ton_deadline_ns = 0;
    s_ton_stop_powod = HDA_TON_STOP_BRAK;
    s_ton_stop_zadany = HDA_TON_STOP_BRAK;
    s_ton_run_po_stop = 0;
    hda_bdl = reinterpret_cast<HdaBdlWpis*>(bdl_page);
    hda_bdl_phys = bdl_page;
    for (unsigned i = 0; i < HDA_MAKS_BUFOROW_DMA; ++i) hda_bufory_phys[i] = sample_pages[i];
    put(HDA_REG_GCTL, 1, 4);
    put(SD + HDA_SD_STS, HDA_SD_STS_FIFORDY, 1);
    put(SD + HDA_SD_LPIB, 0, 4);
}
HdaWewStanTonu tone() {
    HdaWewStanTonu value{};
    hda_wew_pobierz_stan_tonu(&value);
    return value;
}
bool run() { return (get(SD, 2) & HDA_SD_CTL_RUN) != 0; }
void expect_stop(uint32_t reason) {
    CHECK(!run());
    CHECK(!hda_ton_aktywny());
    CHECK(tone().active == 0);
    CHECK(tone().stop_reason == reason);
    CHECK(tone().run_after_stop == 0);
}
}

// Only the device boundary is replaced. All deadline/START/STOP/readback and
// tone state transitions above come from the production hda.cpp translation unit.
void hda_host_mmio_zapis(uint32_t offset, uint8_t size, uint32_t value) {
    if (offset == SD + HDA_SD_STS || offset == HDA_REG_STATESTS) {
        put(offset, get(offset, size) & ~value, size); // real W1C semantics
    } else {
        if (offset == SD && size == 2) {
            if (refuse_stop && run()) value |= HDA_SD_CTL_RUN;
            if (refuse_start && !run()) value &= ~HDA_SD_CTL_RUN;
            if ((value & HDA_SD_CTL_RUN) && !run()) ++starts;
            if (!(value & HDA_SD_CTL_RUN) && run()) ++stops;
            if (value & HDA_SD_CTL_SRST) put(SD + HDA_SD_LPIB, 0, 4);
        }
        put(offset, value, size);
    }
}
void wypisz_log(const char*) {}
bool hpet_dostepny() { return clock_ready; }
uint64_t czas_monotoniczny_ns() {
    if (clock_reads_until_pause && --clock_reads_until_pause == 0) now_ns += HdaPcm::MAX_GAP_NS;
    return now_ns;
}
void* FizycznyZakresDoWirtualnego(uint64_t physical, size_t) { return reinterpret_cast<void*>(physical); }
bool CzyZakresWMapowaniuFizycznym(uint64_t, size_t) { return true; }
const HdaDiagSnapshot* hda_diag_pobierz_opublikowana(uint32_t*) { return nullptr; }
HdaTransportStatus hda_transport_zglos(uint32_t, uint32_t, uint32_t cmd) {
    if (transport_zajety) return HDA_TRANSPORT_BUSY;
    ++verbs; ostatni_verb = cmd;
    if (((cmd >> 16) & 0xF) == 0x3) ++amp_verbs;          // SET_AMP_GAIN_MUTE (verb 4-bitowy 0x3)
    return HDA_TRANSPORT_PENDING;
}
bool hda_transport_stan(uint32_t, HdaDiagKontroler* k) { if (!k) return false; *k = HdaDiagKontroler{}; k->vendor_id = 0x8086; k->device_id = 0x2668; return true; }
HdaTransportStatus hda_transport_krok(uint32_t, uint32_t) { return HDA_TRANSPORT_SUCCESS; }
HdaTransportStatus hda_transport_odbierz(uint32_t, uint32_t, uint32_t* response) { *response = 0; return HDA_TRANSPORT_SUCCESS; }
void hda_transport_anuluj(uint32_t, uint32_t) {}

static void period(uint32_t index, uint64_t advance = HdaPcm::PERIOD_NS) {
    now_ns += advance;
    put(SD + HDA_SD_LPIB, index * HdaPcm::PERIOD_BYTES, 4);
    hda_zaplanuj_prace_odroczona();
    hda_wykonaj_zaplanowana_prace_odroczona();
}
static Pcm::Frame first(uint32_t slot) {
    Pcm::Frame frame{};
    std::memcpy(&frame, sample_pages[slot], sizeof(frame));
    return frame;
}

static uintptr_t pcm_user_begin, pcm_user_end;
static bool pcm_copy_ok(void* out, const void* in, size_t size) {
    const auto address = reinterpret_cast<uintptr_t>(in);
    if (pcm_user_end && (address < pcm_user_begin || address > pcm_user_end || size > pcm_user_end-address)) return false;
    if (!in || !out || reinterpret_cast<uintptr_t>(in) < 4096) return false;
    std::memcpy(out, in, size);
    return true;
}
static bool pcm_copy_fail(void* out, const void* in, size_t size) {
    if (size) std::memcpy(out, in, size / 2);
    return false;
}
static void test_pcm_api() {
    reset();
    const Pcm::Owner owner{42, 7}, stranger{43,7}, reused{42,8};
    BwsPcmFormat fmt{1,sizeof(fmt),48000,2,1,0};
    auto invoke=[&](Pcm::Owner o,uint64_t op,uint64_t a,uint64_t b=0,uint64_t c=0) {
        return bws_pcm_obsluz(o,op,a,b,c,pcm_copy_ok,pcm_copy_ok);
    };
    const auto address=reinterpret_cast<uint64_t>(&fmt);
    CHECK(invoke(owner,PCM_OPEN,0,sizeof(fmt),1)==PCM_MEMORY);
    CHECK(invoke(owner,PCM_OPEN,address,sizeof(fmt),2)==PCM_INVALID);
    fmt.rate=44100;
    CHECK(invoke(owner,PCM_OPEN,address,sizeof(fmt),1)==PCM_UNSUPPORTED);
    fmt.rate=48000;
    const auto h=invoke(owner,PCM_OPEN,address,sizeof(fmt),1);
    CHECK(h>0);
    const auto second=invoke(owner,PCM_OPEN,address,sizeof(fmt),1);
    CHECK(second>0 && invoke(owner,PCM_OPEN,address,sizeof(fmt),1)==PCM_LIMIT);
    CHECK(invoke(owner,PCM_CLOSE,second)==PCM_OK);
    CHECK(invoke(owner,PCM_CLOSE,second)==PCM_HANDLE);
    CHECK(invoke(owner,99,h)==PCM_INVALID);
    CHECK(invoke(owner,PCM_WRITE,h,0,UINT64_MAX)==PCM_INVALID);
    CHECK(invoke(owner,PCM_WRITE,h,0,0)==PCM_INVALID);
    const auto other=invoke(stranger,PCM_OPEN,address,sizeof(fmt),1);
    CHECK(other>0 && other!=h);
    BwsPcmState state{};
    CHECK(invoke(stranger,PCM_QUERY,h,reinterpret_cast<uint64_t>(&state),sizeof(state))==PCM_HANDLE);
    CHECK(invoke(reused,PCM_CLOSE,h)==PCM_HANDLE);
    CHECK(invoke(owner,PCM_GAIN,h,101,0)==PCM_INVALID);
    CHECK(invoke(owner,PCM_GAIN,h,25,1)==PCM_OK);
    CHECK(invoke(owner,PCM_QUERY,h,reinterpret_cast<uint64_t>(&state),sizeof(state))==PCM_OK);
    CHECK(state.volume==25 && state.muted==1 && state.queued==0);
    int16_t samples[512];
    for(auto& v:samples) v=1000;
    const auto data=reinterpret_cast<uint64_t>(samples);
    CHECK(invoke(owner,PCM_WRITE,h,data,UINT64_MAX)==PCM_INVALID);
    CHECK(invoke(owner,PCM_WRITE,h,UINT64_MAX-10,256)==PCM_MEMORY);
    CHECK(invoke(owner,PCM_WRITE,h,1,256)==PCM_MEMORY);
    CHECK(bws_pcm_obsluz(owner,PCM_WRITE,h,data,256,pcm_copy_fail,pcm_copy_ok)==PCM_MEMORY);
    pcm_user_begin = reinterpret_cast<uintptr_t>(data);
    pcm_user_end = pcm_user_begin + 512; // first page suffix mapped, following page absent
    CHECK(invoke(owner,PCM_WRITE,h,reinterpret_cast<uint64_t>(data)+508,256)==PCM_MEMORY);
    pcm_user_end = 0;

    CHECK(invoke(owner,PCM_QUERY,h,reinterpret_cast<uint64_t>(&state),sizeof(state))==PCM_OK && state.queued==0);
    for(unsigned i=0;i<16;++i) CHECK(invoke(owner,PCM_WRITE,h,data,256)==256);
    CHECK(invoke(owner,PCM_WRITE,h,data,1)==PCM_FULL);
    Pcm::Frame consumed[10];
    CHECK(pcm_mixer.mix(consumed,10)==Pcm::OK);
    CHECK(invoke(owner,PCM_WRITE,h,data,256)==10);
    CHECK(invoke(owner,PCM_WRITE,h,data,1)==PCM_FULL);

    CHECK(invoke(owner,PCM_GAIN,h,100,0)==PCM_OK);
    hda_wykonaj_prace_odroczona();
    CHECK(first(0).left==1000);
    CHECK(invoke(owner,PCM_WRITE,h,data,256)==256);
    CHECK(invoke(stranger,PCM_WRITE,other,data,200)==200);
    period(1);
    CHECK(invoke(stranger,PCM_WRITE,other,data,256)==256);
    CHECK(invoke(owner,PCM_FINISH,h)==PCM_OK);
    CHECK(invoke(owner,PCM_GAIN,h,25,1)==PCM_OK);
    CHECK(invoke(owner,PCM_QUERY,h,reinterpret_cast<uint64_t>(&state),sizeof(state))==PCM_OK &&
        state.volume==25 && state.muted==1);
    hda_blokada=true;
    CHECK(!pcm_posprzataj_proces(owner.pid,owner.generation));
    CHECK(invoke(owner,PCM_CLOSE,h)==PCM_BUSY);
    hda_blokada=false;
    CHECK(pcm_posprzataj_proces(owner.pid,owner.generation));
    CHECK(pcm_posprzataj_proces(owner.pid,owner.generation));
    CHECK(invoke(owner,PCM_CLOSE,h)==PCM_HANDLE);
    CHECK(invoke(stranger,PCM_QUERY,other,reinterpret_cast<uint64_t>(&state),sizeof(state))==PCM_OK);
    CHECK(pcm_clients==1);
    CHECK(pcm_posprzataj_proces(owner.pid,owner.generation));
    CHECK(pcm_posprzataj_proces(owner.pid,owner.generation));
    CHECK(pcm_clients==1);
    CHECK(invoke(stranger,PCM_FINISH,other)==PCM_OK);
    CHECK(invoke(stranger,PCM_CLOSE,other)==PCM_OK);
    CHECK(pcm_clients==0);
}
int main() {
    test_pcm_api();
    using namespace PcmJadro;
    reset();
    put(0x80, HDA_SD_CTL_RUN, 2); // unrelated input stream
    CHECK(hda_test_ton(440, 500));
    CHECK(run() && tone().active == 1 && starts == 1);
    const uint64_t deadline = tone().deadline_ns;
    CHECK(deadline == now_ns + 500000000ULL);
    for (unsigned i = 1; i <= 23; ++i) period(i % HdaPcm::PERIODS);
    CHECK(run() && tone().active == 1 && starts == 1);
    now_ns = deadline + 1000000;
    hda_wykonaj_prace_odroczona();
    expect_stop(HDA_TON_STOP_DEADLINE);
    CHECK(tone().deadline_ns == deadline && tone().stopped_ns == now_ns);
    CHECK(get(0x80, 2) == HDA_SD_CTL_RUN && stops == 1);

    reset(); CHECK(hda_wew_test_start(HDA_TEST_LEWY, 440, 500, 1));
    Pcm::Frame sample{};
    std::memcpy(&sample, sample_pages[0] + sizeof(sample), sizeof(sample));
    CHECK(sample.left != 0 && sample.right == 0);
    CHECK(hda_wew_test_start(HDA_TEST_SWEEP, 0, 500, 3));
    CHECK(starts == 1 && stops == 0);
    hda_stop(); expect_stop(HDA_TON_STOP_RECZNY);

    reset(); CHECK(hda_test_ton(440, 500));
    put(SD + HDA_SD_STS, HDA_SD_STS_DESE, 1);
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_BLAD_STRUMIENIA);
    CHECK(pcm_stats.bledy_dma == 1);
    CHECK(pcm_stats.descriptor_error == 1 && pcm_stats.fifo_error == 0);
    reset(); CHECK(hda_test_ton(440, 500));
    hda_powiadom_reset_kontrolera(1); CHECK(run());
    hda_powiadom_reset_kontrolera(0); expect_stop(HDA_TON_STOP_RESET_KONTROLERA);
    CHECK(!hda_test_ton(440, 100));
    reset(); CHECK(hda_test_ton(440, 500)); put(HDA_REG_GCTL, 0, 4);
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_RESET_KONTROLERA);

    reset(); clock_ready = false;
    CHECK(!hda_test_ton(440, 500)); CHECK(!run()); CHECK(verbs == 0);
    CHECK(!hda_wew_test_start(HDA_TEST_TON, 440, 500, 3)); CHECK(!run());
    reset(); CHECK(hda_test_ton(440, 500)); clock_ready = false;
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_BLAD_ZEGARA);

    reset(); CHECK(hda_test_ton(440, 500));
    hda_blokada = true;
    hda_stop(); CHECK(run()); hda_blokada = false;
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_RECZNY);

    reset();
    Uchwyt a, b, extra;
    CHECK(otworz(a) == Wynik::Ok && otworz(b) == Wynik::Ok);
    CHECK(otworz(extra) == Wynik::Limit);
    int16_t left[Pcm::CAPACITY_FRAMES * 2], right[Pcm::CAPACITY_FRAMES * 2];
    for (unsigned i = 0; i < Pcm::CAPACITY_FRAMES; ++i) {
        left[i * 2] = 4000; left[i * 2 + 1] = -8000;
        right[i * 2] = 1000; right[i * 2 + 1] = 1000;
    }
    uint32_t accepted = 0;
    CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok && accepted == Pcm::CAPACITY_FRAMES);
    CHECK(zapisz(b, right, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok && accepted == Pcm::CAPACITY_FRAMES);
    hda_wykonaj_prace_odroczona();
    CHECK(starts == 1 && run());
    CHECK(get(SD + HDA_SD_CBL, 4) == HdaPcm::RING_BYTES);
    CHECK(get(SD + HDA_SD_LVI, 2) == HdaPcm::PERIODS - 1);
    for (unsigned i = 0; i < HdaPcm::PERIODS; ++i) {
        CHECK(hda_bdl[i].adres == reinterpret_cast<uint64_t>(sample_pages[i]));
        CHECK(hda_bdl[i].dlugosc == HdaPcm::PERIOD_BYTES);
        if (i < HdaPcm::PRIME) CHECK(first(i).left == 5000 && first(i).right == -7000);
        else CHECK(first(i).left == 0 && first(i).right == 0);
    }
    CHECK(zapisz(b, right, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    CHECK(glosnosc(b, 25, false) == Wynik::Ok);
    hda_blokada = true;
    CHECK(zamknij(a) == Wynik::Zajete);
    period(1);
    CHECK(first(0).left == 5000 && first(1).left == 5000);
    CHECK(hda_pcm_praca_oczekuje());
    hda_wykonaj_zaplanowana_prace_odroczona();
    hda_wykonaj_zaplanowana_prace_odroczona();
    const auto requested = pcm_pending_ns;
    now_ns += 12000000;
    hda_zaplanuj_prace_odroczona(7, false);
    CHECK(pcm_pending_ns == requested && pcm_stats.blokady_pracy == 3);
    CHECK(hda_pcm_pilny_powrot_pid0());
    CHECK(!hda_pcm_pilny_powrot_pid0()); // no repeated priority for the same request
    hda_blokada = false;
    CHECK(zamknij(a) == Wynik::Ok && zamknij(a) == Wynik::ZlyUchwyt);
    hda_wykonaj_zaplanowana_prace_odroczona(); // no new timer IRQ
    CHECK(!hda_pcm_praca_oczekuje() && pcm_stats.maks_czekanie_ns == 12000000);
    CHECK(first(0).left == 0 && first(0).right == 0); // retired and scrubbed
    CHECK(first(1).left == 5000); // link still on slot 1
    CHECK(first(2).left == 5000); // ready for the hardware
    CHECK(first(4).left == 250 && first(4).right == 250);

    // Cross the old two-period deadline. A moderate delay still leaves
    // the programmed data safe. Refill the remaining lead without RUN=0.
    period(3, 2 * HdaPcm::PERIOD_NS + 1);
    CHECK(first(1).left == 0 && first(2).left == 0 && first(3).left == 5000);
    CHECK(first(5).left == 250 && first(6).left == 250);
    CHECK(starts == 1 && pcm_stats.opoznienia == 1 && pcm_stats.odzyskania == 0);
    CHECK(pcm_stats.utracone_okresy == 0 && pcm_stats.bezpieczna_cisza == 0);
    CHECK(pcm_irq_timing.max_gap >= HdaPcm::LATE_NS);
    period(4);
    CHECK(first(4).left == 250 && first(7).left == 250);
    CHECK(hda_wew_test_start(HDA_TEST_TON, 440, 500, 3));
    CHECK(starts == 1 && glosnosc(b, 100, false) == Wynik::Ok);
    period(5);
    std::memcpy(&sample, sample_pages[0] + sizeof(sample), sizeof(sample));
    CHECK(sample.left != 0 && sample.left == sample.right);
    hda_stop();
    CHECK(!hda_ton_aktywny() && run() && stops == 0 && starts == 1);
    CHECK(zamknij(b) == Wynik::Ok);
    period(6);
    CHECK(first(2).left == 0 && first(2).right == 0);
    CHECK(first(5).left == 0 && starts == 1);
    period(7);
    period(HdaPcm::PERIODS); // legal LPIB==CBL and wrap
    CHECK(pcm_ring.current() == HdaPcm::PERIODS && starts == 1);

    // A whole missed lap must not permit any writes, even if the reported
    // LPIB matches the old value or a hardware stop request is refused.
    auto checksum = [&] {
        uint64_t c = 5381;
        for (const auto& page : sample_pages)
            for (uint8_t value : page) c = c * 33 + value;
        return c;
    };
    CHECK(otworz(b) == Wynik::Ok);
    auto sum = checksum();
    refuse_stop = true;
    const uint64_t late_before = pcm_stats.opoznienia;
    period(0, HdaPcm::STALL_NS + 1);
    CHECK(run() && pcm_retry && checksum() == sum);
    period(1);
    CHECK(pcm_retry && checksum() == sum && starts == 1);
    refuse_stop = false;
    period(2);
    CHECK(!run() && checksum() == sum);
    hda_wykonaj_prace_odroczona();
    CHECK(run() && starts == 2 && pcm_stats.opoznienia == late_before + 1 && pcm_stats.odzyskania == 1);
    CHECK(pcm_stats.przyczyny[static_cast<unsigned>(HdaPcm::Fault::Watchdog)] == 1);
    CHECK(pcm_stats.zapisane_zdarzenia == 1 && pcm_events[0].ns >= pcm_events[0].termin_ns);
    for (unsigned i = 0; i < HdaPcm::PERIODS; ++i) CHECK(first(i).left == 0 && first(i).right == 0);
    CHECK(wylacz() && !run() && otworz(b) == Wynik::Niedostepne);

    using HdaPcm::Action;
    using HdaPcm::Fault;
    HdaPcm::Ring ring;
    ring.start(1);
    CHECK(ring.observe(HdaPcm::PERIOD_BYTES, 1 + HdaPcm::PERIOD_NS) == Fault::None);
    CHECK(ring.next() == Action::Scrub);
    ring.scrubbed();
    CHECK(ring.next() == Action::Fill);
    ring.published();
    CHECK(ring.next() == Action::Fill);
    ring.published();
    CHECK(ring.next() == Action::Wait);
    CHECK(ring.observe(ring.lpib, ring.sampled_ns + HdaPcm::MAX_GAP_NS) == Fault::Ambiguous);
    ring.start(1);
    CHECK(ring.observe(HdaPcm::RING_BYTES + 4, 2) == Fault::Position);
    CHECK(ring.observe(HdaPcm::RING_BYTES - 4, 2) == Fault::Position);
    // A multi-period catch-up within a nonambiguous interval cannot reuse a
    // previously filled slot that has entered the prefetch window.
    CHECK(ring.observe(6 * HdaPcm::PERIOD_BYTES, 1 + HdaPcm::PERIOD_NS * 5) == Fault::None);
    CHECK(ring.next() == Action::Recover);
    ring.start(1);
    CHECK(ring.observe(4 * HdaPcm::PERIOD_BYTES, 1 + HdaPcm::PERIOD_NS * 4) == Fault::None);
    for (unsigned i = 0; i < 4; ++i) { CHECK(ring.next() == Action::Scrub); ring.scrubbed(); }
    for (unsigned i = 0; i < 3; ++i) { CHECK(ring.next() == Action::Skip); ++ring.fill; }
    CHECK(ring.next() == Action::Fill && ring.fill == 7);

    // The timer may verify a slow hardware cursor at the projected deadline,
    // but may not extend past the ambiguity limit of the original observation.
    reset();
    const uint64_t underruns_at_reset = pcm_mixer.underrun_frames();
    CHECK(otworz(a) == Wynik::Ok && otworz(b) == Wynik::Ok);
    CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    CHECK(zapisz(b, right, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    hda_wykonaj_prace_odroczona();
    CHECK(first(4).left == 0 && pcm_mixer.underrun_frames() == underruns_at_reset);
    const uint64_t oldest_sample = pcm_ring.sampled_ns;
    now_ns = pcm_watchdog_ns;
    put(SD + HDA_SD_LPIB, 2 * HdaPcm::PERIOD_BYTES, 4);
    hda_zaplanuj_prace_odroczona();
    CHECK(run() && pcm_irq_timing.extensions == 1 && hda_pcm_praca_oczekuje());
    CHECK(pcm_irq_timing.rechecks == 1);
    CHECK(pcm_watchdog_ns == oldest_sample + HdaPcm::MAX_GAP_NS);
    hda_wykonaj_zaplanowana_prace_odroczona();
    CHECK(run() && starts == 1 && pcm_stats.odzyskania == 0);
    CHECK(first(2).left == 5000); // link currently on slot 2
    CHECK(pcm_mixer.underrun_frames() > underruns_at_reset && !pcm_stats.bledy_dma);
    CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    CHECK(zapisz(b, right, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    now_ns = pcm_ring.sampled_ns + HdaPcm::MAX_GAP_NS;
    hda_zaplanuj_prace_odroczona();
    CHECK(!run() && pcm_irq_timing.extensions == 1);
    CHECK(pcm_irq_timing.rechecks == 2);
    CHECK(first(2).left == 5000); // timer does not touch sample memory
    hda_wykonaj_zaplanowana_prace_odroczona();
    hda_wykonaj_prace_odroczona();
    CHECK(run() && starts == 2 && pcm_stats.odzyskania == 1);
    CHECK(first(0).left == 5000 && first(0).right == -7000); // both queues survived STOP
    CHECK(zamknij(a) == Wynik::Ok && zamknij(b) == Wynik::Ok);

    reset();
    CHECK(otworz(a) == Wynik::Ok && otworz(b) == Wynik::Ok);
    hda_wykonaj_prace_odroczona();
    CHECK(starts == 0 && !run()); // opening does not commit empty DMA periods
    const uint64_t underruns_before_start = pcm_mixer.underrun_frames();
    CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    CHECK(zapisz(b, right, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    refuse_start = true;
    hda_wykonaj_prace_odroczona();
    CHECK(starts == 0 && pcm_mixer.queue_levels().minimum == Pcm::CAPACITY_FRAMES);
    CHECK(pcm_mixer.underrun_frames() == underruns_before_start);
    refuse_start = false;
    period(0);
    CHECK(run() && starts == 1 && first(0).left == 5000);

    // Do not fill the optional fifth period with a partial producer block.
    CHECK(zapisz(a, left, HdaPcm::PERIOD_FRAMES / 2, accepted) == Wynik::Ok);
    CHECK(zapisz(b, right, HdaPcm::PERIOD_FRAMES / 2, accepted) == Wynik::Ok);
    hda_wykonaj_prace_odroczona();
    CHECK(first(4).left == 0 && pcm_mixer.queue_levels().minimum == HdaPcm::PERIOD_FRAMES / 2);
    CHECK(zapisz(a, left, HdaPcm::PERIOD_FRAMES / 2, accepted) == Wynik::Ok);
    CHECK(zapisz(b, right, HdaPcm::PERIOD_FRAMES / 2, accepted) == Wynik::Ok);

    // Time is lost between mixing in CPU memory and rechecking LPIB. No
    // pages can be written, and the samples are still queued for the restart.
    sum = checksum();
    clock_reads_until_pause = 3;
    CHECK(pcm_refill() == Fault::Ambiguous);
    CHECK(checksum() == sum && pcm_mixer.queue_levels().minimum == HdaPcm::PERIOD_FRAMES);
    pcm_recover(now_ns, Fault::Ambiguous);
    hda_wykonaj_prace_odroczona();
    CHECK(starts == 2 && first(0).left == 5000 && first(0).right == -7000);
    CHECK(first(1).left == 0 && first(4).left == 0); // no replay of the previous ring

    reset();
    CHECK(otworz(a) == Wynik::Ok && otworz(b) == Wynik::Ok);
    CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    CHECK(zapisz(b, right, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    hda_wykonaj_prace_odroczona();
    CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    CHECK(zapisz(b, right, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
    DiagOpoznieniaIrq::enable();
    period(4, 4 * HdaPcm::PERIOD_NS);
    CHECK(starts == 1 && pcm_stats.bezpieczna_cisza == 3 && pcm_stats.utracone_okresy == 3);
    CHECK(first(4).left == 0 && first(5).left == 0 && first(6).left == 0);
    CHECK(first(7).left == 5000 && first(0).left == 5000);
    CHECK(pcm_mixer.queue_levels().minimum == 2 * HdaPcm::PERIOD_FRAMES);
    SladPcm skip{};
    CHECK(slad_pcm(0, skip) && skip.rodzaj == BrakPcm::WlasnoscDma && skip.pcm_dostepne);
    CHECK(skip.okres == skip.bajty_dma / HdaPcm::PERIOD_BYTES && skip.minimum_kolejki == Pcm::CAPACITY_FRAMES);
    DiagOpoznieniaIrq::disable();

    // ---- Glosnosc systemowa (Bursztynowy Zasobnik): prawdziwe verby wzmacniacza, stan tylko po zapisie do sprzetu
    {
        reset();
        HdaGlosnoscInfo info{};
        CHECK(konfiguruj_kodek_qemu());                                   // potwierdzony kodek QEMU: DAC NID 2, 0x4A
        CHECK(s_wzmacniacz.regulowany() && s_wzmacniacz.nid == QEMU_NID_DAC && amp_verbs == 1);
        hda_glosnosc_info(&info);
        CHECK(info.urzadzenie && info.backend == HDA_GLOSNOSC_WZMACNIACZ && info.wyciszanie && info.procent == 100 && info.kroki == 0x4A);
        CHECK(info.pci_vendor == 0x8086 && info.pci_device == 0x2668);
        const uint32_t g0 = info.generacja;
        auto amp = [](uint32_t payload) { return (uint32_t(QEMU_NID_DAC) << 20) | (0x3U << 16) | payload; };
        CHECK(hda_glosnosc_ustaw(40) == HDA_GLOSNOSC_OK && ostatni_verb == amp(HdaGlosnosc::AMP_OUT_LR | 30));
        hda_glosnosc_info(&info); CHECK(info.procent == 40 && info.generacja == g0 + 1);
        CHECK(hda_wyciszenie_ustaw(true) == HDA_GLOSNOSC_OK && ostatni_verb == amp(HdaGlosnosc::AMP_OUT_LR | HdaGlosnosc::AMP_MUTE | 30));
        hda_glosnosc_info(&info); CHECK(info.wyciszony && info.procent == 40);
        CHECK(hda_wyciszenie_ustaw(false) == HDA_GLOSNOSC_OK && ostatni_verb == amp(HdaGlosnosc::AMP_OUT_LR | 30));   // poprzedni poziom
        CHECK(hda_glosnosc_ustaw(0) == HDA_GLOSNOSC_OK && ostatni_verb == amp(HdaGlosnosc::AMP_OUT_LR | HdaGlosnosc::AMP_MUTE));
        CHECK(hda_glosnosc_ustaw(101) == HDA_GLOSNOSC_ZLE_ARGUMENTY);
        // zajety transport: ZAJETE, stan i generacja bez zmian (nigdy "udane" bez zapisu)
        CHECK(hda_glosnosc_ustaw(70) == HDA_GLOSNOSC_OK);
        transport_zajety = true;
        hda_glosnosc_info(&info); const uint32_t g1 = info.generacja;
        CHECK(hda_glosnosc_ustaw(20) == HDA_GLOSNOSC_ZAJETE && hda_wyciszenie_ustaw(true) == HDA_GLOSNOSC_ZAJETE);
        hda_glosnosc_info(&info); CHECK(info.procent == 70 && !info.wyciszony && info.generacja == g1);
        transport_zajety = false;
        // ponowna konfiguracja trasy (np. przed tonem) zachowuje poziom uzytkownika
        CHECK(konfiguruj_kodek_qemu());
        CHECK(s_glosnosc.procent == 70);
        // ton odtwarza sie przy dowolnym poziomie (sprzetowy backend: probki bez zmian)
        CHECK(hda_test_ton(440, 500) && run()); hda_stop();
        // wyciszenie sprzetowe nie zatrzymuje strumienia (bit Mute wzmacniacza), odciszenie wraca do poziomu
        CHECK(hda_test_ton(440, 500) && run());
        CHECK(hda_wyciszenie_ustaw(true) == HDA_GLOSNOSC_OK && run() && (ostatni_verb & HdaGlosnosc::AMP_MUTE));
        CHECK(hda_wyciszenie_ustaw(false) == HDA_GLOSNOSC_OK && !(ostatni_verb & HdaGlosnosc::AMP_MUTE));
        hda_stop();
        // brak urzadzenia
        hda_gotowy = false;
        CHECK(hda_glosnosc_ustaw(50) == HDA_GLOSNOSC_NIEOBSLUGIWANE);
        hda_glosnosc_info(&info); CHECK(!info.urzadzenie && info.backend == HDA_GLOSNOSC_BRAK);
        hda_gotowy = true;
        // Software master gain is applied to a complete mixed period.
        s_wzmacniacz = HdaGlosnosc::Wzmacniacz{};
        const uint32_t v = verbs;
        CHECK(hda_glosnosc_ustaw(50) == HDA_GLOSNOSC_OK && verbs == v);
        hda_glosnosc_info(&info); CHECK(info.backend == HDA_GLOSNOSC_PCM && info.procent == 50);
        CHECK(otworz(a) == Wynik::Ok);
        CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
        hda_wykonaj_prace_odroczona();
        CHECK(first(0).left == 2000 && first(0).right == -4000);
        CHECK(zapisz(a, left, Pcm::CAPACITY_FRAMES, accepted) == Wynik::Ok);
        CHECK(hda_wyciszenie_ustaw(true) == HDA_GLOSNOSC_OK && run());
        period(1); CHECK(first(4).left == 0 && first(4).right == 0);
        CHECK(pcm_stats.okresy_master_mute > 0 && pcm_stats.bledy_dma == 0);
        CHECK(hda_wyciszenie_ustaw(false) == HDA_GLOSNOSC_OK && run());
        period(2); CHECK(first(6).left == 2000 && first(6).right == -4000);
    }
    std::printf("HDA production tone: %u scenarios, %u checks PASS (ASan/UBSan; real driver, modeled MMIO)\n", scenarios, checks);
}
