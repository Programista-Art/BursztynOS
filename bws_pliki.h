#pragma once

#include <stdint.h>
#include <stddef.h>

/* Addytywne ABI BWS dla metadanych PSF i systemowego drag & drop. */

#define BWS_METADANE_WERSJA 1U
#define BWS_META_ROZMIAR_DOSTEPNY (1U << 0)
#define BWS_META_CZAS_DOSTEPNY    (1U << 1)
#define BWS_META_PZB_DOSTEPNY     (1U << 2)

/* czas_utworzenia_rtc ma postac dziesietna YYYYMMDDhhmmss. */
struct BwsMetadanePliku {
    uint16_t wersja;
    uint16_t flagi;
    uint8_t typ;
    uint8_t poziom_pzb;
    uint16_t zarezerwowane;
    uint64_t rozmiar;
    uint64_t czas_utworzenia_rtc;
};

#define BWS_DROP_SCIEZKA_MAX 512U
#define BWS_DROP_CELE_MAX 32U

struct BwsCelDrop {
    int32_t x;
    int32_t y;
    int32_t szer;
    int32_t wys;
    char folder[BWS_DROP_SCIEZKA_MAX];
};

enum BwsWynikDrop : uint32_t {
    BWS_DROP_BRAK_CELU = 0,
    BWS_DROP_CEL_POPRAWNY = 1,
    BWS_DROP_PRZENIESIONO = 2,
    BWS_DROP_BLAD = 3
};

/* Wspoldzielony, bezplikowy schowek operacji na plikach (BWS 52..55). */
#define BWS_SCHOWEK_WERSJA 1U

enum BwsOperacjaSchowka : uint8_t {
    BWS_SCHOWEK_PUSTY = 0,
    BWS_SCHOWEK_COPY = 1,
    BWS_SCHOWEK_CUT = 2
};

struct BwsSchowekPlikow {
    uint16_t wersja;
    uint8_t operacja;
    uint8_t typ;
    uint32_t zarezerwowane;
    uint64_t generacja;
    char sciezka[BWS_DROP_SCIEZKA_MAX];
};

#ifdef __cplusplus
static_assert(sizeof(BwsMetadanePliku) == 24,
              "Zmiana BwsMetadanePliku narusza ABI BWS 47");
static_assert(offsetof(BwsMetadanePliku, rozmiar) == 8,
              "Nieprawidlowy uklad BwsMetadanePliku");
static_assert(sizeof(BwsCelDrop) == 528,
              "Zmiana BwsCelDrop narusza ABI BWS 49");
static_assert(sizeof(BwsSchowekPlikow) == 528,
              "Zmiana BwsSchowekPlikow narusza ABI BWS 52/53");
static_assert(offsetof(BwsSchowekPlikow, sciezka) == 16,
              "Nieprawidlowy uklad schowka plikow");
#endif
