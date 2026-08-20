/*
 * =====================================================================
 *  Bursztyn OS SDK - Zarządzanie Pamięcią Ring 3
 *  Implementacja sterty i operatorów C++ w oparciu o BWS 35
 * =====================================================================
 */

#include "bursztyn_api.h"
#include <stddef.h>

// Prosty alokator Bump-Pointer dla Ring 3 (Zastępuje Twoje gui_malloc)
// Zamawia fizyczne ramki z Jądra (BWS 35) tylko, gdy brakuje mu miejsca!

static uint8_t* sterta_obecny_wskaznik = nullptr;
static uint64_t sterta_pozostale_miejsce = 0;

extern "C" void* b_malloc(unsigned long rozmiar) {
    if (rozmiar == 0) return nullptr;

    // Wyrównanie do 8 bajtów (bardzo ważne dla C++)
    unsigned long zaokraglony = (rozmiar + 7) & ~7;

    // Jeżeli nie mamy miejsca w obecnym buforze, zamawiamy nowe strony z Jądra
    if (sterta_pozostale_miejsce < zaokraglony) {
        // Zamawiamy wielokrotność 4KB
        unsigned long do_zamowienia = (zaokraglony + 4095) & ~4095;
        
        void* nowa_pamiec = b_sbrk(do_zamowienia);
        if (!nowa_pamiec) return nullptr; // Jądro odmówiło (Brak RAM lub przekroczono limit)

        if (!sterta_obecny_wskaznik) {
            sterta_obecny_wskaznik = (uint8_t*)nowa_pamiec;
        }
        sterta_pozostale_miejsce += do_zamowienia;
    }

    void* zaalokowane = sterta_obecny_wskaznik;
    sterta_obecny_wskaznik += zaokraglony;
    sterta_pozostale_miejsce -= zaokraglony;

    return zaalokowane;
}

// W prostym alokatorze Bump-Pointer funkcja free nic nie robi.
// W przyszłości możesz zaimplementować tu strukturę połączonej listy (Linked List).
extern "C" void b_free(void* ptr) {
    (void)ptr;
}

// =====================================================================
// GLOBALNE OPERATORY C++ (Aby klasa std::string i wektory działały)
// =====================================================================

void* operator new(unsigned long rozmiar) {
    return b_malloc(rozmiar);
}

void* operator new[](unsigned long rozmiar) {
    return b_malloc(rozmiar);
}

void operator delete(void* p) noexcept {
    b_free(p);
}

void operator delete[](void* p) noexcept {
    b_free(p);
}

void operator delete(void* p, unsigned long) noexcept {
    b_free(p);
}

void operator delete[](void* p, unsigned long) noexcept {
    b_free(p);
}
