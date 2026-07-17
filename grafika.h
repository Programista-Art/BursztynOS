#pragma once
#include <stdint.h>

void InicjalizujGrafike(uint64_t adres_info_multiboot);
void WypiszLog(const char* tekst);
extern "C" void wypisz_na_ekranie(const char* tekst);