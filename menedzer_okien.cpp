#include "app_identity.h"
#include "ui_skala.h"
#include "ekran_format.h"
#include "pulpit_geometria.h"
#include "desktop_layout.h"
#include "desktop_selection_paint.h"
#include "desktop_entries.h"
#include "desktop_layout_settings.h"
#include "obrazy/png_dekoder.h"
/*
 * Menedzer Okien (Pulpit i Pasek Zadan) dla Bursztyn OS
 * Wersja uporzadkowana: poprawne hitboxy, Menu Start i odswiezanie zegara.
 */

#include "bursztyn_gui.h"
#include "plik_popup_ui.h"
#include "start_menu_ui.h"
#include "taskbar_clock.h"

// Opt-in native repaint profiling. No clock reads or extra paints in release.
#ifndef BSF_PROFILE
#define BSF_PROFILE 0
#endif

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
        0x1000ULL, 0x4c000ULL, 0x1001000ULL,
        0x4d000ULL, 0x20000ULL, 0x104d000ULL
    };
}

namespace {

int screen_w = 1024;
int screen_h = 768;
int PASEK_WYS = 40;
int MENU_X = 10;
DisplayGeometry desktop_geometry{};
TaskbarClock::Surface* taskbar_clock=nullptr;
int poczatek_zegara(){return taskbar_clock?taskbar_clock->bounds.x:screen_w-ui_skaluj(150,ui_skala_procent);}
StartMenu::Surface* start_menu=nullptr;

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
constexpr int PULPIT_MAX_WPISOW = DesktopEntries::ModelCapacity;
// Match the established BWS/Explorer enumeration budget; not a model increase.
constexpr int PULPIT_LIST_CAP = 64*1024;
constexpr int PULPIT_NAZWA_CAP = BWS_DROP_SCIEZKA_MAX; // display capacity; PSF validates mutations
int PULPIT_GRID_W = 120;
int PULPIT_GRID_H = 100;
int PULPIT_GRID_Y = 3;
constexpr int PULPIT_REFRESH_TICKS = 3;


void metryki_ekranu(){
if(desktop_geometry.width!=screen_w || desktop_geometry.height!=screen_h || desktop_geometry.scale!=ui_skala_procent)
    desktop_geometry=display_geometry(screen_w,screen_h,ui_skala_procent,0);
PASEK_WYS=desktop_geometry.taskbar_height;
MENU_X=ui_skaluj(10,ui_skala_procent);
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
PULPIT_GRID_Y=ui_skaluj(3,ui_skala_procent);
if(!taskbar_clock)taskbar_clock=new TaskbarClock::Surface;
if(taskbar_clock)taskbar_clock->configure(screen_w,screen_h,PASEK_WYS,ui_skala_procent);
}

inline int przycisk_menu_y() {
    return desktop_geometry.taskbar_y + (PASEK_WYS - PRZYCISK_MENU_H) / 2;
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
const char* const SCIEZKA_TEST_DZWIEKU =
    "/programy/test_dzwieku.cebula/test_dzwieku.bur";
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

static BursztynUI::KontekstRysowania* pulpit_canvas=nullptr;
static void pulpit_prostokat(int x,int y,int w,int h,uint32_t color) {
    if(pulpit_canvas)pulpit_canvas->rysuj_prostokat({x,y,w,h},color);
    else gui_rysuj_prostokat(x,y,w,h,color);
}
static void pulpit_tekst(int x,int y,int w,int h,int scale,uint32_t color,const char* text) {
    if(pulpit_canvas)pulpit_canvas->rysuj_tekst_wysrodkowany({x,y,w,h},text,color,SkalaUI::procent(scale));
    else rysuj_tekst_wysrodkowany(x,y,w,h,scale,color,text);
}

static void rysuj_fragment_ikony(int x, int y, int lx, int ly, int lw, int lh,
                                 uint32_t kolor) {
    const int x0 = ui_skaluj(lx, ui_skala_procent);
    const int y0 = ui_skaluj(ly, ui_skala_procent);
    const int x1 = ui_skaluj(lx + lw, ui_skala_procent);
    const int y1 = ui_skaluj(ly + lh, ui_skala_procent);
    if (x1 > x0 && y1 > y0)
        pulpit_prostokat(x + x0, y + y0, x1 - x0, y1 - y0, kolor);
}

static void rysuj_tekst_ikony(int x, int y, int lx, int ly, int lw, int lh,
                              uint32_t kolor, const char* tekst) {
    const int x0 = ui_skaluj(lx, ui_skala_procent);
    const int y0 = ui_skaluj(ly, ui_skala_procent);
    const int x1 = ui_skaluj(lx + lw, ui_skala_procent);
    const int y1 = ui_skaluj(ly + lh, ui_skala_procent);
    pulpit_tekst(x + x0, y + y0, x1 - x0, y1 - y0,
                             int(ui_skala_procent), kolor, tekst);
}

static void rysuj_etykiete_pulpitu(int x, int y, int w, int h, int skala,
                                   uint32_t kolor, const char* tekst, bool selected=false) {
    if (!tekst || !tekst[0] || w <= 0 || h <= 0) return;
    // Tylko zwykla etykieta potrzebuje jednego cienia; zaznaczenie zapewnia kontrast.
    if(!selected)pulpit_tekst(x + 1, y + 1, w, h, skala, KANONICZNY_KOLOR_CIENIA_PULPITU, tekst);
    pulpit_tekst(x, y, w, h, skala, kolor, tekst);
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
char* lista_pulpitu = nullptr;
int liczba_wpisow_pulpitu = 0;
int pulpit_nadmiar_modelu = 0;
int zaznaczony_wpis_pulpitu = -1; // context anchor, selection lives in desktop_layout
Desktop::Layout<PULPIT_MAX_WPISOW> desktop_layout;
static Desktop::Prostokat desktop_pending_dirty;
static uint64_t desktop_model_generation=0;
static Desktop::Position desktop_previous[PULPIT_MAX_WPISOW];
static DesktopSettings::State* desktop_settings=nullptr;
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
uint64_t hash_modelu_pulpitu = 0;
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
bool odrysuj_transakcje_pulpitu();
void aktualizuj_layout_modelu(Desktop::Layout<PULPIT_MAX_WPISOW>&,WpisPulpitu*,bool);
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
void opis_batch(const char* action,unsigned done,unsigned total,const char* failed=nullptr,const char* reason=nullptr) {
    char a[12],b[12];liczba_do_tekstu(a,done);liczba_do_tekstu(b,total);
    const char* parts[]={action,a," / ",b,failed?". Zatrzymano na: ":"",failed?failed:"",reason?". ":"",reason?reason:""};int n=0;
    for(auto part:parts)for(int i=0;part[i] && n<int(sizeof(pulpit_batch_message))-1;++i)pulpit_batch_message[n++]=part[i];
    pulpit_batch_message[n]=0;
}
bool zapisz_batch_pulpitu() {
    pulpit_batch_count=0;
    if(!pulpit_batch)pulpit_batch=new char[PULPIT_MAX_WPISOW][BWS_DROP_SCIEZKA_MAX]{};
    if(!pulpit_batch)return false;
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].selected && desktop_layout.items[i].placed) {
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
    (void)odswiez_wpisy_pulpitu();
    (void)odrysuj_transakcje_pulpitu();zapisz_uklad_pulpitu();
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
    WynikPlikowy wynik=WynikPlikowy::Blad;
    if(type==PulpitModalTyp::PLIK || type==PulpitModalTyp::FOLDER)
        {wynik=PlikiUI::utworz_element_wynik(SCIEZKA_PULPIT,text,type==PulpitModalTyp::FOLDER);ok=wynik==WynikPlikowy::Ok;}
    else if(type==PulpitModalTyp::REMOVE) {
        if(pulpit_batch_count) {
            unsigned done=0;
            for(;done<pulpit_batch_count;++done) {wynik=usun_drzewo_uzytkownika(pulpit_batch[done]);if(wynik!=WynikPlikowy::Ok)break;}
            ok=done==pulpit_batch_count;
            if(!ok)opis_batch("Usunięto ",done,pulpit_batch_count,pulpit_batch[done],PlikiUI::komunikat_bledu(wynik));
        } else {wynik=usun_drzewo_uzytkownika(pulpit_cel);ok=wynik==WynikPlikowy::Ok;}
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
        (void)otworz_pulpit_modal(PulpitModalTyp::NOTICE,(type==PulpitModalTyp::REMOVE || type==PulpitModalTyp::PLIK || type==PulpitModalTyp::FOLDER) ? (pulpit_batch_count?pulpit_batch_message:PlikiUI::komunikat_bledu(wynik)):"Nie wykonano: konflikt nazwy, nieprawidłowa nazwa lub brak uprawnień.");
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
int porownaj_nazwy_pulpitu(const char* a,const char* b) {
    while(*a && *a==*b){++a;++b;}
    return int(static_cast<unsigned char>(*a))-int(static_cast<unsigned char>(*b));
}
bool odswiez_wpisy_pulpitu() {
    // Keep display buffers off the Ring 3 stack and within BUR's static data
    // budget. They are allocated once, never from the pointer-motion path.
    if(!wpisy_pulpitu)wpisy_pulpitu=new WpisPulpitu[PULPIT_MAX_WPISOW]{};
    if(!lista_pulpitu)lista_pulpitu=new char[PULPIT_LIST_CAP]{};
    if(!wpisy_pulpitu || !lista_pulpitu)return false;
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
    if(!desktop_settings) {
        desktop_settings=new DesktopSettings::State{};
        if(desktop_settings)(void)desktop_settings->load(plik_ukladu_bws);
    }

    static WpisPulpitu* nowe=nullptr;
    if(!nowe)nowe=new WpisPulpitu[PULPIT_MAX_WPISOW]{};
    if(!nowe)return false;
    for(int i=0;i<PULPIT_MAX_WPISOW;++i)nowe[i]=WpisPulpitu{};
    int count = 0;
    int filesSeen = 0;
    static WpisPulpitu incoming;
    static bool retained[DesktopEntries::FileCapacity];
    int pos = 0;
    while (pos < length) {
        int end = pos;
        while (end < length && lista_pulpitu[end] != '\n') ++end;
        if (end - pos <= 7) return false;
        const bool folder = lista_pulpitu[pos] == '[' && lista_pulpitu[pos + 1] == 'K';
        const bool file = lista_pulpitu[pos] == '[' && lista_pulpitu[pos + 1] == 'P';
        const int name_length = end - pos - 7;
        if ((!folder && !file) || name_length <= 0 || name_length >= PULPIT_NAZWA_CAP)
            return false;
        incoming=WpisPulpitu{};incoming.folder = folder;
        for (int i = 0; i < name_length; ++i)
            incoming.nazwa[i] = lista_pulpitu[pos + 7 + i];
        if(!DesktopSettings::identity(incoming.nazwa))return false;
        ++filesSeen;
        // Admit existing identities first, then stable UTF-8 order. At boot,
        // saved identities protect previously visible files from new overflow.
        bool keep=false;
        for(int i=0;i<liczba_wpisow_pulpitu;++i)if(wpisy_pulpitu[i].launcher<0 && teksty_rowne(wpisy_pulpitu[i].nazwa,incoming.nazwa))keep=true;
        if(!liczba_wpisow_pulpitu && desktop_settings)for(int i=0;i<desktop_settings->count;++i)
            if(desktop_settings->records[i].type=='F' && teksty_rowne(desktop_settings->records[i].id,incoming.nazwa))keep=true;
        int at=0;
        while(at<count && ((retained[at] && !keep) || (retained[at]==keep && porownaj_nazwy_pulpitu(nowe[at].nazwa,incoming.nazwa)<0)))++at;
        if(at<count && teksty_rowne(nowe[at].nazwa,incoming.nazwa))return false;
        if(at<DesktopEntries::FileCapacity) {
            if(count<DesktopEntries::FileCapacity)++count;
            for(int j=count-1;j>at;--j){nowe[j]=nowe[j-1];retained[j]=retained[j-1];}
            nowe[at]=incoming;retained[at]=keep;
        }
        pos = end + 1;
    }
    // Model and waiting order do not depend on admission priority or enumeration.
    for(int i=1;i<count;++i) {
        incoming=nowe[i];int j=i;
        while(j>0 && porownaj_nazwy_pulpitu(nowe[j-1].nazwa,incoming.nazwa)>0){nowe[j]=nowe[j-1];--j;}
        nowe[j]=incoming;
    }
    const int excess=filesSeen-count;
    if(excess && excess!=pulpit_nadmiar_modelu)bws_wywolaj(1,reinterpret_cast<uint64_t>(
        "[DESKTOP] Przekroczono limit modelu (128 plików). Pozostałe elementy są dostępne w Eksploratorze.\n"));
    pulpit_nadmiar_modelu=excess;
    uint64_t modelHash=uint64_t(count);
    for(int i=0;i<count;++i)modelHash=(modelHash ^ hash_listy(nowe[i].nazwa,dlugosc_limit(nowe[i].nazwa,PULPIT_NAZWA_CAP)) ^ uint64_t(nowe[i].folder))*UINT64_C(1099511628211);
    const bool modelWasApplied=desktop_settings && desktop_settings->applied;
    const bool modelChanged=desktop_settings && desktop_settings->applied && modelHash!=hash_modelu_pulpitu;
    for(int id=0;id<DesktopEntries::Count;++id) {
        auto& entry=nowe[count++];entry.launcher=id;
        const char* name=DesktopEntries::Launchers[id].name;
        for(int b=0;name[b];++b)entry.nazwa[b]=name[b];
    }
    static Desktop::Layout<PULPIT_MAX_WPISOW>* next=nullptr;
    if(!next)next=new Desktop::Layout<PULPIT_MAX_WPISOW>();
    if(!next)return false;
    *next=desktop_layout;
    int anchor=-1;
    for(int i=0;i<count;++i) {
        Desktop::Position saved{};
        for(int j=0;j<liczba_wpisow_pulpitu;++j)if(wpisy_pulpitu[j].launcher==nowe[i].launcher && wpisy_pulpitu[j].folder==nowe[i].folder &&
            teksty_rowne(wpisy_pulpitu[j].nazwa,nowe[i].nazwa)) {
            saved=desktop_layout.items[j];if(j==zaznaczony_wpis_pulpitu)anchor=i;break;
        }
        next->items[i]=saved;next->items[i].launcher=nowe[i].launcher>=0;
    }
    (void)next->set_count(count);aktualizuj_layout_modelu(*next,nowe,false);
    if(desktop_settings && !desktop_settings->applied) {
        if(desktop_settings) {
            if(desktop_settings->count) {
                [[maybe_unused]] int corrected=desktop_settings->apply(*next,[&](int i) {
                    auto* launcher=DesktopEntries::launcher(nowe[i].launcher);
                    return DesktopSettings::Key{launcher?'L':'F',launcher?launcher->id:nowe[i].nazwa};
                });
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
        desktop_settings->applied=true;
    }
    Desktop::Prostokat changed;
    for(int i=0;i<liczba_wpisow_pulpitu;++i) {
        int match=-1;
        for(int j=0;j<count;++j)if(wpisy_pulpitu[i].launcher==nowe[j].launcher && wpisy_pulpitu[i].folder==nowe[j].folder &&
            teksty_rowne(wpisy_pulpitu[i].nazwa,nowe[j].nazwa)){match=j;break;}
        if(match<0 || desktop_layout.items[i].placed!=next->items[match].placed ||
           desktop_layout.items[i].selected!=next->items[match].selected ||
           desktop_layout.visual_bounds(i)!=next->visual_bounds(match)) {
            if(desktop_layout.items[i].placed)changed=Desktop::unite(changed,desktop_layout.visual_bounds(i));
            if(match>=0 && next->items[match].placed)changed=Desktop::unite(changed,next->visual_bounds(match));
        }
    }
    for(int j=0;j<count;++j) {
        bool found=false;
        for(int i=0;i<liczba_wpisow_pulpitu;++i)if(wpisy_pulpitu[i].launcher==nowe[j].launcher && wpisy_pulpitu[i].folder==nowe[j].folder &&
            teksty_rowne(wpisy_pulpitu[i].nazwa,nowe[j].nazwa)){found=true;break;}
        if(!found && next->items[j].placed)changed=Desktop::unite(changed,next->visual_bounds(j));
    }
    // No renderer, publication or persistence calls occur within this commit.
    // The single WM event thread sees either the old complete state or this one.
    WpisPulpitu* old=wpisy_pulpitu;wpisy_pulpitu=nowe;nowe=old;
    desktop_layout=*next;liczba_wpisow_pulpitu=count;zaznaczony_wpis_pulpitu=anchor;
    pulpit_hover_folder=-1;
    desktop_pending_dirty=Desktop::unite(desktop_pending_dirty,changed);
    if(modelHash!=hash_modelu_pulpitu || !hash_pulpitu_wazny)++desktop_model_generation;
    if(modelWasApplied && next->newlyPlaced)desktop_settings->dirty=true;
    hash_pulpitu = nowy_hash;
    hash_modelu_pulpitu=modelHash;
    hash_pulpitu_wazny = true;
    if(modelChanged)desktop_settings->dirty=true;
    return true;
}

void aktualizuj_layout_pulpitu() {aktualizuj_layout_modelu(desktop_layout,wpisy_pulpitu,true);}
void aktualizuj_layout_modelu(Desktop::Layout<PULPIT_MAX_WPISOW>& layout,WpisPulpitu* entries,bool track) {
    auto& desktop_layout=layout;auto* wpisy_pulpitu=entries;
    const auto metrics=pulpit_metryki(ui_skala_procent);
    const bool geometryChanged=desktop_layout.workWidth>0 && (desktop_layout.workWidth!=screen_w ||
        desktop_layout.workBottom!=screen_h-PASEK_WYS || desktop_layout.dpi!=ui_skala_procent ||
        desktop_layout.ch!=metrics.komorka_wys || desktop_layout.y0!=PULPIT_GRID_Y);
    if(geometryChanged || !desktop_layout.workWidth)
        desktop_layout.geometry(screen_w,screen_h-PASEK_WYS,PULPIT_GRID_Y,ui_skala_procent);
    auto m=pulpit_metryki(ui_skala_procent);
    int maxLines=(m.etykieta_wys+m.odstep_linii)/(m.wysokosc_linii+m.odstep_linii);
    if(maxLines>m.max_linii)maxLines=m.max_linii;
    for(int i=0;i<desktop_layout.count;++i) {
        auto& p=desktop_layout.items[i];
        p.launcher=wpisy_pulpitu[i].launcher>=0;
        p.label=Desktop::wrap(wpisy_pulpitu[i].nazwa,m.etykieta_szer,m.wysokosc_linii,m.odstep_linii,maxLines,int(ui_skala_procent),oblicz_szerokosc_tekstu);
        p.span=1;
    }
    // Retain the existing initial row-major layout. Settings restoration and
    // display changes reserve valid visual positions before relocating others.
    for(int i=0;i<desktop_layout.count;++i)if(!desktop_layout.items[i].placed && !desktop_layout.items[i].custom) {
        // Catalog priority without changing model indices or valid saved positions.
        int order=wpisy_pulpitu[i].launcher>=0?wpisy_pulpitu[i].launcher:DesktopEntries::Count+i;
        auto& p=desktop_layout.items[i];
        p.col=desktop_layout.cols?order%desktop_layout.cols:0;
        p.row=desktop_layout.cols?order/desktop_layout.cols:0;
    }
    bool waiting=false;
    for(int i=0;i<desktop_layout.count;++i)if(!desktop_layout.items[i].placed)waiting=true;
    // Family/theme updates only refresh labels; anchors and persistence stay put.
    desktop_layout.newlyPlaced=0;
    const int corrected=(geometryChanged || waiting)?desktop_layout.reflow():0;
    // A display transition only transforms canonical anchors and repairs their
    // runtime projection. Only model/user placement changes are persisted.
    if(track && !geometryChanged && desktop_settings && desktop_settings->applied &&
       (corrected || desktop_layout.newlyPlaced))desktop_settings->dirty=true;
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
    if(!obecny_czas[0])return false; // Keep the last valid clock on a failed read.

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
        if(pulpit_canvas) {
            for(int y=0;y<g_cached_btn_h;++y)pulpit_canvas->rysuj_wiersz_ekranu(
                PRZYCISK_MENU_X,przycisk_menu_y()+y,g_cached_btn_w,buf+size_t(y)*g_cached_btn_w);
        }else {
            const BwsGuiRegion r{PRZYCISK_MENU_X,przycisk_menu_y(),g_cached_btn_w,g_cached_btn_h,reinterpret_cast<uint64_t>(buf)};
            (void)gui_zastap_regiony(&r,1,desktop_geometry.generation);
        }
    }
}

bool RysujZegar(bool force=false) {
    return taskbar_clock && taskbar_clock->update(ostatni_czas,force,[](Desktop::Prostokat r,const uint32_t* pixels){
        if(pulpit_canvas) {
            for(int y=0;y<r.wysokosc;++y)pulpit_canvas->rysuj_wiersz_ekranu(r.x,r.y+y,r.szerokosc,pixels+size_t(y)*r.szerokosc);
            return true;
        }
        const BwsGuiRegion region{r.x,r.y,r.szerokosc,r.wysokosc,reinterpret_cast<uint64_t>(pixels)};
        return gui_zastap_regiony(&region,1,desktop_geometry.generation);
    });
}
void RysujTaskbarZawartosc() {
    const uint32_t tlo_paska = motyw_kolor(MOTYW_KOLOR_PANEL);
    const uint32_t ramka_paska = motyw_kolor(MOTYW_KOLOR_RAMKA);
    const uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
    const uint32_t podniesiony = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);

    pulpit_prostokat(0, desktop_geometry.taskbar_y, poczatek_zegara(), PASEK_WYS, tlo_paska);
    pulpit_prostokat(0, desktop_geometry.taskbar_y, poczatek_zegara(), 1, ramka_paska);

    RysujPrzyciskMenu();
    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        const GuiOknoInfo& o = okna_taskbara[i];
        if (o.tytul[0] == '\0' || task_x + TASK_BUTTON_W > poczatek_zegara())
            continue;
        const uint32_t kolor = o.aktywne ? akcent :
            (o.stan == GUI_OKNO_ZMINIMALIZOWANE ? podniesiony : motyw_kolor(MOTYW_KOLOR_PRZYCISK));
        const uint32_t kolor_tekst = o.aktywne ? motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU) : motyw_kolor(MOTYW_KOLOR_TEKST);

        pulpit_prostokat(task_x, desktop_geometry.taskbar_y + ui_skaluj(4,ui_skala_procent),
                            TASK_BUTTON_W, SKROT_H, kolor);
        if (!o.aktywne) {
            pulpit_prostokat(task_x, desktop_geometry.taskbar_y + ui_skaluj(4,ui_skala_procent), TASK_BUTTON_W, 1, ramka_paska);
            pulpit_prostokat(task_x, desktop_geometry.taskbar_y + ui_skaluj(4,ui_skala_procent) + SKROT_H - 1, TASK_BUTTON_W, 1, ramka_paska);
            pulpit_prostokat(task_x, desktop_geometry.taskbar_y + ui_skaluj(4,ui_skala_procent), 1, SKROT_H, ramka_paska);
            pulpit_prostokat(task_x + TASK_BUTTON_W - 1, desktop_geometry.taskbar_y + ui_skaluj(4,ui_skala_procent), 1, SKROT_H, ramka_paska);
        }
        char podpis[15] = {};
        for (int c = 0; c < 14 && o.tytul[c] != '\0'; ++c)
            podpis[c] = o.tytul[c];
        pulpit_tekst(task_x, desktop_geometry.taskbar_y + ui_skaluj(4,ui_skala_procent),
                                 TASK_BUTTON_W, SKROT_H, 1,
                                 kolor_tekst, podpis);
        task_x += TASK_BUTTON_W + TASK_BUTTON_GAP;
    }

    if(!ostatni_czas[0])(void)pobierz_i_zaktualizuj_czas();
    (void)RysujZegar(true);
}

void RysujTaskbar() {
    static DesktopSelectionPaint* paint=nullptr;
    static BursztynUI::KontekstRysowania* ctx=nullptr;
    if(!paint)paint=new DesktopSelectionPaint;
    if(!ctx)ctx=new BursztynUI::KontekstRysowania({0,0,8192,8192});
    if(!paint || !ctx)return;
    paint->begin();
    paint->add({desktop_geometry.taskbar_x,desktop_geometry.taskbar_y,
        desktop_geometry.taskbar_width,desktop_geometry.taskbar_height},{0,0,screen_w,screen_h});
    pulpit_canvas=ctx;
    (void)paint->finish(*ctx,[](void*,Desktop::Prostokat){RysujTaskbarZawartosc();},nullptr,desktop_geometry.generation);
    pulpit_canvas=nullptr;
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
    pulpit_prostokat(r.x,r.y,r.szerokosc,t,color);
    pulpit_prostokat(r.x,r.y+r.wysokosc-t,r.szerokosc,t,color);
    pulpit_prostokat(r.x,r.y,t,r.wysokosc,color);
    pulpit_prostokat(r.x+r.szerokosc-t,r.y,t,r.wysokosc,color);
}
void RysujWpisPulpitu(int i) {
    if(!desktop_layout.items[i].placed)return;
    int x,y;prostokat_wpisu_pulpitu(i,&x,&y);
    const auto g=pulpit_geometria_komorki(x,y,ui_skala_procent);
    const auto& p=desktop_layout.items[i];
    if(p.selected || pulpit_hover_folder==i) {
        auto r=wizualny_wpis_pulpitu(i);
        pulpit_prostokat(r.x,r.y,r.szerokosc,r.wysokosc,motyw_kolor(MOTYW_KOLOR_PULPIT_ZAZNACZENIE));
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
            p.selected?motyw_kolor(MOTYW_KOLOR_PULPIT_TEKST_ZAZNACZONY):KANONICZNY_KOLOR_ETYKIETY_PULPITU,text,p.selected || pulpit_hover_folder==i);
    }
}
void RysujWpisyPulpitu() {for(int i=0;i<desktop_layout.count;++i)RysujWpisPulpitu(i);}
bool odrysuj_transakcje_pulpitu() {
    auto dirty=desktop_pending_dirty.przeciecie({0,0,screen_w,screen_h-PASEK_WYS});
    if(dirty.czy_pusty())return false;
    struct Tile {Desktop::Prostokat r;uint32_t* pixels;Tile* next;};
    Tile* head=nullptr;
    static BursztynUI::KontekstRysowania* canvas=nullptr;
    if(!canvas)canvas=new BursztynUI::KontekstRysowania({0,0,decltype(desktop_layout)::GeometryMax,decltype(desktop_layout)::GeometryMax});
    if(!canvas)return false;
    bool ok=true;
    // Prepare every affected tile off-screen before submitting any pixels.
    // Normal single-item changes require one small tile. Large batches retain
    // BWS57's existing 2048x2048 limit without large Ring3 stack allocations.
    for(int y=dirty.y;ok && y<dirty.dolny();y+=2048)for(int x=dirty.x;ok && x<dirty.prawy();x+=2048) {
        int w=dirty.prawy()-x,h=dirty.dolny()-y;if(w>2048)w=2048;if(h>2048)h=2048;
        auto* tile=new Tile{{x,y,w,h},nullptr,head};
        if(!tile){ok=false;break;}
        head=tile;tile->pixels=new uint32_t[size_t(w)*size_t(h)]{};
        if(!tile->pixels){ok=false;break;}
        canvas->ustaw_cel(tile->pixels,tile->r);pulpit_canvas=canvas;
        for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed &&
            Desktop::intersects(tile->r,wizualny_wpis_pulpitu(i)))RysujWpisPulpitu(i);
        pulpit_canvas=nullptr;canvas->ustaw_cel(nullptr,{});
    }
    if(ok)for(auto* t=head;t;t=t->next)
        if(!gui_zastap_region(t->r.x,t->r.y,t->r.szerokosc,t->r.wysokosc,t->pixels)){ok=false;break;}
    if(ok){desktop_pending_dirty={};gui_odswiez();}
    while(head){auto* t=head;head=t->next;delete[] t->pixels;delete t;}
    return ok;
}
void rysuj_ikone_aplikacji(const AplikacjaPulpitu& aplikacja);
void odrysuj_obszar_pulpitu(Desktop::Prostokat r) {
    if(r.szerokosc<=0 || r.wysokosc<=0)return;
    // Zero removes pixels from this transparent desktop surface. The compositor
    // restores its cached wallpaper; no wallpaper decode or full-layer clear.
    gui_rysuj_prostokat(r.x,r.y,r.szerokosc,r.wysokosc,0);
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed && Desktop::intersects(r,wizualny_wpis_pulpitu(i)))RysujWpisPulpitu(i);
}
void odrysuj_zaznaczenie_pulpitu() {
    // Defined below the gesture state; selection changes share the same
    // bounded off-screen replacement path as rubber-band movement.
    extern bool odrysuj_rubber_pulpitu(Desktop::Prostokat,Desktop::Prostokat);
    (void)odrysuj_rubber_pulpitu({},{});
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
static DesktopSelectionPaint* desktop_selection_paint=nullptr;
static BursztynUI::KontekstRysowania* desktop_selection_canvas=nullptr;
static uint64_t desktop_selection_generation=0;
bool odrysuj_rubber_pulpitu(Desktop::Prostokat old_band,Desktop::Prostokat new_band) {
    if(!desktop_selection_paint)desktop_selection_paint=new DesktopSelectionPaint;
    if(!desktop_selection_canvas)desktop_selection_canvas=new BursztynUI::KontekstRysowania({0,0,8192,8192});
    if(!desktop_selection_paint || !desktop_selection_canvas)return false;
    auto& paint=*desktop_selection_paint;paint.begin();
    const Desktop::Prostokat work{0,0,screen_w,screen_h-PASEK_WYS};
    const int t=ui_skaluj(1,ui_skala_procent);
    paint.border(old_band,t,work);paint.border(new_band,t,work);
    for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed &&
        desktop_previous[i].selected!=desktop_layout.items[i].selected)
        paint.add(wizualny_wpis_pulpitu(i),work);
    pulpit_canvas=desktop_selection_canvas;
    bool ok=paint.finish(*desktop_selection_canvas,[](void* data,Desktop::Prostokat r){
        for(int i=0;i<desktop_layout.count;++i)if(desktop_layout.items[i].placed &&
            Desktop::intersects(r,wizualny_wpis_pulpitu(i)))RysujWpisPulpitu(i);
        rysuj_ramke_pulpitu(*static_cast<Desktop::Prostokat*>(data),motyw_kolor(MOTYW_KOLOR_PULPIT_RAMKA));
    },&new_band,desktop_geometry.generation);
    pulpit_canvas=nullptr;
    if(ok)++desktop_selection_generation;
    return ok;
}
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
    if(desktop_gesture.dragging){if(desktop_gesture.rubber)(void)odrysuj_rubber_pulpitu(desktop_gesture.band,{});else obrysy_grupy(true);}
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
            for(int i=0;i<desktop_layout.count;++i)desktop_layout.items[i].selected=desktop_layout.items[i].placed;
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
        if(g.rubber) {
            const auto old_band=g.dragging?g.band:Desktop::Prostokat{};
            int x=e.x<0?0:e.x>=screen_w?screen_w-1:e.x;
            int y=e.y<0?0:e.y>=screen_h-PASEK_WYS?screen_h-PASEK_WYS-1:e.y;
            const Desktop::Prostokat next{x<g.x?x:g.x,y<g.y?y:g.y,
                (x<g.x?g.x-x:x-g.x)+1,(y<g.y?g.y-y:y-g.y)+1};
            zapamietaj_zaznaczenie_pulpitu();
            for(int i=0;i<desktop_layout.count;++i)desktop_layout.items[i].selected=
                desktop_layout.items[i].placed && ((g.ctrl && g.base[i]) || Desktop::intersects(next,wizualny_wpis_pulpitu(i)));
            if(odrysuj_rubber_pulpitu(old_band,next)) {
                g.dragging=true;g.dx=dx;g.dy=dy;g.band=next;gui_odswiez();
            }else for(int i=0;i<desktop_layout.count;++i)
                desktop_layout.items[i].selected=desktop_previous[i].selected;
            return true;
        }
        if(target!=pulpit_hover_folder)ustaw_hover_drop_pulpitu(target);
        if(g.dragging)obrysy_grupy(true);
        g.dragging=true;g.dx=dx;g.dy=dy;obrysy_grupy(false);
        gui_odswiez();return true;
    }
    if(e.typ==BWS_ZDARZENIE_MYSZ_UP && g.candidate) {
        bool layoutChanged=false;
        if(g.dragging && g.rubber)(void)odrysuj_rubber_pulpitu(g.band,{});
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
            // A completed visual move can free space for waiting entries.
            desktop_layout.reflow();
            if(desktop_layout.newlyPlaced) {
                layoutChanged=true;if(desktop_settings)desktop_settings->dirty=true;
                RysujPulpit(true);
            }
            zarejestruj_pulpit_jako_cel_drop();
        } else if(g.item>=0 && !g.ctrl) {
            zapamietaj_zaznaczenie_pulpitu();desktop_layout.click(g.item,false);odrysuj_zaznaczenie_pulpitu();
        }
        g=GestPulpitu{};gui_ustaw_capture_myszy(false);gui_odswiez();if(layoutChanged)zapisz_uklad_pulpitu();return true;
    }
    return false;
}

