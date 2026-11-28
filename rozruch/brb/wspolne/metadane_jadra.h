/*
 * BRB - wykrywanie metadanych natywnego wejscia 64-bit w pliku jadra (Faza R1B; uzycie produkcyjne: R2).
 *
 * Uklad struktury: jadro/arch/metadane_jadra.h (jedno zrodlo prawdy, wspolne z jadrem). Wejscie: niezaufany bufor
 * pliku i ZWALIDOWANY plan z zbuduj_plan_ladowania() - nie ma drugiego parsera ELF; nazwy sekcji i symbole nie sa
 * uzywane (dziala na okrojonym pliku). W R1B modul NIE jest linkowany do brb-x64.efi (sciezka ladowania bez zmian).
 */
#ifndef BRB_METADANE_JADRA_H
#define BRB_METADANE_JADRA_H

#include <stdint.h>

#include "elf64.h"

namespace brb {

enum class BladMetadanych : uint8_t {
    Brak = 0,
    BrakDanych,               // brak bufora albo planu
    NieZnaleziono,            // brak sygnatury w oknie 32 KiB
    Niejednoznaczne,          // wiecej niz jedna sygnatura
    PozaSegmentem,            // struktura nie lezy w calosci w danych z pliku segmentu R bez W/X
    Obciete,                  // struktura wychodzi poza plik albo okno
    ZlaSygnaturaKoncowa,
    NieobslugiwanaWersja,
    ZlyRozmiar,
    NieobslugiwanyKontrakt,
    PolaZarezerwowane,
    NieznaneCechy,
    ZleWyrownanie,            // wyrownanie podkladu nie jest potega dwojki >= 4096
    BazaNiezgodna,            // baza_wirtualna != najnizszy p_vaddr planu
    WejscieNiekanoniczne,
    WejsciePozaSegmentem,     // VA wejscia poza kazdym PT_LOAD
    WejscieNiewykonywalne,    // segment bez PF_X
    WejsciePozaPlikiem,       // VA w czesci BSS (poza p_filesz)
    WejscieRowneElf,          // wejscie natywne == e_entry (wejscia musza byc rozdzielone)
};

struct MetadaneWejscia {
    uint64_t przesuniecie_pliku;
    uint32_t segment;            // indeks segmentu planu z metadanymi
    uint32_t kontrakt_wejscia;
    uint64_t baza_wirtualna;
    uint64_t wejscie64;          // VA
    uint64_t wejscie64_offset;   // offset od bazy wirtualnej (= offset od bazy fizycznej podkladu)
    uint32_t segment_wejscia;
    uint32_t wymagane_cechy;
    uint64_t wyrownanie_podkladu;
};

BladMetadanych znajdz_metadane_wejscia(const uint8_t* dane, uint64_t rozmiar, const PlanLadowania& plan,
                                       MetadaneWejscia* wynik);
const char* opis_bledu_metadanych(BladMetadanych blad);

}  // namespace brb

#endif
