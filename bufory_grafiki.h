#pragma once
#include <stddef.h>
#include <stdint.h>
// Wspolna galaz PML4, supervisor-only. Inicjalizacja przed pierwszym Ring3.
constexpr uint64_t BUFORY_GRAFIKI_BAZA=0xffff900000000000ULL;
constexpr size_t BUFORY_GRAFIKI_SLOT=64ULL*1024*1024;
constexpr unsigned BUFORY_GRAFIKI_SLOTOW=48;
bool bufory_grafiki_inicjalizuj();
void* bufor_grafiki_alokuj(size_t bajty);
void* bufor_grafiki_alokuj_strony(size_t bajty, void** out_ramki, size_t max_ramek, size_t* out_liczba_ramek);
void bufor_grafiki_zwolnij(void*);
uint64_t bufory_grafiki_bajty();
uint64_t bufory_grafiki_alokacje();
