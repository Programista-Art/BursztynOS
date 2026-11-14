/*
 * Bursztyn OS - weryfikacja podpisu paczek aplikacji (.cebula)
 *
 * Podpis chroni integralnosc pliku wykonywalnego przed uruchomieniem.
 * Bez poprawnego podpisu kluczem release loader odmawia startu paczki.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maksymalna dlugosc sciezki .bur wraz z rozszerzeniem ".podpis". */
inline constexpr size_t PODPIS_MAKS_SCIEZKI = 512;

enum class PodpisPaczkiWynik : uint8_t {
    Ok = 0,
    BrakPliku = 1,     /* paczka wymaga podpisu, ale <bur>.podpis nie istnieje */
    BladOdczytu = 2,   /* blad argumentow lub odczytu binarki */
    ZlyRozmiar = 3,    /* plik podpisu ma zla dlugosc (nie 256 B) */
    Niezgodnosc = 4,   /* podpis nie pasuje do tresci pliku */
    BrakSrodowiska = 5 /* crypto (mbedTLS) niedostepne */
};

/* Czytelny opis wyniku do logow i diagnostyki. */
extern "C" const char* podpis_paczki_blad(
    PodpisPaczkiWynik wynik
);

/*
 * Weryfikuje podpis pliku .bur.
 *
 * dane/rozmiar to pelna, zwalidowana zawartosc pliku wykonywalnego.
 * Funkcja czyta `<sciezka_programu>.podpis` z PSF i sprawdza podpis
 * RSA-2048 PKCS#1 v1.5 / SHA-256 kluczem release.
 *
 * Nie alokuje duzych buforow: podpis ma staly rozmiar 256 B, a skrot
 * jest liczony strumieniowo z przekazanego bufora.
 */
extern "C" PodpisPaczkiWynik podpis_paczki_weryfikuj(
    const char* sciezka_programu,
    const uint8_t* dane,
    uint64_t rozmiar
);

/*
 * SHA-256 (mbedTLS) calosci bufora; ten sam prymityw, ktorego uzywa weryfikacja
 * podpisu. Sluzy do porownania z wbudowanymi skrotami aplikacji systemowych
 * (tozsamosc_systemowa.h). Zwraca false przy bledzie crypto.
 */
extern "C" bool podpis_sha256(const uint8_t* dane, uint64_t rozmiar, uint8_t wynik[32]);

#include "tozsamosc_typy.h"
/*
 * Tozsamosc aplikacji systemowej (tozsamosc_systemowa.h): true, gdy SHA-256 `dane`
 * zgadza sie z wpisem tablicy dla DOKLADNIE tej sciezki i rozmiaru, albo gdy
 * `<sciezka>.podpis` jest poprawnym podpisem release tych bajtow.
 */
extern "C" bool tozsamosc_systemowa_zweryfikuj_tablica(
    const TozsamoscSystemowa::Wpis* tablica, size_t liczba,
    const char* sciezka, const uint8_t* dane, uint64_t rozmiar);
// Wersja z tablica wbudowana w jadro (skroty_systemowe.generated.h).
extern "C" bool tozsamosc_systemowa_zweryfikuj(const char* sciezka, const uint8_t* dane, uint64_t rozmiar);
