/*
 * Wspólne nagłówki i definicje dla struktury Multiboot2 oraz warstwy zarządzania pamięcią.
 * Język: C++ (z polskim API)
 */

#pragma once

#include <stdint.h>

extern "C" uint64_t __kernel_start;
extern "C" uint64_t __kernel_end;

#define ROZMIAR_RAMKI 4096

// --- STRUKTURY MULTIBOOT 2 ---

#define MULTIBOOT_TAG_TYPE_MEMORY_MAP 6
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8

// KRYTYCZNA ZMIANA: Zgodność struktury tagów ze standardem MB2 (Typ 16b, Flagi 16b)
struct WpisTaguMB2 {
    uint16_t typ;
    uint16_t flagi;
    uint32_t rozmiar;
};

struct WpisMapyPamieciMB2 {
    uint64_t adres_bazowy;
    uint64_t dlugosc;
    uint32_t typ_obszaru; 
    uint32_t zarezerwowane;
} __attribute__((packed));

struct TagMapyPamieciMB2 {
    uint16_t typ;
    uint16_t flagi;
    uint32_t rozmiar;
    uint32_t rozmiar_wpisu;
    uint32_t wersja_wpisu;
    WpisMapyPamieciMB2 wpisy[0]; 
} __attribute__((packed));

struct TagFramebufferMB2 {
    uint16_t typ;
    uint16_t flagi;
    uint32_t rozmiar;
    uint64_t adres_fizyczny;
    uint32_t pitch;     
    uint32_t szerokosc; 
    uint32_t wysokosc;  
    uint8_t  bpp;       
    uint8_t  typ_bufora;
    uint16_t zarezerwowane; 
} __attribute__((packed));

// --- API PAMIĘCI FIZYCZNEJ (PMM) ---
void ZabezpieczRamke(uint64_t adres_fizyczny);
void OdblokujRamke(uint64_t adres_fizyczny);
void* ZaalokujRamke();
void ZwolnijRamke(void* adres_fizyczny);
void InicjalizujPMM(uint64_t adres_info_multiboot);

// --- API PAMIĘCI WIRTUALNEJ (VMM) ---
void ZmapujStrone(void* adres_wirtualny, void* adres_fizyczny, uint32_t flagi);
void InicjalizujVMM();