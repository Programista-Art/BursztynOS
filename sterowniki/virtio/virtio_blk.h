/*
 * Bursztyn OS - Sterownik dysku blokowego VirtIO-BLK (VirtIO 1.x Modern PCI)
 *
 * Plik: virtio_blk.h
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "virtio_pci.h"
#include "virtqueue.h"
#include "block_device.h"
#include "aktualizacja_rozruch.h"

/*
 * Sygnatura i struktura znacznika dedykowanego dysku testowego (scratch disk).
 * Wymagana do autoryzacji niszczacych testow zapisu VirtIO-BLK (LBA 1).
 */
#define VIRTIO_BLK_TEST_MARKER_LBA       1ULL
#define VIRTIO_BLK_TEST_MARKER_MAGIC     "BURSZTYN_TEST_SCRATCH_DISK_V1"
#define VIRTIO_BLK_TEST_SIGNATURE        0x425552535A54594EULL /* "BURSZTYN" */
#define VIRTIO_BLK_TEST_VERSION          1U
#define VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE 0x00000001U

#pragma pack(push, 1)
struct VirtioBlkTestMarker {
    char     magic[32];
    uint64_t signature;
    uint32_t version;
    uint32_t flags;
    uint64_t scratch_lba;
    uint32_t scratch_sectors;
    uint8_t  reserved[448];
    uint32_t crc32;
};
#pragma pack(pop)

static_assert(sizeof(VirtioBlkTestMarker) == 512, "VirtioBlkTestMarker rozmiar != 512 B");

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Typy zadan VirtIO-BLK (VirtIO 1.0 sec 5.2.6):
 */
#define VIRTIO_BLK_T_IN           0U
#define VIRTIO_BLK_T_OUT          1U
#define VIRTIO_BLK_T_FLUSH        4U
#define VIRTIO_BLK_T_GET_ID       8U

/*
 * Kody statusu zadania zwracane przez urzadzenie w bajcie statusu (1 bajt):
 */
#define VIRTIO_BLK_S_OK           0U
#define VIRTIO_BLK_S_IOERR        1U
#define VIRTIO_BLK_S_UNSUPP       2U

/*
 * Flagi cech urzadzenia VirtIO-BLK:
 */
#define VIRTIO_BLK_F_SIZE_MAX     (1ULL << 1)
#define VIRTIO_BLK_F_SEG_MAX      (1ULL << 2)
#define VIRTIO_BLK_F_GEOMETRY     (1ULL << 4)
#define VIRTIO_BLK_F_RO           (1ULL << 5)
#define VIRTIO_BLK_F_BLK_SIZE     (1ULL << 6)
#define VIRTIO_BLK_F_FLUSH        (1ULL << 9)
#define VIRTIO_BLK_F_TOPOLOGY     (1ULL << 10)

/*
 * Maksymalny rozmiar pojedynczego bufora DMA na jedno zadanie transferu (128 KiB = 256 sektorow 512 B).
 * Dzieki integracji z BlockDevice API, wieksze zadania sa automatycznie dzielone
 * przez wbudowany mechanizm chunking (BlockDevice::read / BlockDevice::write).
 */
constexpr size_t VIRTIO_BLK_BOUNCE_BUFFER_SIZE = 128U * 1024U;

/*
 * Domyslny timeout w milisekundach dla pojedynczej operacji blokowej (5000 ms = 5 s).
 */
constexpr uint64_t VIRTIO_BLK_DEFAULT_TIMEOUT_MS = 5000ULL;

/*
 * Wtorna ochrona petli oczekiwania (spin count guard).
 */
constexpr uint32_t VIRTIO_BLK_MAX_SPINS_GUARD = 50000000U;

/*
 * Maksymalna liczba jednoczesnie obslugiwanych urzadzen blokowych VirtIO-BLK.
 */
constexpr uint32_t VIRTIO_BLK_MAX_DEVICES = 4U;

/*
 * Standardowy naglowek zadania VirtIO-BLK (16 bajtow, zgodny ze specyfikacja VirtIO 1.x):
 */
#pragma pack(push, 1)
struct VirtIOBlkReqHeader {
    uint32_t type;     /* VIRTIO_BLK_T_* */
    uint32_t reserved; /* Zawsze 0 */
    uint64_t sector;   /* Numer sektora poczatkowego w jednostkach 512-bajtowych */
};
#pragma pack(pop)

