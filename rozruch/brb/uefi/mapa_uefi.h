/*
 * BRB - pobieranie migawki mapy pamieci przez GetMemoryMap (UEFI 2.10, 7.2) - Faza 4A.
 *
 * Algorytm: zapytanie o rozmiar (bufor 0 B -> EFI_BUFFER_TOO_SMALL), alokacja bufora stron (EfiLoaderData)
 * z zapasem MAPA_ZAPAS_DESKRYPTOROW deskryptorow na zmiany mapy wywolane ta alokacja, ponowne wywolanie;
 * przy EFI_BUFFER_TOO_SMALL bufor jest zwalniany i proba powtarzana z nowym rozmiarem - najwyzej
 * MAPA_MAKS_PROB razy. Po sukcesie migawka jest walidowana (wspolne/mapa_pamieci.h).
 *
 * To NIE jest ostateczna mapa dla ExitBootServices (Faza 4C): klucz MapKey tej migawki traci waznosc przy
 * pierwszej kolejnej alokacji.
 */
#ifndef BRB_UEFI_MAPA_UEFI_H
#define BRB_UEFI_MAPA_UEFI_H

#include "uefi_typy.h"
#include "../wspolne/mapa_pamieci.h"

namespace brb {

constexpr UINT32 MAPA_MAKS_PROB = 4;
constexpr UINTN MAPA_ZAPAS_DESKRYPTOROW = 16;
constexpr UINTN MAPA_MAKS_BUFOR = 1024 * 1024;   // 4096 wpisow x 256 B; wiecej = niewiarygodna mapa

enum class BladPobraniaMapy : uint8_t {
    Brak = 0,
    BrakFunkcji,            // GetMemoryMap/AllocatePages/FreePages == NULL
    NieoczekiwanyStatus,    // pierwsze wywolanie z buforem 0 B nie zwrocilo EFI_BUFFER_TOO_SMALL
    ZlyRozmiarZgloszony,    // zgloszony rozmiar/krok deskryptora 0 albo poza limitem
    Alokacja,               // AllocatePages bufora nieudane
    ZaDuzoProb,             // mapa rosnie przy kazdej probie
    BladFirmware,           // GetMemoryMap zwrocil inny blad
    Niespojnosc,            // firmware zglosil rozmiar wiekszy niz bufor przy EFI_SUCCESS
    Walidacja,              // migawka nie przeszla waliduj_mape (szczegol w blad_walidacji)
};

struct MigawkaMapy {
    EFI_PHYSICAL_ADDRESS bufor;   // 0 = brak
    UINTN strony;
    UINTN rozmiar;                // bajty zwrocone przez GetMemoryMap
    UINTN klucz;                  // MapKey (wylacznie diagnostyka w Fazie 4A)
    UINTN rozmiar_deskryptora;
    UINT32 wersja;
    UINT32 proby;
    WidokMapy widok;
    StatystykiMapy statystyki;
};

struct WynikMapy {
    EFI_STATUS status;
    BladPobraniaMapy blad;
    BladMapy blad_walidacji;
};

WynikMapy pobierz_migawke_mapy(EFI_BOOT_SERVICES* bs, MigawkaMapy* migawka);
// Zwalnia bufor migawki (bezpieczne do wielokrotnego wywolania). Zwraca status FreePages albo EFI_SUCCESS.
EFI_STATUS zwolnij_migawke_mapy(EFI_BOOT_SERVICES* bs, MigawkaMapy* migawka);
const char* opis_bledu_pobrania_mapy(BladPobraniaMapy b);

}  // namespace brb

#endif
