#include "grafika.h"
#include "pamiec.h"
#include "zegar-rtc.h"
#include "ahci.h"

// --- PODŁĄCZENIE NOWEJ, GŁADKIEJ CZCIONKI 8x16 ---
#include "czcionki.h"

// ==================== ZMIENNE GLOBALNE MATRYCY ====================
static uint32_t* lfb = nullptr;
static uint32_t  lfb_szerokosc = 0;
static uint32_t  lfb_wysokosc = 0;
static uint32_t  lfb_pitch = 0;
static uint8_t   lfb_bpp = 32;

static uint8_t* backbuffer = nullptr;
static uint32_t* bufor_tapety = nullptr; 
static bool tapeta_zaladowana = false;   
static bool ring3_gui_active = false;

// ==================== DEKLARACJE ZAPOWIADAJACE ====================
void UkryjKursor();
void PokazKursor();
void OdswiezEkran();
void PrzeniesNaEkran();
void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys);
void rysuj_zegar_rtc();

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

// Tylko jedno awaryjne okno Jądra - Terminal!
static Okno okna[1] = {
    { 20, 20, 660, 360,  0,0,0,0, "Terminal", "Powłoka Bursztynowa (Ring 3 Terminal)", 0x001A0B00, true, false }
};

static int z_order[1] = {0}; 

// ==================== ZMIENNE ====================
static int okno_przeciagane = -1;
static int chwyt_x = 0;
static int chwyt_y = 0;
static bool lewy_wcisniety = false;

// ==================== PAMIEC TERMINALA ====================
struct ZnakTerminala {
    char znak;
    uint32_t kolor;
};
#define MAX_ROWS 80
#define MAX_COLS 140
static ZnakTerminala term_buf[MAX_ROWS][MAX_COLS];
static int term_r = 0, term_c = 0;
static int term_max_r = 25, term_max_c = 80;

// ==================== KURSOR I MYSZ ====================
static int mysz_x = 500;
static int mysz_y = 300;
static uint32_t bufor_kursora[16][16];
static bool kursor_widoczny = false;

static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
void SerialLog(const char* str) {
    for(int i = 0; str[i] != '\0'; i++) serial_outb(0x3F8, str[i]);
} 

static const uint8_t kursor_bitmapa[16][16] = {
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},{1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},{1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},{1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},{1,2,2,2,2,2,2,1,1,1,1,1,0,0,0,0},{1,2,2,1,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,2,1,0,0,0,0,0,0,0},{1,1,0,0,0,1,2,2,1,0,0,0,0,0,0,0},{0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0},{0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0}
};

// ==================== ZARZADZANIE BUFORAMI TEKSTOWYMI ====================
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
    SerialLog(tekst);
    SerialLog("\n");

    if(!backbuffer) return;
    UkryjKursor();
    DopiszDoBufora(tekst, 0x00FFBF00); DopiszDoBufora("\n", 0x00FFBF00);
    OdswiezEkran(); PokazKursor(); PrzeniesNaEkran();
}

// ==================== SILNIK GRAFICZNY ====================
void PrzeniesNaEkran() {
    if (!lfb || !backbuffer) return;
    uint32_t* dst = (uint32_t*)lfb; uint32_t* src = (uint32_t*)backbuffer;
    uint64_t ilosc_pikseli = ((uint64_t)lfb_pitch * (uint64_t)lfb_wysokosc) / 4;
    for(uint64_t i = 0; i < ilosc_pikseli; i++) dst[i] = src[i];
}

void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys) {
    if(!lfb || !backbuffer) return;
    int start_x = x < 0 ? 0 : x; int start_y = y < 0 ? 0 : y;
    int end_x = x + szer; int end_y = y + wys;
    
    if(end_x > (int)lfb_szerokosc) {
        end_x = lfb_szerokosc; 
    }
    if(end_y > (int)lfb_wysokosc) {
        end_y = lfb_wysokosc;
    }

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
    else if (lfb_bpp == 16) { uint16_t k = *(uint16_t*)piksel; return (((k >> 11) & 0x1F) << 19) | (((k >> 5) & 0x3F) << 10) | ((k & 0x1F) << 3); }
    return 0;
}