static_assert(sizeof(VirtIOBlkReqHeader) == 16, "VirtIOBlkReqHeader rozmiar != 16 B");

/*
 * Struktura stanu sterownika pojedynczego urzadzenia VirtIO-BLK:
 */
struct VirtioBlkDriver {
    VirtioPciDevice* dev;
    VirtQueue*       vq;
    BlockDevice      bdev;

    // Bufor DMA dla naglowka i bajtu statusu (zawsze 64-bajtowy)
    void*            ctrl_vaddr;
    uint64_t         ctrl_paddr;
    size_t           ctrl_pages;

    // Bufor DMA dla danych (bounce buffer dla operacji wejscia/wyjscia)
    void*            data_vaddr;
    uint64_t         data_paddr;
    size_t           data_pages;
    size_t           data_size;

    uint64_t         raw_capacity_512;
    uint64_t         total_sectors;
    uint32_t         logical_sector_size;
    uint32_t         physical_sector_size;
    uint32_t         max_transfer_sectors;

    bool             initialized;
    bool             has_error;
    bool             has_flush;
    bool             read_only;
    bool             use_msix;
    int              irq_vector;
    volatile uint32_t irq_count;
    volatile bool    request_done;

    uint32_t         lock;

    uint64_t         total_reads;
    uint64_t         total_writes;
    uint64_t         total_flushes;
    uint64_t         total_sectors_read;
    uint64_t         total_sectors_written;
};

/*
 * Inicjalizuje sterownik VirtIO-BLK dla wskazanego urzadzenia PCI Modern VirtIO.
 * Negocjuje wylacznie obslugiwane cechy, konfiguruje Split VirtQueue 0, alokuje DMA,
 * ustawia bit DRIVER_OK i rejestruje urzadzenie w blokowy_zarejestruj_dysk().
 */
bool virtio_blk_init(VirtioPciDevice* dev);

/*
 * Skanuje wykryte urzadzenia PCI VirtIO w poszukiwaniu dyskow VirtIO-BLK (Device ID 2)
 * i inicjalizuje wszystkie dostepne. Zwraca liczbe pomyslnie zarejestrowanych dyskow.
 */
uint32_t virtio_blk_probe_and_init(void);

/*
 * Resetuje wskazany dysk VirtIO-BLK: zatrzymuje urzadzenie przez potwierdzony reset
 * sprzetowy (status == 0), zwalnia kolejke VirtQueue i pamiec DMA.
 * W przypadku timeoutu resetu stosuje kwarantanne DMA.
 */
bool virtio_blk_reset(VirtioBlkDriver* drv);

/*
 * Zwraca wskaznik do sterownika o zadanym indeksie (0 .. virtio_blk_get_count() - 1).
 */
VirtioBlkDriver* virtio_blk_get_driver(uint32_t index);

/*
 * Zwraca liczbe aktywnych sterownikow VirtIO-BLK.
 */
uint32_t virtio_blk_get_count(void);

/*
 * Resetuje wszystkie zarejestrowane dyski VirtIO-BLK i czysci tablice sterownikow.
 */
void virtio_blk_reset_all(void);

/*
 * Test obciazeniowy / integracyjny dla wskazanego dysku VirtIO-BLK:
 * weryfikuje zapis, odczyt, flush, zawijanie pierscienia i spojnosc danych.
 * Niszczace operacje zapisu (Krok 3) sa dozwolone wylacznie gdy allow_destructive=true
 * ORAZ nosnik zostanie pozytywnie zweryfikowany jako dedykowany dysk testowy (znacznik LBA 1).
 */
bool virtio_blk_stress_test(VirtioBlkDriver* drv, bool allow_destructive = false);

/*
 * Polskie aliasy ABI zgodne ze stylem Bursztyn OS:
 */
static inline bool virtio_blk_zainicjalizuj(VirtioPciDevice* dev) {
    return virtio_blk_init(dev);
}
static inline uint32_t virtio_blk_skanuj_i_inicjalizuj(void) {
    return virtio_blk_probe_and_init();
}
static inline bool virtio_blk_zresetuj(VirtioBlkDriver* drv) {
    return virtio_blk_reset(drv);
}

#ifdef __cplusplus
}
#endif
