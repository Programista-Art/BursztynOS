/*
 * Bursztyn OS - Intel 82540EM / QEMU E1000
 *
 * Sterownik pollingowy dla PCI 8086:100E.
 *
 * Publiczne ABI pozostaje zgodne z e1000.h:
 *
 *   inicjalizuj_e1000()
 *   e1000_wyslij_pakiet()
 *   e1000_obsluz_odbior()
 *   pobierz_mac_adres()
 *
 * Najwazniejsze zasady bezpieczenstwa:
 *
 *   - dostep PCI idzie przez centralny pci.cpp,
 *   - obslugiwane sa funkcje PCI 0..7 dla urzadzen multifunction,
 *   - BAR0 jest walidowany jako MMIO i mapowane jest cale 128 KiB,
 *   - MMIO jest mapowane z PCD/PWT,
 *   - karta jest resetowana z timeoutem,
 *   - przerwania E1000 sa maskowane, bo sterownik pracuje pollingowo,
 *   - RX/TX descriptor rings i bufory pochodza z PMM,
 *   - TX nie alokuje i nie zwalnia ramki dla kazdego pakietu,
 *   - wszystkie oczekiwania sprzetowe maja timeout,
 *   - RX sprawdza DD, EOP, errors oraz dlugosc przed przekazaniem ramki,
 *   - TX sprawdza maksymalny rozmiar ramki i DD deskryptora,
 *   - dostep RX/TX ma zabezpieczenie przed reentrancy,
 *   - nie zapisujemy z powrotem bitow PCI Status typu W1C.
 *
 * Ograniczenia:
 *
 *   - ten plik swiadomie obsluguje tylko Intel 82540EM (8086:100E),
 *   - brak MSI/MSI-X i IRQ; polling jest celowy,
 *   - brak jumbo frames,
 *   - brak offloadow checksum/TSO,
 *   - obecny PMM/VMM Bursztyna zaklada identity-map DMA ponizej 4 GiB,
 *   - ZmapujStrone() zwraca void, wiec nie da sie jeszcze sprawdzic
 *     programowo nieudanego mapowania MMIO.
 *
 * Przy przyszlym HHDM nalezy zastapic dma_wirtualny_z_fizycznego()
 * centralnym helperem phys->virt.
 */

#include "e1000.h"
#include "pamiec.h"
#include "pci.h"
#include "siec.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. API ZEWNĘTRZNE
 * ========================================================================= */

void wypisz_log(
    const char* tekst
);

/* =========================================================================
 * 2. STALE SPRZETOWE
 * ========================================================================= */

namespace {

constexpr uint16_t INTEL_VENDOR_ID =
    0x8086U;

constexpr uint16_t E1000_82540EM_DEVICE_ID =
    0x100EU;

constexpr uint8_t PCI_KLASA_SIEC =
    0x02U;

constexpr uint8_t PCI_PODKLASA_ETHERNET =
    0x00U;

constexpr uint32_t PCI_BAR_IO =
    1U << 0;

constexpr uint32_t PCI_BAR_MEM_TYPE_MASK =
    0x6U;

constexpr uint32_t PCI_BAR_MEM_TYPE_32 =
    0x0U;

constexpr uint32_t PCI_BAR_MEM_TYPE_64 =
    0x4U;

constexpr uint32_t PCI_BAR_MEM_ADDR_MASK =
    0xFFFFFFF0U;

constexpr uint64_t E1000_MMIO_ROZMIAR =
    UINT64_C(128) *
    1024U;

/* Rejestry globalne */
constexpr uint32_t REG_CTRL =
    0x0000U;

constexpr uint32_t REG_STATUS =
    0x0008U;

constexpr uint32_t REG_EERD =
    0x0014U;

constexpr uint32_t REG_ICR =
    0x00C0U;

constexpr uint32_t REG_IMS =
    0x00D0U;

constexpr uint32_t REG_IMC =
    0x00D8U;

constexpr uint32_t REG_RCTL =
    0x0100U;

constexpr uint32_t REG_TCTL =
    0x0400U;

constexpr uint32_t REG_TIPG =
    0x0410U;

/* RX ring */
constexpr uint32_t REG_RDBAL =
    0x2800U;

constexpr uint32_t REG_RDBAH =
    0x2804U;

constexpr uint32_t REG_RDLEN =
    0x2808U;

constexpr uint32_t REG_RDH =
    0x2810U;

constexpr uint32_t REG_RDT =
    0x2818U;

/* TX ring */
constexpr uint32_t REG_TDBAL =
    0x3800U;

constexpr uint32_t REG_TDBAH =
    0x3804U;

constexpr uint32_t REG_TDLEN =
    0x3808U;

constexpr uint32_t REG_TDH =
    0x3810U;

constexpr uint32_t REG_TDT =
    0x3818U;

/* Multicast / Receive Address */
constexpr uint32_t REG_MTA_BASE =
    0x5200U;

constexpr uint32_t REG_RAL0 =
    0x5400U;

constexpr uint32_t REG_RAH0 =
    0x5404U;

/* CTRL */
constexpr uint32_t CTRL_ASDE =
    1U << 5;

constexpr uint32_t CTRL_SLU =
    1U << 6;

constexpr uint32_t CTRL_RST =
    1U << 26;

/* STATUS */
constexpr uint32_t STATUS_LU =
    1U << 1;

/* RAH */
constexpr uint32_t RAH_AV =
    1U << 31;

/* EEPROM EERD dla 82540EM */
constexpr uint32_t EERD_START =
    1U << 0;

constexpr uint32_t EERD_DONE =
    1U << 4;

constexpr uint32_t EERD_ADDR_SHIFT =
    8U;

constexpr uint32_t EERD_DATA_SHIFT =
    16U;

/* RCTL */
constexpr uint32_t RCTL_EN =
    1U << 1;

constexpr uint32_t RCTL_BAM =
    1U << 15;

constexpr uint32_t RCTL_SECRC =
    1U << 26;

/*
 * BSIZE=00, BSEX=0 oznacza 2048 bajtow.
 */
constexpr uint32_t RCTL_BSIZE_2048 =
    0U;

/* TCTL */
constexpr uint32_t TCTL_EN =
    1U << 1;

constexpr uint32_t TCTL_PSP =
    1U << 3;

constexpr uint32_t TCTL_CT_SHIFT =
    4U;

constexpr uint32_t TCTL_COLD_SHIFT =
    12U;

constexpr uint32_t TCTL_RTLC =
    1U << 24;

constexpr uint32_t TCTL_CT =
    0x0FU;

constexpr uint32_t TCTL_COLD =
    0x40U;

/* TIPG - wartosci zalecane dla gigabit/full duplex w klasycznym E1000 */
constexpr uint32_t TIPG_IPGT =
    10U;

constexpr uint32_t TIPG_IPGR1 =
    8U;

constexpr uint32_t TIPG_IPGR2 =
    6U;

/* RX descriptor status */
constexpr uint8_t RXD_STAT_DD =
    1U << 0;

constexpr uint8_t RXD_STAT_EOP =
    1U << 1;

/* TX descriptor command/status */
constexpr uint8_t TXD_CMD_EOP =
    1U << 0;

constexpr uint8_t TXD_CMD_IFCS =
    1U << 1;

constexpr uint8_t TXD_CMD_RS =
    1U << 3;

constexpr uint8_t TXD_STAT_DD =
    1U << 0;

/* Ring sizes */
constexpr uint32_t E1000_NUM_RX_DESC =
    32U;

constexpr uint32_t E1000_NUM_TX_DESC =
    8U;

constexpr uint32_t E1000_RX_BUFOR_BAJTOW =
    2048U;

constexpr uint32_t E1000_TX_BUFOR_BAJTOW =
    2048U;

constexpr uint32_t E1000_MIN_RAMKA_ETH =
    14U;

constexpr uint32_t E1000_MAX_RAMKA_ETH =
    1518U;

/*
 * RDLEN/TDLEN musza byc wielokrotnoscia 128 bajtow.
 */
constexpr uint32_t RX_RING_BAJTOW =
    E1000_NUM_RX_DESC *
    16U;

constexpr uint32_t TX_RING_BAJTOW =
    E1000_NUM_TX_DESC *
    16U;

static_assert(
    RX_RING_BAJTOW == 512U,
    "RX ring powinien zajmowac 512 bajtow"
);

static_assert(
    TX_RING_BAJTOW == 128U,
    "TX ring powinien zajmowac 128 bajtow"
);

static_assert(
    (RX_RING_BAJTOW &
     0x7FU) == 0,
    "RDLEN musi byc wielokrotnoscia 128 bajtow"
);

static_assert(
    (TX_RING_BAJTOW &
     0x7FU) == 0,
    "TDLEN musi byc wielokrotnoscia 128 bajtow"
);

/* Timeouty pollingowe */
constexpr uint32_t TIMEOUT_RESET =
    5U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_EEPROM =
    500U * 1000U;

constexpr uint32_t TIMEOUT_LINK =
    2U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_TX_DESC =
    2U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_TX_DONE =
    5U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_TX_LOCK =
    100U * 1000U;

/* =========================================================================
 * 3. DESKRYPTORY DMA
 * ========================================================================= */

struct E1000RxDesc {
    uint64_t adres;

