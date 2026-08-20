#include <stdint.h>

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

extern "C" void main(); // Zewnętrzna funkcja main() pisana przez użytkownika
extern "C" __attribute__((noreturn)) void _start();

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        4096, 32768, 0x601000,   // Tekst pod adresem 0x601000
        36864, 131072, 0x609000  // Dane pod adresem 0x609000
    };
}

extern "C" __attribute__((noreturn)) void _start() {
    // Tutaj w przyszłości możesz parsować argumenty konsoli
    main();
    
    // Po zakończeniu main(), aplikacja musi się bezpiecznie zamknąć.
    // Zgodnie z dyspozytorem BWS, nr_funkcji 32 to SYS_EXIT, 
    // który usuwa warstwę obrazu i kończy proces.
    asm volatile(
        "mov $32, %%r8\n"
        "syscall\n"
        ::: "r8", "rcx", "r11", "memory"
    );
    while (true);
}