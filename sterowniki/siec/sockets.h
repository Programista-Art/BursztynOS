/*
 * Bursztyn OS - sockets.h
 *
 * Minimalny publiczny interfejs strumienia TCP dla warstw wyzszych
 * (m.in. TLS/mbedTLS).
 *
 * WAZNE:
 * Obecna implementacja siec.cpp utrzymuje JEDNA aktywna sesje TCP.
 * Nie istnieja jeszcze niezalezne obiekty GniazdoTCP ani tablica socketow.
 *
 * Dlatego ten naglowek swiadomie NIE wystawia struktury GniazdoTCP*.
 * Wczesniejsza wersja deklarowala API:
 *
 *   tcp_polacz(...)
 *   tcp_wyslij(GniazdoTCP*, ...)
 *   tcp_odbierz(GniazdoTCP*, ...)
 *   tcp_zamknij(GniazdoTCP*)
 *
 * ale takie symbole nie istnieja w aktualnym siec.cpp. Uzycie tamtego
 * naglowka prowadziloby do bledow linkera albo stworzenia drugiego,
 * niespojnego modelu TCP.
 *
 * Faktyczne ABI:
 *
 *   tcp_gniazdo_polacz()
 *   tcp_gniazdo_wyslij()
 *   tcp_gniazdo_odbierz()
 *   tcp_gniazdo_otwarte()
 *   tcp_gniazdo_zamknij()
 *
 * Docelowo, gdy stos TCP otrzyma wiele rownoleglych polaczen, ten interfejs
 * mozna rozszerzyc o opaque handle / ID gniazda bez ujawniania wewnetrznego
 * stanu TCP.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. STALE PUBLICZNEGO ABI
 * ========================================================================= */

/*
 * IPv4 w obecnym stosie Bursztyna ma zawsze 4 bajty.
 */
#define SOCKETS_IPV4_BAJTOW UINT32_C(4)

/*
 * Port TCP 0 jest zarezerwowany i odrzucany przez tcp_gniazdo_polacz().
 */
#define SOCKETS_TCP_PORT_MIN UINT16_C(1)
#define SOCKETS_TCP_PORT_MAX UINT16_C(65535)

/*
 * Kody zwracane przez funkcje strumieniowe.
 *
 * SEND:
 *   >= 0  liczba faktycznie wyslanych bajtow
 *   -1    blad
 *
 * RECV:
 *   > 0   liczba odebranych bajtow
 *    0    EOF - peer zamknal TCP i lokalny bufor jest pusty
 *   -1    blad argumentow/stanu
 *   -2    polaczenie nadal otwarte, ale chwilowo brak danych
 *
 * -2 jest odpowiednikiem "would block" dla pollingowego stosu i jest
 * mapowane przez tls.cpp na MBEDTLS_ERR_SSL_WANT_READ.
 */
#define SOCKETS_TCP_BLAD        (-1)
#define SOCKETS_TCP_BRAK_DANYCH (-2)

/* =========================================================================
 * 2. PUBLICZNE ABI TCP
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Nawiązuje jedna aktywna sesje TCP.
 *
 * cel_ip:
 *   wskaznik na dokladnie 4 bajty IPv4.
 *
 * port:
 *   1..65535, w byte-order hosta.
 *
 * Zwraca:
 *   true  - zakonczono SYN -> SYN/ACK -> ACK,
 *   false - bledne argumenty, brak konfiguracji/routingu/ARP, timeout,
 *           RST albo inna sesja TCP jest juz zajeta.
 *
 * UWAGA:
 * Implementacja nie kopiuje publicznego obiektu socketu do wywolujacego.
 * Stan sesji pozostaje prywatny w siec.cpp.
 */
bool tcp_gniazdo_polacz(
    uint8_t* cel_ip,
    uint16_t port
);

/*
 * Wysyla surowe bajty przez aktualna sesje TCP.
 *
 * dane:
 *   bufor tylko do odczytu.
 *
 * dlugosc:
 *   0 jest dozwolone i zwraca 0.
 *
 * Zwraca:
 *   >=0 - liczba faktycznie wyslanych bajtow,
 *   -1  - blad.
 *
 * Funkcja MOZE zwrocic sukces czesciowy. Warstwa wyzsza musi wtedy
 * ponowic wysylanie pozostalej czesci bufora.
 *
 * Obecny stos nie gwarantuje jeszcze pelnego TCP production-grade:
 * retransmisji z timerem RTO, congestion control, SACK itd.
 */
