#pragma once
#include <stdint.h>
#include <stddef.h>
#include "jadro/bws/pzb.h"

/*
 * Bursztyn OS - mapowanie anonimowe dla ABI SysV (mmap/munmap/mprotect).
 *
 * Zakres: `RING3_MMAP_BASE .. RING3_MMAP_LIMIT` - nowe okno w przestrzeni
 * procesu, pomiedzy SHM (36 GiB) a stosem. Nie koliduje z:
 *
 *   .bur obraz  16 MiB ..            (RING3_BASE)
 *   sterta      32 GiB .. 32 GiB+256M (RING3_HEAP_BASE)
 *   SHM         36 GiB .. 36 GiB+8M   (RING3_SHM_BASE)
 *   mmap        40 GiB .. 40 GiB+256M (ten modul)
 *   stos        ~512 GiB              (RING3_STACK_TOP)
 *
 * Model jest prosty i celowo zachowawczy: mapowanie jest EAGER (ramka
 * alokowana od razu), bez COW i bez demand pagingu. To wystarcza kazdej
 * libc (malloc na mmap, TLS, bufory) i nie zmienia modelu VMM w sposob,
 * ktory wymagalby przebudowy handlera #PF.
 *
 * Kontrakt bledow jest zgodny z ABI SysV: sukces to adres (>0) albo 0,
 * porazka to -errno.
 */

/* Okno adresowe mmap. Wartosci sa swiadomie poza obecnymi zakresami. */
inline constexpr uint64_t RING3_MMAP_BASE = 0x0000000A00000000ULL;  /* 40 GiB */
inline constexpr uint64_t RING3_MMAP_ROZMIAR = 256ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t RING3_MMAP_LIMIT = RING3_MMAP_BASE + RING3_MMAP_ROZMIAR;

static_assert(RING3_MMAP_BASE > 0x0000000900000000ULL,
              "mmap musi lezec powyzej SHM (36 GiB)");
static_assert(RING3_MMAP_LIMIT < 0x00007FFFF0000000ULL,
              "mmap musi lezec ponizej stosu Ring 3");

/* =========================================================================
 * FLAGI
 * ========================================================================= */

/* Zgodne z Linux x86-64 (wartosci sa czescia ABI, nie naszym wyborem). */
enum PosixMmapProt : uint64_t {
    POSIX_PROT_NONE  = 0,
    POSIX_PROT_READ  = 1,
    POSIX_PROT_WRITE = 2,
    POSIX_PROT_EXEC  = 4
};

enum PosixMmapFlagi : uint64_t {
    POSIX_MAP_SHARED     = 0x01,
    POSIX_MAP_PRIVATE    = 0x02,
    POSIX_MAP_FIXED      = 0x10,
    POSIX_MAP_ANONYMOUS  = 0x20,
    POSIX_MAP_NORESERVE  = 0x4000
};

constexpr uint64_t POSIX_MAP_TYP_MASKA = 0x0F;  /* SHARED | PRIVATE */

/* =========================================================================
 * OPIS REGIONU (czysta logika, testowalna na hoscie)
 * ========================================================================= */

struct MmapRegion {
    uint64_t adres;       /* poczatek, wyrownany do strony */
    uint64_t dlugosc;     /* rozmiar w bajtach, wielokrotnosc strony */
    uint32_t ochrona;     /* PROT_* */
    bool     uzywany;
};

/* Maksymalna liczba jednoczesnych mapowan na proces. */
inline constexpr uint32_t MMAP_MAKS_REGIONOW = 16;
/* Minimalna i maksymalna dlugosc jednego mapowania. */
inline constexpr uint64_t MMAP_MIN_DLUGOSC = 4096ULL;
inline constexpr uint64_t MMAP_MAKS_DLUGOSC = 64ULL * 1024ULL * 1024ULL;

/*
 * Znajduje wolne miejsce na mapowanie `dlugosc` bajtow.
 *
 * `regiony` to tablica MMAP_MAKS_REGIONOW wpisow; funkcja nie modyfikuje
 * danych, tylko wybiera adres. Strategia: first-fit od dolu okna, z
 * zachowaniem strony przerwy (aby przechwycic off-by-one w kodzie
 * uzytkownika jako #PF, a nie ciche nadpisanie sasiedniego mapowania).
 *
 * Zwraca 0, gdy brak miejsca.
 */
uint64_t mmap_znajdz_miejsce(const MmapRegion* regiony, uint32_t liczba,
                             uint64_t dlugosc);

/* Sprawdza, czy adres jest poprawny dla mmap (wyrownanie, okno, zakres). */
bool mmap_adres_poprawny(uint64_t adres, uint64_t dlugosc);

/* Sprawdza zestaw flag. Zwraca -errno albo 0. */
int64_t mmap_waliduj_flagi(uint64_t flagi, uint64_t ochrona);

/* Znajduje region zawierajacy dany zakres. Zwraca indeks albo -1. */
int32_t mmap_znajdz_region(const MmapRegion* regiony, uint32_t liczba,
                           uint64_t adres, uint64_t dlugosc);
