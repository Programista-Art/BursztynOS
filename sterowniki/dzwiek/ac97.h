#pragma once
/*
 * Bursztyn OS - sterownik Intel ICH AC97 (jadro). Logika: ac97_rdzen.h + ac97_pcm.h; tu tylko interfejs jadra.
 *
 * AC97 jest inicjalizowane wylacznie, gdy HDA nie dziala (jadro/dzwiek/wyjscie_audio_polityka.h).
 * Model wykonania jak HDA: bez IRQ urzadzenia, praca odroczona z timera i planisty (hda_praca_odroczona.h).
 */
#include <stdint.h>
#include "ac97_rdzen.h"
#include "ac97_pcm.h"
#include "hda.h"

// Skan PCI bez zapisow do urzadzenia: true, gdy istnieje kontroler 04:01 z obslugiwanym ID Intel.
bool ac97_wykryj();
// Pelna inicjalizacja pierwszego poprawnego kontrolera. true = gotowy do odtwarzania.
bool inicjalizuj_ac97();
// Zatrzymanie DMA i zwolnienie zasobow (false = DMA nie zatrzymalo sie, bufory zachowane).
bool ac97_wylacz();

// Wywolywane z przerwania timera: tylko znacznik czasu, bez dostepu do sprzetu.
void ac97_zaplanuj_prace();
bool ac97_praca_oczekuje();
// Praca odroczona (IF=1, kontekst planowalny): statusy RW1C, uzupelnianie BDL, start/stop.
void ac97_obsluz_okresowo();

struct Ac97Migawka {
    bool wykryty, aktywny;
    Ac97::Diagnostyka kontroler;
    Ac97::StatystykiPcm pcm;
    uint8_t bus, slot, func;
};
bool ac97_migawka(Ac97Migawka* out);

// Glosnosc systemowa w tym samym formacie co HDA (Bursztynowy Zasobnik, BWS 107/108).
void ac97_glosnosc_info(HdaGlosnoscInfo* out);
uint32_t ac97_glosnosc_ustaw(uint32_t procent);
uint32_t ac97_wyciszenie_ustaw(bool wyciszony);

namespace Ac97Pcm {
int64_t open(Pcm::Owner owner, const BwsPcmFormat& format);
int64_t write(Pcm::Owner owner, uint64_t handle, const int16_t* samples, uint32_t frames);
int64_t query(Pcm::Owner owner, uint64_t handle, BwsPcmState& state);
int64_t statistics(Pcm::Owner owner, uint64_t handle, BwsPcmStats& out);
int64_t gain(Pcm::Owner owner, uint64_t handle, uint64_t volume, uint64_t mute);
int64_t close(Pcm::Owner owner, uint64_t handle);
int64_t finish(Pcm::Owner owner, uint64_t handle);
bool posprzataj(uint32_t pid, uint64_t generation);
bool diag_stream(uint32_t pid, uint64_t generation, BwsPcmStats& out);
bool diag_starvation(uint32_t index, Pcm::Starvation& out);
}
