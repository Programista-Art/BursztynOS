#pragma once
// Ograniczony, wznawialny dekoder PODGLADU JPEG (Ring 3, bez libc).
//
// Cel: obrazy, ktorych pelna powierzchnia BGRA nie miesci sie w budzecie pamieci aplikacji
// (np. 8000x8000 = 244 MiB). Dekoder NIE tworzy pelnego obrazu: zapisuje tylko zredukowane
// plaszczyzny skladowych (1/2, 1/4 albo 1/8 rozdzielczosci) i z nich buduje powierzchnie podgladu.
//   * baseline/extended (SOF0/SOF1, 8 bit, Huffman): skala 1/2, 1/4 (zredukowane IDCT 2x2/4x4)
//     albo 1/8 (tylko DC) — dobierana tak, by podglad miescil sie w budzecie;
//   * progressive (SOF2): tylko skany DC (pierwszy przebieg) => zawsze skala 1/8;
//   * plik jest czytany sekwencyjnie przez callback (bufor 32 KiB), bez wczytywania calosci;
//   * praca jest wznawialna (`krok`), wiec aplikacja moze obslugiwac zdarzenia miedzy porcjami.
// Nie jest to widok 100%: wynik zawsze niesie wspolczynnik skali zrodla (`skala`).
#include "dekoder_obrazu.h"

enum class PodgladStan : uint8_t { Gotowy, Dalej, Blad };

struct PodgladZrodlo {
    // Odczyt `n` bajtow od `przesuniecie`; zwraca liczbe przeczytanych bajtow (0 = blad/EOF).
    uint32_t (*czytaj)(void* ctx, uint64_t przesuniecie, uint8_t* cel, uint32_t n);
    void* ctx;
    uint64_t rozmiar;
};

struct PodgladOpcje {
    uint64_t budzet_bajtow;     // maks. suma plaszczyzn + powierzchni podgladu
    uint32_t maks_pikseli_podgladu;
    uint32_t maks_bok_zrodla;   // limit wymiarow zrodla
    uint64_t maks_pikseli_zrodla;
};

struct PodgladWynik {
    uint32_t szer_zrodla{0}, wys_zrodla{0};
    uint32_t szer{0}, wys{0};
    uint32_t skala{0};          // 2, 4 albo 8 (zrodlo / podglad)
    bool progresywny{false};
    uint32_t* bgra{nullptr};
    uint64_t szczyt_bajtow{0};  // najwiekszy jednoczesny przydzial dekodera
};

struct PodgladJpeg;
// Tworzy dekoder; zwraca nullptr i ustawia *blad, gdy plik jest niepoprawny/zbyt duzy/brak pamieci.
PodgladJpeg* podglad_jpeg_otworz(const PodgladZrodlo& zrodlo, const PodgladOpcje& opcje,
                                 AlokujObraz alokuj, ZwolnijObraz zwolnij, BladObrazu* blad);
// Wykonuje do `maks_jednostek` jednostek pracy (wiersz MCU / wiersz wyjsciowy).
PodgladStan podglad_jpeg_krok(PodgladJpeg* d, uint32_t maks_jednostek);
uint32_t podglad_jpeg_postep_promile(const PodgladJpeg* d);
const PodgladWynik& podglad_jpeg_wynik(const PodgladJpeg* d);
BladObrazu podglad_jpeg_blad(const PodgladJpeg* d);
// Przekazuje wlasnosc powierzchni (wynik.bgra) wywolujacemu i zwalnia reszte.
void podglad_jpeg_zamknij(PodgladJpeg* d, bool zachowaj_powierzchnie);
// Tylko naglowek: wymiary i rodzaj (bez alokacji poza buforem odczytu).
BladObrazu podglad_jpeg_naglowek(const PodgladZrodlo& zrodlo, uint32_t* w, uint32_t* h, bool* progresywny,
                                 AlokujObraz alokuj, ZwolnijObraz zwolnij);
