#pragma once
#include "tapety_format.h"
#include "pzb.h"

void tapety_inicjalizuj(uint64_t mbi = 0);
bool tapety_instaluj_sprawdz(uint64_t mbi);
bool tapety_instaluj_start(uint64_t mbi,uint64_t* razem);
bool tapety_instaluj_step(uint64_t* wykonane,uint32_t* elementy,uint32_t* wszystkie,bool* gotowe);
uint32_t tapety_liczba_zainstalowanych();
uint32_t tapety_liczba_wymaganych();
inline const char* tapety_komunikat_braku_pakietu(bool tryb_zainstalowany,
                                                  bool zasoby_bsp2_dostepne) {
    return tryb_zainstalowany && zasoby_bsp2_dostepne
        ? "[TAPETA] Brak tapety.pak w trybie zainstalowanym; uzywam zasobow BSP2."
        : "[TAPETA] Ostrzezenie: brak prawidlowego modulu Multiboot2 tapety.pak; zachowuje istniejace tapety.";
}
bool tapety_weryfikuj_instalacje();
bool tapety_weryfikuj_start(uint64_t* razem);
bool tapety_weryfikuj_step(uint64_t* wykonane,bool* gotowe);
bool tapety_obsluz(const proces_t&, const TapetyZadanie&, TapetyOdpowiedz*);
void bws_ipc_inicjalizuj_tapety();
// Przygotowanie nowego rozmiaru do prywatnego bufora transakcji; bez publikacji.
bool tapety_przygotuj_ekran(uint32_t* dst,uint32_t w,uint32_t h,size_t cap);
