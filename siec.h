/*
 * Bursztyn OS - publiczny interfejs stosu sieciowego
 *
 * Implementacja:
 *   siec.cpp
 *
 * Warstwy/protokoly:
 *   Ethernet II
 *   ARP
 *   IPv4
 *   ICMP
 *   UDP
 *   DHCPv4
 *   DNS
 *   TCP
 *   HTTP
 *
 * HTTPS/TLS jest implementowane w osobnej warstwie, ale korzysta z
 * tcp_gniazdo_*() zadeklarowanych w tym naglowku.
 *
 * UWAGA O BEZPIECZENSTWIE:
 *
 * Struktury ethernet_header/ipv4_header/... reprezentuja format danych
 * "na przewodzie". Sa packed i moga znajdowac sie pod niewyrownanym
 * adresem. Nie nalezy ich uzywac do parsowania niezaufanej ramki bez
 * uprzedniej kontroli dlugosci.
 *
 * Poprawiony siec.cpp wykonuje bounds-checking przed dereferencja i nie
 * polega na slepym rzutowaniu calej odebranej ramki na te struktury.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. BYTE ORDER
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Bursztyn OS jest obecnie x86_64 little-endian.
 *
 * Funkcje inline sa bezpieczniejsze niz stare makra:
 *
 *   HTONS(i++)
 *
 * nie wykona juz i++ kilka razy.
 */
inline constexpr uint16_t bws_bswap16(
    uint16_t wartosc
) noexcept {
    return
        static_cast<uint16_t>(
            (wartosc << 8) |
            (wartosc >> 8)
        );
}

inline constexpr uint32_t bws_bswap32(
    uint32_t wartosc
) noexcept {
    return
        ((wartosc & UINT32_C(0x000000FF)) << 24) |
        ((wartosc & UINT32_C(0x0000FF00)) << 8)  |
        ((wartosc & UINT32_C(0x00FF0000)) >> 8)  |
        ((wartosc & UINT32_C(0xFF000000)) >> 24);
}

inline constexpr uint16_t bws_htons(
    uint16_t wartosc
) noexcept {
    return bws_bswap16(wartosc);
}

inline constexpr uint16_t bws_ntohs(
    uint16_t wartosc
) noexcept {
    return bws_bswap16(wartosc);
}

inline constexpr uint32_t bws_htonl(
    uint32_t wartosc
) noexcept {
    return bws_bswap32(wartosc);
}

inline constexpr uint32_t bws_ntohl(
    uint32_t wartosc
) noexcept {
    return bws_bswap32(wartosc);
}

/*
 * Zachowujemy stare nazwy dla zgodnosci ze starszym kodem.
 * Argument jest przekazywany do funkcji tylko raz.
 */
#define HTONS(x) bws_htons(static_cast<uint16_t>(x))
#define NTOHS(x) bws_ntohs(static_cast<uint16_t>(x))
#define HTONL(x) bws_htonl(static_cast<uint32_t>(x))
#define NTOHL(x) bws_ntohl(static_cast<uint32_t>(x))

#else

/*
 * Awaryjna wersja dla kodu C.
 * Projekt jadra jest obecnie budowany jako C++, ale naglowek pozostaje
 * czytelny rowniez dla przyszlych modulow C.
 */
static inline uint16_t bws_htons(
    uint16_t wartosc
) {
    return
        (uint16_t)(
            (wartosc << 8) |
            (wartosc >> 8)
        );
}

static inline uint16_t bws_ntohs(
    uint16_t wartosc
) {
    return bws_htons(wartosc);
}

static inline uint32_t bws_htonl(
    uint32_t wartosc
) {
    return
        ((wartosc & UINT32_C(0x000000FF)) << 24) |
        ((wartosc & UINT32_C(0x0000FF00)) << 8)  |
        ((wartosc & UINT32_C(0x00FF0000)) >> 8)  |
        ((wartosc & UINT32_C(0xFF000000)) >> 24);
}

static inline uint32_t bws_ntohl(
    uint32_t wartosc
) {
    return bws_htonl(wartosc);
}

#define HTONS(x) bws_htons((uint16_t)(x))
#define NTOHS(x) bws_ntohs((uint16_t)(x))
#define HTONL(x) bws_htonl((uint32_t)(x))
#define NTOHL(x) bws_ntohl((uint32_t)(x))

#endif

/* =========================================================================
 * 2. STALE PROTOKOLOWE
 * ========================================================================= */

#define SIEC_ETHERTYPE_IPV4 UINT16_C(0x0800)
#define SIEC_ETHERTYPE_ARP  UINT16_C(0x0806)

#define SIEC_IP_PROTO_ICMP UINT8_C(1)
#define SIEC_IP_PROTO_TCP  UINT8_C(6)
#define SIEC_IP_PROTO_UDP  UINT8_C(17)

