#include "siec.h"
#include "grafika.h"
#include <stdint.h>
#include <stddef.h>

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"

static bool ostatni_certyfikat_zaufany = false;

// ISRG Root X1 (Let's Encrypt). Kolejne korzenie mozna dopisac do tego
// lancucha PEM bez zmian w kodzie TLS.
static const char zaufane_ca[] =
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

extern "C" bool kernel_tls_certyfikat_zaufany() {
    return ostatni_certyfikat_zaufany;
}

static int tls_wyslij(void*, const unsigned char* dane, size_t dlugosc) {
    int wynik = tcp_gniazdo_wyslij(dane, (uint32_t)dlugosc);
    return wynik < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : wynik;
}

static int tls_odbierz(void*, unsigned char* dane, size_t dlugosc) {
    int wynik = tcp_gniazdo_odbierz(dane, (uint32_t)dlugosc);
    if (wynik == -2) return MBEDTLS_ERR_SSL_WANT_READ;
    return wynik < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : wynik;
}

static uint32_t tekst_dlugosc(const char* tekst) {
    uint32_t n = 0;
    while (tekst && tekst[n]) n++;
    return n;
}

static void tekst_dodaj(char* cel, uint32_t pojemnosc, const char* tekst) {
    uint32_t i = tekst_dlugosc(cel);
    uint32_t j = 0;
    while (tekst[j] && i + 1 < pojemnosc) cel[i++] = tekst[j++];
    cel[i] = 0;
}

extern "C" bool kernel_siec_pobierz_https(uint8_t* cel_ip, const char* domena,
                                            const char* sciezka, char* bufor,
                                            uint32_t max_dlugosc) {
    if (!cel_ip || !domena || !sciezka || !bufor || max_dlugosc < 2) return false;
    ostatni_certyfikat_zaufany = false;
    for (uint32_t i = 0; i < max_dlugosc; i++) bufor[i] = 0;

    if (!tcp_gniazdo_polacz(cel_ip, 443)) {
        wypisz_log("[TLS] Nie udalo sie otworzyc gniazda TCP na porcie 443.");
        return false;
    }

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt magazyn_ca;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509_crt_init(&magazyn_ca);

    bool sukces = false;
    char zadanie[512] = {0};
    uint32_t wyslano = 0;
    uint32_t dlugosc_zadania = 0;
    uint32_t odebrano = 0;
    uint32_t puste_proby = 0;
    const unsigned char personalizacja[] = "BursztynOS-Hussar-TLS";
    int wynik = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                     personalizacja, sizeof(personalizacja) - 1);
    if (wynik != 0) goto koniec;
    wynik = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (wynik != 0) goto koniec;

    // Bez magazynu CA nadal sprawdzamy nazwę hosta i czytamy cały łańcuch X.509.
    // Flaga zaufania pozostaje fałszywa, dopóki łańcuch nie ma zaufanego korzenia.
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    if (mbedtls_x509_crt_parse(&magazyn_ca, (const unsigned char*)zaufane_ca,
                               sizeof(zaufane_ca)) < 0) goto koniec;
    mbedtls_ssl_conf_ca_chain(&config, &magazyn_ca, nullptr);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &drbg);
    if (mbedtls_ssl_setup(&ssl, &config) != 0) goto koniec;
    if (mbedtls_ssl_set_hostname(&ssl, domena) != 0) goto koniec;
    mbedtls_ssl_set_bio(&ssl, nullptr, tls_wyslij, tls_odbierz, nullptr);

    wypisz_log("[TLS] ClientHello: TLS 1.2, SNI i walidacja X.509...");
    for (uint32_t proby = 0; proby < 10000000; proby++) {
        wynik = mbedtls_ssl_handshake(&ssl);
        if (wynik == 0) break;
        if (wynik != MBEDTLS_ERR_SSL_WANT_READ && wynik != MBEDTLS_ERR_SSL_WANT_WRITE)
            goto koniec;
    }
    if (wynik != 0 || mbedtls_ssl_get_peer_cert(&ssl) == nullptr) goto koniec;

    ostatni_certyfikat_zaufany = mbedtls_ssl_get_verify_result(&ssl) == 0;
    wypisz_log(ostatni_certyfikat_zaufany
        ? "[X509] Lancuch i nazwa hosta sa zaufane."
        : "[X509] Certyfikat odczytany; brak zaufanego korzenia CA w systemie.");

    tekst_dodaj(zadanie, sizeof(zadanie), "GET ");
    tekst_dodaj(zadanie, sizeof(zadanie), sciezka);
    tekst_dodaj(zadanie, sizeof(zadanie), " HTTP/1.0\r\nHost: ");
    tekst_dodaj(zadanie, sizeof(zadanie), domena);
    tekst_dodaj(zadanie, sizeof(zadanie), "\r\nUser-Agent: Hussar/1.0 BursztynOS\r\nConnection: close\r\n\r\n");

    dlugosc_zadania = tekst_dlugosc(zadanie);
    while (wyslano < dlugosc_zadania) {
        wynik = mbedtls_ssl_write(&ssl, (const unsigned char*)zadanie + wyslano,
                                  dlugosc_zadania - wyslano);
        if (wynik > 0) wyslano += (uint32_t)wynik;
        else if (wynik != MBEDTLS_ERR_SSL_WANT_READ && wynik != MBEDTLS_ERR_SSL_WANT_WRITE) goto koniec;
    }

    while (odebrano + 1 < max_dlugosc && puste_proby < 10000000) {
        wynik = mbedtls_ssl_read(&ssl, (unsigned char*)bufor + odebrano,
                                 max_dlugosc - odebrano - 1);
        if (wynik > 0) { odebrano += (uint32_t)wynik; puste_proby = 0; }
        else if (wynik == MBEDTLS_ERR_SSL_WANT_READ || wynik == MBEDTLS_ERR_SSL_WANT_WRITE) puste_proby++;
        else if (wynik == 0 || wynik == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        else goto koniec;
    }
    bufor[odebrano] = 0;
    sukces = odebrano > 0;
    if (tcp_gniazdo_otwarte()) mbedtls_ssl_close_notify(&ssl);

koniec:
    if (!sukces) wypisz_log("[TLS] Handshake lub transmisja HTTPS nie powiodla sie.");
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509_crt_free(&magazyn_ca);
    tcp_gniazdo_zamknij();
    return sukces;
}
