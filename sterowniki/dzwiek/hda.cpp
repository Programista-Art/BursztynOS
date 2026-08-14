/*
 * Mechanizm: Sterownik Intel High Definition Audio (HDA)
 * Opis: Nowoczesny standard audio. Korzysta z MMIO, DMA Streams i Immediate Commands.
 */

#include "hda.h"
#include "../../pamiec.h"
#include <stdint.h>

void wypisz_log(const char* tekst);

extern "C" {
    uint32_t pci_odczytaj_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
    void pci_zapisz_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t wartosc);
}

// Funkcje dostępu do pamięci MMIO
static inline void mmio_zapisz_bajt(uint64_t adres, uint8_t wartosc) { *(volatile uint8_t*)adres = wartosc; }
static inline void mmio_zapisz_word(uint64_t adres, uint16_t wartosc) { *(volatile uint16_t*)adres = wartosc; }
static inline void mmio_zapisz_dword(uint64_t adres, uint32_t wartosc) { *(volatile uint32_t*)adres = wartosc; }
static inline uint8_t mmio_odczytaj_bajt(uint64_t adres) { return *(volatile uint8_t*)adres; }
static inline uint16_t mmio_odczytaj_word(uint64_t adres) { return *(volatile uint16_t*)adres; }
static inline uint32_t mmio_odczytaj_dword(uint64_t adres) { return *(volatile uint32_t*)adres; }

static uint64_t hda_mmio_base = 0;
static uint64_t hda_stream_base = 0;
static bool hda_gotowy = false;

static HDA_BDL_Wpis hda_bdl[HDA_MAX_DESKRYPTORY] __attribute__((aligned(128)));
static uint8_t hda_bufory[HDA_ILOSC_BUFOROW][HDA_ROZMIAR_BUFORA] __attribute__((aligned(4096)));

static inline uint32_t virt_to_phys(const void* v) {
    return (uint32_t)(uintptr_t)v;
}

// Mechanizm Immediate Command do przesyłania poleceń do kodeka bez użycia skomplikowanego CORB/RIRB
static uint32_t hda_wyslij_komende(uint32_t komenda) {
    if (!hda_mmio_base) return 0;
    
    // Czekamy aż magistrala komunikacyjna będzie wolna (Bit 0 w ICS = 0)
    while (mmio_odczytaj_word(hda_mmio_base + 0x68) & 0x0001);
    
    // Wpisujemy polecenie
    mmio_zapisz_dword(hda_mmio_base + 0x60, komenda);
    
    // Zlecamy wysłanie (Bit 0 w ICS = 1)
    mmio_zapisz_word(hda_mmio_base + 0x68, mmio_odczytaj_word(hda_mmio_base + 0x68) | 0x0001);
    
    // Czekamy na przetworzenie przez kodek
    while (mmio_odczytaj_word(hda_mmio_base + 0x68) & 0x0001);
    
    // Zwracamy odpowiedź kodeka
    return mmio_odczytaj_dword(hda_mmio_base + 0x64);
}

