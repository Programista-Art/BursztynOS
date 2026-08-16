/*
 * Bursztyn OS - PCI
 *
 * Publiczny interfejs klasycznego PCI Configuration Mechanism #1.
 *
 * Implementacja znajduje sie w pci.cpp i korzysta z portow:
 *
 *   0xCF8 - CONFIG_ADDRESS
 *   0xCFC - CONFIG_DATA
 *
 * Publiczne funkcje zachowuja C linkage, aby sterowniki C/C++ mogly
 * korzystac z jednego, centralnego dostepu do przestrzeni konfiguracyjnej.
 *
 * WAZNE:
 * Nie nalezy implementowac lokalnych kopii pci_odczytaj_dword() ani
 * pci_zapisz_dword() w sterownikach. Dostep przez 0xCF8/0xCFC musi byc
 * serializowany w jednym miejscu, poniewaz CONFIG_ADDRESS jest wspolnym
 * stanem sprzetowym.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. GRANICE PCI CONFIGURATION MECHANISM #1
 * ========================================================================= */

#define PCI_LICZBA_MAGISTRAL UINT16_C(256)
#define PCI_LICZBA_SLOTOW    UINT8_C(32)
#define PCI_LICZBA_FUNKCJI   UINT8_C(8)

#define PCI_MAKS_SLOT UINT8_C(31)
#define PCI_MAKS_FUNC UINT8_C(7)

/*
 * Legacy CF8/CFC udostepnia pierwsze 256 bajtow konfiguracji funkcji PCI.
 * Publiczne ABI korzysta z uint8_t offset, wiec zakres jest naturalnie
 * ograniczony do 0x00..0xFF.
 */
#define PCI_ROZMIAR_CONFIG_LEGACY UINT16_C(256)

/*
 * Odczyt Vendor ID == 0xFFFF oznacza brak funkcji PCI.
 */
#define PCI_VENDOR_BRAK UINT16_C(0xFFFF)

/* =========================================================================
 * 2. STANDARDOWE OFFSETY KONFIGURACJI
 * ========================================================================= */

#define PCI_OFFSET_VENDOR_DEVICE  UINT8_C(0x00)
#define PCI_OFFSET_COMMAND_STATUS UINT8_C(0x04)
#define PCI_OFFSET_CLASS_INFO     UINT8_C(0x08)
#define PCI_OFFSET_HEADER_INFO    UINT8_C(0x0C)

#define PCI_OFFSET_BAR0 UINT8_C(0x10)
#define PCI_OFFSET_BAR1 UINT8_C(0x14)
#define PCI_OFFSET_BAR2 UINT8_C(0x18)
#define PCI_OFFSET_BAR3 UINT8_C(0x1C)
#define PCI_OFFSET_BAR4 UINT8_C(0x20)
#define PCI_OFFSET_BAR5 UINT8_C(0x24)

/* =========================================================================
 * 3. BITY PCI COMMAND
 * ========================================================================= */

#define PCI_COMMAND_IO_SPACE      UINT16_C(0x0001)
#define PCI_COMMAND_MEMORY_SPACE  UINT16_C(0x0002)
#define PCI_COMMAND_BUS_MASTER    UINT16_C(0x0004)
#define PCI_COMMAND_SPECIAL_CYCLE UINT16_C(0x0008)
#define PCI_COMMAND_MWI           UINT16_C(0x0010)
#define PCI_COMMAND_VGA_SNOOP     UINT16_C(0x0020)
#define PCI_COMMAND_PARITY        UINT16_C(0x0040)
#define PCI_COMMAND_SERR          UINT16_C(0x0100)
#define PCI_COMMAND_FAST_B2B      UINT16_C(0x0200)
#define PCI_COMMAND_INT_DISABLE   UINT16_C(0x0400)

/* =========================================================================
 * 4. PUBLICZNE ABI
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Odczytuje 32 bity z legacy PCI config space.
 *
 * bus:
 *   0..255
 *
 * slot:
 *   0..31
 *
 * func:
 *   0..7
 *
 * offset:
 *   dowolny bajt 0x00..0xFF; implementacja wyrownuje go do granicy
 *   DWORD przez offset & 0xFC.
 *
 * Dla nieprawidlowego slot/func poprawiony pci.cpp zwraca UINT32_MAX.
 *
 * Uwaga:
 * Sam wynik UINT32_MAX nie zawsze oznacza blad calego DWORD. Do wykrywania
 * obecnosci funkcji nalezy sprawdzac Vendor ID == PCI_VENDOR_BRAK.
 */
uint32_t pci_odczytaj_dword(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset
);

/*
 * Zapisuje 32 bity do legacy PCI config space.
 *
 * Nieprawidlowy slot/func jest ignorowany.
 *
 * Zapis do rejestrow PCI powinien byc wykonywany jako read-modify-write,
 * jezeli tylko czesc bitow ma zostac zmieniona.
 */
void pci_zapisz_dword(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset,
    uint32_t data
);

/*
 * Skanuje PCI 0..255 / 0..31 / func 0..7 dla urzadzen multifunction.
 * Raport jest zapisywany przez implementacje do:
 *
 *   /logi/pci.txt
 */
