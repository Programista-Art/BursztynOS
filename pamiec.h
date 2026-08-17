/*
 * Bursztyn OS - wspolne definicje PMM, VMM i Multiboot2.
 *
 * Ten naglowek opisuje:
 *  - symbole granic obrazu jadra dostarczane przez linker.ld,
 *  - format tagow informacji Multiboot2,
 *  - format wpisow mapy pamieci,
 *  - wspolne stale stronicowania x86_64,
 *  - publiczne API Physical Memory Managera i Virtual Memory Managera.
 *
 * WAZNE:
 * Tagi w strukturze informacji Multiboot2 NIE maja formatu
 * uint16_t type + uint16_t flags. Taki uklad wystepuje w naglowku
 * Multiboot2 zapisanym w obrazie jadra. Tagi przekazane przez GRUB
 * do jadra maja:
 *
 *     uint32_t type;
 *     uint32_t size;
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * 1. ARCHITEKTURA I PODSTAWOWE STALE PAMIECI
 * ========================================================================= */

static_assert(
    sizeof(void*) == 8,
    "Bursztyn OS wymaga architektury x86_64"
);

static_assert(
    sizeof(uintptr_t) == 8,
    "uintptr_t musi miec 64 bity"
);

/*
 * Zachowujemy dotychczasowa nazwe makra, poniewaz korzystaja z niej
 * istniejace moduly PMM/VMM.
 */
#define ROZMIAR_RAMKI 4096ULL

inline constexpr uint64_t ROZMIAR_RAMKI_4K =
    static_cast<uint64_t>(ROZMIAR_RAMKI);

inline constexpr uint64_t MASKA_OFFSETU_STRONY_4K =
    ROZMIAR_RAMKI_4K - 1ULL;

static_assert(
    (ROZMIAR_RAMKI_4K &
     (ROZMIAR_RAMKI_4K - 1ULL)) == 0,
    "Rozmiar ramki musi byc potega dwojki"
);

/* =========================================================================
 * 2. SYMBOLE LINKERA
 * ========================================================================= */

/*
 * __kernel_start i __kernel_end sa symbolami adresowymi utworzonymi przez
 * linker.ld, a nie zwyklymi zmiennymi uint64_t.
 *
 * Poprawne pobranie adresu:
 *
 *   uintptr_t start =
 *       reinterpret_cast<uintptr_t>(__kernel_start);
 *
 * Nie nalezy odczytywac *__kernel_start jako danych.
 */
extern "C" uint8_t __kernel_start[];
extern "C" uint8_t __kernel_end[];

/*
 * Dodatkowe symbole eksportowane przez poprawiony linker.ld.
 * Sa przydatne PMM/VMM do diagnostyki i ochrony konkretnych sekcji.
 */
extern "C" uint8_t __kernel_phys_start[];
extern "C" uint8_t __kernel_phys_end[];

extern "C" uint8_t __text_start[];
extern "C" uint8_t __text_end[];

extern "C" uint8_t __rodata_start[];
extern "C" uint8_t __rodata_end[];

extern "C" uint8_t __data_start[];
extern "C" uint8_t __data_end[];

extern "C" uint8_t __bss_start[];
extern "C" uint8_t __bss_end[];

/* =========================================================================
 * 3. MULTIBOOT2 - TYPY TAGOW INFORMACJI
 * ========================================================================= */

/*
 * To sa typy tagow STRUKTURY INFORMACJI przekazywanej przez bootloader.
 */
#define MULTIBOOT_TAG_TYPE_END          0U
#define MULTIBOOT_TAG_TYPE_MEMORY_MAP   6U
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER  8U
#define MULTIBOOT_TAG_TYPE_EFI32        11U
#define MULTIBOOT_TAG_TYPE_EFI64        12U
#define MULTIBOOT_TAG_TYPE_ACPI_OLD     14U
#define MULTIBOOT_TAG_TYPE_ACPI_NEW     15U

/*
 * Standard Multiboot2 wymaga wyrownania kazdego tagu informacji do 8 bajtow.
 */
inline constexpr uint64_t MULTIBOOT2_WYROWNANIE_TAGU =
    8ULL;

inline constexpr uint64_t MULTIBOOT2_MASKA_WYROWNANIA =
    MULTIBOOT2_WYROWNANIE_TAGU - 1ULL;

/* =========================================================================
 * 4. WSPOLNY NAGLOWEK TAGU MULTIBOOT2
 * ========================================================================= */

/*
 * Poprawny format tagu informacji Multiboot2:
 *
 * offset 0: uint32_t type
 * offset 4: uint32_t size
 */
struct WpisTaguMB2 {
    uint32_t typ;
    uint32_t rozmiar;
} __attribute__((packed));

static_assert(
    sizeof(WpisTaguMB2) == 8,
    "Naglowek tagu informacji Multiboot2 musi miec 8 bajtow"
);

