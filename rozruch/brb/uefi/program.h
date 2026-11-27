/*
 * BRB - logika aplikacji UEFI Fazy 2, niezalezna od punktu wejscia (testowalna na hoscie z atrapami
 * firmware o tej samej konwencji wywolan ms_abi).
 *
 * Zakres: walidacja tablic firmware, kontrola zaladowanego obrazu i relokacji, polski ekran tekstowy (Faza 2),
 * odczyt i walidacja jadra (Faza 3), przydzial stron, ladowanie i weryfikacja obrazu z pelnym zwolnieniem
 * (Faza 4A), oczekiwanie na klawisz z limitem czasu, powrot do firmware.
 * Program NIE konczy uslug rozruchowych i NIE uruchamia jadra (Fazy 4B/4C).
 */
#ifndef BRB_UEFI_PROGRAM_H
#define BRB_UEFI_PROGRAM_H

#include "ladowanie_jadra.h"
#include "plik_jadra.h"
#include "uefi_typy.h"
#include "../wspolne/elf64.h"
#include "../wspolne/log_szeregowy.h"

namespace brb {

enum class BladFirmware : uint8_t {
    Brak = 0,
    BrakUchwytuObrazu,
    BrakTablicySystemowej,
    ZlaSygnaturaTablicy,
    ZaMalaTablica,
    ZaStaraRewizja,
    BrakUslugRozruchowych,
    ZlaSygnaturaUslug,
    ZaMaleUslugi,
    BrakFunkcjiUslug,
    BrakKonsoliWyjscia,
};

struct WynikWalidacji {
    EFI_STATUS   status;
    BladFirmware blad;
};

// Minimalne wymagania (uzasadnienie w docs/brb/architektura-uefi.md, sekcja 14.3).
constexpr UINT32 MIN_REWIZJA_UEFI = EFI_REWIZJA(2, 0);
constexpr UINT32 MIN_ROZMIAR_USLUG = static_cast<UINT32>(offsetof(EFI_BOOT_SERVICES, Stall) + sizeof(EFI_STALL));

// Kontrola tablicy systemowej i uslug rozruchowych PRZED jakimkolwiek wywolaniem firmware.
// ConIn nie jest wymagane (brak klawiatury obslugiwany osobno).
WynikWalidacji waliduj_firmware(EFI_HANDLE obraz, const EFI_SYSTEM_TABLE* st);
const char* opis_bledu_firmware(BladFirmware blad);   // ASCII, do logu
const char* nazwa_statusu(EFI_STATUS status);          // ASCII, np. "EFI_TIMEOUT"

struct Ustawienia {
    UINT32 limit_czasu_s;        // czas oczekiwania na klawisz (zdarzenie zegara)
    UINT32 krok_awaryjny_ms;     // krok Stall w trybie awaryjnym (gdy zdarzenia niedostepne)
    UINT32 czekanie_bez_klawiatury_s;
    bool polskie_znaki_konsoli;  // false: transliteracja ASCII na konsoli firmware (patrz program.cpp)
};
constexpr Ustawienia USTAWIENIA_DOMYSLNE{60, 100, 5, false};

enum class WynikKlawisza : uint8_t { Klawisz, LimitCzasu, BladUrzadzenia };

struct StanProgramu {                       // do diagnostyki i testow
    bool transliteracja = false;            // konsola bez polskich glifow -> ASCII
    bool tryb_awaryjny_klawiatury = false;  // zdarzenia niedostepne -> Stall
    bool relokacja_ok = false;
    UINT32 ostrzezenia_glifow = 0;
    WynikKlawisza klawisz = WynikKlawisza::LimitCzasu;
    // Faza 3: odczyt i walidacja jadra
    EFI_STATUS status_jadra = EFI_NOT_READY;   // EFI_NOT_READY = etap nie zostal wykonany
    BladPliku blad_pliku = BladPliku::Brak;
    BladElf blad_elf = BladElf::Brak;
    UINT64 rozmiar_jadra = 0;
    UINT32 segmenty = 0;
    bool bufor_zwolniony = false;
    // Faza 4A: przydzial stron, ladowanie i weryfikacja obrazu
    EFI_STATUS status_4a = EFI_NOT_READY;      // EFI_NOT_READY = etap nie zostal wykonany
    BladPrzydzialu blad_4a = BladPrzydzialu::Brak;
    EtapFazy4A etap_4a = EtapFazy4A::WejscieZweryfikowane;
    UINT64 strony_4a = 0;
    UINT32 bledy_sprzatania_4a = 0;
};

// Kontrola, czy obraz zostal poprawnie zaladowany i zrelokowany: adres kodu BRB w [ImageBase, +ImageSize)
// oraz wskaznik bezwzgledny zapisany w danych (wymaga relokacji DIR64) == adres wyliczony wzgledem RIP.
bool sprawdz_obraz(const EFI_LOADED_IMAGE_PROTOCOL* obraz, const EFI_SYSTEM_TABLE* st);

// pamiec: tlumacz adresow fizycznych dla Fazy 4A (w UEFI identycznosc - efi_main.cpp; w testach hosta
// symulowana pamiec fizyczna). Wymagany jawnie, aby zaden kod nie dereferencjonowal adresu fizycznego "z gory".
EFI_STATUS uruchom(EFI_HANDLE obraz, EFI_SYSTEM_TABLE* st, LogSzeregowy& log, const Ustawienia& ustawienia,
                   const DostepFizyczny& pamiec, StanProgramu* stan = nullptr);

#ifdef BRB_TEST_KONFLIKT_STRONA
// Wylacznie wariant testowy (cel brb-uefi-test-konflikt): prawowity przydzial jednej strony z zakresu jadra
// przez AllocatePages przed Faza 4A, wzorzec straznika i jego kontrola po Fazie 4A (uefi/test_konflikt.cpp).
struct BlokadaTestowa {
    EFI_PHYSICAL_ADDRESS adres;
    bool zajeta;
};
void zajmij_strone_testowa(EFI_BOOT_SERVICES* bs, UINT64 adres, const DostepFizyczny& pamiec, BlokadaTestowa* b,
                           LogSzeregowy& log);
void zwolnij_strone_testowa(EFI_BOOT_SERVICES* bs, const DostepFizyczny& pamiec, BlokadaTestowa* b, LogSzeregowy& log);
#endif

}  // namespace brb

#endif
