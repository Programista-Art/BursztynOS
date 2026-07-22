#include "grafika.h"
#include "pamiec.h"
#include "zegar-rtc.h"

// ==================== ZMIENNE GLOBALNE MATRYCY ====================
static uint32_t* lfb = nullptr;
static uint32_t  lfb_szerokosc = 0;
static uint32_t  lfb_wysokosc = 0;
static uint32_t  lfb_pitch = 0;
static uint8_t   lfb_bpp = 32;

static uint8_t* backbuffer = nullptr;

// ==================== DEKLARACJE ZAPOWIADAJACE ====================
void UkryjKursor();
void PokazKursor();
void OdswiezEkran();
void PrzeniesNaEkran();
void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys);
void DopiszZnakDoEdytora(char c);

// ==================== STRUKTURY OKIEN ====================
struct Okno {
    uint32_t x, y, szer, wys;
    uint32_t stary_x, stary_y, stary_szer, stary_wys; 
    const char* krotka_nazwa;
    const char* tytul;
    uint32_t kolor_tla;
    bool widoczne;
    bool zmaksymalizowane;
};

static Okno okna[2] = {
    { 20, 20, 660, 360,  0,0,0,0, "Terminal", "Powloka Bursztyna (Ring 3 Terminal)", 0x001A0B00, true, false },
    { 180, 80, 560, 400, 0,0,0,0, "Edytor", "Edytor Avocado - Nowy Plik", 0x00280F00, true, false }
};

static int z_order[2] = {1, 0}; 

// ==================== ZMIENNE MENU BURSZTYN ====================
static bool menu_otwarte = false;
static char menu_szukaj[32] = {0};
static int  menu_szukaj_len = 0;

struct MenuApp {
    int id_okna;
    const char* nazwa;
};
static const MenuApp menu_aplikacje[2] = {
    {0, "Terminal (shell)"},
    {1, "Edytor Avocado"}
};
static int aktualne_menu_y[2] = {-1, -1};

static int okno_przeciagane = -1;
static int chwyt_x = 0;
static int chwyt_y = 0;

static bool lewy_wcisniety = false;

// ==================== PAMIEC TERMINALA I EDYTORA ====================
struct ZnakTerminala {
    char znak;
    uint32_t kolor;
};
#define MAX_ROWS 80
#define MAX_COLS 140
static ZnakTerminala term_buf[MAX_ROWS][MAX_COLS];
static int term_r = 0, term_c = 0;
static int term_max_r = 25, term_max_c = 80;

static ZnakTerminala edit_buf[MAX_ROWS][MAX_COLS];
static int edit_r = 0, edit_c = 0;
static int edit_max_r = 25, edit_max_c = 80;

static int mysz_x = 500;
static int mysz_y = 300;
static uint32_t bufor_kursora[16][16];
static bool kursor_widoczny = false;

// ==================== DIAGNOSTYKA ====================
static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
void SerialLog(const char* str) {
    for(int i = 0; str[i] != '\0'; i++) serial_outb(0x3F8, str[i]);
} 

// ==================== STEROWNIK BOCHS VBE ====================
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF
#define VBE_DISPI_INDEX_ID     0
#define VBE_DISPI_INDEX_XRES   1
#define VBE_DISPI_INDEX_YRES   2
#define VBE_DISPI_INDEX_BPP    3
#define VBE_DISPI_INDEX_ENABLE 4
#define VBE_DISPI_DISABLED     0x00
#define VBE_DISPI_ENABLED      0x01
#define VBE_DISPI_LFB_ENABLED  0x40

static inline void outw(uint16_t port, uint16_t val) { asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port)); }
static inline uint16_t inw(uint16_t port) { uint16_t val; asm volatile ("inw %1, %0" : "=a"(val) : "Nd"(port)); return val; }

void BochsVBE_Write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index); outw(VBE_DISPI_IOPORT_DATA, value);
}
uint16_t BochsVBE_Read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index); return inw(VBE_DISPI_IOPORT_DATA);
}

// ==================== BITMAPY KURSORA I CZCIONEK ====================
static const uint8_t kursor_bitmapa[16][16] = {
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},{1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},{1,2,2,2,2,2,2,1,1,1,1,1,0,0,0,0},{1,2,2,1,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,2,1,0,0,0,0,0,0,0},{1,1,0,0,0,1,2,2,1,0,0,0,0,0,0,0},{0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0}
};

