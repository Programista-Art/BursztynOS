/*
 * Mechanizm: Wyłączenie przestarzałego PIC, aktywacja Local APIC oraz IOAPIC
 * Opis: Wycisza układy Intel 8259A, a następnie mapuje przerwania sprzętowe
 * (Klawiatura, Mysz) poprzez zaawansowany kontroler IOAPIC.
 */

#include <stdint.h>

static inline void wyjscie_port_bajt(uint16_t port, uint8_t wartosc) {
    asm volatile ("outb %0, %1" : : "a"(wartosc), "Nd"(port));
}

static inline uint64_t odczytaj_msr(uint32_t msr) {
    uint32_t dolny, gorny;
    asm volatile("rdmsr" : "=a"(dolny), "=d"(gorny) : "c"(msr));
    return ((uint64_t)gorny << 32) | dolny;
}

static inline void zapisz_msr(uint32_t msr, uint64_t wartosc) {
    uint32_t dolny = wartosc & 0xFFFFFFFF;
    uint32_t gorny = wartosc >> 32;
    asm volatile("wrmsr" : : "a"(dolny), "d"(gorny), "c"(msr));
}

void WylaczPrzestarzalePIC() {
    wyjscie_port_bajt(0x21, 0xFF);
    wyjscie_port_bajt(0xA1, 0xFF);
}

// Struktury dla Local APIC
#define APIC_SPURIOUS_INT_REGISTER 0x0F0
#define APIC_TIMER_LVT_REGISTER    0x320
#define APIC_TIMER_INITIAL_COUNT   0x380
#define APIC_TIMER_DIVIDE_CONFIG   0x3E0

volatile uint32_t* baza_lapic_wirtualna;

// --- NOWE: Struktury dla IOAPIC (Routing Sprzętowy) ---
volatile uint32_t* baza_ioapic_wirtualna;

void PrzekierujPrzerwanieIOAPIC(uint8_t irq, uint8_t wektor_docelowy) {
    uint32_t dolny = wektor_docelowy; // Flagi: odmaskowane (bit 16=0), edge-triggered
    uint32_t gorny = 0;               // Docelowy APIC ID = 0 (nasz jedyny procesor)
    uint8_t rejestr = 0x10 + (irq * 2); // Wpisy w tabeli przekierowań zaczynają się od offsetu 0x10

    // Zapis do dolnej części rejestru IOAPIC
    baza_ioapic_wirtualna[0] = rejestr;
    baza_ioapic_wirtualna[4] = dolny;
    
    // Zapis do górnej części rejestru IOAPIC
    baza_ioapic_wirtualna[0] = rejestr + 1;
    baza_ioapic_wirtualna[4] = gorny;
}

extern "C" void InicjalizujAPIC() {
    WylaczPrzestarzalePIC();

    // Inicjalizacja Local APIC (Rdzeń procesora)
    uint64_t rejestr_apic_base = odczytaj_msr(0x1B);
    baza_lapic_wirtualna = (uint32_t*)(rejestr_apic_base & 0xFFFFF000);
    zapisz_msr(0x1B, rejestr_apic_base | (1 << 11));
    baza_lapic_wirtualna[APIC_SPURIOUS_INT_REGISTER / 4] = 255 | 0x100;

    // Timer APIC na wektorze 32
    baza_lapic_wirtualna[APIC_TIMER_LVT_REGISTER / 4] = 32 | 0x20000;
    baza_lapic_wirtualna[APIC_TIMER_DIVIDE_CONFIG / 4] = 0x3;
    baza_lapic_wirtualna[APIC_TIMER_INITIAL_COUNT / 4] = 0x05FFFFFF;

    // --- NOWE: Inicjalizacja routingu wejścia z urządzeń w IOAPIC ---
    baza_ioapic_wirtualna = (uint32_t*)0xFEC00000; // Standardowy fizyczny adres chipsetu IOAPIC
    
    // Klawiatura PS/2 operuje na standardowym kablu IRQ 1. Wysyłamy ją do wektora 33 w Jądrze.
    PrzekierujPrzerwanieIOAPIC(1, 33);
    
    // Mysz PS/2 operuje na standardowym kablu IRQ 12. Wysyłamy ją do wektora 44 w Jądrze.
    PrzekierujPrzerwanieIOAPIC(12, 44);
}