/*
 * =====================================================================
 *  Bursztyn OS - Software Development Kit (SDK)
 *  Oficjalne API Systemowe (Ring 3) - Wersja 2.0 (Zdarzenia & Okna)
 * =====================================================================
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "bws_api.h"
#include "bws_zdarzenia.h"
#include "bws_nowoczesne.h"
#include "skladacz_obrazu.h"

// =====================================================================
// KANONICZNY UKŁAD PAMIĘCI APLIKACJI RING 3
// =====================================================================
inline constexpr uint64_t BUR_RING3_BASE            = 0x0000000001000000ULL; // 16 MiB
inline constexpr uint64_t BUR_RING3_DEFAULT_TEXT_VA = 0x0000000001001000ULL;
inline constexpr uint64_t BUR_RING3_DEFAULT_DATA_VA = 0x0000000001009000ULL;
inline constexpr uint64_t BUR_RING3_HEAP_BASE       = 0x0000000800000000ULL; // 32 GiB
inline constexpr uint64_t BUR_RING3_SHM_BASE        = 0x0000000900000000ULL; // 36 GiB
inline constexpr uint64_t BUR_RING3_STACK_TOP       = 0x00007FFFF0000000ULL;

// Struktury ABI sa importowane z kanonicznych naglowkow kernela/SDK.

// =====================================================================
// BWS - RDZENNY MECHANIZM WYWOŁAŃ SYSTEMOWYCH
// =====================================================================
static inline uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0) {
    register uint64_t r8 asm("r8") = nr_funkcji;
    register uint64_t r9 asm("r9") = arg1;
    register uint64_t r10 asm("r10") = arg2;
    register uint64_t r12 asm("r12") = arg3;
    register uint64_t r13 asm("r13") = arg4;
    register uint64_t rax asm("rax");

    asm volatile (
        "syscall"
        : "=a" (rax)
        : "r" (r8), "r" (r9), "r" (r10), "r" (r12), "r" (r13)
        : "rcx", "r11", "memory"
    );
    return rax;
}

// =====================================================================
// 1. STANDARDOWE WEJŚCIE / WYJŚCIE I ZARZĄDZANIE PROCESEM
// =====================================================================
static inline void b_print(const char* tekst) { bws_wywolaj(1, (uint64_t)tekst); }
static inline char b_getchar() { return (char)bws_wywolaj(4); }
static inline void b_get_time(char* bufor_wyjsciowy) { bws_wywolaj(9, (uint64_t)bufor_wyjsciowy); }
static inline bool b_exec(const char* sciezka_programu) { return bws_wywolaj(10, (uint64_t)sciezka_programu) == 1; }

__attribute__((noreturn)) static inline void b_exit() {
    bws_wywolaj(32);
    while (true) asm volatile("pause");
}

// =====================================================================
// 2. BURSZTYNOWY SYSTEM PLIKÓW (VFS)
// =====================================================================
static inline bool b_file_create(const char* sciezka) { return bws_wywolaj(2, (uint64_t)sciezka) == 1; }
static inline bool b_file_write(const char* sciezka, const char* dane, uint32_t dlugosc) { return bws_wywolaj(3, (uint64_t)sciezka, (uint64_t)dane, dlugosc) == 1; }
static inline bool b_file_read(const char* sciezka, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(5, (uint64_t)sciezka, (uint64_t)bufor, max_dlugosc) == 1; }
static inline bool b_dir_list(const char* sciezka, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(6, (uint64_t)sciezka, (uint64_t)bufor, max_dlugosc) == 1; }
static inline bool b_file_delete(const char* sciezka) { return bws_wywolaj(7, (uint64_t)sciezka) == 1; }
static inline bool b_file_rename(const char* stara_nazwa, const char* nowa_nazwa) { return bws_wywolaj(8, (uint64_t)stara_nazwa, (uint64_t)nowa_nazwa) == 1; }

// =====================================================================
// 3. SIEĆ (TCP/IP, DNS, HTTP)
// =====================================================================
static inline void b_net_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4) { bws_wywolaj(11, ip1, ip2, ip3, ip4); }
static inline bool b_net_dns(const char* domena, uint8_t* wyjsciowy_ip) { return bws_wywolaj(12, (uint64_t)domena, (uint64_t)wyjsciowy_ip) == 1; }
static inline bool b_net_http_download(uint8_t* docelowy_ip, const char* domena, const char* sciezka_http, const char* plik_dysk) {
    return bws_wywolaj(13, (uint64_t)docelowy_ip, (uint64_t)domena, (uint64_t)sciezka_http, (uint64_t)plik_dysk) == 1;
}

// =====================================================================
// 4. PAMIĘĆ
// =====================================================================
static inline void* b_sbrk(uint64_t rozmiar_bajty) {
    uint64_t stary_limit = bws_wywolaj(35, rozmiar_bajty);
    return stary_limit == 0 ? nullptr : (void*)stary_limit;
}

// =====================================================================
// 5. GUI I GRAFIKA (Bursztyn Compositor)
// =====================================================================
static inline void b_gui_draw_window(int x, int y, int w, int h, const char* tytul) {
    uint64_t pos = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t size = ((uint64_t)(uint32_t)w << 32) | (uint32_t)h;
    bws_wywolaj(14, pos, size, (uint64_t)tytul);
}

static inline void b_gui_draw_text(int x, int y, uint32_t kolor, int skala, const char* tekst) {
    uint64_t kolor_skala = ((uint64_t)(uint32_t)skala << 32) | (uint64_t)kolor;
    bws_wywolaj(20, (uint64_t)(int64_t)x, (uint64_t)(int64_t)y, kolor_skala, (uint64_t)tekst);
}

static inline void b_gui_clear_area(int x, int y, int w, int h) {
    bws_wywolaj(16, (uint64_t)(int64_t)x, (uint64_t)(int64_t)y, (uint64_t)(int64_t)w, (uint64_t)(int64_t)h);
}

static inline void b_gui_draw_rect(int x, int y, int w, int h, uint32_t kolor) {
    uint64_t pos = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t size = ((uint64_t)(uint32_t)w << 32) | (uint32_t)h;
    bws_wywolaj(21, pos, size, kolor);
}

static inline void b_gui_get_resolution(int* szerokosc, int* wysokosc) { bws_wywolaj(23, (uint64_t)szerokosc, (uint64_t)wysokosc); }
static inline int b_gui_get_char_width(uint32_t unicode) { return (int)bws_wywolaj(24, unicode); }
static inline void b_gui_capture_mouse(bool stan) { bws_wywolaj(22, stan ? 1 : 0); }
static inline void b_gui_refresh() { bws_wywolaj(17); }
static inline void b_gui_refresh_desktop() { bws_wywolaj(19); }

static inline bool b_gui_blit(int x, int y, int szer, int wys, const uint32_t* piksele) {
    uint64_t pos = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t size = ((uint64_t)(uint32_t)szer << 32) | (uint32_t)wys;
    return bws_wywolaj(57, pos, size, (uint64_t)piksele) == 1;
}

static inline bool b_gui_set_window_title(const char* tytul) {
    return bws_wywolaj(58, (uint64_t)tytul) == 1;
}

static inline int b_gui_layer_create(int x, int y, int szer, int wys, int z_order) {
    uint64_t pozycja = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t rozmiar = ((uint64_t)(uint32_t)szer << 32) | (uint32_t)wys;
    uint64_t wynik = bws_wywolaj(33, pozycja, rozmiar, (uint32_t)z_order);
    return wynik ? (int)wynik - 1 : -1;
}

static inline void b_gui_layer_move(int nowy_x, int nowy_y) {
    bws_wywolaj(34, (uint64_t)(int64_t)nowy_x, (uint64_t)(int64_t)nowy_y);
}

static inline bool b_gui_set_geometry(int x, int y, int szer, int wys) {
    uint64_t pos = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t size = ((uint64_t)(uint32_t)szer << 32) | (uint32_t)wys;
    return bws_wywolaj(BWS_GUI_GEOMETRIA, pos, size) == 1;
}

static inline uint32_t b_gui_get_monitor_count() {
    uint64_t n = bws_wywolaj(BWS_GUI_MONITOR_INFO, 0, 0);
    return (n == UINT64_MAX || n > UINT32_MAX) ? 0U : (uint32_t)n;
}

static inline bool b_gui_get_monitor_info(uint32_t indeks, struct GuiMonitorInfo* info) {
    return info && bws_wywolaj(BWS_GUI_MONITOR_INFO, indeks, (uint64_t)info) == 1;
}

static inline bool b_gui_set_work_area(int x, int y, int szer, int wys) {
    uint64_t pos = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t size = ((uint64_t)(uint32_t)szer << 32) | (uint32_t)wys;
    return bws_wywolaj(BWS_GUI_USTAW_WORK_AREA, pos, size) == 1;
}

// =====================================================================
// 6. ZAAWANSOWANE ZARZĄDZANIE ZDARZENIAMI I OKNAMI
// =====================================================================

// Odpytuje, czy jest zdarzenie, bez blokowania programu
static inline bool b_event_poll(struct bws_zdarzenie* zdarzenie) {
    return bws_wywolaj(37, (uint64_t)zdarzenie) == 1;
}

// Blokuje/usypia program do momentu, aż system wyśle do niego zdarzenie
static inline bool b_event_wait(struct bws_zdarzenie* zdarzenie) {
    return bws_wywolaj(38, (uint64_t)zdarzenie) == 1;
}

static inline bool b_gui_is_shell_closed() {
    return bws_wywolaj(36) == 1;
}

// Ustawia przechwytywanie wszystkich zdarzeń wejścia do aktualnego okna
static inline void b_gui_set_capture(bool stan) {
    bws_wywolaj(39, stan ? 1 : 0);
}

// Rysuje "nakładkę" (np. otwarte menu kontekstowe) na wierzchu okna
static inline void b_gui_set_overlay(bool aktywne, int x, int y, int szer, int wys) {
    uint64_t pos = ((uint64_t)x << 32) | (uint32_t)y;
    uint64_t size = ((uint64_t)szer << 32) | (uint32_t)wys;
    bws_wywolaj(40, aktywne ? 1 : 0, pos, size);
}

// Zrzuca okno z ekranu, zachowując je w pamięci
static inline bool b_gui_minimize_layer() {
    return bws_wywolaj(41) == 1;
}

// Zwraca informacje o wszystkich oknach na pulpicie (do rysowania Paska Zadań!)
static inline uint32_t b_gui_get_windows_snapshot(struct GuiOknoInfo* bufor, uint32_t max_okien) {
    return (uint32_t)bws_wywolaj(42, (uint64_t)bufor, max_okien);
}

// Przywraca i przenosi okno na wierzch (Focus)
static inline bool b_gui_activate_window(int id_okna) {
    return bws_wywolaj(43, id_okna) == 1;
}

static inline bool b_gui_set_cursor(uint32_t typ) {
    return bws_wywolaj(BWS_GUI_CURSOR_SHAPE, typ) == 1;
}

static inline bool b_gui_set_custom_cursor(const struct GuiCustomCursorInfo* info) {
    return info && bws_wywolaj(BWS_GUI_CURSOR_CUSTOM, (uint64_t)info) == 1;
}

// =====================================================================
// 7. MULTIMEDIA I CZAS MONOTONICZNY
// =====================================================================
static inline void b_audio_play_tone(uint32_t czestotliwosc_hz, uint32_t czas_ms) {
    bws_wywolaj(27, czestotliwosc_hz, czas_ms);
}

static inline uint64_t b_time_monotonic_ns() {
    return bws_wywolaj(60);
}

// =====================================================================
// 8. SHARED MEMORY (Pamięć współdzielona)
// =====================================================================
static inline uint64_t b_shm_create(uint64_t rozmiar, uint32_t flagi) {
    uint64_t h = bws_wywolaj(BWS_SHM_CREATE, rozmiar, flagi);
    return h == UINT64_MAX ? 0 : h;
}

static inline void* b_shm_map(uint64_t handle, uint32_t flagi) {
    if (!handle) return nullptr;
    uint64_t a = bws_wywolaj(BWS_SHM_MAP, handle, flagi);
    return (a == 0 || a == UINT64_MAX) ? nullptr : (void*)a;
}

static inline bool b_shm_unmap(void* adres) {
    return adres && bws_wywolaj(BWS_SHM_UNMAP, (uint64_t)adres) == 1;
}

static inline bool b_shm_close(uint64_t handle) {
    return handle && bws_wywolaj(BWS_SHM_CLOSE, handle) == 1;
}

// =====================================================================
// 9. IPC (Message channels / Porty)
// =====================================================================
static inline uint64_t b_ipc_create() {
    uint64_t h = bws_wywolaj(BWS_IPC_CREATE);
    return h == UINT64_MAX ? 0 : h;
}

static inline bool b_ipc_send(uint64_t endpoint, const struct BwsIpcWyslij* wiadomosc) {
    return endpoint && wiadomosc && bws_wywolaj(BWS_IPC_SEND, endpoint, (uint64_t)wiadomosc) == 1;
}

static inline bool b_ipc_receive(uint64_t endpoint, struct BwsIpcOdbierz* wiadomosc) {
    return endpoint && wiadomosc && bws_wywolaj(BWS_IPC_RECEIVE, endpoint, (uint64_t)wiadomosc) == 1;
}

static inline bool b_ipc_close(uint64_t endpoint) {
    return endpoint && bws_wywolaj(BWS_IPC_CLOSE, endpoint) == 1;
}

// =====================================================================
// 10. FILE WATCHING (Obserwatorzy plików)
// =====================================================================
static inline uint64_t b_file_watch(const char* sciezka, uint32_t maska) {
    if (!sciezka || !maska) return 0;
    uint64_t h = bws_wywolaj(BWS_FILE_WATCH, BWS_FILE_WATCH_DODAJ, (uint64_t)sciezka, maska);
    return h == UINT64_MAX ? 0 : h;
}

static inline bool b_file_unwatch(uint64_t handle) {
    return handle && bws_wywolaj(BWS_FILE_WATCH, BWS_FILE_WATCH_USUN, handle) == 1;
}

// =====================================================================
// 11. CLIPBOARD MIME (Schowek systemowy)
// =====================================================================
static inline bool b_clipboard_format(uint32_t indeks, char* mime, uint32_t pojemnosc) {
    return mime && pojemnosc && bws_wywolaj(BWS_CLIPBOARD_ENUM, indeks, (uint64_t)mime, pojemnosc) == 1;
}

static inline bool b_clipboard_size(const char* mime, uint64_t* rozmiar) {
    return mime && rozmiar && bws_wywolaj(BWS_CLIPBOARD_SIZE, (uint64_t)mime, (uint64_t)rozmiar) == 1;
}

static inline bool b_clipboard_read(const char* mime, void* dane, uint64_t pojemnosc, uint64_t* odczytano) {
    return mime && odczytano && (pojemnosc == 0 || dane) &&
        bws_wywolaj(BWS_CLIPBOARD_READ, (uint64_t)mime, (uint64_t)dane, pojemnosc, (uint64_t)odczytano) == 1;
}

static inline bool b_clipboard_set(const char* mime, const void* dane, uint64_t rozmiar) {
    return mime && (rozmiar == 0 || dane) &&
        bws_wywolaj(BWS_CLIPBOARD_SET, (uint64_t)mime, (uint64_t)dane, rozmiar) == 1;
}

// =====================================================================
// 12. GNIAZDA SIECIOWE (Sockets TCP/IPv4)
// =====================================================================
static inline uint64_t b_socket_create(uint32_t typ = BWS_SOCKET_TCP_IPV4) {
    uint64_t h = bws_wywolaj(BWS_SOCKET_CREATE, typ);
    return h == UINT64_MAX ? 0 : h;
}

static inline bool b_socket_connect(uint64_t handle, const uint8_t ipv4[4], uint16_t port) {
    return handle && ipv4 && port && bws_wywolaj(BWS_SOCKET_CONNECT, handle, (uint64_t)ipv4, port) == 1;
}

static inline bool b_socket_send(uint64_t handle, const void* dane, uint32_t rozmiar, struct BwsIoResult* wynik) {
    return handle && wynik && (rozmiar == 0 || dane) &&
        bws_wywolaj(BWS_SOCKET_SEND, handle, (uint64_t)dane, rozmiar, (uint64_t)wynik) == 1;
}

static inline bool b_socket_recv(uint64_t handle, void* dane, uint32_t pojemnosc, struct BwsIoResult* wynik) {
    return handle && dane && pojemnosc && wynik &&
        bws_wywolaj(BWS_SOCKET_RECV, handle, (uint64_t)dane, pojemnosc, (uint64_t)wynik) == 1;
}

static inline bool b_socket_close(uint64_t handle) {
    return handle && bws_wywolaj(BWS_SOCKET_CLOSE, handle) == 1;
}