static const uint8_t czcionka_pl_8x8[18][8] = {
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x0C}, {0x08,0x10,0x3C,0x60,0x60,0x66,0x3C,0x00}, {0x00,0x00,0x3E,0x66,0x7E,0x60,0x3C,0x0C}, 
    {0x18,0x18,0x38,0x78,0x18,0x18,0x18,0x00}, {0x08,0x10,0x7C,0x66,0x66,0x66,0x66,0x00}, {0x08,0x10,0x3C,0x66,0x66,0x66,0x3C,0x00}, 
    {0x08,0x10,0x3E,0x60,0x3C,0x06,0x7C,0x00}, {0x08,0x10,0x7E,0x0C,0x18,0x30,0x7E,0x00}, {0x00,0x18,0x7E,0x0C,0x18,0x30,0x7E,0x00}, 
    {0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x0C}, {0x08,0x10,0x3C,0x66,0x60,0x60,0x66,0x3C}, {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x0C}, 
    {0x60,0x60,0x60,0x70,0x60,0x60,0x7E,0x00}, {0x08,0x10,0x66,0x76,0x7E,0x7E,0x6E,0x66}, {0x08,0x10,0x3C,0x66,0x66,0x66,0x66,0x3C}, 
    {0x08,0x10,0x3C,0x66,0x60,0x3C,0x06,0x66}, {0x08,0x10,0x7E,0x06,0x0C,0x18,0x30,0x60}, {0x18,0x00,0x7E,0x06,0x0C,0x18,0x30,0x60}  
};

static const uint8_t czcionka_8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},{0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00},{0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},{0x00,0x66,0x6C,0x18,0x36,0x66,0x00,0x00},{0x38,0x6C,0x6C,0x38,0x6D,0x66,0x3B,0x00},{0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},{0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},{0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},{0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},{0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00},{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00},{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},{0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00},{0x7E,0x66,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},{0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00},{0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},{0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},{0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},{0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00},
    {0x3C,0x66,0x6E,0x6E,0x60,0x66,0x3C,0x00},{0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00},{0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},{0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},{0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},{0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},{0x3E,0x18,0x18,0x18,0x18,0x18,0x3E,0x00},{0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00},{0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},{0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},{0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},{0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},{0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},{0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},{0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},{0x63,0x63,0x6B,0x7F,0x77,0x63,0x63,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},{0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},{0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},{0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x00},{0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},{0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00},
    {0x60,0x30,0x18,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},{0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},{0x00,0x00,0x3C,0x60,0x60,0x66,0x3C,0x00},
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},{0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},{0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00},{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},{0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},{0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x70},{0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},{0x00,0x00,0x6C,0xFE,0xD6,0xD6,0xC6,0x00},{0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},{0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},{0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},{0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    {0x30,0x30,0x7C,0x30,0x30,0x34,0x18,0x00},{0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},{0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00},{0x00,0x00,0xC6,0xD6,0xFE,0x6C,0x6C,0x00},
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},{0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C},{0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},{0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},{0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},{0x3B,0x6E,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

// --- PODWÓJNE BUFOROWANIE ---
void PrzeniesNaEkran() {
    if (!lfb || !backbuffer) return;
    uint32_t* dst = (uint32_t*)lfb;
    uint32_t* src = (uint32_t*)backbuffer;
    uint64_t ilosc_pikseli = ((uint64_t)lfb_pitch * (uint64_t)lfb_wysokosc) / 4;
    for(uint64_t i = 0; i < ilosc_pikseli; i++) dst[i] = src[i];
}

void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys) {
    if(!lfb || !backbuffer) return;
    int start_x = x < 0 ? 0 : x; int start_y = y < 0 ? 0 : y;
    int end_x = x + szer; int end_y = y + wys;
    if(end_x > (int)lfb_szerokosc) end_x = lfb_szerokosc;
    if(end_y > (int)lfb_wysokosc) end_y = lfb_wysokosc;
    int bajtow_na_piksel = lfb_bpp / 8;
    for(int rzad = start_y; rzad < end_y; rzad++) {
        volatile uint8_t* dst = (volatile uint8_t*)lfb + rzad * lfb_pitch + start_x * bajtow_na_piksel;
        uint8_t* src = backbuffer + rzad * lfb_pitch + start_x * bajtow_na_piksel;
        int bajtow = (end_x - start_x) * bajtow_na_piksel;
        for(int b = 0; b < bajtow; b++) dst[b] = src[b];
    }
}

