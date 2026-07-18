/*
 * Mechanizm: Sterownik Klawiatury PS/2 z buforem cyklicznym
 * Opis: Przechwytuje kody skanowania (scancodes), tłumaczy je na znaki ASCII 
 * i zapisuje do bufora, z którego aplikacje Ring 3 mogą je odczytać (Syscall 4).
 */

#include <stdint.h>
#include <stdbool.h>

// Pomocnicza funkcja do czytania z portu I/O
static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc;
    asm volatile ("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

// Zmienne z innych modułów jądra
extern volatile uint32_t* baza_lapic_wirtualna;
#define LAPIC_EOI_OFFSET 0x0B0

// ---------------------------------------------------------
// BUFOR CYKLICZNY KLAWIATURY
// ---------------------------------------------------------
#define ROZMIAR_BUFORA 256
static char bufor_klawiatury[ROZMIAR_BUFORA];
static volatile int bufor_glowa = 0; // Miejsce, gdzie sterownik dodaje literkę
static volatile int bufor_ogon = 0;  // Miejsce, skąd aplikacja pobiera literkę

static bool shift_wcisniety = false;

// ---------------------------------------------------------
// MAPOWANIE KLAWISZY (Scancode Set 1) na kody ASCII
// ---------------------------------------------------------
const char scancode_ascii[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

// Mapowanie dla wciśniętego klawisza Shift
const char scancode_ascii_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

/*
 * Obsługa Przerwania od Klawiatury (IRQ 1)
 */
extern "C" void ObslugaPrzerwaniaKlawiatury() {
    uint8_t scancode = wejscie_port_bajt(0x60);
    
    // Obsługa klawiszy Shift (0x2A = Lewy Shift, 0x36 = Prawy Shift)
    if (scancode == 0x2A || scancode == 0x36) {
        shift_wcisniety = true;
    } 
    // Odciśnięcie klawiszy Shift (Kody puszczenia to wciśnięcie + 0x80)
    else if (scancode == 0xAA || scancode == 0xB6) { 
        shift_wcisniety = false;
    } 
    // Zwykłe klawisze (Najwyższy bit równy 0 oznacza akcję "wciśnięcie")
    else if (!(scancode & 0x80)) { 
        if (scancode < sizeof(scancode_ascii)) {
            char znak = shift_wcisniety ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
            
            if (znak != 0) {
                // Oblicz następną pozycję w buforze kołowym
                int nastepna_glowa = (bufor_glowa + 1) % ROZMIAR_BUFORA;
                
                // Jeśli bufor nie jest pełny, zapisz literkę
                if (nastepna_glowa != bufor_ogon) {
                    bufor_klawiatury[bufor_glowa] = znak;
                    bufor_glowa = nastepna_glowa;
                }
            }
        }
    }

    // KRYTYCZNE: Sygnał End of Interrupt (EOI) dla LAPIC, by przyjąć kolejne przerwania!
    if (baza_lapic_wirtualna) {
        baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
    }
}

/*
 * Funkcja udostępniana dla interfejsu wywołań systemowych (Syscalls).
 * Wyciąga jeden najstarszy znak z bufora. Zwraca 0, jeśli bufor jest pusty.
 */
extern "C" char pobierz_znak_klawiatury() {
    if (bufor_glowa == bufor_ogon) {
        return 0; // Bufor jest pusty
    }
    
    char znak = bufor_klawiatury[bufor_ogon];
    bufor_ogon = (bufor_ogon + 1) % ROZMIAR_BUFORA;
    return znak;
}