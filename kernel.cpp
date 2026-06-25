/*
 * Minimalny punkt wejściowy jądra systemu operacyjnego zrealizowany w języku C++ (64-bit).
 * Zostaje on wywołany przez skrypt `boot.S` po przełączeniu procesora
 * ze środowiska chronionego (Protected Mode) w ostateczny tryb Long Mode.
 */

#include <stdint.h>

#include "pamiec.h"
#include "psf.h"

// Deklaracje dostępnych publicznych metod z innych modułów
extern "C" void InicjalizujGDT();
extern "C" void InicjalizujIDT();
extern "C" void InicjalizujAPIC();
extern "C" void InicjalizujMyszPS2(); // Deklaracja z mysz.cpp

// Pobieramy informację o najwyższej ramce z PMM (Menedżera Pamięci Fizycznej)
extern uint64_t najwyzsza_znaleziona_ramka; 

// --- NOWE: ULEPSZONE ZARZĄDZANIE EKRANEM ---
static int aktualny_wiersz_logow = 0;

// Wypisuje logi systemowe linijka po linijce (od góry)
void WypiszLog(const char* tekst) {
    volatile uint16_t* bufor_vga = (uint16_t*)0xB8000;
    // uint16_t kolor = (0x0A << 8); // Jasnozielony dla logów jądra
    uint16_t kolor = (0x06 << 8); // Kod 0x06 to Brązowy/Ciemnopomarańczowy
    
    // Zabezpieczenie przed wyjściem za ekran (zostawiamy wiersz 24 na klawiaturę)
    if (aktualny_wiersz_logow >= 23) {
        aktualny_wiersz_logow = 0; // W prymitywnej wersji po prostu wracamy na góre ekranu
    }

    int i = 0;
    while (tekst[i] != '\0' && i < 80) {
        bufor_vga[(aktualny_wiersz_logow * 80) + i] = kolor | tekst[i];
        i++;
    }
    aktualny_wiersz_logow++;
}

// Funkcja współdzielona z klawiatura.cpp i mysz.cpp
// Zarezerwowana wyłącznie dla dolnego paska (wiersz 24)
extern "C" void WypiszNaEkranie(const char* tekst) {
    volatile uint16_t* bufor_vga = (uint16_t*)0xB8000;
    uint16_t kolor = (0x0E << 8); // Żółty kolor dla interakcji (klawiatura/mysz)
    int i = 0;
    
    // Piszemy tylko na ostatnim, 24. wierszu ekranu
    while (tekst[i] != '\0' && i < 80) {
        bufor_vga[(24 * 80) + i] = kolor | tekst[i];
        i++;
    }
    
    // Wyczyść resztę linii wizualnie (niezbędne, by klawisz Backspace działał poprawnie)
    while (i < 80) {
        bufor_vga[(24 * 80) + i] = kolor | ' ';
        i++;
    }
}

// --- NOWE: FUNKCJE POMOCNICZE (Liczby na tekst) ---
void UIntToStr(uint64_t wartosc, char* bufor) {
    if (wartosc == 0) {
        bufor[0] = '0';
        bufor[1] = '\0';
        return;
    }
    int i = 0;
    char temp[32];
    // Wyciąganie reszty z dzielenia by zbudować tekst od tyłu
    while (wartosc > 0) {
        temp[i++] = (wartosc % 10) + '0';
        wartosc /= 10;
    }
    // Odwrócenie na poprawną kolejność
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

// --- GŁÓWNY PUNKT WEJŚCIA ---
extern "C" void kernel_main(uint64_t multiboot_magic, uint64_t multiboot_info_ptr) {
    if (multiboot_magic != 0x36D76289) {
        return; 
    }

    InicjalizujPMM(multiboot_info_ptr);

    WypiszLog("========================================");
    WypiszLog("      Witamy w Bursztyn OS 64-bit       ");
    WypiszLog("========================================");

    // Dynamiczne obliczanie i wypisywanie pamięci RAM w megabajtach
    // 1 Ramka = 4096 bajtów (4KB)
    uint64_t ram_mb = (najwyzsza_znaleziona_ramka * 4096) / (1024 * 1024);
    
    char ram_str[32];
    char ram_msg[80];
    UIntToStr(ram_mb, ram_str);
    ZlaczStringi(ram_msg, "[PMM] Skan pamieci fizycznej. Wykryto: ", ram_str, " MB RAM");
    WypiszLog(ram_msg);

    InicjalizujGDT();
    WypiszLog("[GDT] Tablica Deskryptorow zaladowana.");

    InicjalizujIDT();
    WypiszLog("[IDT] Tabela Przerwan (ISR) gotowa.");

    InicjalizujVMM();
    WypiszLog("[VMM] Paging 4-poziomowy aktywowany.");

    uint64_t adres_wirtualny_dysku = 0x40000000;
    uint32_t rozmiar_dysku = 2 * 1024 * 1024; // 2 MB

    for (uint32_t i = 0; i < rozmiar_dysku; i += 4096) {
        void* wolna_ramka_fizyczna = ZaalokujRamke();
        if (wolna_ramka_fizyczna) {
            ZmapujStrone((void*)(adres_wirtualny_dysku + i), wolna_ramka_fizyczna, 0b00000011);
        }
    }

    inicjalizuj_psf((void*)adres_wirtualny_dysku, rozmiar_dysku);
    WypiszLog("[BSP] Bursztynowy System Plikow zamontowany w RAM (2MB).");

    InicjalizujAPIC();
    WypiszLog("[APIC] Nowoczesny kontroler przerwan uruchomiony.");

    // Inicjalizacja myszy PS/2 przed włączeniem przerwań
    InicjalizujMyszPS2();
    WypiszLog("[I/O] Sterowniki Mysz i Klawiatura (PS/2) gotowe.");
    
    WypiszLog("----------------------------------------");
    WypiszLog("System operacyjny gotowy. Mozesz pisac!");

    // Rozpoczynamy asynchroniczne przyjmowanie przerwań (klawiatura ożywa)
    asm volatile("sti");

    while (true) {
        // Procesor śpi dopóki nie zostanie wybudzony np. przez wciśnięcie klawisza
        asm volatile ("hlt");
    }
}