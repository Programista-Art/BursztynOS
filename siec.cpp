#include "siec.h"
#include "e1000.h"

void wypisz_log(const char* tekst);
extern "C" void e1000_obsluz_odbior();

// Globalne ustawienia i adresy
uint8_t nasz_ip[4] = {0, 0, 0, 0};
uint8_t brama_ip[4] = {0, 0, 0, 0}; 
volatile bool odebrano_pong = false;

volatile bool dns_odebrano = false;
uint8_t dns_resolved_ip[4] = {0, 0, 0, 0};

// Pomocnicze funkcje tekstowe dla logów sieciowych
static void siec_int_do_str(int wartosc, char* bufor) {
    if (wartosc == 0) { bufor[0] = '0'; bufor[1] = '\0'; return; }
    int i = 0; char temp[16];
    while (wartosc > 0) { temp[i++] = (wartosc % 10) + '0'; wartosc /= 10; }
    int j = 0; while (i > 0) bufor[j++] = temp[--i];
    bufor[j] = '\0';
}
static void siec_kopiuj_str(char* cel, const char* zrodlo) {
    int i = 0; while(zrodlo[i] != '\0') { cel[i] = zrodlo[i]; i++; } cel[i] = '\0';
}
static void siec_dopisz_str(char* cel, const char* zrodlo) {
    int i = 0; while(cel[i] != '\0') i++;
    int j = 0; while(zrodlo[j] != '\0') cel[i++] = zrodlo[j++]; cel[i] = '\0';
}
static int siec_strlen(const char* str) {
    int len = 0; while(str[len]) len++; return len;
}

// ---------------------------------------------------------
// PAMIĘĆ PODRĘCZNA: TABELA ARP (ARP CACHE)
// ---------------------------------------------------------
#define ROZMIAR_TABLICY_ARP 16
struct ArpWpis { uint8_t ip[4]; uint8_t mac[6]; bool aktywny; };
static ArpWpis tablica_arp[ROZMIAR_TABLICY_ARP];

static void skopiuj_mac(uint8_t* cel, uint8_t* zrodlo) { for(int i=0; i<6; i++) cel[i] = zrodlo[i]; }
static void skopiuj_ip(uint8_t* cel, uint8_t* zrodlo) { for(int i=0; i<4; i++) cel[i] = zrodlo[i]; }

bool szukaj_w_cache_arp(uint8_t ip[4], uint8_t wyjscie_mac[6]) {
    for(int i = 0; i < ROZMIAR_TABLICY_ARP; i++) {
        if(tablica_arp[i].aktywny && tablica_arp[i].ip[0] == ip[0] && tablica_arp[i].ip[1] == ip[1] &&
           tablica_arp[i].ip[2] == ip[2] && tablica_arp[i].ip[3] == ip[3]) {
            skopiuj_mac(wyjscie_mac, tablica_arp[i].mac); return true;
        }
    }
    return false;
}

void dodaj_do_cache_arp(uint8_t ip[4], uint8_t mac[6]) {
    for(int i = 0; i < ROZMIAR_TABLICY_ARP; i++) {
        if(tablica_arp[i].aktywny && tablica_arp[i].ip[0] == ip[0] && tablica_arp[i].ip[1] == ip[1] &&
           tablica_arp[i].ip[2] == ip[2] && tablica_arp[i].ip[3] == ip[3]) {
            skopiuj_mac(tablica_arp[i].mac, mac); return;
        }
    }
    for(int i = 0; i < ROZMIAR_TABLICY_ARP; i++) {
        if(!tablica_arp[i].aktywny) {
            skopiuj_ip(tablica_arp[i].ip, ip); skopiuj_mac(tablica_arp[i].mac, mac); tablica_arp[i].aktywny = true; return;
        }
    }
    skopiuj_ip(tablica_arp[0].ip, ip); skopiuj_mac(tablica_arp[0].mac, mac);
}

uint16_t oblicz_sume_kontrolna(void* dane, int bajty) {
    uint16_t* bufor = (uint16_t*)dane; uint32_t suma = 0;
    while(bajty > 1) { suma += *bufor++; bajty -= 2; }
    if(bajty == 1) suma += *(uint8_t*)bufor;
    suma = (suma >> 16) + (suma & 0xFFFF); suma += (suma >> 16);
    return (uint16_t)(~suma);
}

