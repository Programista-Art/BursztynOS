/*
 * Bursztyn OS - Menedżer Pakietów (.cebula)
 * Aplikacja Ring 3, która automatycznie rozpakowuje paczki .cebula
 * i instaluje je w systemie.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct NaglowekBur {
    uint8_t  magia[4];            
    uint64_t punkt_wejscia;       
    uint64_t tekst_przesuniecie;  
    uint64_t tekst_rozmiar;       
    uint64_t tekst_wirtualny;     
    uint64_t dane_przesuniecie;   
    uint64_t dane_rozmiar;        
    uint64_t dane_wirtualny;      
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        4096, 32768, 0x601000,
        36864, 131072, 0x609000
    };
}

extern "C" uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0) {
    uint64_t wynik;
    asm volatile(
        "movq %1, %%r8\n"
        "movq %2, %%r9\n"
        "movq %3, %%r10\n"
        "movq %4, %%r12\n"
        "movq %5, %%r13\n"
        "syscall\n"
        "movq %%rax, %0\n"
        : "=r"(wynik)
        : "r"(nr_funkcji), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4)
        : "rcx", "r11", "r8", "r9", "r10", "r12", "r13", "memory"
    );
    return wynik;
}

// Funkcje pomocnicze I/O
void wypisz(const char* tekst) { bws_wywolaj(1, (uint64_t)tekst); }
char pobierz_znak() { return (char)bws_wywolaj(4); }

// To jest struktura naszego własnego formatu paczki!
struct NaglowekCebula {
    char magia[8];            // Zawsze "CEBULA01"
    char nazwa_katalogu[32];  // np. "gra" (stworzy folder /programy/gra.cebula/)
    char nazwa_pliku_bur[32]; // np. "gra.bur"
    uint32_t rozmiar_manifestu;
    uint32_t rozmiar_programu;
} __attribute__((packed));

void zlacz_teksty(char* cel, const char* a, const char* b, const char* c) {
    int i = 0;
    while (*a) cel[i++] = *a++;
    while (*b) cel[i++] = *b++;
    while (*c) cel[i++] = *c++;
    cel[i] = '\0';
}

bool porownaj_tekst_n(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') break;
    }
    return true;
}

void pobierz_linie(char* bufor, int max_dlugosc) {
    int poz = 0;
    while (poz < max_dlugosc - 1) {
        char z = pobierz_znak();
        if (z == '\n' || z == '\r') break;
        if (z == '\b' && poz > 0) {
            poz--;
            wypisz("\b");
        } else if (z >= 32 && z <= 126) {
            bufor[poz++] = z;
            char tmp[2] = {z, '\0'};
            wypisz(tmp);
        }
    }
    bufor[poz] = '\0';
}

extern "C" __attribute__((noreturn)) void _start() {
    wypisz("\n==========================================\n");
    wypisz("   Instalator Paczek (.cebula) v1.0       \n");
    wypisz("==========================================\n");
    wypisz("Podaj sciezke do pliku .cebula (np. /tymczasowe/gra.cebula):\n> ");

    char sciezka_zrodlowa[128];
    pobierz_linie(sciezka_zrodlowa, 128);
    wypisz("\n\n");

    // Dynamicznie zamawiamy duży bufor z jądra (BWS 35) aby wczytać plik
    // Załóżmy maks 2 MB na paczkę (można powiększyć)
    uint64_t adres_bufora = bws_wywolaj(35, 2 * 1024 * 1024);
    if (adres_bufora == 0) {
        wypisz("[BLAD] Brak pamieci RAM na rozpakowanie paczki!\n");
        bws_wywolaj(32);
    }
    
    char* bufor_paczki = (char*)adres_bufora;

    wypisz("[1] Wczytywanie pliku...\n");
    if (bws_wywolaj(5, (uint64_t)sciezka_zrodlowa, (uint64_t)bufor_paczki, 2 * 1024 * 1024) == 0) {
        wypisz("[BLAD] Nie znaleziono pliku lub blad odczytu!\n");
        bws_wywolaj(32);
    }

    wypisz("[2] Analizowanie naglowka paczki...\n");
    NaglowekCebula* naglowek_paczki = (NaglowekCebula*)bufor_paczki;

    if (!porownaj_tekst_n(naglowek_paczki->magia, "CEBULA01", 8)) {
        wypisz("[BLAD] To nie jest poprawny plik .cebula (zla magia)!\n");
        bws_wywolaj(32);
    }

    wypisz("[INFO] Wykryto paczke: ");
    wypisz(naglowek_paczki->nazwa_katalogu);
    wypisz("\n");

    // Budowanie sciezek
    char sciezka_folderu[128];
    zlacz_teksty(sciezka_folderu, "/programy/", naglowek_paczki->nazwa_katalogu, ".cebula");

    char sciezka_manifestu[128];
    zlacz_teksty(sciezka_manifestu, sciezka_folderu, "/opis.aplikacji", "");

    char sciezka_binarki[128];
    zlacz_teksty(sciezka_binarki, sciezka_folderu, "/", naglowek_paczki->nazwa_pliku_bur);

    wypisz("[3] Tworzenie struktury katalogow...\n");
    // UWAGA: Aktualnie w BWS 2 utworz_plik używasz też dla ścieżek, 
    // jeżeli w przyszłości dodasz BWS 44 (utworz_katalog), użyj go tutaj!
    bws_wywolaj(2, (uint64_t)sciezka_manifestu); 
    bws_wywolaj(2, (uint64_t)sciezka_binarki);

    wypisz("[4] Rozpakowywanie manifestu (opis.aplikacji)...\n");
    char* wyluskany_manifest = bufor_paczki + sizeof(NaglowekCebula);
    if (bws_wywolaj(3, (uint64_t)sciezka_manifestu, (uint64_t)wyluskany_manifest, naglowek_paczki->rozmiar_manifestu) == 0) {
        wypisz("[BLAD] Nie udalo sie zapisac manifestu!\n");
    }

    wypisz("[5] Rozpakowywanie kodu programu (.bur)...\n");
    char* wyluskana_binarka = bufor_paczki + sizeof(NaglowekCebula) + naglowek_paczki->rozmiar_manifestu;
    if (bws_wywolaj(3, (uint64_t)sciezka_binarki, (uint64_t)wyluskana_binarka, naglowek_paczki->rozmiar_programu) == 0) {
        wypisz("[BLAD] Nie udalo sie zapisac pliku .bur!\n");
    }

    wypisz("\n==========================================\n");
    wypisz(" SUKCES! Aplikacja zostala zainstalowana. \n");
    wypisz(" Mozesz ja znalezc w folderze: \n ");
    wypisz(sciezka_folderu);
    wypisz("\n==========================================\n");

    bws_wywolaj(32); // Zakończ proces
    while(true) {}
}