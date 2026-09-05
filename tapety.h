#pragma once
#include "tapety_format.h"
#include "pzb.h"

void tapety_inicjalizuj(uint64_t mbi = 0);
bool tapety_obsluz(const proces_t&, const TapetyZadanie&, TapetyOdpowiedz*);
void bws_ipc_inicjalizuj_tapety();
