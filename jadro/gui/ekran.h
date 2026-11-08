#pragma once
#include "ekran_format.h"
#include "pzb.h"
void ekran_inicjalizuj();
bool ekran_obsluz(const proces_t&,const EkranZadanie&,EkranOdpowiedz*);
void ekran_obsluz_timeout();
void ekran_posprzataj_proces(int pid);
uint32_t ekran_skala();
void bws_aktualizuj_ekran(uint32_t skala);
bool ekran_test_virtio_gpu_modeset(bool capture_modes = false);

/*
 * Przelacza tryb ekranu z wnetrza jadra (bez przechodzenia przez BWS).
 *
 * Uzywane przez kompozytor, gdy medium (okno QEMU, monitor VM) zglosi nowa
 * geometrie. Przechodzi te sama transakcje co tryb uzytkownika, wiec ma
 * rollback: nieudane przelaczenie zostawia poprzedni, dzialajacy tryb.
 *
 * Zwraca EKRAN_OK (0) przy powodzeniu albo kod bledu z ekran_format.h.
 */
uint32_t ekran_zastosuj_tryb(const TrybEkranu& tryb, uint32_t skala,
                             int owner, bool persist, bool wymagaj_potwierdzenia);
