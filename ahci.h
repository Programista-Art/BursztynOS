/*
 * Bursztyn OS - AHCI (SATA)
 *
 * Publiczny interfejs sterownika AHCI.
 *
 * Implementacja:
 *   ahci.cpp
 *
 * Obecny sterownik:
 *
 *   - wykrywa kontroler SATA AHCI przez PCI,
 *   - wybiera jeden glowny dysk SATA,
 *   - wykonuje IDENTIFY DEVICE,
 *   - wymaga LBA48,
 *   - obsluguje logiczne sektory 512 B,
 *   - wykonuje synchroniczne operacje pollingowe,
 *   - ogranicza pojedynczy transfer do 32 sektorow / 16 KiB.
 *
 * Struktury rejestrow MMIO HBA, Command List, Command Table, PRDT i FIS
 * sa celowo prywatne dla ahci.cpp. Inne moduly jadra powinny korzystac
 * tylko z API ponizej.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. PUBLICZNE PARAMETRY API
 * ========================================================================= */

/*
 * Nazwy maja prefiks AHCI_API_, aby nie kolidowac z prywatnymi constexpr
 * implementacji ahci.cpp.
 */

/*
 * Poprawiony ahci.cpp dopuszcza tylko dyski z logicznym sektorem 512 B.
 *
 * Jest to obecnie zgodne z Bursztynowym Systemem Plikow i API blokowym.
 * Dysk raportujacy inny logiczny rozmiar sektora jest odrzucany podczas
 * IDENTIFY DEVICE.
 */
#define AHCI_API_ROZMIAR_SEKTORA UINT32_C(512)

/*
 * Maksymalna liczba sektorow w jednym publicznym wywolaniu read/write.
 */
#define AHCI_API_MAKS_SEKTOROW_OPERACJI UINT32_C(32)

/*
 * Maksymalna liczba bajtow kopiowana przez pojedyncza operacje.
 */
#define AHCI_API_MAKS_BAJTOW_OPERACJI \
    (AHCI_API_ROZMIAR_SEKTORA * AHCI_API_MAKS_SEKTOROW_OPERACJI)

/*
 * Komendy READ DMA EXT / WRITE DMA EXT uzywane przez implementacje
 * obsluguja adresowanie LBA48.
 */
#define AHCI_API_LBA48_MAKS UINT64_C(0x0000FFFFFFFFFFFF)

/* =========================================================================
 * 2. PUBLICZNE ABI
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inicjalizuje kontroler i glowny dysk AHCI.
 *
 * Implementacja:
 *   - wyszukuje PCI class 01/subclass 06/prog-if 01,
 *   - wlacza PCI Memory Space + Bus Master,
 *   - mapuje ABAR,
 *   - wykonuje BIOS/OS handoff, jesli HBA go wymaga,
 *   - resetuje HBA,
 *   - wybiera pierwszy aktywny port SATA ATA,
 *   - przydziela fizyczne bufory DMA,
 *   - wykonuje IDENTIFY DEVICE.
 *
 * Ponowne wywolanie po poprawnej inicjalizacji nie inicjalizuje sterownika
 * drugi raz.
 *
 * Brak wartosci zwrotnej zostaje zachowany dla zgodnosci ABI.
 * Niepowodzenie jest raportowane przez log i powoduje, ze funkcje I/O
 * zwracaja false.
 */
void inicjalizuj_kontroler_ahci();

/*
 * Synchronicznie odczytuje sektory z glownego dysku AHCI.
 *
 * lba:
 *   pierwszy sektor logiczny.
 *
 * ilosc_sektorow:
 *   1..AHCI_API_MAKS_SEKTOROW_OPERACJI.
 *
 * bufor_docelowy:
 *   musi wskazywac co najmniej:
 *
 *     ilosc_sektorow * AHCI_API_ROZMIAR_SEKTORA
 *
 *   zapisywalnych bajtow pamieci kernela.
 *
 * Zwraca true tylko wtedy, gdy cala operacja zakonczyla sie poprawnie.
 *
 * Zwraca false m.in. przy:
 *   - niezainicjalizowanym AHCI,
 *   - nullptr,
 *   - liczbie sektorow == 0,
 *   - transferze > 32 sektorow,
 *   - wyjsciu poza pojemnosc dysku,
 *   - zajetym sterowniku,
 *   - timeout,
 *   - PxIS.TFES / PxTFD.ERR / PxSERR.
 *
 * UWAGA:
 * To jest API kernela. Nie wolno przekazywac tu bezposrednio wskaznikow
 * Ring 3. Syscall powinien najpierw zweryfikowac i skopiowac dane przez
 * bezpieczne copy_from_user/copy_to_user.
 */
