/*
 * Bursztyn OS - sterownik AHCI 1.x (SATA)
 *
 * Publiczne API:
 *
 *   inicjalizuj_kontroler_ahci()
 *   czytaj_z_glownego_dysku_ahci()
 *   zapisz_na_glowny_dysk_ahci()
 *
 * Sterownik pracuje w trybie polling i obsluguje jeden wybrany dysk SATA.
 *
 * Najwazniejsze zasady bezpieczenstwa:
 *
 *   - PCI korzysta wylacznie z centralnego pci.cpp (brak lokalnego CF8/CFC),
 *   - skan PCI obsluguje urzadzenia wielofunkcyjne,
 *   - BAR5 jest walidowany jako 32-bitowy MMIO AHCI BAR,
 *   - mapowany jest caly obszar rejestrow HBA, a nie tylko jedna strona,
 *   - wykonywany jest BIOS/OS ownership handoff, jezeli CAP2.BOH go wymaga,
 *   - HBA jest resetowane z timeoutem,
 *   - silnik PxCMD zatrzymuje/startuje z timeoutem,
 *   - tylko jeden port korzysta z jednego zestawu CLB/FB/CTBA,
 *   - bufory DMA pochodza z PMM i maja znane fizyczne adresy,
 *   - dane 16 KiB sa rozbite na maks. 4 niezalezne ramki PRDT,
 *   - kazda operacja ma bounds-checking i timeout,
 *   - IDENTIFY DEVICE sprawdza LBA48, rozmiar sektora i pojemnosc dysku,
 *   - odczyt/zapis jest serializowany try-lockiem, aby IRQ nie moglo
 *     zakleszczyc sie na przerwanym wlascicielu blokady,
 *   - bledy PxIS.TFES/PxTFD/PxSERR sa traktowane jako blad operacji.
 *
 * Ograniczenia obecnej wersji:
 *
 *   - jeden aktywny dysk SATA,
 *   - sektor logiczny musi miec 512 bajtow,
 *   - maks. 32 sektory / 16 KiB na jedno wywolanie,
 *   - brak NCQ i hot-plug,
 *   - polling zamiast IRQ,
 *   - obecny PMM/VMM udostepnia identity map fizycznej pamieci < 4 GiB.
 *
 * Przy przyszlym HHDM funkcja dma_wirtualny_z_fizycznego() musi zostac
 * zastapiona centralnym phys->virt helperem.
 */

#include "ahci.h"
#include "pamiec.h"
#include "pci.h"

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
 * 2. STALE AHCI / ATA
 * ========================================================================= */

namespace {

constexpr uint32_t AHCI_GHC_HR =
    1U << 0;

constexpr uint32_t AHCI_GHC_IE =
    1U << 1;

constexpr uint32_t AHCI_GHC_AE =
    1U << 31;

constexpr uint32_t AHCI_CAP_S64A =
    1U << 31;

constexpr uint32_t AHCI_CAP_SCLO =
    1U << 24;

constexpr uint32_t AHCI_CAP_SSS =
    1U << 27;

constexpr uint32_t AHCI_CAP_NP_MASK =
    0x1FU;

constexpr uint32_t AHCI_CAP2_BOH =
    1U << 0;

constexpr uint32_t AHCI_BOHC_BOS =
    1U << 0;

constexpr uint32_t AHCI_BOHC_OOS =
    1U << 1;

constexpr uint32_t AHCI_BOHC_BB =
    1U << 4;

constexpr uint32_t AHCI_PXCMD_ST =
    1U << 0;

constexpr uint32_t AHCI_PXCMD_SUD =
    1U << 1;

constexpr uint32_t AHCI_PXCMD_POD =
    1U << 2;

constexpr uint32_t AHCI_PXCMD_CLO =
    1U << 3;

constexpr uint32_t AHCI_PXCMD_FRE =
    1U << 4;

constexpr uint32_t AHCI_PXCMD_FR =
    1U << 14;

constexpr uint32_t AHCI_PXCMD_CR =
    1U << 15;

constexpr uint32_t AHCI_PXCMD_ICC_MASK =
    0xFU << 28;

constexpr uint32_t AHCI_PXCMD_ICC_ACTIVE =
    1U << 28;

constexpr uint32_t AHCI_PXIS_TFES =
    1U << 30;

constexpr uint32_t AHCI_PXTFD_ERR =
    1U << 0;

constexpr uint32_t AHCI_PXTFD_DRQ =
    1U << 3;

constexpr uint32_t AHCI_PXTFD_BSY =
    1U << 7;

constexpr uint32_t AHCI_SSTS_DET_MASK =
    0x0FU;

constexpr uint32_t AHCI_SSTS_IPM_MASK =
    0x0FU << 8;

constexpr uint32_t AHCI_SSTS_DET_PRESENT =
    3U;

constexpr uint32_t AHCI_SSTS_IPM_ACTIVE =
    1U;

constexpr uint32_t SATA_SIGNATURE_ATA =
    UINT32_C(0x00000101);

constexpr uint8_t FIS_TYPE_REG_H2D =
    0x27U;

constexpr uint8_t FIS_COMMAND_BIT =
    1U << 7;

constexpr uint8_t ATA_CMD_IDENTIFY_DEVICE =
    0xECU;

constexpr uint8_t ATA_CMD_READ_DMA_EXT =
    0x25U;

constexpr uint8_t ATA_CMD_WRITE_DMA_EXT =
    0x35U;

constexpr uint8_t ATA_DEVICE_LBA =
    1U << 6;

constexpr uint32_t AHCI_SEKTOR_BAJTOW =
    512U;

constexpr uint32_t AHCI_MAKS_SEKTOROW_OPERACJI =
    32U;

constexpr uint32_t AHCI_MAKS_BAJTOW_OPERACJI =
    AHCI_SEKTOR_BAJTOW *
    AHCI_MAKS_SEKTOROW_OPERACJI;

constexpr uint32_t AHCI_DMA_STRONA =
    4096U;

constexpr uint32_t AHCI_LICZBA_STRON_DANYCH =
    AHCI_MAKS_BAJTOW_OPERACJI /
    AHCI_DMA_STRONA;

static_assert(
    AHCI_LICZBA_STRON_DANYCH == 4U,
    "16 KiB DMA powinno korzystac z czterech stron 4 KiB"
);

constexpr uint32_t AHCI_SLOT_KOMENDY =
    0U;

constexpr uint32_t AHCI_SLOT_BIT =
    1U << AHCI_SLOT_KOMENDY;

constexpr uint32_t AHCI_PRDT_DBC_MASK =
    0x003FFFFFU;

constexpr uint32_t AHCI_PRDT_IOC =
    1U << 31;

constexpr uint32_t AHCI_CMDHDR_CFL_MASK =
    0x001FU;

constexpr uint32_t AHCI_CMDHDR_W =
    1U << 6;

constexpr uint8_t PCI_KLASA_STORAGE =
    0x01U;

constexpr uint8_t PCI_PODKLASA_SATA =
    0x06U;

constexpr uint8_t PCI_PROGIF_AHCI =
    0x01U;

constexpr uint32_t PCI_BAR_IO =
    1U << 0;

constexpr uint32_t PCI_BAR_MEMORY_TYPE_MASK =
    0x6U;

constexpr uint32_t PCI_BAR_MEMORY_TYPE_32 =
    0x0U;

constexpr uint32_t PCI_BAR_MEMORY_ADDRESS_MASK =
    0xFFFFFFF0U;

constexpr uint32_t TIMEOUT_BIOS_HANDOFF =
    5U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_HBA_RESET =
    5U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_PORT_ENGINE =
    2U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_PORT_READY =
    5U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_COMMAND =
    20U * 1000U * 1000U;

constexpr uint32_t TIMEOUT_DEVICE_DETECT =
    2U * 1000U * 1000U;

/* =========================================================================
 * 3. REJESTRY MMIO AHCI
 * ========================================================================= */

struct PortAHCI {
    uint32_t clb;       /* 0x00 Command List Base Address */
    uint32_t clbu;      /* 0x04 */
    uint32_t fb;        /* 0x08 FIS Base Address */
    uint32_t fbu;       /* 0x0C */

