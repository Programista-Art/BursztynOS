/*
 * Menedzer Okien (Pulpit i Pasek Zadan) dla Bursztyn OS
 * Wersja uporzadkowana: poprawne hitboxy, Menu Start i odswiezanie zegara.
 */

#include "bursztyn_gui.h"

struct NaglowekBur {
    uint8_t  magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        4096, 32768, 0x601000,
        36864, 131072, 0x609000
    };
}

namespace {

constexpr int PASEK_WYS = 40;
constexpr int MENU_X = 10;
constexpr int MENU_W = 260;
constexpr int MENU_GORNY_MARGINES = 10;
constexpr int MENU_ELEMENT_WYS = 25;
constexpr int MENU_ELEMENTOW = 10;
constexpr int MENU_WYS =
    (MENU_GORNY_MARGINES * 2) + (MENU_ELEMENTOW * MENU_ELEMENT_WYS);

// Pozycja "Gry ->" w glownym Menu Start.
constexpr int MENU_GRY_INDEX = 7;
constexpr int MENU_GRY_W = 180;
constexpr int MENU_GRY_ELEMENTOW = 1;
constexpr int MENU_GRY_WYS =
    (MENU_GORNY_MARGINES * 2) + (MENU_GRY_ELEMENTOW * MENU_ELEMENT_WYS);

constexpr int PRZYCISK_MENU_X = 10;
constexpr int PRZYCISK_MENU_W = 80;
constexpr int PRZYCISK_MENU_H = 30;

constexpr int IKONA_W = 48;
constexpr int IKONA_H = 48;
constexpr int SKROT_W = 32;
constexpr int SKROT_H = 32;

constexpr int BUFOR_CZASU = 32;
constexpr int TASK_BUTTON_X = 100;
constexpr int TASK_BUTTON_W = 112;
constexpr int TASK_BUTTON_GAP = 4;
constexpr int PULPIT_MAX_WPISOW = 128;
constexpr int PULPIT_LIST_CAP = 16384;
constexpr int PULPIT_NAZWA_CAP = 56;
constexpr int PULPIT_GRID_W = 82;
constexpr int PULPIT_GRID_H = 82;
constexpr int PULPIT_GRID_Y = 140;
constexpr int PULPIT_REFRESH_TICKS = 3;

const char* const SCIEZKA_POWLOKA = "/shell.bur";
const char* const SCIEZKA_EKSPLORATOR =
    "/programy/eksplorator.cebula/eksplorator-plikow.bur";
const char* const SCIEZKA_NOTATNIK = "/programy/notatnik.cebula/notatnik.bur";
const char* const SCIEZKA_KALKULATOR = "/programy/kalkulator.cebula/kalkulator.bur";
const char* const SCIEZKA_HUSSAR = "/programy/przegladarka.cebula/przegladarka.bur";
const char* const SCIEZKA_TEST = "/programy/test.cebula/test.bur";
const char* const SCIEZKA_SNAKE = "/gry/snake.cebula/snake.bur";
const char* const SCIEZKA_PRZEGLADARKA_OBRAZOW =
    "/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur";
const char* const SCIEZKA_PULPIT = "/uzytkownicy/Pulpit";

struct WpisPulpitu {
    char nazwa[PULPIT_NAZWA_CAP];
    bool folder;
};


// Lokalny format ikony pulpitu.
// Nie zależy od GuiIkona/gui_rysuj_ikone, więc działa z obecnym bursztyn_gui.h.
struct FragmentIkonyPulpitu {
    uint8_t x;
    uint8_t y;
    uint8_t szer;
    uint8_t wys;
    uint8_t indeks_koloru;
} __attribute__((packed));

static_assert(sizeof(FragmentIkonyPulpitu) == 5,
              "FragmentIkonyPulpitu musi miec dokladnie 5 bajtow");

// __attribute__((section(".data"), used))
// static const uint32_t IKONA_SNAKE_PALETA[] = {
//     0X00FFBF00,
// };

// __attribute__((section(".data"), used))
// static const FragmentIkonyPulpitu IKONA_SNAKE_FRAGMENTY[] = {
//     {6, 2, 2, 1, 0},
//     {5, 3, 5, 1, 0},
//     {34, 3, 4, 1, 0},
//     {5, 4, 7, 1, 0},
//     {34, 4, 1, 1, 0},
//     {36, 4, 1, 1, 0},
//     {6, 5, 4, 1, 0},
//     {11, 5, 2, 1, 0},
//     {33, 5, 4, 1, 0},
//     {6, 6, 3, 1, 0},
//     {10, 6, 1, 1, 0},
//     {12, 6, 2, 1, 0},
//     {32, 6, 3, 1, 0},
//     {7, 7, 6, 1, 0},
//     {14, 7, 1, 1, 0},
//     {31, 7, 1, 1, 0},
//     {34, 7, 1, 1, 0},
//     {7, 8, 9, 1, 0},
//     {31, 8, 3, 1, 0},
//     {7, 9, 1, 1, 0},
//     {9, 9, 2, 1, 0},
//     {12, 9, 1, 1, 0},
//     {14, 9, 2, 1, 0},
//     {29, 9, 4, 1, 0},
//     {7, 10, 7, 1, 0},
//     {15, 10, 4, 1, 0},
//     {20, 10, 1, 1, 0},
//     {22, 10, 1, 1, 0},
//     {24, 10, 2, 1, 0},
//     {28, 10, 3, 1, 0},
//     {35, 10, 2, 1, 0},
//     {9, 11, 1, 2, 0},
//     {11, 11, 2, 1, 0},
//     {14, 11, 6, 1, 0},
//     {21, 11, 1, 1, 0},
//     {23, 11, 1, 1, 0},
//     {25, 11, 1, 2, 0},
//     {27, 11, 6, 1, 0},
//     {35, 11, 1, 1, 0},
//     {11, 12, 8, 1, 0},
//     {20, 12, 3, 1, 0},
//     {27, 12, 2, 1, 0},
//     {30, 12, 1, 1, 0},
//     {33, 12, 2, 1, 0},
//     {11, 13, 1, 1, 0},
//     {13, 13, 4, 1, 0},
//     {19, 13, 4, 1, 0},
//     {24, 13, 6, 1, 0},
//     {32, 13, 1, 1, 0},
//     {10, 14, 1, 1, 0},
//     {12, 14, 2, 1, 0},
//     {15, 14, 4, 1, 0},
//     {20, 14, 6, 1, 0},
//     {27, 14, 2, 1, 0},
//     {31, 14, 1, 2, 0},
//     {33, 14, 1, 1, 0},
//     {13, 15, 1, 1, 0},
//     {17, 15, 1, 1, 0},
//     {24, 15, 1, 1, 0},
//     {26, 15, 2, 1, 0},
//     {13, 16, 3, 1, 0},
//     {18, 16, 1, 1, 0},
//     {22, 16, 2, 1, 0},
//     {25, 16, 1, 1, 0},
//     {28, 16, 1, 1, 0},
//     {30, 16, 1, 1, 0},
//     {12, 17, 1, 1, 0},
//     {14, 17, 1, 1, 0},
//     {17, 17, 3, 1, 0},
//     {23, 17, 3, 1, 0},
//     {27, 17, 1, 1, 0},
//     {30, 17, 2, 1, 0},
//     {18, 18, 8, 1, 0},
//     {31, 18, 3, 1, 0},
//     {13, 19, 1, 1, 0},
//     {20, 19, 2, 1, 0},
//     {23, 19, 5, 1, 0},
//     {29, 19, 1, 1, 0},
//     {16, 20, 1, 2, 0},
//     {19, 20, 4, 1, 0},
//     {25, 20, 2, 1, 0},
//     {14, 21, 1, 1, 0},
//     {18, 21, 2, 1, 0},
//     {21, 21, 4, 1, 0},
//     {17, 22, 7, 1, 0},
//     {26, 22, 1, 1, 0},
//     {14, 23, 1, 1, 0},
//     {16, 23, 2, 1, 0},
//     {19, 23, 2, 1, 0},
//     {24, 23, 2, 1, 0},
//     {28, 23, 1, 1, 0},
//     {14, 24, 3, 1, 0},
//     {21, 24, 5, 1, 0},
//     {12, 25, 3, 1, 0},
//     {17, 25, 1, 1, 0},
//     {19, 25, 1, 1, 0},
//     {21, 25, 1, 1, 0},
//     {24, 25, 1, 1, 0},
//     {26, 25, 1, 1, 0},
//     {30, 25, 1, 1, 0},
//     {11, 26, 2, 2, 0},
//     {15, 26, 1, 1, 0},
//     {19, 26, 2, 1, 0},
//     {24, 26, 4, 1, 0},
//     {14, 27, 1, 1, 0},
//     {18, 27, 2, 1, 0},
//     {27, 27, 2, 1, 0},
//     {31, 27, 1, 1, 0},
//     {10, 28, 2, 1, 0},
//     {17, 28, 2, 1, 0},
//     {21, 28, 1, 1, 0},
//     {10, 29, 1, 1, 0},
//     {13, 29, 1, 1, 0},
//     {15, 29, 2, 1, 0},
//     {22, 29, 1, 1, 0},
//     {25, 29, 2, 1, 0},
//     {9, 30, 2, 1, 0},
//     {12, 30, 1, 1, 0},
//     {24, 30, 1, 1, 0},
//     {27, 30, 1, 1, 0},
//     {9, 31, 1, 1, 0},
//     {11, 31, 1, 1, 0},
//     {13, 31, 1, 1, 0},
//     {26, 31, 2, 1, 0},
//     {32, 31, 1, 1, 0},
//     {9, 32, 3, 1, 0},
//     {29, 32, 1, 1, 0},
//     {33, 32, 1, 2, 0},
//     {10, 33, 1, 1, 0},
//     {7, 34, 3, 1, 0},
//     {32, 34, 3, 1, 0},
//     {7, 35, 1, 1, 0},
//     {34, 35, 2, 1, 0},
//     {6, 36, 2, 1, 0},
//     {6, 37, 1, 1, 0},
// };


// static constexpr uint32_t IKONA_SNAKE_KOLOROW =
//     static_cast<uint32_t>(sizeof(IKONA_SNAKE_PALETA) /
//                           sizeof(IKONA_SNAKE_PALETA[0]));

// static constexpr uint32_t IKONA_SNAKE_FRAGMENTOW =
//     static_cast<uint32_t>(sizeof(IKONA_SNAKE_FRAGMENTY) /
//                           sizeof(IKONA_SNAKE_FRAGMENTY[0]));

// Ikona Snake — 44×44, 2 kolorów (kwantyzacja 48), 221 fragmentów (1105B + 8B paleta) — .data (nie .text)
// FragmentIkonyPulpitu: {x, y, szer, wys, idx_koloru} — współrzędne względne wewnątrz ikony (0..43)
// Ikona Snake — 44×44, 1 kolorów (kwantyzacja 48), 47 fragmentów (235B + 4B paleta) — .data (nie .text)
// FragmentIkonyPulpitu: {x, y, szer, wys, idx_koloru} — współrzędne względne wewnątrz ikony (0..43)


// Ikona Snake — 44×44, 48 kolorów (kwantyzacja 48), 372 fragmentów (1860B + 192B paleta) — .data (nie .text)
// FragmentIkonyPulpitu: {x, y, szer, wys, idx_koloru} — współrzędne względne wewnątrz ikony (0..43)
__attribute__((section(".data"), used))
static const uint32_t IKONA_SNAKE_PALETA[] = {
    0x00AFD654,
    0x00AFD754,
    0x00AFD855,
    0x00AED655,
    0x00AED654,
    0x00AED753,
    0x00AFD755,
    0x00AFD854,
    0x00AED755,
    0x00A7CF52,
    0x0096BE4B,
    0x00AFD655,
    0x00AED754,
    0x0097BE4B,
    0x009DC54E,
    0x00ADD554,
    0x00A4CC50,
    0x00B6D554,
    0x00EEDB60,
    0x00FEC850,
    0x00FFC850,
    0x00FF8087,
    0x00FDDB63,
    0x00FFDC64,
    0x00FEDC64,
    0x00FEDB64,
    0x00FEDC63,
    0x00F4DB61,
    0x00B1D755,
    0x00B0D755,
    0x00ACD453,
    0x009FC74F,
    0x00FDDC64,
    0x00ACD454,
    0x0099C14C,
    0x00FCDB63,
    0x0097BF4B,
    0x00ABD353,
    0x00ADD654,
    0x00AAD253,
    0x00A5CD50,
    0x009CC44D,
    0x00A8D052,
    0x00F7DB62,
    0x00A9D152,
    0x0098BF4B,
    0x009AC24D,
    0x0097C04B,
};

__attribute__((section(".data"), used))
static const FragmentIkonyPulpitu IKONA_SNAKE_FRAGMENTY[] = {
    {11, 3, 1, 1, 0},
    {12, 3, 1, 1, 1},
    {13, 3, 1, 1, 2},
    {14, 3, 1, 1, 3},
    {15, 3, 1, 1, 4},
    {9, 4, 1, 1, 5},
    {10, 4, 7, 1, 6},
    {17, 4, 1, 1, 1},
    {7, 5, 1, 1, 7},
    {8, 5, 1, 1, 8},
    {9, 5, 2, 1, 6},
    {11, 5, 1, 1, 9},
    {12, 5, 1, 1, 10},
    {13, 5, 1, 1, 3},
    {14, 5, 4, 1, 6},
    {18, 5, 1, 1, 11},
    {19, 5, 1, 1, 12},
    {6, 6, 1, 1, 0},
    {7, 6, 4, 1, 6},
    {11, 6, 2, 2, 10},
    {13, 6, 1, 1, 13},
    {14, 6, 6, 1, 6},
    {20, 6, 1, 1, 12},
    {5, 7, 1, 1, 7},
    {6, 7, 5, 1, 6},
    {13, 7, 1, 1, 14},
    {14, 7, 7, 1, 6},
    {5, 8, 6, 1, 6},
    {11, 8, 1, 1, 15},
    {12, 8, 1, 1, 16},
    {13, 8, 1, 1, 3},
    {14, 8, 3, 1, 17},
    {17, 8, 4, 1, 6},
    {21, 8, 1, 2, 5},
    {5, 9, 2, 1, 17},
    {7, 9, 2, 1, 6},
    {9, 9, 1, 1, 11},
    {10, 9, 3, 1, 17},
    {13, 9, 1, 1, 18},
    {14, 9, 1, 1, 19},
    {15, 9, 1, 1, 20},
    {16, 9, 1, 1, 19},
    {17, 9, 1, 2, 17},
    {18, 9, 3, 2, 6},
    {5, 10, 1, 1, 21},
    {6, 10, 7, 1, 19},
    {13, 10, 4, 1, 20},
    {21, 10, 1, 1, 8},
    {4, 11, 2, 1, 21},
    {7, 11, 1, 1, 20},
    {8, 11, 1, 1, 19},
    {9, 11, 6, 1, 20},
    {15, 11, 1, 1, 19},
    {16, 11, 1, 1, 22},
    {17, 11, 1, 4, 18},
    {18, 11, 4, 2, 6},
    {0, 12, 4, 1, 21},
    {12, 12, 3, 1, 22},
    {15, 12, 2, 1, 23},
    {1, 13, 2, 2, 21},
    {12, 13, 5, 1, 23},
    {18, 13, 3, 3, 6},
    {21, 13, 1, 1, 8},
    {12, 14, 1, 1, 24},
    {13, 14, 4, 1, 23},
    {21, 14, 1, 1, 1},
    {12, 15, 5, 1, 23},
    {17, 15, 1, 2, 17},
    {21, 15, 1, 1, 12},
    {12, 16, 1, 1, 25},
    {13, 16, 3, 1, 23},
    {16, 16, 1, 1, 26},
    {18, 16, 2, 3, 6},
    {20, 16, 1, 1, 8},
    {12, 17, 4, 2, 23},
    {16, 17, 1, 1, 27},
    {17, 17, 1, 1, 28},
    {20, 17, 1, 1, 1},
    {11, 18, 1, 1, 24},
    {16, 18, 1, 1, 18},
    {17, 18, 1, 1, 29},
    {20, 18, 1, 1, 0},
    {31, 18, 1, 1, 30},
    {32, 18, 1, 1, 16},
    {33, 18, 1, 1, 31},
    {11, 19, 4, 2, 23},
    {15, 19, 1, 1, 32},
    {16, 19, 1, 1, 17},
    {17, 19, 3, 1, 6},
    {30, 19, 1, 1, 8},
    {31, 19, 1, 1, 14},
    {15, 20, 1, 1, 18},
    {16, 20, 1, 1, 29},
    {17, 20, 2, 1, 6},
    {19, 20, 1, 1, 1},
    {20, 20, 1, 1, 12},
    {21, 20, 1, 1, 8},
    {22, 20, 1, 1, 12},
    {23, 20, 1, 1, 7},
    {24, 20, 1, 1, 6},
    {25, 20, 1, 1, 12},
    {26, 20, 1, 1, 1},
    {29, 20, 1, 1, 33},
    {30, 20, 1, 1, 14},
    {31, 20, 1, 1, 34},
    {10, 21, 1, 2, 35},
    {11, 21, 3, 2, 23},
    {14, 21, 1, 1, 26},
    {15, 21, 1, 1, 17},
    {16, 21, 13, 1, 6},
    {29, 21, 1, 1, 16},
    {30, 21, 1, 1, 36},
    {31, 21, 1, 1, 37},
    {32, 21, 1, 1, 1},
    {33, 21, 1, 1, 6},
    {34, 21, 1, 1, 38},
    {14, 22, 1, 1, 18},
    {15, 22, 3, 2, 6},
    {18, 22, 1, 1, 8},
    {19, 22, 1, 1, 15},
    {20, 22, 1, 1, 37},
    {21, 22, 1, 1, 39},
    {22, 22, 1, 1, 9},
    {23, 22, 2, 1, 16},
    {25, 22, 2, 1, 31},
    {27, 22, 1, 1, 40},
    {28, 22, 1, 1, 15},
    {29, 22, 1, 1, 34},
    {30, 22, 1, 1, 41},
    {31, 22, 1, 1, 42},
    {32, 22, 1, 1, 39},
    {33, 22, 1, 1, 33},
    {34, 22, 1, 1, 3},
    {35, 22, 2, 1, 6},
    {8, 23, 1, 1, 12},
    {9, 23, 1, 2, 17},
    {10, 23, 1, 1, 43},
    {11, 23, 2, 2, 23},
    {13, 23, 1, 1, 24},
    {14, 23, 1, 2, 17},
    {18, 23, 1, 1, 44},
    {19, 23, 1, 1, 14},
    {20, 23, 1, 1, 41},
    {27, 23, 1, 1, 44},
    {28, 23, 1, 1, 31},
    {29, 23, 1, 1, 36},
    {30, 23, 2, 1, 10},
    {32, 23, 1, 1, 45},
    {33, 23, 1, 1, 46},
    {34, 23, 1, 1, 14},
    {35, 23, 1, 1, 16},
    {36, 23, 1, 1, 33},
    {37, 23, 1, 1, 2},
    {38, 23, 1, 1, 6},
    {39, 23, 1, 1, 4},
    {7, 24, 1, 1, 2},
    {8, 24, 1, 1, 6},
    {10, 24, 1, 1, 32},
    {13, 24, 1, 1, 43},
    {15, 24, 2, 2, 6},
    {17, 24, 1, 1, 1},
    {27, 24, 1, 1, 37},
    {28, 24, 2, 1, 36},
    {32, 24, 4, 1, 10},
    {36, 24, 1, 1, 36},
    {37, 24, 1, 1, 16},
    {38, 24, 1, 1, 15},
    {39, 24, 2, 1, 6},
    {6, 25, 1, 1, 8},
    {7, 25, 2, 1, 6},
    {9, 25, 1, 2, 18},
    {10, 25, 3, 3, 23},
    {13, 25, 1, 2, 18},
    {14, 25, 1, 1, 28},
    {17, 25, 1, 1, 4},
    {26, 25, 1, 1, 2},
    {27, 25, 1, 1, 16},
    {28, 25, 1, 2, 10},
    {35, 25, 3, 1, 10},
    {38, 25, 1, 1, 46},
    {39, 25, 1, 1, 9},
    {40, 25, 2, 1, 6},
    {5, 26, 4, 1, 6},
    {14, 26, 2, 3, 6},
    {16, 26, 1, 1, 2},
    {26, 26, 1, 1, 3},
    {27, 26, 1, 1, 14},
    {36, 26, 1, 1, 13},
    {37, 26, 3, 1, 10},
    {40, 26, 1, 1, 9},
    {41, 26, 1, 1, 6},
    {42, 26, 1, 1, 8},
    {4, 27, 4, 1, 6},
    {8, 27, 1, 1, 8},
    {9, 27, 1, 1, 27},
    {13, 27, 1, 1, 17},
    {16, 27, 1, 1, 11},
    {25, 27, 1, 1, 6},
    {26, 27, 1, 1, 15},
    {27, 27, 1, 1, 45},
    {28, 27, 1, 1, 47},
    {38, 27, 1, 1, 13},
    {39, 27, 1, 1, 10},
    {40, 27, 1, 1, 41},
    {41, 27, 1, 1, 4},
    {42, 27, 1, 3, 6},
    {43, 27, 1, 1, 29},
    {3, 28, 1, 1, 3},
    {4, 28, 3, 1, 6},
    {7, 28, 1, 1, 8},
    {9, 28, 1, 1, 25},
    {10, 28, 2, 4, 23},
    {12, 28, 1, 1, 25},
    {13, 28, 1, 1, 29},
    {25, 28, 1, 1, 8},
    {26, 28, 1, 1, 40},
    {27, 28, 1, 2, 10},
    {38, 28, 2, 1, 10},
    {40, 28, 1, 1, 34},
    {41, 28, 1, 1, 15},
    {43, 28, 1, 1, 7},
    {3, 29, 4, 2, 6},
    {9, 29, 1, 1, 24},
    {12, 29, 1, 1, 43},
    {13, 29, 2, 1, 6},
    {15, 29, 1, 1, 3},
    {24, 29, 1, 1, 1},
    {25, 29, 1, 1, 38},
    {26, 29, 1, 1, 34},
    {38, 29, 1, 1, 13},
    {39, 29, 1, 1, 10},
    {40, 29, 1, 1, 14},
    {41, 29, 1, 1, 4},
    {43, 29, 1, 1, 8},
    {9, 30, 1, 1, 32},
    {12, 30, 1, 1, 27},
    {13, 30, 3, 2, 6},
    {24, 30, 1, 1, 2},
    {25, 30, 1, 1, 40},
    {26, 30, 1, 2, 10},
    {27, 30, 1, 1, 13},
    {38, 30, 2, 1, 10},
    {40, 30, 1, 1, 44},
    {41, 30, 2, 1, 6},
    {43, 30, 1, 1, 1},
    {3, 31, 1, 1, 38},
    {4, 31, 3, 3, 6},
    {9, 31, 1, 1, 22},
    {12, 31, 1, 1, 43},
    {23, 31, 1, 1, 3},
    {24, 31, 1, 1, 15},
    {25, 31, 1, 1, 41},
    {38, 31, 1, 1, 46},
    {39, 31, 1, 1, 44},
    {40, 31, 3, 1, 6},
    {3, 32, 1, 1, 30},
    {10, 32, 1, 1, 25},
    {11, 32, 1, 1, 23},
    {12, 32, 1, 1, 22},
    {13, 32, 1, 1, 28},
    {14, 32, 1, 2, 6},
    {15, 32, 1, 2, 8},
    {23, 32, 1, 1, 6},
    {24, 32, 1, 1, 16},
    {25, 32, 2, 1, 10},
    {37, 32, 1, 1, 16},
    {38, 32, 1, 1, 30},
    {39, 32, 3, 1, 6},
    {42, 32, 1, 1, 7},
    {3, 33, 1, 1, 44},
    {10, 33, 1, 1, 26},
    {11, 33, 2, 2, 23},
    {13, 33, 1, 1, 17},
    {22, 33, 1, 1, 2},
    {23, 33, 1, 1, 42},
    {24, 33, 2, 1, 10},
    {35, 33, 1, 1, 16},
    {36, 33, 1, 1, 37},
    {37, 33, 4, 1, 6},
    {41, 33, 1, 1, 12},
    {3, 34, 1, 1, 16},
    {4, 34, 4, 1, 6},
    {10, 34, 1, 1, 32},
    {13, 34, 1, 1, 18},
    {14, 34, 2, 1, 6},
    {16, 34, 1, 1, 1},
    {20, 34, 1, 1, 4},
    {21, 34, 1, 1, 6},
    {22, 34, 1, 1, 39},
    {23, 34, 1, 1, 46},
    {24, 34, 1, 1, 10},
    {33, 34, 1, 1, 4},
    {34, 34, 1, 1, 15},
    {35, 34, 5, 1, 6},
    {40, 34, 1, 1, 8},
    {4, 35, 1, 1, 44},
    {5, 35, 3, 1, 6},
    {8, 35, 1, 1, 1},
    {11, 35, 1, 1, 22},
    {12, 35, 1, 1, 23},
    {13, 35, 1, 1, 43},
    {14, 35, 1, 2, 17},
    {15, 35, 2, 1, 6},
    {17, 35, 1, 1, 1},
    {18, 35, 1, 1, 0},
    {19, 35, 2, 1, 6},
    {21, 35, 1, 1, 39},
    {22, 35, 1, 1, 34},
    {23, 35, 1, 1, 10},
    {24, 35, 1, 1, 13},
    {30, 35, 1, 1, 1},
    {31, 35, 1, 1, 2},
    {32, 35, 7, 1, 6},
    {39, 35, 1, 1, 11},
    {4, 36, 1, 1, 34},
    {5, 36, 1, 1, 44},
    {6, 36, 3, 1, 6},
    {9, 36, 1, 1, 0},
    {10, 36, 1, 1, 6},
    {11, 36, 1, 1, 17},
    {12, 36, 2, 1, 18},
    {15, 36, 4, 1, 6},
    {19, 36, 1, 1, 4},
    {20, 36, 1, 1, 16},
    {21, 36, 1, 1, 34},
    {22, 36, 1, 1, 36},
    {23, 36, 1, 1, 16},
    {24, 36, 1, 1, 4},
    {25, 36, 1, 1, 6},
    {26, 36, 1, 1, 11},
    {27, 36, 1, 1, 6},
    {28, 36, 1, 1, 8},
    {29, 36, 9, 1, 6},
    {5, 37, 1, 1, 34},
    {6, 37, 1, 1, 37},
    {7, 37, 10, 1, 6},
    {17, 37, 1, 1, 3},
    {18, 37, 1, 1, 44},
    {19, 37, 1, 2, 34},
    {20, 37, 1, 1, 10},
    {21, 37, 1, 1, 45},
    {22, 37, 1, 1, 9},
    {23, 37, 12, 1, 6},
    {35, 37, 1, 1, 1},
    {6, 38, 1, 1, 47},
    {7, 38, 1, 1, 31},
    {8, 38, 1, 1, 9},
    {9, 38, 1, 1, 30},
    {10, 38, 3, 1, 4},
    {13, 38, 1, 1, 30},
    {14, 38, 1, 1, 39},
    {15, 38, 1, 1, 9},
    {16, 38, 1, 1, 31},
    {17, 38, 1, 1, 41},
    {18, 38, 1, 1, 36},
    {20, 38, 1, 1, 31},
    {21, 38, 1, 1, 37},
    {22, 38, 9, 1, 6},
    {31, 38, 2, 1, 8},
    {33, 38, 1, 1, 4},
    {7, 39, 2, 1, 13},
    {9, 39, 6, 1, 36},
    {15, 39, 2, 1, 10},
    {17, 39, 1, 1, 47},
    {18, 39, 1, 1, 16},
    {19, 39, 1, 1, 15},
    {20, 39, 9, 1, 6},
    {29, 39, 2, 1, 2},
    {10, 40, 2, 1, 10},
    {12, 40, 1, 1, 36},
    {13, 40, 1, 1, 10},
    {14, 40, 1, 1, 36},
};

static constexpr uint32_t IKONA_SNAKE_KOLOROW =
    static_cast<uint32_t>(sizeof(IKONA_SNAKE_PALETA) / sizeof(IKONA_SNAKE_PALETA[0]));

static constexpr uint32_t IKONA_SNAKE_FRAGMENTOW =
    static_cast<uint32_t>(sizeof(IKONA_SNAKE_FRAGMENTY) / sizeof(IKONA_SNAKE_FRAGMENTY[0]));

// koniec ikony


__attribute__((noinline))
static void rysuj_ikone_paleta(
    int x,
    int y,
    const uint32_t* paleta,
    uint32_t liczba_kolorow,
    const FragmentIkonyPulpitu* fragmenty,
    uint32_t liczba_fragmentow)
{
    if (!paleta || !fragmenty || liczba_kolorow == 0)
        return;

    for (uint32_t i = 0; i < liczba_fragmentow; ++i) {
        const FragmentIkonyPulpitu& f = fragmenty[i];

        if (f.szer == 0 || f.wys == 0)
            continue;

        if (static_cast<uint32_t>(f.indeks_koloru) >= liczba_kolorow)
            continue;

        gui_rysuj_prostokat(
            x + static_cast<int>(f.x),
            y + static_cast<int>(f.y),
            static_cast<int>(f.szer),
            static_cast<int>(f.wys),
            paleta[f.indeks_koloru]);
    }
}




int screen_w = 1024;
int screen_h = 768;
bool menu_start_otwarte = false;
bool menu_gry_otwarte = false;
char ostatni_czas[BUFOR_CZASU] = {};
GuiOknoInfo okna_taskbara[SKLADACZ_MAKS_WARSTW] = {};
uint32_t liczba_okien_taskbara = 0;
WpisPulpitu wpisy_pulpitu[PULPIT_MAX_WPISOW] = {};
char lista_pulpitu[PULPIT_LIST_CAP] = {};
int liczba_wpisow_pulpitu = 0;
int zaznaczony_wpis_pulpitu = -1;
uint64_t hash_pulpitu = 0;
bool hash_pulpitu_wazny = false;
bool pulpit_drop_hover = false;
int pulpit_refresh_ticks = 0;
BwsCelDrop cele_drop_pulpitu[BWS_DROP_CELE_MAX] = {};

enum class PulpitContextAction : uint8_t {
    NONE, OPEN, RUN, COPY, CUT, PASTE, REFRESH, NOWY_PLIK, NOWY_FOLDER
};
struct PulpitContextItem { const char* label; PulpitContextAction action; };
bool pulpit_context_open = false;
int pulpit_context_x = 0;
int pulpit_context_y = 0;
int pulpit_context_w = 184;
int pulpit_context_count = 0;
int pulpit_context_hover = -1;
PulpitContextItem pulpit_context_items[8] = {};

/* Wspolny dialog modalny (ten sam mechanizm co w Eksploratorze). */
bool odswiez_wpisy_pulpitu();
void RysujPulpit(bool wymus_pelne_odswiezenie);
enum class PulpitModalTyp : uint8_t { BRAK, PLIK, FOLDER };
enum class PulpitAnsi : uint8_t { NONE, ESC, CSI, CSI_DELETE };
GuiModalDialog pulpit_modal = {};
PulpitModalTyp pulpit_modal_typ = PulpitModalTyp::BRAK;
PulpitAnsi pulpit_ansi = PulpitAnsi::NONE;

bool nazwa_nowego_tworu_poprawna(const char* nazwa) {
    if (!nazwa) return false;
    int dlugosc = 0;
    while (nazwa[dlugosc] != '\0' && dlugosc < 48) ++dlugosc;
    if (dlugosc == 0 || nazwa[dlugosc] != '\0') return false;
    if ((dlugosc == 1 && nazwa[0] == '.') ||
        (dlugosc == 2 && nazwa[0] == '.' && nazwa[1] == '.'))
        return false;
    for (int i = 0; i < dlugosc; ++i) {
        const uint8_t znak = static_cast<uint8_t>(nazwa[i]);
        if (znak < 0x20U || znak == 0x7FU ||
            znak == '/' || znak == '\\')
            return false;
    }
    return true;
}

void zamknij_pulpit_modal() {
    if (gui_modal_otwarty(&pulpit_modal)) gui_modal_zamknij(&pulpit_modal);
    pulpit_modal_typ = PulpitModalTyp::BRAK;
    pulpit_ansi = PulpitAnsi::NONE;
}

void odswiez_pulpit_natychmiast() {
    hash_pulpitu_wazny = false;
    (void)odswiez_wpisy_pulpitu();
    RysujPulpit(true);
}

void zatwierdz_pulpit_modal() {
    const char* nazwa = gui_modal_tekst(&pulpit_modal);
    char pelna[512] = {};
    bool utworzono = false;
    if (pulpit_modal_typ != PulpitModalTyp::BRAK &&
        nazwa_nowego_tworu_poprawna(nazwa)) {
        int pozycja = 0;
        while (SCIEZKA_PULPIT[pozycja] != '\0' &&
               pozycja < static_cast<int>(sizeof(pelna)) - 2) {
            pelna[pozycja] = SCIEZKA_PULPIT[pozycja];
            ++pozycja;
        }
        pelna[pozycja++] = '/';
        int nazwa_i = 0;
        while (nazwa[nazwa_i] != '\0' &&
               pozycja < static_cast<int>(sizeof(pelna)) - 1) {
            pelna[pozycja++] = nazwa[nazwa_i++];
        }
        pelna[pozycja] = '\0';
        utworzono = pulpit_modal_typ == PulpitModalTyp::FOLDER
            ? utworz_katalog_uzytkownika(pelna)
            : utworz(pelna);
    }
    zamknij_pulpit_modal();
    zaznaczony_wpis_pulpitu = -1;
    if (utworzono) odswiez_pulpit_natychmiast();
    else gui_odswiez();
}

bool otworz_pulpit_modal(PulpitModalTyp typ) {
    zamknij_pulpit_modal();
    constexpr int SZER = 360;
    constexpr int WYS = 176;
    int x = (screen_w - SZER) / 2;
    int y = (screen_h - PASEK_WYS - WYS) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    GuiModalProstokat obszar{x, y, SZER, WYS};
    if (!gui_modal_otworz(&pulpit_modal,
                          typ == PulpitModalTyp::FOLDER ? "Nowy folder"
                                                        : "Nowy plik",
                          "", nullptr, &obszar, 47))
        return false;
    pulpit_modal_typ = typ;
    pulpit_ansi = PulpitAnsi::NONE;
    (void)gui_rejestruj_cele_drop(nullptr, 0);
    gui_odswiez();
    return true;
}

bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h);
void wyzeruj_bufor(char* bufor, int rozmiar);

