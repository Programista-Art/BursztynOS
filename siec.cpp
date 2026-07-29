#include "siec.h"
#include "e1000.h"

extern void WypiszLog(const char* tekst);
extern "C" void e1000_obsluz_odbior(); // Potrzebne do odświeżania karty podczas czekania na ARP

// Domyślny adres IP (QEMU user-net)
uint8_t nasz_ip[4] = {10, 0, 2, 15};

// --- NOWOŚĆ: Globalna flaga do komunikacji między nadajnikiem a odbiornikiem ---
volatile bool odebrano_pong = false;

// ---------------------------------------------------------
// PAMIĘĆ PODRĘCZNA: TABELA ARP (ARP CACHE)
// ---------------------------------------------------------
#define ROZMIAR_TABLICY_ARP 16

struct ArpWpis {
    uint8_t ip[4];
    uint8_t mac[6];
    bool aktywny;
};

static ArpWpis tablica_arp[ROZMIAR_TABLICY_ARP];

static void skopiuj_mac(uint8_t* cel, uint8_t* zrodlo) { for(int i=0; i<6; i++) cel[i] = zrodlo[i]; }
static void skopiuj_ip(uint8_t* cel, uint8_t* zrodlo) { for(int i=0; i<4; i++) cel[i] = zrodlo[i]; }

bool szukaj_w_cache_arp(uint8_t ip[4], uint8_t wyjscie_mac[6]) {
    for(int i = 0; i < ROZMIAR_TABLICY_ARP; i++) {
        if(tablica_arp[i].aktywny && 
           tablica_arp[i].ip[0] == ip[0] && tablica_arp[i].ip[1] == ip[1] &&
           tablica_arp[i].ip[2] == ip[2] && tablica_arp[i].ip[3] == ip[3]) {
            skopiuj_mac(wyjscie_mac, tablica_arp[i].mac);
            return true;
        }
    }
    return false;
}

void dodaj_do_cache_arp(uint8_t ip[4], uint8_t mac[6]) {
    // 1. Sprawdź, czy już go mamy, by zaktualizować (jeśli np. ktoś zmienił kartę sieciową)
    for(int i = 0; i < ROZMIAR_TABLICY_ARP; i++) {
        if(tablica_arp[i].aktywny && 
           tablica_arp[i].ip[0] == ip[0] && tablica_arp[i].ip[1] == ip[1] &&
           tablica_arp[i].ip[2] == ip[2] && tablica_arp[i].ip[3] == ip[3]) {
            skopiuj_mac(tablica_arp[i].mac, mac);
            return;
        }
    }
    // 2. Jeśli nie mamy, znajdź wolne miejsce
    for(int i = 0; i < ROZMIAR_TABLICY_ARP; i++) {
        if(!tablica_arp[i].aktywny) {
            skopiuj_ip(tablica_arp[i].ip, ip);
            skopiuj_mac(tablica_arp[i].mac, mac);
            tablica_arp[i].aktywny = true;
            return;
        }
    }
    // 3. Prosta polityka nadpisywania, jeśli braknie miejsc
    skopiuj_ip(tablica_arp[0].ip, ip);
    skopiuj_mac(tablica_arp[0].mac, mac);
}

// ---------------------------------------------------------
// GENEROWANIE RAMEK SIECIOWYCH
// ---------------------------------------------------------

uint16_t oblicz_sume_kontrolna(void* dane, int bajty) {
    uint16_t* bufor = (uint16_t*)dane;
    uint32_t suma = 0;
    while(bajty > 1) { suma += *bufor++; bajty -= 2; }
    if(bajty == 1) suma += *(uint8_t*)bufor;
    suma = (suma >> 16) + (suma & 0xFFFF);
    suma += (suma >> 16);
    return (uint16_t)(~suma);
}

