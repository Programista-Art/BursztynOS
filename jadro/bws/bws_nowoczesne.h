#pragma once
#include "display_geometry.h"

#include <stdint.h>
#include <stddef.h>
#include "bws_ota.h"

#define BWS_NOWOCZESNE_ABI_WERSJA 1U

// Internal notification hint, stripped before publishing an event. This is
// never an accepted watcher mask or an additional userspace event field.
constexpr uint32_t BWS_POWIADOM_PODDRZEWO=1U<<31;

struct GuiMonitorInfo {
    uint32_t wersja;
    uint32_t monitor_id;
    int32_t x;
    int32_t y;
    int32_t szer;
    int32_t wys;
    int32_t work_x;
    int32_t work_y;
    int32_t work_szer;
    int32_t work_wys;
    uint32_t scale_x1000;
    uint32_t flagi;
};

enum BwsShmFlagi : uint32_t {
    BWS_SHM_READ = 1U << 0,
    BWS_SHM_WRITE = 1U << 1
};

#define BWS_IPC_MAX_DANYCH 512U

enum BwsIpcFlagi : uint32_t {
    BWS_IPC_ZAWIERA_SHM = 1U << 0
};

struct BwsIpcWyslij {
    uint32_t wersja;
    uint32_t flagi;
    uint64_t shm_handle;
    uint64_t dane;
    uint32_t rozmiar;
    uint32_t zarezerwowane;
};

struct BwsIpcOdbierz {
    uint32_t wersja;
    uint32_t flagi;
    uint64_t nadawca_pid;
    uint64_t shm_handle;
    uint32_t rozmiar;
    uint32_t zarezerwowane;
    uint8_t dane[BWS_IPC_MAX_DANYCH];
};

enum BwsFileWatchOperacja : uint32_t {
    BWS_FILE_WATCH_DODAJ = 1,
    BWS_FILE_WATCH_USUN = 2
};

enum BwsFileWatchMaska : uint32_t {
    BWS_FILE_CREATE = 1U << 0,
    BWS_FILE_DELETE = 1U << 1,
    BWS_FILE_MODIFY = 1U << 2,
    BWS_FILE_RENAME = 1U << 3,
    BWS_FILE_MOVE = 1U << 4,
    BWS_FILE_WATCH_WSZYSTKO = (1U << 5) - 1U
};

#define BWS_CLIPBOARD_MIME_MAX 64U
#define BWS_CLIPBOARD_FORMATOW_MAX 8U
#define BWS_CLIPBOARD_FORMAT_MAX (1024U * 1024U)

enum GuiCursorShape : uint32_t {
    GUI_CURSOR_ARROW = 0,
    GUI_CURSOR_HAND = 1,
    GUI_CURSOR_TEXT = 2,
    GUI_CURSOR_RESIZE_NS = 3,
    GUI_CURSOR_RESIZE_EW = 4,
    GUI_CURSOR_RESIZE_NWSE = 5,
    GUI_CURSOR_RESIZE_NESW = 6,
    GUI_CURSOR_WAIT = 7,
    GUI_CURSOR_CROSSHAIR = 8,
    GUI_CURSOR_CUSTOM = 0xFFFFFFFFU
};

#define GUI_CURSOR_CUSTOM_MAX_W 64U
#define GUI_CURSOR_CUSTOM_MAX_H 64U

struct GuiCustomCursorInfo {
    uint32_t wersja;
    uint32_t szer;
    uint32_t wys;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    uint32_t format;
    uint64_t piksele;
};

enum GuiCustomCursorFormat : uint32_t {
    GUI_CURSOR_BGRA8888 = 1,
    GUI_CURSOR_RGBA8888 = 2
};

enum BwsSocketTyp : uint32_t {
    BWS_SOCKET_TCP_IPV4 = 1
};

/* =========================================================================
 * BWS98/BWS99 - skalowany blit i skala warstwy
 * ========================================================================= */

/*
 * Opis jednego skalowanego blitu z bufora Ring 3 do warstwy procesu.
 *
 * Bufor zrodla jest ZAWSZE ciagly (stride == src_w * 4). Kontrakt jest
 * celowo waski: "przeskaluj caly obraz src_w x src_h do prostokata
 * dst_w x dst_h". Przycinanie do warstwy robi jadro. Dzieki temu nie ma
 * drugiej, ukrytej definicji wspolrzednych.
 *
 * dst_x/dst_y to wspolrzedne ekranu, tak jak w BWS57.
 */
struct BwsBlitSkalowany {
    uint32_t wersja;        /* BWS_NOWOCZESNE_ABI_WERSJA */
    uint32_t flagi;         /* BWS_BLIT_* */
    uint32_t filtr;         /* SkalowanieFiltr */
    uint32_t tryb;          /* SkalowanieTryb (tylko z ZACHOWAJ_ASPECT) */
    int32_t dst_x;
    int32_t dst_y;
    int32_t dst_w;
    int32_t dst_h;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t zarezerwowane;
    uint32_t zarezerwowane2;
    uint64_t piksele;
};

enum BwsBlitFlagi : uint32_t {
    /* Domyslnie: piksel pusty (0x00000000) nie jest zapisywany. */
    BWS_BLIT_PRZEZROCZYSTE_ZERO = 1U << 0,
    /* Zamiast pomijac puste piksele, nadpisz caly prostokat (BWS57 tryb 2). */
    BWS_BLIT_ZAMIEN_REGION = 1U << 1,
    /* Zloz wynik metoda source-over zamiast nadpisywania. */
    BWS_BLIT_ALFA_SOURCE_OVER = 1U << 2,
    /* Uzyj trybu dopasowania (fit/cover) zamiast prostego rozciagniecia. */
    BWS_BLIT_ZACHOWAJ_ASPECT = 1U << 3
};

#ifdef __cplusplus
static_assert(sizeof(BwsBlitSkalowany) == 56, "ABI BwsBlitSkalowany");
#endif


enum BwsIoStatus : uint32_t {
    BWS_IO_OK = 0,
    BWS_IO_EOF = 1,
    BWS_IO_WOULD_BLOCK = 2,
    BWS_IO_BLAD = 3
};

struct BwsIoResult {
    uint32_t wersja;
    uint32_t status;
    uint32_t bajty;
    uint32_t zarezerwowane;
};

#ifdef __cplusplus
static_assert(sizeof(GuiMonitorInfo) == 48, "ABI GuiMonitorInfo");
static_assert(sizeof(BwsIpcWyslij) == 32, "ABI BwsIpcWyslij");
static_assert(sizeof(BwsIpcOdbierz) == 544, "ABI BwsIpcOdbierz");
static_assert(sizeof(GuiCustomCursorInfo) == 32, "ABI GuiCustomCursorInfo");
static_assert(sizeof(BwsIoResult) == 16, "ABI BwsIoResult");
#endif
