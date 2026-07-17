/*
 * Mechanizm: Sterownik Myszy PS/2 w trybie przepływowym (Streaming)
 * Opis: Używa kontrolera 8042 w emulacji. Mysz generuje przesył w małych paczkach
 * po 3 bajty każdy, dających podgląd na zmiany stanu wcisku i relatywnego ruchu.
 */

#include <stdint.h>

// Pomocnicze funkcje in/out 
static inline void wyjscie_port_bajt(uint16_t port, uint8_t wartosc) {
    asm volatile ("outb %0, %1" : : "a"(wartosc), "Nd"(port));
}

static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc;
    asm volatile ("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

// Zewnętrzne zmienne systemowe
extern "C" void wypisz_na_ekranie(const char* buf);
extern volatile uint32_t* baza_lapic_wirtualna; // Z apic.cpp
#define LAPIC_EOI_OFFSET 0x0B0                  // Rejestr End of Interrupt

// Oczekuje dopóki port sprzętowy kontrolera (0x64) nie da zgody na Zapis (bit 1)
void MyszCzekajNaZapis() {
    uint32_t odliczenie = 100000;
    while(odliczenie--) {
        if((wejscie_port_bajt(0x64) & 2) == 0) return;
    }
}

// Oczekuje dopóki port sprzętowy kontrolera (0x64) nie poinformuje o gotowych Odczytach (bit 0)
void MyszCzekajNaOdczyt() {
    uint32_t odliczenie = 100000;
    while(odliczenie--) {
        if((wejscie_port_bajt(0x64) & 1) == 1) return;
    }
}

/*
 * Inicjalizacja kontrolera Auxiliary (Mysz PS/2) w płycie głównej
 * Należy to wywołać raz z jądra przed włączeniem przerwań (sti)
 */
extern "C" void InicjalizujMyszPS2() {
    uint8_t status_kontrolera;

    // 1. Zezwól na obsługę urządzenia pomocniczego
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0xA8);

    // 2. Wczytaj obecny status konfiguracyjny
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0x20);
    MyszCzekajNaOdczyt();
    status_kontrolera = wejscie_port_bajt(0x60);

    // 3. Włącz przerwania dla wektora Myszy (Bit nr 1 = 1) i zapobiegnij deaktywacji zegara
    status_kontrolera |= 2;
    status_kontrolera &= ~0x20;

    // 4. Zapisz z powrotem zaktualizowany konfigurator
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0x60);
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x60, status_kontrolera);

    // 5. Zresetuj mysz (0xFF lub 0xF6) i użyj domyślnych ustawień
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x60, 0xF6);
    MyszCzekajNaOdczyt();
    wejscie_port_bajt(0x60); // Odbierz ACK (0xFA)
    
    // 6. Uruchom ciągły przepływ i nasłuch pakietów (Enable Data Reporting)
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x60, 0xF4);
    MyszCzekajNaOdczyt();
    wejscie_port_bajt(0x60); // Odbierz ACK (0xFA)
}

// Zmienne gromadzące stan cyklu pakietowania
uint8_t licznik_pakietu = 0;
uint8_t pakiety[3];

/*
 * Obsługa Przerwania od Myszy. 
 * Zbiera wektor 3-częściowy do jednej puli i uwalnia dopiero gdy otrzyma całość
 */
extern "C" void ObslugaPrzerwaniaMyszy() {
    uint8_t d = wejscie_port_bajt(0x60);
    
    // Synchronizacja: Pierwszy bajt zawsze musi mieć bit 3 ustawiony na 1
    if (licznik_pakietu == 0 && !(d & 0x08)) {
        // Zły pakiet - zwalniamy przerwanie, ale nie zapisujemy
        if(baza_lapic_wirtualna) baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
        return;
    }

    pakiety[licznik_pakietu] = d;
    licznik_pakietu++;

    if (licznik_pakietu >= 3) {
        // Mamy pełne 3 bajty! Dekodujemy logikę.
        bool l_klik = pakiety[0] & 0x01;
        bool p_klik = pakiety[0] & 0x02;

        // Rozszerzanie znaku (Sign Extension) dla osi X i Y
        int delta_x = pakiety[1];
        if (pakiety[0] & 0x10) delta_x |= 0xFFFFFF00; // Jeśli bit 4=1, liczba jest ujemna

        int delta_y = pakiety[2];
        if (pakiety[0] & 0x20) delta_y |= 0xFFFFFF00; // Jeśli bit 5=1, liczba jest ujemna

        if (l_klik) {
            wypisz_na_ekranie("Mysz: Lewy klik         ");
        } else if (p_klik) {
            wypisz_na_ekranie("Mysz: Prawy klik        ");
        } else if (delta_x != 0 || delta_y != 0) {
            // Wypisujemy z dużą ilością spacji, aby nadpisać "stare" komunikaty na żółtym pasku
            wypisz_na_ekranie("Mysz: Przesuniecie      ");
        }

        licznik_pakietu = 0; // Reset na poczet następnej ramki
    }

    // KRYTYCZNE: Powiedz LAPIC, że obsłużyłeś przerwanie, by wysłał kolejne!
    if(baza_lapic_wirtualna) {
        baza_lapic_wirtualna[LAPIC_EOI_OFFSET / 4] = 0;
    }
}