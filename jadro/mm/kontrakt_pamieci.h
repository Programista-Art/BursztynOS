#pragma once
/*
 * Bursztyn OS - wewnetrzny kontrakt pamieci rozruchu, wersja 1 (Faza R1A).
 *
 * CZYM JEST: minimalna, wersjonowana struktura WEWNETRZNA jadra, z ktorej korzystaja menedzery pamieci (PMM, VMM).
 * Opisuje, gdzie fizycznie lezy obraz jadra, pod jakimi stalymi adresami wirtualnymi jest widoczny, ktore zakresy
 * fizyczne naleza do zasobow rozruchu i jak dlugo, oraz gdzie jest mapa pamieci firmware.
 *
 * CZYM NIE JEST: to NIE jest publiczne ABI przekazania sterowania. Docelowy natywny protokol BRBI v1
 * (docs/brb/protokol-rozruchowy.md) i adapter `InformacjeRozruchu` naleza do Fazy 7 i nie sa tu wprowadzane.
 * Kontrakt jest budowany WEWNATRZ jadra przez adapter zrodla rozruchu; dzis jedynym adapterem jest tymczasowy
 * adapter zgodnosci Multiboot2 (zbuduj_z_multiboot2). Struktura nie jest przekazywana przez zaden program
 * rozruchowy i moze sie zmienic razem z jadrem (pole `wersja` chroni przed pomieszaniem wersji w obrebie jadra).
 *
 * Model podkladu (R1A): jeden CIAGLY region fizyczny [jadro_baza_fizyczna, +jadro_rozmiar) zachowujacy wzgledne
 * przesuniecia segmentow ELF wzgledem jadro_baza_wirtualna. Brak przetwarzania relokacji ELF.
 *
 * Wlasnosc: kontrakt jest kopiowany do .bss jadra (pmm.cpp) i zyje do konca pracy jadra. Struktury, na ktore
 * wskazuje (MBI, mapa pamieci), sa zakresami w `zakresy[]` z jawnym czasem zycia.
 *
 * Dostep do pamieci rozruchu przed VMM: flaga FLAGA_TOZSAMOSC_4GIB_PRZED_VMM oznacza, ze aktywne w chwili wejscia
 * tablice stron mapuja tozsamosciowo 0..4 GiB (boot.S w trybie MB2; BRBI 2.2 wymaga tego samego). Tylko wtedy
 * PMM moze czytac MBI/mape przez adres fizyczny uzyty jako wskaznik - i tylko ponizej 4 GiB.
 *
 * Wszystkie funkcje sa czyste (bez stanu globalnego, bez asemblera); dostep do pamieci i tlumaczenie adresow
 * dostarcza wywolujacy - dzieki temu testy hosta nigdy nie dereferencjonuja dowolnych adresow fizycznych.
 */

#include "adresy.h"
#include "obraz_jadra.h"

#include <stdint.h>
#include <stddef.h>

