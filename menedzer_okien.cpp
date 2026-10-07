#include "ui_skala.h"
#include "ekran_format.h"
#include "pulpit_geometria.h"
#include "desktop_layout.h"
#include "desktop_entries.h"
#include "desktop_layout_settings.h"
#include "obrazy/png_dekoder.h"
/*
 * Menedzer Okien (Pulpit i Pasek Zadan) dla Bursztyn OS
 * Wersja uporzadkowana: poprawne hitboxy, Menu Start i odswiezanie zegara.
 */

#include "bursztyn_gui.h"
#include "plik_popup_ui.h"

#ifndef BURSZTYN_DIAG_BOOT
#define BURSZTYN_DIAG_BOOT 0
#endif

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
        4096, 262144, 0x1001000,
        266240, 131072, 0x1041000
    };
}

namespace {

int screen_w = 1024;
int screen_h = 768;
int PASEK_WYS = 40;
int MENU_X = 10;
int MENU_W = 260;
int MENU_GORNY_MARGINES = 10;
int MENU_ELEMENT_WYS = 25;
constexpr int MENU_ELEMENTOW = 18;
int MENU_WYS = (10 * 2) + (MENU_ELEMENTOW * 25);

// Pozycja "Gry ->" w glownym Menu Start.
constexpr int MENU_GRY_INDEX = 15;
int MENU_GRY_W = 180;
constexpr int MENU_GRY_ELEMENTOW = 1;
int MENU_GRY_WYS = (10 * 2) + (MENU_GRY_ELEMENTOW * 25);

constexpr int LOGO_LOGICZNA_SZER = 24;
constexpr int LOGO_LOGICZNA_WYS = 26;
constexpr int PRZYCISK_MENU_LOGICZNA_SZER = 48;
constexpr int PRZYCISK_MENU_LOGICZNA_WYS = 30;
constexpr int TASKBAR_LOGICZNA_WYS = 40;

int PRZYCISK_MENU_X = 10;
int PRZYCISK_MENU_W = 48;
int PRZYCISK_MENU_H = 30;

int PULPIT_LINE_H = 16;
int PULPIT_LINE_GAP = 2;
int SKROT_W = 32;
int SKROT_H = 32;

constexpr int BUFOR_CZASU = 32;
int TASK_BUTTON_X = 66;
int TASK_BUTTON_W = 112;
int TASK_BUTTON_GAP = 4;
constexpr int PULPIT_MAX_WPISOW = 128+DesktopEntries::Count;
constexpr int PULPIT_LIST_CAP = 16384;
constexpr int PULPIT_NAZWA_CAP = BWS_DROP_SCIEZKA_MAX; // display capacity; PSF validates mutations
int PULPIT_GRID_W = 120;
int PULPIT_GRID_H = 100;
int PULPIT_GRID_Y = 3;
constexpr int PULPIT_REFRESH_TICKS = 3;


void metryki_ekranu(){
PASEK_WYS=ui_skaluj(TASKBAR_LOGICZNA_WYS,ui_skala_procent);
MENU_X=ui_skaluj(10,ui_skala_procent);
MENU_W=ui_skaluj(260,ui_skala_procent);
MENU_GORNY_MARGINES=ui_skaluj(10,ui_skala_procent);
MENU_ELEMENT_WYS=ui_skaluj(25,ui_skala_procent);
MENU_GRY_W=ui_skaluj(180,ui_skala_procent);
PRZYCISK_MENU_X=ui_skaluj(10,ui_skala_procent);
PRZYCISK_MENU_W=ui_skaluj(PRZYCISK_MENU_LOGICZNA_SZER,ui_skala_procent);
PRZYCISK_MENU_H=ui_skaluj(PRZYCISK_MENU_LOGICZNA_WYS,ui_skala_procent);
const PulpitMetryki pulpit=pulpit_metryki(ui_skala_procent);
PULPIT_LINE_H=pulpit.wysokosc_linii;
PULPIT_LINE_GAP=pulpit.odstep_linii;
SKROT_W=ui_skaluj(32,ui_skala_procent);
SKROT_H=ui_skaluj(32,ui_skala_procent);
TASK_BUTTON_X=ui_skaluj(66,ui_skala_procent);
TASK_BUTTON_W=ui_skaluj(112,ui_skala_procent);
TASK_BUTTON_GAP=ui_skaluj(4,ui_skala_procent);
PULPIT_GRID_W=pulpit.komorka_szer;
PULPIT_GRID_H=pulpit.komorka_wys;
PULPIT_GRID_Y=ui_skaluj(140,ui_skala_procent);
MENU_WYS=2*MENU_GORNY_MARGINES+MENU_ELEMENTOW*MENU_ELEMENT_WYS;
MENU_GRY_WYS=2*MENU_GORNY_MARGINES+MENU_GRY_ELEMENTOW*MENU_ELEMENT_WYS;
}

inline int przycisk_menu_y() {
    return screen_h - PASEK_WYS + (PASEK_WYS - PRZYCISK_MENU_H) / 2;
}

const char* const SCIEZKA_POWLOKA = "/shell.bur";
const char* const SCIEZKA_EKSPLORATOR =
    "/programy/eksplorator.cebula/eksplorator-plikow.bur";
const char* const SCIEZKA_NOTATNIK = "/programy/notatnik.cebula/notatnik.bur";
const char* const SCIEZKA_KALKULATOR = "/programy/kalkulator.cebula/kalkulator.bur";
const char* const SCIEZKA_HUSSAR = "/programy/przegladarka.cebula/przegladarka.bur";
const char* const SCIEZKA_TEST = "/programy/test.cebula/test.bur";
const char* const SCIEZKA_DEMO_UI =
    "/programy/demo_ui.cebula/demo_ui.bur";
const char* const SCIEZKA_KONTROLKI2 =
    "/programy/kontrolki2.cebula/kontrolki2.bur";
const char* const SCIEZKA_KONTROLKI3 =
    "/programy/kontrolki3.cebula/kontrolki3.bur";
const char* const SCIEZKA_KONTROLKI4 =
    "/programy/kontrolki4.cebula/kontrolki4.bur";
const char* const SCIEZKA_SNAKE = "/gry/snake.cebula/snake.bur";
const char* const SCIEZKA_PRZEGLADARKA_OBRAZOW =
    "/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur";
const char* const SCIEZKA_AKTUALIZACJE =
    "/programy/aktualizacje.cebula/aktualizacje.bur";
const char* const SCIEZKA_MOTYWY =
    "/programy/motywy.cebula/motywy.bur";
const char* const SCIEZKA_PULPIT = DesktopEntries::Directory;

// Kanoniczna tabela uruchamiania ikon wbudowanych. Obsluga klikniecia nie
// zna poszczegolnych aplikacji; kazda ikona korzysta z tego samego mechanizmu
// uruchom-lub-aktywuj i zachowuje systemowa polityke jednej instancji.
using TypIkonyAplikacji=DesktopEntries::TypIkonyAplikacji;
struct AplikacjaPulpitu {int x,y;TypIkonyAplikacji typ_ikony;};
void rysuj_ikone_aplikacji(const AplikacjaPulpitu&);
void uruchom_lub_aktywuj(const char*,const char*);

struct WpisPulpitu {
    char nazwa[PULPIT_NAZWA_CAP];
    bool folder;
    int launcher=-1;
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
// UWAGA: celowo bez `const`. `const` sugeruje sekcję tylko-do-odczytu (.rodata),
// natomiast jawne section(".data") wymaga sekcji zapisywalnej. Mieszanka
// `static const ... section(".data")` powodowała ostrzeżenie asemblera
// "ignoring changed section attributes for .data" (sekcja raz read-only,
// raz writable). Deklaracja writable + section(".data") zachowuje wymagane
// umieszczenie w segmencie DATA .bur (nie TEXT/.rodata, limit 32 KiB TEXT).
__attribute__((section(".data"), used))
static uint32_t IKONA_SNAKE_PALETA[] = {
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
static FragmentIkonyPulpitu IKONA_SNAKE_FRAGMENTY[] = {
    // (jw.: writable .data, nie const — patrz komentarz przy palecie).
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

static void rysuj_fragment_ikony(int x, int y, int lx, int ly, int lw, int lh,
                                 uint32_t kolor) {
    const int x0 = ui_skaluj(lx, ui_skala_procent);
    const int y0 = ui_skaluj(ly, ui_skala_procent);
    const int x1 = ui_skaluj(lx + lw, ui_skala_procent);
    const int y1 = ui_skaluj(ly + lh, ui_skala_procent);
    if (x1 > x0 && y1 > y0)
        gui_rysuj_prostokat(x + x0, y + y0, x1 - x0, y1 - y0, kolor);
}

static void rysuj_tekst_ikony(int x, int y, int lx, int ly, int lw, int lh,
                              uint32_t kolor, const char* tekst) {
    const int x0 = ui_skaluj(lx, ui_skala_procent);
    const int y0 = ui_skaluj(ly, ui_skala_procent);
    const int x1 = ui_skaluj(lx + lw, ui_skala_procent);
    const int y1 = ui_skaluj(ly + lh, ui_skala_procent);
    rysuj_tekst_wysrodkowany(x + x0, y + y0, x1 - x0, y1 - y0,
                             int(ui_skala_procent), kolor, tekst);
}

static void rysuj_etykiete_pulpitu(int x, int y, int w, int h, int skala,
                                   uint32_t kolor, const char* tekst) {
    if (!tekst || !tekst[0] || w <= 0 || h <= 0) return;
    // Subtelny cien pod glifami zapewnia doskonala czytelnosc na jasnych tapetach.
    rysuj_tekst_wysrodkowany(x + 1, y + 1, w, h, skala, KANONICZNY_KOLOR_CIENIA_PULPITU, tekst);
    rysuj_tekst_wysrodkowany(x, y, w, h, skala, kolor, tekst);
}

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

        rysuj_fragment_ikony(x, y, static_cast<int>(f.x), static_cast<int>(f.y),
                             static_cast<int>(f.szer), static_cast<int>(f.wys),
                             paleta[f.indeks_koloru]);
    }
}




bool menu_start_otwarte = false;
bool menu_gry_otwarte = false;
char ostatni_czas[BUFOR_CZASU] = {};
GuiOknoInfo okna_taskbara[SKLADACZ_MAKS_WARSTW] = {};
uint32_t liczba_okien_taskbara = 0;
WpisPulpitu* wpisy_pulpitu = nullptr;
char lista_pulpitu[PULPIT_LIST_CAP] = {};
int liczba_wpisow_pulpitu = 0;
int zaznaczony_wpis_pulpitu = -1; // context anchor, selection lives in desktop_layout
Desktop::Layout<PULPIT_MAX_WPISOW> desktop_layout;
static Desktop::Position desktop_previous[PULPIT_MAX_WPISOW];
static DesktopSettings::State* desktop_settings=nullptr;
static int desktop_restore_x[PULPIT_MAX_WPISOW],desktop_restore_y[PULPIT_MAX_WPISOW];
DesktopSettings::Key klucz_ukladu_pulpitu(int i) {
    auto* launcher=DesktopEntries::launcher(wpisy_pulpitu[i].launcher);
    return {launcher?'L':'F',launcher?launcher->id:wpisy_pulpitu[i].nazwa};
}
uint64_t plik_ukladu_bws(uint64_t nr,uint64_t a,uint64_t b,uint64_t c) {
    return bws_wywolaj(nr,a,b,c);
}
#if BURSZTYNDIAGDESKTOPLAYOUT
void diagnostyka_ukladu(const char* label,int value) {
    char line[96]{},digits[10];int n=0,d=0;
    while(*label && n<80)line[n++]=*label++;
    do{digits[d++]=char('0'+value%10);value/=10;}while(value && d<10);
    while(d)line[n++]=digits[--d];
    line[n++]='\n';
    bws_wywolaj(1,reinterpret_cast<uint64_t>(line));
}
#endif
void zapisz_uklad_pulpitu() {
    if(!desktop_settings || !desktop_settings->dirty)return;
    // Called after capture release and visual commit. WM owns no graphics lock;
    // the compositor is a separate task. BWS owns filesystem authorization.
    bool ok=desktop_settings->save(desktop_layout,klucz_ukladu_pulpitu,plik_ukladu_bws);
    if(!ok)bws_wywolaj(1,reinterpret_cast<uint64_t>("[DESKTOP-LAYOUT] save failed\n"));
#if BURSZTYNDIAGDESKTOPLAYOUT
    if(ok)bws_wywolaj(1,reinterpret_cast<uint64_t>("[DESKTOP-LAYOUT] saved\n"));
#endif
}

void aktualizuj_layout_pulpitu();
void odrysuj_obszar_pulpitu(Desktop::Prostokat r);
void odrysuj_zaznaczenie_pulpitu();
uint64_t hash_pulpitu = 0;
bool hash_pulpitu_wazny = false;
bool pulpit_drop_hover = false;
int pulpit_hover_folder = -1;
int pulpit_refresh_ticks = 0;
BwsCelDrop cele_drop_pulpitu[BWS_DROP_CELE_MAX] = {};

using PulpitContextAction = PlikiUI::Akcja;
PlikiUI::MenuPlikowe* pulpit_menu = nullptr;
PlikiUI::DialogPlikowy* pulpit_dialog = nullptr;
PlikiUI::WyborKatalogu* pulpit_wybor = nullptr;
bool pulpit_context_open = false;
bool odswiez_wpisy_pulpitu();
void RysujPulpit(bool wymus_pelne_odswiezenie);
enum class PulpitModalTyp : uint8_t { BRAK, PLIK, FOLDER, REMOVE, RENAME, MOVE, OPEN_WITH, NOTICE };
PulpitModalTyp pulpit_modal_typ = PulpitModalTyp::BRAK;
char pulpit_cel[512]{};
bool pulpit_cel_folder = false;
static char (*pulpit_batch)[BWS_DROP_SCIEZKA_MAX]=nullptr;
static unsigned pulpit_batch_count=0;
static char pulpit_batch_message[768]{};
bool sciezka_wpisu_pulpitu(int,char*,int);
void liczba_do_tekstu(char* out,unsigned n) {char rev[12];int len=0;do{rev[len++]=char('0'+n%10);n/=10;}while(n);for(int i=0;i<len;++i)out[i]=rev[len-1-i];out[len]=0;}
void opis_batch(const char* action,unsigned done,unsigned total,const char* failed=nullptr) {
    char a[12],b[12];liczba_do_tekstu(a,done);liczba_do_tekstu(b,total);
    const char* parts[]={action,a," / ",b,failed?". Zatrzymano na: ":"",failed?failed:""};int n=0;
    for(auto part:parts)for(int i=0;part[i] && n<int(sizeof(pulpit_batch_message))-1;++i)pulpit_batch_message[n++]=part[i];
    pulpit_batch_message[n]=0;
}
bool zapisz_batch_pulpitu() {
    pulpit_batch_count=0;
    if(!pulpit_batch)pulpit_batch=new char[PULPIT_MAX_WPISOW][BWS_DROP_SCIEZKA_MAX]{};
    if(!pulpit_batch)return false;
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected) {
        if(!sciezka_wpisu_pulpitu(i,pulpit_batch[pulpit_batch_count],BWS_DROP_SCIEZKA_MAX))return false;
        ++pulpit_batch_count;
    }
    return pulpit_batch_count>0;
}


void zamknij_pulpit_modal() {
    if(pulpit_dialog)pulpit_dialog->zamknij();
    if(pulpit_wybor)pulpit_wybor->zamknij();
    pulpit_modal_typ=PulpitModalTyp::BRAK;
}
void odswiez_pulpit_natychmiast() {
    hash_pulpitu_wazny=false;(void)odswiez_wpisy_pulpitu();RysujPulpit(true);
}
bool otworz_pulpit_modal(PulpitModalTyp typ,const char* message=nullptr) {
    zamknij_pulpit_modal();
    if(typ==PulpitModalTyp::MOVE) {
        if(!pulpit_wybor)pulpit_wybor=new PlikiUI::WyborKatalogu();
        BursztynUI::Prostokat work{0,0,screen_w,screen_h-PASEK_WYS};
        if(!pulpit_wybor->otworz(pulpit_cel,SCIEZKA_PULPIT,work,work))return false;
        pulpit_modal_typ=typ;(void)gui_rejestruj_cele_drop(nullptr,0);gui_odswiez();return true;
    }
    const char* initial=typ==PulpitModalTyp::PLIK?"Nowy plik.txt":typ==PulpitModalTyp::MOVE?SCIEZKA_PULPIT:"";
    if(typ==PulpitModalTyp::RENAME) {
        initial=pulpit_cel;for(const char* c=pulpit_cel;*c;++c)if(*c=='/')initial=c+1;
    }
    const char* title=typ==PulpitModalTyp::FOLDER?"Utwórz folder":typ==PulpitModalTyp::PLIK?"Utwórz plik tekstowy":
        typ==PulpitModalTyp::REMOVE?"Usuń trwale":typ==PulpitModalTyp::RENAME?"Zmień nazwę":
        typ==PulpitModalTyp::MOVE?"Przenieś do folderu":typ==PulpitModalTyp::OPEN_WITH?"Otwórz za pomocą":"Operacja plikowa";
    bool confirmation=typ==PulpitModalTyp::REMOVE || typ==PulpitModalTyp::OPEN_WITH || typ==PulpitModalTyp::NOTICE;
    BursztynUI::Prostokat work{0,0,screen_w,screen_h-PASEK_WYS};
    const char* question=message?message:typ==PulpitModalTyp::REMOVE?pulpit_cel:
        typ==PulpitModalTyp::OPEN_WITH?"Aplikacja: Notatnik (pliki tekstowe)":typ==PulpitModalTyp::MOVE?"Ścieżka folderu docelowego:":nullptr;
    if(!pulpit_dialog->otworz(title,confirmation?nullptr:initial,question,work,work,typ==PulpitModalTyp::MOVE?511:55,
            typ==PulpitModalTyp::REMOVE?"Usuń":typ==PulpitModalTyp::OPEN_WITH?"Otwórz":"OK"))return false;
    pulpit_modal_typ=typ;(void)gui_rejestruj_cele_drop(nullptr,0);gui_odswiez();return true;
}
void zatwierdz_pulpit_modal() {
    const char* text=pulpit_modal_typ==PulpitModalTyp::MOVE?pulpit_wybor->wybrana_sciezka():pulpit_dialog->tekst();
    const auto type=pulpit_modal_typ;
    bool ok=false;
    if(type==PulpitModalTyp::PLIK || type==PulpitModalTyp::FOLDER)
        ok=PlikiUI::utworz_element(SCIEZKA_PULPIT,text,type==PulpitModalTyp::FOLDER);
    else if(type==PulpitModalTyp::REMOVE) {
        if(pulpit_batch_count) {
            unsigned done=0;
            for(;done<pulpit_batch_count;++done)if(!usun_twor_uzytkownika(pulpit_batch[done]))break;
            ok=done==pulpit_batch_count;
            if(!ok)opis_batch("Usunięto ",done,pulpit_batch_count,pulpit_batch[done]);
        } else ok=usun_twor_uzytkownika(pulpit_cel);
    }
    else if(type==PulpitModalTyp::RENAME)ok=PlikiUI::nazwa_poprawna(text) && zmien_nazwe_uzytkownika(pulpit_cel,text);
    else if(type==PulpitModalTyp::MOVE)ok=przenies_twor_uzytkownika(pulpit_cel,text);
    else if(type==PulpitModalTyp::OPEN_WITH)ok=PlikiUI::notatnik(pulpit_cel,pulpit_cel_folder);
    else ok=true;
    zamknij_pulpit_modal();
    if(type==PulpitModalTyp::NOTICE)return;
    if(ok)odswiez_pulpit_natychmiast();
    else {
        odswiez_pulpit_natychmiast();
        (void)otworz_pulpit_modal(PulpitModalTyp::NOTICE,type==PulpitModalTyp::REMOVE && pulpit_batch_count?pulpit_batch_message:"Nie wykonano: konflikt nazwy, nieprawidłowa nazwa lub brak uprawnień.");
    }
    pulpit_batch_count=0;
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
    if(indeks>=0 && indeks<liczba_wpisow_pulpitu && wpisy_pulpitu[indeks].launcher>=0)return false;
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

bool teksty_rowne(const char* a,const char* b) {while(*a && *a==*b){++a;++b;}return *a==*b;}
bool odswiez_wpisy_pulpitu() {
    // Keep display buffers off the Ring 3 stack and within BUR's static data
    // budget. They are allocated once, never from the pointer-motion path.
    if(!wpisy_pulpitu)wpisy_pulpitu=new WpisPulpitu[PULPIT_MAX_WPISOW]{};
    if(!wpisy_pulpitu)return false;
    wyzeruj_bufor(lista_pulpitu, PULPIT_LIST_CAP);
    if (!wylistuj_katalog_uzytkownika(
            SCIEZKA_PULPIT, lista_pulpitu, PULPIT_LIST_CAP)) {
        // Launchers existed independently of filesystem listing before this
        // integration. Retain loaded entries on failure; on first load still
        // expose the protected catalog without creating a directory or files.
        if(liczba_wpisow_pulpitu>0)return false;
        lista_pulpitu[0]=0;
    }
    const int length = dlugosc_limit(lista_pulpitu, PULPIT_LIST_CAP);
    if (length >= PULPIT_LIST_CAP || (length > 0 && lista_pulpitu[length - 1] != '\n'))
        return false;
    const uint64_t nowy_hash = hash_listy(lista_pulpitu, length);
    if (hash_pulpitu_wazny && nowy_hash == hash_pulpitu) return false;

    static WpisPulpitu* nowe=nullptr;
    if(!nowe)nowe=new WpisPulpitu[PULPIT_MAX_WPISOW]{};
    if(!nowe)return false;
    for(int i=0;i<PULPIT_MAX_WPISOW;++i)nowe[i]=WpisPulpitu{};
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
    if(count+DesktopEntries::Count>PULPIT_MAX_WPISOW)return false;
    for(int id=0;id<DesktopEntries::Count;++id) {
        auto& entry=nowe[count++];entry.launcher=id;
        const char* name=DesktopEntries::Launchers[id].name;
        for(int b=0;name[b];++b)entry.nazwa[b]=name[b];
    }
    for(int i=0;i<desktop_layout.count;++i)desktop_previous[i]=desktop_layout.items[i];
    for(int i=0;i<count;++i) {
        Desktop::Position saved{};
        for(int j=0;j<liczba_wpisow_pulpitu;++j)if(wpisy_pulpitu[j].launcher==nowe[i].launcher && wpisy_pulpitu[j].folder==nowe[i].folder &&
            teksty_rowne(wpisy_pulpitu[j].nazwa,nowe[i].nazwa)) {saved=desktop_previous[j];break;}
        desktop_layout.items[i]=saved;
    }
    for (int i = 0; i < count; ++i) wpisy_pulpitu[i] = nowe[i];
    liczba_wpisow_pulpitu = count;desktop_layout.count=count;aktualizuj_layout_pulpitu();
    if(!desktop_settings) {
        desktop_settings=new DesktopSettings::State{};
        if(desktop_settings) {
            if(desktop_settings->load(plik_ukladu_bws)) {
                [[maybe_unused]] int corrected=desktop_settings->apply(desktop_layout,klucz_ukladu_pulpitu);
#if BURSZTYNDIAGDESKTOPLAYOUT
                diagnostyka_ukladu("[DESKTOP-LAYOUT] reflow=",corrected);
#endif
            }
#if BURSZTYNDIAGDESKTOPLAYOUT
            bws_wywolaj(1,reinterpret_cast<uint64_t>("[DESKTOP-LAYOUT] /ustawienia/uklad-pulpitu.cfg\n"));
            diagnostyka_ukladu("[DESKTOP-LAYOUT] loaded=",desktop_settings->count);
            diagnostyka_ukladu("[DESKTOP-LAYOUT] launchers=",desktop_settings->matchedLaunchers);
            diagnostyka_ukladu("[DESKTOP-LAYOUT] files=",desktop_settings->matchedFiles);
            diagnostyka_ukladu("[DESKTOP-LAYOUT] stale=",desktop_settings->stale);
#endif
        }
    }
    if (zaznaczony_wpis_pulpitu >= count) zaznaczony_wpis_pulpitu = -1;
    hash_pulpitu = nowy_hash;
    hash_pulpitu_wazny = true;
    return true;
}

void aktualizuj_layout_pulpitu() {
    const int previousDpi=desktop_layout.dpi;
    const bool geometryChanged=desktop_layout.workWidth>0 && (desktop_layout.workWidth!=screen_w ||
        desktop_layout.workBottom!=screen_h-PASEK_WYS || desktop_layout.dpi!=ui_skala_procent);
    if(geometryChanged)for(int i=0;i<desktop_layout.count;++i) {
        auto r=desktop_layout.visual_bounds(i);desktop_restore_x[i]=r.x;desktop_restore_y[i]=r.y;
    }
    desktop_layout.geometry(screen_w,screen_h-PASEK_WYS,PULPIT_GRID_Y,ui_skala_procent);
    auto m=pulpit_metryki(ui_skala_procent);
    int maxLines=(desktop_layout.rows*desktop_layout.ch-m.ikona_wys-m.odstep_etykiety-ui_skaluj(6,ui_skala_procent))/(m.wysokosc_linii+m.odstep_linii);
    for(int i=0;i<desktop_layout.count;++i) {
        auto& p=desktop_layout.items[i];
        p.label=Desktop::wrap(wpisy_pulpitu[i].nazwa,m.etykieta_szer,m.wysokosc_linii,m.odstep_linii,maxLines,int(ui_skala_procent),oblicz_szerokosc_tekstu);
        int height=m.ikona_wys+m.odstep_etykiety+p.label.height+ui_skaluj(6,ui_skala_procent);
        p.span=(height+desktop_layout.ch-1)/desktop_layout.ch;
    }
    if(geometryChanged)for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed)
        desktop_layout.visual_position(i,int(int64_t(desktop_restore_x[i])*ui_skala_procent/previousDpi),
            int(int64_t(desktop_restore_y[i])*ui_skala_procent/previousDpi));
    // Retain the existing initial row-major layout. Settings restoration and
    // display changes reserve valid visual positions before relocating others.
    for(int i=0;i<desktop_layout.count;++i)if(!desktop_layout.items[i].placed)
        desktop_layout.place(i,i%desktop_layout.cols,i/desktop_layout.cols);
    const int corrected=desktop_layout.reflow();
    if(corrected && desktop_settings)desktop_settings->dirty=true;
#if BURSZTYNDIAGDESKTOPLAYOUT
    if(corrected)diagnostyka_ukladu("[DESKTOP-LAYOUT] reflow=",corrected);
#endif

}
void prostokat_wpisu_pulpitu(int indeks, int* x, int* y) {
    auto r=desktop_layout.cell(indeks);
    if(x)*x=r.x;
    if(y)*y=r.y;
}
Desktop::Prostokat wizualny_wpis_pulpitu(int i) {
    int x,y;prostokat_wpisu_pulpitu(i,&x,&y);
    return Desktop::visual(x,y,desktop_layout.items[i].label,ui_skala_procent);
}
int wpis_pulpitu_w_punkcie(int x, int y) {
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed && wizualny_wpis_pulpitu(i).zawiera(x,y))return i;
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed && desktop_layout.cell(i).zawiera(x,y))return i;
    return -1;
}



