#pragma once
#include "bursztyn_gui.h"
#include "ekran_format.h"
inline bool ekran_zapytaj(uint64_t endpoint,EkranZadanie req,EkranOdpowiedz*out){
    if(!endpoint||!out)return false;
    req.wersja=1;req.odpowiedz=endpoint;
    BwsIpcWyslij s{};s.wersja=1;s.dane=reinterpret_cast<uint64_t>(&req);s.rozmiar=sizeof(req);
    if(!ipc_send(EKRAN_IPC,&s))return false;
    BwsIpcOdbierz r{};r.wersja=1;
    if(!ipc_receive(endpoint,&r)||r.nadawca_pid||r.flagi||r.rozmiar!=sizeof(*out))return false;
    for(size_t i=0;i<sizeof(*out);++i)reinterpret_cast<uint8_t*>(out)[i]=r.dane[i];
    return out->wersja==1&&out->liczba_trybow<=EKRAN_MAX_TRYBOW;
}