void wyslij_zapytanie_arp(uint8_t cel_ip[4]) {
    uint8_t pakiet[64]; // Minimum 60 bajtów dla Ethernetu
    for(int i=0; i<64; i++) pakiet[i] = 0;

    ethernet_header* eth = (ethernet_header*)pakiet;
    for(int i=0; i<6; i++) eth->cel_mac[i] = 0xFF; // FF:FF... (Rozgłoszeniowy MAC)
    skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
    eth->typ = HTONS(0x0806); // ARP

    arp_header* arp = (arp_header*)(pakiet + sizeof(ethernet_header));
    arp->typ_sprzetu = HTONS(1);
    arp->typ_protokolu = HTONS(0x0800);
    arp->dlugosc_mac = 6;
    arp->dlugosc_ip = 4;
    arp->operacja = HTONS(1); // 1 = ZAPYTANIE (Request)
    
    skopiuj_mac(arp->nadawca_mac, pobierz_mac_adres());
    skopiuj_ip(arp->nadawca_ip, nasz_ip);
    for(int i=0; i<6; i++) arp->cel_mac[i] = 0x00; // Nie wiemy kogo szukamy
    skopiuj_ip(arp->cel_ip, cel_ip);

    WypiszLog("[ARP] Brak MAC docelowego. Wysylam zapytanie (Broadcast)...");
    e1000_wyslij_pakiet(pakiet, 60);
}

extern "C" void bws_siec_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4) {
    uint8_t cel_ip[4] = {ip1, ip2, ip3, ip4};
    uint8_t docelowy_mac[6];

    // 1. Sprawdzamy Tabelę ARP!
    if (!szukaj_w_cache_arp(cel_ip, docelowy_mac)) {
        // Jeśli nie znamy adresu, pytamy się wszystkich w sieci:
        wyslij_zapytanie_arp(cel_ip);

        // 2. Inteligentne, aktywne oczekiwanie na odpowiedź
        int timeout = 0;
        while (!szukaj_w_cache_arp(cel_ip, docelowy_mac) && timeout < 50000000) {
            e1000_obsluz_odbior(); // Jądro "odpytuje" w locie kartę E1000!
            timeout++;
            asm volatile("pause");
        }

        if (timeout >= 50000000) {
            WypiszLog("[SIEC] Blad PING: Adres IP nie odpowiada na zapytanie ARP (Timeout).");
            return;
        }
    }

    // 3. Sukces! Mamy MAC z tabeli ARP. Budujemy docelowy pakiet IPv4/ICMP.
    uint8_t pakiet[74];
    for(int i=0; i<74; i++) pakiet[i] = 0;

    ethernet_header* eth = (ethernet_header*)pakiet;
    skopiuj_mac(eth->cel_mac, docelowy_mac); // PRAWIDŁOWY, INDYWIDUALNY MAC!
    skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
    eth->typ = HTONS(0x0800); 

    ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
    ip->wersja_ihl = 0x45; 
    ip->tos = 0;
    ip->dlugosc_calkowita = HTONS(60); 
    ip->id = HTONS(1);
    ip->flagi_fragment = 0;
    ip->ttl = 64;
    ip->protokol = 1; 
    skopiuj_ip(ip->zrodlo_ip, nasz_ip);
    skopiuj_ip(ip->cel_ip, cel_ip);
    ip->suma_kontrolna = oblicz_sume_kontrolna(ip, 20);

    icmp_header* icmp = (icmp_header*)(pakiet + sizeof(ethernet_header) + 20);
    icmp->typ = 8; 
    icmp->kod = 0;
    icmp->id = HTONS(0x1234);
    icmp->sekwencja = HTONS(1);
    icmp->suma_kontrolna = oblicz_sume_kontrolna(icmp, 40);

    // KRYTYCZNA ZMIANA: Przed wystrzeleniem PINGA, resetujemy wskaźnik!
    odebrano_pong = false;
    
    WypiszLog("[SIEC] Adres docelowy potwierdzony. Wysylam pakiet ICMP Echo Request!");
    e1000_wyslij_pakiet(pakiet, 74);

    // 4. Aktywne nasłuchiwanie PONG-a z sieci
    // Ponieważ powłoka w Ring 3 usypia czytanie karty, Jądro musi na chwilę zawiesić wykonywanie
    // by dać szansę procesorowi odebrać zwrotny sygnał ICMP Echo Reply!
    int timeout_ping = 0;
    while (!odebrano_pong && timeout_ping < 50000000) {
        e1000_obsluz_odbior(); // Pobieraj pakiety z kabla na żywo!
        timeout_ping++;
        asm volatile("pause");
    }

    // Jeśli pętla została przerwana przez timeout (nikt nie odpisał)
    if (!odebrano_pong) {
        WypiszLog("[SIEC] Upinal czas oczekiwania na odpowiedz (Request timed out).");
    }
}