bool inicjalizuj_hda() {
    wypisz_log("[HDA] Skanowanie magistrali PCI w poszukiwaniu Intel HD Audio...");
    
    uint8_t a_bus = 0, a_slot = 0, a_func = 0;
    bool znaleziono = false;
    
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_odczytaj_dword(bus, slot, 0, 0);
            if (vendor_device == 0xFFFFFFFF) continue;
            
            uint32_t class_rev = pci_odczytaj_dword(bus, slot, 0, 8);
            uint8_t klasa = (class_rev >> 24) & 0xFF;
            uint8_t podklasa = (class_rev >> 16) & 0xFF;
            
            if (klasa == 0x04 && podklasa == 0x03) {
                a_bus = bus; a_slot = slot; a_func = 0;
                znaleziono = true;
                break;
            }
        }
        if (znaleziono) break;
    }

    if (!znaleziono) {
        wypisz_log("[HDA] Blad: Nie znaleziono kompatybilnej karty Intel HD Audio!");
        return false;
    }

    // Włączamy Bus Mastering i Memory Space
    uint32_t cmd = pci_odczytaj_dword(a_bus, a_slot, a_func, 0x04);
    pci_zapisz_dword(a_bus, a_slot, a_func, 0x04, cmd | 0x06);

    // Odczytujemy bazę MMIO z BAR0
    uint32_t bar0 = pci_odczytaj_dword(a_bus, a_slot, a_func, 0x10);
    uint32_t bar1 = 0;
    if ((bar0 & 0x06) == 0x04) { // Jeśli to 64-bitowy BAR
        bar1 = pci_odczytaj_dword(a_bus, a_slot, a_func, 0x14);
    }
    
    hda_mmio_base = ((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0);
    
    if (hda_mmio_base == 0) {
        wypisz_log("[HDA] Blad: Nieprawidlowy adres MMIO karty!");
        return false;
    }

    // MAPOWANIE PAMIĘCI (Zabezpieczenie przez czarnym ekranem / Page Faultem na Cache)
    // 16 KB (4 strony) wystarczy z nawiązką na wszystkie rejestry
    for(int i = 0; i < 4; i++) {
        ZmapujStrone((void*)(hda_mmio_base + i*4096), (void*)(hda_mmio_base + i*4096), 0b11 | 0x10 | 0x08);
    }

    // --- RESET KONTROLERA (GCTL) ---
    mmio_zapisz_dword(hda_mmio_base + 0x08, 0); // Wyzeruj CRST
    for(volatile int i=0; i<100000; i++);
    mmio_zapisz_dword(hda_mmio_base + 0x08, 1); // Włącz CRST
    int timeout = 1000000;
    while (!(mmio_odczytaj_dword(hda_mmio_base + 0x08) & 1) && timeout-- > 0);
    
    if (timeout <= 0) {
        wypisz_log("[HDA] Blad: Kontroler HDA nie odpowiedzial na reset!");
        return false;
    }
    
    // Szukamy strumienia Output (GCAP rejestr)
    uint16_t gcap = mmio_odczytaj_word(hda_mmio_base + 0x00);
    uint8_t iss = (gcap >> 8) & 0x0F; // Liczba Input Streams
    
    // Strumienie wyjściowe (Output Streams) zaczynają się od indeksu ISS
    hda_stream_base = hda_mmio_base + 0x80 + (iss * 0x20);

    // --- PODSTAWOWA INICJALIZACJA KODEKA QEMU (Unmute i Setup) ---
    hda_wyslij_komende(0x00220011); // N02: Format (48kHz, 16-bit, 2-kanały)
    hda_wyslij_komende(0x00270610); // N02: Stream ID 1
    hda_wyslij_komende(0x0033B07F); // N03: Unmute (Odblokuj wzmacniacz wyjscia)
    hda_wyslij_komende(0x00370740); // N03: Ustaw Pin jako wyjście
    
    hda_gotowy = true;
    wypisz_log("[HDA] Nowoczesny kontroler Intel HD Audio gotowy do pracy!");
    return true;
}

