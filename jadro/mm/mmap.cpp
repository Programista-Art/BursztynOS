#include "mmap.h"

/*
 * Bursztyn OS - alokacja okna adresowego mmap (czysta logika).
 *
 * Ten plik nie mapuje stron: wybiera i waliduje adresy. Mapowanie i zwalnianie
 * ramek robi jadro (jadro/bws/... przez VMM/PMM), bo tylko ono ma CR3 i PMM.
 * Rozdzial pozwala przetestowac cala arytmetyke zakresow bez QEMU.
 */

namespace {

constexpr uint64_t ROZMIAR_STRONY = 4096ULL;
constexpr uint64_t MASKA_STRONY = ROZMIAR_STRONY - 1ULL;

inline uint64_t wyrownaj_w_gore(uint64_t v) {
    if (v > UINT64_MAX - MASKA_STRONY) return 0;
    return (v + MASKA_STRONY) & ~MASKA_STRONY;
}

}  // namespace

bool mmap_adres_poprawny(uint64_t adres, uint64_t dlugosc) {
    if (dlugosc == 0) return false;
    if ((dlugosc & MASKA_STRONY) != 0) return false;
    if ((adres & MASKA_STRONY) != 0) return false;
    if (adres < RING3_MMAP_BASE) return false;
    if (adres >= RING3_MMAP_LIMIT) return false;
    if (dlugosc > RING3_MMAP_LIMIT - adres) return false;
    return true;
}

bool mmap_zakres_pliku_poprawny(uint64_t rozmiar_pliku, uint64_t offset,
                                uint64_t dlugosc) {
    if (dlugosc == 0 || offset > rozmiar_pliku) return false;
    return dlugosc <= rozmiar_pliku - offset;
}

int64_t mmap_waliduj_flagi(uint64_t flagi, uint64_t ochrona) {
    /* Musi byc dokladnie jeden z MAP_SHARED / MAP_PRIVATE. */
    const uint64_t typ = flagi & POSIX_MAP_TYP_MASKA;
    if (typ != POSIX_MAP_SHARED && typ != POSIX_MAP_PRIVATE)
        return -22;  /* EINVAL */

    /* Dozwolone sa tylko znane bity. */
    const uint64_t dozwolone = POSIX_MAP_SHARED | POSIX_MAP_PRIVATE |
                               POSIX_MAP_FIXED | POSIX_MAP_ANONYMOUS |
                               POSIX_MAP_NORESERVE;
    if (flagi & ~dozwolone) return -22;  /* EINVAL */

    /* Ochrona: tylko znane bity. */
    if (ochrona & ~(POSIX_PROT_READ | POSIX_PROT_WRITE | POSIX_PROT_EXEC))
        return -22;  /* EINVAL */

    /*
     * W^X takze dla mmap: PROT_WRITE|PROT_EXEC jednoczesnie to ta sama
     * dziura, ktora loader odrzuca w segmentach ELF. Wyjatek: PROT_NONE
     * (ochrona 0) jest legalne - to rezerwacja bez dostepu.
     */
    if ((ochrona & POSIX_PROT_WRITE) && (ochrona & POSIX_PROT_EXEC)) return -13;  /* EACCES */
    return 0;
}

int32_t mmap_znajdz_region(const MmapRegion* regiony, uint32_t liczba,
                           uint64_t adres, uint64_t dlugosc) {
    if (!regiony || !mmap_adres_poprawny(adres, dlugosc)) return -1;
    const uint64_t koniec = adres + dlugosc;
    for (uint32_t i = 0; i < liczba; ++i) {
        if (!regiony[i].uzywany) continue;
        const uint64_t r_koniec = regiony[i].adres + regiony[i].dlugosc;
        if (adres >= regiony[i].adres && koniec <= r_koniec) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

uint64_t mmap_znajdz_miejsce(const MmapRegion* regiony, uint32_t liczba,
                             uint64_t dlugosc) {
    if (dlugosc == 0 || (dlugosc & MASKA_STRONY) != 0) return 0;
    if (dlugosc > RING3_MMAP_ROZMIAR) return 0;

    /*
     * First-fit: przechodzimy okno od dolu, probujac kolejne kandydatury.
     * Sortowanie regionow byloby szybsze asymptotycznie, ale przy limicie
     * 16 regionow koszt jest pomijalny, a brak sortowania = brak stanu
     * pomocniczego i brak alokacji.
     */
    uint64_t kandydat = RING3_MMAP_BASE;
    uint32_t proby = 0;
    while (kandydat + dlugosc <= RING3_MMAP_LIMIT && proby <= MMAP_MAKS_REGIONOW + 1) {
        ++proby;
        bool kolizja = false;
        uint64_t nastepny = 0;
        for (uint32_t i = 0; i < liczba; ++i) {
            if (!regiony[i].uzywany) continue;
            const uint64_t r_start = regiony[i].adres;
            const uint64_t r_koniec = regiony[i].adres + regiony[i].dlugosc;
            if (kandydat < r_koniec && r_start < kandydat + dlugosc) {
                kolizja = true;
                if (r_koniec > nastepny) nastepny = r_koniec;
            }
        }
        if (!kolizja) return kandydat;
        if (nastepny <= kandydat) return 0;  /* brak postepu: unikamy petli */
        kandydat = nastepny;
    }
    return 0;
}
