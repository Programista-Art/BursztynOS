/*
 * Bursztyn OS — Specyfikacja techniczna, walidator formatu wykonywalnego BUR v2,
 * procesor relokacji oraz planer ukladu przestrzeni adresowej Ring 3 ASLR (bur_v2.h).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pamiec.h"
#include "pzb.h"

/* =========================================================================
 * 1. STALE, WERSJONOWANIE I FLAGI FORMATU BUR v2
 * ========================================================================= */

inline constexpr uint8_t BUR_V2_MAGIA_0 = 'B';
inline constexpr uint8_t BUR_V2_MAGIA_1 = 'U';
inline constexpr uint8_t BUR_V2_MAGIA_2 = 'R';
inline constexpr uint8_t BUR_V2_MAGIA_3 = 0x02U;

inline constexpr uint16_t BUR_V2_WERSJA_AKTUALNA  = 2U;
inline constexpr uint16_t BUR_V2_ROZMIAR_NAGLOWKA = 160U;
inline constexpr uint16_t BUR_V2_ARCH_X86_64      = 0x003EU;
inline constexpr uint16_t BUR_V2_ABI_BIEZACE      = 1U;
inline constexpr uint32_t BUR_V2_STRONA_4K        = 4096U;

/* Flagi bezpieczenstwa naglowka BUR v2 (pole `flagi`) */
inline constexpr uint32_t BUR_V2_FLAGA_PIE            = 1U << 0; // Obraz relokowalny (ASLR-ready)
inline constexpr uint32_t BUR_V2_FLAGA_SSP            = 1U << 1; // Wymaga inicjalizacji __stack_chk_guard
inline constexpr uint32_t BUR_V2_FLAGA_WX_SCISLY      = 1U << 2; // Scisle rozdzielenie .text (RX), .rodata (RO+NX), .data (RW+NX)
inline constexpr uint32_t BUR_V2_FLAGA_STRAZNIK_RO    = 1U << 3; // Dedykowana strona 4 KiB RO+NX dla __stack_chk_guard
inline constexpr uint32_t BUR_V2_FLAGA_SUMA_KONTROLNA = 1U << 4; // Wymagana weryfikacja FNV-1a64 naglowka

inline constexpr uint32_t BUR_V2_FLAGI_ZNANE =
    BUR_V2_FLAGA_PIE |
    BUR_V2_FLAGA_SSP |
    BUR_V2_FLAGA_WX_SCISLY |
    BUR_V2_FLAGA_STRAZNIK_RO |
    BUR_V2_FLAGA_SUMA_KONTROLNA;

/* Wymagane funkcje loadera (pole `wymagane_funkcje`; nieznane bity powodowuja odrzucenie) */
inline constexpr uint32_t BUR_V2_FUNKCJA_BRAK        = 0U;
inline constexpr uint32_t BUR_V2_FUNKCJA_ASLR        = 1U << 0;
inline constexpr uint32_t BUR_V2_FUNKCJA_RELOKACJE   = 1U << 1;
inline constexpr uint32_t BUR_V2_FUNKCJA_STRAZNIK_RO = 1U << 2;

inline constexpr uint32_t BUR_V2_FUNKCJE_WSPIERANE =
    BUR_V2_FUNKCJA_ASLR |
    BUR_V2_FUNKCJA_RELOKACJE |
    BUR_V2_FUNKCJA_STRAZNIK_RO;

/* Jawne kody bledow walidacji i ladowania BUR v2 */
enum BurV2KodBledu : uint32_t {
    BUR_V2_OK = 0U,
    BUR_V2_BLAD_ZA_MALY_PLIK,
    BUR_V2_BLAD_ZA_DUZY_PLIK,
    BUR_V2_BLAD_ZLA_MAGIA,
    BUR_V2_BLAD_ZLA_WERSJA,
    BUR_V2_BLAD_ZLY_ROZMIAR_NAGLOWKA,
    BUR_V2_BLAD_ZLA_ARCHITEKTURA,
    BUR_V2_BLAD_ZLE_ABI,
    BUR_V2_BLAD_NIEZNANE_FLAGI,
    BUR_V2_BLAD_NIEZNANE_FUNKCJE,
    BUR_V2_BLAD_ZLE_WYROWNANIE,
    BUR_V2_BLAD_POLE_ZAREZERWOWANE,
    BUR_V2_BLAD_SUMA_KONTROLNA,
    BUR_V2_BLAD_SEGMENT_TEKST,
    BUR_V2_BLAD_PRZEPELNIENIE_TEKST,
    BUR_V2_BLAD_PUNKT_WEJSCIA,
    BUR_V2_BLAD_SEGMENT_RODATA,
    BUR_V2_BLAD_PRZEPELNIENIE_RODATA,
    BUR_V2_BLAD_SEGMENT_DANE,
    BUR_V2_BLAD_PRZEPELNIENIE_DANE,
    BUR_V2_BLAD_STRAZNIK_STOSU,
    BUR_V2_BLAD_TABLICA_RELOKACJI,
    BUR_V2_BLAD_ROZMIAR_STOSU,
    BUR_V2_BLAD_UPRAWNIENIA_METADANE,
    BUR_V2_BLAD_RELOKACJA_TYP,
    BUR_V2_BLAD_RELOKACJA_ZAKRES,
    BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE,
    BUR_V2_BLAD_BRAK_ENTROPII,
    BUR_V2_BLAD_BRAK_PAMIECI,
    BUR_V2_BLAD_MAPOWANIE_VMM,
};

