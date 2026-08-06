/*
 * Minimalny punkt wejściowy jądra Bursztyn OS w języku C++ (64-bit).
 * Przystosowany do Trybu Graficznego (Linear Framebuffer).
 */

#include <stdint.h>
#include <stdbool.h>

#include "pamiec.h"
#include "psf.h"
#include "grafika.h" // PODŁĄCZENIE GRAFIKI
#include "loader.h"  // Zapewnia dostęp do Loadera programów .bur
#include "ahci.h"    // STEROWNIK DYSKU

// Deklaracje zewnętrznych procedur asemblerowych i systemowych
extern "C" void InicjalizujGDT();
extern "C" void InicjalizujIDT();
extern "C" void inicjalizuj_apic(); 
extern "C" void InicjalizujMyszPS2();

// Nowe podsystemy z poprzednich kroków (TSS i BWS)
extern "C" void inicjalizuj_tss(void* stos_jadra);
extern "C" void zaladuj_tss(uint16_t selektor_tss);
extern "C" void inicjalizuj_syscalls();
extern "C" uint64_t stack_top; // Wskaźnik na szczyt stosu zdefiniowany w boot.S

// Zmienna z PMM (Physical Memory Manager) określająca ilość pamięci RAM
extern uint64_t najwyzsza_znaleziona_ramka; 

extern "C" void skanuj_magistrale_pci();
extern "C" void wczytaj_tapete_z_dysku();  
    
extern "C" bool usun_twor(const char* sciezka);
extern "C" bool utworz_plik(const char* sciezka);
extern "C" bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc);
extern "C" bool utworz_katalog(const char* sciezka);

// Symbole wstrzykiwane przez GNU Linker (objcopy) z pliku shell_blob.o
extern "C" uint8_t _binary_shell_bin_start[];
extern "C" uint8_t _binary_shell_bin_end[];

extern "C" uint8_t _binary_notatnik_bin_start[];
extern "C" uint8_t _binary_notatnik_bin_end[];

// Prototyp funkcji uruchamiającej program z uwzględnieniem Systemu Uprawnień PZB oraz flagi z_syscalla
extern "C" bool bws_uruchom_program_z_pliku(const char* sciezka, uint8_t bzl_poziom, uint64_t flagi_praw, bool z_syscalla);

// --- NOWE: FUNKCJE SIECIOWE (Karta Intel E1000 i DHCP) ---
extern "C" void inicjalizuj_e1000();
extern "C" void e1000_obsluz_odbior();
extern "C" void uruchom_klienta_dhcp();

void UIntToStr(uint64_t wartosc, char* bufor) {
    if (wartosc == 0) { bufor[0] = '0'; bufor[1] = '\0'; return; }
    int i = 0; char temp[32];
    while (wartosc > 0) { temp[i++] = (wartosc % 10) + '0'; wartosc /= 10; }
    int j = 0; while (i > 0) { bufor[j++] = temp[--i]; }
    bufor[j] = '\0';
}

void ZlaczStringi(char* cel, const char* str1, const char* str2, const char* str3) {
    int i = 0;
    while (*str1) cel[i++] = *str1++;
    while (*str2) cel[i++] = *str2++;
    while (*str3) cel[i++] = *str3++;
    cel[i] = '\0';
}