    uint32_t is;        /* 0x10 Interrupt Status */
    uint32_t ie;        /* 0x14 Interrupt Enable */
    uint32_t cmd;       /* 0x18 Command and Status */
    uint32_t reserved0; /* 0x1C */

    uint32_t tfd;       /* 0x20 Task File Data */
    uint32_t sig;       /* 0x24 Signature */
    uint32_t ssts;      /* 0x28 SATA Status */
    uint32_t sctl;      /* 0x2C SATA Control */

    uint32_t serr;      /* 0x30 SATA Error */
    uint32_t sact;      /* 0x34 SATA Active */
    uint32_t ci;        /* 0x38 Command Issue */
    uint32_t sntf;      /* 0x3C SATA Notification */

    uint32_t fbs;       /* 0x40 FIS-based Switching */
    uint32_t reserved1[11];

    uint32_t vendor[4];
};

struct HbaAHCI {
    uint32_t cap;       /* 0x00 */
    uint32_t ghc;       /* 0x04 */
    uint32_t is;        /* 0x08 */
    uint32_t pi;        /* 0x0C */
    uint32_t vs;        /* 0x10 */

    uint32_t ccc_ctl;   /* 0x14 */
    uint32_t ccc_ports; /* 0x18 */
    uint32_t em_loc;    /* 0x1C */
    uint32_t em_ctl;    /* 0x20 */
    uint32_t cap2;      /* 0x24 */
    uint32_t bohc;      /* 0x28 */

    uint8_t reserved[0x74];
    uint8_t vendor[0x60];

    PortAHCI ports[32];
};

static_assert(
    sizeof(PortAHCI) == 0x80U,
    "Rejestry jednego portu AHCI musza zajmowac 0x80 bajtow"
);

static_assert(
    offsetof(PortAHCI, cmd) == 0x18U,
    "PxCMD ma nieprawidlowy offset"
);

static_assert(
    offsetof(PortAHCI, tfd) == 0x20U,
    "PxTFD ma nieprawidlowy offset"
);

static_assert(
    offsetof(PortAHCI, ci) == 0x38U,
    "PxCI ma nieprawidlowy offset"
);

static_assert(
    offsetof(HbaAHCI, cap2) == 0x24U,
    "CAP2 ma nieprawidlowy offset"
);

static_assert(
    offsetof(HbaAHCI, bohc) == 0x28U,
    "BOHC ma nieprawidlowy offset"
);

static_assert(
    offsetof(HbaAHCI, ports) == 0x100U,
    "Porty AHCI musza zaczynac sie od offsetu 0x100"
);

static_assert(
    sizeof(HbaAHCI) == 0x1100U,
    "Pelny blok rejestrow AHCI dla 32 portow ma 0x1100 bajtow"
);

/* =========================================================================
 * 4. STRUKTURY DMA AHCI
 * ========================================================================= */

struct NaglowekKomendyAHCI {
    uint16_t flagi;
    uint16_t liczba_prdt;

    uint32_t licznik_bajtow;

    uint32_t ctba;
    uint32_t ctbau;

    uint32_t reserved[4];
} __attribute__((packed));

struct PrdtAHCI {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc_i;
} __attribute__((packed));

struct FisRegH2D {
    uint8_t typ;
    uint8_t pmport_c;
    uint8_t komenda;
    uint8_t feature_low;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_high;

    uint8_t count_low;
    uint8_t count_high;
    uint8_t icc;
    uint8_t control;

    uint8_t reserved[4];
} __attribute__((packed));

struct TablicaKomendyAHCI {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];

    PrdtAHCI prdt[
        AHCI_LICZBA_STRON_DANYCH
    ];
} __attribute__((packed));

static_assert(
    sizeof(NaglowekKomendyAHCI) == 32U,
    "Command Header AHCI musi miec 32 bajty"
);

static_assert(
    sizeof(PrdtAHCI) == 16U,
    "PRDT entry musi miec 16 bajtow"
);

static_assert(
    sizeof(FisRegH2D) == 20U,
    "Register H2D FIS musi miec 20 bajtow"
);

static_assert(
    offsetof(TablicaKomendyAHCI, prdt) == 128U,
    "PRDT musi zaczynac sie po 128 bajtach command table"
);

static_assert(
    sizeof(TablicaKomendyAHCI) <=
        AHCI_DMA_STRONA,
    "Command table musi zmiescic sie w jednej stronie PMM"
);

/* =========================================================================
 * 5. GLOBALNY STAN STEROWNIKA
 * ========================================================================= */

struct BdfPCI {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
};

struct ZasobyDMA {
    void* lista_komend_phys;
    void* fis_phys;
    void* tablica_komendy_phys;

    void* dane_phys[
        AHCI_LICZBA_STRON_DANYCH
    ];

    bool gotowe;
};

volatile HbaAHCI* hba =
    nullptr;

volatile PortAHCI* glowny_port =
    nullptr;

int glowny_numer_portu =
    -1;

BdfPCI kontroler_bdf{};

uint64_t liczba_sektorow_dysku =
    0;

uint32_t cap_hba =
    0;

ZasobyDMA dma{};

bool ahci_gotowy =
    false;

bool blokada_operacji =
    false;

