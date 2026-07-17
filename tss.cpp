/*
 * Mechanizm: Task State Segment (TSS) dla 64-bit Long Mode
 * Opis: Pomimo spłaszczonego modelu pamięci, architektura x86_64 nadal
 * bezwzględnie wymaga zdefiniowania jednej struktury TSS w systemie. 
 * Jego głównym zadaniem jest dostarczenie procesorowi adresu stosu jądra (RSP0),
 * na który ma się on przełączyć w momencie sprzętowego wywołania wyjątku
 * z poziomu Ring 3 (np. przerwania zegarowe, sprzętowe błędy), aby nie nadpisać stosu użytkownika.
 */

#include <stdint.h>

/*
 * Struktura Task State Segment (TSS) ściśle według specyfikacji x86_64.
 * Rozmiar tej struktury to 104 bajty (0x68).
 */
struct tss_wpis {
    uint32_t zarezerwowane1; // Ignorowane w 64-bit
    
    // Wskaźniki na stosy dla operacji przejścia na wyższy uprzywilejowany ring
    uint64_t rsp0; // Tutaj trafia wskaźnik stosu jądra dla powrotu z Ring 3 -> Ring 0
    uint64_t rsp1; // Nie używamy w Bursztyn OS (Ring 1)
    uint64_t rsp2; // Nie używamy (Ring 2)
    
    uint64_t zarezerwowane2;
    
    // Interrupt Stack Table (IST). 7 stosów omijających mechanizm głównego stosu w wypadku 
    // krytycznych awarii (np. Double Fault). Tutaj ustawione wszystkie na zero (puste).
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    
    uint64_t zarezerwowane3;
    uint16_t zarezerwowane4;
    uint16_t iopb_offset; // Wskaźnik bitowej mapy portów I/O. Zazwyczaj wielkość całego TSS
} __attribute__((packed));

// Globalna instancja TSS, która będzie osadzona w naszym systemie
struct tss_wpis globalny_tss;

/*
 * Konfiguruje TSS ładując podany na wejściu adres szczytu stosu jądra (ring 0) 
 * tak, aby sprzęt wiedział jak dokonać zrzutu środowiska podczas zmiany pierścieni.
 */
extern "C" void inicjalizuj_tss(void* stos_jadra) {
    // Zabezpieczenie: Czyścimy całą strukturę zerami.
    // W środowiskach 'freestanding' lepiej nie ufać domyślnej inicjalizacji BSS.
    uint8_t* tss_ptr = (uint8_t*)&globalny_tss;
    for(uint32_t i = 0; i < sizeof(struct tss_wpis); i++) {
        tss_ptr[i] = 0;
    }

    globalny_tss.rsp0 = (uint64_t)stos_jadra;
    
    // Blokujemy mechanizm nadpisywania portów z Ring 3 (brak mapy wejścia/wyjścia)
    // Offset ustawiony na rozmiar całego obiektu oznacza brak mapy w ciele za strukturą.
    globalny_tss.iopb_offset = sizeof(struct tss_wpis);
}

/*
 * Zewnętrzna, wygodna funkcja dla jądra do załadowania Task Registera w procesorze
 */
extern "C" void zaladuj_tss(uint16_t selektor_tss) {
    asm volatile("ltr %0" : : "rm"(selektor_tss));
}

/*
 * === [INSTRUKCJA INTEGRACJI GDT DLA DEWELOPERA] ===
 * * Aby procesor uznał nową tablicę TSS, musisz dodać wpis do GDT i załadować
 * nowy wskaźnik (LTR - Load Task Register). W przeciwieństwie do standardowych 
 * wpisów segmentacji danych, TSS w Long Mode waży DWA RAZY tyle (16 bajtów), 
 * gdyż jego baza zajmuje pełne 64-bity.
 *
 * 1. Zmień rozmiar swojej GDT (np. nasza_tablica_gdt) by miała 7 wpisów (5 starych + 2 podwójnego złączenia dla TSS).
 *
 * 2. W Twoim pliku zarządzającym GDT (np. `gdt.cpp`) dodaj na końcu ładowanie 16-bajtowego deskryptora:
 * * extern "C" struct tss_wpis globalny_tss; // Deklaracja z tego pliku
 * uint64_t baza_tss = (uint64_t)&globalny_tss;
 * uint32_t limit_tss = sizeof(struct tss_wpis) - 1;
 * * // Wpis nr 5 (Pierwsze 8 bajtów):
 * UstawWpisGDT(5, baza_tss, limit_tss, 0x89, 0x00); // 0x89 (Present, Ring 0, Systemowy typ = 64-bit TSS)
 * * // Wpis nr 6 (Kolejne 8 bajtów, po prostu góra wskaźnika przeniesiona niżej):
 * // Wymaga ręcznej interpolacji, jeśli twoja UstawWpisGDT przyjmuje 32-bit:
 * nasza_tablica_gdt[6].limit_dolny = (baza_tss >> 32) & 0xFFFF;
 * nasza_tablica_gdt[6].baza_dolna = (baza_tss >> 48) & 0xFFFF;
 * // Z resztą wyzerowaną dla wpisu nr 6!
 *
 * 3. Zwiększ wymiar swojego GDTR.rozmiar do `(sizeof(DeskryptorGDT) * 7) - 1`.
 * * 4. Po załadowaniu `lgdt` i przeładowaniu segmentów, po prostu wywołaj nową funkcję:
 * zaladuj_tss(0x28); // 5. wpis * 8 bitów = 0x28
 */