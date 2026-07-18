/*
 * Mechanizm: API Wywołań Systemowych (Syscalls) w konwencji Bursztyn OS
 * Opis: Punkt docelowy C++ odbierający przełączone w locie asynchroniczne żądania
 * wywołane ze strefy programu Ring 3 z wykorzystaniem MSR_LSTAR. Definiuje procedurę
 * konfiguracyjną i dyspozytor odpowiadający konwencji `snake_case`.
 */

#include <stdint.h>
#include <stdbool.h>

// Prototyp zewnętrznej funkcji API jądra dla ekranu i operacji w systemie plików (Z Etapu 5)
extern "C" {
    void wypisz_na_ekranie(const char* buf); 
    bool utworz_plik(const char* sciezka);
    bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc);
    char pobierz_znak_klawiatury(); 
    bool czytaj_z_pliku(const char* sciezka, char* bufor, uint32_t max_dlugosc);
    bool wylistuj_katalog(const char* sciezka, char* bufor, uint32_t max_dlugosc);
}

// Zewnętrzny odnośnik do punktu wejściowego SYSCALL zakodowanego w Asemblerze
extern "C" void brama_wywolan_systemowych();

// Zewnętrzny adres ze strefy linkera symbolizujący środek stosu Ring 0
extern "C" uint64_t stack_top;

// Globalna zmienna przechowująca wskaźnik na rdzenny stos.
uint64_t bezpieczny_stos_jadra;

// Pomocnicza funkcja C przestrzeni zapisującej do Model Specific Registers (MSR)
static inline void zapisz_msr(uint32_t msr, uint64_t wartosc) {
    uint32_t dolny = (uint32_t)(wartosc & 0xFFFFFFFF);
    uint32_t gorny = (uint32_t)(wartosc >> 32);
    asm volatile("wrmsr" : : "a"(dolny), "d"(gorny), "c"(msr));
}

static inline uint64_t odczytaj_msr(uint32_t msr) {
    uint32_t dolny, gorny;
    asm volatile("rdmsr" : "=a"(dolny), "=d"(gorny) : "c"(msr));
    return ((uint64_t)gorny << 32) | dolny;
}

extern "C" void inicjalizuj_syscalls() {
    uint64_t efer = odczytaj_msr(0xC0000080);
    zapisz_msr(0xC0000080, efer | 1); 

    uint32_t star_gorny = (0x13 << 16) | 0x08; 
    zapisz_msr(0xC0000081, ((uint64_t)star_gorny << 32));

    zapisz_msr(0xC0000082, (uint64_t)&brama_wywolan_systemowych);
    zapisz_msr(0xC0000084, 0x200);

    bezpieczny_stos_jadra = (uint64_t)&stack_top;
    zapisz_msr(0xC0000102, (uint64_t)&bezpieczny_stos_jadra);
}

/*
 * --- DYSPOZYTOR KODÓW (Bursztyn OS ABI) ---
 */
extern "C" uint64_t obsluga_wywolan_systemowych(uint64_t nr_funkcji, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    (void)arg4; 

    uint64_t kod_wyniku = 0;

    switch(nr_funkcji) {
        case 1: {
            // sys_wypisz_tekst(const char* tekst)
            wypisz_na_ekranie((const char*)arg1);
            kod_wyniku = 1; 
            break;
        }

        case 2: {
            // sys_utworz_plik(const char* sciezka)
            bool result = utworz_plik((const char*)arg1);
            kod_wyniku = result ? 1 : 0;
            break;
        }

        case 3: {
            // sys_zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc)
            bool result = zapisz_do_pliku((const char*)arg1, (const char*)arg2, (uint32_t)arg3);
            kod_wyniku = result ? 1 : 0;
            break;
        }

        case 4: {
            // sys_pobierz_znak()
            kod_wyniku = (uint64_t)pobierz_znak_klawiatury();
            break;
        }

        case 5: {
            // sys_czytaj_z_pliku
            bool result = czytaj_z_pliku((const char*)arg1, (char*)arg2, (uint32_t)arg3);
            kod_wyniku = result ? 1 : 0;
            break;
        }

        case 6: {
            // sys_wylistuj_katalog
            bool result = wylistuj_katalog((const char*)arg1, (char*)arg2, (uint32_t)arg3);
            kod_wyniku = result ? 1 : 0;
            break;
        }

        default:
            wypisz_na_ekranie("[!] Otrzymano nierozpoznany wektor z Ring 3!");
            kod_wyniku = (uint64_t)-1;
            break;
    }

    return kod_wyniku;
}