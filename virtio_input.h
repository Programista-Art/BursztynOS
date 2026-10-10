/*
 * Bursztyn OS - Sterownik Urzadzen Wejsciowych VirtIO-INPUT (VirtIO 1.x Modern)
 *
 * Plik: virtio_input.h
 *
 * Implementacja sterownika urzadzen wejsciowych zgodnego ze specyfikacja
 * VirtIO 1.x (VirtIO 1.0/1.1/1.2 sec 5.8):
 *   - nowoczesny transport PCI (VirtIO Device Type ID 18, Vendor 0x1AF4),
 *   - wymog cechy VIRTIO_F_VERSION_1,
 *   - bezpieczne zapytania do przestrzeni konfiguracji (CFG_ID_NAME, SERIAL, DEVIDS, EV_BITS, ABS_INFO),
 *   - klasyfikacja urzadzenia na podstawie bitmapy cech EV (klawiatura, mysz wzgledna, tablet bezwzgledny),
 *   - kolejka zdarzen (eventq, queue 0) ze stale zarezerwowanymi buforami DMA i natychmiastowym recyclingiem,
 *   - obsluga przerwan MSI-X oraz bounded polling fallback,
 *   - integracja z jednolita abstrakcja InputDevice i dyspozytorem zdarzen jądra.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "virtio_pci.h"
#include "virtqueue.h"
#include "input_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. POLE SELECT I SUBSEL PRZESTRZENI KONFIGURACJI VIRTIO-INPUT
 * ========================================================================= */

#define VIRTIO_INPUT_CFG_UNSET     0x00U
#define VIRTIO_INPUT_CFG_ID_NAME   0x01U
#define VIRTIO_INPUT_CFG_ID_SERIAL 0x02U
#define VIRTIO_INPUT_CFG_ID_DEVIDS 0x03U
#define VIRTIO_INPUT_CFG_PROP_BITS 0x10U
#define VIRTIO_INPUT_CFG_EV_BITS   0x11U
#define VIRTIO_INPUT_CFG_ABS_INFO  0x12U

/* =========================================================================
 * 2. STRUKTURY DANYCH ZGODNE ZE SPECYFIKACJA VIRTIO 1.X (sec 5.8)
 * ========================================================================= */

#pragma pack(push, 1)

struct VirtioInputAbsInfo {
    uint32_t min;
    uint32_t max;
    uint32_t fuzz;
    uint32_t flat;
    uint32_t res;
};
static_assert(sizeof(VirtioInputAbsInfo) == 20, "VirtioInputAbsInfo rozmiar != 20 B");

struct VirtioInputDevids {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};
static_assert(sizeof(VirtioInputDevids) == 8, "VirtioInputDevids rozmiar != 8 B");

struct VirtioInputEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};
static_assert(sizeof(VirtioInputEvent) == 8, "VirtioInputEvent rozmiar != 8 B");

struct VirtioInputConfig {
    uint8_t select;
    uint8_t subsel;
    uint8_t size;
    uint8_t reserved[5];
    union {
        char string[128];
        uint8_t bitmap[128];
        VirtioInputAbsInfo abs;
        VirtioInputDevids ids;
    } u;
};
static_assert(offsetof(VirtioInputConfig, u) == 8, "input payload offset");
static_assert(sizeof(VirtioInputConfig) == 136, "VirtioInputConfig rozmiar != 136 B");

#pragma pack(pop)

/* =========================================================================
 * 3. STRUKTURA I STAN STEROWNIKA VIRTIO-INPUT
 * ========================================================================= */

constexpr size_t VIRTIO_INPUT_NUM_EVENT_BUFS = 64;
constexpr size_t VIRTIO_INPUT_MAX_DRIVERS = 8;
enum class VirtioInputBufferState : uint8_t { FREE, POSTED_TO_DEVICE, RETURNED_IN_USED_RING, BEING_PROCESSED };

enum class VirtioInputInitState : uint8_t {
    UNINITIALIZED, RESETTING, NEGOTIATING, QUEUE_SETUP, BUFFERS_POSTED,
    INTERRUPTS_READY, DRIVER_READY, REGISTERED, FAILED,
    PREPARED, ACTIVATING, ACTIVE
};