/* =========================================================================
 * 6. PROSTE HELPERY
 * ========================================================================= */

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

        p[i] = 0;
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

        d[i] = s[i];
    }
}

uint64_t adres_fizyczny(
    const void* phys
) {
    return
        reinterpret_cast<uint64_t>(
            phys
        );
}

/*
 * Obecny VMM Bursztyna utrzymuje identity map fizycznych 0..4 GiB.
 * PMM przydziela ramki DMA ponizej 1 GiB.
 */
void* dma_wirtualny_z_fizycznego(
    void* phys
) {
    return phys;
}

bool adres_dma_poprawny(
    const void* phys,
    size_t dlugosc
) {
    if (!phys ||
        dlugosc == 0) {

        return false;
    }

    const uint64_t p =
        adres_fizyczny(
            phys
        );

    if ((p &
         (AHCI_DMA_STRONA - 1U)) != 0) {

        return false;
    }

    if (p >
        UINT64_MAX -
            (dlugosc - 1U)) {

        return false;
    }

    /*
     * PMM jest obecnie ograniczony do <1 GiB, a to dodatkowo zapewnia
     * kompatybilnosc z HBA bez CAP.S64A.
     */
    if ((cap_hba &
         AHCI_CAP_S64A) == 0) {

        if (p +
                dlugosc -
                1U >
            UINT32_MAX) {

            return false;
        }
    }

    return true;
}

void bariera_przed_dma() {
    asm volatile(
        "mfence"
        :
        :
        : "memory"
    );
}

void bariera_po_dma() {
    asm volatile(
        "mfence"
        :
        :
        : "memory"
    );
}

void pause_cpu() {
    asm volatile(
        "pause"
        :
        :
        : "memory"
    );
}

/* =========================================================================
 * 7. LOG LICZBOWY
 * ========================================================================= */

void uint_do_str(
    uint32_t wartosc,
    char* bufor,
    size_t pojemnosc
) {
    if (!bufor ||
        pojemnosc == 0) {

        return;
    }

    char temp[16] = {};
    size_t n = 0;

    do {
        if (n >=
            sizeof(temp)) {

            bufor[0] =
                '\0';

            return;
        }

        temp[n++] =
            static_cast<char>(
                '0' +
                wartosc % 10U
            );

        wartosc /=
            10U;

    } while (wartosc != 0);

    if (n + 1U >
        pojemnosc) {

        bufor[0] =
            '\0';

        return;
    }

    size_t out =
        0;

    while (n != 0) {
        bufor[out++] =
            temp[--n];
    }

    bufor[out] =
        '\0';
}

void log_port(
    const char* prefix,
    uint32_t port
) {
    if (!prefix) {
        return;
    }

    char log[96] = {};
    size_t p = 0;

    while (prefix[p] != '\0' &&
           p + 1U <
               sizeof(log)) {

        log[p] =
            prefix[p];

        ++p;
    }

    char liczba[16] = {};

    uint_do_str(
        port,
        liczba,
        sizeof(liczba)
    );

    size_t i = 0;

    while (liczba[i] != '\0' &&
           p + 1U <
               sizeof(log)) {

        log[p++] =
            liczba[i++];
    }

    if (p + 2U <
        sizeof(log)) {

        log[p++] = '.';
    }

    log[p] =
        '\0';

    wypisz_log(
        log
    );
}

/* =========================================================================
 * 8. TRY-LOCK OPERACJI
 * ========================================================================= */

/*
 * Nie spinujemy.
 *
 * Gdyby IRQ przerwalo kod trzymajacy blokade i samo sprobowalo wejsc do
 * AHCI, spinlock zakleszczylby pojedynczy CPU. Try-lock zwraca wtedy blad.
 */
bool ahci_sprobuj_zablokowac() {
    return
        !__atomic_test_and_set(
            &blokada_operacji,
            __ATOMIC_ACQUIRE
        );
}

void ahci_odblokuj() {
    __atomic_clear(
        &blokada_operacji,
        __ATOMIC_RELEASE
    );
}

class GuardAHCI {
public:
    GuardAHCI()
        : aktywny_(
              ahci_sprobuj_zablokowac()
          ) {
    }

    ~GuardAHCI() {
        if (aktywny_) {
            ahci_odblokuj();
        }
    }

    bool aktywny() const {
        return aktywny_;
    }

    GuardAHCI(
        const GuardAHCI&
    ) = delete;

    GuardAHCI& operator=(
        const GuardAHCI&
    ) = delete;

private:
    bool aktywny_;
};

/* =========================================================================
 * 9. PCI - WYSZUKIWANIE AHCI
 * ========================================================================= */

bool znajdz_kontroler_ahci(
    BdfPCI* wynik,
    uint32_t* abar
) {
    if (!wynik ||
        !abar) {

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

            const bool multi =
                pci_jest_multifunction(
                    pci_typ_naglowka(
                        header_info
                    )
                );

            const uint8_t maks_func =
                multi
                    ? PCI_MAKS_FUNC
                    : 0U;

            for (uint8_t func = 0;
                 func <=
                    maks_func;
                 ++func) {

                if (func != 0 &&
                    pci_vendor_id(
                        bus,
                        slot,
                        func) ==
                        PCI_VENDOR_BRAK) {

                    continue;
                }

                const uint32_t info =
                    pci_odczytaj_dword(
                        bus,
                        slot,
                        func,
                        PCI_OFFSET_CLASS_INFO
                    );

                if (pci_klasa(
                        info) !=
                        PCI_KLASA_STORAGE ||
                    pci_podklasa(
                        info) !=
                        PCI_PODKLASA_SATA ||
                    pci_prog_if(
                        info) !=
                        PCI_PROGIF_AHCI) {

                    continue;
                }

                const uint32_t bar5 =
                    pci_odczytaj_dword(
                        bus,
                        slot,
                        func,
                        PCI_OFFSET_BAR5
                    );

                if (bar5 == 0 ||
                    bar5 ==
                        UINT32_MAX) {

                    continue;
                }

                if ((bar5 &
                     PCI_BAR_IO) != 0) {

                    wypisz_log(
                        "[AHCI] BAR5 jest I/O BAR, oczekiwano MMIO."
                    );

                    continue;
                }

                if ((bar5 &
                     PCI_BAR_MEMORY_TYPE_MASK) !=
                    PCI_BAR_MEMORY_TYPE_32) {

                    /*
                     * BAR5 nie ma BAR6, z ktorym moglby tworzyc 64-bitowy BAR.
                     */
                    wypisz_log(
                        "[AHCI] Nieobslugiwany typ BAR5."
                    );

                    continue;
                }

                const uint32_t adres =
                    bar5 &
                    PCI_BAR_MEMORY_ADDRESS_MASK;

                if (adres == 0) {
                    continue;
                }

                wynik->bus =
                    bus;

                wynik->slot =
                    slot;

                wynik->func =
                    func;

                *abar =
                    adres;

                return true;
            }
        }
    }

    return false;
}

