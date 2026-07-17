#include <stdint.h>

// Struktura pojedynczego wpisu GDT (8 bajtów)
struct DeskryptorGDT {
    uint16_t limit_dolny;
    uint16_t baza_dolna;
    uint8_t  baza_srodkowa;
    uint8_t  dostep;
    uint8_t  granicznik_flagi;
    uint8_t  baza_gorna;
} __attribute__((packed));

struct RejestrGDT {
    uint16_t rozmiar;
    uint64_t adres;
} __attribute__((packed));

// POTRZEBUJEMY 7 WPISÓW! (Null, KernCode, KernData, UserData, UserCode, TSS_Dolny, TSS_Gorny)
struct DeskryptorGDT tablica_gdt[7];
struct RejestrGDT wskaznik_gdtr;

extern "C" void zaladuj_zaktualizowane_gdt(uint64_t adres_gdtr);

// Importujemy globalny stan TSS utworzony i zadeklarowany w pliku tss.cpp
extern uint8_t globalny_tss; 

void UstawWpisGDT(int numer, uint64_t baza, uint32_t limit, uint8_t dostep, uint8_t flagi) {
    tablica_gdt[numer].baza_dolna = (baza & 0xFFFF);
    tablica_gdt[numer].baza_srodkowa = (baza >> 16) & 0xFF;
    tablica_gdt[numer].baza_gorna = (baza >> 24) & 0xFF;

    tablica_gdt[numer].limit_dolny = (limit & 0xFFFF);
    tablica_gdt[numer].granicznik_flagi = ((limit >> 16) & 0x0F) | (flagi & 0xF0);

    tablica_gdt[numer].dostep = dostep;
}

extern "C" void InicjalizujGDT() {
    // 0. Null Descriptor (Wymóg sprzętowy x86)
    UstawWpisGDT(0, 0, 0, 0, 0);
    
    // 1. Kernel Code (Kod Jądra Ring 0) - Offset 0x08
    UstawWpisGDT(1, 0, 0xFFFFF, 0x9A, 0xAF);
    
    // 2. Kernel Data (Dane Jądra Ring 0) - Offset 0x10
    UstawWpisGDT(2, 0, 0xFFFFF, 0x92, 0xAF);
    
    // 3. User Data (Dane Aplikacji Ring 3) - Offset 0x18
    // KRYTYCZNE: Instrukcja SYSRET wymaga by Dane Użytkownika leżały w GDT PRZED Kodem Użytkownika!
    UstawWpisGDT(3, 0, 0xFFFFF, 0xF2, 0xAF); // 0xF2 = (DPL 3) Zezwolenie na Ring 3
    
    // 4. User Code (Kod Aplikacji Ring 3) - Offset 0x20
    UstawWpisGDT(4, 0, 0xFFFFF, 0xFA, 0xAF); // 0xFA = (DPL 3) Zezwolenie na Ring 3

    // 5 i 6. Task State Segment (TSS) - Specjalny podwójny wpis 16-bajtowy dla 64-bitów! - Offset 0x28
    uint64_t baza_tss = (uint64_t)&globalny_tss;
    uint32_t limit_tss = 104 - 1; // Struktura tss_wpis ma dokładnie 104 bajty (0x68)
    
    // Część dolna (Wpis nr 5)
    UstawWpisGDT(5, baza_tss, limit_tss, 0x89, 0x00); // 0x89 = Typ Systemowy TSS (64-bit)
    
    // Część górna (Wpis nr 6) - Kontynuacja wskaźnika bazy 64-bitowego
    UstawWpisGDT(6, 0, 0, 0, 0); // Zerujemy podstawy
    tablica_gdt[6].limit_dolny = (baza_tss >> 32) & 0xFFFF;
    tablica_gdt[6].baza_dolna = (baza_tss >> 48) & 0xFFFF;

    // Przekazanie nowego rozmiaru tabeli do rejestru GDTR
    wskaznik_gdtr.rozmiar = (sizeof(struct DeskryptorGDT) * 7) - 1;
    wskaznik_gdtr.adres = (uint64_t)&tablica_gdt;

    zaladuj_zaktualizowane_gdt((uint64_t)&wskaznik_gdtr);
}