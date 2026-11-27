#pragma once
/*
 * Bursztyn OS - metadane natywnego wejscia jadra w pliku ELF (Faza R1B).
 *
 * Cel: przyszly BRB (R2) musi znalezc adres natywnego wejscia 64-bit (wejscie_jadra64) w OKROJONYM pliku jadra,
 * bez symboli, nazw sekcji i zewnetrznej tablicy symboli. e_entry pozostaje wejsciem Multiboot2 (_start, 32-bit).
 *
 * Mechanizm (deterministyczny, odporny na strip):
 *   - 64-bajtowa struktura tylko do odczytu, umieszczona przez jadro/arch/wejscie64.S w sekcji `.multiboot.metadane`,
 *     ktora linker.ld (KEEP(*(.multiboot.*))) sklada do wyjsciowej sekcji .boot, ZA naglowkiem Multiboot2,
 *   - .boot to pierwszy segment PT_LOAD (R, bez W i X), z danymi w pliku - strip usuwa symbole i sekcje
 *     niealokowane, nie zawartosc segmentow,
 *   - czytelnik szuka sygnatury wylacznie: w pierwszych 32768 B pliku (to samo okno co naglowek MB2), na
 *     przesunieciach wyrownanych do 8, wewnatrz danych z pliku segmentu PT_LOAD tylko do odczytu; dokladnie jedno
 *     trafienie - zero albo wiele = blad (brak / niejednoznacznosc),
 *   - czytelnik korzysta z ISTNIEJACEGO, zwalidowanego planu ELF BRB (rozruch/brb/wspolne/elf64.h); nie ma drugiego
 *     parsera ELF (rozruch/brb/wspolne/metadane_jadra.cpp).
 *
 * To NIE jest publiczny protokol BRBI (Faza 7): opisuje wylacznie, gdzie jest wejscie i jakiego kontraktu
 * przekazania oczekuje (kontrakt_wejscia = 1: R1B, RDI = MAGIC_WEJSCIA_NATYWNEGO, RSI = PA KontraktV1).
 *
 * Wartosci wyprowadzalne z naglowkow programu NIE sa duplikowane, z jednym swiadomym wyjatkiem: baza_wirtualna
 * (= najnizszy p_vaddr) - czytelnik MUSI sprawdzic zgodnosc z planem, co wykrywa podmiane/niezgodnosc metadanych.
 */

#include <stdint.h>
#include <stddef.h>
#include "metadane_jadra_asm.h"

namespace MetadaneJadra {

inline constexpr uint64_t SYGNATURA = 0x4F5244414A425242ULL;          // "BRBJADRO" (LE)
inline constexpr uint64_t SYGNATURA_KONCOWA = ~SYGNATURA;
inline constexpr uint16_t WERSJA = 1;
inline constexpr uint16_t ROZMIAR = 64;
inline constexpr uint32_t KONTRAKT_WEJSCIA_R1B = 1;                    // RDI = magic, RSI = PA KontraktV1
inline constexpr uint64_t MAGIC_WEJSCIA_NATYWNEGO = 0x4B41524A31425242ULL;  // "BRB1JRAK" (LE) w RDI
inline constexpr uint32_t OKNO_WYSZUKIWANIA = 32768;
inline constexpr uint64_t WYROWNANIE_PODKLADU = 0x1000;               // minimalne wyrownanie bazy fizycznej

/* Wymagane cechy procesora (bity pola wymagane_cechy). */
enum Cecha : uint32_t {
    CECHA_LONG_MODE = 1U << 0,     // CPUID 80000001h EDX[29]
    CECHA_PAE = 1U << 1,           // CPUID 1 EDX[6]
    CECHA_STRONICOWANIE_4P = 1U << 2,  // CR4.LA57 = 0 (stronicowanie 4-poziomowe)
    CECHA_NX_JESLI_DOSTEPNE = 1U << 3, // EFER.NXE = 1, gdy CPUID 80000001h EDX[20] = 1
};
inline constexpr uint32_t MASKA_CECH = CECHA_LONG_MODE | CECHA_PAE | CECHA_STRONICOWANIE_4P | CECHA_NX_JESLI_DOSTEPNE;
inline constexpr uint32_t CECHY_R1B = MASKA_CECH;

/* Uklad w pliku (little endian). Asercje przesuniec ponizej; zrodlo prawdy dla wejscie64.S. */
struct Uklad {
    uint64_t sygnatura;          // 0
    uint16_t wersja;             // 8
    uint16_t rozmiar;            // 10
    uint32_t kontrakt_wejscia;   // 12
    uint64_t baza_wirtualna;     // 16  = najnizszy p_vaddr (kontrola zgodnosci)
    uint64_t wejscie64;          // 24  VA natywnego wejscia (segment PF_X, dane z pliku)
    uint64_t wyrownanie_podkladu;// 32  potega dwojki >= 4096
    uint32_t wymagane_cechy;     // 40
    uint32_t flagi;              // 44  zarezerwowane = 0
    uint64_t zarezerwowane;      // 48  = 0
    uint64_t sygnatura_koncowa;  // 56  = ~sygnatura
};
static_assert(sizeof(Uklad) == 64, "Metadane jadra musza miec 64 bajty");
static_assert(offsetof(Uklad, wersja) == 8 && offsetof(Uklad, kontrakt_wejscia) == 12 &&
              offsetof(Uklad, baza_wirtualna) == 16 && offsetof(Uklad, wejscie64) == 24 &&
              offsetof(Uklad, wyrownanie_podkladu) == 32 && offsetof(Uklad, wymagane_cechy) == 40 &&
              offsetof(Uklad, flagi) == 44 && offsetof(Uklad, zarezerwowane) == 48 &&
              offsetof(Uklad, sygnatura_koncowa) == 56, "Nieprawidlowy uklad metadanych jadra");

static_assert(SYGNATURA == METADANE_SYGNATURA && SYGNATURA_KONCOWA == METADANE_SYGNATURA_KONCOWA &&
              WERSJA == METADANE_WERSJA && ROZMIAR == METADANE_ROZMIAR &&
              KONTRAKT_WEJSCIA_R1B == METADANE_KONTRAKT_R1B && WYROWNANIE_PODKLADU == METADANE_WYROWNANIE &&
              CECHY_R1B == METADANE_CECHY, "metadane_jadra_asm.h niezgodny z metadane_jadra.h");

} // namespace MetadaneJadra
