#include "grafika.h"
#include "pamiec.h"

static uint32_t* lfb = nullptr;
static uint32_t  lfb_szerokosc = 0;
static uint32_t  lfb_wysokosc = 0;
static uint32_t  lfb_pitch = 0;
static uint8_t   lfb_bpp = 32;

static uint8_t* backbuffer = nullptr;

struct Okno {
    int x, y, szer, wys;
    int stary_x, stary_y, stary_szer, stary_wys; // Pamiec dla maksymalizacji
    const char* tytul;
    const char* krotka_nazwa; // Wyswietlane na pasku zadan
    uint32_t kolor_tla;
    bool widoczne;
    bool zmaksymalizowane;
};

// Delikatnie poszerzamy okno terminala, by przy skali=1 pieknie pomiescilo 80 kolumn!
static Okno okna[2] = {
    { 20, 20, 660, 360,  0,0,0,0, "Powloka Bursztyna (Ring 3 Terminal)", "Terminal", 0x001A0B00, true, false },
    { 180, 80, 560, 400, 0,0,0,0, "Edytor Avocado - Nowy Plik", "Edytor", 0x00280F00, true, false }
};

static int z_order[2] = {1, 0}; // 0 = spod, 1 = wierzch

static int okno_przeciagane = -1;
static int chwyt_x = 0;
static int chwyt_y = 0;

struct ZnakTerminala {
    char znak;
    uint32_t kolor;
};
// Zwiekszone limity tablicy, aby zmiescic znaki na zmaksymalizowanym ekranie 1024x768
#define MAX_ROWS 80
#define MAX_COLS 140
static ZnakTerminala term_buf[MAX_ROWS][MAX_COLS];
static int term_r = 0, term_c = 0;
static int term_max_r = 25, term_max_c = 80;

static int mysz_x = 500;
static int mysz_y = 300;
static uint32_t bufor_kursora[16][16];
static bool kursor_widoczny = false;
static uint8_t ostatnie_przyciski = 0;

static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
void SerialLog(const char* str) {
    for(int i = 0; str[i] != '\0'; i++) serial_outb(0x3F8, str[i]);
} 
static void SerialLogHex(uint32_t val) {
    const char* hex = "0123456789ABCDEF";
    char buf[11] = "0x00000000";
    for(int i = 0; i < 8; i++) buf[9 - i] = hex[(val >> (i * 4)) & 0xF];
    SerialLog(buf);
}

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

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t val; asm volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
void BochsVBE_Write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}
uint16_t BochsVBE_Read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static const uint8_t kursor_bitmapa[16][16] = {
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,1,1,1,1,0,0,0,0},
    {1,2,2,1,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,2,1,0,0,0,0,0,0,0},
    {1,1,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0}
};

static const uint8_t czcionka_8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},{0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00},{0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},{0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},{0x00,0x66,0x6C,0x18,0x36,0x66,0x00,0x00},{0x38,0x6C,0x6C,0x38,0x6D,0x66,0x3B,0x00},{0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},{0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},{0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},{0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},{0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},{0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00},{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},{0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00},{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},{0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00},{0x7E,0x66,0x0C,0x18,0x30,0x30,0x30,0x00},{0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},{0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00},{0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},{0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},{0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},{0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},{0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00},
    {0x3C,0x66,0x6E,0x6E,0x60,0x66,0x3C,0x00},{0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00},{0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},{0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},{0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},{0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},{0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00},{0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},{0x3E,0x18,0x18,0x18,0x18,0x18,0x3E,0x00},{0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00},{0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},{0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},{0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},{0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},{0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},{0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},{0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},{0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},{0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},{0x63,0x63,0x6B,0x7F,0x77,0x63,0x63,0x00},{0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},{0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},{0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},{0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},{0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x00},{0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},{0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00},
    {0x60,0x30,0x18,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},{0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},{0x00,0x00,0x3C,0x60,0x60,0x66,0x3C,0x00},{0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},{0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},{0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00},{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},{0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},{0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},{0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x70},{0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},{0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},{0x00,0x00,0x6C,0xFE,0xD6,0xD6,0xC6,0x00},{0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},{0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},{0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},{0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},{0x30,0x30,0x7C,0x30,0x30,0x34,0x18,0x00},{0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},{0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00},{0x00,0x00,0xC6,0xD6,0xFE,0x6C,0x6C,0x00},{0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},{0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C},{0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},{0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},{0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},{0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},{0x3B,0x6E,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

void PrzeniesNaEkran() {
    if (!lfb || !backbuffer) return;
    volatile uint32_t* dst = (volatile uint32_t*)lfb;
    uint32_t* src = (uint32_t*)backbuffer;
    
    uint64_t ilosc_pikseli = ((uint64_t)lfb_pitch * (uint64_t)lfb_wysokosc) / 4;
    for(uint64_t i = 0; i < ilosc_pikseli; i++) {
        dst[i] = src[i];
    }
}

void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys) {
    if(!lfb || !backbuffer) return;
    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = x + szer;
    int end_y = y + wys;
    
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
    
    if (lfb_bpp == 32) {
        *(uint32_t*)piksel = kolor;
    } else if (lfb_bpp == 24) {
        piksel[0] = kolor & 0xFF;         
        piksel[1] = (kolor >> 8) & 0xFF;  
        piksel[2] = (kolor >> 16) & 0xFF; 
    } else if (lfb_bpp == 16) {
        uint16_t r = (kolor >> 16) & 0xFF;
        uint16_t g = (kolor >> 8) & 0xFF;
        uint16_t b = kolor & 0xFF;
        uint16_t k16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        *(uint16_t*)piksel = k16;
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
        uint8_t r = ((k >> 11) & 0x1F) << 3;
        uint8_t g = ((k >> 5) & 0x3F) << 2;
        uint8_t b = (k & 0x1F) << 3;
        return (r << 16) | (g << 8) | b;
    }
    return 0;
}

