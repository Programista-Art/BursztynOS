#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"

namespace BursztynUI {

enum class TypZdarzenia {
    WcisniecieMyszy,
    ZwolnienieMyszy,
    RuchMyszy,
    WejscieMyszy,
    OpuszczenieMyszy,
    WprowadzenieTekstu,
    KlawiszWcisniety,
    KlawiszZwolniony,
    Fokus,
    UtrataFokusu,
    ZmianaRozmiaru,
    Zamkniecie
};

struct ZdarzenieMyszki {
    TypZdarzenia typ{TypZdarzenia::RuchMyszy};
    int x{0};
    int y{0};
    int przycisk{0}; // 0 = brak, 1 = lewy, 2 = prawy, 3 = srodkowy
    bool obsluzone{false};

    constexpr bool czy_lewy_przycisk() const { return przycisk == 1; }
    constexpr bool czy_prawy_przycisk() const { return przycisk == 2; }
    constexpr Punkt pozycja() const { return {x, y}; }
};

struct ZdarzenieKlawiatury {
    TypZdarzenia typ{TypZdarzenia::KlawiszWcisniety};
    uint32_t kod_klawisza{0};
    uint32_t kod_scancode{0};
    uint32_t modyfikatory{0};
    bool obsluzone{false};
};

struct ZdarzenieWprowadzaniaTekstu {
    TypZdarzenia typ{TypZdarzenia::WprowadzenieTekstu};
    uint32_t punkt_kodowy{0}; // Unicode codepoint UTF-32
    bool obsluzone{false};
};

} // namespace BursztynUI
