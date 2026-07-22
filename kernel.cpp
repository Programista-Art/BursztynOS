/*
 * Minimalny punkt wejściowy jądra Bursztyn OS w języku C++ (64-bit).
 * Przystosowany do Trybu Graficznego (Linear Framebuffer).
 */

#include <stdint.h>

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

extern "C" void skanuj_magistrale_pci();
extern "C" void wczytaj_tapete_z_dysku();

// Zmienna z PMM (Physical Memory Manager) określająca ilość pamięci RAM
extern uint64_t najwyzsza_znaleziona_ramka; 

// --- DEKLARACJA NASZEGO NOWEGO SKANERA PCI ---
extern void skanuj_magistrale_pci();

// --- NAPRAWA BŁĘDÓW KOMPILACJI (BRAKUJĄCE DEKLARACJE) ---
// Symbole wstrzykiwane przez GNU Linker (objcopy) z pliku shell_blob.o
extern char _binary_shell_bin_start[];
extern char _binary_shell_bin_end[];

// --- DEKLARACJA NASZEGO NOWEGO SKANERA PCI ---
extern void skanuj_magistrale_pci();

// Prototyp funkcji uruchamiającej program z uwzględnieniem Systemu Uprawnień PZB
extern "C" bool bws_uruchom_program_z_pliku(const char* sciezka, uint8_t bzl_poziom, uint64_t flagi_praw);
// --------------------------------------------------------

// ---------------------------------------------------------
// Własne, wbudowane funkcje pomocnicze
// ---------------------------------------------------------

void UIntToStr(uint64_t wartosc, char* bufor) {
    if (wartosc == 0) { 
        bufor[0] = '0'; 
        bufor[1] = '\0'; 
        return; 
    }
    int i = 0; 
    char temp[32];
    
    while (wartosc > 0) { 
        temp[i++] = (wartosc % 10) + '0'; 
        wartosc /= 10; 
    }
    
    int j = 0; 
    while (i > 0) { 
        bufor[j++] = temp[--i]; 
    }
    bufor[j] = '\0';
}

void ZlaczStringi(char* cel, const char* str1, const char* str2, const char* str3) {
    int i = 0;
    while (*str1) cel[i++] = *str1++;
    while (*str2) cel[i++] = *str2++;
    while (*str3) cel[i++] = *str3++;
    cel[i] = '\0';
}

// ---------------------------------------------------------
// GŁÓWNA FUNKCJA JĄDRA SYSTEMU
// ---------------------------------------------------------