inline const char* bur_v2_opis_bledu(BurV2KodBledu kod) noexcept {
    switch (kod) {
        case BUR_V2_OK:                          return nullptr;
        case BUR_V2_BLAD_ZA_MALY_PLIK:           return "plik mniejszy niz NaglowekBurV2 (160 B)";
        case BUR_V2_BLAD_ZA_DUZY_PLIK:           return "plik przekracza BUR_V2_MAKS_OBRAZ_ROZMIAR";
        case BUR_V2_BLAD_ZLA_MAGIA:              return "nieprawidlowa magia BUR v2";
        case BUR_V2_BLAD_ZLA_WERSJA:             return "nieobslugiwana wersja naglowka BUR v2";
        case BUR_V2_BLAD_ZLY_ROZMIAR_NAGLOWKA:   return "nieprawidlowy rozmiar_naglowka w NaglowekBurV2";
        case BUR_V2_BLAD_ZLA_ARCHITEKTURA:       return "nieobslugiwana architektura w NaglowekBurV2";
        case BUR_V2_BLAD_ZLE_ABI:                return "nieobslugiwana wersja ABI BWS w NaglowekBurV2";
        case BUR_V2_BLAD_NIEZNANE_FLAGI:         return "nieznane bity flag w NaglowekBurV2";
        case BUR_V2_BLAD_NIEZNANE_FUNKCJE:       return "nieobslugiwane wymagane_funkcje w NaglowekBurV2";
        case BUR_V2_BLAD_ZLE_WYROWNANIE:         return "nieprawidlowe wyrownanie_strony w NaglowekBurV2";
        case BUR_V2_BLAD_POLE_ZAREZERWOWANE:     return "niezerowe pole zarezerwowane w NaglowekBurV2";
        case BUR_V2_BLAD_SUMA_KONTROLNA:         return "nieprawidlowa suma_kontrolna_naglowka BUR v2";
        case BUR_V2_BLAD_SEGMENT_TEKST:          return "nieprawidlowe parametry segmentu .text";
        case BUR_V2_BLAD_PRZEPELNIENIE_TEKST:    return "przepelnienie zakresu segmentu .text";
        case BUR_V2_BLAD_PUNKT_WEJSCIA:          return "punkt_wejscia_rva poza segmentem .text";
        case BUR_V2_BLAD_SEGMENT_RODATA:         return "nieprawidlowe lub nakladajace sie parametry .rodata";
        case BUR_V2_BLAD_PRZEPELNIENIE_RODATA:   return "przepelnienie zakresu segmentu .rodata";
        case BUR_V2_BLAD_SEGMENT_DANE:           return "nieprawidlowe lub nakladajace sie parametry .data/.bss";
        case BUR_V2_BLAD_PRZEPELNIENIE_DANE:     return "przepelnienie zakresu segmentu .data/.bss";
        case BUR_V2_BLAD_STRAZNIK_STOSU:         return "offset_straznika_stosu poza dozwolonym zakresem";
        case BUR_V2_BLAD_TABLICA_RELOKACJI:      return "nieprawidlowy naglowek lub zakres tablicy relokacji";
        case BUR_V2_BLAD_ROZMIAR_STOSU:          return "liczba stron stosu poza dozwolonym zakresem 4..256";
        case BUR_V2_BLAD_UPRAWNIENIA_METADANE:   return "nieprawidlowe metadane uprawnien / zaufania";
        case BUR_V2_BLAD_RELOKACJA_TYP:          return "nieobslugiwany typ relokacji BUR v2";
        case BUR_V2_BLAD_RELOKACJA_ZAKRES:       return "relokacja poza dozwolonym segmentem obrazu";
        case BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE: return "przepelnienie wartosci lub zasiegu relokacji";
        case BUR_V2_BLAD_BRAK_ENTROPII:          return "podsystem entropii CSPRNG niegotowy do ASLR";
        case BUR_V2_BLAD_BRAK_PAMIECI:           return "brak wolnych ramek fizycznych dla obrazu BUR v2";
        case BUR_V2_BLAD_MAPOWANIE_VMM:          return "blad mapowania stron VMM dla obrazu BUR v2";
        default:                                 return "nieznany blad BUR v2";
    }
}

/* Typy relokacji BUR v2 */
enum BurV2TypRelokacji : uint32_t {
    BUR_V2_RELOC_BRAK        = 0U,
    BUR_V2_RELOC_ABS64       = 1U, // R_X86_64_RELATIVE / R_X86_64_64: *(uint64_t*)(baza + offset) = baza + addend
    BUR_V2_RELOC_SIEC_DANE64 = 2U, // Alias R_X86_64_RELATIVE w .data: *(uint64_t*)(baza + offset) = baza + addend
    BUR_V2_RELOC_PC32        = 3U, // R_X86_64_PC32 / PLT32: *(int32_t*)(baza + offset) = (baza + addend) - (baza + offset)
    BUR_V2_RELOC_ABS32       = 4U, // R_X86_64_32: *(uint32_t*)(baza + offset) = uint32(baza + addend), wymaga <= UINT32_MAX
    BUR_V2_RELOC_ABS32S      = 5U, // R_X86_64_32S: *(int32_t*)(baza + offset) = int32(baza + addend), wymaga INT32_MIN..INT32_MAX
};

/* Pojedynczy wpis tablicy relokacji (24 bajty, naturalnie wyrownany, little-endian) */
struct BurV2Relokacja {
    uint64_t offset_w_obrazie; // 0x00: RVA modyfikowanego pola wzgledem bazy obrazu
    int64_t  addend;           // 0x08: RVA symbolu docelowego + staly addend relokacji
    uint32_t typ;              // 0x10: BurV2TypRelokacji
    uint32_t zarezerwowane;    // 0x14: Musi wynosic 0
} __attribute__((packed));

static_assert(sizeof(BurV2Relokacja) == 24, "BurV2Relokacja musi miec 24 bajty");
static_assert(offsetof(BurV2Relokacja, offset_w_obrazie) == 0x00, "offset_w_obrazie == 0x00");
static_assert(offsetof(BurV2Relokacja, addend) == 0x08, "addend == 0x08");
static_assert(offsetof(BurV2Relokacja, typ) == 0x10, "typ == 0x10");
static_assert(offsetof(BurV2Relokacja, zarezerwowane) == 0x14, "zarezerwowane == 0x14");