#define SIEC_PORT_DHCP_SERWER UINT16_C(67)
#define SIEC_PORT_DHCP_KLIENT UINT16_C(68)
#define SIEC_PORT_DNS         UINT16_C(53)
#define SIEC_PORT_HTTP        UINT16_C(80)
#define SIEC_PORT_HTTPS       UINT16_C(443)

#define SIEC_MAKS_DNS_NAZWA UINT32_C(253)

/* =========================================================================
 * 3. FLAGI TCP
 * ========================================================================= */

#define TCP_FIN UINT8_C(0x01)
#define TCP_SYN UINT8_C(0x02)
#define TCP_RST UINT8_C(0x04)
#define TCP_PSH UINT8_C(0x08)
#define TCP_ACK UINT8_C(0x10)
#define TCP_URG UINT8_C(0x20)
#define TCP_ECE UINT8_C(0x40)
#define TCP_CWR UINT8_C(0x80)

/* =========================================================================
 * 4. WIRE-FORMAT: ETHERNET
 * ========================================================================= */

struct ethernet_header {
    uint8_t cel_mac[6];
    uint8_t zrodlo_mac[6];
    uint16_t typ;
} __attribute__((packed));

/* =========================================================================
 * 5. WIRE-FORMAT: ARP
 * ========================================================================= */

struct arp_header {
    uint16_t typ_sprzetu;
    uint16_t typ_protokolu;

    uint8_t dlugosc_mac;
    uint8_t dlugosc_ip;

    uint16_t operacja;

    uint8_t nadawca_mac[6];
    uint8_t nadawca_ip[4];

    uint8_t cel_mac[6];
    uint8_t cel_ip[4];
} __attribute__((packed));

/* =========================================================================
 * 6. WIRE-FORMAT: IPv4
 * ========================================================================= */

struct ipv4_header {
    uint8_t wersja_ihl;
    uint8_t tos;

    uint16_t dlugosc_calkowita;
    uint16_t id;
    uint16_t flagi_fragment;

    uint8_t ttl;
    uint8_t protokol;

    uint16_t suma_kontrolna;

    uint8_t zrodlo_ip[4];
    uint8_t cel_ip[4];
} __attribute__((packed));

/* =========================================================================
 * 7. WIRE-FORMAT: ICMP ECHO
 * ========================================================================= */

struct icmp_header {
    uint8_t typ;
    uint8_t kod;

    uint16_t suma_kontrolna;

    uint16_t id;
    uint16_t sekwencja;
} __attribute__((packed));

/* =========================================================================
 * 8. WIRE-FORMAT: UDP
 * ========================================================================= */

struct udp_header {
    uint16_t port_zrodlowy;
    uint16_t port_docelowy;

    uint16_t dlugosc;
    uint16_t suma_kontrolna;
} __attribute__((packed));

/* =========================================================================
 * 9. WIRE-FORMAT: DHCPv4 / BOOTP
 * ========================================================================= */

/*
 * BOOTP fixed header:
 *   236 bajtow
 *
 * + magic cookie:
 *   4 bajty
 *
 * + lokalna tablica opcji:
 *   64 bajty
 *
 * Razem ta pomocnicza struktura ma 304 bajty.
 *
 * Poprawiony siec.cpp NIE zaklada, ze odebrany DHCP ma dokladnie ten
 * rozmiar. Parser korzysta z rzeczywistej dlugosci UDP i TLV options.
 */
struct dhcp_header {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;

    uint32_t xid;

    uint16_t secs;
    uint16_t flags;

    uint8_t ciaddr[4];
    uint8_t yiaddr[4];
    uint8_t siaddr[4];
    uint8_t giaddr[4];

    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];

    uint8_t magic_cookie[4];

    uint8_t options[64];
} __attribute__((packed));

/* =========================================================================
 * 10. WIRE-FORMAT: DNS
 * ========================================================================= */

struct dns_header {
    uint16_t id;
    uint16_t flags;

    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

/* =========================================================================
 * 11. WIRE-FORMAT: TCP
 * ========================================================================= */

struct tcp_header {
    uint16_t port_zrodlowy;
    uint16_t port_docelowy;

    uint32_t numer_sekwencyjny;
    uint32_t numer_potwierdzenia;

    /*
     * Gorne 4 bity = data offset w 32-bitowych slowach.
     */
    uint8_t przesuniecie_danych;

    uint8_t flagi;

    uint16_t rozmiar_okna;
    uint16_t suma_kontrolna;
    uint16_t wazny_wskaznik;
} __attribute__((packed));

/*
 * Pseudo-header jest uzywany tylko do checksum TCP/UDP.
 * Nie jest samodzielnym naglowkiem wysylanym do Ethernetu.
 */
struct tcp_pseudo_header {
    uint8_t zrodlo_ip[4];
    uint8_t cel_ip[4];

