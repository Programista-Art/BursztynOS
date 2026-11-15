/*
 * Bursztyn OS - weryfikacja podpisu paczek aplikacji (.cebula)
 *
 * Model zaufania:
 *   - Kazda paczka instalowana z medium zewnetrznego (plyta, aktualizacja,
 *     recznie wgrany plik) MUSI byc podpisana kluczem release.
 *   - Podpis jest liczony nad SUROWYMI BAJTAMI pliku .bur, wiec obejmuje
 *     rowniez naglowek BUR (punkty wejscia, granice segmentow, rozmiary).
 *     Podmiana naglowka uniewaznia podpis.
 *   - Format podpisu: 256-bajtowy blok RSA-2048 PKCS#1 v1.5 nad
 *     SHA-256 pliku, zapisany w pliku `<program>.podpis` obok binarki.
 *
 * Klucz publiczny jest ten sam co dla OTA (aktualizacja_klucz.h), dzieki
 * czemu jeden klucz release podpisuje system i aplikacje.
 *
 * Fail-closed: kazdy blad odczytu, brak pliku podpisu dla paczki wymagajacej
 * podpisu albo niezgodnosc kryptograficzna konczy sie odmowa uruchomienia.
 */

#include "podpis_paczki.h"

#include <stdint.h>
#include <stddef.h>

#include "mbedtls/sha256.h"
#include "mbedtls/rsa.h"

#include "psf.h"
#include "aktualizacja_klucz.h"
#include "tozsamosc_systemowa.h"

namespace {

/* Maksymalny rozmiar pliku .bur, ktory mozemy podpisac jednym przebiegiem.
 * Zgodny z BUR_MAKS_ROZMIAR_PLIKU; podpis nie zmienia tego limitu. */
constexpr uint64_t MAKS_ROZMIAR_MB = 128ULL * 1024ULL * 1024ULL;

/* Rozmiar podpisu RSA-2048. */
constexpr size_t ROZMIAR_PODPISU = AKTUALIZACJA_KLUCZ_ROZMIAR;

/* Dlugosc rozszerzenia dopisywanego do sciezki .bur. */
constexpr size_t ROZMIAR_ROZSZERZENIA = sizeof(".podpis");

/*
 * Buduje sciezke `<program>.podpis` z bezpiecznej sciezki .bur.
 * Zwraca false, gdy wynik nie miesci sie w buforze.
 */
bool sciezka_podpisu(
    const char* sciezka_programu,
    char* wynik,
    size_t pojemnosc
) {
    if (!sciezka_programu || !wynik || pojemnosc == 0) {
        return false;
    }

    size_t dlugosc = 0;
    while (sciezka_programu[dlugosc] != '\0') {
        if (dlugosc + 1 >= pojemnosc) {
            return false;
        }
        wynik[dlugosc] = sciezka_programu[dlugosc];
        ++dlugosc;
    }

    constexpr char ROZSZERZENIE[] = ".podpis";
    for (size_t i = 0; i < sizeof(ROZSZERZENIE); ++i) {
        if (i >= pojemnosc - dlugosc) {
            return false;
        }
        wynik[dlugosc + i] = ROZSZERZENIE[i];
        if (ROZSZERZENIE[i] == '\0') {
            return true;
        }
    }

    return false;
}

} // namespace

extern "C" const char* podpis_paczki_blad(
    PodpisPaczkiWynik wynik
) {
    switch (wynik) {
        case PodpisPaczkiWynik::Ok: return "ok";
        case PodpisPaczkiWynik::BrakPliku: return "signature-file-missing";
        case PodpisPaczkiWynik::BladOdczytu: return "signature-read-failed";
        case PodpisPaczkiWynik::ZlyRozmiar: return "signature-size-invalid";
        case PodpisPaczkiWynik::Niezgodnosc: return "signature-mismatch";
        case PodpisPaczkiWynik::BrakSrodowiska: return "crypto-not-ready";
        default: return "signature-unknown";
    }
}

extern "C" bool podpis_sha256(const uint8_t* dane, uint64_t rozmiar, uint8_t wynik[32]) {
    if (!dane || !wynik || rozmiar == 0 || rozmiar > MAKS_ROZMIAR_MB) return false;
    return mbedtls_sha256_ret(dane, static_cast<size_t>(rozmiar), wynik, 0) == 0;
}

