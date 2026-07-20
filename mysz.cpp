/*
 * Mechanizm: Sterownik Myszy PS/2 z Odkodowywaniem Pakietów (GUI)
 * Opis: Odbiera 3-bajtowe pakiety od kontrolera 8042, dekoduje
 * przesunięcia osi X i Y oraz przesyła je do Składacza Obrazu.
 * Wzbogacony o sprzętową i programową akcelerację dla matryc HD.
 */

#include <stdint.h>

static inline void wyjscie_port_bajt(uint16_t port, uint8_t wartosc) {
    asm volatile ("outb %0, %1" : : "a"(wartosc), "Nd"(port));
}

static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc;
    asm volatile ("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

extern volatile uint32_t* baza_lapic_wirtualna;
#define LAPIC_EOI_OFFSET 0x0B0

// Funkcja zewnętrzna ze Składacza Obrazu (grafika.cpp)
extern "C" void ZaktualizujMysze(int dx, int dy, uint8_t przyciski);

void MyszCzekajNaZapis() {
    uint32_t odliczenie = 100000;
    while(odliczenie--) if((wejscie_port_bajt(0x64) & 2) == 0) return;
}

void MyszCzekajNaOdczyt() {
    uint32_t odliczenie = 100000;
    while(odliczenie--) if((wejscie_port_bajt(0x64) & 1) == 1) return;
}

extern "C" void InicjalizujMyszPS2() {
    uint8_t status;
    
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xA8); // Aktywacja drugiego portu PS/2
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0x20); // Żądanie statusu kontrolera
    MyszCzekajNaOdczyt(); status = wejscie_port_bajt(0x60);
    
    status |= 2;       // Włącz przerwania (IRQ 12)
    status &= ~0x20;   // Włącz zegar myszy
    
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0x60);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, status);
    
    // 1. Przywrócenie ustawień domyślnych
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 0xF6);
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 

    // 2. --- ZWIĘKSZENIE CZUŁOŚCI (Rozdzielczość sprzętowa) ---
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 0xE8); // Komenda set resolution
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 0x03); // Maksymalna rozdzielczość (8 counts/mm)
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 

    // 3. --- ZWIĘKSZENIE CZĘSTOTLIWOŚCI (Sample Rate) ---
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 0xF3); // Komenda set sample rate
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 200);  // Maksymalne 200 próbek na sekundę
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 
    
    // 4. Włączenie raportowania pakietów w urządzeniu
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 0xF4);
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 
}

uint8_t licznik_pakietu = 0;
uint8_t pakiety[3];

extern "C" void ObslugaPrzerwaniaMyszy() {
    uint8_t d = wejscie_port_bajt(0x60);
    
    // Synchronizacja pakietów (Bit 3 w pierwszym bajcie musi być równy 1)
    if (licznik_pakietu == 0 && !(d & 0x08)) {
        if(baza_lapic_wirtualna) baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
        return;
    }

    pakiety[licznik_pakietu++] = d;

    // Pełen pakiet = 3 bajty
    if (licznik_pakietu >= 3) {
        uint8_t flagi = pakiety[0];
        
        // Zabezpieczenie: Ignorujemy pakiet, jeśli wystąpiło przepełnienie sprzętowe (zbyt gwałtowny ruch)
        if (!(flagi & 0xC0)) {
            // Konwersja surowych bajtów na przesunięcie ze znakiem
            int dx = pakiety[1];
            int dy = pakiety[2];

            // Wyciąganie ujemnych wartości na podstawie flag (Uzupełnienie do dwóch)
            if (flagi & (1 << 4)) dx -= 256; 
            if (flagi & (1 << 5)) dy -= 256;

            // --- AKCELERACJA PROGRAMOWA ---
            // Mnożymy przesunięcie, aby kursor był szybki i z łatwością pokrył całą matrycę 1024x768
            dx *= 2;
            dy *= 2;

            uint8_t przyciski = flagi & 0x07; // 3 pierwsze bity to Lewy, Prawy, Środkowy

            // Wywołaj sprzętowy aktualizator grafiki w Menedżerze Okien
            ZaktualizujMysze(dx, dy, przyciski);
        }
        
        licznik_pakietu = 0; 
    }

    // Odblokowanie linii dla następnego przerwania
    if(baza_lapic_wirtualna) {
        baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
    }
}