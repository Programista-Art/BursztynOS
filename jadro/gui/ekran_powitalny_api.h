#pragma once
#include <stdint.h>
// System presentation layer; never a BWS application.
void powitanie_inicjalizuj(bool wlacz);
void powitanie_pulpit_gotowy(bool tapeta=false,int pid=0);
void powitanie_tick();
bool powitanie_aktywne();
// True after WM/kernel desktop handshake; splash may still be fading.
bool powitanie_pulpit_potwierdzony();
uint8_t* powitanie_zrodlo(uint8_t* scena,int w,int h,int pitch,int x,int y,int rw,int rh,uint32_t skala);
// Wszystkie wywolania pod istniejaca blokada ekranu (takze kopiowanie zrodla).
// Wyciszanie nie pobiera blokad ani nie wysyla polecen GPU.
void powitanie_wycisz();
void powitanie_reset_ekranu();
// Optional counter of completed compositor transactions, no per-frame logging.
void powitanie_diag_prezentacja();

// Under the existing screen lock. A new size/DPI/surface invalidates readiness.
bool powitanie_pierwsza_klatka_gotowa();
bool powitanie_pierwsza_publikacja();
void powitanie_publikacja();
