/*
 * Mechanizm: Polski System Plików 64-bit (BSP64 / BursztynFS)
 * Opis: Architektura wzorowana na nowoczesnych rozwiązaniach (ext4).
 * Używa bloków 4KB, 64-bitowych wskaźników i wyrównania do Cache-Line.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h> 

// NOWOCZESNA ARCHITEKTURA 64-BITOWA
#define PSF_ROZMIAR_BLOKU 4096
#define PSF_MAX_NAZWA 56 // (8 bajtów ID + 56 bajtów nazwa = Równe 64 bajty na wpis!)
#define PSF_MAX_BLOKOW_W_WEZLE 490 // Pozwala na pliki do ~2 MB na samych wskaźnikach bezpośrednich

#define TYP_WOLNY    0
#define TYP_PLIK     1
#define TYP_KATALOG  2

// Znacznik pustego bloku/adresu
#define BARK_BLOKU 0xFFFFFFFFFFFFFFFFULL

// Nowoczesny 64-bitowy Superblok
struct superblok {
    char sygnatura[4];          // "BSP2" (Wersja 64-bit)
    uint64_t calkowity_rozmiar; // Pojemność dysku
    uint64_t ilosc_blokow;      // Ile bloków mieści dysk
    uint64_t id_korzenia;       // Numer węzła startowego "/"
    uint64_t start_wezlow;      // Start węzłów (Inodes)
    uint64_t start_danych;      // Start danych surowych
} __attribute__((packed));

// Węzeł Indeksowy (Inode) - Konstrukcja przystosowana pod ogromne dyski (Waga: 4096 bajtów)
struct wezel_indeksowy {
    uint8_t  typ;                                 
    uint64_t rozmiar_w_bajtach;
    uint64_t czas_utworzenia;  // Na przyszłość dla zegara RTC
    uint64_t flagi_zabezpieczen; // Na przyszłość dla praw dostępu

    // 1. Bloki bezpośrednie (Ext4 style)
    uint64_t wskazniki_blokow[PSF_MAX_BLOKOW_W_WEZLE]; 
    
    // 2. Architektura pod ogromne pliki (Bloki Pośrednie)
    uint64_t blok_posredni_1; // Prowadzi do bloku z 512 wskaźnikami (dodatkowe 2MB)
    uint64_t blok_posredni_2; // Prowadzi do bloku z blokami (dodatkowy 1GB)
    uint64_t blok_posredni_3; // Prowadzi do bloku z blokami bloków (dodatkowe 512GB)

    // Wypychacz struktury do równego rozmiaru 4096 bajtów (1 Pełny Blok)
    uint8_t zarezerwowane[127]; 
} __attribute__((packed));

// Wpis wewnątrz folderu (Równe 64 Bajty!)
struct wpis_katalogowy {
    uint64_t id_wezla;                 
    char nazwa[PSF_MAX_NAZWA];         
} __attribute__((packed));

#ifdef __cplusplus
extern "C" {
#endif

    void inicjalizuj_psf(void* adres_ram_dysku, uint32_t rozmiar_w_bajtach);
    bool utworz_katalog(const char* sciezka);
    bool utworz_plik(const char* sciezka);
    bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc);
    bool czytaj_z_pliku(const char* sciezka, char* bufor, uint32_t max_dlugosc);
    uint8_t* bsp_wczytaj_plik_do_pamieci(const char* sciezka, uint64_t* rozmiar_wyj);
    bool wylistuj_katalog(const char* sciezka, char* bufor, uint32_t max_dlugosc);

#ifdef __cplusplus
}
#endif