extern "C" void kernel_main(uint64_t multiboot_magic, uint64_t multiboot_info_ptr) {
    // 1. Ochrona: Upewnijmy się, że uruchomił nas autoryzowany bootloader (GRUB)
    if (multiboot_magic != 0x36D76289) {
        return; // Kernel Panic: Zły bootloader!
    }

    // 2. Fundamentalna inicjalizacja hardware'u i zarządzania pamięci
    InicjalizujPMM(multiboot_info_ptr);
    InicjalizujGDT();
    
    // --- Inicjalizacja TSS ---
    // (Musi odbyć się bezpośrednio po GDT, a przed jakimikolwiek przerwaniami)
    inicjalizuj_tss((void*)&stack_top);
    zaladuj_tss(0x28); // Ładujemy 5. wpis w GDT (5 * 8 bajtów = 0x28)

    InicjalizujIDT();
    InicjalizujVMM(); // Paging 4-poziomowy

    // --- Inicjalizacja Bursztynowych Wywołań Systemowych (BWS) ---
    inicjalizuj_syscalls();

    // 3. WŁĄCZENIE EKRANU W TRYBIE GRAFICZNYM (LFB pobrany z tagów Multiboot2)
    InicjalizujGrafike(multiboot_info_ptr);

    // UWAGA: Od tego momentu mamy GUI! Używamy graficznego WypiszLog.
    WypiszLog("==================================================");
    WypiszLog(" Witamy w Bursztyn OS 64-bit (Skladacz Obrazu LFB)");
    WypiszLog("==================================================");

    // 4. Obliczanie i logowanie dostępnej pamięci RAM
    uint64_t ram_mb = (najwyzsza_znaleziona_ramka * 4096) / (1024 * 1024);
    char ram_str[32]; char ram_msg[80];
    UIntToStr(ram_mb, ram_str);
    ZlaczStringi(ram_msg, "[PMM] Skan pamieci fizycznej. Wykryto: ", ram_str, " MB RAM");
    WypiszLog(ram_msg);

    WypiszLog("[VMM] Paging 4-poziomowy aktywowany. Tablice przebudowane.");

    // 5. Konfiguracja RAM-Dysku pod system plików BSP (Bursztynowy System Plików)
    uint64_t adres_wirtualny_dysku = 0x40000000; // Podnosimy wysoko - 1GB
    uint32_t rozmiar_dysku = 2 * 1024 * 1024;    // 2 MB przestrzeni

    for (uint32_t i = 0; i < rozmiar_dysku; i += 4096) {
        void* wolna_ramka_fizyczna = ZaalokujRamke();
        if (wolna_ramka_fizyczna) {
            // Flagi 0b11 = Present (Obecna) | Read/Write (Zapisywalna)
            ZmapujStrone((void*)(adres_wirtualny_dysku + i), wolna_ramka_fizyczna, 0b00000011);
        }
    }

    inicjalizuj_psf((void*)adres_wirtualny_dysku, rozmiar_dysku);
    WypiszLog("[BSP] Bursztynowy System Plikow zamontowany w RAM (2MB).");

    // --- Tworzenie zaufanego drzewa katalogów (Wg specyfikacji Bursztyna) ---
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
    WypiszLog("[BSP] Pomyslnie wygenerowano polskie drzewo katalogow.");

    // 6. Inicjalizacja asynchronicznych mechanizmów wejścia
    inicjalizuj_apic();
    WypiszLog("[APIC] Kontroler przerwan (LAPIC/IOAPIC) uruchomiony.");
    
    InicjalizujMyszPS2();
    WypiszLog("[I/O] Sterowniki Mysz i Klawiatura (PS/2) gotowe.");
    WypiszLog("[BWS] API Wywolan Systemowych gotowe.");

    //START DYSKU I ŁADOWANIE MULTIMEDIÓW 
  

    // --- WYWOŁANIE SKANERA PCI (Wpisuje logi do /logi/pci.txt) ---
    skanuj_magistrale_pci();

    // --- NOWY STEROWNIK DYSKU (SATA) ---
    inicjalizuj_kontroler_ahci();
    wczytaj_tapete_z_dysku();
    // // Krotki test odczytu prawdziwego dysku na maszynie wirtualnej (Tylko Ring 0)
    // void* bufor_testowy_dysku = ZaalokujRamke(); // Daje nam bezpieczne 4096 bajtow z mapy
    // if (czytaj_z_glownego_dysku_ahci(0, 1, bufor_testowy_dysku)) {
    //     WypiszLog("[AHCI-TEST] Odczyt Sektora LBA 0 (512 bajtow) ZAKONCZONY SUKCESEM!");
    // } else {
    //     WypiszLog("[AHCI-TEST] Blad! Zaden z dyskow twardych nie odpowiedzial.");
    // }

    // -----------------------------------

    WypiszLog("--------------------------------------------------");
    WypiszLog("System operacyjny gotowy!");

    // 7. Odblokowanie przerwań sprzętowych
    asm volatile("sti");

    // Zapisujemy wyodrębnioną binarkę powłoki na wirtualny RAM Dysk
    utworz_plik("/shell.bur");
    uint64_t shell_rozmiar = (uint64_t)(_binary_shell_bin_end - _binary_shell_bin_start);
    
    // --- DIAGNOSTYCZNE LOGOWANIE ROZMIARU W BAJTACH ---
    char dbg_rozmiar[32];
    char dbg_msg[80];
    UIntToStr(shell_rozmiar, dbg_rozmiar);
    ZlaczStringi(dbg_msg, "[JADRO] Binarka shell.bur pobrana z linkera, rozmiar: ", dbg_rozmiar, " bajtow");
    WypiszLog(dbg_msg);
    // --------------------------------------------------------

    zapisz_do_pliku("/shell.bur", (const char*)_binary_shell_bin_start, shell_rozmiar);
    WypiszLog("[BSP] Wbudowana Powloka gotowa do odczytu z dysku.");

    // Skaczemy do Ring 3 uruchamiając Terminal! 
    // Poziom PZB: 4 (Użytkownik), Uprawnienia: 0xFF (pełne prawa np. do pisania poza /system)
    bws_uruchom_program_z_pliku("/shell.bur", 4, 0xFF);

    // 8. Pętla bezczynności - Kernel czeka na eventy (Halt state)
    while (true) {
        asm volatile ("hlt");
    }
}