namespace KontraktPamieci {

inline constexpr uint32_t SYGNATURA = 0x4B504D42U;   // "BMPK" (LE): Bursztyn - Mapa Pamieci - Kontrakt
inline constexpr uint16_t WERSJA = 1U;

enum Zrodlo : uint32_t {
    ZRODLO_NIEZNANE = 0,
    ZRODLO_MB2_ZGODNOSC = 1,           // tymczasowy adapter zgodnosci Multiboot2 (boot.S, PA == VA)
    ZRODLO_BRB_NATYWNE_PRZYSZLE = 2,   // zarezerwowane dla R1B/R2/Fazy 7; dzis brak sciezki wejscia w jadrze
};

enum FormatMapy : uint32_t {
    MAPA_BRAK = 0,
    MAPA_MB2_TAG6 = 1,                 // tag Multiboot2 typu 6 (naglowek 16 B + wpisy >= 24 B)
    MAPA_EFI_PRZYSZLA = 2,             // zarezerwowane (deskryptory EFI_MEMORY_DESCRIPTOR) - nieobslugiwane w R1A
};

enum ModelPodkladu : uint32_t {
    PODKLAD_CIAGLY = 1,
};

enum Flaga : uint32_t {
    FLAGA_TOZSAMOSC_4GIB_PRZED_VMM = 1U << 0,
    FLAGA_CR3_W_OBRAZIE = 1U << 1,     // tablice stron rozruchu leza w .bss obrazu (boot.S)
};
inline constexpr uint32_t MASKA_FLAG = FLAGA_TOZSAMOSC_4GIB_PRZED_VMM | FLAGA_CR3_W_OBRAZIE;

enum Rodzaj : uint32_t {
    RODZAJ_OBRAZ_JADRA = 1,            // ciagly podklad obrazu (z BSS)
    RODZAJ_TABLICE_STRON_ROZRUCHU = 2, // tablice stron programu rozruchowego poza obrazem
    RODZAJ_INFORMACJE_ROZRUCHU = 3,    // MBI (MB2) - zawiera tez mape pamieci
    RODZAJ_MODUL = 4,                  // modul rozruchu (np. tapety.pak)
    RODZAJ_FRAMEBUFFER = 5,            // LFB (MMIO) - bez aliasu Direct-Map
    RODZAJ_ODZYSKIWALNY_ROZRUCHU = 6,  // stos/GDT/trampolina programu rozruchowego (przyszle)
};

enum CzasZycia : uint32_t {
    ZYCIE_TRWALE = 1,                  // nigdy nie wraca do alokatora
    ZYCIE_DO_ZWOLNIENIA_PRZEZ_KONSUMENTA = 2, // konsument zwalnia jawnie (dzis: OdblokujZakres po imporcie modulu)
    ZYCIE_DO_PRZELACZENIA_CR3 = 3,     // do zaladowania wlasnych tablic, GDT i stosu (R1A: trzymane trwale)
};

enum FlagaZakresu : uint32_t {
    ZAKRES_BEZ_DIRECT_MAP = 1U << 0,   // nie wolno tworzyc aliasu WB w HHDM (MMIO)
    ZAKRES_DOSTEP_TOZSAMOSCIOWY = 1U << 1, // konsument czyta zakres przez mape tozsamosciowa (dzis: MBI, moduly)
};
inline constexpr uint32_t MASKA_FLAG_ZAKRESU = ZAKRES_BEZ_DIRECT_MAP | ZAKRES_DOSTEP_TOZSAMOSCIOWY;

struct Zakres {
    uint64_t poczatek;
    uint64_t dlugosc;
    uint32_t rodzaj;
    uint32_t czas_zycia;
    uint32_t flagi;
    uint32_t zarezerwowane;
};
static_assert(sizeof(Zakres) == 32, "Zakres kontraktu musi miec 32 bajty");
static_assert(offsetof(Zakres, dlugosc) == 8 && offsetof(Zakres, rodzaj) == 16 && offsetof(Zakres, flagi) == 24,
              "Nieprawidlowy uklad Zakres");
static_assert(sizeof(ObrazJadra::Segment) == 32, "Segment obrazu musi miec 32 bajty");
static_assert(offsetof(ObrazJadra::Segment, rozmiar) == 8 && offsetof(ObrazJadra::Segment, zajete) == 16 &&
              offsetof(ObrazJadra::Segment, uprawnienia) == 24, "Nieprawidlowy uklad Segment");

inline constexpr uint32_t MAKS_SEGMENTOW = ObrazJadra::MAKS_SEGMENTOW;
inline constexpr uint32_t MAKS_ZAKRESOW = 64U;
inline constexpr uint64_t MAKS_ROZMIAR_INFORMACJI = 1024ULL * 1024ULL;   // jak BRB_MAKS_ROZMIAR_INFORMACJI
inline constexpr uint64_t MAKS_ROZMIAR_MODULU_MB2 = 256ULL * 1024ULL * 1024ULL;  // dotychczasowy limit PMM (D-01)
inline constexpr uint64_t LIMIT_DOSTEPU_TOZSAMOSCIOWEGO = 0x100000000ULL;

struct KontraktV1 {
    uint32_t sygnatura;
    uint16_t wersja;
    uint16_t zarezerwowane0;
    uint32_t rozmiar;                  // sizeof(KontraktV1)
    uint32_t zrodlo;                   // Zrodlo
    uint32_t flagi;                    // Flaga
    uint32_t model_podkladu;           // ModelPodkladu
    uint64_t jadro_baza_wirtualna;
    uint64_t jadro_baza_fizyczna;
    uint64_t jadro_rozmiar;            // bajty okna obrazu (wielokrotnosc 4 KiB, z BSS)
    uint64_t cr3_rozruchu;             // PA aktywnego PML4 w chwili wejscia (0 = nieznany)
    uint64_t mapa_adres;               // PA mapy pamieci
    uint64_t mapa_rozmiar;             // bajty (dla MB2: rozmiar calego tagu 6)
    uint32_t mapa_format;              // FormatMapy
    uint32_t mapa_rozmiar_wpisu;
    uint64_t informacje_adres;         // PA struktury informacji rozruchu (MBI)
    uint64_t informacje_rozmiar;
    uint32_t liczba_segmentow;
    uint32_t liczba_zakresow;
    ObrazJadra::Segment segmenty[MAKS_SEGMENTOW];
    Zakres zakresy[MAKS_ZAKRESOW];
};
static_assert(offsetof(KontraktV1, sygnatura) == 0 && offsetof(KontraktV1, wersja) == 4 &&
              offsetof(KontraktV1, rozmiar) == 8 && offsetof(KontraktV1, zrodlo) == 12 &&
              offsetof(KontraktV1, flagi) == 16 && offsetof(KontraktV1, model_podkladu) == 20 &&
              offsetof(KontraktV1, jadro_baza_wirtualna) == 24 && offsetof(KontraktV1, jadro_baza_fizyczna) == 32 &&
              offsetof(KontraktV1, jadro_rozmiar) == 40 && offsetof(KontraktV1, cr3_rozruchu) == 48 &&
              offsetof(KontraktV1, mapa_adres) == 56 && offsetof(KontraktV1, mapa_rozmiar) == 64 &&
              offsetof(KontraktV1, mapa_format) == 72 && offsetof(KontraktV1, mapa_rozmiar_wpisu) == 76 &&
              offsetof(KontraktV1, informacje_adres) == 80 && offsetof(KontraktV1, informacje_rozmiar) == 88 &&
              offsetof(KontraktV1, liczba_segmentow) == 96 && offsetof(KontraktV1, liczba_zakresow) == 100 &&
              offsetof(KontraktV1, segmenty) == 104 && offsetof(KontraktV1, zakresy) == 104 + 32 * 8,
              "Nieprawidlowy uklad KontraktV1");
static_assert(sizeof(KontraktV1) == 104 + 32 * 8 + 32 * 64, "Nieprawidlowy rozmiar KontraktV1");

enum class Blad : uint32_t {
    OK = 0,
    // struktura
    OBCIETY,
    ZLA_SYGNATURA,
    WERSJA_NIEOBSLUGIWANA,
    ROZMIAR_NIEZGODNY,
    POLA_ZAREZERWOWANE,
    ZRODLO_NIEZNANE,
    FLAGI_NIEZNANE,
    MODEL_PODKLADU,
    // obraz
    UKLAD_OBRAZU,                      // szczegoly: ObrazJadra::BladUkladu
    NIEZGODNY_Z_LINKEREM,
    MB2_NIETOZSAMOSCIOWY,
    OBRAZ_NIEOPISANY,                  // brak/duplikat zakresu RODZAJ_OBRAZ_JADRA zgodnego z podkladem
    // mapa i informacje
    MAPA_NIEOBSLUGIWANA,
    MAPA_NIEPOPRAWNA,
    INFORMACJE_NIEPOPRAWNE,
    // zakresy
    ZA_DUZO_ZAKRESOW,
    ZAKRES_NIEPOPRAWNY,
    DUPLIKAT_WLASNOSCI,
    ZASOB_W_CIENIU_OKNA,
    CR3_NIEOPISANY,
    // adapter MB2
    MB2_BRAK_INFORMACJI,
    MB2_TAGI_USZKODZONE,
    MB2_BRAK_MAPY,
    MB2_WIELE_MAP,
    MB2_WIELE_FRAMEBUFFEROW,
    TLUMACZENIE_OBRAZU,
    PODKLAD_NIECIAGLY,
    MB2_TAG21_NIEZGODNY,
    // klasyfikacja wobec mapy pamieci firmware (PMM)
    OBRAZ_NA_ACPI_NVS,
    OBRAZ_NA_ZAREZERWOWANEJ,
    OBRAZ_NA_ACPI_ODZYSKIWALNEJ,
    OBRAZ_NA_USZKODZONEJ,
    OBRAZ_NA_NIEZNANYM_TYPIE,
    OBRAZ_POZA_RAM,
};

inline const char* opis_bledu(Blad b) {
    switch (b) {
    case Blad::OK: return "OK";
    case Blad::OBCIETY: return "kontrakt obciety albo brak danych";
    case Blad::ZLA_SYGNATURA: return "zla sygnatura kontraktu";
    case Blad::WERSJA_NIEOBSLUGIWANA: return "nieobslugiwana wersja kontraktu";
    case Blad::ROZMIAR_NIEZGODNY: return "rozmiar kontraktu niezgodny z wersja";
    case Blad::POLA_ZAREZERWOWANE: return "niezerowe pola zarezerwowane albo nieuzyte wpisy";
    case Blad::ZRODLO_NIEZNANE: return "nieznane zrodlo rozruchu";
    case Blad::FLAGI_NIEZNANE: return "nieznane flagi kontraktu";
    case Blad::MODEL_PODKLADU: return "nieobslugiwany model podkladu fizycznego";
    case Blad::UKLAD_OBRAZU: return "niepoprawny uklad obrazu jadra";
    case Blad::NIEZGODNY_Z_LINKEREM: return "uklad obrazu niezgodny z symbolami linkera";
    case Blad::MB2_NIETOZSAMOSCIOWY: return "zrodlo MB2 wymaga PA == VA (boot.S jest zalezny od polozenia)";
    case Blad::OBRAZ_NIEOPISANY: return "brak dokladnie jednego zakresu obrazu zgodnego z podkladem";
    case Blad::MAPA_NIEOBSLUGIWANA: return "nieobslugiwany format mapy pamieci";
    case Blad::MAPA_NIEPOPRAWNA: return "niepoprawny opis mapy pamieci";
    case Blad::INFORMACJE_NIEPOPRAWNE: return "niepoprawny opis struktury informacji rozruchu";
    case Blad::ZA_DUZO_ZAKRESOW: return "za duzo zakresow rozruchu";
    case Blad::ZAKRES_NIEPOPRAWNY: return "niepoprawny zakres rozruchu";
    case Blad::DUPLIKAT_WLASNOSCI: return "zakresy rozruchu nachodza na siebie (podwojna wlasnosc)";
    case Blad::ZASOB_W_CIENIU_OKNA: return "zasob czytany tozsamosciowo lezy fizycznie w oknie VA obrazu";
    case Blad::CR3_NIEOPISANY: return "tablice stron rozruchu poza opisanymi zakresami";
    case Blad::MB2_BRAK_INFORMACJI: return "brak albo niepoprawny naglowek MBI";
    case Blad::MB2_TAGI_USZKODZONE: return "uszkodzone tagi MBI";
    case Blad::MB2_BRAK_MAPY: return "brak mapy pamieci MB2 (tag 6)";
    case Blad::MB2_WIELE_MAP: return "wiecej niz jedna mapa pamieci MB2";
    case Blad::MB2_WIELE_FRAMEBUFFEROW: return "wiecej niz jeden framebuffer MB2";
    case Blad::TLUMACZENIE_OBRAZU: return "nie mozna przetlumaczyc obrazu przez aktywne tablice stron";
    case Blad::PODKLAD_NIECIAGLY: return "podklad fizyczny obrazu nie jest ciagly";
    case Blad::MB2_TAG21_NIEZGODNY: return "tag MB2 21 (adres bazowy) niezgodny z podkladem obrazu";
    case Blad::OBRAZ_NA_ACPI_NVS: return "obraz jadra lezy na pamieci ACPI NVS";
    case Blad::OBRAZ_NA_ZAREZERWOWANEJ: return "obraz jadra lezy na pamieci zarezerwowanej (firmware/runtime)";
    case Blad::OBRAZ_NA_ACPI_ODZYSKIWALNEJ: return "obraz jadra lezy na tablicach ACPI (reclaimable)";
    case Blad::OBRAZ_NA_USZKODZONEJ: return "obraz jadra lezy na uszkodzonej pamieci";
    case Blad::OBRAZ_NA_NIEZNANYM_TYPIE: return "obraz jadra lezy na pamieci nieznanego typu";
    case Blad::OBRAZ_POZA_RAM: return "obraz jadra lezy poza RAM opisanym w mapie";
    }
    return "nieznany blad kontraktu";
}

/* Oczekiwany uklad z symboli linkera (jadro/arch/linker.ld) - kontrakt nie moze go zmienic. */
struct OczekiwanyObraz {
    uint64_t va_poczatek;
    uint64_t rozmiar;
    uint32_t liczba_segmentow;
    ObrazJadra::Segment segmenty[MAKS_SEGMENTOW];
};

/*
 * Symbole linkera potrzebne do opisu segmentow (VA). Konce *_koniec to konce zawartosci segmentow PT_LOAD
 * (p_vaddr + p_memsz): __boot_end, __text_end, __rodata_end, __kernel_dynamic_end (= __bss_end przy pustych
 * sekcjach strazniczych TLS/dynamic, wymuszonych asercjami linker.ld).
 */
struct SymboleObrazu {
    uint64_t kernel_start;
    uint64_t boot_koniec;
    uint64_t text_start;
    uint64_t text_koniec;
    uint64_t rodata_start;
    uint64_t rodata_koniec;
    uint64_t data_start;
    uint64_t data_koniec;
    uint64_t kernel_end;
};

/* Segmenty z linker.ld: .boot (R), .text (RX), .rodata (R), .data+.bss (RW). Bledne symbole daja segmenty, ktore
 * odrzuci ObrazJadra::zbuduj_uklad() (przepelnienia w odejmowaniu sa celowo zostawione walidatorowi). */
inline OczekiwanyObraz oczekiwany_z_symboli(const SymboleObrazu& s) {
    OczekiwanyObraz o{};
    o.va_poczatek = s.kernel_start;
    o.rozmiar = s.kernel_end >= s.kernel_start ? s.kernel_end - s.kernel_start : 0;
    o.liczba_segmentow = 4;
    o.segmenty[0] = {s.kernel_start, s.text_start - s.kernel_start, s.boot_koniec - s.kernel_start, ObrazJadra::ODCZYT, 0};
    o.segmenty[1] = {s.text_start, s.rodata_start - s.text_start, s.text_koniec - s.text_start,
                     ObrazJadra::ODCZYT | ObrazJadra::WYKONANIE, 0};
    o.segmenty[2] = {s.rodata_start, s.data_start - s.rodata_start, s.rodata_koniec - s.rodata_start, ObrazJadra::ODCZYT, 0};
    o.segmenty[3] = {s.data_start, s.kernel_end - s.data_start, s.data_koniec - s.data_start,
                     ObrazJadra::ODCZYT | ObrazJadra::ZAPIS, 0};
    return o;
}

inline void kopiuj(void* cel, const void* zrodlo, size_t n) {
    uint8_t* d = static_cast<uint8_t*>(cel);
    const uint8_t* s = static_cast<const uint8_t*>(zrodlo);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}

inline bool wszystkie_zera(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) if (b[i]) return false;
    return true;
}

