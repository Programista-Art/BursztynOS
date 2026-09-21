#pragma once
#include "ekran_format.h"
#include "pzb.h"
void ekran_inicjalizuj();
bool ekran_obsluz(const proces_t&,const EkranZadanie&,EkranOdpowiedz*);
void ekran_obsluz_timeout();
void ekran_posprzataj_proces(int pid);
uint32_t ekran_skala();
void bws_aktualizuj_ekran(uint32_t skala);
bool ekran_test_virtio_gpu_modeset();
