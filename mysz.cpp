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

// Funkcja zewnętrzna ze Składacza Obrazu (nowa w snake_case)
extern "C" void zaktualizuj_mysze(int dx, int dy, uint8_t przyciski);

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

// Funkcja obsługująca myszkę po nowemu w snake_case
extern "C" void obsluga_przerwania_myszy() {
    uint8_t d = wejscie_port_bajt(0x60);
    
    if (licznik_pakietu == 0 && !(d & 0x08)) {
        if(baza_lapic_wirtualna) baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
        return;
    }

    pakiety[licznik_pakietu++] = d;

    if (licznik_pakietu >= 3) {
        uint8_t flagi = pakiety[0];
        
        int dx = pakiety[1];
        int dy = pakiety[2];

        if (flagi & (1 << 4)) dx -= 256; 
        if (flagi & (1 << 5)) dy -= 256;

        uint8_t przyciski = flagi & 0x07; 
        
        // Wywołaj sterownik interfejsu (w snake_case)
        zaktualizuj_mysze(dx, dy, przyciski);
        
        licznik_pakietu = 0; 
    }

    if(baza_lapic_wirtualna) {
        baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
    }
}