    uint16_t dlugosc;
    uint16_t suma_kontrolna;

    uint8_t status;
    uint8_t bledy;

    uint16_t specjalne;
} __attribute__((packed));

struct E1000TxDesc {
    uint64_t adres;

    uint16_t dlugosc;

    uint8_t cso;
    uint8_t komenda;

    uint8_t status;
    uint8_t css;

    uint16_t specjalne;
} __attribute__((packed));

static_assert(
    sizeof(E1000RxDesc) == 16U,
    "RX descriptor E1000 musi miec 16 bajtow"
);

static_assert(
    sizeof(E1000TxDesc) == 16U,
    "TX descriptor E1000 musi miec 16 bajtow"
);

static_assert(
    offsetof(E1000RxDesc, status) == 12U,
    "Nieprawidlowy layout RX descriptor"
);

static_assert(
    offsetof(E1000TxDesc, komenda) == 11U,
    "Nieprawidlowy layout TX descriptor"
);

static_assert(
    offsetof(E1000TxDesc, status) == 12U,
    "Nieprawidlowy layout TX descriptor"
);

/* =========================================================================
 * 4. STAN STEROWNIKA
 * ========================================================================= */

struct BdfPCI {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
};

volatile uint32_t* e1000_mmio_baza =
    nullptr;

uint64_t e1000_mmio_phys =
    0;

BdfPCI e1000_bdf{};

uint8_t mac_adres[6] = {};

void* rx_ring_phys =
    nullptr;

void* tx_ring_phys =
    nullptr;

volatile E1000RxDesc* rx_descs =
    nullptr;

volatile E1000TxDesc* tx_descs =
    nullptr;

void* rx_bufory_phys[
    E1000_NUM_RX_DESC
] = {};

void* tx_bufory_phys[
    E1000_NUM_TX_DESC
] = {};

uint16_t rx_aktualny =
    0;

uint16_t tx_aktualny =
    0;

bool e1000_gotowy =
    false;

bool inicjalizacja_w_toku =
    false;

bool blokada_tx =
    false;

bool blokada_rx =
    false;

/* =========================================================================
 * 5. PROSTE HELPERY
 * ========================================================================= */

void pause_cpu() {
    asm volatile(
        "pause"
        :
        :
        : "memory"
    );
}

void bariera_dma_przed_urzadzeniem() {
    asm volatile(
        "mfence"
        :
        :
        : "memory"
    );
}

void bariera_dma_po_urzadzeniu() {
    asm volatile(
        "mfence"
        :
        :
        : "memory"
    );
}

void wyzeruj(
    void* ptr,
    size_t dlugosc
) {
    if (!ptr) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(
            ptr
        );

    for (size_t i = 0;
         i < dlugosc;
         ++i) {

        p[i] =
            0;
    }
}

void kopiuj(
    void* cel,
    const void* zrodlo,
    size_t dlugosc
) {
    if (!cel ||
        !zrodlo) {

        return;
    }

    uint8_t* d =
        static_cast<uint8_t*>(
            cel
        );

    const uint8_t* s =
        static_cast<const uint8_t*>(
            zrodlo
        );

    for (size_t i = 0;
         i < dlugosc;
         ++i) {

        d[i] =
            s[i];
    }
}

bool mac_poprawny(
    const uint8_t mac[6]
) {
    if (!mac) {
        return false;
    }

    uint8_t or_all =
        0;

    bool wszystkie_ff =
        true;

    for (size_t i = 0;
         i < 6U;
         ++i) {

        or_all |=
            mac[i];

        if (mac[i] !=
            0xFFU) {

            wszystkie_ff =
                false;
        }
    }

    if (or_all == 0 ||
        wszystkie_ff) {

        return false;
    }

    /*
     * Adres interfejsu musi byc unicast.
     */
    if ((mac[0] &
         0x01U) != 0) {

        return false;
    }

    return true;
}

/*
 * Obecny PMM zwraca fizyczny adres, a kernel ma identity-map pierwszych
 * 4 GiB. Ten helper izoluje zalozenie od reszty sterownika.
 */
void* dma_wirtualny_z_fizycznego(
    void* phys
) {
    return phys;
}

bool ramka_dma_dostepna_cpu(
    void* phys
) {
    if (!phys) {
        return false;
    }

    const uint64_t p =
        reinterpret_cast<uint64_t>(
            phys
        );

    if ((p &
         (ROZMIAR_RAMKI_4K - 1U)) != 0) {

        return false;
    }

    /*
     * CPU musi moc dereferencjonowac ramke przez obecny identity map.
     */
    return
        p <=
        UINT32_MAX -
            (ROZMIAR_RAMKI_4K - 1U);
}

/* =========================================================================
 * 6. BEZPIECZNE LOGOWANIE MAC
 * ========================================================================= */

bool dopisz_znak(
    char* bufor,
    size_t pojemnosc,
    size_t* pos,
    char c
) {
    if (!bufor ||
        !pos ||
        *pos + 1U >=
            pojemnosc) {

        return false;
    }

    bufor[
        (*pos)++] =
        c;

    bufor[
        *pos] =
        '\0';

    return true;
}

bool dopisz_tekst(
    char* bufor,
    size_t pojemnosc,
    size_t* pos,
    const char* tekst
) {
    if (!tekst) {
        return false;
    }

    for (size_t i = 0;
         tekst[i] != '\0';
         ++i) {

        if (!dopisz_znak(
                bufor,
                pojemnosc,
                pos,
                tekst[i])) {

            return false;
        }
    }

    return true;
}

void loguj_mac() {
    static constexpr char HEX[] =
        "0123456789ABCDEF";

    char log[80] = {};
    size_t p = 0;

    (void)dopisz_tekst(
        log,
        sizeof(log),
        &p,
        "[E1000] MAC: "
    );

    for (size_t i = 0;
         i < 6U;
         ++i) {

        (void)dopisz_znak(
            log,
            sizeof(log),
            &p,
            HEX[
                (mac_adres[i] >> 4) &
                0x0FU
            ]
        );

        (void)dopisz_znak(
            log,
            sizeof(log),
            &p,
            HEX[
                mac_adres[i] &
                0x0FU
            ]
        );

        if (i + 1U < 6U) {
            (void)dopisz_znak(
                log,
                sizeof(log),
                &p,
                ':'
            );
        }
    }

    wypisz_log(
        log
    );
}

/* =========================================================================
 * 7. MMIO
 * ========================================================================= */

bool offset_mmio_poprawny(
    uint32_t offset
) {
    return
        e1000_mmio_baza != nullptr &&
        (offset &
         0x3U) == 0 &&
        static_cast<uint64_t>(
            offset) +
            sizeof(uint32_t) <=
            E1000_MMIO_ROZMIAR;
}

void zapisz_rejestr(
    uint32_t offset,
    uint32_t wartosc
) {
    if (!offset_mmio_poprawny(
            offset)) {

        return;
    }

    e1000_mmio_baza[
        offset /
        sizeof(uint32_t)
    ] =
        wartosc;

    asm volatile(
        ""
        :
        :
        : "memory"
    );
}

uint32_t czytaj_rejestr(
    uint32_t offset
) {
    if (!offset_mmio_poprawny(
            offset)) {

        return
            UINT32_MAX;
    }

    const uint32_t v =
        e1000_mmio_baza[
            offset /
            sizeof(uint32_t)
        ];

    asm volatile(
        ""
        :
        :
        : "memory"
    );

    return v;
}

void flush_mmio() {
    (void)czytaj_rejestr(
        REG_STATUS
    );
}

/* =========================================================================
 * 8. BLOKADY BEZ DEADLOCKA REENTRANCY
 * ========================================================================= */

bool pobierz_blokade_z_timeoutem(
    bool* blokada,
    uint32_t timeout
) {
    if (!blokada) {
        return false;
    }

    for (uint32_t proba = 0;
         proba <
            timeout;
         ++proba) {

        bool oczekiwane =
            false;

        if (__atomic_compare_exchange_n(
                blokada,
                &oczekiwane,
                true,
                false,
                __ATOMIC_ACQUIRE,
                __ATOMIC_RELAXED)) {

            return true;
        }

        pause_cpu();
    }

    return false;
}

bool sprobuj_pobrac_blokade(
    bool* blokada
) {
    if (!blokada) {
        return false;
    }

    bool oczekiwane =
        false;

    return
        __atomic_compare_exchange_n(
            blokada,
            &oczekiwane,
            true,
            false,
            __ATOMIC_ACQUIRE,
            __ATOMIC_RELAXED
        );
}

void zwolnij_blokade(
    bool* blokada
) {
    if (!blokada) {
        return;
    }

    __atomic_store_n(
        blokada,
        false,
        __ATOMIC_RELEASE
    );
}

class GuardBlokady {
public:
    GuardBlokady(
        bool* blokada,
        uint32_t timeout,
        bool natychmiast
    )
        : blokada_(
              blokada
          ),
          aktywny_(
              natychmiast
                  ? sprobuj_pobrac_blokade(
                        blokada)
                  : pobierz_blokade_z_timeoutem(
                        blokada,
                        timeout)
          ) {
    }