/*
 * Naglowek binarny BUR v2 (160 bajtow, little-endian, bez niejawnego paddingu):
 *   - Jawne oddzielenie .text (RX), .rodata (R-- NX), .ssp_guard (R-- NX), .data (RW- NX) oraz .bss (RW- NX).
 *   - Jawna wersja formatu, rozmiar naglowka, architektura x86-64, wersja ABI BWS, wymagane funkcje.
 *   - Tablica relokacji ze sprawdzonym rozmiarem rekordu.
 *   - Dedykowana strona RO+NX dla kanarka stosu (`BUR_V2_FLAGA_STRAZNIK_RO`) lub slot w `.data`.
 *   - Jawne pole zarezerwowane (musi byc 0) oraz suma kontrolna FNV-1a64.
 */
struct NaglowekBurV2 {
    uint8_t  magia[4];                 // 0x00: "BUR\x02"
    uint16_t wersja;                   // 0x04: BUR_V2_WERSJA_AKTUALNA (2)
    uint16_t rozmiar_naglowka;         // 0x06: BUR_V2_ROZMIAR_NAGLOWKA (160)
    uint16_t architektura;             // 0x08: BUR_V2_ARCH_X86_64 (0x003E)
    uint16_t wersja_abi;               // 0x0A: BUR_V2_ABI_BIEZACE (1)
    uint32_t flagi;                    // 0x0C: BUR_V2_FLAGA_*
    uint32_t wymagane_funkcje;         // 0x10: BUR_V2_FUNKCJA_* (nieznane bity odrzucane)
    uint32_t wyrownanie_strony;        // 0x14: BUR_V2_STRONA_4K (4096)

    uint64_t punkt_wejscia_rva;        // 0x18: RVA punktu wejscia wzgledem bazy obrazu (w .text)

    uint64_t tekst_przesuniecie;       // 0x20: Offset .text w pliku (4 KiB aligned)
    uint64_t tekst_rozmiar;            // 0x28: Rozmiar .text w pliku i pamieci (4 KiB aligned, RX)
    uint64_t tekst_rva;                // 0x30: RVA .text wzgledem bazy obrazu (4 KiB aligned)

    uint64_t rodata_przesuniecie;      // 0x38: Offset .rodata w pliku (4 KiB aligned)
    uint64_t rodata_rozmiar;           // 0x40: Rozmiar .rodata (4 KiB aligned, RO + NX, 0 jesli brak)
    uint64_t rodata_rva;               // 0x48: RVA .rodata (4 KiB aligned)

    uint64_t dane_przesuniecie;        // 0x50: Offset .data w pliku (4 KiB aligned)
    uint64_t dane_rozmiar_pliku;       // 0x58: Liczba bajtow .data kopiowanych z pliku
    uint64_t dane_rozmiar_pamieci;     // 0x60: Rozmiar .data + .bss w pamieci (4 KiB aligned, RW + NX)
    uint64_t dane_rva;                 // 0x68: RVA .data (4 KiB aligned)

    uint64_t reloc_przesuniecie;       // 0x70: Offset tablicy BurV2Relokacja w pliku (8 B aligned)
    uint32_t reloc_liczba;             // 0x78: Liczba wpisow relokacji
    uint16_t reloc_rozmiar_wpisu;      // 0x7C: sizeof(BurV2Relokacja) == 24 (lub 0 gdy reloc_liczba == 0)
    uint16_t domyślne_strony_stosu;    // 0x7E: Liczba stron 4 KiB stosu (4..256, domyslnie 16 = 64 KiB)

    uint64_t offset_straznika_stosu;   // 0x80: RVA strony .ssp_guard (4 KiB aligned gdy STRAZNIK_RO) lub slotu .data
    uint32_t metadane_uprawnien;       // 0x88: Opcjonalna maska deklarowanych uprawnien PZB (0 lub PRAWO_*)
    uint32_t poziom_zaufania_hint;     // 0x8C: Opcjonalny poziom zaufania (0..4)
    uint64_t zarezerwowane;            // 0x90: Musi wynosic 0 (niezerowe odrzucane fail-closed)
    uint64_t suma_kontrolna_naglowka;  // 0x98: FNV-1a64 po bajtach [0x00..0x97] (lub 0 gdy niewymagana)
} __attribute__((packed));

