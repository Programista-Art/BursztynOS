#pragma once
#include <stdint.h>

#define HTONS(x) ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF))

struct ethernet_header {
    uint8_t  cel_mac[6];
    uint8_t  zrodlo_mac[6];
    uint16_t typ; // 0x0806 = ARP, 0x0800 = IPv4
} __attribute__((packed));

struct arp_header {
    uint16_t typ_sprzetu;
    uint16_t typ_protokolu;
    uint8_t  dlugosc_mac;
    uint8_t  dlugosc_ip;
    uint16_t operacja; // 1 = Request, 2 = Reply
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
    uint8_t  protokol; // 1 = ICMP, 6 = TCP, 17 = UDP
    uint16_t suma_kontrolna;
    uint8_t  zrodlo_ip[4];
    uint8_t  cel_ip[4];
} __attribute__((packed));

struct icmp_header {
    uint8_t  typ; // 8 = Echo Request (Ping), 0 = Echo Reply
    uint8_t  kod;
    uint16_t suma_kontrolna;
    uint16_t id;
    uint16_t sekwencja;
} __attribute__((packed));