// ---------------------------------------------------------
// INTELIGENTNE TRASOWANIE (ROUTING) - Wspólne dla ICMP, DNS i TCP
// ---------------------------------------------------------
void wyslij_zapytanie_arp(uint8_t cel_ip[4]) {
    uint8_t pakiet[64]; for(int i=0; i<64; i++) pakiet[i] = 0;
    ethernet_header* eth = (ethernet_header*)pakiet;
    for(int i=0; i<6; i++) eth->cel_mac[i] = 0xFF; 
    skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
    eth->typ = HTONS(0x0806); 

    arp_header* arp = (arp_header*)(pakiet + sizeof(ethernet_header));
    arp->typ_sprzetu = HTONS(1); arp->typ_protokolu = HTONS(0x0800);
    arp->dlugosc_mac = 6; arp->dlugosc_ip = 4; arp->operacja = HTONS(1); 
    
    skopiuj_mac(arp->nadawca_mac, pobierz_mac_adres()); skopiuj_ip(arp->nadawca_ip, nasz_ip);
    for(int i=0; i<6; i++) arp->cel_mac[i] = 0x00; 
    skopiuj_ip(arp->cel_ip, cel_ip);
    
    e1000_wyslij_pakiet(pakiet, 60);
}

bool rozwiaz_adres_mac(uint8_t cel_ip[4], uint8_t wyjscie_mac[6]) {
    bool w_naszej_sieci = (cel_ip[0] == nasz_ip[0] && cel_ip[1] == nasz_ip[1] && cel_ip[2] == nasz_ip[2]);
    uint8_t* ip_do_zapytania = cel_ip;
    
    if (!w_naszej_sieci && brama_ip[0] != 0) {
        ip_do_zapytania = brama_ip; // Routing pakietu przez Bramę (Router)
    }

    if (!szukaj_w_cache_arp(ip_do_zapytania, wyjscie_mac)) {
        wyslij_zapytanie_arp(ip_do_zapytania);
        int timeout = 0;
        while (!szukaj_w_cache_arp(ip_do_zapytania, wyjscie_mac) && timeout < 50000000) {
            e1000_obsluz_odbior(); timeout++; asm volatile("pause");
        }
        if (timeout >= 50000000) return false;
    }
    return true;
}

// ---------------------------------------------------------
// PROTOKÓŁ DHCP (UDP/IP)
// ---------------------------------------------------------
void wyslij_pakiet_dhcp(uint8_t typ_wiadomosci, uint8_t* zapytanie_ip, uint8_t* serwer_ip) {
    uint8_t pakiet[400]; for(int i=0; i<400; i++) pakiet[i] = 0;

    ethernet_header* eth = (ethernet_header*)pakiet;
    for(int i=0; i<6; i++) eth->cel_mac[i] = 0xFF; 
    skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres()); eth->typ = HTONS(0x0800); 

    ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
    ip->wersja_ihl = 0x45; ip->tos = 0; ip->dlugosc_calkowita = HTONS(20 + 8 + sizeof(dhcp_header));
    ip->id = HTONS(1); ip->flagi_fragment = 0; ip->ttl = 64; ip->protokol = 17; 
    
    uint8_t broadcast_ip[4] = {255, 255, 255, 255}; uint8_t zrodlo_ip[4] = {0, 0, 0, 0}; 
    skopiuj_ip(ip->zrodlo_ip, zrodlo_ip); skopiuj_ip(ip->cel_ip, broadcast_ip);
    ip->suma_kontrolna = oblicz_sume_kontrolna(ip, 20);

    udp_header* udp = (udp_header*)(pakiet + sizeof(ethernet_header) + 20);
    udp->port_zrodlowy = HTONS(68); udp->port_docelowy = HTONS(67); 
    udp->dlugosc = HTONS(8 + sizeof(dhcp_header)); udp->suma_kontrolna = 0; 

    dhcp_header* dhcp = (dhcp_header*)(pakiet + sizeof(ethernet_header) + 20 + 8);
    dhcp->op = 1; dhcp->htype = 1; dhcp->hlen = 6; dhcp->hops = 0;
    dhcp->xid = HTONL(0x12345678); dhcp->secs = 0; dhcp->flags = HTONS(0x8000); 
    skopiuj_mac(dhcp->chaddr, pobierz_mac_adres());
    
    dhcp->magic_cookie[0] = 0x63; dhcp->magic_cookie[1] = 0x82; dhcp->magic_cookie[2] = 0x53; dhcp->magic_cookie[3] = 0x63;

    int opt_idx = 0;
    dhcp->options[opt_idx++] = 53; dhcp->options[opt_idx++] = 1; dhcp->options[opt_idx++] = typ_wiadomosci;

    if (typ_wiadomosci == 3 && zapytanie_ip && serwer_ip) { 
        dhcp->options[opt_idx++] = 50; dhcp->options[opt_idx++] = 4;
        dhcp->options[opt_idx++] = zapytanie_ip[0]; dhcp->options[opt_idx++] = zapytanie_ip[1];
        dhcp->options[opt_idx++] = zapytanie_ip[2]; dhcp->options[opt_idx++] = zapytanie_ip[3];
        dhcp->options[opt_idx++] = 54; dhcp->options[opt_idx++] = 4;
        dhcp->options[opt_idx++] = serwer_ip[0]; dhcp->options[opt_idx++] = serwer_ip[1];
        dhcp->options[opt_idx++] = serwer_ip[2]; dhcp->options[opt_idx++] = serwer_ip[3];
    }
    dhcp->options[opt_idx++] = 255; 
    e1000_wyslij_pakiet(pakiet, sizeof(ethernet_header) + 20 + 8 + sizeof(dhcp_header));
}