static_assert(sizeof(NaglowekBurV2) == 160, "NaglowekBurV2 musi miec dokladnie 160 bajtow");
static_assert(offsetof(NaglowekBurV2, magia) == 0x00, "magia == 0x00");
static_assert(offsetof(NaglowekBurV2, wersja) == 0x04, "wersja == 0x04");
static_assert(offsetof(NaglowekBurV2, rozmiar_naglowka) == 0x06, "rozmiar_naglowka == 0x06");
static_assert(offsetof(NaglowekBurV2, architektura) == 0x08, "architektura == 0x08");
static_assert(offsetof(NaglowekBurV2, wersja_abi) == 0x0A, "wersja_abi == 0x0A");
static_assert(offsetof(NaglowekBurV2, flagi) == 0x0C, "flagi == 0x0C");
static_assert(offsetof(NaglowekBurV2, wymagane_funkcje) == 0x10, "wymagane_funkcje == 0x10");
static_assert(offsetof(NaglowekBurV2, wyrownanie_strony) == 0x14, "wyrownanie_strony == 0x14");
static_assert(offsetof(NaglowekBurV2, punkt_wejscia_rva) == 0x18, "punkt_wejscia_rva == 0x18");
static_assert(offsetof(NaglowekBurV2, tekst_przesuniecie) == 0x20, "tekst_przesuniecie == 0x20");
static_assert(offsetof(NaglowekBurV2, tekst_rozmiar) == 0x28, "tekst_rozmiar == 0x28");
static_assert(offsetof(NaglowekBurV2, tekst_rva) == 0x30, "tekst_rva == 0x30");
static_assert(offsetof(NaglowekBurV2, rodata_przesuniecie) == 0x38, "rodata_przesuniecie == 0x38");
static_assert(offsetof(NaglowekBurV2, rodata_rozmiar) == 0x40, "rodata_rozmiar == 0x40");
static_assert(offsetof(NaglowekBurV2, rodata_rva) == 0x48, "rodata_rva == 0x48");
static_assert(offsetof(NaglowekBurV2, dane_przesuniecie) == 0x50, "dane_przesuniecie == 0x50");
static_assert(offsetof(NaglowekBurV2, dane_rozmiar_pliku) == 0x58, "dane_rozmiar_pliku == 0x58");
static_assert(offsetof(NaglowekBurV2, dane_rozmiar_pamieci) == 0x60, "dane_rozmiar_pamieci == 0x60");
static_assert(offsetof(NaglowekBurV2, dane_rva) == 0x68, "dane_rva == 0x68");
static_assert(offsetof(NaglowekBurV2, reloc_przesuniecie) == 0x70, "reloc_przesuniecie == 0x70");
static_assert(offsetof(NaglowekBurV2, reloc_liczba) == 0x78, "reloc_liczba == 0x78");
static_assert(offsetof(NaglowekBurV2, reloc_rozmiar_wpisu) == 0x7C, "reloc_rozmiar_wpisu == 0x7C");
static_assert(offsetof(NaglowekBurV2, domyślne_strony_stosu) == 0x7E, "domyslne_strony_stosu == 0x7E");
static_assert(offsetof(NaglowekBurV2, offset_straznika_stosu) == 0x80, "offset_straznika_stosu == 0x80");
static_assert(offsetof(NaglowekBurV2, metadane_uprawnien) == 0x88, "metadane_uprawnien == 0x88");
static_assert(offsetof(NaglowekBurV2, poziom_zaufania_hint) == 0x8C, "poziom_zaufania_hint == 0x8C");
static_assert(offsetof(NaglowekBurV2, zarezerwowane) == 0x90, "zarezerwowane == 0x90");
static_assert(offsetof(NaglowekBurV2, suma_kontrolna_naglowka) == 0x98, "suma_kontrolna_naglowka == 0x98");

/* Limity bezpieczenstwa BUR v2 */
inline constexpr uint64_t BUR_V2_MAKS_SEGMENT_ROZMIAR = 64ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t BUR_V2_MAKS_OBRAZ_ROZMIAR   = 128ULL * 1024ULL * 1024ULL;
inline constexpr uint32_t BUR_V2_MAKS_RELOKACJI       = 65536U;

/* Obliczanie sumy kontrolnej FNV-1a64 po pierwszych 152 bajtach NaglowekBurV2 (bez pola suma_kontrolna_naglowka) */
inline uint64_t bur_v2_oblicz_sume_naglowka(const NaglowekBurV2& n) noexcept {
    const uint8_t* bajty = reinterpret_cast<const uint8_t*>(&n);
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < offsetof(NaglowekBurV2, suma_kontrolna_naglowka); ++i) {
        h ^= static_cast<uint64_t>(bajty[i]);
        h *= 0x100000001B3ULL;
    }
    return h;
}

inline void bur_v2_ustaw_domyslne_pola_naglowka(NaglowekBurV2* n) noexcept {
    if (!n) return;
    n->magia[0] = BUR_V2_MAGIA_0;
    n->magia[1] = BUR_V2_MAGIA_1;
    n->magia[2] = BUR_V2_MAGIA_2;
    n->magia[3] = BUR_V2_MAGIA_3;
    n->wersja = BUR_V2_WERSJA_AKTUALNA;
    n->rozmiar_naglowka = BUR_V2_ROZMIAR_NAGLOWKA;
    n->architektura = BUR_V2_ARCH_X86_64;
    n->wersja_abi = BUR_V2_ABI_BIEZACE;
    n->wyrownanie_strony = BUR_V2_STRONA_4K;
    n->reloc_rozmiar_wpisu = sizeof(BurV2Relokacja);
}

/* =========================================================================
 * 2. ARCHITEKTURA PRZESTRZENI ADRESOWEJ RING 3 ASLR
 * =========================================================================
 *
 * Okna losowania (4 KiB page-aligned, z obowiazkowymi stronami straznika P=0):
 *   - Obraz PIE (.text/.rodata/.ssp_guard/.data/.bss):
 *       Baza losowana z okna [0x0000000100000000, 0x0000000500000000)
 *       (20 bitow entropii stron 4 KiB = 1 048 576 mozliwych pozycji,
 *        calkowicie powyzej niskiego okna jadra i legacy RING3_BASE).
 *   - Sterta procesu (Heap):
 *       Baza losowana z okna [0x0000000800000000, 0x0000000C00000000)
 *       (20 bitow entropii stron 4 KiB).
 *   - Pamiec wspoldzielona (SHM):
 *       Baza losowana z okna [0x0000001000000000, 0x0000001400000000)
 *       (20 bitow entropii stron 4 KiB).
 *   - Stos uzytkownika (Stack):
 *       Szczyt losowany z okna [0x00007FFFE0000000, 0x00007FFFF0000000)
 *       (16 bitow entropii stron 4 KiB) z niezmapowana strona straznika
 *       bezposrednio ponizej dolu stosu oraz powyzej szczytu stosu.
 */

inline constexpr uint64_t ASLR_PIE_WINDOW_BASE    = 0x0000000100000000ULL; // 4 GiB
inline constexpr uint64_t ASLR_PIE_WINDOW_PAGES   = 1ULL << 20;            // 20 bitow entropii (4 GiB okno)

inline constexpr uint64_t ASLR_HEAP_WINDOW_BASE   = 0x0000000800000000ULL; // 32 GiB
inline constexpr uint64_t ASLR_HEAP_WINDOW_PAGES  = 1ULL << 20;            // 20 bitow entropii (4 GiB okno)

inline constexpr uint64_t ASLR_SHM_WINDOW_BASE    = 0x0000001000000000ULL; // 64 GiB
inline constexpr uint64_t ASLR_SHM_WINDOW_PAGES   = 1ULL << 20;            // 20 bitow entropii (4 GiB okno)