void PostawPiksel(int x, int y, uint32_t kolor) {
    if(!backbuffer || x < 0 || x >= (int)lfb_szerokosc || y < 0 || y >= (int)lfb_wysokosc) return;
    uint32_t offset = y * lfb_pitch + x * (lfb_bpp / 8);
    uint8_t* piksel = backbuffer + offset;
    if (lfb_bpp == 32) *(uint32_t*)piksel = kolor;
    else if (lfb_bpp == 24) { piksel[0] = kolor & 0xFF; piksel[1] = (kolor >> 8) & 0xFF; piksel[2] = (kolor >> 16) & 0xFF; } 
    else if (lfb_bpp == 16) {
        uint16_t r = (kolor >> 16) & 0xFF; uint16_t g = (kolor >> 8) & 0xFF; uint16_t b = kolor & 0xFF;
        *(uint16_t*)piksel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}

uint32_t PobierzPiksel(int x, int y) {
    if(!backbuffer || x < 0 || x >= (int)lfb_szerokosc || y < 0 || y >= (int)lfb_wysokosc) return 0;
    uint32_t offset = y * lfb_pitch + x * (lfb_bpp / 8);
    uint8_t* piksel = backbuffer + offset;
    if (lfb_bpp == 32) return *(uint32_t*)piksel;
    else if (lfb_bpp == 24) return piksel[0] | (piksel[1] << 8) | (piksel[2] << 16);
    else if (lfb_bpp == 16) {
        uint16_t k = *(uint16_t*)piksel;
        return (((k >> 11) & 0x1F) << 19) | (((k >> 5) & 0x3F) << 10) | ((k & 0x1F) << 3);
    }
    return 0;
}

void RysujProstokat(int px, int py, int szer, int wys, uint32_t kolor) {
    int start_x = px < 0 ? 0 : px; int start_y = py < 0 ? 0 : py;
    int end_x = px + szer; int end_y = py + wys;
    if (end_x > (int)lfb_szerokosc) end_x = lfb_szerokosc;
    if (end_y > (int)lfb_wysokosc) end_y = lfb_wysokosc;
    for(int y = start_y; y < end_y; y++) {
        for(int x = start_x; x < end_x; x++) PostawPiksel(x, y, kolor);
    }
}

void RysujZnak(char c, int px, int py, uint32_t kolor_tekstu, uint32_t kolor_tla, bool przezroczyste_tlo, int skala) {
    const uint8_t* znak = nullptr; uint8_t kod = (uint8_t)c;
    if (kod >= 32 && kod <= 126) znak = czcionka_8x8[kod - 32];
    else {
        int pl_idx = -1;
        switch(kod) {
            case 0xB9: pl_idx = 0; break; case 0xE6: pl_idx = 1; break; case 0xEA: pl_idx = 2; break;
            case 0xB3: pl_idx = 3; break; case 0xF1: pl_idx = 4; break; case 0xF3: pl_idx = 5; break;
            case 0x9C: pl_idx = 6; break; case 0x9F: pl_idx = 7; break; case 0xBF: pl_idx = 8; break;
            case 0xA5: pl_idx = 9; break; case 0xC6: pl_idx = 10; break; case 0xCA: pl_idx = 11; break;
            case 0xA3: pl_idx = 12; break; case 0xD1: pl_idx = 13; break; case 0xD3: pl_idx = 14; break;
            case 0x8C: pl_idx = 15; break; case 0x8F: pl_idx = 16; break; case 0xAF: pl_idx = 17; break;
        }
        if (pl_idx != -1) znak = czcionka_pl_8x8[pl_idx];
        else znak = czcionka_8x8['?' - 32];
    }

    for(int y = 0; y < 8; y++) {
        for(int x = 0; x < 8; x++) {
            bool zmaluj = znak[y] & (1 << (7 - x));
            if(zmaluj) {
                for(int sy=0; sy<skala; sy++) for(int sx=0; sx<skala; sx++) PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tekstu);
            } else if (!przezroczyste_tlo) { 
                for(int sy=0; sy<skala; sy++) for(int sx=0; sx<skala; sx++) PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tla);
            }
        }
    }
}

void WypiszTekst(const char* tekst, int px, int py, uint32_t kolor_tekstu, int skala) {
    int start_x = px;
    for(int i = 0; tekst[i] != '\0'; i++) { RysujZnak(tekst[i], start_x, py, kolor_tekstu, 0, true, skala); start_x += 8 * skala; }
}

// Funkcja typu Case-Insensitive do filtrowania menu
static bool ZawieraTekst(const char* pelny_tekst, const char* szukany) {
    if (szukany[0] == '\0') return true; 
    for (int i = 0; pelny_tekst[i] != '\0'; i++) {
        int j = 0;
        while (szukany[j] != '\0' && pelny_tekst[i + j] != '\0') {
            char t = pelny_tekst[i + j];
            char s = szukany[j];
            if (t >= 'A' && t <= 'Z') t += 32;
            if (s >= 'A' && s <= 'Z') s += 32;
            if (t != s) break; 
            j++;
        }
        if (szukany[j] == '\0') return true; 
    }
    return false;
}

// ==================== POMOCNIK ZEGARA ====================
void rysuj_zegar_rtc() {
    czas_rtc czas;
    pobierz_czas_rtc(&czas);
    char bufor_czasu[16];
    formatuj_czas_do_stringa(&czas, bufor_czasu);
    
    int zegar_x = lfb_szerokosc - 150;
    int zegar_y = lfb_wysokosc - 28;
    
    RysujProstokat(zegar_x, zegar_y - 7, 140, 24, 0x00280F00); // Tlo pod zegarem
    WypiszTekst(bufor_czasu, zegar_x, zegar_y, 0x00FFBF00, 2);
}

