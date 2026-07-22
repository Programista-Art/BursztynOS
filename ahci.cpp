/*
 * Mechanizm: Sterownik Pamięci Masowej AHCI (SATA / SSD)
 * Opis: Konfiguruje kontroler dysków, organizuje obszary pamięci RAM (Listy Komend, FIS)
 * oraz obsługuje bezpośredni transfer danych do dysków z użyciem protokołu SATA.
 */

#include "ahci.h"
#include "pamiec.h"

// Funkcje dostarczane przez inne części Jądra Bursztyna
extern "C" uint32_t pci_odczytaj_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
extern void WypiszLog(const char* tekst);
void ZlaczStringi(char* cel, const char* str1, const char* str2, const char* str3);
void UIntToStr(uint64_t wartosc, char* bufor);

// ---------------------------------------------------------
// FUNKCJE ZAPISU DO MAGISTRALI PCI
// ---------------------------------------------------------
static inline void wyjscie_port_dword(uint16_t port, uint32_t wartosc) {
    asm volatile ("outl %0, %1" : : "a"(wartosc), "Nd"(port));
}

void pci_zapisz_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data) {
    uint32_t adres = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    wyjscie_port_dword(0xCF8, adres);
    wyjscie_port_dword(0xCFC, data);
}

// ---------------------------------------------------------
// POLSKIE STRUKTURY KONTROLERA AHCI
// ---------------------------------------------------------

struct struktura_portu_ahci {
    uint32_t adres_listy_komend_dolny;
    uint32_t adres_listy_komend_gorny;
    uint32_t adres_fis_dolny;
    uint32_t adres_fis_gorny;
    uint32_t status_przerwan;
    uint32_t wlaczenie_przerwan;
    uint32_t status_i_sterowanie;
    uint32_t zarezerwowane0;
    uint32_t status_zadania;
    uint32_t sygnatura;
    uint32_t status_sata;
    uint32_t sterowanie_sata;
    uint32_t blad_sata;
    uint32_t aktywne_sata;
    uint32_t powiadomienia_komend;
    uint32_t powiadomienia_sata;
    uint32_t fbs;
    uint32_t zarezerwowane1[11];
    uint32_t specyficzne_dla_dostawcy[4];
} __attribute__((packed));

struct pamiec_kontrolera_ahci {
    uint32_t mozliwosci_hosta;
    uint32_t globalne_sterowanie;
    uint32_t status_przerwan;
    uint32_t zaimplementowane_porty;
    uint32_t wersja;
    uint32_t sterowanie_ccc;
    uint32_t porty_ccc;
    uint32_t lokacja_em;
    uint32_t sterowanie_em;
    uint32_t mozliwosci_hosta_rozszerzone;
    uint32_t kontrola_przejecia;
    uint8_t  zarezerwowane[0x74];
    uint8_t  specyficzne_dla_dostawcy[0x60];
    struktura_portu_ahci porty[32]; 
} __attribute__((packed));

struct naglowek_komendy_ahci {
    uint8_t  dlugosc_fisu_i_kierunek; 
    uint8_t  status_resetu;
    uint16_t ilosc_wpisow_deskryptora;
    uint32_t licznik_przeslanych_bajtow;
    uint32_t adres_tablicy_dolny;
    uint32_t adres_tablicy_gorny;
    uint32_t zarezerwowane[4];
} __attribute__((packed));

struct deskryptor_regionu_fizycznego_ahci {
    uint32_t adres_bazowy_dolny;
    uint32_t adres_bazowy_gorny;
    uint32_t zarezerwowane0;
    uint32_t rozmiar_bajtowy_oraz_flagi; 
} __attribute__((packed));

struct tablica_komend_ahci {
    uint8_t  ramka_polecenia_fis[64];
    uint8_t  ramka_atapi[16];
    uint8_t  zarezerwowane[48];
    deskryptor_regionu_fizycznego_ahci wpisy_pamieci[1]; 
} __attribute__((packed));

