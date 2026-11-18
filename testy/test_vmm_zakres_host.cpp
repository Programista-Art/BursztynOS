// Host (ASan/UBSan): reguly izolacji Ring 3 <-> jadro: zakres_wolny nad PRAWDZIWYM drzewem tablic stron (4 poziomy,
// strony 4K / 2M / 1G), predykat mapowania strony uzytkownika i zgodnosc z lista zarezerwowanych okien VA.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <cstdint>
#include "../jadro/mm/vmm_zakres.h"
#include "../jadro/mm/zarezerwowane_va.h"
using namespace VmmZakres;
static int bledy = 0, proby = 0;
#define CHECK(x) do { ++proby; if (!(x)) { ++bledy; printf("BLAD %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

// Drzewo tablic stron: "fizyczny" adres tablicy = indeks w wektorze (<< 12), wpis niesie ten adres.
struct Drzewo {
    std::vector<uint64_t*> tab;
    Drzewo() { nowa(); }
    ~Drzewo() { for (auto* t : tab) free(t); }
    size_t nowa() { auto* t = static_cast<uint64_t*>(calloc(512, 8)); tab.push_back(t); return tab.size() - 1; }
    uint64_t* pml4() { return tab[0]; }
    const uint64_t* dziecko(uint64_t wpis) const {
        const uint64_t idx = (wpis & 0x000FFFFFFFFFF000ULL) >> 12;
        return idx < tab.size() ? tab[idx] : nullptr;
    }
    static unsigned ix(uint64_t va, int poziom) { return (va >> (12 + 9 * (poziom - 1))) & 511; }
    // mapuje lisc na danym poziomie (1 = 4K, 2 = 2M, 3 = 1G) w wierszu tablic tworzac poddrzewa
    void mapuj(uint64_t va, int poziom_liscia) {
        uint64_t* t = tab[0];
        for (int p = 4; p > poziom_liscia; --p) {
            uint64_t& w = t[ix(va, p)];
            if (!(w & PTE_OBECNY)) { size_t n = nowa(); w = (uint64_t(n) << 12) | PTE_OBECNY | 2; }
            t = tab[(w & 0x000FFFFFFFFFF000ULL) >> 12];
        }
        t[ix(va, poziom_liscia)] = 0x1000ULL | PTE_OBECNY | 2 | (poziom_liscia > 1 ? PTE_DUZY : 0);
    }
    bool wolny(uint64_t s, uint64_t k) { return zakres_wolny(pml4(), s, k, [this](uint64_t w) { return dziecko(w); }); }
};

