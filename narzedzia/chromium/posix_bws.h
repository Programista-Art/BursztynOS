// Bursztyn OS - cienka warstwa BWS dla Chromium (Ozone).
//
// Chromium nie moze includowac naglowkow jadra (inne flagi, inne ABI), wiec
// ten naglowek deklaruje WYLACZNIE funkcje Ring 3, ktorych backend uzywa.
// Kazda deklaracja ma odpowiadajacy wpis w jadro/gui/bursztyn_gui.h i
// jadro/bws/bws_api.h; zgodnosc pilnuje narzedzia/chromium/weryfikuj_backend.py.

#ifndef THIRD_PARTY_BURSZTYN_POSIX_BWS_H_
#define THIRD_PARTY_BURSZTYN_POSIX_BWS_H_

#include <stdint.h>

extern "C" {

// BWS - Bursztynowe Wywolania Systemowe. ABI: numer w R8, argumenty
// w R9/R10/R12/R13, wynik w RAX. Wrapper asemblerowy musi byc identyczny
// z bursztyn_libc / bws_wywolaj w Ring 3.
uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3, uint64_t arg4);

// --- Warstwy i prezentacja (BWS 33/34/57/61/98/99) ---------------------
int bws_utworz_warstwe(int x, int y, int szer, int wys, int z_order);
void bws_przesun_warstwe(int nowy_x, int nowy_y);
int bws_ustaw_geometrie_okna(int x, int y, int szer, int wys, int stan);
bool bws_blit_skalowany(int dst_x, int dst_y, int dst_w, int dst_h,
                        const uint32_t* piksele, uint32_t src_w, uint32_t src_h,
                        uint32_t filtr, uint32_t flagi, uint32_t tryb);
bool bws_ustaw_skale_warstwy(int src_w, int src_h, uint32_t tryb);
void bws_odswiez();
void bws_odswiez_pulpit();
bool bws_ustaw_tytul_okna(const char* tytul);
bool bws_minimalizuj_okno();
bool bws_aktywuj_okno(uint64_t window_id);

// --- Wejscie i zdarzenia (BWS 18/37/38/39/77) -------------------------
struct BwsZdarzenie;
bool bws_pobierz_zdarzenie(BwsZdarzenie* zdarzenie);
bool bws_czekaj_na_zdarzenie_z_timeoutem(BwsZdarzenie* zdarzenie, uint64_t timeout_ticki);
void bws_ustaw_capture_myszy(bool stan);
bool bws_ustaw_kursor(uint32_t typ);

// --- Pamiec (BWS 64-67) ----------------------------------------------
uint64_t bws_shm_create(uint64_t rozmiar, uint32_t flagi);
void* bws_shm_map(uint64_t handle, uint32_t flagi);
bool bws_shm_unmap(void* adres);
bool bws_shm_close(uint64_t handle);

// --- Czas (BWS 60) ---------------------------------------------------
uint64_t bws_czas_monotoniczny_ns();

// --- Siec (BWS 79-83) ------------------------------------------------
uint64_t bws_socket_create(uint32_t typ);
bool bws_socket_connect(uint64_t handle, const uint8_t ipv4[4], uint16_t port);
bool bws_socket_send(uint64_t handle, const void* dane, uint32_t rozmiar, uint32_t* status);
bool bws_socket_recv(uint64_t handle, void* dane, uint32_t pojemnosc, uint32_t* status);
bool bws_socket_close(uint64_t handle);

// --- Proces (BWS 10/32/45) -------------------------------------------
bool bws_uruchom_program(const char* sciezka);
void bws_zakoncz_proces();

}  // extern "C"

#endif  // THIRD_PARTY_BURSZTYN_POSIX_BWS_H_
