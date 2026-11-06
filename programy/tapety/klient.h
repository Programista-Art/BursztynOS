#pragma once
#include "bursztyn_gui.h"
#include "tapety_format.h"

inline bool tapety_zapytaj(uint64_t endpoint, TapetyZadanie req, TapetyOdpowiedz* out) {
    if (!endpoint || !out) return false;
    req.wersja = 1; req.odpowiedz = endpoint;
    BwsIpcWyslij send{};
    send.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    send.dane = reinterpret_cast<uint64_t>(&req); send.rozmiar = sizeof(req);
    if (!ipc_send(TAPETY_IPC, &send)) return false;
    BwsIpcOdbierz recv{}; recv.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    if (!ipc_receive(endpoint, &recv) || recv.nadawca_pid != 0 || recv.flagi ||
        recv.rozmiar != sizeof(*out)) return false;
    auto* dst = reinterpret_cast<uint8_t*>(out);
    for (size_t i = 0; i < sizeof(*out); ++i) dst[i] = recv.dane[i];
    return out->wersja == 1;
}

inline bool tapety_zastosuj_obraz(uint64_t endpoint, const char* path, uint32_t mode,
                                  const uint8_t* pixels, uint32_t width, uint32_t height,
                                  uint32_t stride, size_t bytes, TapetyOdpowiedz* out) {
    if (!endpoint || !out || !pixels || !tapety_sciezka_powierzchni(path)) return false;
    TapetyObrazZadanie req{};
    req.podstawowe.wersja = 2;
    req.podstawowe.operacja = TAPETY_ZASTOSUJ_OBRAZ;
    req.podstawowe.tryb = mode;
    req.podstawowe.odpowiedz = endpoint;
    tapety_kopiuj(req.podstawowe.sciezka, path, sizeof(req.podstawowe.sciezka));
    req.piksele = reinterpret_cast<uint64_t>(pixels);
    req.bajty = bytes;
    req.szerokosc = width;
    req.wysokosc = height;
    req.stride = stride;
    req.format_pikseli = 1;
    BwsIpcWyslij send{};
    send.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    send.dane = reinterpret_cast<uint64_t>(&req);
    send.rozmiar = sizeof(req);
    if (!ipc_send(TAPETY_IPC, &send)) return false;
    BwsIpcOdbierz recv{};
    recv.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    if (!ipc_receive(endpoint, &recv) || recv.nadawca_pid != 0 || recv.flagi ||
        recv.rozmiar != sizeof(*out)) return false;
    auto* dst = reinterpret_cast<uint8_t*>(out);
    for (size_t i = 0; i < sizeof(*out); ++i) dst[i] = recv.dane[i];
    return out->wersja == 1;
}