struct struktura_fisu_host_dysk_ahci {
    uint8_t  typ_fisu;        
    uint8_t  flagi_portu;     
    uint8_t  komenda;         
    uint8_t  cecha_dolna;
    
    uint8_t  lba0;            
    uint8_t  lba1;            
    uint8_t  lba2;            
    uint8_t  urzadzenie;      
    
    uint8_t  lba3;            
    uint8_t  lba4;            
    uint8_t  lba5;            
    uint8_t  cecha_gorna;
    
    uint8_t  licznik_sektorow_dolny;
    uint8_t  licznik_sektorow_gorny;
    uint8_t  zarezerwowane_iso;
    uint8_t  sterowanie;
    
    uint32_t zarezerwowane;
} __attribute__((packed));


// --- KRYTYCZNA ZMIANA: Dodano VOLATILE do wskaźnika bazy! ---
// Informuje kompilator (-O2), że rejestry kontrolera mogą zmieniać się same z siebie.
static volatile pamiec_kontrolera_ahci* adres_bazy_ahci = nullptr;
static int glowny_port_dysku = -1;

static uint8_t dma_lista_komend[1024] __attribute__((aligned(1024)));
static uint8_t dma_bufor_fis[256]     __attribute__((aligned(256)));
static uint8_t dma_tablica_komend[4096] __attribute__((aligned(128)));

static uint8_t dma_bufor_danych[16384] __attribute__((aligned(4096)));


// --- POMOCNICY ---
static void wyzeruj_pamiec(void* wskaznik, uint32_t bajty) {
    uint8_t* pamiec = (uint8_t*)wskaznik;
    for(uint32_t i = 0; i < bajty; i++) pamiec[i] = 0;
}

// KRYTYCZNA ZMIANA: port stał się volatile
static void zatrzymaj_silnik_komend(volatile struktura_portu_ahci* port) {
    port->status_i_sterowanie &= ~(1 << 0); // Bit ST
    port->status_i_sterowanie &= ~(1 << 4); // Bit FRE
    
    while (true) {
        if (port->status_i_sterowanie & (1 << 14)) continue; // Bit FR
        if (port->status_i_sterowanie & (1 << 15)) continue; // Bit CR
        break;
    }
}

static void uruchom_silnik_komend(volatile struktura_portu_ahci* port) {
    while (port->status_i_sterowanie & (1 << 15)); 
    port->status_i_sterowanie |= (1 << 4); // Bit FRE
    port->status_i_sterowanie |= (1 << 0); // Bit ST
}

