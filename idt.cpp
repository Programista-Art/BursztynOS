/*
 * Mechanizm: Zmodyfikowana wersja IDT z obsługą dla Advanced Programmable Interrupt Controller
 * Opis: Przygotowuje bramki od 0 do 47 z 256 dostępnych, gdzie pierwsze 32 
 * zarezerwowane są dla wyjątków a wektory > 32 dla urządzeń zewnętrznych.
 */

#include <stdint.h>

static inline void wyjscie_port_bajt(uint16_t port, uint8_t wartosc) {
    asm volatile ("outb %0, %1" : : "a"(wartosc), "Nd"(port));
}

extern "C" void wypisz_na_ekranie(const char* buf);

// Deklaracje sterowników sprzętowych podpiętych do jądra
extern "C" void ObslugaPrzerwaniaKlawiatury();
extern "C" void ObslugaPrzerwaniaMyszy();

struct DeskryptorIDT {
    uint16_t offset_czesc1;
    uint16_t selektor_kodu;
    uint8_t  interrupt_stack;
    uint8_t  typ_oraz_atrybuty;
    uint16_t offset_czesc2;
    uint32_t offset_czesc3;
    uint32_t puste_zero;
} __attribute__((packed));

struct RejestrIDT {
    uint16_t rozmiar;
    uint64_t adres;
} __attribute__((packed));

struct DeskryptorIDT tablica_idt[256];
struct RejestrIDT wskaznik_idtr;

extern "C" void zaladuj_zaktualizowane_idt(uint64_t adres_idtr);
extern uint64_t tablica_isr[];

void UstawWpisIDT(uint8_t wektor, uint64_t procedura_isr, uint8_t flagi) {
    tablica_idt[wektor].offset_czesc1     = procedura_isr & 0xFFFF;
    tablica_idt[wektor].selektor_kodu     = 0x08;
    tablica_idt[wektor].interrupt_stack   = 0;
    tablica_idt[wektor].typ_oraz_atrybuty = flagi;
    tablica_idt[wektor].offset_czesc2     = (procedura_isr >> 16) & 0xFFFF;
    tablica_idt[wektor].offset_czesc3     = (procedura_isr >> 32) & 0xFFFFFFFF;
    tablica_idt[wektor].puste_zero        = 0;
}

struct RejestryStanowe {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rsi, rdi, rdx, rcx, rbx, rax;
    uint64_t wektor_przerwania, kod_bledu;
    uint64_t adres_powrotu, rejestr_cs, rflags, stary_rsp, stary_ss;
} __attribute__((packed));

extern volatile uint32_t* baza_lapic_wirtualna;
#define LAPIC_EOI_OFFSET 0x0B0

// --- NOWY DYSPOZYTOR PRZERWAŃ ---
extern "C" void WspolnaObslugaPrzerwan(struct RejestryStanowe* stan) {
    if (stan->wektor_przerwania < 32) {
        wypisz_na_ekranie("BLAD SYSTEMOWY: Wystapil wyjatek sprzetowy!");
        while(1) asm volatile("hlt");
    }

    if (stan->wektor_przerwania >= 32) {
        // Przekierowanie do odpowiedniego sterownika
        if (stan->wektor_przerwania == 33) {
            ObslugaPrzerwaniaKlawiatury(); // Klawiatura uderza w 33
        } 
        else if (stan->wektor_przerwania == 44) {
            ObslugaPrzerwaniaMyszy();      // Mysz uderza w 44
        }
        // Wyciszyliśmy printowanie Zegara (wektor 32), by pozwolić Ci pisać!

        // Wysłanie End of Interrupt (EOI) musi odbyć się po KAŻDYM przerwaniu sprzętowym!
        if(baza_lapic_wirtualna) {
            baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
        }
    }
}

extern "C" void InicjalizujIDT() {
    wskaznik_idtr.rozmiar = (sizeof(struct DeskryptorIDT) * 256) - 1;
    wskaznik_idtr.adres   = (uint64_t)&tablica_idt;

    for (int i = 0; i < 48; i++) {
        UstawWpisIDT(i, tablica_isr[i], 0x8E);
    }
    zaladuj_zaktualizowane_idt((uint64_t)&wskaznik_idtr);
}