void RysujProstokat(int px, int py, int szer, int wys, uint32_t kolor) {
    int start_x = px < 0 ? 0 : px; int start_y = py < 0 ? 0 : py;
    int end_x = px + szer; int end_y = py + wys;
    
    if (end_x > (int)lfb_szerokosc) {
        end_x = lfb_szerokosc; 
    }
    if (end_y > (int)lfb_wysokosc) {
        end_y = lfb_wysokosc;
    }

    for(int y = start_y; y < end_y; y++) {
        for(int x = start_x; x < end_x; x++) PostawPiksel(x, y, kolor);
    }
}

void RysujZnak(uint32_t unicode, int px, int py, uint32_t kolor_tekstu, uint32_t kolor_tla, bool przezroczyste_tlo, int skala) {
    const uint8_t* glyph = PobierzZnakPL(unicode);
    if (!glyph) {
        if (unicode >= 32 && unicode <= 126) glyph = czcionka_ascii_8x16[unicode - 32];
        else glyph = czcionka_ascii_8x16[0]; 
    }

    for(int y = 0; y < 16; y++) {
        for(int x = 0; x < 9; x++) { 
            bool zmaluj = false;
            if (x < 8) zmaluj = glyph[y] & (1 << (7 - x));
            
            if(zmaluj) {
                for(int sy=0; sy<skala; sy++)
                    for(int sx=0; sx<skala; sx++) PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tekstu);
            } else if (!przezroczyste_tlo) { 
                for(int sy=0; sy<skala; sy++)
                    for(int sx=0; sx<skala; sx++) PostawPiksel(px + (x*skala) + sx, py + (y*skala) + sy, kolor_tla);
            }
        }
    }
}

void WypiszTekst(const char* tekst, int px, int py, uint32_t kolor_tekstu, int skala) {
    int start_x = px; int i = 0;
    while (tekst[i] != '\0') {
        uint32_t unicode = (uint8_t)tekst[i];
        if ((tekst[i] & 0xE0) == 0xC0 && tekst[i+1] != '\0') {
            uint8_t b1 = (uint8_t)tekst[i]; uint8_t b2 = (uint8_t)tekst[i+1];
            unicode = ((b1 & 0x1F) << 6) | (b2 & 0x3F); i++; 
        }
        RysujZnak(unicode, start_x, py, kolor_tekstu, 0, true, skala);
        start_x += 9 * skala; i++;
    }
}

void rysuj_zegar_rtc() {
    czas_rtc czas; 
    pobierz_czas_rtc(&czas);
    char bufor_czasu[16]; 
    formatuj_czas_do_stringa(&czas, bufor_czasu);
    
    int zegar_x = lfb_szerokosc - 150; 
    int zegar_y = lfb_wysokosc - 32;
    
    // Zegarmistrzowski kamuflaż: Rysujemy tło identyczne jak Pasek Zadań!
    RysujProstokat(zegar_x, lfb_wysokosc - 40, 150, 40, 0x001A0B00); // Tło paska
    RysujProstokat(zegar_x, lfb_wysokosc - 40, 150, 2, 0x00E58A00);  // Złota ramka na górze
    
    // Rysujemy żółty tekst zegara
    WypiszTekst(bufor_czasu, zegar_x + 10, zegar_y, 0x00FFBF00, 2);
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
            }
            cx += 9 * skala;
        }
    }
    if (rysuj_kursor && aktywne_okno) {
        int cx = start_x + (c_cursor * 9 * skala); int cy = start_y + (r_cursor * wysokosc_linii); 
        if (cy + 14 < py + wys && cx + 9 < px + szer) RysujProstokat(cx, cy + 14, 9, 2, 0x00FFBF00); 
    }
}

