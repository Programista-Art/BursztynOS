#include "siec.h"
#include "e1000.h"
#include "grafika.h"
#include <stdint.h>
#include <stddef.h>

#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"

static bool ostatni_certyfikat_zaufany = false;

// Liczba bajtow ciala ostatniej odpowiedzi. Zmienna nalezy do stosu TCP i
// jest uzywana m.in. przez warstwe syscalli podczas zapisu pobranego pliku.
extern uint32_t tcp_zapisano_bajtow;

void wypisz_blad_mbedtls(int ret) {
    char buf[32] = "[TLS] Blad: -0x0000";
    int val = ret < 0 ? -ret : ret;
    const char* hex = "0123456789ABCDEF";
    buf[15] = hex[(val >> 12) & 0xF];
    buf[16] = hex[(val >> 8) & 0xF];
    buf[17] = hex[(val >> 4) & 0xF];
    buf[18] = hex[val & 0xF];
    wypisz_log(buf);
}

static void tls_pompuj_siec() {
    e1000_obsluz_odbior();
    asm volatile("pause");
}

static void tls_log_blad(const char* etap, int kod) {
    char log[96] = "[TLS] Blad ";
    int p = 11;
    int i = 0;
    while (etap[i] && p < 70) log[p++] = etap[i++];
    log[p++] = ' ';
    log[p++] = '(';
    log[p++] = '0';
    log[p++] = 'x';
    const char cyfry[] = "0123456789ABCDEF";
    uint32_t wartosc = kod < 0 ? (uint32_t)(-kod) : (uint32_t)kod;
    bool zaczeto = false;
    for (int przesuniecie = 28; przesuniecie >= 0; przesuniecie -= 4) {
        uint8_t cyfra = (wartosc >> przesuniecie) & 0x0F;
        if (cyfra || zaczeto || przesuniecie == 0) {
            log[p++] = cyfry[cyfra];
            zaczeto = true;
        }
    }
    log[p++] = ')';
    log[p] = '\0';
    wypisz_log(log);
}

extern "C" bool kernel_tls_certyfikat_zaufany() {
    return ostatni_certyfikat_zaufany;
}

static int tls_wyslij(void*, const unsigned char* dane, size_t dlugosc) {
    int wynik = tcp_gniazdo_wyslij(dane, (uint32_t)dlugosc);
    if (wynik == -2) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return wynik < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : wynik;
}

