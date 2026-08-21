/*
 * Bursztyn OS - stos sieciowy IPv4 dla Intel E1000
 *
 * Obslugiwane protokoly:
 *
 *   Ethernet II
 *   ARP
 *   IPv4 (bez fragmentacji)
 *   ICMP Echo
 *   UDP
 *   DHCPv4
 *   DNS A
 *   TCP - pojedyncza aktywna sesja
 *   HTTP/1.0 - transport dla BWS
 *
 * Ten plik celowo NIE implementuje TLS. Warstwa HTTPS/mbedTLS korzysta z:
 *
 *   tcp_gniazdo_polacz()
 *   tcp_gniazdo_wyslij()
 *   tcp_gniazdo_odbierz()
 *   tcp_gniazdo_otwarte()
 *   tcp_gniazdo_zamknij()
 *
 * Najwazniejsze zasady bezpieczenstwa:
 *
 *   - zadnego parsera nie wolno uruchomic przed sprawdzeniem dlugosci ramki,
 *   - wszystkie wielobajtowe pola wire-format sa odczytywane przez funkcje
 *     BE16/BE32 albo z packed struktur o zweryfikowanym rozmiarze,
 *   - fragmentowane IPv4 jest odrzucane, bo reassembly nie jest jeszcze
 *     zaimplementowane,
 *   - ARP cache przyjmuje tylko poprawne Ethernet/IPv4 ARP z sensownym MAC,
 *   - DNS sprawdza transaction ID, porty, serwer i wszystkie granice nazw,
 *   - DHCP parser sprawdza kazda opcje przed odczytem,
 *   - TCP sprawdza IP/port, numer sekwencyjny, rozmiar naglowka i checksum,
 *   - pojedynczy globalny TCP jest jawnie serializowany jako jedna sesja,
 *   - bufory odbiorcze nigdy nie sa zapisywane poza podanym limitem.
 *
 * Ograniczenia obecnej wersji:
 *
 *   - brak IPv6,
 *   - brak IP fragmentation/reassembly,
 *   - brak pelnego congestion control / retransmission TCP,
 *   - jedna aktywna sesja TCP naraz,
 *   - brak zegarowego TTL ARP cache,
 *   - polling E1000 zamiast kolejki IRQ/worker,
 *   - DHCP nie implementuje jeszcze renew/rebind lease.
 *
 * Te ograniczenia sa jawne i fail-closed zamiast probowac obslugiwac
 * niepelne przypadki w sposob ryzykowny.
 */

#include "siec.h"
#include "e1000.h"
#include "sterowniki/czas/hpet.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef BURSZTYN_DEBUG_DNS_RAW
#define BURSZTYN_DEBUG_DNS_RAW 0
#endif

/* =========================================================================
 * 1. API ZEWNĘTRZNE
 * ========================================================================= */

void wypisz_log(
    const char* tekst
);

extern "C" void e1000_obsluz_odbior();

/* =========================================================================
 * 2. STALE PROTOKOLOW
 * ========================================================================= */

