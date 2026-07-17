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
}

// Zewnętrzny odnośnik do punktu wejściowego SYSCALL zakodowanego w Asemblerze
extern "C" void brama_wywolan_systemowych();

// Zewnętrzny adres ze strefy linkera symbolizujący środek stosu Ring 0
extern "C" uint64_t stack_top;

// KRYTYCZNA ZMIANA: Globalna zmienna przechowująca wskaźnik na rdzenny stos.
// Dzięki temu asemblerowe 'movq %gs:0, %rsp' odczyta wartość z poprawnego obszaru RAM.
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

/*
 * Inicjuje maszynerię MSR od wywołań w trybie Fast (SYSCALL / SYSRET).
 * Zapisuje do odpowiednich rejestrów przesunięcia selektorów oraz rzut do Asemblera.
 */
extern "C" void inicjalizuj_syscalls() {
    // 1. Aktywuj operację instrukcji 'syscall' z przestrzeni EFER w procesorze
    uint64_t efer = odczytaj_msr(0xC0000080);
    zapisz_msr(0xC0000080, efer | 1); // Włącz bit System Call Enable (SCE)

    // 2. MSR_STAR (Ring 0 & Ring 3 Target Selectors)
    // Instrukcja SYSRET nakłada żelazne zasady dla GDT w x86_64:
    // SS dla Ring 3 procesor odczytuje jako = (STAR[63:48] + 8) | 3
    // CS dla Ring 3 procesor odczytuje jako = (STAR[63:48] + 16) | 3
    // Aby to zadziałało, w Twoim gdt.cpp DANE UŻYTKOWNIKA (User Data) MUSZĄ BYĆ PRZED KODEM (User Code)!
    // Baza = 0x13 (Co oznacza Segment Jądra Danych 0x10 + RPL 3).
    // Wtedy SS = 0x13 + 8 = 0x1B (Wpis 0x18 w GDT to odtąd User Data)
    // Wtedy CS = 0x13 + 16 = 0x23 (Wpis 0x20 w GDT to odtąd User Code)
    uint32_t star_gorny = (0x13 << 16) | 0x08; 
    zapisz_msr(0xC0000081, ((uint64_t)star_gorny << 32));

    // 3. MSR_LSTAR (Target RIP) - wskaźnik wejściowy bramy asemblera
    zapisz_msr(0xC0000082, (uint64_t)&brama_wywolan_systemowych);

    // 4. MSR_FMASK (Flagi ucinane przy wywołaniu). Wymuszamy 0x200 czyli zamknięcie powiadomień (Clear Interrupt Flag) dla bezpieczeństwa Jądra.
    zapisz_msr(0xC0000084, 0x200);

    // 5. Zabezpieczenie dla swapgs:
    // Do MSR_KernelGSBase ładujemy adres naszej bezpiecznej zmiennej, która trzyma adres stosu.
    bezpieczny_stos_jadra = (uint64_t)&stack_top;
    zapisz_msr(0xC0000102, (uint64_t)&bezpieczny_stos_jadra);
}

/*
 * --- DYSPOZYTOR KODÓW (Bursztyn OS ABI) ---
 * Przechwytuje wektory argumentów przygotowane przez warstwę C++ Calling Convention
 * (po wyjściu z tłumaczenia R8, R9 w Asemblerze) i zagnieżdża polecenia.
 */
extern "C" uint64_t obsluga_wywolan_systemowych(uint64_t nr_funkcji, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    (void)arg4; // W naszym testowym API nie używamy aż tylu w tym etapie, lecz ABI jest zachowane

    uint64_t kod_wyniku = 0;

    switch(nr_funkcji) {
        case 1: {
            // sys_wypisz_tekst(const char* tekst)
            // UWAGA: W przyszłości (PZB-0) będziesz musiał sprawdzać, czy wskaźnik arg1 faktycznie należy do Ring 3!
            wypisz_na_ekranie((const char*)arg1);
            kod_wyniku = 1; // Sukces
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

        default:
            wypisz_na_ekranie("[!] Otrzymano nierozpoznany wektor z Ring 3!");
            kod_wyniku = (uint64_t)-1;
            break;
    }

    return kod_wyniku; // Wartość powraca do Asemblera, a stamtąd do przestrzeni User via RAX.
}