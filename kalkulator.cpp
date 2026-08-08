/*
 * Kalkulator (GUI) dla Bursztyn OS (Aplikacja Ring 3)
 * Wersja nowoczesna z idealnie wyśrodkowanymi przyciskami.
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
        4096,  16384, 0x601000, 
        20480, 32768, 0x605000  
    };
}

static char calc_display[64] __attribute__((section(".data"))) = "0";
static char calc_num_buf[32] __attribute__((section(".data"))) = "0";
static int  calc_op1 = 0;
static char calc_op = 0;
static int  calc_state = 0; 

const char* calc_btns[16] = {
    "7", "8", "9", "/",
    "4", "5", "6", "*",
    "1", "2", "3", "-",
    "C", "0", "=", "+"
};

int WIN_X = 300, WIN_Y = 150, WIN_W = 320, WIN_H = 380;
bool dragging = false, aplikacja_zminimalizowana = false;
int drag_off_x = 0, drag_off_y = 0;
int screen_w = 1024, screen_h = 768; 




void calc_append_str(char* dest, const char* src) {
    int i = 0; while(dest[i]) i++;
    int j = 0; while(src[j] && i < 62) dest[i++] = src[j++];
    dest[i] = '\0';
}

void calc_set_str(char* dest, const char* src) {
    int i = 0; while(src[i] && i < 62) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
}

int calc_str_to_int(const char* str) {
    int res = 0; int i = 0; bool neg = false;
    if (str[i] == '-') { neg = true; i++; }
    while (str[i] >= '0' && str[i] <= '9') { res = res * 10 + (str[i] - '0'); i++; }
    return neg ? -res : res;
}

void calc_int_to_str(int val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0; bool neg = false;
    if (val < 0) { neg = true; val = -val; }
    char temp[32];
    while (val > 0) { temp[i++] = (val % 10) + '0'; val /= 10; }
    int j = 0; if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = temp[--i];
    buf[j] = '\0';
}

void KalkulatorKlik(char btn) {
    if (btn >= '0' && btn <= '9') {
        if (calc_state == 0 || calc_state == 3) {
            if (calc_state == 3 || (calc_num_buf[0] == '0' && calc_num_buf[1] == '\0')) {
                calc_num_buf[0] = btn; calc_num_buf[1] = '\0';
                calc_set_str(calc_display, calc_num_buf);
            } else {
                char b[2] = {btn, 0};
                calc_append_str(calc_num_buf, b); calc_append_str(calc_display, b);
            }
            calc_state = 0;
        } else if (calc_state == 1) { 
            calc_num_buf[0] = btn; calc_num_buf[1] = '\0';
            char b[2] = {btn, 0}; calc_append_str(calc_display, b);
            calc_state = 2;
        } else if (calc_state == 2) {
            if (!(calc_num_buf[0] == '0' && calc_num_buf[1] == '\0')) {
                char b[2] = {btn, 0};
                calc_append_str(calc_num_buf, b); calc_append_str(calc_display, b);
            }
        }
    } else if (btn == 'C' || btn == 'c') {
        calc_set_str(calc_display, "0"); calc_set_str(calc_num_buf, "0");
        calc_op1 = 0; calc_op = 0; calc_state = 0;
    } else if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
        if (calc_state == 0) {
            calc_op1 = calc_str_to_int(calc_num_buf); calc_op = btn; calc_state = 1;
            char b[2] = {btn, 0}; calc_append_str(calc_display, b); 
        } else if (calc_state == 1) {
            calc_op = btn;
            int len = 0; while(calc_display[len]) len++;
            if (len >= 1) calc_display[len-1] = btn; 
        } else if (calc_state == 2) {
            int op2 = calc_str_to_int(calc_num_buf); int res = 0; bool ok = true;
            if (calc_op == '+') res = calc_op1 + op2;
            if (calc_op == '-') res = calc_op1 - op2;
            if (calc_op == '*') res = calc_op1 * op2;
            if (calc_op == '/') { if(op2 != 0) res = calc_op1 / op2; else ok = false; }
            
            if (!ok) { calc_set_str(calc_display, "ERR"); calc_state = 3; } 
            else {
                calc_op1 = res; calc_op = btn;
                calc_int_to_str(res, calc_num_buf); calc_set_str(calc_display, calc_num_buf);
                char b[2] = {btn, 0}; calc_append_str(calc_display, b);
                calc_state = 1;
            }
        } else if (calc_state == 3) {
            if (calc_display[0] == 'E') { calc_op1 = 0; calc_set_str(calc_display, "0"); calc_set_str(calc_num_buf, "0"); } 
            else { calc_op1 = calc_str_to_int(calc_display); }
            calc_op = btn;
            char b[2] = {btn, 0}; calc_append_str(calc_display, b);
            calc_state = 1;
        }
    } else if (btn == '=' || btn == '\n' || btn == '\r') {
        if (calc_state == 2) {
            int op2 = calc_str_to_int(calc_num_buf); int res = 0; bool ok = true;
            if (calc_op == '+') res = calc_op1 + op2;
            if (calc_op == '-') res = calc_op1 - op2;
            if (calc_op == '*') res = calc_op1 * op2;
            if (calc_op == '/') { if(op2 != 0) res = calc_op1 / op2; else ok = false; }
            
            if (!ok) calc_set_str(calc_display, "ERR");
            else { calc_int_to_str(res, calc_num_buf); calc_set_str(calc_display, calc_num_buf); }
            calc_state = 3; calc_op = 0;
        }
    }
}

void RysujInterfejs(bool odswiez_tlo) {
    if (odswiez_tlo) {
        gui_odswiez_pulpit();
        
        gui_rysuj_prostokat(0, screen_h - 40, screen_w, 40, 0x001A0B00); 
        gui_rysuj_prostokat(0, screen_h - 40, screen_w, 2, 0x00E58A00);
        
        gui_rysuj_prostokat(10, screen_h - 35, 80, 30, 0x00E58A00);
        gui_wypisz_tekst_kolor(14, screen_h - 33, 0x001A0B00, "Menu");
        
        uint32_t kolor_tla = aplikacja_zminimalizowana ? 0x001A0B00 : 0x004A2500;
        gui_rysuj_prostokat(100, screen_h - 40, 140, 40, kolor_tla);
        gui_rysuj_prostokat(100, screen_h - 40, 1, 40, 0x00E58A00);
        gui_rysuj_prostokat(239, screen_h - 40, 1, 40, 0x00E58A00);
        gui_rysuj_prostokat(100, screen_h - 40, 140, 1, 0x00E58A00);
        gui_wypisz_tekst_kolor(120, screen_h - 28, aplikacja_zminimalizowana ? 0x00D1D5DB : 0x00FFFFFF, "Kalkulator");
    }

    if (aplikacja_zminimalizowana) { gui_odswiez(); return; }

    gui_rysuj_okno(WIN_X, WIN_Y, WIN_W, WIN_H, "Kalkulator Systemowy");
    
    // Przyciski kontrolne okna
    gui_rysuj_prostokat(WIN_X + WIN_W - 50, WIN_Y + 4, 20, 20, 0x00E58A00);
    rysuj_tekst_wysrodkowany(WIN_X + WIN_W - 50, WIN_Y + 4, 20, 20, 1, 0x001A0B00, "-");
    
    gui_rysuj_prostokat(WIN_X + WIN_W - 26, WIN_Y + 4, 20, 20, 0x00AA0000);
    rysuj_tekst_wysrodkowany(WIN_X + WIN_W - 26, WIN_Y + 4, 20, 20, 1, 0x00FFFFFF, "X");

    // Nowoczesny wyświetlacz (Ciemny ekran z subtelnym dolnym pasekiem)
    gui_rysuj_prostokat(WIN_X + 10, WIN_Y + 35, WIN_W - 20, 50, 0x00050200);
    gui_rysuj_prostokat(WIN_X + 10, WIN_Y + 84, WIN_W - 20, 2, 0x00E58A00);
    
    // Tekst wyświetlacza (Prawo-stronnie wyrównany do środka)
    int szer_tekstu = oblicz_szerokosc_tekstu(calc_display, 2);
    int text_x = WIN_X + WIN_W - 20 - 10 - szer_tekstu; 
    if (text_x < WIN_X + 15) text_x = WIN_X + 15; 
    int text_y = WIN_Y + 35 + 25 - 16; // Wyśrodkowanie w pionie
    gui_wypisz_tekst_kolor_skala(text_x, text_y, 0x00FFBF00, 2, calc_display);

    // Siatka przycisków
    int start_x = WIN_X + 10;
    int start_y = WIN_Y + 95;
    int bw = (WIN_W - 50) / 4;
    int bh = (WIN_H - 140) / 4;

    for(int row=0; row<4; row++) {
        for(int col=0; col<4; col++) {
            int bx = start_x + col*(bw+10);
            int by = start_y + row*(bh+10);
            
            char btn_char = calc_btns[row*4 + col][0];
            uint32_t btn_color = 0x00242424; // Ciemnoszary jak w nowoczesnych Kalkulatorach Windows
            uint32_t txt_color = 0x00FFFFFF;
            
            if (btn_char == 'C') { btn_color = 0x00661100; txt_color = 0x00FF5555; } // Ciemna czerwień
            else if (btn_char == '=') { btn_color = 0x00E58A00; txt_color = 0x001A0B00; } // Bursztyn
            else if (btn_char == '/' || btn_char == '*' || btn_char == '-' || btn_char == '+') { 
                btn_color = 0x00302211; txt_color = 0x00E58A00; // Ciemny bursztyn dla operatorów
            }
            
            gui_rysuj_prostokat(bx, by, bw, bh, btn_color);
            
            // Cienka, elegancka ramka dla głębi
            gui_rysuj_prostokat(bx, by, bw, 1, 0x00404040);
            gui_rysuj_prostokat(bx, by + bh - 1, bw, 1, 0x00101010);
            gui_rysuj_prostokat(bx, by, 1, bh, 0x00404040);
            gui_rysuj_prostokat(bx + bw - 1, by, 1, bh, 0x00101010);

            // Używamy nowej funkcji do idealnego wyśrodkowania!
            rysuj_tekst_wysrodkowany(bx, by, bw, bh, 2, txt_color, calc_btns[row*4 + col]); 
        }
    }
    gui_odswiez();
}

extern "C" __attribute__((noreturn)) void _start() {
    calc_state = 0;
    calc_op1 = 0;
    calc_op = 0;
    calc_display[0] = '0'; calc_display[1] = '\0';
    calc_num_buf[0] = '0'; calc_num_buf[1] = '\0';
    dragging = false;
    aplikacja_zminimalizowana = false;
    drag_off_x = 0; drag_off_y = 0;

    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    gui_ustaw_przejecie_myszy(true);

    uint8_t poprz_przycisk = 0; int old_mx = -1, old_my = -1;
    bool wyjdz = false; bool redraw = true; bool odswiez_tlo = true;

    while (!wyjdz) {
        int mx, my; uint8_t mb; gui_pobierz_mysz(&mx, &my, &mb);
        bool klik = (mb == 1 && poprz_przycisk == 0);
        bool przytrzymany = (mb == 1);

        if (mx != old_mx || my != old_my) { 
            if (dragging && !aplikacja_zminimalizowana) {
                odswiez_tlo = true; 
                redraw = true; 
            }
        }

        if (klik) {
            if (my >= screen_h - 40) {
                if (mx >= 100 && mx <= 240) { aplikacja_zminimalizowana = !aplikacja_zminimalizowana; odswiez_tlo = true; redraw = true; }
            }
            else if (!aplikacja_zminimalizowana && mx >= WIN_X && mx <= WIN_X + WIN_W && my >= WIN_Y && my <= WIN_Y + WIN_H) {
                if (my >= WIN_Y && my <= WIN_Y + 26) {
                    if (mx >= WIN_X + WIN_W - 50 && mx <= WIN_X + WIN_W - 30) { 
                        aplikacja_zminimalizowana = true; 
                        odswiez_tlo = true; 
                        redraw = true; 
                    } 
                    else if (mx >= WIN_X + WIN_W - 26 && mx <= WIN_X + WIN_W - 6) { wyjdz = true; }
                    else { dragging = true; drag_off_x = mx - WIN_X; drag_off_y = my - WIN_Y; }
                } else {
                    int start_x = WIN_X + 10; int start_y = WIN_Y + 95;
                    int bw = (WIN_W - 50) / 4; int bh = (WIN_H - 140) / 4;
                    bool trafiono = false; 
                    for(int row=0; row<4 && !trafiono; row++) {
                        for(int col=0; col<4; col++) {
                            int bx = start_x + col*(bw+10); int by = start_y + row*(bh+10);
                            if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                                KalkulatorKlik(calc_btns[row*4 + col][0]);
                                redraw = true; trafiono = true; break;
                            }
                        }
                    }
                }
            }
        }

        if (mb == 0) { dragging = false; }
        
        if (dragging && przytrzymany && !aplikacja_zminimalizowana) { 
            WIN_X = mx - drag_off_x; WIN_Y = my - drag_off_y; 
            if (WIN_X < 0) { WIN_X = 0; } if (WIN_Y < 0) { WIN_Y = 0; } 
            odswiez_tlo = true; redraw = true; 
        }
        
        poprz_przycisk = mb; old_mx = mx; old_my = my;

        if (redraw) { RysujInterfejs(odswiez_tlo); redraw = false; odswiez_tlo = false; }
    }

    gui_ustaw_przejecie_myszy(false); 
    gui_odswiez_pulpit(); 
    gui_odswiez();
    bws_wywolaj(10, (uint64_t)"/menedzer_okien.bur");
    while(true);
}