bool podwojny_klik(uint64_t previous, uint64_t current) {
    if (!previous || current < previous) return false;
    const uint64_t difference = current - previous;
    return current >= UINT64_C(1000000000)
        ? difference <= UINT64_C(500000000) : difference <= 50;
}

bool tekst_rowny(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
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

enum class StanPrzyciskuMenu : uint8_t {
    Normalny = 0,
    Hover = 1,
    Wcisniety = 2,
    Aktywny = 3,
    Liczba = 4
};

static uint32_t* g_master_logo_pixels = nullptr;
static uint32_t g_master_logo_w = 0;
static uint32_t g_master_logo_h = 0;
static bool g_master_logo_probowano = false;

static uint32_t* g_cached_przyciski[4] = {nullptr, nullptr, nullptr, nullptr};
static uint32_t g_cached_skala = 0;
static int g_cached_btn_w = 0;
static int g_cached_btn_h = 0;

static bool menu_przycisk_hover = false;
static bool menu_przycisk_wcisniety = false;

void wczytaj_master_logo() {
    if (g_master_logo_probowano) return;
    g_master_logo_probowano = true;

    const char* const sciezka = "/obrazy/logo.png";
    uint64_t ret = bws_wywolaj(44, (uint64_t)sciezka);
    if (ret <= 1) return;
    uint64_t rozmiar_pliku = ret - 1;

    uint8_t* bufor_png = new uint8_t[rozmiar_pliku];
    if (!bufor_png) return;

    uint64_t czyt_ret = bws_wywolaj(5, (uint64_t)sciezka, (uint64_t)bufor_png, rozmiar_pliku);
    if (czyt_ret == 1) {
        uint32_t* pix = nullptr;
        int w = 0, h = 0;
        if (BursztynPNG::dekoduj_png(bufor_png, rozmiar_pliku, &pix, &w, &h)) {
            if (pix && w > 0 && h > 0) {
                g_master_logo_pixels = pix;
                g_master_logo_w = static_cast<uint32_t>(w);
                g_master_logo_h = static_cast<uint32_t>(h);
            }
        }
    }
    delete[] bufor_png;
}

void zaktualizuj_cache_przycisku(uint32_t skala) {
    if (g_cached_skala == skala && g_cached_przyciski[0] != nullptr) return;

    wczytaj_master_logo();

    const int btn_w = ui_skaluj(PRZYCISK_MENU_LOGICZNA_SZER, skala);
    const int btn_h = ui_skaluj(PRZYCISK_MENU_LOGICZNA_WYS, skala);
    const int logo_w = ui_skaluj(LOGO_LOGICZNA_SZER, skala);
    const int logo_h = ui_skaluj(LOGO_LOGICZNA_WYS, skala);
    const int logo_x = (btn_w - logo_w) / 2;
    const int logo_y = (btn_h - logo_h) / 2;
    const int pasek_h = ui_skaluj(TASKBAR_LOGICZNA_WYS, skala);
    (void)pasek_h;

    for (int s = 0; s < 4; ++s) {
        if (g_cached_przyciski[s]) {
            delete[] g_cached_przyciski[s];
            g_cached_przyciski[s] = nullptr;
        }
    }

    g_cached_skala = skala;
    g_cached_btn_w = btn_w;
    g_cached_btn_h = btn_h;

    const size_t total_pix = (size_t)btn_w * (size_t)btn_h;
    if (total_pix == 0) return;

    for (int s = 0; s < 4; ++s) {
        g_cached_przyciski[s] = new uint32_t[total_pix];
        if (!g_cached_przyciski[s]) return;
    }

    uint32_t* scaled_logo = nullptr;
    if (g_master_logo_pixels && logo_w > 0 && logo_h > 0) {
        scaled_logo = new uint32_t[(size_t)logo_w * (size_t)logo_h];
        if (scaled_logo) {
            for (int dy = 0; dy < logo_h; ++dy) {
                uint32_t sy0 = (uint32_t)((uint64_t)dy * g_master_logo_h / logo_h);
                uint32_t sy1 = (uint32_t)((uint64_t)(dy + 1) * g_master_logo_h / logo_h);
                if (sy1 <= sy0) sy1 = sy0 + 1;
                if (sy1 > g_master_logo_h) sy1 = g_master_logo_h;

                for (int dx = 0; dx < logo_w; ++dx) {
                    uint32_t sx0 = (uint32_t)((uint64_t)dx * g_master_logo_w / logo_w);
                    uint32_t sx1 = (uint32_t)((uint64_t)(dx + 1) * g_master_logo_w / logo_w);
                    if (sx1 <= sx0) sx1 = sx0 + 1;
                    if (sx1 > g_master_logo_w) sx1 = g_master_logo_w;

                    uint64_t sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
                    uint64_t cnt = 0;
                    for (uint32_t sy = sy0; sy < sy1; ++sy) {
                        for (uint32_t sx = sx0; sx < sx1; ++sx) {
                            uint32_t p = g_master_logo_pixels[sy * g_master_logo_w + sx];
                            uint32_t a = (p >> 24) & 0xFF;
                            uint32_t r = (p >> 16) & 0xFF;
                            uint32_t g = (p >> 8) & 0xFF;
                            uint32_t b = p & 0xFF;
                            sum_a += a;
                            sum_r += (uint64_t)r * a;
                            sum_g += (uint64_t)g * a;
                            sum_b += (uint64_t)b * a;
                            cnt++;
                        }
                    }
                    if (cnt > 0 && sum_a > 0) {
                        uint32_t fa = (uint32_t)(sum_a / cnt);
                        uint32_t fr = (uint32_t)(sum_r / sum_a);
                        uint32_t fg = (uint32_t)(sum_g / sum_a);
                        uint32_t fb = (uint32_t)(sum_b / sum_a);
                        scaled_logo[dy * logo_w + dx] = (fa << 24) | (fr << 16) | (fg << 8) | fb;
                    } else {
                        scaled_logo[dy * logo_w + dx] = 0;
                    }
                }
            }
        }
    }

    const uint32_t c_podniesiony = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
    const uint32_t c_akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
    const uint32_t c_akcent_aktywny = motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY);
    const uint32_t c_ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);

    for (int s = 0; s < 4; ++s) {
        uint32_t* buf = g_cached_przyciski[s];
        uint32_t fill_color = c_podniesiony;
        uint32_t border_color = c_ramka;

        if (s == 1) { // Hover
            fill_color = c_podniesiony;
            border_color = c_akcent;
        } else if (s == 2) { // Wcisniety
            fill_color = c_akcent_aktywny;
            border_color = c_akcent_aktywny;
        } else if (s == 3) { // Aktywny (menu otwarte)
            fill_color = c_podniesiony;
            border_color = c_akcent;
        }

        for (int by = 0; by < btn_h; ++by) {
            for (int bx = 0; bx < btn_w; ++bx) {
                bool is_border = (bx == 0 || bx == btn_w - 1 || by == 0 || by == btn_h - 1);
                uint32_t bg_color = is_border ? border_color : fill_color;

                const uint32_t bg_r = (bg_color >> 16) & 0xFF;
                const uint32_t bg_g = (bg_color >> 8) & 0xFF;
                const uint32_t bg_b = bg_color & 0xFF;

                uint32_t pix = 0;
                if (scaled_logo &&
                    bx >= logo_x && bx < logo_x + logo_w &&
                    by >= logo_y && by < logo_y + logo_h) {
                    uint32_t lp = scaled_logo[(by - logo_y) * logo_w + (bx - logo_x)];
                    uint32_t a = (lp >> 24) & 0xFF;
                    uint32_t lr = (lp >> 16) & 0xFF;
                    uint32_t lg = (lp >> 8) & 0xFF;
                    uint32_t lb = lp & 0xFF;

                    uint32_t out_r = (lr * a + bg_r * (255 - a)) / 255;
                    uint32_t out_g = (lg * a + bg_g * (255 - a)) / 255;
                    uint32_t out_b = (lb * a + bg_b * (255 - a)) / 255;
                    pix = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
                } else if (!scaled_logo) {
                    // Fallback: bursztynowy symbol (diament) na srodku
                    int cx = btn_w / 2;
                    int cy = btn_h / 2;
                    int r_diag = (logo_h > 4) ? (logo_h / 3) : 4;
                    int dist = (bx > cx ? bx - cx : cx - bx) + (by > cy ? by - cy : cy - by);
                    if (dist <= r_diag) {
                        pix = (dist == r_diag) ? (0xFF000000 | c_akcent) : (0xFF000000 | c_akcent_aktywny);
                    } else {
                        pix = 0xFF000000 | (bg_color & 0x00FFFFFF);
                    }
                } else {
                    pix = 0xFF000000 | (bg_color & 0x00FFFFFF);
                }
                buf[by * btn_w + bx] = pix;
            }
        }
    }

    if (scaled_logo) {
        delete[] scaled_logo;
    }
}

