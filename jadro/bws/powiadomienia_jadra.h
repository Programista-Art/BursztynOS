#pragma once
/*
 * Bursztyn OS - powiadomienia systemowe z kodu jadra (Bursztynowy Zasobnik, docs/bursztynowy-zasobnik.md).
 * Wywolywac poza przerwaniami (sekcja krytyczna jest krotka, bez alokacji). Zrodlo zawsze SYSTEM - tylko kod jadra
 * moze tworzyc takie wpisy; aplikacje zglaszaja przez BWS 109 z nadawca ustalanym przez jadro.
 */
#include <stdint.h>
#include "zasobnik_api.h"

void powiadomienie_systemowe(uint32_t poziom, const char* tytul, const char* tresc);
void powiadomienia_posprzataj_proces(int pid);
