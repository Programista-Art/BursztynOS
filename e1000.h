#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void inicjalizuj_e1000();
void e1000_wyslij_pakiet(void* dane, uint16_t dlugosc);
void e1000_obsluz_odbior(); // Funkcja nasłuchująca nowych pakietów

uint8_t* pobierz_mac_adres();

#ifdef __cplusplus
}
#endif