void RysujPrzyciskMenu() {
    zaktualizuj_cache_przycisku(ui_skala_procent);
    StanPrzyciskuMenu stan = StanPrzyciskuMenu::Normalny;
    if (menu_start_otwarte) {
        stan = StanPrzyciskuMenu::Aktywny;
    } else if (menu_przycisk_wcisniety) {
        stan = StanPrzyciskuMenu::Wcisniety;
    } else if (menu_przycisk_hover) {
        stan = StanPrzyciskuMenu::Hover;
    }
    const uint32_t* buf = g_cached_przyciski[static_cast<size_t>(stan)];
    if (buf && g_cached_btn_w > 0 && g_cached_btn_h > 0) {
        gui_blit(PRZYCISK_MENU_X, przycisk_menu_y(),
                 g_cached_btn_w, g_cached_btn_h, buf);
    }
}

void RysujTaskbar() {
    const uint32_t tlo_paska = motyw_kolor(MOTYW_KOLOR_PANEL);
    const uint32_t ramka_paska = motyw_kolor(MOTYW_KOLOR_RAMKA);
    const uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
    const uint32_t podniesiony = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);

    gui_rysuj_prostokat(0, screen_h - PASEK_WYS, screen_w, PASEK_WYS, tlo_paska);
    gui_rysuj_prostokat(0, screen_h - PASEK_WYS, screen_w, 1, ramka_paska);

    RysujPrzyciskMenu();
    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        const GuiOknoInfo& o = okna_taskbara[i];
        if (o.tytul[0] == '\0' || task_x + TASK_BUTTON_W > screen_w - ui_skaluj(150,ui_skala_procent))
            continue;
        const uint32_t kolor = o.aktywne ? akcent :
            (o.stan == GUI_OKNO_ZMINIMALIZOWANE ? podniesiony : motyw_kolor(MOTYW_KOLOR_PRZYCISK));
        const uint32_t kolor_tekst = o.aktywne ? motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU) : motyw_kolor(MOTYW_KOLOR_TEKST);

        gui_rysuj_prostokat(task_x, screen_h - ui_skaluj(36,ui_skala_procent),
                            TASK_BUTTON_W, SKROT_H, kolor);
        if (!o.aktywne) {
            gui_rysuj_prostokat(task_x, screen_h - ui_skaluj(36,ui_skala_procent), TASK_BUTTON_W, 1, ramka_paska);
            gui_rysuj_prostokat(task_x, screen_h - ui_skaluj(36,ui_skala_procent) + SKROT_H - 1, TASK_BUTTON_W, 1, ramka_paska);
            gui_rysuj_prostokat(task_x, screen_h - ui_skaluj(36,ui_skala_procent), 1, SKROT_H, ramka_paska);
            gui_rysuj_prostokat(task_x + TASK_BUTTON_W - 1, screen_h - ui_skaluj(36,ui_skala_procent), 1, SKROT_H, ramka_paska);
        }
        char podpis[15] = {};
        for (int c = 0; c < 14 && o.tytul[c] != '\0'; ++c)
            podpis[c] = o.tytul[c];
        rysuj_tekst_wysrodkowany(task_x, screen_h - ui_skaluj(36,ui_skala_procent),
                                 TASK_BUTTON_W, SKROT_H, 1,
                                 kolor_tekst, podpis);
        task_x += TASK_BUTTON_W + TASK_BUTTON_GAP;
    }

    // Atomowe rysowanie zegara RTC na powierzchni paska zadań
    if (ostatni_czas[0] == '\0') {
        (void)pobierz_i_zaktualizuj_czas();
    }
    const int zegar_szer = ui_skaluj(150, ui_skala_procent);
    const int zegar_wys = PASEK_WYS;
    const int zegar_x = screen_w - zegar_szer;
    const int zegar_y = screen_h - zegar_wys;
    if (zegar_x >= 0 && zegar_y >= 0) {
        gui_rysuj_prostokat(zegar_x, zegar_y, zegar_szer, zegar_wys, podniesiony);
        gui_rysuj_prostokat(zegar_x, zegar_y, 1, zegar_wys, ramka_paska);
        gui_rysuj_prostokat(zegar_x, zegar_y, zegar_szer, 1, ramka_paska);
        if (ostatni_czas[0] != '\0') {
            rysuj_tekst_wysrodkowany(zegar_x, zegar_y, zegar_szer, zegar_wys,
                                     1, akcent, ostatni_czas);
        }
    }
}

