/*
 * Bursztyn OS - aktualizacja systemu (interfejs wewnetrzny jadra).
 *
 * Rozdzial odpowiedzialnosci:
 *   - aktualizacja_format.h: czysty parser (fail-closed, bez I/O),
 *   - ten modul: kryptografia (SHA-256, RSA), PSF (pobieranie/staging),
 *     siec (HTTPS), polityka PZB, logi faz po polsku.
 *
 * BWS ABI nie jest zmieniane. Modul nie dodaje nowych numerow BWS.
 * Udostepnia wewnetrzne API jadra + samotest wywolywany z trybu
 * testow automatycznych (kontrolowany interfejs testowy).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pzb.h"
#include "aktualizacja_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sciezki systemowe aktualizacji (PL). */
#define AKT_SCIEZKA_KATALOG        "/aktualizacje"
#define AKT_SCIEZKA_PRZYGOTOWANIE  "/aktualizacje/przygotowanie"
#define AKT_SCIEZKA_POBIERANIE_TMP "/aktualizacje/pobieranie.tmp"
#define AKT_SCIEZKA_WYKAZ          "/aktualizacje/wykazaktualizacji.wykaz"
#define AKT_SCIEZKA_WERSJA         "/system/wersja"
#define AKT_SCIEZKA_STAN           "/aktualizacje/stan"

/* Polityka PZB dla aktualizacji. */
#define AKT_PZB_SPRAWDZANIE   PZB_ZAUFANE
#define AKT_PZB_POBIERANIE    PZB_ZAUFANE
#define AKT_PZB_WERYFIKACJA   PZB_ZAUFANE
#define AKT_PZB_INSTALACJA    PZB_USLUGI

/* Etapy aktualizacji (logi PL). */
typedef enum akt_etap {
    AKT_ETAP_SPRAWDZANIE = 0,
    AKT_ETAP_POBIERANIE_WYKAZU = 1,
    AKT_ETAP_WERYFIKACJA_WYKAZU = 2,
    AKT_ETAP_POBIERANIE_PAKIETU = 3,
    AKT_ETAP_WERYFIKACJA_PAKIETU = 4,
    AKT_ETAP_PRZYGOTOWANIE = 5,
    AKT_ETAP_INSTALACJA = 6,
    AKT_ETAP_WERYFIKACJA_INSTALACJI = 7,
    AKT_ETAP_GOTOWA = 8,
    AKT_ETAP_BLAD = 9
} akt_etap_t;

/* SHA-256 przez mbedtls. */
bool aktualizacja_sha256(const uint8_t* dane, size_t len, uint8_t wynik[32]);

/* Weryfikacja podpisu RSA-2048/PKCS1v15/SHA-256 nad danymi kanonicznymi. */
bool aktualizacja_weryfikuj_podpis(const uint8_t* dane, size_t len,
                                   const uint8_t podpis[256]);

/* Weryfikacja wykazu: parser + podpis. */
bool aktualizacja_weryfikuj_wykaz(const char* dane, size_t len, akt_wykaz_t* wykaz_out);

/* Weryfikacja pakietu w pamieci wobec zweryfikowanego wykazu
 * (struktura, rozmiar, SHA-256 calosci, per-file SHA-256). */
bool aktualizacja_weryfikuj_pakiet(const uint8_t* dane, size_t len,
                                   const akt_wykaz_t* wykaz);

/* Biezaca wersja systemu (z /system/wersja albo wbudowana). */
bool aktualizacja_biezaca_wersja(akt_wersja_t* out);

/* PZB: ktory poziom moze dana faze. */
bool aktualizacja_czy_moze_sprawdzac(const proces_t* proces);
bool aktualizacja_czy_moze_pobierac(const proces_t* proces);
bool aktualizacja_czy_moze_weryfikowac(const proces_t* proces);
bool aktualizacja_czy_moze_instalowac(const proces_t* proces);

/* Przygotowanie katalogow i pliku wersji. Idempotentne. */
void aktualizacja_przygotuj_system_plikow(void);

/* Pobranie pliku przez HTTPS (lub HTTP gdy https==false) do pliku tymczasowego.
 * Zwraca true tylko gdy pobrano w calosci; przy bledzie plik tmp jest usuwany
 * (lub pozostaje do bezpiecznego odrzucenia) i system nie jest modyfikowany. */
bool aktualizacja_pobierz_do_tmp(const char* domena, const char* sciezka_http,
                                 bool https, uint64_t maks_rozmiar);

/* Instalacja wczesniej zweryfikowanego pakietu (staging -> commit).
 * Nigdy nie wywolywac bez pelnej weryfikacji. */
bool aktualizacja_zainstaluj_zweryfikowany(const uint8_t* dane, size_t len,
                                           const akt_wykaz_t* wykaz);

/* Rozruch i zarzadzanie slotami A/B */
void aktualizacja_sprawdz_rozruch(uint64_t mbi);
bool aktualizacja_potwierdz_rozruch(void);
uint8_t aktualizacja_aktywny_slot(void);
uint8_t aktualizacja_nieaktywny_slot(void);

/* Samotest parsersko-kryptograficzny (parsery + RSA na wektorze testowym).
 * Uruchamiany w trybie testow automatycznych; nie modyfikuje systemu. */
void aktualizacja_samotest_automatyczny(void);

#ifdef __cplusplus
}
#endif
