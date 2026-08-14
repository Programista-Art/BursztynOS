/*
 * Mechanizm: API Wywolan Systemowych (Syscalls) w konwencji Bursztyn OS
 * Opis: Punkt docelowy C++ odbierajacy przelaczone w locie asynchroniczne zadania
 * wywolane ze strefy programu Ring 3 z wykorzystaniem MSR_LSTAR.
 * Wzbogacony o restrykcyjna warstwe bezpieczenstwa (PZB).
 */

#include <stdint.h>
#include <stdbool.h>
#include "pzb.h"
#include "zegar-rtc.h" // PODLACZENIE ZEGARA DO BWS
#include "grafika.h"
#include "siec.h"

// Prototypy zewnetrznych funkcji API jadra 
extern "C" {
    void wypisz_na_ekranie(const char* buf); 
    bool utworz_plik(const char* sciezka);
    bool zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc);
    char pobierz_znak_klawiatury(); 
    bool czytaj_z_pliku(const char* sciezka, char* bufor, uint32_t max_dlugosc);
    bool wylistuj_katalog(const char* sciezka, char* bufor, uint32_t max_dlugosc);
    
    // Prototypy dla usuwania i zmiany nazwy
    bool usun_twor(const char* sciezka);
    bool zmien_nazwe_tworu(const char* sciezka, const char* nowa_nazwa);
    
    bool bws_uruchom_program_z_pliku(const char* sciezka_pliku, uint8_t bzl_poziom, uint64_t flagi_praw, bool z_syscalla);
    
    // Funkcja ping
    void bws_siec_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4);
    // UWAGA: Usunięto stąd bws_siec_dns i bws_siec_pobierz_http, ponieważ uzywamy kernel_siec_... z "siec.h"
    
    // Funkcje GUI
    void bws_gui_rysuj_okno(int x, int y, int szer, int wys, const char* tytul);
    void bws_gui_wypisz_tekst(int x, int y, const char* text);
    void bws_gui_wyczyscz_obszar(int x, int y, int szer, int wys);
    void bws_gui_odswiez();
    void bws_gui_pobierz_mysz(int* x, int* y, uint8_t* przyciski);
    void bws_gui_odswiez_pulpit();
    void bws_gui_wypisz_tekst_kolor(int x, int y, uint64_t kolor_skala, const char* text);
    void bws_gui_rysuj_prostokat(int x, int y, int w, int h, uint32_t kolor);
    void bws_gui_ustaw_przejecie_myszy(bool stan);
    void bws_gui_pobierz_rozdzielczosc(int* szer, int* wys);
    int bws_gui_pobierz_szerokosc_znaku(uint32_t unicode);
    bool gui_czy_zamknieto_powloke();
    void bws_dzwiek_test(uint32_t czestotliwosc, uint32_t czas);
}

extern bool hda_test_ton(uint32_t czestotliwosc_hz, uint32_t czas_ms);

// Globalna zmienna z siec.cpp przetrzymująca ilość pobranych bajtów z Internetu
extern uint32_t tcp_zapisano_bajtow;

// Zewnetrzny odnosnik do punktu wejsciowego SYSCALL zakodowanego w Asemblerze
extern "C" void brama_wywolan_systemowych();

// Zewnetrzny adres ze strefy linkera symbolizujacy srodek stosu Ring 0
extern "C" uint64_t stack_top;

// Globalna zmienna przechowujaca wskaznik na rdzenny stos.
uint64_t bezpieczny_stos_jadra;

static inline void zapisz_msr(uint32_t msr, uint64_t wartosc) {
    uint32_t dolny = (uint32_t)(wartosc & 0xFFFFFFFF);
    uint32_t gorny = (uint32_t)(wartosc >> 32);
    asm volatile("wrmsr" : : "a"(dolny), "d"(gorny), "c"(msr));
}

static inline uint64_t odczytaj_msr(uint32_t msr) {
    uint32_t dolny, gorny;
    asm volatile("rdmsr" : "=a"(dolny), "=d"(gorny) : "c"(msr));
    return ((uint64_t)gorny << 32) | dolny;
}

extern "C" void inicjalizuj_syscalls() {
    uint64_t efer = odczytaj_msr(0xC0000080);
    zapisz_msr(0xC0000080, efer | 1); 

    uint32_t star_gorny = (0x13 << 16) | 0x08; 
    zapisz_msr(0xC0000081, ((uint64_t)star_gorny << 32));
    zapisz_msr(0xC0000082, (uint64_t)&brama_wywolan_systemowych);
    zapisz_msr(0xC0000084, 0x200);

    bezpieczny_stos_jadra = (uint64_t)&stack_top;
    zapisz_msr(0xC0000102, (uint64_t)&bezpieczny_stos_jadra);
}

