/*
 * BRB - Faza R2: wymagania i wybor fizycznego umieszczenia obrazu jadra (model ciagly C + E).
 *
 * Wejscie: plan Fazy 3 (zlinkowane VA; p_paddr == p_vaddr wymuszone przez POLITYKA_JADRA_BURSZTYN) i zwalidowane
 * metadane natywnego wejscia. Wyjscie: rozmiar ciaglego podkladu, wymagane wyrownanie, liczba stron do przydzialu
 * (z dopelnieniem wyrownania) oraz baza wybrana wewnatrz FAKTYCZNIE przydzielonego zakresu. Czysta logika, bez
 * firmware; kazde dzialanie arytmetyczne sprawdzane. Oryginalne p_paddr nie sa adresami docelowymi.
 */
#ifndef BRB_UMIEJSCOWIENIE_H
#define BRB_UMIEJSCOWIENIE_H

#include <stdint.h>

#include "elf64.h"
#include "metadane_jadra.h"

namespace brb {

constexpr uint64_t UMIESZCZENIE_LIMIT_TYMCZASOWY = 0x100000000ull;   // caly obraz < 4 GiB (R1A/R1B)
constexpr uint64_t UMIESZCZENIE_MAKS_WYROWNANIE = 0x40000000ull;     // 1 GiB - gorny bezpiecznik

enum class BladUmiejscowienia : uint8_t {
    Brak = 0,
    BrakDanych,
    BazaNiezgodna,         // najnizsza strona VA planu != baza_wirtualna metadanych
    PlanNieciagly,         // segment poza zakresem planu, nieposortowany albo nakladajacy sie
    Wyrownanie,            // wyrownanie nie jest potega dwojki w [4 KiB, 1 GiB] albo p_align niezgodne
    Przepelnienie,
    PrzydzialNiewyrownany, // firmware zwrocilo adres niewyrownany do 4 KiB albo zero stron
    ZaMalyPrzydzial,       // obraz z wyrownaniem nie miesci sie w przydziale
    PozaLimitem,           // koniec obrazu powyzej limitu tymczasowego (4 GiB)
    PozaSzerokoscia,       // koniec obrazu poza szerokoscia fizyczna CPU (MAXPHYADDR)
};

struct WymaganiaObrazu {
    uint64_t baza_wirtualna;     // najnizsza strona VA (= metadane.baza_wirtualna)
    uint64_t rozmiar;            // bajty ciaglego podkladu (wielokrotnosc 4 KiB, z lukami i BSS)
    uint64_t strony;             // rozmiar / 4 KiB
    uint64_t wyrownanie;         // wymagane wyrownanie bazy fizycznej (potega dwojki >= 4 KiB)
    uint64_t strony_przydzialu;  // strony + (wyrownanie / 4 KiB - 1) - dopelnienie na wyrownanie
};

struct WybraneUmiejscowienie {
    uint64_t przydzial;          // adres zwrocony przez firmware
    uint64_t strony_przydzialu;
    uint64_t baza;               // wyrownana baza obrazu w przydziale
    uint64_t dopelnienie_przed;  // bajty [przydzial, baza) - posiadane, nieuzywane
    uint64_t dopelnienie_po;     // bajty [baza + rozmiar, koniec przydzialu) - posiadane, nieuzywane
};

BladUmiejscowienia wymagania_obrazu(const PlanLadowania& plan, const MetadaneWejscia& meta, WymaganiaObrazu* wynik);

/*
 * Wybor bazy w przydziale [przydzial, + strony * 4 KiB): najnizszy adres wyrownany do wym.wyrownanie, taki ze caly
 * obraz miesci sie w przydziale, ponizej limitu (wylacznie) i ponizej 2^bity_fizyczne.
 */
BladUmiejscowienia wybierz_baze(const WymaganiaObrazu& wym, uint64_t przydzial, uint64_t strony_przydzialu, uint64_t limit,
                                unsigned bity_fizyczne, WybraneUmiejscowienie* wynik);

const char* opis_bledu_umiejscowienia(BladUmiejscowienia b);

}  // namespace brb

#endif