void zarejestruj_pulpit_jako_cel_drop() {
    for (uint32_t i = 0; i < BWS_DROP_CELE_MAX; ++i)
        cele_drop_pulpitu[i] = BwsCelDrop{};
    uint32_t count = 0;
    for (int i = 0; i < liczba_wpisow_pulpitu &&
         count + 1U < BWS_DROP_CELE_MAX; ++i) {
        if (!wpisy_pulpitu[i].folder || !desktop_layout.items[i].placed) continue;
        int x = 0, y = 0;
        prostokat_wpisu_pulpitu(i, &x, &y);
        BwsCelDrop& cel = cele_drop_pulpitu[count];
        auto r=desktop_layout.cell(i);
        cel.x = x; cel.y = y; cel.szer = r.szerokosc; cel.wys = r.wysokosc;
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

void rysuj_ramke_pulpitu(Desktop::Prostokat r,uint32_t color) {
    int t=ui_skaluj(1,ui_skala_procent);
    if(r.szerokosc<=0 || r.wysokosc<=0)return;
    gui_rysuj_prostokat(r.x,r.y,r.szerokosc,t,color);
    gui_rysuj_prostokat(r.x,r.y+r.wysokosc-t,r.szerokosc,t,color);
    gui_rysuj_prostokat(r.x,r.y,t,r.wysokosc,color);
    gui_rysuj_prostokat(r.x+r.szerokosc-t,r.y,t,r.wysokosc,color);
}
void RysujWpisPulpitu(int i) {
    if(!desktop_layout.items[i].placed)return;
    int x,y;prostokat_wpisu_pulpitu(i,&x,&y);
    const auto g=pulpit_geometria_komorki(x,y,ui_skala_procent);
    const auto& p=desktop_layout.items[i];
    if(p.selected || pulpit_hover_folder==i) {
        auto r=wizualny_wpis_pulpitu(i);
        gui_rysuj_prostokat(r.x,r.y,r.szerokosc,r.wysokosc,motyw_kolor(MOTYW_KOLOR_PULPIT_ZAZNACZENIE));
        rysuj_ramke_pulpitu(r,motyw_kolor(MOTYW_KOLOR_PULPIT_RAMKA));
    }
    if(auto* launcher=DesktopEntries::launcher(wpisy_pulpitu[i].launcher)) {
        AplikacjaPulpitu app{x,y,launcher->icon};
        rysuj_ikone_aplikacji(app);
    } else if(PlikiUI::tekstowy(wpisy_pulpitu[i].nazwa,wpisy_pulpitu[i].folder)) {
        // Native document silhouette, folded corner and text strokes, using
        // the same scaled fragments as the existing desktop icon renderer.
        uint32_t paper=motyw_kolor(MOTYW_KOLOR_PANEL);
        uint32_t ink=motyw_kolor(MOTYW_KOLOR_TEKST);
        if(!paper)paper=0x01000000U;
        if(!ink)ink=0x01000000U;
        rysuj_fragment_ikony(g.ikona_x,g.ikona_y,9,2,25,42,ink);
        rysuj_fragment_ikony(g.ikona_x,g.ikona_y,10,3,23,40,paper);
        rysuj_fragment_ikony(g.ikona_x,g.ikona_y,34,10,6,34,ink);
        rysuj_fragment_ikony(g.ikona_x,g.ikona_y,34,11,5,32,paper);
        for(int n=0;n<7;++n)rysuj_fragment_ikony(g.ikona_x,g.ikona_y,33,3+n,1+n,1,ink);
        for(int n=0;n<4;++n)rysuj_fragment_ikony(g.ikona_x,g.ikona_y,15,18+n*5,n==3?14:19,2,ink);
    } else {
        uint32_t color=wpisy_pulpitu[i].folder?0x00FFBF00:0x00708090;
        if(konczy_sie(wpisy_pulpitu[i].nazwa,".bur"))color=0x0038B000;
        else if(konczy_sie(wpisy_pulpitu[i].nazwa,".cebula"))color=0x00D050A0;
        rysuj_fragment_ikony(g.ikona_x,g.ikona_y,4,4,40,40,color);
        if(wpisy_pulpitu[i].folder)rysuj_fragment_ikony(g.ikona_x,g.ikona_y,8,0,18,8,color);
    }
    auto m=pulpit_metryki(ui_skala_procent);
    for(int line=0;line<p.label.count;++line) {
        char text[PULPIT_NAZWA_CAP+4]{};
        int size=p.label.ends[line]-p.label.starts[line];
        for(int b=0;b<size;++b)text[b]=wpisy_pulpitu[i].nazwa[p.label.starts[line]+b];
        if(!p.label.complete && line==p.label.count-1) {
            text[size]=char(0xe2);text[size+1]=char(0x80);text[size+2]=char(0xa6);
        }
        rysuj_etykiete_pulpitu(g.etykieta_x,g.etykieta_y+line*(m.wysokosc_linii+m.odstep_linii),
            g.etykieta_szer,m.wysokosc_linii,int(ui_skala_procent),
            p.selected?motyw_kolor(MOTYW_KOLOR_PULPIT_TEKST_ZAZNACZONY):KANONICZNY_KOLOR_ETYKIETY_PULPITU,text);
    }
}
void RysujWpisyPulpitu() {for(int i=0;i<desktop_layout.count;++i)RysujWpisPulpitu(i);}
void rysuj_ikone_aplikacji(const AplikacjaPulpitu& aplikacja);
void odrysuj_obszar_pulpitu(Desktop::Prostokat r) {
    if(r.szerokosc<=0 || r.wysokosc<=0)return;
    // Zero removes pixels from this transparent desktop surface. The compositor
    // restores its cached wallpaper; no wallpaper decode or full-layer clear.
    gui_rysuj_prostokat(r.x,r.y,r.szerokosc,r.wysokosc,0);
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed && Desktop::intersects(r,wizualny_wpis_pulpitu(i)))RysujWpisPulpitu(i);
}
void odrysuj_zaznaczenie_pulpitu() {
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed && desktop_previous[i].selected!=desktop_layout.items[i].selected)
        odrysuj_obszar_pulpitu(wizualny_wpis_pulpitu(i));
}
void zapamietaj_zaznaczenie_pulpitu() {for(int i=0;i<desktop_layout.count;++i)desktop_previous[i]=desktop_layout.items[i];}

