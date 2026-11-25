/*
 * Bursztyn OS - Intel High Definition Audio (HDA)
 *
 * Aktualny zakres sterownika:
 *
 *   - kontroler PCI klasy Multimedia / HDA (04:03),
 *   - polling, bez IRQ,
 *   - jeden Output Stream,
 *   - 48 kHz / 16-bit / stereo PCM,
 *   - wykrywanie i konfiguracja tras kodeka przez istniejacy transport,
 *   - jeden silnik PCM dla miksera i diagnostycznego generatora tonu.
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
 *   - praca odroczona przygotowuje okresy na podstawie SDnLPIB,
 *   - HPET ogranicza czas, w ktorym pozycja modulo pozostaje jednoznaczna,
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
#include "hda_quirks.h"
#include "hda_glosnosc.h"
#include "hda_pcm_ring.h"
#include "jadro/arch/diag_opoznienia_irq.h"
#include "jadro/dzwiek/mikser_pcm.h"
#include "jadro/dzwiek/pcm_jadro.h"
#include "jadro/dzwiek/pcm_sched_diag.h"
#include "../czas/hpet.h"
#include "pamiec.h"
#include "pci.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef BURSZTYN_HDA_HOST_TEST
// Modeluje jedynie zapis rejestru (w tym W1C); test wykonuje prawdziwy kod tonu.
extern void hda_host_mmio_zapis(uint32_t offset, uint8_t szerokosc, uint32_t wartosc);
#endif

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

// One page per PCM period, all descriptors owned by this driver.
constexpr uint32_t HDA_MAKS_BDL = HdaPcm::PERIODS;
constexpr uint32_t HDA_MAKS_BUFOROW_DMA = HDA_MAKS_BDL;
static_assert(HdaPcm::PERIOD_BYTES == ROZMIAR_RAMKI_4K);
static_assert(Pcm::RATE == HDA_SAMPLE_RATE && sizeof(Pcm::Frame) == HDA_BAJTY_RAMKI_AUDIO);
static_assert(HdaPcm::BYTES_PER_SEC == HDA_SAMPLE_RATE * HDA_BAJTY_RAMKI_AUDIO);
static_assert(HdaPcm::PERIOD_FRAMES <= Pcm::MAX_MIX_FRAMES);

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

constexpr uint16_t HDA_AMP_SET_INPUT =
    1U << 14;

constexpr uint16_t HDA_AMP_SET_LEFT =
    1U << 13;

constexpr uint16_t HDA_AMP_SET_RIGHT =
    1U << 12;

constexpr uint16_t HDA_VERB_SET_CONNECTION_SELECT =
    0x701U;

constexpr uint16_t HDA_VERB_GET_CONNECTION_SELECT =
    0xF01U;

constexpr uint16_t HDA_VERB_SET_EAPD_BTL_ENABLE =
    0x70CU;

constexpr uint16_t HDA_VERB_GET_EAPD_BTL_ENABLE =
    0xF0CU;

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

/* Glosnosc systemowa (hda_glosnosc.h): wzmacniacz aktywnej trasy i stan poziomu/wyciszenia. */
HdaGlosnosc::Wzmacniacz s_wzmacniacz{};
HdaGlosnosc::Stan s_glosnosc{};

HdaDiagSciezka s_aktywna_sciezka{};
bool s_sciezka_skonfigurowana = false;
bool s_legacy_qemu_potwierdzony = false;
uint64_t s_ton_deadline_ns = 0;
uint64_t s_ton_last_running_ns = 0;
uint64_t s_ton_stopped_ns = 0;
bool s_ton_aktywny = false;
uint32_t s_ton_stop_powod = HDA_TON_STOP_BRAK;
uint32_t s_ton_stop_zadany = HDA_TON_STOP_BRAK;
uint8_t s_ton_run_po_stop = 0;

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

