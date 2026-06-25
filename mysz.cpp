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

    // 1. Zezwól na obsługę urządzenia pomocniczego przez wysłanie komendy do rejestru statusu
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0xA8);

    // 2. Wczytaj obecny status konfiguracyjny (Bajt informacyjny 0x20, Odbierz 0x60)
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0x20);
    MyszCzekajNaOdczyt();
    status_kontrolera = wejscie_port_bajt(0x60);

    // 3. Włącz sprzętowe przerwania kontrolera dla wektora Myszy (Bit nr 1 = 1)
    status_kontrolera |= 2;
    // Zapobiegnij deaktywacji zegara podrzędnego (Oczyść bit nr 5)
    status_kontrolera &= ~0x20;

    // 4. Zapisz z powrotem zaktualizowany konfigurator (Komenda zapisu to 0x60)
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0x60);
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x60, status_kontrolera);

    // 5. Powiedz myszy by domyślnie używała jej wewnętrznego protokołu. Komendy dla myszy przesyłamy poprzedzając je bajtem 0xD4 
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x60, 0xF6); // Komenda Reset / Przywróć Ustawienia domyślne dla Myszy
    MyszCzekajNaOdczyt();
    wejscie_port_bajt(0x60);       // Odbierz potwierdzenie ACK (0xFA)
    
    // 6. Nakaz myszy by uruchomiła ciągły przepływ i nasłuch pakietów (Enable Data Reporting)
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x64, 0xD4);
    MyszCzekajNaZapis();
    wyjscie_port_bajt(0x60, 0xF4); // Komenda Włączenia nasłuchu ruchu
    MyszCzekajNaOdczyt();
    wejscie_port_bajt(0x60);       // Odbierz potwierdzenie ACK (0xFA)
}

// Zmienne gromadzące stan cyklu pakietowania
uint8_t licznik_pakietu = 0;
uint8_t pakiety[3];

extern "C" void WypiszNaEkranie(const char* buf);
char temp_string[80] = "Mysz zostala uzyta.";

/*
 * Obsługa Przerwania od Myszy. 
 * Zbiera wektor 3-częściowy do jednej puli i uwalnia dopiero gdy otrzyma całość
 */
extern "C" void ObslugaPrzerwaniaMyszy() {
    // Port 0x60 wyrzuca zbuforowane bajty. 
    // Trzeba upewnić się, czy coś w ogóle przyszło lub kontroler się odblokował, 
    // by zapobiec fałszywym stanom zsynchronizowanych kolejek.
    uint8_t d = wejscie_port_bajt(0x60);
    
    // Zapewnienie synchronizacji paczki: Pakiet początkowy (część 1) zazwyczaj posiada zawsze włączony bit 3
    if (licznik_pakietu == 0 && !(d & 0x08)) {
        return; // To jest zły pakiet, pomijamy synchronizację
    }

    pakiety[licznik_pakietu] = d;
    licznik_pakietu++;

    if (licznik_pakietu >= 3) {
        // Posiadamy całą wiadomość 3 bajtową!
        
        // pakiety[0]: Stan kliknięcia, Zwrot X (Bit 4), Zwrot Y (Bit 5), Przepełnienie
        // pakiety[1]: Ruch na osi X (8 bitowe)
        // pakiety[2]: Ruch na osi Y (8 bitowe)
        bool l_klik = pakiety[0] & 0x01;
        bool p_klik = pakiety[0] & 0x02;

        if (l_klik) {
            WypiszNaEkranie("Mysz: Lewy przycisk");
        } else if (p_klik) {
            WypiszNaEkranie("Mysz: Prawy przycisk");
        } else {
            // Wypisz jako Echo by upewnić się że funkcja odczytuje relatywne osie
            WypiszNaEkranie(temp_string);
        }

        // Zakończenie cyklu (od nowa)
        licznik_pakietu = 0;
    }
}
