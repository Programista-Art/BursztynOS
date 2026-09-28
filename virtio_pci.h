/*
 * Bursztyn OS - Wspolna warstwa transportowa VirtIO 1.x Modern PCI
 *
 * Plik: virtio_pci.h
 *
 * Implementacja uniwersalnego transportu PCI zgodnego ze standardem VirtIO 1.x
 * (VirtIO 1.0/1.1/1.2 Modern Specification).
 *
 * Zapewnia:
 *   - wykrywanie urzadzen VirtIO Modern PCI (Vendor 0x1AF4),
 *   - bezpieczne parsowanie lancucha PCI Capabilities (PCI_CAP_ID_VNDR = 0x09),
 *   - wyszukiwanie i walidacje struktur:
 *       * VIRTIO_PCI_CAP_COMMON_CFG
 *       * VIRTIO_PCI_CAP_NOTIFY_CFG
 *       * VIRTIO_PCI_CAP_ISR_CFG
 *       * VIRTIO_PCI_CAP_DEVICE_CFG (opcjonalna)
 *   - bezpieczne mapowanie MMIO przez ZmapujStrone() z pelna ochrona przed overflow,
 *   - zarzadzanie rejestrami stanu (reset, ACKNOWLEDGE, DRIVER, FEATURES_OK),
 *   - negocjacje cech (wymog VIRTIO_F_VERSION_1),
 *   - rejestry konfiguracji kolejek VirtQueue dla kolejnych etapow,
 *   - dostep do obszaru powiadomien (notify) i rejestru ISR.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pci.h"

/* =========================================================================
 * 1. IDENTYFIKATORY PCI I TYPY STRUKTUR VIRTIO
 * ========================================================================= */

#define VIRTIO_PCI_VENDOR_ID UINT16_C(0x1AF4)

/*
 * Typy struktur w capability vendor-specific (VirtIO 1.0 sec 4.1.4):
 */
#define VIRTIO_PCI_CAP_COMMON_CFG 1U
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2U
#define VIRTIO_PCI_CAP_ISR_CFG    3U
#define VIRTIO_PCI_CAP_DEVICE_CFG 4U
#define VIRTIO_PCI_CAP_PCI_CFG    5U

/*
 * Bity rejestru device_status (VirtIO 1.0 sec 2.1):
 */
#define VIRTIO_STATUS_RESET              0x00U
#define VIRTIO_STATUS_ACKNOWLEDGE        0x01U
#define VIRTIO_STATUS_DRIVER             0x02U
#define VIRTIO_STATUS_DRIVER_OK          0x04U
#define VIRTIO_STATUS_FEATURES_OK        0x08U
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40U
#define VIRTIO_STATUS_FAILED             0x80U

/*
 * Standardowe flagi cech (features) VirtIO 1.x:
 */
#define VIRTIO_F_VERSION_1           (UINT64_C(1) << 32)
#define VIRTIO_F_ACCESS_PLATFORM     (UINT64_C(1) << 33)
#define VIRTIO_F_RING_PACKED         (UINT64_C(1) << 34)
#define VIRTIO_F_ORDER_PLATFORM      (UINT64_C(1) << 36)
#define VIRTIO_F_SR_IOV              (UINT64_C(1) << 37)
#define VIRTIO_F_NOTIFICATION_DATA   (UINT64_C(1) << 38)

/*
 * Identyfikatory typu urzadzenia VirtIO (VirtIO Device Type ID):
 */
#define VIRTIO_ID_NET     1U
#define VIRTIO_ID_BLOCK   2U
#define VIRTIO_ID_CONSOLE 3U
#define VIRTIO_ID_RNG     4U
#define VIRTIO_ID_BALLOON 5U
#define VIRTIO_ID_SCSI    8U
#define VIRTIO_ID_GPU     16U
#define VIRTIO_ID_INPUT   18U
#define VIRTIO_ID_SOCKET  19U

/* =========================================================================
 * 2. STRUKTURY REJESTROW MMIO VIRTIO 1.X
 * ========================================================================= */

#pragma pack(push, 1)

