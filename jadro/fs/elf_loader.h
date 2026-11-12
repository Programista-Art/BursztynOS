#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * Bursztyn OS - loader ELF64.
 *
 * Powod istnienia: format .bur jest natywny i pozostaje domyslny, ale zaden
 * zewnetrzny kompilator (gcc, clang, musl, Chromium) nie potrafi go
 * wygenerowac. Bez loadera ELF64 nie da sie uruchomic zadnego obcego
 * binarium, a wiec port Chromium jest niemozliwy.
 *
 * Ten modul jest swiadomie MALY i walidujacy, nie zastepuje loader.cpp:
 *
 *   - rozpoznaje wylacznie ELF64, little-endian, x86-64,
 *   - obsluguje PT_LOAD i PT_GNU_STACK (NX!), reszte odrzuca albo ignoruje,
 *   - zachowuje W^X: segment z PF_X jest RX, segment z PF_W jest RW+NX,
 *     segment z PF_X i PF_W jednoczesnie jest ODRZUCANY,
 *   - BSS jest zerowany (p_memsz > p_filesz),
 *   - nie implementuje linkowania dynamicznego (PT_INTERP/PT_DYNAMIC);
 *     binaria musza byc statyczne. To stan przejsciowy, nie docelowy.
 *
 * Walidacja jest kompletna PRZED pierwszym mapowaniem, bo loader dziala
 * w Ring 0 z danymi z dysku i bledna decyzja oznacza natychmiastowe
 * przejecie systemu.
 */

/* =========================================================================
 * NAGLOWEK ELF64 (tylko pola, ktorych uzywamy)
 * ========================================================================= */

struct Elf64_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};
static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr ABI");

struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};
static_assert(sizeof(Elf64_Phdr) == 56, "Elf64_Phdr ABI");

constexpr uint8_t ELF_MAGIA[4] = {0x7F, 'E', 'L', 'F'};
constexpr uint8_t ELFCLASS64 = 2;
constexpr uint8_t ELFDATA2LSB = 1;
constexpr uint8_t EV_CURRENT = 1;
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t ET_DYN = 3;
constexpr uint16_t EM_X86_64 = 62;
constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PT_DYNAMIC = 2;
constexpr uint32_t PT_INTERP = 3;
constexpr uint32_t PT_GNU_STACK = 0x6474E551U;
constexpr uint32_t PF_X = 1U;
constexpr uint32_t PF_W = 2U;
constexpr uint32_t PF_R = 4U;

/* =========================================================================
 * WYNIK WALIDACJI
 * ========================================================================= */

enum ElfStatus : int32_t {
    ELF_OK = 0,
    ELF_ZA_DUZY = 1,
    ELF_ZLA_MAGIA = 2,
    ELF_ZLA_KLASA = 3,
    ELF_ZLY_ENDIAN = 4,
    ELF_ZLA_WERSJA = 5,
    ELF_ZLY_TYP = 6,
    ELF_ZLA_ARCHITEKTURA = 7,
    ELF_ZLY_NAGLOWEK_PROGRAMU = 8,
    ELF_ZLY_SEGMENT = 9,
    ELF_SEGMENTY_NACHODZA = 10,
    ELF_WEJSCIE_POZA_KODEM = 11,
    ELF_WX_NARUSZONE = 12,
    ELF_LINKOWANIE_DYNAMICZNE = 13,
    ELF_ZLY_ROZMIAR = 14
};

const char* elf_status_opis(ElfStatus status);

/*
 * Pelna walidacja naglowka i tablicy programow.
 *
 * `obraz` to bufor z zawartoscia pliku (juz w pamieci jadra), `rozmiar`
 * to jego dlugosc. Funkcja nie dotyka niczego poza tym buforem i nie
 * alokuje pamieci.
 */
ElfStatus elf_waliduj(const uint8_t* obraz, size_t rozmiar);

/* =========================================================================
 * OPIS LADOWANIA
 * ========================================================================= */

/* Maksymalna liczba segmentow PT_LOAD, ktore przyjmujemy. */
constexpr uint32_t ELF_MAKS_SEGMENTOW = 8;

struct ElfSegment {
    uint64_t wirtualny;      /* adres docelowy w Ring 3 */
    uint64_t rozmiar_pliku;  /* p_filesz */
    uint64_t rozmiar_pamieci;/* p_memsz (>= p_filesz, reszta to BSS) */
    uint64_t przesuniecie;   /* p_offset w pliku */
    uint32_t flagi;          /* PF_* po walidacji */
    uint32_t wykonywalny;    /* 0/1 - uint32 zamiast bool dla stabilnego ABI */
    uint32_t zapisywalny;    /* 0/1 */
    uint32_t zarezerwowane;  /* wyrownanie do 48 B, zawsze 0 */
};
static_assert(sizeof(ElfSegment) == 48, "ElfSegment ABI");

struct ElfObraz {
    uint64_t wejscie;        /* e_entry */
    uint32_t liczba_segmentow;
    ElfSegment segmenty[ELF_MAKS_SEGMENTOW];
    uint64_t dolny_adres;    /* najnizszy p_vaddr */
    uint64_t gorny_adres;    /* najwyzszy p_vaddr + p_memsz */
    bool     wymaga_nx_stosu;/* PT_GNU_STACK bez PF_X */
    uint32_t suma_flag;      /* OR flag wszystkich segmentow */
};

/*
 * Wypelnia ElfObraz po udanej walidacji. Zwraca ELF_OK albo blad.
 * Wywoluj dopiero po elf_waliduj() == ELF_OK; funkcja i tak waliduje
 * ponownie, bo taniej jest sprawdzic dwa razy niz mapowac zly segment.
 */
ElfStatus elf_opisz(const uint8_t* obraz, size_t rozmiar, ElfObraz* wynik);

/* Czy adres wejscia lezy w segmencie wykonywalnym. */
bool elf_wejscie_w_kodzie(const ElfObraz& obraz);

/* Czy dowolny segment narusza W^X (PF_X i PF_W jednoczesnie). */
bool elf_narusza_wx(const ElfObraz& obraz);
