#pragma once
#include "instalator_format.h"
#include "pzb.h"
void instalator_boot(uint64_t mbi);
bool instalator_tryb();
bool instalator_system_zainstalowany();
void instalator_autoryzuj(int pid);
void instalator_zamknij_proces(int pid);
bool instalator_obsluz(const proces_t&,const InstalatorZadanie&,InstalatorOdpowiedz*);
bool kernel_instaluj_system();
bool kernel_instaluj_system_start(uint64_t* razem);
bool kernel_instaluj_system_step(uint64_t* wykonane,uint32_t* pliki,uint32_t* wszystkie,bool* gotowe);
bool kernel_weryfikuj_system();
bool kernel_weryfikuj_system_start(uint64_t* razem);
bool kernel_weryfikuj_system_step(uint64_t* wykonane,bool* gotowe);
uint32_t kernel_liczba_plikow_instalacji();
