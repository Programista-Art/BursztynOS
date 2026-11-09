// Actual playback driver + same deferred entry as production PID0. PCI/codec
// transport and MMIO hardware effects are modeled; no diagnostic test tick.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "../sterowniki/dzwiek/hda.cpp"
#include "../sterowniki/dzwiek/hda_praca_odroczona.h"

namespace {
alignas(4096) uint8_t registers[16384];
alignas(4096) uint8_t bdl_page[4096];
alignas(4096) uint8_t sample_pages[HDA_MAKS_BUFOROW_DMA][4096];
uint64_t now_ns;
bool clock_ready;
bool refuse_stop;
uint32_t verbs;
unsigned stop_failure_logs;
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
    verbs = 0;
    stop_failure_logs = 0;
    hda_mmio_base = reinterpret_cast<uint64_t>(registers);
    hda_stream_base = hda_mmio_base + SD;
    hda_kontroler_odtwarzania = 0;
    hda_gcap = HDA_GCAP_64OK;
    hda_codec_addr = 0;
    hda_gotowy = true;
    hda_blokada = false;
    hda_praca_zaplanowana = false;
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
    put(SD + HDA_SD_LPIB, 4, 4);
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
        if (refuse_stop && offset == SD && size == 2 && run()) value |= HDA_SD_CTL_RUN;
        put(offset, value, size);
    }
}
void wypisz_log(const char* message) {
    if (std::strstr(message, "STOP niepotwierdzony")) ++stop_failure_logs;
}
bool hpet_dostepny() { return clock_ready; }
uint64_t czas_monotoniczny_ns() { return now_ns; }
void* FizycznyZakresDoWirtualnego(uint64_t physical, size_t) { return reinterpret_cast<void*>(physical); }
bool CzyZakresWMapowaniuFizycznym(uint64_t, size_t) { return true; }
const HdaDiagSnapshot* hda_diag_pobierz_opublikowana(uint32_t*) { return nullptr; }
HdaTransportStatus hda_transport_zglos(uint32_t, uint32_t, uint32_t) { ++verbs; return HDA_TRANSPORT_PENDING; }
HdaTransportStatus hda_transport_krok(uint32_t, uint32_t) { return HDA_TRANSPORT_SUCCESS; }
HdaTransportStatus hda_transport_odbierz(uint32_t, uint32_t, uint32_t* response) { *response = 0; return HDA_TRANSPORT_SUCCESS; }
void hda_transport_anuluj(uint32_t, uint32_t) {}

int main() {
    reset();
    // The startup tone uses a cyclic BDL; only the production deferred worker
    // stops it. The unrelated SD0 must be untouched when first OSS (SD4) stops.
    put(0x80, HDA_SD_CTL_RUN, 2);
    CHECK(hda_test_ton(440, 500));
    CHECK(run()); CHECK(tone().active == 1);
    const uint64_t deadline = tone().deadline_ns;
    CHECK(deadline == now_ns + 500000000ULL);
    now_ns = deadline - 1000000ULL;
    hda_wykonaj_prace_odroczona();
    CHECK(run()); CHECK(tone().active == 1);
    now_ns = deadline + 16000000ULL; // next scheduled PID0 visit: 16 ms tolerance
    hda_wykonaj_prace_odroczona();
    expect_stop(HDA_TON_STOP_DEADLINE);
    CHECK(tone().deadline_ns == deadline); // retain the deadline for diagnosis
    CHECK(get(0x80, 2) == HDA_SD_CTL_RUN);

    // The timer only schedules work; syscall boundaries execute it outside IRQ.
    reset(); CHECK(hda_test_ton(440, 500));
    now_ns = tone().deadline_ns - 1000000ULL;
    hda_zaplanuj_prace_odroczona();
    hda_wykonaj_zaplanowana_prace_odroczona();
    CHECK(run()); CHECK(!hda_praca_zaplanowana);
    now_ns = tone().deadline_ns + 16000000ULL;
    hda_zaplanuj_prace_odroczona();
    hda_zaplanuj_prace_odroczona(); // coalesce repeated timer requests
    CHECK(run()); // IRQ scheduling alone must not touch the hardware
    hda_wykonaj_zaplanowana_prace_odroczona();
    expect_stop(HDA_TON_STOP_DEADLINE);
    CHECK(!hda_praca_zaplanowana);
    CHECK(tone().stopped_ns == now_ns);

    reset(); CHECK(hda_test_ton(440, 500)); hda_stop(); expect_stop(HDA_TON_STOP_RECZNY);
    reset(); CHECK(hda_test_ton(440, 500));
    put(SD + HDA_SD_STS, HDA_SD_STS_DESE, 1);
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_BLAD_STRUMIENIA);
    reset(); CHECK(hda_test_ton(440, 500));
    hda_powiadom_reset_kontrolera(1); CHECK(run());
    hda_powiadom_reset_kontrolera(0); expect_stop(HDA_TON_STOP_RESET_KONTROLERA);
    reset(); CHECK(hda_test_ton(440, 500)); put(HDA_REG_GCTL, 0, 4);
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_RESET_KONTROLERA);

    reset(); clock_ready = false;
    CHECK(!hda_test_ton(440, 500)); CHECK(!run()); CHECK(verbs == 0);
    CHECK(!hda_wew_test_start(HDA_TEST_TON, 440, 500, 3)); CHECK(!run());
    reset(); CHECK(hda_test_ton(440, 500)); clock_ready = false;
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_BLAD_ZEGARA);

    reset(); CHECK(hda_wew_test_start(HDA_TEST_TON, 440, 500, 3));
    CHECK(run()); now_ns = tone().deadline_ns + 16000000ULL;
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_DEADLINE);
    // Diagnostic cleanup must preserve the deadline stop reason.
    hda_wew_test_stop(); CHECK(tone().stop_reason == HDA_TON_STOP_DEADLINE);

    reset(); CHECK(hda_test_ton(440, 500)); hda_blokada = true;
    hda_stop(); CHECK(run()); hda_blokada = false;
    hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_RECZNY);
    reset(); CHECK(hda_test_ton(440, 500)); now_ns = tone().deadline_ns + 1;
    hda_blokada = true; hda_wykonaj_prace_odroczona(); CHECK(run());
    hda_blokada = false; hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_DEADLINE);

    reset(); CHECK(hda_test_ton(440, 500)); refuse_stop = true;
    now_ns = tone().deadline_ns + 1;
    hda_wykonaj_prace_odroczona();
    CHECK(run()); CHECK(tone().active == 1); CHECK(tone().run_after_stop == 1);
    CHECK(tone().stop_reason == HDA_TON_STOP_DEADLINE);
    CHECK(stop_failure_logs == 1);
    hda_wykonaj_prace_odroczona();
    CHECK(run()); CHECK(tone().active == 1); CHECK(stop_failure_logs == 1);
    refuse_stop = false; hda_wykonaj_prace_odroczona(); expect_stop(HDA_TON_STOP_DEADLINE);

    reset(); s_legacy_qemu_potwierdzony = false;
    CHECK(!konfiguruj_kodek_qemu()); CHECK(verbs == 0);
    CHECK(!hda_test_ton(440, 500)); CHECK(!run()); CHECK(verbs == 0);
    HdaWewSciezkaTestu route{}; hda_wew_pobierz_sciezke_testu(&route);
    CHECK(route.konwerter_nid == 0); CHECK(route.pin_nid == 0);
    std::printf("HDA production tone: %u scenarios, %u checks PASS (ASan/UBSan; real driver, modeled MMIO)\n", scenarios, checks);
}