struct VirtioInputDriver {
    VirtioInputInitState init_state;
    uint32_t failed_init_stage;
    VirtioPciDevice*   dev;
    VirtQueue*         eventq;
    VirtQueue*         statusq;
    InputDevice        input_dev;

    /* Bufor DMA na pre-postowane deskryptory zdarzen */
    void*              dma_virt;
    uint64_t           dma_phys;
    size_t             dma_pages;
    VirtioInputEvent*  dma_events;
    size_t             num_event_bufs;

    /* Identyfikacja i wlasciwosci sprzetowe */
    char               name[128];
    char               serial[128];
    VirtioInputDevids  ids;

    bool               has_keyboard;
    bool               has_rel_mouse;
    bool               has_abs_tablet;

    /* Stan operacyjny i przerwania */
    bool               initialized;
    bool               has_error;
    bool               use_msix;
    int                irq_vector;
    volatile uint32_t  irq_count;
    volatile bool      has_pending_events;
    uint64_t           total_events_processed;
    // Service/reset exclusion is nonblocking: an IRQ or recursive poll never
    // waits for the interrupted owner. DMA stays live until confirmed reset.
    uint32_t           service_lock;
    bool               reset_requested;
    VirtioInputBufferState buffer_state[VIRTIO_INPUT_NUM_EVENT_BUFS];
    uint16_t           buffer_head[VIRTIO_INPUT_NUM_EVENT_BUFS];
    uint64_t           syn_sequence;
    uint32_t           diagnostic_events;
    bool               first_event_logged;
};

/* =========================================================================
 * 4. PUBLICZNE API STEROWNIKA VIRTIO-INPUT
 * ========================================================================= */

/* Bezpieczne odpytanie przestrzeni konfiguracji urzadzenia (select/subsel) */
bool virtio_input_query_config(VirtioPciDevice* dev, uint8_t select, uint8_t subsel,
                               void* out_data, uint8_t* out_size, size_t max_out);

/* Early preparation never sets DRIVER_OK, registers InputDevice, or notifies.
 * Activation is permitted only after the common late startup pass opens it. */
bool virtio_input_prepare(VirtioInputDriver* drv, VirtioPciDevice* dev);
bool virtio_input_activate(VirtioInputDriver* drv);
uint32_t virtio_input_activate_prepared(void);
/* Compatibility ABI: init now means prepare only. */
bool virtio_input_init(VirtioInputDriver* drv, VirtioPciDevice* dev);
bool virtio_input_reset(VirtioInputDriver* drv);
void virtio_input_reset_all(void);
void virtio_input_quiesce_for_reboot(void);

/* PCI preparation only, shared by normal/Installer/automated boot. */
uint32_t virtio_input_probe_and_prepare(void);
uint32_t virtio_input_probe_and_init(void);
size_t virtio_input_get_count(void);
VirtioInputDriver* virtio_input_get_driver(size_t index);

/* Pobieranie i dyspozycja zakonczonych zdarzen z kolejki eventq */
size_t virtio_input_poll_events(VirtioInputDriver* drv, size_t max_events);

/* Wspolna funkcja serwisowa wywolywana periodycznie z glownej petli idle */
void virtio_input_obsluz(void);

/* Testy integracyjne / obciazeniowe (wylacznie w trybie bursztyn.test=automated) */
bool virtio_input_stress_test(VirtioInputDriver* drv);
bool virtio_input_stress_test_all(void);

/* Polskie aliasy ABI zgodne ze stylem Bursztyn OS: */
static inline bool virtio_input_zainicjalizuj(VirtioInputDriver* drv, VirtioPciDevice* dev) {
    return virtio_input_init(drv, dev);
}
static inline bool virtio_input_zresetuj(VirtioInputDriver* drv) {
    return virtio_input_reset(drv);
}
static inline size_t virtio_input_obsluz_zdarzenia(VirtioInputDriver* drv, size_t max_events) {
    return virtio_input_poll_events(drv, max_events);
}

#ifdef __cplusplus
}
#endif
