#pragma once
#include <stdint.h>
// Wpis wbudowanej tozsamosci aplikacji systemowej: SHA-256 pliku .bur zbudowanego
// razem z jadrem (generowany przez narzedzia/generuj_skroty_systemowe.py).
namespace TozsamoscSystemowa {
struct Wpis {
    const char* sciezka;
    uint8_t skrot[32];
    uint32_t rozmiar;
};
}