void RysujMenu() {
    if (!menu_otwarte) return;
    int menu_szer = 300;
    int menu_wys = 400;
    int menu_x = 0;
    int menu_y = lfb_wysokosc - 40 - menu_wys;

    RysujProstokat(menu_x, menu_y, menu_szer, menu_wys, 0x008A5A00);             
    RysujProstokat(menu_x + 2, menu_y + 2, menu_szer - 4, menu_wys - 4, 0x001A0B00); 

    RysujProstokat(menu_x + 10, menu_y + 10, menu_szer - 20, 36, 0x00280F00); 
    WypiszTekst("Szukaj:", menu_x + 16, menu_y + 20, 0x008A5A00, 2);
    WypiszTekst(menu_szukaj, menu_x + 130, menu_y + 20, 0x00D1D5DB, 2);
    
    if (menu_otwarte) {
        RysujProstokat(menu_x + 130 + (menu_szukaj_len * 16), menu_y + 34, 12, 2, 0x00FFBF00);
    }
    RysujProstokat(menu_x + 10, menu_y + 60, menu_szer - 20, 2, 0x008A5A00); 

    int rysuj_y = menu_y + 70;
    for (int i = 0; i < 2; i++) {
        if (ZawieraTekst(menu_aplikacje[i].nazwa, menu_szukaj)) {
            aktualne_menu_y[i] = rysuj_y; 
            RysujProstokat(menu_x + 10, rysuj_y, menu_szer - 20, 40, 0x00280F00); 
            WypiszTekst("> ", menu_x + 20, rysuj_y + 12, 0x00FFBF00, 2);
            WypiszTekst(menu_aplikacje[i].nazwa, menu_x + 50, rysuj_y + 12, 0x00FFBF00, 2);
            rysuj_y += 50; 
        } else {
            aktualne_menu_y[i] = -1; 
        }
    }
}

void RysujOkno(int id) {
    if (!okna[id].widoczne) return;
    int px = okna[id].x; int py = okna[id].y; int szer = okna[id].szer; int wys = okna[id].wys;
    if (szer < 10 || wys < 40) return;
    
    bool aktywne = (z_order[1] == id);
    uint32_t kolor_paska = aktywne ? 0x00FFBF00 : 0x008A5A00;
    uint32_t kolor_tekstu_paska = aktywne ? 0x001A0B00 : 0x00D1D5DB;

    RysujProstokat(px, py, szer, wys, 0x008A5A00);             
    RysujProstokat(px + 2, py + 2, szer - 4, 24, kolor_paska);  
    WypiszTekst(okna[id].tytul, px + 8, py + 6, kolor_tekstu_paska, 2);         
    
    RysujProstokat(px + szer - 74, py + 4, 20, 20, 0x00E58A00); 
    WypiszTekst("-", px + szer - 70, py + 6, 0x001A0B00, 2);
    RysujProstokat(px + szer - 50, py + 4, 20, 20, 0x00E58A00); 
    WypiszTekst(okna[id].zmaksymalizowane ? "v" : "^", px + szer - 46, py + 6, 0x001A0B00, 2);
    RysujProstokat(px + szer - 26, py + 4, 20, 20, 0x00AA0000); 
    WypiszTekst("X", px + szer - 22, py + 6, 0x00FFFFFF, 2);

    RysujProstokat(px + 2, py + 28, szer - 4, wys - 30, okna[id].kolor_tla); 
}

void RysujZawartoscTerminala(int px, int py, int szer, int wys) {
    int skala = 1; int wysokosc_linii = 12; int start_x = px + 6; int start_y = py + 28 + 4;
    for (int r = 0; r < term_max_r; r++) {
        int cx = start_x; int cy = start_y + (r * wysokosc_linii);
        if (cy + wysokosc_linii >= py + wys) break;
        for (int c = 0; c < term_max_c; c++) {
            char z = term_buf[r][c].znak;
            if (z != 0) { if (cx + 8*skala >= px + szer - 6) break; RysujZnak(z, cx, cy, term_buf[r][c].kolor, 0, true, skala); }
            cx += 8 * skala;
        }
    }
}

void RysujZawartoscEdytora(int px, int py, int szer, int wys) {
    int skala = 1; int wysokosc_linii = 12; int start_x = px + 6; int start_y = py + 28 + 4;
    for (int r = 0; r < edit_max_r; r++) {
        int cx = start_x; int cy = start_y + (r * wysokosc_linii);
        if (cy + wysokosc_linii >= py + wys) break;
        for (int c = 0; c < edit_max_c; c++) {
            char z = edit_buf[r][c].znak;
            if (z != 0) { if (cx + 8*skala >= px + szer - 6) break; RysujZnak(z, cx, cy, edit_buf[r][c].kolor, 0, true, skala); }
            cx += 8 * skala;
        }
    }
}