int dlugosc_limit(const char* tekst, int limit) {
    if (!tekst) return limit;
    for (int i = 0; i < limit; ++i) if (tekst[i] == '\0') return i;
    return limit;
}

bool kopiuj_tekst(char* cel, int pojemnosc, const char* zrodlo) {
    if (!cel || pojemnosc <= 0 || !zrodlo) return false;
    const int n = dlugosc_limit(zrodlo, pojemnosc);
    if (n >= pojemnosc) return false;
    for (int i = 0; i <= n; ++i) cel[i] = zrodlo[i];
    return true;
}

bool konczy_sie(const char* tekst, const char* sufiks) {
    const int n = dlugosc_limit(tekst, 512);
    const int s = dlugosc_limit(sufiks, 32);
    if (n >= 512 || s >= 32 || s > n) return false;
    for (int i = 0; i < s; ++i) if (tekst[n - s + i] != sufiks[i]) return false;
    return true;
}

bool sciezka_wpisu_pulpitu(int indeks, char* wynik, int pojemnosc) {
    if (!wynik || indeks < 0 || indeks >= liczba_wpisow_pulpitu) return false;
    const int baza = dlugosc_limit(SCIEZKA_PULPIT, pojemnosc);
    const int nazwa = dlugosc_limit(wpisy_pulpitu[indeks].nazwa, PULPIT_NAZWA_CAP);
    if (baza >= pojemnosc || nazwa >= PULPIT_NAZWA_CAP ||
        baza + 1 + nazwa + 1 > pojemnosc) return false;
    int out = 0;
    for (int i = 0; i < baza; ++i) wynik[out++] = SCIEZKA_PULPIT[i];
    wynik[out++] = '/';
    for (int i = 0; i < nazwa; ++i) wynik[out++] = wpisy_pulpitu[indeks].nazwa[i];
    wynik[out] = '\0';
    return true;
}