int main() {
    // ---- predykat mapowania strony uzytkownika -------------------------------------------------------------------
    CHECK(wolno_mapowac_strone_uzytkownika(false, false, 0x1201F0000ULL));   // puste: wolno
    CHECK(wolno_mapowac_strone_uzytkownika(false, true, 0x1201F0000ULL));
    CHECK(!wolno_mapowac_strone_uzytkownika(true, true, 0x1201F0000ULL));    // obecna strona uzytkownika: nigdy
    CHECK(!wolno_mapowac_strone_uzytkownika(true, true, 0x1000000ULL));
    CHECK(!wolno_mapowac_strone_uzytkownika(true, false, 0x1201F0000ULL));   // strona jadra >= 4 GiB: nigdy (przypadek z awarii)
    CHECK(!wolno_mapowac_strone_uzytkownika(true, false, 0x2000000000ULL));
    CHECK(!wolno_mapowac_strone_uzytkownika(true, false, LIMIT_IDENTITY_MAPY));
    CHECK(wolno_mapowac_strone_uzytkownika(true, false, LIMIT_IDENTITY_MAPY - 4096)); // identity-mapa < 4 GiB: wspolna, rozbijana
    CHECK(wolno_mapowac_strone_uzytkownika(true, false, 0x1000000ULL));      // RING3_BASE (v1)

    // ---- zakres_wolny: puste drzewo ------------------------------------------------------------------------------
    { Drzewo d;
      CHECK(d.wolny(0x100000000ULL, 0x200000000ULL));
      CHECK(!d.wolny(5, 5)); CHECK(!d.wolny(10, 5));                        // pusty/odwrocony: fail-closed
      CHECK(!d.wolny(0x7FFFFFFFF000ULL, 0x800000000001ULL));                // poza dolna polowa
      CHECK(!zakres_wolny(nullptr, 0, 4096, [](uint64_t) { return (const uint64_t*)nullptr; })); }

    // ---- lisc 4K scratch Tapet (0x120000000..+32 MiB) i dokladne granice -----------------------------------------
    { Drzewo d;
      for (uint64_t o = 0; o < ZarezerwowaneVA::SUROWY_BMP_ROZMIAR; o += 4096) d.mapuj(ZarezerwowaneVA::SUROWY_BMP_START + o, 1);
      const uint64_t a = ZarezerwowaneVA::SUROWY_BMP_START, b = a + ZarezerwowaneVA::SUROWY_BMP_ROZMIAR;
      CHECK(!d.wolny(0x1201F0000ULL, 0x1201F0000ULL + 0x77000));            // dokladnie baza z awarii
      CHECK(!d.wolny(a, a + 4096));  CHECK(!d.wolny(b - 4096, b));
      CHECK(!d.wolny(a - 4096, a + 1));                                     // nachodzi o 1 bajt
      CHECK(d.wolny(a - 4096, a));   CHECK(d.wolny(b, b + 4096));           // styk bez nakladania: wolne
      CHECK(!d.wolny(0x100000000ULL, 0x200000000ULL));                      // okno PIE jako calosc
      CHECK(d.wolny(0x800000000ULL, 0x900000000ULL)); }                     // sterta ASLR wolna
    // ---- duze strony 2M i 1G -------------------------------------------------------------------------------------
    { Drzewo d; d.mapuj(0x140000000ULL, 2);                                  // 2 MiB
      CHECK(!d.wolny(0x140000000ULL + 0x1FF000, 0x140000000ULL + 0x200000)); CHECK(d.wolny(0x140200000ULL, 0x140400000ULL));
      CHECK(d.wolny(0x13FE00000ULL, 0x140000000ULL)); }
    { Drzewo d; d.mapuj(0x200000000ULL, 3);                                  // 1 GiB (dolna polowa PML4[0])
      CHECK(!d.wolny(0x200000000ULL + 0x3FFFF000, 0x200000000ULL + 0x40000000)); CHECK(d.wolny(0x240000000ULL, 0x241000000ULL)); }
    // ---- okno PIE przecina mapowania dynamiczne spoza listy (BAR VA==PA w oknie sterty i SHM) --------------------
    { Drzewo d; d.mapuj(0x800123000ULL, 1); d.mapuj(0x1000456000ULL, 1);
      CHECK(!d.wolny(0x800000000ULL, 0x810000000ULL)); CHECK(!d.wolny(0x1000000000ULL, 0x1010000000ULL));
      CHECK(d.wolny(0x810000000ULL, 0x900000000ULL)); }
    // ---- nieczytelna tablica potomna = fail-closed ----------------------------------------------------------------
    { Drzewo d; uint64_t* p = d.pml4(); p[0] = (uint64_t(9999) << 12) | PTE_OBECNY | 2;
      CHECK(!d.wolny(0x1000, 0x2000)); }
    // ---- polowa jadra (PML4[256..]) nie jest zakresem uzytkownika: poza dolna polowa => false -----------------------
    { Drzewo d; CHECK(!d.wolny(0xFFFF900000000000ULL, 0xFFFF900000001000ULL)); }

    // ---- zgodnosc listy statycznej z wlasna logika ----------------------------------------------------------------
    for (const auto& z : ZarezerwowaneVA::ZAKRESY) {
        CHECK(z.rozmiar > 0 && (z.start & 4095) == 0 && (z.rozmiar & 4095) == 0);
        CHECK(ZarezerwowaneVA::koliduje_z_jadrem(z.start, z.start + 4096));
        CHECK(ZarezerwowaneVA::koliduje_z_jadrem(z.start + z.rozmiar - 4096, z.start + z.rozmiar));
        CHECK(ZarezerwowaneVA::koliduje_z_jadrem(z.start - 4096, z.start + 1));            // nachodzenie o 1 bajt wykryte
        CHECK(z.start >= LIMIT_IDENTITY_MAPY);                                // wszystkie okna powyzej identity-mapy
    }
    // zadne dwa okna statyczne sie nie nakladaja
    for (size_t i = 0; i < sizeof(ZarezerwowaneVA::ZAKRESY) / sizeof(ZarezerwowaneVA::ZAKRESY[0]); ++i)
        for (size_t j = i + 1; j < sizeof(ZarezerwowaneVA::ZAKRESY) / sizeof(ZarezerwowaneVA::ZAKRESY[0]); ++j)
            CHECK(ZarezerwowaneVA::ZAKRESY[i].start + ZarezerwowaneVA::ZAKRESY[i].rozmiar <= ZarezerwowaneVA::ZAKRESY[j].start ||
                  ZarezerwowaneVA::ZAKRESY[j].start + ZarezerwowaneVA::ZAKRESY[j].rozmiar <= ZarezerwowaneVA::ZAKRESY[i].start);
    printf("%s: %d sprawdzen, %d bledow\n", bledy ? "FAIL" : "PASS", proby, bledy);
    return bledy ? 1 : 0;
}
