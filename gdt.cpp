/*
 * Mechanizm: Globalna Tablica Deskryptorów (GDT) dla 64-bit
 * Opis: Ustawia segmenty jądra oraz przestrzeni użytkownika dla trybu Long Mode.
 * Mimo iż stronicowanie przejmuje główną kontrolę, segmentacja w Ring 0 i 3 jest 
 * wymagana konwencyjnie.
 */

#include <stdint.h>

// Flagi konfiguracyjne określające właściwości segmentu w architekturze x86_64
#define DOSTEP_KOD_JADRA   0x9A // 1001 1010 - Present, Ring 0, Kod, Wykonywalny/Odczyt
#define DOSTEP_DANE_JADRA  0x92 // 1001 0010 - Present, Ring 0, Dane, Odczyt/Zapis
#define DOSTEP_KOD_USER    0xFA // 1111 1010 - Present, Ring 3, Kod, Wykonywalny/Odczyt
#define DOSTEP_DANE_USER   0xF2 // 1111 0010 - Present, Ring 3, Dane, Odczyt/Zapis

#define FLAGA_64_BIT       0x20 // 0010 0000 - Bit Long Mode (L) dla kodu x86_64
#define FLAGA_DOMYSLNA     0x00 // Puste flagi wymiarowe dla sekcji danych

// Pojedynczy wpis (deskryptor) GDT w architekturze 64-bit ma długość 8 bajtów (zgodność wsteczna)
struct DeskryptorGDT {
    uint16_t limit_dolny;       // Pierwsze 16 bitów z limitu segmentu
    uint16_t baza_dolna;        // Pierwsze 16 bitów bazowego adresu
    uint8_t  baza_srodkowa;     // Następne 8 bitów bazy
    uint8_t  bajt_dostepu;      // Flagi uprawnień oraz rodzaj segmentu
    uint8_t  granulacja_flagi;  // Górne 4 bity limitu oraz 4 bity flag (np. 64-bit)
    uint8_t  baza_gorna;        // Ostatnie 8 bitów bazy (dla deskryptorów systemowych TSS jest to inaczej)
} __attribute__((packed));

// Rejestr GDT (GDTR) używany przez procesor przy wywołaniu specjalnej instrukcji LGDT (10 bajtów)
struct RejestrGDT {
    uint16_t rozmiar;           // Pełny rozmiar tablicy minus 1 bajt
    uint64_t adres;             // Liniowy adres w pamięci, gdzie spoczywa tablica (64-bit)
} __attribute__((packed));

// Stworzenie tablicy z 5 wpisami:
// 0: Null, 1: Kod Jądra, 2: Dane Jądra, 3: Kod Użytkownika, 4: Dane Użytkownika
struct DeskryptorGDT nasza_tablica_gdt[5];
struct RejestrGDT wskaznik_gdtr;

// Zewnętrzna procedura w Asemblerze ładująca struktury GDT prosto do szyny i zmieniająca CS
extern "C" void zaladuj_zaktualizowane_gdt(uint64_t adres_gdtr);

// Pomocnicza funkcja do kompozycji pojedynczego bloku pamięci GDT 
void UstawWpisGDT(int indeks, uint32_t baza, uint32_t limit, uint8_t dostep, uint8_t flagi) {
    // Rozkład bitowy bazy (Ignorowany w 64-bit dla zwykłych segmentów, musi być 0)
    nasza_tablica_gdt[indeks].baza_dolna    = (baza & 0xFFFF);
    nasza_tablica_gdt[indeks].baza_srodkowa = (baza >> 16) & 0xFF;
    nasza_tablica_gdt[indeks].baza_gorna    = (baza >> 24) & 0xFF;

    // Rozkład bitowy limitu (Ignorowany, 0)
    nasza_tablica_gdt[indeks].limit_dolny   = (limit & 0xFFFF);

    // Połączenie wyższego nibble limitu z flagami (Takimi jak Long Mode flag i Granulacja)
    nasza_tablica_gdt[indeks].granulacja_flagi = (limit >> 16) & 0x0F;
    nasza_tablica_gdt[indeks].granulacja_flagi |= (flagi & 0xF0);
    
    // Uprawnienia Ring i Typ (Kod/Dane)
    nasza_tablica_gdt[indeks].bajt_dostepu  = dostep;
}

// Główna funkcja wywoływana z poziomu jądra w celu zbudowania architektury ochrony pamięci
extern "C" void InicjalizujGDT() {
    wskaznik_gdtr.rozmiar = (sizeof(struct DeskryptorGDT) * 5) - 1;
    wskaznik_gdtr.adres   = (uint64_t)&nasza_tablica_gdt;

    // Definiowanie wymaganej płaskiej struktury segmentacji:
    UstawWpisGDT(0, 0, 0, 0, 0);                               // Segment zerowy (Null)
    UstawWpisGDT(1, 0, 0, DOSTEP_KOD_JADRA, FLAGA_64_BIT);     // Segment Kodu Jądra (Selektor = 0x08)
    UstawWpisGDT(2, 0, 0, DOSTEP_DANE_JADRA, FLAGA_DOMYSLNA);  // Segment Danych Jądra (Selektor = 0x10)
    UstawWpisGDT(3, 0, 0, DOSTEP_KOD_USER, FLAGA_64_BIT);      // Segment Kodu Użytkownika Ring 3 (0x1B)
    UstawWpisGDT(4, 0, 0, DOSTEP_DANE_USER, FLAGA_DOMYSLNA);   // Segment Danych Użytkownika Ring 3 (0x23)

    // Załaduj wskaźnik GDTR
    zaladuj_zaktualizowane_gdt((uint64_t)&wskaznik_gdtr);
}
