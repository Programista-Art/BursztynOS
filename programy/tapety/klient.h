#pragma once
#include "../../bursztyn_gui.h"
#include "../../tapety_format.h"

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