void aktualizuj_overlay_menu() {gui_ustaw_system_overlay(false,0,0,0,0);}

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
        case TypIkonyAplikacji::Diagnostyka:
            // A themed monitor with a diagnostic pulse, using native icon primitives.
            rysuj_fragment_ikony(x,y,3,6,42,30,motyw_kolor(MOTYW_KOLOR_RAMKA));
            rysuj_fragment_ikony(x,y,6,9,36,24,motyw_kolor(MOTYW_KOLOR_PANEL));
            rysuj_fragment_ikony(x,y,9,21,9,3,motyw_kolor(MOTYW_KOLOR_AKCENT));
            rysuj_fragment_ikony(x,y,18,15,3,9,motyw_kolor(MOTYW_KOLOR_AKCENT));
            rysuj_fragment_ikony(x,y,21,24,3,5,motyw_kolor(MOTYW_KOLOR_AKCENT));
            rysuj_fragment_ikony(x,y,24,21,15,3,motyw_kolor(MOTYW_KOLOR_AKCENT));
            rysuj_fragment_ikony(x,y,20,36,8,5,motyw_kolor(MOTYW_KOLOR_RAMKA));
            rysuj_fragment_ikony(x,y,14,41,20,3,motyw_kolor(MOTYW_KOLOR_RAMKA));
            break;
        case TypIkonyAplikacji::TestDzwieku:
            // Glosnik + fala: znak aplikacji Test dźwięku
            rysuj_fragment_ikony(x,y,0,0,48,48,0x001A2030);
            rysuj_fragment_ikony(x,y,2,2,44,44,0x00243048);
            rysuj_fragment_ikony(x,y,10,18,10,12,0x00FFBF00);
            rysuj_fragment_ikony(x,y,20,14,4,20,0x00FFBF00);
            rysuj_fragment_ikony(x,y,28,12,3,24,0x0066D9FF);
            rysuj_fragment_ikony(x,y,34,8,3,32,0x0066D9FF);
            break;
    }


}