// ---------------------------------------------------------
// PARSER PRZYCHODZĄCYCH PAKIETÓW Z KARTY E1000
// ---------------------------------------------------------

void obsluz_pakiet_sieciowy(uint8_t* pakiet, uint16_t dlugosc) {
    if (dlugosc < sizeof(ethernet_header)) return;

    ethernet_header* eth = (ethernet_header*)pakiet;
    uint16_t typ_eth = HTONS(eth->typ);

    if (typ_eth == 0x0806) { 
        arp_header* arp = (arp_header*)(pakiet + sizeof(ethernet_header));
        
        // ZAWSZE łapiemy i zapisujemy MAC nadawcy z każdej komunikacji ARP (Uczymy się w tle!)
        dodaj_do_cache_arp(arp->nadawca_ip, arp->nadawca_mac);

        if (arp->cel_ip[0] == nasz_ip[0] && arp->cel_ip[1] == nasz_ip[1] &&
            arp->cel_ip[2] == nasz_ip[2] && arp->cel_ip[3] == nasz_ip[3]) {
            
            // Odpowiedź na zapytanie do nas (Ktoś chce poznać nasz MAC)
            if (HTONS(arp->operacja) == 1) {
                skopiuj_mac(eth->cel_mac, eth->zrodlo_mac);
                skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
                arp->operacja = HTONS(2); 
                skopiuj_mac(arp->cel_mac, arp->nadawca_mac);
                skopiuj_ip(arp->cel_ip, arp->nadawca_ip);
                skopiuj_mac(arp->nadawca_mac, pobierz_mac_adres());
                skopiuj_ip(arp->nadawca_ip, nasz_ip);
                e1000_wyslij_pakiet(pakiet, dlugosc);
            }
            // Zauważono odpowiedź na NASZE wcześniejsze zapytanie ARP
            else if (HTONS(arp->operacja) == 2) {
                WypiszLog("[ARP] Odebrano dopasowanie MAC. Tabela Cache zaktualizowana!");
            }
        }
    } 
    else if (typ_eth == 0x0800) { 
        ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
        
        if (ip->cel_ip[0] != nasz_ip[0] || ip->cel_ip[3] != nasz_ip[3]) return;

        if (ip->protokol == 1) {
            uint32_t ihl_bajty = (ip->wersja_ihl & 0x0F) * 4;
            icmp_header* icmp = (icmp_header*)((uint8_t*)ip + ihl_bajty);
            
            // Ktoś Pinguje z zewnątrz naszą maszynę
            if (icmp->typ == 8) {
                WypiszLog("[SIEC] Odebrano zewnetrzny PING. Odbijam (PONG!)...");

                skopiuj_mac(eth->cel_mac, eth->zrodlo_mac);
                skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());

                uint8_t stary_zrodlowy_ip[4];
                skopiuj_ip(stary_zrodlowy_ip, ip->zrodlo_ip);
                skopiuj_ip(ip->zrodlo_ip, nasz_ip);
                skopiuj_ip(ip->cel_ip, stary_zrodlowy_ip);
                
                ip->suma_kontrolna = 0;
                ip->suma_kontrolna = oblicz_sume_kontrolna(ip, ihl_bajty);

                icmp->typ = 0; 
                icmp->suma_kontrolna = 0;
                uint16_t dlugosc_icmp = HTONS(ip->dlugosc_calkowita) - ihl_bajty;
                icmp->suma_kontrolna = oblicz_sume_kontrolna(icmp, dlugosc_icmp);

                e1000_wyslij_pakiet(pakiet, dlugosc);
            }
            // Ktoś odpowiada na Ping z naszego terminala
            else if (icmp->typ == 0) {
                WypiszLog("[SIEC] SUKCES! Serwer docelowy zwrocil ping (PONG)!");
                odebrano_pong = true; // Flaga PONG zarejestrowana z sukcesem!
            }
        }
    }
}