inline bool czas_zycia_dozwolony(uint32_t rodzaj, uint32_t zycie) {
    switch (rodzaj) {
    case RODZAJ_OBRAZ_JADRA:
    case RODZAJ_INFORMACJE_ROZRUCHU:
    case RODZAJ_FRAMEBUFFER: return zycie == ZYCIE_TRWALE;
    case RODZAJ_TABLICE_STRON_ROZRUCHU: return zycie == ZYCIE_TRWALE || zycie == ZYCIE_DO_PRZELACZENIA_CR3;
    case RODZAJ_MODUL: return zycie == ZYCIE_DO_ZWOLNIENIA_PRZEZ_KONSUMENTA;
    case RODZAJ_ODZYSKIWALNY_ROZRUCHU: return zycie == ZYCIE_DO_PRZELACZENIA_CR3;
    default: return false;
    }
}

/*
 * Pelna walidacja kontraktu. `dane`/`dostepne` - bufor z kontraktem (moze byc obciety albo niewyrownany),
 * `oczekiwany` - uklad z linkera (nullptr = bez porownania, tylko testy struktury), `uklad`/`bl_ukladu` - wynik.
 * Kontrakt jest najpierw kopiowany do zmiennej lokalnej (brak zalozen o wyrownaniu bufora).
 */