void RysujPulpit(bool wymus_pelne_odswiezenie) {
#if BSF_PROFILE
    const uint64_t profile_start=gui_czas_monotoniczny_ns();
#endif
    if (wymus_pelne_odswiezenie) {
        // Dla procesu warstwowego BWS 19 zeruje prywatna warstwe.
        // Keep the already committed taskbar intact until its complete new
        // strip replaces it. Clearing the desktop must not erase that strip.
        gui_rysuj_prostokat(0,0,screen_w,desktop_geometry.work_height,0);
        desktop_pending_dirty={};
    }

    RysujTaskbar();


    RysujWpisyPulpitu();


    // BWS 17 sklada warstwy i prezentuje gotowa klatke.
    zarejestruj_pulpit_jako_cel_drop();
    gui_odswiez();
#if BSF_PROFILE
    const uint64_t profile_elapsed=gui_czas_monotoniczny_ns()-profile_start;
    char profile_line[160]{};unsigned profile_n=0;
    auto profile_text=[&](const char* s){while(*s)profile_line[profile_n++]=*s++;};
    auto profile_num=[&](uint64_t v){char b[24];unsigned n=0;do{b[n++]=char('0'+v%10);v/=10;}while(v);while(n)profile_line[profile_n++]=b[--n];};
    profile_text("[BSF DESKTOP] ns=");profile_num(profile_elapsed);
    profile_text(" full=");profile_num(wymus_pelne_odswiezenie);
    profile_text(" menu=");profile_num(menu_start_otwarte);
    profile_text(" entries=");profile_num(desktop_layout.count);
    profile_text("\n");wypisz(profile_line);
#endif
}

