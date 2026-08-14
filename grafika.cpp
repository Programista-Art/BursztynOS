#include "grafika.h"
#include "pamiec.h"
#include "zegar-rtc.h"
#include "ahci.h"

// --- PODŁĄCZENIE NOWEJ CZCIONKI I STEROWNIKA GOP ---
#include "czcionki/extronic16B_unicode.h"
#include "sterowniki/grafika/uefi_gop.h"

// --- ŁATKI DLA BARE-METAL C++ (Brak stdlibc++) ---
void operator delete(void*, unsigned long) {}
void operator delete(void*) {}

// Własna definicja "Placement New". 
// Zmusza C++ do zbudowania vtable klasy pod wskazanym bezpiecznym adresem w pamięci RAM.
inline void* operator new(unsigned long, void* p) { return p; }
inline void* operator new[](unsigned long, void* p) { return p; }

extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
}

extern "C" bool bws_uruchom_program_z_pliku(const char* sciezka_pliku, uint8_t bzl_poziom, uint64_t flagi_praw, bool z_syscalla);
extern "C" void* PobierzAktualnePML4();

bool flaga_zamkniecia_powloki = false;

// =========================================================================
// WIRTUALNA WARSTWA ABSTRAKCJI SPRZĘTU (HAL) - DISPLAY DRIVER
// =========================================================================

class SterownikEkranu {
protected:
    uint32_t szerokosc;
    uint32_t wysokosc;
    uint32_t pitch;
    uint8_t  bpp;
    volatile uint8_t* framebuffer_fizyczny;

public:
    virtual ~SterownikEkranu() {}
    
    virtual bool Inicjalizuj(uint64_t adres_lfb, uint32_t w, uint32_t h, uint32_t p, uint8_t b) {
        szerokosc = w; wysokosc = h; pitch = p; bpp = b;
        framebuffer_fizyczny = (volatile uint8_t*)adres_lfb;
        return true;
    }
    
    virtual void KopiujNaEkran(uint8_t* backbuffer) = 0;
    virtual void KopiujFragmentNaEkran(uint8_t* backbuffer, int x, int y, int szer, int wys) = 0;

    uint32_t PobierzSzerokosc() { return szerokosc; }
    uint32_t PobierzWysokosc() { return wysokosc; }
    uint32_t PobierzPitch() { return pitch; }
    uint8_t  PobierzBpp() { return bpp; }
};

// ---------------------------------------------------------
// 1. STEROWNIK STANDARDU VESA VBE (Stare PC i tradycyjny BIOS)
// ---------------------------------------------------------
class SterownikVESA : public SterownikEkranu {
public:
    void KopiujNaEkran(uint8_t* backbuffer) override {
        uint32_t* dst = (uint32_t*)framebuffer_fizyczny; 
        uint32_t* src = (uint32_t*)backbuffer;
        uint64_t ilosc_pikseli = ((uint64_t)pitch * (uint64_t)wysokosc) / 4;
        for(uint64_t i = 0; i < ilosc_pikseli; i++) dst[i] = src[i];
    }

    void KopiujFragmentNaEkran(uint8_t* backbuffer, int x, int y, int szer, int wys) override {
        int bajtow_na_piksel = bpp / 8;
        int start_x = x < 0 ? 0 : x;
        int start_y = y < 0 ? 0 : y;
        int end_x = start_x + szer; if(end_x > (int)szerokosc) end_x = szerokosc;
        int end_y = start_y + wys;  if(end_y > (int)wysokosc) end_y = wysokosc;
        
        for(int rzad = start_y; rzad < end_y; rzad++) {
            volatile uint8_t* dst = framebuffer_fizyczny + rzad * pitch + start_x * bajtow_na_piksel;
            uint8_t* src = backbuffer + rzad * pitch + start_x * bajtow_na_piksel;
            int bajtow = (end_x - start_x) * bajtow_na_piksel;
            for(int b = 0; b < bajtow; b++) dst[b] = src[b];
        }
    }
};

// ---------------------------------------------------------
// 2. STEROWNIK UEFI GOP (Nowoczesne PC i firmware EFI)
// ---------------------------------------------------------
class SterownikGOP : public SterownikEkranu {
public:
    bool Inicjalizuj(uint64_t adres_lfb, uint32_t w, uint32_t h, uint32_t p, uint8_t b) override {
        SterownikEkranu::Inicjalizuj(adres_lfb, w, h, p, b);
        InicjalizujGOP(adres_lfb, w, h, p, b);
        return true;
    }

    void KopiujNaEkran(uint8_t* backbuffer) override {
        if (!gop_ekran.framebuffer) return;
        uint32_t* dst = (uint32_t*)gop_ekran.framebuffer; 
        uint32_t* src = (uint32_t*)backbuffer;
        uint64_t ilosc_pikseli = ((uint64_t)gop_ekran.pitch * (uint64_t)gop_ekran.wysokosc) / 4;
        for(uint64_t i = 0; i < ilosc_pikseli; i++) dst[i] = src[i];
    }

