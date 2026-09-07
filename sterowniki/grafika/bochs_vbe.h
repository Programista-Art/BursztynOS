#pragma once
#include "../../ekran_format.h"
bool bochs_wykryj(uint64_t boot_lfb, const TrybEkranu& boot, uint64_t* vram);
bool bochs_sprawdz_tryb(const TrybEkranu&);
bool bochs_ustaw_tryb(const TrybEkranu&);
