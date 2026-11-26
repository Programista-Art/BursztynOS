// BWS 111 (informacja o systemie plikow): psf_statystyki() na produkcyjnym psf.cpp (RAM i trwaly wolumin),
// wypelnij_informacje_magazynu() i polskie opisy MagazynTekst (Eksplorator, Grajek).
#include "storage_format.h"
#include "psf.h"
#include "jadro/fs/magazyn_info.h"
#include "jadro/bws/bws_magazyn_tekst.h"
#include "sterowniki/czas/zegar-rtc.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
using namespace Storage;
void* kmalloc(size_t n) { return std::malloc(n); }
void kfree(void* p) { std::free(p); }
void wypisz_log(const char*) {}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
static bool g_auth = true;
bool blokowy_zapis_instalatora() { return g_auth; }
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
struct Fake {
    std::map<uint64_t, std::array<uint8_t, 512>> d; Device dev;
    explicit Fake(uint64_t n) : dev{this, n, 512, true, rd, wr, fl} {}
    static bool rd(void* c, uint64_t s, uint32_t n, void* b) { auto& f = *(Fake*)c; for (unsigned i = 0; i < n; ++i) { auto it = f.d.find(s + i); if (it == f.d.end()) std::memset((uint8_t*)b + 512 * i, 0, 512); else std::memcpy((uint8_t*)b + 512 * i, it->second.data(), 512); } return true; }
    static bool wr(void* c, uint64_t s, uint32_t n, const void* b) { auto& f = *(Fake*)c; for (unsigned i = 0; i < n; ++i) std::memcpy(f.d[s + i].data(), (const uint8_t*)b + 512 * i, 512); return true; }
    static bool fl(void*) { return true; }
};
int main() {
    std::vector<uint8_t> ram(16u << 20);
    psf_ustaw_partycje(nullptr);
    inicjalizuj_psf(ram.data(), uint32_t(ram.size()));
    PsfStatystyki s{};
    CHECK(psf_statystyki(&s) && s.gotowy && !s.trwaly);                  // Live / RAM
    CHECK(s.wezly == 1024 && s.wolne_wezly == 1023 && s.wolne_bajty == psf_wolne_bajty() && s.bloki_danych > 0);
    BwsMagazynV1 m{};
    wypelnij_informacje_magazynu(m, s, false);
    CHECK(m.wersja == 1 && m.rozmiar == 64 && (m.flagi & BWS_MAG_GOTOWY) && !(m.flagi & BWS_MAG_TRWALY));
    CHECK(m.maks_nazwa_bajty == 55 && m.rozmiar_bloku == 4096 && m.maks_rozmiar_pliku == PSF_MAKS_ROZMIAR_PLIKU);
    char t[96];
    MagazynTekst::opis_wolnego(t, sizeof(t), m);
    CHECK(std::strstr(t, "Wolne: ") && std::strstr(t, "RAM: pliki znikną po restarcie") && !std::strstr(t, "trwały"));
    CHECK(utworz_katalog("/dane") && utworz_plik("/dane/a"));
    std::vector<char> blok(4096 * 3, 'x');
    CHECK(zapisz_do_pliku("/dane/a", blok.data(), uint32_t(blok.size())));
    PsfStatystyki s2{};
    CHECK(psf_statystyki(&s2) && s2.wolne_wezly == s.wolne_wezly - 2 && s2.wolne_bajty < s.wolne_bajty);
    // Trwaly wolumin instalacji (uklad instalatora)
    Fake disk(2u * 1024 * 1024 * 2);   // 2 GiB
    Layout l{}; const uint8_t uuid[16] = {1, 2, 3};
    CHECK(plan(&disk.dev, &l) && gpt_write(&disk.dev, l, uuid) && psf_formatuj_instalacje(l.bsp) && psf_znacznik_instalacji(uuid, true));
    psf_ustaw_partycje(&l.bsp);
    inicjalizuj_psf(ram.data(), uint32_t(ram.size()));
    PsfStatystyki s3{};
    CHECK(psf_statystyki(&s3) && s3.gotowy && s3.trwaly && s3.bloki_danych * 4096 > (1ull << 30));
    wypelnij_informacje_magazynu(m, s3, true);
    CHECK((m.flagi & BWS_MAG_TRWALY) && (m.flagi & BWS_MAG_ZAINSTALOWANY));
    MagazynTekst::opis_wolnego(t, sizeof(t), m);
    CHECK(std::strstr(t, "(dysk trwały)") && std::strstr(t, " GB z "));
    // formatowanie rozmiarow
    char r[32]; r[0] = 0;
    MagazynTekst::dopisz_rozmiar(r, sizeof(r), 0, 1536); CHECK(std::strcmp(r, "1,5 KB") == 0);
    MagazynTekst::dopisz_rozmiar(r, sizeof(r), 0, 512); CHECK(std::strcmp(r, "512 B") == 0);
    MagazynTekst::dopisz_rozmiar(r, sizeof(r), 0, 3ull << 30); CHECK(std::strcmp(r, "3,0 GB") == 0);
    char maly[8]; MagazynTekst::opis_wolnego(maly, sizeof(maly), m); CHECK(std::strlen(maly) < sizeof(maly));
    BwsMagazynV1 pusty{}; MagazynTekst::opis_wolnego(t, sizeof(t), pusty); CHECK(std::strstr(t, "niedostępny"));
    CHECK(!psf_statystyki(nullptr));
    std::printf("BWS 111 magazyn: %u sprawdzen OK (RAM i trwaly BSP2, opisy PL)\n", checks);
}