bool hda_test_ton(uint32_t czestotliwosc_hz, uint32_t czas_ms) {
    if (!hda_gotowy) return false;

    const uint32_t sample_rate = 48000;
    uint32_t ilosc_probek = (sample_rate * czas_ms) / 1000;
    uint32_t rozmiar = ilosc_probek * 4;

    uint32_t max_rozmiar = HDA_ILOSC_BUFOROW * HDA_ROZMIAR_BUFORA;
    if (rozmiar > max_rozmiar) rozmiar = max_rozmiar;

    // Prosta tablica fali dla tonu testowego
    static const int16_t sine_tab[256] = {
            0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
         6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
        12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
        18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
        23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
        27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
        30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
        32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
        32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
        32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
        30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683,
        27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
        23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
        18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
        12539, 11793, 11039, 10278,  9512,  8739,  7962,  7179,
         6393,  5602,  4808,  4011,  3212,  2410,  1608,   804,
            0,  -804, -1608, -2410, -3212, -4011, -4808, -5602,
        -6393, -7179, -7962, -8739, -9512,-10278,-11039,-11793,
       -12539,-13279,-14010,-14732,-15446,-16151,-16846,-17530,
       -18204,-18868,-19519,-20159,-20787,-21403,-22005,-22594,
       -23170,-23731,-24279,-24811,-25329,-25832,-26319,-26790,
       -27245,-27683,-28105,-28510,-28898,-29268,-29621,-29956,
       -30273,-30571,-30852,-31113,-31356,-31580,-31785,-31971,
       -32137,-32285,-32412,-32521,-32609,-32678,-32728,-32757,
       -32767,-32757,-32728,-32678,-32609,-32521,-32412,-32285,
       -32137,-31971,-31785,-31580,-31356,-31113,-30852,-30571,
       -30273,-29956,-29621,-29268,-28898,-28510,-28105,-27683,
       -27245,-26790,-26319,-25832,-25329,-24811,-24279,-23731,
       -23170,-22594,-22005,-21403,-20787,-20159,-19519,-18868,
       -18204,-17530,-16846,-16151,-15446,-14732,-14010,-13279,
       -12539,-11793,-11039,-10278, -9512, -8739, -7962, -7179,
        -6393, -5602, -4808, -4011, -3212, -2410, -1608,  -804
    };

    uint8_t* bufor = (uint8_t*)hda_bufory;
    uint32_t krok = (czestotliwosc_hz * 65536) / sample_rate;
    if (krok == 0) krok = 1;

    uint32_t faza = 0;
    for (uint32_t i = 0; i < rozmiar / 4; i++) {
        uint8_t indeks = (uint8_t)(faza >> 8); 
        int16_t wartosc = sine_tab[indeks];
        int16_t* probka = (int16_t*)(bufor + i * 4);
        probka[0] = wartosc; 
        probka[1] = wartosc; 
        faza += krok;
        if (faza >= 65536) faza -= 65536; 
    }

    // --- Uruchomienie Strumienia DMA ---
    mmio_zapisz_bajt(hda_stream_base + 0x00, 0); // Zatrzymaj strumień (RUN = 0)
    mmio_zapisz_bajt(hda_stream_base + 0x03, 0x1C); // Reset statusów przerwań
    
    // Budujemy Listę BDL
    uint32_t ilosc_buforow = rozmiar / HDA_ROZMIAR_BUFORA;
    if (rozmiar % HDA_ROZMIAR_BUFORA > 0) ilosc_buforow++;

    uint32_t pozostalo = rozmiar;
    for (uint32_t i = 0; i < ilosc_buforow; i++) {
        hda_bdl[i].adres_dolny = virt_to_phys(hda_bufory[i]);
        hda_bdl[i].adres_gorny = 0;
        uint32_t len = (pozostalo > HDA_ROZMIAR_BUFORA) ? HDA_ROZMIAR_BUFORA : pozostalo;
        hda_bdl[i].dlugosc = len;
        
        // KRYTYCZNE ZABEZPIECZENIE: Całkowicie wyłączamy IOC (Interrupt On Completion).
        // Zapobiega to rzucaniu sprzętowych wyjątków (BSOD) na koniec odtwarzania dźwięku!
        hda_bdl[i].flagi = 0; 
        
        pozostalo -= len;
    }

    // Ładujemy parametry
    mmio_zapisz_dword(hda_stream_base + 0x18, virt_to_phys(hda_bdl)); // BDL Lower
    mmio_zapisz_dword(hda_stream_base + 0x1C, 0);                     // BDL Upper
    mmio_zapisz_word(hda_stream_base + 0x0C, ilosc_buforow - 1);      // LVI (Last Valid Index)
    mmio_zapisz_word(hda_stream_base + 0x12, 0x0011);                 // 48kHz, 16-bit, Stereo
    
    // Ustawiamy Stream ID = 1 i START!
    mmio_zapisz_bajt(hda_stream_base + 0x02, (1 << 4));
    mmio_zapisz_bajt(hda_stream_base + 0x00, 0x01); // RUN = 1

    return true;
}

void hda_stop() {
    if (hda_gotowy && hda_stream_base) {
        mmio_zapisz_bajt(hda_stream_base + 0x00, 0); // Zatrzymaj RUN
    }
}