inline constexpr uint64_t ASLR_STACK_WINDOW_BASE  = 0x00007FFFE0000000ULL;
inline constexpr uint64_t ASLR_STACK_WINDOW_PAGES = 1ULL << 16;            // 16 bitow entropii (256 MiB okno)

struct PlanUkladuAslrRing3 {
    uint64_t baza_obrazu;
    uint64_t tekst_va;
    uint64_t rodata_va;
    uint64_t dane_va;
    uint64_t punkt_wejscia_va;
    uint64_t straznik_stosu_va; // Adres __stack_chk_guard (strona RO+NX lub slot w .data)

    uint64_t sterta_baza;
    uint64_t sterta_limit;

    uint64_t shm_baza;
    uint64_t shm_limit;

    uint64_t stos_guard_dolny;
    uint64_t stos_dol;
    uint64_t stos_szczyt;
    uint64_t stos_guard_gorny;
};

/* =========================================================================
 * 3. WALIDATOR NAGLOWKA BUR v2 (BEZSTANOWY, ODPORNY NA OVERFLOW)
 * ========================================================================= */

inline BurV2KodBledu bur_v2_waliduj_naglowek_kod(
    const NaglowekBurV2& n,
    uint64_t rozmiar_pliku
) noexcept {
    if (rozmiar_pliku < sizeof(NaglowekBurV2)) {
        return BUR_V2_BLAD_ZA_MALY_PLIK;
    }
    if (rozmiar_pliku > BUR_V2_MAKS_OBRAZ_ROZMIAR) {
        return BUR_V2_BLAD_ZA_DUZY_PLIK;
    }
    if (n.magia[0] != BUR_V2_MAGIA_0 ||
        n.magia[1] != BUR_V2_MAGIA_1 ||
        n.magia[2] != BUR_V2_MAGIA_2 ||
        n.magia[3] != BUR_V2_MAGIA_3) {
        return BUR_V2_BLAD_ZLA_MAGIA;
    }
    if (n.wersja != BUR_V2_WERSJA_AKTUALNA) {
        return BUR_V2_BLAD_ZLA_WERSJA;
    }
    /* Akceptujemy 160 (lub 0 w prostych strukturach inicjalizowanych zerami jezeli nie ustawiono) ->
     * W produkcyjnym BUR v2 rozmiar_naglowka MUSI wynosic dokladnie sizeof(NaglowekBurV2) == 160. */
    if (n.rozmiar_naglowka != BUR_V2_ROZMIAR_NAGLOWKA) {
        return BUR_V2_BLAD_ZLY_ROZMIAR_NAGLOWKA;
    }
    if (n.architektura != BUR_V2_ARCH_X86_64) {
        return BUR_V2_BLAD_ZLA_ARCHITEKTURA;
    }
    if (n.wersja_abi != BUR_V2_ABI_BIEZACE) {
        return BUR_V2_BLAD_ZLE_ABI;
    }
    if ((n.flagi & ~BUR_V2_FLAGI_ZNANE) != 0) {
        return BUR_V2_BLAD_NIEZNANE_FLAGI;
    }
    if ((n.wymagane_funkcje & ~BUR_V2_FUNKCJE_WSPIERANE) != 0) {
        return BUR_V2_BLAD_NIEZNANE_FUNKCJE;
    }
    if (n.wyrownanie_strony != BUR_V2_STRONA_4K) {
        return BUR_V2_BLAD_ZLE_WYROWNANIE;
    }
    if (n.zarezerwowane != 0ULL) {
        return BUR_V2_BLAD_POLE_ZAREZERWOWANE;
    }
    if (n.poziom_zaufania_hint > 4U) {
        return BUR_V2_BLAD_UPRAWNIENIA_METADANE;
    }
    if (((n.flagi & BUR_V2_FLAGA_SUMA_KONTROLNA) != 0 || n.suma_kontrolna_naglowka != 0ULL) &&
        n.suma_kontrolna_naglowka != bur_v2_oblicz_sume_naglowka(n)) {
        return BUR_V2_BLAD_SUMA_KONTROLNA;
    }

    auto wyrownane4k = [](uint64_t v) noexcept {
        return (v & 0xFFFULL) == 0ULL;
    };

    if (n.tekst_rozmiar == 0 || n.tekst_rozmiar > BUR_V2_MAKS_SEGMENT_ROZMIAR ||
        !wyrownane4k(n.tekst_przesuniecie) || !wyrownane4k(n.tekst_rozmiar) ||
        !wyrownane4k(n.tekst_rva) || n.tekst_rva == 0 ||
        n.tekst_przesuniecie < sizeof(NaglowekBurV2)) {
        return BUR_V2_BLAD_SEGMENT_TEKST;
    }

    uint64_t tekst_plik_koniec = 0;
    uint64_t tekst_rva_koniec = 0;
    if (__builtin_add_overflow(n.tekst_przesuniecie, n.tekst_rozmiar, &tekst_plik_koniec) ||
        tekst_plik_koniec > rozmiar_pliku ||
        __builtin_add_overflow(n.tekst_rva, n.tekst_rozmiar, &tekst_rva_koniec)) {
        return BUR_V2_BLAD_PRZEPELNIENIE_TEKST;
    }

    if (n.punkt_wejscia_rva < n.tekst_rva || n.punkt_wejscia_rva >= tekst_rva_koniec) {
        return BUR_V2_BLAD_PUNKT_WEJSCIA;
    }

    uint64_t rodata_plik_koniec = tekst_plik_koniec;
    uint64_t rodata_rva_koniec = tekst_rva_koniec;
    if (n.rodata_rozmiar > 0) {
        if (n.rodata_rozmiar > BUR_V2_MAKS_SEGMENT_ROZMIAR ||
            !wyrownane4k(n.rodata_przesuniecie) ||
            !wyrownane4k(n.rodata_rozmiar) ||
            !wyrownane4k(n.rodata_rva) ||
            n.rodata_przesuniecie < tekst_plik_koniec ||
            n.rodata_rva < tekst_rva_koniec) {
            return BUR_V2_BLAD_SEGMENT_RODATA;
        }
        if (__builtin_add_overflow(n.rodata_przesuniecie, n.rodata_rozmiar, &rodata_plik_koniec) ||
            rodata_plik_koniec > rozmiar_pliku ||
            __builtin_add_overflow(n.rodata_rva, n.rodata_rozmiar, &rodata_rva_koniec)) {
            return BUR_V2_BLAD_PRZEPELNIENIE_RODATA;
        }
    } else if (n.rodata_przesuniecie != 0 || n.rodata_rva != 0) {
        return BUR_V2_BLAD_SEGMENT_RODATA;
    }

    uint64_t min_dane_rva = rodata_rva_koniec;
    if ((n.flagi & BUR_V2_FLAGA_STRAZNIK_RO) != 0) {
        if ((n.flagi & BUR_V2_FLAGA_SSP) == 0 ||
            !wyrownane4k(n.offset_straznika_stosu) ||
            n.offset_straznika_stosu < rodata_rva_koniec) {
            return BUR_V2_BLAD_STRAZNIK_STOSU;
        }
        if (__builtin_add_overflow(n.offset_straznika_stosu, static_cast<uint64_t>(ROZMIAR_RAMKI_4K), &min_dane_rva)) {
            return BUR_V2_BLAD_STRAZNIK_STOSU;
        }
    }

    if (n.dane_rozmiar_pamieci == 0 ||
        n.dane_rozmiar_pamieci > BUR_V2_MAKS_SEGMENT_ROZMIAR ||
        n.dane_rozmiar_pliku > n.dane_rozmiar_pamieci ||
        !wyrownane4k(n.dane_przesuniecie) ||
        !wyrownane4k(n.dane_rozmiar_pamieci) ||
        !wyrownane4k(n.dane_rva) ||
        n.dane_przesuniecie < rodata_plik_koniec ||
        n.dane_rva < min_dane_rva) {
        return BUR_V2_BLAD_SEGMENT_DANE;
    }

    uint64_t dane_plik_koniec = 0;
    uint64_t dane_rva_koniec = 0;
    if (__builtin_add_overflow(n.dane_przesuniecie, n.dane_rozmiar_pliku, &dane_plik_koniec) ||
        dane_plik_koniec > rozmiar_pliku ||
        __builtin_add_overflow(n.dane_rva, n.dane_rozmiar_pamieci, &dane_rva_koniec) ||
        dane_rva_koniec > BUR_V2_MAKS_OBRAZ_ROZMIAR) {
        return BUR_V2_BLAD_PRZEPELNIENIE_DANE;
    }

    if ((n.flagi & BUR_V2_FLAGA_STRAZNIK_RO) == 0) {
        if ((n.flagi & BUR_V2_FLAGA_SSP) != 0 || n.offset_straznika_stosu != 0) {
            if ((n.offset_straznika_stosu & 0x7ULL) != 0 ||
                n.offset_straznika_stosu < n.dane_rva ||
                n.offset_straznika_stosu + sizeof(uint64_t) > dane_rva_koniec) {
                return BUR_V2_BLAD_STRAZNIK_STOSU;
            }
        }
    }

    if (n.reloc_liczba > 0) {
        if (n.reloc_rozmiar_wpisu != sizeof(BurV2Relokacja) ||
            n.reloc_liczba > BUR_V2_MAKS_RELOKACJI ||
            (n.reloc_przesuniecie & 0x7ULL) != 0 ||
            n.reloc_przesuniecie < dane_plik_koniec) {
            return BUR_V2_BLAD_TABLICA_RELOKACJI;
        }
        const uint64_t bajty_reloc =
            static_cast<uint64_t>(n.reloc_liczba) * sizeof(BurV2Relokacja);
        uint64_t reloc_koniec = 0;
        if (__builtin_add_overflow(n.reloc_przesuniecie, bajty_reloc, &reloc_koniec) ||
            reloc_koniec > rozmiar_pliku) {
            return BUR_V2_BLAD_TABLICA_RELOKACJI;
        }
    } else if (n.reloc_rozmiar_wpisu != 0 && n.reloc_rozmiar_wpisu != sizeof(BurV2Relokacja)) {
        return BUR_V2_BLAD_TABLICA_RELOKACJI;
    }

    if (n.domyślne_strony_stosu < 4U || n.domyślne_strony_stosu > 256U) {
        return BUR_V2_BLAD_ROZMIAR_STOSU;
    }

    return BUR_V2_OK;
}

