#include "e1000.h"
#include "pamiec.h"

extern void WypiszLog(const char* tekst);
extern "C" uint32_t pci_odczytaj_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
extern "C" void pci_zapisz_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data);

// Zewnętrzna funkcja z naszego nowego stosu sieciowego
extern void obsluz_pakiet_sieciowy(uint8_t* pakiet, uint16_t dlugosc);

// Pomocnicze funkcje tekstowe
static void ZlaczStringi(char* cel, const char* s1, const char* s2) {
    int i = 0, j = 0;
    while(s1 && s1[j]) cel[i++] = s1[j++];
    j = 0;
    while(s2 && s2[j]) cel[i++] = s2[j++];
    cel[i] = '\0';
}
static void HexDoStr(uint8_t val, char* buf) {
    const char* hex = "0123456789ABCDEF";
    buf[0] = hex[(val >> 4) & 0xF];
    buf[1] = hex[val & 0xF];
    buf[2] = '\0';
}

static volatile uint32_t* e1000_mmio_baza = nullptr;
static uint8_t mac_adres[6];

// Struktury DMA (Deskryptory Transmisji i Odbioru)
struct e1000_rx_desc {
    uint64_t adres;
    uint16_t dlugosc;
    uint16_t suma_kontrolna;
    uint8_t  status;
    uint8_t  bledy;
    uint16_t specjalne;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t adres;
    uint16_t dlugosc;
    uint8_t  cso;
    uint8_t  komenda;
    uint8_t  status;
    uint8_t  css;
    uint16_t specjalne;
} __attribute__((packed));

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

static struct e1000_rx_desc* rx_descs;
static struct e1000_tx_desc* tx_descs;
static uint16_t rx_aktualny = 0;
static uint16_t tx_aktualny = 0;

static inline void zapisz_rejestr(uint16_t offset, uint32_t wartosc) {
    e1000_mmio_baza[offset / 4] = wartosc;
}
static inline uint32_t czytaj_rejestr(uint16_t offset) {
    return e1000_mmio_baza[offset / 4];
}

uint8_t* pobierz_mac_adres() { return mac_adres; }

extern "C" void inicjalizuj_e1000() {
    WypiszLog("[E1000] Szukam karty sieciowej Intel PRO/1000...");
    uint32_t pci_bar0 = 0;
    
    uint16_t bus = 0, slot = 0; 
    bool znaleziono = false;

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint32_t id = pci_odczytaj_dword((uint8_t)bus, (uint8_t)slot, 0, 0);
            if ((id & 0xFFFF) == 0x8086 && (id >> 16) == 0x100E) { // Wirtualna Karta QEMU E1000
                pci_bar0 = pci_odczytaj_dword((uint8_t)bus, (uint8_t)slot, 0, 0x10);
                znaleziono = true; break;
            }
        }
        if (znaleziono) break;
    }

    if (!znaleziono) { WypiszLog("[E1000] Brak wspieranej karty sieciowej."); return; }

    uint32_t cmd = pci_odczytaj_dword((uint8_t)bus, (uint8_t)slot, 0, 0x04);
    pci_zapisz_dword((uint8_t)bus, (uint8_t)slot, 0, 0x04, cmd | (1 << 2)); // Włącz Bus Mastering (DMA)

    uint64_t mmio = pci_bar0 & ~0xF;
    
    // KRYTYCZNA POPRAWKA: Mapujemy całe 128 KB (32 strony po 4 KB) przestrzeni MMIO,
    // ponieważ rejestr MAC (0x5400) leży daleko poza pierwszą stroną!
    for(int i = 0; i < 32; i++) {
        ZmapujStrone((void*)(mmio + i * 4096), (void*)(mmio + i * 4096), 0b11 | 0x10);
    }
    
    e1000_mmio_baza = (volatile uint32_t*)mmio;

    // Odczyt adresu MAC
    uint32_t mac_low = czytaj_rejestr(0x5400);  // RAL
    uint32_t mac_high = czytaj_rejestr(0x5404); // RAH
    mac_adres[0] = mac_low & 0xFF;
    mac_adres[1] = (mac_low >> 8) & 0xFF;
    mac_adres[2] = (mac_low >> 16) & 0xFF;
    mac_adres[3] = (mac_low >> 24) & 0xFF;
    mac_adres[4] = mac_high & 0xFF;
    mac_adres[5] = (mac_high >> 8) & 0xFF;

    char mac_log[64] = "[E1000] Zarejestrowano MAC: ";
    char buf[4];
    for(int i=0; i<6; i++) { HexDoStr(mac_adres[i], buf); ZlaczStringi(mac_log, mac_log, buf); if(i<5) ZlaczStringi(mac_log, mac_log, ":"); }
    WypiszLog(mac_log);

    // Inicjalizacja buforów odbiorczych (RX)
    rx_descs = (struct e1000_rx_desc*)ZaalokujRamke();
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].adres = (uint64_t)ZaalokujRamke();
        rx_descs[i].status = 0;
    }
    zapisz_rejestr(0x2800, (uint64_t)rx_descs & 0xFFFFFFFF); // RDBAL
    zapisz_rejestr(0x2804, ((uint64_t)rx_descs >> 32) & 0xFFFFFFFF); // RDBAH
    zapisz_rejestr(0x2808, E1000_NUM_RX_DESC * 16); // RDLEN
    zapisz_rejestr(0x2810, 0); // RDH
    zapisz_rejestr(0x2818, E1000_NUM_RX_DESC - 1); // RDT
    // Włącz Odbieranie: EN, BSIZE=2048, BSEX=0, BAM
    zapisz_rejestr(0x0100, (1 << 1) | (1 << 15)); // RCTL

    // Inicjalizacja buforów wysyłania (TX)
    tx_descs = (struct e1000_tx_desc*)ZaalokujRamke();
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].adres = 0;
        tx_descs[i].komenda = 0;
    }
    zapisz_rejestr(0x3800, (uint64_t)tx_descs & 0xFFFFFFFF); // TDBAL
    zapisz_rejestr(0x3804, ((uint64_t)tx_descs >> 32) & 0xFFFFFFFF); // TDBAH
    zapisz_rejestr(0x3808, E1000_NUM_TX_DESC * 16); // TDLEN
    zapisz_rejestr(0x3810, 0); // TDH
    zapisz_rejestr(0x3818, 0); // TDT
    // Włącz Wysyłanie: EN, PSP
    zapisz_rejestr(0x0400, (1 << 1) | (1 << 3)); // TCTL

    // Aktywuj Przerwania (Choć my użyjemy Pollingu dla uproszczenia w Jądrze)
    zapisz_rejestr(0x00D0, 0x1F6DC); // IMS

    WypiszLog("[E1000] Karta podniesiona. Link aktywny!");
}