    void KopiujFragmentNaEkran(uint8_t* backbuffer, int x, int y, int szer, int wys) override {
        if (!gop_ekran.framebuffer) return;
        int bajtow_na_piksel = bpp / 8;
        int start_x = x < 0 ? 0 : x;
        int start_y = y < 0 ? 0 : y;
        int end_x = start_x + szer; if(end_x > (int)gop_ekran.szerokosc) end_x = gop_ekran.szerokosc;
        int end_y = start_y + wys;  if(end_y > (int)gop_ekran.wysokosc) end_y = gop_ekran.wysokosc;
        
        for(int rzad = start_y; rzad < end_y; rzad++) {
            volatile uint8_t* dst = (volatile uint8_t*)gop_ekran.framebuffer + rzad * gop_ekran.pitch + start_x * bajtow_na_piksel;
            uint8_t* src = backbuffer + rzad * gop_ekran.pitch + start_x * bajtow_na_piksel;
            int bajtow = (end_x - start_x) * bajtow_na_piksel;
            for(int b = 0; b < bajtow; b++) dst[b] = src[b];
        }
    }
};

static SterownikEkranu* aktywny_ekran = nullptr;

// =========================================================================

static uint8_t* backbuffer = nullptr;
static uint32_t* bufor_tapety = nullptr; 
static bool tapeta_zaladowana = false;   
static bool ring3_gui_active = false;

void UkryjKursor();
void PokazKursor();
void OdswiezEkran();
void PrzeniesNaEkran();
void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys);
void rysuj_zegar_rtc();

struct Okno {
    uint32_t x, y, szer, wys;
    uint32_t stary_x, stary_y, stary_szer, stary_wys; 
    const char* krotka_nazwa;
    const char* tytul;
    uint32_t kolor_tla;
    bool widoczne;
    bool zmaksymalizowane;
};

static Okno okna[1] = {
    { 20, 20, 660, 360,  0,0,0,0, "Terminal", "Powłoka Bursztynowa", 0x001A0B00, true, false }
};
static int z_order[1] = {0}; 
static int okno_przeciagane = -1;
static int chwyt_x = 0;
static int chwyt_y = 0;
static bool lewy_wcisniety = false;

struct ZnakTerminala { char znak; uint32_t kolor; };
#define MAX_ROWS 80
#define MAX_COLS 140
static ZnakTerminala term_buf[MAX_ROWS][MAX_COLS];
static int term_r = 0, term_c = 0;
static int term_max_r = 25, term_max_c = 80;

static int mysz_x = 500; static int mysz_y = 300;
static uint32_t bufor_kursora[16][16];
static bool kursor_widoczny = false;

static inline void serial_outb(uint16_t port, uint8_t val) { asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port)); }
void SerialLog(const char* str) { for(int i = 0; str[i] != '\0'; i++) serial_outb(0x3F8, str[i]); } 

static const uint8_t kursor_bitmapa[16][16] = {
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},{1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},{1,2,2,2,2,2,2,1,1,1,1,1,0,0,0,0},{1,2,2,1,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,2,1,0,0,0,0,0,0,0},{1,1,0,0,0,1,2,2,1,0,0,0,0,0,0,0},{0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0}
};

void DopiszDoBufora(const char* tekst, uint32_t kolor) {
    for(int i = 0; tekst[i] != '\0'; i++) {
        if (tekst[i] == '\n') { term_r++; term_c = 0; }
        else if (tekst[i] == '\r') { term_c = 0; }
        else if (tekst[i] == '\b') { 
            if (term_c > 0) { 
                term_c--; term_buf[term_r][term_c].znak = 0; 
                if (term_c > 0 && (term_buf[term_r][term_c-1].znak & 0xE0) == 0xC0) { term_c--; term_buf[term_r][term_c].znak = 0; }
            } 
        }
        else {
            term_buf[term_r][term_c].znak = tekst[i]; term_buf[term_r][term_c].kolor = kolor; term_c++;
            if (term_c >= term_max_c) { term_r++; term_c = 0; }
        }
        if (term_r >= term_max_r) {
            for(int r = 1; r < term_max_r; r++) { for(int c = 0; c < term_max_c; c++) term_buf[r-1][c] = term_buf[r][c]; }
            for(int c = 0; c < term_max_c; c++) term_buf[term_max_r-1][c].znak = 0;
            term_r = term_max_r - 1;
        }
    }
}

void wypisz_log(const char* tekst) {
    SerialLog(tekst); SerialLog("\n");
    if(!backbuffer) return;
    UkryjKursor();
    DopiszDoBufora(tekst, 0x00FFBF00); DopiszDoBufora("\n", 0x00FFBF00);
    OdswiezEkran(); PokazKursor(); PrzeniesNaEkran();
}

void PrzeniesNaEkran() {
    if (!backbuffer || !aktywny_ekran) return;
    aktywny_ekran->KopiujNaEkran(backbuffer);
}

void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys) {
    if (!backbuffer || !aktywny_ekran) return;
    aktywny_ekran->KopiujFragmentNaEkran(backbuffer, x, y, szer, wys);
}

