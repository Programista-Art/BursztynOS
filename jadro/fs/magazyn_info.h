#pragma once
// Wypelnienie odpowiedzi BWS 111 ze statystyk PSF (czysta funkcja, testowana na hoscie).
#include "jadro/bws/bws_magazyn.h"
#include "psf.h"

inline void wypelnij_informacje_magazynu(BwsMagazynV1& o, const PsfStatystyki& s, bool zainstalowany) {
    o = BwsMagazynV1{};
    o.wersja = BWS_MAGAZYN_V1;
    o.rozmiar = sizeof(o);
    o.flagi = (s.gotowy ? BWS_MAG_GOTOWY : 0u) | (s.trwaly ? BWS_MAG_TRWALY : 0u) |
              (zainstalowany ? BWS_MAG_ZAINSTALOWANY : 0u);
    o.maks_nazwa_bajty = PSF_MAX_NAZWA - 1U;
    o.pojemnosc_bajty = s.bloki_danych * uint64_t(PSF_ROZMIAR_BLOKU);
    o.wolne_bajty = s.wolne_bajty;
    o.maks_rozmiar_pliku = PSF_MAKS_ROZMIAR_PLIKU;
    o.wezly = s.wezly;
    o.wolne_wezly = s.wolne_wezly;
    o.rozmiar_bloku = PSF_ROZMIAR_BLOKU;
}
