/*
 * Mechanizm: Menedżer Pamięci Fizycznej (Physical Memory Manager - PMM)
 * Opis: Implementuje tzw. Bitmap Allocator. Tworzy tablicę z bitami, gdzie
 * każdy bit oznacza 4 KB przydziału RAM (1 = Wolne, 0 = Zajęte). Odczytuje mapę
 * podaną przez Multiboot2 by dowiedzieć się, ile RAMu posiada komputer i które
 * pola nie należą pod obszar jądra/urządzeń.
 */

#include "pamiec.h"
#include <stddef.h>

// Uproszczony model - rezerwacja z góry 32 MB mapy by pomieścić aż do 1 Terabajta RAM.
// By skrócić binarkę dla deweloperki ograniczmy obsługę do 4GB max (131072 bajty mapy).
#define MAX_RAMEK (1048576) // (4GB / 4KB)
uint8_t mapa_bitowa[MAX_RAMEK / 8];
uint64_t najwyzsza_znaleziona_ramka = 0;

/*
 * Operacje bitowe
 */
void OdblokujRamke(uint64_t adres_fizyczny) {
    uint64_t numer_ramki = adres_fizyczny / ROZMIAR_RAMKI;
    if (numer_ramki >= MAX_RAMEK) return;

    uint64_t indeks_bajtu = numer_ramki / 8;
    uint8_t  indeks_bitu  = numer_ramki % 8;
    // 1 oznacza WOLNE, by ZaalokujRamke łatwiej znalazło "1"
    mapa_bitowa[indeks_bajtu] |= (1 << indeks_bitu); 
}

void ZabezpieczRamke(uint64_t adres_fizyczny) {
    uint64_t numer_ramki = adres_fizyczny / ROZMIAR_RAMKI;
    if (numer_ramki >= MAX_RAMEK) return;

    uint64_t indeks_bajtu = numer_ramki / 8;
    uint8_t  indeks_bitu  = numer_ramki % 8;
    // 0 oznacza ZAJĘTE/ZABLOKOWANE
    mapa_bitowa[indeks_bajtu] &= ~(1 << indeks_bitu); 
}

bool CzyRamkaWolna(uint64_t adres_fizyczny) {
    uint64_t numer_ramki = adres_fizyczny / ROZMIAR_RAMKI;
    if (numer_ramki >= MAX_RAMEK) return false;

    uint64_t indeks_bajtu = numer_ramki / 8;
    uint8_t  indeks_bitu  = numer_ramki % 8;
    return (mapa_bitowa[indeks_bajtu] & (1 << indeks_bitu)) != 0;
}