// IRQ and BSP scheduler handoff. The MMIO watchdog and the short DMA
// publication section must run on the same CPU (as the current scheduler).
bool pcm_interest = false;
uint64_t pcm_pending_ns = 0;
uint64_t pcm_last_priority_request = 0;
uint64_t pcm_last_priority_ns = 0;
bool pcm_boost_w_toku = false;
uint64_t pcm_guard_max_ns = 0;
uint64_t pcm_guard_misses = 0;

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
        if (__atomic_load_n(&pcm_interest, __ATOMIC_ACQUIRE)) {
            if (aktywny_) began_ = czas_monotoniczny_ns();
            else __atomic_add_fetch(&pcm_guard_misses, 1, __ATOMIC_RELAXED);
        }
    }

    ~GuardHDA() {
        if (aktywny_) {
            if (began_) {
                const uint64_t held = czas_monotoniczny_ns() - began_;
                if (held > pcm_guard_max_ns) pcm_guard_max_ns = held;
            }
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
    uint64_t began_ = 0;
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

#ifdef BURSZTYN_HDA_HOST_TEST
    hda_host_mmio_zapis(offset, 1, wartosc);
#else
    *reinterpret_cast<
        volatile uint8_t*>(
            hda_mmio_base +
            offset
        ) =
        wartosc;
#endif
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

#ifdef BURSZTYN_HDA_HOST_TEST
    hda_host_mmio_zapis(offset, 2, wartosc);
#else
    *reinterpret_cast<
        volatile uint16_t*>(
            hda_mmio_base +
            offset
        ) =
        wartosc;
#endif
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

#ifdef BURSZTYN_HDA_HOST_TEST
    hda_host_mmio_zapis(offset, 4, wartosc);
#else
    *reinterpret_cast<
        volatile uint32_t*>(
            hda_mmio_base +
            offset
        ) =
        wartosc;
#endif
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
    s_legacy_qemu_potwierdzony = false;
    HdaDiagKontroler kontroler{};
    if (!hda_transport_stan(hda_kontroler_odtwarzania, &kontroler) ||
        kontroler.vendor_id != 0x8086U || kontroler.device_id != 0x2668U) {
        return false;
    }
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
    s_legacy_qemu_potwierdzony = true;

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
    if (!s_legacy_qemu_potwierdzony || hda_codec_addr >
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
    HdaGlosnosc::Wzmacniacz wzm{};
    wzm.jest = true; wzm.cad = hda_codec_addr; wzm.nid = QEMU_NID_DAC;
    wzm.caps = HdaGlosnosc::QEMU_AMP_CAPS; wzm.gain_ref = static_cast<uint8_t>(QEMU_HDA_GAIN & 0x7F);
    s_wzmacniacz = wzm;                         // 100 % = dotychczasowe 0x4A
    const uint16_t amp_payload = HdaGlosnosc::payload(wzm, s_glosnosc);

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

const HdaDiagWidget* znajdz_widget_w_snapshot(
    const HdaDiagSnapshot& snap,
    uint8_t k,
    uint8_t cad,
    uint8_t fg_nid,
    uint8_t nid
) {
    if (k >= HDA_DIAG_MAKS_KONTROLEROW) return nullptr;
    const uint32_t n_kodekow = snap.kontrolery[k].liczba_kodekow;
    for (uint32_t ki = 0; ki < n_kodekow && ki < HDA_DIAG_MAKS_KODEKOW; ++ki) {
        if (snap.kodeki[k][ki].cad != cad) continue;
        const uint32_t n_fg = snap.kodeki[k][ki].liczba_fg;
        for (uint32_t fi = 0; fi < n_fg && fi < HDA_DIAG_MAKS_FG; ++fi) {
            if (fg_nid != 0 && snap.fg[k][ki][fi].nid != fg_nid) continue;
            const uint32_t n_w = snap.fg[k][ki][fi].widget_count;
            for (uint32_t wi = 0; wi < n_w && wi < HDA_DIAG_MAKS_WIDGETOW; ++wi) {
                if (snap.widgety[k][ki][fi][wi].nid == nid) {
                    return &snap.widgety[k][ki][fi][wi];
                }
            }
        }
    }
    return nullptr;
}

const HdaDiagFG* znajdz_afg_w_snapshot(
    const HdaDiagSnapshot& snap,
    uint8_t k,
    uint8_t cad,
    uint8_t fg_nid
) {
    if (k >= HDA_DIAG_MAKS_KONTROLEROW) return nullptr;
    const uint32_t n_kodekow = snap.kontrolery[k].liczba_kodekow;
    for (uint32_t ki = 0; ki < n_kodekow && ki < HDA_DIAG_MAKS_KODEKOW; ++ki) {
        if (snap.kodeki[k][ki].cad != cad) continue;
        const uint32_t n_fg = snap.kodeki[k][ki].liczba_fg;
        for (uint32_t fi = 0; fi < n_fg && fi < HDA_DIAG_MAKS_FG; ++fi) {
            if (snap.fg[k][ki][fi].audio && (fg_nid == 0 || snap.fg[k][ki][fi].nid == fg_nid)) {
                return &snap.fg[k][ki][fi];
            }
        }
    }
    return nullptr;
}

bool konfiguruj_trase_audio(
    const HdaDiagSciezka& sciezka,
    const HdaDiagSnapshot& snap
) {
    if (sciezka.codec_cad > 14U || sciezka.dlugosc == 0 ||
        sciezka.dlugosc > HDA_DIAG_MAKS_DLUGOSC_SCIEZKI) {
        return false;
    }

    uint32_t resp = 0;
    const uint8_t cad = sciezka.codec_cad;
    const uint8_t k = sciezka.kontroler_idx;

    const auto* afg = znajdz_afg_w_snapshot(snap, k, cad, sciezka.fg_nid);
    const uint8_t fg_nid = afg ? afg->nid : (sciezka.fg_nid ? sciezka.fg_nid : 1);
    const uint32_t fg_in_caps = afg ? afg->in_amp_caps : 0;
    const uint32_t fg_out_caps = afg ? afg->out_amp_caps : 0;

    // 1. Power state: AFG i widgety trasy w stan D0
    if (!wyslij_immediate(
            verb12(cad, fg_nid, HDA_VERB_SET_POWER_STATE, HDA_POWER_D0),
            &resp)) {
        return false;
    }

    for (uint8_t h = 0; h < sciezka.dlugosc; ++h) {
        const uint8_t nid = sciezka.nids[h];
        if (nid == 0 || nid >= 128) continue;
        (void)wyslij_immediate(
            verb12(cad, nid, HDA_VERB_SET_POWER_STATE, HDA_POWER_D0),
            &resp);
    }

    // 2. Connection Select Control dla widgetow posrednich i pinu
    for (uint8_t h = 1; h < sciezka.dlugosc; ++h) {
        const uint8_t curr_nid = sciezka.nids[h];
        const uint8_t prev_nid = sciezka.nids[h - 1];
        const auto* w = znajdz_widget_w_snapshot(snap, k, cad, fg_nid, curr_nid);

        uint8_t conn_idx = 255;
        if (sciezka.selektory[h] < 255) {
            conn_idx = sciezka.selektory[h];
        } else if (w) {
            for (uint8_t ci = 0; ci < w->liczba_polaczen && ci < HDA_DIAG_MAKS_POLACZEN; ++ci) {
                if (w->polaczenia[ci] == prev_nid) {
                    conn_idx = ci;
                    break;
                }
            }
        }

        if (conn_idx != 255) {
            (void)wyslij_immediate(
                verb12(cad, curr_nid, HDA_VERB_SET_CONNECTION_SELECT, conn_idx),
                &resp);
        }
    }

    // 3. Konfiguracja DAC (Audio Output converter)
    const uint8_t dac_nid = sciezka.konwerter_nid ? sciezka.konwerter_nid : sciezka.nids[0];
    const uint32_t set_format =
        (static_cast<uint32_t>(cad) << 28) |
        (static_cast<uint32_t>(dac_nid) << 20) |
        (static_cast<uint32_t>(HDA_VERB_SET_STREAM_FORMAT) << 8) |
        static_cast<uint32_t>(HDA_FORMAT_48K_16_STEREO);

    if (!wyslij_immediate(set_format, &resp)) {
        return false;
    }

    if (!wyslij_immediate(
            verb12(cad, dac_nid, HDA_VERB_SET_CHANNEL_STREAMID,
                   static_cast<uint8_t>(HDA_STREAM_TAG << 4)),
            &resp)) {
        return false;
    }

    // 4. Wzmacniacze: Unmute i bezpieczny gain wzdluz calej aktywnej trasy. Pierwszy regulowany wzmacniacz
    //    wyjsciowy (od konwertera) jest kontrolka glosnosci systemowej: jego bezpieczny gain = 100 %.
    HdaGlosnosc::Wzmacniacz wzm{};
    for (uint8_t h = 0; h < sciezka.dlugosc; ++h) {
        const uint8_t nid = sciezka.nids[h];
        const auto* w = znajdz_widget_w_snapshot(snap, k, cad, fg_nid, nid);

        // Output Amplifier
        const bool ma_out_amp = w ? ((w->awcap & (1U << 2)) != 0 || w->out_amp_caps != 0) : (nid == dac_nid);
        if (ma_out_amp) {
            const uint32_t caps = (w && w->out_amp_caps) ? w->out_amp_caps : fg_out_caps;
            uint8_t gain = 0;
            if (!hda_konserwatywne_wzmocnienie(caps, &gain)) {
                gain = static_cast<uint8_t>(caps & 0x7F);
                if (gain == 0 && (caps & 0x7F00) != 0) {
                    gain = static_cast<uint8_t>(((caps >> 8) & 0x7F) / 2);
                }
                if (gain == 0 && nid == QEMU_NID_DAC) {
                    gain = static_cast<uint8_t>(QEMU_HDA_GAIN & 0x7F);
                }
            }
            uint16_t amp_out = HdaGlosnosc::payload_staly(gain);
            if (!wzm.jest && HdaGlosnosc::kroki(caps) > 0 && (gain & 0x7F) > 0) {
                wzm.jest = true; wzm.cad = cad; wzm.nid = nid; wzm.caps = caps;
                wzm.gain_ref = static_cast<uint8_t>(gain & 0x7F);
                amp_out = HdaGlosnosc::payload(wzm, s_glosnosc);
            }
            (void)wyslij_immediate(
                verb4(cad, nid, HDA_VERB4_SET_AMP_GAIN_MUTE, amp_out),
                &resp);
        }

        // Input Amplifier
        if (h > 0) {
            const bool ma_in_amp = w && (((w->awcap & (1U << 1)) != 0) || w->in_amp_caps != 0);
            if (ma_in_amp) {
                const uint32_t caps = w->in_amp_caps ? w->in_amp_caps : fg_in_caps;
                uint8_t gain = 0;
                if (!hda_konserwatywne_wzmocnienie(caps, &gain)) {
                    gain = static_cast<uint8_t>(caps & 0x7F);
                    if (gain == 0 && (caps & 0x7F00) != 0) {
                        gain = static_cast<uint8_t>(((caps >> 8) & 0x7F) / 2);
                    }
                }
                uint8_t in_idx = (sciezka.selektory[h] < 16) ? sciezka.selektory[h] : 0;
                const uint16_t amp_in =
                    HDA_AMP_SET_INPUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
                    (static_cast<uint16_t>(in_idx & 0x0F) << 8) | (gain & 0x7F);
                (void)wyslij_immediate(
                    verb4(cad, nid, HDA_VERB4_SET_AMP_GAIN_MUTE, amp_in),
                    &resp);
            }
        }
    }

    s_wzmacniacz = wzm;                         // brak regulowanego wzmacniacza = backend programowy (PCM)

    // 5. Pin Widget Control dla pinu glosnika/wyjscia
    const uint8_t pin_nid = sciezka.pin_nid ? sciezka.pin_nid : sciezka.nids[sciezka.dlugosc - 1];
    const auto* pin_w = znajdz_widget_w_snapshot(snap, k, cad, fg_nid, pin_nid);
    uint8_t pin_ctrl = HDA_PIN_OUT_ENABLE;
    if (sciezka.cel == HDA_CEL_SLUCHAWKI && pin_w && (pin_w->pin_caps & (1U << 3))) {
        pin_ctrl |= 0x80U;
    }
    if (!wyslij_immediate(
            verb12(cad, pin_nid, HDA_VERB_SET_PIN_WIDGET_CONTROL, pin_ctrl),
            &resp)) {
        return false;
    }

    // 6. EAPD: External Amplifier Power Down / BTL Enable
    if (pin_w && (pin_w->pin_caps & HDA_PINCAP_EAPD)) {
        uint32_t cur_eapd = 0;
        (void)wyslij_immediate(
            verb12(cad, pin_nid, HDA_VERB_GET_EAPD_BTL_ENABLE, 0),
            &cur_eapd);
        const uint8_t new_eapd = static_cast<uint8_t>((cur_eapd & 0x07) | 0x02);
        (void)wyslij_immediate(
            verb12(cad, pin_nid, HDA_VERB_SET_EAPD_BTL_ENABLE, new_eapd),
            &resp);
        uint32_t verif_eapd = 0;
        (void)wyslij_immediate(
            verb12(cad, pin_nid, HDA_VERB_GET_EAPD_BTL_ENABLE, 0),
            &verif_eapd);
    }

    return true;
}

void loguj_konfiguracje_trasy(
    const HdaDiagSciezka& sciezka,
    const HdaDiagSnapshot& snap
) {
    char buf[256];
    size_t pos = 0;
    auto append_str = [&](const char* s) {
        while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
        buf[pos] = '\0';
    };
    auto append_hex = [&](uint64_t v, int digits) {
        const char hex_chars[] = "0123456789ABCDEF";
        append_str("0x");
        char tmp[17];
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

    const auto& cinfo = snap.kontrolery[sciezka.kontroler_idx];
    pos = 0;
    append_str("[HDA] PCI ");
    append_hex(cinfo.bus, 2);
    append_str(":");
    append_hex(cinfo.slot, 2);
    append_str(".");
    append_dec(cinfo.func);
    append_str(cinfo.vendor_id == 0x8086 ? " Intel HDA" : (cinfo.vendor_id == 0x10DE ? " NVIDIA HDA" : " HDA"));
    append_str(" (vendor=");
    append_hex(cinfo.vendor_id, 4);
    append_str(" device=");
    append_hex(cinfo.device_id, 4);
    append_str(")");
    wypisz_log(buf);

    const auto* afg = znajdz_afg_w_snapshot(snap, sciezka.kontroler_idx, sciezka.codec_cad, sciezka.fg_nid);
    uint32_t codec_vendor = 0, codec_device = 0;
    for (uint32_t ki = 0; ki < cinfo.liczba_kodekow && ki < HDA_DIAG_MAKS_KODEKOW; ++ki) {
        if (snap.kodeki[sciezka.kontroler_idx][ki].cad == sciezka.codec_cad) {
            codec_vendor = snap.kodeki[sciezka.kontroler_idx][ki].vendor_id;
            codec_device = snap.kodeki[sciezka.kontroler_idx][ki].device_id;
            break;
        }
    }
    pos = 0;
    append_str("[HDA] codec CAD=");
    append_dec(sciezka.codec_cad);
    append_str(" vendor=");
    append_hex(codec_vendor, 4);
    append_str(" device=");
    append_hex(codec_device, 4);
    wypisz_log(buf);

    pos = 0;
    append_str("[HDA] AFG NID=");
    append_hex(afg ? afg->nid : sciezka.fg_nid, 2);
    wypisz_log(buf);

    pos = 0;
    append_str("[HDA] speaker pin NID=");
    append_hex(sciezka.pin_nid, 2);
    append_str(" (cel=");
    append_str(hda_nazwa_celu_pinu(sciezka.cel));
    append_str(" ocena=");
    append_dec(sciezka.ocena);
    append_str(")");
    wypisz_log(buf);

    pos = 0;
    append_str("[HDA] route: ");
    for (uint8_t h = 0; h < sciezka.dlugosc && h < HDA_DIAG_MAKS_DLUGOSC_SCIEZKI; ++h) {
        if (h > 0) append_str(" -> ");
        append_hex(sciezka.nids[h], 2);
    }
    wypisz_log(buf);

    for (uint8_t h = 0; h < sciezka.dlugosc && h < HDA_DIAG_MAKS_DLUGOSC_SCIEZKI; ++h) {
        const uint8_t nid = sciezka.nids[h];
        const auto* w = znajdz_widget_w_snapshot(snap, sciezka.kontroler_idx, sciezka.codec_cad, sciezka.fg_nid, nid);
        pos = 0;
        append_str("[HDA] widget NID=");
        append_hex(nid, 2);
        if (w) {
            append_str(" typ=");
            append_str(hda_nazwa_typu_widgetu(w->typ));
            append_str(" awcap=");
            append_hex(w->awcap, 8);
            if (hda_typ_widgetu_pin(w->typ)) {
                append_str(" pin_caps=");
                append_hex(w->pin_caps, 8);
                if (w->pin_caps & HDA_PINCAP_EAPD) {
                    append_str(" EAPD=capable");
                }
            }
            if (w->in_amp_caps || (w->awcap & 2)) {
                append_str(" in_amp=");
                append_hex(w->in_amp_caps, 8);
            }
            if (w->out_amp_caps || (w->awcap & 4)) {
                append_str(" out_amp=");
                append_hex(w->out_amp_caps, 8);
            }
            if (w->liczba_polaczen > 1) {
                append_str(" conn_idx=");
                append_dec(sciezka.selektory[h]);
            }
        }
        append_str(" power=D0");
        wypisz_log(buf);
    }
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

int16_t probka_trojkatna(uint32_t faza) {
    const int32_t p = static_cast<int32_t>(uint64_t(faza) * 65536 / HDA_SAMPLE_RATE);
    const int32_t v = p < 16384 ? p : (p < 49152 ? 32768 - p : p - 65536);
    return static_cast<int16_t>(v * 3 / 4);
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

// Published with local IRQs masked, read only by the BSP timer.
uint64_t pcm_watchdog_ns = 0;
bool pcm_watchdog_fired = false;
HdaPcm::WatchdogWindow pcm_watchdog_window{};

void pcm_set_deadline(const HdaPcm::Ring& ring) {
    pcm_watchdog_window = ring.watchdog_window();
    const uint64_t due = pcm_watchdog_window.safe_until(ring.lpib, ring.sampled_ns);
    __atomic_store_n(&pcm_watchdog_ns, due ? due : ring.sampled_ns, __ATOMIC_RELEASE);
}

class PcmCommitIrq {
#ifndef BURSZTYN_HDA_HOST_TEST
    uint64_t flags_;
#endif
public:
    PcmCommitIrq() {
#ifndef BURSZTYN_HDA_HOST_TEST
        asm volatile("pushfq; popq %0; cli" : "=r"(flags_) :: "memory");
#endif
    }
    ~PcmCommitIrq() {
#ifndef BURSZTYN_HDA_HOST_TEST
        if (flags_ & (1ULL << 9)) asm volatile("sti" ::: "memory");
#endif
    }
};

bool uruchom_stream(uint64_t& started_ns) {
    {
        PcmCommitIrq commit;
        stream_wyczysc_sts();
        uint32_t ctl = stream_odczytaj_ctl();
        ctl &= ~(HDA_SD_CTL_SRST | HDA_SD_CTL_IRQ_MASK);
        ctl |= HDA_SD_CTL_RUN;
        started_ns = czas_monotoniczny_ns();
        HdaPcm::Ring initial;
        initial.start(started_ns);
        pcm_set_deadline(initial);
        stream_zapisz_ctl(ctl);
    }

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
    {
        PcmCommitIrq commit;
        __atomic_store_n(&pcm_watchdog_ns, 0, __ATOMIC_RELEASE);
        if (!hda_stream_base) return true;
        stream_zapisz_ctl(stream_odczytaj_ctl() & ~HDA_SD_CTL_RUN);
    }

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

// Protected by GuardHDA. No code taking this try-lock may sleep or yield.
Pcm::Mixer pcm_mixer;
constexpr Pcm::Owner pcm_internal{UINT32_MAX, 1};
constexpr Pcm::Owner pcm_legacy{UINT32_MAX, 2};
HdaPcm::Ring pcm_ring;
PcmJadro::Statystyki pcm_stats{};
Pcm::Frame pcm_stage[HdaPcm::PERIOD_FRAMES];
uint32_t pcm_clients = 0;
uint32_t pcm_app_clients = 0;
bool pcm_running = false;
bool pcm_shutdown = false;
bool pcm_retry = false;
uint64_t pcm_idle_since = 0;
uint64_t pcm_retry_ns = 0;
uint64_t pcm_app_prime_since = 0;
uint64_t pcm_recovery_since = 0;
uint64_t pcm_watchdog_time = 0;
uint64_t pcm_watchdog_irq_gap = 0;
uint32_t pcm_watchdog_lpib = 0;
uint64_t pcm_worker_request = 0;
uint32_t pcm_observed_lpib = 0;
constexpr uint32_t PCM_EVENTS = 32;
PcmJadro::Zdarzenie pcm_events[PCM_EVENTS]{};
constexpr uint32_t PCM_SHORTFALL_EVENTS = 64;
PcmJadro::SladPcm pcm_shortfalls[PCM_SHORTFALL_EVENTS]{};
uint32_t pcm_shortfall_count = 0;

struct PcmIrqTiming {
    uint64_t count, last, max_gap, last_gap, pid, ring0, boosts, rechecks, extensions;
};
PcmIrqTiming pcm_irq_timing{};

void pcm_interest_set(bool active) {
    PcmCommitIrq commit;
    if (!active) {
        pcm_app_prime_since = 0;
        __atomic_store_n(&pcm_pending_ns, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&pcm_boost_w_toku, false, __ATOMIC_RELEASE);
        pcm_irq_timing.last = pcm_irq_timing.last_gap = 0;
    }
    __atomic_store_n(&pcm_interest, active, __ATOMIC_RELEASE);
}

struct LegacyPcm {
    uint64_t handle;
    uint32_t frames_left, frequency, phase, sweep_frame;
    uint8_t channels;
    bool sweep, diagnostic;
};
LegacyPcm pcm_tone{};

uint32_t pcm_sd() {
    return static_cast<uint32_t>(hda_stream_base - hda_mmio_base);
}

bool pcm_device_ready() {
    return !pcm_shutdown && __atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE) &&
           hpet_dostepny() && (mmio_odczytaj32(HDA_REG_GCTL) & HDA_GCTL_CRST);
}

void pcm_feed_tone(uint32_t target) {
    if (!pcm_tone.frames_left) return;
    Pcm::Status status{};
    if (pcm_mixer.status(pcm_legacy, pcm_tone.handle, &status) != Pcm::OK || status.queued >= target)
        return;
    uint32_t remaining = target - status.queued;
    if (remaining > pcm_tone.frames_left) remaining = pcm_tone.frames_left;
    while (remaining) {
        constexpr uint32_t batch = 128;
        int16_t samples[batch * 2];
        const uint32_t count = remaining < batch ? remaining : batch;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t hz = pcm_tone.frequency;
            if (pcm_tone.sweep) {
                hz = HDA_TEST_SWEEP_MIN_HZ +
                     (HDA_TEST_SWEEP_MAX_HZ - HDA_TEST_SWEEP_MIN_HZ) *
                     pcm_tone.sweep_frame / HDA_SAMPLE_RATE;
                if (++pcm_tone.sweep_frame == HDA_SAMPLE_RATE) pcm_tone.sweep_frame = 0;
            }
            int32_t v = probka_trojkatna(pcm_tone.phase);
            if (pcm_tone.diagnostic) v = v * HDA_TEST_AMPLITUDA / 24576;
            samples[2 * i] = pcm_tone.channels & 1 ? static_cast<int16_t>(v) : 0;
            samples[2 * i + 1] = pcm_tone.channels & 2 ? static_cast<int16_t>(v) : 0;
            pcm_tone.phase = (pcm_tone.phase + hz) % HDA_SAMPLE_RATE;
        }
        uint32_t accepted = 0;
        (void)pcm_mixer.write(pcm_legacy, pcm_tone.handle, samples, count, &accepted);
        pcm_tone.frames_left -= accepted;
        remaining -= accepted;
        if (accepted != count) break;
    }
    if (!pcm_tone.frames_left) (void)pcm_mixer.finish(pcm_legacy, pcm_tone.handle);
}

bool pcm_prepare(uint32_t offset = 0) {
    const uint64_t begin = czas_monotoniczny_ns();
    const uint64_t irq_before = __atomic_load_n(&pcm_irq_timing.count, __ATOMIC_RELAXED);
    pcm_feed_tone(offset + HdaPcm::PERIOD_FRAMES);
    (void)pcm_mixer.preview(pcm_stage, HdaPcm::PERIOD_FRAMES, offset);
    const bool software_master = !s_wzmacniacz.regulowany();
    bool has_signal = false;
    for (auto& f : pcm_stage) {
        if (software_master) {
            f.left = HdaGlosnosc::skaluj_probke(f.left, s_glosnosc, true);
            f.right = HdaGlosnosc::skaluj_probke(f.right, s_glosnosc, true);
        }
        has_signal |= f.left != 0 || f.right != 0;
    }
    const uint64_t duration = czas_monotoniczny_ns() - begin;
    ++pcm_stats.okresy_miksowania;
    if (__atomic_load_n(&pcm_irq_timing.count, __ATOMIC_RELAXED) != irq_before)
        ++pcm_stats.irq_w_miksie;
    pcm_stats.czas_miksowania_ns += duration;
    if (duration > pcm_stats.maks_miks_ns) pcm_stats.maks_miks_ns = duration;
    return has_signal;
}

void pcm_trace_shortfall(PcmJadro::BrakPcm reason, uint64_t period, uint64_t previous_ns,
                         uint64_t previous_bytes, Pcm::QueueLevels queue, uint32_t missing,
                         bool start, bool staged, bool available) {
    if (!DiagOpoznieniaIrq::active() || pcm_shortfall_count == PCM_SHORTFALL_EVENTS) return;
    uint64_t irq_gap;
    {
        PcmCommitIrq commit;
        irq_gap = pcm_irq_timing.last_gap;
    }
    pcm_shortfalls[pcm_shortfall_count++] = {
        pcm_ring.sampled_ns, previous_ns, pcm_worker_request, irq_gap, period,
        pcm_ring.bytes, previous_bytes, pcm_ring.lpib, queue.minimum, queue.maximum,
        queue.expecting, missing, reason, start, s_glosnosc.wyciszony, staged, available
    };
    PcmSchedDiag::record(PcmSchedDiag::Kind::HdaShortfall, pcm_ring.sampled_ns, 0,
                         static_cast<uint32_t>(period), missing, static_cast<uint8_t>(reason));
}

void pcm_commit_mix(bool signal, uint64_t period, uint64_t previous_ns,
                    uint64_t previous_bytes, bool start) {
    const auto queue = pcm_mixer.queue_levels();
    const auto missing = pcm_mixer.consume(HdaPcm::PERIOD_FRAMES, czas_monotoniczny_ns());
    if (s_glosnosc.wyciszony) ++pcm_stats.okresy_master_mute;
    else if (!signal) ++pcm_stats.cisza_miksera;
    if (missing)
        pcm_trace_shortfall(PcmJadro::BrakPcm::Kolejka, period, previous_ns,
                            previous_bytes, queue, missing, start, true, queue.maximum != 0);
}

void pcm_copy_period(uint32_t slot) {
    auto* dst = static_cast<Pcm::Frame*>(dma_wirtualny_z_fizycznego(hda_bufory_phys[slot]));
    for (uint32_t i = 0; i < HdaPcm::PERIOD_FRAMES; ++i) dst[i] = pcm_stage[i];
    bariera_dma_przed_urzadzeniem();
    ++pcm_stats.okresy;
}

bool pcm_stop_dma() {
    // The IRQ can request a stop, but only this readback releases DMA ownership.
    if (!zatrzymaj_stream_wewnetrznie()) return false;
    pcm_running = false;
    __atomic_store_n(&pcm_watchdog_fired, false, __ATOMIC_RELEASE);
    return true;
}

bool pcm_start_dma() {
    static_assert(HdaPcm::PRIME * HdaPcm::PERIOD_FRAMES <= Pcm::CAPACITY_FRAMES);
    if (!pcm_device_ready() || !pcm_stop_dma()) return false;
    // FIFOS is encoded as bytes minus one. Keep at least one full period
    // between the link and the first byte to be published during RUN.
    constexpr uint32_t sd_fifos = 0x10;
    if (uint32_t(mmio_odczytaj16(pcm_sd() + sd_fifos)) + 1 > HdaPcm::PERIOD_BYTES)
        return false;
    wyzeruj(hda_bdl, ROZMIAR_RAMKI_4K);
    bool primed[HdaPcm::PRIME]{};
    for (uint32_t i = 0; i < HdaPcm::PERIODS; ++i) {
        hda_bdl[i] = {reinterpret_cast<uint64_t>(hda_bufory_phys[i]), HdaPcm::PERIOD_BYTES, 0};
        // The extra slots are initially silent. Only four periods are
        // queued ahead, independent of the length of the physical BDL.
        if (i < HdaPcm::PRIME) {
            primed[i] = pcm_prepare(i * HdaPcm::PERIOD_FRAMES);
            pcm_copy_period(i);
        } else {
            wyzeruj(dma_wirtualny_z_fizycznego(hda_bufory_phys[i]), HdaPcm::PERIOD_BYTES);
        }
    }
    if (!zaprogramuj_stream(HdaPcm::PERIODS, HdaPcm::RING_BYTES)) return false;
    uint64_t started_ns = 0;
    pcm_running = true; // RUN may already be set on failure.
    if (!uruchom_stream(started_ns) || !(stream_odczytaj_ctl() & HDA_SD_CTL_RUN)) {
        (void)pcm_stop_dma();
        return false;
    }
    pcm_ring.start(started_ns);
    for (uint32_t i = 0; i < HdaPcm::PRIME; ++i) {
        pcm_ring.written[i] = primed[i];
        pcm_commit_mix(primed[i], i, 0, 0, true);
    }
    ++pcm_stats.starty;
    if (pcm_retry) {
        ++pcm_stats.odzyskania;
        const uint64_t elapsed = started_ns - pcm_recovery_since;
        if (elapsed > pcm_stats.maks_odzyskanie_ns) pcm_stats.maks_odzyskanie_ns = elapsed;
    }
    pcm_retry = false;
    pcm_interest_set(true);
    return true;
}

HdaPcm::Fault pcm_observe() {
    const uint64_t now = czas_monotoniczny_ns();
    if (now >= pcm_ring.sampled_ns) {
        const uint64_t gap = now - pcm_ring.sampled_ns;
        if (gap > pcm_stats.maks_przerwa_ns) pcm_stats.maks_przerwa_ns = gap;
    }
    pcm_observed_lpib = mmio_odczytaj32(pcm_sd() + HDA_SD_LPIB);
    if (__atomic_load_n(&pcm_watchdog_fired, __ATOMIC_ACQUIRE))
        return HdaPcm::Fault::Watchdog;
    const auto fault = pcm_ring.observe(pcm_observed_lpib, now);
    if (pcm_ring.delta > pcm_stats.maks_skok_lpib) pcm_stats.maks_skok_lpib = pcm_ring.delta;
    return fault;
}

// Composing a period and changing DMA memory are separate. Re-observe after
// composing, even if the next action has changed while the CPU was away.
HdaPcm::Fault pcm_refill() {
    using namespace HdaPcm;
    bool staged = false;
    bool signal = false;
    for (uint32_t n = 0; n < 2 * PERIODS; ++n) {
        const auto previous_ns = pcm_ring.sampled_ns;
        const auto previous_bytes = pcm_ring.bytes;
        const auto period = pcm_ring.fill;
        {
            PcmCommitIrq commit;
            const Fault fault = pcm_observe();
            if (fault != Fault::None) return fault;
            switch (pcm_ring.next()) {
                case Action::Wait:
                    pcm_set_deadline(pcm_ring);
                    return Fault::None;
                case Action::Recover:
                    // Even a scrub would race hardware or stale data has
                    // already reached its next turn. Only STOP may free it.
                    return Fault::Ownership;
                case Action::Scrub:
                    wyzeruj(dma_wirtualny_z_fizycznego(hda_bufory_phys[Ring::slot(pcm_ring.retire)]),
                            PERIOD_BYTES);
                    bariera_dma_przed_urzadzeniem();
                    pcm_ring.scrubbed();
                    pcm_set_deadline(pcm_ring);
                    continue;
                case Action::Skip: {
                    ++pcm_ring.fill;
                    ++pcm_stats.bezpieczna_cisza;
                    const bool available = pcm_tone.frames_left || pcm_mixer.has_pcm();
                    if (available) ++pcm_stats.utracone_okresy;
                    if (DiagOpoznieniaIrq::active())
                        pcm_trace_shortfall(PcmJadro::BrakPcm::WlasnoscDma, period, previous_ns,
                                            previous_bytes, pcm_mixer.queue_levels(), 0, false, staged, available);
                    continue;
                }
                case Action::Fill:
                    if (staged) {
                        const uint64_t lead = (pcm_ring.fill * PERIOD_BYTES - pcm_ring.bytes) / sizeof(Pcm::Frame);
                        if (lead > pcm_stats.maks_wyprzedzenie_ramek) pcm_stats.maks_wyprzedzenie_ramek = lead;
                        pcm_copy_period(Ring::slot(pcm_ring.fill));
                        pcm_ring.published(signal);
                        pcm_set_deadline(pcm_ring);
                        break;
                    }
                    // Keep the fifth period available until there is PCM to
                    // publish. In particular do not consume silence at cold start.
                    if (pcm_ring.fill == pcm_ring.current() + LEAD - 1 && !pcm_tone.frames_left) {
                        const auto queues = pcm_mixer.queue_levels();
                        if ((queues.expecting && queues.minimum < PERIOD_FRAMES) || !pcm_mixer.has_pcm()) {
                            pcm_set_deadline(pcm_ring);
                            return Fault::None;
                        }
                    }
                    pcm_set_deadline(pcm_ring);
                    break;
            }
        }
        if (staged) {
            pcm_commit_mix(signal, period, previous_ns, previous_bytes, false);
            staged = false;
            continue;
        }
        signal = pcm_prepare();
        staged = true;
    }
    // Bound work per invocation. Keep outstanding work for the next point.
    uint64_t empty = 0;
    const uint64_t now = czas_monotoniczny_ns();
    (void)__atomic_compare_exchange_n(&pcm_pending_ns, &empty, now, false,
                                      __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    return Fault::None;
}

bool zatrzymaj_ton_wewnetrznie(uint32_t powod) {
    if (!__atomic_load_n(&s_ton_aktywny, __ATOMIC_ACQUIRE)) return true;
    if (pcm_tone.handle) {
        (void)pcm_mixer.close(pcm_legacy, pcm_tone.handle);
        pcm_tone = {};
    }
    __atomic_store_n(&s_ton_stop_powod, powod, __ATOMIC_RELEASE);
    // Preserve the historical RUN=0 for the standalone test. If other
    // clients are open, RUN belongs to them and the test only closes its PCM.
    const bool stopped = pcm_clients || !pcm_running || pcm_stop_dma();
    const uint8_t run = (stream_odczytaj_ctl() & HDA_SD_CTL_RUN) != 0;
    __atomic_store_n(&s_ton_run_po_stop, run, __ATOMIC_RELEASE);
    if (!stopped) {
        __atomic_store_n(&s_ton_stop_zadany, powod, __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&s_ton_stopped_ns, hpet_dostepny() ? czas_monotoniczny_ns() : 0,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_aktywny, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_stop_zadany, HDA_TON_STOP_BRAK, __ATOMIC_RELEASE);
    return true;
}

bool pcm_start_tone(uint32_t mode, uint32_t hz, uint32_t ms, uint8_t channels, bool diag) {
    if (!hda_api_czas_poprawny(ms) || !channels || channels > 3 ||
        (mode != HDA_TEST_SWEEP && !hda_api_czestotliwosc_poprawna(hz))) return false;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny() || !pcm_device_ready() || (pcm_retry && pcm_running)) return false;

    // Replace just the legacy producer, leaving any running DMA in place.
    if (pcm_tone.handle) (void)pcm_mixer.close(pcm_legacy, pcm_tone.handle);
    pcm_tone = {};
    if (pcm_mixer.open(pcm_legacy, {Pcm::RATE, 2, Pcm::FORMAT_S16}, &pcm_tone.handle) != Pcm::OK)
        return false;
    pcm_tone.frames_left = hda_api_liczba_ramek_audio(ms);
    pcm_tone.frequency = hz;
    pcm_tone.channels = channels;
    pcm_tone.sweep = mode == HDA_TEST_SWEEP;
    pcm_tone.diagnostic = diag;
    const bool was_running = pcm_running;
    if (!was_running && !pcm_start_dma()) {
        (void)pcm_mixer.close(pcm_legacy, pcm_tone.handle);
        pcm_tone = {};
        __atomic_store_n(&s_ton_aktywny, false, __ATOMIC_RELEASE);
        ++pcm_stats.bledy_dma;
        return false;
    }

    const uint64_t now = czas_monotoniczny_ns();
    // On a running ring a newly inserted test starts on a later DMA period.
    const uint64_t lead = was_running ? HdaPcm::LEAD * HdaPcm::PERIOD_NS : 0;
    __atomic_store_n(&s_ton_deadline_ns, now + uint64_t(ms) * 1000000ULL + lead, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_last_running_ns, now, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_stopped_ns, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_run_po_stop, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_stop_powod, HDA_TON_STOP_BRAK, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_stop_zadany, HDA_TON_STOP_BRAK, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ton_aktywny, true, __ATOMIC_RELEASE);
    return true;
}

void pcm_recover(uint64_t now, HdaPcm::Fault fault) {
    if (!pcm_retry) {
        PcmCommitIrq commit;
        const uint32_t cause = static_cast<uint32_t>(fault);
        static_assert(static_cast<unsigned>(HdaPcm::Fault::Count) ==
                      sizeof(pcm_stats.przyczyny) / sizeof(pcm_stats.przyczyny[0]));
        ++pcm_stats.przyczyny[cause];
        const bool irq_stop = fault == HdaPcm::Fault::Watchdog;
        const uint64_t deadline = __atomic_load_n(&pcm_watchdog_ns, __ATOMIC_RELAXED);
        if (pcm_stats.zapisane_zdarzenia < PCM_EVENTS) {
            pcm_events[pcm_stats.zapisane_zdarzenia++] = {
                now, deadline, irq_stop ? pcm_watchdog_irq_gap : pcm_irq_timing.last_gap,
                pcm_worker_request, pcm_ring.sampled_ns, pcm_observed_lpib, cause,
                irq_stop ? pcm_watchdog_lpib : 0
            };
        }
        pcm_recovery_since = irq_stop ? pcm_watchdog_time : now;
        PcmSchedDiag::record(PcmSchedDiag::Kind::HdaRecovery, now, 0, cause,
                             static_cast<uint32_t>((irq_stop ? pcm_watchdog_irq_gap : pcm_irq_timing.last_gap) / 1000ULL));
    }
    // No page can be cleared until STOP is confirmed. Retry on the next
    // scheduled turn after success; back off only if the device refused.
    const bool stopped = pcm_stop_dma();
    pcm_retry = true;
    pcm_retry_ns = now + (stopped ? 0 : HdaPcm::PERIOD_NS);
}

} // namespace

/* =========================================================================
 * 21. PUBLICZNA INICJALIZACJA
 * ========================================================================= */

bool inicjalizuj_hda() {
    if (__atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) return true;
    if (!hda_transport_wykryj()) return false;
    (void)hda_diag_inicjalizuj();

    bool any = false;
    for (uint32_t k = 0; k < hda_transport_liczba(); ++k) {
        const uint64_t start = czas_monotoniczny_ns();
        for (uint32_t steps = 0; steps < 2000000; ++steps) {
            if (hda_transport_init_krok(k)) break;
            if (czas_monotoniczny_ns() - start > 100000000ULL) break;
            pause_cpu();
        }
        HdaDiagKontroler info{};
        if (!hda_transport_stan(k, &info) || !info.gotowy) continue;
        any = true;
    }
    if (!any) return false;

    // Run discovery scan across ready controllers to discover topology and audio routes
    hda_diag_startuj_skan(HDA_DIAG_WSZYSTKIE_KONTROLERY);
    const uint64_t scan_start = czas_monotoniczny_ns();
    while (czas_monotoniczny_ns() - scan_start < 500000000ULL /* 500 ms bounded timeout */) {
        if (hda_diag_krok_skanu(HDA_DIAG_WSZYSTKIE_KONTROLERY)) break;
        pause_cpu();
    }

    const HdaDiagSnapshot* snap = hda_diag_pobierz_opublikowana(nullptr);
    const HdaDiagSciezka* najlepsza = nullptr;
    uint8_t najlepsza_ocena = 0;

    if (snap && snap->liczba_sciezek > 0) {
        for (uint32_t j = 0; j < snap->liczba_sciezek && j < HDA_DIAG_MAKS_SCIEZEK; ++j) {
            const auto& r = snap->sciezki[j];
            if (r.kontroler_idx >= hda_transport_liczba()) continue;
            HdaDiagKontroler kinfo{};
            if (!hda_transport_stan(r.kontroler_idx, &kinfo) || !kinfo.gotowy) continue;
            uint8_t waga = r.ocena;
            // Highest priority: built-in speaker on Intel controller
            if (kinfo.vendor_id == 0x8086 && r.cel == HDA_CEL_GLOSNIK) {
                waga = 150;
            } else if (r.cel == HDA_CEL_GLOSNIK) {
                waga = 120;
            }
            if (waga > najlepsza_ocena) {
                najlepsza_ocena = waga;
                najlepsza = &r;
            }
        }
    }

    if (najlepsza && snap) {
        const uint32_t k = najlepsza->kontroler_idx;
        HdaDiagKontroler info{};
        hda_transport_stan(k, &info);
        hda_kontroler_odtwarzania = k;
        hda_mmio_base = hda_transport_mmio(k);
        hda_gcap = info.gcap;
        hda_bdf = {info.bus, info.slot, info.func};
        hda_initial_statests = info.initial_statests;
        hda_discovered_codec_mask = info.discovered_codec_mask;
        hda_codec_addr = najlepsza->codec_cad;
        hda_stream_base = hda_mmio_base + 0x80 + uint64_t(info.iss) * 0x20;

        if (zaalokuj_dma() && resetuj_stream() && konfiguruj_trase_audio(*najlepsza, *snap)) {
            s_aktywna_sciezka = *najlepsza;
            s_sciezka_skonfigurowana = true;
            __atomic_store_n(&hda_gotowy, true, __ATOMIC_RELEASE);
            hda_transport_test_dostepny(k, true);
            loguj_konfiguracje_trasy(*najlepsza, *snap);
        } else {
            zwolnij_dma();
            hda_mmio_base = 0;
            hda_stream_base = 0;
            hda_kontroler_odtwarzania = UINT32_MAX;
            s_sciezka_skonfigurowana = false;
        }
    }

    // Fallback: If no topology route was configured, try proven QEMU setup on controller 0x8086:0x2668
    if (!__atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) {
        for (uint32_t k = 0; k < hda_transport_liczba(); ++k) {
            HdaDiagKontroler info{};
            if (!hda_transport_stan(k, &info) || !info.gotowy) continue;
            if (info.vendor_id == 0x8086 && info.device_id == 0x2668) {
                hda_kontroler_odtwarzania = k;
                hda_mmio_base = hda_transport_mmio(k);
                hda_gcap = info.gcap;
                hda_bdf = {info.bus, info.slot, info.func};
                hda_initial_statests = info.initial_statests;
                hda_discovered_codec_mask = info.discovered_codec_mask;
                hda_stream_base = hda_mmio_base + 0x80 + uint64_t(info.iss) * 0x20;
                if (znajdz_kodek() && zaalokuj_dma() && konfiguruj_kodek_qemu() && resetuj_stream()) {
                    __atomic_store_n(&hda_gotowy, true, __ATOMIC_RELEASE);
                    hda_transport_test_dostepny(k, true);
                    break;
                } else {
                    zwolnij_dma();
                    hda_mmio_base = 0;
                    hda_stream_base = 0;
                    hda_kontroler_odtwarzania = UINT32_MAX;
                }
            }
        }
    }

    return __atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE);
}

/* =========================================================================
 * 22. PUBLICZNY TON TESTOWY (ASYNCHRONICZNY / NON-BLOCKING)
 * ========================================================================= */

bool hda_test_ton(uint32_t hz, uint32_t ms) {
    return pcm_start_tone(HDA_TEST_TON, hz, ms, 3, false);
}

void hda_zatrzymaj_ton() {
    __atomic_store_n(&s_ton_stop_zadany, HDA_TON_STOP_RECZNY, __ATOMIC_RELEASE);
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (guard.aktywny()) (void)zatrzymaj_ton_wewnetrznie(HDA_TON_STOP_RECZNY);
}

// The timer never reads or fills sample memory. If the worker has not
// revalidated its DMA window in time, request RUN=0 without waiting in IRQ.
void hda_pcm_watchdog_irq(unsigned pid, bool ring0) {
    if (!__atomic_load_n(&pcm_interest, __ATOMIC_ACQUIRE)) return;
    const bool clock_ok = hpet_dostepny();
    const uint64_t now = clock_ok ? czas_monotoniczny_ns() : 1;
    if (pcm_irq_timing.last && now >= pcm_irq_timing.last) {
        pcm_irq_timing.last_gap = now - pcm_irq_timing.last;
        if (pcm_irq_timing.last_gap > pcm_irq_timing.max_gap) {
            pcm_irq_timing.max_gap = pcm_irq_timing.last_gap;
            pcm_irq_timing.pid = pid;
            pcm_irq_timing.ring0 = ring0;
        }
    }
    pcm_irq_timing.last = now;
    __atomic_add_fetch(&pcm_irq_timing.count, 1, __ATOMIC_RELAXED);
    uint64_t empty = 0;
    (void)__atomic_compare_exchange_n(&pcm_pending_ns, &empty, now, false,
                                      __ATOMIC_RELEASE, __ATOMIC_RELAXED);

    const uint64_t limit = __atomic_load_n(&pcm_watchdog_ns, __ATOMIC_ACQUIRE);
    if (!limit || __atomic_load_n(&pcm_watchdog_fired, __ATOMIC_RELAXED) ||
        (clock_ok && now < limit)) return;
    pcm_watchdog_lpib = 0;
    if (clock_ok) {
        ++pcm_irq_timing.rechecks;
        const uint32_t pos = mmio_odczytaj32(pcm_sd() + HDA_SD_LPIB);
        pcm_watchdog_lpib = pos;
        const uint64_t safe = pcm_watchdog_window.safe_until(pos, now);
        if (safe > now) {
            __atomic_store_n(&pcm_watchdog_ns, safe, __ATOMIC_RELEASE);
            ++pcm_irq_timing.extensions;
            return;
        }
    }
    const uint32_t reg = pcm_sd() + HDA_SD_CTL_LO;
    mmio_zapisz16(reg, mmio_odczytaj16(reg) & ~HDA_SD_CTL_RUN);
    pcm_watchdog_time = now;
    pcm_watchdog_irq_gap = pcm_irq_timing.last_gap;
    __atomic_store_n(&pcm_watchdog_fired, true, __ATOMIC_RELEASE);
}

bool hda_pcm_praca_oczekuje() {
    return __atomic_load_n(&pcm_pending_ns, __ATOMIC_ACQUIRE) != 0;
}

bool hda_blokada_aktywna() {
    return __atomic_load_n(&hda_blokada, __ATOMIC_ACQUIRE);
}

bool hda_pcm_pilny_w_toku() {
    return __atomic_load_n(&pcm_boost_w_toku, __ATOMIC_ACQUIRE);
}

// Called by scheduler with IF=0. Rate-limits priority return to at most once
// per 12 ms while a pending request remains unserviced, so a boosted PID0
// that gets preempted or hits a busy lock can still be boosted again.
bool hda_pcm_pilny_powrot_pid0() {
    const uint64_t pending = __atomic_load_n(&pcm_pending_ns, __ATOMIC_ACQUIRE);
    if (!pending || !hpet_dostepny()) return false;
    const uint64_t now = czas_monotoniczny_ns();
    if (now < pending || now - pending < 12000000ULL) return false;
    if (pending == pcm_last_priority_request && now >= pcm_last_priority_ns &&
        now - pcm_last_priority_ns < 12000000ULL)
        return false;
    pcm_last_priority_request = pending;
    pcm_last_priority_ns = now;
    __atomic_store_n(&pcm_boost_w_toku, true, __ATOMIC_RELEASE);
    ++pcm_irq_timing.boosts;
    return true;
}

void hda_obsluz_okresowo() {
#ifndef BURSZTYN_HDA_HOST_TEST
    uint64_t flags;
    asm volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    if (!(flags & (1ULL << 9))) return;
#endif
    __atomic_store_n(&pcm_boost_w_toku, false, __ATOMIC_RELEASE);
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) {
        if (hda_pcm_praca_oczekuje()) __atomic_add_fetch(&pcm_stats.blokady_pracy, 1, __ATOMIC_RELAXED);
        return; // the pending timestamp stays published
    }
    pcm_worker_request = __atomic_exchange_n(&pcm_pending_ns, 0, __ATOMIC_ACQ_REL);
    if (pcm_shutdown || !__atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) return;
    if (!pcm_running && !pcm_clients && !pcm_tone.handle) {
        pcm_interest_set(false);
        return;
    }

    const uint64_t now = czas_monotoniczny_ns();
    if (pcm_worker_request && now >= pcm_worker_request) {
        const uint64_t wait = now - pcm_worker_request;
        ++pcm_stats.paczki_pracy;
        pcm_stats.suma_czekania_ns += wait;
        if (wait > pcm_stats.maks_czekanie_ns) pcm_stats.maks_czekanie_ns = wait;
        if (wait >= PcmSchedDiag::LONG_SCHED_NS)
            PcmSchedDiag::record(PcmSchedDiag::Kind::HdaWorkerLate, now, 0,
                                 static_cast<uint32_t>(wait / 1000ULL),
                                 static_cast<uint32_t>(pcm_stats.blokady_pracy));
    }
    const bool clock_ok = hpet_dostepny();
    const bool controller_ok = (mmio_odczytaj32(HDA_REG_GCTL) & HDA_GCTL_CRST) != 0;
    const uint8_t dma_status = pcm_running ? stream_odczytaj_sts() : 0;
    const bool dma_error = (dma_status & (HDA_SD_STS_DESE | HDA_SD_STS_FIFOE)) != 0;
    if (!pcm_retry) {
        if (dma_status & HDA_SD_STS_FIFOE) ++pcm_stats.fifo_error;
        if (dma_status & HDA_SD_STS_DESE) ++pcm_stats.descriptor_error;
    }
    if (__atomic_load_n(&s_ton_aktywny, __ATOMIC_ACQUIRE)) {
        uint32_t reason = __atomic_load_n(&s_ton_stop_zadany, __ATOMIC_ACQUIRE);
        if (!controller_ok) reason = HDA_TON_STOP_RESET_KONTROLERA;
        else if (!clock_ok) reason = HDA_TON_STOP_BLAD_ZEGARA;
        else if (dma_error) reason = HDA_TON_STOP_BLAD_STRUMIENIA;
        else if (reason == HDA_TON_STOP_BRAK &&
                 now >= __atomic_load_n(&s_ton_deadline_ns, __ATOMIC_ACQUIRE))
            reason = HDA_TON_STOP_DEADLINE;
        if (reason != HDA_TON_STOP_BRAK) (void)zatrzymaj_ton_wewnetrznie(reason);
        else if (pcm_running) __atomic_store_n(&s_ton_last_running_ns, now, __ATOMIC_RELEASE);
    }

    using HdaPcm::Fault;
    if (dma_error) ++pcm_stats.bledy_dma;
    if (!clock_ok || !controller_ok || dma_error) {
        if (pcm_running) pcm_recover(now, clock_ok ? Fault::Device : Fault::Clock);
        return;
    }
    if (pcm_running) {
        if (pcm_retry) {
            if (now >= pcm_retry_ns) pcm_recover(now, Fault::None);
            return;
        }
        // Give a stream joining an active ring its full 2-period preroll grace
        // before forcing promotion, so it does not count spurious starvation.
        if (const uint32_t timed_out = pcm_mixer.promote_timed_out(now, 2 * HdaPcm::PERIOD_NS))
            pcm_stats.start_przedwczesny += timed_out;
        if (!pcm_clients && !pcm_tone.handle && pcm_idle_since &&
            now - pcm_idle_since >= 256000000ULL) {
            if (pcm_stop_dma()) pcm_interest_set(false);
            return;
        }
        if (now >= pcm_ring.sampled_ns) {
            const uint64_t gap = now - pcm_ring.sampled_ns;
            if (gap >= HdaPcm::LATE_NS) {
                ++pcm_stats.opoznienia;
                const uint64_t late = gap - HdaPcm::LATE_NS;
                if (late > pcm_stats.maks_spoznienie_ns) pcm_stats.maks_spoznienie_ns = late;
            }
        }
        const Fault fault = pcm_refill();
        if (fault != Fault::None) {
            pcm_recover(czas_monotoniczny_ns(), fault);
        } else if (!(stream_odczytaj_ctl() & HDA_SD_CTL_RUN)) {
            ++pcm_stats.bledy_dma;
            pcm_recover(now, Fault::Device);
        }
    } else if (pcm_app_clients && !pcm_tone.frames_left) {
        if (!pcm_mixer.has_pcm()) {
            pcm_app_prime_since = 0;
            return;
        }
        const auto queues = pcm_mixer.queue_levels();
        const bool ready = pcm_mixer.has_ready_pcm() &&
            (!queues.expecting || queues.minimum >= HdaPcm::PRIME * HdaPcm::PERIOD_FRAMES);
        if (!ready) {
            if (!pcm_app_prime_since) pcm_app_prime_since = now;
            const uint64_t grace = (pcm_retry || queues.expecting > 1)
                ? 2 * HdaPcm::PERIOD_NS : HdaPcm::PERIOD_NS;
            if (now - pcm_app_prime_since < grace) return;
            const uint32_t promoted = pcm_mixer.promote_all_nonempty();
            if (promoted) pcm_stats.start_przedwczesny += promoted;
            else ++pcm_stats.start_przedwczesny;
        }
        if (now >= pcm_retry_ns) {
            if (!pcm_start_dma()) {
                ++pcm_stats.bledy_dma;
                pcm_recover(now, Fault::Start);
            } else {
                pcm_app_prime_since = 0;
            }
        }
    } else {
        if (!pcm_mixer.has_pcm() && !pcm_retry) pcm_app_prime_since = 0;
        if ((pcm_clients || pcm_tone.handle) && (pcm_retry || pcm_tone.frames_left || pcm_mixer.has_pcm()) &&
            now >= pcm_retry_ns && !pcm_start_dma()) {
            ++pcm_stats.bledy_dma;
            pcm_recover(now, Fault::Start);
        }
    }
}

void hda_powiadom_reset_kontrolera(uint32_t kontroler_idx) {
    if (kontroler_idx != hda_kontroler_odtwarzania) return;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return;
    __atomic_store_n(&hda_gotowy, false, __ATOMIC_RELEASE);
    (void)zatrzymaj_ton_wewnetrznie(HDA_TON_STOP_RESET_KONTROLERA);
    (void)pcm_stop_dma();
    pcm_interest_set(false);
}

void hda_wew_pobierz_stan_tonu(HdaWewStanTonu* out) {
    if (!out) return;
    out->deadline_ns = __atomic_load_n(&s_ton_deadline_ns, __ATOMIC_ACQUIRE);
    out->stop_reason = __atomic_load_n(&s_ton_stop_powod, __ATOMIC_ACQUIRE);
    out->active = __atomic_load_n(&s_ton_aktywny, __ATOMIC_ACQUIRE) ? 1U : 0U;
    out->run_after_stop = __atomic_load_n(&s_ton_run_po_stop, __ATOMIC_ACQUIRE);
    out->last_running_ns = __atomic_load_n(&s_ton_last_running_ns, __ATOMIC_ACQUIRE);
    out->stopped_ns = __atomic_load_n(&s_ton_stopped_ns, __ATOMIC_ACQUIRE);
}

bool hda_ton_aktywny() {
    hda_obsluz_okresowo();
    return __atomic_load_n(&s_ton_aktywny, __ATOMIC_ACQUIRE);
}

/* =========================================================================
 * 23. PUBLICZNY STOP
 * ========================================================================= */

void hda_stop() {
    hda_zatrzymaj_ton();
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

uint8_t sciezka_test_cad = 0;
uint8_t sciezka_test_konw = 0;
uint8_t sciezka_test_pin = 0;
uint8_t sciezka_test_fizyczny = 0;

} // namespace

void hda_wew_pobierz_sciezke_testu(HdaWewSciezkaTestu* out) {
    if (!out) return;
    if (s_sciezka_skonfigurowana && __atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) {
        out->cad = s_aktywna_sciezka.codec_cad;
        out->konwerter_nid = s_aktywna_sciezka.konwerter_nid;
        out->pin_nid = s_aktywna_sciezka.pin_nid;
        out->routing_fizyczny = 1;
    } else if (s_legacy_qemu_potwierdzony && hda_codec_addr != 0xFFU &&
               __atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) {
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

bool hda_wew_test_start(uint32_t mode, uint32_t hz, uint32_t ms, uint8_t channels) {
    return pcm_start_tone(mode, hz, ms, channels, true);
}

void hda_wew_test_stop() {
    hda_zatrzymaj_ton();
}

bool hda_wew_test_tick(uint64_t) {
    // Zgodnosc diagnostyki; bezpieczenstwo deadline zapewnia sciezka produkcyjna.
    return hda_ton_aktywny();
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

/* =========================================================================
 * 25. GLOSNOSC SYSTEMOWA (Bursztynowy Zasobnik, BWS 107/108)
 * ========================================================================= */

void hda_glosnosc_info(HdaGlosnoscInfo* out) {
    if (!out) return;
    *out = HdaGlosnoscInfo{};
    out->procent = s_glosnosc.procent;
    out->wyciszony = s_glosnosc.wyciszony;
    out->generacja = s_glosnosc.generacja;
    if (!__atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) return;
    out->urzadzenie = true;
    out->odtwarza = (stream_odczytaj_ctl() & HDA_SD_CTL_RUN) != 0;
    if (s_wzmacniacz.regulowany()) {
        out->backend = HDA_GLOSNOSC_WZMACNIACZ;
        out->wyciszanie = s_wzmacniacz.wyciszanie();
        out->kodek = s_wzmacniacz.cad; out->wezel = s_wzmacniacz.nid;
        out->kroki = HdaGlosnosc::kroki(s_wzmacniacz.caps);
    } else {
        out->backend = HDA_GLOSNOSC_PCM;
        out->wyciszanie = true;                  // cisza w nowych okresach PCM
    }
    out->cel = s_sciezka_skonfigurowana ? s_aktywna_sciezka.cel : 0;
    HdaDiagKontroler k{};
    if (hda_kontroler_odtwarzania != UINT32_MAX && hda_transport_stan(hda_kontroler_odtwarzania, &k)) {
        out->pci_vendor = k.vendor_id; out->pci_device = k.device_id;
    }
}

namespace {
/* Nowy stan: wzmacniacz albo skalowanie PCM. Blad sprzetu = stan bez zmian. */
uint32_t hda_glosnosc_zastosuj(const HdaGlosnosc::Stan& nowy) {
    if (!__atomic_load_n(&hda_gotowy, __ATOMIC_ACQUIRE)) return HDA_GLOSNOSC_NIEOBSLUGIWANE;
    if (s_wzmacniacz.regulowany()) {
        if (nowy.wyciszony && !s_wzmacniacz.wyciszanie()) return HDA_GLOSNOSC_NIEOBSLUGIWANE;
        uint32_t resp = 0;
        if (!wyslij_immediate(verb4(s_wzmacniacz.cad, s_wzmacniacz.nid, HDA_VERB4_SET_AMP_GAIN_MUTE,
                                    HdaGlosnosc::payload(s_wzmacniacz, nowy)), &resp))
            return HDA_GLOSNOSC_ZAJETE;
        s_glosnosc.procent = nowy.procent; s_glosnosc.wyciszony = nowy.wyciszony;
    } else {
        s_glosnosc.procent = nowy.procent; s_glosnosc.wyciszony = nowy.wyciszony;
    }
    ++s_glosnosc.generacja;
    return HDA_GLOSNOSC_OK;
}
}  // namespace

uint32_t hda_glosnosc_ustaw(uint32_t procent) {
    if (procent > 100U) return HDA_GLOSNOSC_ZLE_ARGUMENTY;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return HDA_GLOSNOSC_ZAJETE;
    HdaGlosnosc::Stan n = s_glosnosc;
    n.procent = procent;
    return hda_glosnosc_zastosuj(n);
}

uint32_t hda_wyciszenie_ustaw(bool wyciszony) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return HDA_GLOSNOSC_ZAJETE;
    HdaGlosnosc::Stan n = s_glosnosc;
    n.wyciszony = wyciszony;
    return hda_glosnosc_zastosuj(n);
}

namespace PcmJadro {
namespace {
Wynik wynik(Pcm::Result result) {
    switch (result) {
        case Pcm::OK: return Wynik::Ok;
        case Pcm::LIMIT: return Wynik::Limit;
        case Pcm::BAD_HANDLE: return Wynik::ZlyUchwyt;
        case Pcm::FULL: return Wynik::Pelny;
        default: return Wynik::ZleDane;
    }
}
}

Wynik otworz(Uchwyt& out) {
    out = 0;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return Wynik::Zajete;
    if (!pcm_device_ready()) return Wynik::Niedostepne;
    const auto rc = pcm_mixer.open(pcm_internal, {Pcm::RATE, 2, Pcm::FORMAT_S16}, &out);
    if (rc == Pcm::OK) {
        ++pcm_clients;
        pcm_idle_since = 0;
        pcm_interest_set(true);
    }
    return wynik(rc);
}

Wynik zapisz(Uchwyt handle, const int16_t* samples, uint32_t frames, uint32_t& accepted) {
    accepted = 0;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return Wynik::Zajete;
    if (!pcm_device_ready()) return Wynik::Niedostepne;
    return wynik(pcm_mixer.write(pcm_internal, handle, samples, frames, &accepted));
}

Wynik glosnosc(Uchwyt handle, uint32_t volume, bool mute) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return Wynik::Zajete;
    return wynik(pcm_mixer.gain(pcm_internal, handle, volume, mute));
}

Wynik zamknij(Uchwyt handle) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return Wynik::Zajete;
    const auto rc = pcm_mixer.close(pcm_internal, handle);
    if (rc == Pcm::OK && !--pcm_clients)
        pcm_idle_since = czas_monotoniczny_ns();
    return wynik(rc);
}

bool wylacz() {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny() || !pcm_stop_dma()) return false;
    (void)zatrzymaj_ton_wewnetrznie(HDA_TON_STOP_RECZNY);
    pcm_mixer.cleanup(pcm_internal);
    pcm_mixer.cleanup(pcm_legacy);
    pcm_clients = 0;
    pcm_shutdown = true;
    pcm_interest_set(false);
    return true;
}

bool statystyki(Statystyki& out) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return false;
    {
        PcmCommitIrq commit;
        out = pcm_stats;
        out.przerwania = pcm_irq_timing.count;
        out.maks_irq_ns = pcm_irq_timing.max_gap;
        out.maks_irq_pid = pcm_irq_timing.pid;
        out.maks_irq_ring0 = pcm_irq_timing.ring0;
        out.pilne_powroty = pcm_irq_timing.boosts;
        out.przedluzenia_watchdoga = pcm_irq_timing.extensions;
        out.proby_watchdoga = pcm_irq_timing.rechecks;
    }
    out.blokady = __atomic_load_n(&pcm_guard_misses, __ATOMIC_RELAXED);
    out.maks_blokada_ns = pcm_guard_max_ns;
    out.braki_ramek = pcm_mixer.underrun_frames();
    return true;
}

bool zdarzenie(uint32_t index, Zdarzenie& out) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny() || index >= pcm_stats.zapisane_zdarzenia) return false;
    out = pcm_events[index];
    return true;
}
bool slad_pcm(uint32_t index, SladPcm& out) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny() || index >= pcm_shortfall_count) return false;
    out = pcm_shortfalls[index];
    return true;
}
}