namespace {

constexpr size_t ETH_LEN =
    14;

constexpr size_t ARP_LEN =
    28;

constexpr size_t IPV4_MIN_LEN =
    20;

constexpr size_t UDP_LEN =
    8;

constexpr size_t TCP_MIN_LEN =
    20;

constexpr size_t ICMP_ECHO_LEN =
    8;

constexpr uint16_t ETHERTYPE_IPV4 =
    0x0800U;

constexpr uint16_t ETHERTYPE_ARP =
    0x0806U;

constexpr uint8_t IP_PROTO_ICMP =
    1U;

constexpr uint8_t IP_PROTO_TCP =
    6U;

constexpr uint8_t IP_PROTO_UDP =
    17U;

constexpr uint16_t PORT_DHCP_SERVER =
    67U;

constexpr uint16_t PORT_DHCP_CLIENT =
    68U;

constexpr uint16_t PORT_DNS =
    53U;

constexpr uint16_t PORT_HTTP =
    80U;

constexpr uint16_t PORT_EPHEMERAL_MIN =
    49152U;

constexpr uint16_t PORT_EPHEMERAL_START =
    50000U;

constexpr uint16_t PORT_DNS_START =
    53000U;

constexpr uint8_t TCP_FLAG_FIN =
    0x01U;

constexpr uint8_t TCP_FLAG_SYN =
    0x02U;

constexpr uint8_t TCP_FLAG_RST =
    0x04U;

constexpr uint8_t TCP_FLAG_PSH =
    0x08U;

constexpr uint8_t TCP_FLAG_ACK =
    0x10U;

constexpr uint16_t TCP_OKNO =
    8192U;

constexpr uint16_t TCP_MSS_WYSYLANIA =
    1400U;

constexpr size_t MAKS_RAMKA_ETH =
    1514U;

constexpr size_t MAKS_PAKIET_IPV4 =
    1500U;

constexpr uint64_t ARP_TIMEOUT_MS = 2000;
constexpr uint64_t DHCP_TIMEOUT_MS = 8000;
constexpr uint64_t PING_TIMEOUT_MS = 4000;
constexpr uint64_t DNS_TIMEOUT_MS = 4000;
constexpr uint64_t TCP_CONNECT_TIMEOUT_MS = 5000;
constexpr uint64_t HTTP_TIMEOUT_MS = 12000;

uint64_t deadline_ms(uint64_t odstep) {
    const uint64_t teraz = czas_monotoniczny_ms();
    return teraz > UINT64_MAX - odstep ? UINT64_MAX : teraz + odstep;
}

constexpr size_t ROZMIAR_TABLICY_ARP =
    16;

constexpr size_t TCP_BUFOR_GNIAZDA =
    64U * 1024U;

constexpr size_t HTTP_REQUEST_MAX =
    4096U;

constexpr size_t DNS_PACKET_MAX =
    512U;

constexpr size_t DNS_NAZWA_MAX =
    253U;

constexpr size_t DNS_LABEL_MAX =
    63U;

constexpr size_t DHCP_PACKET_MAX =
    548U;

constexpr size_t DHCP_BOOTP_FIXED =
    236U;

constexpr size_t DHCP_COOKIE_OFFSET =
    236U;

constexpr size_t DHCP_OPTIONS_OFFSET =
    240U;

constexpr uint32_t DHCP_MAGIC_COOKIE =
    UINT32_C(0x63825363);

constexpr uint8_t DHCP_DISCOVER =
    1U;

constexpr uint8_t DHCP_OFFER =
    2U;

constexpr uint8_t DHCP_REQUEST =
    3U;

constexpr uint8_t DHCP_ACK =
    5U;

constexpr uint8_t DHCP_NAK =
    6U;

constexpr uint8_t DHCP_OPT_PAD =
    0U;

constexpr uint8_t DHCP_OPT_SUBNET =
    1U;

constexpr uint8_t DHCP_OPT_ROUTER =
    3U;

constexpr uint8_t DHCP_OPT_DNS =
    6U;

constexpr uint8_t DHCP_OPT_REQ_IP =
    50U;

constexpr uint8_t DHCP_OPT_MSG_TYPE =
    53U;

constexpr uint8_t DHCP_OPT_SERVER_ID =
    54U;

constexpr uint8_t DHCP_OPT_PARAM_REQ =
    55U;

constexpr uint8_t DHCP_OPT_END =
    255U;

constexpr uint16_t ICMP_ECHO_ID =
    0xB055U;

constexpr uint16_t ICMP_ECHO_SEQ =
    1U;

/* =========================================================================
 * 3. WIRE STRUCTURES
 * ========================================================================= */

struct EthNaglowek {
    uint8_t cel_mac[6];
    uint8_t zrodlo_mac[6];
    uint16_t typ;
} __attribute__((packed));

struct ArpNaglowek {
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

struct Ipv4Naglowek {
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

struct UdpNaglowek {
    uint16_t port_zrodlowy;
    uint16_t port_docelowy;
    uint16_t dlugosc;
    uint16_t suma_kontrolna;
} __attribute__((packed));

struct TcpNaglowek {
    uint16_t port_zrodlowy;
    uint16_t port_docelowy;
    uint32_t numer_sekwencyjny;
    uint32_t numer_potwierdzenia;
    uint8_t przesuniecie_danych;
    uint8_t flagi;
    uint16_t rozmiar_okna;
    uint16_t suma_kontrolna;
    uint16_t wazny_wskaznik;
} __attribute__((packed));

struct IcmpEchoNaglowek {
    uint8_t typ;
    uint8_t kod;
    uint16_t suma_kontrolna;
    uint16_t id;
    uint16_t sekwencja;
} __attribute__((packed));

static_assert(
    sizeof(EthNaglowek) == ETH_LEN,
    "Ethernet II header musi miec 14 bajtow"
);

static_assert(
    sizeof(ArpNaglowek) == ARP_LEN,
    "Ethernet/IPv4 ARP header musi miec 28 bajtow"
);

static_assert(
    sizeof(Ipv4Naglowek) == IPV4_MIN_LEN,
    "Minimalny IPv4 header musi miec 20 bajtow"
);

static_assert(
    sizeof(UdpNaglowek) == UDP_LEN,
    "UDP header musi miec 8 bajtow"
);

static_assert(
    sizeof(TcpNaglowek) == TCP_MIN_LEN,
    "Minimalny TCP header musi miec 20 bajtow"
);

static_assert(
    sizeof(IcmpEchoNaglowek) == ICMP_ECHO_LEN,
    "ICMP Echo header musi miec 8 bajtow"
);

/* =========================================================================
 * 4. BYTE ORDER / BEZPIECZNE ODCZYTY
 * ========================================================================= */

constexpr uint16_t zamien16(
    uint16_t v
) {
    return
        static_cast<uint16_t>(
            (v << 8) |
            (v >> 8)
        );
}

constexpr uint32_t zamien32(
    uint32_t v
) {
    return
        ((v & UINT32_C(0x000000FF)) << 24) |
        ((v & UINT32_C(0x0000FF00)) << 8)  |
        ((v & UINT32_C(0x00FF0000)) >> 8)  |
        ((v & UINT32_C(0xFF000000)) >> 24);
}

uint16_t host_na_siec16(
    uint16_t v
) {
    return zamien16(v);
}

uint16_t siec_na_host16(
    uint16_t v
) {
    return zamien16(v);
}

uint32_t host_na_siec32(
    uint32_t v
) {
    return zamien32(v);
}

uint32_t siec_na_host32(
    uint32_t v
) {
    return zamien32(v);
}

uint16_t czytaj_be16(
    const uint8_t* p
) {
    if (!p) {
        return 0;
    }

    return
        static_cast<uint16_t>(
            (static_cast<uint16_t>(
                 p[0]) << 8) |
            static_cast<uint16_t>(
                p[1])
        );
}

uint32_t czytaj_be32(
    const uint8_t* p
) {
    if (!p) {
        return 0;
    }

    return
        (static_cast<uint32_t>(
             p[0]) << 24) |
        (static_cast<uint32_t>(
             p[1]) << 16) |
        (static_cast<uint32_t>(
             p[2]) << 8) |
        static_cast<uint32_t>(
            p[3]);
}

void zapisz_be16(
    uint8_t* p,
    uint16_t v
) {
    if (!p) {
        return;
    }

    p[0] =
        static_cast<uint8_t>(
            v >> 8
        );

    p[1] =
        static_cast<uint8_t>(
            v &
            0xFFU
        );
}

void zapisz_be32(
    uint8_t* p,
    uint32_t v
) {
    if (!p) {
        return;
    }

    p[0] =
        static_cast<uint8_t>(
            v >> 24
        );

    p[1] =
        static_cast<uint8_t>(
            (v >> 16) &
            0xFFU
        );

    p[2] =
        static_cast<uint8_t>(
            (v >> 8) &
            0xFFU
        );

    p[3] =
        static_cast<uint8_t>(
            v &
            0xFFU
        );
}

/* =========================================================================
 * 5. PROSTE OPERACJE PAMIECIOWE / TEKSTOWE
 * ========================================================================= */

void wyzeruj(
    void* ptr,
    size_t rozmiar
) {
    if (!ptr) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(
            ptr
        );

    for (size_t i = 0;
         i < rozmiar;
         ++i) {

        p[i] = 0;
    }
}

void kopiuj_bajty(
    void* cel,
    const void* zrodlo,
    size_t rozmiar
) {
    if (!cel ||
        !zrodlo) {

        return;
    }

    uint8_t* d =
        static_cast<uint8_t*>(
            cel
        );

    const uint8_t* s =
        static_cast<const uint8_t*>(
            zrodlo
        );

    for (size_t i = 0;
         i < rozmiar;
         ++i) {

        d[i] = s[i];
    }
}

void skopiuj_mac(
    uint8_t cel[6],
    const uint8_t zrodlo[6]
) {
    kopiuj_bajty(
        cel,
        zrodlo,
        6
    );
}

void skopiuj_ip(
    uint8_t cel[4],
    const uint8_t zrodlo[4]
) {
    kopiuj_bajty(
        cel,
        zrodlo,
        4
    );
}

bool ip_rowne(
    const uint8_t a[4],
    const uint8_t b[4]
) {
    if (!a ||
        !b) {

        return false;
    }

    return
        a[0] == b[0] &&
        a[1] == b[1] &&
        a[2] == b[2] &&
        a[3] == b[3];
}

bool mac_rowne(
    const uint8_t a[6],
    const uint8_t b[6]
) {
    if (!a ||
        !b) {

        return false;
    }

    for (size_t i = 0;
         i < 6;
         ++i) {

        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

bool ip_jest_zero(
    const uint8_t ip[4]
) {
    if (!ip) {
        return true;
    }

    return
        ip[0] == 0 &&
        ip[1] == 0 &&
        ip[2] == 0 &&
        ip[3] == 0;
}

bool ip_jest_broadcast(
    const uint8_t ip[4]
) {
    if (!ip) {
        return false;
    }

    return
        ip[0] == 255 &&
        ip[1] == 255 &&
        ip[2] == 255 &&
        ip[3] == 255;
}

bool ip_jest_multicast(
    const uint8_t ip[4]
) {
    if (!ip) {
        return false;
    }

    return
        ip[0] >= 224U &&
        ip[0] <= 239U;
}

bool mac_jest_broadcast(
    const uint8_t mac[6]
) {
    if (!mac) {
        return false;
    }

    for (size_t i = 0;
         i < 6;
         ++i) {

        if (mac[i] != 0xFFU) {
            return false;
        }
    }

    return true;
}

bool mac_jest_poprawny(
    const uint8_t mac[6]
) {
    if (!mac) {
        return false;
    }

    uint8_t suma =
        0;

    for (size_t i = 0;
         i < 6;
         ++i) {

        suma |=
            mac[i];
    }

    if (suma == 0 ||
        mac_jest_broadcast(
            mac)) {

        return false;
    }

    /*
     * ARP cache przechowuje tylko unicast Ethernet.
     */
    if ((mac[0] &
         0x01U) != 0) {

        return false;
    }

    return true;
}

size_t dlugosc_tekstu_limit(
    const char* tekst,
    size_t limit
) {
    if (!tekst) {
        return limit;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            return i;
        }
    }

    return limit;
}

bool dopisz_tekst(
    char* cel,
    size_t pojemnosc,
    size_t* pozycja,
    const char* tekst
) {
    if (!cel ||
        !pozycja ||
        !tekst ||
        *pozycja >=
            pojemnosc) {

        return false;
    }

    size_t p =
        *pozycja;

    for (size_t i = 0;
         tekst[i] != '\0';
         ++i) {

        if (p + 1 >=
            pojemnosc) {

            return false;
        }

        cel[p++] =
            tekst[i];
    }

    cel[p] = '\0';
    *pozycja = p;

    return true;
}

void uint_do_str(
    uint32_t wartosc,
    char* bufor,
    size_t pojemnosc
) {
    if (!bufor ||
        pojemnosc == 0) {

        return;
    }

    if (wartosc == 0) {
        if (pojemnosc >= 2) {
            bufor[0] = '0';
            bufor[1] = '\0';
        } else {
            bufor[0] = '\0';
        }

        return;
    }

    char odwrotnie[16] = {};
    size_t n = 0;

    while (wartosc != 0 &&
           n < sizeof(odwrotnie)) {

        odwrotnie[n++] =
            static_cast<char>(
                '0' +
                wartosc % 10U
            );

        wartosc /=
            10U;
    }

    if (n + 1 >
        pojemnosc) {

        bufor[0] = '\0';
        return;
    }

    size_t out =
        0;

    while (n > 0) {
        bufor[out++] =
            odwrotnie[--n];
    }

    bufor[out] = '\0';
}

void wypisz_ip_log(
    const char* prefix,
    const uint8_t ip[4]
) {
    if (!prefix ||
        !ip) {

        return;
    }

    char log[128] = {};
    size_t p = 0;

    if (!dopisz_tekst(
            log,
            sizeof(log),
            &p,
            prefix)) {

        return;
    }

    for (size_t i = 0;
         i < 4;
         ++i) {

        char liczba[4] = {};

        uint_do_str(
            ip[i],
            liczba,
            sizeof(liczba)
        );

        if (!dopisz_tekst(
                log,
                sizeof(log),
                &p,
                liczba)) {

            return;
        }

        if (i != 3) {
            if (!dopisz_tekst(
                    log,
                    sizeof(log),
                    &p,
                    ".")) {

                return;
            }
        }
    }

    wypisz_log(
        log
    );
}

#if BURSZTYN_DEBUG_DNS_RAW
void wypisz_dns_u16(const char* prefix, uint16_t wartosc) {
    char log[96] = {};
    char liczba[8] = {};
    size_t p = 0;
    uint_do_str(wartosc, liczba, sizeof(liczba));
    if (dopisz_tekst(log, sizeof(log), &p, prefix) &&
        dopisz_tekst(log, sizeof(log), &p, liczba)) wypisz_log(log);
}

void wypisz_dns_u32(const char* prefix, uint32_t wartosc) {
    char log[96] = {};
    char liczba[16] = {};
    size_t p = 0;
    uint_do_str(wartosc, liczba, sizeof(liczba));
    if (dopisz_tekst(log, sizeof(log), &p, prefix) &&
        dopisz_tekst(log, sizeof(log), &p, liczba)) wypisz_log(log);
}

void wypisz_dns_hexdump(const uint8_t* dane, size_t dlugosc) {
    static const char hex[] = "0123456789ABCDEF";
    if (!dane) return;
    if (dlugosc > DNS_PACKET_MAX) dlugosc = DNS_PACKET_MAX;
    for (size_t off = 0; off < dlugosc; off += 16U) {
        char linia[64] = "[DNS-RAW] ";
        size_t p = 10U;
        const size_t koniec = off + 16U < dlugosc ? off + 16U : dlugosc;
        for (size_t i = off; i < koniec && p + 3U < sizeof(linia); ++i) {
            linia[p++] = hex[(dane[i] >> 4U) & 0x0FU];
            linia[p++] = hex[dane[i] & 0x0FU];
            linia[p++] = ' ';
        }
        linia[p] = '\0';
        wypisz_log(linia);
    }
}
#endif

/* =========================================================================
 * 6. INTERNET CHECKSUM
 * ========================================================================= */

uint32_t suma_dodaj_bajty(
    uint32_t suma,
    const uint8_t* dane,
    size_t dlugosc
) {
    if (!dane) {
        return suma;
    }

    size_t i = 0;

    while (i + 1 <
           dlugosc) {

        suma +=
            (static_cast<uint32_t>(
                 dane[i]) << 8) |
            static_cast<uint32_t>(
                dane[i + 1]
            );

        i += 2;
    }

    if (i <
        dlugosc) {

        suma +=
            static_cast<uint32_t>(
                dane[i]
            ) << 8;
    }

    return suma;
}

uint16_t suma_zakoncz(
    uint32_t suma
) {
    while ((suma >>
            16) != 0) {

        suma =
            (suma &
             0xFFFFU) +
            (suma >>
             16);
    }

    return
        static_cast<uint16_t>(
            ~suma &
            0xFFFFU
        );
}

uint16_t suma_kontrolna_bajty(
    const void* dane,
    size_t dlugosc
) {
    if (!dane) {
        return 0;
    }

    return
        suma_zakoncz(
            suma_dodaj_bajty(
                0,
                static_cast<const uint8_t*>(
                    dane
                ),
                dlugosc
            )
        );
}

uint16_t suma_transport_ipv4(
    const uint8_t zrodlo_ip[4],
    const uint8_t cel_ip[4],
    uint8_t protokol,
    const uint8_t* segment,
    uint16_t dlugosc
) {
    uint32_t suma =
        0;

    suma =
        suma_dodaj_bajty(
            suma,
            zrodlo_ip,
            4
        );

    suma =
        suma_dodaj_bajty(
            suma,
            cel_ip,
            4
        );

    suma +=
        static_cast<uint32_t>(
            protokol
        );

    suma +=
        static_cast<uint32_t>(
            dlugosc
        );

    suma =
        suma_dodaj_bajty(
            suma,
            segment,
            dlugosc
        );

    uint16_t wynik =
        suma_zakoncz(
            suma
        );

    /*
     * W UDP wartosc zero jest kodowana jako 0xFFFF.
     */
    if (wynik == 0 &&
        protokol ==
            IP_PROTO_UDP) {

        wynik =
            0xFFFFU;
    }

    return wynik;
}

/* Publiczna sygnatura zachowana dla zgodnosci z siec.h/starym kodem. */
} // namespace

uint16_t oblicz_sume_kontrolna(
    void* dane,
    int bajty
) {
    if (!dane ||
        bajty <= 0) {

        return 0;
    }

    return
        suma_kontrolna_bajty(
            dane,
            static_cast<size_t>(
                bajty
            )
        );
}

namespace {

/* =========================================================================
 * 7. GLOBALNA KONFIGURACJA IPv4
 * ========================================================================= */

} // namespace

uint8_t nasz_ip[4] = {
    0, 0, 0, 0
};

uint8_t brama_ip[4] = {
    0, 0, 0, 0
};

volatile uint64_t ipv4_rx_packets = 0;
volatile uint64_t udp_rx_packets = 0;
volatile uint64_t dns_rx_packets = 0;
volatile uint64_t arp_rx_packets = 0;
volatile uint64_t icmp_rx_packets = 0;

namespace {

uint8_t maska_podsieci[4] = {
    255, 255, 255, 0
};

uint8_t serwer_dns_ip[4] = {
    8, 8, 8, 8
};

bool konfiguracja_ipv4_gotowa =
    false;

uint16_t kolejny_ipv4_id =
    1;

uint16_t nastepny_port_ephemeral =
    PORT_EPHEMERAL_START;

uint16_t nastepny_port_dns =
    PORT_DNS_START;

uint16_t nastepny_dns_id =
    0xA001U;

uint32_t dhcp_xid =
    UINT32_C(0xB0550001);

uint8_t dhcp_offer_ip[4] = {};
uint8_t dhcp_offer_serwer[4] = {};
uint8_t dhcp_offer_maska[4] = {};
uint8_t dhcp_offer_brama[4] = {};
uint8_t dhcp_offer_dns[4] = {};

bool dhcp_otrzymano_offer =
    false;

bool dhcp_otrzymano_ack =
    false;

bool dhcp_otrzymano_nak =
    false;

/* =========================================================================
 * 8. ARP CACHE
 * ========================================================================= */

struct ArpWpis {
    uint8_t ip[4];
    uint8_t mac[6];
    bool aktywny;
    uint32_t generacja;
};

ArpWpis tablica_arp[
    ROZMIAR_TABLICY_ARP
] = {};

uint32_t generacja_arp =
    1;

size_t indeks_zastepowania_arp =
    0;

bool arp_ip_poprawne(
    const uint8_t ip[4]
) {
    if (!ip ||
        ip_jest_zero(ip) ||
        ip_jest_broadcast(ip) ||
        ip_jest_multicast(ip)) {

        return false;
    }

    return true;
}

void wyczysc_cache_arp() {
    for (size_t i = 0;
         i <
            ROZMIAR_TABLICY_ARP;
         ++i) {

        tablica_arp[i] =
            {};
    }

    generacja_arp =
        1;

    indeks_zastepowania_arp =
        0;
}

bool ip_w_naszej_podsieci(
    const uint8_t ip[4]
) {
    if (!ip) {
        return false;
    }

    for (size_t i = 0;
         i < 4;
         ++i) {

        if ((ip[i] &
             maska_podsieci[i]) !=
            (nasz_ip[i] &
             maska_podsieci[i])) {

            return false;
        }
    }

    return true;
}

uint16_t przydziel_port(
    uint16_t* licznik,
    uint16_t poczatek
) {
    if (!licznik) {
        return poczatek;
    }

    uint16_t wynik =
        *licznik;

    ++(*licznik);

    if (*licznik <
            PORT_EPHEMERAL_MIN ||
        *licznik == 0) {

        *licznik =
            poczatek;
    }

    if (wynik <
        PORT_EPHEMERAL_MIN) {

        wynik =
            poczatek;
    }

    return wynik;
}

uint16_t przydziel_ipv4_id() {
    const uint16_t id =
        kolejny_ipv4_id++;

    if (kolejny_ipv4_id == 0) {
        kolejny_ipv4_id =
            1;
    }

    return id;
}

/* =========================================================================
 * 9. PUBLICZNY ARP CACHE
 * ========================================================================= */

} // namespace

bool szukaj_w_cache_arp(
    uint8_t ip[4],
    uint8_t wyjscie_mac[6]
) {
    if (!ip ||
        !wyjscie_mac) {

        return false;
    }

    for (size_t i = 0;
         i <
            ROZMIAR_TABLICY_ARP;
         ++i) {

        ArpWpis& wpis =
            tablica_arp[i];

        if (!wpis.aktywny ||
            !ip_rowne(
                wpis.ip,
                ip)) {

            continue;
        }

        if (!mac_jest_poprawny(
                wpis.mac)) {

            wpis.aktywny =
                false;

            continue;
        }

        skopiuj_mac(
            wyjscie_mac,
            wpis.mac
        );

        wpis.generacja =
            generacja_arp++;

        return true;
    }

    return false;
}

void dodaj_do_cache_arp(
    uint8_t ip[4],
    uint8_t mac[6]
) {
    if (!arp_ip_poprawne(
            ip) ||
        !mac_jest_poprawny(
            mac)) {

        return;
    }

    for (size_t i = 0;
         i <
            ROZMIAR_TABLICY_ARP;
         ++i) {

        ArpWpis& wpis =
            tablica_arp[i];

        if (wpis.aktywny &&
            ip_rowne(
                wpis.ip,
                ip)) {

            skopiuj_mac(
                wpis.mac,
                mac
            );

            wpis.generacja =
                generacja_arp++;

            return;
        }
    }

    for (size_t i = 0;
         i <
            ROZMIAR_TABLICY_ARP;
         ++i) {

        ArpWpis& wpis =
            tablica_arp[i];

        if (!wpis.aktywny) {
            skopiuj_ip(
                wpis.ip,
                ip
            );

            skopiuj_mac(
                wpis.mac,
                mac
            );

            wpis.aktywny =
                true;

            wpis.generacja =
                generacja_arp++;

            return;
        }
    }

    /*
     * Pelna tabela: prosty round-robin. Nie udajemy TTL bez stabilnego
     * zegara monotonicznego.
     */
    ArpWpis& wpis =
        tablica_arp[
            indeks_zastepowania_arp
        ];

    indeks_zastepowania_arp =
        (indeks_zastepowania_arp + 1) %
        ROZMIAR_TABLICY_ARP;

    skopiuj_ip(
        wpis.ip,
        ip
    );

    skopiuj_mac(
        wpis.mac,
        mac
    );

    wpis.aktywny =
        true;

    wpis.generacja =
        generacja_arp++;
}

namespace {

/* =========================================================================
 * 10. WYSYLANIE ETHERNET/IP
 * ========================================================================= */

bool mac_karty_gotowy() {
    return
        mac_jest_poprawny(
            pobierz_mac_adres()
        );
}

void wypelnij_eth(
    EthNaglowek* eth,
    const uint8_t cel_mac[6],
    uint16_t typ
) {
    if (!eth ||
        !cel_mac) {

        return;
    }

    skopiuj_mac(
        eth->cel_mac,
        cel_mac
    );

    skopiuj_mac(
        eth->zrodlo_mac,
        pobierz_mac_adres()
    );

    eth->typ =
        host_na_siec16(
            typ
        );
}

bool wypelnij_ipv4(
    Ipv4Naglowek* ip,
    uint16_t calkowita_dlugosc,
    uint8_t protokol,
    const uint8_t zrodlo[4],
    const uint8_t cel[4]
) {
    if (!ip ||
        !zrodlo ||
        !cel ||
        calkowita_dlugosc <
            IPV4_MIN_LEN) {

        return false;
    }

    wyzeruj(
        ip,
        sizeof(*ip)
    );

    ip->wersja_ihl =
        0x45U;

    ip->tos =
        0;

    ip->dlugosc_calkowita =
        host_na_siec16(
            calkowita_dlugosc
        );

    ip->id =
        host_na_siec16(
            przydziel_ipv4_id()
        );

    /*
     * DF=1. Nie implementujemy fragmentacji ani reassembly.
     */
    ip->flagi_fragment =
        host_na_siec16(
            0x4000U
        );

    ip->ttl =
        64U;

    ip->protokol =
        protokol;

    skopiuj_ip(
        ip->zrodlo_ip,
        zrodlo
    );

    skopiuj_ip(
        ip->cel_ip,
        cel
    );

    ip->suma_kontrolna =
        0;

    ip->suma_kontrolna =
        host_na_siec16(
            suma_kontrolna_bajty(
                ip,
                IPV4_MIN_LEN
            )
        );

    return true;
}

bool wyslij_ethernet(
    uint8_t* ramka,
    size_t dlugosc
) {
    if (!ramka ||
        dlugosc <
            ETH_LEN ||
        dlugosc >
            MAKS_RAMKA_ETH ||
        !mac_karty_gotowy()) {

        return false;
    }

    /*
     * Ethernet wymaga min. 60 bajtow bez FCS. E1000 dopisuje FCS.
     */
    if (dlugosc < 60U) {
        for (size_t i = dlugosc;
             i < 60U;
             ++i) {

            ramka[i] = 0;
        }

        dlugosc =
            60U;
    }

    const bool wyslano = e1000_wyslij_pakiet(
        ramka,
        static_cast<uint16_t>(
            dlugosc
        )
    );

    return wyslano;
}

/* =========================================================================
 * 11. ARP REQUEST/REPLY
 * ========================================================================= */

void zbuduj_i_wyslij_arp(
    uint16_t operacja,
    const uint8_t cel_mac_eth[6],
    const uint8_t nadawca_ip[4],
    const uint8_t cel_mac_arp[6],
    const uint8_t cel_ip[4]
) {
    if (!cel_mac_eth ||
        !nadawca_ip ||
        !cel_mac_arp ||
        !cel_ip) {

        return;
    }

    uint8_t ramka[64] = {};

    EthNaglowek* eth =
        reinterpret_cast<EthNaglowek*>(
            ramka
        );

    wypelnij_eth(
        eth,
        cel_mac_eth,
        ETHERTYPE_ARP
    );

    ArpNaglowek* arp =
        reinterpret_cast<ArpNaglowek*>(
            ramka +
            ETH_LEN
        );

    arp->typ_sprzetu =
        host_na_siec16(
            1
        );

    arp->typ_protokolu =
        host_na_siec16(
            ETHERTYPE_IPV4
        );

    arp->dlugosc_mac =
        6;

    arp->dlugosc_ip =
        4;

    arp->operacja =
        host_na_siec16(
            operacja
        );

    skopiuj_mac(
        arp->nadawca_mac,
        pobierz_mac_adres()
    );

    skopiuj_ip(
        arp->nadawca_ip,
        nadawca_ip
    );

    skopiuj_mac(
        arp->cel_mac,
        cel_mac_arp
    );

    skopiuj_ip(
        arp->cel_ip,
        cel_ip
    );

    (void)wyslij_ethernet(
        ramka,
        ETH_LEN +
            ARP_LEN
    );
}

} // namespace

void wyslij_zapytanie_arp(
    uint8_t cel_ip[4]
) {
    if (!cel_ip ||
        !konfiguracja_ipv4_gotowa ||
        !arp_ip_poprawne(
            cel_ip)) {

        return;
    }

    static const uint8_t broadcast_mac[6] = {
        0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF
    };

    static const uint8_t pusty_mac[6] = {
        0, 0, 0, 0, 0, 0
    };

    zbuduj_i_wyslij_arp(
        1,
        broadcast_mac,
        nasz_ip,
        pusty_mac,
        cel_ip
    );
}

bool rozwiaz_adres_mac(
    uint8_t cel_ip[4],
    uint8_t wyjscie_mac[6]
) {
    if (!cel_ip ||
        !wyjscie_mac ||
        !konfiguracja_ipv4_gotowa ||
        !mac_karty_gotowy()) {

        return false;
    }

    /* 127/8 jest przestrzenia loopback i nigdy nie moze trafic do E1000. */
    if (cel_ip[0] == 127U) {
        wypisz_log("[ROUTE] result=NET_ERR_LOOPBACK_UNSUPPORTED");
        return false;
    }

    if (ip_rowne(
            cel_ip,
            nasz_ip)) {

        skopiuj_mac(
            wyjscie_mac,
            pobierz_mac_adres()
        );

        return true;
    }

    if (ip_jest_broadcast(
            cel_ip)) {

        for (size_t i = 0;
             i < 6;
             ++i) {

            wyjscie_mac[i] =
                0xFFU;
        }

        return true;
    }

    if (ip_jest_multicast(
            cel_ip) ||
        ip_jest_zero(
            cel_ip)) {

        return false;
    }

    uint8_t nastepny_hop[4] = {};

    if (ip_w_naszej_podsieci(
            cel_ip)) {

        skopiuj_ip(
            nastepny_hop,
            cel_ip
        );
        wypisz_log("[ROUTE] local subnet");
    } else {
        if (ip_jest_zero(
                brama_ip)) {

            return false;
        }

        skopiuj_ip(
            nastepny_hop,
            brama_ip
        );
        wypisz_log("[ROUTE] via gateway");
    }
    wypisz_ip_log("[ROUTE] next_hop=", nastepny_hop);

    if (szukaj_w_cache_arp(
            nastepny_hop,
            wyjscie_mac)) {
        wypisz_ip_log("[ARP] cache hit ip=", nastepny_hop);
        return true;
    }

    wypisz_ip_log("[ARP] cache miss ip=", nastepny_hop);
    wypisz_ip_log("[ARP] request ip=", nastepny_hop);

    wyslij_zapytanie_arp(
        nastepny_hop
    );

    const uint64_t arp_deadline = deadline_ms(ARP_TIMEOUT_MS);
    while (czas_monotoniczny_ms() < arp_deadline) {

        e1000_obsluz_odbior();

        if (szukaj_w_cache_arp(
                nastepny_hop,
                wyjscie_mac)) {

            wypisz_ip_log("[ARP] resolved ip=", nastepny_hop);
            return true;
        }

        asm volatile(
            "pause"
        );
    }

    wypisz_ip_log("[ARP] timeout ip=", nastepny_hop);
    return false;
}

namespace {

/* =========================================================================
 * 12. DHCP
 * ========================================================================= */

struct OpcjeDHCP {
    uint8_t typ_wiadomosci;