inline Blad waliduj(const void* dane, size_t dostepne, const OczekiwanyObraz* oczekiwany, ObrazJadra::Uklad* uklad,
                    ObrazJadra::BladUkladu* bl_ukladu = nullptr) {
    if (bl_ukladu) *bl_ukladu = ObrazJadra::BladUkladu::OK;
    if (!dane || dostepne < 12) return Blad::OBCIETY;
    uint32_t sygnatura = 0, rozmiar = 0;
    uint16_t wersja = 0;
    kopiuj(&sygnatura, dane, 4);
    kopiuj(&wersja, static_cast<const uint8_t*>(dane) + 4, 2);
    kopiuj(&rozmiar, static_cast<const uint8_t*>(dane) + 8, 4);
    if (sygnatura != SYGNATURA) return Blad::ZLA_SYGNATURA;
    if (wersja != WERSJA) return Blad::WERSJA_NIEOBSLUGIWANA;
    if (rozmiar != sizeof(KontraktV1)) return Blad::ROZMIAR_NIEZGODNY;
    if (dostepne < rozmiar) return Blad::OBCIETY;
    KontraktV1 k;
    kopiuj(&k, dane, sizeof(k));

    if (k.zarezerwowane0 != 0) return Blad::POLA_ZAREZERWOWANE;
    if (k.zrodlo != ZRODLO_MB2_ZGODNOSC && k.zrodlo != ZRODLO_BRB_NATYWNE_PRZYSZLE) return Blad::ZRODLO_NIEZNANE;
    if ((k.flagi & ~MASKA_FLAG) != 0) return Blad::FLAGI_NIEZNANE;
    if (k.model_podkladu != PODKLAD_CIAGLY) return Blad::MODEL_PODKLADU;
    if (k.liczba_segmentow > MAKS_SEGMENTOW || k.liczba_zakresow > MAKS_ZAKRESOW) return Blad::ZA_DUZO_ZAKRESOW;
    for (uint32_t i = 0; i < MAKS_SEGMENTOW; ++i) {
        if (k.segmenty[i].zarezerwowane != 0) return Blad::POLA_ZAREZERWOWANE;
        if (i >= k.liczba_segmentow && !wszystkie_zera(&k.segmenty[i], sizeof(k.segmenty[i]))) {
            return Blad::POLA_ZAREZERWOWANE;
        }
    }
    for (uint32_t i = 0; i < MAKS_ZAKRESOW; ++i) {
        if (k.zakresy[i].zarezerwowane != 0) return Blad::POLA_ZAREZERWOWANE;
        if (i >= k.liczba_zakresow && !wszystkie_zera(&k.zakresy[i], sizeof(k.zakresy[i]))) {
            return Blad::POLA_ZAREZERWOWANE;
        }
    }

    ObrazJadra::Uklad u{};
    const ObrazJadra::BladUkladu bu = ObrazJadra::zbuduj_uklad(
        k.jadro_baza_wirtualna, k.jadro_rozmiar, k.jadro_baza_fizyczna, k.segmenty, k.liczba_segmentow, &u);
    if (bu != ObrazJadra::BladUkladu::OK) {
        if (bl_ukladu) *bl_ukladu = bu;
        return Blad::UKLAD_OBRAZU;
    }
    if (oczekiwany) {
        if (oczekiwany->va_poczatek != u.va_poczatek || oczekiwany->rozmiar != ObrazJadra::rozmiar_obrazu(u) ||
            oczekiwany->liczba_segmentow != u.liczba_segmentow) {
            return Blad::NIEZGODNY_Z_LINKEREM;
        }
        for (uint32_t i = 0; i < u.liczba_segmentow; ++i) {
            const ObrazJadra::Segment& a = oczekiwany->segmenty[i];
            const ObrazJadra::Segment& b = u.segmenty[i];
            if (a.va != b.va || a.rozmiar != b.rozmiar || a.zajete != b.zajete || a.uprawnienia != b.uprawnienia) {
                return Blad::NIEZGODNY_Z_LINKEREM;
            }
        }
    }
    if (k.zrodlo == ZRODLO_MB2_ZGODNOSC && !ObrazJadra::tozsamosciowy(u)) return Blad::MB2_NIETOZSAMOSCIOWY;

    // Mapa pamieci (R1A: wylacznie tag MB2 typu 6).
    if (k.mapa_format == MAPA_EFI_PRZYSZLA) return Blad::MAPA_NIEOBSLUGIWANA;
    if (k.mapa_format != MAPA_MB2_TAG6) return Blad::MAPA_NIEPOPRAWNA;
    if (k.mapa_adres == 0 || (k.mapa_adres & 7ULL) != 0 || k.mapa_rozmiar < 16 + 24 ||
        k.mapa_rozmiar > MAKS_ROZMIAR_INFORMACJI || k.mapa_rozmiar_wpisu < 24 ||
        ((k.mapa_rozmiar - 16) % k.mapa_rozmiar_wpisu) != 0 ||
        !Adresy::zakres_fizyczny_poprawny(Adresy::AdresFizyczny(k.mapa_adres), Adresy::RozmiarBajtow(k.mapa_rozmiar))) {
        return Blad::MAPA_NIEPOPRAWNA;
    }
    if (k.informacje_adres == 0 || (k.informacje_adres & 7ULL) != 0 || k.informacje_rozmiar < 16 ||
        k.informacje_rozmiar > MAKS_ROZMIAR_INFORMACJI ||
        !Adresy::zakres_fizyczny_poprawny(Adresy::AdresFizyczny(k.informacje_adres),
                                          Adresy::RozmiarBajtow(k.informacje_rozmiar))) {
        return Blad::INFORMACJE_NIEPOPRAWNE;
    }
    if (k.zrodlo == ZRODLO_MB2_ZGODNOSC &&
        !Adresy::zakres_zawiera(k.informacje_adres, k.informacje_rozmiar, k.mapa_adres, k.mapa_rozmiar)) {
        return Blad::MAPA_NIEPOPRAWNA;   // w MB2 mapa jest tagiem wewnatrz MBI
    }

    // Zakresy.
    uint32_t obrazy = 0, informacje = 0;
    for (uint32_t i = 0; i < k.liczba_zakresow; ++i) {
        const Zakres& z = k.zakresy[i];
        if (!Adresy::zakres_fizyczny_poprawny(Adresy::AdresFizyczny(z.poczatek), Adresy::RozmiarBajtow(z.dlugosc)) ||
            !czas_zycia_dozwolony(z.rodzaj, z.czas_zycia) || (z.flagi & ~MASKA_FLAG_ZAKRESU) != 0 ||
            (z.rodzaj == RODZAJ_FRAMEBUFFER && (z.flagi & ZAKRES_BEZ_DIRECT_MAP) == 0)) {
            return Blad::ZAKRES_NIEPOPRAWNY;
        }
        if (z.rodzaj == RODZAJ_OBRAZ_JADRA) {
            ++obrazy;
            if (z.poczatek != u.pa_poczatek || z.dlugosc != ObrazJadra::rozmiar_obrazu(u)) return Blad::OBRAZ_NIEOPISANY;
        }
        if (z.rodzaj == RODZAJ_INFORMACJE_ROZRUCHU) {
            ++informacje;
            if (!Adresy::zakres_zawiera(z.poczatek, z.dlugosc, k.informacje_adres, k.informacje_rozmiar)) {
                return Blad::INFORMACJE_NIEPOPRAWNE;
            }
        }
        for (uint32_t j = 0; j < i; ++j) {
            const Zakres& w = k.zakresy[j];
            if (!Adresy::zakresy_nachodza(z.poczatek, z.dlugosc, w.poczatek, w.dlugosc)) continue;
            /*
             * Wyjatek wylacznie dla zrodla MB2: zasob moze lezec w luzie stron obrazu (poza bajtami segmentow).
             * PMM i tak rezerwuje strony obrazu na stale, a rezerwacja stala wygrywa z rezerwacja modulu
             * (OdblokujZakres pomija ramki obrazu). Dla przyszlego BRB obraz jest wylaczny co do strony.
             */
            const bool obraz_z = z.rodzaj == RODZAJ_OBRAZ_JADRA, obraz_w = w.rodzaj == RODZAJ_OBRAZ_JADRA;
            if (k.zrodlo == ZRODLO_MB2_ZGODNOSC && (obraz_z != obraz_w)) {
                const Zakres& inny = obraz_z ? w : z;
                if (!ObrazJadra::bajty_segmentow_przecinaja(u, inny.poczatek, inny.dlugosc)) continue;
            }
            return Blad::DUPLIKAT_WLASNOSCI;
        }
        /*
         * Przy obrazie nie-tozsamosciowym VA okna obrazu nie sa juz tozsamosciowe: zasob czytany przez
         * PA-jako-wskaznik nie moze lezec fizycznie pod tymi adresami (czytalby obraz jadra zamiast siebie).
         */
        if (!ObrazJadra::tozsamosciowy(u) && (z.flagi & ZAKRES_DOSTEP_TOZSAMOSCIOWY) &&
            ObrazJadra::okno_przecina(u, z.poczatek, z.dlugosc)) {
            return Blad::ZASOB_W_CIENIU_OKNA;
        }
    }
    if (obrazy != 1) return Blad::OBRAZ_NIEOPISANY;
    if (informacje != 1) return Blad::INFORMACJE_NIEPOPRAWNE;
    if (!ObrazJadra::tozsamosciowy(u) &&
        (ObrazJadra::okno_przecina(u, k.informacje_adres, k.informacje_rozmiar) ||
         ObrazJadra::okno_przecina(u, k.mapa_adres, k.mapa_rozmiar))) {
        return Blad::ZASOB_W_CIENIU_OKNA;
    }

    // Tablice stron rozruchu.
    if (k.cr3_rozruchu != 0) {
        if (!Adresy::fizyczny_strony_poprawny(Adresy::AdresFizyczny(k.cr3_rozruchu))) return Blad::CR3_NIEOPISANY;
        const bool cr3_w_obrazie = Adresy::zakres_zawiera(u.pa_poczatek, ObrazJadra::rozmiar_obrazu(u),
                                                          k.cr3_rozruchu, 4096);
        if (((k.flagi & FLAGA_CR3_W_OBRAZIE) != 0) != cr3_w_obrazie) return Blad::CR3_NIEOPISANY;
        if (!cr3_w_obrazie) {
            bool opisany = false;
            for (uint32_t i = 0; i < k.liczba_zakresow; ++i) {
                const Zakres& z = k.zakresy[i];
                if (z.rodzaj == RODZAJ_TABLICE_STRON_ROZRUCHU &&
                    Adresy::zakres_zawiera(z.poczatek, z.dlugosc, k.cr3_rozruchu, 4096)) {
                    opisany = true;
                }
            }
            if (!opisany) return Blad::CR3_NIEOPISANY;
        }
    } else if (k.flagi & FLAGA_CR3_W_OBRAZIE) {
        return Blad::CR3_NIEOPISANY;
    }

    if (uklad) *uklad = u;
    return Blad::OK;
}

