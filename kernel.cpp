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
#include "sterowniki/dzwiek/hda.h" // STEROWNIK DŹWIĘKU INTEL HDA
#include "heap.h"    // ALOKATOR PAMIĘCI (STERTA)

// Deklaracje zewnętrznych procedur asemblerowych i systemowych
extern "C" void InicjalizujGDT();
extern "C" void InicjalizujIDT();
extern "C" void inicjalizuj_apic(); 
extern "C" void InicjalizujMyszPS2();

// Nowe podsystemy z poprzednich kroków (TSS i BWS)
extern "C" void inicjalizuj_tss(void* stos_jadra);
extern "C" void zaladuj_tss(uint16_t selektor_tss);
extern "C" void inicjalizuj_syscalls();
extern "C" void inicjalizuj_mbedtls(); // <--- DODAJ TĘ LINIJKĘ TUTAJ
extern "C" uint64_t stack_top; // Wskaźnik na szczyt stosu zdefiniowany w boot.S

// Zmienna z PMM (Physical Memory Manager) określająca ilość pamięci RAM
extern uint64_t najwyzsza_znaleziona_ramka; 

extern "C" void skanuj_magistrale_pci();
extern "C" void wczytaj_tapete_z_dysku();  
    
extern "C" bool usun_twor(const char* sciezka);
extern "C" bool utworz_plik(const char* sciezka);
extern "C" bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc);
extern "C" bool utworz_katalog(const char* sciezka);

// Deklaracja inicjalizacji zrewidowanego, drzewiastego BSP
extern "C" void inicjalizuj_psf(void* adres_ram_dysku, uint32_t rozmiar_w_bajtach);

// Symbole wstrzykiwane przez GNU Linker (objcopy) z plików aplikacji
extern "C" uint8_t _binary_shell_bin_start[];
extern "C" uint8_t _binary_shell_bin_end[];

extern "C" uint8_t _binary_notatnik_bin_start[];
extern "C" uint8_t _binary_notatnik_bin_end[];

//  Deklaracja symboli Kalkulatora z pliku Makefile!
extern "C" uint8_t _binary_kalkulator_bin_start[];
extern "C" uint8_t _binary_kalkulator_bin_end[];

// Deklaracja symboli Menedżera Okien
extern "C" uint8_t _binary_menedzer_okien_bin_start[];
extern "C" uint8_t _binary_menedzer_okien_bin_end[];

extern "C" char _binary_przegladarka_bin_start[];
extern "C" char _binary_przegladarka_bin_end[];

// Prototyp funkcji uruchamiającej program z uwzględnieniem Systemu Uprawnień PZB
extern "C" bool bws_uruchom_program_z_pliku(const char* sciezka, uint8_t bzl_poziom, uint64_t flagi_praw, bool z_syscalla);

