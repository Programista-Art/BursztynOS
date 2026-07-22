/*
 * Mechanizm: Nagłówki sterownika AHCI (SATA / SSD)
 * Opis: Interfejs publiczny dla innych modułów jądra.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Blok extern "C" gwarantuje, że kompilator C++ nie zdekoruje nazw tych funkcji.
// Dzięki temu Jądro będzie mogło bez problemu znaleźć je podczas konsolidacji (linkowania).
#ifdef __cplusplus
extern "C" {
#endif

void inicjalizuj_kontroler_ahci();
bool czytaj_z_glownego_dysku_ahci(uint64_t lba, uint32_t ilosc_sektorow, void* bufor_docelowy);
bool zapisz_na_glowny_dysk_ahci(uint64_t lba, uint32_t ilosc_sektorow, void* dane_zrodlowe);

#ifdef __cplusplus
}
#endif