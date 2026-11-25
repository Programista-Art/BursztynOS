#pragma once
/*
 * BWS 111: informacja o systemie plikow (tylko odczyt, bez uprawnien specjalnych - te same dane co wolne
 * miejsce w Eksploratorze). arg1 = bufor*, arg2 = rozmiar bufora (>= sizeof(BwsMagazynV1)), arg3 = wersja (1).
 * Wynik: 0 = OK, 1 = zla wersja, 2 = zly bufor, 3 = PSF niegotowy.
 *
 * BWS_MAG_TRWALY: PSF/BSP2 zamontowany z partycji dysku (zapisy przetrwaja restart).
 * Bez tej flagi system plikow dziala wylacznie w RAM (sesja Live lub awaryjny RAM FS) - pliki znikna.
 */
#include <stdint.h>
#include <stddef.h>

#define BWS_MAGAZYN_V1 1U
enum : uint32_t {
    BWS_MAG_GOTOWY = 1u << 0,
    BWS_MAG_TRWALY = 1u << 1,
    BWS_MAG_ZAINSTALOWANY = 1u << 2,   // system uruchomiony z instalacji (nie z ISO Live)
};
enum : uint64_t { BWS_MAG_OK = 0, BWS_MAG_ZLA_WERSJA = 1, BWS_MAG_ZLY_BUFOR = 2, BWS_MAG_NIEGOTOWY = 3 };

struct BwsMagazynV1 {
    uint32_t wersja, rozmiar;
    uint32_t flagi;
    uint32_t maks_nazwa_bajty;          // dlugosc nazwy w bajtach UTF-8 (bez NUL)
    uint64_t pojemnosc_bajty;           // obszar danych PSF
    uint64_t wolne_bajty;               // wolne bloki danych x 4096
    uint64_t maks_rozmiar_pliku;        // limit formatu (wskazniki bezposrednie + 1x i 2x posrednie)
    uint64_t wezly, wolne_wezly;        // kazdy plik i katalog zajmuje jeden wezel
    uint32_t rozmiar_bloku, zarezerwowane;
};
#ifdef __cplusplus
static_assert(sizeof(BwsMagazynV1) == 64, "ABI BWS 111");
static_assert(offsetof(BwsMagazynV1, pojemnosc_bajty) == 16, "ABI BWS 111");
#endif