void OdswiezEkran() {
    if(!backbuffer) return;
    RysujProstokat(0, 0, lfb_szerokosc, lfb_wysokosc, 0x001A0B00);
    
    for(int k = 0; k < 2; k++) {
        int i = z_order[k];
        if (!okna[i].widoczne) continue;
        RysujOkno(i);
        if (i == 0) RysujZawartoscTerminala(okna[i].x, okna[i].y, okna[i].szer, okna[i].wys);
        else if (i == 1) {
            RysujZawartoscEdytora(okna[i].x, okna[i].y, okna[i].szer, okna[i].wys);
            if (z_order[1] == 1) {
                int cx = okna[i].x + 6 + (edit_c * 8);
                int cy = okna[i].y + 28 + 4 + (edit_r * 12);
                if (cy + 10 < (int)(okna[i].y + okna[i].wys) && cx + 8 < (int)(okna[i].x + okna[i].szer)) {
                    RysujProstokat(cx, cy + 10, 8, 2, 0x00FFBF00); 
                }
            }
        }
    }

    if (lfb_wysokosc >= 40) {
        RysujProstokat(0, lfb_wysokosc - 40, lfb_szerokosc, 40, 0x00280F00); 
        RysujProstokat(0, lfb_wysokosc - 40, lfb_szerokosc, 2, 0x008A5A00);  
        
        uint32_t kolor_menu_btn = menu_otwarte ? 0x00E58A00 : 0x001A0B00;
        uint32_t kolor_menu_txt = menu_otwarte ? 0x001A0B00 : 0x00FFBF00;
        RysujProstokat(0, lfb_wysokosc - 38, 120, 38, kolor_menu_btn);
        WypiszTekst("Menu", 28, lfb_wysokosc - 28, kolor_menu_txt, 2);
        
        int taskbar_x = 130;
        for (int i = 0; i < 2; i++) {
            uint32_t kolor_btn = 0x008A5A00;
            uint32_t kolor_tekstu_btn = 0x00D1D5DB; 
            
            if (!okna[i].widoczne) { kolor_btn = 0x001A0B00; kolor_tekstu_btn = 0x008A5A00; }
            else if (z_order[1] == i && !menu_otwarte) { kolor_btn = 0x00E58A00; kolor_tekstu_btn = 0x001A0B00; }
            
            RysujProstokat(taskbar_x, lfb_wysokosc - 35, 180, 30, kolor_btn);
            WypiszTekst(okna[i].krotka_nazwa, taskbar_x + 10, lfb_wysokosc - 28, kolor_tekstu_btn, 2);
            taskbar_x += 190;
        }

        // Rysujemy po prawej stronie ekranu uzywajac wspolnego helpera (zgodnosc z przerwaniami)
        rysuj_zegar_rtc();
    }

    if (menu_otwarte) {
        RysujMenu();
    }
}

void DopiszZnakDoEdytora(char c) {
    if (c == '\n' || c == '\r') { edit_r++; edit_c = 0; }
    else if (c == '\b') {
        if (edit_c > 0) { edit_c--; edit_buf[edit_r][edit_c].znak = 0; } 
        else if (edit_r > 0) {
            edit_r--; edit_c = edit_max_c - 1;
            while(edit_c > 0 && edit_buf[edit_r][edit_c].znak == 0) edit_c--;
            if (edit_buf[edit_r][edit_c].znak != 0) edit_c++;
        }
    } else {
        edit_buf[edit_r][edit_c].znak = c;
        edit_buf[edit_r][edit_c].kolor = 0x00D1D5DB; 
        edit_c++;
        if (edit_c >= edit_max_c) { edit_r++; edit_c = 0; }
    }
    if (edit_r >= edit_max_r) {
        for(int r = 1; r < edit_max_r; r++) for(int c = 0; c < edit_max_c; c++) edit_buf[r-1][c] = edit_buf[r][c];
        for(int c = 0; c < edit_max_c; c++) edit_buf[edit_max_r-1][c].znak = 0;
        edit_r = edit_max_r - 1;
    }
}

void DopiszDoBufora(const char* tekst, uint32_t kolor) {
    for(int i = 0; tekst[i] != '\0'; i++) {
        if (tekst[i] == '\n') { term_r++; term_c = 0; }
        else if (tekst[i] == '\r') { term_c = 0; }
        else if (tekst[i] == '\b') { if (term_c > 0) { term_c--; term_buf[term_r][term_c].znak = 0; } }
        else {
            term_buf[term_r][term_c].znak = tekst[i]; term_buf[term_r][term_c].kolor = kolor; term_c++;
            if (term_c >= term_max_c) { term_r++; term_c = 0; }
        }
        if (term_r >= term_max_r) {
            for(int r = 1; r < term_max_r; r++) for(int c = 0; c < term_max_c; c++) term_buf[r-1][c] = term_buf[r][c];
            for(int c = 0; c < term_max_c; c++) term_buf[term_max_r-1][c].znak = 0;
            term_r = term_max_r - 1;
        }
    }
}

extern "C" bool zaktualizuj_klawiature_gui(char znak) {
    if (!backbuffer) return false;
    
    if (menu_otwarte) {
        UkryjKursor();
        if (znak == '\b') {
            if (menu_szukaj_len > 0) { menu_szukaj_len--; menu_szukaj[menu_szukaj_len] = '\0'; }
        } else if (znak >= 32 && znak <= 126 && menu_szukaj_len < 30) {
            menu_szukaj[menu_szukaj_len++] = znak; menu_szukaj[menu_szukaj_len] = '\0';
        }
        OdswiezEkran();
        PokazKursor(); PrzeniesNaEkran();
        return true; 
    }

    int aktywne_okno = z_order[1];
    if (!okna[aktywne_okno].widoczne) aktywne_okno = z_order[0];
    
    if (aktywne_okno == 1 && okna[1].widoczne) { 
        UkryjKursor();
        DopiszZnakDoEdytora(znak);
        OdswiezEkran();
        PokazKursor(); PrzeniesNaEkran();
        return true; 
    }
    return false; 
}

