/*
 * Bursztyn OS - HTTPS / TLS
 *
 * Warstwa TLS korzystajaca z mbedTLS i pojedynczego strumienia TCP
 * udostepnianego przez siec.cpp.
 *
 * BEZPIECZENSTWO:
 *
 *   - HTTPS dziala w trybie MBEDTLS_SSL_VERIFY_REQUIRED,
 *   - nazwa hosta jest przekazywana przez mbedtls_ssl_set_hostname(),
 *     wiec podlega walidacji certyfikatu i jest uzywana jako SNI,
 *   - brak magazynu zaufanych CA powoduje odmowe polaczenia (fail-closed),
 *   - certyfikat jest oznaczany jako zaufany tylko po zakonczonym
 *     handshake i mbedtls_ssl_get_verify_result() == 0,
 *   - nie istnieje juz automatyczny fallback do VERIFY_NONE,
 *   - domena i sciezka HTTP sa walidowane przed zbudowaniem requestu,
 *   - WANT_READ/WANT_WRITE maja ograniczone petle oczekiwania,
 *   - brak TLS close_notify jest traktowany jako potencjalnie uciety
 *     transfer, a nie jako sukces,
 *   - przepelnienie bufora odpowiedzi jest wykrywane i zwraca blad,
 *   - tcp_zapisano_bajtow jest publikowane dopiero po pelnym sukcesie.
 *
 * MAGAZYN CA:
 *
 * Bursztyn OS powinien dostarczyc w osobnym module funkcje:
 *
 *   extern "C" bool bursztyn_tls_pobierz_magazyn_ca(
 *       const unsigned char** dane,
 *       size_t* dlugosc
 *   );
 *
 * Funkcja jest opcjonalnym weak hookiem. Jezeli nie istnieje albo zwroci
 * false, HTTPS zostanie bezpiecznie odrzucony.
 *
 * Dane moga byc:
 *
 *   - jedna lub wiele certyfikatow PEM,
 *   - pojedynczym certyfikatem DER.
 *
 * Dla PEM przekazana dlugosc musi obejmowac koncowy bajt '\0', zgodnie
 * z kontraktem mbedtls_x509_crt_parse().
 *
 * Docelowo magazyn CA powinien byc ladowany z podpisanego komponentu
 * systemowego Bursztyn OS, a jego aktualizacja powinna podlegac PZB.
 */

#include "siec.h"
#include "e1000.h"
#include "grafika.h"
#include "sterowniki/czas/hpet.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"

/* =========================================================================
 * 1. HOOK MAGAZYNU CA
 * ========================================================================= */

/*
 * Weak declaration:
 *   brak implementacji nie powoduje bledu linkera,
 *   ale HTTPS pozostaje wtedy bezpiecznie wylaczone.
 */
extern "C" bool bursztyn_tls_pobierz_magazyn_ca(
    const unsigned char** dane,
    size_t* dlugosc
);

/* Minimalny, jawny magazyn zaufania zachowany z ostatniej dzialajacej
   wersji HTTPS. To publiczny ISRG Root X1, nie certyfikat serwera i nie
   obejscie VERIFY_REQUIRED. */
