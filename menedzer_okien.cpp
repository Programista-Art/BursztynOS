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
        4096, 32768, 0x601000,
        36864, 131072, 0x609000
    };
}

int screen_w = 1024, screen_h = 768;
bool menu_start_otwarte = false;

int hover_mx = -1, hover_my = -1;
int old_mx = -1, old_my = -1;

void RysujPulpit(bool wymus_pelne_odswiezenie) {
    if (wymus_pelne_odswiezenie) gui_odswiez_pulpit();

    gui_rysuj_prostokat(0, screen_h - 40, screen_w, 40, 0x001A0B00); 
    gui_rysuj_prostokat(0, screen_h - 40, screen_w, 2, 0x00E58A00);  

    // Przycisk Menu z wyśrodkowanym tekstem
    gui_rysuj_prostokat(10, screen_h - 35, 80, 30, 0x00E58A00);
    rysuj_tekst_wysrodkowany(10, screen_h - 35, 80, 30, 1, 0x001A0B00, "Menu");

    // Ikona Notatnika
    gui_rysuj_prostokat(50, 50, 48, 48, 0x00FFBF00); 
    gui_rysuj_prostokat(52, 52, 44, 44, 0x00FFFFFF); 
    gui_rysuj_prostokat(56, 58, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 64, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 70, 20, 2, 0x00000000);
    gui_rysuj_prostokat(56, 76, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 82, 24, 2, 0x00000000);
    rysuj_tekst_wysrodkowany(50, 104, 48, 16, 1, 0x00FFFFFF, "Notatnik");

    // Ikona Kalkulatora
    gui_rysuj_prostokat(130, 50, 48, 48, 0x008A5A00); 
    gui_rysuj_prostokat(132, 52, 44, 44, 0x001A0B00); 
    rysuj_tekst_wysrodkowany(130, 60, 48, 16, 1, 0x00FFBF00, "+ -");
    rysuj_tekst_wysrodkowany(130, 80, 48, 16, 1, 0x00FFBF00, "* =");
    rysuj_tekst_wysrodkowany(130, 104, 48, 16, 1, 0x00FFFFFF, "Kalkulator");

    // --- NOWE: Ikona Przeglądarki Hussar ---
    gui_rysuj_prostokat(210, 50, 48, 48, 0x000078D7); // Jasnoniebieska ramka
    gui_rysuj_prostokat(212, 52, 44, 44, 0x000055AA); // Ciemnoniebieskie wnętrze
    rysuj_tekst_wysrodkowany(210, 65, 48, 16, 1, 0x00FFFFFF, "WWW");
    rysuj_tekst_wysrodkowany(210, 104, 48, 16, 1, 0x00FFFFFF, "Hussar");

    // Menu Start
    if (menu_start_otwarte) {
        int menu_wys = 185; // ZWIĘKSZONO WYSOKOŚĆ dla 6 elementów
        int menu_y = screen_h - 40 - menu_wys;
        
        gui_rysuj_prostokat(10, menu_y, 220, menu_wys, 0x00301500); 
        gui_rysuj_prostokat(10, menu_y, 220, 1, 0x00E58A00);
        gui_rysuj_prostokat(10, menu_y, 1, menu_wys, 0x00E58A00);
        gui_rysuj_prostokat(229, menu_y, 1, menu_wys, 0x00E58A00);
        
        // 6 elementów w menu (Dodano Hussara)
        const char* menu_elementy[6] = {"> Powłoka Bursztyna", "> Notatnik", "> Kalkulator", "> Przeglądarka Hussar", "> Uruchom ponownie", "> Zamknij"};
        
        for (int i = 0; i < 6; i++) {
            int item_y = menu_y + 10 + (i * 25); 
            
            bool podswietl = (hover_mx >= 10 && hover_mx <= 230 && hover_my >= item_y && hover_my < item_y + 25);
            
            if (podswietl) {
                gui_rysuj_prostokat(11, item_y, 218, 25, 0x00E58A00);
                gui_wypisz_tekst_kolor(20, item_y + 5, 0x001A0B00, menu_elementy[i]);
            } else {
                gui_wypisz_tekst_kolor(20, item_y + 5, 0x00FFFFFF, menu_elementy[i]);
            }
        }
    }
    gui_odswiez();
}

extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    gui_ustaw_przejecie_myszy(true);
    
    RysujPulpit(true);
    
    uint8_t poprz_przycisk = 0;

    while (true) {
        int mx, my; uint8_t mb;
        gui_pobierz_mysz(&mx, &my, &mb);
        bool klik = (mb == 1 && poprz_przycisk == 0);
        
        hover_mx = mx; 
        hover_my = my;

        if ((mx != old_mx || my != old_my) && menu_start_otwarte) {
            RysujPulpit(false);
        }
        old_mx = mx; old_my = my;

        if (klik) {
            // Obsługa kliknięć w ikony na pulpicie
            if (!menu_start_otwarte && mx >= 50 && mx <= 98 && my >= 50 && my <= 98) {
                gui_ustaw_przejecie_myszy(false);
                bws_wywolaj(10, (uint64_t)"/programy/notatnik.cebula/notatnik.bur");
            }
            else if (!menu_start_otwarte && mx >= 130 && mx <= 178 && my >= 50 && my <= 98) {
                gui_ustaw_przejecie_myszy(false);
                bws_wywolaj(10, (uint64_t)"/programy/kalkulator.cebula/kalkulator.bur");
            }
            else if (!menu_start_otwarte && mx >= 210 && mx <= 258 && my >= 50 && my <= 98) { // Ikona Hussara
                gui_ustaw_przejecie_myszy(false);
                bws_wywolaj(10, (uint64_t)"/programy/przegladarka.cebula/przegladarka.bur"); 
            }
            // Obsługa otwierania/zamykania Menu
            else if (mx >= 10 && mx <= 90 && my >= screen_h - 35 && my <= screen_h - 5) {
                menu_start_otwarte = !menu_start_otwarte;
                RysujPulpit(false); 
            }
            // Obsługa kliknięć wewnątrz otwartego Menu Start
            else if (menu_start_otwarte && mx >= 10 && mx <= 230 && my >= screen_h - 40 - 185 && my <= screen_h - 40) {
                int menu_y = screen_h - 40 - 185;
                int item_index = (my - (menu_y + 10)) / 25;
                
                if (item_index >= 0 && item_index < 6) {
                    if (item_index == 0) { gui_ustaw_przejecie_myszy(false); bws_wywolaj(10, (uint64_t)"/shell.bur"); }
                    else if (item_index == 1) { gui_ustaw_przejecie_myszy(false); bws_wywolaj(10, (uint64_t)"/programy/notatnik.cebula/notatnik.bur"); }
                    else if (item_index == 2) { gui_ustaw_przejecie_myszy(false); bws_wywolaj(10, (uint64_t)"/programy/kalkulator.cebula/kalkulator.bur"); }
                    else if (item_index == 3) { gui_ustaw_przejecie_myszy(false); bws_wywolaj(10, (uint64_t)"/programy/przegladarka.cebula/przegladarka.bur"); }
                    else if (item_index == 4) { bws_wywolaj(25); while(true); } // Uruchom ponownie
                    else if (item_index == 5) { bws_wywolaj(26); while(true); } // Zamknij
                }
            }
            else {
                // Zamknięcie menu kliknięciem w tło
                if (menu_start_otwarte) {
                    menu_start_otwarte = false;
                    RysujPulpit(true); 
                }
            }
        }
        poprz_przycisk = mb;
    }
}
