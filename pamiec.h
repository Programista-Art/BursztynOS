/*
 * Wspólne nagłówki i definicje dla struktury Multiboot2 oraz warstwy zarządzania pamięcią.
 * Język: C++ (z polskim API)
 */

#pragma once

#include <stdint.h>

// Symboliczne adresy początku i końca z linkera
// UWAGA: Aby uzyskać ich fizyczny adres w kodzie C++, należy użyć operatora '&', np. &__kernel_start
extern "C" uint64_t __kernel_start;
extern "C" uint64_t __kernel_end;

// Rozmiar strony / ramki fizycznej to 4 KB w architekturze x86_64
#define ROZMIAR_RAMKI 4096

// --- STRUKTURY MULTIBOOT 2 ---

#define MULTIBOOT_TAG_TYPE_MEMORY_MAP 6

// Podstawowy element z którego składa się każda informacja Multiboot2
struct WpisTaguMB2 {
    uint32_t typ;
    uint32_t rozmiar;
};

// Mapa podziału RAM dostarczona przez BIOS / UEFI przekazywana przez GRUB
struct WpisMapyPamieciMB2 {
    uint64_t adres_bazowy;
    uint64_t dlugosc;
    uint32_t typ_obszaru; // 1 = Wolny RAM do użytku, reszta = Zarezerwowane / ACPI / Hardware
    uint32_t zarezerwowane;
} __attribute__((packed));

struct TagMapyPamieciMB2 {
    uint32_t typ; // Musi wynosić 6
    uint32_t rozmiar;
    uint32_t rozmiar_wpisu;
    uint32_t wersja_wpisu;
    WpisMapyPamieciMB2 wpisy[0]; // Tablica elastyczna
} __attribute__((packed));

// --- API PAMIĘCI FIZYCZNEJ (PMM) ---

// Zablokuj ramkę by nikt nie mógł jej nadpisać (np. Kod Jądra)
void ZabezpieczRamke(uint64_t adres_fizyczny);

// Odblokuj by VMM mógł na niej zbudować tablice
void OdblokujRamke(uint64_t adres_fizyczny);

// Znajdź i zwróć pierwszą wolną ramkę
void* ZaalokujRamke();

// Oddaj przestarzałą do puli
void ZwolnijRamke(void* adres_fizyczny);

// Funkcja rozruchowa skanująca pamięć MB2
void InicjalizujPMM(uint64_t adres_info_multiboot);

// --- API PAMIĘCI WIRTUALNEJ (VMM) ---

// Mapowanie konkretnego wirtualnego offsetu w 4-stopniowym drzewie PT
void ZmapujStrone(void* adres_wirtualny, void* adres_fizyczny, uint32_t flagi);

// Główna funkcja przebudowująca identity paging z Asemblera na dedykowane C++
void InicjalizujVMM();

// --- STRUKTURY GRAFIKI (FRAMEBUFFER) ---
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8

struct TagFramebufferMB2 {
    uint32_t typ;
    uint32_t rozmiar;
    uint64_t adres_fizyczny;
    uint32_t pitch;     // Ilość bajtów na jeden pełny wiersz pikseli
    uint32_t szerokosc; // Rozdzielczość X
    uint32_t wysokosc;  // Rozdzielczość Y
    uint8_t  bpp;       // Bits per pixel (zazwyczaj 32)
    uint8_t  typ_bufora;
    uint16_t zarezerwowane; // POPRAWKA: Specyfikacja MB2 wymaga tu dokładnie 2 bajtów (uint16_t)
} __attribute__((packed));