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
 * 3.1 BITY STATUSU I CAPABILITIES
 * ========================================================================= */

#define PCI_STATUS_CAP_LIST       UINT16_C(0x0010)
#define PCI_CAP_POINTER_OFFSET    UINT8_C(0x34)

#define PCI_CAP_ID_MSI            UINT8_C(0x05)
#define PCI_CAP_ID_VNDR           UINT8_C(0x09)
#define PCI_CAP_ID_MSIX           UINT8_C(0x11)

/* MSI Control bits */
#define PCI_MSI_CTRL_ENABLE       UINT16_C(0x0001)
#define PCI_MSI_CTRL_MMC_MASK     UINT16_C(0x000E)
#define PCI_MSI_CTRL_MMC_SHIFT    1U
#define PCI_MSI_CTRL_MME_MASK     UINT16_C(0x0070)
#define PCI_MSI_CTRL_MME_SHIFT    4U
#define PCI_MSI_CTRL_64BIT        UINT16_C(0x0080)
#define PCI_MSI_CTRL_PVM          UINT16_C(0x0100)

/* MSI-X Control bits */
#define PCI_MSIX_CTRL_TABLE_SIZE  UINT16_C(0x07FF)
#define PCI_MSIX_CTRL_FUNC_MASK   UINT16_C(0x4000)
#define PCI_MSIX_CTRL_ENABLE      UINT16_C(0x8000)

/* MSI-X Table / PBA offsets and BIR masks */
#define PCI_MSIX_BIR_MASK         UINT32_C(0x00000007)
#define PCI_MSIX_OFFSET_MASK      UINT32_C(0xFFFFFFF8)

/* MSI-X Vector Control */
#define PCI_MSIX_VECTOR_CTRL_MASK UINT32_C(0x00000001)

#define VIRTIO_MSI_NO_VECTOR      UINT16_C(0xFFFF)

#pragma pack(push, 1)
struct PciMsixTableEntry {
    volatile uint32_t msg_addr_low;
    volatile uint32_t msg_addr_high;
    volatile uint32_t msg_data;
    volatile uint32_t vector_ctrl;
};
#pragma pack(pop)

struct PciMsiCapability {
    uint8_t  cap_offset;
    uint16_t msg_control;
    bool     is_64bit;
    bool     has_pvm;
    uint8_t  multiple_msg_capable;
};

struct PciMsixCapability {
    uint8_t  cap_offset;
    uint16_t msg_control;
    uint16_t table_size; // 1..2048
    uint8_t  table_bir;  // 0..5
    uint32_t table_offset; // 8-byte aligned offset in BAR
    uint8_t  pba_bir;    // 0..5
    uint32_t pba_offset;   // 8-byte aligned offset in BAR
};

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

/*
 * Wyszukuje wpis w lancuchu PCI capabilities. Zwraca offset w config space (0x40..0xFC)
 * lub 0 w przypadku braku / petli / uszkodzonego lancucha.
 */
uint8_t pci_find_capability(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id);

/*
 * Parsuje capability MSI (ID 0x05) z weryfikacja granic.
 */
bool pci_find_msi(uint8_t bus, uint8_t slot, uint8_t func, PciMsiCapability* out_msi);

/*
 * Parsuje capability MSI-X (ID 0x11) z weryfikacja granic, BIR i rozmiaru tabeli.
 */
bool pci_find_msix(uint8_t bus, uint8_t slot, uint8_t func, PciMsixCapability* out_msix);

/*
 * Weryfikuje czy tabela MSI-X miesci sie w zadeklarowanym BAR (ochrona przed overflow).
 */
bool pci_msix_validate_bar_bounds(const PciMsixCapability* msix, uint64_t table_bar_size, uint64_t pba_bar_size);

/*
 * Wlacza MSI dla funkcji PCI:
 *  - blokuje INTx (Command bit 10),
 *  - wylacza MSI-X,
 *  - programuje adres (APIC ID) i wektor (32-bit lub 64-bit),
 *  - odmaskowuje (jesli PVM obecne),
 *  - ustawia bit MSI Enable.
 */
bool pci_msi_enable(uint8_t bus, uint8_t slot, uint8_t func,
                    const PciMsiCapability* msi, uint8_t apic_id, uint8_t vector);

/*
 * Wylacza MSI.
 */
void pci_msi_disable(uint8_t bus, uint8_t slot, uint8_t func, const PciMsiCapability* msi);

/*
 * Maskuje/odmaskowuje wektor MSI (jesli PVM jest obslugiwane).
 */
void pci_msi_set_mask(uint8_t bus, uint8_t slot, uint8_t func, const PciMsiCapability* msi, bool mask);

/*
 * Wlacza MSI-X dla funkcji PCI:
 *  - blokuje INTx,
 *  - wylacza MSI,
 *  - czysci Function Mask i wlacza MSI-X Enable.
 */
bool pci_msix_enable(uint8_t bus, uint8_t slot, uint8_t func, const PciMsixCapability* msix);

/*
 * Wylacza MSI-X dla funkcji PCI (ustawia Function Mask i czysci MSI-X Enable).
 */
void pci_msix_disable(uint8_t bus, uint8_t slot, uint8_t func, const PciMsixCapability* msix);

/*
 * Bezpiecznie programuje wpis w tabeli MSI-X:
 *  - maskuje wpis (Vector Control bit 0 = 1),
 *  - bariera pamieci,
 *  - zapis adresu i wektora,
 *  - bariera pamieci,
 *  - unmask (jesli masked == false), bariera pamieci.
 */
bool pci_msix_program_entry(volatile PciMsixTableEntry* table, uint16_t table_size,
                            uint16_t entry_index, uint8_t apic_id, uint8_t vector, bool masked);

/*
 * Maskuje wpis w tabeli MSI-X.
 */
bool pci_msix_mask_entry(volatile PciMsixTableEntry* table, uint16_t table_size, uint16_t entry_index);

/*
 * Odmaskowuje wpis w tabeli MSI-X.
 */
bool pci_msix_unmask_entry(volatile PciMsixTableEntry* table, uint16_t table_size, uint16_t entry_index);

/*
 * Sprawdza czy dany wpis w tabeli MSI-X jest zamaskowany.
 */
bool pci_msix_is_entry_masked(volatile const PciMsixTableEntry* table, uint16_t table_size, uint16_t entry_index);

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