    bool ma_maske;
    uint8_t maska[4];

    bool ma_brame;
    uint8_t brama[4];

    bool ma_dns;
    uint8_t dns[4];

    bool ma_serwer;
    uint8_t serwer[4];
};

bool dhcp_dodaj_opcje(
    uint8_t* bufor,
    size_t pojemnosc,
    size_t* pos,
    uint8_t kod,
    const uint8_t* dane,
    uint8_t dlugosc
) {
    if (!bufor ||
        !pos ||
        !dane) {

        return false;
    }

    if (*pos >
            pojemnosc ||
        static_cast<size_t>(
            dlugosc) + 2U >
            pojemnosc -
                *pos) {

        return false;
    }

    bufor[
        (*pos)++] =
        kod;

    bufor[
        (*pos)++] =
        dlugosc;

    for (uint8_t i = 0;
         i < dlugosc;
         ++i) {

        bufor[
            (*pos)++] =
            dane[i];
    }

    return true;
}

bool parsuj_opcje_dhcp(
    const uint8_t* opcje,
    size_t dlugosc,
    OpcjeDHCP* wynik
) {
    if (!opcje ||
        !wynik) {

        return false;
    }

    *wynik =
        {};

    size_t pos =
        0;

    while (pos <
           dlugosc) {

        const uint8_t kod =
            opcje[pos++];

        if (kod ==
            DHCP_OPT_PAD) {

            continue;
        }

        if (kod ==
            DHCP_OPT_END) {

            return true;
        }

        if (pos >=
            dlugosc) {

            return false;
        }

        const uint8_t len =
            opcje[pos++];

        if (static_cast<size_t>(
                len) >
            dlugosc -
                pos) {

            return false;
        }

        const uint8_t* dane =
            opcje +
            pos;

        switch (kod) {
            case DHCP_OPT_MSG_TYPE:
                if (len == 1) {
                    wynik->typ_wiadomosci =
                        dane[0];
                }
                break;

            case DHCP_OPT_SUBNET:
                if (len >= 4) {
                    wynik->ma_maske =
                        true;

                    skopiuj_ip(
                        wynik->maska,
                        dane
                    );
                }
                break;

            case DHCP_OPT_ROUTER:
                if (len >= 4) {
                    wynik->ma_brame =
                        true;

                    skopiuj_ip(
                        wynik->brama,
                        dane
                    );
                }
                break;

            case DHCP_OPT_DNS:
                if (len >= 4) {
                    wynik->ma_dns =
                        true;

                    skopiuj_ip(
                        wynik->dns,
                        dane
                    );
                }
                break;

            case DHCP_OPT_SERVER_ID:
                if (len == 4) {
                    wynik->ma_serwer =
                        true;

                    skopiuj_ip(
                        wynik->serwer,
                        dane
                    );
                }
                break;

            default:
                break;
        }

        pos +=
            len;
    }

    /*
     * Brak END traktujemy jako uszkodzona liste.
     */
    return false;
}

bool wyslij_dhcp_internal(
    uint8_t typ_wiadomosci,
    const uint8_t* zapytanie_ip,
    const uint8_t* serwer_ip
) {
    if (!mac_karty_gotowy()) {
        return false;
    }

    uint8_t ramka[
        ETH_LEN +
        IPV4_MIN_LEN +
        UDP_LEN +
        DHCP_PACKET_MAX
    ] = {};

    static const uint8_t broadcast_mac[6] = {
        0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF
    };

    static const uint8_t ip_zero[4] = {
        0, 0, 0, 0
    };

    static const uint8_t ip_broadcast[4] = {
        255, 255, 255, 255
    };

    EthNaglowek* eth =
        reinterpret_cast<EthNaglowek*>(
            ramka
        );

    wypelnij_eth(
        eth,
        broadcast_mac,
        ETHERTYPE_IPV4
    );

    uint8_t* udp_ptr =
        ramka +
        ETH_LEN +
        IPV4_MIN_LEN;

    UdpNaglowek* udp =
        reinterpret_cast<UdpNaglowek*>(
            udp_ptr
        );

    uint8_t* dhcp =
        udp_ptr +
        UDP_LEN;

    /*
     * BOOTP fixed header.
     */
    dhcp[0] = 1; /* BOOTREQUEST */
    dhcp[1] = 1; /* Ethernet */
    dhcp[2] = 6;
    dhcp[3] = 0;

    zapisz_be32(
        dhcp + 4,
        dhcp_xid
    );

    zapisz_be16(
        dhcp + 8,
        0
    );

    zapisz_be16(
        dhcp + 10,
        0x8000U
    );

    skopiuj_mac(
        dhcp + 28,
        pobierz_mac_adres()
    );

    zapisz_be32(
        dhcp +
        DHCP_COOKIE_OFFSET,
        DHCP_MAGIC_COOKIE
    );

    size_t opt =
        DHCP_OPTIONS_OFFSET;

    const uint8_t msg_type_dane[1] = {
        typ_wiadomosci
    };

    if (!dhcp_dodaj_opcje(
            dhcp,
            DHCP_PACKET_MAX,
            &opt,
            DHCP_OPT_MSG_TYPE,
            msg_type_dane,
            1)) {

        return false;
    }

    if (typ_wiadomosci ==
            DHCP_REQUEST &&
        zapytanie_ip &&
        serwer_ip) {

        if (!dhcp_dodaj_opcje(
                dhcp,
                DHCP_PACKET_MAX,
                &opt,
                DHCP_OPT_REQ_IP,
                zapytanie_ip,
                4) ||
            !dhcp_dodaj_opcje(
                dhcp,
                DHCP_PACKET_MAX,
                &opt,
                DHCP_OPT_SERVER_ID,
                serwer_ip,
                4)) {

            return false;
        }
    }

    const uint8_t lista_parametrow[] = {
        DHCP_OPT_SUBNET,
        DHCP_OPT_ROUTER,
        DHCP_OPT_DNS
    };

    if (!dhcp_dodaj_opcje(
            dhcp,
            DHCP_PACKET_MAX,
            &opt,
            DHCP_OPT_PARAM_REQ,
            lista_parametrow,
            sizeof(lista_parametrow))) {

        return false;
    }

    if (opt >=
        DHCP_PACKET_MAX) {

        return false;
    }

    dhcp[opt++] =
        DHCP_OPT_END;

    /*
     * RFC wymaga BOOTP/DHCP przynajmniej 300 bajtow IP payload na wielu
     * starszych implementacjach. Dopelniamy sam DHCP do 300, jezeli trzeba.
     */
    size_t dhcp_len =
        opt;

    if (dhcp_len <
        300U) {

        dhcp_len =
            300U;
    }

    const size_t udp_len =
        UDP_LEN +
        dhcp_len;

    const size_t ip_len =
        IPV4_MIN_LEN +
        udp_len;

    if (ip_len >
        UINT16_MAX) {

        return false;
    }

    udp->port_zrodlowy =
        host_na_siec16(
            PORT_DHCP_CLIENT
        );

    udp->port_docelowy =
        host_na_siec16(
            PORT_DHCP_SERVER
        );

    udp->dlugosc =
        host_na_siec16(
            static_cast<uint16_t>(
                udp_len
            )
        );

    udp->suma_kontrolna =
        0;

    Ipv4Naglowek* ip =
        reinterpret_cast<Ipv4Naglowek*>(
            ramka +
            ETH_LEN
        );

    if (!wypelnij_ipv4(
            ip,
            static_cast<uint16_t>(
                ip_len
            ),
            IP_PROTO_UDP,
            ip_zero,
            ip_broadcast)) {

        return false;
    }

    /*
     * UDP checksum w DHCPv4 moze byc 0. Pozostawiamy 0, aby maksymalnie
     * zachowac kompatybilnosc z prostymi serwerami/boot stackami.
     */
    return
        wyslij_ethernet(
            ramka,
            ETH_LEN +
                ip_len
        );
}

} // namespace

void wyslij_pakiet_dhcp(
    uint8_t typ_wiadomosci,
    uint8_t* zapytanie_ip,
    uint8_t* serwer_ip
) {
    (void)wyslij_dhcp_internal(
        typ_wiadomosci,
        zapytanie_ip,
        serwer_ip
    );
}

extern "C" void uruchom_klienta_dhcp() {
    wypisz_log(
        "[DHCP] Wysylam DHCPDISCOVER..."
    );

    konfiguracja_ipv4_gotowa =
        false;

    wyzeruj(
        nasz_ip,
        sizeof(nasz_ip)
    );

    wyzeruj(
        brama_ip,
        sizeof(brama_ip)
    );

    maska_podsieci[0] = 255;
    maska_podsieci[1] = 255;
    maska_podsieci[2] = 255;
    maska_podsieci[3] = 0;

    serwer_dns_ip[0] = 8;
    serwer_dns_ip[1] = 8;
    serwer_dns_ip[2] = 8;
    serwer_dns_ip[3] = 8;

    wyczysc_cache_arp();

    dhcp_otrzymano_offer =
        false;

    dhcp_otrzymano_ack =
        false;

    dhcp_otrzymano_nak =
        false;

    /*
     * XID rozni sie pomiedzy kolejnymi probami i zalezy od MAC.
     * Nie jest kryptograficzny - ma jedynie korelowac sesje DHCP.
     */
    const uint8_t* mac =
        pobierz_mac_adres();

    dhcp_xid +=
        UINT32_C(0x00010001);

    if (mac) {
        dhcp_xid ^=
            (static_cast<uint32_t>(
                 mac[2]) << 24) |
            (static_cast<uint32_t>(
                 mac[3]) << 16) |
            (static_cast<uint32_t>(
                 mac[4]) << 8) |
            static_cast<uint32_t>(
                mac[5]);
    }

    if (!wyslij_dhcp_internal(
            DHCP_DISCOVER,
            nullptr,
            nullptr)) {

        wypisz_log(
            "[DHCP] Nie udalo sie wyslac DISCOVER."
        );

        return;
    }

    const uint64_t offer_deadline = deadline_ms(DHCP_TIMEOUT_MS);
    while (czas_monotoniczny_ms() < offer_deadline) {

        e1000_obsluz_odbior();

        if (__atomic_load_n(
                &dhcp_otrzymano_offer,
                __ATOMIC_ACQUIRE)) {

            break;
        }

        asm volatile(
            "pause"
        );
    }

    if (!__atomic_load_n(
            &dhcp_otrzymano_offer,
            __ATOMIC_ACQUIRE)) {

        wypisz_log(
            "[DHCP] Timeout oczekiwania na DHCPOFFER."
        );

        return;
    }

    wypisz_log(
        "[DHCP] DHCPOFFER odebrany, wysylam DHCPREQUEST..."
    );

    if (!wyslij_dhcp_internal(
            DHCP_REQUEST,
            dhcp_offer_ip,
            dhcp_offer_serwer)) {

        wypisz_log(
            "[DHCP] Nie udalo sie wyslac DHCPREQUEST."
        );

        return;
    }

    const uint64_t ack_deadline = deadline_ms(DHCP_TIMEOUT_MS);
    while (czas_monotoniczny_ms() < ack_deadline) {

        e1000_obsluz_odbior();

        if (__atomic_load_n(
                &dhcp_otrzymano_ack,
                __ATOMIC_ACQUIRE) ||
            __atomic_load_n(
                &dhcp_otrzymano_nak,
                __ATOMIC_ACQUIRE)) {

            break;
        }

        asm volatile(
            "pause"
        );
    }

    if (__atomic_load_n(
            &dhcp_otrzymano_nak,
            __ATOMIC_ACQUIRE)) {

        wypisz_log(
            "[DHCP] Serwer odrzucil konfiguracje (DHCPNAK)."
        );

        return;
    }

    if (!__atomic_load_n(
            &dhcp_otrzymano_ack,
            __ATOMIC_ACQUIRE)) {

        wypisz_log(
            "[DHCP] Timeout oczekiwania na DHCPACK."
        );

        return;
    }

    konfiguracja_ipv4_gotowa =
        true;

    wypisz_ip_log(
        "[DHCP] IPv4: ",
        nasz_ip
    );

    wypisz_ip_log(
        "[DHCP] Brama: ",
        brama_ip
    );

    wypisz_ip_log(
        "[DHCP] Maska: ",
        maska_podsieci
    );

    wypisz_ip_log(
        "[DHCP] DNS: ",
        serwer_dns_ip
    );
}

namespace {

/* =========================================================================
 * 13. ICMP / PING
 * ========================================================================= */

uint8_t oczekiwany_ping_ip[4] = {};
uint16_t oczekiwany_ping_id =
    0;

uint16_t oczekiwany_ping_seq =
    0;

} // namespace

volatile bool odebrano_pong =
    false;

extern "C" uint32_t bws_siec_ping(
    uint8_t ip1,
    uint8_t ip2,
    uint8_t ip3,
    uint8_t ip4
) {
    if (!konfiguracja_ipv4_gotowa) {
        wypisz_log(
            "[ICMP] result=NET_ERR_NO_IP"
        );
        return 0;
    }

    uint8_t cel_ip[4] = {
        ip1, ip2, ip3, ip4
    };

    if (ip_jest_zero(
            cel_ip) ||
        ip_jest_broadcast(
            cel_ip) ||
        ip_jest_multicast(
            cel_ip)) {

        wypisz_log(
            "[ICMP] result=NET_ERR_INVALID_ARG"
        );
        return 0;
    }

    wypisz_ip_log("[ICMP] dst=", cel_ip);

    uint8_t docelowy_mac[6] = {};

    if (!rozwiaz_adres_mac(
            cel_ip,
            docelowy_mac)) {

        wypisz_log(
            "[ICMP] result=NET_ERR_ARP_TIMEOUT"
        );
        return 0;
    }

    constexpr size_t ICMP_PAYLOAD =
        32U;

    constexpr size_t ICMP_LEN =
        ICMP_ECHO_LEN +
        ICMP_PAYLOAD;

    uint8_t ramka[
        ETH_LEN +
        IPV4_MIN_LEN +
        ICMP_LEN
    ] = {};

    EthNaglowek* eth =
        reinterpret_cast<EthNaglowek*>(
            ramka
        );

    wypelnij_eth(
        eth,
        docelowy_mac,
        ETHERTYPE_IPV4
    );

    Ipv4Naglowek* ip =
        reinterpret_cast<Ipv4Naglowek*>(
            ramka +
            ETH_LEN
        );

    if (!wypelnij_ipv4(
            ip,
            static_cast<uint16_t>(
                IPV4_MIN_LEN +
                ICMP_LEN
            ),
            IP_PROTO_ICMP,
            nasz_ip,
            cel_ip)) {

        wypisz_log("[ICMP] result=NET_ERR_IPV4_HEADER");
        return 0;
    }

    IcmpEchoNaglowek* icmp =
        reinterpret_cast<IcmpEchoNaglowek*>(
            ramka +
            ETH_LEN +
            IPV4_MIN_LEN
        );

    icmp->typ =
        8;

    icmp->kod =
        0;

    icmp->id =
        host_na_siec16(
            ICMP_ECHO_ID
        );

    icmp->sekwencja =
        host_na_siec16(
            ICMP_ECHO_SEQ
        );

    uint8_t* payload =
        reinterpret_cast<uint8_t*>(
            icmp
        ) +
        ICMP_ECHO_LEN;

    for (size_t i = 0;
         i <
            ICMP_PAYLOAD;
         ++i) {

        payload[i] =
            static_cast<uint8_t>(
                i
            );
    }

    icmp->suma_kontrolna =
        0;

    icmp->suma_kontrolna =
        host_na_siec16(
            suma_kontrolna_bajty(
                icmp,
                ICMP_LEN
            )
        );

    skopiuj_ip(
        oczekiwany_ping_ip,
        cel_ip
    );

    oczekiwany_ping_id =
        ICMP_ECHO_ID;

    oczekiwany_ping_seq =
        ICMP_ECHO_SEQ;

    __atomic_store_n(
        &odebrano_pong,
        false,
        __ATOMIC_RELEASE
    );

    if (!wyslij_ethernet(
            ramka,
            sizeof(ramka))) {

        wypisz_log("[ICMP] result=NET_ERR_E1000_TX");
        return 0;
    }
    wypisz_ip_log("[IP] source=", nasz_ip);
    wypisz_log("[ICMP] send=NET_OK");

    const uint64_t ping_deadline = deadline_ms(PING_TIMEOUT_MS);
    while (czas_monotoniczny_ms() < ping_deadline) {

        e1000_obsluz_odbior();

        if (__atomic_load_n(
                &odebrano_pong,
                __ATOMIC_ACQUIRE)) {

            wypisz_log(
                "[ICMP] Echo Reply odebrane."
            );
            wypisz_log("[ICMP] result=NET_OK_REPLY");
            return 2;
        }

        asm volatile(
            "pause"
        );
    }

    wypisz_log(
        "[ICMP] result=NET_OK_REPLY_TIMEOUT"
    );
    return 1;
}

/* =========================================================================
 * 14. DNS
 * ========================================================================= */

volatile bool dns_odebrano =
    false;

uint8_t dns_resolved_ip[4] = {
    0, 0, 0, 0
};

namespace {

uint16_t dns_oczekiwane_id =
    0;

uint16_t dns_oczekiwany_port =
    0;

uint8_t dns_oczekiwany_serwer[4] = {};

bool znak_dns_poprawny(
    char c
) {
    const uint8_t u =
        static_cast<uint8_t>(
            c
        );

    if (u <= 0x20U ||
        u >= 0x7FU) {

        return false;
    }

    /*
     * Blokujemy znaki, ktore na pewno nie naleza do prostej nazwy hosta
     * wykorzystywanej przez obecny resolver.
     */
    return
        c != '/' &&
        c != '\\' &&
        c != ':' &&
        c != '\r' &&
        c != '\n';
}

bool koduj_nazwe_dns(
    const char* domena,
    uint8_t* wyjscie,
    size_t pojemnosc,
    size_t* zapisano
) {
    if (!domena ||
        !wyjscie ||
        !zapisano) {

        return false;
    }

    const size_t len =
        dlugosc_tekstu_limit(
            domena,
            DNS_NAZWA_MAX + 1U
        );

    if (len == 0 ||
        len >
            DNS_NAZWA_MAX) {

        return false;
    }

    size_t out =
        0;

    size_t start =
        0;

    for (size_t i = 0;
         i <= len;
         ++i) {

        const bool koniec_label =
            i == len ||
            domena[i] == '.';

        if (!koniec_label) {
            if (!znak_dns_poprawny(
                    domena[i])) {

                return false;
            }

            continue;
        }

        const size_t label_len =
            i -
            start;

        if (label_len == 0 ||
            label_len >
                DNS_LABEL_MAX) {

            return false;
        }

        if (out + 1U +
                label_len >
            pojemnosc) {

            return false;
        }

        wyjscie[out++] =
            static_cast<uint8_t>(
                label_len
            );

        for (size_t j = 0;
             j <
                label_len;
             ++j) {

            wyjscie[out++] =
                static_cast<uint8_t>(
                    domena[
                        start + j]
                );
        }

        start =
            i + 1U;
    }

    if (out + 1U >
        pojemnosc) {

        return false;
    }

    wyjscie[out++] =
        0;

    *zapisano =
        out;

    return true;
}

bool dns_pomin_nazwe(
    const uint8_t* pakiet,
    size_t dlugosc,
    size_t* offset
) {
    if (!pakiet ||
        !offset ||
        *offset >=
            dlugosc) {

        return false;
    }

    size_t p =
        *offset;

    size_t etykiety =
        0;

    while (p <
           dlugosc) {

        const uint8_t len =
            pakiet[p];

        if (len == 0) {
            ++p;
            *offset = p;
            return true;
        }

        if ((len &
             0xC0U) ==
            0xC0U) {

            if (p + 2U >
                dlugosc) {

                return false;
            }

            const uint16_t wsk =
                static_cast<uint16_t>(
                    ((len &
                      0x3FU) << 8) |
                    pakiet[p + 1]
                );

            if (wsk >=
                dlugosc) {

                return false;
            }

            p += 2;
            *offset = p;
            return true;
        }

        if ((len &
             0xC0U) != 0 ||
            len >
                DNS_LABEL_MAX) {

            return false;
        }

        ++p;

        if (static_cast<size_t>(
                len) >
            dlugosc -
                p) {

            return false;
        }

        p +=
            len;

        if (++etykiety > 127U) {
            return false;
        }
    }

    return false;
}

bool wyslij_zapytanie_dns(
    const char* domena,
    const uint8_t dns_ip[4],
    uint16_t id,
    uint16_t port_zrodlowy
) {
    uint8_t docelowy_mac[6] = {};

    uint8_t cel_ip_mut[4] = {};
    skopiuj_ip(
        cel_ip_mut,
        dns_ip
    );

    if (!rozwiaz_adres_mac(
            cel_ip_mut,
            docelowy_mac)) {

        return false;
    }

    uint8_t ramka[
        ETH_LEN +
        IPV4_MIN_LEN +
        UDP_LEN +
        DNS_PACKET_MAX
    ] = {};

    EthNaglowek* eth =
        reinterpret_cast<EthNaglowek*>(
            ramka
        );

    wypelnij_eth(
        eth,
        docelowy_mac,
        ETHERTYPE_IPV4
    );

    uint8_t* dns =
        ramka +
        ETH_LEN +
        IPV4_MIN_LEN +
        UDP_LEN;

    /*
     * DNS header - 12 bajtow.
     */
    zapisz_be16(
        dns + 0,
        id
    );

    zapisz_be16(
        dns + 2,
        0x0100U
    );

    zapisz_be16(
        dns + 4,
        1
    );

    zapisz_be16(
        dns + 6,
        0
    );

    zapisz_be16(
        dns + 8,
        0
    );

    zapisz_be16(
        dns + 10,
        0
    );

    size_t qname_len =
        0;

    if (!koduj_nazwe_dns(
            domena,
            dns + 12,
            DNS_PACKET_MAX - 12U - 4U,
            &qname_len)) {

        return false;
    }

    size_t dns_len =
        12U +
        qname_len;

    zapisz_be16(
        dns +
        dns_len,
        1
    );

    dns_len +=
        2;

    zapisz_be16(
        dns +
        dns_len,
        1
    );

    dns_len +=
        2;

    const size_t udp_len =
        UDP_LEN +
        dns_len;

    const size_t ip_len =
        IPV4_MIN_LEN +
        udp_len;

    UdpNaglowek* udp =
        reinterpret_cast<UdpNaglowek*>(
            ramka +
            ETH_LEN +
            IPV4_MIN_LEN
        );

    udp->port_zrodlowy =
        host_na_siec16(
            port_zrodlowy
        );

    udp->port_docelowy =
        host_na_siec16(
            PORT_DNS
        );

    udp->dlugosc =
        host_na_siec16(
            static_cast<uint16_t>(
                udp_len
            )
        );

    udp->suma_kontrolna =
        0;

    Ipv4Naglowek* ip =
        reinterpret_cast<Ipv4Naglowek*>(
            ramka +
            ETH_LEN
        );

    if (!wypelnij_ipv4(
            ip,
            static_cast<uint16_t>(
                ip_len
            ),
            IP_PROTO_UDP,
            nasz_ip,
            dns_ip)) {

        return false;
    }

    udp->suma_kontrolna =
        host_na_siec16(
            suma_transport_ipv4(
                nasz_ip,
                dns_ip,
                IP_PROTO_UDP,
                reinterpret_cast<uint8_t*>(
                    udp
                ),
                static_cast<uint16_t>(
                    udp_len
                )
            )
        );

    return
        wyslij_ethernet(
            ramka,
            ETH_LEN +
                ip_len
        );
}

} // namespace

extern "C" bool kernel_siec_dns(
    const char* domena,
    uint8_t* wyjsciowy_ip
) {
    if (!domena ||
        !wyjsciowy_ip ||
        !konfiguracja_ipv4_gotowa) {

        return false;
    }

    const size_t len =
        dlugosc_tekstu_limit(
            domena,
            DNS_NAZWA_MAX + 1U
        );

    if (len == 0 ||
        len >
            DNS_NAZWA_MAX) {

        return false;
    }

    const uint16_t id =
        ++nastepny_dns_id;

    if (nastepny_dns_id == 0) {
        nastepny_dns_id =
            1;
    }

    const uint16_t port =
        przydziel_port(
            &nastepny_port_dns,
            PORT_DNS_START
        );

    wypisz_log("[DNS] query host=");
    wypisz_log(domena);
    wypisz_ip_log("[DNS] server=", serwer_dns_ip);

    dns_oczekiwane_id =
        id;

    dns_oczekiwany_port =
        port;

    skopiuj_ip(
        dns_oczekiwany_serwer,
        serwer_dns_ip
    );

    wyzeruj(
        dns_resolved_ip,
        sizeof(dns_resolved_ip)
    );

    __atomic_store_n(
        &dns_odebrano,
        false,
        __ATOMIC_RELEASE
    );

    if (!wyslij_zapytanie_dns(
            domena,
            serwer_dns_ip,
            id,
            port)) {

        return false;
    }

    const uint64_t dns_deadline = deadline_ms(DNS_TIMEOUT_MS);
    while (czas_monotoniczny_ms() < dns_deadline) {

        e1000_obsluz_odbior();

        if (__atomic_load_n(
                &dns_odebrano,
                __ATOMIC_ACQUIRE)) {

            skopiuj_ip(
                wyjsciowy_ip,
                dns_resolved_ip
            );

            wypisz_ip_log("[DNS] A=", dns_resolved_ip);

            return
                !ip_jest_zero(
                    wyjsciowy_ip
                );
        }

        asm volatile(
            "pause"
        );
    }
    wypisz_log("[DNS] response timeout.");

    return false;
}

/* =========================================================================
 * 15. TCP
 * ========================================================================= */

namespace {

enum class StanTCP : uint8_t {
    ZAMKNIETY = 0,
    SYN_WYSLANY,
    USTANOWIONY,
    FIN_WYSLANY
};

volatile StanTCP stan_tcp =
    StanTCP::ZAMKNIETY;

uint32_t tcp_nasz_seq =
    0;

uint32_t tcp_nasz_ack =
    0;

uint16_t tcp_nasz_port =
    PORT_EPHEMERAL_START;

uint16_t tcp_cel_port =
    PORT_HTTP;

uint8_t tcp_cel_ip[4] = {};
uint8_t tcp_cel_mac[6] = {};

bool tcp_tryb_gniazda =
    false;

bool tcp_sesja_zajeta =
    false;

bool tcp_przepelnienie_odbioru =
    false;

bool tcp_rst_odebrany =
    false;

char* tcp_bufor_odbiorczy =
    nullptr;

uint32_t tcp_max_bajtow =
    0;

uint8_t tcp_bufor_gniazda[
    TCP_BUFOR_GNIAZDA
] = {};

uint32_t tcp_gniazdo_poczatek =
    0;

uint32_t tcp_gniazdo_koniec =
    0;

uint32_t tcp_poczatkowy_seq() {
    /*
     * Prosty ISN, nie kryptograficzny. Dla obecnego pojedynczego stosu
     * wystarcza do unikniecia stalego 1000 pomiedzy kolejnymi polaczeniami.
     */
    static uint32_t licznik =
        UINT32_C(0x31415926);

    licznik +=
        UINT32_C(0x1021);

    const uint8_t* mac =
        pobierz_mac_adres();

    if (mac) {
        licznik ^=
            (static_cast<uint32_t>(
                 mac[4]) << 8) |
            static_cast<uint32_t>(
                mac[5]);
    }

    return licznik;
}

void tcp_wyczysc_bufor_gniazda() {
    tcp_gniazdo_poczatek =
        0;

    tcp_gniazdo_koniec =
        0;
}

void tcp_resetuj_sesje(
    bool zwolnij_wlasciciela
) {
    stan_tcp =
        StanTCP::ZAMKNIETY;

    tcp_nasz_ack =
        0;

    tcp_cel_port =
        0;

    wyzeruj(
        tcp_cel_ip,
        sizeof(tcp_cel_ip)
    );

    wyzeruj(
        tcp_cel_mac,
        sizeof(tcp_cel_mac)
    );

    tcp_tryb_gniazda =
        false;

    tcp_bufor_odbiorczy =
        nullptr;

    tcp_max_bajtow =
        0;

    tcp_przepelnienie_odbioru =
        false;

    tcp_rst_odebrany =
        false;

    tcp_wyczysc_bufor_gniazda();

    if (zwolnij_wlasciciela) {
        tcp_sesja_zajeta =
            false;
    }
}

bool tcp_rezerwuj_sesje() {
    if (tcp_sesja_zajeta ||
        stan_tcp !=
            StanTCP::ZAMKNIETY) {

        return false;
    }

    tcp_sesja_zajeta =
        true;

    return true;
}

bool tcp_dane_sesji_poprawne() {
    return
        konfiguracja_ipv4_gotowa &&
        tcp_sesja_zajeta &&
        tcp_cel_port != 0 &&
        !ip_jest_zero(
            tcp_cel_ip) &&
        mac_jest_poprawny(
            tcp_cel_mac);
}

bool tcp_wyslij_internal(
    uint8_t flagi,
    const uint8_t* payload,
    uint16_t payload_len
) {
    if (!tcp_dane_sesji_poprawne()) {
        return false;
    }

    if (payload_len >
        TCP_MSS_WYSYLANIA) {

        return false;
    }

    if (payload_len > 0 &&
        !payload) {

        return false;
    }

    const size_t tcp_len =
        TCP_MIN_LEN +
        payload_len;

    const size_t ip_len =
        IPV4_MIN_LEN +
        tcp_len;

    if (ip_len >
        MAKS_PAKIET_IPV4) {

        return false;
    }

    uint8_t ramka[
        ETH_LEN +
        MAKS_PAKIET_IPV4
    ] = {};

    EthNaglowek* eth =
        reinterpret_cast<EthNaglowek*>(
            ramka
        );

    wypelnij_eth(
        eth,
        tcp_cel_mac,
        ETHERTYPE_IPV4
    );

    Ipv4Naglowek* ip =
        reinterpret_cast<Ipv4Naglowek*>(
            ramka +
            ETH_LEN
        );

    if (!wypelnij_ipv4(
            ip,
            static_cast<uint16_t>(
                ip_len
            ),
            IP_PROTO_TCP,
            nasz_ip,
            tcp_cel_ip)) {

        return false;
    }

    TcpNaglowek* tcp =
        reinterpret_cast<TcpNaglowek*>(
            ramka +
            ETH_LEN +
            IPV4_MIN_LEN
        );

    tcp->port_zrodlowy =
        host_na_siec16(
            tcp_nasz_port
        );

    tcp->port_docelowy =
        host_na_siec16(
            tcp_cel_port
        );

    tcp->numer_sekwencyjny =
        host_na_siec32(
            tcp_nasz_seq
        );

    tcp->numer_potwierdzenia =
        host_na_siec32(
            tcp_nasz_ack
        );

    tcp->przesuniecie_danych =
        static_cast<uint8_t>(
            (TCP_MIN_LEN / 4U) <<
            4
        );

    tcp->flagi =
        flagi;

    tcp->rozmiar_okna =
        host_na_siec16(
            TCP_OKNO
        );

    tcp->suma_kontrolna =
        0;

    tcp->wazny_wskaznik =
        0;

    if (payload_len > 0) {
        kopiuj_bajty(
            reinterpret_cast<uint8_t*>(
                tcp
            ) +
            TCP_MIN_LEN,
            payload,
            payload_len
        );
    }

    tcp->suma_kontrolna =
        host_na_siec16(
            suma_transport_ipv4(
                nasz_ip,
                tcp_cel_ip,
                IP_PROTO_TCP,
                reinterpret_cast<uint8_t*>(
                    tcp
                ),
                static_cast<uint16_t>(
                    tcp_len
                )
            )
        );

    return
        wyslij_ethernet(
            ramka,
            ETH_LEN +
                ip_len
        );
}

bool tcp_przygotuj_polaczenie(
    const uint8_t cel_ip[4],
    uint16_t port,
    bool tryb_gniazda
) {
    if (!cel_ip ||
        port == 0 ||
        !konfiguracja_ipv4_gotowa) {

        return false;
    }

    if (!tcp_rezerwuj_sesje()) {
        return false;
    }

    uint8_t cel_mut[4] = {};
    skopiuj_ip(
        cel_mut,
        cel_ip
    );

    uint8_t docelowy_mac[6] = {};

    if (!rozwiaz_adres_mac(
            cel_mut,
            docelowy_mac) ||
        !mac_jest_poprawny(
            docelowy_mac)) {

        tcp_resetuj_sesje(
            true
        );

        return false;
    }

    skopiuj_ip(
        tcp_cel_ip,
        cel_ip
    );

    skopiuj_mac(
        tcp_cel_mac,
        docelowy_mac
    );

    tcp_cel_port =
        port;

    tcp_nasz_port =
        przydziel_port(
            &nastepny_port_ephemeral,
            PORT_EPHEMERAL_START
        );

    tcp_nasz_seq =
        tcp_poczatkowy_seq();

    tcp_nasz_ack =
        0;

    tcp_tryb_gniazda =
        tryb_gniazda;

    tcp_bufor_odbiorczy =
        nullptr;

    tcp_max_bajtow =
        0;

    tcp_przepelnienie_odbioru =
        false;

    tcp_rst_odebrany =
        false;

    tcp_wyczysc_bufor_gniazda();

    stan_tcp =
        StanTCP::SYN_WYSLANY;

    if (!tcp_wyslij_internal(
            TCP_FLAG_SYN,
            nullptr,
            0)) {

        tcp_resetuj_sesje(
            true
        );

        return false;
    }

    return true;
}

bool tcp_czekaj_na_polaczenie() {
    const uint64_t connect_deadline = deadline_ms(TCP_CONNECT_TIMEOUT_MS);
    while (czas_monotoniczny_ms() < connect_deadline) {

        e1000_obsluz_odbior();

        if (stan_tcp ==
            StanTCP::USTANOWIONY) {

            return true;
        }

        if (stan_tcp ==
                StanTCP::ZAMKNIETY ||
            tcp_rst_odebrany) {

            return false;
        }

        asm volatile(
            "pause"
        );
    }

    return false;
}

bool tcp_zamknij_grzecznie() {
    if (stan_tcp ==
        StanTCP::USTANOWIONY) {

        if (tcp_wyslij_internal(
                TCP_FLAG_FIN |
                TCP_FLAG_ACK,
                nullptr,
                0)) {

            ++tcp_nasz_seq;

            stan_tcp =
                StanTCP::FIN_WYSLANY;

            return true;
        }
    }

    return false;
}

uint32_t tcp_gniazdo_dostepne() {
    if (tcp_gniazdo_koniec <
        tcp_gniazdo_poczatek) {

        tcp_wyczysc_bufor_gniazda();

        return 0;
    }

    return
        tcp_gniazdo_koniec -
        tcp_gniazdo_poczatek;
}

void tcp_gniazdo_kompaktuj() {
    if (tcp_gniazdo_poczatek == 0) {
        return;
    }

    const uint32_t pozostalo =
        tcp_gniazdo_dostepne();

    for (uint32_t i = 0;
         i <
            pozostalo;
         ++i) {

        tcp_bufor_gniazda[i] =
            tcp_bufor_gniazda[
                tcp_gniazdo_poczatek +
                i
            ];
    }

    tcp_gniazdo_poczatek =
        0;

    tcp_gniazdo_koniec =
        pozostalo;
}

bool tcp_gniazdo_ma_miejsce(
    uint32_t potrzebne
) {
    if (potrzebne >
        TCP_BUFOR_GNIAZDA) {

        return false;
    }

    if (tcp_gniazdo_koniec +
            potrzebne <=
        TCP_BUFOR_GNIAZDA) {

        return true;
    }

    tcp_gniazdo_kompaktuj();

    return
        tcp_gniazdo_koniec +
            potrzebne <=
        TCP_BUFOR_GNIAZDA;
}

void tcp_przyjmij_payload(
    const uint8_t* payload,
    uint32_t payload_len,
    uint32_t numer_seq
) {
    if (!payload ||
        payload_len == 0) {

        return;
    }

    /*
     * Segment dokladnie oczekiwany.
     *
     * Prosty stos nie sklada segmentow out-of-order. W przypadku dziury
     * odpowie bieżącym ACK i serwer powinien retransmitowac.
     */
    if (numer_seq !=
        tcp_nasz_ack) {

        (void)tcp_wyslij_internal(
            TCP_FLAG_ACK,
            nullptr,
            0
        );

        return;
    }

    if (tcp_tryb_gniazda) {
        /*
         * Dla TLS nie wolno utracic ani jednego bajtu. Gdy bufor jest pelny,
         * nie przesuwamy ACK - sender retransmituje po tym jak klient oprozni
         * bufor przez tcp_gniazdo_odbierz().
         */
        if (!tcp_gniazdo_ma_miejsce(
                payload_len)) {

            (void)tcp_wyslij_internal(
                TCP_FLAG_ACK,
                nullptr,
                0
            );

            return;
        }

        kopiuj_bajty(
            tcp_bufor_gniazda +
                tcp_gniazdo_koniec,
            payload,
            payload_len
        );

        tcp_gniazdo_koniec +=
            payload_len;

        tcp_nasz_ack +=
            payload_len;

        (void)tcp_wyslij_internal(
            TCP_FLAG_ACK,
            nullptr,
            0
        );

        return;
    }

    /*
     * HTTP raw response.
     * Jesli caller dal zbyt maly bufor, zapisujemy tyle ile sie miesci,
     * oznaczamy truncation, ale ACKujemy caly segment, aby polaczenie moglo
     * sie poprawnie zamknac i funkcja zwrocila false zamiast zawisnac.
     */
    uint32_t wolne =
        0;

    if (tcp_bufor_odbiorczy &&
        tcp_zapisano_bajtow <
            tcp_max_bajtow) {

        wolne =
            tcp_max_bajtow -
            tcp_zapisano_bajtow;
    }

    const uint32_t do_kopii =
        payload_len <
                wolne
            ? payload_len
            : wolne;

    if (do_kopii > 0) {
        kopiuj_bajty(
            tcp_bufor_odbiorczy +
                tcp_zapisano_bajtow,
            payload,
            do_kopii
        );

        tcp_zapisano_bajtow +=
            do_kopii;
    }

    if (do_kopii !=
        payload_len) {

        tcp_przepelnienie_odbioru =
            true;
    }

    tcp_nasz_ack +=
        payload_len;

    (void)tcp_wyslij_internal(
        TCP_FLAG_ACK,
        nullptr,
        0
    );
}

} // namespace

uint32_t tcp_zapisano_bajtow =
    0;

volatile bool tcp_dane_odebrane =
    false;

/* Publiczna zgodna funkcja starego kodu. */
void wyslij_pakiet_tcp(
    uint8_t flagi,
    uint8_t* payload,
    uint16_t payload_len
) {
    (void)tcp_wyslij_internal(
        flagi,
        payload,
        payload_len
    );
}

extern "C" bool tcp_gniazdo_polacz(
    uint8_t* cel_ip,
    uint16_t port
) {
    if (!cel_ip ||
        port == 0) {

        return false;
    }

    if (!tcp_przygotuj_polaczenie(
            cel_ip,
            port,
            true)) {

        return false;
    }

    if (!tcp_czekaj_na_polaczenie()) {
        tcp_resetuj_sesje(
            true
        );

        return false;
    }

    return true;
}

extern "C" int tcp_gniazdo_wyslij(
    const uint8_t* dane,
    uint32_t dlugosc
) {
    if (!dane ||
        dlugosc == 0) {

        return
            dlugosc == 0
                ? 0
                : -1;
    }

    if (!tcp_sesja_zajeta ||
        !tcp_tryb_gniazda ||
        stan_tcp !=
            StanTCP::USTANOWIONY) {

        return -1;
    }

    uint32_t wyslano =
        0;

    while (wyslano <
           dlugosc) {

        const uint32_t pozostalo =
            dlugosc -
            wyslano;

        const uint16_t fragment =
            static_cast<uint16_t>(
                pozostalo >
                        TCP_MSS_WYSYLANIA
                    ? TCP_MSS_WYSYLANIA
                    : pozostalo
            );

        if (!tcp_wyslij_internal(
                TCP_FLAG_PSH |
                TCP_FLAG_ACK,
                dane +
                    wyslano,
                fragment)) {

            return
                wyslano != 0
                    ? static_cast<int>(
                        wyslano)
                    : -1;
        }

        tcp_nasz_seq +=
            fragment;

        wyslano +=
            fragment;

        /*
         * Dajemy stosowi szanse przetworzyc ACK/RST pomiedzy segmentami.
         */
        e1000_obsluz_odbior();

        if (stan_tcp ==
                StanTCP::ZAMKNIETY ||
            tcp_rst_odebrany) {

            break;
        }
    }

    return
        static_cast<int>(
            wyslano
        );
}

extern "C" int tcp_gniazdo_odbierz(
    uint8_t* dane,
    uint32_t maksymalna_dlugosc
) {
    if (!dane ||
        maksymalna_dlugosc == 0) {

        return -1;
    }

    if (!tcp_sesja_zajeta ||
        !tcp_tryb_gniazda) {

        return -1;
    }

    e1000_obsluz_odbior();

    const uint32_t dostepne =
        tcp_gniazdo_dostepne();

    if (dostepne == 0) {
        return
            stan_tcp ==
                    StanTCP::ZAMKNIETY
                ? 0
                : -2;
    }

    const uint32_t n =
        dostepne <
                maksymalna_dlugosc
            ? dostepne
            : maksymalna_dlugosc;

    kopiuj_bajty(
        dane,
        tcp_bufor_gniazda +
            tcp_gniazdo_poczatek,
        n
    );

    tcp_gniazdo_poczatek +=
        n;

    if (tcp_gniazdo_poczatek ==
        tcp_gniazdo_koniec) {

        tcp_wyczysc_bufor_gniazda();
    }

    return
        static_cast<int>(
            n
        );
}

extern "C" bool tcp_gniazdo_otwarte() {
    return
        tcp_sesja_zajeta &&
        tcp_tryb_gniazda &&
        stan_tcp ==
            StanTCP::USTANOWIONY;
}

extern "C" void tcp_gniazdo_zamknij() {
    if (!tcp_sesja_zajeta) {
        tcp_resetuj_sesje(
            true
        );

        return;
    }

    (void)tcp_zamknij_grzecznie();

    /*
     * Nie czekamy bez konca na FIN-ACK. TLS potrzebuje deterministycznego
     * close callbacku.
     */
    for (uint32_t proba = 0;
         proba <
            100000U;
         ++proba) {

        e1000_obsluz_odbior();

        if (stan_tcp ==
            StanTCP::ZAMKNIETY) {

            break;
        }

        asm volatile(
            "pause"
        );
    }

    tcp_resetuj_sesje(
        true
    );
}

namespace {

/* =========================================================================
 * 16. HTTP
 * ========================================================================= */

bool http_tekst_bez_crlf(
    const char* tekst,
    size_t max_len,
    bool wymaga_slash
) {
    if (!tekst) {
        return false;
    }

    const size_t len =
        dlugosc_tekstu_limit(
            tekst,
            max_len + 1U
        );

    if (len == 0 ||
        len >
            max_len) {

        return false;
    }

    if (wymaga_slash &&
        tekst[0] != '/') {

        return false;
    }

    for (size_t i = 0;
         i < len;
         ++i) {

        const char c =
            tekst[i];

        if (c == '\r' ||
            c == '\n' ||
            c == '\0') {

            return false;
        }
    }

    return true;
}

bool zbuduj_http_get(
    const char* domena,
    const char* sciezka,
    char* wynik,
    size_t pojemnosc,
    size_t* dlugosc
) {
    if (!domena ||
        !sciezka ||
        !wynik ||
        !dlugosc) {

        return false;
    }

    if (!http_tekst_bez_crlf(
            domena,
            DNS_NAZWA_MAX,
            false) ||
        !http_tekst_bez_crlf(
            sciezka,
            2047,
            true)) {

        return false;
    }

    size_t p =
        0;

    wynik[0] =
        '\0';

    if (!dopisz_tekst(
            wynik,
            pojemnosc,
            &p,
            "GET ") ||
        !dopisz_tekst(
            wynik,
            pojemnosc,
            &p,
            sciezka) ||
        !dopisz_tekst(
            wynik,
            pojemnosc,
            &p,
            " HTTP/1.0\r\nHost: ") ||
        !dopisz_tekst(
            wynik,
            pojemnosc,
            &p,
            domena) ||
        !dopisz_tekst(
            wynik,
            pojemnosc,
            &p,
            "\r\nConnection: close\r\nUser-Agent: BursztynOS/0.1\r\nAccept: */*\r\n\r\n")) {

        return false;
    }

    *dlugosc =
        p;

    return true;
}

} // namespace

extern "C" bool kernel_siec_pobierz_http(
    uint8_t cel_ip[4],
    const char* domena,
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    if (!cel_ip ||
        !domena ||
        !sciezka ||
        !bufor ||
        max_dlugosc == 0 ||
        !konfiguracja_ipv4_gotowa) {

        return false;
    }

    if (!tcp_przygotuj_polaczenie(
            cel_ip,
            PORT_HTTP,
            false)) {

        wypisz_log(
            "[HTTP] Nie udalo sie przygotowac sesji TCP."
        );

        return false;
    }

    tcp_bufor_odbiorczy =
        bufor;

    tcp_max_bajtow =
        max_dlugosc;

    tcp_zapisano_bajtow =
        0;

    tcp_przepelnienie_odbioru =
        false;

    __atomic_store_n(
        &tcp_dane_odebrane,
        false,
        __ATOMIC_RELEASE
    );

    /*
     * Nie zerujemy max_dlugosc bajtow z kernela bez limitu.
     * Caller kontroluje lifetime, a tcp_zapisano_bajtow okresla faktyczna
     * dlugosc. Dla tekstowego API syscalls dopisuje NUL po transferze.
     */

    if (!tcp_czekaj_na_polaczenie()) {
        wypisz_log(
            "[HTTP] Timeout/RST podczas handshake TCP."
        );

        tcp_resetuj_sesje(
            true
        );

        return false;
    }

    char request[
        HTTP_REQUEST_MAX] = {};

    size_t request_len =
        0;

    if (!zbuduj_http_get(
            domena,
            sciezka,
            request,
            sizeof(request),
            &request_len)) {

        tcp_gniazdo_zamknij();

        return false;
    }

    size_t wyslano =
        0;

    while (wyslano <
           request_len) {

        const size_t pozostalo =
            request_len -
            wyslano;

        const uint16_t fragment =
            static_cast<uint16_t>(
                pozostalo >
                        TCP_MSS_WYSYLANIA
                    ? TCP_MSS_WYSYLANIA
                    : pozostalo
            );

        if (!tcp_wyslij_internal(
                TCP_FLAG_PSH |
                TCP_FLAG_ACK,
                reinterpret_cast<const uint8_t*>(
                    request
                ) +
                    wyslano,
                fragment)) {

            tcp_resetuj_sesje(
                true
            );

            return false;
        }

        tcp_nasz_seq +=
            fragment;

        wyslano +=
            fragment;
    }

    const uint64_t http_deadline = deadline_ms(HTTP_TIMEOUT_MS);
    while (czas_monotoniczny_ms() < http_deadline) {

        e1000_obsluz_odbior();

        if (stan_tcp ==
                StanTCP::ZAMKNIETY ||
            tcp_rst_odebrany) {

            break;
        }

        asm volatile(
            "pause"
        );
    }

    if (stan_tcp !=
        StanTCP::ZAMKNIETY) {

        (void)tcp_zamknij_grzecznie();
    }

    const bool sukces =
        tcp_zapisano_bajtow > 0 &&
        !tcp_przepelnienie_odbioru &&
        !tcp_rst_odebrany;

    if (tcp_przepelnienie_odbioru) {
        wypisz_log(
            "[HTTP] Odpowiedz przekroczyla bufor - transfer odrzucony jako uciety."
        );
    }

    if (sukces) {
        char log[128] = {};
        size_t p = 0;

        (void)dopisz_tekst(
            log,
            sizeof(log),
            &p,
            "[HTTP] Odebrano bajtow: "
        );

        char nbuf[16] = {};

        uint_do_str(
            tcp_zapisano_bajtow,
            nbuf,
            sizeof(nbuf)
        );

        (void)dopisz_tekst(
            log,
            sizeof(log),
            &p,
            nbuf
        );

        wypisz_log(
            log
        );
    }

    tcp_resetuj_sesje(
        true
    );

    return sukces;
}

/* =========================================================================
 * 17. PARSER ODBIORCZY
 * ========================================================================= */

namespace {

struct WidokIPv4 {
    Ipv4Naglowek* ip;
    uint8_t* payload;
    size_t payload_len;
    size_t ihl;
    size_t calkowita_dlugosc;
};

bool parsuj_ipv4(
    uint8_t* dane,
    size_t dostepne,
    WidokIPv4* wynik
) {
    if (!dane ||
        !wynik ||
        dostepne <
            IPV4_MIN_LEN) {

        return false;
    }

    Ipv4Naglowek* ip =
        reinterpret_cast<Ipv4Naglowek*>(
            dane
        );

    const uint8_t wersja =
        ip->wersja_ihl >>
        4;

    const uint8_t ihl_slowa =
        ip->wersja_ihl &
        0x0FU;

    if (wersja != 4 ||
        ihl_slowa < 5) {

        return false;
    }

    const size_t ihl =
        static_cast<size_t>(
            ihl_slowa) *
        4U;

    if (ihl >
        dostepne) {

        return false;
    }

    const uint16_t total =
        siec_na_host16(
            ip->dlugosc_calkowita
        );

    if (total < ihl ||
        total >
            dostepne) {

        return false;
    }

    /*
     * Internet checksum poprawnego headera z wlaczonym polem checksum
     * sklada sie do 0.
     */
    if (suma_kontrolna_bajty(
            ip,
            ihl) != 0) {

        return false;
    }

    const uint16_t frag =
        siec_na_host16(
            ip->flagi_fragment
        );

    /*
     * Odrzuc MF i offset != 0. DF (0x4000) jest dozwolone.
     */
    if ((frag &
         0x3FFFU) != 0) {

        return false;
    }

    wynik->ip =
        ip;

    wynik->payload =
        dane +
        ihl;

    wynik->payload_len =
        total -
        ihl;

    wynik->ihl =
        ihl;

    wynik->calkowita_dlugosc =
        total;

    return true;
}

bool udp_poprawny(
    const WidokIPv4& widok,
    UdpNaglowek** udp_wyj,
    uint8_t** payload_wyj,
    size_t* payload_len_wyj
) {
    if (!udp_wyj ||
        !payload_wyj ||
        !payload_len_wyj ||
        widok.payload_len <
            UDP_LEN) {

        return false;
    }

    UdpNaglowek* udp =
        reinterpret_cast<UdpNaglowek*>(
            widok.payload
        );

    const uint16_t udp_len =
        siec_na_host16(
            udp->dlugosc
        );

    if (udp_len <
            UDP_LEN ||
        udp_len >
            widok.payload_len) {

        return false;
    }

    if (udp->suma_kontrolna != 0) {
        /* Generator koduje wynik 0 jako 0xFFFF dla UDP (RFC 768).
           Ta sama funkcja uzyta do weryfikacji zwraca wiec 0xFFFF dla
           poprawnego segmentu zawierajacego juz checksum. */
        if (suma_transport_ipv4(
                widok.ip->zrodlo_ip,
                widok.ip->cel_ip,
                IP_PROTO_UDP,
                reinterpret_cast<uint8_t*>(
                    udp
                ),
                udp_len) != 0xFFFFU) {

            return false;
        }
    }

    *udp_wyj =
        udp;

    *payload_wyj =
        reinterpret_cast<uint8_t*>(
            udp
        ) +
        UDP_LEN;

    *payload_len_wyj =
        udp_len -
        UDP_LEN;

    return true;
}

void obsluz_arp(
    uint8_t* ramka,
    size_t dlugosc
) {
    if (!ramka ||
        dlugosc <
            ETH_LEN +
            ARP_LEN) {

        return;
    }
    __atomic_add_fetch(&arp_rx_packets, 1ULL, __ATOMIC_RELAXED);

    EthNaglowek* eth =
        reinterpret_cast<EthNaglowek*>(
            ramka
        );

    ArpNaglowek* arp =
        reinterpret_cast<ArpNaglowek*>(
            ramka +
            ETH_LEN
        );

    if (siec_na_host16(
            arp->typ_sprzetu) != 1 ||
        siec_na_host16(
            arp->typ_protokolu) !=
            ETHERTYPE_IPV4 ||
        arp->dlugosc_mac != 6 ||
        arp->dlugosc_ip != 4) {

        return;
    }

    const uint16_t operacja =
        siec_na_host16(
            arp->operacja
        );

    if (operacja != 1 &&
        operacja != 2) {

        return;
    }

    /*
     * Ethernet source i ARP sender MAC musza opisywac to samo urzadzenie.
     */
    if (!mac_rowne(
            eth->zrodlo_mac,
            arp->nadawca_mac) ||
        !mac_jest_poprawny(
            arp->nadawca_mac) ||
        !arp_ip_poprawne(
            arp->nadawca_ip)) {

        return;
    }

    dodaj_do_cache_arp(
        arp->nadawca_ip,
        arp->nadawca_mac
    );
    wypisz_ip_log("[ARP] reply/cache ip=", arp->nadawca_ip);

    if (!konfiguracja_ipv4_gotowa ||
        !ip_rowne(
            arp->cel_ip,
            nasz_ip)) {

        return;
    }

    if (operacja == 1) {
        zbuduj_i_wyslij_arp(
            2,
            arp->nadawca_mac,
            nasz_ip,
            arp->nadawca_mac,
            arp->nadawca_ip
        );
    }
}

void obsluz_dhcp(
    const WidokIPv4& widok,
    UdpNaglowek* udp,
    uint8_t* payload,
    size_t payload_len
) {
    if (!udp ||
        !payload ||
        payload_len <
            DHCP_OPTIONS_OFFSET + 1U) {

        return;
    }

    if (siec_na_host16(
            udp->port_zrodlowy) !=
            PORT_DHCP_SERVER ||
        siec_na_host16(
            udp->port_docelowy) !=
            PORT_DHCP_CLIENT) {

        return;
    }

    /*
     * BOOTREPLY / Ethernet / hlen=6.
     */
    if (payload[0] != 2 ||
        payload[1] != 1 ||
        payload[2] != 6) {

        return;
    }

    if (czytaj_be32(
            payload + 4) !=
        dhcp_xid) {

        return;
    }

    const uint8_t* mac =
        pobierz_mac_adres();

    if (!mac ||
        !mac_rowne(
            payload + 28,
            mac)) {

        return;
    }

    if (czytaj_be32(
            payload +
            DHCP_COOKIE_OFFSET) !=
        DHCP_MAGIC_COOKIE) {

        return;
    }

    OpcjeDHCP opcje{};

    if (!parsuj_opcje_dhcp(
            payload +
                DHCP_OPTIONS_OFFSET,
            payload_len -
                DHCP_OPTIONS_OFFSET,
            &opcje)) {

        return;
    }

    const uint8_t* yiaddr =
        payload + 16;

    if (opcje.typ_wiadomosci ==
        DHCP_OFFER) {

        if (ip_jest_zero(
                yiaddr)) {

            return;
        }

        skopiuj_ip(
            dhcp_offer_ip,
            yiaddr
        );

        if (opcje.ma_serwer) {
            skopiuj_ip(
                dhcp_offer_serwer,
                opcje.serwer
            );
        } else {
            skopiuj_ip(
                dhcp_offer_serwer,
                widok.ip->zrodlo_ip
            );
        }

        if (opcje.ma_maske) {
            skopiuj_ip(
                dhcp_offer_maska,
                opcje.maska
            );
        } else {
            dhcp_offer_maska[0] = 255;
            dhcp_offer_maska[1] = 255;
            dhcp_offer_maska[2] = 255;
            dhcp_offer_maska[3] = 0;
        }

        if (opcje.ma_brame) {
            skopiuj_ip(
                dhcp_offer_brama,
                opcje.brama
            );
        } else {
            wyzeruj(
                dhcp_offer_brama,
                sizeof(dhcp_offer_brama)
            );
        }

        if (opcje.ma_dns) {
            skopiuj_ip(
                dhcp_offer_dns,
                opcje.dns
            );
        } else {
            dhcp_offer_dns[0] = 8;
            dhcp_offer_dns[1] = 8;
            dhcp_offer_dns[2] = 8;
            dhcp_offer_dns[3] = 8;
        }

        __atomic_store_n(
            &dhcp_otrzymano_offer,
            true,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (opcje.typ_wiadomosci ==
        DHCP_NAK) {

        __atomic_store_n(
            &dhcp_otrzymano_nak,
            true,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (opcje.typ_wiadomosci !=
        DHCP_ACK) {

        return;
    }

    uint8_t nowy_ip[4] = {};

    if (!ip_jest_zero(
            yiaddr)) {

        skopiuj_ip(
            nowy_ip,
            yiaddr
        );
    } else {
        skopiuj_ip(
            nowy_ip,
            dhcp_offer_ip
        );
    }

    if (ip_jest_zero(
            nowy_ip)) {

        return;
    }

    skopiuj_ip(
        nasz_ip,
        nowy_ip
    );

    if (opcje.ma_maske) {
        skopiuj_ip(
            maska_podsieci,
            opcje.maska
        );
    } else {
        skopiuj_ip(
            maska_podsieci,
            dhcp_offer_maska
        );
    }

    if (opcje.ma_brame) {
        skopiuj_ip(
            brama_ip,
            opcje.brama
        );
    } else {
        skopiuj_ip(
            brama_ip,
            dhcp_offer_brama
        );
    }

    if (opcje.ma_dns) {
        skopiuj_ip(
            serwer_dns_ip,
            opcje.dns
        );
    } else {
        skopiuj_ip(
            serwer_dns_ip,
            dhcp_offer_dns
        );
    }

    /*
     * Chron przed patologiczna maska 0.0.0.0 z uszkodzonej odpowiedzi.
     */
    if (ip_jest_zero(
            maska_podsieci)) {

        maska_podsieci[0] = 255;
        maska_podsieci[1] = 255;
        maska_podsieci[2] = 255;
        maska_podsieci[3] = 0;
    }

    if (ip_jest_zero(
            serwer_dns_ip)) {

        serwer_dns_ip[0] = 8;
        serwer_dns_ip[1] = 8;
        serwer_dns_ip[2] = 8;
        serwer_dns_ip[3] = 8;
    }

    __atomic_store_n(
        &dhcp_otrzymano_ack,
        true,
        __ATOMIC_RELEASE
    );
}

void obsluz_dns(
    const WidokIPv4& widok,
    UdpNaglowek* udp,
    uint8_t* payload,
    size_t payload_len
) {
    if (!udp ||
        !payload ||
        payload_len <
            12U) {

        return;
    }

    if (!ip_rowne(
            widok.ip->zrodlo_ip,
            dns_oczekiwany_serwer) ||
        siec_na_host16(
            udp->port_zrodlowy) !=
            PORT_DNS ||
        siec_na_host16(
            udp->port_docelowy) !=
            dns_oczekiwany_port) {

        return;
    }

    const uint16_t id =
        czytaj_be16(
            payload + 0
        );

    const uint16_t flags =
        czytaj_be16(
            payload + 2
        );

    const uint16_t qdcount =
        czytaj_be16(
            payload + 4
        );

    const uint16_t ancount =
        czytaj_be16(
            payload + 6
        );

    const uint16_t nscount =
        czytaj_be16(payload + 8);
    const uint16_t arcount =
        czytaj_be16(payload + 10);

#if BURSZTYN_DEBUG_DNS_RAW
    wypisz_dns_u16("[DNS-RAW] txid=", id);
    wypisz_dns_u16("[DNS-RAW] flags=", flags);
    wypisz_dns_u16("[DNS-RAW] rcode=", flags & 0x000FU);
    wypisz_dns_u16("[DNS-RAW] qdcount=", qdcount);
    wypisz_dns_u16("[DNS-RAW] ancount=", ancount);
    wypisz_dns_u16("[DNS-RAW] nscount=", nscount);
    wypisz_dns_u16("[DNS-RAW] arcount=", arcount);
    wypisz_dns_hexdump(payload, payload_len);
#else
    (void)nscount;
    (void)arcount;
#endif

    if (id !=
            dns_oczekiwane_id ||
        (flags &
         0x8000U) == 0 ||
        (flags &
         0x000FU) != 0 ||
        qdcount == 0) {

        return;
    }

    size_t pos =
        12U;

    for (uint16_t i = 0;
         i < qdcount;
         ++i) {

        if (!dns_pomin_nazwe(
                payload,
                payload_len,
                &pos)) {

            return;
        }

        if (pos + 4U >
            payload_len) {

            return;
        }

        pos +=
            4U;
    }

    for (uint16_t i = 0;
         i < ancount;
         ++i) {

        if (!dns_pomin_nazwe(
                payload,
                payload_len,
                &pos)) {

            return;
        }

        if (pos + 10U >
            payload_len) {

            return;
        }

        const uint16_t typ =
            czytaj_be16(
                payload +
                pos
            );

        const uint16_t klasa =
            czytaj_be16(
                payload +
                pos +
                2U
            );

        const uint16_t rdlen =
            czytaj_be16(
                payload +
                pos +
                8U
            );

#if BURSZTYN_DEBUG_DNS_RAW
        const uint32_t ttl =
            (static_cast<uint32_t>(payload[pos + 4U]) << 24U) |
            (static_cast<uint32_t>(payload[pos + 5U]) << 16U) |
            (static_cast<uint32_t>(payload[pos + 6U]) << 8U) |
            static_cast<uint32_t>(payload[pos + 7U]);
        wypisz_dns_u16("[DNS-RAW] answer type=", typ);
        wypisz_dns_u16("[DNS-RAW] answer class=", klasa);
        wypisz_dns_u32("[DNS-RAW] answer ttl=", ttl);
        wypisz_dns_u16("[DNS-RAW] answer rdlength=", rdlen);
#endif

        pos +=
            10U;

        if (static_cast<size_t>(
                rdlen) >
            payload_len -
                pos) {

            return;
        }

        if (typ == 1 &&
            klasa == 1 &&
            rdlen == 4) {

#if BURSZTYN_DEBUG_DNS_RAW
            /* To jest RDATA z pakietu przed jakakolwiek interpretacja. */
            wypisz_ip_log("[DNS-RAW] A rdata=", payload + pos);
#endif

            skopiuj_ip(
                dns_resolved_ip,
                payload +
                    pos
            );

            if (!ip_jest_zero(
                    dns_resolved_ip)) {

                __atomic_store_n(
                    &dns_odebrano,
                    true,
                    __ATOMIC_RELEASE
                );

                return;
            }
        }

        pos +=
            rdlen;
    }
}

void obsluz_udp(
    const WidokIPv4& widok
) {
    UdpNaglowek* udp =
        nullptr;

    uint8_t* payload =
        nullptr;

    size_t payload_len =
        0;

    if (!udp_poprawny(
            widok,
            &udp,
            &payload,
            &payload_len)) {

        return;
    }
    __atomic_add_fetch(&udp_rx_packets, 1ULL, __ATOMIC_RELAXED);

    const uint16_t port_docelowy =
        siec_na_host16(
            udp->port_docelowy
        );

    if (port_docelowy ==
        PORT_DHCP_CLIENT) {

        obsluz_dhcp(
            widok,
            udp,
            payload,
            payload_len
        );

        return;
    }

    if (!konfiguracja_ipv4_gotowa ||
        !ip_rowne(
            widok.ip->cel_ip,
            nasz_ip)) {

        return;
    }

    if (port_docelowy ==
        dns_oczekiwany_port) {

        __atomic_add_fetch(&dns_rx_packets, 1ULL, __ATOMIC_RELAXED);

        obsluz_dns(
            widok,
            udp,
            payload,
            payload_len
        );
    }
}

void obsluz_icmp(
    uint8_t* ramka,
    size_t dlugosc_ramki,
    const WidokIPv4& widok
) {
    if (!ramka ||
        widok.payload_len <
            ICMP_ECHO_LEN) {

        return;
    }
    __atomic_add_fetch(&icmp_rx_packets, 1ULL, __ATOMIC_RELAXED);

    if (suma_kontrolna_bajty(
            widok.payload,
            widok.payload_len) != 0) {

        return;
    }

    IcmpEchoNaglowek* icmp =
        reinterpret_cast<IcmpEchoNaglowek*>(
            widok.payload
        );

    if (icmp->kod != 0) {
        return;
    }

    if (icmp->typ == 0) {
        if (!ip_rowne(
                widok.ip->cel_ip,
                nasz_ip) ||
            !ip_rowne(
                widok.ip->zrodlo_ip,
                oczekiwany_ping_ip) ||
            siec_na_host16(
                icmp->id) !=
                oczekiwany_ping_id ||
            siec_na_host16(
                icmp->sekwencja) !=
                oczekiwany_ping_seq) {

            return;
        }

        __atomic_store_n(
            &odebrano_pong,
            true,
            __ATOMIC_RELEASE
        );

        return;
    }

    if (icmp->typ != 8 ||
        !konfiguracja_ipv4_gotowa ||
        !ip_rowne(
            widok.ip->cel_ip,
            nasz_ip)) {

        return;
    }

    /*
     * Echo Reply tworzymy w osobnym buforze. Nie mutujemy DMA RX bufora
     * E1000, bo jego lifetime nalezy do sterownika.
     */
    const size_t icmp_len =
        widok.payload_len;

    const size_t ip_len =
        IPV4_MIN_LEN +
        icmp_len;

    if (ETH_LEN +
            ip_len >
        MAKS_RAMKA_ETH) {

        return;
    }

    uint8_t odpowiedz[
        ETH_LEN +
        MAKS_PAKIET_IPV4
    ] = {};

    EthNaglowek* rx_eth =
        reinterpret_cast<EthNaglowek*>(
            ramka
        );

    EthNaglowek* tx_eth =
        reinterpret_cast<EthNaglowek*>(
            odpowiedz
        );

    wypelnij_eth(
        tx_eth,
        rx_eth->zrodlo_mac,
        ETHERTYPE_IPV4
    );

    Ipv4Naglowek* tx_ip =
        reinterpret_cast<Ipv4Naglowek*>(
            odpowiedz +
            ETH_LEN
        );

    if (!wypelnij_ipv4(
            tx_ip,
            static_cast<uint16_t>(
                ip_len
            ),
            IP_PROTO_ICMP,
            nasz_ip,
            widok.ip->zrodlo_ip)) {

        return;
    }

    uint8_t* tx_icmp =
        odpowiedz +
        ETH_LEN +
        IPV4_MIN_LEN;

    kopiuj_bajty(
        tx_icmp,
        widok.payload,
        icmp_len
    );

    tx_icmp[0] =
        0;

    tx_icmp[2] =
        0;

    tx_icmp[3] =
        0;

    const uint16_t checksum =
        suma_kontrolna_bajty(
            tx_icmp,
            icmp_len
        );

    zapisz_be16(
        tx_icmp + 2,
        checksum
    );

    (void)dlugosc_ramki;

    (void)wyslij_ethernet(
        odpowiedz,
        ETH_LEN +
            ip_len
    );
}

void obsluz_tcp(
    const WidokIPv4& widok
) {
    if (widok.payload_len <
        TCP_MIN_LEN) {

        return;
    }

    TcpNaglowek* tcp =
        reinterpret_cast<TcpNaglowek*>(
            widok.payload
        );

    const uint8_t data_offset_words =
        tcp->przesuniecie_danych >>
        4;

    if (data_offset_words < 5) {
        return;
    }

    const size_t tcp_hdr_len =
        static_cast<size_t>(
            data_offset_words) *
        4U;

    if (tcp_hdr_len >
        widok.payload_len) {

        return;
    }

    if (suma_transport_ipv4(
            widok.ip->zrodlo_ip,
            widok.ip->cel_ip,
            IP_PROTO_TCP,
            widok.payload,
            static_cast<uint16_t>(
                widok.payload_len
            )
        ) != 0) {

        return;
    }

    if (!tcp_sesja_zajeta ||
        !ip_rowne(
            widok.ip->cel_ip,
            nasz_ip) ||
        !ip_rowne(
            widok.ip->zrodlo_ip,
            tcp_cel_ip) ||
        siec_na_host16(
            tcp->port_docelowy) !=
            tcp_nasz_port ||
        siec_na_host16(
            tcp->port_zrodlowy) !=
            tcp_cel_port) {

        return;
    }

    const uint8_t flagi =
        tcp->flagi;

    const uint32_t seq =
        siec_na_host32(
            tcp->numer_sekwencyjny
        );

    const uint32_t ack =
        siec_na_host32(
            tcp->numer_potwierdzenia
        );

    if ((flagi &
         TCP_FLAG_RST) != 0) {

        tcp_rst_odebrany =
            true;

        stan_tcp =
            StanTCP::ZAMKNIETY;

        return;
    }

    if (stan_tcp ==
        StanTCP::SYN_WYSLANY) {

        if ((flagi &
             (TCP_FLAG_SYN |
              TCP_FLAG_ACK)) !=
                (TCP_FLAG_SYN |
                 TCP_FLAG_ACK)) {

            return;
        }

        if (ack !=
            tcp_nasz_seq + 1U) {

            return;
        }

        tcp_nasz_ack =
            seq + 1U;

        ++tcp_nasz_seq;

        stan_tcp =
            StanTCP::USTANOWIONY;

        (void)tcp_wyslij_internal(
            TCP_FLAG_ACK,
            nullptr,
            0
        );

        return;
    }

    if (stan_tcp !=
            StanTCP::USTANOWIONY &&
        stan_tcp !=
            StanTCP::FIN_WYSLANY) {

        return;
    }

    const uint8_t* payload =
        widok.payload +
        tcp_hdr_len;

    const uint32_t payload_len =
        static_cast<uint32_t>(
            widok.payload_len -
            tcp_hdr_len
        );

    if (payload_len > 0) {
        tcp_przyjmij_payload(
            payload,
            payload_len,
            seq
        );

        if (!tcp_tryb_gniazda &&
            tcp_zapisano_bajtow > 0) {

            __atomic_store_n(
                &tcp_dane_odebrane,
                true,
                __ATOMIC_RELEASE
            );
        }
    }

    if ((flagi &
         TCP_FLAG_FIN) != 0) {

        /*
         * FIN zuzywa jeden numer sekwencyjny. Jesli pakiet zawieral payload,
         * tcp_przyjmij_payload przesunal ACK o jego dlugosc.
         *
         * Akceptujemy FIN tylko gdy lezy dokladnie na oczekiwanym koncu.
         */
        const uint32_t fin_seq =
            seq +
            payload_len;

        if (fin_seq ==
            tcp_nasz_ack) {

            ++tcp_nasz_ack;

            (void)tcp_wyslij_internal(
                TCP_FLAG_ACK,
                nullptr,
                0
            );

            stan_tcp =
                StanTCP::ZAMKNIETY;
        }

        return;
    }

    /*
     * FIN_WAIT: zwykle ACK naszego FIN, a potem FIN peer.
     * Sam ACK nie wymaga dodatkowej akcji.
     */
    (void)ack;
}

void obsluz_ipv4(
    uint8_t* ramka,
    size_t dlugosc
) {
    if (!ramka ||
        dlugosc <
            ETH_LEN +
            IPV4_MIN_LEN) {

        return;
    }

    WidokIPv4 widok{};

    if (!parsuj_ipv4(
            ramka +
                ETH_LEN,
            dlugosc -
                ETH_LEN,
            &widok)) {

        return;
    }
    __atomic_add_fetch(&ipv4_rx_packets, 1ULL, __ATOMIC_RELAXED);

    /*
     * DHCP musi byc dopuszczone zanim nasz_ip zostanie skonfigurowane.
     * Pozostale protokoly kierujemy tylko do lokalnego IPv4 albo broadcast.
     */
    if (widok.ip->protokol !=
        IP_PROTO_UDP) {

        if (!konfiguracja_ipv4_gotowa ||
            (!ip_rowne(
                 widok.ip->cel_ip,
                 nasz_ip) &&
             !ip_jest_broadcast(
                 widok.ip->cel_ip))) {

            return;
        }
    }

    switch (widok.ip->protokol) {
        case IP_PROTO_ICMP:
            obsluz_icmp(
                ramka,
                dlugosc,
                widok
            );
            break;

        case IP_PROTO_UDP:
            obsluz_udp(
                widok
            );
            break;

        case IP_PROTO_TCP:
            obsluz_tcp(
                widok
            );
            break;

        default:
            break;
    }
}

} // namespace

void obsluz_pakiet_sieciowy(
    uint8_t* pakiet,
    uint16_t dlugosc
) {
    if (!pakiet ||
        dlugosc <
            ETH_LEN) {

        return;
    }

    EthNaglowek* eth =
        reinterpret_cast<EthNaglowek*>(
            pakiet
        );

    const uint16_t typ =
        siec_na_host16(
            eth->typ
        );

    if (typ ==
        ETHERTYPE_ARP) {

        obsluz_arp(
            pakiet,
            dlugosc
        );

        return;
    }

    if (typ ==
        ETHERTYPE_IPV4) {

        obsluz_ipv4(
            pakiet,
            dlugosc
        );
    }
}