void zamknij_menu_i_odtworz_pulpit() {
    if (!menu_start_otwarte && !menu_gry_otwarte) return;

    menu_start_otwarte = false;
    menu_gry_otwarte = false;
    aktualizuj_overlay_menu();

    if(start_menu)start_menu->close();
    RysujPrzyciskMenu();gui_odswiez();
}

bool aktywuj_istniejace(const char* executable);

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
        (void)aktywuj_istniejace(SCIEZKA_EKSPLORATOR);
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
    const bool was_open=pulpit_context_open;
    if(pulpit_menu)pulpit_menu->zamknij();
    pulpit_context_open=false;zarejestruj_pulpit_jako_cel_drop();
    if(was_open)gui_odswiez();
}
bool obsluz_zmiane_plikow_pulpitu(const bws_zdarzenie& event) {
    if(event.typ!=BWS_ZDARZENIE_PLIKI_ZMIENIONE)return false;
    zamknij_pulpit_context();
    // Keep a complete old model while a modal or gesture owns the desktop.
    // The existing deferred timer/direct-action path refreshes after release.
    if(!desktop_gesture.candidate && pulpit_modal_typ==PulpitModalTyp::BRAK) {
        (void)odswiez_wpisy_pulpitu();
        (void)odrysuj_transakcje_pulpitu();zapisz_uklad_pulpitu();
    }
    return true;
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
        if(pulpit_wybor->wynik_pliku()==BursztynUI::WynikDialoguPliku::Wybrano){zatwierdz_pulpit_modal();return true;}
        else if(!pulpit_wybor->czy_otwarta()){pulpit_batch_count=0;zamknij_pulpit_modal();}
        if(pulpit_wybor->events.wymaga_odswiezenia())gui_odswiez();
        return true;
    }
    if(pulpit_dialog->zdarzenie(zdarzenie)) {
        if(pulpit_dialog->wynik()==1){zatwierdz_pulpit_modal();return true;}
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

bool aktywuj_istniejace(const char* executable) {
    GuiOknoInfo matching[SKLADACZ_MAKS_WARSTW]{};
    uint32_t count=gui_pobierz_okna_programu(matching,SKLADACZ_MAKS_WARSTW,executable);
    for(uint32_t i=0;i<count;++i){
        if(!matching[i].tytul[0])continue;
        zamknij_menu_i_odtworz_pulpit();
        return gui_aktywuj_okno(matching[i].window_id);
    }
    return false;
}

void uruchom_lub_aktywuj(const char* sciezka, const char*) {
    if (!aktywuj_istniejace(sciezka)) uruchom_program_gui(sciezka);
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
        case 15:
            uruchom_lub_aktywuj(SCIEZKA_TEST_DZWIEKU, "Test dźwięku");
            break;
        case 16:
            uruchom_lub_aktywuj("/programy/diagnostyka.cebula/diagnostyka.bur", "Diagnostyka systemu");
            break;
        case 21:
            uruchom_lub_aktywuj(SCIEZKA_SNAKE,"Snake");
            break;
        case 18:
            wykonaj_restart();
            break;
        case 20:
            uruchom_lub_aktywuj(AppIdentity::Settings, "Ustawienia");
            break;
        case 19:
            wykonaj_zamkniecie();
            break;
        default:
            break;
    }
}

