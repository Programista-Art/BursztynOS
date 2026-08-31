#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "../bws_zdarzenia.h"

namespace BursztynUI {

enum class TypZdarzenia {
    WcisniecieMyszy,
    ZwolnienieMyszy,
    Klikniecie,
    PodwojneKlikniecie,
    RuchMyszy,
    WejscieMyszy,
    OpuszczenieMyszy,
    RolkaMyszy,
    WprowadzenieTekstu,
    KlawiszWcisniety,
    KlawiszZwolniony,
    UzyskanieSkupienia,
    UtrataSkupienia,
    Fokus = UzyskanieSkupienia,
    UtrataFokusu = UtrataSkupienia,
    ZmianaWartosci,
    ZmianaTekstu,
    ZmianaRozmiaru,
    WlasneRysowanie,
    Zamkniecie
};

struct ZdarzenieMyszki {
    TypZdarzenia typ{TypZdarzenia::RuchMyszy};
    int x{0};
    int y{0};
    int delta_x{0};
    int delta_y{0};
    int przycisk{0}; // 0 = brak, 1 = lewy, 2 = prawy, 3 = srodkowy
    uint32_t modyfikatory{0};
    uint64_t timestamp{0};
    int liczba_klikniec{1};
    bool obsluzone{false};

    constexpr bool czy_lewy_przycisk() const { return przycisk == 1; }
    constexpr bool czy_prawy_przycisk() const { return przycisk == 2; }
    constexpr bool czy_srodkowy_przycisk() const { return przycisk == 3; }
    constexpr bool czy_shift() const { return (modyfikatory & BWS_MOD_SHIFT) != 0; }
    constexpr bool czy_ctrl() const { return (modyfikatory & BWS_MOD_CTRL) != 0; }
    constexpr bool czy_alt() const { return (modyfikatory & BWS_MOD_ALT) != 0; }
    constexpr Punkt pozycja() const { return {x, y}; }
};

struct ZdarzenieKlawiatury {
    TypZdarzenia typ{TypZdarzenia::KlawiszWcisniety};
    uint32_t kod_klawisza{0};
    uint32_t kod_skanu{0};
    uint32_t modyfikatory{0};
    bool obsluzone{false};

    constexpr bool czy_shift() const { return (modyfikatory & BWS_MOD_SHIFT) != 0; }
    constexpr bool czy_ctrl() const { return (modyfikatory & BWS_MOD_CTRL) != 0; }
    constexpr bool czy_alt() const { return (modyfikatory & BWS_MOD_ALT) != 0; }
};

struct ZdarzenieWprowadzaniaTekstu {
    TypZdarzenia typ{TypZdarzenia::WprowadzenieTekstu};
    uint32_t punkt_kodowy{0}; // Unicode codepoint UTF-32
    bool obsluzone{false};
};

struct ZdarzenieRolki {
    TypZdarzenia typ{TypZdarzenia::RolkaMyszy};
    int x{0};
    int y{0};
    int delta_x{0};
    int delta_y{0};
    uint32_t modyfikatory{0};
    bool obsluzone{false};

    constexpr bool czy_shift() const { return (modyfikatory & BWS_MOD_SHIFT) != 0; }
    constexpr bool czy_ctrl() const { return (modyfikatory & BWS_MOD_CTRL) != 0; }
    constexpr bool czy_alt() const { return (modyfikatory & BWS_MOD_ALT) != 0; }
    constexpr Punkt pozycja() const { return {x, y}; }
};

} // namespace BursztynUI