    uint8_t zero;
    uint8_t protokol;

    uint16_t dlugosc_tcp;
} __attribute__((packed));

/* =========================================================================
 * 12. KONTROLA ABI WIRE-FORMAT
 * ========================================================================= */

#ifdef __cplusplus

static_assert(
    sizeof(ethernet_header) == 14,
    "Ethernet II header musi miec 14 bajtow"
);

static_assert(
    offsetof(ethernet_header, typ) == 12,
    "Nieprawidlowy offset Ethernet EtherType"
);

static_assert(
    sizeof(arp_header) == 28,
    "Ethernet/IPv4 ARP header musi miec 28 bajtow"
);

static_assert(
    offsetof(arp_header, nadawca_mac) == 8,
    "Nieprawidlowy offset ARP sender MAC"
);

static_assert(
    offsetof(arp_header, cel_ip) == 24,
    "Nieprawidlowy offset ARP target IPv4"
);

static_assert(
    sizeof(ipv4_header) == 20,
    "Minimalny IPv4 header musi miec 20 bajtow"
);

static_assert(
    offsetof(ipv4_header, suma_kontrolna) == 10,
    "Nieprawidlowy offset IPv4 checksum"
);

static_assert(
    offsetof(ipv4_header, zrodlo_ip) == 12,
    "Nieprawidlowy offset IPv4 source"
);

static_assert(
    offsetof(ipv4_header, cel_ip) == 16,
    "Nieprawidlowy offset IPv4 destination"
);

static_assert(
    sizeof(icmp_header) == 8,
    "ICMP Echo header musi miec 8 bajtow"
);

static_assert(
    sizeof(udp_header) == 8,
    "UDP header musi miec 8 bajtow"
);

static_assert(
    sizeof(dns_header) == 12,
    "DNS header musi miec 12 bajtow"
);

static_assert(
    sizeof(tcp_header) == 20,
    "Minimalny TCP header musi miec 20 bajtow"
);

static_assert(
    offsetof(tcp_header, numer_sekwencyjny) == 4,
    "Nieprawidlowy offset TCP SEQ"
);

static_assert(
    offsetof(tcp_header, przesuniecie_danych) == 12,
    "Nieprawidlowy offset TCP Data Offset"
);

static_assert(
    offsetof(tcp_header, suma_kontrolna) == 16,
    "Nieprawidlowy offset TCP checksum"
);

static_assert(
    sizeof(tcp_pseudo_header) == 12,
    "TCP pseudo-header musi miec 12 bajtow"
);

static_assert(
    offsetof(dhcp_header, xid) == 4,
    "Nieprawidlowy offset DHCP xid"
);

static_assert(
    offsetof(dhcp_header, yiaddr) == 16,
    "Nieprawidlowy offset DHCP yiaddr"
);

static_assert(
    offsetof(dhcp_header, chaddr) == 28,
    "Nieprawidlowy offset DHCP chaddr"
);

static_assert(
    offsetof(dhcp_header, magic_cookie) == 236,
    "DHCP magic cookie musi zaczynac sie pod offsetem 236"
);

static_assert(
    offsetof(dhcp_header, options) == 240,
    "DHCP options musza zaczynac sie pod offsetem 240"
);

static_assert(
    sizeof(dhcp_header) == 304,
    "Pomocniczy dhcp_header powinien miec 304 bajty"
);

#endif /* __cplusplus */

/* =========================================================================
 * 13. GLOBALNY STAN IPv4
 * ========================================================================= */

/*
 * Zachowane publicznie dla zgodnosci ze starszym kodem.
 *
 * Nowy kod powinien traktowac je jako read-only poza siec.cpp.
 */
extern uint8_t nasz_ip[4];
extern uint8_t brama_ip[4];

extern volatile bool odebrano_pong;

extern volatile bool dns_odebrano;
extern uint8_t dns_resolved_ip[4];

extern volatile bool tcp_dane_odebrane;

/*
 * Liczba bajtow rzeczywiscie zapisanych przez ostatni transfer HTTP/TCP.
 * syscalls.cpp wykorzystuje ja do bezpiecznego copy_to_user.
 */
extern uint32_t tcp_zapisano_bajtow;

/* =========================================================================
 * 14. NISKIE API STOSU
 * ========================================================================= */

/*
 * Internet checksum (RFC 1071).
 *
 * Funkcja historyczna zachowana dla kompatybilnosci.
 * Nowy siec.cpp liczy checksum bajtowo i nie wymaga wyrownania uint16_t.
 */
uint16_t oblicz_sume_kontrolna(
    void* dane,
    int bajty
);

/*
 * ARP cache.
 *
 * Parametry nie sa const dla zachowania obecnego ABI.
 */
bool szukaj_w_cache_arp(
    uint8_t ip[4],
    uint8_t wyjscie_mac[6]
);

void dodaj_do_cache_arp(
    uint8_t ip[4],
    uint8_t mac[6]
);

void wyslij_zapytanie_arp(
    uint8_t cel_ip[4]
);

bool rozwiaz_adres_mac(
    uint8_t cel_ip[4],
    uint8_t wyjscie_mac[6]
);

/*
 * Legacy/publiczny helper DHCP.
 * Typ:
 *   1 = DHCPDISCOVER
 *   3 = DHCPREQUEST
 */
void wyslij_pakiet_dhcp(
    uint8_t typ_wiadomosci,
    uint8_t* zapytanie_ip,
    uint8_t* serwer_ip
);

/*
 * Legacy/publiczny helper pojedynczej sesji TCP.
 *
 * Preferowane API dla nowych modulow:
 *   tcp_gniazdo_*()
 */
void wyslij_pakiet_tcp(
    uint8_t flagi,
    uint8_t* payload,
    uint16_t payload_len
);

/*
 * Punkt wejscia z odbiornika E1000.
 *
 * pakiet musi wskazywac pelna ramke Ethernet bez FCS.
 */
void obsluz_pakiet_sieciowy(
    uint8_t* pakiet,
    uint16_t dlugosc
);

/* =========================================================================
 * 15. API KERNELA / C LINKAGE
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Uruchamia prostego klienta DHCPv4.
 *
 * Po sukcesie siec.cpp ustawia:
 *   nasz_ip,
 *   brama_ip,
 *   maske podsieci (prywatnie),
 *   DNS (prywatnie).
 */
void uruchom_klienta_dhcp();

/*
 * ICMP Echo Request.
 *
 * Obecne ABI nie zwraca bool - wynik trafia do logu.
 */
void bws_siec_ping(
    uint8_t ip1,
    uint8_t ip2,
    uint8_t ip3,
    uint8_t ip4
);

/*
 * Resolver DNS A.
 *
 * wyjsciowy_ip:
 *   minimum 4 zapisywalne bajty.
 */
bool kernel_siec_dns(
    const char* domena,
    uint8_t* wyjsciowy_ip
);

/*
 * Pobiera surowa odpowiedz HTTP/1.0 do bufora kernela.
 *
 * tcp_zapisano_bajtow zawiera liczbe zapisanych bajtow.
 *
 * Zwraca false przy:
 *   - bledzie DNS/routingu/TCP,
 *   - RST,
 *   - timeout,
 *   - ucietym transferze z powodu zbyt malego bufora.
 */
bool kernel_siec_pobierz_http(
    uint8_t* cel_ip,
    const char* domena,
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
);

/*
 * HTTPS/TLS - implementowane poza siec.cpp.
 */
bool kernel_siec_pobierz_https(
    uint8_t cel_ip[4],
    const char* domena,
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
);

bool kernel_tls_certyfikat_zaufany();

/* =========================================================================
 * 16. API STRUMIENIA TCP DLA TLS
 * ========================================================================= */

/*
 * Jedna aktywna sesja TCP w obecnej implementacji.
 *
 * tcp_gniazdo_polacz:
 *   true  - handshake SYN/SYN-ACK/ACK zakonczony,
 *   false - brak konfiguracji, ARP, timeout, RST lub inna sesja zajeta.
 */
bool tcp_gniazdo_polacz(
    uint8_t* cel_ip,
    uint16_t port
);

/*
 * Zwraca:
 *   >=0 - liczba wyslanych bajtow,
 *   -1  - blad.
 *
 * Uwaga: obecny stos nie implementuje jeszcze pelnej retransmisji i
 * congestion control TCP.
 */
int tcp_gniazdo_wyslij(
    const uint8_t* dane,
    uint32_t dlugosc
);

/*
 * Zwraca:
 *   >0  - liczba odebranych bajtow,
 *    0  - peer zamknal polaczenie i bufor jest pusty,
 *   -1  - bledne argumenty/stan,
 *   -2  - polaczenie otwarte, ale obecnie brak danych.
 */
int tcp_gniazdo_odbierz(
    uint8_t* dane,
    uint32_t maksymalna_dlugosc
);

void tcp_gniazdo_zamknij();

bool tcp_gniazdo_otwarte();

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 * 17. LEKKIE HELPERY
 * ========================================================================= */

#ifdef __cplusplus

inline constexpr bool siec_ipv4_bajty_poprawne(
    const uint8_t ip[4]
) noexcept {
    return
        ip != nullptr;
}

inline constexpr bool siec_port_poprawny(
    uint16_t port
) noexcept {
    return
        port != 0;
}

inline constexpr bool siec_tcp_flaga_ustawiona(
    uint8_t flagi,
    uint8_t flaga
) noexcept {
    return
        (flagi &
         flaga) != 0;
}

#endif /* __cplusplus */