void UkryjKursor() {
    if (!kursor_widoczny || !backbuffer) return;
    for(int y=0; y<16; y++) {
        for(int x=0; x<16; x++) {
            if (mysz_x + x < (int)lfb_szerokosc && mysz_y + y < (int)lfb_wysokosc && mysz_x >= 0 && mysz_y >= 0) {
                PostawPiksel(mysz_x + x, mysz_y + y, bufor_kursora[y][x]);
            }
        }
    }
    kursor_widoczny = false;
}

void PokazKursor() {
    if (kursor_widoczny || !backbuffer) return;
    for(int y=0; y<16; y++) {
        for(int x=0; x<16; x++) {
            if (mysz_x + x < (int)lfb_szerokosc && mysz_y + y < (int)lfb_wysokosc && mysz_x >= 0 && mysz_y >= 0) {
                bufor_kursora[y][x] = PobierzPiksel(mysz_x + x, mysz_y + y);
                uint8_t typ_piksela = kursor_bitmapa[y][x];
                if (typ_piksela == 1) PostawPiksel(mysz_x + x, mysz_y + y, 0x00000000); 
                else if (typ_piksela == 2) PostawPiksel(mysz_x + x, mysz_y + y, 0x00FFFFFF); 
            }
        }
    }
    kursor_widoczny = true;
}

static void OgraniczOkno(Okno& o) {
    if (o.zmaksymalizowane) return; 
    int32_t min_x = -(int32_t)o.szer + 80; int32_t max_x = (int32_t)lfb_szerokosc - 80;
    int32_t min_y = 0; int32_t max_y = (int32_t)lfb_wysokosc - 40; 
    
    if ((int32_t)o.x < min_x) o.x = (uint32_t)min_x;
    if ((int32_t)o.x > max_x) o.x = (uint32_t)max_x;
    if ((int32_t)o.y < min_y) o.y = (uint32_t)min_y;
    if ((int32_t)o.y > max_y) o.y = (uint32_t)max_y;
}

static void WyciagnijNaWierzch(int id_okna) {
    if (z_order[1] == id_okna) return; 
    z_order[0] = z_order[1]; z_order[1] = id_okna;
}

