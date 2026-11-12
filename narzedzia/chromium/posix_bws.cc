// Bursztyn OS - implementacja warstwy BWS dla Chromium.
//
// Zasada: kazda funkcja tutaj to cienki wrapper na bws_wywolaj(). Zero logiki
// biznesowej, zero alokacji, zero cachowania. Dzieki temu backend Ozone nie
// ma wlasnej, drugiej definicji zachowania systemu.

#include "posix_bws.h"

#include <stdint.h>

namespace {

// Numery BWS - musza byc identyczne z jadro/bws/bws_api.h.
// Weryfikuje to narzedzia/chromium/weryfikuj_backend.py.
constexpr uint64_t BWS_WYPISZ = 1;
constexpr uint64_t BWS_CZAS_MONOTONICZNY_NS = 60;
constexpr uint64_t BWS_GUI_GEOMETRIA = 61;
constexpr uint64_t BWS_GUI_MONITOR_INFO = 62;
constexpr uint64_t BWS_SHM_CREATE = 64;
constexpr uint64_t BWS_SHM_MAP = 65;
constexpr uint64_t BWS_SHM_UNMAP = 66;
constexpr uint64_t BWS_SHM_CLOSE = 67;
constexpr uint64_t BWS_GUI_CURSOR_SHAPE = 77;
constexpr uint64_t BWS_SOCKET_CREATE = 79;
constexpr uint64_t BWS_SOCKET_CONNECT = 80;
constexpr uint64_t BWS_SOCKET_SEND = 81;
constexpr uint64_t BWS_SOCKET_RECV = 82;
constexpr uint64_t BWS_SOCKET_CLOSE = 83;
constexpr uint64_t BWS_GUI_BLIT_SKALOWANY = 98;
constexpr uint64_t BWS_GUI_SKALA_WARSTWY = 99;

inline uint64_t spakuj(int a, int b) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
         static_cast<uint32_t>(b);
}

}  // namespace

