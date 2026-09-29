/*
 * Bursztyn OS - Sterownik karty graficznej VirtIO-GPU (VirtIO 1.x Modern PCI 2D)
 *
 * Plik: virtio_gpu.h
 *
 * Definicje struktur i interfejsu VirtIO-GPU 2D zgodne ze standardem VirtIO 1.0 / 1.1 / 1.2
 * (Device ID 16, Queue 0: controlq, Queue 1: cursorq).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "virtio_pci.h"
#include "virtqueue.h"

#ifndef BURSZTYN_VIRTIO_HOST_TEST
#include "sterowniki/dma.h"
#else
struct DmaBuffer {
    void* virtual_address;
    uint64_t physical_address;
    size_t size;
    size_t page_count;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. CECHY (FEATURES) I STALE VIRTIO-GPU
 * ========================================================================= */

#define VIRTIO_GPU_F_VIRGL (UINT64_C(1) << 0)
#define VIRTIO_GPU_F_EDID  (UINT64_C(1) << 1)

#define VIRTIO_GPU_MAX_SCANOUTS 16U
#define VIRTIO_GPU_MAX_DEVICES   4U

#define VIRTIO_GPU_FLAG_FENCE (UINT32_C(1) << 0)

/* =========================================================================
 * 2. TYPY POLECEN I ODPOWIEDZI (CONTROL TYPES)
 * ========================================================================= */

enum VirtioGpuCtrlType : uint32_t {
    /* Polecenia 2D */
    VIRTIO_GPU_CMD_GET_DISPLAY_INFO        = 0x0100U,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      = 0x0101U,
    VIRTIO_GPU_CMD_RESOURCE_UNREF          = 0x0102U,
    VIRTIO_GPU_CMD_SET_SCANOUT             = 0x0103U,
    VIRTIO_GPU_CMD_RESOURCE_FLUSH          = 0x0104U,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     = 0x0105U,
    VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  = 0x0106U,
    VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING  = 0x0107U,
    VIRTIO_GPU_CMD_GET_CAPSET_INFO         = 0x0108U,
    VIRTIO_GPU_CMD_GET_CAPSET              = 0x0109U,
    VIRTIO_GPU_CMD_GET_EDID                = 0x010aU,

    /* Polecenia kursora */
    VIRTIO_GPU_CMD_UPDATE_CURSOR           = 0x0300U,
    VIRTIO_GPU_CMD_MOVE_CURSOR             = 0x0301U,

    /* Odpowiedzi sukcesu */
    VIRTIO_GPU_RESP_OK_NODATA              = 0x1100U,
    VIRTIO_GPU_RESP_OK_DISPLAY_INFO        = 0x1101U,
    VIRTIO_GPU_RESP_OK_CAPSET_INFO         = 0x1102U,
    VIRTIO_GPU_RESP_OK_CAPSET              = 0x1103U,
    VIRTIO_GPU_RESP_OK_EDID                = 0x1104U,

    /* Odpowiedzi bledu */
    VIRTIO_GPU_RESP_ERR_UNSPEC             = 0x1200U,
    VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY      = 0x1201U,
    VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID = 0x1202U,
    VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID= 0x1203U,
    VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID = 0x1204U,
    VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER  = 0x1205U
};

/* =========================================================================
 * 3. FORMATY PIKSELI 2D (VIRTIO_GPU_FORMATS)
 * ========================================================================= */

enum VirtioGpuFormat : uint32_t {
    VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1U,
    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2U,
    VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3U,
    VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4U,
    VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67U,
    VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM = 68U
};

#ifdef __cplusplus
static_assert(VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM == 1U, "VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM must be 1 per VirtIO spec");
static_assert(VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM == 2U, "VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM must be 2 per VirtIO spec");
static_assert(VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM == 3U, "VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM must be 3 per VirtIO spec");
static_assert(VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM == 4U, "VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM must be 4 per VirtIO spec");
static_assert(VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM == 67U, "VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM must be 67 per VirtIO spec");
static_assert(VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM == 68U, "VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM must be 68 per VirtIO spec");
#endif

/* =========================================================================
 * 4. STRUKTURY PROTOKOLU VIRTIO-GPU
 * ========================================================================= */

#pragma pack(push, 1)

/* Naglowek kazdego polecenia i odpowiedzi */
struct VirtioGpuCtrlHdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuCtrlHdr) == 24, "VirtioGpuCtrlHdr must be 24 bytes");
#endif

/* Struktura prostokata 2D */
struct VirtioGpuRect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuRect) == 16, "VirtioGpuRect must be 16 bytes");
#endif

