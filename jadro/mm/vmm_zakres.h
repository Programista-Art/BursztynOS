#pragma once
/*
 * Czyste, testowalne na hoscie reguly izolacji przestrzeni adresowej Ring 3 od mapowan jadra
 * (bez asemblera, PMM i CR3 - tablice stron sa dostarczane przez wywolujacego).
 *
 * Granica uzytkownik/jadro nie moze zalezec wylacznie od recznie utrzymywanej listy adresow
 * (zarezerwowane_va.h): dolna polowa PML4 niesie mapowania jadra klonowane do kazdego procesu
 * (identity-mapa < 4 GiB, MMIO, bufory grafiki, RAM-dysk, sterta jadra, BAR-y mapowane VA==PA, ...).
 * Dlatego:
 *   1. ASLR sprawdza FAKTYCZNY stan tabel (zakres_wolny) i losuje ponownie przy jakimkolwiek obecnym wpisie;
 *   2. mapper strony uzytkownika stosuje wolno_mapowac_strone_uzytkownika (nigdy nie zastepuje strony jadra
 *      powyzej identity-mapy, a strony uzytkownika nie nadpisuje nigdzie).
 */
#include <stdint.h>

namespace VmmZakres {

inline constexpr uint64_t PTE_OBECNY = 1ULL << 0;
inline constexpr uint64_t PTE_DUZY   = 1ULL << 7;                       // PS na poziomie PD/PDP
inline constexpr uint64_t LIMIT_IDENTITY_MAPY = 4ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t KONIEC_DOLNEJ_POLOWY = 0x0000800000000000ULL;  // pierwszy niekanoniczny adres

/*
 * Czy mapper strony uzytkownika moze zapisac nowa strone pod `adres`, gdy walker widzi wpis opisany
 * parametrami? Obecna strona uzytkownika: nigdy (brak podwojnego mapowania). Obecna strona jadra:
 * tylko w identity-mapie < 4 GiB (wspolna, huge-page; w prywatnej kopii procesu jest rozbijana) - powyzej
 * kazde obecne mapowanie jest mapowaniem jadra (okna VA, MMIO, bufory) i NIE moze zostac zastapione.
 */
inline constexpr bool wolno_mapowac_strone_uzytkownika(bool obecna, bool user, uint64_t adres) {
    return !obecna || (!user && adres < LIMIT_IDENTITY_MAPY);
}

/*
 * Czy polotwarty przedzial [start, koniec) dolnej polowy przestrzeni adresowej NIE zawiera zadnego
 * obecnego wpisu (lisc 4 KiB, 2 MiB, 1 GiB)? `dziecko(wpis)` zwraca tablice nizszego poziomu albo nullptr
 * (fail-closed: nieczytelna tablica = zakres zajety). Pusty/odwrocony przedzial lub wyjscie poza dolna polowe
 * = false. Pomija nieobecne poddrzewa w calosci, wiec koszt zalezy od liczby obecnych tablic, nie od rozmiaru zakresu.
 */
template <class Dziecko>
inline bool zakres_wolny_poziom(const uint64_t* t, int poziom, uint64_t baza, uint64_t start,
                                uint64_t koniec, Dziecko& dziecko) {
    const uint64_t span = 1ULL << (12 + 9 * (poziom - 1));
    for (unsigned i = 0; i < 512; ++i) {
        const uint64_t b = baza + static_cast<uint64_t>(i) * span;
        const uint64_t e = b + span;
        if (e <= start) continue;
        if (b >= koniec) break;
        const uint64_t w = t[i];
        if (!(w & PTE_OBECNY)) continue;
        if (poziom == 1 || (poziom <= 3 && (w & PTE_DUZY))) return false;
        const uint64_t* c = dziecko(w);
        if (!c) return false;
        if (!zakres_wolny_poziom(c, poziom - 1, b, start, koniec, dziecko)) return false;
    }
    return true;
}

template <class Dziecko>
inline bool zakres_wolny(const uint64_t* pml4, uint64_t start, uint64_t koniec, Dziecko dziecko) {
    if (!pml4 || koniec <= start || koniec > KONIEC_DOLNEJ_POLOWY) return false;
    return zakres_wolny_poziom(pml4, 4, 0, start, koniec, dziecko);
}

} // namespace VmmZakres