/*
 * Naglowek capability PCI VirtIO (VirtIO 1.0 sec 4.1.4):
 */
struct VirtioPciCapHeader {
    uint8_t cap_vndr;    /* 0x09 = PCI_CAP_ID_VNDR */
    uint8_t cap_next;    /* Nastepna pozycja na liscie */
    uint8_t cap_len;     /* Dlugosc biezacej struktury cap */
    uint8_t cfg_type;    /* VIRTIO_PCI_CAP_* */
    uint8_t bar;         /* Indeks BAR (0..5) */
    uint8_t id;          /* Opcjonalny identyfikator */
    uint8_t padding[2];
    uint32_t offset;     /* Przesuniecie wzgledem poczatku BAR */
    uint32_t length;     /* Dlugosc obszaru wewnatrz BAR */
};

/*
 * Capability powiadomien z mnoznikiem notify_off_multiplier:
 */
struct VirtioPciNotifyCapHeader {
    VirtioPciCapHeader cap;
    uint32_t notify_off_multiplier;
};

/*
 * Struktura konfiguracji wspolnej VirtIO (VirtIO 1.0 sec 4.1.4.3):
 */
struct VirtioPciCommonCfgRegs {
    /* Konfiguracja globalna urzadzenia: */
    uint32_t device_feature_select; /* R/W */
    uint32_t device_feature;        /* R/O dla sterownika */
    uint32_t driver_feature_select; /* R/W */
    uint32_t driver_feature;        /* R/W */
    uint16_t config_msix_vector;    /* R/W */
    uint16_t num_queues;            /* R/O dla sterownika */
    uint8_t  device_status;          /* R/W */
    uint8_t  config_generation;      /* R/O dla sterownika */

    /* Konfiguracja konkretnej kolejki VirtQueue: */
    uint16_t queue_select;          /* R/W */
    uint16_t queue_size;            /* R/W */
    uint16_t queue_msix_vector;     /* R/W */
    uint16_t queue_enable;          /* R/W */
    uint16_t queue_notify_off;      /* R/O dla sterownika */
    uint64_t queue_desc;            /* R/W */
    uint64_t queue_driver;          /* R/W */
    uint64_t queue_device;          /* R/W */
};

#pragma pack(pop)

static_assert(sizeof(VirtioPciCapHeader) == 16, "VirtioPciCapHeader rozmiar != 16");
static_assert(sizeof(VirtioPciNotifyCapHeader) == 20, "VirtioPciNotifyCapHeader rozmiar != 20");
static_assert(sizeof(VirtioPciCommonCfgRegs) == 56, "VirtioPciCommonCfgRegs rozmiar != 56");
static_assert(offsetof(VirtioPciCommonCfgRegs, device_status) == 0x14,
              "VirtIO device_status must be at common config offset 0x14");
static_assert(sizeof(VirtioPciCommonCfgRegs::device_status) == 1,
              "VirtIO device_status must use an 8-bit access");

/* =========================================================================
 * 3. STRUKTURA REPREZENTACJI URZADZENIA VIRTIO PCI
 * ========================================================================= */

struct VirtioPciBarInfo {
    uint64_t phys_addr;
    uint64_t size;
    volatile uint8_t* vaddr;
    bool is_mapped;
    bool is_64bit;
};

struct VirtioPciDevice {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;

    uint16_t pci_vendor_id;
    uint16_t pci_device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint8_t  revision;
    uint16_t virtio_id; // Typ urzadzenia (RNG=4, BLK=2, NET=1 itd.)

    /* Wskazniki i metadane zmapowanych obszarow MMIO: */
    volatile VirtioPciCommonCfgRegs* common_cfg;
    uint32_t common_cfg_len;
    uint8_t  common_cfg_bar;

    volatile uint8_t* notify_base;
    uint32_t notify_cap_len;
    uint32_t notify_multiplier;
    uint8_t  notify_bar;

    volatile uint8_t* isr;
    uint32_t isr_cap_len;
    uint8_t  isr_bar;

    volatile uint8_t* device_cfg;
    uint32_t device_cfg_len;
    uint8_t  device_cfg_bar;
    bool     has_device_cfg;

