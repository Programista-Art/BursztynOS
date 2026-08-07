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
    if (wymus_pelne_odswiezenie) {
        gui_odswiez_pulpit(); // Jądro czyści ekran i rysuje tapetę
    }

    // Rysowanie Paska Zadań Ring 3 - Zostawiamy 120px miejsca na Zegar!
    gui_rysuj_prostokat(0, screen_h - 40, screen_w - 120, 40, 0x001A0B00); // Tło paska
    gui_rysuj_prostokat(0, screen_h - 40, screen_w - 120, 2, 0x00E58A00);  // Złota ramka na górze

    // Przycisk "Start / Menu"
    RysujPrzycisk(10, screen_h - 35, 80, 30, 0x00E58A00, 0x001A0B00, " Menu");

    // Ikona Notatnika na pulpicie
    gui_rysuj_prostokat(50, 50, 64, 64, 0x00FFBF00);
    gui_rysuj_prostokat(52, 52, 60, 60, 0x00FFFFFF);
    gui_wypisz_tekst_kolor(46, 120, 0x00FFFFFF, "Notatnik");

    // Jeśli Menu Start jest otwarte, rysujemy je na wierzchu!
    if (menu_start_otwarte) {
        gui_rysuj_prostokat(10, screen_h - 100, 220, 60, 0x00301500); 
        
        // Złota ramka dookoła menu Start
        gui_rysuj_prostokat(10, screen_h - 100, 220, 1, 0x00E58A00);
        gui_rysuj_prostokat(10, screen_h - 100, 1, 60, 0x00E58A00);
        gui_rysuj_prostokat(229, screen_h - 100, 1, 60, 0x00E58A00);
        
        gui_wypisz_tekst_kolor(20, screen_h - 80, 0x00FFFFFF, "> Powłoka Bursztyna");
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
            // 2. Kliknięcie w przycisk Start (Menu)
            else if (mx >= 10 && mx <= 90 && my >= screen_h - 35 && my <= screen_h - 5) {
                menu_start_otwarte = !menu_start_otwarte;
                RysujPulpit(false); // Rysujemy bez odświeżania tła (brak mrugania zegara!)
            }
            // 3. Kliknięcia wewnątrz otwartego Menu Start
            else if (menu_start_otwarte && mx >= 10 && mx <= 230 && my >= screen_h - 100 && my <= screen_h - 40) {
                // Wybrano: Powłoka Bursztyna
                if (my >= screen_h - 90 && my < screen_h - 60) {
                    gui_ustaw_przejecie_myszy(false);
                    // Zabijamy Pulpit i ładujemy powłokę testową (stary pasek powróci)
                    bws_wywolaj(10, (uint64_t)"/shell.bur"); 
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