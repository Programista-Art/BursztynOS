// Actual BWS 89/90 handlers and actual diagnostic scan; modeled IC transport.
#include "../bws_nowoczesne.cpp"
#include "../sterowniki/dzwiek/hda_wewnetrzny.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

static unsigned checks = 0, scenarios = 0, input_copies = 0, verbs = 0, starts = 0, stops = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
struct UserRange { uintptr_t start; size_t size; bool writable; };
static std::vector<UserRange> ranges;
static std::set<void*> allocations;
static bool allocation_fails = false, controller = true, codec = true, playing = false;
static uint64_t now = 1000000000ULL;
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
uint64_t czas_monotoniczny_ns() { return now; }
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
bool hda_wew_wyslij_immediate(uint32_t cmd, uint32_t* out) {
    ++verbs; const uint8_t nid = (cmd >> 20) & 0x7f; const uint16_t verb = (cmd >> 8) & 0xfff;
    const uint8_t param = cmd & 0xff; CHECK((cmd >> 28) == 0);
    CHECK(verb == 0xf00 || verb == 0xf02 || verb == 0xf05 || verb == 0xf1c);
    if (!codec) return false;
    *out = 0;
    if (verb == 0xf1c) { CHECK(nid == 3); *out = 0x01014010; return true; }
    if (verb == 0xf02) { CHECK(nid == 3); *out = 2; return true; }
    if (verb != 0xf00) return true;
    if (!nid && param == HDA_PAR_VENDOR_ID) *out = 0x1af40022;
    else if (!nid && param == HDA_PAR_REV_ID) *out = 0x00100100;
    else if (!nid && param == HDA_PAR_SUB_NODE_COUNT) *out = 0x00010001;
    else if (nid == 1 && param == HDA_PAR_FGROUP_TYPE) *out = 1;
    else if (nid == 1 && param == HDA_PAR_SUB_NODE_COUNT) *out = 0x00020002;
    else if (nid == 2 && param == HDA_PAR_WIDGET_CAP) *out = 1;
    else if (nid == 3 && param == HDA_PAR_WIDGET_CAP) *out = 0x400101;
    else if (nid == 3 && param == HDA_PAR_PIN_CAP) *out = 0x10;
    else if (nid == 3 && param == HDA_PAR_CONNLIST_LEN) *out = 1;
    return true;
}
bool hda_wew_send_verb_readonly(uint32_t verb, uint32_t* response) {
    return hda_wew_wyslij_immediate(verb, response);
}
bool hda_wew_aktywne_sondowanie_kodekow(uint16_t* responsive_mask) {
    if (!responsive_mask || !controller) return false;
    *responsive_mask = codec ? 1 : 0;
    return true;
}
bool hda_wew_sonda_pojedynczego_kodeka(uint8_t cad, bool* out_odpowiada, uint32_t* out_vendor) {
    if (!controller) return false;
    if (cad == 0 && codec) {
        if (out_odpowiada) *out_odpowiada = true;
        if (out_vendor) *out_vendor = 0x1af40022;
    } else {
        if (out_odpowiada) *out_odpowiada = false;
        if (out_vendor) *out_vendor = 0;
    }
    return true;
}
static void user(void* p, size_t n, bool writable = true) { ranges.push_back({reinterpret_cast<uintptr_t>(p), n, writable}); }
int main() {
    proces_t p{}; p.uprawnienia = PRAWO_GUI;
    auto snapshot = std::make_unique<HdaDiagSnapshot>(); user(snapshot.get(), sizeof(*snapshot));
    BwsHdaSnapshotZadanie request{}; request.wersja = HDA_DIAG_WERSJA_ABI; request.rozmiar = sizeof(request);
    request.bufor = reinterpret_cast<uint64_t>(snapshot.get()); request.bufor_maks = sizeof(*snapshot); user(&request, sizeof(request));
    HdaDiagSkan scan{}; user(&scan, sizeof(scan));
    const uint64_t req = reinterpret_cast<uint64_t>(&request), status = reinterpret_cast<uint64_t>(&scan);
    ++scenarios; CHECK(hda_diag_inicjalizuj());
    CHECK(!bws_hda_diag(p, BWS_HDA_DIAG_SNAPSHOT, req, 0, 0)); CHECK(allocations.empty());
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SKAN_START, 0, 0, 0));
    for (unsigned i = 0; i < 64; ++i) {
        (void)bws_hda_diag(p, BWS_HDA_DIAG_SKAN_KROK, 0, 0, 0);
        CHECK(bws_hda_diag(p, BWS_HDA_DIAG_STAN, status, 0, 0));
        if (!scan.aktywny) break;
    }
    CHECK(scan.gotowy); CHECK(!scan.aktywny); CHECK(verbs < 128);
    input_copies = 0; CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SNAPSHOT, req, 0, 0));
    CHECK(input_copies == 1); CHECK(allocations.empty()); CHECK(request.zapisano == sizeof(*snapshot));
    CHECK(snapshot->liczba_kontrolerow == 1); CHECK(snapshot->kontrolery[0].vendor_id == 0x8086);
    CHECK(snapshot->kontrolery[0].device_id == 0x2668); CHECK(snapshot->kontrolery[0].statests == 1);
    CHECK(snapshot->kodeki[0][0].vendor_id == 0x1af4); CHECK(snapshot->fg[0][0][0].audio);
    CHECK(snapshot->fg[0][0][0].widget_count == 2); CHECK(snapshot->widgety[0][0][0][1].typ == HDA_WID_PIN);
    CHECK(snapshot->widgety[0][0][0][1].liczba_polaczen == 1); CHECK(snapshot->widgety[0][0][0][1].polaczenia[0] == 2);
    CHECK(snapshot->liczba_sciezek > 0);
    /* Test anulowania skanu */
    ++scenarios;
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SKAN_START, 0, 0, 0));
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_STAN, status, 0, 0));
    CHECK(scan.aktywny);
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_SKAN_ANULUJ, 0, 0, 0));
    CHECK(bws_hda_diag(p, BWS_HDA_DIAG_STAN, status, 0, 0));
    CHECK(!scan.aktywny);
    CHECK(scan.przerwany);
    ++scenarios; auto good = request;
    request.bufor = UINT64_MAX - 4; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.bufor_maks = sizeof(*snapshot) - 1; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.bufor_maks = sizeof(*snapshot) + 4097; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.wersja = 0; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    request.rozmiar = 0; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); request = good;
    CHECK(!bws_hda_diag(p, 4, 1, 0, 0)); CHECK(!bws_hda_diag(p, 999, 0, 0, 0));
    ranges.back().writable = false; CHECK(!bws_hda_diag(p, 1, status, 0, 0)); ranges.back().writable = true;
    ranges[1].writable = false; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); ranges[1].writable = true;
    allocation_fails = true; CHECK(!bws_hda_diag(p, 4, req, 0, 0)); allocation_fails = false; CHECK(allocations.empty());
    ++scenarios; p.uprawnienia = 0;
    CHECK(!bws_hda_diag(p, 1, status, 0, 0)); CHECK(!bws_hda_diag(p, 2, 0, 0, 0)); CHECK(!bws_hda_test(p, 2, 0, 0, 0));
    p.uprawnienia = PRAWO_GUI;
    BwsHdaTestStart test{}; test.wersja = HDA_DIAG_WERSJA_ABI; test.rozmiar = sizeof(test);
    test.tryb = HDA_TEST_TON; test.czestotliwosc_hz = 440; test.czas_ms = 1500; test.kanaly = 3;
    user(&test, sizeof(test)); const uint64_t tptr = reinterpret_cast<uint64_t>(&test);
    ++scenarios; const auto correct = test;
    for (uint32_t mode : {uint32_t(HDA_TEST_TON), uint32_t(HDA_TEST_SWEEP), uint32_t(HDA_TEST_LEWY), uint32_t(HDA_TEST_PRAWY), uint32_t(HDA_TEST_OBA)}) {
        test.tryb = mode; CHECK(bws_hda_test(p, 1, tptr, 0, 0)); CHECK(playing);
        if (mode == HDA_TEST_LEWY) CHECK(last_channels == 1);
        if (mode == HDA_TEST_PRAWY) CHECK(last_channels == 2);
    }
    CHECK(starts == 5); CHECK(stops >= 5); now += 2000000000ULL;
    CHECK(!hda_diag_test_tick(now / 1000000)); CHECK(!playing);
    CHECK(bws_hda_test(p, 2, 0, 0, 0)); CHECK(bws_hda_test(p, 2, 0, 0, 0));
    ++scenarios;
    test = correct; test.tryb = 99; CHECK(!bws_hda_test(p, 1, tptr, 0, 0));
    test = correct; test.czestotliwosc_hz = 0; CHECK(!bws_hda_test(p, 1, tptr, 0, 0));
    test = correct; test.czestotliwosc_hz = 0xffffffff; CHECK(!bws_hda_test(p, 1, tptr, 0, 0));
    test = correct; test.czas_ms = 0; CHECK(!bws_hda_test(p, 1, tptr, 0, 0));
    test = correct; test.czas_ms = HDA_API_MAX_CZAS_MS + 1; CHECK(!bws_hda_test(p, 1, tptr, 0, 0));
    test = correct; test.kanaly = 4; CHECK(!bws_hda_test(p, 1, tptr, 0, 0));
    test = correct; test.wersja = 0; CHECK(!bws_hda_test(p, 1, tptr, 0, 0));
    CHECK(!bws_hda_test(p, 0xf00, tptr, 0, 0)); CHECK(!bws_hda_test(p, 1, 1, 0, 0)); CHECK(!playing);
    ++scenarios; hda_diag_inicjalizuj(); controller = false;
    CHECK(bws_hda_diag(p, 2, 0, 0, 0)); CHECK(bws_hda_diag(p, 1, status, 0, 0));
    CHECK(scan.blad == HDA_DIAG_BLAD_BRAK_KONTROLERA); CHECK(!scan.aktywny);
    controller = true; codec = false; hda_diag_inicjalizuj(); CHECK(bws_hda_diag(p, 2, 0, 0, 0));
    for (unsigned i = 0; i < 64; ++i) if (bws_hda_diag(p, 3, 0, 0, 0)) break;
    CHECK(bws_hda_diag(p, 4, req, 0, 0)); CHECK(snapshot->liczba_kontrolerow == 1);
    CHECK(!snapshot->kodeki[0][0].odpowiada); CHECK(allocations.empty());
    std::printf("Test dzwieku actual BWS89/90/HDA: %u scenarios, %u checks PASS; ASan+UBSan\n", scenarios, checks);
}