    /* Wynegocjowane cechy: */
    uint64_t device_features;
    uint64_t driver_features;

    /* Bufor zmapowanych BARow (0..5): */
    VirtioPciBarInfo bars[6];

    /* Generic PCI Interrupt (MSI-X / MSI): */
    bool has_msix;
    bool has_msi;
    bool msix_enabled;
    bool msi_enabled;
    PciMsixCapability msix_cap;
    PciMsiCapability  msi_cap;
    volatile PciMsixTableEntry* msix_table;
    uint8_t msix_table_bar;

    bool initialized;
};

/* =========================================================================
 * 4. PUBLICZNE API TRANSPORTU VIRTIO PCI
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Glowna inicjalizacja podsystemu VirtIO PCI: skanuje PCI i wykrywa urzadzenia modern.
 */
void virtio_pci_inicjalizuj();

/*
 * Probuje rozpoznac i zainicjalizowac urzadzenie VirtIO Modern PCI pod zadanym BDF.
 * Wykonuje pelna sekwencje: reset, ACKNOWLEDGE, DRIVER, negocjacja cech (wymog VERSION_1),
 * FEATURES_OK i weryfikacja zatwierdzenia. NIE ustawia DRIVER_OK.
 * Zwraca true w przypadku pelnego sukcesu.
 */
bool virtio_pci_probe(uint8_t bus, uint8_t slot, uint8_t func,
                      VirtioPciDevice* dev, uint64_t requested_features);

/*
 * Zwalnia zmapowane zasoby urzadzenia i czysci strukture.
 */
void virtio_pci_cleanup(VirtioPciDevice* dev);

/*
 * Zarzadzanie stanem urzadzenia (device_status):
 */
uint8_t virtio_pci_get_status(const VirtioPciDevice* dev);
void virtio_pci_set_status(VirtioPciDevice* dev, uint8_t status);
bool virtio_pci_reset(VirtioPciDevice* dev);

/*
 * Negocjacja cech (features):
 */
uint64_t virtio_pci_read_device_features(VirtioPciDevice* dev);
void virtio_pci_write_driver_features(VirtioPciDevice* dev, uint64_t features);
bool virtio_pci_negotiate_features(VirtioPciDevice* dev, uint64_t requested_features);

/*
 * Konfiguracja kolejek VirtQueue (dla przyszlej implementacji VirtQueue):
 */
void virtio_pci_select_queue(VirtioPciDevice* dev, uint16_t queue_index);
uint16_t virtio_pci_get_queue_size(const VirtioPciDevice* dev);
void virtio_pci_set_queue_size(VirtioPciDevice* dev, uint16_t size);
uint16_t virtio_pci_get_queue_msix_vector(const VirtioPciDevice* dev);
void virtio_pci_set_queue_msix_vector(VirtioPciDevice* dev, uint16_t vector);
uint16_t virtio_pci_get_queue_enable(const VirtioPciDevice* dev);
void virtio_pci_set_queue_enable(VirtioPciDevice* dev, uint16_t enable);
uint16_t virtio_pci_get_queue_notify_off(const VirtioPciDevice* dev);
void virtio_pci_set_queue_desc(VirtioPciDevice* dev, uint64_t desc_phys);
void virtio_pci_set_queue_driver(VirtioPciDevice* dev, uint64_t driver_phys);
void virtio_pci_set_queue_device(VirtioPciDevice* dev, uint64_t device_phys);
uint64_t virtio_pci_get_queue_desc(const VirtioPciDevice* dev);
uint64_t virtio_pci_get_queue_driver(const VirtioPciDevice* dev);
uint64_t virtio_pci_get_queue_device(const VirtioPciDevice* dev);

/*
 * Obliczanie adresu powiadomien i notyfikacja kolejki:
 */
volatile uint16_t* virtio_pci_notify_address(const VirtioPciDevice* dev, uint16_t queue_notify_off);
void virtio_pci_notify_queue(VirtioPciDevice* dev, uint16_t queue_index, uint16_t queue_notify_off);

