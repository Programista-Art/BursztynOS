/*
 * Bursztyn OS - publiczne ABI BWS diagnostyki HDA
 *
 * Wspoldzielone miedzy jadrem a Ring 3.
 * Kazda struktura publiczna ma pola wersja i rozmiar.
 *
 * Numery BWS (zamrozone):
 *   89 = BWS_HDA_DIAG   - wylacznie do odczytu diagnostycznego
 *   90 = BWS_HDA_TEST   - kontrolowany test odtwarzania
 *
 * Nie istnieje syscall z surowymi verbami HDA.
 * Ring3 nie otrzymuje adresow fizycznych ani mapowan MMIO.
 */

#pragma once

#include <stdint.h>
#include "sterowniki/dzwiek/hda_diag_format.h"

/*
 * Polecenia BWS_HDA_DIAG (arg1).
 */
enum BwsHdaDiagPolecenie : uint64_t {
    BWS_HDA_DIAG_STAN = 1,
    BWS_HDA_DIAG_SKAN_START = 2,
    BWS_HDA_DIAG_SKAN_KROK = 3,
    BWS_HDA_DIAG_SNAPSHOT = 4,
    BWS_HDA_DIAG_STRUMIEN = 5,
    BWS_HDA_DIAG_TEST_STAN = 6,
    BWS_HDA_DIAG_SKAN_ANULUJ = 7
};

/*
 * Polecenia BWS_HDA_TEST (arg1).
 */
enum BwsHdaTestPolecenie : uint64_t {
    BWS_HDA_TEST_START = 1,
    BWS_HDA_TEST_STOP = 2
};

/*
 * Argumenty BWS_HDA_TEST_START (arg2..arg5).
 */
struct BwsHdaTestStart {
    uint32_t wersja;
    uint32_t rozmiar;
    uint32_t tryb;             /* HdaDiagTrybTestu */
    uint32_t czestotliwosc_hz;
    uint32_t czas_ms;
    uint8_t kanaly;            /* bit0=L, bit1=R */
    uint8_t rezerwa[3];
};

/*
 * Zadanie snapshotu: uzytkownik wskazuje bufor.
 * Kernel waliduje wskaznik i dlugosc przez BWS safety layer.
 */
struct BwsHdaSnapshotZadanie {
    uint32_t wersja;
    uint32_t rozmiar;          /* sizeof(BwsHdaSnapshotZadanie) */
    uint64_t bufor;            /* adres uzytkownika na HdaDiagSnapshot */
    uint32_t bufor_maks;       /* pojemnosc bufora w bajtach */
    uint32_t rezerwa;
    uint32_t generacja;        /* OUT: generacja po sukcesie */
    uint32_t zapisano;         /* OUT: faktyczna liczba bajtow */
};

/*
 * Statyczna kontrola rozmiarow ABI.
 */
static_assert(sizeof(BwsHdaTestStart) == 24, "BwsHdaTestStart layout");
static_assert(sizeof(BwsHdaSnapshotZadanie) == 32, "BwsHdaSnapshotZadanie layout");
