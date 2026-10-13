/*
 * Bursztyn OS - HDA diagnostyka: API jadra
 *
 * Kernel-owned snapshot diagnostyczny HDA.
 * Ring3 nie ma dostepu do MMIO, verbów ani adresów DMA. BAR PCI jest tylko metadana.
 *
 * Implementacja:
 *   hda_diag.cpp  - inkrementalny skan i publikacja atomowa
 *   hda_transport.cpp - wspolny asynchroniczny IC / CORB / RIRB
 *
 * Czysty format/dekodowanie:
 *   hda_diag_format.h (host-testowalny)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hda_diag_format.h"


/*
 * Inicjalizuje podsystem diagnostyczny (nie resetuje kontrolera).
 * Bezpieczna przed pelna inicjalizacja odtwarzania.
 */
bool hda_diag_inicjalizuj();

/*
 * Startuje (lub restartuje) inkrementalny skan topologii.
 * Nie wywoluj na sciezce krytycznego bootu.
 */
constexpr uint32_t HDA_DIAG_WSZYSTKIE_KONTROLERY = UINT32_MAX;
bool hda_diag_startuj_skan(uint32_t kontroler = HDA_DIAG_WSZYSTKIE_KONTROLERY);

/*
 * Wykonuje ograniczona porcje pracy skanu (max HDA_DIAG_SLOW_NA_KROK
 * faz jednej transakcji verb). Zwraca true, gdy skan zakonczony (gotowy lub blad).
 */
bool hda_diag_krok_skanu(uint32_t kontroler = HDA_DIAG_WSZYSTKIE_KONTROLERY);

/*
 * Stan skanu dla Ring3 (kopiowany przez BWS).
 */
void hda_diag_pobierz_stan(HdaDiagSkan* out, uint32_t kontroler = HDA_DIAG_WSZYSTKIE_KONTROLERY);
uint64_t hda_diag_maks_krok_ns();

/*
 * Anuluje biezacy skan jesli byl aktywny.
 */
void hda_diag_anuluj_skan(uint32_t kontroler = HDA_DIAG_WSZYSTKIE_KONTROLERY);

/*
 * Pobiera wskaznik do ostatnio opublikowanej pelnej migawki.
 * Zwraca nullptr jesli zaden skan sie jeszcze nie zakonczyl.
 */
const HdaDiagSnapshot* hda_diag_pobierz_opublikowana(uint32_t* generacja);

/*
 * Atomowa publikacja: kopiuje kompletna migawke do bufora uzytkownika
 * po stronie jadra. Nigdy nie zwraca polowy starej i polowy nowej.
 *
 * out, out_maks - bufor
 * zapisano      - faktyczna liczba bajtow
 * generacja     - numer generacji opublikowanej migawki
 */
bool hda_diag_pobierz_snapshot(
    HdaDiagSnapshot* out,
    uint32_t out_maks,
    uint32_t* zapisano,
    uint32_t* generacja
);

/*
 * Stan strumienia odtwarzania.
 */
bool hda_diag_pobierz_strumien(HdaDiagStrumien* out);

/*
 * Stan testu tonu.
 */
bool hda_diag_pobierz_test(HdaDiagStanTestu* out);

/*
 * Kontrolowany test odtwarzania (kernel-owned).
 * tryb: HdaDiagTrybTestu
 * kanaly: bit0=L, bit1=R
 *
 * Nie przyjmuje verbów, NID-ów ani adresów z Ring3.
 */
bool hda_diag_test_start(
    uint32_t tryb,
    uint32_t czestotliwosc_hz,
    uint32_t czas_ms,
    uint8_t kanaly
);

/* Natychmiastowe, bezpieczne zatrzymanie testowego strumienia. */
void hda_diag_test_stop();

/* Aktualizuje licznik czasu i auto-stop. Zwraca true gdy gra. */
bool hda_diag_test_tick(uint64_t teraz_ms);

/*
 * Sciezka odtwarzania uzywana przez test (raportowana przed startem).
 */
void hda_diag_pobierz_sciezke_testu(
    uint8_t* kontroler,
    uint8_t* cad,
    uint8_t* konwerter,
    uint8_t* pin,
    uint8_t* routing_fizyczny
);

/*
 * Odczyt surowego verb (tylko dla wewnetrznych warstw jadra).
 * Nigdy nie eksponowane przez BWS.
 */
bool hda_diag_wyslij_verb(uint32_t komenda, uint32_t* odpowiedz);
uint32_t hda_diag_verb12(uint8_t cad, uint8_t nid, uint16_t verb, uint8_t payload);
uint32_t hda_diag_verb4(uint8_t cad, uint8_t nid, uint8_t verb, uint16_t payload);