// --- WYSZUKIWANIE I INICJALIZACJA ---
extern "C" void inicjalizuj_kontroler_ahci() {
    WypiszLog("[AHCI] Szukam kontrolera pamieci masowej na magistrali PCI...");
    uint32_t adres_fizyczny_abar = 0;
    
    uint8_t final_bus = 0, final_slot = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor = pci_odczytaj_dword(bus, slot, 0, 0x00) & 0xFFFF;
            if (vendor == 0xFFFF) continue;
            
            uint32_t klasa_podklasa = pci_odczytaj_dword(bus, slot, 0, 0x08);
            uint8_t klasa = (klasa_podklasa >> 24) & 0xFF;
            uint8_t podklasa = (klasa_podklasa >> 16) & 0xFF;
            
            if (klasa == 0x01 && podklasa == 0x06) {
                uint32_t bar5 = pci_odczytaj_dword(bus, slot, 0, 0x24);
                adres_fizyczny_abar = bar5 & 0xFFFFFFF0; 
                final_bus = bus; final_slot = slot;
                break;
            }
        }
        if (adres_fizyczny_abar != 0) break;
    }
    
    if (adres_fizyczny_abar == 0) {
        WypiszLog("[AHCI] Nie znaleziono kontrolera dyskow!");
        return;
    }

    uint32_t komenda_pci = pci_odczytaj_dword(final_bus, final_slot, 0, 0x04);
    if ((komenda_pci & 0x06) != 0x06) {
        pci_zapisz_dword(final_bus, final_slot, 0, 0x04, komenda_pci | 0x06);
        WypiszLog("[AHCI] Aktywowano prawa zapisu do RAM (PCI Bus Mastering).");
    }
    
    ZmapujStrone((void*)((uint64_t)adres_fizyczny_abar), (void*)((uint64_t)adres_fizyczny_abar), 0b11 | 0x10);
    adres_bazy_ahci = (volatile pamiec_kontrolera_ahci*)((uint64_t)adres_fizyczny_abar);
    
    WypiszLog("[AHCI] Kontroler aktywny. Skanuje wewnetrzne porty SATA...");
    
    adres_bazy_ahci->globalne_sterowanie |= (1 << 31); // Włącz tryb AHCI (GHC.AE)
    
    uint32_t aktywne_porty = adres_bazy_ahci->zaimplementowane_porty;
    
    for (int i = 0; i < 32; i++) {
        if (aktywne_porty & (1 << i)) {
            volatile struktura_portu_ahci* port = &adres_bazy_ahci->porty[i];
            
            if ((port->status_sata & 0x0F) == 3 && port->sygnatura == 0x00000101) { 
                
                zatrzymaj_silnik_komend(port);
                
                wyzeruj_pamiec(dma_lista_komend, 1024);
                wyzeruj_pamiec(dma_bufor_fis, 256);
                wyzeruj_pamiec(dma_tablica_komend, 4096);
                
                port->adres_listy_komend_dolny = (uint64_t)&dma_lista_komend & 0xFFFFFFFF;
                port->adres_listy_komend_gorny = ((uint64_t)&dma_lista_komend >> 32) & 0xFFFFFFFF;
                
                port->adres_fis_dolny = (uint64_t)&dma_bufor_fis & 0xFFFFFFFF;
                port->adres_fis_gorny = ((uint64_t)&dma_bufor_fis >> 32) & 0xFFFFFFFF;
                
                naglowek_komendy_ahci* naglowki = (naglowek_komendy_ahci*)&dma_lista_komend;
                naglowki[0].ilosc_wpisow_deskryptora = 1;
                naglowki[0].adres_tablicy_dolny = (uint64_t)&dma_tablica_komend & 0xFFFFFFFF;
                naglowki[0].adres_tablicy_gorny = ((uint64_t)&dma_tablica_komend >> 32) & 0xFFFFFFFF;
                
                uruchom_silnik_komend(port);
                
                if (glowny_port_dysku == -1) glowny_port_dysku = i;
                
                char log[64]; char nr[4]; UIntToStr(i, nr);
                ZlaczStringi(log, "[AHCI] Przypisano i aktywowano dysk SATA na porcie ", nr, ".");
                WypiszLog(log);
            }
        }
    }
}