void obsluz_klikniecie(int mx, int my) {
    if (punkt_w_prostokacie(mx,my,PRZYCISK_MENU_X,przycisk_menu_y(),PRZYCISK_MENU_W,PRZYCISK_MENU_H)) {
        if(menu_start_otwarte)zamknij_menu_i_odtworz_pulpit();
        else {
            if(!start_menu)start_menu=new StartMenu::Surface();
            if(start_menu){DisplayGeometry g{};
                BursztynUI::Prostokat work{0,0,screen_w,screen_h-PASEK_WYS};
                if(gui_pobierz_geometrie_ekranu(&g)&&g.work_width>0&&g.work_height>0)
                    work={g.work_x,g.work_y,g.work_width,g.work_height};
                start_menu->open(work,MENU_X);menu_start_otwarte=start_menu->czy_otwarta();}
            RysujPrzyciskMenu();gui_odswiez();
        }
        return;
    }

    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        if (okna_taskbara[i].tytul[0] == '\0' ||
            task_x + TASK_BUTTON_W > poczatek_zegara()) continue;
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
    start_menu=new StartMenu::Surface();
    pulpit_menu=new PlikiUI::MenuPlikowe();
    pulpit_dialog=new PlikiUI::DialogPlikowy();
#if BURSZTYN_DIAG_BOOT
    wm_log("[WM-BOOT] entered-ring3\n");
    wm_log("[WM] _start begun\n");
#endif
    GuiMonitorInfo mon{};if(gui_pobierz_monitor(0,&mon))ui_skala_procent=mon.scale_x1000/10;
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    DisplayGeometry initial{};
    if(gui_pobierz_geometrie_ekranu(&initial)) {
        desktop_geometry=initial;screen_w=initial.width;screen_h=initial.height;ui_skala_procent=initial.scale;
    }
    metryki_ekranu();
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
#if BSF_PROFILE
    for(unsigned profile_sample=0;profile_sample<7;++profile_sample)RysujPulpit(true);
#endif
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
            if(start_menu)start_menu->close();
            menu_start_otwarte=false;menu_gry_otwarte=false;anuluj_gest_pulpitu();
            // Pola zdarzenia (dx/dy/kod) pochodza z chwili wyslania i moga byc
            // nieaktualne, gdy kolejka zdarzen jest przetwarzana z opoznieniem
            // (np. szybkie sekwencje modesetow). Zapis obszaru roboczego ze
            // starych wartosci nadpisywalby swiezy stan BWS62 opublikowany
            // synchronicznie przez usluge ekranu i powodowalby clipping okien.
            // Zrodlem prawdy jest biezacy stan jadra; zdarzenie to tylko budzik.
            GuiMonitorInfo mon_now{};
            DisplayGeometry geometry{};
            if(gui_pobierz_geometrie_ekranu(&geometry) && geometry.width>0 && geometry.height>0) {
                desktop_geometry=geometry;
                screen_w=geometry.width;screen_h=geometry.height;ui_skala_procent=geometry.scale;
            }else if(gui_pobierz_monitor(0,&mon_now)&&mon_now.szer>0&&mon_now.wys>0){
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
            rozmiesc_ikony_ekranu();zarejestruj_pulpit_jako_cel_drop();aktualizuj_overlay_menu();RysujPulpit(true);
            if(geometry.generation)gui_potwierdz_geometrie_pulpitu(geometry);
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) {
            BursztynUI::Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
            metryki_ekranu();
            aktualizuj_layout_pulpitu();
            if(start_menu)start_menu->refresh();
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
            zamknij_menu_i_odtworz_pulpit();
            zamknij_pulpit_context();
            zamknij_pulpit_modal();
            continue;
        }

        if(obsluz_zmiane_plikow_pulpitu(zdarzenie))continue;

        if(start_menu && start_menu->handle(zdarzenie)) {
            const int action=start_menu->take_action();
            menu_start_otwarte=start_menu->czy_otwarta();
            if(action>=0){zamknij_menu_i_odtworz_pulpit();obsluz_element_menu(action);}
            else {RysujPrzyciskMenu();gui_odswiez();}
            if(zdarzenie.typ!=BWS_ZDARZENIE_TIMER)continue;
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
            odswiez_pulpit_natychmiast();
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
                (void)odswiez_wpisy_pulpitu();
                (void)odrysuj_transakcje_pulpitu();zapisz_uklad_pulpitu();
                if(!start_pulpitu_gotowy&&hash_pulpitu_wazny) {
                    RysujPulpit(true);
                    start_pulpitu_gotowy=gui_potwierdz_gotowosc_pulpitu();
                }
            }
        }

        const bool clock_changed=pobierz_i_zaktualizuj_czas();
        if(clock_changed || (taskbar_clock && taskbar_clock->needs_repaint())) {
            if(RysujZegar())gui_odswiez();
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
