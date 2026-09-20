/*
 * Bursztyn OS - Sterownik karty sieciowej VirtIO-NET (VirtIO 1.x Modern PCI)
 *
 * Plik: virtio_net.h
 *
 * Uniwersalny sterownik sieciowy zgodny ze specyfikacja VirtIO 1.x (Modern PCI),
 * integrujacy sie z generycznym rejestrem NetworkDevice API.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "virtio_pci.h"
#include "virtqueue.h"
#include "network_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. STALE I FLAGE CECH VIRTIO-NET (VirtIO 1.0/1.1 sec 5.1)
 * ========================================================================= */

#define VIRTIO_NET_F_CSUM                   (UINT64_C(1) << 0)
#define VIRTIO_NET_F_GUEST_CSUM             (UINT64_C(1) << 1)
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS    (UINT64_C(1) << 2)
#define VIRTIO_NET_F_MTU                    (UINT64_C(1) << 3)
#define VIRTIO_NET_F_MAC                    (UINT64_C(1) << 5)
#define VIRTIO_NET_F_GSO                    (UINT64_C(1) << 6)
#define VIRTIO_NET_F_GUEST_TSO4             (UINT64_C(1) << 7)
#define VIRTIO_NET_F_GUEST_TSO6             (UINT64_C(1) << 8)
#define VIRTIO_NET_F_GUEST_ECN              (UINT64_C(1) << 9)
#define VIRTIO_NET_F_GUEST_UFO              (UINT64_C(1) << 10)
#define VIRTIO_NET_F_HOST_TSO4              (UINT64_C(1) << 11)
#define VIRTIO_NET_F_HOST_TSO6              (UINT64_C(1) << 12)
#define VIRTIO_NET_F_HOST_ECN               (UINT64_C(1) << 13)
#define VIRTIO_NET_F_HOST_UFO               (UINT64_C(1) << 14)
#define VIRTIO_NET_F_MRG_RXBUF              (UINT64_C(1) << 15)
#define VIRTIO_NET_F_STATUS                 (UINT64_C(1) << 16)
#define VIRTIO_NET_F_CTRL_VQ                (UINT64_C(1) << 17)
#define VIRTIO_NET_F_CTRL_RX                (UINT64_C(1) << 18)
#define VIRTIO_NET_F_CTRL_VLAN              (UINT64_C(1) << 19)
#define VIRTIO_NET_F_GUEST_ANNOUNCE         (UINT64_C(1) << 21)
#define VIRTIO_NET_F_MQ                     (UINT64_C(1) << 22)
#define VIRTIO_NET_F_CTRL_MAC_ADDR          (UINT64_C(1) << 23)

/* Bity pola status w konfiguracji urzadzenia */
#define VIRTIO_NET_S_LINK_UP                0x0001U
#define VIRTIO_NET_S_ANNOUNCE               0x0002U

/* Indeksy kolejek VirtQueue */
#define VIRTIO_NET_RX_QUEUE_INDEX           0U
#define VIRTIO_NET_TX_QUEUE_INDEX           1U

/* Limity rozmiarow i buforow */
constexpr size_t   VIRTIO_NET_BUFFER_SIZE     = 2048U;
constexpr uint32_t VIRTIO_NET_MAX_RX_BUFFERS  = 128U;
constexpr uint32_t VIRTIO_NET_MAX_TX_BUFFERS  = 64U;
constexpr uint32_t VIRTIO_NET_MAX_DEVICES     = 4U;

/* =========================================================================
 * 2. STRUKTURY NAGLOWKA I KONFIGURACJI VIRTIO-NET
 * ========================================================================= */

#pragma pack(push, 1)

/*
 * Standardowy naglowek pakietu VirtIO-NET dla transportu Modern PCI (VirtIO 1.x / VIRTIO_F_VERSION_1):
 * Zgodnie ze specyfikacja VirtIO 1.0 (sec 5.1.6) dla urzadzen modern (VIRTIO_F_VERSION_1)
 * naglowek virtio_net_hdr zawiera pole num_buffers (lacznie 12 bajtow).
 * W nadawaniu (TX) pole num_buffers musi byc wyzerowane (0).
 */
struct VirtioNetHdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
};

/*
 * Struktura konfiguracji specyficznej dla urzadzenia (device-specific config):
 */
struct VirtioNetConfig {
    uint8_t  mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
};

#pragma pack(pop)