uint64_t hash_listy(const char* tekst, int n) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; i < n; ++i) {
        hash ^= static_cast<uint8_t>(tekst[i]);
        hash *= UINT64_C(1099511628211);
    }
    return hash ^ static_cast<uint64_t>(n);
}

bool odswiez_wpisy_pulpitu() {
    wyzeruj_bufor(lista_pulpitu, PULPIT_LIST_CAP);
    if (!wylistuj_katalog_uzytkownika(
            SCIEZKA_PULPIT, lista_pulpitu, PULPIT_LIST_CAP)) return false;
    const int length = dlugosc_limit(lista_pulpitu, PULPIT_LIST_CAP);
    if (length >= PULPIT_LIST_CAP || (length > 0 && lista_pulpitu[length - 1] != '\n'))
        return false;
    const uint64_t nowy_hash = hash_listy(lista_pulpitu, length);
    if (hash_pulpitu_wazny && nowy_hash == hash_pulpitu) return false;

    WpisPulpitu nowe[PULPIT_MAX_WPISOW] = {};
    int count = 0;
    int pos = 0;
    while (pos < length) {
        int end = pos;
        while (end < length && lista_pulpitu[end] != '\n') ++end;
        if (end - pos <= 7 || count >= PULPIT_MAX_WPISOW) return false;
        const bool folder = lista_pulpitu[pos] == '[' && lista_pulpitu[pos + 1] == 'K';
        const bool file = lista_pulpitu[pos] == '[' && lista_pulpitu[pos + 1] == 'P';
        const int name_length = end - pos - 7;
        if ((!folder && !file) || name_length <= 0 || name_length >= PULPIT_NAZWA_CAP)
            return false;
        nowe[count].folder = folder;
        for (int i = 0; i < name_length; ++i)
            nowe[count].nazwa[i] = lista_pulpitu[pos + 7 + i];
        ++count;
        pos = end + 1;
    }
    for (int i = 0; i < count; ++i) wpisy_pulpitu[i] = nowe[i];
    liczba_wpisow_pulpitu = count;
    if (zaznaczony_wpis_pulpitu >= count) zaznaczony_wpis_pulpitu = -1;
    hash_pulpitu = nowy_hash;
    hash_pulpitu_wazny = true;
    return true;
}

