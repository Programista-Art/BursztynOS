/* Host unit smoke for motyw subsystem (ABI + fallback palette). */
#define BURSZTYN_HOST_TEST 1
#include "motyw.h"
#include "../jadro/gui/motyw.cpp"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    static_assert(sizeof(BursztynMotyw) == 172, "ABI v2 size");
    static_assert(sizeof(BursztynMotywV1) == 144, "ABI v1 size");
    static_assert(MOTYW_ABI_WERSJA == 2, "ABI version");

    motyw_inicjalizuj();
    const BursztynMotyw* aktywny = motyw_pobierz_aktywny();
    assert(aktywny != nullptr);
    assert(aktywny->wersja == MOTYW_ABI_WERSJA);
    assert(aktywny->rozmiar_struktury == sizeof(BursztynMotyw));
    assert(aktywny->nazwa[0] != '\0');
    assert(aktywny->id[0] != '\0');
    assert(motyw_waliduj(aktywny));

    std::printf("[MOTYW-HOST] PASS\n");
    return 0;
}
