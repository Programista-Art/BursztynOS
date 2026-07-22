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
}

// Zewnetrzny odnosnik do punktu wejsciowego SYSCALL zakodowanego w Asemblerze
extern "C" void brama_wywolan_systemowych();

// Zewnetrzny adres ze strefy linkera symbolizujacy srodek stosu Ring 0
extern "C" uint64_t stack_top;

// Globalna zmienna przechowujaca wskaznik na rdzenny stos.
uint64_t bezpieczny_stos_jadra;

// Pomocnicza funkcja przestrzeni zapisujacej do Model Specific Registers (MSR)
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

// Pomocnicza funkcja dla Jadra, przydatna przy ochronie katalogow systemowych
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
    (void)arg4; 
    uint64_t kod_wyniku = 0;

    switch(nr_funkcji) {
        case 1: {
            // sys_wypisz_tekst(const char* tekst) - Zawsze dozwolone dla terminala
            wypisz_na_ekranie((const char*)arg1);
            kod_wyniku = 1; 
            break;
        }
        case 2: {
            // sys_utworz_plik(const char* sciezka)
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) {
                wypisz_na_ekranie("\n[BWS Zablokowano] Brak uprawnienia PRAWO_PLIKI_ZAPISZ!\n");
                return 0;
            }
            const char* sciezka = (const char*)arg1;
            
            // PZB: Uzytkownicy (poziom 4 i wyzej) nie maja praw modyfikowania krytycznych folderow systemowych!
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK &&
                (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) {
                wypisz_na_ekranie("\n[PZB Odrzucono] Ring 3 probuje tworzyc pliki w obszarze systemowym!\n");
                return 0; 
            }
            bool result = utworz_plik(sciezka);
            kod_wyniku = result ? 1 : 0;
            break;
        }
        case 3: {
            // sys_zapisz_do_pliku(const char* sciezka, const char* dane, uint32_t dlugosc)
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) {
                wypisz_na_ekranie("\n[BWS Zablokowano] Brak uprawnienia PRAWO_PLIKI_ZAPISZ!\n");
                return 0;
            }
            const char* sciezka = (const char*)arg1;
            
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK &&
                (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) {
                wypisz_na_ekranie("\n[PZB Odrzucono] Ring 3 probuje wprowadzac zmiany w plikach systemowych!\n");
                return 0; 
            }
            bool result = zapisz_do_pliku(sciezka, (const char*)arg2, (uint32_t)arg3);
            kod_wyniku = result ? 1 : 0;
            break;
        }
        case 4: {
            // sys_pobierz_znak()
            kod_wyniku = (uint64_t)pobierz_znak_klawiatury();
            break;
        }
        case 5: {
            // sys_czytaj_z_pliku
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_CZYTAJ)) {
                wypisz_na_ekranie("\n[BWS Zablokowano] Brak uprawnienia PRAWO_PLIKI_CZYTAJ!\n");
                return 0;
            }
            bool result = czytaj_z_pliku((const char*)arg1, (char*)arg2, (uint32_t)arg3);
            kod_wyniku = result ? 1 : 0;
            break;
        }
        case 6: {
            // sys_wylistuj_katalog
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_CZYTAJ)) {
                wypisz_na_ekranie("\n[BWS Zablokowano] Brak uprawnienia PRAWO_PLIKI_CZYTAJ!\n");
                return 0;
            }
            bool result = wylistuj_katalog((const char*)arg1, (char*)arg2, (uint32_t)arg3);
            kod_wyniku = result ? 1 : 0;
            break;
        }
        
        case 7: {
            // sys_usun_twor(const char* sciezka) - NOWO
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) {
                wypisz_na_ekranie("\n[BWS Zablokowano] Brak uprawnienia PRAWO_PLIKI_ZAPISZ!\n");
                return 0;
            }
            const char* sciezka = (const char*)arg1;
            
            // Ochrona folderow systemowych
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK &&
                (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) {
                wypisz_na_ekranie("\n[PZB Odrzucono] Ring 3 probuje usuwac pliki systemowe!\n");
                return 0; 
            }
            bool result = usun_twor(sciezka);
            kod_wyniku = result ? 1 : 0;
            break;
        }
        
        case 8: {
            // sys_zmien_nazwe_tworu(const char* sciezka, const char* nowa_nazwa) - NOWO
            if (!(aktywny_proces.uprawnienia & PRAWO_PLIKI_ZAPISZ)) {
                wypisz_na_ekranie("\n[BWS Zablokowano] Brak uprawnienia PRAWO_PLIKI_ZAPISZ!\n");
                return 0;
            }
            const char* sciezka = (const char*)arg1;
            const char* nowa_nazwa = (const char*)arg2;
            
            // Ochrona folderow systemowych
            if (aktywny_proces.poziom_zaufania >= PZB_UZYTKOWNIK &&
                (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) {
                wypisz_na_ekranie("\n[PZB Odrzucono] Ring 3 probuje zmieniac nazwy plikow systemowych!\n");
                return 0; 
            }
            bool result = zmien_nazwe_tworu(sciezka, nowa_nazwa);
            kod_wyniku = result ? 1 : 0;
            break;
        }
        
        case 9: {
            // sys_pobierz_czas(char* bufor) - BWS nr 9
            // Umozliwia aplikacjom z Ring 3 odczyt czasu z plyty glownej
            czas_rtc czas;
            pobierz_czas_rtc(&czas);
            formatuj_czas_do_stringa(&czas, (char*)arg1);
            kod_wyniku = 1;
            break;
        }

        default: {
            wypisz_na_ekranie("[!] Otrzymano nierozpoznany wektor z Ring 3!");
            kod_wyniku = (uint64_t)-1;
            break;
        }
    }

    return kod_wyniku;
}