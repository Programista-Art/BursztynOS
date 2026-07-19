#include "grafika.h"
#include "pamiec.h"

// Zmienne globalne stanu matrycy
static uint32_t* lfb = nullptr;
static uint32_t  lfb_szerokosc = 0;
static uint32_t  lfb_wysokosc = 0;
static uint32_t  lfb_pitch = 0;
static uint8_t   lfb_bpp = 32; // NOWOŚĆ: Przechowuje głębię kolorów (np. 32, 24, 16)

// Ograniczenia i współrzędne Okna Terminala
static uint32_t term_x_start = 12;
static uint32_t term_y_start = 40;
static uint32_t term_x_max = 808;
static uint32_t term_y_max = 608;
static uint32_t log_y = 42; 
static uint32_t term_x = 14; 

// Zmienne dla Kursora Myszy
static int mysz_x = 500;
static int mysz_y = 300;
static uint32_t bufor_kursora[16][16];
static bool kursor_widoczny = false;

static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
void SerialLog(const char* str) {
    for(int i = 0; str[i] != '\0'; i++) {
        serial_outb(0x3F8, str[i]); 
    }
}

// Macierz obrazka kursora
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

// --- NOWOŚĆ: BEZPIECZNE RYSOWANIE NIEZALEŻNE OD BPP ---
void PostawPiksel(uint32_t x, uint32_t y, uint32_t kolor) {
    if(!lfb || x >= lfb_szerokosc || y >= lfb_wysokosc) return;
    
    uint32_t offset = y * lfb_pitch + x * (lfb_bpp / 8);
    uint8_t* piksel = (uint8_t*)lfb + offset;
    
    if (lfb_bpp == 32) {
        *(uint32_t*)piksel = kolor;
    } else if (lfb_bpp == 24) {
        piksel[0] = kolor & 0xFF;         // B
        piksel[1] = (kolor >> 8) & 0xFF;  // G
        piksel[2] = (kolor >> 16) & 0xFF; // R
    } else if (lfb_bpp == 16) {
        uint16_t r = (kolor >> 16) & 0xFF;
        uint16_t g = (kolor >> 8) & 0xFF;
        uint16_t b = kolor & 0xFF;
        uint16_t k16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        *(uint16_t*)piksel = k16;
    }
}

uint32_t PobierzPiksel(uint32_t x, uint32_t y) {
    if(!lfb || x >= lfb_szerokosc || y >= lfb_wysokosc) return 0;
    
    uint32_t offset = y * lfb_pitch + x * (lfb_bpp / 8);
    uint8_t* piksel = (uint8_t*)lfb + offset;
    
    if (lfb_bpp == 32) {
        return *(uint32_t*)piksel;
    } else if (lfb_bpp == 24) {
        return piksel[0] | (piksel[1] << 8) | (piksel[2] << 16);
    } else if (lfb_bpp == 16) {
        uint16_t k = *(uint16_t*)piksel;
        uint8_t r = ((k >> 11) & 0x1F) << 3;
        uint8_t g = ((k >> 5) & 0x3F) << 2;
        uint8_t b = (k & 0x1F) << 3;
        return (r << 16) | (g << 8) | b;
    }
    return 0;
}
// ------------------------------------------------------

void RysujProstokat(uint32_t px, uint32_t py, uint32_t szer, uint32_t wys, uint32_t kolor) {
    if (px >= lfb_szerokosc || py >= lfb_wysokosc) return;
    
    uint32_t max_x = (px + szer > lfb_szerokosc) ? (lfb_szerokosc - px) : szer;
    uint32_t max_y = (py + wys > lfb_wysokosc) ? (lfb_wysokosc - py) : wys;

    for(uint32_t y = 0; y < max_y; y++) {
        for(uint32_t x = 0; x < max_x; x++) {
            PostawPiksel(px + x, py + y, kolor);
        }
    }
}

