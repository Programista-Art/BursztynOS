/*
 * Bursztyn OS - Intel High Definition Audio (HDA)
 *
 * Publiczny interfejs sterownika HDA.
 *
 * Implementacja:
 *   hda.cpp
 *
 * Aktualny sterownik:
 *   - wykrywa kontroler PCI klasy 04:03,
 *   - pracuje pollingowo, bez IRQ,
 *   - obsluguje jeden Output Stream,
 *   - uzywa 48 kHz / 16-bit / stereo PCM,
 *   - konfiguruje kodek przez Immediate Command,
 *   - posiada bezpieczne DMA przez PMM,
 *   - obsluguje obecnie topologie kodeka QEMU hda-output/hda-duplex,
 *   - udostepnia synchroniczny test tonu.
 *
 * Struktury MMIO, Stream Descriptor oraz BDL sa celowo prywatne dla
 * hda.cpp. Inne moduly jadra powinny korzystac tylko z API ponizej.
 *
 * Symbole zachowuja linkage C++, zgodnie z aktualnym hda.cpp.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. PUBLICZNE PARAMETRY OBECNEGO STEROWNIKA
 * ========================================================================= */

/*
 * Format PCM uzywany przez obecna implementacje.
 */
#define HDA_API_SAMPLE_RATE_HZ UINT32_C(48000)
#define HDA_API_BITY_NA_PROBKE UINT32_C(16)
#define HDA_API_KANALY         UINT32_C(2)

/*
 * Stereo 16-bit:
 *
 *   2 kanaly * 2 bajty = 4 bajty na sample frame.
 */
#define HDA_API_BAJTY_RAMKI_AUDIO UINT32_C(4)

/*
 * Zakres akceptowany przez hda_test_ton().
 *
 * Jest zgodny z walidacja aktualnego syscalla audio Bursztyn OS.
 */
#define HDA_API_MIN_CZESTOTLIWOSC_HZ UINT32_C(20)
#define HDA_API_MAX_CZESTOTLIWOSC_HZ UINT32_C(20000)

/*
 * Maksymalny czas jednego synchronicznego testu tonu.
 */
#define HDA_API_MAX_CZAS_MS UINT32_C(10000)

/*
 * Stream tag 0 jest zarezerwowany przez HDA.
 * Aktualna implementacja uzywa tagu 1.
 */
#define HDA_API_STREAM_TAG UINT32_C(1)

/* =========================================================================
 * 2. PUBLICZNE API
 * ========================================================================= */

/*
 * Inicjalizuje kontroler HDA.
 *
 * Funkcja:
 *   - wyszukuje PCI class 04 / subclass 03,
 *   - wlacza PCI Memory Space + Bus Master,
 *   - mapuje BAR0 MMIO,
 *   - wykonuje globalny reset HDA,
 *   - wykrywa kodek,
 *   - przydziela BDL i bufory DMA przez PMM,
 *   - przygotowuje jeden Output Stream,
 *   - konfiguruje kodek QEMU.
 *
 * Zwraca:
 *   true  - kontroler i wspierany kodek sa gotowe,
 *   false - inicjalizacja nie powiodla sie.
 *
 * Ponowne wywolanie po poprawnej inicjalizacji zwraca true.
 *
 * Obecna implementacja jest celowo konserwatywna: jezeli wykryje kodek
 * o nieznanej topologii, nie wysyla przypadkowych verbow do losowych NID.
 */
bool inicjalizuj_hda();

/*
 * Synchronicznie odtwarza testowy ton.
 *
 * czestotliwosc_hz:
 *   HDA_API_MIN_CZESTOTLIWOSC_HZ ..
 *   HDA_API_MAX_CZESTOTLIWOSC_HZ.
 *
 * czas_ms:
 *   1 .. HDA_API_MAX_CZAS_MS.
 *
 * Zwraca true tylko wtedy, gdy:
 *   - HDA jest zainicjalizowane,
 *   - parametry sa poprawne,
 *   - DMA stream wystartowal,
 *   - w trakcie odtwarzania nie wystapil blad streamu,
 *   - stream zostal poprawnie zatrzymany.
 *
 * Funkcja jest synchroniczna: wraca dopiero po zakonczeniu zadanego czasu
 * albo po bledzie/timeout.
 */