    ~GuardBlokady() {
        if (aktywny_) {
            zwolnij_blokade(
                blokada_
            );
        }
    }

    bool aktywny() const {
        return aktywny_;
    }

    GuardBlokady(
        const GuardBlokady&
    ) = delete;

    GuardBlokady& operator=(
        const GuardBlokady&
    ) = delete;

private:
    bool* blokada_;
    bool aktywny_;
};

/* =========================================================================
 * 9. PCI - WYSZUKIWANIE 82540EM
 * ========================================================================= */

bool znajdz_e1000(
    BdfPCI* wynik
) {
    if (!wynik) {
        return false;
    }

    for (uint16_t bus16 = 0;
         bus16 <
            PCI_LICZBA_MAGISTRAL;
         ++bus16) {

        const uint8_t bus =
            static_cast<uint8_t>(
                bus16
            );

        for (uint8_t slot = 0;
             slot <=
                PCI_MAKS_SLOT;
             ++slot) {

            const uint16_t vendor0 =
                pci_vendor_id(
                    bus,
                    slot,
                    0
                );

            if (vendor0 ==
                PCI_VENDOR_BRAK) {

                continue;
            }

            const uint32_t header_info =
                pci_odczytaj_dword(
                    bus,
                    slot,
                    0,
                    PCI_OFFSET_HEADER_INFO
                );

            const uint8_t maks_func =
                pci_jest_multifunction(
                    pci_typ_naglowka(
                        header_info
                    )
                )
                    ? PCI_MAKS_FUNC
                    : 0U;

            for (uint8_t func = 0;
                 func <=
                    maks_func;
                 ++func) {

                const uint16_t vendor =
                    pci_vendor_id(
                        bus,
                        slot,
                        func
                    );

                if (vendor ==
                    PCI_VENDOR_BRAK) {

                    continue;
                }

                const uint16_t device =
                    pci_device_id(
                        bus,
                        slot,
                        func
                    );

                if (vendor !=
                        INTEL_VENDOR_ID ||
                    device !=
                        E1000_82540EM_DEVICE_ID) {

                    continue;
                }

                const uint32_t class_info =
                    pci_odczytaj_dword(
                        bus,
                        slot,
                        func,
                        PCI_OFFSET_CLASS_INFO
                    );

                if (pci_klasa(
                        class_info) !=
                        PCI_KLASA_SIEC ||
                    pci_podklasa(
                        class_info) !=
                        PCI_PODKLASA_ETHERNET) {

                    continue;
                }

                wynik->bus =
                    bus;

                wynik->slot =
                    slot;

                wynik->func =
                    func;

                return true;
            }
        }
    }

    return false;
}

/* =========================================================================
 * 10. PCI BAR0
 * ========================================================================= */

bool pobierz_bar0_mmio(
    const BdfPCI& bdf,
    uint64_t* adres
) {
    if (!adres) {
        return false;
    }

    const uint32_t bar0 =
        pci_odczytaj_dword(
            bdf.bus,
            bdf.slot,
            bdf.func,
            PCI_OFFSET_BAR0
        );

    if (bar0 == 0 ||
        bar0 ==
            UINT32_MAX ||
        (bar0 &
         PCI_BAR_IO) != 0) {

        return false;
    }

    const uint32_t typ =
        bar0 &
        PCI_BAR_MEM_TYPE_MASK;

    if (typ ==
        PCI_BAR_MEM_TYPE_32) {

        *adres =
            static_cast<uint64_t>(
                bar0 &
                PCI_BAR_MEM_ADDR_MASK
            );

        return
            *adres != 0;
    }

    if (typ ==
        PCI_BAR_MEM_TYPE_64) {

        const uint32_t bar1 =
            pci_odczytaj_dword(
                bdf.bus,
                bdf.slot,
                bdf.func,
                PCI_OFFSET_BAR1
            );

        *adres =
            (static_cast<uint64_t>(
                 bar1) << 32) |
            static_cast<uint64_t>(
                bar0 &
                PCI_BAR_MEM_ADDR_MASK
            );

        return
            *adres != 0;
    }

    /*
     * Legacy "below 1 MiB" memory BAR nie jest obslugiwany.
     */
    return false;
}

/* =========================================================================
 * 11. PCI COMMAND
 * ========================================================================= */

bool wlacz_pci_dla_e1000(
    const BdfPCI& bdf
) {
    const uint32_t command_status =
        pci_odczytaj_dword(
            bdf.bus,
            bdf.slot,
            bdf.func,
            PCI_OFFSET_COMMAND_STATUS
        );

    uint16_t command =
        static_cast<uint16_t>(
            command_status &
            0xFFFFU
        );

    command =
        static_cast<uint16_t>(
            command |
            PCI_COMMAND_MEMORY_SPACE |
            PCI_COMMAND_BUS_MASTER |
            PCI_COMMAND_INT_DISABLE
        );

    /*
     * Gorna polowa DWORD to Status z bitami W1C.
     * Wysylamy tam zera, zeby niczego przypadkowo nie wyczyscic.
     */
    pci_zapisz_dword(
        bdf.bus,
        bdf.slot,
        bdf.func,
        PCI_OFFSET_COMMAND_STATUS,
        static_cast<uint32_t>(
            command
        )
    );

    const uint16_t po =
        static_cast<uint16_t>(
            pci_odczytaj_dword(
                bdf.bus,
                bdf.slot,
                bdf.func,
                PCI_OFFSET_COMMAND_STATUS
            ) &
            0xFFFFU
        );

    const uint16_t wymagane =
        static_cast<uint16_t>(
            PCI_COMMAND_MEMORY_SPACE |
            PCI_COMMAND_BUS_MASTER
        );

    return
        (po &
         wymagane) ==
        wymagane;
}

void wylacz_pci_busmaster(
    const BdfPCI& bdf
) {
    const uint32_t command_status =
        pci_odczytaj_dword(
            bdf.bus,
            bdf.slot,
            bdf.func,
            PCI_OFFSET_COMMAND_STATUS
        );

    uint16_t command =
        static_cast<uint16_t>(
            command_status &
            0xFFFFU
        );

    command =
        static_cast<uint16_t>(
            command &
            ~PCI_COMMAND_BUS_MASTER
        );

    pci_zapisz_dword(
        bdf.bus,
        bdf.slot,
        bdf.func,
        PCI_OFFSET_COMMAND_STATUS,
        static_cast<uint32_t>(
            command
        )
    );
}

/* =========================================================================
 * 12. MAPOWANIE MMIO
 * ========================================================================= */

bool mapuj_mmio(
    uint64_t mmio
) {
    if (mmio == 0 ||
        mmio >
            UINT64_MAX -
            (E1000_MMIO_ROZMIAR - 1U)) {

        return false;
    }

    /*
     * Identity-map jako supervisor RW + uncached.
     */
    const uint64_t start =
        mmio &
        ~UINT64_C(0xFFF);

    const uint64_t koniec =
        (mmio +
         E1000_MMIO_ROZMIAR -
         1U) &
        ~UINT64_C(0xFFF);

    const uint32_t flagi =
        VMM_FLAGA_OBECNA |
        VMM_FLAGA_ZAPIS |
        VMM_FLAGA_PWT |
        VMM_FLAGA_PCD;

    for (uint64_t strona =
             start;
         strona <=
             koniec;
         strona +=
             ROZMIAR_RAMKI_4K) {

        ZmapujStrone(
            reinterpret_cast<void*>(
                strona
            ),
            reinterpret_cast<void*>(
                strona
            ),
            flagi
        );

        if (strona >
            UINT64_MAX -
                ROZMIAR_RAMKI_4K) {

            break;
        }
    }

    e1000_mmio_phys =
        mmio;

    e1000_mmio_baza =
        reinterpret_cast<volatile uint32_t*>(
            mmio
        );

    /*
     * Nie mamy bool z ZmapujStrone(). Pierwszy bezpieczny read MMIO jest
     * obecnie jedynym praktycznym testem, ze mapping istnieje.
     */
    const uint32_t status =
        czytaj_rejestr(
            REG_STATUS
        );

    return
        status !=
        UINT32_MAX;
}

/* =========================================================================
 * 13. RESET KARTY
 * ========================================================================= */

void maskuj_przerwania_e1000() {
    zapisz_rejestr(
        REG_IMC,
        UINT32_MAX
    );

    flush_mmio();

    /*
     * Read ICR kasuje oczekujace przyczyny przerwan.
     */
    (void)czytaj_rejestr(
        REG_ICR
    );
}

bool resetuj_e1000() {
    if (!e1000_mmio_baza) {
        return false;
    }

    maskuj_przerwania_e1000();

    zapisz_rejestr(
        REG_RCTL,
        0
    );

    zapisz_rejestr(
        REG_TCTL,
        0
    );

    flush_mmio();

    const uint32_t ctrl =
        czytaj_rejestr(
            REG_CTRL
        );

    if (ctrl ==
        UINT32_MAX) {

        return false;
    }

    zapisz_rejestr(
        REG_CTRL,
        ctrl |
        CTRL_RST
    );

    flush_mmio();

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_RESET;
         ++proba) {

        if ((czytaj_rejestr(
                 REG_CTRL) &
             CTRL_RST) == 0) {

            maskuj_przerwania_e1000();

            return true;
        }

        pause_cpu();
    }

