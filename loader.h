/*
 * Bursztyn OS - publiczny interfejs loadera programow .bur
 *
 * Ten naglowek definiuje stabilna, binarna czesc formatu programu BUR
 * oraz publiczne wejscie loadera uzywane przez jadro i BWS.
 *
 * WAZNE:
 * NaglowekBur jest zapisywany bezposrednio do pliku .bur, dlatego jego
 * rozmiar, kolejnosc pol i offsety stanowia ABI formatu pliku.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pzb.h"

/* =========================================================================
 * 1. FLAGI STRON x86_64 UZYWANE PRZEZ LOADER
 * ========================================================================= */

/*
 * Typowane stale zamiast makr zapobiegaja przypadkowemu uzyciu typu int
 * i zachowuja zgodnosc z argumentami VMM opartymi na uint32_t.
 */
inline constexpr uint32_t FLAGA_OBECNA =
    1U << 0; // Present

inline constexpr uint32_t FLAGA_ZAPIS =
    1U << 1; // Read/Write

inline constexpr uint32_t FLAGA_USER =
    1U << 2; // User/Supervisor: dostep z Ring 3

/* =========================================================================
 * 2. FORMAT PLIKU .BUR
 * ========================================================================= */

inline constexpr uint8_t BUR_MAGIA_0 = 'B';
inline constexpr uint8_t BUR_MAGIA_1 = 'U';
inline constexpr uint8_t BUR_MAGIA_2 = 'R';
inline constexpr uint8_t BUR_MAGIA_3 = '\0';

inline constexpr size_t BUR_ROZMIAR_MAGII = 4;

/*
 * Naglowek binarny Bursztynowego Programu Wykonywalnego.
 *
 * Format na dysku:
 *
 * offset  rozmiar  pole
 * 0x00       4     magia = "BUR\0"
 * 0x04       8     punkt_wejscia
 * 0x0C       8     tekst_przesuniecie
 * 0x14       8     tekst_rozmiar
 * 0x1C       8     tekst_wirtualny
 * 0x24       8     dane_przesuniecie
 * 0x2C       8     dane_rozmiar
 * 0x34       8     dane_wirtualny
 *
 * Razem: 60 bajtow.
 *
 * __attribute__((packed)) jest wymagane, poniewaz bez niego kompilator
 * x86_64 dodalby padding po polu magia[4] i zmienil format pliku.
 */
struct NaglowekBur {
    uint8_t magia[4];

    uint64_t punkt_wejscia;

    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;

    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

/* =========================================================================
 * 3. KONTROLA ABI FORMATU BUR
 * ========================================================================= */

static_assert(
    sizeof(NaglowekBur) == 60,
    "NaglowekBur musi miec dokladnie 60 bajtow"
);

static_assert(
    alignof(NaglowekBur) == 1,
    "NaglowekBur musi pozostac struktura packed"
);

static_assert(
    offsetof(NaglowekBur, magia) == 0x00,
    "Nieprawidlowy offset pola magia"
);

static_assert(
    offsetof(NaglowekBur, punkt_wejscia) == 0x04,
    "Nieprawidlowy offset pola punkt_wejscia"
);

static_assert(
    offsetof(NaglowekBur, tekst_przesuniecie) == 0x0C,
    "Nieprawidlowy offset pola tekst_przesuniecie"
);

static_assert(
    offsetof(NaglowekBur, tekst_rozmiar) == 0x14,
    "Nieprawidlowy offset pola tekst_rozmiar"
);

static_assert(
    offsetof(NaglowekBur, tekst_wirtualny) == 0x1C,
    "Nieprawidlowy offset pola tekst_wirtualny"
);

static_assert(
    offsetof(NaglowekBur, dane_przesuniecie) == 0x24,
    "Nieprawidlowy offset pola dane_przesuniecie"
);

static_assert(
    offsetof(NaglowekBur, dane_rozmiar) == 0x2C,
    "Nieprawidlowy offset pola dane_rozmiar"
);

static_assert(
    offsetof(NaglowekBur, dane_wirtualny) == 0x34,
    "Nieprawidlowy offset pola dane_wirtualny"
);

/* =========================================================================
 * 4. PUBLICZNE API LOADERA
 * ========================================================================= */

/*
 * Uruchamia program .bur z systemu plikow.
 *
 * sciezka_pliku:
 *   sciezka do pliku .bur.
 *
 * bzl_poziom:
 *   Bursztynowy Poziom Zaufania procesu, aktualnie 0..5.
 *
 * flagi_praw:
 *   maska uprawnien PZB przypisywana do procesu.
 *
 * z_syscalla:
 *   true  - sciezka_pliku pochodzi z Ring 3 i MUSI zostac bezpiecznie
 *           skopiowana z przestrzeni uzytkownika;
 *   false - sciezka_pliku pochodzi z zaufanego kodu jadra.
 *
 * Zwraca:
 *   true  - proces zostal w pelni przygotowany i opublikowany jako GOTOWY;
 *   false - blad walidacji, brak zasobow, zla sciezka albo uruchomiona
 *           juz instancja tego samego programu.
 */
extern "C" bool bws_uruchom_program_z_pliku(
    const char* sciezka_pliku,
    uint8_t bzl_poziom,
    uint64_t flagi_praw,
    bool z_syscalla
);