/* =========================================================================
 * 10. PCI COMMAND
 * ========================================================================= */

bool wlacz_pci_mmio_i_busmaster(
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
            PCI_COMMAND_BUS_MASTER
        );

    /*
     * Gorna polowa DWORD to PCI Status z wieloma bitami W1C.
     * NIE zapisujemy przeczytanego statusu z powrotem, bo wyczyscilibysmy
     * aktywne flagi. Zapisujemy tylko dolne 16 bitow Command, upper=0.
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

/* =========================================================================
 * 11. MAPOWANIE ABAR
 * ========================================================================= */

bool mapuj_abar(
    uint32_t abar
) {
    const uint64_t start =
        static_cast<uint64_t>(
            abar
        );

    if (start >
        UINT64_MAX -
            (sizeof(HbaAHCI) - 1U)) {

        return false;
    }

    const uint64_t koniec =
        start +
        sizeof(HbaAHCI) -
        1U;

    const uint64_t pierwsza =
        start &
        ~UINT64_C(0xFFF);

    const uint64_t ostatnia =
        koniec &
        ~UINT64_C(0xFFF);

    const uint32_t flagi =
        VMM_FLAGA_OBECNA |
        VMM_FLAGA_ZAPIS |
        VMM_FLAGA_PWT |
        VMM_FLAGA_PCD;

    for (uint64_t strona =
             pierwsza;
         strona <=
             ostatnia;
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

    hba =
        reinterpret_cast<volatile HbaAHCI*>(
            static_cast<uint64_t>(
                abar
            )
        );

    return
        hba != nullptr;
}

/* =========================================================================
 * 12. BIOS / OS HANDOFF
 * ========================================================================= */

bool wykonaj_bios_handoff() {
    if (!hba) {
        return false;
    }

    if ((hba->cap2 &
         AHCI_CAP2_BOH) == 0) {

        return true;
    }

    hba->bohc |=
        AHCI_BOHC_OOS;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_BIOS_HANDOFF;
         ++proba) {

        const uint32_t bohc =
            hba->bohc;

        if ((bohc &
             (AHCI_BOHC_BOS |
              AHCI_BOHC_BB)) == 0) {

            return true;
        }

        pause_cpu();
    }

    wypisz_log(
        "[AHCI] Timeout BIOS/OS ownership handoff."
    );

    return false;
}

/* =========================================================================
 * 13. RESET HBA
 * ========================================================================= */

bool resetuj_hba() {
    if (!hba) {
        return false;
    }

    /*
     * Tryb AHCI musi byc wlaczony. Wylaczamy globalne IRQ, bo sterownik
     * dziala pollingiem.
     */
    hba->ghc =
        (hba->ghc |
         AHCI_GHC_AE) &
        ~AHCI_GHC_IE;

    hba->ghc |=
        AHCI_GHC_HR;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_HBA_RESET;
         ++proba) {

        if ((hba->ghc &
             AHCI_GHC_HR) == 0) {

            /*
             * Reset moze wyzerowac AE.
             */
            hba->ghc =
                (hba->ghc |
                 AHCI_GHC_AE) &
                ~AHCI_GHC_IE;

            hba->is =
                UINT32_MAX;

            return true;
        }

        pause_cpu();
    }

    wypisz_log(
        "[AHCI] Timeout resetu HBA."
    );

    return false;
}

/* =========================================================================
 * 14. DMA - ALOKACJA
 * ========================================================================= */

void zwolnij_zasoby_dma() {
    if (dma.lista_komend_phys) {
        ZwolnijRamke(
            dma.lista_komend_phys
        );
    }

    if (dma.fis_phys) {
        ZwolnijRamke(
            dma.fis_phys
        );
    }

    if (dma.tablica_komendy_phys) {
        ZwolnijRamke(
            dma.tablica_komendy_phys
        );
    }

    for (uint32_t i = 0;
         i <
            AHCI_LICZBA_STRON_DANYCH;
         ++i) {

        if (dma.dane_phys[i]) {
            ZwolnijRamke(
                dma.dane_phys[i]
            );
        }
    }

    dma =
        {};
}

bool zaalokuj_zasoby_dma() {
    if (dma.gotowe) {
        return true;
    }

    dma =
        {};

    dma.lista_komend_phys =
        ZaalokujRamke();

    dma.fis_phys =
        ZaalokujRamke();

    dma.tablica_komendy_phys =
        ZaalokujRamke();

    for (uint32_t i = 0;
         i <
            AHCI_LICZBA_STRON_DANYCH;
         ++i) {

        dma.dane_phys[i] =
            ZaalokujRamke();
    }

    if (!dma.lista_komend_phys ||
        !dma.fis_phys ||
        !dma.tablica_komendy_phys) {

        zwolnij_zasoby_dma();
        return false;
    }

    for (uint32_t i = 0;
         i <
            AHCI_LICZBA_STRON_DANYCH;
         ++i) {

        if (!dma.dane_phys[i]) {
            zwolnij_zasoby_dma();
            return false;
        }
    }

    if (!adres_dma_poprawny(
            dma.lista_komend_phys,
            AHCI_DMA_STRONA) ||
        !adres_dma_poprawny(
            dma.fis_phys,
            AHCI_DMA_STRONA) ||
        !adres_dma_poprawny(
            dma.tablica_komendy_phys,
            AHCI_DMA_STRONA)) {

        zwolnij_zasoby_dma();
        return false;
    }

    for (uint32_t i = 0;
         i <
            AHCI_LICZBA_STRON_DANYCH;
         ++i) {

        if (!adres_dma_poprawny(
                dma.dane_phys[i],
                AHCI_DMA_STRONA)) {

            zwolnij_zasoby_dma();
            return false;
        }
    }

    wyzeruj(
        dma_wirtualny_z_fizycznego(
            dma.lista_komend_phys
        ),
        AHCI_DMA_STRONA
    );

    wyzeruj(
        dma_wirtualny_z_fizycznego(
            dma.fis_phys
        ),
        AHCI_DMA_STRONA
    );

    wyzeruj(
        dma_wirtualny_z_fizycznego(
            dma.tablica_komendy_phys
        ),
        AHCI_DMA_STRONA
    );

    for (uint32_t i = 0;
         i <
            AHCI_LICZBA_STRON_DANYCH;
         ++i) {

        wyzeruj(
            dma_wirtualny_z_fizycznego(
                dma.dane_phys[i]
            ),
            AHCI_DMA_STRONA
        );
    }

    dma.gotowe =
        true;

    return true;
}