/* ------------------------------------------------------------------------------------------------------------
 * Klasyfikacja zakresu fizycznego wobec mapy pamieci MB2 (typy MB2 spec 3.6.8 = typy E820/ACPI).
 * ---------------------------------------------------------------------------------------------------------- */

inline constexpr uint32_t MB2_TYP_RAM = 1, MB2_TYP_ZAREZERWOWANA = 2, MB2_TYP_ACPI_ODZYSKIWALNA = 3,
                          MB2_TYP_ACPI_NVS = 4, MB2_TYP_USZKODZONA = 5;

struct WynikKlasyfikacji {
    Blad blad;            // OK = zakres w calosci pokryty typem 1 i nie przecina zadnego innego typu
    uint32_t typ;         // typ kolidujacego wpisu (0 = dziura)
    uint64_t adres;       // najnizszy adres kolizji/dziury
};

inline Blad blad_dla_typu(uint32_t typ) {
    switch (typ) {
    case MB2_TYP_ACPI_NVS: return Blad::OBRAZ_NA_ACPI_NVS;
    case MB2_TYP_ZAREZERWOWANA: return Blad::OBRAZ_NA_ZAREZERWOWANEJ;
    case MB2_TYP_ACPI_ODZYSKIWALNA: return Blad::OBRAZ_NA_ACPI_ODZYSKIWALNEJ;
    case MB2_TYP_USZKODZONA: return Blad::OBRAZ_NA_USZKODZONEJ;
    default: return Blad::OBRAZ_NA_NIEZNANYM_TYPIE;
    }
}