extern "C" void zaktualizuj_mysze(int dx, int dy, uint8_t przyciski) {
    if (!backbuffer) return;
    int stary_mysz_x = mysz_x; int stary_mysz_y = mysz_y;
    
    UkryjKursor();
    mysz_x += dx; mysz_y -= dy; 
    if (mysz_x < 0) mysz_x = 0;
    if (mysz_x >= (int)lfb_szerokosc - 2) mysz_x = lfb_szerokosc - 2;
    if (mysz_y < 0) mysz_y = 0;
    if (mysz_y >= (int)lfb_wysokosc - 2) mysz_y = lfb_wysokosc - 2;
    
    bool nowy_lewy = (przyciski & 0x01);
    bool klik_lewy = (nowy_lewy && !lewy_wcisniety);
    bool puszcz_lewy = (!nowy_lewy && lewy_wcisniety);
    
    bool wymaga_odrysowania = false;
    
    if (klik_lewy && okno_przeciagane == -1) {
        bool przechwycono = false;

        if (mysz_x >= 0 && mysz_x <= 120 && mysz_y >= (int)lfb_wysokosc - 40) {
            menu_otwarte = !menu_otwarte;
            wymaga_odrysowania = true;
            przechwycono = true;
        }

        if (!przechwycono && menu_otwarte) {
            int menu_szer = 300;
            int menu_wys = 400;
            int menu_x = 0;
            int menu_y = lfb_wysokosc - 40 - menu_wys;

            if (mysz_x >= menu_x && mysz_x <= menu_x + menu_szer && mysz_y >= menu_y && mysz_y <= menu_y + menu_wys) {
                for (int i = 0; i < 2; i++) {
                    if (aktualne_menu_y[i] != -1 && mysz_y >= aktualne_menu_y[i] && mysz_y <= aktualne_menu_y[i] + 40) {
                        okna[menu_aplikacje[i].id_okna].widoczne = true;
                        WyciagnijNaWierzch(menu_aplikacje[i].id_okna);
                        menu_otwarte = false;
                        menu_szukaj[0] = '\0';
                        menu_szukaj_len = 0;
                        wymaga_odrysowania = true;
                        przechwycono = true;
                        break;
                    }
                }
                if (!przechwycono) przechwycono = true; 
            } else {
                menu_otwarte = false;
                wymaga_odrysowania = true;
            }
        }
        
        if (!przechwycono && mysz_y >= (int)lfb_wysokosc - 40) { 
            int taskbar_x = 130;
            for (int i = 0; i < 2; i++) {
                if (mysz_x >= taskbar_x && mysz_x <= taskbar_x + 180) {
                    if (okna[i].widoczne && z_order[1] == i) okna[i].widoczne = false;
                    else { okna[i].widoczne = true; WyciagnijNaWierzch(i); }
                    wymaga_odrysowania = true;
                    przechwycono = true;
                    break;
                }
                taskbar_x += 190;
            }
        }
        
        if (!przechwycono) {
            for (int k = 1; k >= 0; k--) {
                int i = z_order[k];
                if (!okna[i].widoczne) continue;
                
                int px = (int)okna[i].x; int py = (int)okna[i].y; int sz = (int)okna[i].szer; int wy = (int)okna[i].wys;
                
                if (mysz_x >= px && mysz_x <= px + sz && mysz_y >= py && mysz_y <= py + wy) {
                    WyciagnijNaWierzch(i);
                    wymaga_odrysowania = true;
                    
                    if (mysz_y <= py + 26) {
                        if (mysz_x >= px + sz - 26 && mysz_x <= px + sz - 6) { 
                            okna[i].widoczne = false; break;
                        }
                        if (mysz_x >= px + sz - 50 && mysz_x <= px + sz - 30) { 
                            if (!okna[i].zmaksymalizowane) {
                                okna[i].stary_x = okna[i].x; okna[i].stary_y = okna[i].y;
                                okna[i].stary_szer = okna[i].szer; okna[i].stary_wys = okna[i].wys;
                                okna[i].x = 0; okna[i].y = 0;
                                okna[i].szer = lfb_szerokosc; okna[i].wys = lfb_wysokosc - 40; 
                                okna[i].zmaksymalizowane = true;
                            } else {
                                okna[i].x = okna[i].stary_x; okna[i].y = okna[i].stary_y;
                                okna[i].szer = okna[i].stary_szer; okna[i].wys = okna[i].stary_wys;
                                okna[i].zmaksymalizowane = false;
                            }
                            if (i == 0) {
                                term_max_c = (okna[0].szer - 12) / 8;
                                term_max_r = (okna[0].wys - 36) / 12;
                                if (term_max_c > MAX_COLS) term_max_c = MAX_COLS;
                                if (term_max_r > MAX_ROWS) term_max_r = MAX_ROWS;
                            } else if (i == 1) {
                                edit_max_c = (okna[1].szer - 12) / 8;
                                edit_max_r = (okna[1].wys - 36) / 12;
                                if (edit_max_c > MAX_COLS) edit_max_c = MAX_COLS;
                                if (edit_max_r > MAX_ROWS) edit_max_r = MAX_ROWS;
                            }
                            break;
                        }
                        if (mysz_x >= px + sz - 74 && mysz_x <= px + sz - 54) { 
                            okna[i].widoczne = false; break;
                        }
                        if (!okna[i].zmaksymalizowane && mysz_x < px + sz - 79) { 
                            okno_przeciagane = i;
                            chwyt_x = mysz_x - okna[i].x; chwyt_y = mysz_y - okna[i].y;
                        }
                    }
                    break; 
                }
            }
        }
    }
    else if (puszcz_lewy && okno_przeciagane != -1) {
        okno_przeciagane = -1;
    }
    
    if (okno_przeciagane != -1 && nowy_lewy && (dx != 0 || dy != 0)) {
        okna[okno_przeciagane].x = mysz_x - chwyt_x;
        okna[okno_przeciagane].y = mysz_y - chwyt_y;
        OgraniczOkno(okna[okno_przeciagane]);
        wymaga_odrysowania = true;
    }
    
    lewy_wcisniety = nowy_lewy;

    if (wymaga_odrysowania) { OdswiezEkran(); PokazKursor(); PrzeniesNaEkran(); } 
    else { PokazKursor(); PrzeniesFragmentNaEkran(stary_mysz_x, stary_mysz_y, 16, 16); PrzeniesFragmentNaEkran(mysz_x, mysz_y, 16, 16); }
}

extern "C" void* PobierzAktualnePML4();