void RysujProstokat(int px, int py, int szer, int wys, uint32_t kolor) {
    int start_x = px < 0 ? 0 : px;
    int start_y = py < 0 ? 0 : py;
    int end_x = px + szer;
    int end_y = py + wys;
    if (end_x > (int)lfb_szerokosc) end_x = lfb_szerokosc;
    if (end_y > (int)lfb_wysokosc) end_y = lfb_wysokosc;
    
    for(int y = start_y; y < end_y; y++) {
        for(int x = start_x; x < end_x; x++) {
            PostawPiksel(x, y, kolor);
        }
    }
}

void RysujZnak(char c, int px, int py, uint32_t kolor_tekstu, uint32_t kolor_tla, bool przezroczyste_tlo, int skala) {
    if(c < 32 || c > 126) c = '?';
    const uint8_t* znak = czcionka_8x8[c - 32];
    for(int y = 0; y < 8; y++) {
        for(int x = 0; x < 8; x++) {
            bool zmaluj = znak[y] & (1 << (7 - x));
            if(zmaluj) {
                for(int sy=0; sy<skala; sy++)
                    for(int sx=0; sx<skala; sx++)
                        PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tekstu);
            } else if (!przezroczyste_tlo) { 
                for(int sy=0; sy<skala; sy++)
                    for(int sx=0; sx<skala; sx++)
                        PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tla);
            }
        }
    }
}

void WypiszTekst(const char* tekst, int px, int py, uint32_t kolor_tekstu, int skala) {
    int start_x = px;
    for(int i = 0; tekst[i] != '\0'; i++) {
        RysujZnak(tekst[i], start_x, py, kolor_tekstu, 0, true, skala);
        start_x += 8 * skala;
    }
}

void RysujOkno(int id) {
    if (!okna[id].widoczne) return;
    int px = okna[id].x;
    int py = okna[id].y;
    int szer = okna[id].szer;
    int wys = okna[id].wys;

    if (szer < 10 || wys < 40) return;
    
    // Cien / obrys okna
    RysujProstokat(px, py, szer, wys, 0x008A5A00);             
    
    // Belka Tytulowa (Kolor zalezy od tego, czy okno ma Focus)
    bool aktywne = (z_order[1] == id);
    uint32_t kolor_paska = aktywne ? 0x00FFBF00 : 0x008A5A00;
    RysujProstokat(px + 2, py + 2, szer - 4, 24, kolor_paska);  
    
    // Jasna (aktywna) belka = ciemny tekst; Ciemna belka = zgaszony tekst
    uint32_t kolor_tekstu_paska = aktywne ? 0x001A0B00 : 0x00D1D5DB;
    // Tytul okna pozostawiamy w skali=2 by ladnie wygladal w GUI
    WypiszTekst(okna[id].tytul, px + 8, py + 6, kolor_tekstu_paska, 2);         
    
    // Przycisk MAKSYMALIZUJ [ ^ ] lub PRZYWROC [ v ]
    RysujProstokat(px + szer - 50, py + 4, 20, 20, 0x00E58A00);
    WypiszTekst(okna[id].zmaksymalizowane ? "v" : "^", px + szer - 46, py + 6, 0x001A0B00, 2);

    // Przycisk ZAMKNIJ/MINIMALIZUJ [ X ]
    RysujProstokat(px + szer - 26, py + 4, 20, 20, 0x00AA0000);
    WypiszTekst("X", px + szer - 22, py + 6, 0x00FFFFFF, 2);

    // Wnetrze
    RysujProstokat(px + 2, py + 28, szer - 4, wys - 30, okna[id].kolor_tla); 
}

