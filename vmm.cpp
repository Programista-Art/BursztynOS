/*
 * Mechanizm: Zarządzanie Pamięcią Wirtualną (VMM - Virtual Memory Manager)
 * Opis: Hierarchiczna implementacja struktury tablic strony dla przestrzeni Ring 0 i 3.
 * Pobiera ramki operacyjne z nowo powstałego mechanizmu PMM.
 */

#include "pamiec.h"
#include <stdint.h>

// Makra stałych decydujących o charakterystyce mapowanego przydziału
#define FLAGA_OBECNA      0b00000001  // Bit 0 = P
#define FLAGA_ZAPIS       0b00000010  // Bit 1 = RW (Zezwolenie na rzut z zapisem)
#define FLAGA_UZYTKOWNIKA 0b00000100  // Bit 2 = US (Zezwolenie na przydział dostępny dla Ring 3)

#define MASKA_INDEKSU 0x1FF
#define MASKA_ADRESU_FIZYCZNEGO 0x000FFFFFFFFFF000ULL 

// Bazowy, najwyższy punkt drzewa nowej wirtualnej instalacji
uint64_t* globalne_pml4 = nullptr;

void WyzerujStrone(void* wskaznik) {
    uint64_t* wsk = (uint64_t*)wskaznik;
    for (int i = 0; i < 512; i++) {
        wsk[i] = 0;
    }
}

void ZmapujStrone(void* adres_wirtualny, void* adres_fizyczny, uint32_t flagi) {
    if (!globalne_pml4) return;

    uint64_t wir = (uint64_t)adres_wirtualny;
    uint64_t fiz = (uint64_t)adres_fizyczny;

    uint64_t wew_pml4 = (wir >> 39) & MASKA_INDEKSU;
    uint64_t wew_pdp  = (wir >> 30) & MASKA_INDEKSU;
    uint64_t wew_pd   = (wir >> 21) & MASKA_INDEKSU;
    uint64_t wew_pt   = (wir >> 12) & MASKA_INDEKSU;

    if (!(globalne_pml4[wew_pml4] & FLAGA_OBECNA)) {
        uint64_t* nowa_pdp = (uint64_t*)ZaalokujRamke();
        if (!nowa_pdp) return; 
        WyzerujStrone(nowa_pdp);
        globalne_pml4[wew_pml4] = ((uint64_t)nowa_pdp) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;
    }
    uint64_t* w_pdp = (uint64_t*)(globalne_pml4[wew_pml4] & MASKA_ADRESU_FIZYCZNEGO);

    if (!(w_pdp[wew_pdp] & FLAGA_OBECNA)) {
        uint64_t* nowa_pd = (uint64_t*)ZaalokujRamke();
        if (!nowa_pd) return; 
        WyzerujStrone(nowa_pd);
        w_pdp[wew_pdp] = ((uint64_t)nowa_pd) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;
    }
    uint64_t* w_pd = (uint64_t*)(w_pdp[wew_pdp] & MASKA_ADRESU_FIZYCZNEGO);

    if (!(w_pd[wew_pd] & FLAGA_OBECNA)) {
        uint64_t* nowa_pt = (uint64_t*)ZaalokujRamke();
        if (!nowa_pt) return; 
        WyzerujStrone(nowa_pt);
        w_pd[wew_pd] = ((uint64_t)nowa_pt) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;
    }
    uint64_t* w_pt = (uint64_t*)(w_pd[wew_pd] & MASKA_ADRESU_FIZYCZNEGO);

    w_pt[wew_pt] = (fiz & MASKA_ADRESU_FIZYCZNEGO) | flagi;

    asm volatile("invlpg (%0)" : : "r" (adres_wirtualny) : "memory");
}

void InicjalizujVMM() {
    globalne_pml4 = (uint64_t*)ZaalokujRamke();
    if (!globalne_pml4) return;
    WyzerujStrone(globalne_pml4);

    // --- ROZWIAZANIE PROBLEMU KURY I JAJKA (DWUFAZOWE MAPOWANIE) ---
    
    // FAZA 1: Mapujemy tylko 16 MB.
    // Dlaczego? boot.S dał nam tylko 2 MB widocznej pamięci. Wygenerowanie 
    // tablic dla 16 MB zajmie zaledwie kilka ramek, które PMM bezpiecznie
    // znajdzie poniżej ograniczenia 2 MB.
    for (uint64_t i = 0; i < 4096; i++) {
        void* ptr = (void*)(i * ROZMIAR_RAMKI);
        ZmapujStrone(ptr, ptr, FLAGA_OBECNA | FLAGA_ZAPIS);
    }

    ZmapujStrone((void*)0xFEE00000ULL, (void*)0xFEE00000ULL, FLAGA_OBECNA | FLAGA_ZAPIS | 0x10 | 0x08); 
    ZmapujStrone((void*)0xFEC00000ULL, (void*)0xFEC00000ULL, FLAGA_OBECNA | FLAGA_ZAPIS | 0x10 | 0x08); 

    // ZMIANA KONTEKSTU: Przełączamy procesor na nowe tablice!
    // Od tej sekundy Jądro sprzętowo widzi i ma dostęp do 16 MB pamięci RAM.
    uint64_t adres_bazy = (uint64_t)globalne_pml4;
    asm volatile("mov %0, %%cr3" : : "r"(adres_bazy) : "memory");

    // FAZA 2: Bezpieczne rozszerzenie pamięci do 256 MB.
    // Skoro widzimy już wolny RAM aż do 16 MB, system może bez problemu 
    // pobierać ramki np. z 6. czy 10. Megabajta, a funkcja WyzerujStrone()
    // nie spowoduje już błędu Triple/Page Fault!
    for (uint64_t i = 4096; i < 65536; i++) { // 65536 ramek = potężne 256 MB
        void* ptr = (void*)(i * ROZMIAR_RAMKI);
        ZmapujStrone(ptr, ptr, FLAGA_OBECNA | FLAGA_ZAPIS);
    }
}

// Zewnętrzny interfejs dla loadera programów Ring 3
extern "C" void* PobierzAktualnePML4() {
    return (void*)globalne_pml4;
}