// ==================== ODSWIEZANIE EKRANU ====================
void OdswiezEkran() {
    if(!backbuffer) return;
    
    if (tapeta_zaladowana && bufor_tapety) {
        for(int y = 0; y < (int)lfb_wysokosc; y++) {
            uint32_t* dst_row = (uint32_t*)(backbuffer + y * lfb_pitch);
            uint32_t* src_row = bufor_tapety + y * lfb_szerokosc;
            for(int x = 0; x < (int)lfb_szerokosc; x++) dst_row[x] = src_row[x];
        }
    } else {
        RysujProstokat(0, 0, lfb_szerokosc, lfb_wysokosc, 0x001A0B00);
    }
    
    for(int k = 0; k < 1; k++) {
        int i = z_order[k];
        if (!okna[i].widoczne) continue;
        
        RysujOkno(i);
        if (i == 0) RysujTekstZBufora(term_buf, term_max_r, term_max_c, term_r, term_c, okna[0].x, okna[0].y, okna[0].szer, okna[0].wys, false, false);
    }

    if (lfb_wysokosc >= 40) {
        // ROZWIĄZANIE PROBLEMU: Kiedy Pulpit jest uśpiony, Jądro rysuje awaryjny pasek 
        // na całą szerokość ekranu. Dzięki temu zegar nie wisi brzydko w powietrzu!
        if (!ring3_gui_active) {
            RysujProstokat(0, lfb_wysokosc - 40, lfb_szerokosc, 40, 0x001A0B00);
            RysujProstokat(0, lfb_wysokosc - 40, lfb_szerokosc, 2, 0x00E58A00);
            WypiszTekst("Bursztyn OS - Terminal", 20, lfb_wysokosc - 28, 0x008A5A00, 1);
        }
        rysuj_zegar_rtc();
    }
}

// ==================== KONTROLA URZADZEN ====================
extern "C" bool zaktualizuj_klawiature_gui(char znak) {
    // Terminal nie przejmuje wpisywania klawiatury jeśli Ring 3 działa.
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
    int32_t min_x = -(int32_t)o.szer + 80; 
    int32_t max_x = (int32_t)lfb_szerokosc - 80;
    int32_t min_y = 0; 
    int32_t max_y = (int32_t)lfb_wysokosc - 40; 
    
    if ((int32_t)o.x < min_x) {
        o.x = (uint32_t)min_x; 
    }
    if ((int32_t)o.x > max_x) {
        o.x = (uint32_t)max_x;
    }
    
    if ((int32_t)o.y < min_y) {
        o.y = (uint32_t)min_y; 
    }
    if ((int32_t)o.y > max_y) {
        o.y = (uint32_t)max_y;
    }
}

