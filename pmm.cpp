/*
 * Mechanizm: Menedżer Pamięci Fizycznej (Physical Memory Manager - PMM)
 * Opis: Implementuje tzw. Bitmap Allocator z optymalizacją czasu dostępu.
 */

#include "pamiec.h"
#include <stddef.h>

// TWARDE OGRANICZENIE: PMM nigdy nie wyda ramki poza granicą 4000 MB.
// Gwarantuje to, że Jądro zawsze operuje na bezpiecznym, udokumentowanym terenie.
#define MAX_RAMEK (1024000) 
uint8_t mapa_bitowa[MAX_RAMEK / 8];
uint64_t najwyzsza_znaleziona_ramka = 0;

static uint64_t ostatnia_alokacja = 0; 

void OdblokujRamke(uint64_t adres_fizyczny) {
    uint64_t numer_ramki = adres_fizyczny / ROZMIAR_RAMKI;
    if (numer_ramki >= MAX_RAMEK) return; 

    uint64_t indeks_bajtu = numer_ramki / 8;
    uint8_t  indeks_bitu  = numer_ramki % 8;
    mapa_bitowa[indeks_bajtu] |= (1 << indeks_bitu);  
}

void ZabezpieczRamke(uint64_t adres_fizyczny) {
    uint64_t numer_ramki = adres_fizyczny / ROZMIAR_RAMKI;
    if (numer_ramki >= MAX_RAMEK) return; 

    uint64_t indeks_bajtu = numer_ramki / 8;
    uint8_t  indeks_bitu  = numer_ramki % 8;
    mapa_bitowa[indeks_bajtu] &= ~(1 << indeks_bitu);  
}

bool CzyRamkaWolna(uint64_t adres_fizyczny) {
    uint64_t numer_ramki = adres_fizyczny / ROZMIAR_RAMKI;
    if (numer_ramki >= MAX_RAMEK) return false;

    uint64_t indeks_bajtu = numer_ramki / 8;
    uint8_t  indeks_bitu  = numer_ramki % 8;
    return (mapa_bitowa[indeks_bajtu] & (1 << indeks_bitu)) != 0;
}

void InicjalizujPMM(uint64_t adres_info_multiboot) {
    for (uint64_t i = 0; i < (MAX_RAMEK / 8); i++) {
        mapa_bitowa[i] = 0;
    }

    if (adres_info_multiboot == 0) return;

    uint32_t calkowity_rozmiar = *(uint32_t*)adres_info_multiboot;
    uint64_t aktualny_adres = adres_info_multiboot + 8;

    TagMapyPamieciMB2* mapa_mmap = nullptr;

    while (aktualny_adres < adres_info_multiboot + calkowity_rozmiar) {
        WpisTaguMB2* tag = (WpisTaguMB2*)aktualny_adres;
        if (tag->typ == 0) break;
        if (tag->typ == MULTIBOOT_TAG_TYPE_MEMORY_MAP) {
            mapa_mmap = (TagMapyPamieciMB2*)tag;
            break;
        }
        aktualny_adres += (tag->rozmiar + 7) & ~7;
    }

    if (mapa_mmap != nullptr) {
        uint32_t liczba_wpisow = (mapa_mmap->rozmiar - sizeof(TagMapyPamieciMB2)) / mapa_mmap->rozmiar_wpisu;

        for (uint32_t i = 0; i < liczba_wpisow; i++) {
            WpisMapyPamieciMB2* wezel = (WpisMapyPamieciMB2*)((uint8_t*)mapa_mmap->wpisy + (i * mapa_mmap->rozmiar_wpisu));
            
            if (wezel->typ_obszaru == 1) {
                for (uint64_t adres = wezel->adres_bazowy; adres < (wezel->adres_bazowy + wezel->dlugosc); adres += ROZMIAR_RAMKI) {
                    if ((adres / ROZMIAR_RAMKI) >= MAX_RAMEK) break; 
                    OdblokujRamke(adres);
                }
                
                uint64_t koncowa_ramka = (wezel->adres_bazowy + wezel->dlugosc) / ROZMIAR_RAMKI;
                if (koncowa_ramka > MAX_RAMEK) {
                    koncowa_ramka = MAX_RAMEK; 
                }
                if (koncowa_ramka > najwyzsza_znaleziona_ramka) {
                    najwyzsza_znaleziona_ramka = koncowa_ramka;
                }
            }
        }
    }
    
    // Zabezpieczenie (Fallback), gdyby stary BIOS/GRUB przekazał pustą mapę Pamięci
    if (najwyzsza_znaleziona_ramka == 0) {
        najwyzsza_znaleziona_ramka = 131072; // Zakładamy minimum 512 MB
        for (uint64_t a = 0; a < 512 * 1024 * 1024; a += ROZMIAR_RAMKI) OdblokujRamke(a);
    }

    // 2. Zabezpiecz kluczowe obszary systemowe poniżej 1MB
    for (uint64_t a = 0; a < 0x100000; a += ROZMIAR_RAMKI) {
        ZabezpieczRamke(a);
    }

    uint64_t jadro_start = (uint64_t)&__kernel_start;
    uint64_t jadro_koniec = (uint64_t)&__kernel_end;
    for (uint64_t a = jadro_start; a < jadro_koniec; a += ROZMIAR_RAMKI) {
        ZabezpieczRamke(a);
    }

    for (uint64_t a = adres_info_multiboot; a < adres_info_multiboot + calkowity_rozmiar; a += ROZMIAR_RAMKI) {
        ZabezpieczRamke(a);
    }

    ZabezpieczRamke(0xFEE00000);
    ZabezpieczRamke(0xFEC00000);
}

void* ZaalokujRamke() {
    uint64_t limit = najwyzsza_znaleziona_ramka;
    if (limit > MAX_RAMEK) limit = MAX_RAMEK;

    for (uint64_t i = ostatnia_alokacja; i < limit; i++) {
        uint64_t indeks_bajtu = i / 8;
        uint8_t  indeks_bitu  = i % 8;

        if (mapa_bitowa[indeks_bajtu] & (1 << indeks_bitu)) {
            mapa_bitowa[indeks_bajtu] &= ~(1 << indeks_bitu);
            ostatnia_alokacja = i; 
            return (void*)(i * ROZMIAR_RAMKI);
        }
    }
    
    for (uint64_t i = 0; i < ostatnia_alokacja; i++) {
        uint64_t indeks_bajtu = i / 8;
        uint8_t  indeks_bitu  = i % 8;

        if (mapa_bitowa[indeks_bajtu] & (1 << indeks_bitu)) {
            mapa_bitowa[indeks_bajtu] &= ~(1 << indeks_bitu);
            ostatnia_alokacja = i; 
            return (void*)(i * ROZMIAR_RAMKI);
        }
    }
    return nullptr; 
}

void ZwolnijRamke(void* adres_fizyczny) {
    OdblokujRamke((uint64_t)adres_fizyczny);
}