/* =========================================================================
 * 15. PORT - STOP / START
 * ========================================================================= */

bool zatrzymaj_silnik(
    volatile PortAHCI* port
) {
    if (!port) {
        return false;
    }

    port->cmd &=
        ~AHCI_PXCMD_ST;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_PORT_ENGINE;
         ++proba) {

        if ((port->cmd &
             AHCI_PXCMD_CR) == 0) {

            break;
        }

        pause_cpu();

        if (proba + 1U ==
            TIMEOUT_PORT_ENGINE) {

            return false;
        }
    }

    port->cmd &=
        ~AHCI_PXCMD_FRE;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_PORT_ENGINE;
         ++proba) {

        if ((port->cmd &
             AHCI_PXCMD_FR) == 0) {

            return true;
        }

        pause_cpu();
    }

    return false;
}

bool uruchom_silnik(
    volatile PortAHCI* port
) {
    if (!port) {
        return false;
    }

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_PORT_ENGINE;
         ++proba) {

        if ((port->cmd &
             AHCI_PXCMD_CR) == 0) {

            uint32_t cmd =
                port->cmd;

            cmd |=
                AHCI_PXCMD_FRE |
                AHCI_PXCMD_POD;

            if ((cap_hba &
                 AHCI_CAP_SSS) != 0) {

                cmd |=
                    AHCI_PXCMD_SUD;
            }

            cmd &=
                ~AHCI_PXCMD_ICC_MASK;

            cmd |=
                AHCI_PXCMD_ICC_ACTIVE;

            port->cmd =
                cmd;

            port->cmd |=
                AHCI_PXCMD_ST;

            return true;
        }

        pause_cpu();
    }

    return false;
}

/* =========================================================================
 * 16. DETEKCJA PORTU SATA
 * ========================================================================= */

bool port_ma_aktywne_ata(
    volatile PortAHCI* port
) {
    if (!port) {
        return false;
    }

    /*
     * Po globalnym resecie HBA kontrolery ze staggered spin-up moga
     * pozostawic port bez wlaczonego zasilania/spin-up. W takim stanie
     * PxSSTS.DET nigdy nie przejdzie w 3, mimo ze dysk jest podlaczony.
     * Aktywacja musi nastapic przed testem obecnosci urzadzenia, a nie
     * dopiero pozniej w uruchom_silnik().
     */
    uint32_t cmd =
        port->cmd |
        AHCI_PXCMD_POD;

    if ((cap_hba &
         AHCI_CAP_SSS) != 0) {

        cmd |=
            AHCI_PXCMD_SUD;
    }

    cmd &=
        ~AHCI_PXCMD_ICC_MASK;

    cmd |=
        AHCI_PXCMD_ICC_ACTIVE;

    port->cmd =
        cmd;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_DEVICE_DETECT;
         ++proba) {

        const uint32_t ssts =
            port->ssts;

        const uint32_t det =
            ssts &
            AHCI_SSTS_DET_MASK;

        const uint32_t ipm =
            (ssts &
             AHCI_SSTS_IPM_MASK) >>
            8;

        if (det ==
                AHCI_SSTS_DET_PRESENT &&
            ipm ==
                AHCI_SSTS_IPM_ACTIVE) {

            const uint32_t signature =
                port->sig;

            /*
             * QEMU moze pozostawic PxSIG jako 0xFFFFFFFF po resecie HBA,
             * mimo aktywnego lacza SATA. Nie odrzucamy takiego portu:
             * ATA IDENTIFY wykonane po konfiguracji DMA jest ostateczna
             * i wiarygodna weryfikacja typu urzadzenia.
             */
            return
                signature ==
                    SATA_SIGNATURE_ATA ||
                signature ==
                    UINT32_MAX;
        }

        pause_cpu();
    }

    return false;
}

int znajdz_glowny_port() {
    if (!hba) {
        return -1;
    }

    const uint32_t pi =
        hba->pi;

    const uint32_t np =
        (cap_hba &
         AHCI_CAP_NP_MASK) +
        1U;

    const uint32_t maks_portow =
        np < 32U
            ? np
            : 32U;

    for (uint32_t i = 0;
         i <
            maks_portow;
         ++i) {

        if ((pi &
             (1U << i)) == 0) {

            continue;
        }

        volatile PortAHCI* port =
            &hba->ports[i];

        if (port_ma_aktywne_ata(
                port)) {

            return
                static_cast<int>(
                    i
                );
        }

    }

    return -1;
}

/* =========================================================================
 * 17. KONFIGURACJA DMA PORTU
 * ========================================================================= */

bool skonfiguruj_port(
    volatile PortAHCI* port
) {
    if (!port ||
        !dma.gotowe) {

        return false;
    }

    if (!zatrzymaj_silnik(
            port)) {

        wypisz_log(
            "[AHCI] Nie udalo sie zatrzymac silnika portu."
        );

        return false;
    }

    const uint64_t clb =
        adres_fizyczny(
            dma.lista_komend_phys
        );

    const uint64_t fb =
        adres_fizyczny(
            dma.fis_phys
        );

    port->clb =
        static_cast<uint32_t>(
            clb &
            UINT64_C(0xFFFFFFFF)
        );

    port->clbu =
        static_cast<uint32_t>(
            clb >> 32
        );

    port->fb =
        static_cast<uint32_t>(
            fb &
            UINT64_C(0xFFFFFFFF)
        );

    port->fbu =
        static_cast<uint32_t>(
            fb >> 32
        );

    port->ie =
        0;

    port->is =
        UINT32_MAX;

    port->serr =
        UINT32_MAX;

    NaglowekKomendyAHCI* lista =
        static_cast<NaglowekKomendyAHCI*>(
            dma_wirtualny_z_fizycznego(
                dma.lista_komend_phys
            )
        );

    wyzeruj(
        lista,
        AHCI_DMA_STRONA
    );

    const uint64_t ctba =
        adres_fizyczny(
            dma.tablica_komendy_phys
        );

    lista[
        AHCI_SLOT_KOMENDY
    ].ctba =
        static_cast<uint32_t>(
            ctba &
            UINT64_C(0xFFFFFFFF)
        );

    lista[
        AHCI_SLOT_KOMENDY
    ].ctbau =
        static_cast<uint32_t>(
            ctba >> 32
        );

    lista[
        AHCI_SLOT_KOMENDY
    ].liczba_prdt =
        0;

    bariera_przed_dma();

    if (!uruchom_silnik(
            port)) {

        wypisz_log(
            "[AHCI] Nie udalo sie uruchomic silnika portu."
        );

        return false;
    }

    return true;
}

/* =========================================================================
 * 18. PRZYGOTOWANIE COMMAND TABLE / PRDT
 * ========================================================================= */

