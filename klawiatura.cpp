#include <stdint.h>
#include <stdbool.h>

// Funkcja asemblerowa do odczytu z portu
static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc;
    asm volatile ("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

// Zmienne systemowe LAPIC
extern volatile uint32_t* baza_lapic_wirtualna;
#define LAPIC_EOI_OFFSET 0x0B0

// Połączenie z GUI (Zarządca Okien)
extern "C" bool zaktualizuj_klawiature_gui(char znak);

// Wewnętrzny bufor dla terminala Ring 3
#define ROZMIAR_BUFORA 256
static char bufor_klawiatury[ROZMIAR_BUFORA];
static volatile int bufor_glowa = 0;
static volatile int bufor_ogon = 0;

// Stany modyfikatorów klawiatury
static bool lewy_shift = false;
static bool prawy_shift = false;
static bool prawy_alt = false;
static bool klawisz_rozszerzony_e0 = false; // Rozpoznaje sprzętowe kody np. Prawy Alt

// Tablica ASCII (standardowa klawiatura US)
const char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,
  '*',  0,  ' ',  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,
    0,  0,   0,   0,   0,   0, '-',   0,   0,   0, '+',   0,   0,
    0,  0,   0,   0,   0,   0,   0,   0,   0
};

const char kbd_us_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0,
  '*',  0,  ' ',  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,
    0,  0,   0,   0,   0,   0, '-',   0,   0,   0, '+',   0,   0,
    0,  0,   0,   0,   0,   0,   0,   0,   0
};

// Funkcja kierująca bajt do Edytora (GUI) lub Terminala (Ring 3)
static void zglos_bajt_do_systemu(uint8_t bajt) {
    if (!zaktualizuj_klawiature_gui((char)bajt)) {
        int nastepna_glowa = (bufor_glowa + 1) % ROZMIAR_BUFORA;
        if (nastepna_glowa != bufor_ogon) {
            bufor_klawiatury[bufor_glowa] = (char)bajt;
            bufor_glowa = nastepna_glowa;
        }
    }
}

// GŁÓWNA FUNKCJA PRZERWANIA - Dopasowana nazwa (snake_case) do idt.cpp!
extern "C" void obsluga_przerwania_klawiatury() {
    uint8_t scancode = wejscie_port_bajt(0x60);

    // Sprzętowe kody rozszerzone (np. strzałki, Prawy Alt) zaczynają się od 0xE0
    if (scancode == 0xE0) {
        klawisz_rozszerzony_e0 = true;
        return;
    }

    if (klawisz_rozszerzony_e0) {
        if (scancode == 0x38) prawy_alt = true;       // Wciśnięto Prawy Alt
        else if (scancode == 0xB8) prawy_alt = false; // Puszczono Prawy Alt
        
        klawisz_rozszerzony_e0 = false;
        if(baza_lapic_wirtualna) baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
        return;
    }

    // Śledzenie wciśniętych Shiftów
    if (scancode == 0x2A) lewy_shift = true;
    else if (scancode == 0xAA) lewy_shift = false;
    else if (scancode == 0x36) prawy_shift = true;
    else if (scancode == 0xB6) prawy_shift = false;

    bool shift = lewy_shift || prawy_shift;

    if (!(scancode & 0x80)) { // 0x80 to bit zwolnienia klawisza (Key Up)
        
        // --- MAGIA: POLSKIE ZNAKI UTF-8 (2 BAJTY) ---
        if (prawy_alt) {
            if (scancode == 0x1E) { zglos_bajt_do_systemu(0xC4); zglos_bajt_do_systemu(shift ? 0x84 : 0x85); } // Ą / ą
            else if (scancode == 0x2E) { zglos_bajt_do_systemu(0xC4); zglos_bajt_do_systemu(shift ? 0x86 : 0x87); } // Ć / ć
            else if (scancode == 0x12) { zglos_bajt_do_systemu(0xC4); zglos_bajt_do_systemu(shift ? 0x98 : 0x99); } // Ę / ę
            else if (scancode == 0x26) { zglos_bajt_do_systemu(0xC5); zglos_bajt_do_systemu(shift ? 0x81 : 0x82); } // Ł / ł
            else if (scancode == 0x31) { zglos_bajt_do_systemu(0xC5); zglos_bajt_do_systemu(shift ? 0x83 : 0x84); } // Ń / ń
            else if (scancode == 0x18) { zglos_bajt_do_systemu(0xC3); zglos_bajt_do_systemu(shift ? 0x92 : 0xB3); } // Ó / ó
            else if (scancode == 0x1F) { zglos_bajt_do_systemu(0xC5); zglos_bajt_do_systemu(shift ? 0x9A : 0x9B); } // Ś / ś
            else if (scancode == 0x2D) { zglos_bajt_do_systemu(0xC5); zglos_bajt_do_systemu(shift ? 0xB9 : 0xBA); } // Ź / ź (na klawiszu X)
            else if (scancode == 0x2C) { zglos_bajt_do_systemu(0xC5); zglos_bajt_do_systemu(shift ? 0xBB : 0xBC); } // Ż / ż (na klawiszu Z)
        } 
        else if (scancode < 128) {
            // Zwykłe znaki ASCII
            char znak = shift ? kbd_us_shift[scancode] : kbd_us[scancode];
            if (znak != 0) {
                zglos_bajt_do_systemu((uint8_t)znak);
            }
        }
    }

    // Odblokowanie przerwań sprzętowych APIC
    if (baza_lapic_wirtualna) {
        baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
    }
}

// Funkcja dla Wywołań Systemowych (BWS) z pliku syscalls.cpp
extern "C" char pobierz_znak_klawiatury() {
    if (bufor_glowa == bufor_ogon) return 0;
    
    char znak = bufor_klawiatury[bufor_ogon];
    bufor_ogon = (bufor_ogon + 1) % ROZMIAR_BUFORA;
    return znak;
}