/* Informacja o pojedynczym scanoucie */
struct VirtioGpuDisplayOne {
    VirtioGpuRect r;
    uint32_t enabled;
    uint32_t flags;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuDisplayOne) == 24, "VirtioGpuDisplayOne must be 24 bytes");
#endif

/* Odpowiedz VIRTIO_GPU_RESP_OK_DISPLAY_INFO */
struct VirtioGpuRespDisplayInfo {
    VirtioGpuCtrlHdr hdr;
    VirtioGpuDisplayOne pmodes[VIRTIO_GPU_MAX_SCANOUTS];
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuRespDisplayInfo) == 408, "VirtioGpuRespDisplayInfo must be 408 bytes");
#endif

/* Polecenie VIRTIO_GPU_CMD_RESOURCE_CREATE_2D */
struct VirtioGpuResourceCreate2d {
    VirtioGpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuResourceCreate2d) == 40, "VirtioGpuResourceCreate2d must be 40 bytes");
#endif

/* Wpis pamieci fizycznej scatter-gather dla bufora backing */
struct VirtioGpuMemEntry {
    uint64_t addr;   /* 64-bitowy adres fizyczny */
    uint32_t length; /* dlugosc w bajtach */
    uint32_t padding;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuMemEntry) == 16, "VirtioGpuMemEntry must be 16 bytes");
#endif

/* Polecenie VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING */
struct VirtioGpuResourceAttachBacking {
    VirtioGpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuResourceAttachBacking) == 32, "VirtioGpuResourceAttachBacking must be 32 bytes");
#endif

/* Polecenie VIRTIO_GPU_CMD_SET_SCANOUT */
struct VirtioGpuSetScanout {
    VirtioGpuCtrlHdr hdr;
    VirtioGpuRect r;
    uint32_t scanout_id;
    uint32_t resource_id;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuSetScanout) == 48, "VirtioGpuSetScanout must be 48 bytes");
#endif

/* Polecenie VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D */
struct VirtioGpuTransferToHost2d {
    VirtioGpuCtrlHdr hdr;
    VirtioGpuRect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuTransferToHost2d) == 56, "VirtioGpuTransferToHost2d must be 56 bytes");
#endif

/* Polecenie VIRTIO_GPU_CMD_RESOURCE_FLUSH */
struct VirtioGpuResourceFlush {
    VirtioGpuCtrlHdr hdr;
    VirtioGpuRect r;
    uint32_t resource_id;
    uint32_t padding;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuResourceFlush) == 48, "VirtioGpuResourceFlush must be 48 bytes");
#endif

/* Polecenie VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING */
struct VirtioGpuResourceDetachBacking {
    VirtioGpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t padding;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuResourceDetachBacking) == 32, "VirtioGpuResourceDetachBacking must be 32 bytes");
#endif

/* Polecenie VIRTIO_GPU_CMD_RESOURCE_UNREF */
struct VirtioGpuResourceUnref {
    VirtioGpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t padding;
};
#ifdef __cplusplus
static_assert(sizeof(VirtioGpuResourceUnref) == 32, "VirtioGpuResourceUnref must be 32 bytes");
#endif

#pragma pack(pop)

/* =========================================================================
 * 5. STAN STEROWNIKA VIRTIO-GPU
 * ========================================================================= */


struct VirtioGpuDriver {
    VirtioPciDevice* dev;
    VirtQueue*       controlq;
    VirtQueue*       cursorq;

    /* Przerwania MSI-X */
    bool     use_msix;
    int      irq_vector;
    uint32_t irq_count;
    volatile uint32_t command_done;

    /* Bufor DMA dla polecen/odpowiedzi */
    void*    cmd_vaddr;
    uint64_t cmd_paddr;
    size_t   cmd_pages;

    /* Informacje o ekranie */
    uint32_t display_width;
    uint32_t display_height;
    uint32_t scanout_id;
    bool     scanout_enabled;

    /* Zasob 2D i bufor ramki */
    uint32_t resource_id;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_stride;
    uint32_t fb_format;
    void*    fb_vaddr;
    size_t   fb_size;
    size_t   fb_pages;

    /* Backing memory scatter-gather */
    VirtioGpuMemEntry* mem_entries;
    uint32_t           nr_entries;
    bool               is_contiguous_dma;
    DmaBuffer          fb_dma;
    void**             allocated_frames;