extern "C" {

int bws_utworz_warstwe(int x, int y, int szer, int wys, int z_order) {
  return static_cast<int>(bws_wywolaj(33, spakuj(x, y), spakuj(szer, wys),
                                      static_cast<uint64_t>(z_order), 0));
}

void bws_przesun_warstwe(int nowy_x, int nowy_y) {
  bws_wywolaj(34, spakuj(nowy_x, nowy_y), 0, 0, 0);
}

int bws_ustaw_geometrie_okna(int x, int y, int szer, int wys, int stan) {
  return static_cast<int>(bws_wywolaj(BWS_GUI_GEOMETRIA, spakuj(x, y),
                                      spakuj(szer, wys),
                                      static_cast<uint64_t>(stan), 0));
}

bool bws_blit_skalowany(int dst_x, int dst_y, int dst_w, int dst_h,
                        const uint32_t* piksele, uint32_t src_w, uint32_t src_h,
                        uint32_t filtr, uint32_t flagi, uint32_t tryb) {
  // Descriptor must match BwsBlitSkalowany (56 B) exactly.
  struct Opis {
    uint32_t wersja;
    uint32_t flagi;
    uint32_t filtr;
    uint32_t tryb;
    int32_t dst_x;
    int32_t dst_y;
    int32_t dst_w;
    int32_t dst_h;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t zarezerwowane;
    uint32_t zarezerwowane2;
    uint64_t piksele;
  } opis{1, flagi, filtr, tryb, dst_x, dst_y, dst_w, dst_h,
         src_w, src_h, 0, 0, reinterpret_cast<uint64_t>(piksele)};
  return bws_wywolaj(BWS_GUI_BLIT_SKALOWANY,
                     reinterpret_cast<uint64_t>(&opis), sizeof(opis), 0, 0) == 1;
}

bool bws_ustaw_skale_warstwy(int src_w, int src_h, uint32_t tryb) {
  return bws_wywolaj(BWS_GUI_SKALA_WARSTWY, spakuj(src_w, src_h), tryb, 0, 0) == 1;
}

void bws_odswiez() { bws_wywolaj(17, 0, 0, 0, 0); }
void bws_odswiez_pulpit() { bws_wywolaj(19, 0, 0, 0, 0); }

bool bws_ustaw_tytul_okna(const char* tytul) {
  return bws_wywolaj(58, reinterpret_cast<uint64_t>(tytul), 0, 0, 0) == 1;
}

bool bws_minimalizuj_okno() { return bws_wywolaj(41, 0, 0, 0, 0) == 1; }

bool bws_aktywuj_okno(uint64_t window_id) {
  return bws_wywolaj(43, window_id, 0, 0, 0) == 1;
}

bool bws_pobierz_zdarzenie(BwsZdarzenie* zdarzenie) {
  return bws_wywolaj(37, reinterpret_cast<uint64_t>(zdarzenie), 0, 0, 0) == 1;
}

bool bws_czekaj_na_zdarzenie_z_timeoutem(BwsZdarzenie* zdarzenie,
                                         uint64_t timeout_ticki) {
  return bws_wywolaj(38, reinterpret_cast<uint64_t>(zdarzenie), timeout_ticki,
                     0, 0) == 1;
}

void bws_ustaw_capture_myszy(bool stan) {
  bws_wywolaj(39, stan ? 1ULL : 0ULL, 0, 0, 0);
}

bool bws_ustaw_kursor(uint32_t typ) {
  return bws_wywolaj(BWS_GUI_CURSOR_SHAPE, typ, 0, 0, 0) == 1;
}

uint64_t bws_shm_create(uint64_t rozmiar, uint32_t flagi) {
  return bws_wywolaj(BWS_SHM_CREATE, rozmiar, flagi, 0, 0);
}

void* bws_shm_map(uint64_t handle, uint32_t flagi) {
  return reinterpret_cast<void*>(bws_wywolaj(BWS_SHM_MAP, handle, flagi, 0, 0));
}

bool bws_shm_unmap(void* adres) {
  return bws_wywolaj(BWS_SHM_UNMAP, reinterpret_cast<uint64_t>(adres), 0, 0, 0) == 1;
}

bool bws_shm_close(uint64_t handle) {
  return bws_wywolaj(BWS_SHM_CLOSE, handle, 0, 0, 0) == 1;
}

uint64_t bws_czas_monotoniczny_ns() {
  return bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0);
}

uint64_t bws_socket_create(uint32_t typ) {
  return bws_wywolaj(BWS_SOCKET_CREATE, typ, 0, 0, 0);
}

bool bws_socket_connect(uint64_t handle, const uint8_t ipv4[4], uint16_t port) {
  const uint64_t ip = static_cast<uint64_t>(ipv4[0]) |
                      (static_cast<uint64_t>(ipv4[1]) << 8) |
                      (static_cast<uint64_t>(ipv4[2]) << 16) |
                      (static_cast<uint64_t>(ipv4[3]) << 24);
  return bws_wywolaj(BWS_SOCKET_CONNECT, handle, ip, port, 0) == 1;
}

bool bws_socket_send(uint64_t handle, const void* dane, uint32_t rozmiar,
                     uint32_t* status) {
  return bws_wywolaj(BWS_SOCKET_SEND, handle, reinterpret_cast<uint64_t>(dane),
                     rozmiar, reinterpret_cast<uint64_t>(status)) == 1;
}

bool bws_socket_recv(uint64_t handle, void* dane, uint32_t pojemnosc,
                     uint32_t* status) {
  return bws_wywolaj(BWS_SOCKET_RECV, handle, reinterpret_cast<uint64_t>(dane),
                     pojemnosc, reinterpret_cast<uint64_t>(status)) == 1;
}

bool bws_socket_close(uint64_t handle) {
  return bws_wywolaj(BWS_SOCKET_CLOSE, handle, 0, 0, 0) == 1;
}

bool bws_uruchom_program(const char* sciezka) {
  return bws_wywolaj(10, reinterpret_cast<uint64_t>(sciezka), 0, 0, 0) == 1;
}

void bws_zakoncz_proces() { bws_wywolaj(32, 0, 0, 0, 0); }

}  // extern "C"