void PostawPiksel(int x, int y, uint32_t kolor) {
    if(!backbuffer || !aktywny_ekran || x < 0 || x >= (int)aktywny_ekran->PobierzSzerokosc() || y < 0 || y >= (int)aktywny_ekran->PobierzWysokosc()) return;
    uint32_t offset = y * aktywny_ekran->PobierzPitch() + x * (aktywny_ekran->PobierzBpp() / 8);
    uint8_t* piksel = backbuffer + offset;
    
    if (aktywny_ekran->PobierzBpp() == 32) *(uint32_t*)piksel = kolor;
    else if (aktywny_ekran->PobierzBpp() == 24) { piksel[0] = kolor & 0xFF; piksel[1] = (kolor >> 8) & 0xFF; piksel[2] = (kolor >> 16) & 0xFF; }
}

uint32_t PobierzPiksel(int x, int y) {
    if(!backbuffer || !aktywny_ekran || x < 0 || x >= (int)aktywny_ekran->PobierzSzerokosc() || y < 0 || y >= (int)aktywny_ekran->PobierzWysokosc()) return 0;
    uint32_t offset = y * aktywny_ekran->PobierzPitch() + x * (aktywny_ekran->PobierzBpp() / 8);
    uint8_t* piksel = backbuffer + offset;
    
    if (aktywny_ekran->PobierzBpp() == 32) return *(uint32_t*)piksel;
    else if (aktywny_ekran->PobierzBpp() == 24) return piksel[0] | (piksel[1] << 8) | (piksel[2] << 16);
    return 0;
}

void RysujProstokat(int px, int py, int szer, int wys, uint32_t kolor) {
    if(!aktywny_ekran) return;
    int start_x = px < 0 ? 0 : px; int start_y = py < 0 ? 0 : py;
    int end_x = px + szer; int end_y = py + wys;
    
    if (end_x > (int)aktywny_ekran->PobierzSzerokosc()) end_x = aktywny_ekran->PobierzSzerokosc(); 
    if (end_y > (int)aktywny_ekran->PobierzWysokosc()) end_y = aktywny_ekran->PobierzWysokosc();

    for(int y = start_y; y < end_y; y++) {
        for(int x = start_x; x < end_x; x++) PostawPiksel(x, y, kolor);
    }
}

void RysujZnak(uint32_t unicode, int px, int py, uint32_t kolor_tekstu, uint32_t kolor_tla, bool przezroczyste_tlo, int skala) {
    const uint16_t* glyph = nullptr;
    uint32_t max_znaki = sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]);
    int szerokosc = 8;
    if (unicode < max_znaki) { glyph = nowa_czcionka_16x16[unicode]; szerokosc = nowa_czcionka_szerokosci[unicode]; } 
    else { glyph = nowa_czcionka_16x16[0]; }
    if (szerokosc > 16) szerokosc = 16;
    for(int y = 0; y < 16; y++) {
        for(int x = 0; x < szerokosc; x++) { 
            bool zmaluj = (glyph[y] & (1 << (15 - x))) != 0;
            if(zmaluj) {
                for(int sy=0; sy<skala; sy++) for(int sx=0; sx<skala; sx++) PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tekstu);
            } else if (!przezroczyste_tlo) { 
                for(int sy=0; sy<skala; sy++) for(int sx=0; sx<skala; sx++) PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tla);
            }
        }
    }
}

void WypiszTekst(const char* tekst, int px, int py, uint32_t kolor_tekstu, int skala) {
    int start_x = px; int i = 0;
    uint32_t max_znaki = sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]);
    while (tekst[i] != '\0') {
        uint32_t unicode = (uint8_t)tekst[i];
        if ((tekst[i] & 0xE0) == 0xC0 && tekst[i+1] != '\0') {
            uint8_t b1 = (uint8_t)tekst[i]; uint8_t b2 = (uint8_t)tekst[i+1];
            unicode = ((b1 & 0x1F) << 6) | (b2 & 0x3F); i++; 
        }
        RysujZnak(unicode, start_x, py, kolor_tekstu, 0, true, skala);
        int szerokosc_znaku = 8;
        if (unicode < max_znaki) szerokosc_znaku = nowa_czcionka_szerokosci[unicode];
        start_x += (szerokosc_znaku + 1) * skala; 
        i++;
    }
}

void RysujOkno(int id) {
    if (!okna[id].widoczne) return;
    int px = okna[id].x; int py = okna[id].y; int szer = okna[id].szer; int wys = okna[id].wys;
    if (szer < 10 || wys < 40) return;
    
    bool aktywne = (z_order[0] == id);
    uint32_t kolor_paska = aktywne ? 0x00FFBF00 : 0x008A5A00;
    uint32_t kolor_tekstu_paska = aktywne ? 0x001A0B00 : 0x00D1D5DB;

    RysujProstokat(px, py, szer, wys, 0x008A5A00);             
    RysujProstokat(px + 2, py + 2, szer - 4, 24, kolor_paska);  
    WypiszTekst(okna[id].tytul, px + 8, py + 4, kolor_tekstu_paska, 1);         
    
    int min_btn_x = (px + szer - 74);
    
    RysujProstokat(min_btn_x, py + 4, 20, 20, 0x00E58A00); 
    WypiszTekst("-", min_btn_x + 4, py + 6, 0x001A0B00, 1);
    
    RysujProstokat(px + szer - 50, py + 4, 20, 20, 0x00E58A00); 
    WypiszTekst(okna[id].zmaksymalizowane ? "v" : "^", px + szer - 46, py + 6, 0x001A0B00, 1);
    
    RysujProstokat(px + szer - 26, py + 4, 20, 20, 0x00AA0000); 
    WypiszTekst("X", px + szer - 22, py + 6, 0x00FFFFFF, 1);

    RysujProstokat(px + 2, py + 28, szer - 4, wys - 30, okna[id].kolor_tla); 
}