    wypisz_log(
        "[E1000] Timeout resetu kontrolera."
    );

    return false;
}

/* =========================================================================
 * 14. MAC - RAL/RAH I EEPROM
 * ========================================================================= */

bool odczytaj_mac_z_rejestrow() {
    const uint32_t low =
        czytaj_rejestr(
            REG_RAL0
        );

    const uint32_t high =
        czytaj_rejestr(
            REG_RAH0
        );

    if (low ==
            UINT32_MAX ||
        high ==
            UINT32_MAX) {

        return false;
    }

    mac_adres[0] =
        static_cast<uint8_t>(
            low &
            0xFFU
        );

    mac_adres[1] =
        static_cast<uint8_t>(
            (low >> 8) &
            0xFFU
        );

    mac_adres[2] =
        static_cast<uint8_t>(
            (low >> 16) &
            0xFFU
        );

    mac_adres[3] =
        static_cast<uint8_t>(
            (low >> 24) &
            0xFFU
        );

    mac_adres[4] =
        static_cast<uint8_t>(
            high &
            0xFFU
        );

    mac_adres[5] =
        static_cast<uint8_t>(
            (high >> 8) &
            0xFFU
        );

    return
        mac_poprawny(
            mac_adres
        );
}

bool eeprom_odczytaj_word(
    uint8_t adres,
    uint16_t* wynik
) {
    if (!wynik) {
        return false;
    }

    zapisz_rejestr(
        REG_EERD,
        EERD_START |
        (static_cast<uint32_t>(
             adres) <<
         EERD_ADDR_SHIFT)
    );

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_EEPROM;
         ++proba) {

        const uint32_t v =
            czytaj_rejestr(
                REG_EERD
            );

        if ((v &
             EERD_DONE) != 0) {

            *wynik =
                static_cast<uint16_t>(
                    v >>
                    EERD_DATA_SHIFT
                );

            return true;
        }

        pause_cpu();
    }

    return false;
}

