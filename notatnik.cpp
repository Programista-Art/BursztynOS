/*
 * Notatnik Avocado dla Bursztyn OS (Aplikacja Ring 3)
 * Wersja dystrybuowana w paczce .cebula
 */

#include <stdint.h>
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

// Deklaracja funkcji wejściowej z atrybutem noreturn
extern "C" __attribute__((noreturn)) void _start();

// Nagłówek ląduje na samym początku pliku (offset 0)
extern "C" __attribute__((section(".naglowek"), used))
const struct NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    (uint64_t)&_start,
    4096,  16384, 0x601000,  // Przesunięcie, rozmiar i adres wirtualny dla .tekst
    20480, 16384, 0x605000   // Przesunięcie, rozmiar i adres wirtualny dla .dane
};

// PRZENIESIONY BUFOR: Ląduje w sekcji .data, chroniąc stos Ring 3 przed przepełnieniem!
static char bufor[4096] __attribute__((section(".data"))) = {};

// 2. BRAMA WYWOŁAŃ SYSTEMOWYCH (BWS)
uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0) {
    register uint64_t r8 asm("r8") = nr_funkcji;
    register uint64_t r9 asm("r9") = arg1;
    register uint64_t r10 asm("r10") = arg2;
    register uint64_t r12 asm("r12") = arg3;
    register uint64_t r13 asm("r13") = arg4;
    register uint64_t rax asm("rax");

    asm volatile (
        "syscall"
        : "=a" (rax)
        : "r" (r8), "r" (r9), "r" (r10), "r" (r12), "r" (r13)
        : "rcx", "r11", "memory"
    );
    return rax;
}

// Interfejs API Avocado/Bursztyn
void print(const char* tekst) { bws_wywolaj(1, (uint64_t)tekst); }
bool utworz(const char* plik) { return bws_wywolaj(2, (uint64_t)plik) != 0; }
bool zapisz_plik(const char* plik, const char* dane, uint32_t dlugosc) { return bws_wywolaj(3, (uint64_t)plik, (uint64_t)dane, dlugosc) != 0; }
char getch() { return (char)bws_wywolaj(4); }

bool str_cmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 == *(const unsigned char*)s2;
}

// 3. GŁÓWNA LOGIKA NOTATNIKA
extern "C" __attribute__((noreturn)) void _start() {
    print("\n");
    print("==================================================\n");
    print(" Notatnik Avocado (Ring 3)\n");
    print("==================================================\n");
    print("Wpisz lokalizacje pliku do edycji (np. /notatka.txt): ");

    char sciezka[64];
    int sciezka_len = 0;
    while(true) {
        char c = getch();
        if(c == '\n' || c == '\r') { sciezka[sciezka_len] = '\0'; print("\n"); break; }
        if(c == '\b' && sciezka_len > 0) { sciezka_len--; print("\b \b"); }
        else if(c >= 32 && sciezka_len < 63) { sciezka[sciezka_len++] = c; char tmp[2]={c,0}; print(tmp); }
    }

    print("\n[ Wprowadzaj tekst. Aby zapisac i wyjsc, wpisz ':zapisz' w nowej linii ]\n");
    print("--------------------------------------------------\n");

    // Zerowanie globalnego bufora z sekcji .data
    for(int i=0; i<4096; i++) bufor[i] = 0;
    int bufor_len = 0;

    char linia[128];
    int linia_len = 0;

    while(true) {
        char c = getch();
        if(c == '\n' || c == '\r') {
            linia[linia_len] = '\0';
            print("\n");
            
            // Komenda specjalna zamykająca edytor
            if (str_cmp(linia, ":zapisz")) {
                break; 
            }

            // Przepisanie Linii do dużego bufora
            for(int i=0; i<linia_len; i++) {
                if (bufor_len < 4095) bufor[bufor_len++] = linia[i];
            }
            if (bufor_len < 4095) bufor[bufor_len++] = '\n';
            linia_len = 0;
        }
        else if(c == '\b') {
            if(linia_len > 0) { linia_len--; print("\b \b"); }
        }
        else if (c >= 32 && linia_len < 127) {
            linia[linia_len++] = c;
            char tmp[2]={c,0}; print(tmp);
        }
    }

    print("Zapisywanie dokumentu do systemu BSP64...\n");
    utworz(sciezka);
    if (zapisz_plik(sciezka, bufor, bufor_len)) {
        print("Zapisano pomyslnie! Notatnik zamkniety.\n");
    } else {
        print("Blad: Nie udalo sie zapisac pliku lub brak uprawnien.\n");
    }

    // Wywołanie nr 10 zastępuje ten proces z powrotem Powłoką Systemową!
    bws_wywolaj(10, (uint64_t)"/shell.bur");
    while(true);
}