void prostokat_wpisu_pulpitu(int indeks, int* x, int* y) {
    int kolumny = (screen_w - 40) / PULPIT_GRID_W;
    if (kolumny < 1) kolumny = 1;
    if (x) *x = 30 + (indeks % kolumny) * PULPIT_GRID_W;
    if (y) *y = PULPIT_GRID_Y + (indeks / kolumny) * PULPIT_GRID_H;
}

int wpis_pulpitu_w_punkcie(int x, int y) {
    for (int i = 0; i < liczba_wpisow_pulpitu; ++i) {
        int px = 0, py = 0;
        prostokat_wpisu_pulpitu(i, &px, &py);
        if (punkt_w_prostokacie(x, y, px, py, 68, 72)) return i;
    }
    return -1;
}

void podpis_wpisu(const char* nazwa, char* wynik, int pojemnosc) {
    if (!wynik || pojemnosc < 5) return;
    int src = 0, out = 0, znaki = 0;
    while (nazwa[src] && znaki < 9 && out + 4 < pojemnosc) {
        const uint8_t first = static_cast<uint8_t>(nazwa[src]);
        int bytes = first < 0x80U ? 1 : ((first & 0xE0U) == 0xC0U ? 2 :
                    ((first & 0xF0U) == 0xE0U ? 3 : 4));
        for (int b = 0; b < bytes && nazwa[src] && out + 1 < pojemnosc; ++b)
            wynik[out++] = nazwa[src++];
        ++znaki;
    }
    if (nazwa[src] && out + 4 <= pojemnosc) {
        wynik[out++] = '.'; wynik[out++] = '.'; wynik[out++] = '.';
    }
    wynik[out] = '\0';
}