bool hda_test_ton(
    uint32_t czestotliwosc_hz,
    uint32_t czas_ms
);

/*
 * Zatrzymuje aktualny Output Stream.
 *
 * Funkcja:
 *   - jest bezpieczna przed inicjalizacja,
 *   - jest idempotentna,
 *   - nie zwalnia zasobow DMA,
 *   - nie resetuje calego kontrolera.
 */
void hda_stop();

/* =========================================================================
 * 3. HELPERY HEADER-ONLY
 * ========================================================================= */

#ifdef __cplusplus

/*
 * Sprawdza czestotliwosc zgodnie z kontraktem hda_test_ton().
 */
inline constexpr bool hda_api_czestotliwosc_poprawna(
    uint32_t hz
) noexcept {
    return
        hz >= HDA_API_MIN_CZESTOTLIWOSC_HZ &&
        hz <= HDA_API_MAX_CZESTOTLIWOSC_HZ;
}

/*
 * Sprawdza czas odtwarzania.
 */
inline constexpr bool hda_api_czas_poprawny(
    uint32_t czas_ms
) noexcept {
    return
        czas_ms >= 1U &&
        czas_ms <= HDA_API_MAX_CZAS_MS;
}

/*
 * Pelna walidacja argumentow testowego tonu.
 */
inline constexpr bool hda_api_ton_poprawny(
    uint32_t hz,
    uint32_t czas_ms
) noexcept {
    return
        hda_api_czestotliwosc_poprawna(hz) &&
        hda_api_czas_poprawny(czas_ms);
}

/*
 * Liczba sample frames dla zadanego czasu przy 48 kHz.
 *
 * Funkcja zwraca 0 dla czasu spoza publicznego zakresu.
 * Dla obecnego limitu 10 s wynik bezpiecznie miesci sie w uint32_t.
 */
inline constexpr uint32_t hda_api_liczba_ramek_audio(
    uint32_t czas_ms
) noexcept {
    return
        hda_api_czas_poprawny(czas_ms)
            ? static_cast<uint32_t>(
                (static_cast<uint64_t>(
                    HDA_API_SAMPLE_RATE_HZ
                ) *
                 static_cast<uint64_t>(
                    czas_ms
                 )) /
                UINT64_C(1000)
              )
            : 0U;
}

/* =========================================================================
 * 4. KONTROLA KONTRAKTU
 * ========================================================================= */

static_assert(
    sizeof(uint16_t) == 2U,
    "HDA wymaga 16-bitowego uint16_t"
);

static_assert(
    sizeof(uint32_t) == 4U,
    "HDA wymaga 32-bitowego uint32_t"
);

static_assert(
    sizeof(uint64_t) == 8U,
    "HDA wymaga 64-bitowego uint64_t"
);

static_assert(
    HDA_API_SAMPLE_RATE_HZ == 48000U,
    "Aktualny sterownik HDA jest skonfigurowany na 48 kHz"
);

static_assert(
    HDA_API_BITY_NA_PROBKE == 16U,
    "Aktualny sterownik HDA jest skonfigurowany na PCM16"
);

static_assert(
    HDA_API_KANALY == 2U,
    "Aktualny sterownik HDA jest skonfigurowany na stereo"
);

static_assert(
    HDA_API_BAJTY_RAMKI_AUDIO == 4U,
    "Stereo PCM16 powinno zajmowac 4 bajty na sample frame"
);

static_assert(
    HDA_API_MIN_CZESTOTLIWOSC_HZ < HDA_API_MAX_CZESTOTLIWOSC_HZ,
    "Nieprawidlowy zakres czestotliwosci HDA"
);

static_assert(
    HDA_API_MAX_CZAS_MS == 10000U,
    "Zmiana limitu czasu wymaga synchronizacji z hda.cpp/syscalls"
);

static_assert(
    HDA_API_STREAM_TAG >= 1U &&
    HDA_API_STREAM_TAG <= 15U,
    "Stream tag HDA musi byc w zakresie 1..15"
);

#endif /* __cplusplus */