static const unsigned char ISRG_ROOT_X1[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

extern "C" bool bursztyn_tls_pobierz_magazyn_ca(
    const unsigned char** dane,size_t* dlugosc) {
    if(!dane||!dlugosc)return false;
    *dane=ISRG_ROOT_X1;
    *dlugosc=sizeof(ISRG_ROOT_X1);
    return true;
}

/* =========================================================================
 * 2. STAN PUBLICZNY
 * ========================================================================= */

namespace {

bool ostatni_certyfikat_zaufany =
    false;

} // namespace

extern "C" bool kernel_tls_certyfikat_zaufany() {
    return
        __atomic_load_n(
            &ostatni_certyfikat_zaufany,
            __ATOMIC_ACQUIRE
        );
}

/* =========================================================================
 * 3. STALE
 * ========================================================================= */

namespace {

constexpr uint16_t PORT_HTTPS =
    443U;

constexpr size_t TLS_MAX_DOMENA =
    253U;

constexpr size_t TLS_MAX_LABEL =
    63U;

constexpr size_t TLS_MAX_SCIEZKA_HTTP =
    2047U;

constexpr size_t TLS_HTTP_REQUEST_MAX =
    4096U;

/*
 * Ograniczamy pojedyncze wywolanie BIO do 16 KiB.
 * TCP i mbedTLS poprawnie obsluguja czesciowe read/write.
 */
constexpr size_t TLS_BIO_FRAGMENT_MAX =
    16U * 1024U;

constexpr uint64_t TLS_HANDSHAKE_TIMEOUT_MS = 15000;
constexpr uint64_t TLS_IO_TIMEOUT_MS = 15000;
constexpr uint64_t TLS_CLOSE_TIMEOUT_MS = 1000;

uint64_t tls_deadline(uint64_t timeout_ms) {
    const uint64_t now = czas_monotoniczny_ms();
    return now > UINT64_MAX - timeout_ms ? UINT64_MAX : now + timeout_ms;
}

/* =========================================================================
 * 4. PROSTE FUNKCJE POMOCNICZE
 * ========================================================================= */

size_t min_size(
    size_t a,
    size_t b
) {
    return
        a < b
            ? a
            : b;
}

size_t tekst_dlugosc_limit(
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
        pojemnosc == 0 ||
        *pozycja >= pojemnosc) {

        return false;
    }

    size_t p =
        *pozycja;

    for (size_t i = 0;
         tekst[i] != '\0';
         ++i) {

        if (p + 1U >=
            pojemnosc) {

            return false;
        }

        cel[p++] =
            tekst[i];
    }

    cel[p] =
        '\0';

    *pozycja =
        p;

    return true;
}

bool znak_domeny_poprawny(
    char c
) {
    return
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-';
}

bool domena_poprawna(
    const char* domena
) {
    if (!domena) {
        return false;
    }

    const size_t len =
        tekst_dlugosc_limit(
            domena,
            TLS_MAX_DOMENA + 1U
        );

    if (len == 0 ||
        len > TLS_MAX_DOMENA) {

        return false;
    }

    /*
     * Akceptujemy standardowe ASCII hostname/FQDN. IDN powinien trafic
     * tutaj jako ASCII Punycode (xn--...).
     */
    size_t label_start =
        0;

    for (size_t i = 0;
         i <= len;
         ++i) {

        const bool koniec =
            i == len ||
            domena[i] == '.';

        if (!koniec) {
            if (!znak_domeny_poprawny(
                    domena[i])) {

                return false;
            }

            continue;
        }

        const size_t label_len =
            i -
            label_start;

        if (label_len == 0 ||
            label_len > TLS_MAX_LABEL) {

            return false;
        }

        if (domena[label_start] == '-' ||
            domena[i - 1U] == '-') {

            return false;
        }

        label_start =
            i + 1U;
    }

    return true;
}

bool sciezka_http_poprawna(
    const char* sciezka
) {
    if (!sciezka) {
        return false;
    }

    const size_t len =
        tekst_dlugosc_limit(
            sciezka,
            TLS_MAX_SCIEZKA_HTTP + 1U
        );

    if (len == 0 ||
        len > TLS_MAX_SCIEZKA_HTTP ||
        sciezka[0] != '/') {

        return false;
    }

    for (size_t i = 0;
         i < len;
         ++i) {

        const uint8_t c =
            static_cast<uint8_t>(
                sciezka[i]
            );

        /*
         * Request-target powinien byc ASCII/percent-encoded.
         * Blokujemy kontrolne, spacje, DEL i surowe znaki spoza ASCII.
         * Tym samym CRLF injection jest niemozliwy.
         */
        if (c <= 0x20U ||
            c >= 0x7FU) {

            return false;
        }
    }

    return true;
}

bool zbuduj_zadanie_http(
    const char* domena,
    const char* sciezka,
    char* wynik,
    size_t pojemnosc,
    size_t* dlugosc
) {
    if (!domena ||
        !sciezka ||
        !wynik ||
        !dlugosc ||
        pojemnosc == 0) {

        return false;
    }

    if (!domena_poprawna(
            domena) ||
        !sciezka_http_poprawna(
            sciezka)) {

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
            "\r\nUser-Agent: BursztynOS/0.1\r\n"
            "Accept: */*\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n\r\n")) {

        return false;
    }

    *dlugosc =
        p;

    return true;
}

