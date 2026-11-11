/*
 * Bursztyn OS - publiczne ABI listy procesow (BWS 96/97).
 *
 * BWS_PROCESY_LISTA wypelnia tablice BwsProcesInfo rzeczywistym stanem
 * tablica_procesow[]. Aplikacja (menedzer zadan, diagnostyka) widzi
 * dokladnie to, co scheduler faktycznie wykonuje - zadnych danych
 * generowanych z indeksu petli.
 *
 * BWS_PROCES_ZABIJ konczy proces o wskazanym PID. To jedyna droga do
 * zamkniecia zawieszonej aplikacji bez restartu systemu.
 */

#pragma once

#include <stdint.h>

inline constexpr uint32_t BWS_PROCESOW_ABI = 1U;

/* Maksymalna liczba wpisow jednego wywolania BWS_PROCESY_LISTA. */
inline constexpr uint32_t BWS_PROCESY_MAKS = 16U;

inline constexpr uint32_t BWS_PROCES_NAZWA_MAKS = 64U;

/* Stany procesu odzwierciedlaja scheduler.h (PROCES_*). */
struct BwsProcesInfo {
    uint32_t wersja;        /* BWS_PROCESOW_ABI */
    uint32_t pid;
    uint32_t stan;          /* PROCES_PUSTY..PROCES_KONCZACY */
    uint32_t poziom_pzb;    /* PZB 0..5 */
    uint64_t uprawnienia;   /* maska PRAWO_* */
    uint64_t pamiec_bajtow; /* zajete strony Ring 3 (szacunek z granicy sterty) */
    uint64_t sterta_bajtow; /* granica_sterty - RING3_HEAP_BASE */
    char nazwa[BWS_PROCES_NAZWA_MAKS]; /* nazwa pliku .bur, NUL-terminated */
};

/* Wartosci zwracane przez BWS_PROCES_ZABIJ. */
inline constexpr uint64_t BWS_ZABIJ_OK = 1U;
inline constexpr uint64_t BWS_ZABIJ_BRAK_PROCESU = 0U;
inline constexpr uint64_t BWS_ZABIJ_NIE_WOLNO = 0U;