void RysujTekstZBufora(ZnakTerminala buf[][MAX_COLS], int max_r, int max_c, int r_cursor, int c_cursor, int px, int py, int szer, int wys, bool rysuj_kursor, bool aktywne_okno) {
    int skala = 1; int wysokosc_linii = 16 * skala; 
    int start_x = px + 6; int start_y = py + 28 + 4;
    uint32_t max_znaki = sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]);
    
    for (int r = 0; r < max_r; r++) {
        int cx = start_x; int cy = start_y + (r * wysokosc_linii);
        if (cy + wysokosc_linii >= py + wys) break;
        
        for (int c = 0; c < max_c; c++) {
            char z = buf[r][c].znak; uint32_t kolor = buf[r][c].kolor;
            if (z != 0) { 
                uint32_t unicode = (uint8_t)z;
                if ((z & 0xE0) == 0xC0 && c + 1 < max_c && buf[r][c+1].znak != 0) {
                    uint8_t z2 = (uint8_t)buf[r][c+1].znak; unicode = ((unicode & 0x1F) << 6) | (z2 & 0x3F); c++; 
                }
                if (cx + 9*skala >= px + szer - 6) break; 
                RysujZnak(unicode, cx, cy, kolor, 0, true, skala); 
                
                int szerokosc_znaku = 8;
                if (unicode < max_znaki) { szerokosc_znaku = nowa_czcionka_szerokosci[unicode]; }
                cx += (szerokosc_znaku + 1) * skala;
            } else {
                cx += 9 * skala;
            }
        }
    }
    
    if (rysuj_kursor && aktywne_okno) {
        int cx = start_x;
        for(int c = 0; c < c_cursor; c++) {
            char z = buf[r_cursor][c].znak;
            if (z != 0) {
                uint32_t unicode = (uint8_t)z;
                if ((z & 0xE0) == 0xC0 && c + 1 < max_c && buf[r_cursor][c+1].znak != 0) {
                    uint8_t z2 = (uint8_t)buf[r_cursor][c+1].znak; unicode = ((unicode & 0x1F) << 6) | (z2 & 0x3F); c++;
                }
                int sw = 8;
                if (unicode < max_znaki) sw = nowa_czcionka_szerokosci[unicode];
                cx += (sw + 1) * skala;
            } else { cx += 9 * skala; }
        }
        int cy = start_y + (r_cursor * wysokosc_linii); 
        if (cy + 14 < py + wys && cx + 9 < px + szer) RysujProstokat(cx, cy + 14, 9, 2, 0x00FFBF00); 
    }
}

void rysuj_zegar_rtc() {
    if(!aktywny_ekran) return;
    czas_rtc czas; pobierz_czas_rtc(&czas); char bufor_czasu[16]; formatuj_czas_do_stringa(&czas, bufor_czasu);
    int zegar_x = aktywny_ekran->PobierzSzerokosc() - 150; 
    int zegar_y = aktywny_ekran->PobierzWysokosc() - 32;
    RysujProstokat(zegar_x, aktywny_ekran->PobierzWysokosc() - 40, 150, 40, 0x001A0B00); 
    RysujProstokat(zegar_x, aktywny_ekran->PobierzWysokosc() - 40, 150, 2, 0x00E58A00);  
    WypiszTekst(bufor_czasu, zegar_x + 10, zegar_y, 0x00FFBF00, 2);
}

void OdswiezEkran() {
    if(!backbuffer || !aktywny_ekran) return;
    
    if (tapeta_zaladowana && bufor_tapety) {
        for(int y = 0; y < (int)aktywny_ekran->PobierzWysokosc(); y++) {
            uint32_t* dst_row = (uint32_t*)(backbuffer + y * aktywny_ekran->PobierzPitch());
            uint32_t* src_row = bufor_tapety + y * aktywny_ekran->PobierzSzerokosc();
            for(int x = 0; x < (int)aktywny_ekran->PobierzSzerokosc(); x++) dst_row[x] = src_row[x];
        }
    } else {
        RysujProstokat(0, 0, aktywny_ekran->PobierzSzerokosc(), aktywny_ekran->PobierzWysokosc(), 0x001A0B00);
    }
    
    for(int k = 0; k < 1; k++) {
        int i = z_order[k];
        if (!okna[i].widoczne) continue;
        
        RysujOkno(i);
        if (i == 0) RysujTekstZBufora(term_buf, term_max_r, term_max_c, term_r, term_c, okna[0].x, okna[0].y, okna[0].szer, okna[0].wys, false, false);
    }

    if (aktywny_ekran->PobierzWysokosc() >= 40) {
        if (!ring3_gui_active) {
            RysujProstokat(0, aktywny_ekran->PobierzWysokosc() - 40, aktywny_ekran->PobierzSzerokosc(), 40, 0x001A0B00);
            RysujProstokat(0, aktywny_ekran->PobierzWysokosc() - 40, aktywny_ekran->PobierzSzerokosc(), 2, 0x00E58A00);
            WypiszTekst("Bursztyn OS - Terminal", 20, aktywny_ekran->PobierzWysokosc() - 28, 0x008A5A00, 1);
        }
        rysuj_zegar_rtc();
    }
}

