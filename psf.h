/*
 * Mechanizm: Polski System Plików (PSF) - Definicje Struktur i API
 * Opis: Hierarchiczny system plików operujący na blokach 512 bajtowych,
 * wyposażony we wskaźniki katalogów w celu obsługi wielowarstwowych ścieżek
 * typu "/katalog/podkatalog/plik.txt". Konwencja publicznego API i struktur
 * dostosowana do standardu snake_case.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h> // Zabezpieczenie dla typu bool w przypadku kompilacji C/C++

#define PSF_ROZMIAR_BLOKU 512
#define PSF_MAX_NAZWA 28 // 28 znaków nazwy + 4 bajty ID = 32 bajty na wpis
#define PSF_MAX_BLOKOW_W_WEZLE 10 // Uproszczenie, na razie tylko bezpośrednie wskaźniki na bloki

// Deklaracje flag dla identyfikacji węzła
#define TYP_WOLNY 0
#define TYP_PLIK 1
#define TYP_KATALOG 2

// Struktura opisująca korzeń i układ pamięci RAM dysku
struct superblok {
    char sygnatura[4];          // Zawsze "PSF1"
    uint32_t calkowity_rozmiar; // Pojemność całkowita dysku RAM w bajtach
    uint32_t ilosc_blokow;      // Ile bloków mieści dysk RAM
    uint32_t id_korzenia;       // Numer węzła startowego (katalog "/"), zazwyczaj 1.
    uint32_t start_wezlow;      // Od którego bloku fizycznie na dysku zaczynają się struktury "wezel_indeksowy"
    uint32_t start_danych;      // Od którego bloku fizycznie zaczynają się dane surowe
} __attribute__((packed));

// Węzeł Indeksowy (tzw. Inode). Reprezentuje jeden twór na dysku (Plik lub Katalog)
struct wezel_indeksowy {
    uint8_t typ;                                 // TYP_WOLNY, TYP_PLIK, lub TYP_KATALOG
    uint32_t rozmiar_w_bajtach;                  // Aktualny rozmiar zarezerwowanych danych pliku (lub ilość wpisów katalogowych)
    uint32_t wskazniki_blokow[PSF_MAX_BLOKOW_W_WEZLE]; // Bezpośrednie identyfikatory bloków surowych przydzielonych dla tego tworu
    
    // Wypełniacz do równych 64 bajtów by na jednym bloku (512b) zmieściło się ich równo 8.
    // 1 (typ) + 4 (rozmiar) + 40 (wskazniki 10x4) = 45 bajtow. Zostaje 19.
    uint8_t zarezerwowane[19]; 
} __attribute__((packed));

// Pojedynczy wpis leżący na bloku przypisanym węzłowi typu KATALOG
// Jeśli wezel o typie KATALOG wskazuje na blok danych np. nr 5, to na bloku nr 5
// ułożona jest tablica tych wpisów (512 / 32 = 16 Wpisów w jednym bloku)
struct wpis_katalogowy {
    uint32_t id_wezla;                 // Odnośnik - 0 oznacza pusty wpis, inne to cel do `wezel_indeksowy`
    char nazwa[PSF_MAX_NAZWA];         // C-String z nazwą (np. "plik.txt" lub "System")
} __attribute__((packed));

// --- API Publiczne Jądra (Konwencja snake_case) ---

#ifdef __cplusplus
extern "C" {
#endif

    void inicjalizuj_psf(void* adres_ram_dysku, uint32_t rozmiar_w_bajtach);
    bool utworz_katalog(const char* sciezka);
    bool utworz_plik(const char* sciezka);
    bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc);
    bool czytaj_z_pliku(const char* sciezka, char* bufor, uint32_t max_dlugosc);
    
    // Funkcja dodana w celu spięcia Systemu Plików z Loaderem programów (.bur)
    uint8_t* bsp_wczytaj_plik_do_pamieci(const char* sciezka, uint64_t* rozmiar_wyj);

#ifdef __cplusplus
}
#endif