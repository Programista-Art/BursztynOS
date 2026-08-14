/*
 * Bursztyn OS - dynamiczna sterta jadra.
 *
 * Alokator jest calkowicie freestanding. Obszar sterty musi zostac najpierw
 * zmapowany przez VMM, a nastepnie przekazany do inicjalizuj_sterte_jadra().
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * Naglowek ma wyrownanie 16-bajtowe. Dzieki temu sizeof(BlokSterty) jest
 * wielokrotnoscia 16 i dane uzytkownika lezace bezposrednio za naglowkiem
 * rowniez zachowuja wymagane wyrownanie.
 */
struct alignas(16) BlokSterty {
    uint64_t rozmiar;          // Liczba bajtow dostepnych za naglowkiem.
    bool czy_wolny;            // true - blok wolny, false - zajety.
    BlokSterty* nastepny;      // Nastepny blok w kolejnosci adresow.
    BlokSterty* poprzedni;     // Poprzedni blok w kolejnosci adresow.
    uint32_t magic;            // Sygnatura pozwalajaca rozpoznac nasz blok.
};

static_assert(alignof(BlokSterty) == 16,
              "BlokSterty musi miec wyrownanie 16 bajtow");
static_assert((sizeof(BlokSterty) % 16) == 0,
              "Rozmiar naglowka musi byc wielokrotnoscia 16 bajtow");

void inicjalizuj_sterte_jadra(void* adres_poczatkowy,
                              uint64_t rozmiar_poczatkowy);
void* kmalloc(uint64_t rozmiar);
void kfree(void* wskaznik);