extern "C" void uruchom_klienta_dhcp() {
    wypisz_log("[DHCP] Uruchamiam klienta DHCP (Wysylam UDP DISCOVER)...");
    wyslij_pakiet_dhcp(1, nullptr, nullptr);
    int timeout = 0;
    while (nasz_ip[0] == 0 && timeout < 100000000) { e1000_obsluz_odbior(); timeout++; asm volatile("pause"); }
    if (nasz_ip[0] == 0) wypisz_log("[DHCP] Blad: Nie udalo sie odebrac adresu IP od rutera (Timeout).");
}

// ---------------------------------------------------------
// PING (ICMP) I DNS (UDP)
// ---------------------------------------------------------
extern "C" void bws_siec_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4) {
    if (nasz_ip[0] == 0) { wypisz_log("[SIEC] Blad PING: Brak IP!"); return; }
    uint8_t cel_ip[4] = {ip1, ip2, ip3, ip4}; uint8_t docelowy_mac[6];

    if (!rozwiaz_adres_mac(cel_ip, docelowy_mac)) { wypisz_log("[SIEC] Blad PING: Brak trasy do celu (Ruter/Host nie odpowiada)."); return; }

    uint8_t pakiet[74]; for(int i=0; i<74; i++) pakiet[i] = 0;
    ethernet_header* eth = (ethernet_header*)pakiet;
    skopiuj_mac(eth->cel_mac, docelowy_mac); skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres()); eth->typ = HTONS(0x0800); 

    ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
    ip->wersja_ihl = 0x45; ip->tos = 0; ip->dlugosc_calkowita = HTONS(60); ip->id = HTONS(1);
    ip->flagi_fragment = 0; ip->ttl = 64; ip->protokol = 1; 
    skopiuj_ip(ip->zrodlo_ip, nasz_ip); skopiuj_ip(ip->cel_ip, cel_ip); ip->suma_kontrolna = oblicz_sume_kontrolna(ip, 20);

    icmp_header* icmp = (icmp_header*)(pakiet + sizeof(ethernet_header) + 20);
    icmp->typ = 8; icmp->kod = 0; icmp->id = HTONS(0x1234); icmp->sekwencja = HTONS(1);
    icmp->suma_kontrolna = oblicz_sume_kontrolna(icmp, 40);

    odebrano_pong = false;
    e1000_wyslij_pakiet(pakiet, 74);
    int timeout_ping = 0;
    while (!odebrano_pong && timeout_ping < 50000000) { e1000_obsluz_odbior(); timeout_ping++; asm volatile("pause"); }
    if (!odebrano_pong) wypisz_log("[SIEC] Upinal czas oczekiwania na odpowiedz (Request timed out).");
}

