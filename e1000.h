/*
 * Bursztyn OS - Intel E1000
 *
 * Publiczny interfejs sterownika Intel 82540EM / QEMU E1000.
 *
 * Implementacja:
 *   e1000.cpp
 *
 * Obecny sterownik:
 *   - obsluguje PCI 8086:100E,
 *   - pracuje pollingowo,
 *   - korzysta z DMA przez PMM,
 *   - ma osobne stale bufory RX/TX,
 *   - odbiera standardowe ramki Ethernet bez jumbo frames.
 *
 * Struktury deskryptorow RX/TX oraz rejestry MMIO sa celowo prywatne dla
 * e1000.cpp. Inne moduly powinny korzystac tylko z API ponizej.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. PUBLICZNE PARAMETRY API
 * ========================================================================= */

/*
 * Minimalny rozmiar ramki przekazywanej przez stos do sterownika.
 *
 * To minimum naglowka Ethernet II. Sterownik/sprzet moze dopelnic ramke
 * do minimalnej dlugosci wymaganej przez Ethernet.
 */
#define E1000_API_MIN_RAMKA_ETH UINT16_C(14)

/*
 * Maksymalna standardowa ramka Ethernet razem z naglowkiem i bez jumbo.
 *
 * Poprawiony e1000.cpp odbiera ramki maks. 1518 B.
 */
#define E1000_API_MAX_RAMKA_ETH UINT16_C(1518)

/*
 * Rozmiar pojedynczego bufora DMA RX/TX.
 *
 * Sterownik wykorzystuje strony PMM 4 KiB, ale na ramke przeznacza
 * maksymalnie 2048 bajtow.
 */
#define E1000_API_BUFOR_DMA_BAJTOW UINT16_C(2048)

/*
 * Rozmiary ringow w poprawionej implementacji.
 */
#define E1000_API_RX_DESCRIPTORS UINT16_C(32)
#define E1000_API_TX_DESCRIPTORS UINT16_C(8)

/*
 * Identyfikatory obecnie wspieranego urzadzenia.
 */
#define E1000_API_VENDOR_INTEL UINT16_C(0x8086)
#define E1000_API_DEVICE_82540EM UINT16_C(0x100E)

/* =========================================================================
 * 2. PUBLICZNE ABI
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint64_t e1000_rx_packets;
extern volatile uint64_t e1000_tx_attempts;
extern volatile uint64_t e1000_tx_success;
extern volatile uint64_t e1000_tx_errors;
extern volatile uint64_t e1000_tx_timeout;
extern volatile uint64_t e1000_tx_ring_full;

/*
 * Inicjalizuje Intel 82540EM / QEMU E1000.
 *
 * Implementacja:
 *   - znajduje urzadzenie PCI 8086:100E,
 *   - wlacza Memory Space + Bus Master,
 *   - mapuje BAR0 MMIO,
 *   - resetuje kontroler,
 *   - odczytuje/programuje MAC,
 *   - przydziela ringi i bufory DMA,
 *   - uruchamia RX/TX,
 *   - maskuje IRQ, poniewaz sterownik pracuje pollingowo.
 *
 * Funkcja zachowuje void dla zgodnosci z obecnym ABI.
 * Blad inicjalizacji jest raportowany do logu, a pozostale funkcje
 * bezpiecznie nic nie robia, dopoki sterownik nie jest gotowy.
 */
void inicjalizuj_e1000();

/*
 * Wysyla jedna ramke Ethernet.
 *
 * dane:
 *   wskaznik do bufora kernela zawierajacego pelna ramke Ethernet.
 *
 * dlugosc:
 *   E1000_API_MIN_RAMKA_ETH .. E1000_API_MAX_RAMKA_ETH.
 *
 * Poprawiony sterownik kopiuje dane do stalego bufora DMA przypisanego
 * do deskryptora TX. Nie przechowuje przekazanego wskaznika po powrocie.
 *
 * UWAGA ABI:
 * Funkcja zwraca void, wiec timeout lub brak wolnego deskryptora nie moze
 * zostac przekazany do wywolujacego jako blad. W takim przypadku ramka
 * zostaje bezpiecznie odrzucona.
 *
 * Docelowo warto skoordynowanie zmienic ABI na:
 *
 *   bool e1000_wyslij_pakiet(...);
 *
 * razem z e1000.cpp i siec.cpp.
 */