bool podwojny_klik(uint64_t previous, uint64_t current) {
    if (!previous || current < previous) return false;
    const uint64_t difference = current - previous;
    return current >= UINT64_C(1000000000)
        ? difference <= UINT64_C(500000000) : difference <= 50;
}

bool tekst_zaczyna_sie(const char* tekst, const char* prefiks) {
    if (!tekst || !prefiks) return false;
    for (int i = 0; prefiks[i] != '\0'; ++i)
        if (tekst[i] != prefiks[i]) return false;
    return true;
}

uint64_t sygnatura_okien() {
    uint64_t h = liczba_okien_taskbara;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        const GuiOknoInfo& o = okna_taskbara[i];
        h ^= o.window_id + (h << 6U) + (h >> 2U);
        h ^= (static_cast<uint64_t>(o.stan) << 1U) | o.aktywne;
    }
    return h;
}

void odswiez_snapshot_okien() {
    liczba_okien_taskbara = gui_pobierz_okna(
        okna_taskbara, SKLADACZ_MAKS_WARSTW);
}

bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return false;

    const int64_t prawa = (int64_t)x + (int64_t)w;
    const int64_t dol = (int64_t)y + (int64_t)h;

    return (int64_t)px >= (int64_t)x &&
           (int64_t)py >= (int64_t)y &&
           (int64_t)px < prawa &&
           (int64_t)py < dol;
}

void wyzeruj_bufor(char* bufor, int rozmiar) {
    if (!bufor || rozmiar <= 0) return;
    for (int i = 0; i < rozmiar; i++) bufor[i] = '\0';
}

bool pobierz_i_zaktualizuj_czas() {
    char obecny_czas[BUFOR_CZASU];
    wyzeruj_bufor(obecny_czas, BUFOR_CZASU);

    // BWS 9 w obecnym ABI wypelnia bufor czasu przekazany przez Ring 3.
    bws_wywolaj(9, (uint64_t)obecny_czas);

    // Obrona na wypadek, gdy implementacja BWS 9 zapomni o terminatorze.
    obecny_czas[BUFOR_CZASU - 1] = '\0';

    bool zmieniony = false;
    for (int i = 0; i < BUFOR_CZASU; i++) {
        if (ostatni_czas[i] != obecny_czas[i]) zmieniony = true;
        ostatni_czas[i] = obecny_czas[i];
    }

    return zmieniony;
}

void RysujTaskbar() {
    gui_rysuj_prostokat(0, screen_h - PASEK_WYS,
                        screen_w, PASEK_WYS, 0x001A0B00);
    gui_rysuj_prostokat(0, screen_h - PASEK_WYS,
                        screen_w, 2, 0x00E58A00);
    gui_rysuj_prostokat(PRZYCISK_MENU_X, screen_h - 35,
                        PRZYCISK_MENU_W, PRZYCISK_MENU_H, 0x00E58A00);
    rysuj_tekst_wysrodkowany(PRZYCISK_MENU_X, screen_h - 35,
                             PRZYCISK_MENU_W, PRZYCISK_MENU_H,
                             1, 0x001A0B00, "Menu");
    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        const GuiOknoInfo& o = okna_taskbara[i];
        if (o.tytul[0] == '\0' || task_x + TASK_BUTTON_W > screen_w - 150)
            continue;
        const uint32_t kolor = o.aktywne ? 0x00E58A00 :
            (o.stan == GUI_OKNO_ZMINIMALIZOWANE ? 0x00452B00 : 0x008A5A00);
        gui_rysuj_prostokat(task_x, screen_h - 36,
                            TASK_BUTTON_W, SKROT_H, kolor);
        char podpis[15] = {};
        for (int c = 0; c < 14 && o.tytul[c] != '\0'; ++c)
            podpis[c] = o.tytul[c];
        rysuj_tekst_wysrodkowany(task_x, screen_h - 36,
                                 TASK_BUTTON_W, SKROT_H, 1,
                                 0x00FFFFFF, podpis);
        task_x += TASK_BUTTON_W + TASK_BUTTON_GAP;
    }
}

void zarejestruj_pulpit_jako_cel_drop() {
    for (uint32_t i = 0; i < BWS_DROP_CELE_MAX; ++i)
        cele_drop_pulpitu[i] = BwsCelDrop{};
    uint32_t count = 0;
    for (int i = 0; i < liczba_wpisow_pulpitu &&
         count + 1U < BWS_DROP_CELE_MAX; ++i) {
        if (!wpisy_pulpitu[i].folder) continue;
        int x = 0, y = 0;
        prostokat_wpisu_pulpitu(i, &x, &y);
        BwsCelDrop& cel = cele_drop_pulpitu[count];
        cel.x = x; cel.y = y; cel.szer = 68; cel.wys = 72;
        if (!sciezka_wpisu_pulpitu(i, cel.folder, sizeof(cel.folder))) continue;
        ++count;
    }
    BwsCelDrop& pulpit = cele_drop_pulpitu[count];
    pulpit.x = 0; pulpit.y = 0; pulpit.szer = screen_w;
    pulpit.wys = screen_h > PASEK_WYS ? screen_h - PASEK_WYS : 0;
    if (pulpit.wys > 0 && kopiuj_tekst(
            pulpit.folder, static_cast<int>(sizeof(pulpit.folder)), SCIEZKA_PULPIT))
        ++count;
    (void)gui_rejestruj_cele_drop(cele_drop_pulpitu, count);
}

void RysujWpisyPulpitu() {
    for (int i = 0; i < liczba_wpisow_pulpitu; ++i) {
        int x = 0, y = 0;
        prostokat_wpisu_pulpitu(i, &x, &y);
        if (y + 72 >= screen_h - PASEK_WYS) break;
        if (i == zaznaczony_wpis_pulpitu)
            gui_rysuj_prostokat(x, y, 68, 72, 0x00603800);
        uint32_t kolor = wpisy_pulpitu[i].folder ? 0x00FFBF00 : 0x00708090;
        if (konczy_sie(wpisy_pulpitu[i].nazwa, ".bur")) kolor = 0x0038B000;
        else if (konczy_sie(wpisy_pulpitu[i].nazwa, ".cebula")) kolor = 0x00D050A0;
        else if (konczy_sie(wpisy_pulpitu[i].nazwa, ".txt")) kolor = 0x004C9BE8;
        gui_rysuj_prostokat(x + 14, y + 4, 40, 40, kolor);
        if (wpisy_pulpitu[i].folder)
            gui_rysuj_prostokat(x + 18, y, 18, 8, kolor);
        char podpis[40] = {};
        podpis_wpisu(wpisy_pulpitu[i].nazwa, podpis, sizeof(podpis));
        rysuj_tekst_wysrodkowany(x, y + 49, 68, 18, 1, 0x00FFFFFF, podpis);
    }
}

int menu_glowne_y() {
    int y = screen_h - PASEK_WYS - MENU_WYS;
    if (y < 0) y = 0;
    return y;
}

int menu_gry_x() {
    // Preferuj podmenu po prawej stronie. Na bardzo malej rozdzielczosci
    // sproboj pokazac je po lewej stronie glownego menu.
    const int po_prawej = MENU_X + MENU_W;
    if (po_prawej + MENU_GRY_W <= screen_w) return po_prawej;

    const int po_lewej = MENU_X - MENU_GRY_W;
    if (po_lewej >= 0) return po_lewej;
    return 0;
}

int menu_gry_y() {
    int y = menu_glowne_y() + MENU_GORNY_MARGINES +
            MENU_GRY_INDEX * MENU_ELEMENT_WYS;

    const int dol_pulpitu = screen_h - PASEK_WYS;
    if (y + MENU_GRY_WYS > dol_pulpitu)
        y = dol_pulpitu - MENU_GRY_WYS;
    if (y < 0) y = 0;
    return y;
}

void aktualizuj_overlay_menu() {
    if (!menu_start_otwarte) {
        gui_ustaw_system_overlay(false, 0, 0, 0, 0);
        return;
    }

    int lewo = MENU_X;
    int gora = menu_glowne_y();
    int prawo = MENU_X + MENU_W;
    int dol = gora + MENU_WYS;

    if (menu_gry_otwarte) {
        const int gry_x = menu_gry_x();
        const int gry_y = menu_gry_y();
        if (gry_x < lewo) lewo = gry_x;
        if (gry_y < gora) gora = gry_y;
        if (gry_x + MENU_GRY_W > prawo) prawo = gry_x + MENU_GRY_W;
        if (gry_y + MENU_GRY_WYS > dol) dol = gry_y + MENU_GRY_WYS;
    }

    gui_ustaw_system_overlay(true, lewo, gora, prawo - lewo, dol - gora);
}

void RysujMenuGry() {
    if (!menu_start_otwarte || !menu_gry_otwarte) return;

    const int x = menu_gry_x();
    const int y = menu_gry_y();

    gui_rysuj_prostokat(x, y, MENU_GRY_W, MENU_GRY_WYS, 0x00301500);
    gui_rysuj_prostokat(x, y, MENU_GRY_W, 1, 0x00E58A00);
    gui_rysuj_prostokat(x, y, 1, MENU_GRY_WYS, 0x00E58A00);
    gui_rysuj_prostokat(x + MENU_GRY_W - 1, y,
                        1, MENU_GRY_WYS, 0x00794400);
    gui_rysuj_prostokat(x, y + MENU_GRY_WYS - 1,
                        MENU_GRY_W, 1, 0x00794400);

    const char* gry[MENU_GRY_ELEMENTOW] = {
        "> Snake"
    };

    for (int i = 0; i < MENU_GRY_ELEMENTOW; ++i) {
        const int item_y = y + MENU_GORNY_MARGINES +
                           i * MENU_ELEMENT_WYS;
        gui_wypisz_tekst_kolor(x + 10, item_y + 5,
                               0x00FFFFFF, gry[i]);
    }
}

