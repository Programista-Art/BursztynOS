/*
 * Mechanizm: Sterownik Myszy PS/2 w trybie przepływowym (Streaming)
 * Opis: Używa kontrolera 8042 w emulacji. Zbiera ruchy myszy i je resetuje,
 * ale zapobiega rysowaniu powiadomień po ekranie, aby Terminal pozostał czysty!
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
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xA8);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0x20);
    MyszCzekajNaOdczyt(); status = wejscie_port_bajt(0x60);
    
    status |= 2;
    status &= ~0x20;
    
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0x60);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, status);
    
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 0xF6);
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 
    
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis(); wyjscie_port_bajt(0x60, 0xF4);
    MyszCzekajNaOdczyt(); wejscie_port_bajt(0x60); 
}

uint8_t licznik_pakietu = 0;
uint8_t pakiety[3];

extern "C" void ObslugaPrzerwaniaMyszy() {
    uint8_t d = wejscie_port_bajt(0x60);
    
    if (licznik_pakietu == 0 && !(d & 0x08)) {
        if(baza_lapic_wirtualna) baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
        return;
    }

    pakiety[licznik_pakietu++] = d;

    if (licznik_pakietu >= 3) {
        /* * PAKIET ZEBRAWY POPRAWNIE
         * Zignorowano wypisywanie informacji, by Shell był przejrzysty.
         * W przyszłości podepniemy tu renderowanie graficznego kursora!
         */
        licznik_pakietu = 0; 
    }

    // Odblokowanie linii dla następnego przerwania
    if(baza_lapic_wirtualna) {
        baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
    }
}