/*
 * Host gate okna adresowego mmap (punkt 2.18, czesc niezalezna od VMM).
 *
 * Sprawdza dokladnie te reguly, ktore decyduja o bezpieczenstwie mapowania:
 *
 *   1. okno mmap nie koliduje z obrazem .bur, sterta, SHM ani stosem,
 *   2. first-fit wybiera najnizszy wolny adres i nie wchodzi na sasiednie
 *      mapowania,
 *   3. adresy i dlugosci musza byc wyrownane do strony,
 *   4. flagi musza zawierac dokladnie jedno z SHARED/PRIVATE i zadnych
 *      nieznanych bitow,
 *   5. W^X obowiazuje takze w mmap: PROT_WRITE|PROT_EXEC jest odrzucane,
 *   6. zapytanie o region trafia tylko wtedy, gdy CALY zakres jest pokryty
 *      (czesciowe pokrycie to blad, nie sukces).
 */
#include "jadro/mm/mmap.h"
#include "jadro/mm/pamiec.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool warunek, const char* opis) {
    ++g_checks;
    if (!warunek) {
        std::printf("FAIL: %s\n", opis);
        ++g_failures;
    }
}

MmapRegion pusty() {
    MmapRegion r{};
    return r;
}

MmapRegion region(uint64_t adres, uint64_t dlugosc) {
    MmapRegion r{};
    r.adres = adres;
    r.dlugosc = dlugosc;
    r.ochrona = POSIX_PROT_READ | POSIX_PROT_WRITE;
    r.uzywany = true;
    return r;
}

/* --- 1. Okno nie koliduje z innymi zakresami -------------------------- */

void test_okno() {
    check(RING3_MMAP_BASE > RING3_HEAP_BASE, "mmap powyzej sterty");
    check(RING3_MMAP_BASE > RING3_SHM_BASE, "mmap powyzej SHM");
    check(RING3_MMAP_LIMIT < RING3_STACK_TOP, "mmap ponizej stosu");
    check(RING3_MMAP_BASE > RING3_BASE, "mmap powyzej obrazu .bur");
    check(RING3_MMAP_ROZMIAR >= 4ULL * 1024ULL * 1024ULL * 1024ULL,
          "okno mmap ma co najmniej 4 GiB");
    check(RING3_MMAP_LIMIT == RING3_MMAP_BASE + RING3_MMAP_ROZMIAR,
          "granica okna spojna");
}

/* --- 2. Walidacja adresu i dlugosci ---------------------------------- */

void test_adres() {
    check(mmap_adres_poprawny(RING3_MMAP_BASE, 4096), "poczatek okna poprawny");
    check(mmap_adres_poprawny(RING3_MMAP_LIMIT - 4096, 4096), "koniec okna poprawny");
    check(!mmap_adres_poprawny(RING3_MMAP_BASE - 4096, 4096), "przed oknem odrzucony");
    check(!mmap_adres_poprawny(RING3_MMAP_LIMIT, 4096), "za oknem odrzucony");
    check(!mmap_adres_poprawny(RING3_MMAP_LIMIT - 4096, 8192), "przekroczenie okna");
    check(!mmap_adres_poprawny(RING3_MMAP_BASE + 1, 4096), "brak wyrownania adresu");
    check(!mmap_adres_poprawny(RING3_MMAP_BASE, 4097), "brak wyrownania dlugosci");
    check(!mmap_adres_poprawny(RING3_MMAP_BASE, 0), "zerowa dlugosc odrzucona");
    check(!mmap_adres_poprawny(0, 4096), "adres 0 odrzucony");
    check(!mmap_adres_poprawny(RING3_HEAP_BASE, 4096), "sterta odrzucona");
    check(!mmap_adres_poprawny(0x00007FFFF0000000ULL, 4096), "stos odrzucony");
}

/* --- 3. Flag i ochrona ----------------------------------------------- */

