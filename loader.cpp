#include "loader.h"
#include "grafika.h" // Używamy Składacza Obrazu do pięknych logów!
#include "pamiec.h"  // Zapewnia dostęp do ZaalokujRamke() i ZmapujStrone() bez błędów linkera!

// Oczekiwana funkcja z vmm.cpp zwracająca wskaźnik na drzewo stron (dodana na końcu vmm.cpp)
extern "C" void* PobierzAktualnePML4();

// Oczekiwana funkcja z Asemblera (ring3.S), która wykonuje instrukcję IRETQ
// UWAGA: Nazwa musi dokładnie pasować do wyeksportowanego symbolu w asemblerze!
extern "C" void przejdz_do_ring3(uint64_t punkt_wejscia, uint64_t wirtualny_stos);

// Oczekiwana funkcja z Twojego Bursztynowego Systemu Plików (BSP)
extern "C" uint8_t* bsp_wczytaj_plik_do_pamieci(const char* sciezka, uint64_t* rozmiar_wyj);

// ---------------------------------------------------------
// Własne funkcje pomocnicze do pamięci (brak <string.h>)
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// GŁÓWNY SYSTEM ŁADOWANIA APLIKACJI .BUR
// ---------------------------------------------------------
extern "C" bool bws_uruchom_program_z_pliku(const char* sciezka_pliku, uint8_t bzl_poziom, uint64_t flagi_praw) {
    WypiszLog("[LOADER] Proba uruchomienia programu...");

    // 1. Wczytanie pliku z BSP
    uint64_t rozmiar_pliku = 0;
    uint8_t* bufor_pliku = bsp_wczytaj_plik_do_pamieci(sciezka_pliku, &rozmiar_pliku);
    
    if (!bufor_pliku || rozmiar_pliku < sizeof(NaglowekBur)) {
        WypiszLog("[LOADER-BLAD] Nie znaleziono pliku lub plik jest za maly!");
        return false;
    }

    // 2. Mapowanie nagłówka z pamięci
    NaglowekBur* naglowek = (NaglowekBur*)bufor_pliku;

    // 3. Weryfikacja Sygnatury (Magii)
    const uint8_t oczekiwana_magia[4] = {'B', 'U', 'R', '\0'};
    if (!PorownajPamiec(naglowek->magia, oczekiwana_magia, 4)) {
        WypiszLog("[LOADER-BLAD] Nieprawidlowy format pliku! To nie jest program .bur");
        return false;
    }

    WypiszLog("[LOADER] Sygnatura BUR poprawna. Alokacja pamieci uzytkownika...");

    // Obliczamy flagi VMM. FLAGA_USER (bit 2) jest KRYTYCZNA dla Ring 3!
    uint64_t flagi_vmm_user = FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_USER;

    // 4. Mapowanie sekcji .tekst (KODU)
    for (uint64_t offset = 0; offset < naglowek->tekst_rozmiar; offset += 4096) {
        void* ramka = ZaalokujRamke();
        void* adres_wirtualny = (void*)(naglowek->tekst_wirtualny + offset);
        ZmapujStrone(adres_wirtualny, ramka, flagi_vmm_user);
    }
    
    // Kopiujemy kod z bufora pod adres wirtualny
    KopiujPamiec((void*)naglowek->tekst_wirtualny, bufor_pliku + naglowek->tekst_przesuniecie, naglowek->tekst_rozmiar);

    // 5. Mapowanie sekcji .dane (ZMIENNE)
    if (naglowek->dane_rozmiar > 0) {
        for (uint64_t offset = 0; offset < naglowek->dane_rozmiar; offset += 4096) {
            void* ramka = ZaalokujRamke();
            void* adres_wirtualny = (void*)(naglowek->dane_wirtualny + offset);
            ZmapujStrone(adres_wirtualny, ramka, flagi_vmm_user);
        }
        KopiujPamiec((void*)naglowek->dane_wirtualny, bufor_pliku + naglowek->dane_przesuniecie, naglowek->dane_rozmiar);
    }

    // 6. Utworzenie Stosu Użytkownika (np. 16 KB na wysokim adresie)
    uint64_t wirtualna_baza_stosu = 0x00007FFFF0000000; 
    for (int i = 0; i < 4; i++) { // 4 strony * 4KB = 16KB stosu
        void* ramka_stosu = ZaalokujRamke();
        ZmapujStrone((void*)(wirtualna_baza_stosu + (i * 4096)), ramka_stosu, flagi_vmm_user);
    }
    uint64_t wirtualny_szczyt_stosu = wirtualna_baza_stosu + 16384; // Stos rośnie w dół na x86

    // 7. Utworzenie struktury procesu
    proces_t nowy_proces;
    nowy_proces.pid = 1; 
    nowy_proces.poziom_zaufania = bzl_poziom;
    nowy_proces.uprawnienia = flagi_praw;
    nowy_proces.przestrzen_adresowa = PobierzAktualnePML4();
    
    // Zapobiega ostrzeżeniu kompilatora ([-Wunused-but-set-variable]) dopóki nie dodasz Menedżera Procesów
    (void)nowy_proces; 

    WypiszLog("[LOADER] Program zaladowany. Przejscie do Ring 3...");

    // 8. Ostateczny Skok!
    przejdz_do_ring3(naglowek->punkt_wejscia, wirtualny_szczyt_stosu);

    return true;
}