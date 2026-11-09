#pragma once
#include <stdint.h>

// A hardware exception originating in an active Ring 3 process can be isolated
// using the normal process-exit path. System/machine exceptions remain fatal.
inline constexpr bool wyjatek_izoluj_proces(uint64_t vector, uint64_t cs, bool active_user) {
    return active_user && (cs & 3U) == 3U && vector < 32 && vector != 2 && vector != 8 && vector != 18;
}

// Kernel-only normal-exit adapter; never registered as a BWS call.
[[noreturn]] void syscalls_zakoncz_proces_po_wyjatku();
