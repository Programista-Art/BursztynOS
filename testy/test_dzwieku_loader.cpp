// Execute production PSF snapshot, boot package installer and complete loader.
// Only privileged CPU operations, PMM/VMM and kernel stacks are host substitutes.
#include "../jadro/fs/psf.cpp"
#include "psf_bootstrap.h"
#include "scheduler.h"
#include "stos_jadra.h"
#include "podpis_paczki.h"
#include "bur_v2.h"
#include "uklad_procesu.h"
#include "vmm_zakres.h"
#include "blob_skompresowany.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

static unsigned checks = 0, scenarios = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
static std::vector<std::string> logs;
static std::set<void*> allocations, frames;
static std::set<void*> psf_allocations;
static size_t allocation_calls = 0, largest_allocation = 0;
static bool fail_allocation = false;
static int frame_budget = -1;
static int map_budget = -1;
void* kmalloc(size_t n) {
    ++allocation_calls;
    if (fail_allocation) return nullptr;
    largest_allocation = std::max(largest_allocation, n);
    void* p = std::malloc(n); if (p) allocations.insert(p); return p;
}
void kfree(void* p) { CHECK(allocations.erase(p) == 1); std::free(p); }
void wypisz_log(const char* s) { logs.emplace_back(s); }
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
bool blokowy_zapis_instalatora() { return false; }
bool skopiuj_string_z_uzytkownika(char* dst, const char* src, size_t cap) {
    if (!dst || !src || std::strlen(src) >= cap) return false;
    std::memcpy(dst, src, std::strlen(src) + 1); return true;
}
proces_t tablica_procesow[MAKS_PROCESOW]{};
struct Page { uint8_t* bytes; uint64_t flags; };
static int original_root;
static void* current_root = &original_root;
static std::map<void*, std::map<uint64_t, Page>> spaces;
static std::map<int, void*> stacks;
void* ZaalokujRamke() {
    if (!frame_budget) return nullptr;
    if (frame_budget > 0) --frame_budget;
    void* p = std::aligned_alloc(4096, 4096);
    if (p) { std::memset(p, 0xa5, 4096); frames.insert(p); } return p;
}
void ZwolnijRamke(void* p) { CHECK(frames.erase(p) == 1); std::free(p); }
void* FizycznyZakresDoWirtualnego(uint64_t p, size_t n) {
    CHECK(n <= 4096); CHECK(frames.count(reinterpret_cast<void*>(p)) == 1);
    return reinterpret_cast<void*>(p);
}
static bool vmm_zainicjalizowany = true;
static bool nx_aktywny = true, secure_entropy = true;
static constexpr uint64_t PTE_WRITE=2,PTE_PRESENT=1,PTE_USER=4,PTE_NX=1ULL<<63;
extern "C" bool entropia_pobierz_bajty(uint8_t* out,size_t n) {
    if(!secure_entropy)return false;
    static uint64_t seed=1;
    for(size_t i=0;i<n;++i) { seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;out[i]=uint8_t(seed); }
    return true;
}
struct BlokadaVMM { ~BlokadaVMM() {} };
static uint64_t* pml4_z_cr3() { return static_cast<uint64_t*>(current_root); }
/* The loader resets per-process FPU state; the host has no hardware to reset. */
void fpu_reset_process(int) {}
extern "C" void scheduler_nowy_proces_w_slocie(int pid) {
    CHECK(pid > 0 && pid < MAKS_PROCESOW);
}
extern "C" bool tozsamosc_systemowa_zweryfikuj(const char*, const uint8_t*, uint64_t) {
    return false;
}
extern "C" bool VmmZakresWolnyWBazie(uint64_t start, uint64_t end) {
    // The fixture models only the base identity map and per-process pages.
    return vmm_zainicjalizowany && end > start && start >= (1ULL << 32);
}
/*
 * Host stand-in for the kernel entropy-based ASLR offset.
 *
 * Returns a deterministic but non-zero offset so the test can assert that the
 * stack base is NOT the compile-time constant. Returning 0 everywhere would
 * silently disable ASLR coverage.
 */
extern "C" unsigned long stack_protector_losowy_offset_stron(
    unsigned long maks_stron, unsigned long sol) {
    if (maks_stron <= 1) return 0;
    return ((sol * 2654435761UL + 12345UL) % (maks_stron - 1)) + 1;
}
/*
 * Host stand-in for package signature verification.
 *
 * The loader regression focuses on BUR header validation, W^X mapping and
 * ASLR layout - not on RSA. Real RSA verification is covered by
 * test_podpis_paczki.py. The injectable result keeps this loader test hermetic
 * (no mbedTLS dependency) without weakening the production path.
 */
