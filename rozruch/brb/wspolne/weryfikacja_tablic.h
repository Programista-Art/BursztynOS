/*
 * BRB - Faza R2: NIEZALEZNA weryfikacja przygotowanych (nieaktywnych) tablic stron przejscia.
 *
 * Weryfikator nie korzysta z kodu budowniczego (jadro/mm/plan_przejscia.h) ani z ObrazJadra: oczekiwane mapowania
 * i uprawnienia wylicza z PRZESUNIETEGO planu ELF (prawa p_flags segmentow, p_vaddr, adresy docelowe) oraz z listy
 * zasobow tozsamosciowych z rejestru BRB. Przechodzi po tablicach od CR3 (Intel SDM t. 3A 4.5, tabele 4-14..4-19):
 *  - kazda tablica posrednia: ramka wyrownana, w puli posiadanych ramek, ponizej 2^MAXPHYADDR,
 *  - bity zarezerwowane: adres powyzej MAXPHYADDR, PS w PML4E, PAT/PS w lisciach 4 KiB nie sa uzywane przez BRB,
 *    strony 1 GiB / 2 MiB nie sa oczekiwane (budowniczy uzywa wylacznie 4 KiB),
 *  - zaden poziom nie ma bitu U (supervisor-only), uprawnienia efektywne: W i U = iloczyn, NX = suma,
 *  - kazdy lisc musi byc: strona okna obrazu (PA = baza + (VA - baza_wirtualna), W/X wg segmentu), strona zasobu
 *    tozsamosciowego (PA == VA, uprawnienia klasy) albo strona puli (PA == VA, R+NX); inaczej = mapowanie
 *    nieoczekiwane,
 *  - kompletnosc: kazda strona okna, kazdego zasobu i puli obecna dokladnie raz; wejscie natywne tlumaczy sie na
 *    wlasciwy bajt i jest wykonywalne; strona 0 i strony tuz przed/za oknem niezmapowane (straznik),
 *  - W^X i aliasy: zaden lisc W+X; zasoby i pula rozlaczne z podkladem obrazu (alias niemozliwy), sprawdzane jawnie.
 * Wynik dotyczy tablic NIEAKTYWNYCH: BRB nie laduje CR3 w R2, wiec zadna z tych ochron nie jest egzekwowana sprzetowo.
 */
#ifndef BRB_WERYFIKACJA_TABLIC_H
#define BRB_WERYFIKACJA_TABLIC_H

#include <stdint.h>

#include "elf64.h"

namespace brb {

enum KlasaZasobu : uint32_t {
    ZASOB_KOD_PRZEJSCIA = 1,   // R+X
    ZASOB_STOS = 2,            // R+W+NX
    ZASOB_TYLKO_ODCZYT = 3,    // R+NX
    ZASOB_DANE = 4,            // R+W+NX
};

struct ZasobTozsamosciowy {
    uint64_t pa;
    uint64_t dlugosc;
    uint32_t klasa;
};

constexpr uint32_t WERYFIKACJA_MAKS_ZASOBOW = 16;

struct OczekiwaniaTablic {
    const PlanLadowania* plan;         // plan PRZESUNIETY (adres_fizyczny = podklad)
    uint64_t baza_wirtualna;
    uint64_t baza_fizyczna;
    uint64_t rozmiar;                  // bajty okna (wielokrotnosc 4 KiB)
    uint64_t wejscie64;                // VA natywnego wejscia (z metadanych)
    const ZasobTozsamosciowy* zasoby;
    uint32_t liczba_zasobow;
    uint64_t pula_pa;                  // posiadane ramki tablic
    uint64_t pula_ramek;
    uint64_t cr3;
    uint64_t kontrakt_pa;              // zakres, ktory jadro przeczyta jako pierwszy (RSI); 0 = brak
    uint64_t kontrakt_dlugosc;
    unsigned bity_fizyczne;
    bool nx;
};

struct DostepTablic {
    const uint64_t* (*tablica)(void* kontekst, uint64_t pa);   // 512 wpisow albo nullptr
    void* kontekst;
};

enum class BladTablic : uint8_t {
    Brak = 0,
    BrakDanych,
    Cr3Niepoprawny,
    TablicaPozaPula,          // ramka tablicy posredniej poza posiadana pula albo niewyrownana
    BrakDostepuDoTablicy,
    BitZarezerwowany,         // adres > MAXPHYADDR, PS w PML4E, nieoczekiwana duza strona
    BitUzytkownika,           // U ustawiony na dowolnym poziomie
    MapowanieNieoczekiwane,   // lisc poza oknem, zasobami i pula
    ZlyAdresOkna,             // PA strony okna != baza + offset
    ZleUprawnieniaOkna,       // W/X strony okna niezgodne z segmentem ELF
    ZlaTozsamosc,             // zasob/pula: PA != VA
    ZleUprawnieniaZasobu,
    WPlusX,                   // lisc jednoczesnie zapisywalny i wykonywalny
    BrakStronyOkna,           // niekompletne okno
    BrakStronyZasobu,         // niekompletny zasob albo pula
    WejscieNiepoprawne,       // wejscie natywne nie tlumaczy sie na wlasciwy bajt albo nie jest wykonywalne
    StraznikZmapowany,        // strona 0 albo strona tuz przed/za oknem zmapowana
    AliasPodkladu,            // zasob albo pula nachodzi na podklad obrazu albo okno VA
    KontraktNiedostepny,      // zakres kontraktu nie jest tozsamosciowy R+NX w calosci
    BrakNx,                   // polityka R2 wymaga NX (W^X tablic przejscia)
};

struct RaportTablic {
    uint64_t liscie;
    uint64_t strony_okna;
    uint64_t strony_zasobow;
    uint64_t strony_puli;
    uint64_t tablice;          // odwiedzone tablice posrednie i PT
    uint64_t wejscie_pa;
    uint64_t adres_bledu;      // VA (albo PA tablicy) pierwszego bledu
};

BladTablic weryfikuj_tablice_przejscia(const OczekiwaniaTablic& o, const DostepTablic& dostep, RaportTablic* raport);
const char* opis_bledu_tablic(BladTablic b);

}  // namespace brb

#endif