static_assert(
    offsetof(WpisTaguMB2, typ) == 0,
    "Nieprawidlowy offset pola typ"
);

static_assert(
    offsetof(WpisTaguMB2, rozmiar) == 4,
    "Nieprawidlowy offset pola rozmiar"
);

/* =========================================================================
 * 5. MULTIBOOT2 - MAPA PAMIECI
 * ========================================================================= */

#define MULTIBOOT_MEMORY_AVAILABLE         1U
#define MULTIBOOT_MEMORY_RESERVED          2U
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE  3U
#define MULTIBOOT_MEMORY_NVS               4U
#define MULTIBOOT_MEMORY_BADRAM            5U

/*
 * Jeden wpis mapy pamieci Multiboot2.
 */
struct WpisMapyPamieciMB2 {
    uint64_t adres_bazowy;
    uint64_t dlugosc;
    uint32_t typ_obszaru;
    uint32_t zarezerwowane;
} __attribute__((packed));

static_assert(
    sizeof(WpisMapyPamieciMB2) == 24,
    "Wpis mapy pamieci Multiboot2 musi miec 24 bajty"
);

static_assert(
    offsetof(WpisMapyPamieciMB2, adres_bazowy) == 0,
    "Nieprawidlowy offset adresu bazowego mapy pamieci"
);

static_assert(
    offsetof(WpisMapyPamieciMB2, dlugosc) == 8,
    "Nieprawidlowy offset dlugosci mapy pamieci"
);

static_assert(
    offsetof(WpisMapyPamieciMB2, typ_obszaru) == 16,
    "Nieprawidlowy offset typu obszaru mapy pamieci"
);

/*
 * Naglowek tagu mapy pamieci.
 *
 * Pole wpisy[0] jest swiadomym rozszerzeniem GNU C++ zachowanym dla
 * kompatybilnosci z obecnym kodem pamiec.cpp:
 *
 *     tag->wpisy[i]
 *
 * Nie jest ono fizycznym dodatkowym polem struktury. Pierwszy wpis zaczyna
 * sie dokladnie pod offsetem 16.
 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

struct TagMapyPamieciMB2 {
    uint32_t typ;
    uint32_t rozmiar;

    uint32_t rozmiar_wpisu;
    uint32_t wersja_wpisu;

    WpisMapyPamieciMB2 wpisy[0];
} __attribute__((packed));

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static_assert(
    sizeof(TagMapyPamieciMB2) == 16,
    "Naglowek tagu mapy pamieci musi miec 16 bajtow"
);

static_assert(
    offsetof(TagMapyPamieciMB2, wpisy) == 16,
    "Pierwszy wpis mapy pamieci musi zaczynac sie pod offsetem 16"
);

/* =========================================================================
 * 6. MULTIBOOT2 - FRAMEBUFFER
 * ========================================================================= */

/*
 * Wspolna czesc tagu framebuffer.
 *
 * Po tym naglowku standard Multiboot2 moze umiescic dodatkowe pola
 * zalezne od typ_bufora. Obecny renderer Bursztyn OS korzysta z danych
 * wspolnych: adres, pitch, szerokosc, wysokosc i bpp.
 */
struct TagFramebufferMB2 {
    uint32_t typ;
    uint32_t rozmiar;

    uint64_t adres_fizyczny;

    uint32_t pitch;
    uint32_t szerokosc;
    uint32_t wysokosc;

    uint8_t bpp;
    uint8_t typ_bufora;
    uint16_t zarezerwowane;
} __attribute__((packed));

static_assert(
    sizeof(TagFramebufferMB2) == 32,
    "Wspolna czesc tagu framebuffer Multiboot2 musi miec 32 bajty"
);

static_assert(
    offsetof(TagFramebufferMB2, adres_fizyczny) == 8,
    "Nieprawidlowy offset adresu framebufferu"
);

static_assert(
    offsetof(TagFramebufferMB2, pitch) == 16,
    "Nieprawidlowy offset pitch framebufferu"
);

static_assert(
    offsetof(TagFramebufferMB2, szerokosc) == 20,
    "Nieprawidlowy offset szerokosci framebufferu"
);

static_assert(
    offsetof(TagFramebufferMB2, wysokosc) == 24,
    "Nieprawidlowy offset wysokosci framebufferu"
);

static_assert(
    offsetof(TagFramebufferMB2, bpp) == 28,
    "Nieprawidlowy offset BPP framebufferu"
);

/*
 * Wartosci pola typ_bufora wedlug Multiboot2.
 */
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED  0U
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB      1U
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2U

/* =========================================================================
 * 7. FLAGI PTE x86_64
 * ========================================================================= */

/*
 * Nazwy sa prefiksowane VMM_, aby nie kolidowaly z FLAGA_OBECNA /
 * FLAGA_ZAPIS / FLAGA_USER z loader.h.
 */
inline constexpr uint32_t VMM_FLAGA_OBECNA =
    1U << 0;