extern "C" bool zaktualizuj_klawiature_gui(char znak) {
    (void)znak;
    return false; 
}

void UkryjKursor() {
    if (!kursor_widoczny || !backbuffer || !aktywny_ekran) return;
    for(int y=0; y<16; y++) {
        for(int x=0; x<16; x++) {
            if (mysz_x + x < (int)aktywny_ekran->PobierzSzerokosc() && mysz_y + y < (int)aktywny_ekran->PobierzWysokosc() && mysz_x >= 0 && mysz_y >= 0) {
                PostawPiksel(mysz_x + x, mysz_y + y, bufor_kursora[y][x]);
            }
        }
    }
    kursor_widoczny = false;
}

void PokazKursor() {
    if (kursor_widoczny || !backbuffer || !aktywny_ekran) return;
    for(int y=0; y<16; y++) {
        for(int x=0; x<16; x++) {
            if (mysz_x + x < (int)aktywny_ekran->PobierzSzerokosc() && mysz_y + y < (int)aktywny_ekran->PobierzWysokosc() && mysz_x >= 0 && mysz_y >= 0) {
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
    if (o.zmaksymalizowane || !aktywny_ekran) return; 
    int32_t min_x = -(int32_t)o.szer + 80; 
    int32_t max_x = (int32_t)aktywny_ekran->PobierzSzerokosc() - 80;
    int32_t min_y = 0; 
    int32_t max_y = (int32_t)aktywny_ekran->PobierzWysokosc() - 40; 
    
    if ((int32_t)o.x < min_x) o.x = (uint32_t)min_x; 
    if ((int32_t)o.x > max_x) o.x = (uint32_t)max_x;
    if ((int32_t)o.y < min_y) o.y = (uint32_t)min_y; 
    if ((int32_t)o.y > max_y) o.y = (uint32_t)max_y;
}

extern "C" void zaktualizuj_mysze(int dx, int dy, uint8_t przyciski) {
    if (!backbuffer || !aktywny_ekran) return;
    int stary_mysz_x = mysz_x; int stary_mysz_y = mysz_y;
    UkryjKursor();
    mysz_x += dx; mysz_y -= dy; 
    
    if (mysz_x < 0) mysz_x = 0; 
    if (mysz_x >= (int)aktywny_ekran->PobierzSzerokosc() - 2) mysz_x = aktywny_ekran->PobierzSzerokosc() - 2;
    if (mysz_y < 0) mysz_y = 0; 
    if (mysz_y >= (int)aktywny_ekran->PobierzWysokosc() - 2) mysz_y = aktywny_ekran->PobierzWysokosc() - 2;
    
    if (ring3_gui_active) {
        lewy_wcisniety = (przyciski & 0x01);
        PokazKursor(); 
        PrzeniesFragmentNaEkran(stary_mysz_x, stary_mysz_y, 16, 16); 
        PrzeniesFragmentNaEkran(mysz_x, mysz_y, 16, 16);
        return;
    }

    bool nowy_lewy = (przyciski & 0x01);
    bool klik_lewy = (nowy_lewy && !lewy_wcisniety);
    bool puszcz_lewy = (!nowy_lewy && lewy_wcisniety);
    bool wymaga_odrysowania = false;
    
    if (klik_lewy && okno_przeciagane == -1) {
        for (int k = 0; k >= 0; k--) {
            int i = z_order[k]; if (!okna[i].widoczne) continue;
            int px = (int)okna[i].x; int py = (int)okna[i].y; int sz = (int)okna[i].szer; int wy = (int)okna[i].wys;
            
            if (mysz_x >= px && mysz_x <= px + sz && mysz_y >= py && mysz_y <= py + wy) {
                wymaga_odrysowania = true;
                if (mysz_y <= py + 26) {
                    if (mysz_x >= px + sz - 26 && mysz_x <= px + sz - 6) { 
                        okna[i].widoczne = false; 
                        if (i == 0) flaga_zamkniecia_powloki = true;
                        break; 
                    }
                    int min_btn_x = (px + sz - 74);
                    if (mysz_x >= min_btn_x && mysz_x <= min_btn_x + 20) { 
                        okna[i].widoczne = false; 
                        if (i == 0) flaga_zamkniecia_powloki = true;
                        break; 
                    }
                    if (mysz_x >= px + sz - 50 && mysz_x <= px + sz - 30) { 
                        if (!okna[i].zmaksymalizowane) {
                            okna[i].stary_x = okna[i].x; okna[i].stary_y = okna[i].y; okna[i].stary_szer = okna[i].szer; okna[i].stary_wys = okna[i].wys;
                            okna[i].x = 0; okna[i].y = 0; okna[i].szer = aktywny_ekran->PobierzSzerokosc(); okna[i].wys = aktywny_ekran->PobierzWysokosc() - 40; 
                            okna[i].zmaksymalizowane = true;
                        } else {
                            okna[i].x = okna[i].stary_x; okna[i].y = okna[i].stary_y; okna[i].szer = okna[i].stary_szer; okna[i].wys = okna[i].stary_wys;
                            okna[i].zmaksymalizowane = false;
                        }
                        if (i == 0) {
                            term_max_c = (okna[0].szer - 12) / 9; term_max_r = (okna[0].wys - 36) / 16; 
                            if (term_max_c > MAX_COLS) term_max_c = MAX_COLS; 
                            if (term_max_r > MAX_ROWS) term_max_r = MAX_ROWS;
                        }
                        break;
                    }
                    int drag_max = (px + sz - 79);
                    if (!okna[i].zmaksymalizowane && mysz_x < drag_max) { 
                        okno_przeciagane = i; chwyt_x = mysz_x - okna[i].x; chwyt_y = mysz_y - okna[i].y;
                    }
                }
                break; 
            }
        }
    }
    else if (puszcz_lewy && okno_przeciagane != -1) { okno_przeciagane = -1; }
    
    if (okno_przeciagane != -1 && nowy_lewy && (dx != 0 || dy != 0)) {
        okna[okno_przeciagane].x = mysz_x - chwyt_x; okna[okno_przeciagane].y = mysz_y - chwyt_y;
        OgraniczOkno(okna[okno_przeciagane]); wymaga_odrysowania = true;
    }
    lewy_wcisniety = nowy_lewy;

    if (wymaga_odrysowania) { OdswiezEkran(); PokazKursor(); PrzeniesNaEkran(); } 
    else { PokazKursor(); PrzeniesFragmentNaEkran(stary_mysz_x, stary_mysz_y, 16, 16); PrzeniesFragmentNaEkran(mysz_x, mysz_y, 16, 16); }
}

extern "C" void obsluga_przerwania_zegara() {
    static uint8_t stara_sekunda = 255;
    czas_rtc czas; 
    pobierz_czas_rtc(&czas);
    
    if (czas.sekundy != stara_sekunda) {
        stara_sekunda = czas.sekundy;
        if (!backbuffer || !aktywny_ekran || aktywny_ekran->PobierzWysokosc() < 40) return;
        bool kursor_byl = kursor_widoczny; 
        if (kursor_byl) UkryjKursor();
        
        rysuj_zegar_rtc(); 
        int zegar_x = aktywny_ekran->PobierzSzerokosc() - 150; 
        
        PrzeniesFragmentNaEkran(zegar_x, aktywny_ekran->PobierzWysokosc() - 40, 150, 40);
        
        if (kursor_byl) PokazKursor();
    }
}

extern "C" void wczytaj_tapete_z_dysku() {
    if (!aktywny_ekran) return;
    wypisz_log("[GRAFIKA] Proba wczytania tapeta.bmp z dysku AHCI (LBA 10)...");
    
    // ZMIANA VMM: Przenosimy mapowania powyzęj bariery 4 GB w adresy bezpieczne (0x1...ULL)
    uint64_t vaddr_raw = 0x120000000ULL;
    for(uint64_t i = 0; i < 32 * 1024 * 1024; i += 4096) ZmapujStrone((void*)(vaddr_raw + i), ZaalokujRamke(), 0b11);
    asm volatile("mov %0, %%cr3" : : "r"(PobierzAktualnePML4()) : "memory");
    uint8_t* raw_bmp = (uint8_t*)vaddr_raw;

    if (!czytaj_z_glownego_dysku_ahci(10, 1, raw_bmp)) return; 
    if (raw_bmp[0] != 'B' || raw_bmp[1] != 'M') return; 

    uint32_t rozmiar_pliku = *(uint32_t*)(&raw_bmp[2]);
    uint32_t sektory_do_odczytu = (rozmiar_pliku + 511) / 512;
    if (sektory_do_odczytu > (32 * 1024 * 1024 / 512)) sektory_do_odczytu = (32 * 1024 * 1024 / 512);

    uint32_t lba = 10; uint32_t przeczytane = 0;
    while (przeczytane < sektory_do_odczytu) {
        uint32_t paczka = sektory_do_odczytu - przeczytane;
        if (paczka > 32) paczka = 32;
        if (!czytaj_z_glownego_dysku_ahci(lba, paczka, raw_bmp + (przeczytane * 512))) return;
        lba += paczka; przeczytane += paczka;
    }

    uint32_t piksele_offset = *(uint32_t*)(&raw_bmp[10]);
    int32_t bmp_szerokosc = *(int32_t*)(&raw_bmp[18]);
    int32_t bmp_wysokosc = *(int32_t*)(&raw_bmp[22]);
    uint16_t bmp_bpp = *(uint16_t*)(&raw_bmp[28]);

    bool rysuj_od_gory = false;
    if (bmp_wysokosc < 0) { bmp_wysokosc = -bmp_wysokosc; rysuj_od_gory = true; }
    if (bmp_bpp != 24 && bmp_bpp != 32) return;

    // ZMIANA VMM: Przenosimy bufor tapety powyżej bariery 4 GB
    uint64_t vaddr_tapeta = 0x110000000ULL;
    uint64_t waga_tapety_bajty = (uint64_t)aktywny_ekran->PobierzSzerokosc() * (uint64_t)aktywny_ekran->PobierzWysokosc() * 4;
    for(uint64_t i = 0; i < waga_tapety_bajty; i += 4096) ZmapujStrone((void*)(vaddr_tapeta + i), ZaalokujRamke(), 0b11);
    asm volatile("mov %0, %%cr3" : : "r"(PobierzAktualnePML4()) : "memory");
    bufor_tapety = (uint32_t*)vaddr_tapeta;

    uint64_t ilosc_pikseli_ekranu = aktywny_ekran->PobierzSzerokosc() * aktywny_ekran->PobierzWysokosc();
    for(uint64_t i = 0; i < ilosc_pikseli_ekranu; i++) bufor_tapety[i] = 0x000A0500; 

    int offset_x = ((int)aktywny_ekran->PobierzSzerokosc() - (int)bmp_szerokosc) / 2;
    int offset_y = ((int)aktywny_ekran->PobierzWysokosc() - (int)bmp_wysokosc) / 2;
    
    if (offset_x < 0) offset_x = 0; 
    if (offset_y < 0) offset_y = 0;

    for (int y = 0; y < bmp_wysokosc; y++) {
        for (int x = 0; x < bmp_szerokosc; x++) {
            int docelowy_x = offset_x + x; int docelowy_y = offset_y + y;
            if (docelowy_x >= (int)aktywny_ekran->PobierzSzerokosc() || docelowy_y >= (int)aktywny_ekran->PobierzWysokosc()) continue;
            
            int d_y = rysuj_od_gory ? y : (bmp_wysokosc - 1 - y);
            int rzad_bajtow = ((bmp_szerokosc * bmp_bpp) + 31) / 32 * 4;
            uint8_t* piksel = raw_bmp + piksele_offset + (d_y * rzad_bajtow) + (x * (bmp_bpp / 8));
            
            uint32_t kolor = 0;
            if (bmp_bpp == 24 || bmp_bpp == 32) kolor = (piksel[2] << 16) | (piksel[1] << 8) | piksel[0]; 
            bufor_tapety[docelowy_y * aktywny_ekran->PobierzSzerokosc() + docelowy_x] = kolor;
        }
    }

    tapeta_zaladowana = true;
    wypisz_log("[GRAFIKA] Pulpit i tapeta gotowe!");
    OdswiezEkran(); PrzeniesNaEkran();
}

extern "C" void wypisz_na_ekranie(const char* tekst) {
    if(!backbuffer) return;
    UkryjKursor(); DopiszDoBufora(tekst, 0x00E58A00); OdswiezEkran(); PokazKursor(); PrzeniesNaEkran();
}

// =========================================================================
// GŁÓWNA INTELIGENCJA DETEKCJI I URUCHAMIANIA
// =========================================================================

// --- TWORZYMY OBIEKTY POPRZEZ "PLACEMENT NEW" ZAMIAST "STATIC" ---
static uint8_t st_vesa_pamiec[sizeof(SterownikVESA)];
static uint8_t st_gop_pamiec[sizeof(SterownikGOP)];

void InicjalizujGrafike(uint64_t adres_mb2) {
    if(adres_mb2 == 0) return;
    uint32_t rozmiar = *(uint32_t*)adres_mb2; uint64_t aktualny = adres_mb2 + 8; 
    uint64_t LFB = 0; uint32_t SZER = 0; uint32_t WYS = 0; uint32_t PITCH = 0; uint8_t BPP = 0;
    bool mamy_efi = false;

    // Przeszukujemy tagi Multiboot2...
    while(aktualny < adres_mb2 + rozmiar) {
        TagFramebufferMB2* tag = (TagFramebufferMB2*)aktualny;
        if(tag->typ == 0) break;
        
        if(tag->typ == 11 || tag->typ == 12) mamy_efi = true; 
        
        if(tag->typ == 8) { 
            LFB = tag->adres_fizyczny; PITCH = tag->pitch; 
            SZER = tag->szerokosc; WYS = tag->wysokosc; BPP = tag->bpp; 
        }
        aktualny += (tag->rozmiar + 7) & ~7;
    }

    if(LFB != 0 && SZER > 0 && WYS > 0 && PITCH > 0) {
        uint64_t lfb_waga = (uint64_t)PITCH * (uint64_t)WYS;
        uint64_t map_limit = (lfb_waga + 4095) & ~4095ULL;
        
        // Zabezpieczenie przed nadpisywaniem VMM pod 4GB
        if (LFB >= 0x100000000ULL) {
            for(uint64_t i = 0; i < map_limit; i += 4096) ZmapujStrone((void*)(LFB + i), (void*)(LFB + i), 0b11 | 0x10);
        }
        
        uint64_t vaddr_backbuffer = 0x100000000ULL; 
        for(uint64_t i = 0; i < map_limit; i += 4096) ZmapujStrone((void*)(vaddr_backbuffer + i), ZaalokujRamke(), 0b11);
        asm volatile("mov %0, %%cr3" : : "r"(PobierzAktualnePML4()) : "memory");
        backbuffer = (uint8_t*)vaddr_backbuffer;
        
        // -----------------------------------------------------------------
        // MANUALNA INICJALIZACJA VTABLE W PAMIECI (Placement New)
        // -----------------------------------------------------------------
        if (mamy_efi) {
            aktywny_ekran = new (st_gop_pamiec) SterownikGOP();
            SerialLog("[HAL] Aktywacja nowoczesnego sterownika UEFI GOP.\n");
        } else {
            aktywny_ekran = new (st_vesa_pamiec) SterownikVESA();
            SerialLog("[HAL] Aktywacja uniwersalnego sterownika VESA VBE.\n");
        }
        
        aktywny_ekran->Inicjalizuj(LFB, SZER, WYS, PITCH, BPP);
        
        for(uint64_t i = 0; i < lfb_waga; i++) backbuffer[i] = 0;
        
        if (okna[0].x + okna[0].szer > SZER) okna[0].szer = SZER - okna[0].x - 20;
        if (okna[0].y + okna[0].wys > WYS - 40) okna[0].wys = WYS - okna[0].y - 40;
        
        term_max_c = (okna[0].szer - 12) / 9; term_max_r = (okna[0].wys - 36) / 16; 
        if (term_max_c > MAX_COLS) term_max_c = MAX_COLS; 
        if (term_max_r > MAX_ROWS) term_max_r = MAX_ROWS;
        
        wypisz_log("[SYSTEM] Bursztyn OS HAL gotowy!");
    } else {
        SerialLog("[GRAFIKA] BLAD KRYTYCZNY: Brak LFB w Multiboot2!\n");
        while(true) asm volatile("cli; hlt");
    }
}

// =========================================================================
// API RING 3 (SYSCALLS GUI)
// =========================================================================

extern "C" void bws_gui_rysuj_okno(int x, int y, int szer, int wys, const char* tytul) {
    if(!backbuffer || !aktywny_ekran) return;
    UkryjKursor();
    RysujProstokat(x, y, szer, wys, 0x008A5A00);             
    RysujProstokat(x + 2, y + 2, szer - 4, 24, 0x00FFBF00);  
    WypiszTekst(tytul, x + 8, y + 4, 0x001A0B00, 1);         
    RysujProstokat(x + 2, y + 28, szer - 4, wys - 30, 0x00280F00); 
    PokazKursor();
}

extern "C" void bws_gui_wypisz_tekst(int x, int y, const char* text) {
    if(!backbuffer || !aktywny_ekran) return;
    UkryjKursor();
    WypiszTekst(text, x, y, 0x00D1D5DB, 1);
    PokazKursor();
}

extern "C" void bws_gui_wyczyscz_obszar(int x, int y, int szer, int wys) {
    if(!backbuffer || !aktywny_ekran) return;
    UkryjKursor();
    RysujProstokat(x, y, szer, wys, 0x00280F00);
    PokazKursor();
}

extern "C" void bws_gui_odswiez() {
    if(!backbuffer || !aktywny_ekran) return;
    PrzeniesNaEkran();
}

extern "C" void bws_gui_pobierz_mysz(int* x, int* y, uint8_t* przyciski) {
    if(!backbuffer || !x || !y || !przyciski) return;
    *x = mysz_x;
    *y = mysz_y;
    *przyciski = lewy_wcisniety ? 1 : 0;
}

extern "C" void bws_gui_odswiez_pulpit() {
    if(!backbuffer || !aktywny_ekran) return;
    UkryjKursor();
    OdswiezEkran(); 
    PokazKursor();
}

extern "C" void bws_gui_wypisz_tekst_kolor(int x, int y, uint64_t kolor_skala, const char* text) {
    if(!backbuffer || !aktywny_ekran) return;
    uint32_t kolor = kolor_skala & 0xFFFFFFFF;
    int skala = (kolor_skala >> 32) & 0xFFFFFFFF;
    if (skala == 0) skala = 1; 
    
    UkryjKursor();
    WypiszTekst(text, x, y, kolor, skala);
    PokazKursor();
}

extern "C" void bws_gui_rysuj_prostokat(int x, int y, int w, int h, uint32_t kolor) {
    if(!backbuffer || !aktywny_ekran) return;
    UkryjKursor();
    RysujProstokat(x, y, w, h, kolor);
    PokazKursor();
}

extern "C" void bws_gui_ustaw_przejecie_myszy(bool stan) {
    ring3_gui_active = stan;
    if (stan) okna[0].widoczne = false; 
    else okna[0].widoczne = true; 
}

extern "C" void bws_gui_pobierz_rozdzielczosc(int* szer, int* wys) {
    if(szer) *szer = aktywny_ekran ? aktywny_ekran->PobierzSzerokosc() : 0;
    if(wys) *wys = aktywny_ekran ? aktywny_ekran->PobierzWysokosc() : 0;
}

extern "C" bool gui_czy_zamknieto_powloke() {
    if (flaga_zamkniecia_powloki) {
        flaga_zamkniecia_powloki = false;
        return true;
    }
    return false;
}

extern "C" int bws_gui_pobierz_szerokosc_znaku(uint32_t unicode) {
    uint32_t max_znaki = sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]);
    if (unicode < max_znaki) return nowa_czcionka_szerokosci[unicode];
    return 8;
}