inline const char* bur_v2_blad_naglowka(
    const NaglowekBurV2& n,
    uint64_t rozmiar_pliku
) noexcept {
    return bur_v2_opis_bledu(bur_v2_waliduj_naglowek_kod(n, rozmiar_pliku));
}

inline bool bur_v2_waliduj_naglowek(
    const NaglowekBurV2& n,
    uint64_t rozmiar_pliku
) noexcept {
    return bur_v2_waliduj_naglowek_kod(n, rozmiar_pliku) == BUR_V2_OK;
}

/* =========================================================================
 * 4. PLANER PRZESTRZENI ADRESOWEJ RING 3 ASLR
 * ========================================================================= */

inline uint64_t bur_v2_indeks_bez_obciazenia(uint64_t los, uint64_t liczba_pozycji) noexcept {
    if (liczba_pozycji <= 1ULL) return 0ULL;
    if ((liczba_pozycji & (liczba_pozycji - 1ULL)) == 0ULL) {
        return los & (liczba_pozycji - 1ULL);
    }
    return los % liczba_pozycji;
}

inline bool bur_v2_zaplanuj_aslr(
    const NaglowekBurV2& n,
    uint64_t los_pie,
    uint64_t los_heap,
    uint64_t los_shm,
    uint64_t los_stack,
    PlanUkladuAslrRing3* out_plan
) noexcept {
    if (!out_plan) return false;

    const uint64_t pie_slot = bur_v2_indeks_bez_obciazenia(los_pie, ASLR_PIE_WINDOW_PAGES) * ROZMIAR_RAMKI_4K;
    const uint64_t heap_slot = bur_v2_indeks_bez_obciazenia(los_heap, ASLR_HEAP_WINDOW_PAGES) * ROZMIAR_RAMKI_4K;
    const uint64_t shm_slot = bur_v2_indeks_bez_obciazenia(los_shm, ASLR_SHM_WINDOW_PAGES) * ROZMIAR_RAMKI_4K;
    const uint64_t stack_slot = bur_v2_indeks_bez_obciazenia(los_stack, ASLR_STACK_WINDOW_PAGES) * ROZMIAR_RAMKI_4K;

    PlanUkladuAslrRing3 p{};
    p.baza_obrazu = (n.flagi & BUR_V2_FLAGA_PIE)
        ? (ASLR_PIE_WINDOW_BASE + pie_slot)
        : RING3_BASE;

    p.tekst_va = p.baza_obrazu + n.tekst_rva;
    p.rodata_va = n.rodata_rozmiar ? (p.baza_obrazu + n.rodata_rva) : 0ULL;
    p.dane_va = p.baza_obrazu + n.dane_rva;
    p.punkt_wejscia_va = p.baza_obrazu + n.punkt_wejscia_rva;
    p.straznik_stosu_va = n.offset_straznika_stosu
        ? (p.baza_obrazu + n.offset_straznika_stosu)
        : 0ULL;

    p.sterta_baza = ASLR_HEAP_WINDOW_BASE + heap_slot;
    p.sterta_limit = p.sterta_baza + (256ULL * 1024ULL * 1024ULL);

    p.shm_baza = ASLR_SHM_WINDOW_BASE + shm_slot;
    p.shm_limit = p.shm_baza + (256ULL * 1024ULL * 1024ULL);

    const uint64_t rozmiar_stosu =
        static_cast<uint64_t>(n.domyślne_strony_stosu) * ROZMIAR_RAMKI_4K;
    p.stos_szczyt = ASLR_STACK_WINDOW_BASE + stack_slot + rozmiar_stosu + ROZMIAR_RAMKI_4K;
    p.stos_dol = p.stos_szczyt - rozmiar_stosu;
    p.stos_guard_dolny = p.stos_dol - ROZMIAR_RAMKI_4K;
    p.stos_guard_gorny = p.stos_szczyt;

    // Weryfikacja braku nakladania sie wszystkich okien oraz kanonicznosci User VA:
    const uint64_t koniec_obrazu = p.dane_va + n.dane_rozmiar_pamieci;
    if (koniec_obrazu <= p.baza_obrazu || koniec_obrazu >= p.sterta_baza) return false;
    if (p.sterta_limit <= p.sterta_baza || p.sterta_limit >= p.shm_baza) return false;
    if (p.shm_limit <= p.shm_baza || p.shm_limit >= p.stos_guard_dolny) return false;
    if (p.stos_guard_gorny + ROZMIAR_RAMKI_4K >= 0x0000800000000000ULL) return false;
    if ((p.stos_szczyt & 0xFULL) != 0ULL) return false;

    *out_plan = p;
    return true;
}

