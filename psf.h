/*
 * Mechanizm: Polski System Plików (PSF) - Definicje Struktur i API
 * Opis: Hierarchiczny system plików operujący na blokach 512 bajtowych,
 * wyposażony we wskaźniki katalogów w celu obsługi wielowarstwowych ścieżek
 * typu "/katalog/podkatalog/plik.txt". Konwencja publicznego API i struktur
 * dostosowana do standardu snake_case.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h> 

#define PSF_ROZMIAR_BLOKU 512
#define PSF_MAX_NAZWA 28 
#define PSF_MAX_BLOKOW_W_WEZLE 64 // <-- ZWIĘKSZONO! Pozwala na pliki do 32 KB

// --- DEFINICJE TYPÓW WĘZŁÓW ---
#define TYP_WOLNY    0
#define TYP_PLIK     1
#define TYP_KATALOG  2

// Struktura opisująca korzeń i układ pamięci RAM dysku
struct superblok {
    char sygnatura[4];          // Zawsze "PSF1"
    uint32_t calkowity_rozmiar; // Pojemność całkowita dysku RAM w bajtach
    uint32_t ilosc_blokow;      // Ile bloków mieści dysk RAM
    uint32_t id_korzenia;       // Numer węzła startowego (katalog "/"), zazwyczaj 1.
    uint32_t start_wezlow;      // Od którego bloku fizycznie na dysku zaczynają się struktury "wezel_indeksowy"
    uint32_t start_danych;      // Od którego bloku fizycznie zaczynają się dane surowe
} __attribute__((packed));

// Węzeł Indeksowy (Inode) - Zwiększono do 512 bajtów (1 Pełny Blok)
struct wezel_indeksowy {
    uint8_t typ;                                 
    uint32_t rozmiar_w_bajtach;                  
    uint32_t wskazniki_blokow[PSF_MAX_BLOKOW_W_WEZLE]; 
    
    // Wypychacz struktury do równego rozmiaru 512 bajtów 
    // (1 bajt typ + 4 bajty rozmiar + 256 bajtów wskazniki = 261. Zostaje 251 bajtów)
    uint8_t zarezerwowane[251]; 
} __attribute__((packed));

struct wpis_katalogowy {
    uint32_t id_wezla;                 
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