/*
 * Odczyt rejestru przerwania ISR (odczyt kasuje flage przerwania w urzadzeniu):
 */
uint8_t virtio_pci_read_isr(VirtioPciDevice* dev);

/*
 * Obsluga licznika zmian konfiguracji urzadzenia (config generation):
 */
uint8_t virtio_pci_get_config_generation(const VirtioPciDevice* dev);

/*
 * Bezpieczny dostep do konfiguracji specyficznej dla urzadzenia (device-specific config):
 */
uint8_t  virtio_pci_read_device_config8(const VirtioPciDevice* dev, uint32_t offset);
uint16_t virtio_pci_read_device_config16(const VirtioPciDevice* dev, uint32_t offset);
uint32_t virtio_pci_read_device_config32(const VirtioPciDevice* dev, uint32_t offset);
uint64_t virtio_pci_read_device_config64(const VirtioPciDevice* dev, uint32_t offset);
void virtio_pci_write_device_config8(VirtioPciDevice* dev, uint32_t offset, uint8_t val);
void virtio_pci_write_device_config16(VirtioPciDevice* dev, uint32_t offset, uint16_t val);
void virtio_pci_write_device_config32(VirtioPciDevice* dev, uint32_t offset, uint32_t val);

/*
 * Odczytuje spojny blok konfiguracji specyficznej dla urzadzenia z uzyciem config_generation retry.
 * Zwraca true jesli udalo sie odczytac bez zmian pokolenia w trakcie kopiowania.
 */
bool virtio_pci_read_device_config_consistent(const VirtioPciDevice* dev,
                                              uint32_t offset,
                                              void* dst,
                                              uint32_t length,
                                              uint32_t max_retries = 8);

/*
 * Walidacja mnoznika notify_off_multiplier wedlug specyfikacji VirtIO:
 *  - bez VIRTIO_F_NOTIFICATION_DATA: 0 lub parzysta potega dwojki (2, 4, 8, ...)
 *  - z VIRTIO_F_NOTIFICATION_DATA: 0 lub potega dwojki bedaca wielokrotnoscia 4 (4, 8, 16, ...)
 */
bool virtio_pci_validate_notify_multiplier(uint32_t multiplier, bool has_notification_data);

/*
 * Czytelna nazwa urzadzenia VirtIO na podstawie identyfikatora:
 */
const char* virtio_pci_device_name(uint16_t virtio_id);

/*
 * Dostep do listy znalezionych urzadzen w systemie:
 */
uint32_t virtio_pci_liczba_urzadzen();
VirtioPciDevice* virtio_pci_pobierz_urzadzenie(uint32_t indeks);

/* Wylaczona domyslnie telemetria kontrolnego przebiegu bootu.  Implementacja
 * jest no-op, jezeli BURSZTYN_DIAG_VIRTIO_BOOT nie zostalo ustawione. */
void virtio_pci_diag_trace_device(const VirtioPciDevice* dev, const char* phase);
void virtio_pci_diag_trace_type(uint16_t virtio_id, const char* phase);
void virtio_pci_diag_trace_queue(const VirtioPciDevice* dev, uint16_t queue_index, const char* phase);

/*
 * MSI-X VirtIO helpers:
 */
uint16_t virtio_pci_get_config_msix_vector(const VirtioPciDevice* dev);
void virtio_pci_set_config_msix_vector(VirtioPciDevice* dev, uint16_t vector);
bool virtio_pci_set_queue_msix_vector_checked(VirtioPciDevice* dev, uint16_t queue_index, uint16_t entry_index);
bool virtio_pci_program_msix_entry(VirtioPciDevice* dev, uint16_t entry_index, uint8_t apic_id, uint8_t vector, bool masked);
bool virtio_pci_mask_msix_entry(VirtioPciDevice* dev, uint16_t entry_index);
bool virtio_pci_unmask_msix_entry(VirtioPciDevice* dev, uint16_t entry_index);
bool virtio_pci_is_entry_masked(const VirtioPciDevice* dev, uint16_t entry_index);

#ifdef __cplusplus
}
#endif
