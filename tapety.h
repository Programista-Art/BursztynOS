#pragma once
#include "tapety_format.h"
#include "pzb.h"

void tapety_inicjalizuj(uint64_t mbi = 0);
bool tapety_instaluj_sprawdz(uint64_t mbi);
bool tapety_weryfikuj_instalacje();
bool tapety_obsluz(const proces_t&, const TapetyZadanie&, TapetyOdpowiedz*);
void bws_ipc_inicjalizuj_tapety();
// Przygotowanie nowego rozmiaru do prywatnego bufora transakcji; bez publikacji.
bool tapety_przygotuj_ekran(uint32_t* dst,uint32_t w,uint32_t h,size_t cap);
