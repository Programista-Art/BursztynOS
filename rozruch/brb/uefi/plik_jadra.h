/*
 * BRB - odczyt pliku jadra z woluminu, z ktorego uruchomiono BRB (Faza 3).
 *
 * UEFI 2.10: 9.1 (LoadedImage->DeviceHandle), 13.4 (Simple File System), 13.5 (File Protocol, EFI_FILE_INFO),
 * 7.2 (AllocatePages/FreePages). Tylko odczyt: plik otwierany z EFI_FILE_MODE_READ; zadnych Write/Delete/
 * SetInfo/Flush. Zadnego przeszukiwania innych urzadzen. Wszystkie uchwyty sa zamykane, a strony zwalniane na
 * kazdej sciezce bledu; przy sukcesie bufor nalezy do wywolujacego (zwolnij_bufor_jadra).
 */
#ifndef BRB_UEFI_PLIK_JADRA_H
#define BRB_UEFI_PLIK_JADRA_H

#include "uefi_typy.h"
#include "../wspolne/log_szeregowy.h"

namespace brb {

// Sciezka jadra na nosniku Live (docs/brb/specyfikacja-techniczna.md 6.2, audyt-rozruchu.md 8):
// /boot/system_operacyjny.bin; separator UEFI to '\'. Zainstalowany system (jadro_a/jadro_b) - Faza 8.
constexpr const CHAR16* SCIEZKA_JADRA = u"\\boot\\system_operacyjny.bin";
constexpr const char* SCIEZKA_JADRA_ASCII = "\\boot\\system_operacyjny.bin";
constexpr UINTN ROZMIAR_KAWALKA_ODCZYTU = 1024 * 1024;   // jeden Read() najwyzej 1 MiB
constexpr UINTN MAKS_ROZMIAR_INFO_PLIKU = 512;            // EFI_FILE_INFO dla krotkiej nazwy pliku

enum class BladPliku : uint8_t {
    Brak = 0,
    BrakSystemuPlikow,        // urzadzenie obrazu BRB nie ma EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
    ZlyProtokol,              // niepoprawna rewizja/pusty wskaznik funkcji w protokole firmware
    OtwarcieWoluminu,         // OpenVolume nieudane
    BrakPliku,                // Open -> EFI_NOT_FOUND
    OtwarciePliku,            // Open -> inny blad
    Metadane,                 // GetInfo nieudane albo za duza struktura
    NiespojneMetadane,        // EFI_FILE_INFO niespojne (Size, nazwa bez NUL, rozmiary)
    Katalog,                  // atrybut EFI_FILE_DIRECTORY
    PustyPlik,                // FileSize == 0
    ZaDuzyPlik,               // FileSize > limit
    Alokacja,                 // AllocatePages nieudane
    Odczyt,                   // Read -> blad
    PrzedwczesnyKoniec,       // Read zwrocil 0 B przed odczytaniem FileSize bajtow
    ZaDuzoOdczytow,           // firmware zwraca zbyt male porcje (limit iteracji)
    NiespojnyOdczyt,          // Read zglosil wiecej bajtow niz bufor
    PlikDluzszyNizZgloszono,  // po FileSize bajtach plik nadal ma dane
};

struct BuforJadra {
    EFI_PHYSICAL_ADDRESS adres;   // 0 = brak bufora
    UINTN strony;
    UINT64 rozmiar;               // liczba odczytanych bajtow (= FileSize)
    const UINT8* dane() const { return reinterpret_cast<const UINT8*>(static_cast<uintptr_t>(adres)); }
};

struct WynikPliku {
    EFI_STATUS status;
    BladPliku blad;
    UINT32 odczyty;               // liczba wywolan Read (diagnostyka)
};

WynikPliku wczytaj_plik_jadra(EFI_BOOT_SERVICES* bs, EFI_HANDLE urzadzenie, const CHAR16* sciezka, UINT64 maks_rozmiar,
                              BuforJadra* wynik, LogSzeregowy& log);
void zwolnij_bufor_jadra(EFI_BOOT_SERVICES* bs, BuforJadra* bufor);
const char* opis_bledu_pliku(BladPliku blad);

}  // namespace brb

#endif
