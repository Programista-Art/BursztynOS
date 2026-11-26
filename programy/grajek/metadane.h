#pragma once
/*
 * Grajek - metadane utworu (wspolne dla WAV/RIFF INFO, ID3v1 i ID3v2).
 *
 * Wszystkie pola tekstowe sa juz poprawnym UTF-8 (po konwersji z Latin-1/UTF-16 i usunieciu znakow sterujacych)
 * i maja stale, male limity. Okladka nie jest tu przechowywana - tylko jej polozenie w pliku i rozmiar,
 * dekodowanie odbywa sie raz, przy ladowaniu utworu (okladka.h).
 */
#include <stdint.h>

namespace Grajek {

constexpr uint32_t MAKS_POLE_TEKSTU = 96;            // bajty UTF-8 lacznie z NUL
constexpr uint32_t MAKS_OKLADKA_BAJTY = 2U * 1024U * 1024U;

enum class TypOkladki : uint8_t { Brak = 0, JPEG = 1, PNG = 2 };

struct Metadane {
    char tytul[MAKS_POLE_TEKSTU]{};
    char wykonawca[MAKS_POLE_TEKSTU]{};
    char album[MAKS_POLE_TEKSTU]{};
    char rok[8]{};
    char numer[8]{};
    // Okladka (ID3 APIC/PIC): przesuniecie danych obrazu w pliku i rozmiar. niesynchronizowana = dane wymagaja
    // usuniecia "unsynchronisation" (ID3) - wtedy okladka.h wczytuje cala ramke i ja odtwarza.
    TypOkladki okladka = TypOkladki::Brak;
    bool okladka_niesynchronizowana = false;
    uint8_t typ_obrazu_apic = 0xFF;                   // 3 = przednia okladka (preferowana)
    uint64_t okladka_offset = 0;
    uint32_t okladka_rozmiar = 0;
    // dla okladki niesynchronizowanej: cala ramka ID3 (naglowek danych zaczyna sie od okladka_offset_ramki)
    uint64_t okladka_offset_ramki = 0;
    uint32_t okladka_rozmiar_ramki = 0;
};

} // namespace Grajek