int tcp_gniazdo_wyslij(
    const uint8_t* dane,
    uint32_t dlugosc
);

/*
 * Odbiera surowe bajty z aktualnej sesji TCP.
 *
 * dane:
 *   zapisywalny bufor wywolujacego.
 *
 * maksymalna_dlugosc:
 *   musi byc > 0.
 *
 * Zwraca:
 *   >0  - liczba zapisanych bajtow,
 *    0  - peer zamknal transport i nie zostaly zadne dane,
 *   -1  - blad,
 *   -2  - brak danych w tej chwili; sesja pozostaje otwarta.
 *
 * Funkcja ma semantyke pollingowa/non-blocking z punktu widzenia TLS:
 * -2 nie oznacza utraty polaczenia.
 */
int tcp_gniazdo_odbierz(
    uint8_t* dane,
    uint32_t maksymalna_dlugosc
);

/*
 * Zwraca true tylko dla aktualnej sesji w stanie ESTABLISHED.
 */
bool tcp_gniazdo_otwarte();

/*
 * Probuje grzecznie zamknac aktualna sesje TCP (FIN), nastepnie zwalnia
 * prywatny stan transportu. Funkcja jest bezpieczna rowniez wtedy, gdy
 * nie ma aktywnej sesji.
 */
void tcp_gniazdo_zamknij();

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 * 3. HELPERY HEADER-ONLY
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Czysta walidacja adresu wskaznikowego.
 *
 * Nie okresla, czy adres IPv4 jest routowalny ani czy host istnieje.
 */
inline constexpr bool sockets_ipv4_wskaznik_poprawny(
    const uint8_t ip[SOCKETS_IPV4_BAJTOW]
) noexcept {
    return
        ip != nullptr;
}

/*
 * Port 0 nie jest poprawnym portem docelowym tego API.
 */
inline constexpr bool sockets_tcp_port_poprawny(
    uint16_t port
) noexcept {
    return
        port >= SOCKETS_TCP_PORT_MIN;
}

/*
 * Klasyfikacja kodow zwracanych przez tcp_gniazdo_odbierz().
 */
inline constexpr bool sockets_tcp_wynik_to_dane(
    int wynik
) noexcept {
    return
        wynik > 0;
}

inline constexpr bool sockets_tcp_wynik_to_eof(
    int wynik
) noexcept {
    return
        wynik == 0;
}

inline constexpr bool sockets_tcp_wynik_to_brak_danych(
    int wynik
) noexcept {
    return
        wynik ==
        SOCKETS_TCP_BRAK_DANYCH;
}

inline constexpr bool sockets_tcp_wynik_to_blad(
    int wynik
) noexcept {
    return
        wynik ==
        SOCKETS_TCP_BLAD;
}

/* =========================================================================
 * 4. KONTROLA ABI
 * ========================================================================= */

static_assert(
    sizeof(uint8_t) == 1,
    "Sockets wymaga 8-bitowego uint8_t"
);

static_assert(
    sizeof(uint16_t) == 2,
    "Sockets wymaga 16-bitowego uint16_t"
);

static_assert(
    sizeof(uint32_t) == 4,
    "Sockets wymaga 32-bitowego uint32_t"
);

static_assert(
    SOCKETS_IPV4_BAJTOW == 4U,
    "Obecny stos sieciowy obsluguje IPv4"
);

static_assert(
    SOCKETS_TCP_BLAD < 0,
    "Kod bledu TCP musi byc ujemny"
);

static_assert(
    SOCKETS_TCP_BRAK_DANYCH < 0,
    "Kod would-block TCP musi byc ujemny"
);

static_assert(
    SOCKETS_TCP_BLAD != SOCKETS_TCP_BRAK_DANYCH,
    "Kody bledu i chwilowego braku danych musza byc rozne"
);

#endif /* __cplusplus */
