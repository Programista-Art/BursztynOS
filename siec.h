/*
 * Mechanizm: Nagłówki i Struktury Stosu Sieciowego
 */

#pragma once
#include <stdint.h>

// Makra do obracania bajtów (Little Endian <-> Big Endian)
#define HTONS(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define HTONL(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) >> 8) & 0xFF00) | (((x) >> 24) & 0xFF))
#define NTOHS(x) HTONS(x)
#define NTOHL(x) HTONL(x)

extern "C" {
    bool kernel_siec_dns(const char* domena, uint8_t* wyjsciowy_ip);
    bool kernel_siec_pobierz_http(uint8_t* cel_ip, const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc);
    bool kernel_siec_pobierz_https(uint8_t cel_ip[4], const char* domena, const char* sciezka, char* bufor, uint32_t max_dlugosc);
    bool kernel_tls_certyfikat_zaufany();

    bool tcp_gniazdo_polacz(uint8_t* cel_ip, uint16_t port);
    int tcp_gniazdo_wyslij(const uint8_t* dane, uint32_t dlugosc);
    int tcp_gniazdo_odbierz(uint8_t* dane, uint32_t maksymalna_dlugosc);
    void tcp_gniazdo_zamknij();
    bool tcp_gniazdo_otwarte();
}


struct ethernet_header {
    uint8_t  cel_mac[6];
    uint8_t  zrodlo_mac[6];
    uint16_t typ; 
} __attribute__((packed));

struct arp_header {
    uint16_t typ_sprzetu;
    uint16_t typ_protokolu;
    uint8_t  dlugosc_mac;
    uint8_t  dlugosc_ip;
    uint16_t operacja; 
    uint8_t  nadawca_mac[6];
    uint8_t  nadawca_ip[4];
    uint8_t  cel_mac[6];
    uint8_t  cel_ip[4];
} __attribute__((packed));

struct ipv4_header {
    uint8_t  wersja_ihl;
    uint8_t  tos;
    uint16_t dlugosc_calkowita;
    uint16_t id;
    uint16_t flagi_fragment;
    uint8_t  ttl;
    uint8_t  protokol; 
    uint16_t suma_kontrolna;
    uint8_t  zrodlo_ip[4];
    uint8_t  cel_ip[4];
} __attribute__((packed));

struct icmp_header {
    uint8_t  typ; 
    uint8_t  kod;
    uint16_t suma_kontrolna;
    uint16_t id;
    uint16_t sekwencja;
} __attribute__((packed));

struct udp_header {
    uint16_t port_zrodlowy;
    uint16_t port_docelowy;
    uint16_t dlugosc;
    uint16_t suma_kontrolna;
} __attribute__((packed));

struct dhcp_header {
    uint8_t  op;            
    uint8_t  htype;         
    uint8_t  hlen;          
    uint8_t  hops;          
    uint32_t xid;           
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];     
    uint8_t  yiaddr[4];     
    uint8_t  siaddr[4];     
    uint8_t  giaddr[4];     
    uint8_t  chaddr[16];    
    uint8_t  sname[64];     
    uint8_t  file[128];     
    uint8_t  magic_cookie[4]; 
    uint8_t  options[64];   
} __attribute__((packed));

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

// --- NOWOŚĆ KROK 1: Warstwa 4 (TCP) ---

struct tcp_header {
    uint16_t port_zrodlowy;
    uint16_t port_docelowy;
    uint32_t numer_sekwencyjny;    // Sequence Number (SEQ)
    uint32_t numer_potwierdzenia;  // Acknowledgment Number (ACK)
    uint8_t  przesuniecie_danych;  // Data Offset (długość nagłówka)
    uint8_t  flagi;                // Flagi (FIN, SYN, RST, PSH, ACK, URG)
    uint16_t rozmiar_okna;         // Window Size
    uint16_t suma_kontrolna;       // Checksum
    uint16_t wazny_wskaznik;       // Urgent Pointer
} __attribute__((packed));

// Unikalny wymóg TCP: "Pseudo-nagłówek" potrzebny tylko do liczenia sumy kontrolnej.
// Nigdy nie jest wysyłany do sieci bezpośrednio jako struktura.
struct tcp_pseudo_header {
    uint8_t  zrodlo_ip[4];
    uint8_t  cel_ip[4];
    uint8_t  zero;
    uint8_t  protokol; // Dla TCP wynosi 6
    uint16_t dlugosc_tcp;
} __attribute__((packed));

// Flagi TCP
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