// --- FUNKCJE SIECIOWE (Karta Intel E1000 i DHCP) ---
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

    wypisz_log("==================================================");
    wypisz_log(" Witamy w Bursztyn OS 64-bit ");
    wypisz_log(" Polski System Operacyjny");
    wypisz_log(" Github: Programista-Art/BursztynOS ");
    wypisz_log("==================================================");

    uint64_t ram_mb = (najwyzsza_znaleziona_ramka * 4096) / (1024 * 1024);
    char ram_str[32]; char ram_msg[80];
    UIntToStr(ram_mb, ram_str);
    ZlaczStringi(ram_msg, "[PMM] Skan pamieci fizycznej. Wykryto: ", ram_str, " MB RAM");
    wypisz_log(ram_msg);

    wypisz_log("[VMM] Paging 4-poziomowy aktywowany. Tablice przebudowane.");

    // --- INICJALIZACJA STERTY JĄDRA (HEAP) ---
    uint64_t adres_wirtualny_sterty = 0x500000000ULL; // Bezpieczny adres wysoko w pamięci (20 GB)
    uint64_t rozmiar_sterty = 16 * 1024 * 1024;       // 16 MB na stertę

    for (uint64_t i = 0; i < rozmiar_sterty; i += 4096) {
        void* wolna_ramka = ZaalokujRamke();
        if (wolna_ramka) {
            ZmapujStrone((void*)(adres_wirtualny_sterty + i), wolna_ramka, 0b00000011); // Present + Writable
        }
    }
    inicjalizuj_sterte_jadra((void*)adres_wirtualny_sterty, rozmiar_sterty);
    wypisz_log("[HEAP] Alokator Pamieci (Sterta 16 MB) gotowy! Operatory new/delete aktywne.");

    /*
     * mbedTLS korzysta z osobnej, statycznej puli 256 KiB zdefiniowanej w
     * mbedtls_port.cpp. Bez tej inicjalizacji mbedtls_calloc() zwraca nullptr,
     * a mbedtls_ssl_setup() konczy sie bledem -0x7F00
     * (MBEDTLS_ERR_SSL_ALLOC_FAILED).
     */
    inicjalizuj_mbedtls();
    wypisz_log("[KRYPTO] Pula pamieci mbedTLS 256 KiB gotowa.");

    inicjalizuj_apic();
    wypisz_log("[APIC] Kontroler przerwan (LAPIC/IOAPIC) uruchomiony.");
    InicjalizujMyszPS2();
    wypisz_log("[I/O] Sterowniki Mysz i Klawiatura (PS/2) gotowe.");
    wypisz_log("[BWS] API Wywolan Systemowych gotowe.");

    inicjalizuj_kontroler_ahci();
    wczytaj_tapete_z_dysku();

    // --- PODNIESIENIE INTERFEJSU SIECIOWEGO ---
    inicjalizuj_e1000();
    
    // --- AKTYWACJA DHCP ZARAZ PO PODNIESIENIU KARTY! ---
    uruchom_klienta_dhcp();
    wypisz_log("[SIEC] Stos TCP/IP (DHCP, ARP, ICMP, DNS) w pelni operacyjny.");

    // --- INICJALIZACJA KARTY DŹWIĘKOWEJ ---
    if (inicjalizuj_hda()) {
        wypisz_log("[HDA] Karta wykryta poprawnie, odtwarzam dzwiek startowy!");
        hda_test_ton(880, 500);
    }

    // --- WIRTUALIZACJA I DRZEWIASTY SYSTEM PLIKÓW ---
    // ZMIANA VMM: Przenosimy 1 GB ramdysk powyżej granicy 4 GB
    uint64_t adres_wirtualny_dysku = 0x130000000ULL; 
    uint32_t rozmiar_dysku = 2 * 1024 * 1024;    

    for (uint32_t i = 0; i < rozmiar_dysku; i += 4096) {
        void* wolna_ramka_fizyczna = ZaalokujRamke();
        if (wolna_ramka_fizyczna) ZmapujStrone((void*)(adres_wirtualny_dysku + i), wolna_ramka_fizyczna, 0b00000011);
    }

    inicjalizuj_psf((void*)adres_wirtualny_dysku, rozmiar_dysku);

    // Tworzenie drzewa katalogów
    utworz_katalog("/jadro");
    utworz_katalog("/system");
    utworz_katalog("/programy");
    utworz_katalog("/programy/notatnik.cebula");
    utworz_katalog("/programy/kalkulator.cebula"); 
    utworz_katalog("/programy/przegladarka.cebula"); 
    utworz_katalog("/uslugi");
    utworz_katalog("/sterowniki");
    utworz_katalog("/uzytkownicy");
    utworz_katalog("/ustawienia");
    utworz_katalog("/logi");         
    utworz_katalog("/piaskownica");
    utworz_katalog("/tymczasowe");

    skanuj_magistrale_pci();

    wypisz_log("--------------------------------------------------");
    wypisz_log("System operacyjny gotowy!");

    asm volatile("sti");

    // =========================================================
    // --- INSTALACJA POWŁOKI (shell.bur) ---
    // =========================================================
    usun_twor("/shell.bur"); 
    utworz_plik("/shell.bur");
    uint64_t shell_rozmiar = (uint64_t)(_binary_shell_bin_end - _binary_shell_bin_start);
    zapisz_do_pliku("/shell.bur", (const char*)_binary_shell_bin_start, shell_rozmiar);
    wypisz_log("[BSP] Wbudowana Powloka gotowa do odczytu z dysku.");


    uint32_t przegladarka_rozmiar = (uint32_t)(_binary_przegladarka_bin_end - _binary_przegladarka_bin_start);
    if(przegladarka_rozmiar < 24576) przegladarka_rozmiar = 24576; 

    //Przegladarka
    // utworz_katalog("/programy");
    // utworz_katalog("/programy/przegladarka.cebula");
    // --- WDRAŻANIE PACZKI APLIKACJI (PRZEGLĄDARKA HUSSAR) ---
    // =========================================================
    const char* manifest_przegladarki = 
        "nazwa = \"Hussar\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"0.1\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"przegladarka.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\",\n"
        "    \"siec\",\n"
        "    \"pliki_czytaj\"\n"
        "]\n";
        
    int len_manifest_p = 0;
    while (manifest_przegladarki[len_manifest_p] != '\0') len_manifest_p++;


    utworz_plik("/programy/przegladarka.cebula/przegladarka.bur");
    zapisz_do_pliku("/programy/przegladarka.cebula/przegladarka.bur", _binary_przegladarka_bin_start, przegladarka_rozmiar);
    wypisz_log("[BSP] Przeglądarka Hussar zainstalowana jako paczka .cebula!");

    // =========================================================
    // --- WDRAŻANIE PACZKI APLIKACJI (NOTATNIK.CEBULA) ---
    // =========================================================
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

    utworz_plik("/programy/notatnik.cebula/opis.aplikacji");
    zapisz_do_pliku("/programy/notatnik.cebula/opis.aplikacji", manifest_notatnika, len_manifest);

    // Padding pliku (by uchronić Notatnik przed nadpisaniem kodu powłoki przy małym buforze)
    uint64_t notatnik_rozmiar = (uint64_t)(_binary_notatnik_bin_end - _binary_notatnik_bin_start);
    if(notatnik_rozmiar < 24576) notatnik_rozmiar = 24576; 
    
    // Instalacja prawidłowo do folderu paczki (tam gdzie szuka go Pulpit!)
    utworz_plik("/programy/notatnik.cebula/notatnik.bur");
    zapisz_do_pliku("/programy/notatnik.cebula/notatnik.bur", (const char*)_binary_notatnik_bin_start, notatnik_rozmiar);
    wypisz_log("[BSP] Aplikacja Notatnik zainstalowana jako paczka .cebula!");

    // =========================================================
    // --- WDRAŻANIE PACZKI APLIKACJI (KALKULATOR.CEBULA) ---
    // =========================================================
    const char* manifest_kalkulatora = 
        "nazwa = \"Kalkulator\"\n"
        "autor = \"Programista Art\"\n"
        "wersja = \"1.0\"\n"
        "poziom_zaufania = 4\n"
        "plik_startowy = \"kalkulator.bur\"\n"
        "uprawnienia = [\n"
        "    \"okna\"\n"
        "]\n";
        
    int len_manifest_kalk = 0;
    while (manifest_kalkulatora[len_manifest_kalk] != '\0') len_manifest_kalk++;

    utworz_plik("/programy/kalkulator.cebula/opis.aplikacji");
    zapisz_do_pliku("/programy/kalkulator.cebula/opis.aplikacji", manifest_kalkulatora, len_manifest_kalk);

    uint64_t kalkulator_rozmiar = (uint64_t)(_binary_kalkulator_bin_end - _binary_kalkulator_bin_start);
    if(kalkulator_rozmiar < 24576) kalkulator_rozmiar = 24576; 
    
    utworz_plik("/programy/kalkulator.cebula/kalkulator.bur");
    zapisz_do_pliku("/programy/kalkulator.cebula/kalkulator.bur", (const char*)_binary_kalkulator_bin_start, kalkulator_rozmiar);
    wypisz_log("[BSP] Aplikacja Kalkulator zainstalowana jako paczka .cebula!");

    // =========================================================
    // --- WDRAŻANIE MENEDŻERA OKIEN (PULPIT) ---
    // =========================================================
    utworz_plik("/menedzer_okien.bur");
    uint64_t menedzer_rozmiar = (uint64_t)(_binary_menedzer_okien_bin_end - _binary_menedzer_okien_bin_start);
    zapisz_do_pliku("/menedzer_okien.bur", (const char*)_binary_menedzer_okien_bin_start, menedzer_rozmiar);
    wypisz_log("[BSP] Menedzer Okien zainstalowany i gotowy!");

    // =========================================================
    
    // Zamiast terminala (shell.bur), system włącza od razu Twój nowy Pulpit!
    bws_uruchom_program_z_pliku("/menedzer_okien.bur", PZB_UZYTKOWNIK, 0xFFFFFFFF, false);

    while (true) {
        e1000_obsluz_odbior(); 
        asm volatile ("hlt");
    }
}
