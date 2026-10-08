#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "../bws_zdarzenia.h"

namespace BursztynUI {

// Internal GUI tag, never a change to the 40-byte BWS event ABI. Untagged
// values are logical editing commands; physical printable keys stay tagged.
constexpr uint32_t KLAWISZ_FIZYCZNY_GUI = 1U << 17;
// Per-process state survives a menu handing focus to a newly opened dialog.
inline bool klawiatura_fizyczna_gui=false;
inline bool echo_klawiatury(const bws_zdarzenie& e) {
    if(e.typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY)klawiatura_fizyczna_gui=true;
    return e.typ==BWS_ZDARZENIE_KLAWISZ && klawiatura_fizyczna_gui;
}
inline uint32_t klawisz_polecenia(const bws_zdarzenie& e) {
    if (e.typ == BWS_ZDARZENIE_KLAWISZ) {
        switch (e.kod) {
        case 8: case 9: case 13: case 27: return e.kod;
        case 10: return 13;
        case 127: return 0x53;
        default: return 0; // Printable text belongs exclusively to TEXT_INPUT.
        }
    }
    const bool usb = (e.kod & BWS_KEY_SOURCE_USB) != 0;
    const uint32_t b = e.kod & (usb ? 255U : 127U);
    if (usb) {
        switch (b) {
        case 40: case 88: return 13;
        case 41: return 27; case 42: return 8; case 43: return 9;
        case 44: return 32;
        case 76: return 0x53; case 79: return 0x4d; case 80: return 0x4b;
        case 81: return 0x50; case 82: return 0x48; case 74: return 0x47;
        case 77: return 0x4f; case 75: return 0x49; case 78: return 0x51;
        default: break;
        }
        if (!(e.przyciski & BWS_MOD_NUM)) {
            switch (b) {
            case 89: return 0x4f; case 90: return 0x50; case 91: return 0x51;
            case 92: return 0x4b; case 94: return 0x4d; case 95: return 0x47;
            case 96: return 0x48; case 97: return 0x49; case 99: return 0x53;
            default: break;
            }
        }
    } else {
        switch (b) {
        case 1: return 27; case 14: return 8; case 15: return 9;
        case 28: return 13; case 57: return 32;
        default: break;
        }
        if ((e.kod & BWS_KEY_SOURCE_E0) || !(e.przyciski & BWS_MOD_NUM)) {
            switch (b) {
            case 0x47: case 0x48: case 0x49: case 0x4b: case 0x4d:
            case 0x4f: case 0x50: case 0x51: case 0x53: return b;
            default: break;
            }
        }
    }
    return e.kod | KLAWISZ_FIZYCZNY_GUI;
}

enum class PrzyciskMyszy {
    Brak = 0,
    Lewy = 1,
    Prawy = 2,
    Srodkowy = 3
};

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
    // Pole liczbowe pozostaje zgodne z dotychczasowym API zrodlowym.
    int przycisk{0};
    uint32_t modyfikatory{0};
    uint64_t timestamp{0};
    int liczba_klikniec{1};
    bool obsluzone{false};

    constexpr PrzyciskMyszy przycisk_myszy() const {
        return przycisk == 1 ? PrzyciskMyszy::Lewy
             : przycisk == 2 ? PrzyciskMyszy::Prawy
             : przycisk == 3 ? PrzyciskMyszy::Srodkowy
                             : PrzyciskMyszy::Brak;
    }
    constexpr bool czy_lewy_przycisk() const { return przycisk_myszy() == PrzyciskMyszy::Lewy; }
    constexpr bool czy_prawy_przycisk() const { return przycisk_myszy() == PrzyciskMyszy::Prawy; }
    constexpr bool czy_srodkowy_przycisk() const { return przycisk_myszy() == PrzyciskMyszy::Srodkowy; }
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

    uint32_t polecenie() const {
        // Production dispatch already normalizes commands and tags the scan.
        if(kod_skanu & KLAWISZ_FIZYCZNY_GUI)return kod_klawisza;
        // Source compatibility for controls called directly with a physical
        // scan, using exactly the same decoder as the real BWS event route.
        if(kod_klawisza & (BWS_KEY_SOURCE_USB|BWS_KEY_SOURCE_E0)) {
            bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
            e.kod=kod_klawisza;e.przyciski=modyfikatory;
            return klawisz_polecenia(e);
        }
        return kod_klawisza==10?13:kod_klawisza==127?0x53:kod_klawisza;
    }

    constexpr bool czy_shift() const { return (modyfikatory & BWS_MOD_SHIFT) != 0; }
    constexpr bool czy_ctrl() const { return (modyfikatory & BWS_MOD_CTRL) != 0; }
    constexpr bool czy_alt() const { return (modyfikatory & BWS_MOD_ALT) != 0; }
};

struct ZdarzenieWprowadzaniaTekstu {
    TypZdarzenia typ{TypZdarzenia::WprowadzenieTekstu};
    uint32_t punkt_kodowy{0}; // Unicode codepoint UTF-32
    uint32_t modyfikatory{0};
    bool obsluzone{false};

    constexpr bool czy_shift() const { return (modyfikatory & BWS_MOD_SHIFT) != 0; }
    constexpr bool czy_ctrl() const { return (modyfikatory & BWS_MOD_CTRL) != 0; }
    constexpr bool czy_alt() const { return (modyfikatory & BWS_MOD_ALT) != 0; }
    constexpr bool czy_super() const { return (modyfikatory & BWS_MOD_SUPER) != 0; }
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
