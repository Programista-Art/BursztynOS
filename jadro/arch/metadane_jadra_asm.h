/*
 * Bursztyn OS - stale metadanych natywnego wejscia dla asemblera (Faza R1B).
 * Jedno zrodlo prawdy dla wejscie64.S; jadro/arch/metadane_jadra.h sprawdza zgodnosc static_assert.
 */
#ifndef BURSZTYN_METADANE_JADRA_ASM_H
#define BURSZTYN_METADANE_JADRA_ASM_H

#define METADANE_SYGNATURA          0x4F5244414A425242   /* "BRBJADRO" */
#define METADANE_SYGNATURA_KONCOWA  0xB0ADBBBEB5BDADBD   /* ~SYGNATURA */
#define METADANE_WERSJA             1
#define METADANE_ROZMIAR            64
#define METADANE_KONTRAKT_R1B       1
#define METADANE_WYROWNANIE         0x1000
#define METADANE_CECHY              0xF

#endif
