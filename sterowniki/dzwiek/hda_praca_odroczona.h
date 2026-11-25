#pragma once
#include "hda.h"
#include "ac97.h"

// Wspolne haki pracy odroczonej wyjscia audio (nazwy historyczne: HDA). Obsluguja HDA i AC97; sterownik,
// ktory nie jest aktywny, wychodzi po jednym odczycie atomowym. AC97 jest aktywne tylko, gdy HDA nie dziala.

// Updates the timestamp of the first outstanding request, plus the DMA
// watchdog. The timer itself never mixes or touches sample pages.
inline void hda_zaplanuj_prace_odroczona(unsigned pid = 0, bool ring0 = true) {
    hda_pcm_watchdog_irq(pid, ring0);
    ac97_zaplanuj_prace();
}

inline void hda_wykonaj_zaplanowana_prace_odroczona() {
    if (hda_pcm_praca_oczekuje()) hda_obsluz_okresowo();
    if (ac97_praca_oczekuje()) ac97_obsluz_okresowo();
}

// PID0 also handles cold start and resume through the same worker.
inline void hda_wykonaj_prace_odroczona() {
    hda_obsluz_okresowo();
    ac97_obsluz_okresowo();
}
