/*
 * Mechanizm: Zarządzanie Pamięcią Wirtualną (VMM - Virtual Memory Manager)
 * Opis: Niezawodna inicjalizacja za pomocą z góry alokowanych w .bss tablic stron.
 * Rozwiązuje problem 'Kury i Jajka' przy rozruchu na starszych BIOS-ach.
 */

#include "pamiec.h"
#include <stdint.h>

#define FLAGA_OBECNA         0b00000001
#define FLAGA_ZAPIS          0b00000010
#define FLAGA_UZYTKOWNIKA    0b00000100
#define FLAGA_ROZMIAR_STRONY 0b10000000 // Bit 7 (PS) - aktywuje wielkie strony 2MB

#define MASKA_INDEKSU 0x1FF
#define MASKA_ADRESU_FIZYCZNEGO 0x000FFFFFFFFFF000ULL 

// Statyczne tablice dla pierwszych 4 GB. Zapewniają absolutne bezpieczeństwo 
// przy przełączaniu trybów, bo na 100% są początkowo zmapowane przez boot.S
static uint64_t st_pml4[512] __attribute__((aligned(4096)));
static uint64_t st_pdp[512] __attribute__((aligned(4096)));
static uint64_t st_pd[4][512] __attribute__((aligned(4096)));

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

    // ZABEZPIECZENIE: Jeśli trafiliśmy na Wielką Stronę 2MB (np. obszar 0-4 GB z InicjalizujVMM)
    if (w_pd[wew_pd] & FLAGA_ROZMIAR_STRONY) {
        // Jeśli moduł grafiki nakłada flagi Cache Disable na bufor, dodajemy je do wielkiej strony!
        w_pd[wew_pd] |= flagi;
        asm volatile("invlpg (%0)" : : "r" (adres_wirtualny) : "memory");
        return; 
    }

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
    WyzerujStrone(st_pml4);
    WyzerujStrone(st_pdp);
    for(int i = 0; i < 4; i++) WyzerujStrone(st_pd[i]);

    // Linkowanie rdzennej hierarchii
    st_pml4[0] = ((uint64_t)st_pdp) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;

    for(int i = 0; i < 4; i++) {
        st_pdp[i] = ((uint64_t)st_pd[i]) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;
    }

    // Mapowanie CAŁYCH 4 GIGABAJTÓW pamięci za pomocą gigantycznych stron 2MB!
    for (uint64_t p = 0; p < 4; p++) {
        for (uint64_t i = 0; i < 512; i++) {
            uint64_t fiz = (p * 1024 * 1024 * 1024ULL) + (i * 2 * 1024 * 1024ULL);
            st_pd[p][i] = fiz | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA | FLAGA_ROZMIAR_STRONY;
        }
    }

    // Oznaczamy APIC oraz IOAPIC jako Cache Disable (0x10 | 0x08) wprost na strukturach 2MB
    st_pd[3][503] = 0xFEE00000ULL | FLAGA_OBECNA | FLAGA_ZAPIS | 0x10 | 0x08 | FLAGA_ROZMIAR_STRONY;
    st_pd[3][502] = 0xFEC00000ULL | FLAGA_OBECNA | FLAGA_ZAPIS | 0x10 | 0x08 | FLAGA_ROZMIAR_STRONY;

    globalne_pml4 = st_pml4;
    
    // Atomowe przełączenie - od tej pory Jądro i Aplikacje cieszą się w pełni stabilnymi 4 GB pamięci!
    asm volatile("mov %0, %%cr3" : : "r"((uint64_t)globalne_pml4) : "memory");
}

extern "C" void* PobierzAktualnePML4() {
    return (void*)globalne_pml4;
}