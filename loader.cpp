#include "loader.h"
#include "grafika.h" // Używamy Składacza Obrazu do pięknych logów!
#include "pamiec.h"  // Zapewnia dostęp do natywnych funkcji C++: ZaalokujRamke() i ZmapujStrone()

// Oczekiwana funkcja z vmm.cpp zwracająca wskaźnik na drzewo stron
extern "C" void* PobierzAktualnePML4();

// Oczekiwana funkcja z Asemblera (ring3.S)
extern "C" void przejdz_do_ring3(uint64_t punkt_wejscia, uint64_t wirtualny_stos, bool z_syscalla);

// Oczekiwana funkcja z BSP
extern "C" uint8_t* bsp_wczytaj_plik_do_pamieci(const char* sciezka, uint64_t* rozmiar_wyj);

#include "pzb.h"
proces_t aktywny_proces; 

void KopiujPamiec(void* cel, const void* zrodlo, uint64_t rozmiar) {
    uint8_t* c = (uint8_t*)cel;
    const uint8_t* z = (const uint8_t*)zrodlo;
    for (uint64_t i = 0; i < rozmiar; i++) c[i] = z[i];
}

bool PorownajPamiec(const void* ptr1, const void* ptr2, uint64_t rozmiar) {
    const uint8_t* p1 = (const uint8_t*)ptr1;
    const uint8_t* p2 = (const uint8_t*)ptr2;
    for (uint64_t i = 0; i < rozmiar; i++) {
        if (p1[i] != p2[i]) return false;
    }
    return true;
}

extern "C" bool bws_uruchom_program_z_pliku(const char* sciezka_pliku, uint8_t bzl_poziom, uint64_t flagi_praw, bool z_syscalla) {
    wypisz_log("[LOADER] Proba uruchomienia programu...");

    uint64_t rozmiar_pliku = 0;
    uint8_t* bufor_pliku = bsp_wczytaj_plik_do_pamieci(sciezka_pliku, &rozmiar_pliku);
    
    if (!bufor_pliku || rozmiar_pliku < sizeof(NaglowekBur)) {
        wypisz_log("[LOADER-BLAD] Nie znaleziono pliku lub plik jest za maly!");
        return false;
    }

    NaglowekBur* naglowek = (NaglowekBur*)bufor_pliku;

    const uint8_t oczekiwana_magia[4] = {'B', 'U', 'R', '\0'};
    if (!PorownajPamiec(naglowek->magia, oczekiwana_magia, 4)) {
        wypisz_log("[LOADER-BLAD] Nieprawidlowy format pliku! To nie jest program .bur");
        return false;
    }

    wypisz_log("[LOADER] Sygnatura BUR poprawna. Alokacja pamieci uzytkownika...");

    uint32_t flagi_vmm_user = FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_USER;

    // 4. Mapowanie sekcji .tekst (KODU)
    for (uint64_t offset = 0; offset < naglowek->tekst_rozmiar; offset += 4096) {
        void* ramka = ZaalokujRamke();
        void* adres_wirtualny = (void*)(naglowek->tekst_wirtualny + offset);
        ZmapujStrone(adres_wirtualny, ramka, flagi_vmm_user);
    }
    
    // --- BEZPIECZNE KOPIOWANIE KODU ---
    // Kopiujemy tylko tyle bajtów, ile faktycznie znajduje się w pliku!
    uint64_t dostepny_kod_w_pliku = 0;
    if (rozmiar_pliku > naglowek->tekst_przesuniecie) {
        dostepny_kod_w_pliku = rozmiar_pliku - naglowek->tekst_przesuniecie;
    }
    uint64_t do_skopiowania_kod = (naglowek->tekst_rozmiar < dostepny_kod_w_pliku) ? naglowek->tekst_rozmiar : dostepny_kod_w_pliku;
    KopiujPamiec((void*)naglowek->tekst_wirtualny, bufor_pliku + naglowek->tekst_przesuniecie, do_skopiowania_kod);

    // 5. Mapowanie sekcji .dane (ZMIENNE)
    if (naglowek->dane_rozmiar > 0) {
        for (uint64_t offset = 0; offset < naglowek->dane_rozmiar; offset += 4096) { // TUTAJ JEST offset += 4096
            void* ramka = ZaalokujRamke();
            void* adres_wirtualny = (void*)(naglowek->dane_wirtualny + offset);
            ZmapujStrone(adres_wirtualny, ramka, flagi_vmm_user);
        }
        
        // WYZEROWANIE PAMIĘCI DLA .BSS
        uint8_t* ptr_dane = (uint8_t*)naglowek->dane_wirtualny;
        for (uint64_t i = 0; i < naglowek->dane_rozmiar; i++) {
            ptr_dane[i] = 0;
        }

        // --- KOPIOWANIE DANYCH ---
        uint64_t dostepne_dane_w_pliku = 0;
        if (rozmiar_pliku > naglowek->dane_przesuniecie) {
            dostepne_dane_w_pliku = rozmiar_pliku - naglowek->dane_przesuniecie;
        }
        uint64_t do_skopiowania_dane = (naglowek->dane_rozmiar < dostepne_dane_w_pliku) ? naglowek->dane_rozmiar : dostepne_dane_w_pliku;
        KopiujPamiec((void*)naglowek->dane_wirtualny, bufor_pliku + naglowek->dane_przesuniecie, do_skopiowania_dane);
    }

    // 6. Utworzenie Stosu Użytkownika (16 KB na wysokim adresie)
    uint64_t wirtualna_baza_stosu = 0x00007FFFF0000000; 
    for (int i = 0; i < 8; i++) { // ZMIENIONO NA 8 stron * 4KB = 32KB stosu
        void* ramka_stosu = ZaalokujRamke();
        ZmapujStrone((void*)(wirtualna_baza_stosu + (i * 4096)), ramka_stosu, flagi_vmm_user);
    }
    uint64_t wirtualny_szczyt_stosu = wirtualna_baza_stosu + 32768 - 8; // ZMIENIONO NA 32768 (32 KB) - 8 dla wyrównania ABI

    // 7. Rejestracja struktury procesu w Jądrze (PZB)
    aktywny_proces.pid = 1; 
    aktywny_proces.poziom_zaufania = bzl_poziom;
    aktywny_proces.uprawnienia = flagi_praw;
    aktywny_proces.przestrzen_adresowa = PobierzAktualnePML4();

    wypisz_log("[LOADER] Program zaladowany. Zastosowano zabezpieczenia PZB. Przejscie do Ring 3...");

    // 8. Ostateczny Skok
    przejdz_do_ring3(naglowek->punkt_wejscia, wirtualny_szczyt_stosu, z_syscalla);

    return true;
}