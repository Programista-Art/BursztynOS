#pragma once
#include "ekran_powitalny_api.h"
#include "skladacz_obrazu.h"
#include "grafika.h"
extern "C" void zablokuj_ekran();
extern "C" void odblokuj_ekran();
// Lock order: screen -> splash/compositor/graphics; release BEFORE GPU lock.
inline void wycisz_start_przed_restartem() {
    zablokuj_ekran();
    powitanie_wycisz();
    skladacz_obrazu_wycisz();
    grafika_wycisz();
    odblokuj_ekran();
}

