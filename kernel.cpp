/*
 * Minimalny punkt wejściowy jądra Bursztyn OS w języku C++ (64-bit).
 * Przystosowany do Trybu Graficznego (Linear Framebuffer).
 */

#include <stdint.h>

#include "pamiec.h"
#include "psf.h"
#include "grafika.h" // PODŁĄCZENIE GRAFIKI
#include "loader.h"  // Zapewnia dostęp do Loadera programów .bur

// Deklaracje zewnętrznych procedur asemblerowych i systemowych
extern "C" void InicjalizujGDT();
extern "C" void InicjalizujIDT();
extern "C" void inicjalizuj_apic(); // NAPRAWIONE: Brakujący średnik!
extern "C" void InicjalizujMyszPS2();

// Nowe podsystemy z poprzednich kroków (TSS i BWS)
extern "C" void inicjalizuj_tss(void* stos_jadra);
extern "C" void zaladuj_tss(uint16_t selektor_tss);
extern "C" void inicjalizuj_syscalls();
extern "C" uint64_t stack_top; // Wskaźnik na szczyt stosu zdefiniowany w boot.S

// Zmienna z PMM (Physical Memory Manager) określająca ilość pamięci RAM
extern uint64_t najwyzsza_znaleziona_ramka; 

// Automatycznie wygenerowane wskaźniki na binarkę powłoki
extern "C" uint8_t _binary_shell_bin_start[];
extern "C" uint8_t _binary_shell_bin_end[];

// ---------------------------------------------------------
// Własne, wbudowane funkcje pomocnicze (ponieważ nie mamy biblioteki standardowej <string.h>)
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
    InicjalizujGDT();
    InicjalizujIDT();
        // (Musi odbyć się bezpośrednio po GDT, a przed jakimikolwiek przerwaniami)
    inicjalizuj_tss((void*)&stack_top);
    zaladuj_tss(0x28); // Ładujemy 5. wpis w GDT (5 * 8 bajtów = 0x28)
    // 2. Fundamentalna inicjalizacja hardware'u i zarządzania pamięcią
    InicjalizujPMM(multiboot_info_ptr);
   
   
    InicjalizujVMM(); // Paging 4-poziomowy

    // --- NOWE: Inicjalizacja Bursztynowych Wywołań Systemowych (BWS) ---
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

    // 6. Inicjalizacja asynchronicznych mechanizmów wejścia
    inicjalizuj_apic();
    WypiszLog("[APIC] Kontroler przerwan (LAPIC/IOAPIC) uruchomiony.");
    
    InicjalizujMyszPS2();
    WypiszLog("[I/O] Sterowniki Mysz i Klawiatura (PS/2) gotowe.");
    WypiszLog("[BWS] API Wywolan Systemowych gotowe.");

    WypiszLog("--------------------------------------------------");
    WypiszLog("System operacyjny gotowy!");

    // 7. Odblokowanie przerwań sprzętowych
    asm volatile("sti");

    // UWAGA: Jeśli masz już skompilowaną binarkę `.bur`, możesz zapisać ją 
    // sztucznie do systemu plików i przetestować ładowarkę Ring 3:
    // bws_uruchom_program_z_pliku("/aplikacja.bur", 4, 0xFF);
// 7. Odblokowanie przerwań sprzętowych
    asm volatile("sti");

 // Zapisujemy wyodrębnioną binarkę powłoki na wirtualny RAM Dysk
    utworz_plik("/shell.bur"); // <-- DODANA LINIJKA: Tworzy plik przed zapisem!
    uint64_t shell_rozmiar = (uint64_t)(_binary_shell_bin_end - _binary_shell_bin_start);


   // Logowanie diagnostyczne rozmiaru (pomoże nam upewnić się, czy plik nie jest pusty)
    char dbg_rozmiar[32];
    char dbg_msg[80];
    UIntToStr(shell_rozmiar, dbg_rozmiar);
    ZlaczStringi(dbg_msg, "[JADRO] Binarka shell.bur pobrana z linkera, rozmiar: ", dbg_rozmiar, " bajtow");
    WypiszLog(dbg_msg);

    zapisz_do_pliku("/shell.bur", (const char*)_binary_shell_bin_start, shell_rozmiar);
    WypiszLog("[BSP] Wbudowana Powloka gotowa do odczytu z dysku.");

    // Skaczemy do Ring 3 uruchamiając Terminal!
    bws_uruchom_program_z_pliku("/shell.bur", 4, 0xFF);


    // 8. Pętla bezczynności - Kernel czeka na eventy (Halt state)
    while (true) {
        asm volatile ("hlt");
    }
}