/* =========================================================================
 * 5. LOGOWANIE BLEDOW
 * ========================================================================= */

void liczba_hex32(
    uint32_t wartosc,
    char out[9]
) {
    static constexpr char HEX[] =
        "0123456789ABCDEF";

    for (int i = 7;
         i >= 0;
         --i) {

        out[i] =
            HEX[
                wartosc &
                0x0FU
            ];

        wartosc >>=
            4;
    }

    out[8] =
        '\0';
}

uint32_t kod_bezwzgledny(
    int kod
) {
    /*
     * Dziala rowniez dla INT_MIN bez signed overflow.
     */
    const uint32_t u =
        static_cast<uint32_t>(
            kod
        );

    return
        kod < 0
            ? UINT32_C(0) - u
            : u;
}

void tls_log_blad(
    const char* etap,
    int kod
) {
    char log[128] = {};
    size_t p = 0;

    (void)dopisz_tekst(
        log,
        sizeof(log),
        &p,
        "[TLS] Blad "
    );

    if (etap) {
        (void)dopisz_tekst(
            log,
            sizeof(log),
            &p,
            etap
        );
    }

    (void)dopisz_tekst(
        log,
        sizeof(log),
        &p,
        " (0x"
    );

    char hex[9] = {};

    liczba_hex32(
        kod_bezwzgledny(
            kod
        ),
        hex
    );

    (void)dopisz_tekst(
        log,
        sizeof(log),
        &p,
        hex
    );

    (void)dopisz_tekst(
        log,
        sizeof(log),
        &p,
        ")"
    );

    wypisz_log(
        log
    );
}

} // namespace

/*
 * Zachowana publiczna nazwa starej funkcji dla kompatybilnosci z
 * ewentualnym kodem diagnostycznym.
 */
void wypisz_blad_mbedtls(
    int ret
) {
    tls_log_blad(
        "mbedTLS",
        ret
    );
}

