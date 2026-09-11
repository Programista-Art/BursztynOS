#pragma once
#include "storage_format.h"
void blokowy_inicjalizuj();
Storage::Device* blokowy_dysk(uint32_t id);
bool blokowy_znajdz_bsp(uint32_t ordinal, Storage::Partition* out);
bool blokowy_znajdz_esp(uint32_t ordinal, Storage::Partition* out);
bool blokowy_esp_systemowy(Storage::Partition* out);
Storage::Device* blokowy_dysk_systemowy();
void blokowy_zezwol_instalatorowi(uint32_t id,bool enabled);
bool blokowy_zapis_instalatora();
