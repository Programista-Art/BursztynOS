/*
 * BRB - ladowanie zwalidowanych segmentow PT_LOAD do posiadanej pamieci fizycznej i weryfikacja obrazu
 * (Faza 4A). Modul niezalezny od firmware: o wlasnosci stron decyduje warstwa UEFI (AllocatePages), a ten modul
 * dostaje liste POSIADANYCH zakresow i tlumacza adresow (DostepFizyczny). W UEFI tlumacz jest identycznoscia
 * (pamiec identycznosciowa w czasie uslug rozruchowych), w testach hosta - symulowana pamiec fizyczna.
 *
 * Zasady:
 *  - plan pochodzi z jedynego parsera ELF (elf64.h) i jest PONOWNIE wyliczany z tych samych bajtow zrodla
 *    przed zapisem - niezgodnosc = brak zapisu,
 *  - kazdy bajt celu musi lezec w posiadanym zakresie, kazdy bajt zrodla - w zweryfikowanym buforze,
 *    a bufor zrodla nie moze nachodzic na zaden zakres celu,
 *  - cale posiadane strony sa zerowane PRZED kopiowaniem (wypelnienie stron nie ujawnia starej zawartosci,
 *    wspolne strony segmentow o tych samych prawach nie moga skasowac juz skopiowanych danych),
 *  - kopiowane jest dokladnie p_filesz bajtow; [p_filesz, p_memsz) jest zerowane jawnie,
 *  - weryfikacja jest niezalezna od kopiowania: bajt po bajcie (dane = zrodlo, BSS = 0, wypelnienie = 0).
 * Prawa ELF (R/W/X) sa tylko DEKLARACJA: Faza 4A nie zmienia tablic stron, wiec zadna ochrona nie jest
 * egzekwowana sprzetowo przez BRB.
 */
#ifndef BRB_LADOWANIE_H
#define BRB_LADOWANIE_H

#include "elf64.h"

#include <stddef.h>
#include <stdint.h>

namespace brb {

constexpr uint32_t LADOWANIE_MAKS_ZAKRESOW = ELF_MAKS_SEGMENTOW;

struct ZakresStron {
    uint64_t poczatek;   // adres fizyczny, wyrownany do 4 KiB
    uint64_t strony;
};

struct ZakresyPrzydzialu {
    uint32_t liczba;
    ZakresStron zakresy[LADOWANIE_MAKS_ZAKRESOW];
    uint64_t strony_lacznie;
};

struct DostepFizyczny {
    // Zwraca wskaznik do [adres, adres + dlugosc) albo nullptr, gdy zakres nie jest dostepny.
    uint8_t* (*wskaznik)(void* kontekst, uint64_t adres, uint64_t dlugosc);
    void* kontekst;
};

struct ZrodloJadra {
    const uint8_t* dane;        // bajty pliku, ktore przeszly walidacje Fazy 3
    uint64_t rozmiar;
    uint64_t adres_fizyczny;    // polozenie bufora (kontrola nakladania na cele)
    uint64_t rozmiar_bufora;    // rozmiar przydzialu bufora (strony * 4 KiB)
};

enum class BladLadowania : uint8_t {
    Brak = 0,
    BrakDanych,               // brak planu/zrodla/tlumacza
    PustyPlan,                // plan bez segmentow
    NiespojnyPlan,            // plan rozni sie od ponownie wyliczonego z bajtow zrodla
    ZleZakresy,               // zakres niewyrownany, zerowy albo z przepelnieniem
    ZakresyNakladajaSie,      // dwa posiadane zakresy nachodza na siebie
    ZaDuzoZakresow,           // > LADOWANIE_MAKS_ZAKRESOW
    ZrodloNiezgodne,          // rozmiar zrodla != rozmiar pliku w planie albo > bufor
    ZrodloPozaBuforem,        // dane segmentu poza buforem zrodla
    CelPozaPrzydzialem,       // bajt celu poza posiadanymi zakresami
    ZrodloNakladaSieNaCel,    // bufor zrodla nachodzi na zakres celu
    PrzepelnienieArytmetyki,
    BrakDostepuDoPamieci,     // tlumacz odmowil dostepu do posiadanego zakresu
    WeryfikacjaDanych,        // bajt danych w pamieci != bajt zrodla
    WeryfikacjaBss,           // bajt BSS != 0
    WeryfikacjaWypelnienia,   // bajt wypelnienia strony (poza segmentami) != 0
    WejscieNiewykonywalne,    // wejscie planu poza wykonywalnym segmentem
};

struct RaportSegmentu {
    uint64_t adres;
    uint64_t z_pliku;
    uint64_t w_pamieci;
    uint64_t bss;
    bool dane_ok;
    bool bss_ok;
};

struct RaportLadowania {
    uint32_t liczba;
    RaportSegmentu segmenty[ELF_MAKS_SEGMENTOW];
    uint64_t bajty_skopiowane;
    uint64_t bajty_bss;
    uint64_t bajty_wypelnienia;   // posiadane bajty poza segmentami (zerowane i sprawdzane)
    int32_t segment_bledu;        // -1 = brak
    uint64_t adres_bledu;         // adres fizyczny pierwszego niezgodnego bajtu
};

// Zakresy stron do przydzialu: po jednym na segment (posortowane wg adresu fizycznego); segmenty dzielace
// strone (dozwolone przez Faze 3 tylko przy identycznych prawach) sa scalane, aby nie przydzielac strony dwa razy.
BladLadowania zbuduj_zakresy_przydzialu(const PlanLadowania& plan, ZakresyPrzydzialu* wynik);

// Kontrola bez zapisu: plan == ponownie wyliczony plan z bajtow zrodla (ta sama polityka), zrodlo i cele.
BladLadowania sprawdz_przed_kopiowaniem(const PlanLadowania& plan, const PolitykaElf& polityka, const ZrodloJadra& zrodlo,
                                        const ZakresyPrzydzialu& posiadane, const DostepFizyczny& pamiec);

// Kontrola + zerowanie posiadanych stron + kopiowanie p_filesz + jawne zerowanie BSS.
BladLadowania zaladuj_segmenty(const PlanLadowania& plan, const PolitykaElf& polityka, const ZrodloJadra& zrodlo,
                               const ZakresyPrzydzialu& posiadane, const DostepFizyczny& pamiec, RaportLadowania* raport);

// Niezalezna weryfikacja kazdego posiadanego bajtu (dane, BSS, wypelnienie) i spojnosci planu.
BladLadowania weryfikuj_zaladowany_obraz(const PlanLadowania& plan, const ZrodloJadra& zrodlo,
                                         const ZakresyPrzydzialu& posiadane, const DostepFizyczny& pamiec,
                                         RaportLadowania* raport);

const char* opis_bledu_ladowania(BladLadowania b);

}  // namespace brb

#endif