bool odczytaj_mac_z_eeprom() {
    uint16_t slowa[3] = {};

    for (uint8_t i = 0;
         i < 3U;
         ++i) {

        if (!eeprom_odczytaj_word(
                i,
                &slowa[i])) {

            return false;
        }
    }

    mac_adres[0] =
        static_cast<uint8_t>(
            slowa[0] &
            0xFFU
        );

    mac_adres[1] =
        static_cast<uint8_t>(
            slowa[0] >>
            8
        );

    mac_adres[2] =
        static_cast<uint8_t>(
            slowa[1] &
            0xFFU
        );

    mac_adres[3] =
        static_cast<uint8_t>(
            slowa[1] >>
            8
        );

    mac_adres[4] =
        static_cast<uint8_t>(
            slowa[2] &
            0xFFU
        );

    mac_adres[5] =
        static_cast<uint8_t>(
            slowa[2] >>
            8
        );

    return
        mac_poprawny(
            mac_adres
        );
}

void zaprogramuj_mac() {
    const uint32_t low =
        static_cast<uint32_t>(
            mac_adres[0]
        ) |
        (static_cast<uint32_t>(
             mac_adres[1]) << 8) |
        (static_cast<uint32_t>(
             mac_adres[2]) << 16) |
        (static_cast<uint32_t>(
             mac_adres[3]) << 24);

    const uint32_t high =
        static_cast<uint32_t>(
            mac_adres[4]
        ) |
        (static_cast<uint32_t>(
             mac_adres[5]) << 8) |
        RAH_AV;

    zapisz_rejestr(
        REG_RAL0,
        low
    );

    zapisz_rejestr(
        REG_RAH0,
        high
    );

    flush_mmio();
}

bool pobierz_i_zaprogramuj_mac() {
    if (!odczytaj_mac_z_rejestrow()) {
        if (!odczytaj_mac_z_eeprom()) {
            wypisz_log(
                "[E1000] Nie udalo sie odczytac poprawnego MAC."
            );

            return false;
        }
    }

    zaprogramuj_mac();

    return true;
}

/* =========================================================================
 * 15. DMA - CLEANUP
 * ========================================================================= */