void RysujPulpit(bool wymus_pelne_odswiezenie) {
    if (wymus_pelne_odswiezenie) {
        // Dla procesu warstwowego BWS 19 zeruje prywatna warstwe.
        gui_odswiez_pulpit();
    }

    RysujTaskbar();

    // Ikona Notatnika.
    gui_rysuj_prostokat(50, 50, IKONA_W, IKONA_H, 0x00FFBF00);
    gui_rysuj_prostokat(52, 52, 44, 44, 0x00FFFFFF);
    gui_rysuj_prostokat(56, 58, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 64, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 70, 20, 2, 0x00000000);
    gui_rysuj_prostokat(56, 76, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 82, 24, 2, 0x00000000);
    rysuj_tekst_wysrodkowany(50, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Notatnik");

    // Ikona Kalkulatora.
    gui_rysuj_prostokat(130, 50, IKONA_W, IKONA_H, 0x008A5A00);
    gui_rysuj_prostokat(132, 52, 44, 44, 0x001A0B00);
    rysuj_tekst_wysrodkowany(130, 60, IKONA_W, 16,
                             1, 0x00FFBF00, "+ -");
    rysuj_tekst_wysrodkowany(130, 80, IKONA_W, 16,
                             1, 0x00FFBF00, "* =");
    rysuj_tekst_wysrodkowany(130, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Kalkulator");

    // Ikona przegladarki Hussar.
    gui_rysuj_prostokat(210, 50, IKONA_W, IKONA_H, 0x000078D7);
    gui_rysuj_prostokat(212, 52, 44, 44, 0x000055AA);
    rysuj_tekst_wysrodkowany(210, 65, IKONA_W, 16,
                             1, 0x00FFFFFF, "WWW");
    rysuj_tekst_wysrodkowany(210, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Husarz");
    //Test apka
    gui_rysuj_prostokat(290, 50, IKONA_W, IKONA_H, 0x00CC33CC);
    gui_rysuj_prostokat(292, 52, 44, 44, 0x00881188);
    rysuj_tekst_wysrodkowany(290, 65, IKONA_W, 16,
                             1, 0x00FFFFFF, "Test");
    rysuj_tekst_wysrodkowany(290, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Test");
    // Ikona Snake — lokalny format paleta + fragmenty w .data.
    // rysuj_ikone_paleta(
    //     370,
    //     50,
    //     IKONA_SNAKE_PALETA,
    //     IKONA_SNAKE_KOLOROW,
    //     IKONA_SNAKE_FRAGMENTY,
    //     IKONA_SNAKE_FRAGMENTOW);

    // rysuj_tekst_wysrodkowany(
    //     370,
    //     104,
    //     IKONA_W,
    //     16,
    //     1,
    //     0x00FFFFFF,
    //     "Snake");

// Ikona Snake — lokalny format paleta + fragmenty w .data.
rysuj_ikone_paleta(
    370,
    50,
    IKONA_SNAKE_PALETA,
    IKONA_SNAKE_KOLOROW,
    IKONA_SNAKE_FRAGMENTY,
    IKONA_SNAKE_FRAGMENTOW);

rysuj_tekst_wysrodkowany(370, 104, IKONA_W, 16, 1, 0x0095E600, "Snake");


    // Ikona Eksploratora Plikow.
    gui_rysuj_prostokat(460, 50, IKONA_W, IKONA_H, 0x00FFBF00);
    gui_rysuj_prostokat(462, 58, 44, 38, 0x00D59600);
    gui_rysuj_prostokat(466, 52, 20, 10, 0x00FFBF00);
    rysuj_tekst_wysrodkowany(460, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Eksplorator");

    // Ikona Przeglądarki Obrazów.
    gui_rysuj_prostokat(550, 50, IKONA_W, IKONA_H, 0x00301500);
    gui_rysuj_prostokat(552, 52, 44, 44, 0x002080C0);
    gui_rysuj_prostokat(558, 74, 18, 12, 0x00808080);
    gui_rysuj_prostokat(572, 78, 14, 8, 0x00606060);
    gui_rysuj_prostokat(580, 58, 8, 8, 0x00FFD700);
    rysuj_tekst_wysrodkowany(550, 104, IKONA_W, 16, 1, 0x00FFFFFF, "Przeglądarka");
    rysuj_tekst_wysrodkowany(550, 120, IKONA_W, 16, 1, 0x00FFFFFF, "Obrazów");

    RysujWpisyPulpitu();


    if (menu_start_otwarte) {
        const int menu_y = menu_glowne_y();

        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, MENU_WYS, 0x00301500);
        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, 1, 0x00E58A00);
        gui_rysuj_prostokat(MENU_X, menu_y, 1, MENU_WYS, 0x00E58A00);
        gui_rysuj_prostokat(MENU_X + MENU_W - 1, menu_y,
                            1, MENU_WYS, 0x00794400);
        gui_rysuj_prostokat(MENU_X, menu_y + MENU_WYS - 1,
                            MENU_W, 1, 0x00794400);

        const char* menu_elementy[MENU_ELEMENTOW] = {
            "> Powloka Bursztyna",
            "> Eksplorator Plików",
            "> Notatnik",
            "> Kalkulator",
            "> Przegladarka Husarz",
            "> Przeglądarka Obrazów",
            "> Test",
            "> Gry ->",
            "> Uruchom ponownie",
            "> Zamknij"
        };

        for (int i = 0; i < MENU_ELEMENTOW; ++i) {
            const int item_y = menu_y + MENU_GORNY_MARGINES +
                               i * MENU_ELEMENT_WYS;

            if (i == MENU_GRY_INDEX && menu_gry_otwarte) {
                gui_rysuj_prostokat(MENU_X + 2, item_y,
                                    MENU_W - 4, MENU_ELEMENT_WYS,
                                    0x00603800);
            }

            gui_wypisz_tekst_kolor(MENU_X + 10, item_y + 5,
                                   0x00FFFFFF, menu_elementy[i]);
        }

        RysujMenuGry();
    }

    // BWS 17 sklada warstwy i prezentuje gotowa klatke.
    zarejestruj_pulpit_jako_cel_drop();
    gui_odswiez();
}

void zamknij_menu_i_odtworz_pulpit() {
    if (!menu_start_otwarte && !menu_gry_otwarte) return;

    menu_start_otwarte = false;
    menu_gry_otwarte = false;
    aktualizuj_overlay_menu();

    // Pelne odswiezenie usuwa piksele glownego menu i jego podmenu.
    RysujPulpit(true);
}

bool aktywuj_istniejace(const char* prefiks_tytulu);

void uruchom_program_gui(const char* sciezka) {
    if (!sciezka) return;

    zamknij_menu_i_odtworz_pulpit();

    // Nie zerujemy globalnego trybu Ring 3 przed aplikacja GUI. Obecna
    // implementacja w grafika.cpp uzywa jednej globalnej flagi, a aplikacje
    // GUI i tak ustawiaja ten tryb na true po starcie.
    gui_ustaw_przejecie_myszy(true);
    bws_wywolaj(10, (uint64_t)sciezka);
}

void otworz_wpis_pulpitu(int indeks) {
    char sciezka[512] = {};
    if (!sciezka_wpisu_pulpitu(indeks, sciezka, sizeof(sciezka))) return;
    if (wpisy_pulpitu[indeks].folder || konczy_sie(sciezka, ".cebula")) {
        zamknij_menu_i_odtworz_pulpit();
        gui_ustaw_przejecie_myszy(true);
        (void)uruchom_program_z_argumentem_uzytkownika(
            SCIEZKA_EKSPLORATOR, sciezka);
        (void)aktywuj_istniejace("Eksplorator Plików");
        return;
    }
    if (konczy_sie(sciezka, ".bur")) {
        (void)uruchom_program_uzytkownika(sciezka);
        return;
    }
    (void)otworz_plik_skojarzony(sciezka);
}

void dodaj_pulpit_context(const char* label, PulpitContextAction action) {
    if (pulpit_context_count >= 8) return;
    pulpit_context_items[pulpit_context_count++] = {label, action};
}

int pulpit_context_h() { return pulpit_context_count * 24 + 4; }

void rysuj_pulpit_context_row(int index) {
    if (index < 0 || index >= pulpit_context_count) return;
    const int y = pulpit_context_y + 2 + index * 24;
    gui_rysuj_prostokat(pulpit_context_x + 2, y,
                        pulpit_context_w - 4, 24,
                        index == pulpit_context_hover ? 0x00603800 : 0x00301500);
    gui_wypisz_tekst_kolor(pulpit_context_x + 10, y + 5,
                           0x00FFFFFF, pulpit_context_items[index].label);
}

void zamknij_pulpit_context() {
    if (!pulpit_context_open) return;
    (void)gui_ustaw_popup_aplikacji(false, 0, 0, 0, 0);
    pulpit_context_open = false;
    pulpit_context_count = 0;
    pulpit_context_hover = -1;
    zarejestruj_pulpit_jako_cel_drop();
    gui_odswiez();
}

bool otworz_pulpit_context(int x, int y, int wpis) {
    zamknij_pulpit_context();
    pulpit_context_count = 0;
    if (wpis >= 0 && wpis < liczba_wpisow_pulpitu) {
        dodaj_pulpit_context("Otwórz", PulpitContextAction::OPEN);
        if (konczy_sie(wpisy_pulpitu[wpis].nazwa, ".bur"))
            dodaj_pulpit_context("Uruchom", PulpitContextAction::RUN);
        dodaj_pulpit_context("Kopiuj", PulpitContextAction::COPY);
        dodaj_pulpit_context("Wytnij", PulpitContextAction::CUT);
    } else {
        dodaj_pulpit_context("Nowy plik", PulpitContextAction::NOWY_PLIK);
        dodaj_pulpit_context("Nowy folder", PulpitContextAction::NOWY_FOLDER);
        dodaj_pulpit_context("Wklej", PulpitContextAction::PASTE);
        dodaj_pulpit_context("Odśwież", PulpitContextAction::REFRESH);
    }
    const int height = pulpit_context_h();
    pulpit_context_x = x;
    pulpit_context_y = y;
    if (pulpit_context_x + pulpit_context_w > screen_w)
        pulpit_context_x = screen_w - pulpit_context_w;
    if (pulpit_context_y + height > screen_h - PASEK_WYS)
        pulpit_context_y = screen_h - PASEK_WYS - height;
    if (pulpit_context_x < 0) pulpit_context_x = 0;
    if (pulpit_context_y < 0) pulpit_context_y = 0;
    pulpit_context_hover = -1;
    if (!gui_ustaw_popup_aplikacji(true, pulpit_context_x, pulpit_context_y,
                                   pulpit_context_w, height)) return false;
    pulpit_context_open = true;
    (void)gui_rejestruj_cele_drop(nullptr, 0);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y,
                        pulpit_context_w, height, 0x00301500);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y,
                        pulpit_context_w, 2, 0x00E58A00);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y,
                        2, height, 0x00E58A00);
    gui_rysuj_prostokat(pulpit_context_x + pulpit_context_w - 2,
                        pulpit_context_y, 2, height, 0x00794400);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y + height - 2,
                        pulpit_context_w, 2, 0x00794400);
    for (int i = 0; i < pulpit_context_count; ++i) rysuj_pulpit_context_row(i);
    gui_odswiez();
    return true;
}