/*
 * `tag` - bajty calego tagu 6 (`rozmiar` bajtow). Wpisy moga byc nieposortowane i nachodzace (firmware), dlatego:
 * kazde przeciecie z typem != 1 jest bledem (wygrywa najnizszy adres), a pokrycie typem 1 jest liczone jako suma.
 */
inline WynikKlasyfikacji klasyfikuj_wobec_mapy_mb2(const uint8_t* tag, uint64_t rozmiar, uint64_t p, uint64_t n) {
    WynikKlasyfikacji w{Blad::OK, 0, 0};
    uint32_t rozmiar_wpisu = 0;
    if (!tag || rozmiar < 16 + 24) return {Blad::MAPA_NIEPOPRAWNA, 0, 0};
    kopiuj(&rozmiar_wpisu, tag + 8, 4);
    if (rozmiar_wpisu < 24 || ((rozmiar - 16) % rozmiar_wpisu) != 0) return {Blad::MAPA_NIEPOPRAWNA, 0, 0};
    uint64_t k = 0;
    if (n == 0 || !Adresy::dodaj_u64(p, n, &k)) return {Blad::ZAKRES_NIEPOPRAWNY, 0, 0};
    const uint64_t liczba = (rozmiar - 16) / rozmiar_wpisu;
    bool konflikt = false;
    for (uint64_t i = 0; i < liczba; ++i) {
        uint64_t baza = 0, dl = 0;
        uint32_t typ = 0;
        const uint8_t* e = tag + 16 + i * rozmiar_wpisu;
        kopiuj(&baza, e, 8);
        kopiuj(&dl, e + 8, 8);
        kopiuj(&typ, e + 16, 4);
        uint64_t koniec = 0;
        if (dl == 0) continue;
        if (!Adresy::dodaj_u64(baza, dl, &koniec)) return {Blad::MAPA_NIEPOPRAWNA, 0, 0};
        if (typ == MB2_TYP_RAM || !(baza < k && p < koniec)) continue;
        const uint64_t od = baza > p ? baza : p;
        if (!konflikt || od < w.adres) {
            w = {blad_dla_typu(typ), typ, od};
            konflikt = true;
        }
    }
    if (konflikt) return w;
    uint64_t kursor = p;
    while (kursor < k) {
        uint64_t najdalej = kursor;
        for (uint64_t i = 0; i < liczba; ++i) {
            uint64_t baza = 0, dl = 0;
            uint32_t typ = 0;
            const uint8_t* e = tag + 16 + i * rozmiar_wpisu;
            kopiuj(&baza, e, 8);
            kopiuj(&dl, e + 8, 8);
            kopiuj(&typ, e + 16, 4);
            if (typ != MB2_TYP_RAM || dl == 0) continue;
            const uint64_t koniec = baza + dl;   // przepelnienie wykluczone w pierwszej petli
            if (baza <= kursor && kursor < koniec && koniec > najdalej) najdalej = koniec;
        }
        if (najdalej == kursor) return {Blad::OBRAZ_POZA_RAM, 0, kursor};
        kursor = najdalej;
    }
    return w;
}