void RysujZnak(char c, uint32_t px, uint32_t py, uint32_t kolor_tekstu, uint32_t kolor_tla, bool przezroczyste_tlo, int skala) {
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

void WypiszTekst(const char* tekst, uint32_t px, uint32_t py, uint32_t kolor_tekstu, int skala) {
    int start_x = px;
    for(int i = 0; tekst[i] != '\0'; i++) {
        RysujZnak(tekst[i], start_x, py, kolor_tekstu, 0, true, skala);
        start_x += 8 * skala;
    }
}

void RysujOkno(uint32_t px, uint32_t py, uint32_t szer, uint32_t wys, const char* tytul, uint32_t kolor_tla) {
    if (szer < 10 || wys < 40) return; 
    
    RysujProstokat(px, py, szer, wys, 0x00C0C0C0);             
    RysujProstokat(px + 2, py + 2, szer - 4, 24, 0x000000A0);  
    WypiszTekst(tytul, px + 8, py + 6, 0x00FFFFFF, 2);         
    RysujProstokat(px + 2, py + 28, szer - 4, wys - 30, kolor_tla); 
}

void UkryjKursor() {
    if (!kursor_widoczny || !lfb) return;
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
    if (kursor_widoczny || !lfb) return;
    for(int y=0; y<16; y++) {
        for(int x=0; x<16; x++) {
            if (mysz_x + x < (int)lfb_szerokosc && mysz_y + y < (int)lfb_wysokosc && mysz_x >= 0 && mysz_y >= 0) {
                bufor_kursora[y][x] = PobierzPiksel(mysz_x + x, mysz_y + y);
                uint8_t typ_piksela = kursor_bitmapa[y][x];
                if (typ_piksela == 1) { 
                    PostawPiksel(mysz_x + x, mysz_y + y, 0x00000000);
                } else if (typ_piksela == 2) { 
                    PostawPiksel(mysz_x + x, mysz_y + y, 0x00FFFFFF);
                }
            }
        }
    }
    kursor_widoczny = true;
}

extern "C" void ZaktualizujMysze(int dx, int dy, uint8_t przyciski) {
    (void)przyciski; 

    UkryjKursor();

    mysz_x += dx;
    mysz_y -= dy; 

    if (mysz_x < 0) mysz_x = 0;
    if (mysz_x >= (int)lfb_szerokosc - 2) mysz_x = lfb_szerokosc - 2;
    if (mysz_y < 0) mysz_y = 0;
    if (mysz_y >= (int)lfb_wysokosc - 2) mysz_y = lfb_wysokosc - 2;

    PokazKursor();
}

void RysujPulpit(uint32_t okno_px, uint32_t okno_py, uint32_t okno_szer, uint32_t okno_wys) {
    RysujProstokat(0, 0, lfb_szerokosc, lfb_wysokosc, 0x00005A8C);
    
    if (lfb_wysokosc >= 40) {
        RysujProstokat(0, lfb_wysokosc - 40, lfb_szerokosc, 40, 0x00222222);
        WypiszTekst("Bursztyn OS 64-bit | Menu |", 20, lfb_wysokosc - 28, 0x00FFFFFF, 2);
    }

    RysujOkno(okno_px, okno_py, okno_szer, okno_wys, "Powloka Bursztyna (Ring 3 Terminal)", 0x00000000);
    
    if (lfb_szerokosc > okno_szer + 150) {
        uint32_t av_px = okno_px + okno_szer + 20;
        uint32_t av_szer = lfb_szerokosc - av_px - 20;
        RysujOkno(av_px, 50, av_szer, 500, "Edytor Avocado - Nowy Plik", 0x00FFFFFF);
        WypiszTekst("Witamy w trybie graficznym!", av_px + 20, 100, 0x00000000, 2);
        WypiszTekst("Oto pierwsze okna Bursztyn OS.", av_px + 20, 130, 0x00000000, 2);
    }

    PokazKursor();
}

void InicjalizujGrafike(uint64_t adres_mb2) {
    SerialLog("[GRAFIKA] Start inicjalizacji...\n");

    if(adres_mb2 == 0) {
        SerialLog("[GRAFIKA] BLAD: adres_mb2 wskazuje na 0!\n");
        return;
    }
    
    uint32_t rozmiar = *(uint32_t*)adres_mb2;
    uint64_t aktualny = adres_mb2 + 8;
    uint64_t lfb_fizyczny = 0;

    SerialLog("[GRAFIKA] Przeszukiwanie tagow Multiboot2...\n");

    while(aktualny < adres_mb2 + rozmiar) {
        TagFramebufferMB2* tag = (TagFramebufferMB2*)aktualny;
        if(tag->typ == 0) break;
        if(tag->typ == 8) { 
            // TERAZ AKCEPTUJEMY KAŻDY FORMAT BPP!
            lfb_fizyczny = tag->adres_fizyczny;
            lfb_pitch = tag->pitch;
            lfb_szerokosc = tag->szerokosc;
            lfb_wysokosc = tag->wysokosc;
            lfb_bpp = tag->bpp;
            
            SerialLog("[GRAFIKA] Znaleziono Framebuffer!\n");
            break;
        }
        aktualny += (tag->rozmiar + 7) & ~7;
    }

    if(lfb_fizyczny != 0 && lfb_szerokosc > 0 && lfb_wysokosc > 0 && lfb_pitch > 0) {
        
        SerialLog("[GRAFIKA] Alokowanie i mapowanie stron dla obrazu...\n");
        uint64_t lfb_waga = (uint64_t)lfb_pitch * (uint64_t)lfb_wysokosc;
        uint64_t map_limit = (lfb_waga + 4095) & ~4095ULL;
        
        for(uint64_t i = 0; i < map_limit; i += 4096) {
            ZmapujStrone((void*)(lfb_fizyczny + i), (void*)(lfb_fizyczny + i), 0b11);
        }
        lfb = (uint32_t*)lfb_fizyczny;
        
        uint32_t okno_px = 10;
        uint32_t okno_py = 10;
        uint32_t okno_szer = 800;
        uint32_t okno_wys = 600;

        if (okno_px + okno_szer > lfb_szerokosc) {
            okno_szer = (lfb_szerokosc > okno_px) ? (lfb_szerokosc - okno_px) : 0;
        }
        if (okno_py + okno_wys > lfb_wysokosc) {
            okno_wys = (lfb_wysokosc > okno_py) ? (lfb_wysokosc - okno_py) : 0;
        }

        term_x_start = okno_px + 2;
        term_y_start = okno_py + 28;
        term_x_max = okno_px + okno_szer - 2;
        term_y_max = okno_py + okno_wys - 2;
        
        if (term_x_max > lfb_szerokosc) term_x_max = lfb_szerokosc;
        if (term_y_max > lfb_wysokosc) term_y_max = lfb_wysokosc;

        log_y = term_y_start + 2;
        term_x = term_x_start + 4;

        SerialLog("[GRAFIKA] Rysowanie pulpitu OS...\n");
        RysujPulpit(okno_px, okno_py, okno_szer, okno_wys);
        SerialLog("[GRAFIKA] Grafika w pelni gotowa!\n");
        
    } else {
        SerialLog("[GRAFIKA] BLAD KRYTYCZNY: Nie znaleziono Framebuffera LFB!\n");
        while(true) asm volatile("cli; hlt");
    }
}

void WypiszLog(const char* tekst) {
    if(!lfb) return;
    UkryjKursor(); 

    uint32_t kolor = 0x00FFA500; 
    int skala = 2; 
    uint32_t wysokosc_linii = (8 * skala) + 6;

    if(term_y_max > term_y_start + wysokosc_linii && log_y + wysokosc_linii > term_y_max) {
        
        uint32_t bajtow_na_piksel = lfb_bpp / 8;
        uint32_t start_bajt = term_x_start * bajtow_na_piksel;
        uint32_t rozmiar_bajtow = (term_x_max - term_x_start) * bajtow_na_piksel;
        
        // Bezpieczne, bajtowe przewijanie obszaru okna (działa z każdym formatem kolorów)
        for (uint32_t y = term_y_start; y < term_y_max - wysokosc_linii; y++) {
            if (y + wysokosc_linii >= lfb_wysokosc) break;
            uint8_t* rzad_docelowy = (uint8_t*)lfb + y * lfb_pitch;
            uint8_t* rzad_zrodlowy = (uint8_t*)lfb + (y + wysokosc_linii) * lfb_pitch;
            for (uint32_t b = 0; b < rozmiar_bajtow; b++) {
                rzad_docelowy[start_bajt + b] = rzad_zrodlowy[start_bajt + b];
            }
        }
        for (uint32_t y = term_y_max - wysokosc_linii; y < term_y_max; y++) {
            if (y >= lfb_wysokosc) break;
            uint8_t* rzad_docelowy = (uint8_t*)lfb + y * lfb_pitch;
            for (uint32_t b = 0; b < rozmiar_bajtow; b++) {
                rzad_docelowy[start_bajt + b] = 0; // Czyszczenie na czarno
            }
        }
        log_y -= wysokosc_linii;
    }

    uint32_t px = term_x_start + 4;
    for(int i = 0; tekst[i] != '\0'; i++) {
        RysujZnak(tekst[i], px, log_y, kolor, 0, true, skala); 
        px += 8 * skala;
    }
    log_y += wysokosc_linii; 
    term_x = term_x_start + 4;

    PokazKursor();
}

extern "C" void wypisz_na_ekranie(const char* tekst) {
    if(!lfb) return;
    UkryjKursor(); 

    uint32_t kolor = 0x0000FF00; 
    int skala = 2; 
    uint32_t wysokosc_linii = (8 * skala) + 6;

    for(int i = 0; tekst[i] != '\0'; i++) {
        if (tekst[i] == '\n') {
            term_x = term_x_start + 4;
            log_y += wysokosc_linii;
            
            if(term_y_max > term_y_start + wysokosc_linii && log_y + wysokosc_linii > term_y_max) {
                uint32_t bajtow_na_piksel = lfb_bpp / 8;
                uint32_t start_bajt = term_x_start * bajtow_na_piksel;
                uint32_t rozmiar_bajtow = (term_x_max - term_x_start) * bajtow_na_piksel;

                for (uint32_t y = term_y_start; y < term_y_max - wysokosc_linii; y++) {
                    if (y + wysokosc_linii >= lfb_wysokosc) break;
                    uint8_t* rzad_docelowy = (uint8_t*)lfb + y * lfb_pitch;
                    uint8_t* rzad_zrodlowy = (uint8_t*)lfb + (y + wysokosc_linii) * lfb_pitch;
                    for (uint32_t b = 0; b < rozmiar_bajtow; b++) {
                        rzad_docelowy[start_bajt + b] = rzad_zrodlowy[start_bajt + b];
                    }
                }
                for (uint32_t y = term_y_max - wysokosc_linii; y < term_y_max; y++) {
                    if (y >= lfb_wysokosc) break;
                    uint8_t* rzad_docelowy = (uint8_t*)lfb + y * lfb_pitch;
                    for (uint32_t b = 0; b < rozmiar_bajtow; b++) {
                        rzad_docelowy[start_bajt + b] = 0; 
                    }
                }
                log_y -= wysokosc_linii;
            }
        } else if (tekst[i] == '\r') {
            term_x = term_x_start + 4;
        } else if (tekst[i] == '\b') { 
            if (term_x > term_x_start + 4) {
                term_x -= 8 * skala;
                RysujZnak(' ', term_x, log_y, kolor, 0x00000000, false, skala); 
            }
        } else {
            RysujZnak(tekst[i], term_x, log_y, kolor, 0, true, skala); 
            term_x += 8 * skala;
            if(term_x > term_x_max - 16) { 
                term_x = term_x_start + 4;
                log_y += wysokosc_linii;
                
                if(term_y_max > term_y_start + wysokosc_linii && log_y + wysokosc_linii > term_y_max) {
                    uint32_t bajtow_na_piksel = lfb_bpp / 8;
                    uint32_t start_bajt = term_x_start * bajtow_na_piksel;
                    uint32_t rozmiar_bajtow = (term_x_max - term_x_start) * bajtow_na_piksel;

                    for (uint32_t y = term_y_start; y < term_y_max - wysokosc_linii; y++) {
                        if (y + wysokosc_linii >= lfb_wysokosc) break;
                        uint8_t* rzad_docelowy = (uint8_t*)lfb + y * lfb_pitch;
                        uint8_t* rzad_zrodlowy = (uint8_t*)lfb + (y + wysokosc_linii) * lfb_pitch;
                        for (uint32_t b = 0; b < rozmiar_bajtow; b++) {
                            rzad_docelowy[start_bajt + b] = rzad_zrodlowy[start_bajt + b];
                        }
                    }
                    for (uint32_t y = term_y_max - wysokosc_linii; y < term_y_max; y++) {
                        if (y >= lfb_wysokosc) break;
                        uint8_t* rzad_docelowy = (uint8_t*)lfb + y * lfb_pitch;
                        for (uint32_t b = 0; b < rozmiar_bajtow; b++) {
                            rzad_docelowy[start_bajt + b] = 0;
                        }
                    }
                    log_y -= wysokosc_linii;
                }
            }
        }
    }
    PokazKursor();
}