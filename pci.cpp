/*
 * Mechanizm: Skaner Magistrali PCI (Peripheral Component Interconnect)
 * Opis: Umożliwia odczytywanie danych konfiguracyjnych urządzeń na płycie głównej
 * (np. znajdowanie kontrolera dysków AHCI, kart sieciowych, itp.).
 */

#include <stdint.h>

// Funkcje z innych modułów jądra
extern void WypiszLog(const char* tekst);
extern "C" bool utworz_plik(const char* sciezka);
extern "C" bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc);

// Pomocnicza funkcja asemblerowa do wysyłania 32-bitowych danych na port I/O
static inline void wyjscie_port_dword(uint16_t port, uint32_t wartosc) {
    asm volatile ("outl %0, %1" : : "a"(wartosc), "Nd"(port));
}

// Pomocnicza funkcja asemblerowa do odczytywania 32-bitowych danych z portu I/O
static inline uint32_t wejscie_port_dword(uint16_t port) {
    uint32_t wartosc;
    asm volatile ("inl %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

// Główna funkcja odczytująca dane z płyty głównej (poprawnie wyeksportowana do C)
extern "C" uint32_t pci_odczytaj_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    // Struktura adresu: Bit 31 to Enable, bity 16-23 to Bus, 11-15 to Slot, 8-10 to Func, 2-7 to Offset
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    wyjscie_port_dword(0xCF8, address);
    return wejscie_port_dword(0xCFC);
}

// Pomocnicze funkcje tekstowe dla skanera
static void pci_int_do_str(int wartosc, char* bufor) {
    if (wartosc == 0) { bufor[0] = '0'; bufor[1] = '\0'; return; }
    int i = 0; char temp[16];
    while (wartosc > 0) { temp[i++] = (wartosc % 10) + '0'; wartosc /= 10; }
    int j = 0; while (i > 0) bufor[j++] = temp[--i];
    bufor[j] = '\0';
}

static void pci_str_dopisz(char* cel, const char* zrodlo) {
    int i = 0; while(cel[i] != '\0') i++;
    int j = 0; while(zrodlo[j] != '\0') cel[i++] = zrodlo[j++];
    cel[i] = '\0';
}

// Skaner magistrali zapisujący logi na dysk (KRYTYCZNE: posiada extern "C")
extern "C" void skanuj_magistrale_pci() {
    WypiszLog("[PCI] Skanowanie plyty glownej w poszukiwaniu sprzetu...");
    
    // Potężny bufor na raport tekstowy (ok. 2 KB)
    char pelny_raport[2048];
    pelny_raport[0] = '\0';

    int znaleziono = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            
            uint32_t vendor_device = pci_odczytaj_dword(bus, slot, 0, 0x00);
            uint16_t vendor = vendor_device & 0xFFFF;
            
            // 0xFFFF oznacza pusty slot
            if (vendor == 0xFFFF) continue;

            uint32_t info_klasy = pci_odczytaj_dword(bus, slot, 0, 0x08);
            uint8_t klasa    = (info_klasy >> 24) & 0xFF;
            uint8_t podklasa = (info_klasy >> 16) & 0xFF;

            // Tworzenie pojedynczej linii logu dla znalezionego urządzenia
            char log_msg[128]; log_msg[0] = '\0';
            pci_str_dopisz(log_msg, "Zlacze ");
            char nr_bus[8]; pci_int_do_str(bus, nr_bus); pci_str_dopisz(log_msg, nr_bus);
            pci_str_dopisz(log_msg, ":");
            char nr_slot[8]; pci_int_do_str(slot, nr_slot); pci_str_dopisz(log_msg, nr_slot);
            pci_str_dopisz(log_msg, " -> ");

            if (klasa == 0x01) {
                if (podklasa == 0x01) pci_str_dopisz(log_msg, "Stary kontroler dyskow IDE\n");
                else if (podklasa == 0x06) pci_str_dopisz(log_msg, "Nowoczesny kontroler AHCI/SATA\n");
                else if (podklasa == 0x08) pci_str_dopisz(log_msg, "Superszybki dysk NVMe (PCIe)\n");
                else pci_str_dopisz(log_msg, "Inny kontroler dyskow\n");
            } 
            else if (klasa == 0x02) pci_str_dopisz(log_msg, "Karta Sieciowa (Ethernet/Wi-Fi)\n");
            else if (klasa == 0x03) pci_str_dopisz(log_msg, "Karta Graficzna (VGA/3D)\n");
            else if (klasa == 0x0C && podklasa == 0x03) pci_str_dopisz(log_msg, "Kontroler portow USB\n");
            else if (klasa == 0x06) pci_str_dopisz(log_msg, "Mostek systemowy (Bridge)\n");
            else pci_str_dopisz(log_msg, "Nierozpoznany sprzet\n");

            // Dopisanie linii do globalnego raportu
            pci_str_dopisz(pelny_raport, log_msg);
            znaleziono++;
        }
    }

    if (znaleziono > 0) {
        WypiszLog("[PCI] Skanowanie zakonczone. Zapisuje log do /logi/pci.txt");
        
        utworz_plik("/logi/pci.txt");
        
        int len = 0;
        while(pelny_raport[len] != '\0') len++;
        
        zapisz_do_pliku("/logi/pci.txt", pelny_raport, len);
    } else {
        WypiszLog("[PCI] UWAGA: Nie wykryto zadnych urzadzen PCI!");
    }
}