extern "C" void e1000_wyslij_pakiet(void* dane, uint16_t dlugosc) {
    if (!e1000_mmio_baza) return;
    
    // Alokujemy fizyczny bufor dla paczki
    void* bufor = ZaalokujRamke();
    uint8_t* zrodlo = (uint8_t*)dane;
    uint8_t* cel = (uint8_t*)bufor;
    for(int i=0; i<dlugosc; i++) cel[i] = zrodlo[i];

    tx_descs[tx_aktualny].adres = (uint64_t)bufor;
    tx_descs[tx_aktualny].dlugosc = dlugosc;
    // CMD: EOP (End of Packet), IFCS (Insert FCS), RS (Report Status)
    tx_descs[tx_aktualny].komenda = (1 << 0) | (1 << 1) | (1 << 3);
    tx_descs[tx_aktualny].status = 0;

    uint8_t stary_tx = tx_aktualny;
    tx_aktualny = (tx_aktualny + 1) % E1000_NUM_TX_DESC;
    zapisz_rejestr(0x3818, tx_aktualny); // Aktualizuj ogon (TDT) by powiadomić kartę
    
    // Oczekiwanie na wysłanie
    while(!(tx_descs[stary_tx].status & 0xFF));
    
    // Zwalniamy RAM
    ZwolnijRamke(bufor);
}

extern "C" void e1000_obsluz_odbior() {
    if (!e1000_mmio_baza) return;
    
    // Bit 0 w statusie = 1 (Deskryptor Gotowy / Paczka dotarła)
    while (rx_descs[rx_aktualny].status & 0x1) {
        uint8_t* bufor = (uint8_t*)rx_descs[rx_aktualny].adres;
        uint16_t dlugosc = rx_descs[rx_aktualny].dlugosc;
        
        // Przekazanie paczki do analizatora sieci (siec.cpp)
        obsluz_pakiet_sieciowy(bufor, dlugosc);
        
        rx_descs[rx_aktualny].status = 0;
        zapisz_rejestr(0x2818, rx_aktualny); // RDT: Powiedz karcie, że można to nadpisać
        
        rx_aktualny = (rx_aktualny + 1) % E1000_NUM_RX_DESC;
    }
}