#pragma once
#include "jadro/bws/pzb.h"
/* Private kernel metadata; does not change proces_t or BWS ABI. PID reuse is
 * initialized by the loader before any new process can be published. */
struct UkladProcesu {
    uint64_t baza_obrazu, entry, stos_dol, stos_szczyt, canary;
    uint64_t sterta_baza, sterta_limit, shm_baza;
    uint32_t wersja;
};
extern UkladProcesu uklady_procesow[PZB_MAKS_PROCESOW];
