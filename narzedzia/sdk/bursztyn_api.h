/*
 * =====================================================================
 *  Bursztyn OS - Software Development Kit (SDK)
 *  Oficjalne API Systemowe (Ring 3) - Wersja 2.0 (Zdarzenia & Okna)
 * =====================================================================
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// =====================================================================
// STRUKTURY DANYCH (Muszą pokrywać się z definicjami w Jądrze!)
// =====================================================================

// UWAGA: Dopasuj zawartość tych struktur dokładnie do tego, 
// co masz w bws_zdarzenia.h i skladacz_obrazu.h w swoim Jądrze.
struct bws_zdarzenie {
    uint32_t typ;          // np. 1 - Klawiatura, 2 - Mysz, 3 - Okno
    uint32_t parametr1;    // np. kod klawisza lub przycisk myszy
    int32_t mysz_x;
    int32_t mysz_y;
};

struct GuiOknoInfo {
    int pid;
    int x;
    int y;
    int szerokosc;
    int wysokosc;
    int z_order;
    bool zminimalizowane;
    char tytul[64];
};

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
static inline bool b_exec(const char* sciezka_programu) { return bws_wywolaj(10, (uint64_t)sciezka_programu) != 0; }

__attribute__((noreturn)) static inline void b_exit() {
    bws_wywolaj(32);
    while (true) asm volatile("pause");
}

// =====================================================================
// 2. BURSZTYNOWY SYSTEM PLIKÓW (VFS)
// =====================================================================
static inline bool b_file_create(const char* sciezka) { return bws_wywolaj(2, (uint64_t)sciezka) != 0; }
static inline bool b_file_write(const char* sciezka, const char* dane, uint32_t dlugosc) { return bws_wywolaj(3, (uint64_t)sciezka, (uint64_t)dane, dlugosc) != 0; }
static inline bool b_file_read(const char* sciezka, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(5, (uint64_t)sciezka, (uint64_t)bufor, max_dlugosc) != 0; }
static inline bool b_dir_list(const char* sciezka, char* bufor, uint32_t max_dlugosc) { return bws_wywolaj(6, (uint64_t)sciezka, (uint64_t)bufor, max_dlugosc) != 0; }
static inline bool b_file_delete(const char* sciezka) { return bws_wywolaj(7, (uint64_t)sciezka) != 0; }
static inline bool b_file_rename(const char* stara_nazwa, const char* nowa_nazwa) { return bws_wywolaj(8, (uint64_t)stara_nazwa, (uint64_t)nowa_nazwa) != 0; }

// =====================================================================
// 3. SIEĆ (TCP/IP, DNS, HTTP)
// =====================================================================
static inline void b_net_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4) { bws_wywolaj(11, ip1, ip2, ip3, ip4); }
static inline bool b_net_dns(const char* domena, uint8_t* wyjsciowy_ip) { return bws_wywolaj(12, (uint64_t)domena, (uint64_t)wyjsciowy_ip) != 0; }
static inline bool b_net_http_download(uint8_t* docelowy_ip, const char* domena, const char* sciezka_http, const char* plik_dysk) {
    return bws_wywolaj(13, (uint64_t)docelowy_ip, (uint64_t)domena, (uint64_t)sciezka_http, (uint64_t)plik_dysk) != 0;
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
static inline void b_gui_get_resolution(int* szerokosc, int* wysokosc) { bws_wywolaj(23, (uint64_t)szerokosc, (uint64_t)wysokosc); }
static inline void b_gui_capture_mouse(bool stan) { bws_wywolaj(22, stan ? 1 : 0); }
static inline void b_gui_refresh() { bws_wywolaj(17); }

static inline int b_gui_layer_create(int x, int y, int szer, int wys, int z_order) {
    uint64_t pozycja = ((uint64_t)(uint32_t)x << 32) | (uint32_t)y;
    uint64_t rozmiar = ((uint64_t)(uint32_t)szer << 32) | (uint32_t)wys;
    uint64_t wynik = bws_wywolaj(33, pozycja, rozmiar, (uint32_t)z_order);
    return wynik ? (int)wynik - 1 : -1;
}

static inline void b_gui_layer_move(int nowy_x, int nowy_y) {
    bws_wywolaj(34, (uint64_t)(int64_t)nowy_x, (uint64_t)(int64_t)nowy_y);
}

// =====================================================================
// 6. ZAAWANSOWANE ZARZĄDZANIE ZDARZENIAMI I OKNAMI (Nowość!)
// =====================================================================

// Odpytuje, czy jest zdarzenie, bez blokowania programu
static inline bool b_event_poll(struct bws_zdarzenie* zdarzenie) {
    return bws_wywolaj(37, (uint64_t)zdarzenie) != 0;
}

// Blokuje/usypia program do momentu, aż system wyśle do niego zdarzenie
static inline bool b_event_wait(struct bws_zdarzenie* zdarzenie) {
    return bws_wywolaj(38, (uint64_t)zdarzenie) != 0;
}

static inline bool b_gui_is_shell_closed() {
    return bws_wywolaj(36) != 0;
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
    return bws_wywolaj(41) != 0;
}

// Zwraca informacje o wszystkich oknach na pulpicie (do rysowania Paska Zadań!)
static inline uint32_t b_gui_get_windows_snapshot(struct GuiOknoInfo* bufor, uint32_t max_okien) {
    return (uint32_t)bws_wywolaj(42, (uint64_t)bufor, max_okien);
}

// Przywraca i przenosi okno na wierzch (Focus)
static inline bool b_gui_activate_window(int id_okna) {
    return bws_wywolaj(43, id_okna) != 0;
}

// =====================================================================
// 7. MULTIMEDIA
// =====================================================================
static inline void b_audio_play_tone(uint32_t czestotliwosc_hz, uint32_t czas_ms) {
    bws_wywolaj(27, czestotliwosc_hz, czas_ms);
}
