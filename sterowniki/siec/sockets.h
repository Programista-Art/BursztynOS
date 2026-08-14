#pragma once
#include <stdint.h>
#include <stdbool.h>

/*
 * Zanim wdrożymy bibliotekę SSL (mbedTLS), musimy rozbić nasz monolityczny
 * proces pobierania HTTP na system ujednoliconych "Gniazd" (Sockets).
 * Biblioteki kryptograficzne oczekują niezależnego czytania i pisania,
 * aby móc zrealizować skomplikowany "TLS Handshake".
 */

struct GniazdoTCP {
    uint32_t id_gniazda;
    uint8_t  cel_ip[4];
    uint16_t port_zrodlowy;
    uint16_t port_docelowy;     // Dla HTTP to 80, dla HTTPS to 443
    uint32_t numer_sekwencyjny;
    uint32_t numer_potwierdzenia;
    bool     polaczone;
    
    // Specjalny wskaźnik dla przyszłej biblioteki szyfrującej
    void* kontekst_ssl;      // (mbedtls_ssl_context*)
};

extern "C" {
    // 1. Nawiązanie połączenia (Czyste TCP 3-way handshake)
    GniazdoTCP* tcp_polacz(uint8_t cel_ip[4], uint16_t port);
    
    // 2. Wysyłanie surowych bajtów (Używane przez mbedTLS do wysyłania szyfru)
    bool tcp_wyslij(GniazdoTCP* gniazdo, const uint8_t* dane, uint32_t dlugosc);
    
    // 3. Odbieranie surowych bajtów (Używane przez mbedTLS do dekodowania HTTPS)
    int tcp_odbierz(GniazdoTCP* gniazdo, uint8_t* bufor, uint32_t max_dlugosc);
    
    // 4. Zamknięcie połączenia
    void tcp_zamknij(GniazdoTCP* gniazdo);
}