void InicjalizujGrafike(uint64_t adres_mb2) {
    if(adres_mb2 == 0) return;
    uint32_t rozmiar = *(uint32_t*)adres_mb2; uint64_t aktualny = adres_mb2 + 8;
    uint64_t lfb_fizyczny = 0;
    while(aktualny < adres_mb2 + rozmiar) {
        TagFramebufferMB2* tag = (TagFramebufferMB2*)aktualny;
        if(tag->typ == 0) break;
        if(tag->typ == 8) { 
            lfb_fizyczny = tag->adres_fizyczny; lfb_pitch = tag->pitch; lfb_szerokosc = tag->szerokosc; lfb_wysokosc = tag->wysokosc; lfb_bpp = tag->bpp; break;
        }
        aktualny += (tag->rozmiar + 7) & ~7;
    }
    
    if(lfb_fizyczny != 0 && lfb_szerokosc > 0 && lfb_wysokosc > 0 && lfb_pitch > 0) {
        uint64_t lfb_waga = (uint64_t)lfb_pitch * (uint64_t)lfb_wysokosc;
        uint64_t map_limit = (lfb_waga + 4095) & ~4095ULL;
        
        for(uint64_t i = 0; i < map_limit; i += 4096) ZmapujStrone((void*)(lfb_fizyczny + i), (void*)(lfb_fizyczny + i), 0b11 | 0x10);
        lfb = (uint32_t*)lfb_fizyczny;
        
        uint64_t vaddr_backbuffer = 0x80000000ULL; 
        for(uint64_t i = 0; i < map_limit; i += 4096) ZmapujStrone((void*)(vaddr_backbuffer + i), (void*)0, 0b11);
        for(uint64_t i = 0; i < map_limit; i += 4096) { void* ramka = ZaalokujRamke(); if(ramka) ZmapujStrone((void*)(vaddr_backbuffer + i), ramka, 0b11); }
        asm volatile("mov %0, %%cr3" : : "r"(PobierzAktualnePML4()) : "memory");
        backbuffer = (uint8_t*)vaddr_backbuffer;
        
        for(uint64_t i = 0; i < lfb_waga; i++) backbuffer[i] = 0;
        for(int r = 0; r < MAX_ROWS; r++) { for(int c = 0; c < MAX_COLS; c++) { term_buf[r][c].znak = 0; edit_buf[r][c].znak = 0; } }
        
        if (okna[0].x + okna[0].szer > lfb_szerokosc) okna[0].szer = lfb_szerokosc - okna[0].x - 20;
        if (okna[0].y + okna[0].wys > lfb_wysokosc - 40) okna[0].wys = lfb_wysokosc - okna[0].y - 40;
        okna[1].szer = 560;
        if (okna[1].x + okna[1].szer > lfb_szerokosc - 20) { okna[1].x = 20; if (okna[1].x + okna[1].szer > lfb_szerokosc - 20) okna[1].szer = lfb_szerokosc - 40; }
        if (okna[1].y + okna[1].wys > lfb_wysokosc - 40) okna[1].wys = lfb_wysokosc - okna[1].y - 40;
        OgraniczOkno(okna[0]); OgraniczOkno(okna[1]);
        
        term_max_c = (okna[0].szer - 12) / 8;
        term_max_r = (okna[0].wys - 36) / 12;
        if (term_max_c > MAX_COLS) term_max_c = MAX_COLS;
        if (term_max_r > MAX_ROWS) term_max_r = MAX_ROWS;
        edit_max_c = (okna[1].szer - 12) / 8;
        edit_max_r = (okna[1].wys - 36) / 12;
        if (edit_max_c > MAX_COLS) edit_max_c = MAX_COLS;
        if (edit_max_r > MAX_ROWS) edit_max_r = MAX_ROWS;
        
        const char* hlo = "Witaj w Edytorze Avocado! Mozesz wprowadzac tu notatki.\n";
        for(int i = 0; hlo[i] != '\0'; i++) DopiszZnakDoEdytora(hlo[i]);
        
        OdswiezEkran(); PokazKursor(); PrzeniesNaEkran();     
    } else {
        while(true) asm volatile("cli; hlt");
    }
}

// ==================== ZEWNETRZNE API ====================
void WypiszLog(const char* tekst) {
    if(!backbuffer) return;
    UkryjKursor();
    DopiszDoBufora(tekst, 0x00FFBF00);
    DopiszDoBufora("\n", 0x00FFBF00);
    OdswiezEkran();
    PokazKursor();
    PrzeniesNaEkran();
}

extern "C" void wypisz_na_ekranie(const char* tekst) {
    if(!backbuffer) return;
    UkryjKursor();
    DopiszDoBufora(tekst, 0x00E58A00);
    OdswiezEkran();
    PokazKursor();
    PrzeniesNaEkran();
}

// Funkcja obsługująca sprzętowe przerwania Zegara RTC wprost z idt.cpp (Wektor 32)
// Odświeża malutki prostokąt, jeśli nastąpiła zmiana fizycznej sekundy.
extern "C" void obsluga_przerwania_zegara() {
    static uint8_t stara_sekunda = 255;
    czas_rtc czas;
    pobierz_czas_rtc(&czas);
    
    // Rysuj tylko wtedy, gdy zmieniła się sekunda (nie obciąża procesora setkami rysowań w ciągu sekundy)
    if (czas.sekundy != stara_sekunda) {
        stara_sekunda = czas.sekundy;
        
        if (!backbuffer || lfb_wysokosc < 40) return;
        
        bool kursor_byl = kursor_widoczny;
        if (kursor_byl) UkryjKursor();
        
        // Funkcja rysujaca tylko i wyłacznie czas
        rysuj_zegar_rtc();
        
        // Odświeżamy TYLKO ten mały wycinek matrycy by nie blokować GUI
        int zegar_x = lfb_szerokosc - 150;
        int zegar_y = lfb_wysokosc - 28;
        PrzeniesFragmentNaEkran(zegar_x, zegar_y - 7, 140, 24);
        
        if (kursor_byl) PokazKursor();
    }
}