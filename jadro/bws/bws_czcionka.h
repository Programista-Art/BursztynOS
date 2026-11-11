#pragma once
#include <stdint.h>
#include <stddef.h>

/* Additive BWS 51 operation. arg1=0 still returns the legacy height (16).
 * arg1=1, arg2=output, arg3=sizeof(output): a bounded copy of the system font.
 * arg1=2, arg2=output, arg3=BSF::UnicodeBlob::Size: bounded copy of the
 * read-only BSF Unicode 15.1.0 layout tables (bsf/unicode_layout_index.h).
 * No pointers, MMIO or writable kernel mappings are exported. */
constexpr uint32_t BWS_CZCIONKA_WERSJA = 1;
constexpr uint32_t BWS_CZCIONKA_GLIFY = 1170;
struct BwsCzcionkaSystemowa {
    uint32_t wersja;
    uint32_t rozmiar;
    uint32_t liczba_glifow;
    uint32_t wysokosc;
    uint8_t szerokosci[BWS_CZCIONKA_GLIFY];
    uint16_t bitmapy[BWS_CZCIONKA_GLIFY][16];
};
static_assert(sizeof(BwsCzcionkaSystemowa) == 38628, "System font ABI");

uint64_t bws_czcionka_kopiuj(uint64_t prawa, uint64_t polecenie,
                           uint64_t bufor, uint64_t rozmiar);
