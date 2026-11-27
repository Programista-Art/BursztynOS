/*
 * BRB - podstawowe operacje na pamieci (freestanding).
 *
 * brb_* to implementacje wlasciwe (testowane na hoscie). W obrazie UEFI plik pamiec_podstawowa.cpp
 * eksportuje dodatkowo symbole memcpy/memset/memmove/memcmp, bo GCC moze generowac ich wywolania nawet
 * przy -ffreestanding (kopiowanie/zerowanie struktur). W testach hosta (BRB_TEST_HOSTA) symbole te pochodza
 * z biblioteki C hosta i BRB ich nie definiuje.
 */
#ifndef BRB_PAMIEC_PODSTAWOWA_H
#define BRB_PAMIEC_PODSTAWOWA_H

#include <stddef.h>

extern "C" {
void* brb_memcpy(void* cel, const void* zrodlo, size_t n);
void* brb_memmove(void* cel, const void* zrodlo, size_t n);
void* brb_memset(void* cel, int wartosc, size_t n);
int   brb_memcmp(const void* a, const void* b, size_t n);
}

#endif