static_assert(sizeof(VirtioNetHdr) == 12, "VirtioNetHdr rozmiar != 12");
static_assert(sizeof(VirtioNetConfig) == 12, "VirtioNetConfig rozmiar != 12");

/* =========================================================================
 * 3. STRUKTURA STANU STEROWNIKA VIRTIO-NET
 * ========================================================================= */

struct VirtioNetDriver {
    VirtioPciDevice* dev;
    VirtQueue*       rx_vq;
    VirtQueue*       tx_vq;
    NetworkDevice    net_dev;

    /* Pula buforow DMA dla odbioru (RX) */
    void*            rx_dma_vaddr;
    uint64_t         rx_dma_paddr;
    size_t           rx_dma_pages;
    size_t           rx_dma_size;
    uint32_t         rx_count;

    /* Pula buforow DMA dla nadawania (TX) */
    void*            tx_dma_vaddr;
    uint64_t         tx_dma_paddr;
    size_t           tx_dma_pages;
    size_t           tx_dma_size;
    uint32_t         tx_count;
    bool             tx_slot_busy[VIRTIO_NET_MAX_TX_BUFFERS];

    bool             initialized;
    bool             has_error;
    bool             link_up;
    bool             use_msix;

    int              rx_irq_vector;
    int              tx_irq_vector;
    volatile uint64_t rx_irq_count;
    volatile uint64_t tx_irq_count;
    volatile bool    rx_interrupt_pending;
    volatile bool    tx_interrupt_pending;

    uint32_t         rx_lock;
    uint32_t         tx_lock;
    uint32_t         reset_lock;
};

/* =========================================================================
 * 4. PUBLICZNE API STEROWNIKA VIRTIO-NET
 * ========================================================================= */

/*
 * Inicjalizuje sterownik VirtIO-NET dla wskazanego urzadzenia PCI Modern VirtIO.
 * Negocjuje wylacznie obslugiwane cechy, konfiguruje kolejki RX (0) i TX (1),
 * alokuje DMA, pre-postuje bufory odbiorcze, konfiguruje MSI-X lub fallback pollingowy,
 * ustawia DRIVER_OK i rejestruje urzadzenie w NetworkDevice API (net_register_device).
 */
bool virtio_net_init(VirtioPciDevice* dev);

/*
 * Skanuje wykryte urzadzenia PCI VirtIO w poszukiwaniu kart VirtIO-NET (Device ID 1)
 * i inicjalizuje wszystkie dostepne. Zwraca liczbe pomyslnie zarejestrowanych kart.
 */
uint32_t virtio_net_probe_and_init(void);

/*
 * Resetuje wskazana karte VirtIO-NET: zatrzymuje urzadzenie przez potwierdzony reset
 * sprzetowy (status == 0), wyrejestrowuje z NetworkDevice, zwalnia kolejki i pamiec DMA.
 * W przypadku timeoutu resetu stosuje kwarantanne DMA.
 */
bool virtio_net_reset(VirtioNetDriver* drv);

/*
 * Resetuje wszystkie zarejestrowane karty VirtIO-NET i czysci tablice sterownikow.
 */
void virtio_net_reset_all(void);

/*
 * Zwraca wskaznik do sterownika o zadanym indeksie (0 .. virtio_net_get_count() - 1).
 */
VirtioNetDriver* virtio_net_get_driver(uint32_t index);

/*
 * Zwraca liczbe aktywnych sterownikow VirtIO-NET.
 */
uint32_t virtio_net_get_count(void);

/*
 * Operacje NetworkDevice:
 */
NetError virtio_net_transmit(NetworkDevice* dev, const void* frame, size_t length);
void virtio_net_poll_rx(NetworkDevice* dev);
NetLinkState virtio_net_get_link_state(const NetworkDevice* dev);
void virtio_net_get_stats(const NetworkDevice* dev, NetDeviceStats* out_stats);

/*
 * Test obciazeniowy / integracyjny dla wskazanej karty VirtIO-NET.
 */
bool virtio_net_stress_test(VirtioNetDriver* drv);

/*
 * Polskie aliasy ABI zgodne ze stylem Bursztyn OS:
 */
static inline bool virtio_net_zainicjalizuj(VirtioPciDevice* dev) {
    return virtio_net_init(dev);
}
static inline uint32_t virtio_net_skanuj_i_inicjalizuj(void) {
    return virtio_net_probe_and_init();
}
static inline bool virtio_net_zresetuj(VirtioNetDriver* drv) {
    return virtio_net_reset(drv);
}

#ifdef __cplusplus
}
#endif
