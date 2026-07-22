#include <stdint.h>
#include <stdbool.h>

static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc;
    asm volatile ("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

extern volatile uint32_t* baza_lapic_wirtualna;
#define LAPIC_EOI_OFFSET 0x0B0

// Funkcja zewnętrzna ze Składacza Obrazu (routing zdarzeń)
extern "C" bool zaktualizuj_klawiature_gui(char znak);

#define ROZMIAR_BUFORA 256
static char bufor_klawiatury[ROZMIAR_BUFORA];
static volatile int bufor_glowa = 0;
static volatile int bufor_ogon = 0;

static bool shift_wcisniety = false;

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

// Funkcja przechwytująca wektor IRQ 1 (teraz w snake_case!)
extern "C" void obsluga_przerwania_klawiatury() {
    uint8_t scancode = wejscie_port_bajt(0x60);
    
    if (scancode == 0x2A || scancode == 0x36) {
        shift_wcisniety = true;
    } 
    else if (scancode == 0xAA || scancode == 0xB6) { 
        shift_wcisniety = false;
    } 
    else if (!(scancode & 0x80)) { 
        if (scancode < sizeof(scancode_ascii)) {
            char znak = shift_wcisniety ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
            
            if (znak != 0) {
                // Przekazanie do GUI - jeśłi GUI tego nie zje, ląduje w buforze Terminala
                if (!zaktualizuj_klawiature_gui(znak)) {
                    int nastepna_glowa = (bufor_glowa + 1) % ROZMIAR_BUFORA;
                    if (nastepna_glowa != bufor_ogon) {
                        bufor_klawiatury[bufor_glowa] = znak;
                        bufor_glowa = nastepna_glowa;
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
    if (bufor_glowa == bufor_ogon) return 0;
    
    char znak = bufor_klawiatury[bufor_ogon];
    bufor_ogon = (bufor_ogon + 1) % ROZMIAR_BUFORA;
    return znak;
}