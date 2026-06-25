#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>

// Flagi stronicowania x86-64 rozszerzone o dostęp dla Ring 3
#define FLAGA_OBECNA  (1 << 0) // Bit 0: Strona znajduje się w pamięci RAM
#define FLAGA_ZAPIS   (1 << 1) // Bit 1: Zezwolenie na zapis do strony
#define FLAGA_USER    (1 << 2) // Bit 2: KLUCZOWA FLAGA! Zezwala Ring 3 na dostęp do tej strony

// Specyfikacja nagłówka binarnego Bursztynowego Programu Wykonywalnego (.bur)
struct NaglowekBur {
    uint8_t  magia[4];            // Sygnatura identyfikacyjna "BUR\0" (0x42, 0x55, 0x52, 0x00)
    uint64_t punkt_wejscia;       // Adres wirtualny w Ring 3, gdzie zaczyna się kod aplikacji
    
    uint64_t tekst_przesuniecie;  // Gdzie w pliku zaczyna się sekcja .tekst
    uint64_t tekst_rozmiar;       // Rozmiar sekcji .tekst w bajtach
    uint64_t tekst_wirtualny;     // Docelowy adres wirtualny aplikacji dla sekcji .tekst
    
    uint64_t dane_przesuniecie;   // Gdzie w pliku zaczyna się sekcja .dane
    uint64_t dane_rozmiar;        // Rozmiar sekcji .dane w bajtach
    uint64_t dane_wirtualny;      // Docelowy adres wirtualny aplikacji dla sekcji .dane
} __attribute__((packed));

// Struktura opisująca instancję procesu w pamięci Jądra Bursztyna
typedef struct proces {
    uint64_t pid;                 // Unikalny identyfikator procesu
    uint8_t  poziom_zaufania;     // Wartość logiczna BZL (0-5)
    uint64_t uprawnienia;         // Bitowa mapa przyznanych praw aplikacyjnych
    void* przestrzen_adresowa; // Wskaźnik na tablicę stron PML4 procesu (VMM)
} proces_t;

// Publiczne funkcje modułu Loadera
extern "C" bool bws_uruchom_program_z_pliku(const char* sciezka_pliku, uint8_t bzl_poziom, uint64_t flagi_praw);

#endif // LOADER_H