    /* Stary zasob oczekujacy na zwolnienie lub wycofanie (atomic modesetting) */
    uint32_t old_resource_id;
    void*    old_fb_vaddr;
    void**   old_allocated_frames;
    size_t   old_fb_pages;
    VirtioGpuMemEntry* old_mem_entries;
    uint32_t old_nr_entries;
    uint32_t old_display_width;
    uint32_t old_display_height;

    /* Flagi stanu */
    bool initialized;
    bool has_error;
    bool resource_created;
    bool backing_attached;
    bool scanout_set;

    /* Blokada spinlock */
    uint32_t lock;

    /* Liczniki diagnostyczne */
    uint64_t cmd_count;
    uint64_t transfer_count;
    uint64_t flush_count;
    uint64_t error_count;
};

/* =========================================================================
 * 6. PUBLICZNY INTERFEJS STEROWNIKA VIRTIO-GPU
 * ========================================================================= */

/* =========================================================================
 * BURSZTYNDIAGREBOOT - diagnostyka sciezki restartu / shutdown.
 * Domyslnie 0 (wylaczone w produkcji). Wlacz przez -DBURSZTYNDIAGREBOOT=1.
 * ========================================================================= */
#ifndef BURSZTYNDIAGREBOOT
#define BURSZTYNDIAGREBOOT 0
#endif

bool virtio_gpu_init(VirtioPciDevice* dev);
bool virtio_gpu_reset(VirtioGpuDriver* drv);
void virtio_gpu_reset_all(void);

uint32_t virtio_gpu_probe_and_init(void);
VirtioGpuDriver* virtio_gpu_get_driver(uint32_t index);
uint32_t virtio_gpu_get_driver_count(void);

bool virtio_gpu_cmd_get_display_info(VirtioGpuDriver* drv, VirtioGpuRespDisplayInfo* out_info);
bool virtio_gpu_cmd_resource_create_2d(VirtioGpuDriver* drv, uint32_t resource_id, uint32_t format, uint32_t width, uint32_t height);
bool virtio_gpu_cmd_resource_attach_backing(VirtioGpuDriver* drv, uint32_t resource_id, const VirtioGpuMemEntry* entries, uint32_t nr_entries);
bool virtio_gpu_cmd_set_scanout(VirtioGpuDriver* drv, uint32_t scanout_id, uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
bool virtio_gpu_cmd_transfer_to_host_2d(VirtioGpuDriver* drv, uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
bool virtio_gpu_cmd_resource_flush(VirtioGpuDriver* drv, uint32_t resource_id, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
bool virtio_gpu_cmd_resource_detach_backing(VirtioGpuDriver* drv, uint32_t resource_id);
bool virtio_gpu_cmd_resource_unref(VirtioGpuDriver* drv, uint32_t resource_id);

bool virtio_gpu_transfer_and_flush(VirtioGpuDriver* drv, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
bool virtio_gpu_setup_framebuffer(VirtioGpuDriver* drv, uint32_t width, uint32_t height);
void virtio_gpu_free_stale_resource(VirtioGpuDriver* drv);
bool virtio_gpu_rollback_modeset(VirtioGpuDriver* drv);
bool virtio_gpu_test_runtime_modeset(VirtioGpuDriver* drv);
bool virtio_gpu_render_test_pattern(VirtioGpuDriver* drv);
bool virtio_gpu_test_teardown_reinit(VirtioGpuDriver* drv);
bool virtio_gpu_stress_dirty_rectangles(VirtioGpuDriver* drv);

/*
 * Funkcje przygotowania GPU do restartu/shutdown systemu.
 *
 * virtio_gpu_quiesce_for_reboot():
 *   Czeka (z ograniczonym timeoutem) na zakonczenie ewentualnie trwajacego
 *   polecenia controlq. NIE wykonuje pelnego resetu urzadzenia VirtIO
 *   (nie zapisuje device_status = 0), aby uniknac wczesniej zaobserwowanego
 *   deadlocka QEMU w virtiogpureset() podczas resetu sprzetowego.
 *   Wywolywac przed psf_flush_cache() i przed wylaczeniem przerwan.
 *
 * virtio_gpu_log_reboot_state():
 *   Loguje na port szeregowy aktualny stan sterownika GPU (resource_id,
 *   old_resource_id, kolejka avail/used, flagi). Wywolywac pod BURSZTYNDIAGREBOOT.
 */
void virtio_gpu_quiesce_for_reboot(void);
void virtio_gpu_log_reboot_state(void);
void virtio_gpu_stop_submissions(void);
bool virtio_gpu_is_quiescing(void);

#ifdef __cplusplus
}
#endif
