// Actual BWS 89/90, scanner and transport against deterministic PCI/MMIO/DMA hardware.
#include "../bws_nowoczesne.cpp"
#include "../sterowniki/dzwiek/hda_wewnetrzny.h"
#include "hda_host_model.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

static unsigned checks = 0, scenarios = 0, input_copies = 0, starts = 0, stops = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
struct UserRange { uintptr_t start; size_t size; bool writable; };
static std::vector<UserRange> ranges;
static std::set<void*> allocations;
static bool allocation_fails = false, controller = true, codec = true, playing = false;
static HdaHostModel model;
static uint64_t& now = model.ns;
static uint8_t last_channels = 0;
static bool valid(const void* p, size_t n, bool write) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (!p || a > UINTPTR_MAX - n) return false;
    for (const auto& r : ranges) if ((!write || r.writable) && a >= r.start &&
            a - r.start <= r.size && n <= r.size - (a - r.start)) return true;
    return false;
}
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* p, size_t n) { return valid(p, n, true); }
bool skopiuj_z_przestrzeni_uzytkownika(void* dst, const void* src, size_t n) {
    ++input_copies; if (!valid(src, n, false)) return false; std::memcpy(dst, src, n); return true;
}
bool skopiuj_do_przestrzeni_uzytkownika(void* dst, const void* src, size_t n) {
    if (!valid(dst, n, true)) return false;
    std::memcpy(dst, src, n); return true;
}
void* kmalloc(size_t n) { if (allocation_fails) return nullptr; void* p = std::malloc(n); allocations.insert(p); return p; }
void kfree(void* p) { CHECK(allocations.erase(p) == 1); std::free(p); }
bool hpet_dostepny() { return true; }
uint64_t czas_monotoniczny_ns() { return HdaHostModel::clock(&model); }
uint32_t hda_wew_kontroler_odtwarzania(){return 0;}
bool inicjalizuj_hda() { return controller; }
bool hda_wew_kontroler_gotowy() { return controller; }
bool hda_wew_pobierz_stan_kontrolera(HdaWewStanKontrolera* out) {
    if (!controller) return false;
    *out = {}; out->slot = 27; out->vendor_id = 0x8086;
    out->device_id = 0x2668; out->gcap = 0x4401; out->statests = 1; out->oss = 4; out->dma64 = 1;
    out->initial_statests = 1;
    out->discovered_codec_mask = 1;
    out->responsive_codec_mask = codec ? 1 : 0;
    return true;
}
bool hda_wew_pobierz_stan_strumienia(HdaWewStanStrumienia* out) { *out = {}; out->run = playing; return controller; }
void hda_wew_pobierz_sciezke_testu(HdaWewSciezkaTestu* out) { *out = {0, 2, 3, 0}; }
bool hda_wew_test_start(uint32_t, uint32_t, uint32_t, uint8_t channels) {
    CHECK(!playing); ++starts; last_channels = channels; playing = controller; return playing;
}
void hda_wew_test_stop() { ++stops; playing = false; }
bool hda_wew_test_tick(uint64_t) { return playing; }
uint32_t hda_wew_verb12(uint8_t cad, uint8_t nid, uint16_t verb, uint8_t payload) {
    return uint32_t(cad) << 28 | uint32_t(nid) << 20 | uint32_t(verb) << 8 | payload;
}
uint32_t hda_wew_verb4(uint8_t cad, uint8_t nid, uint8_t verb, uint16_t payload) {
    return uint32_t(cad) << 28 | uint32_t(nid) << 20 | uint32_t(verb) << 16 | payload;
}
bool hda_wew_wyslij_immediate(uint32_t,uint32_t*){std::abort();}
static void user(void* p, size_t n, bool writable = true) { ranges.push_back({reinterpret_cast<uintptr_t>(p), n, writable}); }
int main() {
    model.add();model.init_all();hda_transport_test_dostepny(0,true);
    proces_t p{}; p.uprawnienia = PRAWO_GUI;
    auto snapshot = std::make_unique<HdaDiagSnapshot>(); user(snapshot.get(), sizeof(*snapshot));
    BwsHdaSnapshotZadanie request{}; request.wersja = HDA_DIAG_WERSJA_ABI; request.rozmiar = sizeof(request);
    request.bufor = reinterpret_cast<uint64_t>(snapshot.get()); request.bufor_maks = sizeof(*snapshot); user(&request, sizeof(request));
    HdaDiagSkan scan{}; user(&scan, sizeof(scan));
    const uint64_t req = reinterpret_cast<uint64_t>(&request), status = reinterpret_cast<uint64_t>(&scan);
    uint64_t wall_max=0;
    ++scenarios; CHECK(hda_diag_inicjalizuj());
    CHECK(!bws_hda_diag(p, BWS_HDA_DIAG_SNAPSHOT, req, 0, 0)); CHECK(allocations.size()==3);
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SKAN_START, 0, 0, 0));
    for (unsigned i = 0; i < 30000; ++i) {
        const auto before=model.devices[0].verbs;
        const auto start=std::chrono::steady_clock::now();
        (void)bws_hda_diag(p, BWS_HDA_DIAG_SKAN_KROK, 0, 0, 0);
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        wall_max=std::max(wall_max,uint64_t(elapsed));
        CHECK(model.devices[0].verbs-before<=1);now+=100000;
        CHECK(bws_hda_diag(p, BWS_HDA_DIAG_STAN, status, sizeof(scan), HDA_DIAG_WERSJA_ABI));
        if (!scan.aktywny) break;
    }
    CHECK(scan.gotowy); CHECK(!scan.aktywny); CHECK(model.devices[0].verbs < 128);
    input_copies = 0; CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SNAPSHOT, req, 0, 0));
    CHECK(input_copies == 1); CHECK(allocations.size()==3); CHECK(request.zapisano == sizeof(*snapshot));
    CHECK(snapshot->liczba_kontrolerow == 1); CHECK(snapshot->kontrolery[0].vendor_id == 0x8086);
    CHECK(snapshot->kontrolery[0].device_id == 0x2668); CHECK(snapshot->kontrolery[0].initial_statests == 1);
    CHECK(snapshot->kodeki[0][0].vendor_id == 0x1af4); CHECK(snapshot->fg[0][0][0].audio);
    CHECK(snapshot->fg[0][0][0].widget_count == 2); CHECK(snapshot->widgety[0][0][0][1].typ == HDA_WID_PIN);
    CHECK(snapshot->widgety[0][0][0][1].liczba_polaczen == 1); CHECK(snapshot->widgety[0][0][0][1].polaczenia[0] == 2);
    CHECK(snapshot->liczba_sciezek > 0);
    /* Test anulowania skanu */
    ++scenarios;
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SKAN_START, 0, 0, 0));
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_STAN, status, sizeof(scan), HDA_DIAG_WERSJA_ABI));
    CHECK(scan.aktywny);
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SKAN_ANULUJ, 0, 0, 0));
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_STAN, status, sizeof(scan), HDA_DIAG_WERSJA_ABI));
    CHECK(!scan.aktywny);
    CHECK(scan.przerwany);
    ++scenarios; BwsHdaSkanZadanie selected{HDA_DIAG_WERSJA_ABI,sizeof(BwsHdaSkanZadanie),0,0};
    user(&selected,sizeof(selected));const uint64_t selected_ptr=reinterpret_cast<uint64_t>(&selected);
    CHECK(!bws_hda_diag(p,BWS_HDA_DIAG_STAN,status,sizeof(scan)-1,HDA_DIAG_WERSJA_ABI));
    CHECK(!bws_hda_diag(p,BWS_HDA_DIAG_STAN,status,sizeof(scan),1));
    selected.wersja=1;CHECK(!bws_hda_diag(p,2,selected_ptr,0,0));selected.wersja=HDA_DIAG_WERSJA_ABI;
    selected.rozmiar=15;CHECK(!bws_hda_diag(p,2,selected_ptr,0,0));selected.rozmiar=sizeof(selected);
    selected.kontroler_idx=1;CHECK(!bws_hda_diag(p,2,selected_ptr,0,0));selected.kontroler_idx=0;
    selected.rezerwa=1;CHECK(!bws_hda_diag(p,2,selected_ptr,0,0));selected.rezerwa=0;
    CHECK(bws_hda_diag(p,2,selected_ptr,0,0));CHECK(bws_hda_diag(p,7,selected_ptr,0,0));
    ++scenarios; auto good = request;
    request.bufor = UINT64_MAX - 4; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.bufor_maks = sizeof(*snapshot) - 1; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.bufor_maks = sizeof(*snapshot) + 4097; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.wersja = 0; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.rozmiar = 0; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    CHECK(!bws_hda_diag(p, 4, 1, 0, 0)); CHECK(!bws_hda_diag(p, 999, 0, 0, 0));
    ranges[2].writable = false; CHECK(!bws_hda_diag(p, 1, status, sizeof(scan), HDA_DIAG_WERSJA_ABI)); ranges[2].writable = true;
    ranges[1].writable = false; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); ranges[1].writable = true;
    allocation_fails = true; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); allocation_fails = false; CHECK(allocations.size()==3);
    ++scenarios; p.uprawnienia = 0;
    CHECK(!bws_hda_diag(p, 1, status, sizeof(scan), HDA_DIAG_WERSJA_ABI)); CHECK(!bws_hda_diag(p, 2, 0, 0, 0)); CHECK(!bws_hda_test(p, 2, 0, 0, 0));
    p.uprawnienia = PRAWO_GUI;
    BwsHdaTestStart test{}; test.wersja = HDA_DIAG_WERSJA_ABI; test.rozmiar = sizeof(test);
    test.tryb = HDA_TEST_TON; test.czestotliwosc_hz = 440; test.czas_ms = 1500; test.kanaly = 3;
    user(&test, sizeof(test)); const uint64_t tptr = reinterpret_cast<uint64_t>(&test);
    ++scenarios; const auto correct = test;
    for (uint32_t mode : {uint32_t(HDA_TEST_TON), uint32_t(HDA_TEST_SWEEP), uint32_t(HDA_TEST_LEWY), uint32_t(HDA_TEST_PRAWY), uint32_t(HDA_TEST_OBA)}) {
        test.tryb = mode; CHECK(bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI)); CHECK(playing);
        if (mode == HDA_TEST_LEWY) CHECK(last_channels == 1);
        if (mode == HDA_TEST_PRAWY) CHECK(last_channels == 2);
    }
    CHECK(starts == 5); CHECK(stops >= 5); now += 2000000000ULL;
    CHECK(!hda_diag_test_tick(now / 1000000)); CHECK(!playing);
    CHECK(bws_hda_test(p, 2, 0, 0, 0)); CHECK(bws_hda_test(p, 2, 0, 0, 0));
    ++scenarios;
    test = correct; test.tryb = 99; CHECK(!bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI));
    test = correct; test.czestotliwosc_hz = 0; CHECK(!bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI));
    test = correct; test.czestotliwosc_hz = 0xffffffff; CHECK(!bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI));
    test = correct; test.czas_ms = 0; CHECK(!bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI));
    test = correct; test.czas_ms = HDA_API_MAX_CZAS_MS + 1; CHECK(!bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI));
    test = correct; test.kanaly = 4; CHECK(!bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI));
    test = correct; test.wersja = 0; CHECK(!bws_hda_test(p, 1, tptr, 0, HDA_DIAG_WERSJA_ABI));
    CHECK(!bws_hda_test(p, 0xf00, tptr, 0, 0)); CHECK(!bws_hda_test(p, 1, 1, 0, 0)); CHECK(!playing);
    ++scenarios; hda_transport_host_reset();model.devices.clear();model.init_all();hda_diag_inicjalizuj(); controller = false;
    CHECK(bws_hda_diag(p, 2, 0, 0, 0)); CHECK(bws_hda_diag(p, 1, status, sizeof(scan), HDA_DIAG_WERSJA_ABI));
    for(unsigned i=0;i<30000;++i){if(bws_hda_diag(p,3,0,0,0))break;now+=100000;}
    CHECK(bws_hda_diag(p,1,status,sizeof(scan),HDA_DIAG_WERSJA_ABI));
    CHECK(scan.blad == HDA_DIAG_BLAD_BRAK_KONTROLERA); CHECK(!scan.aktywny);
    hda_transport_host_reset();model.add();model.devices[0].codecs.clear();model.init_all();hda_transport_test_dostepny(0,true);
    controller = true; codec = false; hda_diag_inicjalizuj(); CHECK(bws_hda_diag(p, 2, 0, 0, 0));
    for (unsigned i = 0; i < 30000; ++i) {if (bws_hda_diag(p, 3, 0, 0, 0)) break;now+=100000;}
    CHECK(bws_hda_diag(p, 4, req, 0, 0)); CHECK(snapshot->liczba_kontrolerow == 1);
    CHECK(!snapshot->kodeki[0][0].odpowiada);CHECK(snapshot->kontrolery[0].gotowy);
    CHECK(snapshot->kontrolery[0].blad==HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA);CHECK(allocations.size()==3);
    BwsHdaMetryki metrics{};user(&metrics,sizeof(metrics));
    CHECK(bws_hda_diag(p,BWS_HDA_DIAG_METRYKI,reinterpret_cast<uint64_t>(&metrics),sizeof(metrics),HDA_DIAG_WERSJA_ABI));
    std::printf("Actual BWS89 maximum host elapsed %llu ns; instrumented service %llu ns\n",(unsigned long long)wall_max,(unsigned long long)metrics.maks_serwis_ns);
    hda_transport_host_reset();
    std::printf("Test dzwieku actual BWS89/90/HDA: %u scenarios, %u checks PASS; ASan+UBSan\n", scenarios, checks);
}