bool e1000_wyslij_pakiet(
    void* dane,
    uint16_t dlugosc
);

/*
 * Przetwarza oczekujace ramki RX.
 *
 * Sterownik pracuje pollingowo, wiec stos sieciowy wywoluje te funkcje
 * podczas oczekiwania na ARP/DHCP/DNS/TCP/TLS.
 *
 * Jedno wywolanie przetwarza maksymalnie jeden pelny obrot ringu RX.
 * Funkcja ma zabezpieczenie przed reentrancy.
 */
void e1000_obsluz_odbior();

/*
 * Zwraca wskaznik do 6-bajtowego adresu MAC nalezacego do sterownika.
 *
 * Wskaznik:
 *   - jest wazny przez caly czas zycia kernela,
 *   - nie powinien byc zwalniany,
 *   - powinien byc traktowany jako read-only przez kod poza e1000.cpp.
 *
 * Sygnatura pozostaje uint8_t* dla zgodnosci z obecnym ABI.
 */
uint8_t* pobierz_mac_adres();

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 * 3. LEKKIE HELPERY HEADER-ONLY
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Sprawdza, czy dlugosc moze byc przekazana do e1000_wyslij_pakiet().
 */
inline constexpr bool e1000_api_dlugosc_ramki_poprawna(
    uint16_t dlugosc
) noexcept {
    return
        dlugosc >= E1000_API_MIN_RAMKA_ETH &&
        dlugosc <= E1000_API_MAX_RAMKA_ETH &&
        dlugosc <= E1000_API_BUFOR_DMA_BAJTOW;
}

/*
 * Podstawowa walidacja unicastowego MAC.
 *
 * Odrzuca:
 *   00:00:00:00:00:00
 *   FF:FF:FF:FF:FF:FF
 *   adresy multicast.
 */
inline constexpr bool e1000_api_mac_poprawny(
    const uint8_t mac[6]
) noexcept {
    if (mac == nullptr) {
        return false;
    }

    uint8_t suma_or =
        0;

    bool wszystkie_ff =
        true;

    for (unsigned int i = 0;
         i < 6U;
         ++i) {

        suma_or =
            static_cast<uint8_t>(
                suma_or |
                mac[i]
            );

        if (mac[i] !=
            UINT8_C(0xFF)) {

            wszystkie_ff =
                false;
        }
    }

    return
        suma_or != 0 &&
        !wszystkie_ff &&
        (mac[0] &
         UINT8_C(0x01)) == 0;
}

/* =========================================================================
 * 4. KONTROLA STALYCH ABI
 * ========================================================================= */

static_assert(
    sizeof(uint8_t) == 1,
    "E1000 wymaga 8-bitowego uint8_t"
);

static_assert(
    sizeof(uint16_t) == 2,
    "E1000 wymaga 16-bitowego uint16_t"
);

static_assert(
    sizeof(uint32_t) == 4,
    "E1000 wymaga 32-bitowego uint32_t"
);

static_assert(
    sizeof(uint64_t) == 8,
    "E1000 wymaga 64-bitowego uint64_t"
);

static_assert(
    E1000_API_MIN_RAMKA_ETH == 14U,
    "Minimalna ramka API musi odpowiadac naglowkowi Ethernet II"
);

static_assert(
    E1000_API_MAX_RAMKA_ETH == 1518U,
    "Obecny sterownik nie obsluguje jumbo frames"
);

static_assert(
    E1000_API_MAX_RAMKA_ETH <= E1000_API_BUFOR_DMA_BAJTOW,
    "Bufor DMA musi pomiescic maksymalna ramke Ethernet"
);

static_assert(
    E1000_API_RX_DESCRIPTORS == 32U,
    "Zmiana liczby RX descriptorow wymaga aktualizacji e1000.cpp"
);

static_assert(
    E1000_API_TX_DESCRIPTORS == 8U,
    "Zmiana liczby TX descriptorow wymaga aktualizacji e1000.cpp"
);

#endif /* __cplusplus */