void RysujZawartoscTerminala(int px, int py, int szer, int wys) {
    // NAPRAWA: Zmniejszono skale terminala z 2 do 1, by zmiescil 80 kolumn w normalnym oknie
    int skala = 1;
    int wysokosc_linii = 12; // 8 pikseli wysokosci + 4 piksele przerwy
    int start_x = px + 6;
    int start_y = py + 28 + 4;
    
    for (int r = 0; r < term_max_r; r++) {
        int cx = start_x;
        int cy = start_y + (r * wysokosc_linii);
        
        // ZABEZPIECZENIE: Zatrzymanie renderowania przed wyjsciem za ramke osi Y
        if (cy + wysokosc_linii >= py + wys) break;
        
        for (int c = 0; c < term_max_c; c++) {
            char z = term_buf[r][c].znak;
            if (z != 0) {
                // ZABEZPIECZENIE: Zatrzymanie renderowania przed wyjsciem za ramke osi X
                if (cx + 8*skala >= px + szer - 6) break;
                RysujZnak(z, cx, cy, term_buf[r][c].kolor, 0, true, skala);
            }
            cx += 8 * skala;
        }
    }
}

void DopiszDoBufora(const char* tekst, uint32_t kolor) {
    for(int i = 0; tekst[i] != '\0'; i++) {
        if (tekst[i] == '\n') {
            term_r++; term_c = 0;
        } else if (tekst[i] == '\r') {
            term_c = 0;
        } else if (tekst[i] == '\b') {
            if (term_c > 0) {
                term_c--;
                term_buf[term_r][term_c].znak = 0;
            }
        } else {
            term_buf[term_r][term_c].znak = tekst[i];
            term_buf[term_r][term_c].kolor = kolor;
            term_c++;
            if (term_c >= term_max_c) {
                term_r++; term_c = 0;
            }
        }
        
        if (term_r >= term_max_r) {
            for(int r = 1; r < term_max_r; r++) {
                for(int c = 0; c < term_max_c; c++) {
                    term_buf[r-1][c] = term_buf[r][c];
                }
            }
            for(int c = 0; c < term_max_c; c++) {
                term_buf[term_max_r-1][c].znak = 0;
            }
            term_r = term_max_r - 1;
        }
    }
}