extern "C" void kernel_main(uint64_t multiboot_magic, uint64_t multiboot_info_ptr) {
    if (multiboot_magic != 0x36D76289) return; 

    InicjalizujPMM(multiboot_info_ptr);
    InicjalizujGDT();
    
    inicjalizuj_tss((void*)&stack_top);
    zaladuj_tss(0x28); 

    InicjalizujIDT();
    InicjalizujVMM(); 
    inicjalizuj_syscalls();
    InicjalizujGrafike(multiboot_info_ptr);

    WypiszLog("==================================================");
    WypiszLog(" Witamy w Bursztyn OS 64-bit (GUI + Paging)");
    WypiszLog("==================================================");

    uint64_t ram_mb = (najwyzsza_znaleziona_ramka * 4096) / (1024 * 1024);
    char ram_str[32]; char ram_msg[80];
    UIntToStr(ram_mb, ram_str);
    ZlaczStringi(ram_msg, "[PMM] Skan pamieci fizycznej. Wykryto: ", ram_str, " MB RAM");
    WypiszLog(ram_msg);

    WypiszLog("[VMM] Paging 4-poziomowy aktywowany. Tablice przebudowane.");

    inicjalizuj_apic();
    WypiszLog("[APIC] Kontroler przerwan (LAPIC/IOAPIC) uruchomiony.");
    InicjalizujMyszPS2();
    WypiszLog("[I/O] Sterowniki Mysz i Klawiatura (PS/2) gotowe.");
    WypiszLog("[BWS] API Wywolan Systemowych gotowe.");

    inicjalizuj_kontroler_ahci();
    wczytaj_tapete_z_dysku();

    // --- PODNIESIENIE INTERFEJSU SIECIOWEGO ---
    inicjalizuj_e1000();
    
    // --- AKTYWACJA DHCP ZARAZ PO PODNIESIENIU KARTY! ---
    uruchom_klienta_dhcp();
    WypiszLog("[SIEC] Stos TCP/IP (DHCP, ARP, ICMP, DNS) w pelni operacyjny.");

    uint64_t adres_wirtualny_dysku = 0x40000000; 
    uint32_t rozmiar_dysku = 2 * 1024 * 1024;    

    for (uint32_t i = 0; i < rozmiar_dysku; i += 4096) {
        void* wolna_ramka_fizyczna = ZaalokujRamke();
        if (wolna_ramka_fizyczna) ZmapujStrone((void*)(adres_wirtualny_dysku + i), wolna_ramka_fizyczna, 0b00000011);
    }

    inicjalizuj_psf((void*)adres_wirtualny_dysku, rozmiar_dysku);

    utworz_katalog("/jadro");
    utworz_katalog("/system");
    utworz_katalog("/programy");
    utworz_katalog("/uslugi");
    utworz_katalog("/sterowniki");
    utworz_katalog("/uzytkownicy");
    utworz_katalog("/ustawienia");
    utworz_katalog("/logi");         
    utworz_katalog("/piaskownica");
    utworz_katalog("/tymczasowe");

    skanuj_magistrale_pci();

    WypiszLog("--------------------------------------------------");
    WypiszLog("System operacyjny gotowy!");

    asm volatile("sti");

    usun_twor("/shell.bur"); 
    utworz_plik("/shell.bur");
    uint64_t shell_rozmiar = (uint64_t)(_binary_shell_bin_end - _binary_shell_bin_start);
    zapisz_do_pliku("/shell.bur", (const char*)_binary_shell_bin_start, shell_rozmiar);
    WypiszLog("[BSP] Wbudowana Powloka gotowa do odczytu z dysku.");
  
  const char* manifest_notatnika = 
        "nazwa = \"Notatnik\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"notatnik.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"pliki_czytaj\",\n"
        "    \"pliki_zapisz\"\n"
        "]\n";
        
    int len_manifest = 0;
    while (manifest_notatnika[len_manifest] != '\0') len_manifest++;
    
    // Zapis manifestu
    utworz_plik("/opis_notatnika.txt");
    zapisz_do_pliku("/opis_notatnika.txt", manifest_notatnika, len_manifest);

    // Pobranie PRAWIDŁOWEGO rozmiaru wyliczonego przez czysty GNU Linker
    uint64_t notatnik_rozmiar = (uint64_t)(_binary_notatnik_bin_end - _binary_notatnik_bin_start);
    
    // Zapis binarnego, natywnego pliku z kodem maszynowym .bur
    utworz_plik("/notatnik.bur");
    zapisz_do_pliku("/notatnik.bur", (const char*)_binary_notatnik_bin_start, notatnik_rozmiar);
    
    WypiszLog("[BSP] Aplikacja Notatnik zainstalowana w glownym katalogu (/)!");

    // =========================================================
    
    // Start Powłoki Ring 3
    bws_uruchom_program_z_pliku("/shell.bur", PZB_UZYTKOWNIK, 0xFFFFFFFF, false);


    // 10. Pętla bezczynności - Kernel sprawdza sieć (Polling)
    while (true) {
        // Analiza czy karta sieciowa (DMA) zrzuciła nowe pakiety do RAM-u
        e1000_obsluz_odbior(); 
        asm volatile ("hlt");
    }
}