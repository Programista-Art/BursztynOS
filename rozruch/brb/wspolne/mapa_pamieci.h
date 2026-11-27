/*
 * BRB - walidacja i klasyfikacja migawki mapy pamieci UEFI (Faza 4A), niezalezna od firmware.
 *
 * Wejscie: surowy bufor zwrocony przez GetMemoryMap (UEFI 2.10, 7.2) z krokiem DescriptorSize podanym przez
 * firmware. Pola czytane bajtowo (le32/le64) z przesuniec EFI_MEMORY_DESCRIPTOR: Type 0, PhysicalStart 8,
 * VirtualStart 16, NumberOfPages 24, Attribute 32 - krok nigdy nie jest zakladany jako 40 B.
 *
 * Migawka sluzy do diagnostyki i planowania Fazy 4A. NIE jest ostateczna mapa dla ExitBootServices
 * (Faza 4C pobiera mape ponownie tuz przed wywolaniem). Modul nie zna formatu Multiboot2 (Faza 4B).
 * Klasyfikacja nigdy nie uznaje nieznanego typu za dostepny. Sama migawka NIE daje prawa do zapisu pamieci -
 * wlasnosc stron uzyskuje sie wylacznie przez AllocatePages.
 */
#ifndef BRB_MAPA_PAMIECI_H
#define BRB_MAPA_PAMIECI_H

#include <stddef.h>
#include <stdint.h>

namespace brb {

constexpr uint64_t MAPA_MIN_ROZMIAR_DESKRYPTORA = 40;     // sizeof(EFI_MEMORY_DESCRIPTOR)
constexpr uint64_t MAPA_MAKS_ROZMIAR_DESKRYPTORA = 512;   // wiecej = niewiarygodne (typowo 40/48)
constexpr uint32_t MAPA_WERSJA_DESKRYPTORA = 1;
constexpr uint32_t MAPA_MAKS_WPISOW = 4096;               // OVMF ~100, serwery kilkaset
constexpr uint64_t MAPA_STRONA = 0x1000;

enum class KlasaPamieci : uint8_t {
    Wolna,              // EfiConventionalMemory
    UslugiRozruchowe,   // EfiBootServicesCode/Data (zwalniane dopiero przez ExitBootServices)
    Ladowarka,          // EfiLoaderCode/Data (aplikacje UEFI, w tym BRB)
    UslugiCzasuWykonania, // EfiRuntimeServicesCode/Data, EfiPalCode
    AcpiOdzyskiwalna,   // EfiACPIReclaimMemory
    AcpiNvs,            // EfiACPIMemoryNVS
    Mmio,               // EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace
    Zarezerwowana,      // EfiReservedMemoryType
    Uszkodzona,         // EfiUnusableMemory
    Trwala,             // EfiPersistentMemory
    Nieprzyjeta,        // EfiUnacceptedMemoryType
    Nieznana,           // kazdy inny typ (w tym zakresy OEM/OS) - nigdy nie jest dostepna
};

enum class BladMapy : uint8_t {
    Brak = 0,
    BrakDanych,               // pusty wskaznik przy niezerowym rozmiarze
    ZlyRozmiarDeskryptora,    // < 40, > 512 albo niewielokrotnosc 8
    ZlaWersja,                // DescriptorVersion != 1
    RozmiarNiewielokrotny,    // MemoryMapSize nie jest wielokrotnoscia DescriptorSize
    PustaMapa,                // 0 deskryptorow
    ZaDuzoWpisow,             // > MAPA_MAKS_WPISOW
    NiewyrownanyAdres,        // PhysicalStart niewyrownany do 4 KiB
    ZeroStron,                // NumberOfPages == 0
    PrzepelnienieZakresu,     // PhysicalStart + NumberOfPages * 4 KiB przepelnia 64 bity
    NakladanieWpisow,         // dwa deskryptory opisuja te same strony
};

struct WidokMapy {
    const uint8_t* dane;
    uint64_t rozmiar_deskryptora;
    uint32_t liczba;
};

struct WpisMapy {
    uint32_t typ;
    KlasaPamieci klasa;
    uint64_t poczatek;
    uint64_t koniec;      // wylacznie
    uint64_t strony;
    uint64_t atrybuty;
};

struct StatystykiMapy {
    uint64_t strony[12];  // indeks = KlasaPamieci
    uint64_t najwyzszy_adres;
    uint32_t liczba_powyzej_4gib;
};

// Walidacja calej migawki. Przy sukcesie wynik opisuje poprawny widok (indeksowany wpis()).
BladMapy waliduj_mape(const uint8_t* dane, uint64_t rozmiar_mapy, uint64_t rozmiar_deskryptora, uint32_t wersja,
                      WidokMapy* wynik, StatystykiMapy* statystyki);
// Odczyt wpisu i (i < liczba) z poprawnego widoku.
WpisMapy wpis(const WidokMapy& w, uint32_t i);
KlasaPamieci klasyfikuj_typ(uint32_t typ);
const char* nazwa_klasy(KlasaPamieci k);
const char* opis_bledu_mapy(BladMapy b);

// Wypisuje (przez wywolanie zwrotne) wszystkie wpisy przecinajace [poczatek, koniec) - dowody konfliktu.
// Zwraca liczbe bajtow zakresu NIEopisanych przez zaden wpis (luki w mapie).
uint64_t wpisy_w_zakresie(const WidokMapy& w, uint64_t poczatek, uint64_t koniec,
                          void (*zwrot)(void* kontekst, const WpisMapy& wpis), void* kontekst);

}  // namespace brb

#endif
