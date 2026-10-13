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
#include "hda_diag_format.h"
#include "hda_wewnetrzny.h"
#include "hda_transport.h"
#include "hda_diag.h"
#include "../czas/hpet.h"
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

uint32_t hda_kontroler_odtwarzania = UINT32_MAX;

uint64_t hda_mmio_base =
    0;

uint64_t hda_stream_base =
    0;

uint16_t hda_gcap =
    0;

uint8_t hda_codec_addr =
    0xFFU;

uint16_t hda_initial_statests =
    0;

uint16_t hda_discovered_codec_mask =
    0;

uint16_t hda_responsive_codec_mask =
    0;

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
    if (!phys) return nullptr;

    return FizycznyZakresDoWirtualnego(
        reinterpret_cast<uint64_t>(phys),
        ROZMIAR_RAMKI_4K
    );
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

    return CzyZakresWMapowaniuFizycznym(
        p,
        ROZMIAR_RAMKI_4K
    );
}

/* =========================================================================
 * 6. BLOKADA
 * ========================================================================= */

// Playback ownership reservation: one CAS, never spin waiting for another caller.
// Hardware command ownership lives independently in each controller transport.
bool hda_sprobuj_zablokowac(uint32_t) {
    bool expected=false;
    return __atomic_compare_exchange_n(&hda_blokada,&expected,true,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
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
    uint32_t cmd = 0;
    if (!hda_zbuduj_verb12(cad, nid, verb, payload, &cmd)) {
        return 0;
    }
    return cmd;
}

uint32_t verb4(
    uint8_t cad,
    uint8_t nid,
    uint8_t verb,
    uint16_t payload
) {
    uint32_t cmd = 0;
    if (!hda_zbuduj_verb4(cad, nid, verb, payload, &cmd)) {
        return 0;
    }
    return cmd;
}

struct HdaImmediateDiagLog {
    uint16_t icis_before;
    uint16_t icis_after_stale;
    uint32_t icoi_written;
    uint16_t icis_after_busy;
    uint16_t icis_completion;
    uint32_t icii_response;
    uint32_t elapsed_us;
    HdaTransportStatus status;
};

// Boot/legacy compatibility wrapper. BWS89 NEVER calls this function.
// No transport lock is held between service phases.
HdaTransportStatus hda_transport_send_immediate_ex(uint32_t cmd, uint32_t* out,
                                                  HdaImmediateDiagLog* diag=nullptr) {
    if(!out || hda_kontroler_odtwarzania==UINT32_MAX || !hpet_dostepny()) return HDA_TRANSPORT_NOT_READY;
    const uint32_t k=hda_kontroler_odtwarzania;
    auto status=hda_transport_zglos(k,HDA_WLASCICIEL_BOOT,cmd);
    if(status!=HDA_TRANSPORT_PENDING) return status;
    const uint64_t start=czas_monotoniczny_ns();
    for(uint32_t guard=0;guard<2000000;++guard) {
        status=hda_transport_krok(k,HDA_WLASCICIEL_BOOT);
        if(status!=HDA_TRANSPORT_PENDING) break;
        if(czas_monotoniczny_ns()-start>100000000ULL) { hda_transport_anuluj(k,HDA_WLASCICIEL_BOOT); return HDA_TRANSPORT_TIMEOUT; }
        pause_cpu();
    }
    if(status==HDA_TRANSPORT_PENDING) { hda_transport_anuluj(k,HDA_WLASCICIEL_BOOT); return HDA_TRANSPORT_TIMEOUT; }
    status=hda_transport_odbierz(k,HDA_WLASCICIEL_BOOT,out);
    if(diag) { *diag={}; diag->status=status; diag->icii_response=*out; }
    return status;
}

bool wyslij_immediate(
    uint32_t komenda,
    uint32_t* odpowiedz
) {
    return hda_transport_send_immediate_ex(komenda, odpowiedz) == HDA_TRANSPORT_SUCCESS;
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
     * Zachowaj poczatkowy STATESTS zanim zostanie skasowany (W1C).
     * Diagnostyka potrzebuje tej informacji nawet po acknowledge.
     */
    hda_initial_statests |= states;

    hda_discovered_codec_mask |= states;

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

        if (((states | hda_initial_statests) &
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
        QEMU_HDA_VENDOR_ID || (uint16_t(vendor)!=0x0012 && uint16_t(vendor)!=0x0022)) {

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

} // namespace

/* =========================================================================
 * 21. PUBLICZNA INICJALIZACJA
 * ========================================================================= */

bool inicjalizuj_hda() {
    if(hda_gotowy) return true;
    // Called once during boot. BWS89 never enumerates PCI or calls synchronous initialization.
    if(!hda_transport_wykryj()) return false;
    (void)hda_diag_inicjalizuj();
    bool any=false;
    for(uint32_t k=0;k<hda_transport_liczba();++k) {
        const uint64_t start=czas_monotoniczny_ns();
        for(uint32_t steps=0;steps<2000000;++steps) {
            if(hda_transport_init_krok(k)) break;
            if(czas_monotoniczny_ns()-start>100000000ULL) break;
            pause_cpu();
        }
        HdaDiagKontroler info{};
        if(!hda_transport_stan(k,&info) || !info.gotowy) continue;
        any=true;
        // Physical controllers stay read-only, with independent CORB/RIRB ready for discovery.
        // Only the accepted emulated controller gets the existing QEMU playback setup.
        if(info.vendor_id!=0x8086 || info.device_id!=0x2668 || hda_gotowy) continue;
        hda_kontroler_odtwarzania=k;
        hda_mmio_base=hda_transport_mmio(k); hda_gcap=info.gcap;
        hda_bdf={info.bus,info.slot,info.func};
        hda_initial_statests=info.initial_statests;
        hda_discovered_codec_mask=info.discovered_codec_mask;
        hda_stream_base=hda_mmio_base+0x80+uint64_t(info.iss)*0x20;
        if(!znajdz_kodek() || !zaalokuj_dma() || !konfiguruj_kodek_qemu() || !resetuj_stream()) {
            zwolnij_dma(); hda_mmio_base=0; hda_stream_base=0; hda_kontroler_odtwarzania=UINT32_MAX;
            continue; // Keep registry and precise initialization state, including other controllers.
        }
        __atomic_store_n(&hda_gotowy,true,__ATOMIC_RELEASE);
        hda_transport_test_dostepny(k,true);
    }
    return any;
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

/* =========================================================================
 * 24. WEWNĘTRZNE API DIAGNOSTYCZNE (hda_diag.cpp)
 * =========================================================================
 *
 * Eksportowane wylacznie do warstwy diagnostycznej jadra.
 * Nigdy nie sa wystawiane przez BWS do Ring 3.
 */

#include "hda_wewnetrzny.h"
#include "hda_diag_format.h"

bool hda_wew_wyslij_immediate(uint32_t komenda, uint32_t* odpowiedz) {
    return wyslij_immediate(komenda, odpowiedz);
}

uint32_t hda_wew_verb12(uint8_t cad, uint8_t nid, uint16_t verb, uint8_t payload) {
    return verb12(cad, nid, verb, payload);
}

uint32_t hda_wew_verb4(uint8_t cad, uint8_t nid, uint8_t verb, uint16_t payload) {
    return verb4(cad, nid, verb, payload);
}

bool hda_wew_kontroler_gotowy() {
    return hda_transport_liczba() != 0;
}

bool hda_wew_pobierz_stan_kontrolera(HdaWewStanKontrolera* out) {
    if(!out) return false;
    HdaDiagKontroler c{};
    if(!hda_transport_stan(0,&c)) return false;
    *out={};
    out->bus=c.bus; out->slot=c.slot; out->func=c.func;
    out->vendor_id=c.vendor_id; out->device_id=c.device_id; out->revision=c.revision;
    out->command=c.command; out->bar0=c.bar0; out->bar0_dlugosc=c.bar0_dlugosc;
    out->hda_vmin=c.hda_vmin; out->hda_vmaj=c.hda_vmaj; out->gcap=c.gcap;
    out->gctl=c.gctl; out->statests=c.statests; out->oss=c.oss; out->iss=c.iss;
    out->bss=c.bss; out->dma64=c.dma64; out->initial_statests=c.initial_statests;
    out->discovered_codec_mask=c.discovered_codec_mask; out->responsive_codec_mask=c.responsive_codec_mask;
    return true;
}

bool hda_wew_pobierz_stan_strumienia(HdaWewStanStrumienia* out) {
    if (!out || hda_stream_base == 0) return false;

    out->numer = 1;
    out->stream_tag = HDA_STREAM_TAG;
    out->format_raw = mmio_odczytaj16(
        static_cast<uint32_t>(hda_stream_base - hda_mmio_base) + HDA_SD_FMT);
    out->cbl = mmio_odczytaj32(
        static_cast<uint32_t>(hda_stream_base - hda_mmio_base) + HDA_SD_CBL);
    out->lvi = mmio_odczytaj16(
        static_cast<uint32_t>(hda_stream_base - hda_mmio_base) + HDA_SD_LVI);
    out->lpib = mmio_odczytaj32(
        static_cast<uint32_t>(hda_stream_base - hda_mmio_base) + HDA_SD_LPIB);

    uint32_t ctl = stream_odczytaj_ctl();
    out->ctl = ctl;
    out->run = (ctl & HDA_SD_CTL_RUN) ? 1 : 0;

    const uint8_t sts = stream_odczytaj_sts();
    out->sts = sts;
    out->bcis = (sts & HDA_SD_STS_BCIS) ? 1 : 0;
    out->fifoe = (sts & HDA_SD_STS_FIFOE) ? 1 : 0;
    out->dese = (sts & HDA_SD_STS_DESE) ? 1 : 0;
    out->fifordy = (sts & HDA_SD_STS_FIFORDY) ? 1 : 0;

    /*
     * Sanitized BDL info: nie ujawniamy uzytkowalnego adresu fizycznego.
     * Tylko maska (dolne 12 bitow wyzerowane) i liczba wpisow.
     */
    out->bdl_wpisow = 0;
    out->bdl_maska_fiz = 0;
    out->bdl_uzywa_64 = static_cast<uint8_t>(
        (hda_gcap & HDA_GCAP_64OK) ? 1 : 0);
    if (hda_bdl) {
        uint32_t n = 0;
        for (uint32_t i = 0; i < HDA_MAKS_BDL; ++i) {
            if (hda_bdl[i].dlugosc == 0) break;
            ++n;
        }
        out->bdl_wpisow = n;
        const uint64_t phys = reinterpret_cast<uint64_t>(hda_bdl_phys);
        out->bdl_maska_fiz = static_cast<uint32_t>((phys >> 12) & 0xFFFFFU);
    }
    return true;
}

namespace {

struct StanTestuWew {
    uint8_t tryb = 0;
    uint8_t kanaly = 3;
    bool aktywny = false;
    uint64_t start_walclk = 0;
    uint32_t czas_ticks = 0;
    uint32_t hz_start = 0;
    uint32_t hz_koniec = 0;
};

StanTestuWew stan_test_wew;
uint8_t sciezka_test_cad = 0;
uint8_t sciezka_test_konw = 0;
uint8_t sciezka_test_pin = 0;
uint8_t sciezka_test_fizyczny = 0;

int16_t probka_ton_mask(int32_t v, uint8_t kanal_bit, uint8_t kanaly) {
    return (kanaly & kanal_bit) ? static_cast<int16_t>(v) : 0;
}

bool wypelnij_ton_kanaly(
    uint32_t hz,
    uint8_t kanaly,
    uint32_t* bdl_count,
    uint32_t* cbl_bytes
) {
    if (!bdl_count || !cbl_bytes) return false;
    if (!hda_api_czestotliwosc_poprawna(hz)) return false;

    const uint32_t gcd = nwd_u32(HDA_SAMPLE_RATE, hz);
    if (gcd == 0) return false;
    const uint32_t okres_probek = HDA_SAMPLE_RATE / gcd;
    const uint64_t bytes64 =
        static_cast<uint64_t>(okres_probek) * HDA_BAJTY_RAMKI_AUDIO;
    if (bytes64 < 8U || bytes64 > HDA_MAKS_BAJTOW_OKRESU) return false;
    const uint32_t bytes = static_cast<uint32_t>(bytes64);

    uint32_t count = (bytes + HDA_DMA_BUFOR_BAJTOW - 1U) / HDA_DMA_BUFOR_BAJTOW;
    if (count < 2U) count = 2U;
    if (count > HDA_MAKS_BDL) return false;

    wyzeruj(hda_bdl, ROZMIAR_RAMKI_4K);
    for (uint32_t i = 0; i < count; ++i) {
        wyzeruj(dma_wirtualny_z_fizycznego(hda_bufory_phys[i]), ROZMIAR_RAMKI_4K);
    }

    uint32_t specjalny_pierwszy = 0;
    if (bytes <= HDA_DMA_BUFOR_BAJTOW) {
        specjalny_pierwszy = (bytes / 2U) & ~UINT32_C(3);
        if (specjalny_pierwszy < 4U) specjalny_pierwszy = 4U;
        if (bytes - specjalny_pierwszy < 4U) specjalny_pierwszy = bytes - 4U;
    }

    uint32_t faza = 0;
    uint32_t globalna = 0;
    uint32_t pozostalo = bytes;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t len;
        if (bytes <= HDA_DMA_BUFOR_BAJTOW) {
            len = (i == 0) ? specjalny_pierwszy : bytes - specjalny_pierwszy;
        } else {
            len = pozostalo > HDA_DMA_BUFOR_BAJTOW ? HDA_DMA_BUFOR_BAJTOW : pozostalo;
        }
        if (len < 4U || (len & 3U) != 0) return false;

        uint8_t* bufor = static_cast<uint8_t*>(
            dma_wirtualny_z_fizycznego(hda_bufory_phys[i]));
        const uint32_t frames = len / HDA_BAJTY_RAMKI_AUDIO;
        for (uint32_t f = 0; f < frames; ++f) {
            const int16_t base = probka_trojkatna(faza);
            /* Amplituda ~50% (HDA_TEST_AMPLITUDA) zamiast 75%. */
            const int32_t v = (static_cast<int32_t>(base) * HDA_TEST_AMPLITUDA) / 24576;
            const int16_t l = probka_ton_mask(v, 1, kanaly);
            const int16_t r = probka_ton_mask(v, 2, kanaly);
            const uint32_t off = f * HDA_BAJTY_RAMKI_AUDIO;
            const uint16_t ul = static_cast<uint16_t>(l);
            const uint16_t ur = static_cast<uint16_t>(r);
            bufor[off + 0] = static_cast<uint8_t>(ul & 0xFF);
            bufor[off + 1] = static_cast<uint8_t>(ul >> 8);
            bufor[off + 2] = static_cast<uint8_t>(ur & 0xFF);
            bufor[off + 3] = static_cast<uint8_t>(ur >> 8);
            faza += hz;
            if (faza >= HDA_SAMPLE_RATE) faza -= HDA_SAMPLE_RATE;
            ++globalna;
        }
        hda_bdl[i].adres = reinterpret_cast<uint64_t>(hda_bufory_phys[i]);
        hda_bdl[i].dlugosc = len;
        hda_bdl[i].flagi = 0;
        if (pozostalo >= len) pozostalo -= len; else pozostalo = 0;
    }

    if (globalna != okres_probek || faza != 0) return false;
    *bdl_count = count;
    *cbl_bytes = bytes;
    return true;
}

bool wypelnij_sweep(
    uint32_t hz_start,
    uint32_t hz_koniec,
    uint8_t kanaly,
    uint32_t* bdl_count,
    uint32_t* cbl_bytes
) {
    /*
     * Krótki sweep 200..4000 Hz. Generujemy liniowy narastajacy
     * accumulator fazy (bez FP). Caly sweep miesci sie w ~1 s
     * (limit buforow DMA).
     */
    if (!bdl_count || !cbl_bytes) return false;
    if (hz_start < HDA_TEST_SWEEP_MIN_HZ) hz_start = HDA_TEST_SWEEP_MIN_HZ;
    if (hz_koniec > HDA_TEST_SWEEP_MAX_HZ) hz_koniec = HDA_TEST_SWEEP_MAX_HZ;
    if (hz_start > hz_koniec) return false;

    uint32_t count = HDA_MAKS_BUFOROW_DMA;
    /* Uzyjemy tylko tylu stron ile potrzeba na ~1 s. */
    const uint32_t docelowe = HDA_SAMPLE_RATE * HDA_BAJTY_RAMKI_AUDIO;
    uint32_t uzyte = docelowe / HDA_DMA_BUFOR_BAJTOW;
    if (uzyte < 2U) uzyte = 2U;
    if (uzyte > HDA_MAKS_BDL) uzyte = HDA_MAKS_BDL;
    count = uzyte;
    const uint32_t ramki = count * (HDA_DMA_BUFOR_BAJTOW / HDA_BAJTY_RAMKI_AUDIO);
    const uint32_t bytes_sweep = ramki * HDA_BAJTY_RAMKI_AUDIO;

    wyzeruj(hda_bdl, ROZMIAR_RAMKI_4K);
    for (uint32_t i = 0; i < count; ++i) {
        wyzeruj(dma_wirtualny_z_fizycznego(hda_bufory_phys[i]), ROZMIAR_RAMKI_4K);
    }

    uint32_t faza = 0;
    uint32_t globalna = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* bufor = static_cast<uint8_t*>(
            dma_wirtualny_z_fizycznego(hda_bufory_phys[i]));
        const uint32_t frames = HDA_DMA_BUFOR_BAJTOW / HDA_BAJTY_RAMKI_AUDIO;
        for (uint32_t f = 0; f < frames; ++f) {
            /*
             * Postep sweepu: interpolacja liniowa hz w Q16.
             */
            const uint64_t postep =
                (static_cast<uint64_t>(globalna) << 16) / ramki;
            const uint32_t hz = hz_start +
                static_cast<uint32_t>(
                    (static_cast<uint64_t>(hz_koniec - hz_start) * postep) >> 16);
            const int16_t base = probka_trojkatna(faza);
            const int32_t v = (static_cast<int32_t>(base) * HDA_TEST_AMPLITUDA) / 24576;
            const int16_t l = probka_ton_mask(v, 1, kanaly);
            const int16_t r = probka_ton_mask(v, 2, kanaly);
            const uint32_t off = f * HDA_BAJTY_RAMKI_AUDIO;
            const uint16_t ul = static_cast<uint16_t>(l);
            const uint16_t ur = static_cast<uint16_t>(r);
            bufor[off + 0] = static_cast<uint8_t>(ul & 0xFF);
            bufor[off + 1] = static_cast<uint8_t>(ul >> 8);
            bufor[off + 2] = static_cast<uint8_t>(ur & 0xFF);
            bufor[off + 3] = static_cast<uint8_t>(ur >> 8);
            faza += hz;
            if (faza >= HDA_SAMPLE_RATE) faza -= HDA_SAMPLE_RATE;
            ++globalna;
        }
        hda_bdl[i].adres = reinterpret_cast<uint64_t>(hda_bufory_phys[i]);
        hda_bdl[i].dlugosc = HDA_DMA_BUFOR_BAJTOW;
        hda_bdl[i].flagi = 0;
    }

    *bdl_count = count;
    *cbl_bytes = bytes_sweep;
    return true;
}

} // namespace

void hda_wew_pobierz_sciezke_testu(HdaWewSciezkaTestu* out) {
    if (!out) return;
    /*
     * Obecna implementacja zna stala sciezke QEMU (NID DAC/PIN).
     * Dla nieznanego kodeka nie zgadujemy routingu fizycznego.
     */
    if (hda_codec_addr != 0xFFU && hda_gotowy) {
        out->cad = hda_codec_addr;
        out->konwerter_nid = QEMU_NID_DAC;
        out->pin_nid = QEMU_NID_LINE_OUT;
        out->routing_fizyczny = 0;
    } else {
        out->cad = hda_codec_addr != 0xFFU ? hda_codec_addr : 0;
        out->konwerter_nid = 0;
        out->pin_nid = 0;
        out->routing_fizyczny = 0;
    }
    sciezka_test_cad = out->cad;
    sciezka_test_konw = out->konwerter_nid;
    sciezka_test_pin = out->pin_nid;
    sciezka_test_fizyczny = out->routing_fizyczny;
}

bool hda_wew_test_start(
    uint32_t tryb,
    uint32_t hz,
    uint32_t czas_ms,
    uint8_t kanaly
) {
    if (!__atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) return false;
    if (kanaly == 0) return false;

    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return false;

    (void)zatrzymaj_stream_wewnetrznie();

    uint32_t bdl_count = 0;
    uint32_t cbl_bytes = 0;

    if (tryb == HDA_TEST_SWEEP) {
        if (!wypelnij_sweep(
                HDA_TEST_SWEEP_MIN_HZ, HDA_TEST_SWEEP_MAX_HZ,
                kanaly, &bdl_count, &cbl_bytes)) {
            return false;
        }
    } else {
        if (!wypelnij_ton_kanaly(hz, kanaly, &bdl_count, &cbl_bytes)) {
            return false;
        }
    }

    // Read-only diagnostics cannot change the boot-proven QEMU codec configuration.
    if (!zaprogramuj_stream(bdl_count, cbl_bytes)) return false;
    if (!uruchom_stream()) {
        (void)zatrzymaj_stream_wewnetrznie();
        return false;
    }

    stan_test_wew.tryb = static_cast<uint8_t>(tryb);
    stan_test_wew.kanaly = kanaly;
    stan_test_wew.aktywny = true;
    stan_test_wew.hz_start = hz;
    stan_test_wew.hz_koniec =
        (tryb == HDA_TEST_SWEEP) ? HDA_TEST_SWEEP_MAX_HZ : hz;
    stan_test_wew.czas_ticks =
        static_cast<uint32_t>(
            (static_cast<uint64_t>(czas_ms) * HDA_WALCLK_TICKS_PER_MS));
    if (stan_test_wew.czas_ticks == 0) stan_test_wew.czas_ticks = 1;
    stan_test_wew.start_walclk = mmio_odczytaj32(HDA_REG_WALCLK);

    return true;
}

void hda_wew_test_stop() {
    if (hda_mmio_base == 0) return;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return;
    (void)zatrzymaj_stream_wewnetrznie();
    stan_test_wew.aktywny = false;
    stan_test_wew.tryb = 0;
}

bool hda_wew_test_tick(uint64_t) {
    if (!stan_test_wew.aktywny) return false;
    if (hda_mmio_base == 0) {
        stan_test_wew.aktywny = false;
        return false;
    }
    /*
     * Auto-stop po czas_ms mierzone WALCLK (jak istniejacy hda_test_ton).
     * Kontrola bledow streamu przy kazdym tick.
     */
    const uint8_t sts = stream_odczytaj_sts();
    if ((sts & (HDA_SD_STS_DESE | HDA_SD_STS_FIFOE)) != 0) {
        hda_wew_test_stop();
        return false;
    }
    const uint32_t teraz = mmio_odczytaj32(HDA_REG_WALCLK);
    const uint32_t start = static_cast<uint32_t>(stan_test_wew.start_walclk);
    if (static_cast<uint32_t>(teraz - start) >= stan_test_wew.czas_ticks) {
        hda_wew_test_stop();
        return false;
    }
    return true;
}

static bool s_pierwszy_skan_zalogowany = false;

static void log_pierwszy_skan_probka(
    uint8_t cad,
    uint32_t cmd,
    const HdaImmediateDiagLog& diag
) {
    char buf[200];
    size_t pos = 0;
    auto append_str = [&](const char* s) {
        while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
        buf[pos] = '\0';
    };
    auto append_hex = [&](uint32_t v, int digits) {
        const char hex_chars[] = "0123456789ABCDEF";
        append_str("0x");
        char tmp[9];
        for (int i = digits - 1; i >= 0; --i) {
            tmp[i] = hex_chars[v & 0xF];
            v >>= 4;
        }
        tmp[digits] = '\0';
        append_str(tmp);
    };
    auto append_dec = [&](uint32_t v) {
        char tmp[12];
        int i = 0;
        if (v == 0) tmp[i++] = '0';
        else {
            while (v > 0) {
                tmp[i++] = '0' + (v % 10);
                v /= 10;
            }
        }
        while (i > 0 && pos + 1 < sizeof(buf)) {
            buf[pos++] = tmp[--i];
        }
        buf[pos] = '\0';
    };

    append_str("[HDA Diag] BDF ");
    append_hex(hda_bdf.bus, 2);
    append_str(":");
    append_hex(hda_bdf.slot, 2);
    append_str(".");
    append_dec(hda_bdf.func);
    append_str(" IC InitState:");
    append_hex(hda_initial_statests, 4);
    append_str(" CurState:");
    append_hex(mmio_odczytaj16(HDA_REG_STATESTS), 4);
    append_str(" DiscMask:");
    append_hex(hda_discovered_codec_mask, 4);
    append_str(" CAD:");
    append_dec(cad);
    append_str(" NID:0 Verb:0xF00 Param:0x00 Cmd:");
    append_hex(cmd, 8);
    append_str(" ICIS(bef:");
    append_hex(diag.icis_before, 4);
    append_str(" stl:");
    append_hex(diag.icis_after_stale, 4);
    append_str(" bsy:");
    append_hex(diag.icis_after_busy, 4);
    append_str(" cmp:");
    append_hex(diag.icis_completion, 4);
    append_str(") ICII:");
    append_hex(diag.icii_response, 8);
    append_str(" ");
    append_dec(diag.elapsed_us);
    append_str("us Stat:");
    append_dec(diag.status);

    wypisz_log(buf);
}

bool hda_wew_sonda_pojedynczego_kodeka(
    uint8_t cad,
    bool* out_odpowiada,
    uint32_t* out_vendor
) {
    if (!out_odpowiada || !out_vendor || cad > 14U || hda_mmio_base == 0) {
        return false;
    }
    *out_odpowiada = false;
    *out_vendor = 0;

    uint32_t cmd = 0;
    if (!hda_zbuduj_get_param(cad, 0, HDA_PAR_VENDOR_ID, &cmd)) {
        return false;
    }

    const bool czy_logowac = false;
    uint32_t vendor = 0;
    HdaImmediateDiagLog diag{};
    HdaTransportStatus st = hda_transport_send_immediate_ex(cmd, &vendor, czy_logowac ? &diag : nullptr);

    if (czy_logowac) {
        log_pierwszy_skan_probka(cad, cmd, diag);
        if (cad == 14U) {
            s_pierwszy_skan_zalogowany = true;
        }
    }

    if (st == HDA_TRANSPORT_SUCCESS && vendor != 0 && vendor != 0xFFFFFFFFU) {
        *out_odpowiada = true;
        *out_vendor = vendor;
        hda_responsive_codec_mask = static_cast<uint16_t>(hda_responsive_codec_mask | (1U << cad));
        hda_discovered_codec_mask = static_cast<uint16_t>(hda_discovered_codec_mask | (1U << cad));
    }

    return true;
}

bool hda_wew_aktywne_sondowanie_kodekow(
    uint16_t* responsive_mask
) {
    if (!responsive_mask || hda_mmio_base == 0) {
        return false;
    }

    uint16_t maska = 0;
    for (uint8_t cad = 0; cad < 15U; ++cad) {
        bool odp = false;
        uint32_t vendor = 0;
        if (hda_wew_sonda_pojedynczego_kodeka(cad, &odp, &vendor) && odp) {
            maska = static_cast<uint16_t>(maska | (1U << cad));
        }
    }

    *responsive_mask = maska;
    return true;
}

bool hda_wew_send_verb_readonly(
    uint32_t verb,
    uint32_t* response
) {
    return wyslij_immediate(verb, response);
}


uint32_t hda_wew_kontroler_odtwarzania() { return hda_kontroler_odtwarzania; }
