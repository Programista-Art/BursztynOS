/*
 * Bursztyn OS - dynamiczna sterta jadra.
 *
 * Freestandingowy alokator first-fit uzywany przez jadro.
 *
 * Wymagania:
 *  - obszar sterty musi byc wczesniej zmapowany przez VMM,
 *  - inicjalizuj_sterte_jadra() musi zostac wywolane przed kmalloc(),
 *  - implementacja pracuje na architekturze x86_64,
 *  - dane zwracane przez kmalloc() sa wyrownane do 16 bajtow.
 *
 * Implementacja heap.cpp zapewnia:
 *  - dzielenie wolnych blokow,
 *  - laczenie sasiadujacych wolnych blokow,
 *  - podstawowa walidacje integralnosci listy,
 *  - ochrone przed double-free i obcymi wskaznikami,
 *  - ochrone przed przepelnieniami adresow i rozmiarow,
 *  - synchronizacje kmalloc/kfree w wielozadaniowym jadrze.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * PARAMETRY ABI STERTY
 * ========================================================================= */

constexpr uint64_t HEAP_WYROWNANIE = 16ULL;

/*
 * Bursztyn OS jest obecnie systemem x86_64.
 * Heap przechowuje wskazniki bezposrednio w naglowkach blokow,
 * dlatego zmiana rozmiaru wskaznika zmienilaby ABI sterty.
 */
static_assert(sizeof(void*) == 8,
              "Sterta jadra Bursztyn OS wymaga architektury 64-bitowej");

static_assert(sizeof(uintptr_t) == 8,
              "uintptr_t musi miec 64 bity");

/* =========================================================================
 * NAGLOWEK BLOKU STERTY
 * ========================================================================= */

/*
 * BlokSterty znajduje sie bezposrednio przed obszarem zwracanym przez
 * kmalloc().
 *
 * Lista jest dwukierunkowa i uporzadkowana rosnaco wedlug adresow.
 * Poprawna implementacja heap.cpp zaklada, ze kolejny blok zaczyna sie
 * dokladnie za danymi poprzedniego bloku.
 */
struct alignas(16) BlokSterty {
    uint64_t rozmiar;      // Rozmiar obszaru danych za naglowkiem.
    bool czy_wolny;        // true = wolny, false = zajety.

    BlokSterty* nastepny;  // Nastepny blok w kolejnosci adresow.
    BlokSterty* poprzedni; // Poprzedni blok w kolejnosci adresow.

    uint32_t magic;        // Sygnatura integralnosci naglowka.
};

/*
 * Naglowek musi zachowywac wyrownanie kompatybilne z ABI x86_64.
 * Dzieki temu adres zwracany tuz za BlokSterty jest rowniez
 * wyrownany do 16 bajtow.
 */
static_assert(alignof(BlokSterty) == HEAP_WYROWNANIE,
              "BlokSterty musi miec wyrownanie 16 bajtow");

static_assert((sizeof(BlokSterty) % HEAP_WYROWNANIE) == 0,
              "Rozmiar BlokSterty musi byc wielokrotnoscia 16 bajtow");

static_assert(sizeof(BlokSterty) == 48,
              "Nieoczekiwany layout BlokSterty dla x86_64");

/* =========================================================================
 * API STERTY JADRA
 * ========================================================================= */

/*
 * Inicjalizuje sterte na juz zmapowanym obszarze pamieci.
 *
 * adres_poczatkowy:
 *   pierwszy bajt obszaru przeznaczonego na sterte.
 *
 * rozmiar_poczatkowy:
 *   liczba dostepnych bajtow.
 *
 * Nieudana inicjalizacja pozostawia sterte nieaktywna.
 */
void inicjalizuj_sterte_jadra(void* adres_poczatkowy,
                              uint64_t rozmiar_poczatkowy);

/*
 * Rezerwuje co najmniej 'rozmiar' bajtow.
 *
 * Zwraca:
 *   wskaznik wyrownany do 16 bajtow,
 *   nullptr dla rozmiaru 0, braku pamieci albo wykrycia uszkodzenia sterty.
 */
void* kmalloc(uint64_t rozmiar);

/*
 * Zwalnia blok otrzymany bezposrednio z kmalloc().
 *
 * Funkcja bezpiecznie ignoruje:
 *   - nullptr,
 *   - wskazniki spoza sterty,
 *   - wskazniki do srodka bloku,
 *   - ponowne zwolnienie tego samego bloku.
 */
void kfree(void* wskaznik);

/* =========================================================================
 * GLOBALNE OPERATORY C++
 * ========================================================================= */

/*
 * Definicje tych operatorow znajduja sie w heap.cpp i korzystaja z
 * kmalloc()/kfree(). Deklaracje sa tutaj, aby wszystkie moduly jadra
 * korzystaly z jednego, jawnego interfejsu alokacji.
 */
void* operator new(size_t rozmiar);
void* operator new[](size_t rozmiar);

void operator delete(void* wskaznik) noexcept;
void operator delete[](void* wskaznik) noexcept;

void operator delete(void* wskaznik,
                     size_t rozmiar) noexcept;

void operator delete[](void* wskaznik,
                       size_t rozmiar) noexcept;