// Inicjalizacja polegająca na zablokowaniu całego przedziału, 
// a potem iteracyjnym odblokowywaniu wg spisu inwentarza w BIOS/GRUB.
void InicjalizujPMM(uint64_t adres_info_multiboot) {
    // 1. Zablokuj całkowicie dostępny RAM (Wypełnienie zerami = zajęte)
    for (uint64_t i = 0; i < (MAX_RAMEK / 8); i++) {
        mapa_bitowa[i] = 0;
    }

    // Jeśli bootloader z jakiegoś powodu pominął przekazanie struktury
    if (adres_info_multiboot == 0) return;

    // Nagłówek zaczyna się łącznym bitem rozmiaru bloku informacyjnego
    uint32_t calkowity_rozmiar = *(uint32_t*)adres_info_multiboot;
    uint64_t aktualny_adres = adres_info_multiboot + 8; // Omiń stałe bloki wymiarowe

    TagMapyPamieciMB2* mapa_mmap = nullptr;

    // Przeszukiwanie tagów (Są poukładane pod rząd dopóki tag=0, rozmiar=8)
    while (aktualny_adres < adres_info_multiboot + calkowity_rozmiar) {
        WpisTaguMB2* tag = (WpisTaguMB2*)aktualny_adres;

        if (tag->typ == 0) break; // Koniec tagów

        if (tag->typ == MULTIBOOT_TAG_TYPE_MEMORY_MAP) {
            mapa_mmap = (TagMapyPamieciMB2*)tag;
            break;
        }

        // Tag padding - wyrównany do 8 bajtów (rozmiar zaokrąglony w górę)
        aktualny_adres += (tag->rozmiar + 7) & ~7;
    }

    if (mapa_mmap != nullptr) {
        // Oblicz ilość wpisów na podstawie długości zwróconego tagu
        uint32_t liczba_wpisow = (mapa_mmap->rozmiar - sizeof(TagMapyPamieciMB2)) / mapa_mmap->rozmiar_wpisu;

        for (uint32_t i = 0; i < liczba_wpisow; i++) {
            // POPRAWKA ZGODNOŚCIOWA: Iterowanie po surowych bajtach uwzględniając `rozmiar_wpisu` od GRUB-a.
            WpisMapyPamieciMB2* wezel = (WpisMapyPamieciMB2*)((uint8_t*)mapa_mmap->wpisy + (i * mapa_mmap->rozmiar_wpisu));
            
            // Typ 1 = Wolny, dostępny RAM. Pozostałe (ACPI itp.) ignorujemy.
            if (wezel->typ_obszaru == 1) {
                // Odbezpiecz iteracyjnie co stronę w obszarze
                for (uint64_t adres = wezel->adres_bazowy; adres < (wezel->adres_bazowy + wezel->dlugosc); adres += ROZMIAR_RAMKI) {
                    OdblokujRamke(adres);
                }
                
                // POPRAWKA WYDAJNOŚCIOWA: Aktualizuj szczyt pamięci tylko raz dla sekcji (zamiast dla każdej ramki osobno)
                uint64_t koncowa_ramka = (wezel->adres_bazowy + wezel->dlugosc) / ROZMIAR_RAMKI;
                if (koncowa_ramka > najwyzsza_znaleziona_ramka) {
                    najwyzsza_znaleziona_ramka = koncowa_ramka;
                }
            }
        }
    }

    // 2. Po odblokowaniu "wolnego" RAMu od BIOSu z powrotem musimy zablokować to co faktycznie zajmujemy my!
    
    // Blokujemy pierwszy megabajt (Adresy 0x0 - 0x100000) by ochronić bufor VGA (0xB8000), BIOS, tryb Real Mode etc.
    for (uint64_t a = 0; a < 0x100000; a += ROZMIAR_RAMKI) ZabezpieczRamke(a);

    // Blokujemy przestrzeń fizyczną naszego programu Jądra posiłkując się etykietami Linkera
    uint64_t jadro_start = (uint64_t)&__kernel_start;
    uint64_t jadro_koniec = (uint64_t)&__kernel_end;
    for (uint64_t a = jadro_start; a < jadro_koniec; a += ROZMIAR_RAMKI) ZabezpieczRamke(a);

    // POPRAWKA BEZPIECZEŃSTWA: Zabezpiecz całą strukturę Multiboot2 w pamięci.
    // Struktura ta potrafi być duża (często >4KB), więc pętla chroni nas przed utratą tych danych!
    for (uint64_t a = adres_info_multiboot; a < adres_info_multiboot + calkowity_rozmiar; a += ROZMIAR_RAMKI) {
        ZabezpieczRamke(a);
    }

    // Blokujemy obszar APIC MMIO 0xFEE00000 (Zaraz pod granicą 4GB)
    ZabezpieczRamke(0xFEE00000);
    ZabezpieczRamke(0xFEC00000);
}

// Wydziel z puli
void* ZaalokujRamke() {
    // Proste szukanie sekwencyjne (tzw. First-Fit) pierwszego bitu == 1
    for (uint64_t i = 0; i <= najwyzsza_znaleziona_ramka; i++) {
        uint64_t indeks_bajtu = i / 8;
        uint8_t  indeks_bitu  = i % 8;

        if (mapa_bitowa[indeks_bajtu] & (1 << indeks_bitu)) {
            // Znaleźliśmy 1. Ustaw na 0 = Zajęte
            mapa_bitowa[indeks_bajtu] &= ~(1 << indeks_bitu);
            return (void*)(i * ROZMIAR_RAMKI);
        }
    }
    return nullptr; // Out of Memory (OOM)
}

// Przekaż do puli
void ZwolnijRamke(void* adres_fizyczny) {
    OdblokujRamke((uint64_t)adres_fizyczny);
}