namespace {

/* =========================================================================
 * 6. POMPOWANIE E1000 / KODY RETRY
 * ========================================================================= */

void tls_pompuj_siec() {
    e1000_obsluz_odbior();

    asm volatile(
        "pause"
        :
        :
        : "memory"
    );
}

bool tls_kod_ponow(
    int kod
) {
    if (kod ==
            MBEDTLS_ERR_SSL_WANT_READ ||
        kod ==
            MBEDTLS_ERR_SSL_WANT_WRITE) {

        return true;
    }

#ifdef MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS
    if (kod ==
        MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS) {

        return true;
    }
#endif

#ifdef MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS
    if (kod ==
        MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS) {

        return true;
    }
#endif

    return false;
}

/* =========================================================================
 * 7. BIO mbedTLS -> Bursztyn TCP
 * ========================================================================= */

int tls_wyslij(
    void*,
    const unsigned char* dane,
    size_t dlugosc
) {
    if (!dane &&
        dlugosc != 0) {

        return
            MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    if (dlugosc == 0) {
        return 0;
    }

    const size_t fragment =
        min_size(
            dlugosc,
            TLS_BIO_FRAGMENT_MAX
        );

    const int wynik =
        tcp_gniazdo_wyslij(
            dane,
            static_cast<uint32_t>(
                fragment
            )
        );

    if (wynik == -2) {
        return
            MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    if (wynik < 0) {
        return
            MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    /*
     * tcp_gniazdo_wyslij() moze zwrocic czesciowy sukces.
     * mbedTLS dopuszcza taki wynik i wywola callback ponownie.
     */
    return wynik;
}

int tls_odbierz(
    void*,
    unsigned char* dane,
    size_t dlugosc
) {
    if (!dane ||
        dlugosc == 0) {

        return
            MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    const size_t fragment =
        min_size(
            dlugosc,
            TLS_BIO_FRAGMENT_MAX
        );

    const int wynik =
        tcp_gniazdo_odbierz(
            dane,
            static_cast<uint32_t>(
                fragment
            )
        );

    if (wynik == -2) {
        return
            MBEDTLS_ERR_SSL_WANT_READ;
    }

    if (wynik < 0) {
        return
            MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    /*
     * 0 oznacza EOF transportu TCP.
     * mbedTLS odroznia pozniej taki EOF od TLS close_notify.
     */
    return wynik;
}

/* =========================================================================
 * 8. TRUST STORE
 * ========================================================================= */

bool tls_zawiera_pem(
    const unsigned char* dane,
    size_t dlugosc
) {
    static constexpr char MARKER[] =
        "-----BEGIN CERTIFICATE-----";

    constexpr size_t MARKER_LEN =
        sizeof(MARKER) - 1U;

    if (!dane ||
        dlugosc <
            MARKER_LEN) {

        return false;
    }

    for (size_t i = 0;
         i + MARKER_LEN <=
            dlugosc;
         ++i) {

        bool pasuje =
            true;

        for (size_t j = 0;
             j <
                MARKER_LEN;
             ++j) {

            if (dane[i + j] !=
                static_cast<unsigned char>(
                    MARKER[j])) {

                pasuje =
                    false;
                break;
            }
        }

        if (pasuje) {
            return true;
        }
    }

    return false;
}

bool tls_zaladuj_ca(
    mbedtls_x509_crt* ca
) {
    if (!ca) {
        return false;
    }

    const unsigned char* dane =
        nullptr;

    size_t dlugosc =
        0;

    if (!bursztyn_tls_pobierz_magazyn_ca(
            &dane,
            &dlugosc) ||
        !dane ||
        dlugosc == 0) {

        wypisz_log(
            "[TLS] Magazyn CA jest pusty lub niedostepny."
        );

        return false;
    }

    /*
     * PEM musi zawierac NUL w dlugosci przekazanej do parsera.
     * DER moze naturalnie zawierac zera i nie wymaga terminatora.
     */
    if (tls_zawiera_pem(
            dane,
            dlugosc) &&
        dane[
            dlugosc - 1U] !=
            '\0') {

        wypisz_log(
            "[TLS] Magazyn CA PEM nie zawiera koncowego NUL."
        );

        return false;
    }

    const int wynik =
        mbedtls_x509_crt_parse(
            ca,
            dane,
            dlugosc
        );

    /*
     * mbedtls_x509_crt_parse() moze zwrocic dodatnia liczbe certyfikatow,
     * ktorych nie udalo sie sparsowac. Dla trust store wymagamy pelnego
     * sukcesu, a nie "czesciowo dobrego" zestawu.
     */
    if (wynik != 0) {
        tls_log_blad(
            "parsowania magazynu CA",
            wynik
        );

        return false;
    }

    return true;
}

/* =========================================================================
 * 9. HANDSHAKE
 * ========================================================================= */

bool tls_handshake(
    mbedtls_ssl_context* ssl
) {
    if (!ssl) {
        return false;
    }

    const uint64_t deadline=tls_deadline(TLS_HANDSHAKE_TIMEOUT_MS);
    while(czas_monotoniczny_ms()<deadline) {

        const int wynik =
            mbedtls_ssl_handshake(
                ssl
            );

        if (wynik == 0) {
            return true;
        }

        if (!tls_kod_ponow(
                wynik)) {

            tls_log_blad(
                "handshake",
                wynik
            );

            return false;
        }

        tls_pompuj_siec();
    }

    wypisz_log(
        "[TLS] Timeout podczas handshake."
    );

    return false;
}

/* =========================================================================
 * 10. SSL WRITE
 * ========================================================================= */

bool tls_wyslij_calosc(
    mbedtls_ssl_context* ssl,
    const unsigned char* dane,
    size_t dlugosc
) {
    if (!ssl ||
        (!dane &&
         dlugosc != 0)) {

        return false;
    }

    size_t wyslano =
        0;

    uint64_t deadline=tls_deadline(TLS_IO_TIMEOUT_MS);

    while (wyslano <
           dlugosc) {

        const size_t pozostalo =
            dlugosc -
            wyslano;

        const int wynik =
            mbedtls_ssl_write(
                ssl,
                dane +
                    wyslano,
                pozostalo
            );

        if (wynik > 0) {
            const size_t n =
                static_cast<size_t>(
                    wynik
                );

            if (n >
                pozostalo) {

                return false;
            }

            wyslano +=
                n;

            deadline=tls_deadline(TLS_IO_TIMEOUT_MS);

            tls_pompuj_siec();

            continue;
        }

        if (wynik == 0) {
            if (czas_monotoniczny_ms() >= deadline) {

                wypisz_log(
                    "[TLS] Timeout: ssl_write bez postepu."
                );

                return false;
            }

            tls_pompuj_siec();
            continue;
        }

        if (!tls_kod_ponow(
                wynik)) {

            tls_log_blad(
                "ssl_write",
                wynik
            );

            return false;
        }

        if (czas_monotoniczny_ms() >= deadline) {

            wypisz_log(
                "[TLS] Timeout podczas wysylania HTTPS."
            );

            return false;
        }

        tls_pompuj_siec();
    }

    return true;
}

/* =========================================================================
 * 11. SSL READ
 * ========================================================================= */

enum class WynikOdczytuTLS {
    SUKCES_CLOSE_NOTIFY,
    BLAD,
    PRZEPELNIENIE
};

WynikOdczytuTLS tls_odbierz_odpowiedz(
    mbedtls_ssl_context* ssl,
    char* bufor,
    uint32_t max_dlugosc,
    uint32_t* odebrano_wyj
) {
    if (!ssl ||
        !bufor ||
        !odebrano_wyj ||
        max_dlugosc < 2U) {

        return
            WynikOdczytuTLS::BLAD;
    }

    uint32_t odebrano =
        0;

    uint64_t deadline=tls_deadline(TLS_IO_TIMEOUT_MS);

    for (;;) {
        if (odebrano + 1U >=
            max_dlugosc) {

            /*
             * Bufor jest pelny. Nie mozemy oglosic sukcesu, dopoki nie
             * sprawdzimy, czy peer w tym momencie wyslal clean close_notify.
             *
             * Jeden dodatkowy bajt aplikacyjny oznacza truncation.
             */
            unsigned char test =
                0;

            const int wynik =
                mbedtls_ssl_read(
                    ssl,
                    &test,
                    1
                );

            if (wynik >
                0) {

                return
                    WynikOdczytuTLS::PRZEPELNIENIE;
            }

            if (wynik ==
                MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {

                bufor[odebrano] =
                    '\0';

                *odebrano_wyj =
                    odebrano;

                return
                    WynikOdczytuTLS::SUKCES_CLOSE_NOTIFY;
            }

            /*
             * Zwykle EOF transportu bez close_notify moze oznaczac uciecie
             * zaszyfrowanego strumienia. Bez parsera Content-Length/chunked
             * nie potrafimy udowodnic kompletnosci odpowiedzi.
             */
            if (wynik == 0) {
                wypisz_log(
                    "[TLS] TCP zamkniete bez TLS close_notify - odpowiedz odrzucona jako potencjalnie ucieta."
                );

                return
                    WynikOdczytuTLS::BLAD;
            }

            if (!tls_kod_ponow(
                    wynik)) {

                tls_log_blad(
                    "ssl_read po wypelnieniu bufora",
                    wynik
                );

                return
                    WynikOdczytuTLS::BLAD;
            }

            if (czas_monotoniczny_ms() >= deadline) {

                return
                    WynikOdczytuTLS::PRZEPELNIENIE;
            }

            tls_pompuj_siec();
            continue;
        }

        const uint32_t pozostalo =
            max_dlugosc -
            odebrano -
            1U;

        const int wynik =
            mbedtls_ssl_read(
                ssl,
                reinterpret_cast<unsigned char*>(
                    bufor
                ) +
                    odebrano,
                pozostalo
            );

        if (wynik > 0) {
            const uint32_t n =
                static_cast<uint32_t>(
                    wynik
                );

            if (n >
                pozostalo) {

                return
                    WynikOdczytuTLS::BLAD;
            }

            odebrano +=
                n;

            deadline=tls_deadline(TLS_IO_TIMEOUT_MS);

            /*
             * Utrzymuj NUL dla bezpiecznej diagnostyki tekstowej.
             * tcp_zapisano_bajtow nadal przechowuje prawdziwa dlugosc.
             */
            bufor[odebrano] =
                '\0';

            tls_pompuj_siec();

            continue;
        }

        if (wynik ==
            MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {

            bufor[odebrano] =
                '\0';

            *odebrano_wyj =
                odebrano;

            return
                WynikOdczytuTLS::SUKCES_CLOSE_NOTIFY;
        }

        if (wynik == 0) {
            wypisz_log(
                "[TLS] Polaczenie zakonczone bez TLS close_notify - fail-closed."
            );

            return
                WynikOdczytuTLS::BLAD;
        }

        if (!tls_kod_ponow(
                wynik)) {

            tls_log_blad(
                "ssl_read",
                wynik
            );

            return
                WynikOdczytuTLS::BLAD;
        }

        if (czas_monotoniczny_ms() >= deadline) {

            wypisz_log(
                "[TLS] Timeout podczas odbierania odpowiedzi HTTPS."
            );

            return
                WynikOdczytuTLS::BLAD;
        }

        tls_pompuj_siec();
    }
}

/* =========================================================================
 * 12. CLOSE_NOTIFY
 * ========================================================================= */

void tls_wyslij_close_notify(
    mbedtls_ssl_context* ssl
) {
    if (!ssl ||
        !tcp_gniazdo_otwarte()) {

        return;
    }

    const uint64_t deadline=tls_deadline(TLS_CLOSE_TIMEOUT_MS);
    while(czas_monotoniczny_ms()<deadline) {

        const int wynik =
            mbedtls_ssl_close_notify(
                ssl
            );

        if (wynik == 0) {
            return;
        }

        if (!tls_kod_ponow(
                wynik)) {

            /*
             * Close jest best-effort. Sesja i tak zostanie zniszczona.
             */
            return;
        }

        tls_pompuj_siec();
    }
}

/* =========================================================================
 * 13. PERSONALIZACJA DRBG
 * ========================================================================= */

void przygotuj_personalizacje(
    const char* domena,
    unsigned char* bufor,
    size_t pojemnosc,
    size_t* dlugosc
) {
    if (!bufor ||
        !dlugosc ||
        pojemnosc == 0) {

        return;
    }

    const char* prefix =
        "BursztynOS-TLS-";

    size_t p =
        0;

    while (prefix[p] != '\0' &&
           p <
               pojemnosc) {

        bufor[p] =
            static_cast<unsigned char>(
                prefix[p]
            );

        ++p;
    }

    if (domena) {
        for (size_t i = 0;
             domena[i] != '\0' &&
             p < pojemnosc;
             ++i) {

            bufor[p++] =
                static_cast<unsigned char>(
                    domena[i]
                );
        }
    }

    *dlugosc =
        p;
}

} // namespace

/* =========================================================================
 * 14. HTTPS
 * ========================================================================= */

extern "C" bool kernel_siec_pobierz_https(
    uint8_t* cel_ip,
    const char* domena,
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    /*
     * Stan eksportowany przez syscall/TLS status jest zerowany zanim
     * dotkniemy sieci.
     */
    __atomic_store_n(
        &ostatni_certyfikat_zaufany,
        false,
        __ATOMIC_RELEASE
    );

    tcp_zapisano_bajtow =
        0;

    if (bufor &&
        max_dlugosc != 0) {

        bufor[0] =
            '\0';
    }

    if (!cel_ip ||
        !domena ||
        !sciezka ||
        !bufor ||
        max_dlugosc < 2U) {

        return false;
    }

    if (!domena_poprawna(
            domena) ||
        !sciezka_http_poprawna(
            sciezka)) {

        wypisz_log(
            "[TLS] Nieprawidlowa domena lub sciezka HTTPS."
        );

        return false;
    }

    /*
     * Konteksty sa inicjalizowane przed pierwszym mozliwym cleanup.
     */
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt ca;

    mbedtls_ssl_init(
        &ssl
    );

    mbedtls_ssl_config_init(
        &config
    );

    mbedtls_ctr_drbg_init(
        &drbg
    );

    mbedtls_entropy_init(
        &entropy
    );

    mbedtls_x509_crt_init(
        &ca
    );

    bool sukces =
        false;

    bool handshake_gotowy =
        false;

    bool tcp_polaczone =
        false;

    uint32_t odebrano =
        0;

    do {
        /*
         * Trust store ladujemy PRZED otwarciem TCP. Brak CA nie powoduje
         * nawet wyslania ClientHello.
         */
        if (!tls_zaladuj_ca(
                &ca)) {

            break;
        }

        unsigned char personalizacja[
            320
        ] = {};

        size_t personalizacja_len =
            0;

        przygotuj_personalizacje(
            domena,
            personalizacja,
            sizeof(personalizacja),
            &personalizacja_len
        );

        int wynik =
            mbedtls_ctr_drbg_seed(
                &drbg,
                mbedtls_entropy_func,
                &entropy,
                personalizacja,
                personalizacja_len
            );

        if (wynik != 0) {
            tls_log_blad(
                "inicjalizacji DRBG",
                wynik
            );

            break;
        }

        wynik =
            mbedtls_ssl_config_defaults(
                &config,
                MBEDTLS_SSL_IS_CLIENT,
                MBEDTLS_SSL_TRANSPORT_STREAM,
                MBEDTLS_SSL_PRESET_DEFAULT
            );

        if (wynik != 0) {
            tls_log_blad(
                "ssl_config_defaults",
                wynik
            );

            break;
        }

        /*
         * Twarda polityka klienta:
         * certyfikat serwera MUSI przejsc walidacje.
         */
        mbedtls_ssl_conf_authmode(
            &config,
            MBEDTLS_SSL_VERIFY_REQUIRED
        );

        mbedtls_ssl_conf_ca_chain(
            &config,
            &ca,
            nullptr
        );

        mbedtls_ssl_conf_rng(
            &config,
            mbedtls_ctr_drbg_random,
            &drbg
        );

        wynik =
            mbedtls_ssl_setup(
                &ssl,
                &config
            );

        if (wynik != 0) {
            tls_log_blad(
                "ssl_setup",
                wynik
            );

            break;
        }

        /*
         * Ta sama nazwa:
         *   - SNI,
         *   - expected hostname do X.509.
         */
        wynik =
            mbedtls_ssl_set_hostname(
                &ssl,
                domena
            );

        if (wynik != 0) {
            tls_log_blad(
                "ustawiania SNI/hostname",
                wynik
            );

            break;
        }

        mbedtls_ssl_set_bio(
            &ssl,
            nullptr,
            tls_wyslij,
            tls_odbierz,
            nullptr
        );

        if (!tcp_gniazdo_polacz(
                cel_ip,
                PORT_HTTPS)) {

            wypisz_log(
                "[TLS] Nie udalo sie otworzyc TCP/443."
            );

            break;
        }

        tcp_polaczone =
            true;

        wypisz_log(
            "[TLS] Rozpoczynam handshake z VERIFY_REQUIRED i SNI..."
        );

        if (!tls_handshake(
                &ssl)) {

            break;
        }

        handshake_gotowy =
            true;
        wypisz_log("[TLS] HANDSHAKE OK");

        const uint32_t wynik_weryfikacji =
            mbedtls_ssl_get_verify_result(
                &ssl
            );

        if (wynik_weryfikacji != 0) {
            wypisz_log(
                "[TLS] Certyfikat nie przeszedl pelnej weryfikacji X.509."
            );

            /*
             * Nie publikujemy "trusted" nawet gdy handshake jakims sposobem
             * doszedl do konca przy nietypowej konfiguracji biblioteki.
             */
            break;
        }

        __atomic_store_n(
            &ostatni_certyfikat_zaufany,
            true,
            __ATOMIC_RELEASE
        );
        wypisz_log("[TLS] VERIFY=OK");

        char zadanie[
            TLS_HTTP_REQUEST_MAX
        ] = {};

        size_t dlugosc_zadania =
            0;

        if (!zbuduj_zadanie_http(
                domena,
                sciezka,
                zadanie,
                sizeof(zadanie),
                &dlugosc_zadania)) {

            wypisz_log(
                "[TLS] Zadanie HTTP przekracza limit lub jest niepoprawne."
            );

            break;
        }

        if (!tls_wyslij_calosc(
                &ssl,
                reinterpret_cast<const unsigned char*>(
                    zadanie
                ),
                dlugosc_zadania)) {

            break;
        }

        const WynikOdczytuTLS odczyt =
            tls_odbierz_odpowiedz(
                &ssl,
                bufor,
                max_dlugosc,
                &odebrano
            );

        if (odczyt ==
            WynikOdczytuTLS::PRZEPELNIENIE) {

            wypisz_log(
                "[TLS] Odpowiedz HTTPS nie miesci sie w buforze."
            );

            break;
        }

        if (odczyt !=
            WynikOdczytuTLS::SUKCES_CLOSE_NOTIFY) {

            break;
        }

        if (odebrano == 0) {
            wypisz_log(
                "[TLS] Serwer zamknal TLS bez danych HTTP."
            );

            break;
        }

        /*
         * Publikujemy wynik dopiero po:
         *   - poprawnym certyfikacie,
         *   - pelnym request,
         *   - clean TLS close_notify,
         *   - braku truncation.
         */
        tcp_zapisano_bajtow =
            odebrano;

        bufor[odebrano] =
            '\0';

        sukces =
            true;

    } while (false);

    if (handshake_gotowy &&
        tcp_polaczone) {

        tls_wyslij_close_notify(
            &ssl
        );
    }

    /*
     * Najpierw niszczymy warstwe TLS, dopiero potem transport TCP.
     */
    mbedtls_ssl_free(
        &ssl
    );

    mbedtls_ssl_config_free(
        &config
    );

    mbedtls_x509_crt_free(
        &ca
    );

    mbedtls_ctr_drbg_free(
        &drbg
    );

    mbedtls_entropy_free(
        &entropy
    );

    if (tcp_polaczone) {
        tcp_gniazdo_zamknij();
    }

    if (!sukces) {
        tcp_zapisano_bajtow =
            0;

        /*
         * Nie zerujemy tutaj ostatni_certyfikat_zaufany. Jezeli handshake
         * i X.509 przeszly poprawnie, a pozniej zawiodl sam HTTP/EOF,
         * certyfikat ostatniej sesji nadal byl prawidlowo zweryfikowany.
         *
         * Flaga zostala wyzerowana na poczatku funkcji i ustawiona dopiero
         * po mbedtls_ssl_get_verify_result() == 0.
         */
        bufor[0] =
            '\0';

        wypisz_log(
            "[TLS] HTTPS zakonczone bledem."
        );
    } else {
        wypisz_log(
            "[TLS] HTTPS zakonczone sukcesem; certyfikat zweryfikowany."
        );
    }

    return sukces;
}