void zwolnij_zasoby_dma() {
    if (rx_ring_phys) {
        ZwolnijRamke(
            rx_ring_phys
        );
    }

    if (tx_ring_phys) {
        ZwolnijRamke(
            tx_ring_phys
        );
    }

    rx_ring_phys =
        nullptr;

    tx_ring_phys =
        nullptr;

    rx_descs =
        nullptr;

    tx_descs =
        nullptr;

    for (uint32_t i = 0;
         i <
            E1000_NUM_RX_DESC;
         ++i) {

        if (rx_bufory_phys[i]) {
            ZwolnijRamke(
                rx_bufory_phys[i]
            );

            rx_bufory_phys[i] =
                nullptr;
        }
    }

    for (uint32_t i = 0;
         i <
            E1000_NUM_TX_DESC;
         ++i) {

        if (tx_bufory_phys[i]) {
            ZwolnijRamke(
                tx_bufory_phys[i]
            );

            tx_bufory_phys[i] =
                nullptr;
        }
    }

    rx_aktualny =
        0;

    tx_aktualny =
        0;
}

/* =========================================================================
 * 16. DMA - ALOKACJA RINGOW I BUFOROW
 * ========================================================================= */

bool zaalokuj_rx() {
    rx_ring_phys =
        ZaalokujRamke();

    if (!ramka_dma_dostepna_cpu(
            rx_ring_phys)) {

        if (rx_ring_phys) {
            ZwolnijRamke(
                rx_ring_phys
            );

            rx_ring_phys =
                nullptr;
        }

        return false;
    }

    E1000RxDesc* ring =
        static_cast<E1000RxDesc*>(
            dma_wirtualny_z_fizycznego(
                rx_ring_phys
            )
        );

    wyzeruj(
        ring,
        ROZMIAR_RAMKI_4K
    );

    for (uint32_t i = 0;
         i <
            E1000_NUM_RX_DESC;
         ++i) {

        rx_bufory_phys[i] =
            ZaalokujRamke();

        if (!ramka_dma_dostepna_cpu(
                rx_bufory_phys[i])) {

            return false;
        }

        wyzeruj(
            dma_wirtualny_z_fizycznego(
                rx_bufory_phys[i]
            ),
            ROZMIAR_RAMKI_4K
        );

        ring[i].adres =
            reinterpret_cast<uint64_t>(
                rx_bufory_phys[i]
            );

        ring[i].status =
            0;
    }

    rx_descs =
        ring;

    rx_aktualny =
        0;

    return true;
}

bool zaalokuj_tx() {
    tx_ring_phys =
        ZaalokujRamke();

    if (!ramka_dma_dostepna_cpu(
            tx_ring_phys)) {

        if (tx_ring_phys) {
            ZwolnijRamke(
                tx_ring_phys
            );

            tx_ring_phys =
                nullptr;
        }

        return false;
    }

    E1000TxDesc* ring =
        static_cast<E1000TxDesc*>(
            dma_wirtualny_z_fizycznego(
                tx_ring_phys
            )
        );

    wyzeruj(
        ring,
        ROZMIAR_RAMKI_4K
    );

    for (uint32_t i = 0;
         i <
            E1000_NUM_TX_DESC;
         ++i) {

        tx_bufory_phys[i] =
            ZaalokujRamke();

        if (!ramka_dma_dostepna_cpu(
                tx_bufory_phys[i])) {

            return false;
        }

        wyzeruj(
            dma_wirtualny_z_fizycznego(
                tx_bufory_phys[i]
            ),
            ROZMIAR_RAMKI_4K
        );

        ring[i].adres =
            reinterpret_cast<uint64_t>(
                tx_bufory_phys[i]
            );

        /*
         * DD=1 oznacza, ze software moze uzyc deskryptora.
         */
        ring[i].status =
            TXD_STAT_DD;
    }

    tx_descs =
        ring;

    tx_aktualny =
        0;

    return true;
}

bool zaalokuj_zasoby_dma() {
    zwolnij_zasoby_dma();

    if (!zaalokuj_rx() ||
        !zaalokuj_tx()) {

        zwolnij_zasoby_dma();

        return false;
    }

    return true;
}

/* =========================================================================
 * 17. KONFIGURACJA RX
 * ========================================================================= */

bool skonfiguruj_rx() {
    if (!rx_descs ||
        !rx_ring_phys) {

        return false;
    }

    const uint64_t ring_phys =
        reinterpret_cast<uint64_t>(
            rx_ring_phys
        );

    zapisz_rejestr(
        REG_RDBAL,
        static_cast<uint32_t>(
            ring_phys &
            UINT64_C(0xFFFFFFFF)
        )
    );

    zapisz_rejestr(
        REG_RDBAH,
        static_cast<uint32_t>(
            ring_phys >>
            32
        )
    );

    zapisz_rejestr(
        REG_RDLEN,
        RX_RING_BAJTOW
    );

    zapisz_rejestr(
        REG_RDH,
        0
    );

    zapisz_rejestr(
        REG_RDT,
        E1000_NUM_RX_DESC -
        1U
    );

    /*
     * EN    - wlacz odbior
     * BAM   - akceptuj broadcast (ARP/DHCP)
     * SECRC - usun 4-bajtowy Ethernet FCS z bufora RX
     * BSIZE - 2048 B
     */
    const uint32_t rctl =
        RCTL_EN |
        RCTL_BAM |
        RCTL_SECRC |
        RCTL_BSIZE_2048;

    bariera_dma_przed_urzadzeniem();

    zapisz_rejestr(
        REG_RCTL,
        rctl
    );

    flush_mmio();

    return
        (czytaj_rejestr(
             REG_RCTL) &
         RCTL_EN) != 0;
}

/* =========================================================================
 * 18. KONFIGURACJA TX
 * ========================================================================= */

bool skonfiguruj_tx() {
    if (!tx_descs ||
        !tx_ring_phys) {

        return false;
    }

    const uint64_t ring_phys =
        reinterpret_cast<uint64_t>(
            tx_ring_phys
        );

    zapisz_rejestr(
        REG_TDBAL,
        static_cast<uint32_t>(
            ring_phys &
            UINT64_C(0xFFFFFFFF)
        )
    );

    zapisz_rejestr(
        REG_TDBAH,
        static_cast<uint32_t>(
            ring_phys >>
            32
        )
    );

    zapisz_rejestr(
        REG_TDLEN,
        TX_RING_BAJTOW
    );

    zapisz_rejestr(
        REG_TDH,
        0
    );

    zapisz_rejestr(
        REG_TDT,
        0
    );

    const uint32_t tipg =
        TIPG_IPGT |
        (TIPG_IPGR1 << 10) |
        (TIPG_IPGR2 << 20);

    zapisz_rejestr(
        REG_TIPG,
        tipg
    );

    const uint32_t tctl =
        TCTL_EN |
        TCTL_PSP |
        (TCTL_CT <<
         TCTL_CT_SHIFT) |
        (TCTL_COLD <<
         TCTL_COLD_SHIFT) |
        TCTL_RTLC;

    bariera_dma_przed_urzadzeniem();

    zapisz_rejestr(
        REG_TCTL,
        tctl
    );

    flush_mmio();

    return
        (czytaj_rejestr(
             REG_TCTL) &
         TCTL_EN) != 0;
}