void OdswiezEkran() {
    if(!backbuffer) return;
    
    // Tlo Pulpitu
    RysujProstokat(0, 0, lfb_szerokosc, lfb_wysokosc, 0x001A0B00);
    
    // Pasek Zadan
    if (lfb_wysokosc >= 40) {
        RysujProstokat(0, lfb_wysokosc - 40, lfb_szerokosc, 40, 0x00280F00);
        RysujProstokat(0, lfb_wysokosc - 40, lfb_szerokosc, 2, 0x008A5A00);
        
        WypiszTekst("Menu", 10, lfb_wysokosc - 28, 0x00FFBF00, 2);
        
        int taskbar_x = 100;
        for (int i = 0; i < 2; i++) {
            uint32_t kolor_btn = 0x008A5A00;
            uint32_t kolor_tekstu_btn = 0x00D1D5DB; 
            
            if (!okna[i].widoczne) {
                kolor_btn = 0x001A0B00; 
                kolor_tekstu_btn = 0x008A5A00;
            }
            else if (z_order[1] == i) {
                kolor_btn = 0x00E58A00; 
                kolor_tekstu_btn = 0x001A0B00;
            }
            
            RysujProstokat(taskbar_x, lfb_wysokosc - 35, 200, 30, kolor_btn);
            WypiszTekst(okna[i].krotka_nazwa, taskbar_x + 10, lfb_wysokosc - 28, kolor_tekstu_btn, 2);
            taskbar_x += 210;
        }
    }
    
    // Z-Order
    for(int k = 0; k < 2; k++) {
        int i = z_order[k];
        if (!okna[i].widoczne) continue;
        
        RysujOkno(i);
        
        if (i == 0) {
            RysujZawartoscTerminala(okna[i].x, okna[i].y, okna[i].szer, okna[i].wys);
        } else if (i == 1) {
            if (okna[1].szer > 150 && okna[1].wys > 150) {
                // Dostosowano skale w edytorze z 2 do 1, zeby dopasowac wyglad do nowych proporcji
                WypiszTekst("Edytor Avocado - Gotowy!", okna[1].x + 20, okna[1].y + 50, 0x00FFBF00, 1);
                WypiszTekst("Przycisk [X] minimalizuje okno", okna[1].x + 20, okna[1].y + 70, 0x00D1D5DB, 1);
                WypiszTekst("na pasek zadan w dolnym ekranie.", okna[1].x + 20, okna[1].y + 90, 0x00D1D5DB, 1);
                WypiszTekst("Pomaranczowy przycisk [^] maksymalizuje", okna[1].x + 20, okna[1].y + 110, 0x00E58A00, 1);
            }
        }
    }
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

static bool TrafieniePasekTyulu(const Okno& o, int mx, int my) {
    if (mx < o.x + 2 || mx >= o.x + o.szer - 55) return false;
    if (my < o.y + 2 || my >= o.y + 26)          return false;
    return true;
}

static bool TrafieniePrzycisk(const Okno& o, int mx, int my, int offset_x) {
    if (mx >= o.x + o.szer - offset_x && mx <= o.x + o.szer - offset_x + 20 &&
        my >= o.y + 4 && my <= o.y + 24) return true;
    return false;
}

static bool TrafienieCaleOkno(const Okno& o, int mx, int my) {
    if (mx < o.x || mx >= o.x + o.szer) return false;
    if (my < o.y || my >= o.y + o.wys)  return false;
    return true;
}

static void OgraniczOkno(Okno& o) {
    if (o.zmaksymalizowane) return; 
    int32_t min_x = -(int32_t)o.szer + 80;
    int32_t max_x = (int32_t)lfb_szerokosc - 80;
    int32_t min_y = 0;
    int32_t max_y = (int32_t)lfb_wysokosc - 40; 

    if ((int32_t)o.x < min_x) o.x = (uint32_t)min_x;
    if ((int32_t)o.x > max_x) o.x = (uint32_t)max_x;
    if ((int32_t)o.y < min_y) o.y = (uint32_t)min_y;
    if ((int32_t)o.y > max_y) o.y = (uint32_t)max_y;
}

static void WyciagnijNaWierzch(int id_okna) {
    if (z_order[1] == id_okna) return; 
    z_order[0] = z_order[1];
    z_order[1] = id_okna;
}

extern "C" void ZaktualizujMysze(int dx, int dy, uint8_t przyciski) {
    if (!backbuffer) return;
    int stary_mysz_x = mysz_x;
    int stary_mysz_y = mysz_y;
    
    UkryjKursor();
    
    mysz_x += dx;
    mysz_y -= dy; 
    
    if (mysz_x < 0) mysz_x = 0;
    if (mysz_x >= (int)lfb_szerokosc - 2) mysz_x = lfb_szerokosc - 2;
    if (mysz_y < 0) mysz_y = 0;
    if (mysz_y >= (int)lfb_wysokosc - 2) mysz_y = lfb_wysokosc - 2;
    
    const uint8_t LEWY = 0x01;
    bool lewy_teraz   = (przyciski & LEWY) != 0;
    bool lewy_poprz   = (ostatnie_przyciski & LEWY) != 0;
    bool klik_lewy    = (lewy_teraz && !lewy_poprz);
    bool puszcz_lewy  = (!lewy_teraz && lewy_poprz);
    ostatnie_przyciski = przyciski;
    
    bool wymaga_odrysowania = false;
    
    if (klik_lewy && okno_przeciagane == -1) {
        bool przechwycono = false;
        
        // Pasek Zadan na samym dole
        if (mysz_y >= (int)lfb_wysokosc - 40) {
            int taskbar_x = 100;
            for (int i = 0; i < 2; i++) {
                if (mysz_x >= taskbar_x && mysz_x <= taskbar_x + 200) {
                    if (okna[i].widoczne && z_order[1] == i) okna[i].widoczne = false;
                    else {
                        okna[i].widoczne = true;
                        WyciagnijNaWierzch(i);
                    }
                    wymaga_odrysowania = true;
                    przechwycono = true;
                    break;
                }
                taskbar_x += 210;
            }
        }
        
        if (!przechwycono) {
            for (int k = 1; k >= 0; k--) {
                int i = z_order[k];
                if (!okna[i].widoczne) continue;
                
                if (TrafieniePrzycisk(okna[i], mysz_x, mysz_y, 26)) { 
                    okna[i].widoczne = false;
                    wymaga_odrysowania = true;
                    break;
                }
                else if (TrafieniePrzycisk(okna[i], mysz_x, mysz_y, 50)) { // MAKSYMALIZACJA
                    WyciagnijNaWierzch(i);
                    if (!okna[i].zmaksymalizowane) {
                        okna[i].stary_x = okna[i].x; okna[i].stary_y = okna[i].y;
                        okna[i].stary_szer = okna[i].szer; okna[i].stary_wys = okna[i].wys;
                        okna[i].x = 0; okna[i].y = 0;
                        okna[i].szer = lfb_szerokosc; 
                        okna[i].wys = lfb_wysokosc - 40; 
                        okna[i].zmaksymalizowane = true;
                    } else {
                        okna[i].x = okna[i].stary_x; okna[i].y = okna[i].stary_y;
                        okna[i].szer = okna[i].stary_szer; okna[i].wys = okna[i].stary_wys;
                        okna[i].zmaksymalizowane = false;
                    }
                    
                    // NAPRAWA: Zaktualizuj i przelicz rzedy/kolumny dla Terminala!
                    if (i == 0) {
                        term_max_c = (okna[0].szer - 12) / 8;
                        term_max_r = (okna[0].wys - 36) / 12;
                        if (term_max_c > MAX_COLS) term_max_c = MAX_COLS;
                        if (term_max_r > MAX_ROWS) term_max_r = MAX_ROWS;
                    }
                    wymaga_odrysowania = true;
                    break;
                }
                else if (TrafieniePasekTyulu(okna[i], mysz_x, mysz_y)) { 
                    WyciagnijNaWierzch(i);
                    if (!okna[i].zmaksymalizowane) { 
                        okno_przeciagane = i;
                        chwyt_x = mysz_x - okna[i].x;
                        chwyt_y = mysz_y - okna[i].y;
                    }
                    wymaga_odrysowania = true;
                    break;
                }
                else if (TrafienieCaleOkno(okna[i], mysz_x, mysz_y)) { 
                    WyciagnijNaWierzch(i);
                    wymaga_odrysowania = true;
                    break;
                }
            }
        }
    }
    else if (puszcz_lewy && okno_przeciagane != -1) {
        okno_przeciagane = -1;
    }
    
    // Przesuwanie okna
    if (okno_przeciagane != -1 && lewy_teraz && (dx != 0 || dy != 0)) {
        okna[okno_przeciagane].x = mysz_x - chwyt_x;
        okna[okno_przeciagane].y = mysz_y - chwyt_y;
        OgraniczOkno(okna[okno_przeciagane]);
        wymaga_odrysowania = true;
    }
    
    if (wymaga_odrysowania) {
        OdswiezEkran();      
        PokazKursor();       
        PrzeniesNaEkran();   
    } else {
        PokazKursor();
        PrzeniesFragmentNaEkran(stary_mysz_x, stary_mysz_y, 16, 16);
        PrzeniesFragmentNaEkran(mysz_x, mysz_y, 16, 16);
    }
}

extern "C" void* PobierzAktualnePML4();

void InicjalizujGrafike(uint64_t adres_mb2) {
    SerialLog("[GRAFIKA] Sprawdzanie natywnego interfejsu Bochs VBE (QEMU)...\n");
    uint16_t bga_id = BochsVBE_Read(VBE_DISPI_INDEX_ID);
    
    SerialLog("[GRAFIKA] ID Karty Graficznej VBE znalezione: ");
    SerialLogHex(bga_id); SerialLog("\n");
    
    uint64_t lfb_fizyczny = 0;
    
    if (bga_id >= 0xB0C0 && bga_id <= 0xB0C6) {
        SerialLog("[GRAFIKA] ZNALEZIONO KONTROLER VBE! Wymuszanie 1024x768x32...\n");
        BochsVBE_Write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        BochsVBE_Write(VBE_DISPI_INDEX_XRES, 1024);
        BochsVBE_Write(VBE_DISPI_INDEX_YRES, 768);
        BochsVBE_Write(VBE_DISPI_INDEX_BPP, 32);
        BochsVBE_Write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

        lfb_szerokosc = 1024; lfb_wysokosc = 768; lfb_bpp = 32; lfb_pitch = 1024 * 4;
        lfb_fizyczny = 0xFD000000ULL; 
    } else {
        SerialLog("[GRAFIKA] Brak Bochs VBE. Fallback do odczytu Multiboot2...\n");
        if(adres_mb2 == 0) return;
        uint32_t rozmiar = *(uint32_t*)adres_mb2;
        uint64_t aktualny = adres_mb2 + 8;
        while(aktualny < adres_mb2 + rozmiar) {
            TagFramebufferMB2* tag = (TagFramebufferMB2*)aktualny;
            if(tag->typ == 0) break;
            if(tag->typ == 8) { 
                lfb_fizyczny = tag->adres_fizyczny;
                lfb_pitch = tag->pitch; lfb_szerokosc = tag->szerokosc; lfb_wysokosc = tag->wysokosc; lfb_bpp = tag->bpp;
                break;
            }
            aktualny += (tag->rozmiar + 7) & ~7;
        }
    }
    
    if(lfb_fizyczny != 0 && lfb_szerokosc > 0 && lfb_wysokosc > 0 && lfb_pitch > 0) {
        SerialLog("[GRAFIKA] Mapowanie pamieci zewnetrznej do PMM/VMM...\n");
        uint64_t lfb_waga = (uint64_t)lfb_pitch * (uint64_t)lfb_wysokosc;
        uint64_t map_limit = (lfb_waga + 4095) & ~4095ULL;
        
        for(uint64_t i = 0; i < map_limit; i += 4096) ZmapujStrone((void*)(lfb_fizyczny + i), (void*)(lfb_fizyczny + i), 0b11 | 0x10);
        lfb = (uint32_t*)lfb_fizyczny;
        
        SerialLog("[GRAFIKA] Alokacja wirtualnego Backbuffera RAM...\n");
        uint64_t vaddr_backbuffer = 0x80000000ULL; 
        for(uint64_t i = 0; i < map_limit; i += 4096) ZmapujStrone((void*)(vaddr_backbuffer + i), (void*)0, 0b11);
        for(uint64_t i = 0; i < map_limit; i += 4096) {
            void* ramka = ZaalokujRamke();
            if(ramka) ZmapujStrone((void*)(vaddr_backbuffer + i), ramka, 0b11);
        }
        asm volatile("mov %0, %%cr3" : : "r"(PobierzAktualnePML4()) : "memory");
        backbuffer = (uint8_t*)vaddr_backbuffer;
        
        for(uint64_t i = 0; i < lfb_waga; i++) backbuffer[i] = 0;
        for(int r = 0; r < MAX_ROWS; r++) { for(int c = 0; c < MAX_COLS; c++) term_buf[r][c].znak = 0; }
        
        if (okna[0].x + okna[0].szer > (int)lfb_szerokosc) okna[0].szer = lfb_szerokosc - okna[0].x - 20;
        if (okna[0].y + okna[0].wys > (int)lfb_wysokosc - 40) okna[0].wys = lfb_wysokosc - okna[0].y - 40;

        okna[1].szer = 560;
        if (okna[1].x + okna[1].szer > (int)lfb_szerokosc - 20) {
            okna[1].x = 20; 
            if (okna[1].x + okna[1].szer > (int)lfb_szerokosc - 20) okna[1].szer = lfb_szerokosc - 40; 
        }
        if (okna[1].y + okna[1].wys > (int)lfb_wysokosc - 40) okna[1].wys = lfb_wysokosc - okna[1].y - 40;

        OgraniczOkno(okna[0]);
        OgraniczOkno(okna[1]);
        
        // NAPRAWA: Poprawne kalkulowanie limitow znakow przy starcie dla nowej czcionki (Skala 1)
        term_max_c = (okna[0].szer - 12) / 8;
        term_max_r = (okna[0].wys - 36) / 12;
        if (term_max_c > MAX_COLS) term_max_c = MAX_COLS;
        if (term_max_r > MAX_ROWS) term_max_r = MAX_ROWS;
        
        OdswiezEkran();
        PokazKursor();
        PrzeniesNaEkran();     
        
    } else {
        SerialLog("[GRAFIKA] BLAD KRYTYCZNY: Nie znaleziono Framebuffera!\n");
        while(true) asm volatile("cli; hlt");
    }
}

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