NaglowekKomendyAHCI* naglowek_slot0() {
    if (!dma.gotowe) {
        return nullptr;
    }

    return
        static_cast<NaglowekKomendyAHCI*>(
            dma_wirtualny_z_fizycznego(
                dma.lista_komend_phys
            )
        ) +
        AHCI_SLOT_KOMENDY;
}

TablicaKomendyAHCI* tablica_slot0() {
    if (!dma.gotowe) {
        return nullptr;
    }

    return
        static_cast<TablicaKomendyAHCI*>(
            dma_wirtualny_z_fizycznego(
                dma.tablica_komendy_phys
            )
        );
}

bool wypelnij_prdt(
    TablicaKomendyAHCI* tablica,
    uint32_t bajty,
    uint16_t* liczba_prdt
) {
    if (!tablica ||
        !liczba_prdt ||
        bajty == 0 ||
        bajty >
            AHCI_MAKS_BAJTOW_OPERACJI) {

        return false;
    }

    uint32_t pozostalo =
        bajty;

    uint16_t wpis =
        0;

    while (pozostalo != 0) {
        if (wpis >=
            AHCI_LICZBA_STRON_DANYCH) {

            return false;
        }

        const uint32_t fragment =
            pozostalo >
                    AHCI_DMA_STRONA
                ? AHCI_DMA_STRONA
                : pozostalo;

        const uint64_t phys =
            adres_fizyczny(
                dma.dane_phys[
                    wpis]
            );

        tablica->prdt[
            wpis
        ].dba =
            static_cast<uint32_t>(
                phys &
                UINT64_C(0xFFFFFFFF)
            );

        tablica->prdt[
            wpis
        ].dbau =
            static_cast<uint32_t>(
                phys >> 32
            );

        tablica->prdt[
            wpis
        ].reserved =
            0;

        tablica->prdt[
            wpis
        ].dbc_i =
            (fragment - 1U) &
            AHCI_PRDT_DBC_MASK;

        pozostalo -=
            fragment;

        ++wpis;
    }

    /*
     * Polling nie potrzebuje IOC.
     */
    if (wpis != 0) {
        tablica->prdt[
            wpis - 1U
        ].dbc_i &=
            ~AHCI_PRDT_IOC;
    }

    *liczba_prdt =
        wpis;

    return true;
}

/* =========================================================================
 * 19. GOTOWOSC PORTU / CLO
 * ========================================================================= */

bool czekaj_az_port_gotowy(
    volatile PortAHCI* port
) {
    if (!port) {
        return false;
    }

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_PORT_READY;
         ++proba) {

        if ((port->tfd &
             (AHCI_PXTFD_BSY |
              AHCI_PXTFD_DRQ)) == 0) {

            return true;
        }

        pause_cpu();
    }

    /*
     * Command List Override moze wyprowadzic port z BSY, jezeli HBA
     * reklamuje CAP.SCLO.
     */
    if ((cap_hba &
         AHCI_CAP_SCLO) != 0) {

        port->cmd |=
            AHCI_PXCMD_CLO;

        for (uint32_t proba = 0;
             proba <
                TIMEOUT_PORT_READY;
             ++proba) {

            if ((port->cmd &
                 AHCI_PXCMD_CLO) == 0) {

                break;
            }

            pause_cpu();
        }

        for (uint32_t proba = 0;
             proba <
                TIMEOUT_PORT_READY;
             ++proba) {

            if ((port->tfd &
                 (AHCI_PXTFD_BSY |
                  AHCI_PXTFD_DRQ)) == 0) {

                return true;
            }

            pause_cpu();
        }
    }

    return false;
}

/* =========================================================================
 * 20. ISSUE / WAIT
 * ========================================================================= */

bool uruchom_i_czekaj_na_komende(
    volatile PortAHCI* port
) {
    if (!port) {
        return false;
    }

    if ((port->ci &
         AHCI_SLOT_BIT) != 0 ||
        (port->sact &
         AHCI_SLOT_BIT) != 0) {

        return false;
    }

    if (!czekaj_az_port_gotowy(
            port)) {

        wypisz_log(
            "[AHCI] Port pozostaje BSY/DRQ."
        );

        return false;
    }

    port->is =
        UINT32_MAX;

    port->serr =
        UINT32_MAX;

    bariera_przed_dma();

    port->ci =
        AHCI_SLOT_BIT;

    for (uint32_t proba = 0;
         proba <
            TIMEOUT_COMMAND;
         ++proba) {

        const uint32_t is =
            port->is;

        if ((is &
             AHCI_PXIS_TFES) != 0) {

            wypisz_log(
                "[AHCI] Task File Error podczas komendy."
            );

            port->is =
                UINT32_MAX;

            return false;
        }

        if ((port->ci &
             AHCI_SLOT_BIT) == 0) {

            bariera_po_dma();

            const uint32_t tfd =
                port->tfd;

            const uint32_t serr =
                port->serr;

            const bool blad =
                (tfd &
                 AHCI_PXTFD_ERR) != 0 ||
                (port->is &
                 AHCI_PXIS_TFES) != 0 ||
                serr != 0;

            port->is =
                UINT32_MAX;

            if (serr != 0) {
                port->serr =
                    UINT32_MAX;
            }

            if (blad) {
                wypisz_log(
                    "[AHCI] Komenda zakonczyla sie bledem TFD/SERR."
                );

                return false;
            }

            return true;
        }

        pause_cpu();
    }

    wypisz_log(
        "[AHCI] Timeout oczekiwania na zakonczenie komendy."
    );

    return false;
}

/* =========================================================================
 * 21. IDENTIFY DEVICE
 * ========================================================================= */

uint16_t ident_word(
    const uint8_t* dane,
    uint32_t indeks
) {
    if (!dane ||
        indeks >= 256U) {

        return 0;
    }

    const uint32_t off =
        indeks *
        2U;

    return
        static_cast<uint16_t>(
            static_cast<uint16_t>(
                dane[off]) |
            (static_cast<uint16_t>(
                 dane[off + 1U]) << 8)
        );
}

