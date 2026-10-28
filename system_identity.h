#pragma once
#include "wersja_systemu.h"
#define BURSZTYN_SYSTEM_AUTHOR "Dymitr Wygowski"
#define BURSZTYN_SYSTEM_EMAIL "programista.art@gmail.com"
namespace SystemIdentity {
inline constexpr const char* Name="Bursztyn OS";
inline constexpr const char* Version=BURSZTYN_NAZWA_WERSJI;
inline constexpr const char* Author=BURSZTYN_SYSTEM_AUTHOR;
inline constexpr const char* Email=BURSZTYN_SYSTEM_EMAIL;
struct Property {const char* section;const char* label;const char* value;};
// Build capabilities, not a claim that every driver is active on this machine.
inline constexpr Property Properties[]={
 {"Informacje podstawowe","Wersja systemu",Version},
 {"Informacje podstawowe","Autor systemu",Author},
 {"Informacje podstawowe","E-mail",Email},
 {"Informacje podstawowe","Architektura","x86-64"},
 {"Jądro","Typ","Autorskie, monolityczne"},
 {"Jądro","Paging","x86-64, 4 poziomy"},
 {"Procesy","Model","Ring 3, PZB/BZL"},
 {"Procesy","Pamięć","Prywatne przestrzenie adresowe"},
 {"System plików","Format","PSF / BSP2 — Bursztynowy System Plików"},
 {"Interfejs graficzny","GUI","BursztynUI, autorski kompozytor warstw"},
 {"Sieć","Sterowniki","E1000, VirtIO-net (zależnie od sprzętu)"},
 {"Sieć","Protokoły","DHCP, ARP, ICMP, DNS, TCP/HTTP"}
};
inline constexpr size_t PropertyCount=sizeof(Properties)/sizeof(Properties[0]);
inline constexpr bool starts_section(size_t index){
    if(!index)return true;
    for(size_t n=0;n<64;++n){
        if(Properties[index].section[n]!=Properties[index-1].section[n])return true;
        if(!Properties[index].section[n])return false;
    }
    return true;
}
}
