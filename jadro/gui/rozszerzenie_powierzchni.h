#pragma once
/*
 * Zmiana rozmiaru zywej warstwy okna: nowy bufor zawiera ostatnio opublikowana tresc (lewy gorny rog), a
 * POWIEKSZONY obszar jest wypelniany KRYJACO jednolitym kolorem tla okna z aktywnego motywu (`oknotlo`), a
 * nie zerami. Kompozytor rysuje belke/ramke od razu wg nowej geometrii, a aplikacja (Ring 3) publikuje nowa
 * klatke dopiero pozniej - przezroczyste zera odslanialy w tym czasie tapete/okna pod spodem (belka szersza
 * niz klient). (Replikacja krawedzi starej tresci odrzucona: rozciagala suwaki/paski stanu w pasy.)
 *
 * Semantyka pikseli powierzchni aplikacji (zob. skladacz_obrazu.cpp): 0x00000000 = przezroczysty, kazda inna
 * wartosc = kryjaca (gorny bajt to znacznik, np. 0x01000000 = czern); to NIE jest kanal alfa 0xFF.
 *
 * Czysta funkcja bez zaleznosci od jadra: testy hosta (testy/test_rozszerzenie_powierzchni_host.cpp).
 * Bez alokacji; indeksy w size_t.
 */
#include <stddef.h>
#include <stdint.h>

namespace RozszerzeniePowierzchni {

inline bool kryjacy(uint32_t p) { return p != 0U; }

// Kolor motywu -> piksel kryjacy (czarny 0x000000 musi miec znacznik 0x01000000, bo 0 = przezroczysty).
inline uint32_t piksel_tla(uint32_t kolor_motywu) {
    const uint32_t rgb = kolor_motywu & 0x00FFFFFFU;
    return rgb ? rgb : 0x01000000U;
}

/*
 * stary: sw x sh (stride sw), nowy: w x h (stride w), wszystkie wymiary > 0, bufory o tej pojemnosci.
 * Zwraca false (nic nie zapisuje), gdy argumenty sa niepoprawne.
 */
inline bool kopiuj_i_rozszerz(const uint32_t* stary, int sw, int sh, uint32_t* nowy, int w, int h,
                              uint32_t kolor_tla) {
    if (!nowy || w <= 0 || h <= 0) return false;
    if (!stary || sw <= 0 || sh <= 0) return false;
    const uint32_t tlo = piksel_tla(kolor_tla);
    const int kw = sw < w ? sw : w;
    const int kh = sh < h ? sh : h;
    for (int r = 0; r < kh; ++r) {
        const uint32_t* from = stary + static_cast<size_t>(r) * static_cast<size_t>(sw);
        uint32_t* to = nowy + static_cast<size_t>(r) * static_cast<size_t>(w);
        for (int c = 0; c < kw; ++c) to[c] = from[c];
        for (int c = kw; c < w; ++c) to[c] = tlo;                 // powiekszenie w poziomie
    }
    for (int r = kh; r < h; ++r) {                                // powiekszenie w pionie
        uint32_t* to = nowy + static_cast<size_t>(r) * static_cast<size_t>(w);
        for (int c = 0; c < w; ++c) to[c] = tlo;
    }
    return true;
}

} // namespace RozszerzeniePowierzchni