// No filesystem calls on MouseMove. Authoritative permissions/collisions
// remain in the existing BWS move operation, invoked once per source on drop.
static char pulpit_drop_target[BWS_DROP_SCIEZKA_MAX]{};
bool poprawny_cel_drop_pulpitu(int target) {
    if(target<0 || target>=desktop_layout.count || !wpisy_pulpitu[target].folder ||
       desktop_layout.items[target].selected)return false;
    if(!sciezka_wpisu_pulpitu(target,pulpit_drop_target,sizeof(pulpit_drop_target)))return false;
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected) {
        char source[BWS_DROP_SCIEZKA_MAX]{};
        if(!sciezka_wpisu_pulpitu(i,source,sizeof(source)) ||
           !PlikiUI::cel_przeniesienia_poprawny(source,pulpit_drop_target))return false;
    }
    return desktop_layout.selected_count()>0;
}
void ustaw_hover_drop_pulpitu(int target) {
    int old=pulpit_hover_folder;pulpit_hover_folder=target;
    if(old>=0 && old<desktop_layout.count)odrysuj_obszar_pulpitu(wizualny_wpis_pulpitu(old));
    if(target>=0)odrysuj_obszar_pulpitu(wizualny_wpis_pulpitu(target));
}
bool wykonaj_drop_pulpitu(int target) {
    if(!poprawny_cel_drop_pulpitu(target) || !zapisz_batch_pulpitu()) {
        (void)otworz_pulpit_modal(PulpitModalTyp::NOTICE,"Nieprawidłowy cel przeniesienia.");return false;
    }
    unsigned done=0;
    for(;done<pulpit_batch_count;++done)
        if(!przenies_twor_uzytkownika(pulpit_batch[done],pulpit_drop_target))break;
    bool ok=done==pulpit_batch_count;
    if(!ok)opis_batch("Przeniesiono ",done,pulpit_batch_count,pulpit_batch[done]);
    pulpit_batch_count=0;
    odswiez_pulpit_natychmiast();
    if(!ok)(void)otworz_pulpit_modal(PulpitModalTyp::NOTICE,pulpit_batch_message);
    return ok;
}

struct GestPulpitu {
    bool candidate=false,dragging=false,rubber=false,ctrl=false;
    int item=-1,x=0,y=0,dx=0,dy=0;
    bool base[PULPIT_MAX_WPISOW]{};
    Desktop::Prostokat band;
} desktop_gesture;
static uint32_t desktop_modifiers=0;
void odtworz_obrys(Desktop::Prostokat r) {
    int t=ui_skaluj(1,ui_skala_procent);
    odrysuj_obszar_pulpitu({r.x,r.y,r.szerokosc,t});
    odrysuj_obszar_pulpitu({r.x,r.y+r.wysokosc-t,r.szerokosc,t});
    odrysuj_obszar_pulpitu({r.x,r.y,t,r.wysokosc});
    odrysuj_obszar_pulpitu({r.x+r.szerokosc-t,r.y,t,r.wysokosc});
}
void obrysy_grupy(bool erase) {
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected && desktop_layout.items[i].placed) {
        auto r=wizualny_wpis_pulpitu(i);r.x+=desktop_gesture.dx;r.y+=desktop_gesture.dy;
        if(erase)odtworz_obrys(r);else rysuj_ramke_pulpitu(r,motyw_kolor(MOTYW_KOLOR_PULPIT_RAMKA));
    }
}
void anuluj_gest_pulpitu() {
    if(desktop_gesture.dragging){if(desktop_gesture.rubber)odtworz_obrys(desktop_gesture.band);else obrysy_grupy(true);}
    ustaw_hover_drop_pulpitu(-1);
    desktop_gesture=GestPulpitu{};gui_ustaw_capture_myszy(false);
}
// One production handler is also exercised by host tests.
bool obsluz_gest_pulpitu(const bws_zdarzenie& e) {
    auto& g=desktop_gesture;
    if(e.typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY) {
        desktop_modifiers=e.przyciski;
        bool a=(e.kod&BWS_KEY_SOURCE_USB)?(e.kod&255)==4:(e.kod&127)==0x1e;
        if(e.x && a && (desktop_modifiers&BWS_MOD_CTRL)) {
            zapamietaj_zaznaczenie_pulpitu();
            for(int i=0;i<desktop_layout.count;++i)desktop_layout.items[i].selected=true;
            odrysuj_zaznaczenie_pulpitu();gui_odswiez();return true;
        }
        return false;
    }
    if(e.typ==BWS_ZDARZENIE_BLUR || e.typ==BWS_ZDARZENIE_EKRAN || e.typ==BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) {
        if(g.candidate)anuluj_gest_pulpitu();
        if(e.typ==BWS_ZDARZENIE_BLUR)desktop_modifiers=0;
        return false;
    }
    if(e.typ==BWS_ZDARZENIE_MYSZ_DOWN) {
        int i=wpis_pulpitu_w_punkcie(e.x,e.y);
        if(e.y<0 || e.y>=screen_h-PASEK_WYS || e.x<0 || e.x>=screen_w)return false;
        anuluj_gest_pulpitu();zapamietaj_zaznaczenie_pulpitu();
        g.item=i;g.ctrl=(desktop_modifiers&BWS_MOD_CTRL)!=0;g.rubber=i<0;
        for(int k=0;k<desktop_layout.count;++k)g.base[k]=desktop_layout.items[k].selected;
        desktop_layout.drag_click(i,g.ctrl);zaznaczony_wpis_pulpitu=i;
        odrysuj_zaznaczenie_pulpitu();
        g.candidate=true;g.x=e.x;g.y=e.y;gui_ustaw_capture_myszy(true);gui_odswiez();return true;
    }
    if(e.typ==BWS_ZDARZENIE_MYSZ_RUCH && g.candidate) {
        int dx=e.x-g.x,dy=e.y-g.y,threshold=ui_skaluj(4,ui_skala_procent);
        if(!g.dragging && dx<=threshold && dx>=-threshold && dy<=threshold && dy>=-threshold)return true;
        if(!g.rubber)desktop_layout.clamp_delta(dx,dy);
        int target=g.rubber?-1:wpis_pulpitu_w_punkcie(e.x,e.y);
        if(!poprawny_cel_drop_pulpitu(target))target=-1;
        if(g.dragging && dx==g.dx && dy==g.dy && target==pulpit_hover_folder)return true;
        if(target!=pulpit_hover_folder)ustaw_hover_drop_pulpitu(target);
        if(g.dragging){if(g.rubber)odtworz_obrys(g.band);else obrysy_grupy(true);}
        g.dragging=true;g.dx=dx;g.dy=dy;
        if(g.rubber) {
            int x=e.x<0?0:e.x>=screen_w?screen_w-1:e.x;
            int y=e.y<0?0:e.y>=screen_h-PASEK_WYS?screen_h-PASEK_WYS-1:e.y;
            g.band={x<g.x?x:g.x,y<g.y?y:g.y,(x<g.x?g.x-x:x-g.x)+1,(y<g.y?g.y-y:y-g.y)+1};
            zapamietaj_zaznaczenie_pulpitu();
            for(int i=0;i<desktop_layout.count;++i)desktop_layout.items[i].selected=
                (g.ctrl && g.base[i]) || (desktop_layout.items[i].placed && Desktop::intersects(g.band,wizualny_wpis_pulpitu(i)));
            odrysuj_zaznaczenie_pulpitu();rysuj_ramke_pulpitu(g.band,motyw_kolor(MOTYW_KOLOR_PULPIT_RAMKA));
        } else {
            desktop_layout.clamp_delta(dx,dy);g.dx=dx;g.dy=dy;obrysy_grupy(false);
        }
        gui_odswiez();return true;
    }
    if(e.typ==BWS_ZDARZENIE_MYSZ_UP && g.candidate) {
        bool layoutChanged=false;
        if(g.dragging && g.rubber)odtworz_obrys(g.band);
        else if(g.dragging) {
            obrysy_grupy(true);
            int target=wpis_pulpitu_w_punkcie(e.x,e.y);
            ustaw_hover_drop_pulpitu(-1);
            if(target>=0 && wpisy_pulpitu[target].folder) {
                // Release capture and finish the gesture before entering BWS.
                g=GestPulpitu{};gui_ustaw_capture_myszy(false);
                (void)wykonaj_drop_pulpitu(target);gui_odswiez();return true;
            }
            zapamietaj_zaznaczenie_pulpitu();
            desktop_layout.move_visual_group(g.dx,g.dy);
            if(desktop_settings)for(int i=0;i<desktop_layout.count;++i) {
                const auto& p=desktop_layout.items[i];const auto& old=desktop_previous[i];
                if(p.placed && p.selected && (p.col!=old.col || p.row!=old.row || p.offsetX!=old.offsetX || p.offsetY!=old.offsetY))
                    layoutChanged=desktop_settings->dirty=true;
            }
            // Clear every old location before drawing any new location.
            for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected && desktop_layout.items[i].placed) {
                const auto& old=desktop_previous[i];
                auto r=Desktop::visual(desktop_layout.x0+old.col*desktop_layout.cw+old.offsetX,desktop_layout.y0+old.row*desktop_layout.ch+old.offsetY,old.label,ui_skala_procent);
                gui_rysuj_prostokat(r.x,r.y,r.szerokosc,r.wysokosc,0);
            }
            for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected && desktop_layout.items[i].placed) {
                const auto& old=desktop_previous[i];
                odrysuj_obszar_pulpitu(Desktop::visual(desktop_layout.x0+old.col*desktop_layout.cw+old.offsetX,desktop_layout.y0+old.row*desktop_layout.ch+old.offsetY,old.label,ui_skala_procent));
                RysujWpisPulpitu(i);
            }
            zarejestruj_pulpit_jako_cel_drop();
        } else if(g.item>=0 && !g.ctrl) {
            zapamietaj_zaznaczenie_pulpitu();desktop_layout.click(g.item,false);odrysuj_zaznaczenie_pulpitu();
        }
        g=GestPulpitu{};gui_ustaw_capture_myszy(false);gui_odswiez();if(layoutChanged)zapisz_uklad_pulpitu();return true;
    }
    return false;
}

int menu_glowne_y() {
    int y = screen_h - PASEK_WYS - MENU_WYS;
    if (y < 0) y = 0;
    return y;
}

inline bool prostokat_elementu_menu(int index, int* x, int* y, int* w, int* h) {
    if (index < 0 || index >= MENU_ELEMENTOW) return false;
    if (x) *x = MENU_X;
    if (y) *y = menu_glowne_y() + MENU_GORNY_MARGINES + index * MENU_ELEMENT_WYS;
    if (w) *w = MENU_W;
    if (h) *h = MENU_ELEMENT_WYS;
    return true;
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

    gui_rysuj_prostokat(x, y, MENU_GRY_W, MENU_GRY_WYS, motyw_kolor(MOTYW_KOLOR_PANEL));
    gui_rysuj_prostokat(x, y, MENU_GRY_W, 1, motyw_kolor(MOTYW_KOLOR_AKCENT));
    gui_rysuj_prostokat(x, y, 1, MENU_GRY_WYS, motyw_kolor(MOTYW_KOLOR_AKCENT));
    gui_rysuj_prostokat(x + MENU_GRY_W - 1, y,
                        1, MENU_GRY_WYS, motyw_kolor(MOTYW_KOLOR_RAMKA));
    gui_rysuj_prostokat(x, y + MENU_GRY_WYS - 1,
                        MENU_GRY_W, 1, motyw_kolor(MOTYW_KOLOR_RAMKA));

    const char* gry[MENU_GRY_ELEMENTOW] = {
        "> Snake"
    };

    for (int i = 0; i < MENU_GRY_ELEMENTOW; ++i) {
        const int item_y = y + MENU_GORNY_MARGINES +
                           i * MENU_ELEMENT_WYS;
        gui_wypisz_tekst_kolor(x + 10, item_y + 5,
                               motyw_kolor(MOTYW_KOLOR_TEKST), gry[i]);
    }
}

