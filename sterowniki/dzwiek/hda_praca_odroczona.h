#pragma once
#include "hda.h"

inline bool hda_praca_zaplanowana = false;

// Timer IRQ only coalesces a request. No HDA registers/clock/locks in IRQ.
inline void hda_zaplanuj_prace_odroczona() {
    __atomic_store_n(&hda_praca_zaplanowana, true, __ATOMIC_RELEASE);
}

// Safe C++ syscall boundaries consume work after returning from IRQ and after
// the syscall implementation releases its locks. This avoids dependence on
// PID0 getting a turn while user GUI programs issue many syscalls.
inline void hda_wykonaj_zaplanowana_prace_odroczona() {
    if (__atomic_exchange_n(&hda_praca_zaplanowana, false, __ATOMIC_ACQ_REL))
        hda_obsluz_okresowo();
}

// PID0 is part of the normal round-robin schedule, even with runnable users.
// Its idle loop resumes after timer IRQ/iretq and services hardware outside IRQ.
// Keep this entry shared with the regression harness; no diagnostic test tick
// or user polling is needed for a timed tone to stop.
inline void hda_wykonaj_prace_odroczona() {
    __atomic_store_n(&hda_praca_zaplanowana, false, __ATOMIC_RELEASE);
    hda_obsluz_okresowo();
}
