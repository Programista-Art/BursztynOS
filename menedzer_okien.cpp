/*
 * Menedżer Okien (Pulpit i Pasek Zadań) dla Bursztyn OS
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

int screen_w = 1024, screen_h = 768;
bool menu_start_otwarte = false;

void RysujPulpit(bool wymus_pelne_odswiezenie) {
    if (wymus_pelne_odswiezenie) gui_odswiez_pulpit();

    // Pasek z powrotem na całą szerokość! Zegar wtopi się idealnie.
    gui_rysuj_prostokat(0, screen_h - 40, screen_w, 40, 0x001A0B00); 
    gui_rysuj_prostokat(0, screen_h - 40, screen_w, 2, 0x00E58A00);  

    RysujPrzycisk(10, screen_h - 35, 80, 30, 0x00E58A00, 0x001A0B00, " Menu");

    gui_rysuj_prostokat(50, 50, 64, 64, 0x00FFBF00);
    gui_rysuj_prostokat(52, 52, 60, 60, 0x00FFFFFF);
    gui_wypisz_tekst_kolor(46, 120, 0x00FFFFFF, "Notatnik");

    gui_rysuj_prostokat(150, 50, 64, 64, 0x008A5A00);
    gui_rysuj_prostokat(152, 52, 60, 60, 0x001A0B00);
    gui_wypisz_tekst_kolor(165, 65, 0x00FFBF00, "+ -");
    gui_wypisz_tekst_kolor(165, 85, 0x00FFBF00, "* =");
    gui_wypisz_tekst_kolor(138, 120, 0x00FFFFFF, "Kalkulator");

    if (menu_start_otwarte) {
        int menu_wys = 105; // WYŻSZE MENU
        int menu_y = screen_h - 40 - menu_wys;
        
        gui_rysuj_prostokat(10, menu_y, 220, menu_wys, 0x00301500); 
        gui_rysuj_prostokat(10, menu_y, 220, 1, 0x00E58A00);
        gui_rysuj_prostokat(10, menu_y, 1, menu_wys, 0x00E58A00);
        gui_rysuj_prostokat(229, menu_y, 1, menu_wys, 0x00E58A00);
        
        // DODANY NOTATNIK!
        gui_wypisz_tekst_kolor(20, menu_y + 15, 0x00FFFFFF, "> Powłoka Bursztyna");
        gui_wypisz_tekst_kolor(20, menu_y + 40, 0x00FFFFFF, "> Notatnik");
        gui_wypisz_tekst_kolor(20, menu_y + 65, 0x00FFFFFF, "> Kalkulator");
    }
    gui_odswiez();
}

extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    gui_ustaw_przejecie_myszy(true); // Wyłączamy wbudowane okna Jądra
    
    // Rysujemy pulpit pierwszy raz
    RysujPulpit(true);
    
    uint8_t poprz_przycisk = 0;

    while (true) {
        int mx, my; uint8_t mb;
        gui_pobierz_mysz(&mx, &my, &mb);
        bool klik = (mb == 1 && poprz_przycisk == 0);
        
        if (klik) {
            // 1. Kliknięcie w ikonę Notatnika na pulpicie
            if (!menu_start_otwarte && mx >= 50 && mx <= 114 && my >= 50 && my <= 114) {
                gui_ustaw_przejecie_myszy(false); // Oddajemy władzę
                // Zabijamy Pulpit i ładujemy Notatnik
                bws_wywolaj(10, (uint64_t)"/programy/notatnik.cebula/notatnik.bur");
            }
            // 1.5. Kliknięcie w ikonę Kalkulatora na pulpicie
            else if (!menu_start_otwarte && mx >= 150 && mx <= 214 && my >= 50 && my <= 114) {
                gui_ustaw_przejecie_myszy(false);
                bws_wywolaj(10, (uint64_t)"/programy/kalkulator.cebula/kalkulator.bur");
            }
            // 2. Kliknięcie w przycisk Start (Menu)
            else if (mx >= 10 && mx <= 90 && my >= screen_h - 35 && my <= screen_h - 5) {
                menu_start_otwarte = !menu_start_otwarte;
                RysujPulpit(false); // Rysujemy bez odświeżania tła (brak mrugania zegara!)
            }
            // 3. Kliknięcia wewnątrz otwartego Menu Start
            else if (menu_start_otwarte && mx >= 10 && mx <= 230 && my >= screen_h - 145 && my <= screen_h - 40) {
                int menu_y = screen_h - 40 - 105;
                
                // Wybrano: Powłoka Bursztyna
                if (my >= menu_y + 10 && my < menu_y + 35) {
                    gui_ustaw_przejecie_myszy(false);
                    bws_wywolaj(10, (uint64_t)"/shell.bur"); 
                }
                // POPRAWKA: Wybrano: Notatnik
                else if (my >= menu_y + 35 && my < menu_y + 60) {
                    gui_ustaw_przejecie_myszy(false);
                    bws_wywolaj(10, (uint64_t)"/programy/notatnik.cebula/notatnik.bur"); 
                }
                // Wybrano: Kalkulator
                else if (my >= menu_y + 60 && my < menu_y + 85) {
                    gui_ustaw_przejecie_myszy(false);
                    bws_wywolaj(10, (uint64_t)"/programy/kalkulator.cebula/kalkulator.bur"); 
                }
            }
            // 4. Kliknięcie gdziekolwiek indziej (Zamyka Menu Start, jeśli było otwarte)
            else {
                if (menu_start_otwarte) {
                    menu_start_otwarte = false;
                    // Tu musimy odświeżyć tapetę, żeby zmazać czarne tło menu start
                    RysujPulpit(true); 
                }
            }
        }
        poprz_przycisk = mb;
    }
}