// --- FAKTYCZNY ODCZYT/ZAPIS (Z UŻYCIEM BOUNCE BUFFER) ---
static bool operacja_dysku_ahci(uint64_t lba, uint32_t ilosc_sektorow, void* wirtualny_bufor, bool czy_zapisz) {
    if (glowny_port_dysku == -1) return false;
    if (ilosc_sektorow > 32) return false; 

    // KRYTYCZNA ZMIANA: port jest VOLATILE, wymuszając fizyczny odczyt/zapis za każdym razem!
    volatile struktura_portu_ahci* port = &adres_bazy_ahci->porty[glowny_port_dysku];
    port->status_przerwan = 0xFFFFFFFF; 
    
    naglowek_komendy_ahci* naglowek = (naglowek_komendy_ahci*)&dma_lista_komend;
    naglowek[0].dlugosc_fisu_i_kierunek = (sizeof(struktura_fisu_host_dysk_ahci) / sizeof(uint32_t)) | (czy_zapisz ? (1 << 6) : 0);
    naglowek[0].licznik_przeslanych_bajtow = 0;
    
    tablica_komend_ahci* tablica = (tablica_komend_ahci*)&dma_tablica_komend;
    wyzeruj_pamiec(tablica->ramka_polecenia_fis, 64);
    
    uint32_t bajty_transferu = ilosc_sektorow * 512;
    uint8_t* ptr_user = (uint8_t*)wirtualny_bufor;
    
    if (czy_zapisz) {
        for(uint32_t i=0; i<bajty_transferu; i++) dma_bufor_danych[i] = ptr_user[i];
    }

    tablica->wpisy_pamieci[0].adres_bazowy_dolny = (uint64_t)&dma_bufor_danych & 0xFFFFFFFF;
    tablica->wpisy_pamieci[0].adres_bazowy_gorny = ((uint64_t)&dma_bufor_danych >> 32) & 0xFFFFFFFF;
    tablica->wpisy_pamieci[0].rozmiar_bajtowy_oraz_flagi = bajty_transferu - 1;

    struktura_fisu_host_dysk_ahci* fis = (struktura_fisu_host_dysk_ahci*)(&tablica->ramka_polecenia_fis);
    fis->typ_fisu = 0x27; 
    fis->flagi_portu = 1 << 7; 
    fis->komenda = czy_zapisz ? 0x35 : 0x25; 
    
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->urzadzenie = 1 << 6; 

    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    
    fis->licznik_sektorow_dolny = (uint8_t)(ilosc_sektorow & 0xFF);
    fis->licznik_sektorow_gorny = (uint8_t)((ilosc_sektorow >> 8) & 0xFF);
    
    volatile int timeout = 0;
    
    while ((port->status_zadania & (0x80 | 0x08)) && timeout < 100000000) {
        asm volatile("pause");
        timeout++;
    }
    if (port->status_zadania & (0x80 | 0x08)) {
        WypiszLog("[AHCI] Blad: Dysk zamrozony (Timeout BSY/DRQ)!");
        return false;
    }

    port->status_przerwan = 0xFFFFFFFF; 

    // KRYTYCZNE: BARIERA PAMIĘCI! 
    // Mówimy kompilatorowi: "Zrzuć ramki FIS i listy komend z Cache do fizycznego RAM-u, natychmiast!"
    asm volatile("mfence" ::: "memory");

    // BUM! Wyzwolenie kontrolera
    port->powiadomienia_komend = 1;
    
    timeout = 0;
    // Pętla odpytuje rejestr. Dzięki volatile, robi to uczciwie w każdym przejściu pętli!
    while (port->powiadomienia_komend & 1) {
        if (port->status_przerwan & (1 << 30)) {
            WypiszLog("[AHCI] Blad: Task File Error (Bit 30)!");
            return false; 
        }
        
        asm volatile("pause");
        timeout++;
        
        if (timeout > 500000000) { 
            WypiszLog("[AHCI] Blad: Przekroczono czas oczekiwania na paczke DMA!");
            return false;
        }
    }
    
    // KRYTYCZNE: BARIERA PAMIĘCI DLA ODCZYTU!
    // Mówimy kompilatorowi: "Zanim zaczniesz kopiować, poczekaj aż dysk na pewno skończy wlewać dane!"
    asm volatile("lfence" ::: "memory");

    if (!czy_zapisz) {
        for(uint32_t i=0; i<bajty_transferu; i++) ptr_user[i] = dma_bufor_danych[i];
    }
    
    return true;
}

extern "C" bool czytaj_z_glownego_dysku_ahci(uint64_t lba, uint32_t ilosc_sektorow, void* bufor_docelowy) {
    return operacja_dysku_ahci(lba, ilosc_sektorow, bufor_docelowy, false);
}

extern "C" bool zapisz_na_glowny_dysk_ahci(uint64_t lba, uint32_t ilosc_sektorow, void* dane_zrodlowe) {
    return operacja_dysku_ahci(lba, ilosc_sektorow, dane_zrodlowe, true);
}