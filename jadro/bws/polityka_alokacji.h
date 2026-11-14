#pragma once
/*
 * Polityka rozmiaru POJEDYNCZEJ alokacji sterty (BWS 35). Czysta funkcja, testowana na hoscie.
 *
 *   zwykly proces         : 1 .. 16 MiB
 *   program Tapety        : do 40 MiB (sciezka i PRAWO_GUI|PRAWO_PLIKI_CZYTAJ)
 *   Przegladarka Obrazow  : do 72 MiB, WYLACZNIE gdy loader zweryfikowal bajty (SHA-256 z tablicy w jadrze albo
 *                           podpis release), sciezka sie zgadza i proces ma PRAWO_GUI|PRAWO_PLIKI_CZYTAJ
 * Lacznie obowiazuje limit sterty procesu (256 MiB) w bws_alokuj_sterte. SHA-256 z tablicy w jadrze to lista
 * dopuszczonych binarek zakotwiczona w obrazie jadra (tozsamosc), a nie podpis cyfrowy.
 */
#include <stdint.h>

namespace PolitykaAlokacji {
constexpr uint64_t MIB = 1024ULL * 1024ULL;
constexpr uint64_t LIMIT_ZWYKLY = 16 * MIB;
constexpr uint64_t LIMIT_TAPETY = 40 * MIB;
constexpr uint64_t LIMIT_PRZEGLADARKA = 72 * MIB;

constexpr uint64_t maks_rozmiar(bool tapety_ok, bool przegladarka_zaufana) {
    return przegladarka_zaufana ? LIMIT_PRZEGLADARKA : (tapety_ok ? LIMIT_TAPETY : LIMIT_ZWYKLY);
}
constexpr bool dozwolona(uint64_t rozmiar, bool tapety_ok, bool przegladarka_zaufana) {
    return rozmiar != 0 && rozmiar <= maks_rozmiar(tapety_ok, przegladarka_zaufana);
}
}
