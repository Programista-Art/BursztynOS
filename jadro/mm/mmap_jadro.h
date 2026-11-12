#pragma once
#include "mmap.h"
#include <stdint.h>

/*
 * Bursztyn OS - mmap w jadrze (mapowanie ramek + ksiegowanie).
 *
 * Rozdzial odpowiedzialnosci:
 *
 *   mmap.cpp       - czysta arytmetyka: gdzie wolno, jakie flagi, gdzie jest
 *                    region. Testowalne na hoscie.
 *   mmap_jadro.cpp - faktyczne mapowanie stron przez VMM/PMM i tablica
 *                    regionow per PID. Wymaga CR3 i PMM, wiec dziala tylko
 *                    w Ring 0.
 *
 * Zakres obslugi (swiadomie waski na tym etapie):
 *
 *   - MAP_ANONYMOUS z MAP_PRIVATE albo MAP_SHARED: mapowanie EAGER, ramki
 *     zerowane przy alokacji (PMM juz to robi),
 *   - MAP_FIXED: tylko wewnatrz okna mmap,
 *   - mmap plikowy (-1 fd z MAP_ANONYMOUS jest wymagane): zwraca -ENOSYS,
 *   - COW i demand paging: nie, patrz dlug techniczny w dokumentacji.
 */

/* Stan mmap jednego procesu. */
struct StanMmapProcesu {
    MmapRegion regiony[MMAP_MAKS_REGIONOW];
    uint32_t liczba;
};

/* Zeruje stan przy recyklingu PID. */
void mmap_zwolnij_proces(int pid);

/* Wskaznik do stanu (albo nullptr dla zlego PID). Tylko do diagnostyki. */
const StanMmapProcesu* mmap_stan(int pid);

/*
 * mmap(2) dla ABI SysV.
 *
 * Zwraca adres (dodatni) albo 0 przy porazce. Kod bledu trafia do `bledy`
 * jako UJEMNE -errno (konwencja ABI SysV), zeby wolajacy mogl je zwrocic
 * bezposrednio bez kolejnej negacji.
 */
uint64_t mmap_jadro_mapuj(int pid, uint64_t adres, uint64_t dlugosc,
                          uint64_t ochrona, uint64_t flagi, int64_t fd,
                          uint64_t offset, int64_t* bledy);

/* munmap(2): zwalnia ramki i wpis. Zwraca 0 albo -errno. */
int64_t mmap_jadro_odmapuj(int pid, uint64_t adres, uint64_t dlugosc);

/* mprotect(2): zmienia ochrone istniejacego regionu. */
int64_t mmap_jadro_ochrona(int pid, uint64_t adres, uint64_t dlugosc,
                           uint64_t ochrona);