extern "C" bool bws_siec_dns(const char* domena, uint8_t* wyjsciowy_ip) {
    if (nasz_ip[0] == 0) return false;
    uint8_t serwer_dns[4] = {8, 8, 8, 8}; uint8_t docelowy_mac[6];
    if (!rozwiaz_adres_mac(serwer_dns, docelowy_mac)) return false; 

    uint8_t pakiet[512]; for(int i=0; i<512; i++) pakiet[i] = 0;
    ethernet_header* eth = (ethernet_header*)pakiet;
    skopiuj_mac(eth->cel_mac, docelowy_mac); skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres()); eth->typ = HTONS(0x0800);

    ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
    ip->wersja_ihl = 0x45; ip->tos = 0; ip->id = HTONS(2); ip->flagi_fragment = 0; ip->ttl = 64; ip->protokol = 17; 
    skopiuj_ip(ip->zrodlo_ip, nasz_ip); skopiuj_ip(ip->cel_ip, serwer_dns);

    udp_header* udp = (udp_header*)(pakiet + sizeof(ethernet_header) + 20);
    udp->port_zrodlowy = HTONS(50053); udp->port_docelowy = HTONS(53); udp->suma_kontrolna = 0;

    dns_header* dns = (dns_header*)(pakiet + sizeof(ethernet_header) + 20 + 8);
    dns->id = HTONS(0xABCD); dns->flags = HTONS(0x0100); dns->qdcount = HTONS(1);
    dns->ancount = 0; dns->nscount = 0; dns->arcount = 0;

    uint8_t* qname = pakiet + sizeof(ethernet_header) + 20 + 8 + sizeof(dns_header);
    int lock = 0; uint8_t* q = qname;
    for(int i = 0; i <= siec_strlen(domena); i++) {
        if(domena[i] == '.' || domena[i] == '\0') { *q++ = i - lock; for(; lock < i; lock++) *q++ = domena[lock]; lock++; }
        if(domena[i] == '\0') { *q++ = 0; break; }
    }
    uint16_t* qinfo = (uint16_t*)q; qinfo[0] = HTONS(1); qinfo[1] = HTONS(1); 
    
    int udp_payload_len = (uint8_t*)(qinfo + 2) - (uint8_t*)udp;
    int ip_payload_len = (uint8_t*)(qinfo + 2) - (uint8_t*)ip;
    int caly_pakiet_len = (uint8_t*)(qinfo + 2) - pakiet;

    udp->dlugosc = HTONS(udp_payload_len); ip->dlugosc_calkowita = HTONS(ip_payload_len);
    ip->suma_kontrolna = oblicz_sume_kontrolna(ip, 20);

    dns_odebrano = false; dns_resolved_ip[0] = 0;
    e1000_wyslij_pakiet(pakiet, caly_pakiet_len);

    int timeout_dns = 0;
    while (!dns_odebrano && timeout_dns < 50000000) { e1000_obsluz_odbior(); timeout_dns++; asm volatile("pause"); }
    if (dns_odebrano && dns_resolved_ip[0] != 0) {
        wyjsciowy_ip[0] = dns_resolved_ip[0]; wyjsciowy_ip[1] = dns_resolved_ip[1];
        wyjsciowy_ip[2] = dns_resolved_ip[2]; wyjsciowy_ip[3] = dns_resolved_ip[3]; return true;
    }
    return false;
}

// ---------------------------------------------------------
// --- NOWOŚĆ KROK 2: MASZYNA STANÓW TCP I HTTP (Wbudowana) ---
// ---------------------------------------------------------
enum TCP_STATE { TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED };
volatile TCP_STATE stan_tcp = TCP_CLOSED;

uint32_t tcp_nasz_seq = 0;
uint32_t tcp_nasz_ack = 0;
uint16_t tcp_nasz_port = 50000;
uint16_t tcp_cel_port = 80;
uint8_t  tcp_cel_ip[4] = {0,0,0,0};
uint8_t  tcp_cel_mac[6] = {0,0,0,0,0,0};

volatile bool tcp_dane_odebrane = false;
char* tcp_bufor_odbiorczy = nullptr;
uint32_t tcp_zapisano_bajtow = 0;
uint32_t tcp_max_bajtow = 0;
bool tcp_pomin_naglowki_http = false;