PulpitContextAction pulpit_context_action(int x, int y) {
    if (!pulpit_context_open || !punkt_w_prostokacie(
            x, y, pulpit_context_x, pulpit_context_y,
            pulpit_context_w, pulpit_context_h())) return PulpitContextAction::NONE;
    const int index = (y - pulpit_context_y - 2) / 24;
    return index >= 0 && index < pulpit_context_count
        ? pulpit_context_items[index].action : PulpitContextAction::NONE;
}

void wykonaj_pulpit_context(PulpitContextAction action) {
    const int selected = zaznaczony_wpis_pulpitu;
    char source[512] = {};
    if (selected >= 0) (void)sciezka_wpisu_pulpitu(
        selected, source, static_cast<int>(sizeof(source)));
    zamknij_pulpit_context();
    if (action == PulpitContextAction::NOWY_PLIK) {
        (void)otworz_pulpit_modal(PulpitModalTyp::PLIK);
        return;
    }
    if (action == PulpitContextAction::NOWY_FOLDER) {
        (void)otworz_pulpit_modal(PulpitModalTyp::FOLDER);
        return;
    }
    if (action == PulpitContextAction::OPEN) otworz_wpis_pulpitu(selected);
    else if (action == PulpitContextAction::RUN && source[0])
        (void)uruchom_program_uzytkownika(source);
    else if (action == PulpitContextAction::COPY && source[0])
        (void)ustaw_schowek_plikow(source, BWS_SCHOWEK_COPY);
    else if (action == PulpitContextAction::CUT && source[0])
        (void)ustaw_schowek_plikow(source, BWS_SCHOWEK_CUT);
    else if (action == PulpitContextAction::PASTE) {
        BwsSchowekPlikow clipboard{};
        if (pobierz_schowek_plikow(&clipboard) &&
            clipboard.wersja == BWS_SCHOWEK_WERSJA) {
            bool ok = false;
            if (clipboard.operacja == BWS_SCHOWEK_COPY)
                ok = kopiuj_twor_uzytkownika(clipboard.sciezka, SCIEZKA_PULPIT);
            else if (clipboard.operacja == BWS_SCHOWEK_CUT) {
                ok = przenies_twor_uzytkownika(clipboard.sciezka, SCIEZKA_PULPIT);
                if (ok) (void)wyczysc_schowek_plikow(clipboard.generacja);
            }
        }
    }
    if (action == PulpitContextAction::PASTE ||
        action == PulpitContextAction::REFRESH) {
        hash_pulpitu_wazny = false;
        (void)odswiez_wpisy_pulpitu();
        RysujPulpit(true);
    } else {
        gui_odswiez();
    }
}

bool aktywuj_istniejace(const char* prefiks_tytulu) {
    odswiez_snapshot_okien();
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        if (tekst_zaczyna_sie(okna_taskbara[i].tytul, prefiks_tytulu)) {
            zamknij_menu_i_odtworz_pulpit();
            return gui_aktywuj_okno(okna_taskbara[i].window_id);
        }
    }
    return false;
}

void uruchom_lub_aktywuj(const char* sciezka, const char* prefiks_tytulu) {
    if (!aktywuj_istniejace(prefiks_tytulu)) uruchom_program_gui(sciezka);
}

void uruchom_powloke() {
    uruchom_lub_aktywuj(SCIEZKA_POWLOKA, "Powloka Bursztynowa");
}

void wykonaj_restart() {
    bws_wywolaj(25);
    while (true) asm volatile("pause");
}

void wykonaj_zamkniecie() {
    bws_wywolaj(26);
    while (true) asm volatile("pause");
}

void obsluz_element_menu(int item_index) {
    switch (item_index) {
        case 0:
            uruchom_powloke();
            break;
        case 1:
            uruchom_lub_aktywuj(SCIEZKA_EKSPLORATOR,
                                "Eksplorator Plików");
            break;
        case 2:
            uruchom_lub_aktywuj(SCIEZKA_NOTATNIK, "Notatnik");
            break;
        case 3:
            uruchom_lub_aktywuj(SCIEZKA_KALKULATOR, "Kalkulator");
            break;
        case 4:
            uruchom_lub_aktywuj(SCIEZKA_HUSSAR, "Husarz");
            break;
        case 5:
            uruchom_lub_aktywuj(SCIEZKA_PRZEGLADARKA_OBRAZOW,
                                "Przeglądarka Obrazów");
            break;
        case 6:
            uruchom_lub_aktywuj(SCIEZKA_TEST, "Test");
            break;
        case MENU_GRY_INDEX:
            menu_gry_otwarte = !menu_gry_otwarte;
            aktualizuj_overlay_menu();
            RysujPulpit(true);
            break;
        case 8:
            wykonaj_restart();
            break;
        case 9:
            wykonaj_zamkniecie();
            break;
        default:
            break;
    }
}

void obsluz_element_menu_gry(int item_index) {
    switch (item_index) {
        case 0:
            uruchom_lub_aktywuj(SCIEZKA_SNAKE, "Snake");
            break;
        default:
            break;
    }
}