inline constexpr uint32_t VMM_FLAGA_ZAPIS =
    1U << 1;

inline constexpr uint32_t VMM_FLAGA_PRESENT =
    1U << 0;

inline constexpr uint32_t VMM_FLAGA_USER =
    1U << 2;

inline constexpr uint32_t VMM_FLAGA_PWT =
    1U << 3;

inline constexpr uint32_t VMM_FLAGA_PCD =
    1U << 4;

/*
 * Execute Disable znajduje sie w bicie 63 wpisu PTE i dlatego nie miesci sie
 * w obecnym 32-bitowym parametrze flag funkcji ZmapujStrone().
 *
 * Gdy VMM zostanie rozszerzony o NX, typ argumentu flag powinien zostac
 * zmieniony na uint64_t w pamiec.h i pamiec.cpp jednoczesnie.
 */
inline constexpr uint64_t VMM_FLAGA_NX =
    1ULL << 63;

/* =========================================================================
 * 8. API PHYSICAL MEMORY MANAGERA
 * ========================================================================= */

/*
 * Najwyzszy numer/indeks ramki odnaleziony przez PMM.
 * Zachowujemy istniejacy typ i semantyke implementacji.
 */
extern uint64_t najwyzsza_znaleziona_ramka;

/*
 * Oznacza ramke zawierajaca podany adres fizyczny jako zajeta.
 */
void ZabezpieczRamke(uint64_t adres_fizyczny);

/*
 * Oznacza ramke zawierajaca podany adres fizyczny jako dostepna.
 * Wywoluj tylko dla pamieci, ktora rzeczywiscie nalezy do PMM.
 */
void OdblokujRamke(uint64_t adres_fizyczny);

/*
 * Rezerwuje jedna fizyczna ramke 4 KiB.
 *
 * Zwraca:
 *   fizyczny adres poczatku ramki,
 *   nullptr przy braku dostepnej pamieci.
 */
void* ZaalokujRamke();

/*
 * Zwraca ramke do PMM.
 * Wskaznik powinien byc adresem otrzymanym z ZaalokujRamke().
 */
void ZwolnijRamke(void* adres_fizyczny);

/*
 * Buduje stan PMM na podstawie tagu Memory Map z Multiboot2 i zabezpiecza
 * pamiec zajeta przez jadro oraz struktury startowe.
 */
void InicjalizujPMM(uint64_t adres_info_multiboot);

/* =========================================================================
 * 9. API VIRTUAL MEMORY MANAGERA
 * ========================================================================= */

/*
 * Mapuje jedna strone 4 KiB w aktualnej przestrzeni adresowej.
 *
 * Wymagania:
 *   - adres_wirtualny powinien byc wyrownany do 4096,
 *   - adres_fizyczny powinien byc wyrownany do 4096,
 *   - flagi powinny odpowiadac bitom PTE obslugiwanym przez pamiec.cpp.
 *
 * Obecne API zwraca void, wiec kod wywolujacy nie moze jeszcze wykryc
 * bledu mapowania. Docelowo warto zmienic wynik na bool.
 */
void ZmapujStrone(
    void* adres_wirtualny,
    void* adres_fizyczny,
    uint32_t flagi
);

/*
 * Inicjalizuje docelowe tablice stron jadra.
 */
void InicjalizujVMM();

/*
 * Zwraca adres aktualnej tablicy PML4 / wartosc bazowa przestrzeni adresowej
 * w formacie uzywanym przez obecny VMM Bursztyna.
 */
extern "C" void* PobierzAktualnePML4();

/*
 * Tworzy prywatna hierarchie stron dla procesu Ring 3.
 * Wymagane mapowania jadra powinny pozostac dostepne w nowej przestrzeni.
 */
void* UtworzPrzestrzenAdresowaProcesu();

/*
 * Przelacza aktualna przestrzen adresowa na wskazane PML4.
 * Funkcja zmienia kontekst pamieci procesora i powinna byc wywolywana
 * z odpowiednia synchronizacja wzgledem przerwan/schedulera.
 */
void UstawPrzestrzenAdresowa(void* pml4);

/* =========================================================================
 * 10. FUNKCJE POMOCNICZE
 * ========================================================================= */

inline constexpr bool CzyAdresWyrownanyDoStrony(
    uintptr_t adres
) {
    return
        (adres &
         static_cast<uintptr_t>(
             MASKA_OFFSETU_STRONY_4K)) == 0;
}

inline constexpr uintptr_t WyrownajAdresWDolDoStrony(
    uintptr_t adres
) {
    return
        adres &
        ~static_cast<uintptr_t>(
            MASKA_OFFSETU_STRONY_4K);
}

inline constexpr uintptr_t PobierzAdresKernelStart() {
    return reinterpret_cast<uintptr_t>(__kernel_start);
}

inline constexpr uintptr_t PobierzAdresKernelEnd() {
    return reinterpret_cast<uintptr_t>(__kernel_end);
}
