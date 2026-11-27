/*
 * BRB - walidacja obrazu ELF64 jadra Bursztyn OS i budowa planu ladowania (Faza 3).
 *
 * Modul niezalezny od firmware: wejscie to bufor bajtow (niezaufany) i polityka; wyjscie to kod bledu
 * albo zwalidowany PlanLadowania - OPIS przyszlych operacji (Faza 3 niczego nie kopiuje pod adresy
 * docelowe). Zrodla: System V ABI gABI, rozdz. 4 "ELF Header", 5 "Program Header" (Program Loading);
 * System V ABI AMD64 psABI (EM_X86_64 = 62); Multiboot2 Specification 2.0, rozdz. 3.1.
 *
 * Zasady: wszystkie liczby czytane bajtowo (le16/le32/le64) - bez rzutowania bufora na struktury i bez
 * zalozen o wyrownaniu; kazda suma/iloczyn/zaokraglenie przez arytmetyka.h; petle ograniczone stalymi;
 * naglowki sekcji i nazwy sekcji NIE sa uzywane (o ladowaniu decyduja wylacznie naglowki programu).
 */
#ifndef BRB_ELF64_H
#define BRB_ELF64_H

#include <stddef.h>
#include <stdint.h>

namespace brb {

constexpr uint64_t ELF_STRONA = 0x1000;
// 64 MiB: ten sam limit, ktory instalator stosuje do modulu installer.kernel (jadro/gui/instalator.cpp:345);
// jadro produkcyjne ma 8,9 MiB (okrojone) / 13,7 MiB (prywatny ELF z symbolami).
constexpr uint64_t ELF_MAKS_ROZMIAR_PLIKU = 64ull * 1024 * 1024;
constexpr uint32_t ELF_MAKS_NAGLOWKOW_PROGRAMU = 16;   // docs/brb/architektura-uefi.md 5.1 (jadro ma 4)
constexpr uint32_t ELF_MAKS_SEGMENTOW = 16;
constexpr uint32_t MB2_OKNO_PLIKU = 32768;            // Multiboot2 3.1: naglowek w pierwszych 32768 B
constexpr uint32_t MB2_MAKS_TAGOW = 64;

enum class BladElf : uint8_t {
    Brak = 0,
    ZaKrotki,                 // plik krotszy niz naglowek ELF (64 B) albo brak danych
    ZaDuzyPlik,               // > ELF_MAKS_ROZMIAR_PLIKU
    ZlaSygnatura,             // e_ident[0..3] != 7F 'E' 'L' 'F'
    NieKlasa64,               // EI_CLASS != ELFCLASS64
    NieLittleEndian,          // EI_DATA != ELFDATA2LSB
    ZlaWersja,                // EI_VERSION / e_version != EV_CURRENT
    NieobslugiwaneAbi,        // EI_OSABI spoza {SYSV, GNU} albo EI_ABIVERSION != 0
    ZlaArchitektura,          // e_machine != EM_X86_64
    NieobslugiwanyTyp,        // e_type != ET_EXEC (np. ET_DYN, ET_REL)
    ZlyRozmiarNaglowka,       // e_ehsize != 64
    ZlyRozmiarWpisuPh,        // e_phentsize != 56
    ZlaLiczbaPh,              // e_phnum == 0, PN_XNUM albo > limit
    TablicaPhPozaPlikiem,     // e_phoff + e_phnum*e_phentsize poza plikiem, nachodzi na naglowek, niewyrownane
    NieobslugiwanySegment,    // PT_DYNAMIC, PT_INTERP, PT_SHLIB, PT_TLS
    ZaDuzoSegmentow,          // > ELF_MAKS_SEGMENTOW PT_LOAD
    ZerowySegment,            // PT_LOAD z p_memsz == 0 (niejednoznaczny)
    PlikWiekszyNizPamiec,     // p_filesz > p_memsz
    SegmentPozaPlikiem,       // p_offset + p_filesz poza plikiem (albo przepelnienie)
    ZleWyrownanie,            // p_align nie jest 0, 1 ani potega dwojki
    ZlaKongruencja,           // p_vaddr mod p_align != p_offset mod p_align
    PrzepelnienieAdresu,      // p_vaddr/p_paddr + p_memsz albo zaokraglenie do strony przepelnia 64 bity
    AdresPozaZakresem,        // poza [min_adres_fizyczny, maks_koniec_fizyczny) polityki
    AdresNietozsamosciowy,    // p_paddr != p_vaddr, gdy polityka wymaga tozsamosci
    Nieposortowane,           // PT_LOAD nie rosna wg p_vaddr (gABI)
    Nakladanie,               // dwa segmenty maja wspolne bajty (fizycznie albo wirtualnie)
    KonfliktStrony,           // dwa segmenty o roznych prawach na wspolnej stronie 4 KiB
    ZapisIWykonanie,          // segment jednoczesnie W i X
    BrakPrawaOdczytu,         // segment bez PF_R
    BrakSegmentow,            // brak PT_LOAD
    WejsciePozaSegmentem,     // e_entry poza kazdym PT_LOAD
    WejscieNiewykonywalne,    // e_entry w segmencie bez PF_X
    WejscieWObszarzeZerowanym,// e_entry w czesci BSS (poza p_filesz)
    WejsciePozaZakresem,      // adres fizyczny wejscia >= maks_wejscie polityki
    PrzekroczonyLimitPamieci, // suma stron > maks_pamiec polityki
    BrakNaglowkaMb2,          // polityka wymaga Multiboot2, brak magic w oknie 32 KiB
    ZlyNaglowekMb2,           // magic jest, ale suma/architektura/dlugosc/tagi niepoprawne
};

// Prawa segmentu (wartosci p_flags z gABI).
constexpr uint32_t ELF_PF_X = 1, ELF_PF_W = 2, ELF_PF_R = 4;

struct PolitykaElf {
    uint64_t min_adres_fizyczny;     // najnizszy dozwolony p_paddr
    uint64_t maks_koniec_fizyczny;   // wylaczny limit konca zakresu stron fizycznych
    uint64_t maks_wejscie;           // wylaczny limit fizycznego adresu wejscia
    uint64_t maks_pamiec;            // limit sumy stron (suma zakresow, bez podwojnego liczenia)
    bool wymagaj_tozsamosci;         // p_paddr == p_vaddr
    bool wymagaj_mb2;                // poprawny naglowek Multiboot2 (architektura i386) w oknie 32 KiB
};

// Kontrakt obecnego jadra (docs/brb/architektura-uefi.md 5.1, audyt-rozruchu.md Z-01..Z-03):
//  - jadro nie jest relokowalne, kod jest konsolidowany pod adresy fizyczne (p_paddr == p_vaddr),
//  - poczatek >= 1 MiB (linker.ld: KERNEL_VMA_START), koniec <= 16 MiB (jadro/mm/pamiec.h: RING3_BASE -
//    powyzej zaczyna sie przestrzen Ring 3; identity map jadra nie moze jej przykrywac),
//  - wejscie _start to kod 32-bitowy (boot.S), wiec adres wejscia < 4 GiB,
//  - naglowek Multiboot2 obowiazkowy, dopoki obowiazuje tryb zgodnosci (protokol-rozruchowy.md 6).
constexpr PolitykaElf POLITYKA_JADRA_BURSZTYN{
    0x100000ull, 0x1000000ull, 0x100000000ull, 0x1000000ull - 0x100000ull, true, true};

// Polityka ogolna (testy, przyszle obrazy): pelne adresy 64-bitowe, bez tozsamosci, bez Multiboot2.
constexpr PolitykaElf POLITYKA_OGOLNA{0, ~0ull, ~0ull, 1024ull * 1024 * 1024, false, false};

struct SegmentPlanu {
    uint32_t indeks_ph;              // indeks w tablicy naglowkow programu
    uint32_t prawa;                  // ELF_PF_R | ELF_PF_W | ELF_PF_X
    uint64_t przesuniecie_pliku;     // zrodlo w pliku
    uint64_t adres_fizyczny;         // cel fizyczny (p_paddr)
    uint64_t adres_wirtualny;        // adres wirtualny (p_vaddr) - informacyjnie
    uint64_t bajty_z_pliku;          // p_filesz
    uint64_t rozmiar_w_pamieci;      // p_memsz
    uint64_t bajty_zerowane;         // p_memsz - p_filesz (BSS)
    uint64_t wyrownanie;             // p_align (0/1 = brak wymagan)
    uint64_t strona_poczatek;        // floor(p_paddr / 4 KiB) * 4 KiB
    uint64_t strona_koniec;          // ceil((p_paddr + p_memsz) / 4 KiB) * 4 KiB (wylacznie)
    uint64_t liczba_stron;
};

struct NaglowekMb2 {
    bool obecny;
    uint64_t przesuniecie_pliku;
    uint32_t dlugosc;
    uint32_t architektura;
    uint32_t liczba_tagow;
    int32_t segment;                 // indeks segmentu planu, ktory obejmuje naglowek w pliku, albo -1
};

struct PlanLadowania {
    uint32_t liczba_segmentow;
    SegmentPlanu segmenty[ELF_MAKS_SEGMENTOW];
    uint64_t rozmiar_pliku;
    uint64_t wejscie_wirtualne;      // e_entry
    uint64_t wejscie_fizyczne;       // p_paddr + (e_entry - p_vaddr) segmentu wejscia
    uint32_t segment_wejscia;
    uint64_t zakres_poczatek;        // najnizsza strona fizyczna
    uint64_t zakres_koniec;          // koniec najwyzszej strony fizycznej (wylacznie)
    uint64_t pamiec_stron;           // suma (bez powtorzen) stron fizycznych w bajtach
    uint64_t bajty_z_pliku;          // suma p_filesz
    uint64_t bajty_zerowane;         // suma BSS
    NaglowekMb2 mb2;
};

struct WynikElf {
    BladElf blad;
    int32_t indeks_ph;               // naglowek programu, ktorego dotyczy blad, albo -1
};

// Pelna walidacja i budowa planu. plan jest zawsze zerowany na wejsciu; przy bledzie nie wolno go uzywac.
WynikElf zbuduj_plan_ladowania(const uint8_t* dane, uint64_t rozmiar, const PolitykaElf& polityka,
                               PlanLadowania* plan);
const char* opis_bledu_elf(BladElf blad);   // ASCII, do logu i komunikatow

}  // namespace brb

#endif