#include "sterowniki/dzwiek/hda_pcm_aplikacji.h"
struct PcmFinalStats { Pcm::Owner owner; BwsPcmStats stats; };
PcmFinalStats pcm_final_stats[Pcm::MAX_STREAMS]{};
uint32_t pcm_final_count = 0;
void pcm_fill_sched_stats(Pcm::Owner owner, BwsPcmStats& stats) {
    PcmSchedDiag::PidStats sched{};
    if (PcmSchedDiag::snapshot_pid(owner.pid, sched)) {
        stats.max_runnable_wait_ns = sched.max_runnable_wait_ns;
        stats.max_voluntary_wait_ns = sched.max_voluntary_wait_ns;
        stats.max_exec_gap_ns = sched.max_exec_gap_ns;
    }
}
void pcm_archive(Pcm::Owner owner, uint64_t handle) {
    if (pcm_final_count == Pcm::MAX_STREAMS) return;
    BwsPcmStats stats{};
    if (pcm_mixer.statistics(owner, handle, stats) == Pcm::OK) {
        pcm_fill_sched_stats(owner, stats);
        pcm_final_stats[pcm_final_count++] = {owner, stats};
    }
}
// Implementacja BWS 110 dla HDA. Publiczne PcmAplikacji:: (jadro/dzwiek/wyjscie_audio.cpp) kieruje tu
// wywolania tylko wtedy, gdy HDA jest wybranym wyjsciem audio.
namespace HdaPcmAplikacji {
static int64_t result(Pcm::Result rc) {
    switch (rc) {
        case Pcm::OK: return PCM_OK;
        case Pcm::UNSUPPORTED: return PCM_UNSUPPORTED;
        case Pcm::LIMIT: return PCM_LIMIT;
        case Pcm::BAD_HANDLE: return PCM_HANDLE;
        case Pcm::FULL: return PCM_FULL;
        default: return PCM_INVALID;
    }
}
int64_t open(Pcm::Owner owner, const BwsPcmFormat& fmt) {
    if (fmt.version != BWS_PCM_V1 || fmt.size != sizeof(fmt) || fmt.reserved)
        return PCM_INVALID;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return PCM_BUSY;
    if (!pcm_device_ready()) return PCM_DEVICE;
    uint64_t handle = 0;
    const auto rc = pcm_mixer.open(owner, {fmt.rate, fmt.channels, fmt.encoding},
                                   &handle, 2 * HdaPcm::PERIOD_FRAMES);
    if (rc != Pcm::OK) return result(rc);
    ++pcm_clients;
    ++pcm_app_clients;
    ++pcm_stats.otwarcia_aplikacji;
    PcmSchedDiag::track_pid(owner.pid);
    pcm_idle_since = 0;
    pcm_interest_set(true);
    return static_cast<int64_t>(handle);
}
inline void pcm_notify_ready(uint64_t now) {
    if (!pcm_running && pcm_mixer.has_ready_pcm()) {
        uint64_t empty = 0;
        (void)__atomic_compare_exchange_n(&pcm_pending_ns, &empty, now ? now : 1ULL,
                                          false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    }
}

int64_t write(Pcm::Owner owner, uint64_t handle, const int16_t* samples, uint32_t frames, uint64_t copy_ns) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return PCM_BUSY;
    if (!pcm_device_ready()) return PCM_DEVICE;
    uint32_t accepted = 0;
    const uint64_t now = czas_monotoniczny_ns();
    const auto rc = pcm_mixer.write(owner, handle, samples, frames, &accepted, now);
    if (rc == Pcm::OK) {
        const uint64_t gap = pcm_mixer.write_timing(owner, handle, now, copy_ns);
        if (gap >= PcmSchedDiag::LONG_WRITE_NS)
            PcmSchedDiag::record(PcmSchedDiag::Kind::PcmWriteGap, now, owner.pid,
                                 static_cast<uint32_t>(gap / 1000ULL), accepted);
        pcm_notify_ready(now);
    }
    return rc == Pcm::OK ? static_cast<int64_t>(accepted) : result(rc);
}
int64_t query(Pcm::Owner owner, uint64_t handle, BwsPcmState& state) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return PCM_BUSY;
    Pcm::Status status{};
    const auto rc = pcm_mixer.status(owner, handle, &status);
    if (rc != Pcm::OK) return result(rc);
    state = {BWS_PCM_V1, sizeof(state), status.queued, status.capacity,
             status.volume, status.muted, status.channels, Pcm::RATE};
    return PCM_OK;
}
int64_t statistics(Pcm::Owner owner, uint64_t handle, BwsPcmStats& out) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return PCM_BUSY;
    const auto rc = pcm_mixer.statistics(owner, handle, out);
    if (rc == Pcm::OK) pcm_fill_sched_stats(owner, out);
    return result(rc);
}
int64_t gain(Pcm::Owner owner, uint64_t handle, uint64_t volume, uint64_t mute) {
    if (volume > 100 || mute > 1) return PCM_INVALID;
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return PCM_BUSY;
    return result(pcm_mixer.gain(owner, handle, volume, mute));
}
int64_t close(Pcm::Owner owner, uint64_t handle) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return PCM_BUSY;
    pcm_archive(owner, handle);
    const auto rc = pcm_mixer.close(owner, handle);
    if (rc == Pcm::OK) {
        if (pcm_app_clients) --pcm_app_clients;
        if (!--pcm_clients) pcm_idle_since = czas_monotoniczny_ns();
    }
    return result(rc);
}
int64_t finish(Pcm::Owner owner, uint64_t handle) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return PCM_BUSY;
    const auto rc = pcm_mixer.finish(owner, handle);
    if (rc == Pcm::OK) pcm_notify_ready(czas_monotoniczny_ns());
    return result(rc);
}
}
extern "C" bool hda_pcm_posprzataj_proces(uint32_t pid, uint64_t generation) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return false;
    const Pcm::Owner owner{pid, generation};
    pcm_mixer.archive_owner(owner, [](Pcm::Owner o, uint64_t h) { pcm_archive(o, h); });
    const auto removed = pcm_mixer.cleanup(owner);
    pcm_clients -= removed;
    if (removed <= pcm_app_clients) pcm_app_clients -= removed;
    else pcm_app_clients = 0;
    pcm_stats.usuniete_przy_zakonczeniu += removed;
    if (!pcm_clients) pcm_idle_since = czas_monotoniczny_ns();
    return true;
}


extern "C" bool hda_pcm_diag_stream(uint32_t pid, uint64_t generation, BwsPcmStats& out) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    if (!guard.aktywny()) return false;
    for (uint32_t i = 0; i < pcm_final_count; ++i)
        if (pcm_final_stats[i].owner.pid == pid && pcm_final_stats[i].owner.generation == generation) {
            out = pcm_final_stats[i].stats;
            return true;
        }
    return false;
}


extern "C" bool hda_pcm_diag_starvation(uint32_t index, Pcm::Starvation& out) {
    GuardHDA guard(HDA_TIMEOUT_LOCK);
    return guard.aktywny() && pcm_mixer.starvation(index, out);
}
