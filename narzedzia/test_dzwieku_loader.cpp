// Execute production PSF snapshot, boot package installer and complete loader.
// Only privileged CPU operations, PMM/VMM and kernel stacks are host substitutes.
#include "../psf.cpp"
#include "../psf_bootstrap.h"
#include "../scheduler.h"
#include "../stos_jadra.h"
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
struct Page { uint8_t* bytes; uint32_t flags; };
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
struct BlokadaVMM { ~BlokadaVMM() {} };
static uint64_t* pml4_z_cr3() { return static_cast<uint64_t*>(current_root); }
struct WynikWalkera { bool obecna, user; };
static WynikWalkera sprawdz_mapowanie(uint64_t* root, uint64_t address) {
    const auto& pages = spaces.at(root);
    const auto it = pages.find(address);
    if (it != pages.end()) return {true, bool(it->second.flags & FLAGA_USER)};
    // The real base CR3 has supervisor identity mappings below 4 GiB.
    return {address < (1ULL << 32), false};
}
static bool mapuj_strone_wewnetrznie(uint64_t* root, uint64_t address, uint64_t pa, uint32_t flags) {
    CHECK(current_root != &original_root);
    CHECK(root == current_root); CHECK(!(address & 4095)); CHECK(frames.count(reinterpret_cast<void*>(pa)));
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

#include "../.hda-test/repair/package_actual.inc"
static constexpr const char* path = "/programy/test_dzwieku.cebula/test_dzwieku.bur";
static constexpr const char* bad_path = "/programy/test_dzwieku.cebula/invalid.bur";
static constexpr uint64_t rights = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ;
static std::vector<uint8_t> executable;
static NaglowekBur header;

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
    CHECK(frame->stary_rsp == RING3_STACK_TOP + 4 * 4096 - 8);
    CHECK((frame->rflags & 0x202) == 0x202);
    auto& pages = spaces.at(reinterpret_cast<void*>(p.cr3));
    CHECK(pages.size() == (header.tekst_rozmiar + header.dane_rozmiar) / 4096 + 4);
    auto verify = [&](uint64_t va, uint64_t memory, uint64_t off, uint64_t stored, bool writable) {
        for (uint64_t i = 0; i < memory; i += 4096) {
            const auto& page = pages.at(va + i);
            CHECK((page.flags & (FLAGA_USER | FLAGA_OBECNA)) == (FLAGA_USER | FLAGA_OBECNA));
            CHECK(bool(page.flags & FLAGA_ZAPIS) == writable);
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
    CHECK(argc == 2); std::ifstream file(argv[1], std::ios::binary);
    executable.assign(std::istreambuf_iterator<char>(file), {}); CHECK(executable.size() >= sizeof(header));
    std::memcpy(&header, executable.data(), sizeof(header));
    CHECK(executable.size() < 512 * 1024); CHECK(!bur_blad_naglowka(header, executable.size()));
    std::vector<uint8_t> ram(16 * 1024 * 1024); inicjalizuj_psf(ram.data(), ram.size());
    CHECK(psf_czy_gotowy()); for (const char* dir : KATALOGI_SYSTEMOWE) CHECK(zapewnij_katalog_startowy(dir));
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
    ++scenarios;
    BurObrazPliku a{}, b{};
    CHECK(bsp_wczytaj_obraz_bur(path, &a, BUR_MAKS_ROZMIAR_PLIKU, bur_blad_naglowka));
    CHECK(bsp_wczytaj_obraz_bur(path, &b, BUR_MAKS_ROZMIAR_PLIKU, bur_blad_naglowka));
    CHECK(a.dane != b.dane); CHECK(a.rozmiar == executable.size());
    CHECK(!std::memcmp(a.dane, executable.data(), executable.size())); kfree(a.dane); kfree(b.dane);
    mapping();
    ++scenarios;
    void* root = UtworzPrzestrzenAdresowaProcesu(); UstawPrzestrzenAdresowa(root);
    void* physical = ZaalokujRamke(); const auto va = reinterpret_cast<void*>(RING3_DEFAULT_TEXT_VA);
    vmm_zainicjalizowany = false; CHECK(!VmmMapujNowaStroneProcesu(va, physical, false));
    vmm_zainicjalizowany = true;
    CHECK(!VmmMapujNowaStroneProcesu(va, nullptr, false));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(RING3_BASE - 4096), physical, false));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(RING3_BASE + 1), physical, false));
    CHECK(!VmmMapujNowaStroneProcesu(va, static_cast<uint8_t*>(physical) + 1, false));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(0xffff800000000000ULL), physical, false));
    CHECK(!VmmMapujNowaStroneProcesu(reinterpret_cast<void*>(0x0000800000000000ULL), physical, false));
    UstawPrzestrzenAdresowa(nullptr); CHECK(!VmmMapujNowaStroneProcesu(va, physical, false));
    UstawPrzestrzenAdresowa(root);
    CHECK(VmmMapujNowaStroneProcesu(va, physical, false));
    CHECK(!VmmMapujNowaStroneProcesu(va, physical, true)); // preserve existing USER mapping
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
    std::printf("Test dzwieku loader/package: %u scenarios, %u checks PASS; ASan+UBSan\n", scenarios, checks);
}
