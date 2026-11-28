/*
 * BRB - Faza R2: elastyczne umieszczenie fizyczne jadra, ladowanie, weryfikacja i przygotowanie tablic przejscia.
 *
 * Aktywna architektura natywna (docs/brb/r2-elastyczne-ladowanie.md):
 *   1. walidacja ELF (Faza 3, polityka bez zmian) i metadanych natywnego wejscia (BRBJADRO),
 *   2. przydzial ciaglego podkladu przez firmware: AllocatePages(AllocateMaxAddress < 4 GiB, EfiLoaderData),
 *      z dopelnieniem na wyrownanie; baza wybierana WEWNATRZ zwroconego przydzialu,
 *   3. przesuniecie planu (p_vaddr bez zmian, cel = baza + (p_vaddr - baza_wirtualna)), kopiowanie p_filesz,
 *      zerowanie BSS, luk i wypelnienia stron, niezalezna weryfikacja bajt po bajcie - PRZED ExitBootServices,
 *   4. zasoby przejscia (pula tablic, stos, strona kodu przejscia, rezerwa informacji rozruchu) - kazdy
 *      przez AllocatePages, w rejestrze wlasnosci,
 *   5. tablice przejscia z jadro/mm/plan_przejscia.h (te same, ktore uruchomily jadro w R1B) i ich NIEZALEZNA
 *      weryfikacja (wspolne/weryfikacja_tablic.h) - bez ladowania CR3,
 *   6. R2 jest faza diagnostyczna: wszystkie zasoby sa zwalniane, BRB wraca do firmware (4B/4C pozniej).
 * Nie ma: ExitBootServices, skoku do jadra, zmian CR0/CR3/CR4/EFER, GDT/IDT, adresu stalego ani kopiowania po EBS.
 */
#ifndef BRB_UEFI_LADOWANIE_NATYWNE_H
#define BRB_UEFI_LADOWANIE_NATYWNE_H

#include "plik_jadra.h"
#include "uefi_typy.h"
#include "../wspolne/elf64.h"
#include "../wspolne/ladowanie.h"
#include "../wspolne/log_szeregowy.h"
#include "../wspolne/metadane_jadra.h"
#include "../wspolne/rejestr_zasobow.h"
#include "../wspolne/umiejscowienie.h"
#include "../wspolne/weryfikacja_tablic.h"

namespace brb {

struct CechyProcesora {
    unsigned bity_fizyczne;   // CPUID 80000008h EAX[7:0]; 36 bez liscia
    bool nx;                  // CPUID 80000001h EDX[20]
    bool long_mode;           // CPUID 80000001h EDX[29]
    bool la57_aktywne;        // CR4.LA57 (odczyt; BRB nie zmienia CR4)
};

enum class EtapR2 : uint8_t {
    Start = 0,
    WymaganiaGotowe,
    PodkladPrzydzielony,
    SegmentySkopiowane,
    ObrazZweryfikowany,
    ZasobyPrzejscia,
    TabliceZbudowane,
    TabliceZweryfikowane,
    ZasobyZwolnione,
};

enum class BladR2 : uint8_t {
    Brak = 0,
    BrakFunkcji,              // AllocatePages/FreePages/GetMemoryMap == NULL albo brak danych
    Procesor,                 // brak NX, brak Long Mode, aktywne LA57 albo nieobslugiwana szerokosc fizyczna
    Wymagania,                // wymagania_obrazu odrzucil plan/metadane (szczegol: blad_umiejscowienia)
    MapaPamieci,              // migawka mapy niedostepna
    BrakPamieciCiaglej,       // AllocateMaxAddress podkladu nieudane (szczegol: status)
    Umiejscowienie,           // wybierz_baze odrzucil przydzial (szczegol: blad_umiejscowienia)
    Rejestr,                  // przydzial koliduje z zywym zasobem / rejestr pelny
    Przesuniecie,             // przesun_plan odrzucil (szczegol: blad_ladowania)
    Ladowanie,                // zaladuj_segmenty (szczegol: blad_ladowania)
    Weryfikacja,              // weryfikuj_zaladowany_obraz (szczegol: blad_ladowania)
    PrzydzialZasobu,          // AllocatePages zasobu przejscia nieudane
    UkladObrazu,              // ObrazJadra::zbuduj_uklad odrzucil uklad
    PlanTablic,               // PlanPrzejscia::zbuduj odrzucil (szczegol: blad_planu)
    WeryfikacjaTablic,        // niezalezna weryfikacja tablic (szczegol: blad_tablic)
    NakladanieNaNvs,          // przydzial nachodzi na ACPI NVS w migawce (nie powinno sie zdarzyc)
    SprzatanieNieudane,       // FreePages zwrocil blad (zasob porzucony)
};

constexpr UINTN R2_RAMKI_PULI = 64;           // pula tablic przejscia (256 KiB)
constexpr UINTN R2_STRONY_STOSU = 4;          // stos przejscia 16 KiB
constexpr UINTN R2_STRONY_INFORMACJI = 16;    // rezerwa na kontrakt + informacje rozruchu (wypelniane w 4B)
constexpr UINTN R2_STRONY_KODU = 1;           // strona kodu przejscia (tresc w 4C)

struct RaportR2 {
    EtapR2 etap;
    BladR2 blad;
    EFI_STATUS status;
    BladUmiejscowienia blad_umiejscowienia;
    BladLadowania blad_ladowania;
    uint32_t blad_planu;          // PlanPrzejscia::Blad
    BladTablic blad_tablic;
    WymaganiaObrazu wymagania;
    WybraneUmiejscowienie umiejscowienie;
    RaportLadowania ladowanie;
    RaportTablic tablice;
    uint64_t cr3;
    uint64_t pula_pa, stos_pa, kod_pa, informacje_pa;
    uint32_t ramki_tablic_uzyte;
    uint32_t zakresy_nvs;
    uint64_t bajty_nvs;
    uint32_t bledy_sprzatania;
    uint32_t wlasne_po_sprzataniu;
    RejestrZasobow rejestr;
};

EFI_STATUS faza_r2_zaladuj_natywnie(EFI_BOOT_SERVICES* bs, const EFI_LOADED_IMAGE_PROTOCOL* obraz, const BuforJadra& bufor,
                                    const PlanLadowania& plan, const MetadaneWejscia& meta, const CechyProcesora& cpu,
                                    const DostepFizyczny& pamiec, LogSzeregowy& log, RaportR2* raport);
const char* opis_bledu_r2(BladR2 b);
// CPUID (NX, LM, MAXPHYADDR) i odczyt CR4.LA57 (w buildzie hostowym CR4 nie jest czytany).
CechyProcesora odczytaj_cechy_procesora();

}  // namespace brb

#endif
