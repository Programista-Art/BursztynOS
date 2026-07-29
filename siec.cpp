#include "siec.h"
#include "e1000.h"

extern void WypiszLog(const char* tekst);

// Domyślny adres IP przyznawany przez wbudowany DHCP wirtualizatora QEMU (tryb user-net)
uint8_t nasz_ip[4] = {10, 0, 2, 15};

// Funkcja pomocnicza obliczająca sumę kontrolną pakietów (Wymóg Internetu)
uint16_t oblicz_sume_kontrolna(void* dane, int bajty) {
    uint16_t* bufor = (uint16_t*)dane;
    uint32_t suma = 0;
    while(bajty > 1) { suma += *bufor++; bajty -= 2; }
    if(bajty == 1) suma += *(uint8_t*)bufor;
    suma = (suma >> 16) + (suma & 0xFFFF);
    suma += (suma >> 16);
    return (uint16_t)(~suma);
}

static void skopiuj_mac(uint8_t* cel, uint8_t* zrodlo) {
    for(int i=0; i<6; i++) cel[i] = zrodlo[i];
}
static void skopiuj_ip(uint8_t* cel, uint8_t* zrodlo) {
    for(int i=0; i<4; i++) cel[i] = zrodlo[i];
}

// --- NOWOŚĆ: Budowniczy pakietów PING (Dla programu Ring 3) ---
extern "C" void bws_siec_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4) {
    uint8_t pakiet[74]; // 14 (Eth) + 20 (IP) + 40 (ICMP Payload)
    for(int i=0; i<74; i++) pakiet[i] = 0;

    ethernet_header* eth = (ethernet_header*)pakiet;
    // Wysyłamy jako Broadcast MAC, żeby uniknąć skomplikowanych tabel ARP
    for(int i=0; i<6; i++) eth->cel_mac[i] = 0xFF; 
    for(int i=0; i<6; i++) eth->zrodlo_mac[i] = pobierz_mac_adres()[i];
    eth->typ = HTONS(0x0800); // Protokół: IPv4

    ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
    ip->wersja_ihl = 0x45; // Wersja 4, Długość nagłówka 5 słów (20 bajtów)
    ip->tos = 0;
    ip->dlugosc_calkowita = HTONS(60); // 20 bajtów IP + 40 bajtów ICMP
    ip->id = HTONS(1);
    ip->flagi_fragment = 0;
    ip->ttl = 64;
    ip->protokol = 1; // 1 = ICMP
    for(int i=0; i<4; i++) ip->zrodlo_ip[i] = nasz_ip[i];
    ip->cel_ip[0] = ip1; ip->cel_ip[1] = ip2; ip->cel_ip[2] = ip3; ip->cel_ip[3] = ip4;
    ip->suma_kontrolna = oblicz_sume_kontrolna(ip, 20);

    icmp_header* icmp = (icmp_header*)(pakiet + sizeof(ethernet_header) + 20);
    icmp->typ = 8; // 8 = Echo Request (Ping)
    icmp->kod = 0;
    icmp->id = HTONS(0x1234);
    icmp->sekwencja = HTONS(1);
    icmp->suma_kontrolna = oblicz_sume_kontrolna(icmp, 40);

    WypiszLog("[SIEC] Wysylam pakiet ICMP Echo Request (PING) przez karte sieciowa...");
    e1000_wyslij_pakiet(pakiet, 74);
}

// Główny Parser Pakietów przychodzących z karty E1000
void obsluz_pakiet_sieciowy(uint8_t* pakiet, uint16_t dlugosc) {
    if (dlugosc < sizeof(ethernet_header)) return;

    ethernet_header* eth = (ethernet_header*)pakiet;
    uint16_t typ_eth = HTONS(eth->typ);

    // 1. OBRÓBKA PAKIETÓW ARP (Adresowanie lokalne)
    if (typ_eth == 0x0806) { 
        arp_header* arp = (arp_header*)(pakiet + sizeof(ethernet_header));
        
        // Ktoś pyta kto ma nasz adres IP (ARP Request - Operacja 1)
        if (HTONS(arp->operacja) == 1) {
            if (arp->cel_ip[0] == nasz_ip[0] && arp->cel_ip[1] == nasz_ip[1] &&
                arp->cel_ip[2] == nasz_ip[2] && arp->cel_ip[3] == nasz_ip[3]) {
                
                WypiszLog("[SIEC] Otrzymano zapytanie ARP! Przedstawiam sie (ARP Reply)...");

                skopiuj_mac(eth->cel_mac, eth->zrodlo_mac);
                skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
                
                arp->operacja = HTONS(2); 
                
                skopiuj_mac(arp->cel_mac, arp->nadawca_mac);
                skopiuj_ip(arp->cel_ip, arp->nadawca_ip);
                
                skopiuj_mac(arp->nadawca_mac, pobierz_mac_adres());
                skopiuj_ip(arp->nadawca_ip, nasz_ip);

                e1000_wyslij_pakiet(pakiet, dlugosc);
            }
        }
    } 
    // 2. OBRÓBKA PAKIETÓW IPv4
    else if (typ_eth == 0x0800) { 
        ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
        
        if (ip->cel_ip[0] != nasz_ip[0] || ip->cel_ip[3] != nasz_ip[3]) return;

        if (ip->protokol == 1) {
            uint32_t ihl_bajty = (ip->wersja_ihl & 0x0F) * 4;
            icmp_header* icmp = (icmp_header*)((uint8_t*)ip + ihl_bajty);
            
            // Ktoś nas Ping-uje! (Echo Request - Typ 8)
            if (icmp->typ == 8) {
                WypiszLog("[SIEC] Ktos nas Pinguje! Wysylam ICMP Echo Reply (Pong!)...");

                skopiuj_mac(eth->cel_mac, eth->zrodlo_mac);
                skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());

                uint8_t stary_zrodlowy_ip[4];
                skopiuj_ip(stary_zrodlowy_ip, ip->zrodlo_ip);
                skopiuj_ip(ip->zrodlo_ip, nasz_ip);
                skopiuj_ip(ip->cel_ip, stary_zrodlowy_ip);
                
                ip->suma_kontrolna = 0;
                ip->suma_kontrolna = oblicz_sume_kontrolna(ip, ihl_bajty);

                icmp->typ = 0; // 0 = Echo Reply
                icmp->suma_kontrolna = 0;
                uint16_t dlugosc_icmp = HTONS(ip->dlugosc_calkowita) - ihl_bajty;
                icmp->suma_kontrolna = oblicz_sume_kontrolna(icmp, dlugosc_icmp);

                e1000_wyslij_pakiet(pakiet, dlugosc);
            }
            // NOWOŚĆ: Ktoś odpowiada na naszego Pinga z Shell'a! (Echo Reply - Typ 0)
            else if (icmp->typ == 0) {
                WypiszLog("[SIEC] SUKCES! Otrzymano odpowiedz PONG od zewnetrznego routera! Siec dziala!");
            }
        }
    }
}