void rysuj_ikone_aplikacji(const AplikacjaPulpitu& aplikacja) {
    const PulpitGeometriaKomorki g =
        pulpit_geometria_komorki(aplikacja.x, aplikacja.y, ui_skala_procent);
    const int x = g.ikona_x;
    const int y = g.ikona_y;
    switch (aplikacja.typ_ikony) {
        case TypIkonyAplikacji::Notatnik:
            rysuj_fragment_ikony(x,y,0,0,48,48,0x00FFBF00);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x00FFFFFF);
            rysuj_fragment_ikony(x,y,6,8,32,2,0x00000000);
            rysuj_fragment_ikony(x,y,6,14,32,2,0x00000000);
            rysuj_fragment_ikony(x,y,6,20,20,2,0x00000000);
            rysuj_fragment_ikony(x,y,6,26,32,2,0x00000000);
            rysuj_fragment_ikony(x,y,6,32,24,2,0x00000000);
            break;
        case TypIkonyAplikacji::Kalkulator:
            rysuj_fragment_ikony(x,y,0,0,48,48,0x008A5A00);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x001A0B00);
            rysuj_tekst_ikony(x,y,0,10,48,16,0x00FFBF00,"+ -");
            rysuj_tekst_ikony(x,y,0,30,48,16,0x00FFBF00,"* =");
            break;
        case TypIkonyAplikacji::Husarz:
            rysuj_fragment_ikony(x,y,0,0,48,48,0x000078D7);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x000055AA);
            rysuj_tekst_ikony(x,y,0,15,48,16,0x00FFFFFF,"WWW");
            break;
        case TypIkonyAplikacji::Test:
            rysuj_fragment_ikony(x,y,0,0,48,48,0x00CC33CC);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x00881188);
            rysuj_tekst_ikony(x,y,0,15,48,16,0x00FFFFFF,"Test");
            break;
        case TypIkonyAplikacji::Snake:
            rysuj_ikone_paleta(x, y, IKONA_SNAKE_PALETA,
                               IKONA_SNAKE_KOLOROW,
                               IKONA_SNAKE_FRAGMENTY,
                               IKONA_SNAKE_FRAGMENTOW);
            break;
        case TypIkonyAplikacji::Eksplorator:
            rysuj_fragment_ikony(x,y,0,0,48,48,0x00FFBF00);
            rysuj_fragment_ikony(x,y,2,8,44,38,0x00D59600);
            rysuj_fragment_ikony(x,y,6,2,20,10,0x00FFBF00);
            break;
        case TypIkonyAplikacji::Obrazy:
            rysuj_fragment_ikony(x,y,0,0,48,48,0x00301500);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x002080C0);
            rysuj_fragment_ikony(x,y,8,24,18,12,0x00808080);
            rysuj_fragment_ikony(x,y,22,28,14,8,0x00606060);
            rysuj_fragment_ikony(x,y,30,8,8,8,0x00FFD700);
            break;
        case TypIkonyAplikacji::DemoUI:
            // Jedna duza karta + trzy kontrolki: odrebny znak DemoUI.
            rysuj_fragment_ikony(x,y,0,0,48,48,0x00402010);
            rysuj_fragment_ikony(x,y,3,3,42,42,0x00F2E2C4);
            rysuj_fragment_ikony(x,y,7,8,34,5,0x00E58A00);
            rysuj_fragment_ikony(x,y,7,18,22,7,0x008A5A00);
            rysuj_fragment_ikony(x,y,7,30,10,10,0x00FFBF00);
            rysuj_fragment_ikony(x,y,22,30,19,10,0x00301500);
            break;
        case TypIkonyAplikacji::Kontrolki2:
            // Dwie kolumny kontrolek: zachowany, rozpoznawalny znak K2.
            rysuj_fragment_ikony(x,y,0,0,48,48,0x00301500);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x00E58A00);
            rysuj_fragment_ikony(x,y,6,6,36,12,0x00FFBF00);
            rysuj_fragment_ikony(x,y,6,22,16,18,0x00301500);
            rysuj_fragment_ikony(x,y,26,22,16,18,0x00301500);
            break;
        case TypIkonyAplikacji::Kontrolki3:
            // Trzy bursztynowe suwaki na granatowej karcie: odrebny znak K3.
            rysuj_fragment_ikony(x,y,0,0,48,48,0x001C2940);
            rysuj_fragment_ikony(x,y,3,3,42,42,0x00305070);
            rysuj_fragment_ikony(x,y,8,11,32,4,0x00FFBF00);
            rysuj_fragment_ikony(x,y,26,8,7,10,0x00FFFFFF);
            rysuj_fragment_ikony(x,y,8,23,32,4,0x00E58A00);
            rysuj_fragment_ikony(x,y,14,20,7,10,0x00FFFFFF);
            rysuj_fragment_ikony(x,y,8,35,32,4,0x00CC33CC);
            rysuj_fragment_ikony(x,y,31,32,7,10,0x00FFFFFF);
            break;
        case TypIkonyAplikacji::Kontrolki4:
            // Cztery kolorowe kafle ukladu (siatka/panele): rozpoznawalny znak K4.
            rysuj_fragment_ikony(x,y,0,0,48,48,0x001A243B);
            rysuj_fragment_ikony(x,y,3,3,42,42,0x00273A5C);
            rysuj_fragment_ikony(x,y,7,7,14,14,0x00FFBF00);
            rysuj_fragment_ikony(x,y,27,7,14,14,0x0020A040);
            rysuj_fragment_ikony(x,y,7,27,14,14,0x002070D0);
            rysuj_fragment_ikony(x,y,27,27,14,14,0x00E58A00);
            break;
        case TypIkonyAplikacji::Aktualizacje:
            // Tarcza aktualizacji ze strzalka w dol i podstawka OTA.
            rysuj_fragment_ikony(x,y,0,0,48,48,0x001B2838);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x002D4A6E);
            rysuj_fragment_ikony(x,y,8,36,32,4,0x00E58A00);
            rysuj_fragment_ikony(x,y,21,10,6,16,0x00FFBF00);
            rysuj_fragment_ikony(x,y,15,22,18,4,0x00FFBF00);
            rysuj_fragment_ikony(x,y,18,26,12,4,0x00FFBF00);
            rysuj_fragment_ikony(x,y,21,30,6,4,0x00FFBF00);
            break;
        case TypIkonyAplikacji::Motywy:
            // Paleta kolorow motywu (4 probniki barwne w ramce)
            rysuj_fragment_ikony(x,y,0,0,48,48,0x0020242A);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x00262B31);
            rysuj_fragment_ikony(x,y,6,6,16,16,0x00F2A900);
            rysuj_fragment_ikony(x,y,26,6,16,16,0x0035B7C8);
            rysuj_fragment_ikony(x,y,6,26,16,16,0x0051B87A);
            rysuj_fragment_ikony(x,y,26,26,16,16,0x00E85D5D);
            break;
    }


}

void RysujPulpit(bool wymus_pelne_odswiezenie) {
    if (wymus_pelne_odswiezenie) {
        // Dla procesu warstwowego BWS 19 zeruje prywatna warstwe.
        gui_odswiez_pulpit();
    }

    RysujTaskbar();


    RysujWpisyPulpitu();


    if (menu_start_otwarte) {
        const int menu_y = menu_glowne_y();

        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, MENU_WYS, motyw_kolor(MOTYW_KOLOR_PANEL));
        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, 1, motyw_kolor(MOTYW_KOLOR_AKCENT));
        gui_rysuj_prostokat(MENU_X, menu_y, 1, MENU_WYS, motyw_kolor(MOTYW_KOLOR_AKCENT));
        gui_rysuj_prostokat(MENU_X + MENU_W - 1, menu_y,
                            1, MENU_WYS, motyw_kolor(MOTYW_KOLOR_RAMKA));
        gui_rysuj_prostokat(MENU_X, menu_y + MENU_WYS - 1,
                            MENU_W, 1, motyw_kolor(MOTYW_KOLOR_RAMKA));

        const char* menu_elementy[MENU_ELEMENTOW] = {
            "> Powloka Bursztyna",
            "> Eksplorator Plików",
            "> Notatnik",
            "> Kalkulator",
            "> Przegladarka Husarz",
            "> Przeglądarka Obrazów",
            "> Demo BursztynUI",
            "> Kontrolki 2",
            "> Kontrolki 3",
            "> Kontrolki 4",
            "> Testy systemu",
            "> Tapety",
            "> Ustawienia ekranu",
            "> Aktualizacje Systemu",
            "> Motywy Systemowe",
            "> Gry ->",
            "> Uruchom ponownie",
            "> Zamknij"
        };

        for (int i = 0; i < MENU_ELEMENTOW; ++i) {
            int item_x = 0, item_y = 0, item_w = 0, item_h = 0;
            prostokat_elementu_menu(i, &item_x, &item_y, &item_w, &item_h);

            if (i == MENU_GRY_INDEX && menu_gry_otwarte) {
                gui_rysuj_prostokat(item_x + 2, item_y,
                                    item_w - 4, item_h,
                                    motyw_kolor(MOTYW_KOLOR_PODNIESIONY));
            }

            gui_wypisz_tekst_kolor(item_x + 10, item_y + 5,
                                   motyw_kolor(MOTYW_KOLOR_TEKST), menu_elementy[i]);
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

    if (tekst_rowny(sciezka, SCIEZKA_AKTUALIZACJE)) {
#if BURSZTYN_OTA_TRACE
        bws_wywolaj(1, (uint64_t)"[OTA TRACE] Launching aktualizacje app\n", 0, 0, 0);
#endif
    }

    zamknij_menu_i_odtworz_pulpit();

    // Nie zerujemy globalnego trybu Ring 3 przed aplikacja GUI. Obecna
    // implementacja w grafika.cpp uzywa jednej globalnej flagi, a aplikacje
    // GUI i tak ustawiaja ten tryb na true po starcie.
    gui_ustaw_przejecie_myszy(true);
    bws_wywolaj(10, (uint64_t)sciezka);
}