// Wewnętrzna funkcja łącząca (Multiplexer) dla pakietów TCP
void wyslij_pakiet_tcp(uint8_t flagi, uint8_t* payload, uint16_t payload_len) {
    uint8_t pakiet[1500]; for(int i=0; i<1500; i++) pakiet[i] = 0;

    ethernet_header* eth = (ethernet_header*)pakiet;
    skopiuj_mac(eth->cel_mac, tcp_cel_mac);
    skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
    eth->typ = HTONS(0x0800);

    ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
    ip->wersja_ihl = 0x45; ip->tos = 0; ip->id = HTONS(3);
    ip->dlugosc_calkowita = HTONS(20 + sizeof(tcp_header) + payload_len);
    ip->flagi_fragment = 0; ip->ttl = 64; ip->protokol = 6; // Protokół TCP
    skopiuj_ip(ip->zrodlo_ip, nasz_ip); skopiuj_ip(ip->cel_ip, tcp_cel_ip);
    ip->suma_kontrolna = oblicz_sume_kontrolna(ip, 20);

    tcp_header* tcp = (tcp_header*)(pakiet + sizeof(ethernet_header) + 20);
    tcp->port_zrodlowy = HTONS(tcp_nasz_port);
    tcp->port_docelowy = HTONS(tcp_cel_port);
    tcp->numer_sekwencyjny = HTONL(tcp_nasz_seq);
    tcp->numer_potwierdzenia = HTONL(tcp_nasz_ack);
    tcp->przesuniecie_danych = (sizeof(tcp_header) / 4) << 4; // Podstawowa długość nagłówka TCP (20 bajtów = 5 słów)
    tcp->flagi = flagi;
    tcp->rozmiar_okna = HTONS(8192);
    tcp->wazny_wskaznik = 0;

    // Przekopiuj zawartość payloadu (np. tekst żądania GET)
    if (payload_len > 0) {
        uint8_t* dest_payload = (uint8_t*)tcp + sizeof(tcp_header);
        for(int i=0; i<payload_len; i++) dest_payload[i] = payload[i];
    }

    // Specyficzna dla TCP weryfikacja sumy kontrolnej wykorzystująca "Pseudo-Nagłówek"
    uint8_t bufor_sumy[1500];
    tcp_pseudo_header* ph = (tcp_pseudo_header*)bufor_sumy;
    skopiuj_ip(ph->zrodlo_ip, ip->zrodlo_ip);
    skopiuj_ip(ph->cel_ip, ip->cel_ip);
    ph->zero = 0; ph->protokol = 6;
    ph->dlugosc_tcp = HTONS(sizeof(tcp_header) + payload_len);

    uint8_t* tcp_dane = bufor_sumy + sizeof(tcp_pseudo_header);
    uint8_t* src_tcp = (uint8_t*)tcp;
    for(int i=0; i<(int)(sizeof(tcp_header) + payload_len); i++) tcp_dane[i] = src_tcp[i];
    
    tcp->suma_kontrolna = oblicz_sume_kontrolna(bufor_sumy, sizeof(tcp_pseudo_header) + sizeof(tcp_header) + payload_len);

    e1000_wyslij_pakiet(pakiet, sizeof(ethernet_header) + 20 + sizeof(tcp_header) + payload_len);
}

// Główna publiczna funkcja Jądra wywoływana z powłoki jako HTTP Downloader
extern "C" bool bws_siec_pobierz_http(uint8_t cel_ip[4], const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc) {
    if (nasz_ip[0] == 0) return false;

    if (!rozwiaz_adres_mac(cel_ip, tcp_cel_mac)) {
        wypisz_log("[TCP] Blad routingu do serwera docelowego.");
        return false;
    }

    skopiuj_ip(tcp_cel_ip, cel_ip);
    tcp_cel_port = 80; // Standardowy port stron www (HTTP)
    tcp_nasz_port++; // Otwarcie nowego (losowego) gniazda po naszej stronie
    tcp_nasz_seq = 1000;
    tcp_nasz_ack = 0;
    
    tcp_bufor_odbiorczy = bufor;
    tcp_max_bajtow = max_dlugosc;
    tcp_zapisano_bajtow = 0;
    tcp_dane_odebrane = false;
    tcp_pomin_naglowki_http = false;
    for(uint32_t i=0; i<max_dlugosc; i++) bufor[i] = 0;

    // Etap 1: Inicjacja Połączenia (SYN)
    wypisz_log("[TCP] Rozpoczynam Handshake (Wysylam SYN)...");
    stan_tcp = TCP_SYN_SENT;
    wyslij_pakiet_tcp(TCP_SYN, nullptr, 0);

    // Oczekujemy na odpowiedź serwera (SYN-ACK) by wejść w stan ESTABLISHED
    int timeout = 0;
    while(stan_tcp != TCP_ESTABLISHED && timeout < 50000000) {
        e1000_obsluz_odbior(); timeout++; asm volatile("pause");
    }
    if(stan_tcp != TCP_ESTABLISHED) {
        wypisz_log("[TCP] Timeout oczekiwania na SYN-ACK (Brak odpowiedzi ze strony WWW).");
        return false;
    }

    // Etap 2: Konstrukcja Zapytania HTTP (GET)
    wypisz_log("[HTTP] Sesja nawiazana. Wysylam zapytanie HTTP GET...");
    char http_get[256]; http_get[0] = 0;
    siec_kopiuj_str(http_get, "GET "); siec_dopisz_str(http_get, sciezka);
    siec_dopisz_str(http_get, " HTTP/1.0\r\nHost: "); siec_dopisz_str(http_get, domena);
    siec_dopisz_str(http_get, "\r\nConnection: close\r\n\r\n");
    
    wyslij_pakiet_tcp(TCP_PSH | TCP_ACK, (uint8_t*)http_get, siec_strlen(http_get));
    
    // Etap 3: Czekamy na dane i ostateczne zamknięcie sesji z zewnątrz przez FIN
    timeout = 0;
    while(stan_tcp == TCP_ESTABLISHED && timeout < 100000000) { 
        e1000_obsluz_odbior(); timeout++; asm volatile("pause");
    }

    // Zamknięcie gniazda TCP po naszej stronie (jeśli to my przerwaliśmy timeout)
    if (stan_tcp != TCP_CLOSED) {
        wyslij_pakiet_tcp(TCP_FIN | TCP_ACK, nullptr, 0);
        stan_tcp = TCP_CLOSED;
    }

    if (tcp_zapisano_bajtow > 0) {
        char log[128]; siec_kopiuj_str(log, "[HTTP] Zmieszczono w buforze transferu (bajtow): ");
        char nbuf[16]; siec_int_do_str(tcp_zapisano_bajtow, nbuf); siec_dopisz_str(log, nbuf);
        wypisz_log(log);
        return true;
    }
    
    wypisz_log("[HTTP] Blad: Polaczenie powiodlo sie, ale serwer nie wyslal danych.");
    return false;
}