static PodpisPaczkiWynik signature_result = PodpisPaczkiWynik::Ok;
static unsigned signature_calls;
static bool check_signature_before_mapping;
extern "C" void loader_ustaw_podpis_wymagany(bool);
extern "C" PodpisPaczkiWynik podpis_paczki_weryfikuj(const char*, const uint8_t*, uint64_t) {
    ++signature_calls;
    if (check_signature_before_mapping) CHECK(frames.empty() && stacks.empty() && spaces.empty());
    return signature_result;
}
extern "C" const char* podpis_paczki_blad(PodpisPaczkiWynik) {
    return "ok";
}
struct WynikWalkera { bool obecna, user; };
static WynikWalkera sprawdz_mapowanie(uint64_t* root, uint64_t address) {
    const auto& pages = spaces.at(root);
    const auto it = pages.find(address);
    if (it != pages.end()) return {true, bool(it->second.flags & FLAGA_USER)};
    // The real base CR3 has supervisor identity mappings below 4 GiB.
    return {address < (1ULL << 32), false};
}
static bool mapuj_strone_wewnetrznie(uint64_t* root, uint64_t address, uint64_t pa, uint64_t flags) {
    /*
     * Loader mapuje w NIEAKTYWNEJ przestrzeni nowego procesu (celowo, aby
     * nie przelaczac CR3). Stub musi to dopuszczac, ale tylko dla PML4,
     * ktory powstal przez UtworzPrzestrzenAdresowaProcesu().
     */
    CHECK(spaces.count(root) == 1);
    CHECK(root != reinterpret_cast<uint64_t*>(&original_root));
    CHECK(!(address & 4095)); CHECK(frames.count(reinterpret_cast<void*>(pa)));
    if (!map_budget) return false;
    if (map_budget > 0) --map_budget;
    CHECK(spaces[root].emplace(address, Page{reinterpret_cast<uint8_t*>(pa), flags}).second);
    return true;
}
#include "../.hda-test/repair/vmm_actual.inc"
void* UtworzPrzestrzenAdresowaProcesu() {
    void* p = std::malloc(1); spaces.emplace(p, std::map<uint64_t, Page>{}); return p;
}
void UstawPrzestrzenAdresowa(void* root) { current_root = root; }
extern "C" void* PobierzAktualnePML4() { return current_root; }
bool WirtualnyDoFizycznego(const void* root, uint64_t* out) {
    *out = reinterpret_cast<uint64_t>(root); return true;
}
extern "C" void ZniszczPrzestrzenAdresowaProcesu(void* root) {
    CHECK(root != current_root);
    for (auto& item : spaces.at(root)) ZwolnijRamke(item.second.bytes);
    CHECK(spaces.erase(root) == 1); std::free(root);
}
bool stos_jadra_przydziel(int pid, uint64_t* base, uint64_t* top) {
    void* p = std::calloc(1, 65536); CHECK(stacks.emplace(pid, p).second);
    *base = reinterpret_cast<uint64_t>(p); *top = *base + 65536; return true;
}
void stos_jadra_zwolnij(int pid) { std::free(stacks.at(pid)); stacks.erase(pid); }

static BlobZ::Strumien blob_strumien;
void powitanie_punkt_startu() {}
#include "../.hda-test/repair/package_actual.inc"
static constexpr const char* path = "/programy/test_dzwieku.cebula/test_dzwieku.bur";
static constexpr const char* bad_path = "/programy/test_dzwieku.cebula/invalid.bur";
static constexpr uint64_t rights = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ;
static std::vector<uint8_t> executable;
static NaglowekBur header;