/* =========================================================================
 * 5. WALIDACJA I APLIKOWANIE RELOKACJI BUR v2
 * ========================================================================= */

inline bool bur_v2_czy_rva_w_obrazie(
    const NaglowekBurV2& n,
    uint64_t rva
) noexcept {
    if (rva >= n.tekst_rva && rva < n.tekst_rva + n.tekst_rozmiar) return true;
    if (n.rodata_rozmiar > 0 && rva >= n.rodata_rva && rva < n.rodata_rva + n.rodata_rozmiar) return true;
    if ((n.flagi & BUR_V2_FLAGA_STRAZNIK_RO) != 0 &&
        rva >= n.offset_straznika_stosu && rva < n.offset_straznika_stosu + ROZMIAR_RAMKI_4K) return true;
    if (rva >= n.dane_rva && rva <= n.dane_rva + n.dane_rozmiar_pamieci) return true;
    return false;
}

inline BurV2KodBledu bur_v2_oblicz_wartosc_relokacji(
    const NaglowekBurV2& n,
    const BurV2Relokacja& r,
    uint64_t baza_obrazu,
    uint64_t* out_wartosc,
    size_t* out_szerokosc_bajtow
) noexcept {
    if (!out_wartosc || !out_szerokosc_bajtow) return BUR_V2_BLAD_RELOKACJA_ZAKRES;
    if (r.zarezerwowane != 0U) return BUR_V2_BLAD_RELOKACJA_TYP;

    const uint64_t dane_start = n.dane_rva;
    const uint64_t dane_koniec = n.dane_rva + n.dane_rozmiar_pamieci;

    size_t szerokosc = 0;
    if (r.typ == BUR_V2_RELOC_ABS64 || r.typ == BUR_V2_RELOC_SIEC_DANE64) {
        szerokosc = 8U;
        if ((r.offset_w_obrazie & 0x7ULL) != 0ULL) return BUR_V2_BLAD_RELOKACJA_ZAKRES;
    } else if (r.typ == BUR_V2_RELOC_PC32 || r.typ == BUR_V2_RELOC_ABS32 || r.typ == BUR_V2_RELOC_ABS32S) {
        szerokosc = 4U;
        if ((r.offset_w_obrazie & 0x3ULL) != 0ULL) return BUR_V2_BLAD_RELOKACJA_ZAKRES;
    } else {
        return BUR_V2_BLAD_RELOKACJA_TYP;
    }

    /*
     * W^X: Relokacje ladowania moga modyfikowac wylacznie zapisywalny segment .data/.bss
     * (lub w trakcie przygotowania fizycznych ramek przez loader przed publikacja PTE).
     * Dla zachowania niezmiennosci stron kodu wymuszamy, aby cel relokacji znajdowal sie w .data.
     */
    if (r.offset_w_obrazie < dane_start ||
        r.offset_w_obrazie > dane_koniec ||
        dane_koniec - r.offset_w_obrazie < szerokosc) {
        return BUR_V2_BLAD_RELOKACJA_ZAKRES;
    }

    if (r.addend < 0) {
        return BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE;
    }
    const uint64_t cel_rva = static_cast<uint64_t>(r.addend);
    if (!bur_v2_czy_rva_w_obrazie(n, cel_rva)) {
        return BUR_V2_BLAD_RELOKACJA_ZAKRES;
    }

    uint64_t docelowy_va = 0;
    if (__builtin_add_overflow(baza_obrazu, cel_rva, &docelowy_va) ||
        docelowy_va >= 0x0000800000000000ULL) {
        return BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE;
    }

    uint64_t miejsce_va = 0;
    if (__builtin_add_overflow(baza_obrazu, r.offset_w_obrazie, &miejsce_va)) {
        return BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE;
    }

    *out_szerokosc_bajtow = szerokosc;

    if (r.typ == BUR_V2_RELOC_ABS64 || r.typ == BUR_V2_RELOC_SIEC_DANE64) {
        *out_wartosc = docelowy_va;
        return BUR_V2_OK;
    }

    if (r.typ == BUR_V2_RELOC_PC32) {
        const int64_t disp = static_cast<int64_t>(docelowy_va) - static_cast<int64_t>(miejsce_va);
        if (disp < -2147483648LL || disp > 2147483647LL) {
            return BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE;
        }
        *out_wartosc = static_cast<uint32_t>(static_cast<int32_t>(disp));
        return BUR_V2_OK;
    }

    if (r.typ == BUR_V2_RELOC_ABS32) {
        if (docelowy_va > 0xFFFFFFFFULL) {
            return BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE;
        }
        *out_wartosc = static_cast<uint32_t>(docelowy_va);
        return BUR_V2_OK;
    }

    if (r.typ == BUR_V2_RELOC_ABS32S) {
        const int64_t s = static_cast<int64_t>(docelowy_va);
        if (s < -2147483648LL || s > 2147483647LL) {
            return BUR_V2_BLAD_RELOKACJA_PRZEPELNIENIE;
        }
        *out_wartosc = static_cast<uint32_t>(static_cast<int32_t>(s));
        return BUR_V2_OK;
    }

    return BUR_V2_BLAD_RELOKACJA_TYP;
}