// ---------------------------------------------------------
// GŁÓWNY MULTIPLEXER PAKIETÓW (Analiza Warstwy 3, 4 i 7)
// ---------------------------------------------------------
void obsluz_pakiet_sieciowy(uint8_t* pakiet, uint16_t dlugosc) {
    if (dlugosc < sizeof(ethernet_header)) return;
    ethernet_header* eth = (ethernet_header*)pakiet;
    uint16_t typ_eth = HTONS(eth->typ);

    if (typ_eth == 0x0806) { 
        arp_header* arp = (arp_header*)(pakiet + sizeof(ethernet_header));
        dodaj_do_cache_arp(arp->nadawca_ip, arp->nadawca_mac);

        if (arp->cel_ip[0] == nasz_ip[0] && arp->cel_ip[1] == nasz_ip[1] &&
            arp->cel_ip[2] == nasz_ip[2] && arp->cel_ip[3] == nasz_ip[3]) {
            if (HTONS(arp->operacja) == 1) {
                skopiuj_mac(eth->cel_mac, eth->zrodlo_mac); skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
                arp->operacja = HTONS(2); 
                skopiuj_mac(arp->cel_mac, arp->nadawca_mac); skopiuj_ip(arp->cel_ip, arp->nadawca_ip);
                skopiuj_mac(arp->nadawca_mac, pobierz_mac_adres()); skopiuj_ip(arp->nadawca_ip, nasz_ip);
                e1000_wyslij_pakiet(pakiet, dlugosc);
            }
            else if (HTONS(arp->operacja) == 2) wypisz_log("[ARP] Odebrano dopasowanie MAC. Tabela Cache zaktualizowana!");
        }
    } 
    else if (typ_eth == 0x0800) { 
        ipv4_header* ip = (ipv4_header*)(pakiet + sizeof(ethernet_header));
        bool pakiet_do_mnie = (ip->cel_ip[0] == nasz_ip[0] && ip->cel_ip[1] == nasz_ip[1] &&
                               ip->cel_ip[2] == nasz_ip[2] && ip->cel_ip[3] == nasz_ip[3]);
        bool pakiet_broadcast = (ip->cel_ip[0] == 255 && ip->cel_ip[1] == 255 && ip->cel_ip[2] == 255 && ip->cel_ip[3] == 255);
                                 
        if (!pakiet_do_mnie && !pakiet_broadcast) return;

        if (ip->protokol == 1) { // ICMP (Ping)
            uint32_t ihl_bajty = (ip->wersja_ihl & 0x0F) * 4;
            icmp_header* icmp = (icmp_header*)((uint8_t*)ip + ihl_bajty);
            
            if (icmp->typ == 8) {
                wypisz_log("[SIEC] Odebrano zewnetrzny PING. Odbijam (PONG!)...");
                skopiuj_mac(eth->cel_mac, eth->zrodlo_mac); skopiuj_mac(eth->zrodlo_mac, pobierz_mac_adres());
                uint8_t stary_zrodlowy_ip[4]; skopiuj_ip(stary_zrodlowy_ip, ip->zrodlo_ip);
                skopiuj_ip(ip->zrodlo_ip, nasz_ip); skopiuj_ip(ip->cel_ip, stary_zrodlowy_ip);
                ip->suma_kontrolna = 0; ip->suma_kontrolna = oblicz_sume_kontrolna(ip, ihl_bajty);
                icmp->typ = 0; icmp->suma_kontrolna = 0;
                uint16_t dlugosc_icmp = HTONS(ip->dlugosc_calkowita) - ihl_bajty;
                icmp->suma_kontrolna = oblicz_sume_kontrolna(icmp, dlugosc_icmp);
                e1000_wyslij_pakiet(pakiet, dlugosc);
            }
            else if (icmp->typ == 0) { odebrano_pong = true; }
        }
        else if (ip->protokol == 17) { // UDP (DHCP / DNS)
            uint32_t ihl_bajty = (ip->wersja_ihl & 0x0F) * 4;
            udp_header* udp = (udp_header*)((uint8_t*)ip + ihl_bajty);
            
            if (HTONS(udp->port_docelowy) == 68) { // DHCP
                dhcp_header* dhcp = (dhcp_header*)((uint8_t*)udp + sizeof(udp_header));
                if (dhcp->op == 2 && dhcp->xid == HTONL(0x12345678)) {
                    uint8_t msg_type = 0; uint8_t serwer_ip[4] = {0}; uint8_t* opt = dhcp->options;
                while(*opt != 255) { 
                    if (*opt == 53) { msg_type = opt[2]; } 
                    else if (*opt == 54) { 
                        serwer_ip[0] = opt[2]; serwer_ip[1] = opt[3]; serwer_ip[2] = opt[4]; serwer_ip[3] = opt[5];
                    }
                    else if (*opt == 3) { brama_ip[0] = opt[2]; brama_ip[1] = opt[3]; brama_ip[2] = opt[4]; brama_ip[3] = opt[5]; }
                    opt += 2 + opt[1]; 
                }
                if (msg_type == 2) { wyslij_pakiet_dhcp(3, dhcp->yiaddr, serwer_ip); } 
                else if (msg_type == 5) { 
                    nasz_ip[0] = dhcp->yiaddr[0]; nasz_ip[1] = dhcp->yiaddr[1]; nasz_ip[2] = dhcp->yiaddr[2]; nasz_ip[3] = dhcp->yiaddr[3];
                    char log[128]; siec_kopiuj_str(log, "[DHCP] SUKCES (ACK)! Automatyczny adres IP to: ");
                    char buf[8]; siec_int_do_str(nasz_ip[0], buf); siec_dopisz_str(log, buf); siec_dopisz_str(log, ".");
                    siec_int_do_str(nasz_ip[1], buf); siec_dopisz_str(log, buf); siec_dopisz_str(log, ".");
                    siec_int_do_str(nasz_ip[2], buf); siec_dopisz_str(log, buf); siec_dopisz_str(log, ".");
                    siec_int_do_str(nasz_ip[3], buf); siec_dopisz_str(log, buf);
                    wypisz_log(log);
                }
                }
            }
            else if (HTONS(udp->port_docelowy) == 50053) { // DNS
                dns_header* dns = (dns_header*)((uint8_t*)udp + sizeof(udp_header));
                if ((HTONS(dns->flags) & 0x8000) && dns->id == HTONS(0xABCD)) {
                    uint8_t* ptr = (uint8_t*)dns + sizeof(dns_header);
                    int qdcount = HTONS(dns->qdcount);
                    for(int i = 0; i < qdcount; i++) { while(*ptr != 0) ptr++; ptr += 5; }
                    
                    int ancount = HTONS(dns->ancount);
                    for(int i = 0; i < ancount; i++) {
                        if ((*ptr & 0xC0) == 0xC0) { ptr += 2; } else { while(*ptr != 0) ptr++; ptr += 1; }
                        uint16_t atype = HTONS(*(uint16_t*)ptr); ptr += 2;
                        uint16_t aclass = HTONS(*(uint16_t*)ptr); ptr += 2;
                        ptr += 4; // TTL
                        uint16_t datalen = HTONS(*(uint16_t*)ptr); ptr += 2;
                        
                        if (atype == 1 && aclass == 1 && datalen == 4) {
                            dns_resolved_ip[0] = ptr[0]; dns_resolved_ip[1] = ptr[1];
                            dns_resolved_ip[2] = ptr[2]; dns_resolved_ip[3] = ptr[3];
                            dns_odebrano = true; return; 
                        }
                        ptr += datalen; 
                    }
                }
            }
        }
        // --- NOWOŚĆ KROK 2: Analiza pakietów TCP ---
        else if (ip->protokol == 6) { // TCP
            uint32_t ihl_bajty = (ip->wersja_ihl & 0x0F) * 4;
            tcp_header* tcp = (tcp_header*)((uint8_t*)ip + ihl_bajty);
            uint32_t tcp_hdr_len = (tcp->przesuniecie_danych >> 4) * 4;

            // Upewniamy się, że to pakiet do naszej konkretnej sesji pobierania
            if (HTONS(tcp->port_docelowy) == tcp_nasz_port && 
                ip->zrodlo_ip[0] == tcp_cel_ip[0] && ip->zrodlo_ip[1] == tcp_cel_ip[1] &&
                ip->zrodlo_ip[2] == tcp_cel_ip[2] && ip->zrodlo_ip[3] == tcp_cel_ip[3]) {
                
                uint8_t flagi = tcp->flagi;

                // ODEBRANO: SYN-ACK (Uścisk Dłoni: Akceptacja przez Serwer)
                if (stan_tcp == TCP_SYN_SENT && (flagi & TCP_SYN) && (flagi & TCP_ACK)) {
                    wypisz_log("[TCP] Otrzymano SYN-ACK. Serwer podjal polaczenie. Wysylam potwierdzenie ACK...");
                    tcp_nasz_ack = NTOHL(tcp->numer_sekwencyjny) + 1;
                    tcp_nasz_seq++; 
                    stan_tcp = TCP_ESTABLISHED;
                    wyslij_pakiet_tcp(TCP_ACK, nullptr, 0); // Krok 3 handshake'a
                }
                // ODBIERANIE DANYCH Z INTERNETU
                else if (stan_tcp == TCP_ESTABLISHED) {
                    uint32_t payload_len = HTONS(ip->dlugosc_calkowita) - ihl_bajty - tcp_hdr_len;
                    
                    if (payload_len > 0) {
                        uint8_t* payload = (uint8_t*)tcp + tcp_hdr_len;
                        
                        // Przepisujemy odebrane bajty z karty sieciowej do bufora transferowego (wirtualnego dysku)
                        if (tcp_bufor_odbiorczy && tcp_zapisano_bajtow < tcp_max_bajtow) {
                            int start_idx = 0;
                            // Inteligentne odcinanie nagłówków HTTP (Czekamy na dwie puste linie: \r\n\r\n)
                            if (!tcp_pomin_naglowki_http) {
                                for(uint32_t i=0; i<payload_len-3; i++) {
                                    if(payload[i] == '\r' && payload[i+1] == '\n' && payload[i+2] == '\r' && payload[i+3] == '\n') {
                                        start_idx = i + 4;
                                        tcp_pomin_naglowki_http = true;
                                        break;
                                    }
                                }
                                if(!tcp_pomin_naglowki_http) start_idx = payload_len; // Ignorujemy caly pakiet jesli to wciaz tylko naglowki
                            }

                            // Zapis samego "mięska" pobranego pliku
                            for(uint32_t i=start_idx; i<payload_len && tcp_zapisano_bajtow < tcp_max_bajtow; i++) {
                                tcp_bufor_odbiorczy[tcp_zapisano_bajtow++] = payload[i];
                            }
                            if (tcp_zapisano_bajtow > 0) tcp_dane_odebrane = true;
                        }
                        
                        // Zawsze musimy pokwitować serwerowi odebranie pakietu danych (ACK), inaczem zacznie nam je retransmitować!
                        tcp_nasz_ack = NTOHL(tcp->numer_sekwencyjny) + payload_len;
                        wyslij_pakiet_tcp(TCP_ACK, nullptr, 0);
                    }
                    
                    // Serwer mówi, że skończył wysyłanie (FIN) - zamykamy!
                    if (flagi & TCP_FIN) {
                        wypisz_log("[TCP] Transmisja zakonczona. Serwer inicjuje rozlaczenie (FIN).");
                        tcp_nasz_ack = NTOHL(tcp->numer_sekwencyjny) + 1;
                        wyslij_pakiet_tcp(TCP_ACK, nullptr, 0);
                        stan_tcp = TCP_CLOSED;
                    }
                }
            }
        }
    }
}