static int tls_odbierz(void*, unsigned char* dane, size_t dlugosc) {
    int wynik = tcp_gniazdo_odbierz(dane, (uint32_t)dlugosc);
    // Brak danych w tej chwili nie jest zerwaniem polaczenia. mbedTLS
    // ponowi odczyt po zwrocie WANT_READ; zero pozostaje poprawnym EOF.
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
    tcp_zapisano_bajtow = 0;
    for (uint32_t i = 0; i < max_dlugosc; i++) bufor[i] = 0;

    if (!tcp_gniazdo_polacz(cel_ip, 443)) {
        wypisz_log("[TLS] Nie udalo sie otworzyc gniazda TCP na porcie 443.");
        return false;
    }

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&entropy);

    bool sukces = false;
    char zadanie[512] = {0};
    uint32_t wyslano = 0;
    uint32_t dlugosc_zadania = 0;
    uint32_t odebrano = 0;
    uint32_t puste_proby = 0;
    uint32_t puste_proby_zapisu = 0;
    const unsigned char personalizacja[] = "BursztynOS-Hussar-TLS";
    int wynik = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                     personalizacja, sizeof(personalizacja) - 1);
    if (wynik != 0) {
        tls_log_blad("inicjalizacji DRBG", wynik);
        goto koniec;
    }
    wynik = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (wynik != 0) {
        tls_log_blad("konfiguracji SSL", wynik);
        goto koniec;
    }

    // Tymczasowy tryb bez Trust Store: transmisja jest szyfrowana, lecz
    // tozsamosc serwera nie jest weryfikowana. Nie wolno oznaczac takiej
    // sesji jako zaufanej; docelowo nalezy przywrocic VERIFY_REQUIRED.
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &drbg);
    wynik = mbedtls_ssl_setup(&ssl, &config);
    if (wynik != 0) {
        tls_log_blad("ssl_setup", wynik);
        goto koniec;
    }
    // Ustawia jednoczesnie nazwe do walidacji X.509 i rozszerzenie TLS SNI.
    wynik = mbedtls_ssl_set_hostname(&ssl, domena);
    if (wynik != 0) {
        tls_log_blad("ustawiania SNI", wynik);
        goto koniec;
    }
    mbedtls_ssl_set_bio(&ssl, nullptr, tls_wyslij, tls_odbierz, nullptr);

    wypisz_log("[TLS] ClientHello: TLS 1.2, SNI, bez weryfikacji CA...");
    for (uint32_t proby = 0; proby < 10000000; proby++) {
        wynik = mbedtls_ssl_handshake(&ssl);
        if (wynik == 0) break;
        if (wynik != MBEDTLS_ERR_SSL_WANT_READ && wynik != MBEDTLS_ERR_SSL_WANT_WRITE) {
            wypisz_blad_mbedtls(wynik);
            tls_log_blad("handshake", wynik);
            goto koniec;
        }

        // Bare-metal nie ma osobnego watku obslugi IRQ/TCP. Dopoki mbedTLS
        // czeka, musimy recznie przenosic ramki E1000 do stosu TCP.
        tls_pompuj_siec();
    }
    if (wynik != 0) goto koniec;

    // VERIFY_NONE oznacza, ze wynik sesji nigdy nie dowodzi zaufania X.509.
    ostatni_certyfikat_zaufany = false;
    wypisz_log("[TLS] Kanal szyfrowany; certyfikat serwera niezweryfikowany.");

    tekst_dodaj(zadanie, sizeof(zadanie), "GET ");
    tekst_dodaj(zadanie, sizeof(zadanie), sciezka);
    tekst_dodaj(zadanie, sizeof(zadanie), " HTTP/1.0\r\nHost: ");
    tekst_dodaj(zadanie, sizeof(zadanie), domena);
    tekst_dodaj(zadanie, sizeof(zadanie), "\r\nUser-Agent: Hussar/1.0 BursztynOS\r\nConnection: close\r\n\r\n");

    dlugosc_zadania = tekst_dlugosc(zadanie);
    while (wyslano < dlugosc_zadania) {
        wynik = mbedtls_ssl_write(&ssl, (const unsigned char*)zadanie + wyslano,
                                  dlugosc_zadania - wyslano);
        if (wynik > 0) {
            wyslano += (uint32_t)wynik;
            puste_proby_zapisu = 0;
        }
        else if (wynik != MBEDTLS_ERR_SSL_WANT_READ && wynik != MBEDTLS_ERR_SSL_WANT_WRITE) {
            wypisz_blad_mbedtls(wynik);
            tls_log_blad("ssl_write", wynik);
            goto koniec;
        }
        else if (++puste_proby_zapisu >= 10000000) {
            wypisz_log("[TLS] Timeout podczas wysylania zadania HTTPS.");
            goto koniec;
        }

        // Przetworz ACK-i i ewentualne rekordy TLS pomiedzy kolejnymi zapisami.
        tls_pompuj_siec();
    }

    /*
     * Zwracamy do Ring 3 pelna, surowa odpowiedz HTTP po odszyfrowaniu TLS:
     * linie statusu, wszystkie naglowki, pusta linie oraz cialo dokumentu.
     * Interpretacja statusu i odciecie naglowkow naleza do przegladarki.
     * Jeden bajt bufora rezerwujemy na terminator tekstu C.
     */
    while (odebrano + 1 < max_dlugosc && puste_proby < 10000000) {
        const uint32_t pozostalo = max_dlugosc - odebrano - 1;
        wynik = mbedtls_ssl_read(&ssl,
                                 reinterpret_cast<unsigned char*>(bufor) + odebrano,
                                 pozostalo);
        if (wynik > 0) {
            odebrano += static_cast<uint32_t>(wynik);
            puste_proby = 0;
        }
        else if (wynik == MBEDTLS_ERR_SSL_WANT_READ || wynik == MBEDTLS_ERR_SSL_WANT_WRITE) {
            puste_proby++;
            tls_pompuj_siec();
        }
        else if (wynik == 0 || wynik == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        else {
            wypisz_blad_mbedtls(wynik);
            tls_log_blad("ssl_read", wynik);
            goto koniec;
        }
    }
    bufor[odebrano] = 0;
    tcp_zapisano_bajtow = odebrano;
    sukces = odebrano > 0;

    // close_notify takze moze wymagac oproznienia kolejki TCP.
    if (tcp_gniazdo_otwarte()) {
        for (uint32_t proby = 0; proby < 100000; proby++) {
            wynik = mbedtls_ssl_close_notify(&ssl);
            if (wynik == 0) break;
            if (wynik != MBEDTLS_ERR_SSL_WANT_READ &&
                wynik != MBEDTLS_ERR_SSL_WANT_WRITE) break;
            tls_pompuj_siec();
        }
    }

koniec:
    if (!sukces) wypisz_log("[TLS] Handshake lub transmisja HTTPS nie powiodla sie.");
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    tcp_gniazdo_zamknij();
    return sukces;
}