bool wykonaj_identify(
    volatile PortAHCI* port
) {
    NaglowekKomendyAHCI* naglowek =
        naglowek_slot0();

    TablicaKomendyAHCI* tablica =
        tablica_slot0();

    if (!port ||
        !naglowek ||
        !tablica) {

        return false;
    }

    wyzeruj(
        tablica,
        sizeof(*tablica)
    );

    uint16_t liczba_prdt =
        0;

    if (!wypelnij_prdt(
            tablica,
            AHCI_SEKTOR_BAJTOW,
            &liczba_prdt)) {

        return false;
    }

    naglowek->flagi =
        static_cast<uint16_t>(
            (sizeof(FisRegH2D) /
             sizeof(uint32_t)) &
            AHCI_CMDHDR_CFL_MASK
        );

    naglowek->liczba_prdt =
        liczba_prdt;

    naglowek->licznik_bajtow =
        0;

    FisRegH2D* fis =
        reinterpret_cast<FisRegH2D*>(
            tablica->cfis
        );

    fis->typ =
        FIS_TYPE_REG_H2D;

    fis->pmport_c =
        FIS_COMMAND_BIT;

    fis->komenda =
        ATA_CMD_IDENTIFY_DEVICE;

    wyzeruj(
        dma_wirtualny_z_fizycznego(
            dma.dane_phys[0]
        ),
        AHCI_SEKTOR_BAJTOW
    );

    if (!uruchom_i_czekaj_na_komende(
            port)) {

        return false;
    }

    const uint8_t* ident =
        static_cast<const uint8_t*>(
            dma_wirtualny_z_fizycznego(
                dma.dane_phys[0]
            )
        );

    const uint16_t word83 =
        ident_word(
            ident,
            83
        );

    if ((word83 &
         (1U << 10)) == 0) {

        wypisz_log(
            "[AHCI] Dysk nie obsluguje LBA48; sterownik go odrzuca."
        );

        return false;
    }

    uint64_t sektory =
        static_cast<uint64_t>(
            ident_word(
                ident,
                100
            )
        ) |
        (static_cast<uint64_t>(
             ident_word(
                 ident,
                 101
             )
         ) << 16) |
        (static_cast<uint64_t>(
             ident_word(
                 ident,
                 102
             )
         ) << 32) |
        (static_cast<uint64_t>(
             ident_word(
                 ident,
                 103
             )
         ) << 48);

    if (sektory == 0) {
        return false;
    }

    uint32_t rozmiar_sektora =
        AHCI_SEKTOR_BAJTOW;

    const uint16_t word106 =
        ident_word(
            ident,
            106
        );

    const bool word106_valid =
        (word106 &
         (1U << 14)) != 0 &&
        (word106 &
         (1U << 15)) == 0;

    if (word106_valid &&
        (word106 &
         (1U << 12)) != 0) {

        const uint32_t slowa =
            static_cast<uint32_t>(
                ident_word(
                    ident,
                    117
                )
            ) |
            (static_cast<uint32_t>(
                 ident_word(
                     ident,
                     118
                 )
             ) << 16);

        if (slowa == 0 ||
            slowa >
                UINT32_MAX / 2U) {

            return false;
        }

        rozmiar_sektora =
            slowa *
            2U;
    }

    if (rozmiar_sektora !=
        AHCI_SEKTOR_BAJTOW) {

        wypisz_log(
            "[AHCI] Dysk nie uzywa sektorow logicznych 512 B; brak zgodnosci BSP."
        );

        return false;
    }

    liczba_sektorow_dysku =
        sektory;

    return true;
}

/* =========================================================================
 * 22. COPY DO/Z DMA DATA PAGES
 * ========================================================================= */

bool kopiuj_do_dma(
    const void* zrodlo,
    uint32_t bajty
) {
    if (!zrodlo ||
        bajty >
            AHCI_MAKS_BAJTOW_OPERACJI) {

        return false;
    }

    const uint8_t* src =
        static_cast<const uint8_t*>(
            zrodlo
        );

    uint32_t pozostalo =
        bajty;

    uint32_t offset =
        0;

    for (uint32_t i = 0;
         i <
            AHCI_LICZBA_STRON_DANYCH &&
         pozostalo != 0;
         ++i) {

        const uint32_t fragment =
            pozostalo >
                    AHCI_DMA_STRONA
                ? AHCI_DMA_STRONA
                : pozostalo;

        kopiuj(
            dma_wirtualny_z_fizycznego(
                dma.dane_phys[i]
            ),
            src +
                offset,
            fragment
        );

        offset +=
            fragment;

        pozostalo -=
            fragment;
    }

    return
        pozostalo == 0;
}

bool kopiuj_z_dma(
    void* cel,
    uint32_t bajty
) {
    if (!cel ||
        bajty >
            AHCI_MAKS_BAJTOW_OPERACJI) {

        return false;
    }

    uint8_t* dst =
        static_cast<uint8_t*>(
            cel
        );

    uint32_t pozostalo =
        bajty;

    uint32_t offset =
        0;

    for (uint32_t i = 0;
         i <
            AHCI_LICZBA_STRON_DANYCH &&
         pozostalo != 0;
         ++i) {

        const uint32_t fragment =
            pozostalo >
                    AHCI_DMA_STRONA
                ? AHCI_DMA_STRONA
                : pozostalo;

        kopiuj(
            dst +
                offset,
            dma_wirtualny_z_fizycznego(
                dma.dane_phys[i]
            ),
            fragment
        );

        offset +=
            fragment;

        pozostalo -=
            fragment;
    }

    return
        pozostalo == 0;
}

/* =========================================================================
 * 23. READ/WRITE DMA EXT
 * ========================================================================= */

