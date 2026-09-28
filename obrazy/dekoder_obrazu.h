#pragma once
#include <stddef.h>
#include <stdint.h>

// Shared Ring 3 image boundary. The caller owns the returned BGRA8888 surface.
// A new format can be added without changing consumers of dekoduj_obraz().
enum class FormatObrazu : uint8_t { Nieznany, JPEG, BTP, BMP, PNG };
enum class BladObrazu : uint8_t { OK, Argument, Format, Nieobslugiwany, Uszkodzony, ZaDuzy, Pamiec };
struct DekodowanyObraz {
    uint32_t szerokosc, wysokosc, stride;
    uint32_t format_pikseli; // 1 = BGRA8888 / packed 0xAARRGGBB
    uint8_t* piksele;
    size_t bajty;
};
using AlokujObraz = void* (*)(size_t);
using ZwolnijObraz = void (*)(void*);
constexpr uint32_t OBRAZ_BGRA8888 = 1;
constexpr uint32_t OBRAZ_MAX_BOK = 8192;
constexpr uint32_t OBRAZ_MAX_PIKSELE = 8U * 1024U * 1024U;
constexpr size_t OBRAZ_MAX_PLIK = 16U * 1024U * 1024U;

using Obraz = DekodowanyObraz;
using FormatPiksela = uint32_t;

FormatObrazu rozpoznaj_obraz(const uint8_t* dane, size_t n);
BladObrazu dekoduj_obraz(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                         AlokujObraz alokuj, ZwolnijObraz zwolnij);
BladObrazu dekoduj_jpeg(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                        AlokujObraz alokuj, ZwolnijObraz zwolnij);
BladObrazu sprawdz_jpeg(const uint8_t* dane, size_t n,
                        uint32_t* szerokosc, uint32_t* wysokosc);
BladObrazu dekoduj_png(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                       AlokujObraz alokuj, ZwolnijObraz zwolnij);
BladObrazu sprawdz_png(const uint8_t* dane, size_t n,
                       uint32_t* szerokosc, uint32_t* wysokosc);
const char* blad_obrazu_komunikat(BladObrazu b);

inline bool dekoduj_obraz(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                          AlokujObraz alokuj, ZwolnijObraz zwolnij, BladObrazu* blad) {
    BladObrazu err = dekoduj_obraz(dane, n, out, alokuj, zwolnij);
    if (blad) *blad = err;
    return err == BladObrazu::OK;
}

inline bool dekoduj_jpeg(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                         AlokujObraz alokuj, ZwolnijObraz zwolnij, BladObrazu* blad) {
    BladObrazu err = dekoduj_jpeg(dane, n, out, alokuj, zwolnij);
    if (blad) *blad = err;
    return err == BladObrazu::OK;
}

inline bool dekoduj_png(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                        AlokujObraz alokuj, ZwolnijObraz zwolnij, BladObrazu* blad) {
    BladObrazu err = dekoduj_png(dane, n, out, alokuj, zwolnij);
    if (blad) *blad = err;
    return err == BladObrazu::OK;
}