// --- Package install / repair isolation (production instaluj_paczke, zapisz_plik_od_nowa, planuj_plik) ---
// Unrelated files the audio-test package must never touch: system configuration, both A/B slots, another
// application's resources and user media (music, photos). Contents include NUL bytes and are not text.
struct Sentinel { const char* path; std::vector<uint8_t> bytes; };
static std::vector<Sentinel> sentinels;
static std::vector<uint8_t> read_whole(const char* p) {
    const uint32_t n = rozmiar_pliku(p);
    std::vector<uint8_t> out(n); uint32_t got = 0;
    if (n) CHECK(czytaj_fragment_pliku(p, 0, reinterpret_cast<char*>(out.data()), n, &got) && got == n);
    return out;
}
static void plant_sentinels() {
    const char* paths[] = {"/system/konfig/motyw.cfg", "/sloty/a/system/konfig/slot.cfg",
                           "/sloty/b/system/konfig/slot.cfg", "/programy/notatnik.cebula/opis.aplikacji",
                           "/dane/muzyka/utwor.mp3", "/dane/zdjecia/zdjecie.png"};
    uint32_t seed = 0x13579bdfu;
    for (const char* p : paths) {
        Sentinel s{p, {}}; s.bytes.resize(4096 + 977 * (sentinels.size() + 1));
        for (auto& b : s.bytes) { seed = seed * 1664525u + 1013904223u; b = uint8_t(seed >> 24); }
        CHECK(zapisz_plik_od_nowa_impl(p, reinterpret_cast<const char*>(s.bytes.data()), s.bytes.size()));
        sentinels.push_back(std::move(s));
    }
}
static void check_sentinels() {
    for (const auto& s : sentinels) CHECK(read_whole(s.path) == s.bytes);
}
// Not ASan-instrumented: the local buffer must live on the real stack (ASan may move locals to a heap "fake
// stack"), exactly like a stack-local manifest in the kernel planning frame (installer error 7).
__attribute__((noinline, no_sanitize_address)) static bool plan_from_stack_frame() {
    volatile char local[64];
    for (auto& c : local) c = 'x';
    planowanie_instalacji = true; plan_granica_stosu = biezacy_rsp() + 4096;
    const bool ok = zapisz_plik_od_nowa("/programy/test_dzwieku.cebula/z_stosu",
                                        const_cast<const char*>(local), sizeof(local));
    plan_granica_stosu = 0; planowanie_instalacji = false;
    return ok;
}
static const char* const manifest_path = "/programy/test_dzwieku.cebula/opis.aplikacji";
static void install_repair_isolation() {
    const auto manifest_installed = read_whole(manifest_path);
    CHECK(manifest_installed.size() == sizeof(MANIFEST_TEST_DZWIEKU) - 1);
    CHECK(!std::memcmp(manifest_installed.data(), MANIFEST_TEST_DZWIEKU, manifest_installed.size()));
    check_sentinels();

    // Repair: damaged executable and truncated manifest are rewritten byte-exact; nothing else changes.
    ++scenarios;
    std::vector<uint8_t> damaged(executable.begin(), executable.begin() + executable.size() / 3);
    for (size_t i = 0; i < damaged.size(); i += 97) damaged[i] ^= 0x5a;
    CHECK(zapisz_plik_od_nowa_impl(path, reinterpret_cast<const char*>(damaged.data()), damaged.size()));
    CHECK(zapisz_plik_od_nowa_impl(manifest_path, "nazwa", 5));
    CHECK(boot_install(executable.data(), executable.data() + executable.size()));
    CHECK(read_whole(path) == executable); CHECK(read_whole(manifest_path) == manifest_installed);
    check_sentinels();

    // Strict installer mode (SYSTEMFILES): every written file gets a control record (path, size, CRC-32 of the
    // bytes written) from the shared InstalacjaPlikow module; exactly the two package files are recorded.
    ++scenarios;
    scisla_instalacja = true; pliki_instalacja_ok = true; liczba_plikow_kontrolnych = 0;
    CHECK(boot_install(executable.data(), executable.data() + executable.size()));
    CHECK(liczba_plikow_kontrolnych == 2);
    CHECK(!std::strcmp(pliki_kontrolne[0].sciezka, manifest_path));
    CHECK(pliki_kontrolne[0].rozmiar == manifest_installed.size());
    CHECK(pliki_kontrolne[0].crc == Storage::crc32(manifest_installed.data(), manifest_installed.size()));
    CHECK(!std::strcmp(pliki_kontrolne[1].sciezka, path));
    CHECK(pliki_kontrolne[1].rozmiar == executable.size());
    CHECK(pliki_kontrolne[1].crc == Storage::crc32(executable.data(), executable.size()));
    // A failed earlier file poisons strict mode: the package is NOT written over a good installation.
    CHECK(zapisz_plik_od_nowa_impl(path, reinterpret_cast<const char*>(damaged.data()), damaged.size()));
    pliki_instalacja_ok = false; liczba_plikow_kontrolnych = 0;
    CHECK(!boot_install(executable.data(), executable.data() + executable.size()));
    CHECK(liczba_plikow_kontrolnych == 0); CHECK(read_whole(path) == damaged);
    scisla_instalacja = false; pliki_instalacja_ok = true;
    check_sentinels();

    // Stepwise plan (installer planning phase): files are only planned, no PSF write happens; the plan points at
    // the caller's bytes and snapshots the CRC of small raw sources (manifest), not of the 64 KiB+ executable.
    ++scenarios;
    planowanie_instalacji = true; planowane_liczba = 0; planowane_bajty_razem = 0;
    CHECK(boot_install(executable.data(), executable.data() + executable.size()));
    planowanie_instalacji = false;
    CHECK(planowane_liczba == 2); CHECK(read_whole(path) == damaged);
    CHECK(!std::strcmp(planowane_pliki[0].path, manifest_path) && planowane_pliki[0].data == MANIFEST_TEST_DZWIEKU);
    CHECK(planowane_pliki[0].zrodlo.znane &&
          planowane_pliki[0].zrodlo.crc == Storage::crc32(MANIFEST_TEST_DZWIEKU, sizeof(MANIFEST_TEST_DZWIEKU) - 1));
    CHECK(!std::strcmp(planowane_pliki[1].path, path) && !planowane_pliki[1].skompresowany);
    CHECK(planowane_pliki[1].data == reinterpret_cast<const char*>(executable.data()));
    CHECK(planowane_pliki[1].size == executable.size() && !planowane_pliki[1].zrodlo.znane);
    CHECK(planowane_bajty_razem == manifest_installed.size() + executable.size());
    // Plan source on a dying stack frame is rejected with a diagnosis instead of being written later.
    {
        logs.clear();
        CHECK(!plan_from_stack_frame());
        CHECK(planowane_liczba == 2 && !pliki_instalacja_ok);
        CHECK(diagnoza_instalacji.przyczyna == InstalacjaPlikow::Przyczyna::ZrodloNietrwale);
        CHECK(logs.size() == 1); CHECK(rozmiar_pliku("/programy/test_dzwieku.cebula/z_stosu") == 0);
        pliki_instalacja_ok = true;
    }
    planowane_liczba = 0; planowane_bajty_razem = 0;

    // Restore the good installation for the loader scenarios below.
    CHECK(boot_install(executable.data(), executable.data() + executable.size()));
    CHECK(read_whole(path) == executable);
    check_sentinels();
    std::printf("install/repair isolation PASS (repair, strict CRC records, plan-only, stack source rejected)\n");
}

