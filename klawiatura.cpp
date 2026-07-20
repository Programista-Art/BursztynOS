/*
 * Mechanizm: Sterownik Klawiatury PS/2 z Polskimi Znakami (CP1250)
 * Opis: Przechwytuje kody skanowania, obsługuje klawisze rozszerzone (0xE0 - Prawy Alt)
 * i mapuje wciśnięcia na polskie znaki. Wysłane dane trafiają do GUI lub Ring 3.
 */

#include <stdint.h>
#include <stdbool.h>

static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc;
    asm volatile ("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

extern volatile uint32_t* baza_lapic_wirtualna;
#define LAPIC_EOI_OFFSET 0x0B0

// Funkcja zewnętrzna ze Składacza Obrazu! (Routing zdarzeń GUI)
extern "C" bool ZaktualizujKlawiatureGUI(char znak);

#define ROZMIAR_BUFORA 256
static char bufor_klawiatury[ROZMIAR_BUFORA];
static volatile int bufor_glowa = 0;
static volatile int bufor_ogon = 0;

static bool shift_wcisniety = false;
static bool prawy_alt_wcisniety = false;
static bool oczekuje_e0 = false; // Pamięć stanu dla klawiszy rozszerzonych (np. Prawy Alt)

const char scancode_ascii[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

const char scancode_ascii_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

extern "C" void ObslugaPrzerwaniaKlawiatury() {
    uint8_t scancode = wejscie_port_bajt(0x60);
    
    // Przechwycenie znacznika klawiszy rozszerzonych
    if (scancode == 0xE0) {
        oczekuje_e0 = true;
    } 
    // Obsługa Prawego Alta (AltGr) po znaczniku 0xE0
    else if (oczekuje_e0) {
        if (scancode == 0x38) prawy_alt_wcisniety = true;
        else if (scancode == 0xB8) prawy_alt_wcisniety = false;
        oczekuje_e0 = false;
    } 
    else {
        // Zwykłe klawisze modyfikujące
        if (scancode == 0x2A || scancode == 0x36) {
            shift_wcisniety = true;
        } else if (scancode == 0xAA || scancode == 0xB6) {
            shift_wcisniety = false;
        } else if (!(scancode & 0x80)) { 
            if (scancode < sizeof(scancode_ascii)) {
                char znak = 0;
                
                // Polskie znaki - Wstrzykiwanie kodów jednobajtowych w standardzie CP1250
                if (prawy_alt_wcisniety) {
                    if (scancode == 0x1E) znak = shift_wcisniety ? (char)0xA5 : (char)0xB9; // Ą / ą
                    else if (scancode == 0x2E) znak = shift_wcisniety ? (char)0xC6 : (char)0xE6; // Ć / ć
                    else if (scancode == 0x12) znak = shift_wcisniety ? (char)0xCA : (char)0xEA; // Ę / ę
                    else if (scancode == 0x26) znak = shift_wcisniety ? (char)0xA3 : (char)0xB3; // Ł / ł
                    else if (scancode == 0x31) znak = shift_wcisniety ? (char)0xD1 : (char)0xF1; // Ń / ń
                    else if (scancode == 0x18) znak = shift_wcisniety ? (char)0xD3 : (char)0xF3; // Ó / ó
                    else if (scancode == 0x1F) znak = shift_wcisniety ? (char)0x8C : (char)0x9C; // Ś / ś
                    else if (scancode == 0x2D) znak = shift_wcisniety ? (char)0x8F : (char)0x9F; // Ź / ź
                    else if (scancode == 0x2C) znak = shift_wcisniety ? (char)0xAF : (char)0xBF; // Ż / ż
                }
                
                // Zwykłe mapowanie jeśli to nie był Prawy Alt
                if (znak == 0) {
                    znak = shift_wcisniety ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
                }
                
                if (znak != 0) {
                    // 1. Wysyłamy literkę do Składacza Obrazu. Jeśli Edytor ma Focus, wchłonie on literę!
                    if (!ZaktualizujKlawiatureGUI(znak)) {
                        // 2. Jeśli Edytor jej nie wziął, puszczamy ją w obieg na stos Ring 3 dla Terminala.
                        int nastepna_glowa = (bufor_glowa + 1) % ROZMIAR_BUFORA;
                        if (nastepna_glowa != bufor_ogon) {
                            bufor_klawiatury[bufor_glowa] = znak;
                            bufor_glowa = nastepna_glowa;
                        }
                    }
                }
            }
        }
    }

    if (baza_lapic_wirtualna) {
        baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
    }
}

extern "C" char pobierz_znak_klawiatury() {
    if (bufor_glowa == bufor_ogon) {
        return 0; // Bufor jest pusty
    }
    
    char znak = bufor_klawiatury[bufor_ogon];
    bufor_ogon = (bufor_ogon + 1) % ROZMIAR_BUFORA;
    return znak;
}