bool czytaj_z_glownego_dysku_ahci(
    uint64_t lba,
    uint32_t ilosc_sektorow,
    void* bufor_docelowy
);

/*
 * Synchronicznie zapisuje sektory na glowny dysk AHCI.
 *
 * dane_zrodlowe:
 *   musi wskazywac co najmniej:
 *
 *     ilosc_sektorow * AHCI_API_ROZMIAR_SEKTORA
 *
 *   czytelnych bajtow pamieci kernela.
 *
 * Sygnatura pozostaje void* dla zgodnosci z aktualnym ABI ahci.cpp.
 * Semantycznie bufor jest tylko odczytywany.
 *
 * Te same ograniczenia LBA/liczby sektorow co przy odczycie.
 */
bool zapisz_na_glowny_dysk_ahci(
    uint64_t lba,
    uint32_t ilosc_sektorow,
    void* dane_zrodlowe
);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 * 3. LEKKIE HELPERY HEADER-ONLY
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Sprawdza liczbe sektorow bez dostepu do sprzetu.
 */
inline constexpr bool ahci_api_liczba_sektorow_poprawna(
    uint32_t ilosc_sektorow
) noexcept {
    return
        ilosc_sektorow >= 1U &&
        ilosc_sektorow <=
            AHCI_API_MAKS_SEKTOROW_OPERACJI;
}

/*
 * Oblicza rozmiar transferu dla uprzednio zweryfikowanej liczby sektorow.
 *
 * Dla wartosci spoza limitu zwraca 0.
 */
inline constexpr uint32_t ahci_api_bajty_transferu(
    uint32_t ilosc_sektorow
) noexcept {
    return
        ahci_api_liczba_sektorow_poprawna(
            ilosc_sektorow
        )
            ? ilosc_sektorow *
                AHCI_API_ROZMIAR_SEKTORA
            : 0U;
}

/*
 * Sprawdza, czy zakres [lba, lba + ilosc_sektorow) miesci sie w znanej
 * liczbie sektorow dysku bez ryzyka overflow.
 *
 * To helper czysto matematyczny. Wlasciwy ahci.cpp wykonuje analogiczna
 * kontrole na pojemnosci uzyskanej z IDENTIFY DEVICE.
 */
inline constexpr bool ahci_api_zakres_lba_poprawny(
    uint64_t lba,
    uint32_t ilosc_sektorow,
    uint64_t liczba_sektorow_dysku
) noexcept {
    return
        ahci_api_liczba_sektorow_poprawna(
            ilosc_sektorow
        ) &&
        liczba_sektorow_dysku != 0 &&
        lba < liczba_sektorow_dysku &&
        static_cast<uint64_t>(
            ilosc_sektorow
        ) <=
            liczba_sektorow_dysku -
            lba;
}

/*
 * Sprawdza granice samego adresowania LBA48.
 *
 * Ograniczenie rzeczywistej pojemnosci dysku nadal ma pierwszenstwo.
 */
inline constexpr bool ahci_api_lba48_poprawne(
    uint64_t lba
) noexcept {
    return
        lba <=
        AHCI_API_LBA48_MAKS;
}

/* =========================================================================
 * 4. KONTROLA STALYCH ABI
 * ========================================================================= */

static_assert(
    sizeof(uint32_t) == 4,
    "AHCI wymaga 32-bitowego uint32_t"
);

static_assert(
    sizeof(uint64_t) == 8,
    "AHCI wymaga 64-bitowego uint64_t"
);

static_assert(
    AHCI_API_ROZMIAR_SEKTORA == 512U,
    "Obecny sterownik AHCI i BSP wymagaja sektorow 512 B"
);

static_assert(
    AHCI_API_MAKS_SEKTOROW_OPERACJI == 32U,
    "Zmiana limitu API wymaga aktualizacji PRDT w ahci.cpp"
);

static_assert(
    AHCI_API_MAKS_BAJTOW_OPERACJI == 16384U,
    "32 sektory po 512 B powinny dawac 16 KiB"
);

#endif /* __cplusplus */