inline bool bur_v2_zastosuj_relokacje_danych(
    const NaglowekBurV2& n,
    const BurV2Relokacja* relokacje,
    uint8_t* bufor_danych_pamieci,
    uint64_t baza_obrazu
) noexcept {
    if (n.reloc_liczba == 0) return true;
    if (!relokacje || !bufor_danych_pamieci) return false;

    const uint64_t dane_start = n.dane_rva;
    for (uint32_t i = 0; i < n.reloc_liczba; ++i) {
        const BurV2Relokacja& r = relokacje[i];
        uint64_t wartosc = 0;
        size_t szerokosc = 0;
        if (bur_v2_oblicz_wartosc_relokacji(n, r, baza_obrazu, &wartosc, &szerokosc) != BUR_V2_OK) {
            return false;
        }
        if (r.offset_w_obrazie < dane_start) return false;
        const uint64_t off_w_danych64 = r.offset_w_obrazie - dane_start;
        if (szerokosc == 0U ||
            off_w_danych64 > n.dane_rozmiar_pamieci ||
            szerokosc > n.dane_rozmiar_pamieci - off_w_danych64) {
            return false;
        }
        const size_t off_w_danych = static_cast<size_t>(off_w_danych64);
        if (szerokosc == 8U) {
            uint64_t* cel64 = reinterpret_cast<uint64_t*>(bufor_danych_pamieci + off_w_danych);
            *cel64 = wartosc;
        } else if (szerokosc == 4U) {
            uint32_t* cel32 = reinterpret_cast<uint32_t*>(bufor_danych_pamieci + off_w_danych);
            *cel32 = static_cast<uint32_t>(wartosc);
        } else {
            return false;
        }
    }
    return true;
}

/* Production file model: static PIE with sorted, non-overlapping RELATIVE64
 * writes into DATA only. Other prototype relocation enums are reserved and
 * are not accepted by the complete-file loader. No bytes are modified here. */
inline BurV2KodBledu bur_v2_waliduj_plik(const uint8_t* file, uint64_t size,
    uint64_t base) noexcept {
    if (!file || size < sizeof(NaglowekBurV2)) return BUR_V2_BLAD_ZA_MALY_PLIK;
    NaglowekBurV2 n{};
    for (size_t i=0;i<sizeof(n);++i) reinterpret_cast<uint8_t*>(&n)[i]=file[i];
    auto err=bur_v2_waliduj_naglowek_kod(n,size);
    if (err != BUR_V2_OK) return err;
    uint64_t end=0;
    for (uint32_t i=0;i<n.reloc_liczba;++i) {
        BurV2Relokacja r{};
        const uint64_t off=n.reloc_przesuniecie+uint64_t(i)*sizeof(r);
        for(size_t j=0;j<sizeof(r);++j) reinterpret_cast<uint8_t*>(&r)[j]=file[off+j];
        if(r.typ!=BUR_V2_RELOC_ABS64) return BUR_V2_BLAD_RELOKACJA_TYP;
        uint64_t value=0; size_t width=0;
        err=bur_v2_oblicz_wartosc_relokacji(n,r,base,&value,&width);
        if(err!=BUR_V2_OK) return err;
        if(i && r.offset_w_obrazie<end) return BUR_V2_BLAD_RELOKACJA_ZAKRES;
        end=r.offset_w_obrazie+width;
    }
    return BUR_V2_OK;
}