bool operacja_dysku(
    uint64_t lba,
    uint32_t liczba_sektorow,
    void* bufor,
    bool zapis
) {
    if (!ahci_gotowy ||
        !glowny_port ||
        !dma.gotowe ||
        !bufor ||
        liczba_sektorow == 0 ||
        liczba_sektorow >
            AHCI_MAKS_SEKTOROW_OPERACJI) {

        return false;
    }

    if (liczba_sektorow_dysku == 0 ||
        lba >=
            liczba_sektorow_dysku ||
        static_cast<uint64_t>(
            liczba_sektorow) >
            liczba_sektorow_dysku -
                lba) {

        return false;
    }

    const uint32_t bajty =
        liczba_sektorow *
        AHCI_SEKTOR_BAJTOW;

    GuardAHCI guard;

    if (!guard.aktywny()) {
        /*
         * Inna operacja AHCI trwa. Bez kolejki asynchronicznej bezpieczniej
         * zwrocic blad niz zakleszczyc kernel.
         */
        return false;
    }

    NaglowekKomendyAHCI* naglowek =
        naglowek_slot0();

    TablicaKomendyAHCI* tablica =
        tablica_slot0();

    if (!naglowek ||
        !tablica) {

        return false;
    }

    if (zapis &&
        !kopiuj_do_dma(
            bufor,
            bajty)) {

        return false;
    }

    if (!zapis) {
        /*
         * Nie jest wymagane przez DMA, ale usuwa stare dane w razie
         * diagnostyki niepelnego transferu.
         */
        uint32_t pozostalo =
            bajty;

        for (uint32_t i = 0;
             i <
                AHCI_LICZBA_STRON_DANYCH &&
             pozostalo != 0;
             ++i) {

            const uint32_t fragment =
                pozostalo >
                        AHCI_DMA_STRONA
                    ? AHCI_DMA_STRONA
                    : pozostalo;

            wyzeruj(
                dma_wirtualny_z_fizycznego(
                    dma.dane_phys[i]
                ),
                fragment
            );

            pozostalo -=
                fragment;
        }
    }

    wyzeruj(
        tablica,
        sizeof(*tablica)
    );

    uint16_t liczba_prdt =
        0;

    if (!wypelnij_prdt(
            tablica,
            bajty,
            &liczba_prdt)) {

        return false;
    }

    uint16_t flagi =
        static_cast<uint16_t>(
            (sizeof(FisRegH2D) /
             sizeof(uint32_t)) &
            AHCI_CMDHDR_CFL_MASK
        );

    if (zapis) {
        flagi =
            static_cast<uint16_t>(
                flagi |
                AHCI_CMDHDR_W
            );
    }

    naglowek->flagi =
        flagi;

    naglowek->liczba_prdt =
        liczba_prdt;

    naglowek->licznik_bajtow =
        0;

    FisRegH2D* fis =
        reinterpret_cast<FisRegH2D*>(
            tablica->cfis
        );

    fis->typ =
        FIS_TYPE_REG_H2D;

    fis->pmport_c =
        FIS_COMMAND_BIT;

    fis->komenda =
        zapis
            ? ATA_CMD_WRITE_DMA_EXT
            : ATA_CMD_READ_DMA_EXT;

    fis->lba0 =
        static_cast<uint8_t>(
            lba &
            0xFFU
        );

    fis->lba1 =
        static_cast<uint8_t>(
            (lba >> 8) &
            0xFFU
        );

    fis->lba2 =
        static_cast<uint8_t>(
            (lba >> 16) &
            0xFFU
        );

    fis->device =
        ATA_DEVICE_LBA;

    fis->lba3 =
        static_cast<uint8_t>(
            (lba >> 24) &
            0xFFU
        );

    fis->lba4 =
        static_cast<uint8_t>(
            (lba >> 32) &
            0xFFU
        );

    fis->lba5 =
        static_cast<uint8_t>(
            (lba >> 40) &
            0xFFU
        );

    fis->count_low =
        static_cast<uint8_t>(
            liczba_sektorow &
            0xFFU
        );

    fis->count_high =
        static_cast<uint8_t>(
            (liczba_sektorow >> 8) &
            0xFFU
        );

    if (!uruchom_i_czekaj_na_komende(
            glowny_port)) {

        return false;
    }

    if (!zapis) {
        if (!kopiuj_z_dma(
                bufor,
                bajty)) {

            return false;
        }
    }

    return true;
}

/* =========================================================================
 * 24. CLEANUP NIEUDANEJ INICJALIZACJI
 * ========================================================================= */

void anuluj_inicjalizacje() {
    ahci_gotowy =
        false;

    liczba_sektorow_dysku =
        0;

    if (glowny_port) {
        (void)zatrzymaj_silnik(
            glowny_port
        );
    }

    glowny_port =
        nullptr;

    glowny_numer_portu =
        -1;

    zwolnij_zasoby_dma();

    hba =
        nullptr;

    cap_hba =
        0;
}

} // namespace

/* =========================================================================
 * 25. PUBLICZNA INICJALIZACJA
 * ========================================================================= */

extern "C" void inicjalizuj_kontroler_ahci() {
    if (ahci_gotowy) {
        wypisz_log(
            "[AHCI] Sterownik jest juz zainicjalizowany."
        );

        return;
    }

    GuardAHCI guard;

    if (!guard.aktywny()) {
        wypisz_log(
            "[AHCI] Inicjalizacja odrzucona: sterownik jest zajety."
        );

        return;
    }

    wypisz_log(
        "[AHCI] Szukam kontrolera SATA AHCI..."
    );

    uint32_t abar =
        0;

    BdfPCI bdf{};

    if (!znajdz_kontroler_ahci(
            &bdf,
            &abar)) {

        wypisz_log(
            "[AHCI] Nie znaleziono kontrolera AHCI."
        );

        return;
    }

    kontroler_bdf =
        bdf;

    if (!wlacz_pci_mmio_i_busmaster(
            bdf)) {

        wypisz_log(
            "[AHCI] Nie udalo sie wlaczyc PCI Memory Space/Bus Master."
        );

        return;
    }

    if (!mapuj_abar(
            abar)) {

        wypisz_log(
            "[AHCI] Nie udalo sie zmapowac ABAR."
        );

        return;
    }

    cap_hba =
        hba->cap;

    if (!wykonaj_bios_handoff()) {
        anuluj_inicjalizacje();
        return;
    }

    if (!resetuj_hba()) {
        anuluj_inicjalizacje();
        return;
    }

    /*
     * CAP trzeba odczytac ponownie po HBA reset.
     */
    cap_hba =
        hba->cap;

    const int port =
        znajdz_glowny_port();

    if (port < 0) {
        wypisz_log(
            "[AHCI] Kontroler istnieje, ale brak aktywnego dysku SATA."
        );

        anuluj_inicjalizacje();
        return;
    }

    glowny_numer_portu =
        port;

    glowny_port =
        &hba->ports[
            port
        ];

    if (!zaalokuj_zasoby_dma()) {
        wypisz_log(
            "[AHCI] Brak ramek PMM na bufory DMA."
        );

        anuluj_inicjalizacje();
        return;
    }

    if (!skonfiguruj_port(
            glowny_port)) {

        anuluj_inicjalizacje();
        return;
    }

    if (!wykonaj_identify(
            glowny_port)) {

        wypisz_log(
            "[AHCI] IDENTIFY DEVICE nie powiodlo sie."
        );

        anuluj_inicjalizacje();
        return;
    }

    ahci_gotowy =
        true;

    log_port(
        "[AHCI] Glowny dysk SATA gotowy na porcie ",
        static_cast<uint32_t>(
            port
        )
    );
}

/* =========================================================================
 * 26. PUBLICZNY ODCZYT/ZAPIS
 * ========================================================================= */

extern "C" bool czytaj_z_glownego_dysku_ahci(
    uint64_t lba,
    uint32_t ilosc_sektorow,
    void* bufor_docelowy
) {
    return
        operacja_dysku(
            lba,
            ilosc_sektorow,
            bufor_docelowy,
            false
        );
}

extern "C" bool zapisz_na_glowny_dysk_ahci(
    uint64_t lba,
    uint32_t ilosc_sektorow,
    void* dane_zrodlowe
) {
    return
        operacja_dysku(
            lba,
            ilosc_sektorow,
            dane_zrodlowe,
            true
        );
}