void test_flagi() {
    const uint64_t prot_rw = POSIX_PROT_READ | POSIX_PROT_WRITE;

    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE | POSIX_MAP_ANONYMOUS, prot_rw) == 0,
          "PRIVATE|ANONYMOUS + RW poprawny");
    check(mmap_waliduj_flagi(POSIX_MAP_SHARED | POSIX_MAP_ANONYMOUS, prot_rw) == 0,
          "SHARED|ANONYMOUS + RW poprawny");
    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE, prot_rw) == 0, "PRIVATE poprawny");
    check(mmap_waliduj_flagi(POSIX_MAP_SHARED | POSIX_MAP_NORESERVE, prot_rw) == 0,
          "NORESERVE poprawny");

    /* Dokladnie jedno z SHARED/PRIVATE. */
    check(mmap_waliduj_flagi(POSIX_MAP_ANONYMOUS, prot_rw) == -22,
          "brak typu mapowania -> EINVAL");
    check(mmap_waliduj_flagi(POSIX_MAP_SHARED | POSIX_MAP_PRIVATE, prot_rw) == -22,
          "oba typy naraz -> EINVAL");
    check(mmap_waliduj_flagi(0, prot_rw) == -22, "zero flag -> EINVAL");

    /* Nieznane bity. */
    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE | (1ULL << 40), prot_rw) == -22,
          "nieznana flaga -> EINVAL");
    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE, 1ULL << 20) == -22,
          "nieznana ochrona -> EINVAL");

    /* W^X. */
    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE | POSIX_MAP_ANONYMOUS,
                             POSIX_PROT_WRITE | POSIX_PROT_EXEC) == -13,
          "W|X odrzucone (EACCES)");
    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE | POSIX_MAP_ANONYMOUS,
                             POSIX_PROT_READ | POSIX_PROT_EXEC) == 0,
          "R|X dozwolone");
    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE | POSIX_MAP_ANONYMOUS,
                             POSIX_PROT_NONE) == 0,
          "PROT_NONE dozwolone");
    check(mmap_waliduj_flagi(POSIX_MAP_PRIVATE | POSIX_MAP_ANONYMOUS,
                             POSIX_PROT_WRITE) == 0,
          "samo W dozwolone");
}

void test_zakres_pliku() {
    check(mmap_zakres_pliku_poprawny(8192, 0, 4096),
          "file-backed: pierwsza strona miesci sie w pliku");
    check(mmap_zakres_pliku_poprawny(8192, 4096, 4096),
          "file-backed: zakres do konca pliku");
    check(!mmap_zakres_pliku_poprawny(8192, 4096, 4097),
          "file-backed: przekroczenie konca pliku");
    check(!mmap_zakres_pliku_poprawny(8192, UINT64_MAX, 1),
          "file-backed: offset bez zawijania arytmetyki");
    check(!mmap_zakres_pliku_poprawny(0, 0, 4096),
          "file-backed: pusty plik odrzucony");
}

/* --- 4. First-fit ---------------------------------------------------- */