/* ------------------------------------------------------------------------------------------------------------
 * Tymczasowy adapter zgodnosci Multiboot2 -> KontraktV1.
 *
 * `dostep(pa, n)` zwraca wskaznik do n bajtow pod adresem fizycznym pa albo nullptr (jadro: tozsamosc < 4 GiB;
 * testy: symulowana pamiec). `tlumacz(va, &pa)` tlumaczy VA przez AKTYWNE tablice stron (jadro: przejscie CR3;
 * testy: model). Podklad nie jest zgadywany: kazda strona okna obrazu jest tlumaczona i musi dawac ciagly PA.
 * Reguly pomijania modulow i framebuffera sa IDENTYCZNE z dotychczasowym PMM (audyt D-01 pozostaje do Fazy 7).
 * ---------------------------------------------------------------------------------------------------------- */

inline constexpr uint32_t MB2_TAG_KONIEC = 0, MB2_TAG_MODUL = 3, MB2_TAG_MAPA = 6, MB2_TAG_FRAMEBUFFER = 8,
                          MB2_TAG_ADRES_BAZOWY = 21;

inline bool dodaj_zakres(KontraktV1* k, uint64_t p, uint64_t n, uint32_t rodzaj, uint32_t zycie, uint32_t flagi) {
    if (k->liczba_zakresow >= MAKS_ZAKRESOW) return false;
    k->zakresy[k->liczba_zakresow++] = {p, n, rodzaj, zycie, flagi, 0};
    return true;
}

