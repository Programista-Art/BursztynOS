#pragma once
#include <stdint.h>

// Trusted kernel callers only, in schedulable context. One selected output.
// Stereo interleaved PCM16 at 48000 Hz. No caller pointer is retained.
namespace PcmJadro {
using Uchwyt = uint64_t;

enum class Wynik { Ok, Niedostepne, Zajete, Limit, ZlyUchwyt, ZleDane, Pelny };

Wynik otworz(Uchwyt& out);
Wynik zapisz(Uchwyt uchwyt, const int16_t* pcm, uint32_t ramki, uint32_t& przyjete);
Wynik glosnosc(Uchwyt uchwyt, uint32_t procent, bool wycisz);
Wynik zamknij(Uchwyt uchwyt);

// Terminal shutdown of the selected engine. DMA ownership is retained if
// stopping fails. No more streams can be opened after successful shutdown.
bool wylacz();

struct Statystyki {
    uint64_t starty, okresy, braki_ramek, opoznienia, bledy_dma, odzyskania;
    uint64_t maks_przerwa_ns, czas_miksowania_ns, maks_miks_ns;
    uint64_t przerwania, maks_irq_ns, paczki_pracy, suma_czekania_ns, maks_czekanie_ns;
    uint64_t blokady, blokady_pracy, maks_blokada_ns, maks_odzyskanie_ns;
    uint64_t maks_skok_lpib, maks_spoznienie_ns, utracone_okresy, maks_wyprzedzenie_ramek;
    uint64_t okresy_miksowania, irq_w_miksie, pilne_powroty, przedluzenia_watchdoga;
    uint64_t maks_irq_pid, maks_irq_ring0;
    uint64_t bezpieczna_cisza, cisza_miksera, okresy_master_mute;
    uint64_t proby_watchdoga, fifo_error, descriptor_error;
    uint64_t otwarcia_aplikacji, usuniete_przy_zakonczeniu;
    uint64_t przyczyny[9];
    uint32_t zapisane_zdarzenia;
};
bool statystyki(Statystyki& out);

struct Zdarzenie {
    uint64_t ns, termin_ns, irq_ns, oczekuje_od_ns, ostatni_zaufany_ns;
    uint32_t lpib, przyczyna, lpib_watchdoga;
};
bool zdarzenie(uint32_t indeks, Zdarzenie& out);

enum class BrakPcm : uint8_t { Kolejka, WlasnoscDma };
struct SladPcm {
    uint64_t ns, poprzedni_odczyt_ns, oczekuje_od_ns, przerwa_irq_ns;
    uint64_t okres, bajty_dma, poprzednie_bajty_dma;
    uint32_t lpib, minimum_kolejki, maksimum_kolejki, oczekujace_zrodla, brakujace_ramki;
    BrakPcm rodzaj;
    bool start, master_mute, przygotowano, pcm_dostepne;
};
bool slad_pcm(uint32_t indeks, SladPcm& out);
}
