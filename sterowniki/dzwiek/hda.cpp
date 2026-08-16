/*
 * Bursztyn OS - Intel High Definition Audio (HDA)
 *
 * Aktualny zakres sterownika:
 *
 *   - kontroler PCI klasy Multimedia / HDA (04:03),
 *   - polling, bez IRQ,
 *   - jeden Output Stream,
 *   - 48 kHz / 16-bit / stereo PCM,
 *   - Immediate Command do konfiguracji kodeka,
 *   - kodek QEMU hda-output / hda-duplex (vendor 0x1AF4),
 *   - testowy ton o zadanej czestotliwosci i czasie.
 *
 * Sterownik celowo NIE udaje jeszcze uniwersalnego parsera topologii
 * dowolnego kodeka Realtek/Conexant/etc. Dla nieznanego kodeka failuje
 * bezpiecznie zamiast wysylac twardo zakodowane verb-y do losowych NID.
 *
 * Najwazniejsze poprawki wzgledem starej wersji:
 *
 *   - centralne pci.cpp zamiast lokalnych deklaracji/implementacji,
 *   - skanowanie wszystkich funkcji PCI urzadzen multifunction,
 *   - poprawna walidacja 32/64-bit BAR0,
 *   - PCI Memory Space + Bus Master + INTx Disable,
 *   - MMIO mapowane jako supervisor RW + PCD/PWT,
 *   - kontrolowany global reset HDA z timeoutem,
 *   - wymagane 521 us po wyjsciu z CRST przed enumeracja kodekow,
 *   - STATESTS sluzy do znalezienia adresu kodeka,
 *   - Immediate Command korzysta z ICB + IRV zgodnie ze specyfikacja,
 *   - zadna petla oczekiwania nie jest nieskonczona,
 *   - DMA BDL i bufory pochodza z PMM i maja znane adresy fizyczne,
 *   - BDL ma >= 2 wpisy, jest 128-byte aligned i ma <= 256 wpisow,
 *   - poprawnie programowane SDnCBL, SDnLVI, SDnFMT, SDnBDPL/BDPU,
 *   - Stream RUN to bit 1; bit 0 jest SRST,
 *   - stream przechodzi prawidlowa sekwencje resetu przed programowaniem,
 *   - statusy DESE/FIFOE/BCIS sa czyszczone jako RW1C,
 *   - IRQ HDA i stream interrupt enables sa wylaczone przy pollingu,
 *   - hda_test_ton() jest synchroniczny i zatrzymuje DMA po czas_ms,
 *   - czas odtwarzania mierzony jest sprzetowym WALCLK 24 MHz,
 *   - bufor tonu zawiera calkowity okres sygnalu, wiec cykliczny BDL
 *     nie wprowadza skoku fazy na koncu bufora,
 *   - dostep do sterownika jest serializowany try-lockiem.
 *
 * Dokument referencyjny:
 *   Intel High Definition Audio Specification Rev. 1.0a.
 */

#include "hda.h"
#include "../../pamiec.h"
#include "../../pci.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. ZEWNĘTRZNE API JADRA
 * ========================================================================= */

void wypisz_log(
    const char* tekst
);

/* =========================================================================
 * 2. STALE PCI / HDA
 * ========================================================================= */