void otworz_wpis_pulpitu(int indeks) {
    if(indeks<0 || indeks>=desktop_layout.count)return;
    if(auto* launcher=DesktopEntries::launcher(wpisy_pulpitu[indeks].launcher)) {
        uruchom_lub_aktywuj(launcher->path,launcher->title);return;
    }
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

bool obsluz_klik_wpisu_pulpitu(const bws_zdarzenie& e,int& last,uint64_t& lastTime) {
    const int entry=wpis_pulpitu_w_punkcie(e.x,e.y);
    if(entry<0)return false;
    bool doubleClick=!(desktop_modifiers&BWS_MOD_CTRL) && entry==last && podwojny_klik(lastTime,e.timestamp);
    (void)obsluz_gest_pulpitu(e);last=entry;lastTime=e.timestamp;
    if(doubleClick) {anuluj_gest_pulpitu();otworz_wpis_pulpitu(entry);last=-1;lastTime=0;}
    return true;
}

void zamknij_pulpit_context() {
    if(pulpit_menu)pulpit_menu->zamknij();
    pulpit_context_open=false;zarejestruj_pulpit_jako_cel_drop();gui_odswiez();
}
bool otworz_pulpit_context(int x,int y,int wpis) {
    zamknij_pulpit_context();pulpit_cel[0]=0;pulpit_cel_folder=false;
    bool selected=wpis>=0 && wpis<liczba_wpisow_pulpitu;
    if(selected) {
        if(!desktop_layout.items[wpis].selected)desktop_layout.click(wpis,false);
        zaznaczony_wpis_pulpitu=wpis;
        if(wpisy_pulpitu[wpis].launcher<0)selected=sciezka_wpisu_pulpitu(wpis,pulpit_cel,sizeof(pulpit_cel));
        pulpit_cel_folder=wpisy_pulpitu[wpis].folder;
    }
    BwsSchowekPlikow record{};
    PlikiUI::CelMenu target{selected,pulpit_cel_folder,PlikiUI::tekstowy(pulpit_cel,pulpit_cel_folder),
        selected && konczy_sie(pulpit_cel,".bur"),PlikiUI::schowek_gotowy(&record),static_cast<unsigned>(desktop_layout.selected_count()),false};
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected && wpisy_pulpitu[i].launcher>=0)target.protected_launcher=true;
    BursztynUI::Prostokat work{0,0,screen_w,screen_h-PASEK_WYS};
    pulpit_menu->events.geometria(work,work);pulpit_menu->otworz(x,y,target,true);
    pulpit_context_open=pulpit_menu->menu.czy_otwarte();
    (void)gui_rejestruj_cele_drop(nullptr,0);gui_odswiez();return pulpit_context_open;
}
bool obsluz_prawy_klik_pulpitu(const bws_zdarzenie& e) {
    if(e.typ!=BWS_ZDARZENIE_MYSZ_PRAWY_DOWN)return false;
    const int target=wpis_pulpitu_w_punkcie(e.x,e.y);
    zapamietaj_zaznaczenie_pulpitu();
    if(target>=0 && !desktop_layout.items[target].selected)desktop_layout.click(target,false);
    if(target<0)desktop_layout.clear_selection();
    zaznaczony_wpis_pulpitu=target;
    odrysuj_zaznaczenie_pulpitu();
    if(e.y<screen_h-PASEK_WYS)otworz_pulpit_context(e.x,e.y,target);
    return true;
}
void wykonaj_pulpit_context(PulpitContextAction action) {
    const int selected=zaznaczony_wpis_pulpitu;
    bool protectedSelection=false;
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected && wpisy_pulpitu[i].launcher>=0)protectedSelection=true;
    zamknij_pulpit_context();
    if(protectedSelection && action!=PulpitContextAction::OPEN && action!=PulpitContextAction::NEW_FILE &&
        action!=PulpitContextAction::NEW_FOLDER && action!=PulpitContextAction::REFRESH)return;
    switch(action) {
    case PulpitContextAction::NEW_FILE:otworz_pulpit_modal(PulpitModalTyp::PLIK);return;
    case PulpitContextAction::NEW_FOLDER:otworz_pulpit_modal(PulpitModalTyp::FOLDER);return;
    case PulpitContextAction::REMOVE:
        if(!zapisz_batch_pulpitu())return;
        opis_batch("Usunąć trwale elementy: ",pulpit_batch_count,pulpit_batch_count);
        otworz_pulpit_modal(PulpitModalTyp::REMOVE,pulpit_batch_count==1?pulpit_cel:pulpit_batch_message);return;
    case PulpitContextAction::RENAME:otworz_pulpit_modal(PulpitModalTyp::RENAME);return;
    case PulpitContextAction::MOVE:otworz_pulpit_modal(PulpitModalTyp::MOVE);return;
    case PulpitContextAction::OPEN_WITH:otworz_pulpit_modal(PulpitModalTyp::OPEN_WITH);return;
    case PulpitContextAction::NOTEPAD:PlikiUI::notatnik(pulpit_cel,pulpit_cel_folder);break;
    case PulpitContextAction::OPEN:otworz_wpis_pulpitu(selected);break;
    case PulpitContextAction::COPY:case PulpitContextAction::CUT:
        if(!zapisz_batch_pulpitu() || !ustaw_schowek_wielu_plikow(pulpit_batch,pulpit_batch_count,action==PulpitContextAction::COPY?BWS_SCHOWEK_COPY:BWS_SCHOWEK_CUT))
            otworz_pulpit_modal(PulpitModalTyp::NOTICE,"Nie skopiowano zestawu do schowka: brak dostępu do co najmniej jednego elementu.");
        pulpit_batch_count=0;break;
    case PulpitContextAction::PASTE: {
        PlikiUI::RaportWklejenia report;
        auto result=PlikiUI::wklej(SCIEZKA_PULPIT,&report);
        odswiez_pulpit_natychmiast();
        if(result!=PlikiUI::WynikWklejenia::OK) {
            opis_batch("Wklejono ",report.wykonane,report.wszystkie,report.blad);
            otworz_pulpit_modal(PulpitModalTyp::NOTICE,pulpit_batch_message);return;
        }break;
    }
    case PulpitContextAction::REFRESH:odswiez_pulpit_natychmiast();break;
    default:break;
    }
    gui_odswiez();
}

bool obsluz_popupy_pulpitu(const bws_zdarzenie& zdarzenie) {
    if(pulpit_modal_typ==PulpitModalTyp::MOVE && pulpit_wybor->events.zdarzenie(zdarzenie)) {
        if(pulpit_wybor->wynik_pliku()==BursztynUI::WynikDialoguPliku::Wybrano)zatwierdz_pulpit_modal();
        else if(!pulpit_wybor->czy_otwarta()){pulpit_batch_count=0;zamknij_pulpit_modal();}
        if(pulpit_wybor->events.wymaga_odswiezenia())gui_odswiez();
        return true;
    }
    if(pulpit_dialog->zdarzenie(zdarzenie)) {
        if(pulpit_dialog->wynik()==1)zatwierdz_pulpit_modal();
        else if(pulpit_dialog->wynik()==2 || !pulpit_dialog->czy_otwarta()){pulpit_batch_count=0;zamknij_pulpit_modal();}
        if(pulpit_dialog->events.wymaga_odswiezenia())gui_odswiez();
        return true;
    }
    if(pulpit_menu->events.zdarzenie(zdarzenie)) {
        auto action=pulpit_menu->pobierz_akcje();pulpit_context_open=pulpit_menu->menu.czy_otwarte();
        if(action!=PulpitContextAction::NONE)wykonaj_pulpit_context(action);
        if(!pulpit_context_open)zarejestruj_pulpit_jako_cel_drop();
        if(pulpit_menu->events.wymaga_odswiezenia())gui_odswiez();
        return true;
    }
    return false;
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
    uruchom_lub_aktywuj(SCIEZKA_POWLOKA, "Powłoka Bursztynowa");
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
            uruchom_lub_aktywuj(SCIEZKA_DEMO_UI,
                                "BursztynUI -");
            break;
        case 7:
            uruchom_lub_aktywuj(SCIEZKA_KONTROLKI2,
                                "BursztynUI Kontrolki 2");
            break;
        case 8:
            uruchom_lub_aktywuj(SCIEZKA_KONTROLKI3,
                                "Kontrolki 3");
            break;
        case 9:
            uruchom_lub_aktywuj(SCIEZKA_KONTROLKI4,
                                "Kontrolki 4");
            break;
        case 10:
            uruchom_lub_aktywuj(SCIEZKA_TEST, "Testy Bursztyn OS");
            break;
        case 11:
            uruchom_lub_aktywuj("/programy/tapety.cebula/tapety.bur", "Tapety");
            break;
        case 12:
            uruchom_lub_aktywuj(EKRAN_PROGRAM,"Ustawienia ekranu");
            break;
        case 13:
            uruchom_lub_aktywuj(SCIEZKA_AKTUALIZACJE, "Aktualizacje Systemu");
            break;
        case 14:
            uruchom_lub_aktywuj(SCIEZKA_MOTYWY, "Motywy");
            break;
        case MENU_GRY_INDEX:
            menu_gry_otwarte = !menu_gry_otwarte;
            aktualizuj_overlay_menu();
            RysujPulpit(true);
            break;
        case 16:
            wykonaj_restart();
            break;
        case 17:
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
                            PRZYCISK_MENU_X, przycisk_menu_y(),
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
#if BURSZTYN_DIAG_BOOT
        static uint32_t s_menu_hit_log_cnt = 0;
        if (s_menu_hit_log_cnt < 30) {
            ++s_menu_hit_log_cnt;
            int it0_x = 0, it0_y = 0, it0_w = 0, it0_h = 0;
            prostokat_elementu_menu(0, &it0_x, &it0_y, &it0_w, &it0_h);
            const bool in_item0 = punkt_w_prostokacie(mx, my, it0_x, it0_y, it0_w, it0_h);
            const bool in_menu = punkt_w_prostokacie(mx, my, MENU_X, menu_y, MENU_W, MENU_WYS);
            char hbuf[256]{};
            char* hp = hbuf;
            char* hep = hbuf + sizeof(hbuf);
            auto happ = [&](const char* s) { while (*s && hp + 1 < hep) *hp++ = *s++; };
            auto happ_i = [&](int64_t v) {
                if (v < 0) { if (hp + 1 < hep) *hp++ = '-'; v = -v; }
                char b[24]; int idx = 0;
                do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
                while (idx > 0 && hp + 1 < hep) *hp++ = b[--idx];
            };
            happ("[MENU HIT]\n  menu=("); happ_i(MENU_X); happ(","); happ_i(menu_y);
            happ(")-("); happ_i(MENU_X + MENU_W); happ(","); happ_i(menu_y + MENU_WYS);
            happ(")\n  item=\"Powłoka Bursztyna\"\n  itemrect=(");
            happ_i(it0_x); happ(","); happ_i(it0_y);
            happ(")-("); happ_i(it0_x + it0_w); happ(","); happ_i(it0_y + it0_h);
            happ(")\n  click=("); happ_i(mx); happ(","); happ_i(my);
            happ(")\n  hit=");
            if (in_item0) happ("YES\n");
            else if (in_menu) happ("NO (inside menu, other item)\n");
            else happ("NO (outside menu)\n");
            *hp = '\0';
            wypisz(hbuf);
        }

#endif
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

    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        if (okna_taskbara[i].tytul[0] == '\0' ||
            task_x + TASK_BUTTON_W > screen_w - ui_skaluj(150,ui_skala_procent)) continue;
        if (punkt_w_prostokacie(mx, my, task_x, screen_h - ui_skaluj(36,ui_skala_procent),
                                TASK_BUTTON_W, SKROT_H)) {
            (void)gui_aktywuj_okno(okna_taskbara[i].window_id);
            return;
        }
        task_x += TASK_BUTTON_W + TASK_BUTTON_GAP;
    }
}

void rozmiesc_ikony_ekranu(){
    PULPIT_GRID_Y=ui_skaluj(3,ui_skala_procent);
    aktualizuj_layout_pulpitu();
}


} // namespace

static void wm_log(const char* s) {
    wypisz(s);
}

[[maybe_unused]] static void wm_log_i32(const char* prefix, int32_t val, const char* suffix = "\n") {
    char buf[64];
    size_t p = 0;
    while (*prefix && p + 1 < sizeof(buf)) buf[p++] = *prefix++;
    if (val < 0) {
        if (p + 1 < sizeof(buf)) buf[p++] = '-';
        val = -val;
    }
    char tmp[16];
    int tp = 0;
    if (val == 0) tmp[tp++] = '0';
    else {
        while (val > 0) {
            tmp[tp++] = '0' + (val % 10);
            val /= 10;
        }
    }
    while (tp > 0 && p + 1 < sizeof(buf)) buf[p++] = tmp[--tp];
    while (*suffix && p + 1 < sizeof(buf)) buf[p++] = *suffix++;
    buf[p] = '\0';
    wypisz(buf);
}

