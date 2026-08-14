#include "loader.h"
#include "grafika.h" 
#include "pamiec.h"  

extern "C" void* PobierzAktualnePML4();
extern "C" void przejdz_do_ring3(uint64_t punkt_wejscia, uint64_t wirtualny_stos, bool z_syscalla);
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

    if (naglowek->tekst_rozmiar == 0 ||
        (naglowek->tekst_wirtualny & 0xFFF) != 0 ||
        (naglowek->dane_rozmiar > 0 && (naglowek->dane_wirtualny & 0xFFF) != 0) ||
        naglowek->tekst_przesuniecie > rozmiar_pliku ||
        naglowek->tekst_wirtualny + naglowek->tekst_rozmiar < naglowek->tekst_wirtualny ||
        naglowek->dane_wirtualny + naglowek->dane_rozmiar < naglowek->dane_wirtualny ||
        naglowek->punkt_wejscia < naglowek->tekst_wirtualny ||
        naglowek->punkt_wejscia >= naglowek->tekst_wirtualny + naglowek->tekst_rozmiar) {
        wypisz_log("[LOADER-BLAD] Nieprawidlowy uklad segmentow programu!");
        return false;
    }

    uint32_t flagi_vmm_user = FLAGA_OBECNA | FLAGA_ZAPIS | FLAGA_USER;

    // 1. Alokacja Kodu
    for (uint64_t offset = 0; offset < naglowek->tekst_rozmiar; offset += 4096) {
        void* ramka = ZaalokujRamke();
        if (!ramka) {
            wypisz_log("[LOADER-BLAD] Brak pamieci dla segmentu kodu!");
            return false;
        }
        for (uint64_t i = 0; i < 4096; i++) ((uint8_t*)ramka)[i] = 0;
        void* adres_wirtualny = (void*)(naglowek->tekst_wirtualny + offset);
        ZmapujStrone(adres_wirtualny, ramka, flagi_vmm_user);
    }
    uint64_t dostepny_tekst = rozmiar_pliku - naglowek->tekst_przesuniecie;
    uint64_t kopiowany_tekst = naglowek->tekst_rozmiar < dostepny_tekst
        ? naglowek->tekst_rozmiar : dostepny_tekst;
    KopiujPamiec((void*)naglowek->tekst_wirtualny,
                 bufor_pliku + naglowek->tekst_przesuniecie, kopiowany_tekst);

    // 2. Alokacja Danych
    if (naglowek->dane_rozmiar > 0) {
        for (uint64_t offset = 0; offset < naglowek->dane_rozmiar; offset += 4096) {
            void* ramka = ZaalokujRamke();
            if (!ramka) {
                wypisz_log("[LOADER-BLAD] Brak pamieci dla segmentu danych!");
                return false;
            }
            for (uint64_t i = 0; i < 4096; i++) ((uint8_t*)ramka)[i] = 0;
            void* adres_wirtualny = (void*)(naglowek->dane_wirtualny + offset);
            ZmapujStrone(adres_wirtualny, ramka, flagi_vmm_user);
        }
        if (naglowek->dane_przesuniecie < rozmiar_pliku) {
            uint64_t dostepne_dane = rozmiar_pliku - naglowek->dane_przesuniecie;
            uint64_t kopiowane_dane = naglowek->dane_rozmiar < dostepne_dane
                ? naglowek->dane_rozmiar : dostepne_dane;
            KopiujPamiec((void*)naglowek->dane_wirtualny,
                         bufor_pliku + naglowek->dane_przesuniecie, kopiowane_dane);
        }
    }

    // 3. Utworzenie nowego Stosu dla Aplikacji (16 KB)
    uint64_t wirtualna_baza_stosu = 0x00007FFFF0000000; 
    for (int i = 0; i < 4; i++) { 
        void* ramka_stosu = ZaalokujRamke();
        if (!ramka_stosu) {
            wypisz_log("[LOADER-BLAD] Brak pamieci dla stosu programu!");
            return false;
        }
        for (uint64_t j = 0; j < 4096; j++) ((uint8_t*)ramka_stosu)[j] = 0;
        ZmapujStrone((void*)(wirtualna_baza_stosu + (i * 4096)), ramka_stosu, flagi_vmm_user);
    }
    // KRYTYCZNA POPRAWKA ABI: Stos w x86_64 na starcie musi być przesunięty o 8 bajtów (wyrównanie 16n + 8)!
    uint64_t wirtualny_szczyt_stosu = wirtualna_baza_stosu + 16384 - 8;

    // 4. Rejestracja parametrów w procesie
    aktywny_proces.pid = 1; 
    aktywny_proces.poziom_zaufania = bzl_poziom;
    aktywny_proces.uprawnienia = flagi_praw;
    aktywny_proces.przestrzen_adresowa = PobierzAktualnePML4();

    wypisz_log("[LOADER] Program zaladowany. Zastosowano zabezpieczenia PZB. Przejscie do Ring 3...");

    // ROZWIĄZANIE PROBLEMU: Nieważne czy jesteśmy w Syscallu, dokonujemy 
    // sprzętowego skoku, porzucając kod pożegnalny w "syscall.S".
    // Twoja funkcja "przejdz_do_ring3" wykonuje stosowny IRETQ / SWAPGS.
    przejdz_do_ring3(naglowek->punkt_wejscia, wirtualny_szczyt_stosu, z_syscalla);

    return true; // Kod technicznie nigdy tu nie dotrze.
}