template <class Dostep, class Tlumacz>
inline Blad zbuduj_z_multiboot2(uint64_t mbi, const SymboleObrazu& sym, uint64_t cr3, Dostep dostep,
                                Tlumacz tlumacz, KontraktV1* wynik) {
    if (!wynik) return Blad::OBCIETY;
    if (mbi == 0 || (mbi & 7ULL) != 0) return Blad::MB2_BRAK_INFORMACJI;
    const uint8_t* naglowek = dostep(mbi, 8);
    if (!naglowek) return Blad::MB2_BRAK_INFORMACJI;
    uint32_t calosc = 0;
    kopiuj(&calosc, naglowek, 4);
    if (calosc < 16 || calosc > MAKS_ROZMIAR_INFORMACJI) return Blad::MB2_BRAK_INFORMACJI;
    const uint8_t* mb = dostep(mbi, calosc);
    if (!mb) return Blad::MB2_BRAK_INFORMACJI;

    KontraktV1 k{};
    k.sygnatura = SYGNATURA;
    k.wersja = WERSJA;
    k.rozmiar = sizeof(KontraktV1);
    k.zrodlo = ZRODLO_MB2_ZGODNOSC;
    k.flagi = FLAGA_TOZSAMOSC_4GIB_PRZED_VMM;
    k.model_podkladu = PODKLAD_CIAGLY;
    k.informacje_adres = mbi;
    k.informacje_rozmiar = calosc;
    k.mapa_format = MAPA_MB2_TAG6;

    const OczekiwanyObraz o = oczekiwany_z_symboli(sym);
    k.jadro_baza_wirtualna = o.va_poczatek;
    k.jadro_rozmiar = o.rozmiar;
    k.liczba_segmentow = o.liczba_segmentow;
    for (uint32_t i = 0; i < o.liczba_segmentow; ++i) k.segmenty[i] = o.segmenty[i];
    if (o.rozmiar == 0 || !Adresy::wyrownany_do_strony(o.rozmiar) || !Adresy::wyrownany_do_strony(o.va_poczatek)) {
        return Blad::NIEZGODNY_Z_LINKEREM;
    }

    // Podklad fizyczny: zmierzony przez aktywne tablice stron, strona po stronie.
    uint64_t pa0 = 0;
    if (!tlumacz(o.va_poczatek, &pa0)) return Blad::TLUMACZENIE_OBRAZU;
    for (uint64_t off = Adresy::ROZMIAR_STRONY; off < o.rozmiar; off += Adresy::ROZMIAR_STRONY) {
        uint64_t pa = 0;
        if (!tlumacz(o.va_poczatek + off, &pa)) return Blad::TLUMACZENIE_OBRAZU;
        if (pa != pa0 + off) return Blad::PODKLAD_NIECIAGLY;
    }
    k.jadro_baza_fizyczna = pa0;
    k.cr3_rozruchu = cr3 & ObrazJadra::MASKA_ADRESU_4K;
    if (k.cr3_rozruchu != 0 && Adresy::zakres_zawiera(pa0, o.rozmiar, k.cr3_rozruchu, 4096)) {
        k.flagi |= FLAGA_CR3_W_OBRAZIE;
    }
    if (!dodaj_zakres(&k, pa0, o.rozmiar, RODZAJ_OBRAZ_JADRA, ZYCIE_TRWALE, 0)) return Blad::ZA_DUZO_ZAKRESOW;
    if (!dodaj_zakres(&k, mbi, calosc, RODZAJ_INFORMACJE_ROZRUCHU, ZYCIE_TRWALE, ZAKRES_DOSTEP_TOZSAMOSCIOWY)) {
        return Blad::ZA_DUZO_ZAKRESOW;
    }

    // Tagi: te same reguly scislosci co dotychczasowy przejdz_po_tagach_multiboot() w PMM.
    bool koniec = false, framebuffer = false;
    uint32_t mapy = 0;
    uint64_t off = 8;
    while (off < calosc) {
        if (calosc - off < 8) return Blad::MB2_TAGI_USZKODZONE;
        uint32_t typ = 0, rozmiar = 0;
        kopiuj(&typ, mb + off, 4);
        kopiuj(&rozmiar, mb + off + 4, 4);
        if (rozmiar < 8 || rozmiar > calosc - off) return Blad::MB2_TAGI_USZKODZONE;
        if (typ == MB2_TAG_KONIEC) {
            if (rozmiar != 8) return Blad::MB2_TAGI_USZKODZONE;
            koniec = true;
            break;
        }
        const uint8_t* t = mb + off;
        if (typ == MB2_TAG_MAPA) {
            if (++mapy > 1) return Blad::MB2_WIELE_MAP;
            uint32_t wpis = 0;
            if (rozmiar >= 16) kopiuj(&wpis, t + 8, 4);
            k.mapa_adres = mbi + off;
            k.mapa_rozmiar = rozmiar;
            k.mapa_rozmiar_wpisu = wpis;
        } else if (typ == MB2_TAG_MODUL && rozmiar >= 16) {
            uint32_t s = 0, e = 0;
            kopiuj(&s, t + 8, 4);
            kopiuj(&e, t + 12, 4);
            // Pomijanie jak w dotychczasowym zarezerwuj_modul(): start 0, pusty/odwrocony albo > 256 MiB (D-01).
            if (s != 0 && e > s && static_cast<uint64_t>(e - s) <= MAKS_ROZMIAR_MODULU_MB2) {
                if (!dodaj_zakres(&k, s, static_cast<uint64_t>(e) - s, RODZAJ_MODUL,
                                  ZYCIE_DO_ZWOLNIENIA_PRZEZ_KONSUMENTA, ZAKRES_DOSTEP_TOZSAMOSCIOWY)) {
                    return Blad::ZA_DUZO_ZAKRESOW;
                }
            }
        } else if (typ == MB2_TAG_FRAMEBUFFER && rozmiar >= 32) {
            if (framebuffer) return Blad::MB2_WIELE_FRAMEBUFFEROW;
            framebuffer = true;
            uint64_t adres = 0;
            uint32_t pitch = 0, wysokosc = 0;
            kopiuj(&adres, t + 8, 8);
            kopiuj(&pitch, t + 16, 4);
            kopiuj(&wysokosc, t + 24, 4);
            // Pomijanie jak w dotychczasowym zarezerwuj_framebuffer(): adres 0, pitch 0 albo wysokosc 0.
            if (adres != 0 && pitch != 0 && wysokosc != 0) {
                const uint64_t n = static_cast<uint64_t>(pitch) * wysokosc;   // 32 x 32 bity: bez przepelnienia
                if (Adresy::zakres_fizyczny_poprawny(Adresy::AdresFizyczny(adres), Adresy::RozmiarBajtow(n)) &&
                    !dodaj_zakres(&k, adres, n, RODZAJ_FRAMEBUFFER, ZYCIE_TRWALE, ZAKRES_BEZ_DIRECT_MAP)) {
                    return Blad::ZA_DUZO_ZAKRESOW;
                }
            }
        } else if (typ == MB2_TAG_ADRES_BAZOWY && rozmiar >= 12) {
            uint32_t baza = 0;
            kopiuj(&baza, t + 8, 4);
            if (baza != pa0) return Blad::MB2_TAG21_NIEZGODNY;
        }
        const uint64_t krok = (static_cast<uint64_t>(rozmiar) + 7ULL) & ~7ULL;
        if (krok > calosc - off) return Blad::MB2_TAGI_USZKODZONE;
        off += krok;
    }
    if (!koniec) return Blad::MB2_TAGI_USZKODZONE;
    if (mapy == 0) return Blad::MB2_BRAK_MAPY;
    *wynik = k;
    return Blad::OK;
}

} // namespace KontraktPamieci

#ifdef BURSZTYN_PMM_HOST_TEST
/*
 * Podstawienia srodowiska adaptera MB2 w buildzie hostowym PMM (definicje: pmm.cpp). Domyslnie zachowuja
 * dotychczasowe zachowanie testow hosta: symbole obrazu 1..2 MiB, PA == VA, adres MBI = wskaznik hosta.
 */
namespace PmmHost {
extern KontraktPamieci::SymboleObrazu symbole;
extern bool (*tlumacz)(uint64_t va, uint64_t* pa);
extern const uint8_t* (*dostep)(uint64_t pa, uint64_t n);
extern uint64_t cr3;
} // namespace PmmHost
#endif
