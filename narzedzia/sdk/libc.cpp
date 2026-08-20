/*
 * Bursztyn OS - Biblioteka Standardowa dla Ring 3 (bursztyn_libc.cpp)
 * 
 * Ten plik należy kompilować z każdą aplikacją C++ użytkownika (np. przegladarka.bur).
 * Implementuje on własnego zarządcę pamięci (alokator blokowy), który
 * wykorzystuje BWS 35 (SYS_ZAMOW_PAMIEC) do pobierania surowych stron 4KB
 * od Jądra, a następnie dzieli je na mniejsze obiekty dla C++ (new/delete).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


// Bezpieczne wywołanie BWS zgodne z Twoim nowym ABI
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


// Nagłówek opisujący każdy zaalokowany (lub zwolniony) kawałek pamięci w Ring 3.
struct NaglowekBloku {
    size_t rozmiar;         // Rozmiar użyteczny (bez nagłówka)
    bool wolny;             // Czy blok jest dostępny?
    NaglowekBloku* nastepny; // Wskaźnik na następny blok na stercie
};

// Globalny wskaźnik na początek sterty procesu (początek listy)
NaglowekBloku* poczatek_sterty_ring3 = nullptr;

// Rozmiar strony w Bursztyn OS to 4096 bajtów
constexpr size_t ROZMIAR_STRONY = 4096;


extern "C" void* malloc(size_t rozmiar) {
    if (rozmiar == 0) return nullptr;

    // Wyrównanie do 8 bajtów (bardzo ważne dla architektury x86_64)
    size_t zaokraglony_rozmiar = (rozmiar + 7) & ~7;
    
    NaglowekBloku* obecny = poczatek_sterty_ring3;
    NaglowekBloku* ostatni = nullptr;

    // 1. Spróbuj znaleźć wolny blok (First-Fit)
    while (obecny != nullptr) {
        if (obecny->wolny && obecny->rozmiar >= zaokraglony_rozmiar) {
            obecny->wolny = false;
            // Zwracamy adres zaraz za nagłówkiem
            return (void*)((uint8_t*)obecny + sizeof(NaglowekBloku));
        }
        ostatni = obecny;
        obecny = obecny->nastepny;
    }

    // 2. Jeśli nie znaleziono wolnego bloku, musimy prosić Jądro o nową pamięć (BWS 35)
    size_t potrzebny_rozmiar = zaokraglony_rozmiar + sizeof(NaglowekBloku);
    
    // Obliczamy ile pełnych stron (4KB) potrzebujemy
    size_t zamawiane_bajty = (potrzebny_rozmiar + ROZMIAR_STRONY - 1) & ~(ROZMIAR_STRONY - 1);

    // Wywołanie BWS 35 - Twoja funkcja bws_alokuj_sterte w jądrze!
    uint64_t nowy_obszar = bws_wywolaj(35, zamawiane_bajty);
    
    if (nowy_obszar == 0) {
        return nullptr; // Brak pamięci (Jądro odmówiło alokacji)
    }

    // Konfiguracja nowego bloku w zwróconej pamięci
    NaglowekBloku* nowy_blok = (NaglowekBloku*)nowy_obszar;
    nowy_blok->rozmiar = zamawiane_bajty - sizeof(NaglowekBloku);
    nowy_blok->wolny = false;
    nowy_blok->nastepny = nullptr;

    if (ostatni != nullptr) {
        ostatni->nastepny = nowy_blok;
    } else {
        poczatek_sterty_ring3 = nowy_blok;
    }

    return (void*)((uint8_t*)nowy_blok + sizeof(NaglowekBloku));
}


extern "C" void free(void* ptr) {
    if (!ptr) return;

    // Cofamy się o rozmiar nagłówka, aby odczytać metadane
    NaglowekBloku* blok = (NaglowekBloku*)((uint8_t*)ptr - sizeof(NaglowekBloku));
    
    // Zaznaczamy blok jako wolny, aby malloc mógł go użyć ponownie
    blok->wolny = true;

    // Opcjonalnie: Tutaj w przyszłości można dodać "scalanie" (merging) sąsiednich wolnych bloków
}


// Przeciążenie operatora new - łączy język C++ z naszym alokatorem
void* operator new(size_t size) {
    return malloc(size);
}

// Przeciążenie operatora new dla tablic
void* operator new[](size_t size) {
    return malloc(size);
}

// Przeciążenie operatora delete
void operator delete(void* p) noexcept {
    free(p);
}

// Przeciążenie operatora delete dla tablic
void operator delete[](void* p) noexcept {
    free(p);
}

// Specjalne operatory delete wymagane przez nowsze standardy C++ (np. C++14)
void operator delete(void* p, size_t size) noexcept {
    (void)size; // Rozmiar jest ignorowany, nasz NaglowekBloku już go zna
    free(p);
}

void operator delete[](void* p, size_t size) noexcept {
    (void)size;
    free(p);
}