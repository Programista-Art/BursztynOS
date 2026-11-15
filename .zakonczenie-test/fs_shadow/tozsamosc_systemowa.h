#pragma once
/*
 * Tozsamosc aplikacji systemowych o podwyzszonym zaufaniu (PZB < 4 albo
 * `system_config`).
 *
 * Zrodlo prawdy: skrot SHA-256 pliku .bur zapisany w obrazie jadra w czasie
 * budowania (tablica generowana z tych samych plikow .bin, ktore trafiaja do
 * obrazu), albo - dla niezaleznej aktualizacji aplikacji - poprawny podpis
 * RSA-2048/SHA-256 kluczem release (`<program>.podpis`, podpis_paczki.h).
 * Obie sciezki korzystaja z JUZ ISTNIEJACEGO lancucha zaufania (jadro jest
 * uwierzytelniane przez rozruch i aktualizacje A/B, podpis przez klucz release).
 *
 * Nie zalezy od opcjonalnego modulu Multiboot, od manifestu na dysku ani od
 * CRC/Adler. Weryfikowany jest ten sam bufor, z ktorego loader mapuje obraz.
 */
#include <stddef.h>
#include <stdint.h>
#include "tozsamosc_typy.h"

namespace TozsamoscSystemowa {

#if defined(__has_include)
#if __has_include("skroty_systemowe.generated.h")
#include "skroty_systemowe.generated.h"
#else
inline constexpr Wpis kWbudowaneSkroty[] = {{nullptr, {}, 0}};
#endif
#else
inline constexpr Wpis kWbudowaneSkroty[] = {{nullptr, {}, 0}};
#endif

constexpr bool sciezki_rowne(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == '\0' && b[i] == '\0';
}

template <size_t N>
constexpr const Wpis* znajdz(const Wpis (&tablica)[N], const char* sciezka) {
    for (const auto& w : tablica)
        if (w.sciezka && sciezki_rowne(w.sciezka, sciezka)) return &w;
    return nullptr;
}

// Porownanie skrotu: staly czas (bez wczesnego wyjscia po pierwszej roznicy).
constexpr bool skroty_rowne(const uint8_t* a, const uint8_t* b) {
    uint8_t roznica = 0;
    for (int i = 0; i < 32; ++i) roznica = static_cast<uint8_t>(roznica | (a[i] ^ b[i]));
    return roznica == 0;
}

template <size_t N>
constexpr bool skrot_wbudowany_zgodny(const Wpis (&tablica)[N], const char* sciezka,
                                      const uint8_t skrot[32], uint64_t rozmiar) {
    const Wpis* w = znajdz(tablica, sciezka);
    return w && w->rozmiar == rozmiar && skroty_rowne(w->skrot, skrot);
}

}  // namespace TozsamoscSystemowa