static bool sciezka_zaczyna_sie_od(const char* sciezka, const char* prefiks) {
    int i = 0;
    while (prefiks[i] != '\0') {
        if (sciezka[i] != prefiks[i]) return false;
        i++;
    }
    return true;
}

/*
 * --- DYSPOZYTOR KODOW (Bursztyn OS ABI) Z SYSTEMEM BZL/PZB ---
 */
extern "C" uint64_t obsluga_wywolan_systemowych(uint64_t nr_funkcji, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    // Bezpieczne uruchomienie Menedżera Okien, jeśli kliknięto X na Powłoce
    if (gui_czy_zamknieto_powloke()) {
        bws_uruchom_program_z_pliku("/menedzer_okien.bur", PZB_UZYTKOWNIK, 0xFFFFFFFF, true);
    }

    (void)arg4; 
    uint64_t kod_wyniku = 0;

    switch(nr_funkcji) {
        case 1: { wypisz_na_ekranie((const char*)arg1); kod_wyniku = 1; break; }
        case 2: {
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) return 0;
            const char* sciezka = (const char*)arg1;
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK && (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) return 0; 
            kod_wyniku = utworz_plik(sciezka) ? 1 : 0; break;
        }
        case 3: {
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) return 0;
            const char* sciezka = (const char*)arg1;
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK && (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) return 0; 
            kod_wyniku = zapisz_do_pliku(sciezka, (const char*)arg2, (uint32_t)arg3) ? 1 : 0; break;
        }
        case 4: { kod_wyniku = (uint64_t)pobierz_znak_klawiatury(); break; }
        case 5: {
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_CZYTAJ)) return 0;
            kod_wyniku = czytaj_z_pliku((const char*)arg1, (char*)arg2, (uint32_t)arg3) ? 1 : 0; break;
        }
        case 6: {
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_CZYTAJ)) return 0;
            kod_wyniku = wylistuj_katalog((const char*)arg1, (char*)arg2, (uint32_t)arg3) ? 1 : 0; break;
        }
        case 7: {
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) return 0;
            const char* sciezka = (const char*)arg1;
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK && (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) return 0; 
            kod_wyniku = usun_twor(sciezka) ? 1 : 0; break;
        }
        case 8: {
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) return 0;
            const char* sciezka = (const char*)arg1;
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK && (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) return 0; 
            kod_wyniku = zmien_nazwe_tworu(sciezka, (const char*)arg2) ? 1 : 0; break;
        }
        case 9: {
            czas_rtc czas; pobierz_czas_rtc(&czas); formatuj_czas_do_stringa(&czas, (char*)arg1); kod_wyniku = 1; break;
        }
        case 10: {
            if (!(aktywny_proces.uprawnienia & PRAWO_URUCHOM_PROGRAM)) return 0;
            kod_wyniku = bws_uruchom_program_z_pliku((const char*)arg1, PZB_UZYTKOWNIK, 0xFFFFFFFF, true) ? 1 : 0; break;
        }
        case 11: {
            bws_siec_ping((uint8_t)arg1, (uint8_t)arg2, (uint8_t)arg3, (uint8_t)arg4); kod_wyniku = 1; break;
        }
        case 12: {
            // POPRAWKA: Używamy wewnętrznej funkcji jądra kernel_siec_dns zamiast powłoki bws_
            kod_wyniku = kernel_siec_dns((const char*)arg1, (uint8_t*)arg2) ? 1 : 0;
            break;
        }
        case 13: {
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) return 0;
            
            uint8_t* cel_ip = (uint8_t*)arg1;
            const char* domena = (const char*)arg2;
            const char* sciezka_http = (const char*)arg3;
            const char* sciezka_dyskowa = (const char*)arg4;
            
            static char bufor_pobierania[65536]; 
            for(int i=0; i<65536; i++) bufor_pobierania[i] = 0;
            
            // POPRAWKA: Używamy kernel_siec_pobierz_http zamiast bws_
            if (kernel_siec_pobierz_http(cel_ip, domena, sciezka_http, bufor_pobierania, 65535)) {
                utworz_plik(sciezka_dyskowa);
                zapisz_do_pliku(sciezka_dyskowa, bufor_pobierania, tcp_zapisano_bajtow);
                kod_wyniku = 1;
            }
            break;
        }
        case 14: { 
            int x = arg1 >> 32; int y = arg1 & 0xFFFFFFFF;
            int w = arg2 >> 32; int h = arg2 & 0xFFFFFFFF;
            bws_gui_rysuj_okno(x, y, w, h, (const char*)arg3); 
            kod_wyniku = 1; break; 
        }
        case 15: { bws_gui_wypisz_tekst(arg1, arg2, (const char*)arg3); kod_wyniku = 1; break; }
        case 16: { bws_gui_wyczyscz_obszar(arg1, arg2, arg3, arg4); kod_wyniku = 1; break; }
        case 17: { bws_gui_odswiez(); kod_wyniku = 1; break; }
        case 18: { bws_gui_pobierz_mysz((int*)arg1, (int*)arg2, (uint8_t*)arg3); kod_wyniku = 1; break; }
        case 19: { bws_gui_odswiez_pulpit(); kod_wyniku = 1; break; }
        case 20: { bws_gui_wypisz_tekst_kolor(arg1, arg2, arg3, (const char*)arg4); kod_wyniku = 1; break; }
        case 21: { 
            int x = arg1 >> 32; int y = arg1 & 0xFFFFFFFF;
            int w = arg2 >> 32; int h = arg2 & 0xFFFFFFFF;
            bws_gui_rysuj_prostokat(x, y, w, h, arg3); 
            kod_wyniku = 1; break; 
        }
        case 22: { bws_gui_ustaw_przejecie_myszy(arg1 != 0); kod_wyniku = 1; break; }
        case 23: { bws_gui_pobierz_rozdzielczosc((int*)arg1, (int*)arg2); kod_wyniku = 1; break; }
        case 24: { kod_wyniku = (uint64_t)bws_gui_pobierz_szerokosc_znaku((uint32_t)arg1); break; }
        case 25: { 
            // Uruchom ponownie (Reset przez kontroler klawiatury 8042)
            asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            while(true) asm volatile("cli; hlt");
            break; 
        }
        case 26: { 
            // Zamknij system (ACPI Shutdown dla QEMU - port 0x604)
            asm volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
            while(true) asm volatile("cli; hlt");
            break; 
        }
        case 27: { 
            // TEST Dźwięku 
            hda_test_ton((uint32_t)arg1, (uint32_t)arg2);
            break;
        }
        case 28: {
            wypisz_log("[SYSCALL] Otrzymano żądanie DNS (28)");
            if (arg1 == 0 || arg2 == 0) {
                wypisz_log("[SYSCALL-BLAD] DNS: Pusty wskaźnik domeny lub IP!");
                kod_wyniku = 0;
                break;
            }
            // Bezpieczne wywołanie DNS jądra
            bool wynik = kernel_siec_dns((const char*)arg1, (uint8_t*)arg2);
            kod_wyniku = wynik ? 1 : 0;
            break;
        }
        case 29: {
            wypisz_log("[SYSCALL] Otrzymano żądanie HTTP (29)");
            if (arg1 == 0 || arg2 == 0 || arg3 == 0) {
                wypisz_log("[SYSCALL-BLAD] HTTP: Puste wskaźniki argumentów!");
                kod_wyniku = 0;
                break;
            }
            
            uint8_t* cel_ip = (uint8_t*)arg1;
            const char* domena = (const char*)arg2;
            const char* sciezka = (const char*)arg3;
            
            char* bufor = (char*)(arg4 >> 32);
            uint32_t max_dlugosc = (uint32_t)(arg4 & 0xFFFFFFFF);

            if (bufor == 0 || max_dlugosc == 0) {
                wypisz_log("[SYSCALL-BLAD] HTTP: Nieprawidłowy bufor docelowy!");
                kod_wyniku = 0;
                break;
            }

            bool wynik = kernel_siec_pobierz_http(cel_ip, domena, sciezka, bufor, max_dlugosc);
            kod_wyniku = wynik ? 1 : 0;
            break;
        }
        case 30: {
            if (arg1 == 0 || arg2 == 0 || arg3 == 0) { kod_wyniku = 0; break; }
            char* bufor = (char*)(arg4 >> 32);
            uint32_t max_dlugosc = (uint32_t)arg4;
            if (!bufor || max_dlugosc < 2) { kod_wyniku = 0; break; }
            kod_wyniku = kernel_siec_pobierz_https((uint8_t*)arg1, (const char*)arg2,
                                                   (const char*)arg3, bufor, max_dlugosc) ? 1 : 0;
            break;
        }
        case 31: {
            kod_wyniku = kernel_tls_certyfikat_zaufany() ? 1 : 0;
            break;
        }
        default: {
            wypisz_na_ekranie("[!] Otrzymano nierozpoznany wektor z Ring 3!"); kod_wyniku = (uint64_t)-1; break;
        }
    }
    return kod_wyniku;
}
