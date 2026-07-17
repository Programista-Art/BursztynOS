/*
 * Mechanizm: Sterownik Klawiatury PS/2
 * Opis: Pobiera binarne Scancody z portu 0x60 kontrolera i4240 na płycie głównej
 * w standardzie Scancode Set 1. Filtruje flagi zwolnienia przycisku (Break codes) 
 * by wypisać odpowiedni znak uderzenia w klawisz (Make code).
 */

#include <stdint.h>

// Pomocnicza funkcja C przestrzeni I/O, zakładamy że użytkownik podepnie ją w linkingu z poprzedniego etapu
static inline uint8_t wejscie_port_bajt(uint16_t port) {
    uint8_t wartosc;
    asm volatile ("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

// Zewnętrzna funkcja z kernel.cpp z Etapu 1, użyta do generowania potwierdzenia wizualnego (ECHO)
extern "C" void wypisz_na_ekranie(const char* buf);

// Tablica tłumaczeń (Lookup Table) Scancode Set 1 ograniczona do podstawowych liter amerykańskiej klawiatury.
// Mapowanie zakłada tryb pisania po polsku bez znaków diakrytycznych.
const char mapa_klawiatury_set1[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', // 0 - 9
    '9', '0', '-', '=', '\b', '\t','q', 'w', 'e', 'r', // 10 - 19
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,   // 20 - 29 (28 to Enter)
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', // 30 - 39
    '\'','`', 0,   '\\','z', 'x', 'c', 'v', 'b', 'n', // 40 - 49
    'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   // 50 - 59 (57 to Spacja)
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 60 - 69
    0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   // 70 - 79
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 80 - 89
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 90 - 99
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 100 - 109
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // 110 - 119
    0,   0,   0,   0,   0,   0,   0,   0             // 120 - 127
};

// Globalny bufor wyświetlanego ciągu
char bufor_klawiatury[80] = {0};
int pozycja_kursora = 0;

/*
 * Obsługa Przerwania od Klawiatury. 
 * Powinna być wywoływana za każdym razem, gdy użytkownik skonfiguruje IDT pod wektor np. 33 (IRQ1)
 */
extern "C" void ObslugaPrzerwaniaKlawiatury() {
    // 1. Zczytaj wygenerowany Scancode ze sprzętowego rejestru przydzielonego do odbioru
    uint8_t kod_klawisza = wejscie_port_bajt(0x60);

    // 2. Najbardziej znaczący bit (bit nr 7 = 0x80) określa czy klawisz został wciśnięty czy puszczony
    bool klawisz_zwolniony = (kod_klawisza & 0x80);

    if (!klawisz_zwolniony) {
        // Usuwamy potencjalne modyfikatory wykraczające z tablicy 128 wpisów 
        // (chociaż Scancode Set 1 mieści się w standardzie do bajtu 0x58 bez prefiksów 0xE0)
        char wpisany_znak = mapa_klawiatury_set1[kod_klawisza & 0x7F];

        if (wpisany_znak != 0) {
            // Echo: Zaktualizuj bufor i wywołaj komendę graficzną
            if (wpisany_znak == '\b' && pozycja_kursora > 0) {
                pozycja_kursora--;
                bufor_klawiatury[pozycja_kursora] = ' '; // "Wymaż" graficznie
            } 
            else if (wpisany_znak == '\n') {
                for(int i=0; i<80; i++) bufor_klawiatury[i] = ' '; // Czyszczenie
                pozycja_kursora = 0;
            }
            else if (pozycja_kursora < 79 && wpisany_znak != '\b') {
                bufor_klawiatury[pozycja_kursora] = wpisany_znak;
                pozycja_kursora++;
            }
            
            // Koniec linii, aby print nadpisał starą linijkę
            bufor_klawiatury[79] = '\0';
            
            wypisz_na_ekranie(bufor_klawiatury);
        }
    }
}