static void release_process(int pid) {
    auto& p = tablica_procesow[pid];
    ZniszczPrzestrzenAdresowaProcesu(reinterpret_cast<void*>(p.cr3));
    stos_jadra_zwolnij(pid); p = {};
}
static void clean() {
    CHECK(current_root == &original_root); CHECK(allocations == psf_allocations);
    CHECK(spaces.empty()); CHECK(stacks.empty()); CHECK(frames.empty());
    for (int i = 1; i < MAKS_PROCESOW; ++i) CHECK(tablica_procesow[i].stan == PROCES_PUSTY);
    CHECK(!psf_test_lock_held());
}
static void failure(const std::vector<uint8_t>& data, const char* reason) {
    ++scenarios;
    CHECK(zapisz_plik_od_nowa(bad_path, reinterpret_cast<const char*>(data.data()), data.size()));
    const size_t calls = allocation_calls; logs.clear();
    CHECK(!bws_uruchom_program_z_pliku(bad_path, 3, rights, false));
    CHECK(logs.size() == 1); CHECK(logs[0].size() < 640);
    CHECK(logs[0].find("[BUR FAIL] path='") == 0);
    CHECK(logs[0].find(std::string("reason=") + reason) != std::string::npos);
    for (const char* field : {"file=", "max=134217728", "magic=", "version=", "text_off=",
            "text_file=", "data_off=", "data_file=", "zero_fill=", "mapped="})
        CHECK(logs[0].find(field) != std::string::npos);
    CHECK(logs[0].find("LOADER-DBG") == std::string::npos);
    CHECK(allocation_calls == calls); clean();
    std::printf("negative PASS %s\n", reason);
}
static void mutate(void (*edit)(NaglowekBur&), const char* reason) {
    auto bytes = executable; auto n = header; edit(n);
    std::memcpy(bytes.data(), &n, sizeof(n)); failure(bytes, reason);
}
static void mapping() {
    ++scenarios; logs.clear(); largest_allocation = 0; const size_t before = allocation_calls;
    CHECK(bws_uruchom_program_z_pliku(path, 3, rights, false));
    CHECK(allocation_calls == before + 1); CHECK(largest_allocation == executable.size());
    auto& p = tablica_procesow[1]; CHECK(p.stan == PROCES_GOTOWY);
    CHECK(p.uprawnienia == rights); CHECK(p.poziom_zaufania == 3);
    CHECK(p.cr3 != reinterpret_cast<uint64_t>(&original_root));
    auto* frame = reinterpret_cast<RejestryStanowe*>(p.kernel_rsp);
    CHECK(frame->adres_powrotu == header.punkt_wejscia);
    CHECK(frame->rejestr_cs == 0x23); CHECK(frame->stary_ss == 0x1b);
    /*
     * ASLR stosu: szczyt musi lezec w dozwolonym oknie i byc wyrownany,
     * a nie rowny stalej kompilacyjnej. Test NIE zaklada konkretnej
     * wartosci, bo zalezy ona od entropii.
     */
    CHECK(frame->stary_rsp > RING3_STACK_TOP - 4096UL * 4096UL);
    CHECK(frame->stary_rsp <= RING3_STACK_TOP + 4 * 4096 - 8);
    CHECK(((frame->stary_rsp + 8) & 0xFFF) == 0);
    CHECK((frame->rflags & 0x202) == 0x202);
    auto& pages = spaces.at(reinterpret_cast<void*>(p.cr3));
    /* Stack size is now 64 pages (256 KiB); see LICZBA_STRON_STOSU_USER. */
    CHECK(pages.size() == (header.tekst_rozmiar + header.dane_rozmiar) / 4096 + 64);
    auto verify = [&](uint64_t va, uint64_t memory, uint64_t off, uint64_t stored, bool writable) {
        for (uint64_t i = 0; i < memory; i += 4096) {
            const auto& page = pages.at(va + i);
            CHECK((page.flags & (FLAGA_USER | FLAGA_OBECNA)) == (FLAGA_USER | FLAGA_OBECNA));
            CHECK(bool(page.flags & FLAGA_ZAPIS) == writable);
            /*
             * W^X: dane nigdy nie sa wykonywalne, kod nigdy nie jest
             * zapisywalny. Regresja, ktora usunie NX ze stron danych,
             * musi zatrzymac ten test.
             */
            CHECK(bool(page.flags & VMM_FLAGA_NX) == writable);
            for (uint64_t j = 0; j < 4096; ++j) {
                const auto expected = i + j < stored ? executable.at(off + i + j) : 0;
                CHECK(page.bytes[j] == expected);
            }
        }
    };
    verify(header.tekst_wirtualny, header.tekst_rozmiar, header.tekst_przesuniecie, header.tekst_rozmiar, false);
    verify(header.dane_wirtualny, header.dane_rozmiar, header.dane_przesuniecie,
           std::min<uint64_t>(header.dane_rozmiar, executable.size() - header.dane_przesuniecie), true);
    for (uint64_t va : {RING3_BASE - 4096, RING3_HEAP_BASE, RING3_STACK_TOP - 4096,
                        RING3_STACK_TOP + 4 * 4096, uint64_t(0xffff800000000000ULL)}) CHECK(!pages.count(va));
#if BURSZTYNDIAGTESTDZWIEKU
    CHECK(logs.size() == 5);
    for (const char* stage : {"BUR file accepted", "process created", "Ring3 pages mapped",
                             "entry point validated", "process scheduled (ready)"}) {
        bool found = false; for (const auto& line : logs) if (line.find(stage) != std::string::npos) found = true;
        CHECK(found);
    }
#else
    CHECK(logs.empty());
#endif
    // The real loader has prepared iretq at the linked _start. Host tests do
    // not execute iretq or claim a real CPU transition into Ring 3.
    std::printf("loader PASS ready pid=1 RIP=0x%llx pages=%zu file=%zu zero_fill=%llu\n",
        (unsigned long long)frame->adres_powrotu, pages.size(), executable.size(),
        (unsigned long long)(header.dane_rozmiar - (executable.size() - header.dane_przesuniecie)));
    ++scenarios;
    CHECK(zapisz_plik_od_nowa(bad_path, reinterpret_cast<const char*>(executable.data()), executable.size()));
    CHECK(bws_uruchom_program_z_pliku(bad_path, 3, rights, false));
    auto& second = tablica_procesow[2]; CHECK(second.stan == PROCES_GOTOWY); CHECK(second.cr3 != p.cr3);
    const auto& second_pages = spaces.at(reinterpret_cast<void*>(second.cr3));
    CHECK(second_pages.at(header.tekst_wirtualny).bytes != pages.at(header.tekst_wirtualny).bytes);
    CHECK(!std::memcmp(second_pages.at(header.tekst_wirtualny).bytes, pages.at(header.tekst_wirtualny).bytes, 4096));
    release_process(2);
    release_process(1); clean();
}
int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 3); std::ifstream file(argv[1], std::ios::binary);
    executable.assign(std::istreambuf_iterator<char>(file), {}); CHECK(executable.size() >= sizeof(header));
    std::memcpy(&header, executable.data(), sizeof(header));
    CHECK(executable.size() < 512 * 1024); CHECK(!bur_blad_naglowka(header, executable.size()));
    std::vector<uint8_t> ram(16 * 1024 * 1024); inicjalizuj_psf(ram.data(), ram.size());
    CHECK(psf_czy_gotowy()); for (const char* dir : KATALOGI_SYSTEMOWE) CHECK(zapewnij_katalog_startowy(dir));
    plant_sentinels();
    psf_allocations = allocations; // persistent production PSF cache
    // Same final bytes and verbatim boot installation call; .cebula is a PSF
    // directory, not an archive with a second executable hidden in it.
    CHECK(boot_install(executable.data(), executable.data() + executable.size()));
    CHECK(rozmiar_pliku(path) == executable.size());
    std::vector<uint8_t> installed(executable.size()); uint32_t read = 0;
    CHECK(czytaj_fragment_pliku(path, 0, reinterpret_cast<char*>(installed.data()), installed.size(), &read));
    CHECK(read == installed.size()); CHECK(installed == executable);
    PakietManifest manifest{}; CHECK(pakiet_wczytaj_manifest(path, &manifest));
    CHECK(manifest.poziom_zaufania == 3); CHECK(manifest.uprawnienia == rights);
    CHECK(!std::strcmp(manifest.plik_startowy, "test_dzwieku.bur"));
    install_repair_isolation();
    ++scenarios;
    BurObrazPliku a{}, b{};
    CHECK(bsp_wczytaj_obraz_bur(path, &a, BUR_MAKS_ROZMIAR_PLIKU, bur_blad_naglowka));
    CHECK(bsp_wczytaj_obraz_bur(path, &b, BUR_MAKS_ROZMIAR_PLIKU, bur_blad_naglowka));
    CHECK(a.dane != b.dane); CHECK(a.rozmiar == executable.size());
    CHECK(!std::memcmp(a.dane, executable.data(), executable.size())); kfree(a.dane); kfree(b.dane);
    ++scenarios;
    check_signature_before_mapping = true;
    loader_ustaw_podpis_wymagany(true);
    for (auto rejected : {PodpisPaczkiWynik::BrakPliku, PodpisPaczkiWynik::Niezgodnosc,
                          PodpisPaczkiWynik::ZlyRozmiar, PodpisPaczkiWynik::BrakSrodowiska}) {
        signature_result = rejected;
        const auto before = signature_calls;
        CHECK(!bws_uruchom_program_z_pliku(path, 3, rights, false));
        CHECK(signature_calls == before + 1);
        clean();
    }
    signature_result = PodpisPaczkiWynik::Ok;
    check_signature_before_mapping = false;
    const auto verified_before = signature_calls;
    mapping();
    CHECK(signature_calls == verified_before + 2);
    loader_ustaw_podpis_wymagany(false);
    ++scenarios;
    void* root = UtworzPrzestrzenAdresowaProcesu(); UstawPrzestrzenAdresowa(root);
    void* physical = ZaalokujRamke(); const auto va = reinterpret_cast<void*>(RING3_DEFAULT_TEXT_VA);
    vmm_zainicjalizowany = false; CHECK(!VmmMapujNowaStroneProcesu(va, physical, false, true));
    vmm_zainicjalizowany = true;
    CHECK(!VmmMapujNowaStroneProcesu(va, nullptr, false, true));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(RING3_BASE - 4096), physical, false, true));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(RING3_BASE + 1), physical, false, true));
    CHECK(!VmmMapujNowaStroneProcesu(va, static_cast<uint8_t*>(physical) + 1, false, true));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(0xffff800000000000ULL), physical, false, true));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(0x0000800000000000ULL), physical, false, true));
    UstawPrzestrzenAdresowa(nullptr); CHECK(!VmmMapujNowaStroneProcesu(va, physical, false, true));
    UstawPrzestrzenAdresowa(root);
    CHECK(!VmmMapujNowaStroneProcesu(va, physical, true, true));
    CHECK(!VmmMapujStroneBurV2WPrzestrzeni(root, va, physical, PTE_WRITE));
    CHECK(!VmmMapujStroneBurV2WPrzestrzeni(root, va, physical, PTE_USER));
    nx_aktywny = false;
    CHECK(!VmmMapujNowaStroneProcesu(va, physical, true, false));
    CHECK(!VmmMapujStroneBurV2WPrzestrzeni(root, va, physical, PTE_NX));
    nx_aktywny = true;
    CHECK(VmmMapujNowaStroneProcesu(va, physical, false, true));
    CHECK(!VmmMapujNowaStroneProcesu(va, physical, true, true)); // preserve existing USER mapping
    CHECK(!(spaces.at(root).at(RING3_DEFAULT_TEXT_VA).flags & FLAGA_ZAPIS));
    UstawPrzestrzenAdresowa(&original_root); ZniszczPrzestrzenAdresowaProcesu(root); clean();
    // A valid larger image demonstrates removal of the obsolete 512 KiB
    // scratch buffer limit without changing the documented 128 MiB ABI.
    ++scenarios; auto large = executable; large.resize(659456, 0);
    auto large_header = header; large_header.dane_rozmiar = std::max<uint64_t>(header.dane_rozmiar, 0x90000);
    std::memcpy(large.data(), &large_header, sizeof(large_header));
    CHECK(zapisz_plik_od_nowa(bad_path, reinterpret_cast<const char*>(large.data()), large.size()));
    CHECK(bsp_wczytaj_obraz_bur(bad_path, &a, BUR_MAKS_ROZMIAR_PLIKU, bur_blad_naglowka));
    CHECK(a.rozmiar == large.size()); kfree(a.dane);
    CHECK(bws_uruchom_program_z_pliku(bad_path, 3, rights, false));
    CHECK(tablica_procesow[1].stan == PROCES_GOTOWY);
    release_process(1); clean();
    // Oversized inode: validate size before image allocation; retain prefix
    // for diagnostics. No 128 MiB host allocation is needed for this case.
    ++scenarios; auto* inode = pobierz_wezel(rozwiaz_sciezke(bad_path, nullptr, false));
    auto old_size = inode->rozmiar_w_bajtach; inode->rozmiar_w_bajtach = BUR_MAKS_ROZMIAR_PLIKU + 1;
    logs.clear(); const size_t calls = allocation_calls;
    CHECK(!bws_uruchom_program_z_pliku(bad_path, 3, rights, false));
    CHECK(allocation_calls == calls); CHECK(logs.size() == 1);
    CHECK(logs[0].find("file=134217729") != std::string::npos);
    CHECK(logs[0].find("file-size-exceeds-maximum") != std::string::npos);
    inode->rozmiar_w_bajtach = old_size; clean();
    failure(std::vector<uint8_t>(executable.begin(), executable.begin() + 59), "truncated-header");
    mutate([](auto& n) { n.magia[0] = 'X'; }, "invalid-magic");
    mutate([](auto& n) { n.tekst_przesuniecie = UINT64_MAX & ~4095ULL; }, "text-file-offset-out-of-range");
    mutate([](auto& n) { n.tekst_rozmiar = UINT64_MAX; }, "text-size-out-of-range");
    mutate([](auto& n) { n.tekst_wirtualny = UINT64_MAX & ~4095ULL; }, "text-address-overflow-or-outside-user-image");
    mutate([](auto& n) { n.dane_przesuniecie = UINT64_MAX & ~4095ULL; }, "data-file-offset-out-of-range");
    mutate([](auto& n) { n.dane_rozmiar = UINT64_MAX & ~4095ULL; }, "data-size-out-of-range");
    mutate([](auto& n) { n.dane_wirtualny = UINT64_MAX & ~4095ULL; }, "data-address-overflow-or-outside-user-image");
    mutate([](auto& n) { n.dane_wirtualny = n.tekst_wirtualny; }, "segments-overlap-in-memory");
    mutate([](auto& n) { n.dane_przesuniecie = n.tekst_przesuniecie; }, "segments-overlap-in-file");
    mutate([](auto& n) { n.punkt_wejscia = n.tekst_wirtualny + n.tekst_rozmiar; }, "entry-outside-text");
    mutate([](auto& n) { n.dane_rozmiar = BUR_MAKS_BSS_ROZMIAR + 4096; n.dane_przesuniecie = executable.size(); }, "data-size-out-of-range");
    mutate([](auto& n) { n.tekst_wirtualny = 0xffff800000000000ULL; }, "text-address-overflow-or-outside-user-image");
    mutate([](auto& n) { n.tekst_wirtualny = RING3_BASE - 4096; }, "text-address-overflow-or-outside-user-image");
    mutate([](auto& n) { n.dane_wirtualny = RING3_HEAP_BASE; }, "data-address-overflow-or-outside-user-image");
    mutate([](auto& n) { n.dane_wirtualny = RING3_STACK_TOP - 4096; }, "data-address-overflow-or-outside-user-image");
    auto truncated = executable; truncated.resize(header.tekst_przesuniecie + header.tekst_rozmiar - 1);
    failure(truncated, "text-file-end-overflow-or-truncated");
    // Allocation/mapping failure rolls back and emits one bounded record.
    ++scenarios; fail_allocation = true; logs.clear();
    CHECK(!bws_uruchom_program_z_pliku(path, 3, rights, false)); fail_allocation = false;
    CHECK(logs.size() == 1); CHECK(logs[0].find("image-allocation-failed") != std::string::npos); clean();
    ++scenarios; frame_budget = 2; logs.clear();
    CHECK(!bws_uruchom_program_z_pliku(path, 3, rights, false)); frame_budget = -1;
    unsigned failures = 0; for (const auto& line : logs) if (line.find("[BUR FAIL]") == 0) ++failures;
    CHECK(failures == 1); clean();
    // Page-table allocation failures propagate from the actual checked VMM
    // wrapper. Both the uninstalled frame and already mapped pages are freed.
    const int text_pages = header.tekst_rozmiar / 4096;
    const int image_pages = (header.tekst_rozmiar + header.dane_rozmiar) / 4096;
    for (int budget : {0, 2, text_pages, image_pages}) {
        ++scenarios; map_budget = budget; logs.clear();
        CHECK(!bws_uruchom_program_z_pliku(path, 3, rights, false)); map_budget = -1;
        unsigned reports = 0;
        for (const auto& line : logs) if (line.find("[BUR FAIL]") == 0) ++reports;
        CHECK(reports == 1);
        const char* reason = budget < text_pages ? "kod-failed" :
            budget < image_pages ? "dane-failed" : "stos_uzytkownika-failed";
        CHECK(logs.back().find(reason) != std::string::npos); clean();
    }
    // Diagnostic output remains bounded even with damaged 64-bit fields.
    ++scenarios; BurObrazPliku corrupt{}; corrupt.naglowek.tekst_rozmiar = UINT64_MAX;
    corrupt.naglowek.dane_rozmiar = UINT64_MAX; corrupt.blad = "bad-header";
    char small[32]; bur_formatuj_blad(small, sizeof(small), "x\n'y", corrupt); CHECK(small[31] == 0);
    char full[640]; bur_formatuj_blad(full, sizeof(full), "x\n'y", corrupt);
    CHECK(std::strstr(full, "mapped=overflow")); CHECK(!std::strchr(full, '\n')); clean();
    if(argc==3) {
        std::ifstream v2file(argv[2],std::ios::binary);
        std::vector<uint8_t> v2((std::istreambuf_iterator<char>(v2file)),{});
        CHECK(bur_v2_waliduj_plik(v2.data(),v2.size(),ASLR_PIE_WINDOW_BASE)==BUR_V2_OK);
        NaglowekBurV2 n{};std::memcpy(&n,v2.data(),sizeof(n));
        CHECK(zapisz_plik_od_nowa(bad_path,reinterpret_cast<const char*>(v2.data()),v2.size()));
        psf_allocations=allocations;
        for(unsigned i=0;i<4;++i) {
            CHECK(bws_uruchom_program_z_pliku(bad_path,3,rights,false));
            const auto layout=uklady_procesow[1];CHECK(layout.wersja==2);
            const auto& pages=spaces.at(tablica_procesow[1].przestrzen_adresowa);
            CHECK(!(pages.at(layout.baza_obrazu+n.tekst_rva).flags & (PTE_WRITE|PTE_NX)));
            CHECK((pages.at(layout.canary).flags & (PTE_WRITE|PTE_NX))==PTE_NX);
            CHECK((pages.at(layout.baza_obrazu+n.dane_rva).flags & (PTE_WRITE|PTE_NX))==(PTE_WRITE|PTE_NX));
            CHECK(!pages.count(layout.stos_dol-4096));
            release_process(1);clean();
        }
        secure_entropy=false;CHECK(!bws_uruchom_program_z_pliku(bad_path,3,rights,false));secure_entropy=true;clean();
        nx_aktywny=false;CHECK(!bws_uruchom_program_z_pliku(bad_path,3,rights,false));nx_aktywny=true;clean();
        const unsigned count=(n.tekst_rozmiar+n.rodata_rozmiar+n.dane_rozmiar_pamieci)/4096+1+n.domyślne_strony_stosu;
        for(unsigned budget=0;budget<count;++budget) {
            frame_budget=budget;CHECK(!bws_uruchom_program_z_pliku(bad_path,3,rights,false));frame_budget=-1;clean();
            map_budget=budget;CHECK(!bws_uruchom_program_z_pliku(bad_path,3,rights,false));map_budget=-1;clean();
        }
        std::printf("BUR v2 production loader rollback: %u frame and mapping failure stages PASS; NX/entropy denial PASS\n",count);
    }
    check_sentinels();   // loader scenarios (bad_path rewrites, mutations) never touched unrelated files
    std::printf("Test dzwieku loader/package: %u scenarios, %u checks PASS; ASan+UBSan\n", scenarios, checks);
}
