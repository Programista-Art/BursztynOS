/*
 * Bursztyn OS - centralny identyfikator wersji systemu.
 *
 * Jedno zrodlo prawdy o wersji Bursztyn OS.
 * Wczesniej wersja systemu nie istniala centralnie (tylko wersje
 * per-aplikacyjne w manifestach oraz wersje ABI BWS/PSF).
 * Mechanizm aktualizacji wymaga porownywania wersji, wiec ten naglowek
 * jest wlaczany zarowno przez jadro, jak i przez format aktualizacji.
 *
 * Historia wersji systemu:
 *   0.1.0 - pierwsza wersja bazowa (stan przed mechanizmem aktualizacji).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define BURSZTYN_WERSJA_MAJOR 0U
#define BURSZTYN_WERSJA_MINOR 1U
#define BURSZTYN_WERSJA_PATCH 0U

#define BURSZTYN_WERSJA_TEKST "0.1.0"

/* Kanal domyslny dla wykazu aktualizacji. */
#define BURSZTYN_KANAL_DOMYSLNY "stabilny"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bursztyn_wersja {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} bursztyn_wersja_t;

/* Wypelnia strukture biezaca wersja systemu. Zawsze sie udaje. */
static inline void bursztyn_wersja_biezaca(bursztyn_wersja_t* out) {
    if (!out) return;
    out->major = BURSZTYN_WERSJA_MAJOR;
    out->minor = BURSZTYN_WERSJA_MINOR;
    out->patch = BURSZTYN_WERSJA_PATCH;
}

#ifdef __cplusplus
}
#endif
