/*
 * Mechanizm: Nagłówki i Struktury Stosu Sieciowego
 */

#pragma once
#include <stdint.h>

#define HTONS(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))
#define HTONL(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) >> 8) & 0xFF00) | (((x) >> 24) & 0xFF))

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

// --- NOWOŚĆ KROK 1: Nagłówek DNS ---
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));