extern "C" PodpisPaczkiWynik podpis_paczki_weryfikuj(
    const char* sciezka_programu,
    const uint8_t* dane,
    uint64_t rozmiar
) {
    if (!sciezka_programu) {
        return PodpisPaczkiWynik::BladOdczytu;
    }
    if (!dane || rozmiar == 0 || rozmiar > MAKS_ROZMIAR_MB) {
        return PodpisPaczkiWynik::BladOdczytu;
    }

    /*
     * Podpis lezy obok binarki. Uzywamy PSF, bo to jedyne zrodlo plikow
     * w tej warstwie; sciezka przyszla z walidowanego argumentu loadera.
     */
    char sciezka[PODPIS_MAKS_SCIEZKI]{};

    if (!sciezka_podpisu(sciezka_programu, sciezka, sizeof(sciezka))) {
        return PodpisPaczkiWynik::BladOdczytu;
    }

    uint8_t podpis[ROZMIAR_PODPISU + 1]{};
    uint32_t odczytano = 0;

    if (!czytaj_fragment_pliku(
            sciezka,
            0,
            reinterpret_cast<char*>(podpis),
            static_cast<uint32_t>(sizeof(podpis)),
            &odczytano)) {
        return PodpisPaczkiWynik::BrakPliku;
    }

    /*
     * Krotki odczyt oznacza plik podpisu o zlym rozmiarze. Nie dopuszczamy
     * podpisow o innej dlugosci, bo slabszy klucz obnizylby poziom ochrony.
     */
    if (odczytano != ROZMIAR_PODPISU) {
        return PodpisPaczkiWynik::ZlyRozmiar;
    }

    uint8_t skrot[32]{};

    if (mbedtls_sha256_ret(dane, static_cast<size_t>(rozmiar), skrot, 0) != 0) {
        return PodpisPaczkiWynik::BrakSrodowiska;
    }

    mbedtls_rsa_context rsa;
    mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V15, 0);

    int rc = mbedtls_rsa_import_raw(
        &rsa,
        AKTUALIZACJA_KLUCZ_N, sizeof(AKTUALIZACJA_KLUCZ_N),
        nullptr, 0, nullptr, 0, nullptr, 0,
        nullptr, 0);

    if (rc == 0) {
        static const unsigned char WYKLADNIK[3] = {0x01, 0x00, 0x01};
        rc = mbedtls_rsa_import_raw(
            &rsa, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
            WYKLADNIK, sizeof(WYKLADNIK));
    }

    if (rc == 0) {
        rc = mbedtls_rsa_complete(&rsa);
    }

    if (rc == 0) {
        rc = mbedtls_rsa_check_pubkey(&rsa);
    }

    if (rc == 0) {
        mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256);
        rc = mbedtls_rsa_rsassa_pkcs1_v15_verify(
            &rsa, nullptr, nullptr, MBEDTLS_RSA_PUBLIC,
            MBEDTLS_MD_SHA256, sizeof(skrot), skrot, podpis);
    }

    mbedtls_rsa_free(&rsa);

    for (size_t i = 0; i < sizeof(skrot); ++i) {
        skrot[i] = 0;
    }
    for (size_t i = 0; i < sizeof(podpis); ++i) {
        podpis[i] = 0;
    }

    if (rc != 0) {
        return PodpisPaczkiWynik::Niezgodnosc;
    }

    return PodpisPaczkiWynik::Ok;
}

extern "C" bool tozsamosc_systemowa_zweryfikuj_tablica(
    const TozsamoscSystemowa::Wpis* tablica, size_t liczba,
    const char* sciezka, const uint8_t* dane, uint64_t rozmiar) {
    if (!sciezka || !dane || rozmiar == 0) return false;
    uint8_t skrot[32]{};
    if (tablica && podpis_sha256(dane, rozmiar, skrot)) {
        for (size_t i = 0; i < liczba; ++i) {
            const auto& w = tablica[i];
            if (w.sciezka && TozsamoscSystemowa::sciezki_rowne(w.sciezka, sciezka) &&
                w.rozmiar == rozmiar && TozsamoscSystemowa::skroty_rowne(w.skrot, skrot))
                return true;
        }
    }
    // Niezalezna aktualizacja aplikacji: podpis release dla dokladnie tych bajtow.
    return podpis_paczki_weryfikuj(sciezka, dane, rozmiar) == PodpisPaczkiWynik::Ok;
}

extern "C" bool tozsamosc_systemowa_zweryfikuj(const char* sciezka, const uint8_t* dane, uint64_t rozmiar) {
    return tozsamosc_systemowa_zweryfikuj_tablica(
        TozsamoscSystemowa::kWbudowaneSkroty,
        sizeof(TozsamoscSystemowa::kWbudowaneSkroty) / sizeof(TozsamoscSystemowa::kWbudowaneSkroty[0]),
        sciezka, dane, rozmiar);
}
