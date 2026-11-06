#pragma once
#include "storage_format.h"
void blokowy_inicjalizuj();
uint32_t blokowy_liczba_dyskow();
bool blokowy_zarejestruj_dysk(Storage::Device* dev);
Storage::Device* blokowy_dysk(uint32_t id);
bool blokowy_sprawdz_tozsamosc(uint32_t id);
uint64_t blokowy_licznik_zapisow(uint32_t id = UINT32_MAX);
bool blokowy_znajdz_bsp(uint32_t ordinal, Storage::Partition* out);
bool blokowy_znajdz_esp(uint32_t ordinal, Storage::Partition* out);
bool blokowy_esp_systemowy(Storage::Partition* out);
Storage::Device* blokowy_dysk_systemowy();
void blokowy_zezwol_instalatorowi(uint32_t id,bool enabled);
bool blokowy_zapis_instalatora();