void obsluz_klikniecie(int mx, int my) {
    const int menu_y = menu_glowne_y();
    const int lista_y = menu_y + MENU_GORNY_MARGINES;
    const int lista_h = MENU_ELEMENTOW * MENU_ELEMENT_WYS;

    // Przycisk Menu ma pierwszenstwo niezaleznie od aktualnego stanu menu.
    if (punkt_w_prostokacie(mx, my,
                            PRZYCISK_MENU_X, screen_h - 35,
                            PRZYCISK_MENU_W, PRZYCISK_MENU_H)) {
        if (menu_start_otwarte) {
            zamknij_menu_i_odtworz_pulpit();
        } else {
            menu_start_otwarte = true;
            menu_gry_otwarte = false;
            aktualizuj_overlay_menu();
            RysujPulpit(false);
        }
        return;
    }

    if (menu_start_otwarte) {
        // Najpierw podmenu Gry, bo lezy poza prostokatem glownej listy.
        if (menu_gry_otwarte) {
            const int gry_x = menu_gry_x();
            const int gry_y = menu_gry_y();
            const int gry_lista_y = gry_y + MENU_GORNY_MARGINES;
            const int gry_lista_h = MENU_GRY_ELEMENTOW * MENU_ELEMENT_WYS;

            if (punkt_w_prostokacie(mx, my,
                                    gry_x, gry_lista_y,
                                    MENU_GRY_W, gry_lista_h)) {
                const int item_index =
                    (my - gry_lista_y) / MENU_ELEMENT_WYS;
                if (item_index >= 0 && item_index < MENU_GRY_ELEMENTOW)
                    obsluz_element_menu_gry(item_index);
                return;
            }

            // Klik w ramke/margines podmenu nie powinien przypadkowo
            // uruchomic elementu glownego menu ani zamknac menu.
            if (punkt_w_prostokacie(mx, my,
                                    gry_x, gry_y,
                                    MENU_GRY_W, MENU_GRY_WYS))
                return;
        }

        // Klikalne sa tylko rzeczywiste wiersze listy. Gorny margines nie
        // moze zostac zinterpretowany jako element numer 0.
        if (punkt_w_prostokacie(mx, my,
                                MENU_X, lista_y,
                                MENU_W, lista_h)) {
            const int item_index = (my - lista_y) / MENU_ELEMENT_WYS;
            if (item_index >= 0 && item_index < MENU_ELEMENTOW)
                obsluz_element_menu(item_index);
            return;
        }

        // Klik w margines glownego menu pozostawia menu otwarte.
        if (punkt_w_prostokacie(mx, my,
                                MENU_X, menu_y, MENU_W, MENU_WYS))
            return;

        // Klik poza glownym menu i podmenu zamyka calosc.
        zamknij_menu_i_odtworz_pulpit();
        return;
    }

    // Ikony na pulpicie.
    if (punkt_w_prostokacie(mx, my, 50, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_NOTATNIK, "Notatnik");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 130, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_KALKULATOR, "Kalkulator");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 210, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_HUSSAR, "Husarz");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 290, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_TEST, "Test");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 370, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_SNAKE, "Snake");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 460, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_EKSPLORATOR, "Eksplorator Plików");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 550, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_PRZEGLADARKA_OBRAZOW, "Przeglądarka Obrazów");
        return;
    }

    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        if (okna_taskbara[i].tytul[0] == '\0' ||
            task_x + TASK_BUTTON_W > screen_w - 150) continue;
        if (punkt_w_prostokacie(mx, my, task_x, screen_h - 36,
                                TASK_BUTTON_W, SKROT_H)) {
            (void)gui_aktywuj_okno(okna_taskbara[i].window_id);
            return;
        }
        task_x += TASK_BUTTON_W + TASK_BUTTON_GAP;
    }
}

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);

    if (screen_w <= 0 || screen_h <= PASEK_WYS) {
        gui_zakoncz_aplikacje();
    }

    // Pulpit jest warstwa tla. Aplikacje GUI uzywaja wyzszego z_order.
    if (bws_utworz_warstwe(0, 0, screen_w, screen_h, 0) < 0) {
        gui_zakoncz_aplikacje();
    }

    gui_ustaw_przejecie_myszy(true);

    // Zasiej stan zegara, aby pierwsza iteracja nie wymuszala od razu
    // dodatkowej, niepotrzebnej kompozycji calej klatki.
    (void)pobierz_i_zaktualizuj_czas();
    odswiez_snapshot_okien();
    (void)odswiez_wpisy_pulpitu();

    RysujPulpit(true);

    bool drag_candidate = false;
    bool dragging_file = false;
    int drag_index = -1;
    int drag_start_x = 0;
    int drag_start_y = 0;
    char drag_path[512] = {};
    int last_desktop_click = -1;
    uint64_t last_desktop_click_time = 0;

    while (true) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        const uint64_t stara_sygnatura = sygnatura_okien();
        odswiez_snapshot_okien();
        const bool lifecycle =
            zdarzenie.typ >= BWS_ZDARZENIE_OKNO_UTWORZONE &&
            zdarzenie.typ <= BWS_ZDARZENIE_OKNO_TYTUL;
        if (lifecycle || sygnatura_okien() != stara_sygnatura) {
            RysujTaskbar();
            gui_odswiez();
        }
        const int mx = zdarzenie.x;
        const int my = zdarzenie.y;
        const bool klik = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;

        if (zdarzenie.typ == BWS_ZDARZENIE_BLUR) {
            zamknij_pulpit_context();
            zamknij_pulpit_modal();
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_PLIKI_ZMIENIONE) {
            zamknij_pulpit_context();
            /* Pelne przerysowanie warstwy nadpisaloby piksele otwartego
               dialogu; odswiezamy dopiero po jego zamknieciu. */
            if (!gui_modal_otwarty(&pulpit_modal) && odswiez_wpisy_pulpitu())
                RysujPulpit(true);
            continue;
        }

        if (gui_modal_otwarty(&pulpit_modal)) {
            if (zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ) {
                const char znak = static_cast<char>(zdarzenie.kod);
                if (pulpit_ansi == PulpitAnsi::ESC) {
                    pulpit_ansi = znak == '[' ? PulpitAnsi::CSI
                                              : PulpitAnsi::NONE;
                    continue;
                }
                if (pulpit_ansi == PulpitAnsi::CSI) {
                    pulpit_ansi = PulpitAnsi::NONE;
                    int kierunek = 0;
                    if (znak == 'D') kierunek = -1;
                    else if (znak == 'C') kierunek = 1;
                    else if (znak == '3') pulpit_ansi = PulpitAnsi::CSI_DELETE;
                    if (kierunek &&
                        gui_modal_przesun_karetke(&pulpit_modal, kierunek)) {
                        gui_modal_rysuj(&pulpit_modal);
                        gui_odswiez();
                    }
                    continue;
                }
                if (pulpit_ansi == PulpitAnsi::CSI_DELETE) {
                    pulpit_ansi = PulpitAnsi::NONE;
                    if (znak == '~' && gui_modal_usun_przed(&pulpit_modal)) {
                        gui_modal_rysuj(&pulpit_modal);
                        gui_odswiez();
                    }
                    continue;
                }
                if (znak == '\x1B') {
                    pulpit_ansi = PulpitAnsi::ESC;
                    continue;
                }
                const gui_modal_wynik wynik =
                    gui_modal_klawisz(&pulpit_modal, znak);
                if (wynik == GUI_MODAL_OK) zatwierdz_pulpit_modal();
                else if (wynik == GUI_MODAL_ANULUJ) zamknij_pulpit_modal();
                else {
                    gui_modal_rysuj(&pulpit_modal);
                    gui_odswiez();
                }
                continue;
            }
            if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN ||
                zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) {
                if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) continue;
                const gui_modal_wynik wynik =
                    gui_modal_klik(&pulpit_modal, mx, my);
                if (wynik == GUI_MODAL_OK) zatwierdz_pulpit_modal();
                else if (wynik == GUI_MODAL_ANULUJ) zamknij_pulpit_modal();
                else if (gui_modal_pole_trafione(&pulpit_modal, mx, my)) {
                    gui_modal_karetka_za_mysza(&pulpit_modal, mx);
                    gui_modal_rysuj(&pulpit_modal);
                    gui_odswiez();
                }
                continue;
            }
            if (zdarzenie.typ == BWS_ZDARZENIE_TIMER)
                zarejestruj_pulpit_jako_cel_drop();
            continue;
        }

        if (pulpit_context_open) {
            if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH) {
                int next = -1;
                if (punkt_w_prostokacie(mx, my, pulpit_context_x,
                                        pulpit_context_y, pulpit_context_w,
                                        pulpit_context_h())) {
                    next = (my - pulpit_context_y - 2) / 24;
                    if (next < 0 || next >= pulpit_context_count) next = -1;
                }
                if (next != pulpit_context_hover) {
                    const int old = pulpit_context_hover;
                    pulpit_context_hover = next;
                    if (old >= 0) rysuj_pulpit_context_row(old);
                    if (next >= 0) rysuj_pulpit_context_row(next);
                    gui_odswiez();
                }
                continue;
            }
            if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
                const PulpitContextAction action = pulpit_context_action(mx, my);
                if (action == PulpitContextAction::NONE) zamknij_pulpit_context();
                else wykonaj_pulpit_context(action);
                continue;
            }
            if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN ||
                (zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ &&
                 static_cast<char>(zdarzenie.kod) == '\x1B')) {
                zamknij_pulpit_context();
                continue;
            }
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) {
            if (menu_start_otwarte) zamknij_menu_i_odtworz_pulpit();
            drag_candidate = false;
            dragging_file = false;
            gui_ustaw_capture_myszy(false);
            const int desktop_entry = wpis_pulpitu_w_punkcie(mx, my);
            zaznaczony_wpis_pulpitu = desktop_entry;
            RysujPulpit(true);
            if (my < screen_h - PASEK_WYS)
                (void)otworz_pulpit_context(mx, my, desktop_entry);
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_DRAG_HOVER) {
            /* Wizualny stan drag jest osobnym, paced overlayem skladacza.
             * Pulpit nie rysuje juz pelnoekranowej ramki nad taskbarem. */
            pulpit_drop_hover = true;
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_DRAG_LEAVE) {
            pulpit_drop_hover = false;
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_DRAG_DROP) {
            pulpit_drop_hover = false;
            hash_pulpitu_wazny = false;
            (void)odswiez_wpisy_pulpitu();
            RysujPulpit(true);
            continue;
        }

        const bool left_button = (zdarzenie.przyciski & 1U) != 0;
        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH &&
            drag_candidate && left_button) {
            const int dx = mx - drag_start_x;
            const int dy = my - drag_start_y;
            if (!dragging_file && (dx > 4 || dx < -4 || dy > 4 || dy < -4))
                dragging_file = true;
            if (dragging_file)
                (void)gui_aktualizuj_drag(drag_path, mx, my, false);
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP && drag_candidate) {
            if (dragging_file) {
                const BwsWynikDrop result = gui_aktualizuj_drag(
                    drag_path, mx, my, true);
                if (result == BWS_DROP_PRZENIESIONO) {
                    hash_pulpitu_wazny = false;
                    (void)odswiez_wpisy_pulpitu();
                    zaznaczony_wpis_pulpitu = -1;
                    RysujPulpit(true);
                }
            }
            drag_candidate = false;
            dragging_file = false;
            drag_index = -1;
            drag_path[0] = '\0';
            gui_ustaw_capture_myszy(false);
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_TIMER) {
            zarejestruj_pulpit_jako_cel_drop();
            if (!pulpit_context_open && !gui_modal_otwarty(&pulpit_modal) &&
                ++pulpit_refresh_ticks >= PULPIT_REFRESH_TICKS) {
                pulpit_refresh_ticks = 0;
                if (odswiez_wpisy_pulpitu()) RysujPulpit(true);
            }
        }

        // Zegar jest nakladka jadra rysowana przez skladacz obrazu. Gdy czas
        // sie zmieni, wystarczy zlozyc nowa klatke; nie trzeba ponownie
        // rysowac wszystkich ikon i paska do warstwy pulpitu.
        if (pobierz_i_zaktualizuj_czas()) {
            gui_odswiez();
        }

        if (klik) {
            const int desktop_entry = wpis_pulpitu_w_punkcie(mx, my);
            if (desktop_entry >= 0) {
                const bool double_click = desktop_entry == last_desktop_click &&
                    podwojny_klik(last_desktop_click_time, zdarzenie.timestamp);
                zaznaczony_wpis_pulpitu = desktop_entry;
                RysujPulpit(true);
                last_desktop_click = desktop_entry;
                last_desktop_click_time = zdarzenie.timestamp;
                drag_index = desktop_entry;
                drag_candidate = sciezka_wpisu_pulpitu(
                    drag_index, drag_path, sizeof(drag_path));
                dragging_file = false;
                drag_start_x = mx;
                drag_start_y = my;
                if (drag_candidate) gui_ustaw_capture_myszy(true);
                if (double_click) {
                    gui_ustaw_capture_myszy(false);
                    drag_candidate = false;
                    otworz_wpis_pulpitu(desktop_entry);
                    last_desktop_click = -1;
                    last_desktop_click_time = 0;
                }
                continue;
            }
            last_desktop_click = -1;
            last_desktop_click_time = 0;
            obsluz_klikniecie(mx, my);
        }

    }
}