/* =========================================================================
 * 19. FILTRY / LINK
 * ========================================================================= */

void wyczysc_multicast_table() {
    /*
     * 128 DWORD = 4096 bitow MTA.
     */
    for (uint32_t i = 0;
         i < 128U;
         ++i) {

        zapisz_rejestr(
            REG_MTA_BASE +
            i *
                sizeof(uint32_t),
            0
        );
    }
}

void wymus_link_up() {
    uint32_t ctrl =
        czytaj_rejestr(
            REG_CTRL
        );

    if (ctrl ==
        UINT32_MAX) {

        return;
    }

    ctrl |=
        CTRL_SLU |
        CTRL_ASDE;

    zapisz_rejestr(
        REG_CTRL,
        ctrl
    );

    flush_mmio();
}

bool czekaj_na_link() {
    for (uint32_t proba = 0;
         proba <
            TIMEOUT_LINK;
         ++proba) {

        if ((czytaj_rejestr(
                 REG_STATUS) &
             STATUS_LU) != 0) {

            return true;
        }

        pause_cpu();
    }

    return false;
}

/* =========================================================================
 * 20. DEZAKTYWACJA PO BLEDZIE
 * ========================================================================= */

void zatrzymaj_hardware() {
    if (!e1000_mmio_baza) {
        return;
    }

    maskuj_przerwania_e1000();

    zapisz_rejestr(
        REG_RCTL,
        0
    );

    zapisz_rejestr(
        REG_TCTL,
        0
    );

    flush_mmio();
}

void anuluj_inicjalizacje() {
    __atomic_store_n(
        &e1000_gotowy,
        false,
        __ATOMIC_RELEASE
    );

    zatrzymaj_hardware();

    zwolnij_zasoby_dma();

    if (e1000_mmio_baza) {
        wylacz_pci_busmaster(
            e1000_bdf
        );
    }

    e1000_mmio_baza =
        nullptr;

    e1000_mmio_phys =
        0;

    wyzeruj(
        mac_adres,
        sizeof(mac_adres)
    );
}

/* =========================================================================
 * 21. PUBLICZNY MAC
 * ========================================================================= */

} // namespace

extern "C" uint8_t* pobierz_mac_adres() {
    return mac_adres;
}

/* =========================================================================
 * 22. INICJALIZACJA
 * ========================================================================= */