#if BURSZTYNDIAGABSMAP
static void wm_abs_geometry(const char* cause) {
    char b[160]{};unsigned n=0;
    auto text=[&](const char* p){while(*p&&n+1<sizeof(b))b[n++]=*p++;};
    auto num=[&](unsigned v){char t[12];unsigned i=0;do{t[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(b))b[n++]=t[--i];};
    text("[ABS WM] cause=");text(cause);text(" wm=");num(screen_w);text("x");num(screen_h);
    text(" scale=");num(ui_skala_procent);text("\n");bws_wywolaj(1,(uint64_t)b,0,0,0);
}
#endif

extern "C" __attribute__((noreturn)) void _start() {
    pulpit_menu=new PlikiUI::MenuPlikowe();
    pulpit_dialog=new PlikiUI::DialogPlikowy();
#if BURSZTYN_DIAG_BOOT
    wm_log("[WM-BOOT] entered-ring3\n");
    wm_log("[WM] _start begun\n");
#endif
    GuiMonitorInfo mon{};if(gui_pobierz_monitor(0,&mon))ui_skala_procent=mon.scale_x1000/10;
    metryki_ekranu();
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
#if BURSZTYNDIAGABSMAP
    wm_abs_geometry("startup");
#endif
    zaktualizuj_cache_przycisku(ui_skala_procent);
#if BURSZTYN_DIAG_BOOT
    wm_log_i32("[WM] screen_w=", screen_w);
    wm_log_i32("[WM] screen_h=", screen_h);
    wm_log_i32("[WM] PASEK_WYS=", PASEK_WYS);
#endif

    if (screen_w <= 0 || screen_h <= PASEK_WYS) {
        wm_log("[WM] BLAD: niepoprawne wymiary ekranu!\n");
        gui_zakoncz_aplikacje();
    }

    /* Tylko zaufany manager z PRAWO_SYSTEM_CONFIG publikuje work area.
       Aplikacje nie musza znac wysokosci taskbara. */
    if (!gui_ustaw_obszar_roboczy(
            0, 0, screen_w, screen_h - PASEK_WYS)) {
        wm_log("[WM] BLAD: gui_ustaw_obszar_roboczy zwrocil false!\n");
        gui_zakoncz_aplikacje();
    }
#if BURSZTYN_DIAG_BOOT
    wm_log("[WM] gui_ustaw_obszar_roboczy OK\n");
#endif

    // Pulpit jest warstwa tla. Aplikacje GUI uzywaja wyzszego z_order.
    const int root_layer_id = bws_utworz_warstwe(0, 0, screen_w, screen_h, 0);
#if BURSZTYN_DIAG_BOOT
    wm_log_i32("[WM] bws_utworz_warstwe id=", root_layer_id);
#endif
    if (root_layer_id < 0) {
        wm_log("[WM] BLAD: bws_utworz_warstwe zwrocil < 0!\n");
        gui_zakoncz_aplikacje();
    }
#if BURSZTYN_DIAG_BOOT
    wm_log("[WM-BOOT] root-layer-created\n");
#endif

    gui_ustaw_przejecie_myszy(true);
#if BURSZTYN_DIAG_BOOT
    wm_log("[WM] gui_ustaw_przejecie_myszy(true) OK\n");
#endif

    rozmiesc_ikony_ekranu();

    // Zasiej stan zegara, aby pierwsza iteracja nie wymuszala od razu
    // dodatkowej, niepotrzebnej kompozycji calej klatki.
    (void)pobierz_i_zaktualizuj_czas();
    odswiez_snapshot_okien();
    (void)odswiez_wpisy_pulpitu();

#if BURSZTYN_DIAG_BOOT
    const int desktop_icon_count = liczba_wpisow_pulpitu;
    wm_log_i32("[BWS] desktop_icons=", desktop_icon_count);
    wm_log("[BWS] taskbar visible=1\n");
#endif

#if BURSZTYN_DIAG_BOOT
    wm_log("[WM-BOOT] desktop-render-start\n");
#endif
    RysujPulpit(true);
    zapisz_uklad_pulpitu();
#if BURSZTYN_DIAG_BOOT
    wm_log("[WM-BOOT] desktop-render-complete\n");
#endif
    // The model flag is set only after launchers and saved/default positions
    // are prepared. Filesystem enumeration failure is retried by the WM timer.
    bool start_pulpitu_gotowy=false;
    if(hash_pulpitu_wazny) {
        start_pulpitu_gotowy=gui_potwierdz_gotowosc_pulpitu();
#if BURSZTYN_DIAG_BOOT
        wm_log_i32("[WM-BOOT] handshake-result=",start_pulpitu_gotowy);
#endif
        if(start_pulpitu_gotowy)wm_log("[WM] Initial Desktop ready\n");
    }

    int last_desktop_click = -1;
    uint64_t last_desktop_click_time = 0;

    while (true) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        if(zdarzenie.typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY)desktop_modifiers=zdarzenie.przyciski;
        if(zdarzenie.typ==BWS_ZDARZENIE_BLUR)obsluz_gest_pulpitu(zdarzenie);
        if(zdarzenie.typ==BWS_ZDARZENIE_EKRAN){
            zamknij_pulpit_context();zamknij_pulpit_modal();
            menu_start_otwarte=false;menu_gry_otwarte=false;anuluj_gest_pulpitu();
            // Pola zdarzenia (dx/dy/kod) pochodza z chwili wyslania i moga byc
            // nieaktualne, gdy kolejka zdarzen jest przetwarzana z opoznieniem
            // (np. szybkie sekwencje modesetow). Zapis obszaru roboczego ze
            // starych wartosci nadpisywalby swiezy stan BWS62 opublikowany
            // synchronicznie przez usluge ekranu i powodowalby clipping okien.
            // Zrodlem prawdy jest biezacy stan jadra; zdarzenie to tylko budzik.
            GuiMonitorInfo mon_now{};
            if(gui_pobierz_monitor(0,&mon_now)&&mon_now.szer>0&&mon_now.wys>0){
                if(ui_skala_poprawna(mon_now.scale_x1000/10))ui_skala_procent=mon_now.scale_x1000/10;
                else if(ui_skala_poprawna(zdarzenie.kod))ui_skala_procent=zdarzenie.kod;
                screen_w=mon_now.szer;screen_h=mon_now.wys;
            }else{
                if(ui_skala_poprawna(zdarzenie.kod))ui_skala_procent=zdarzenie.kod;
                screen_w=zdarzenie.dx;screen_h=zdarzenie.dy;
            }
#if BURSZTYNDIAGABSMAP
            wm_abs_geometry("BWS-display-event");
#endif
            metryki_ekranu();
            zaktualizuj_cache_przycisku(ui_skala_procent);
            if(screen_w>0&&screen_h>PASEK_WYS)
                gui_ustaw_obszar_roboczy(0, 0, screen_w, screen_h - PASEK_WYS);
            rozmiesc_ikony_ekranu();zarejestruj_pulpit_jako_cel_drop();aktualizuj_overlay_menu();RysujPulpit(true);zapisz_uklad_pulpitu();continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) {
            BursztynUI::Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
            pulpit_menu->events.popups.zaktualizuj_motyw();
            pulpit_dialog->events.popups.zaktualizuj_motyw();
            if(pulpit_wybor)pulpit_wybor->events.popups.zaktualizuj_motyw();
            g_cached_skala = 0;
            zaktualizuj_cache_przycisku(ui_skala_procent);
            RysujPulpit(true);
            RysujTaskbar();
            gui_odswiez();
            continue;
        }
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

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH) {
            const bool hover_now = punkt_w_prostokacie(mx, my,
                                                        PRZYCISK_MENU_X, przycisk_menu_y(),
                                                        PRZYCISK_MENU_W, PRZYCISK_MENU_H);
            if (hover_now != menu_przycisk_hover) {
                menu_przycisk_hover = hover_now;
                RysujPrzyciskMenu();
                gui_odswiez();
            }
        } else if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
            if (punkt_w_prostokacie(mx, my,
                                    PRZYCISK_MENU_X, przycisk_menu_y(),
                                    PRZYCISK_MENU_W, PRZYCISK_MENU_H)) {
                if (!menu_przycisk_wcisniety) {
                    menu_przycisk_wcisniety = true;
                    RysujPrzyciskMenu();
                    gui_odswiez();
                }
            }
        } else if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP) {
            if (menu_przycisk_wcisniety) {
                menu_przycisk_wcisniety = false;
                RysujPrzyciskMenu();
                gui_odswiez();
            }
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_BLUR) {
            zamknij_pulpit_context();
            zamknij_pulpit_modal();
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_PLIKI_ZMIENIONE) {
            zamknij_pulpit_context();
            /* Pelne przerysowanie warstwy nadpisaloby piksele otwartego
               dialogu; odswiezamy dopiero po jego zamknieciu. */
            if (!desktop_gesture.candidate && pulpit_modal_typ==PulpitModalTyp::BRAK && odswiez_wpisy_pulpitu())
                RysujPulpit(true);
            continue;
        }

        if(obsluz_popupy_pulpitu(zdarzenie))continue;

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) {
            if (menu_start_otwarte) zamknij_menu_i_odtworz_pulpit();
            anuluj_gest_pulpitu();
            last_desktop_click=-1;last_desktop_click_time=0;
            gui_ustaw_capture_myszy(false);
            (void)obsluz_prawy_klik_pulpitu(zdarzenie);
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

        if(zdarzenie.typ!=BWS_ZDARZENIE_MYSZ_DOWN && obsluz_gest_pulpitu(zdarzenie)) {
            if(desktop_gesture.dragging){last_desktop_click=-1;last_desktop_click_time=0;}continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_TIMER) {
            zarejestruj_pulpit_jako_cel_drop();
            if (!desktop_gesture.candidate && !pulpit_context_open && pulpit_modal_typ==PulpitModalTyp::BRAK &&
                ++pulpit_refresh_ticks >= PULPIT_REFRESH_TICKS) {
                pulpit_refresh_ticks = 0;
                if (odswiez_wpisy_pulpitu()) RysujPulpit(true);
                if(!start_pulpitu_gotowy&&hash_pulpitu_wazny) {
                    RysujPulpit(true);
                    start_pulpitu_gotowy=gui_potwierdz_gotowosc_pulpitu();
                }
            }
        }

        // Zegar jest rysowany bezpośrednio w RysujTaskbar(). Gdy czas
        // się zmieni, odrysowujemy pasek zadań i odświeżamy klatkę.
        if (pobierz_i_zaktualizuj_czas()) {
            RysujTaskbar();
            gui_odswiez();
        }

        if (klik) {
            if (menu_start_otwarte || punkt_w_prostokacie(mx, my,
                                    PRZYCISK_MENU_X, przycisk_menu_y(),
                                    PRZYCISK_MENU_W, PRZYCISK_MENU_H)) {
                last_desktop_click = -1;
                last_desktop_click_time = 0;
                obsluz_klikniecie(mx, my);
                continue;
            }
            if(obsluz_klik_wpisu_pulpitu(zdarzenie,last_desktop_click,last_desktop_click_time))continue;
            last_desktop_click = -1;
            last_desktop_click_time = 0;
            if(!obsluz_gest_pulpitu(zdarzenie))obsluz_klikniecie(mx, my);
        }

    }
}
