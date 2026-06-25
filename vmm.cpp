/*
 * Mechanizm: Zarządzanie Pamięcią Wirtualną (VMM - Virtual Memory Manager)
 * Opis: Hierarchiczna implementacja struktury tablic strony dla przestrzeni Ring 0 i 3.
 * Pobiera ramki operacyjne z nowo powstałego mechanizmu PMM. W 64-bit architektura
 * wymaga co najmniej 4 zagłębień: Page Map Level 4 (PML4) -> 
 * Page Directory Pointer (PDP) -> Page Directory (PD) -> Page Table (PT).
 */

#include "pamiec.h"

// Makra stałych decydujących o charakterystyce mapowanego przydziału
#define FLAGA_OBECNA      0b00000001  // Bit 0 = P
#define FLAGA_ZAPIS       0b00000010  // Bit 1 = RW (Zezwolenie na rzut z zapisem)
#define FLAGA_UZYTKOWNIKA 0b00000100  // Bit 2 = US (Zezwolenie na przydział dostępny dla Ring 3)

// 512 wpisów pod 64-bit tablicą stron, stąd maski:
#define MASKA_INDEKSU 0x1FF

// Bazowy, najwyższy punkt drzewa nowej wirtualnej instalacji
uint64_t* globalne_pml4 = nullptr;

// Oczyszczanie świeżo zrabowanej z PMM ramki - brak przestarzałych asocjacji (Unikamy śmieci RAM-u)
void WyzerujStrone(void* wskaznik) {
    uint64_t* wsk = (uint64_t*)wskaznik;
    for (int i = 0; i < 512; i++) {
        wsk[i] = 0;
    }
}

/* 
 * Mapowanie liniowego przestrzennego adresu (Wirtualnego) na zdefiniowany przez nas fizyczny klucz (Hardware)
 * Funkcja samodzielnie zejdzie po węzłach. Jeśli natrafi na brak po drodze = Zgłasza prośbę do PMM i spina relację.
 */
void ZmapujStrone(void* adres_wirtualny, void* adres_fizyczny, uint32_t flagi) {
    if (!globalne_pml4) return;

    // Pobierz wskaźnik jako liczbę nieujemną w celu parsowania masek
    uint64_t wir = (uint64_t)adres_wirtualny;
    uint64_t fiz = (uint64_t)adres_fizyczny;

    // Demontujemy adres liniowy uzyskując poszczególne zjazdy dla węzłów
    uint64_t wew_pml4 = (wir >> 39) & MASKA_INDEKSU;
    uint64_t wew_pdp  = (wir >> 30) & MASKA_INDEKSU;
    uint64_t wew_pd   = (wir >> 21) & MASKA_INDEKSU;
    uint64_t wew_pt   = (wir >> 12) & MASKA_INDEKSU;

    // 1. Zjazd pierwszy (Z PML4 do PDP)
    if (!(globalne_pml4[wew_pml4] & FLAGA_OBECNA)) {
        // Tablica nieobecna. Wykreujmy ją
        uint64_t* nowa_pdp = (uint64_t*)ZaalokujRamke();
        if (!nowa_pdp) return; // Brak fizycznego RAMu!
        WyzerujStrone(nowa_pdp);
        // Spinamy rzut z powrotem. Uprawnienia po drodze dla VMM są uwarunkowane dla Ring 3, mimo iż finalny PT może tego zabronić
        globalne_pml4[wew_pml4] = ((uint64_t)nowa_pdp) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;
    }
    // Przesuń z offsetu PML4 do wskaźnika fizycznego usunięciem bitów flag
    uint64_t* w_pdp = (uint64_t*)(globalne_pml4[wew_pml4] & ~0xFFF);

    // 2. Zjazd z PDP do PD
    if (!(w_pdp[wew_pdp] & FLAGA_OBECNA)) {
        uint64_t* nowa_pd = (uint64_t*)ZaalokujRamke();
        if (!nowa_pd) return; 
        WyzerujStrone(nowa_pd);
        w_pdp[wew_pdp] = ((uint64_t)nowa_pd) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;
    }
    uint64_t* w_pd = (uint64_t*)(w_pdp[wew_pdp] & ~0xFFF);

    // 3. Zjazd z PD do ostatecznej Page Table (PT)
    if (!(w_pd[wew_pd] & FLAGA_OBECNA)) {
        uint64_t* nowa_pt = (uint64_t*)ZaalokujRamke();
        if (!nowa_pt) return; 
        WyzerujStrone(nowa_pt);
        w_pd[wew_pd] = ((uint64_t)nowa_pt) | FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_UZYTKOWNIKA;
    }
    uint64_t* w_pt = (uint64_t*)(w_pd[wew_pd] & ~0xFFF);

    // 4. Przypnij docelowy klucz do Page Table uwzględniając podane przez argument `flagi` ochronne (User/Kernel - RW/RO)
    w_pt[wew_pt] = (fiz & ~0xFFF) | flagi;

    // Przymusowo wyrzuć stary układ w procesorze (TLB Shootdown) dla pewności, że procesor podchwyci przebudowę
    asm volatile("invlpg (%0)" : : "r" (adres_wirtualny) : "memory");
}

/*
 * Odrzucenie pierwotnych 2MB map spłodzonych ręcznie na poczet trybu roboczego Asemblera.
 * Następuje dynamiczna przebudowa struktury przy użyciu dedykowanego VMM.
 */
void InicjalizujVMM() {
    // Alokuj najwyższe pasmo drzewa stronicującego
    globalne_pml4 = (uint64_t*)ZaalokujRamke();
    if (!globalne_pml4) return;
    WyzerujStrone(globalne_pml4);

    // Dla ciągłości pracy mapujemy pierwsze 16 MB pamięci 1:1, by chronić bufor wizualny, sam kod Jądra oraz dolne części APIC (o ile byłyby niżej).
    // Tu używamy skromnych i bezpiecznych paczek 4KB stronicowania względem asemblerowych 2MB.
    // 16 MB = 16384 KB = 4096 Ramek 4KB
    for (uint64_t i = 0; i < 4096; i++) {
        void* ptr = (void*)(i * ROZMIAR_RAMKI);
        ZmapujStrone(ptr, ptr, FLAGA_OBECNA | FLAGA_ZAPIS);
    }

    // Bezpiecznie przebudowujemy mapowanie APIC MMIO 0xFEE00000 z wczorajszej łatki na znowu 1:1 z pominięciem Cache (wymagane w specyfikacji).
    // Odłączamy to od starego systemu z PD używanego w Asemblerze pod indexem 0x200000.
    ZmapujStrone((void*)0xFEE00000, (void*)0xFEE00000, FLAGA_OBECNA | FLAGA_ZAPIS | 0x10 | 0x08); // + Cache Disable, + Write Through
    
    // Zróbmy tak samo dla 0xFEC00000 dla wariantu IOAPIC.
    ZmapujStrone((void*)0xFEC00000, (void*)0xFEC00000, FLAGA_OBECNA | FLAGA_ZAPIS | 0x10 | 0x08); 

    // Właduj wskaźnik zbudowanej tablicy do rejestru CR3 zmuszając CPU do porzucenia dotychczasowych (zdefiniowanych w boot.S i apic.cpp) przydziałów.
    uint64_t adres_bazy = (uint64_t)globalne_pml4;
    asm volatile("mov %0, %%cr3" : : "r"(adres_bazy) : "memory");
}