void skanuj_magistrale_pci();

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 * 5. BEZPIECZNE HELPERY HEADER-ONLY
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Waliduje BDF bez wykonywania I/O.
 */
inline constexpr bool pci_bdf_w_zakresie(
    uint8_t slot,
    uint8_t func
) noexcept {
    return
        slot <= PCI_MAKS_SLOT &&
        func <= PCI_MAKS_FUNC;
}

/*
 * Odczyt pojedynczego bajtu z config space.
 *
 * Funkcja wykorzystuje centralny pci_odczytaj_dword(), wiec zachowuje
 * blokade CF8/CFC z pci.cpp.
 */
inline uint8_t pci_odczytaj_byte(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset
) noexcept {
    const uint32_t dword =
        pci_odczytaj_dword(
            bus,
            slot,
            func,
            offset
        );

    const uint32_t przesuniecie =
        static_cast<uint32_t>(
            offset &
            UINT8_C(0x03)
        ) * 8U;

    return
        static_cast<uint8_t>(
            (dword >>
             przesuniecie) &
            UINT32_C(0xFF)
        );
}

/*
 * Odczyt 16-bitowego pola.
 *
 * Offset musi byc wyrownany do 2 bajtow. Przy nieparzystym offsecie
 * zwracamy 0xFFFF zamiast skladac slowo przecinajace granice DWORD.
 */
inline uint16_t pci_odczytaj_word(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t offset
) noexcept {
    if ((offset &
         UINT8_C(0x01)) != 0) {

        return
            UINT16_C(0xFFFF);
    }

    const uint32_t dword =
        pci_odczytaj_dword(
            bus,
            slot,
            func,
            offset
        );

    const uint32_t przesuniecie =
        static_cast<uint32_t>(
            offset &
            UINT8_C(0x02)
        ) * 8U;

    return
        static_cast<uint16_t>(
            (dword >>
             przesuniecie) &
            UINT32_C(0xFFFF)
        );
}

/*
 * Standardowe pola identyfikacyjne.
 */
inline uint16_t pci_vendor_id(
    uint8_t bus,
    uint8_t slot,
    uint8_t func
) noexcept {
    return
        static_cast<uint16_t>(
            pci_odczytaj_dword(
                bus,
                slot,
                func,
                PCI_OFFSET_VENDOR_DEVICE
            ) &
            UINT32_C(0xFFFF)
        );
}

inline uint16_t pci_device_id(
    uint8_t bus,
    uint8_t slot,
    uint8_t func
) noexcept {
    return
        static_cast<uint16_t>(
            pci_odczytaj_dword(
                bus,
                slot,
                func,
                PCI_OFFSET_VENDOR_DEVICE
            ) >>
            16
        );
}

inline bool pci_funkcja_obecna(
    uint8_t bus,
    uint8_t slot,
    uint8_t func
) noexcept {
    return
        pci_bdf_w_zakresie(
            slot,
            func
        ) &&
        pci_vendor_id(
            bus,
            slot,
            func
        ) !=
            PCI_VENDOR_BRAK;
}

/*
 * Wygodne dekodowanie DWORD 0x08:
 *
 *   bits 31..24 = Base Class
 *   bits 23..16 = Subclass
 *   bits 15..8  = Programming Interface
 *   bits 7..0   = Revision ID
 */
inline uint8_t pci_klasa(
    uint32_t class_info
) noexcept {
    return
        static_cast<uint8_t>(
            class_info >>
            24
        );
}

inline uint8_t pci_podklasa(
    uint32_t class_info
) noexcept {
    return
        static_cast<uint8_t>(
            (class_info >>
             16) &
            UINT32_C(0xFF)
        );
}

inline uint8_t pci_prog_if(
    uint32_t class_info
) noexcept {
    return
        static_cast<uint8_t>(
            (class_info >>
             8) &
            UINT32_C(0xFF)
        );
}

inline uint8_t pci_revision(
    uint32_t class_info
) noexcept {
    return
        static_cast<uint8_t>(
            class_info &
            UINT32_C(0xFF)
        );
}

/*
 * Header Type znajduje sie w byte 2 DWORD 0x0C.
 * Bit 7 oznacza urzadzenie multifunction.
 */
inline uint8_t pci_typ_naglowka(
    uint32_t header_info
) noexcept {
    return
        static_cast<uint8_t>(
            (header_info >>
             16) &
            UINT32_C(0xFF)
        );
}

inline constexpr bool pci_jest_multifunction(
    uint8_t header_type
) noexcept {
    return
        (header_type &
         UINT8_C(0x80)) != 0;
}

/*
 * Minimalna kontrola compile-time ABI typow.
 */
static_assert(
    sizeof(uint8_t) == 1,
    "PCI wymaga 8-bitowego uint8_t"
);

static_assert(
    sizeof(uint16_t) == 2,
    "PCI wymaga 16-bitowego uint16_t"
);

static_assert(
    sizeof(uint32_t) == 4,
    "PCI wymaga 32-bitowego uint32_t"
);

#endif /* __cplusplus */