extern "C" void inicjalizuj_e1000() {
    bool oczekiwane =
        false;

    if (!__atomic_compare_exchange_n(
            &inicjalizacja_w_toku,
            &oczekiwane,
            true,
            false,
            __ATOMIC_ACQUIRE,
            __ATOMIC_RELAXED)) {

        wypisz_log(
            "[E1000] Inicjalizacja jest juz wykonywana."
        );

        return;
    }

    if (__atomic_load_n(
            &e1000_gotowy,
            __ATOMIC_ACQUIRE)) {

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        wypisz_log(
            "[E1000] Sterownik jest juz zainicjalizowany."
        );

        return;
    }

    wypisz_log(
        "[E1000] Szukam Intel 82540EM (8086:100E)..."
    );

    BdfPCI bdf{};

    if (!znajdz_e1000(
            &bdf)) {

        wypisz_log(
            "[E1000] Brak wspieranej karty 82540EM."
        );

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    e1000_bdf =
        bdf;

    uint64_t bar0 =
        0;

    if (!pobierz_bar0_mmio(
            bdf,
            &bar0)) {

        wypisz_log(
            "[E1000] BAR0 nie jest poprawnym MMIO."
        );

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (!wlacz_pci_dla_e1000(
            bdf)) {

        wypisz_log(
            "[E1000] Nie udalo sie wlaczyc PCI Memory Space/Bus Master."
        );

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (!mapuj_mmio(
            bar0)) {

        wypisz_log(
            "[E1000] Nie udalo sie zmapowac BAR0 MMIO."
        );

        wylacz_pci_busmaster(
            bdf
        );

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (!resetuj_e1000()) {
        anuluj_inicjalizacje();

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (!pobierz_i_zaprogramuj_mac()) {
        anuluj_inicjalizacje();

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    loguj_mac();

    wyczysc_multicast_table();

    if (!zaalokuj_zasoby_dma()) {
        wypisz_log(
            "[E1000] Brak fizycznych ramek PMM dla DMA."
        );

        anuluj_inicjalizacje();

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (!skonfiguruj_rx()) {
        wypisz_log(
            "[E1000] Nie udalo sie uruchomic RX."
        );

        anuluj_inicjalizacje();

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (!skonfiguruj_tx()) {
        wypisz_log(
            "[E1000] Nie udalo sie uruchomic TX."
        );

        anuluj_inicjalizacje();

        __atomic_store_n(
            &inicjalizacja_w_toku,
            false,
            __ATOMIC_RELEASE
        );

        return;
    }

    /*
     * Polling: karta NIE moze generowac IRQ do nieprzygotowanego handlera.
     */
    maskuj_przerwania_e1000();

    /*
     * Dodatkowo zerujemy IMS dla czytelnosci stanu po resecie.
     * IMC jest wlasciwym rejestrem maskujacym.
     */
    zapisz_rejestr(
        REG_IMS,
        0
    );

    wymus_link_up();

    __atomic_store_n(
        &e1000_gotowy,
        true,
        __ATOMIC_RELEASE
    );

    if (czekaj_na_link()) {
        wypisz_log(
            "[E1000] Karta gotowa; link aktywny."
        );
    } else {
        /*
         * Brak linku nie niszczy poprawnie skonfigurowanego urzadzenia.
         * DHCP/ARP moga ruszyc pozniej, gdy link sie pojawi.
         */
        wypisz_log(
            "[E1000] Karta gotowa, ale link jest obecnie nieaktywny."
        );
    }

    __atomic_store_n(
        &inicjalizacja_w_toku,
        false,
        __ATOMIC_RELEASE
    );
}

/* =========================================================================
 * 23. TX
 * ========================================================================= */

extern "C" void e1000_wyslij_pakiet(
    void* dane,
    uint16_t dlugosc
) {
    if (!dane ||
        dlugosc <
            E1000_MIN_RAMKA_ETH ||
        dlugosc >
            E1000_MAX_RAMKA_ETH ||
        dlugosc >
            E1000_TX_BUFOR_BAJTOW ||
        !__atomic_load_n(
            &e1000_gotowy,
            __ATOMIC_ACQUIRE) ||
        !tx_descs) {

        return;
    }

    /*
     * Ograniczony wait zamiast bezterminowego spinlocka.
     * Chroni przed reentrancy i przyszlym SMP.
     */
    GuardBlokady guard(
        &blokada_tx,
        TIMEOUT_TX_LOCK,
        false
    );

    if (!guard.aktywny()) {
        /*
         * ABI zwraca void, wiec nie ma jak przekazac bledu wyzej.
         * Bezpieczniej zgubic ramke niz zakleszczyc kernel.
         */
        return;
    }

    const uint16_t slot =
        tx_aktualny;

    volatile E1000TxDesc& desc =
        tx_descs[slot];

    /*
     * Poczekaj az deskryptor po poprzednim obrocie ringu jest wolny.
     */
    bool wolny =
        false;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_TX_DESC;
         ++proba) {

        if ((desc.status &
             TXD_STAT_DD) != 0) {

            wolny =
                true;

            break;
        }

        pause_cpu();
    }

    if (!wolny) {
        wypisz_log(
            "[E1000] TX descriptor pozostaje zajety."
        );

        return;
    }

    void* bufor_phys =
        tx_bufory_phys[
            slot
        ];

    if (!ramka_dma_dostepna_cpu(
            bufor_phys)) {

        return;
    }

    void* bufor =
        dma_wirtualny_z_fizycznego(
            bufor_phys
        );

    kopiuj(
        bufor,
        dane,
        dlugosc
    );

    desc.dlugosc =
        dlugosc;

    desc.cso =
        0;

    desc.komenda =
        TXD_CMD_EOP |
        TXD_CMD_IFCS |
        TXD_CMD_RS;

    desc.css =
        0;

    desc.specjalne =
        0;

    /*
     * Status zerujemy na koncu przygotowania deskryptora.
     */
    desc.status =
        0;

    bariera_dma_przed_urzadzeniem();

    const uint16_t nastepny =
        static_cast<uint16_t>(
            (static_cast<uint32_t>(
                 slot) + 1U) %
            E1000_NUM_TX_DESC
        );

    tx_aktualny =
        nastepny;

    /*
     * Doorbell TX.
     */
    zapisz_rejestr(
        REG_TDT,
        nastepny
    );

    flush_mmio();

    bool zakonczony =
        false;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_TX_DONE;
         ++proba) {

        if ((desc.status &
             TXD_STAT_DD) != 0) {

            zakonczony =
                true;

            break;
        }

        pause_cpu();
    }

    if (!zakonczony) {
        /*
         * Nie zwalniamy stalego bufora DMA - hardware nadal moze miec
         * do niego referencje. Sterownik pozostaje z zasobami przypietymi.
         */
        wypisz_log(
            "[E1000] Timeout transmisji TX."
        );
    }
}

/* =========================================================================
 * 24. RX
 * ========================================================================= */

extern "C" void e1000_obsluz_odbior() {
    if (!__atomic_load_n(
            &e1000_gotowy,
            __ATOMIC_ACQUIRE) ||
        !rx_descs) {

        return;
    }

    /*
     * Natychmiastowy try-lock: parser stosu moze po drodze wywolac kod,
     * ktory znowu pompuje E1000. Reentrant RX powinien wtedy po prostu
     * wrocic, a zewnetrzna petla dokonczy ring.
     */
    GuardBlokady guard(
        &blokada_rx,
        0,
        true
    );

    if (!guard.aktywny()) {
        return;
    }

    /*
     * Maksymalnie jeden pelny obrot ringu na wywolanie. Chroni przed
     * nieskonczona petla przy uszkodzonym statusie deskryptora.
     */
    for (uint32_t przetworzone = 0;
         przetworzone <
            E1000_NUM_RX_DESC;
         ++przetworzone) {

        const uint16_t slot =
            rx_aktualny;

        volatile E1000RxDesc& desc =
            rx_descs[slot];

        const uint8_t status =
            desc.status;

        if ((status &
             RXD_STAT_DD) == 0) {

            break;
        }

        bariera_dma_po_urzadzeniu();

        const uint16_t dlugosc =
            desc.dlugosc;

        const uint8_t bledy =
            desc.bledy;

        const bool kompletna =
            (status &
             RXD_STAT_EOP) != 0;

        void* bufor_phys =
            rx_bufory_phys[
                slot
            ];

        const bool bufor_ok =
            ramka_dma_dostepna_cpu(
                bufor_phys
            );

        const bool dlugosc_ok =
            dlugosc >=
                E1000_MIN_RAMKA_ETH &&
            dlugosc <=
                E1000_MAX_RAMKA_ETH &&
            dlugosc <=
                E1000_RX_BUFOR_BAJTOW;

        if (kompletna &&
            bledy == 0 &&
            bufor_ok &&
            dlugosc_ok) {

            uint8_t* bufor =
                static_cast<uint8_t*>(
                    dma_wirtualny_z_fizycznego(
                        bufor_phys
                    )
                );

            /*
             * siec.cpp ponownie wykonuje bounds-checking wszystkich
             * naglowkow. Sterownik przekazuje tylko zweryfikowana dlugosc.
             */
            obsluz_pakiet_sieciowy(
                bufor,
                dlugosc
            );
        }

        /*
         * Zwracamy deskryptor hardware.
         * Status musi byc wyzerowany przed aktualizacja RDT.
         */
        desc.dlugosc =
            0;

        desc.suma_kontrolna =
            0;

        desc.bledy =
            0;

        desc.specjalne =
            0;

        desc.status =
            0;

        bariera_dma_przed_urzadzeniem();

        zapisz_rejestr(
            REG_RDT,
            slot
        );

        rx_aktualny =
            static_cast<uint16_t>(
                (static_cast<uint32_t>(
                     slot) + 1U) %
                E1000_NUM_RX_DESC
            );
    }
}