namespace {

constexpr uint8_t PCI_KLASA_MULTIMEDIA =
    0x04U;

constexpr uint8_t PCI_PODKLASA_HDA =
    0x03U;

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

/*
 * Standardowy obszar rejestrow kontrolera miesci sie znacznie ponizej
 * 16 KiB. Mapujemy 4 strony, tak jak przewidywala stara implementacja,
 * ale z poprawnymi atrybutami cache MMIO.
 */
constexpr uint64_t HDA_MMIO_ROZMIAR =
    16ULL *
    1024ULL;

/* Globalne rejestry HDA */
constexpr uint32_t HDA_REG_GCAP =
    0x00U;

constexpr uint32_t HDA_REG_VMIN =
    0x02U;

constexpr uint32_t HDA_REG_VMAJ =
    0x03U;

constexpr uint32_t HDA_REG_OUTPAY =
    0x04U;

constexpr uint32_t HDA_REG_GCTL =
    0x08U;

constexpr uint32_t HDA_REG_WAKEEN =
    0x0CU;

constexpr uint32_t HDA_REG_STATESTS =
    0x0EU;

constexpr uint32_t HDA_REG_OUTSTRMPAY =
    0x18U;

constexpr uint32_t HDA_REG_INTCTL =
    0x20U;

constexpr uint32_t HDA_REG_INTSTS =
    0x24U;

constexpr uint32_t HDA_REG_WALCLK =
    0x30U;

constexpr uint32_t HDA_REG_CORBCTL =
    0x4CU;

constexpr uint32_t HDA_REG_RIRBCTL =
    0x5CU;

constexpr uint32_t HDA_REG_ICOI =
    0x60U;

constexpr uint32_t HDA_REG_ICII =
    0x64U;

constexpr uint32_t HDA_REG_ICIS =
    0x68U;

constexpr uint32_t HDA_REG_DPLBASE =
    0x70U;

constexpr uint32_t HDA_REG_DPUBASE =
    0x74U;

/* GCTL */
constexpr uint32_t HDA_GCTL_CRST =
    1U << 0;

/* GCAP */
constexpr uint16_t HDA_GCAP_64OK =
    1U << 0;

constexpr uint16_t HDA_GCAP_BSS_MASK =
    0x00F8U;

constexpr uint16_t HDA_GCAP_ISS_MASK =
    0x0F00U;

constexpr uint16_t HDA_GCAP_OSS_MASK =
    0xF000U;

constexpr uint32_t HDA_GCAP_BSS_SHIFT =
    3U;

constexpr uint32_t HDA_GCAP_ISS_SHIFT =
    8U;

constexpr uint32_t HDA_GCAP_OSS_SHIFT =
    12U;

/* CORBCTL / RIRBCTL */
constexpr uint8_t HDA_CORBCTL_RUN =
    1U << 1;

constexpr uint8_t HDA_RIRBCTL_RUN =
    1U << 1;

/* Immediate Command Status */
constexpr uint16_t HDA_ICIS_ICB =
    1U << 0;

constexpr uint16_t HDA_ICIS_IRV =
    1U << 1;

/* Stream Descriptor */
constexpr uint32_t HDA_STREAM_BASE =
    0x80U;

constexpr uint32_t HDA_STREAM_STRIDE =
    0x20U;

constexpr uint32_t HDA_SD_CTL_LO =
    0x00U;

constexpr uint32_t HDA_SD_CTL_HI =
    0x02U;

constexpr uint32_t HDA_SD_STS =
    0x03U;

constexpr uint32_t HDA_SD_LPIB =
    0x04U;

constexpr uint32_t HDA_SD_CBL =
    0x08U;

constexpr uint32_t HDA_SD_LVI =
    0x0CU;

constexpr uint32_t HDA_SD_FIFOS =
    0x10U;

constexpr uint32_t HDA_SD_FMT =
    0x12U;

constexpr uint32_t HDA_SD_BDPL =
    0x18U;

constexpr uint32_t HDA_SD_BDPU =
    0x1CU;

/*
 * SDnCTL:
 *   bit 0 = SRST
 *   bit 1 = RUN
 *   bits 2..4 = interrupt enable
 *   bits 23..20 = Stream Number/Tag
 */
constexpr uint32_t HDA_SD_CTL_SRST =
    1U << 0;

constexpr uint32_t HDA_SD_CTL_RUN =
    1U << 1;

constexpr uint32_t HDA_SD_CTL_IRQ_MASK =
    (1U << 2) |
    (1U << 3) |
    (1U << 4);

constexpr uint32_t HDA_SD_CTL_STREAM_SHIFT =
    20U;

constexpr uint32_t HDA_SD_CTL_STREAM_MASK =
    0xFU <<
    HDA_SD_CTL_STREAM_SHIFT;

/* SDnSTS RW1C */
constexpr uint8_t HDA_SD_STS_BCIS =
    1U << 2;

constexpr uint8_t HDA_SD_STS_FIFOE =
    1U << 3;

constexpr uint8_t HDA_SD_STS_DESE =
    1U << 4;

constexpr uint8_t HDA_SD_STS_FIFORDY =
    1U << 5;

constexpr uint8_t HDA_SD_STS_RW1C =
    HDA_SD_STS_BCIS |
    HDA_SD_STS_FIFOE |
    HDA_SD_STS_DESE;

/*
 * 48 kHz / 16 bit / stereo:
 *
 * BASE=0      48 kHz
 * MULT=000    x1
 * DIV=000     /1
 * BITS=001    16 bit
 * CHAN=0001   2 channels
 */
constexpr uint16_t HDA_FORMAT_48K_16_STEREO =
    0x0011U;

constexpr uint32_t HDA_SAMPLE_RATE =
    48000U;

constexpr uint32_t HDA_KANALY =
    2U;

constexpr uint32_t HDA_BAJTY_PROBKI =
    2U;

constexpr uint32_t HDA_BAJTY_RAMKI_AUDIO =
    HDA_KANALY *
    HDA_BAJTY_PROBKI;

static_assert(
    HDA_BAJTY_RAMKI_AUDIO == 4U,
    "Stereo 16-bit musi zajmowac 4 bajty na sample frame"
);

/*
 * BDL:
 *   minimum 2,
 *   maksimum 256,
 *   base alignment 128 B,
 *   kazdy bufor alignment 128 B.
 *
 * PMM daje strony 4 KiB, wiec spelnia alignment.
 *
 * Maks. okres dla freq 20..20000 Hz przy 48 kHz ma 48000 sample frames,
 * czyli 192000 bajtow. 48 stron daje 196608 bajtow.
 */
constexpr uint32_t HDA_MAKS_BDL =
    48U;

constexpr uint32_t HDA_MAKS_BUFOROW_DMA =
    HDA_MAKS_BDL;

constexpr uint32_t HDA_DMA_BUFOR_BAJTOW =
    static_cast<uint32_t>(
        ROZMIAR_RAMKI_4K
    );

constexpr uint32_t HDA_MAKS_BAJTOW_OKRESU =
    HDA_MAKS_BUFOROW_DMA *
    HDA_DMA_BUFOR_BAJTOW;

static_assert(
    HDA_MAKS_BDL >= 2U &&
    HDA_MAKS_BDL <= 256U,
    "Liczba wpisow BDL musi byc w zakresie 2..256"
);

static_assert(
    HDA_MAKS_BAJTOW_OKRESU >=
        HDA_SAMPLE_RATE *
        HDA_BAJTY_RAMKI_AUDIO,
    "Bufory musza pomiescic najgorszy 1-sekundowy okres tonu"
);

/* Stream tag 0 jest zarezerwowany. */
constexpr uint8_t HDA_STREAM_TAG =
    1U;

/* QEMU hda-output/hda-duplex */
constexpr uint16_t QEMU_HDA_VENDOR_ID =
    0x1AF4U;

constexpr uint8_t QEMU_NID_AUDIO_FUNCTION_GROUP =
    0x01U;

constexpr uint8_t QEMU_NID_DAC =
    0x02U;

constexpr uint8_t QEMU_NID_LINE_OUT =
    0x03U;

/* Codec verbs */
constexpr uint16_t HDA_VERB_GET_PARAMETER =
    0xF00U;

constexpr uint16_t HDA_VERB_SET_STREAM_FORMAT =
    0x200U;

constexpr uint16_t HDA_VERB_SET_CHANNEL_STREAMID =
    0x706U;

constexpr uint16_t HDA_VERB_SET_PIN_WIDGET_CONTROL =
    0x707U;

constexpr uint16_t HDA_VERB_SET_POWER_STATE =
    0x705U;

/*
 * SET_AMP_GAIN_MUTE to 4-bit verb 0x3 z 16-bitowym payloadem.
 */
constexpr uint8_t HDA_VERB4_SET_AMP_GAIN_MUTE =
    0x03U;

constexpr uint16_t HDA_AMP_SET_OUTPUT =
    1U << 15;

constexpr uint16_t HDA_AMP_SET_LEFT =
    1U << 13;

constexpr uint16_t HDA_AMP_SET_RIGHT =
    1U << 12;

constexpr uint16_t QEMU_HDA_GAIN =
    0x004AU;

constexpr uint8_t HDA_PIN_OUT_ENABLE =
    1U << 6;

constexpr uint8_t HDA_POWER_D0 =
    0U;

constexpr uint8_t HDA_PARAMETER_VENDOR_ID =
    0x00U;

/* Timeouty */
constexpr uint32_t HDA_TIMEOUT_RESET =
    5U *
    1000U *
    1000U;

constexpr uint32_t HDA_TIMEOUT_STREAM =
    2U *
    1000U *
    1000U;

constexpr uint32_t HDA_TIMEOUT_IMMEDIATE =
    2U *
    1000U *
    1000U;

constexpr uint32_t HDA_TIMEOUT_DMA_START =
    2U *
    1000U *
    1000U;

constexpr uint32_t HDA_TIMEOUT_LOCK =
    100U *
    1000U;

/*
 * 521 us * 24 MHz = 12504 takty.
 * Uzywamy 13000 dla malego marginesu.
 */
constexpr uint32_t HDA_CODEC_ENUM_TICKS =
    13000U;

constexpr uint32_t HDA_WALCLK_TICKS_PER_MS =
    24000U;

/*
 * Zgodne z walidacja syscalla BWS27.
 */
constexpr uint32_t HDA_MIN_HZ =
    20U;

constexpr uint32_t HDA_MAX_HZ =
    20000U;

constexpr uint32_t HDA_MAX_MS =
    10000U;

/* =========================================================================
 * 3. BDL
 * ========================================================================= */

struct HdaBdlWpis {
    uint64_t adres;
    uint32_t dlugosc;
    uint32_t flagi;
} __attribute__((packed));

static_assert(
    sizeof(HdaBdlWpis) == 16U,
    "HDA BDL entry musi miec 16 bajtow"
);

static_assert(
    offsetof(HdaBdlWpis, adres) == 0U,
    "Nieprawidlowy layout BDL"
);

static_assert(
    offsetof(HdaBdlWpis, dlugosc) == 8U,
    "Nieprawidlowy layout BDL"
);

static_assert(
    offsetof(HdaBdlWpis, flagi) == 12U,
    "Nieprawidlowy layout BDL"
);

/* =========================================================================
 * 4. STAN STEROWNIKA
 * ========================================================================= */

struct BdfPCI {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
};

uint64_t hda_mmio_base =
    0;

uint64_t hda_stream_base =
    0;

uint16_t hda_gcap =
    0;

uint8_t hda_codec_addr =
    0xFFU;

BdfPCI hda_bdf{};

void* hda_bdl_phys =
    nullptr;

HdaBdlWpis* hda_bdl =
    nullptr;

void* hda_bufory_phys[
    HDA_MAKS_BUFOROW_DMA
] = {};

bool hda_gotowy =
    false;

bool hda_blokada =
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

uint32_t nwd_u32(
    uint32_t a,
    uint32_t b
) {
    while (b != 0) {
        const uint32_t r =
            a %
            b;

        a =
            b;

        b =
            r;
    }

    return a;
}

void* dma_wirtualny_z_fizycznego(
    void* phys
) {
    /*
     * Obecny VMM Bursztyna utrzymuje identity map niskiej pamieci, a PMM
     * przydziela ramki DMA w tym obszarze.
     *
     * Przy przyszlym HHDM nalezy zastapic ten helper centralnym phys->virt.
     */
    return phys;
}

bool fizyczny_dma_poprawny(
    const void* phys
) {
    if (!phys) {
        return false;
    }

    const uint64_t p =
        reinterpret_cast<uint64_t>(
            phys
        );

    if ((p &
         (ROZMIAR_RAMKI_4K - 1ULL)) != 0) {

        return false;
    }

    if ((hda_gcap &
         HDA_GCAP_64OK) == 0 &&
        p >
            UINT32_MAX -
            (ROZMIAR_RAMKI_4K - 1ULL)) {

        return false;
    }

    return true;
}

/* =========================================================================
 * 6. BLOKADA
 * ========================================================================= */

bool hda_sprobuj_zablokowac(
    uint32_t timeout
) {
    for (uint32_t i = 0;
         i < timeout;
         ++i) {

        bool oczekiwane =
            false;

        if (__atomic_compare_exchange_n(
                &hda_blokada,
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

void hda_odblokuj() {
    __atomic_store_n(
        &hda_blokada,
        false,
        __ATOMIC_RELEASE
    );
}

class GuardHDA {
public:
    explicit GuardHDA(
        uint32_t timeout
    )
        : aktywny_(
              hda_sprobuj_zablokowac(
                  timeout
              )
          ) {
    }

    ~GuardHDA() {
        if (aktywny_) {
            hda_odblokuj();
        }
    }

    bool aktywny() const {
        return aktywny_;
    }

    GuardHDA(
        const GuardHDA&
    ) = delete;

    GuardHDA& operator=(
        const GuardHDA&
    ) = delete;

private:
    bool aktywny_;
};

/* =========================================================================
 * 7. MMIO
 * ========================================================================= */

bool mmio_zakres_poprawny(
    uint32_t offset,
    uint32_t rozmiar
) {
    if (hda_mmio_base == 0 ||
        rozmiar == 0) {

        return false;
    }

    return
        static_cast<uint64_t>(
            offset) +
            rozmiar <=
        HDA_MMIO_ROZMIAR;
}

uint8_t mmio_odczytaj8(
    uint32_t offset
) {
    if (!mmio_zakres_poprawny(
            offset,
            1)) {

        return
            UINT8_MAX;
    }

    return
        *reinterpret_cast<
            volatile uint8_t*>(
                hda_mmio_base +
                offset
            );
}

uint16_t mmio_odczytaj16(
    uint32_t offset
) {
    if (!mmio_zakres_poprawny(
            offset,
            2) ||
        (offset &
         1U) != 0) {

        return
            UINT16_MAX;
    }

    return
        *reinterpret_cast<
            volatile uint16_t*>(
                hda_mmio_base +
                offset
            );
}

uint32_t mmio_odczytaj32(
    uint32_t offset
) {
    if (!mmio_zakres_poprawny(
            offset,
            4) ||
        (offset &
         3U) != 0) {

        return
            UINT32_MAX;
    }

    return
        *reinterpret_cast<
            volatile uint32_t*>(
                hda_mmio_base +
                offset
            );
}

void mmio_zapisz8(
    uint32_t offset,
    uint8_t wartosc
) {
    if (!mmio_zakres_poprawny(
            offset,
            1)) {

        return;
    }

    *reinterpret_cast<
        volatile uint8_t*>(
            hda_mmio_base +
            offset
        ) =
        wartosc;
}

void mmio_zapisz16(
    uint32_t offset,
    uint16_t wartosc
) {
    if (!mmio_zakres_poprawny(
            offset,
            2) ||
        (offset &
         1U) != 0) {

        return;
    }

    *reinterpret_cast<
        volatile uint16_t*>(
            hda_mmio_base +
            offset
        ) =
        wartosc;
}

void mmio_zapisz32(
    uint32_t offset,
    uint32_t wartosc
) {
    if (!mmio_zakres_poprawny(
            offset,
            4) ||
        (offset &
         3U) != 0) {

        return;
    }

    *reinterpret_cast<
        volatile uint32_t*>(
            hda_mmio_base +
            offset
        ) =
        wartosc;
}

/*
 * SDnCTL ma 24 bity, a bajt +3 to osobny rejestr SDnSTS.
 * Nie wolno robic 32-bitowego RMW pod offsetem stream+0.
 */
uint32_t stream_odczytaj_ctl() {
    if (hda_stream_base == 0) {
        return 0;
    }

    const uint32_t base =
        static_cast<uint32_t>(
            hda_stream_base -
            hda_mmio_base
        );

    const uint16_t low =
        mmio_odczytaj16(
            base +
            HDA_SD_CTL_LO
        );

    const uint8_t high =
        mmio_odczytaj8(
            base +
            HDA_SD_CTL_HI
        );

    return
        static_cast<uint32_t>(
            low) |
        (static_cast<uint32_t>(
             high) << 16);
}

void stream_zapisz_ctl(
    uint32_t ctl
) {
    if (hda_stream_base == 0) {
        return;
    }

    const uint32_t base =
        static_cast<uint32_t>(
            hda_stream_base -
            hda_mmio_base
        );

    mmio_zapisz16(
        base +
        HDA_SD_CTL_LO,
        static_cast<uint16_t>(
            ctl &
            0xFFFFU
        )
    );

    mmio_zapisz8(
        base +
        HDA_SD_CTL_HI,
        static_cast<uint8_t>(
            (ctl >>
             16) &
            0xFFU
        )
    );
}

uint8_t stream_odczytaj_sts() {
    if (hda_stream_base == 0) {
        return 0;
    }

    return
        mmio_odczytaj8(
            static_cast<uint32_t>(
                hda_stream_base -
                hda_mmio_base
            ) +
            HDA_SD_STS
        );
}

void stream_wyczysc_sts() {
    if (hda_stream_base == 0) {
        return;
    }

    mmio_zapisz8(
        static_cast<uint32_t>(
            hda_stream_base -
            hda_mmio_base
        ) +
        HDA_SD_STS,
        HDA_SD_STS_RW1C
    );
}

/* =========================================================================
 * 8. PCI
 * ========================================================================= */

bool znajdz_hda(
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

            if (!pci_funkcja_obecna(
                    bus,
                    slot,
                    0)) {

                continue;
            }

            const uint32_t header =
                pci_odczytaj_dword(
                    bus,
                    slot,
                    0,
                    PCI_OFFSET_HEADER_INFO
                );

            const uint8_t maks_func =
                pci_jest_multifunction(
                    pci_typ_naglowka(
                        header
                    )
                )
                    ? PCI_MAKS_FUNC
                    : 0U;

            for (uint8_t func = 0;
                 func <=
                    maks_func;
                 ++func) {

                if (!pci_funkcja_obecna(
                        bus,
                        slot,
                        func)) {

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
                        PCI_KLASA_MULTIMEDIA ||
                    pci_podklasa(
                        class_info) !=
                        PCI_PODKLASA_HDA) {

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

bool pobierz_bar0(
    const BdfPCI& bdf,
    uint64_t* wynik
) {
    if (!wynik) {
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

        *wynik =
            static_cast<uint64_t>(
                bar0 &
                PCI_BAR_MEM_ADDR_MASK
            );

        return
            *wynik != 0;
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

        *wynik =
            (static_cast<uint64_t>(
                 bar1) << 32) |
            static_cast<uint64_t>(
                bar0 &
                PCI_BAR_MEM_ADDR_MASK
            );

        return
            *wynik != 0;
    }

    return false;
}

bool wlacz_pci_hda(
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
     * Gorna polowa DWORD to PCI Status z bitami W1C.
     * Nie zapisujemy odczytanego statusu z powrotem.
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
 * 9. MAPOWANIE MMIO
 * ========================================================================= */

bool mapuj_mmio(
    uint64_t baza
) {
    if (baza == 0 ||
        baza >
            UINT64_MAX -
            (HDA_MMIO_ROZMIAR - 1ULL)) {

        return false;
    }

    const uint64_t start =
        baza &
        ~UINT64_C(0xFFF);

    const uint64_t koniec =
        (baza +
         HDA_MMIO_ROZMIAR -
         1ULL) &
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

    hda_mmio_base =
        baza;

    /*
     * ZmapujStrone() nadal zwraca void, wiec nie mozemy wiarygodnie
     * odroznic bledu mapowania od poprawnego rejestru o wartosci 0xFFFF.
     * Weryfikujemy przynajmniej major version.
     */
    const uint8_t major =
        mmio_odczytaj8(
            HDA_REG_VMAJ
        );

    return
        major !=
            UINT8_MAX &&
        major >=
            1U;
}

/* =========================================================================
 * 10. GLOBAL RESET
 * ========================================================================= */

uint32_t liczba_streamow_z_gcap(
    uint16_t gcap
) {
    const uint32_t iss =
        (gcap &
         HDA_GCAP_ISS_MASK) >>
        HDA_GCAP_ISS_SHIFT;

    const uint32_t oss =
        (gcap &
         HDA_GCAP_OSS_MASK) >>
        HDA_GCAP_OSS_SHIFT;

    const uint32_t bss =
        (gcap &
         HDA_GCAP_BSS_MASK) >>
        HDA_GCAP_BSS_SHIFT;

    const uint32_t suma =
        iss +
        oss +
        bss;

    return
        suma <= 30U
            ? suma
            : 30U;
}

void zatrzymaj_wszystkie_streamy(
    uint16_t gcap
) {
    const uint32_t liczba =
        liczba_streamow_z_gcap(
            gcap
        );

    for (uint32_t i = 0;
         i < liczba;
         ++i) {

        const uint32_t base =
            HDA_STREAM_BASE +
            i *
            HDA_STREAM_STRIDE;

        /*
         * Czytamy tylko 24-bit SDCTL.
         */
        const uint16_t low =
            mmio_odczytaj16(
                base +
                HDA_SD_CTL_LO
            );

        const uint8_t high =
            mmio_odczytaj8(
                base +
                HDA_SD_CTL_HI
            );

        uint32_t ctl =
            static_cast<uint32_t>(
                low) |
            (static_cast<uint32_t>(
                 high) << 16);

        ctl &=
            ~HDA_SD_CTL_RUN;

        mmio_zapisz16(
            base +
            HDA_SD_CTL_LO,
            static_cast<uint16_t>(
                ctl &
                0xFFFFU
            )
        );

        mmio_zapisz8(
            base +
            HDA_SD_CTL_HI,
            static_cast<uint8_t>(
                ctl >>
                16
            )
        );
    }
}

bool resetuj_kontroler() {
    if (hda_mmio_base == 0) {
        return false;
    }

    const uint16_t gcap_przed =
        mmio_odczytaj16(
            HDA_REG_GCAP
        );

    if (gcap_przed ==
        UINT16_MAX) {

        return false;
    }

    /*
     * Polling: zadnych IRQ.
     */
    mmio_zapisz32(
        HDA_REG_INTCTL,
        0
    );

    mmio_zapisz16(
        HDA_REG_WAKEEN,
        0
    );

    zatrzymaj_wszystkie_streamy(
        gcap_przed
    );

    /*
     * Immediate Commands nie moga wspolistniec z aktywnym CORB/RIRB.
     */
    mmio_zapisz8(
        HDA_REG_CORBCTL,
        static_cast<uint8_t>(
            mmio_odczytaj8(
                HDA_REG_CORBCTL
            ) &
            ~HDA_CORBCTL_RUN
        )
    );

    mmio_zapisz8(
        HDA_REG_RIRBCTL,
        static_cast<uint8_t>(
            mmio_odczytaj8(
                HDA_REG_RIRBCTL
            ) &
            ~HDA_RIRBCTL_RUN
        )
    );

    /*
     * Wylacz DMA Position Buffer.
     */
    mmio_zapisz32(
        HDA_REG_DPLBASE,
        0
    );

    mmio_zapisz32(
        HDA_REG_DPUBASE,
        0
    );

    uint32_t gctl =
        mmio_odczytaj32(
            HDA_REG_GCTL
        );

    if (gctl ==
        UINT32_MAX) {

        return false;
    }

    gctl &=
        ~HDA_GCTL_CRST;

    mmio_zapisz32(
        HDA_REG_GCTL,
        gctl
    );

    bool wszedl_w_reset =
        false;

    for (uint32_t i = 0;
         i <
            HDA_TIMEOUT_RESET;
         ++i) {

        if ((mmio_odczytaj32(
                 HDA_REG_GCTL) &
             HDA_GCTL_CRST) == 0) {

            wszedl_w_reset =
                true;

            break;
        }

        pause_cpu();
    }

    if (!wszedl_w_reset) {
        wypisz_log(
            "[HDA] Timeout wejscia kontrolera w CRST=0."
        );

        return false;
    }

    /*
     * Przytrzymujemy RESET# przez ograniczony czas.
     * Nie ma jeszcze dzialajacego WALCLK, bo link jest w resecie.
     */
    for (uint32_t i = 0;
         i < 100000U;
         ++i) {

        pause_cpu();
    }

    mmio_zapisz32(
        HDA_REG_GCTL,
        gctl |
        HDA_GCTL_CRST
    );

    bool wyszedl_z_reset =
        false;

    for (uint32_t i = 0;
         i <
            HDA_TIMEOUT_RESET;
         ++i) {

        if ((mmio_odczytaj32(
                 HDA_REG_GCTL) &
             HDA_GCTL_CRST) != 0) {

            wyszedl_z_reset =
                true;

            break;
        }

        pause_cpu();
    }

    if (!wyszedl_z_reset) {
        wypisz_log(
            "[HDA] Timeout wyjscia kontrolera z resetu."
        );

        return false;
    }

    hda_gcap =
        mmio_odczytaj16(
            HDA_REG_GCAP
        );

    if (hda_gcap ==
        UINT16_MAX) {

        return false;
    }

    const uint32_t oss =
        (hda_gcap &
         HDA_GCAP_OSS_MASK) >>
        HDA_GCAP_OSS_SHIFT;

    if (oss == 0) {
        wypisz_log(
            "[HDA] Kontroler nie posiada Output Stream."
        );

        return false;
    }

    /*
     * Stereo 16-bit przy 48 kHz zuzywa 2 slowa 16-bit na frame.
     */
    const uint16_t outpay =
        mmio_odczytaj16(
            HDA_REG_OUTPAY
        );

    const uint16_t outstrmpay =
        mmio_odczytaj16(
            HDA_REG_OUTSTRMPAY
        );

    if ((outpay != 0 &&
         outpay < 2U) ||
        (outstrmpay != 0 &&
         outstrmpay < 2U)) {

        wypisz_log(
            "[HDA] Za mala przepustowosc dla 48k/16/stereo."
        );

        return false;
    }

    const uint32_t iss =
        (hda_gcap &
         HDA_GCAP_ISS_MASK) >>
        HDA_GCAP_ISS_SHIFT;

    const uint64_t stream =
        hda_mmio_base +
        HDA_STREAM_BASE +
        static_cast<uint64_t>(
            iss) *
        HDA_STREAM_STRIDE;

    if (stream <
            hda_mmio_base ||
        stream >
            hda_mmio_base +
            HDA_MMIO_ROZMIAR -
            HDA_STREAM_STRIDE) {

        return false;
    }

    hda_stream_base =
        stream;

    return true;
}

/* =========================================================================
 * 11. WALCLK
 * ========================================================================= */

bool czekaj_walclk_tiki(
    uint32_t tiki,
    uint32_t maks_stagnacji
) {
    if (tiki == 0) {
        return true;
    }

    const uint32_t start =
        mmio_odczytaj32(
            HDA_REG_WALCLK
        );

    if (start ==
        UINT32_MAX) {

        return false;
    }

    uint32_t ostatni =
        start;

    uint32_t stagnacja =
        0;

    for (;;) {
        const uint32_t teraz =
            mmio_odczytaj32(
                HDA_REG_WALCLK
            );

        if (teraz ==
            UINT32_MAX) {

            return false;
        }

        /*
         * unsigned subtraction jest poprawna takze przez wrap uint32_t.
         */
        if (static_cast<uint32_t>(
                teraz -
                start) >=
            tiki) {

            return true;
        }

        if (teraz ==
            ostatni) {

            ++stagnacja;

            if (stagnacja >=
                maks_stagnacji) {

                return false;
            }
        } else {
            ostatni =
                teraz;

            stagnacja =
                0;
        }

        pause_cpu();
    }
}

/* =========================================================================
 * 12. IMMEDIATE COMMAND
 * ========================================================================= */

uint32_t verb12(
    uint8_t cad,
    uint8_t nid,
    uint16_t verb,
    uint8_t payload
) {
    return
        (static_cast<uint32_t>(
             cad &
             0x0FU) << 28) |
        (static_cast<uint32_t>(
             nid) << 20) |
        (static_cast<uint32_t>(
             verb &
             0x0FFFU) << 8) |
        static_cast<uint32_t>(
            payload
        );
}

uint32_t verb4(
    uint8_t cad,
    uint8_t nid,
    uint8_t verb,
    uint16_t payload
) {
    return
        (static_cast<uint32_t>(
             cad &
             0x0FU) << 28) |
        (static_cast<uint32_t>(
             nid) << 20) |
        (static_cast<uint32_t>(
             verb &
             0x0FU) << 16) |
        static_cast<uint32_t>(
            payload
        );
}

bool wyslij_immediate(
    uint32_t komenda,
    uint32_t* odpowiedz
) {
    if (hda_mmio_base == 0 ||
        !odpowiedz) {

        return false;
    }

    /*
     * Poczekaj az ICB=0.
     */
    bool gotowy =
        false;

    for (uint32_t i = 0;
         i <
            HDA_TIMEOUT_IMMEDIATE;
         ++i) {

        const uint16_t icis =
            mmio_odczytaj16(
                HDA_REG_ICIS
            );

        if (icis ==
            UINT16_MAX) {

            return false;
        }

        if ((icis &
             HDA_ICIS_ICB) == 0) {

            gotowy =
                true;

            break;
        }

        pause_cpu();
    }

    if (!gotowy) {
        /*
         * Spec dopuszcza wyczyszczenie ICB przy timeout, gdy CORB nie dziala.
         */
        mmio_zapisz16(
            HDA_REG_ICIS,
            0
        );

        for (uint32_t i = 0;
             i <
                HDA_TIMEOUT_IMMEDIATE;
             ++i) {

            if ((mmio_odczytaj16(
                     HDA_REG_ICIS) &
                 HDA_ICIS_ICB) == 0) {

                gotowy =
                    true;

                break;
            }

            pause_cpu();
        }
    }

    if (!gotowy) {
        return false;
    }

    /*
     * IRV jest RW1C. Przed nowym command usuwamy stara odpowiedz.
     */
    mmio_zapisz16(
        HDA_REG_ICIS,
        HDA_ICIS_IRV
    );

    mmio_zapisz32(
        HDA_REG_ICOI,
        komenda
    );

    /*
     * ICB=1 uruchamia PIO verb.
     */
    mmio_zapisz16(
        HDA_REG_ICIS,
        HDA_ICIS_ICB
    );

    bool odpowiedz_gotowa =
        false;

    for (uint32_t i = 0;
         i <
            HDA_TIMEOUT_IMMEDIATE;
         ++i) {

        const uint16_t icis =
            mmio_odczytaj16(
                HDA_REG_ICIS
            );

        if (icis ==
            UINT16_MAX) {

            return false;
        }

        if ((icis &
             HDA_ICIS_IRV) != 0 &&
            (icis &
             HDA_ICIS_ICB) == 0) {

            odpowiedz_gotowa =
                true;

            break;
        }

        pause_cpu();
    }

    if (!odpowiedz_gotowa) {
        /*
         * Best-effort recovery zalecany dla timeoutu Immediate Command.
         */
        mmio_zapisz16(
            HDA_REG_ICIS,
            0
        );

        return false;
    }

    *odpowiedz =
        mmio_odczytaj32(
            HDA_REG_ICII
        );

    /*
     * Skonsumowana odpowiedz.
     */
    mmio_zapisz16(
        HDA_REG_ICIS,
        HDA_ICIS_IRV
    );

    return true;
}

/* =========================================================================
 * 13. KODEK QEMU
 * ========================================================================= */

bool znajdz_kodek() {
    /*
     * Intel HDA wymaga odczekania minimum 521 us po CRST=1 przed
     * zalozeniem, ze wszystkie kodeki zakonczyly enumeracje.
     */
    if (!czekaj_walclk_tiki(
            HDA_CODEC_ENUM_TICKS,
            HDA_TIMEOUT_RESET)) {

        wypisz_log(
            "[HDA] WALCLK nie dziala podczas enumeracji kodeka."
        );

        return false;
    }

    const uint16_t states =
        static_cast<uint16_t>(
            mmio_odczytaj16(
                HDA_REG_STATESTS
            ) &
            0x7FFFU
        );

    /*
     * W typowym QEMU hda-output kodek ma CAD=0.
     * Jezeli STATESTS nie zostalo ustawione, wykonujemy bezpieczny fallback
     * tylko do CAD=0, bo testowy model QEMU jest jedno-kodekowy.
     */
    uint8_t kandydat =
        0xFFU;

    for (uint8_t cad = 0;
         cad < 15U;
         ++cad) {

        if ((states &
             (1U << cad)) != 0) {

            kandydat =
                cad;

            break;
        }
    }

    if (kandydat ==
        0xFFU) {

        kandydat =
            0U;
    }

    uint32_t vendor =
        0;

    if (!wyslij_immediate(
            verb12(
                kandydat,
                0,
                HDA_VERB_GET_PARAMETER,
                HDA_PARAMETER_VENDOR_ID
            ),
            &vendor)) {

        wypisz_log(
            "[HDA] Immediate Command nie odpowiedzial."
        );

        return false;
    }

    const uint16_t vendor_id =
        static_cast<uint16_t>(
            vendor >>
            16
        );

    if (vendor_id !=
        QEMU_HDA_VENDOR_ID) {

        /*
         * Obecne verb-y NID 2/3 sa topologia QEMU. Dla prawdziwego kodeka
         * trzeba najpierw zaimplementowac parser Function Group/Widgets.
         */
        wypisz_log(
            "[HDA] Wykryto kodek HDA, ale jego topologia nie jest jeszcze obslugiwana."
        );

        return false;
    }

    hda_codec_addr =
        kandydat;

    /*
     * STATESTS jest RW1C.
     */
    if (states != 0) {
        mmio_zapisz16(
            HDA_REG_STATESTS,
            states
        );
    }

    return true;
}

bool konfiguruj_kodek_qemu() {
    if (hda_codec_addr >
        14U) {

        return false;
    }

    uint32_t odpowiedz =
        0;

    /*
     * Function Group + widgets do D0.
     * QEMU odpowiada zerem i akceptuje te verb-y.
     */
    if (!wyslij_immediate(
            verb12(
                hda_codec_addr,
                QEMU_NID_AUDIO_FUNCTION_GROUP,
                HDA_VERB_SET_POWER_STATE,
                HDA_POWER_D0
            ),
            &odpowiedz)) {

        return false;
    }

    if (!wyslij_immediate(
            verb12(
                hda_codec_addr,
                QEMU_NID_DAC,
                HDA_VERB_SET_POWER_STATE,
                HDA_POWER_D0
            ),
            &odpowiedz)) {

        return false;
    }

    /*
     * DAC: taki sam format jak DMA stream.
     *
     * SET_STREAM_FORMAT zajmuje pelne dolne 20 bitow command word:
     * verb 0x2 + 16-bitowy format. Budujemy go jawnie, aby kod pozostal
     * poprawny takze dla formatow, ktorych payload nie miesci sie w 8 bitach.
     */
    const uint32_t set_format =
        (static_cast<uint32_t>(
             hda_codec_addr) << 28) |
        (static_cast<uint32_t>(
             QEMU_NID_DAC) << 20) |
        (static_cast<uint32_t>(
             HDA_VERB_SET_STREAM_FORMAT) << 8) |
        static_cast<uint32_t>(
            HDA_FORMAT_48K_16_STEREO
        );

    if (!wyslij_immediate(
            set_format,
            &odpowiedz)) {

        return false;
    }

    /*
     * Stream 1, channel 0.
     */
    if (!wyslij_immediate(
            verb12(
                hda_codec_addr,
                QEMU_NID_DAC,
                HDA_VERB_SET_CHANNEL_STREAMID,
                static_cast<uint8_t>(
                    HDA_STREAM_TAG <<
                    4
                )
            ),
            &odpowiedz)) {

        return false;
    }

    /*
     * Unmute output amplifier convertera.
     */
    const uint16_t amp_payload =
        HDA_AMP_SET_OUTPUT |
        HDA_AMP_SET_LEFT |
        HDA_AMP_SET_RIGHT |
        QEMU_HDA_GAIN;

    if (!wyslij_immediate(
            verb4(
                hda_codec_addr,
                QEMU_NID_DAC,
                HDA_VERB4_SET_AMP_GAIN_MUTE,
                amp_payload
            ),
            &odpowiedz)) {

        return false;
    }

    /*
     * Pin NID3 jako output.
     */
    if (!wyslij_immediate(
            verb12(
                hda_codec_addr,
                QEMU_NID_LINE_OUT,
                HDA_VERB_SET_PIN_WIDGET_CONTROL,
                HDA_PIN_OUT_ENABLE
            ),
            &odpowiedz)) {

        return false;
    }

    return true;
}

/* =========================================================================
 * 14. DMA
 * ========================================================================= */

void zwolnij_dma() {
    if (hda_bdl_phys) {
        ZwolnijRamke(
            hda_bdl_phys
        );
    }

    hda_bdl_phys =
        nullptr;

    hda_bdl =
        nullptr;

    for (uint32_t i = 0;
         i <
            HDA_MAKS_BUFOROW_DMA;
         ++i) {

        if (hda_bufory_phys[i]) {
            ZwolnijRamke(
                hda_bufory_phys[i]
            );

            hda_bufory_phys[i] =
                nullptr;
        }
    }
}

bool zaalokuj_dma() {
    zwolnij_dma();

    hda_bdl_phys =
        ZaalokujRamke();

    if (!fizyczny_dma_poprawny(
            hda_bdl_phys)) {

        zwolnij_dma();
        return false;
    }

    hda_bdl =
        static_cast<HdaBdlWpis*>(
            dma_wirtualny_z_fizycznego(
                hda_bdl_phys
            )
        );

    wyzeruj(
        hda_bdl,
        ROZMIAR_RAMKI_4K
    );

    for (uint32_t i = 0;
         i <
            HDA_MAKS_BUFOROW_DMA;
         ++i) {

        hda_bufory_phys[i] =
            ZaalokujRamke();

        if (!fizyczny_dma_poprawny(
                hda_bufory_phys[i])) {

            zwolnij_dma();
            return false;
        }

        wyzeruj(
            dma_wirtualny_z_fizycznego(
                hda_bufory_phys[i]
            ),
            ROZMIAR_RAMKI_4K
        );
    }

    return true;
}

/* =========================================================================
 * 15. RESET STREAMU
 * ========================================================================= */

bool czekaj_stream_ctl_bit(
    uint32_t bit,
    bool ustawiony
) {
    for (uint32_t i = 0;
         i <
            HDA_TIMEOUT_STREAM;
         ++i) {

        const bool stan =
            (stream_odczytaj_ctl() &
             bit) != 0;

        if (stan ==
            ustawiony) {

            return true;
        }

        pause_cpu();
    }

    return false;
}

bool resetuj_stream() {
    if (hda_stream_base == 0) {
        return false;
    }

    uint32_t ctl =
        stream_odczytaj_ctl();

    ctl &=
        ~(HDA_SD_CTL_RUN |
          HDA_SD_CTL_IRQ_MASK);

    stream_zapisz_ctl(
        ctl
    );

    if (!czekaj_stream_ctl_bit(
            HDA_SD_CTL_RUN,
            false)) {

        return false;
    }

    ctl =
        stream_odczytaj_ctl();

    ctl |=
        HDA_SD_CTL_SRST;

    ctl &=
        ~HDA_SD_CTL_RUN;

    stream_zapisz_ctl(
        ctl
    );

    if (!czekaj_stream_ctl_bit(
            HDA_SD_CTL_SRST,
            true)) {

        return false;
    }

    ctl =
        stream_odczytaj_ctl();

    ctl &=
        ~HDA_SD_CTL_SRST;

    stream_zapisz_ctl(
        ctl
    );

    if (!czekaj_stream_ctl_bit(
            HDA_SD_CTL_SRST,
            false)) {

        return false;
    }

    stream_wyczysc_sts();

    return true;
}

/* =========================================================================
 * 16. GENERATOR CYKLICZNEGO TONU
 * ========================================================================= */

int16_t probka_trojkatna(
    uint32_t faza
) {
    /*
     * faza: 0..47999.
     * Przeliczamy na 16-bitowy kat i tworzymy triangle wave bez FP/SSE.
     */
    const uint32_t p =
        static_cast<uint32_t>(
            (static_cast<uint64_t>(
                 faza) *
             UINT64_C(65536)) /
            HDA_SAMPLE_RATE
        );

    int32_t v =
        0;

    if (p < 0x4000U) {
        v =
            static_cast<int32_t>(
                p
            );
    } else if (p < 0xC000U) {
        v =
            static_cast<int32_t>(
                0x8000U -
                p
            );
    } else {
        v =
            static_cast<int32_t>(
                p
            ) -
            0x10000;
    }

    /*
     * ~75% maksymalnej amplitudy, aby pozostawic zapas.
     */
    v =
        (v *
         3) /
        4;

    if (v >
        32767) {

        v =
            32767;
    }

    if (v <
        -32768) {

        v =
            -32768;
    }

    return
        static_cast<int16_t>(
            v
        );
}

bool przygotuj_okres_tonu(
    uint32_t hz,
    uint32_t* bdl_count,
    uint32_t* cbl_bytes
) {
    if (!bdl_count ||
        !cbl_bytes ||
        hz < HDA_MIN_HZ ||
        hz > HDA_MAX_HZ) {

        return false;
    }

    const uint32_t gcd =
        nwd_u32(
            HDA_SAMPLE_RATE,
            hz
        );

    if (gcd == 0) {
        return false;
    }

    const uint32_t okres_probek =
        HDA_SAMPLE_RATE /
        gcd;

    const uint64_t bytes64 =
        static_cast<uint64_t>(
            okres_probek) *
        HDA_BAJTY_RAMKI_AUDIO;

    if (bytes64 <
            8U ||
        bytes64 >
            HDA_MAKS_BAJTOW_OKRESU) {

        return false;
    }

    const uint32_t bytes =
        static_cast<uint32_t>(
            bytes64
        );

    uint32_t count =
        (bytes +
         HDA_DMA_BUFOR_BAJTOW -
         1U) /
        HDA_DMA_BUFOR_BAJTOW;

    if (count < 2U) {
        count =
            2U;
    }

    if (count >
        HDA_MAKS_BDL) {

        return false;
    }

    /*
     * Wyzeruj tylko wykorzystywane bufory oraz BDL.
     */
    wyzeruj(
        hda_bdl,
        ROZMIAR_RAMKI_4K
    );

    for (uint32_t i = 0;
         i < count;
         ++i) {

        wyzeruj(
            dma_wirtualny_z_fizycznego(
                hda_bufory_phys[i]
            ),
            ROZMIAR_RAMKI_4K
        );
    }

    uint32_t pozostalo =
        bytes;

    /*
     * Gdy caly okres miesci sie w jednej stronie, BDL nadal musi miec
     * co najmniej dwa wpisy. Dzielimy okres na dwie 4-byte aligned czesci.
     */
    uint32_t specjalny_pierwszy =
        0;

    if (bytes <=
        HDA_DMA_BUFOR_BAJTOW) {

        specjalny_pierwszy =
            (bytes /
             2U) &
            ~UINT32_C(3);

        if (specjalny_pierwszy <
            4U) {

            specjalny_pierwszy =
                4U;
        }

        if (bytes -
                specjalny_pierwszy <
            4U) {

            specjalny_pierwszy =
                bytes -
                4U;
        }
    }

    uint32_t faza =
        0;

    uint32_t globalna_probka =
        0;

    for (uint32_t i = 0;
         i < count;
         ++i) {

        uint32_t len =
            0;

        if (bytes <=
            HDA_DMA_BUFOR_BAJTOW) {

            len =
                (i == 0)
                    ? specjalny_pierwszy
                    : bytes -
                        specjalny_pierwszy;
        } else {
            len =
                pozostalo >
                        HDA_DMA_BUFOR_BAJTOW
                    ? HDA_DMA_BUFOR_BAJTOW
                    : pozostalo;
        }

        if (len < 4U ||
            (len &
             3U) != 0) {

            return false;
        }

        void* phys =
            hda_bufory_phys[i];

        if (!fizyczny_dma_poprawny(
                phys)) {

            return false;
        }

        uint8_t* bufor =
            static_cast<uint8_t*>(
                dma_wirtualny_z_fizycznego(
                    phys
                )
            );

        const uint32_t frames =
            len /
            HDA_BAJTY_RAMKI_AUDIO;

        for (uint32_t f = 0;
             f < frames;
             ++f) {

            const int16_t sample =
                probka_trojkatna(
                    faza
                );

            const uint32_t off =
                f *
                HDA_BAJTY_RAMKI_AUDIO;

            /*
             * Little-endian PCM16 stereo.
             * Piszemy bajtowo, aby nie zalezec od alignment int16_t.
             */
            const uint16_t u =
                static_cast<uint16_t>(
                    sample
                );

            bufor[off + 0U] =
                static_cast<uint8_t>(
                    u &
                    0xFFU
                );

            bufor[off + 1U] =
                static_cast<uint8_t>(
                    u >>
                    8
                );

            bufor[off + 2U] =
                bufor[off + 0U];

            bufor[off + 3U] =
                bufor[off + 1U];

            faza +=
                hz;

            if (faza >=
                HDA_SAMPLE_RATE) {

                faza -=
                    HDA_SAMPLE_RATE;
            }

            ++globalna_probka;
        }

        hda_bdl[i].adres =
            reinterpret_cast<uint64_t>(
                phys
            );

        hda_bdl[i].dlugosc =
            len;

        /*
         * IOC=0: sterownik pollingowy, nie generujemy IRQ.
         */
        hda_bdl[i].flagi =
            0;

        if (pozostalo >=
            len) {

            pozostalo -=
                len;
        } else {
            pozostalo =
                0;
        }
    }

    if (globalna_probka !=
            okres_probek ||
        faza != 0) {

        /*
         * Po calym okresie faza musi wrocic do 0, inaczej cykliczne
         * odtwarzanie mialoby skok na granicy CBL.
         */
        return false;
    }

    *bdl_count =
        count;

    *cbl_bytes =
        bytes;

    return true;
}

/* =========================================================================
 * 17. PROGRAMOWANIE OUTPUT STREAM
 * ========================================================================= */

bool zaprogramuj_stream(
    uint32_t bdl_count,
    uint32_t cbl_bytes
) {
    if (!hda_bdl ||
        !hda_bdl_phys ||
        hda_stream_base == 0 ||
        bdl_count < 2U ||
        bdl_count >
            HDA_MAKS_BDL ||
        cbl_bytes == 0 ||
        (cbl_bytes %
         HDA_BAJTY_RAMKI_AUDIO) != 0) {

        return false;
    }

    if (!resetuj_stream()) {
        wypisz_log(
            "[HDA] Nie udalo sie zresetowac Output Stream."
        );

        return false;
    }

    const uint32_t base =
        static_cast<uint32_t>(
            hda_stream_base -
            hda_mmio_base
        );

    const uint64_t bdl_phys =
        reinterpret_cast<uint64_t>(
            hda_bdl_phys
        );

    if ((bdl_phys &
         0x7FU) != 0) {

        return false;
    }

    if ((hda_gcap &
         HDA_GCAP_64OK) == 0 &&
        (bdl_phys >>
         32) != 0) {

        return false;
    }

    mmio_zapisz32(
        base +
        HDA_SD_CBL,
        cbl_bytes
    );

    mmio_zapisz16(
        base +
        HDA_SD_LVI,
        static_cast<uint16_t>(
            bdl_count -
            1U
        )
    );

    mmio_zapisz16(
        base +
        HDA_SD_FMT,
        HDA_FORMAT_48K_16_STEREO
    );

    mmio_zapisz32(
        base +
        HDA_SD_BDPL,
        static_cast<uint32_t>(
            bdl_phys &
            UINT64_C(0xFFFFFFFF)
        )
    );

    mmio_zapisz32(
        base +
        HDA_SD_BDPU,
        static_cast<uint32_t>(
            bdl_phys >>
            32
        )
    );

    /*
     * Stream 1, bez DEIE/FEIE/IOCE.
     */
    uint32_t ctl =
        stream_odczytaj_ctl();

    ctl &=
        ~(HDA_SD_CTL_STREAM_MASK |
          HDA_SD_CTL_IRQ_MASK |
          HDA_SD_CTL_SRST |
          HDA_SD_CTL_RUN);

    ctl |=
        static_cast<uint32_t>(
            HDA_STREAM_TAG) <<
        HDA_SD_CTL_STREAM_SHIFT;

    stream_zapisz_ctl(
        ctl
    );

    stream_wyczysc_sts();

    bariera_dma_przed_urzadzeniem();

    return true;
}

/* =========================================================================
 * 18. START / STOP STREAMU
 * ========================================================================= */

bool uruchom_stream() {
    stream_wyczysc_sts();

    uint32_t ctl =
        stream_odczytaj_ctl();

    ctl &=
        ~(HDA_SD_CTL_SRST |
          HDA_SD_CTL_IRQ_MASK);

    ctl |=
        HDA_SD_CTL_RUN;

    stream_zapisz_ctl(
        ctl
    );

    if (!czekaj_stream_ctl_bit(
            HDA_SD_CTL_RUN,
            true)) {

        return false;
    }

    /*
     * Czekamy na FIFO ready albo na blad.
     * QEMU zwykle ustawia FIFORDY bardzo szybko.
     */
    for (uint32_t i = 0;
         i <
            HDA_TIMEOUT_DMA_START;
         ++i) {

        const uint8_t sts =
            stream_odczytaj_sts();

        if ((sts &
             (HDA_SD_STS_DESE |
              HDA_SD_STS_FIFOE)) != 0) {

            return false;
        }

        if ((sts &
             HDA_SD_STS_FIFORDY) != 0) {

            return true;
        }

        /*
         * Nie wszystkie implementacje musza uzywac FIFORDY identycznie.
         * Jezeli LPIB zaczal rosnac, DMA na pewno ruszylo.
         */
        const uint32_t base =
            static_cast<uint32_t>(
                hda_stream_base -
                hda_mmio_base
            );

        if (mmio_odczytaj32(
                base +
                HDA_SD_LPIB) != 0) {

            return true;
        }

        pause_cpu();
    }

    return false;
}

bool zatrzymaj_stream_wewnetrznie() {
    if (hda_stream_base == 0) {
        return true;
    }

    uint32_t ctl =
        stream_odczytaj_ctl();

    ctl &=
        ~HDA_SD_CTL_RUN;

    stream_zapisz_ctl(
        ctl
    );

    const bool zatrzymany =
        czekaj_stream_ctl_bit(
            HDA_SD_CTL_RUN,
            false
        );

    bariera_dma_po_urzadzeniu();

    stream_wyczysc_sts();

    return
        zatrzymany;
}

/* =========================================================================
 * 19. ODTWARZANIE PRZEZ WALCLK
 * ========================================================================= */

bool czekaj_na_czas_tonu(
    uint32_t czas_ms
) {
    const uint64_t target64 =
        static_cast<uint64_t>(
            czas_ms) *
        HDA_WALCLK_TICKS_PER_MS;

    if (target64 == 0 ||
        target64 >
            UINT32_MAX) {

        return false;
    }

    const uint32_t target =
        static_cast<uint32_t>(
            target64
        );

    const uint32_t start =
        mmio_odczytaj32(
            HDA_REG_WALCLK
        );

    if (start ==
        UINT32_MAX) {

        return false;
    }

    uint32_t ostatni =
        start;

    uint32_t stagnacja =
        0;

    for (;;) {
        const uint8_t sts =
            stream_odczytaj_sts();

        if ((sts &
             (HDA_SD_STS_DESE |
              HDA_SD_STS_FIFOE)) != 0) {

            return false;
        }

        const uint32_t teraz =
            mmio_odczytaj32(
                HDA_REG_WALCLK
            );

        if (teraz ==
            UINT32_MAX) {

            return false;
        }

        if (static_cast<uint32_t>(
                teraz -
                start) >=
            target) {

            return true;
        }

        if (teraz ==
            ostatni) {

            ++stagnacja;

            if (stagnacja >=
                HDA_TIMEOUT_RESET) {

                return false;
            }
        } else {
            ostatni =
                teraz;

            stagnacja =
                0;
        }

        pause_cpu();
    }
}

/* =========================================================================
 * 20. CLEANUP
 * ========================================================================= */

void zatrzymaj_hardware() {
    if (hda_mmio_base == 0) {
        return;
    }

    (void)zatrzymaj_stream_wewnetrznie();

    mmio_zapisz32(
        HDA_REG_INTCTL,
        0
    );

    mmio_zapisz16(
        HDA_REG_WAKEEN,
        0
    );

    mmio_zapisz8(
        HDA_REG_CORBCTL,
        static_cast<uint8_t>(
            mmio_odczytaj8(
                HDA_REG_CORBCTL
            ) &
            ~HDA_CORBCTL_RUN
        )
    );

    mmio_zapisz8(
        HDA_REG_RIRBCTL,
        static_cast<uint8_t>(
            mmio_odczytaj8(
                HDA_REG_RIRBCTL
            ) &
            ~HDA_RIRBCTL_RUN
        )
    );
}

void anuluj_inicjalizacje() {
    __atomic_store_n(
        &hda_gotowy,
        false,
        __ATOMIC_RELEASE
    );

    zatrzymaj_hardware();

    zwolnij_dma();

    if (hda_mmio_base != 0) {
        wylacz_pci_busmaster(
            hda_bdf
        );
    }

    hda_mmio_base =
        0;

    hda_stream_base =
        0;

    hda_gcap =
        0;

    hda_codec_addr =
        0xFFU;
}

} // namespace

/* =========================================================================
 * 21. PUBLICZNA INICJALIZACJA
 * ========================================================================= */

bool inicjalizuj_hda() {
    if (__atomic_load_n(
            &hda_gotowy,
            __ATOMIC_ACQUIRE)) {

        return true;
    }

    GuardHDA guard(
        HDA_TIMEOUT_LOCK
    );

    if (!guard.aktywny()) {
        return false;
    }

    if (__atomic_load_n(
            &hda_gotowy,
            __ATOMIC_ACQUIRE)) {

        return true;
    }

    wypisz_log(
        "[HDA] Szukam kontrolera High Definition Audio..."
    );

    BdfPCI bdf{};

    if (!znajdz_hda(
            &bdf)) {

        wypisz_log(
            "[HDA] Nie znaleziono kontrolera PCI klasy 04:03."
        );

        return false;
    }

    hda_bdf =
        bdf;

    uint64_t bar0 =
        0;

    if (!pobierz_bar0(
            bdf,
            &bar0)) {

        wypisz_log(
            "[HDA] BAR0 nie jest poprawnym MMIO BAR."
        );

        return false;
    }

    if (!wlacz_pci_hda(
            bdf)) {

        wypisz_log(
            "[HDA] Nie udalo sie wlaczyc Memory Space/Bus Master."
        );

        return false;
    }

    if (!mapuj_mmio(
            bar0)) {

        wypisz_log(
            "[HDA] Nie udalo sie przygotowac MMIO."
        );

        wylacz_pci_busmaster(
            bdf
        );

        hda_mmio_base =
            0;

        return false;
    }

    if (!resetuj_kontroler()) {
        anuluj_inicjalizacje();
        return false;
    }

    /*
     * Polling od samego poczatku.
     */
    mmio_zapisz32(
        HDA_REG_INTCTL,
        0
    );

    if (!znajdz_kodek()) {
        anuluj_inicjalizacje();
        return false;
    }

    if (!zaalokuj_dma()) {
        wypisz_log(
            "[HDA] Brak ramek PMM dla DMA audio."
        );

        anuluj_inicjalizacje();
        return false;
    }

    if (!konfiguruj_kodek_qemu()) {
        wypisz_log(
            "[HDA] Nie udalo sie skonfigurowac kodeka QEMU."
        );

        anuluj_inicjalizacje();
        return false;
    }

    /*
     * Przygotuj stream w stanie zatrzymanym i zresetowanym.
     */
    if (!resetuj_stream()) {
        wypisz_log(
            "[HDA] Output Stream nie przeszedl resetu."
        );

        anuluj_inicjalizacje();
        return false;
    }

    __atomic_store_n(
        &hda_gotowy,
        true,
        __ATOMIC_RELEASE
    );

    wypisz_log(
        "[HDA] Kontroler i kodek QEMU HDA gotowe."
    );

    return true;
}

/* =========================================================================
 * 22. PUBLICZNY TON TESTOWY
 * ========================================================================= */

bool hda_test_ton(
    uint32_t czestotliwosc_hz,
    uint32_t czas_ms
) {
    if (!__atomic_load_n(
            &hda_gotowy,
            __ATOMIC_ACQUIRE) ||
        czestotliwosc_hz <
            HDA_MIN_HZ ||
        czestotliwosc_hz >
            HDA_MAX_HZ ||
        czas_ms == 0 ||
        czas_ms >
            HDA_MAX_MS) {

        return false;
    }

    GuardHDA guard(
        HDA_TIMEOUT_LOCK
    );

    if (!guard.aktywny()) {
        return false;
    }

    /*
     * Zawsze zaczynamy od zatrzymanego silnika.
     */
    if (!zatrzymaj_stream_wewnetrznie()) {
        return false;
    }

    uint32_t bdl_count =
        0;

    uint32_t cbl_bytes =
        0;

    if (!przygotuj_okres_tonu(
            czestotliwosc_hz,
            &bdl_count,
            &cbl_bytes)) {

        return false;
    }

    /*
     * Codec i kontroler musza miec identyczny format oraz Stream Tag.
     * Ponawiamy konfiguracje przed odtworzeniem, bo stream reset nie resetuje
     * kodeka, ale inne przyszle operacje audio moga go przestawic.
     */
    if (!konfiguruj_kodek_qemu()) {
        return false;
    }

    if (!zaprogramuj_stream(
            bdl_count,
            cbl_bytes)) {

        return false;
    }

    if (!uruchom_stream()) {
        (void)zatrzymaj_stream_wewnetrznie();

        wypisz_log(
            "[HDA] Output DMA nie wystartowalo."
        );

        return false;
    }

    const bool czas_ok =
        czekaj_na_czas_tonu(
            czas_ms
        );

    const bool stop_ok =
        zatrzymaj_stream_wewnetrznie();

    if (!czas_ok) {
        wypisz_log(
            "[HDA] Blad/timeout podczas odtwarzania tonu."
        );
    }

    return
        czas_ok &&
        stop_ok;
}

/* =========================================================================
 * 23. PUBLICZNY STOP
 * ========================================================================= */

void hda_stop() {
    if (!__atomic_load_n(
            &hda_gotowy,
            __ATOMIC_ACQUIRE)) {

        return;
    }

    GuardHDA guard(
        HDA_TIMEOUT_LOCK
    );

    if (!guard.aktywny()) {
        return;
    }

    (void)zatrzymaj_stream_wewnetrznie();
}
