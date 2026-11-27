/*
 * BRB - Faza 4A: przydzial stron fizycznych jadra (AllocatePages/AllocateAddress), zaladowanie zwalidowanych
 * segmentow, weryfikacja obrazu i zwolnienie zasobow. UEFI 2.10: 7.2 (AllocatePages, FreePages, GetMemoryMap).
 *
 * Polityka wlasnosci: BRB zapisuje WYLACZNIE strony, ktore firmware przydzielilo mu przez AllocatePages
 * z AllocateAddress dokladnie pod adres z planu. Opis w mapie pamieci ("wolna") nie daje prawa do zapisu.
 * Gdy zakres jest zajety: wycofanie wszystkich wlasnych przydzialow, dowody z mapy pamieci, deterministyczny
 * blad i powrot do firmware. Brak kopiowania po ExitBootServices, brak relokacji, brak odzyskiwania pamieci
 * firmware. Po udanej weryfikacji obraz jest zwalniany (Faza 4A nie przekazuje sterowania).
 *
 * Typ pamieci: EfiLoaderData. Konsekwencja: firmware z polityka ochrony pamieci (np. NX dla danych) moze
 * oznaczyc te strony jako niewykonywalne - Faza 4C musi to uwzglednic przed skokiem (wlasne tablice stron).
 */
#ifndef BRB_UEFI_LADOWANIE_JADRA_H
#define BRB_UEFI_LADOWANIE_JADRA_H

#include "mapa_uefi.h"
#include "plik_jadra.h"
#include "uefi_typy.h"
#include "../wspolne/elf64.h"
#include "../wspolne/ladowanie.h"
#include "../wspolne/log_szeregowy.h"

namespace brb {

enum class EtapFazy4A : uint8_t {
    WejscieZweryfikowane = 0,
    PlanPrzydzialuGotowy,
    ZakresyPrzydzielone,
    SegmentySkopiowane,
    ObrazZweryfikowany,
    ZasobyZwolnione,
    PowrotDoFirmware,
};

enum class BladPrzydzialu : uint8_t {
    Brak = 0,
    BrakFunkcji,             // AllocatePages/FreePages == NULL
    PlanPrzydzialu,          // zbuduj_zakresy_przydzialu odrzucil plan (szczegol: blad_ladowania)
    KonfliktZObrazemBrb,     // zakres celu nachodzi na obraz BRB (LoadedImage)
    KonfliktZBuforemZrodla,  // zakres celu nachodzi na bufor pliku jadra
    MapaPamieci,             // migawka mapy niedostepna (szczegol: blad_mapy)
    ZakresZajety,            // AllocateAddress odmowil (strony nalezace do firmware albo innej aplikacji)
    ZlyAdresZwrocony,        // AllocateAddress zwrocil inny adres niz zadany
    Ladowanie,               // kopiowanie odrzucone przez kontrole (szczegol: blad_ladowania)
    Weryfikacja,             // weryfikacja obrazu nieudana (szczegol: blad_ladowania)
    SprzatanieNieudane,      // FreePages zwrocil blad (zasob porzucony, nie zwalniany ponownie)
};

struct PrzydzialJadra {
    uint32_t liczba;
    struct {
        EFI_PHYSICAL_ADDRESS adres;
        UINTN strony;
        bool wlasny;     // true dopoki BRB jest wlascicielem (po FreePages: false, takze przy bledzie)
    } zakresy[LADOWANIE_MAKS_ZAKRESOW];
};

struct RaportFazy4A {
    EtapFazy4A etap;
    EFI_STATUS status;
    BladPrzydzialu blad;
    BladLadowania blad_ladowania;
    BladPobraniaMapy blad_mapy;
    BladMapy blad_walidacji_mapy;
    int32_t zakres_bledu;          // indeks zakresu, ktorego dotyczy blad, albo -1
    ZakresyPrzydzialu zakresy;
    uint32_t przydzielone_max;     // ile zakresow bylo jednoczesnie posiadanych
    uint32_t wycofane;             // ile zakresow zwolniono w ramach wycofania
    uint32_t bledy_sprzatania;     // nieudane FreePages (zakres porzucony)
    uint32_t wpisy_mapy;
    RaportLadowania ladowanie;
};

EFI_STATUS faza4a_zaladuj_i_zweryfikuj(EFI_BOOT_SERVICES* bs, const EFI_LOADED_IMAGE_PROTOCOL* obraz,
                                       const BuforJadra& bufor, const PlanLadowania& plan, const DostepFizyczny& pamiec,
                                       LogSzeregowy& log, RaportFazy4A* raport);
const char* opis_bledu_przydzialu(BladPrzydzialu b);

}  // namespace brb

#endif