void test_first_fit() {
    MmapRegion r[MMAP_MAKS_REGIONOW];
    for (auto& e : r) e = pusty();

    /* Puste okno: pierwsze mapowanie na poczatku. */
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096) == RING3_MMAP_BASE,
          "puste okno -> poczatek");

    /* Jedno mapowanie na poczatku: nastepne tuz za nim. */
    r[0] = region(RING3_MMAP_BASE, 4096);
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096) == RING3_MMAP_BASE + 4096,
          "drugie mapowanie tuz za pierwszym");

    /* Dziura pomiedzy dwoma: wybierana jest dziura, nie koniec. */
    for (auto& e : r) e = pusty();
    r[0] = region(RING3_MMAP_BASE, 4096);
    r[1] = region(RING3_MMAP_BASE + 4096ULL * 4, 4096);
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096) == RING3_MMAP_BASE + 4096,
          "first-fit wybiera dziure");
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096ULL * 3) == RING3_MMAP_BASE + 4096,
          "first-fit miesci 3 strony w dziurze");
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096ULL * 4) ==
              RING3_MMAP_BASE + 4096ULL * 5,
          "za duzo na dziure -> za ostatnim");

    /* Regiony w kolejnosci odwrotnej: wynik nadal najnizszy wolny. */
    for (auto& e : r) e = pusty();
    r[0] = region(RING3_MMAP_BASE + 4096ULL * 2, 4096);
    r[1] = region(RING3_MMAP_BASE + 4096ULL * 6, 4096);
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096) == RING3_MMAP_BASE,
          "nieposortowane regiony: najnizszy wolny");

    /* Pelne okno: brak miejsca. */
    for (auto& e : r) e = pusty();
    r[0] = region(RING3_MMAP_BASE, RING3_MMAP_ROZMIAR);
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096) == 0,
          "pelne okno -> brak miejsca");

    /* Zla dlugosc. */
    for (auto& e : r) e = pusty();
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 0) == 0, "dlugosc 0 -> 0");
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4097) == 0,
          "dlugosc bez wyrownania -> 0");
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, RING3_MMAP_ROZMIAR + 4096) == 0,
          "dlugosc wieksza od okna -> 0");
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW,
                               512ULL * 1024ULL * 1024ULL) ==
              RING3_MMAP_BASE,
          "mapowanie 512 MiB miesci sie w oknie");

    /* Wyczerpanie 256 regionow nie musi blokowac, jesli jest miejsce. */
    for (auto& e : r) e = pusty();
    for (uint32_t i = 0; i < MMAP_MAKS_REGIONOW; ++i)
        r[i] = region(RING3_MMAP_BASE + 4096ULL * i, 4096);
    check(mmap_znajdz_miejsce(r, MMAP_MAKS_REGIONOW, 4096) ==
              RING3_MMAP_BASE + 4096ULL * MMAP_MAKS_REGIONOW,
          "256 regionow: nastepne miejsce za nimi");
}

/* --- 5. Wyszukiwanie regionu ---------------------------------------- */

void test_region() {
    MmapRegion r[MMAP_MAKS_REGIONOW];
    for (auto& e : r) e = pusty();
    r[0] = region(RING3_MMAP_BASE, 4096ULL * 4);

    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE, 4096) == 0,
          "poczatek regionu znaleziony");
    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE + 4096, 4096) == 0,
          "srodek regionu znaleziony");
    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE, 4096ULL * 4) == 0,
          "caly region znaleziony");
    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE, 4096ULL * 5) == -1,
          "zakres wiekszy niz region -> brak");
    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE + 4096ULL * 4, 4096) == -1,
          "tuz za regionem -> brak");
    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE - 4096, 4096) == -1,
          "przed regionem -> brak");
    check(mmap_znajdz_region(nullptr, 0, RING3_MMAP_BASE, 4096) == -1,
          "nullptr -> brak");

    /* Czesciowe pokrycie dwoch regionow to blad, nie sukces. */
    r[1] = region(RING3_MMAP_BASE + 4096ULL * 4, 4096);
    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE + 4096ULL * 3,
                             4096ULL * 3) == -1,
          "zakres przez dwa regiony -> brak");

    /* Region nieuzywany jest ignorowany. */
    r[0].uzywany = false;
    check(mmap_znajdz_region(r, MMAP_MAKS_REGIONOW, RING3_MMAP_BASE, 4096) == -1,
          "nieuzywany region ignorowany");
}

}  // namespace

int main() {
    test_okno();
    test_adres();
    test_flagi();
    test_zakres_pliku();
    test_first_fit();
    test_region();

    if (g_failures != 0) {
        std::printf("[MMAP-HOST] FAIL %d/%d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("[MMAP-HOST] PASS %d asercji\n", g_checks);
    return 0;
}