extern "C" void zaktualizuj_mysze(int dx, int dy, uint8_t przyciski) {
    if (!backbuffer) return;
    int stary_mysz_x = mysz_x; int stary_mysz_y = mysz_y;
    UkryjKursor();
    mysz_x += dx; mysz_y -= dy; 
    
    if (mysz_x < 0) {
        mysz_x = 0; 
    }
    if (mysz_x >= (int)lfb_szerokosc - 2) {
        mysz_x = lfb_szerokosc - 2;
    }
    
    if (mysz_y < 0) {
        mysz_y = 0; 
    }
    if (mysz_y >= (int)lfb_wysokosc - 2) {
        mysz_y = lfb_wysokosc - 2;
    }
    
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
        bool przechwycono = false;
        
        if (!przechwycono) {
            for (int k = 0; k >= 0; k--) {
                int i = z_order[k]; if (!okna[i].widoczne) continue;
                int px = (int)okna[i].x; int py = (int)okna[i].y; int sz = (int)okna[i].szer; int wy = (int)okna[i].wys;
                
                if (mysz_x >= px && mysz_x <= px + sz && mysz_y >= py && mysz_y <= py + wy) {
                    wymaga_odrysowania = true;
              
                 if (mysz_y <= py + 26) {
                        if (mysz_x >= px + sz - 26 && mysz_x <= px + sz - 6) { okna[i].widoczne = false; break; }
                        int min_btn_x = (px + sz - 74);
                        if (mysz_x >= min_btn_x && mysz_x <= min_btn_x + 20) { okna[i].widoczne = false; break; }
                        if (mysz_x >= px + sz - 50 && mysz_x <= px + sz - 30) { 
                            if (!okna[i].zmaksymalizowane) {
                                okna[i].stary_x = okna[i].x; okna[i].stary_y = okna[i].y; okna[i].stary_szer = okna[i].szer; okna[i].stary_wys = okna[i].wys;
                                okna[i].x = 0; okna[i].y = 0; okna[i].szer = lfb_szerokosc; okna[i].wys = lfb_wysokosc - 40; 
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

extern "C" void* PobierzAktualnePML4();

// ==================== ZALADOWANIE BMP (AHCI) ====================
extern "C" void wczytaj_tapete_z_dysku() {
    wypisz_log("[GRAFIKA] Proba wczytania tapeta.bmp z dysku AHCI (LBA 10)...");
    uint64_t vaddr_raw = 0x91000000ULL;
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

    uint64_t vaddr_tapeta = 0x90000000ULL;
    uint64_t waga_tapety_bajty = (uint64_t)lfb_szerokosc * (uint64_t)lfb_wysokosc * 4;
    for(uint64_t i = 0; i < waga_tapety_bajty; i += 4096) ZmapujStrone((void*)(vaddr_tapeta + i), ZaalokujRamke(), 0b11);
    asm volatile("mov %0, %%cr3" : : "r"(PobierzAktualnePML4()) : "memory");
    bufor_tapety = (uint32_t*)vaddr_tapeta;

    uint64_t ilosc_pikseli_ekranu = lfb_szerokosc * lfb_wysokosc;
    for(uint64_t i = 0; i < ilosc_pikseli_ekranu; i++) bufor_tapety[i] = 0x000A0500; 

    int offset_x = ((int)lfb_szerokosc - (int)bmp_szerokosc) / 2;
    int offset_y = ((int)lfb_wysokosc - (int)bmp_wysokosc) / 2;
    
    if (offset_x < 0) {
        offset_x = 0; 
    }
    if (offset_y < 0) {
        offset_y = 0;
    }

    for (int y = 0; y < bmp_wysokosc; y++) {
        for (int x = 0; x < bmp_szerokosc; x++) {
            int docelowy_x = offset_x + x; int docelowy_y = offset_y + y;
            if (docelowy_x >= (int)lfb_szerokosc || docelowy_y >= (int)lfb_wysokosc) continue;
            
            int d_y = rysuj_od_gory ? y : (bmp_wysokosc - 1 - y);
            int rzad_bajtow = ((bmp_szerokosc * bmp_bpp) + 31) / 32 * 4;
            uint8_t* piksel = raw_bmp + piksele_offset + (d_y * rzad_bajtow) + (x * (bmp_bpp / 8));
            
            uint32_t kolor = 0;
            if (bmp_bpp == 24 || bmp_bpp == 32) kolor = (piksel[2] << 16) | (piksel[1] << 8) | piksel[0]; 
            bufor_tapety[docelowy_y * lfb_szerokosc + docelowy_x] = kolor;
        }
    }

    tapeta_zaladowana = true;
    wypisz_log("[GRAFIKA] Pulpit i tapeta gotowe!");
    OdswiezEkran(); PrzeniesNaEkran();
}

// ==================== INICJALIZACJA GRAFIKI ====================
void InicjalizujGrafike(uint64_t adres_mb2) {
    if(adres_mb2 == 0) return;
    uint32_t rozmiar = *(uint32_t*)adres_mb2; uint64_t aktualny = adres_mb2 + 8; uint64_t lfb_fizyczny = 0;

    while(aktualny < adres_mb2 + rozmiar) {
        TagFramebufferMB2* tag = (TagFramebufferMB2*)aktualny;
        if(tag->typ == 0) break;
        if(tag->typ == 8) { 
            lfb_fizyczny = tag->adres_fizyczny; lfb_pitch = tag->pitch; 
            lfb_szerokosc = tag->szerokosc; lfb_wysokosc = tag->wysokosc; lfb_bpp = tag->bpp; break;
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
        for(int r = 0; r < MAX_ROWS; r++) { for(int c = 0; c < MAX_COLS; c++) { term_buf[r][c].znak = 0; } }
        
        if (okna[0].x + okna[0].szer > lfb_szerokosc) okna[0].szer = lfb_szerokosc - okna[0].x - 20;
        if (okna[0].y + okna[0].wys > lfb_wysokosc - 40) okna[0].wys = lfb_wysokosc - okna[0].y - 40;
        OgraniczOkno(okna[0]);
        
        term_max_c = (okna[0].szer - 12) / 9; term_max_r = (okna[0].wys - 36) / 16; 
        
        if (term_max_c > MAX_COLS) {
            term_max_c = MAX_COLS; 
        }
        if (term_max_r > MAX_ROWS) {
            term_max_r = MAX_ROWS;
        }
        
        OdswiezEkran(); PokazKursor(); PrzeniesNaEkran();     
    } else {
        SerialLog("[GRAFIKA] BLAD KRYTYCZNY: Brak LFB!\n");
        while(true) asm volatile("cli; hlt");
    }
}

extern "C" void wypisz_na_ekranie(const char* tekst) {
    if(!backbuffer) return;
    UkryjKursor(); DopiszDoBufora(tekst, 0x00E58A00); OdswiezEkran(); PokazKursor(); PrzeniesNaEkran();
}

extern "C" void obsluga_przerwania_zegara() {
    static uint8_t stara_sekunda = 255;
    czas_rtc czas; 
    pobierz_czas_rtc(&czas);
    
    if (czas.sekundy != stara_sekunda) {
        stara_sekunda = czas.sekundy;
        if (!backbuffer || lfb_wysokosc < 40) return;
        bool kursor_byl = kursor_widoczny; 
        if (kursor_byl) UkryjKursor();
        
        rysuj_zegar_rtc(); 
        int zegar_x = lfb_szerokosc - 150; 
        
        // Zwiększyliśmy obszar odświeżania na ekran, by objął całą wysokość paska (40 pikseli)
        PrzeniesFragmentNaEkran(zegar_x, lfb_wysokosc - 40, 150, 40);
        
        if (kursor_byl) PokazKursor();
    }
}

// === NOWE FUNKCJE GUI DLA RING 3 (SYSCALLS) ===
extern "C" void bws_gui_rysuj_okno(int x, int y, int szer, int wys, const char* tytul) {
    if(!backbuffer) return;
    UkryjKursor();
    RysujProstokat(x, y, szer, wys, 0x008A5A00);             // Ramka
    RysujProstokat(x + 2, y + 2, szer - 4, 24, 0x00FFBF00);  // Pasek tytułu
    WypiszTekst(tytul, x + 8, y + 4, 0x001A0B00, 1);         // Tytuł
    RysujProstokat(x + 2, y + 28, szer - 4, wys - 30, 0x00280F00); // Tło
    PokazKursor();
}

extern "C" void bws_gui_wypisz_tekst(int x, int y, const char* text) {
    if(!backbuffer) return;
    UkryjKursor();
    WypiszTekst(text, x, y, 0x00D1D5DB, 1);
    PokazKursor();
}

extern "C" void bws_gui_wyczyscz_obszar(int x, int y, int szer, int wys) {
    if(!backbuffer) return;
    UkryjKursor();
    RysujProstokat(x, y, szer, wys, 0x00280F00);
    PokazKursor();
}

extern "C" void bws_gui_odswiez() {
    if(!backbuffer) return;
    PrzeniesNaEkran();
}
// Zwraca pozycję myszy do programów Ring 3
extern "C" void bws_gui_pobierz_mysz(int* x, int* y, uint8_t* przyciski) {
    if(!backbuffer || !x || !y || !przyciski) return;
    *x = mysz_x;
    *y = mysz_y;
    *przyciski = lewy_wcisniety ? 1 : 0;
}

// Czyści ekran i rysuje pulpit
extern "C" void bws_gui_odswiez_pulpit() {
    if(!backbuffer) return;
    UkryjKursor();
    OdswiezEkran(); 
    PokazKursor();
}

// ODCZYTUJEMY SKALĘ ZE ZMIENNEJ 64-bitowej!
extern "C" void bws_gui_wypisz_tekst_kolor(int x, int y, uint64_t kolor_skala, const char* text) {
    if(!backbuffer) return;
    uint32_t kolor = kolor_skala & 0xFFFFFFFF;
    int skala = (kolor_skala >> 32) & 0xFFFFFFFF;
    if (skala == 0) skala = 1; // Kompatybilność wsteczna z powłoką i notatnikiem
    
    UkryjKursor();
    WypiszTekst(text, x, y, kolor, skala);
    PokazKursor();
}

extern "C" void bws_gui_rysuj_prostokat(int x, int y, int w, int h, uint32_t kolor) {
    if(!backbuffer) return;
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
    if(szer) *szer = lfb_szerokosc;
    if(wys) *wys = lfb_wysokosc;
}