/*
 * Bursztyn OS - RTC / CMOS
 *
 * Publiczny interfejs sterownika zegara czasu rzeczywistego.
 *
 * Implementacja znajduje sie w zegar-rtc.cpp.
 *
 * Obecne API zachowuje historyczna semantyke:
 *
 *   void pobierz_czas_rtc(czas_rtc*);
 *
 * czyli blad sprzetowego RTC nie jest zwracany jako bool. Poprawiona
 * implementacja w takiej sytuacji korzysta z ostatniej poprawnej probki,
 * a jezeli jeszcze jej nie ma - zwraca bezpieczny czas awaryjny.
 *
 * Domyslnie zegar CMOS jest interpretowany jako UTC, a zegar-rtc.cpp
 * zamienia go na czas lokalny Polski (CET/CEST). Mozna to zmienic przy
 * kompilacji sterownika:
 *
 *   -DBURSZTYN_RTC_CMOS_JEST_UTC=0
 *
 * Jezeli platforma udostepni przez ACPI prawidlowy rejestr stulecia,
 * zegar-rtc.cpp moze zostac zbudowany np. z:
 *
 *   -DBURSZTYN_RTC_REJESTR_STULECIA=0x32
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * 1. STALE PUBLICZNE
 * ========================================================================= */

/*
 * formatuj_czas_do_stringa() zapisuje:
 *
 *   HH:MM:SS\0
 *
 * czyli dokladnie 9 bajtow.
 */
#define RTC_ROZMIAR_BUFORA_HHMMSS 9U

#define RTC_SEKUNDA_MAX 59U
#define RTC_MINUTA_MAX 59U
#define RTC_GODZINA_MAX 23U

#define RTC_MIESIAC_MIN 1U
#define RTC_MIESIAC_MAX 12U

/*
 * Bez ACPI century-register poprawiona implementacja interpretuje
 * dwu-cyfrowy rok CMOS jako zakres 2000..2099.
 *
 * Pole rok pozostaje uint32_t, aby ABI naglowka nie zostalo niepotrzebnie
 * zawezone i aby przyszle zrodla czasu nie byly ograniczone do 16 bitow.
 */
#define RTC_DOMYSLNY_ROK_MIN 2000U
#define RTC_DOMYSLNY_ROK_MAX 2099U

/* =========================================================================
 * 2. STRUKTURA CZASU
 * ========================================================================= */

/*
 * Czas kalendarzowy zwracany przez sterownik.
 *
 * Przy domyslnej konfiguracji zegar-rtc.cpp:
 *   - sprzetowy CMOS jest traktowany jako UTC,
 *   - struktura ponizej zawiera juz czas lokalny Polski CET/CEST.
 *
 * Struktura NIE jest formatem dyskowym ani sprzetowym TSS/CMOS.
 * Jest zwykla struktura ABI C++ uzywana wewnatrz kernela.
 */
struct czas_rtc {
    uint8_t sekundy;
    uint8_t minuty;
    uint8_t godziny;

    uint8_t dzien;
    uint8_t miesiac;

    /*
     * Pelny rok, np. 2026.
     */
    uint32_t rok;
};

/* =========================================================================
 * 3. KONTROLA ABI
 * ========================================================================= */

#ifdef __cplusplus

static_assert(
    sizeof(uint8_t) == 1,
    "RTC wymaga 8-bitowego uint8_t"
);

static_assert(
    sizeof(uint32_t) == 4,
    "RTC wymaga 32-bitowego uint32_t"
);

static_assert(
    offsetof(czas_rtc, sekundy) == 0,
    "Nieprawidlowy offset czas_rtc.sekundy"
);

static_assert(
    offsetof(czas_rtc, minuty) == 1,
    "Nieprawidlowy offset czas_rtc.minuty"
);

static_assert(
    offsetof(czas_rtc, godziny) == 2,
    "Nieprawidlowy offset czas_rtc.godziny"
);

static_assert(
    offsetof(czas_rtc, dzien) == 3,
    "Nieprawidlowy offset czas_rtc.dzien"
);

static_assert(
    offsetof(czas_rtc, miesiac) == 4,
    "Nieprawidlowy offset czas_rtc.miesiac"
);

static_assert(
    offsetof(czas_rtc, rok) == 8,
    "Nieprawidlowy offset czas_rtc.rok"
);

static_assert(
    sizeof(czas_rtc) == 12,
    "Zmiana rozmiaru czas_rtc wymaga sprawdzenia uzytkownikow ABI"
);

static_assert(
    alignof(czas_rtc) == alignof(uint32_t),
    "czas_rtc powinien zachowac naturalne wyrownanie uint32_t"
);

#endif /* __cplusplus */

/* =========================================================================
 * 4. PUBLICZNE API
 * ========================================================================= */

/*
 * Odczytuje aktualny czas RTC.
 *
 * czas == nullptr:
 *   funkcja nic nie robi.
 *
 * Poprawiona implementacja:
 *   - czeka na stabilna probke CMOS z timeoutem,
 *   - obsluguje BCD/binary oraz 12h/24h,
 *   - waliduje date i godzine,
 *   - domyslnie przelicza UTC na CET/CEST,
 *   - przy chwilowym bledzie zwraca ostatnia poprawna probke,
 *   - przy braku jakiejkolwiek poprawnej probki zwraca
 *     2000-01-01 00:00:00.
 *
 * Funkcja zachowuje C++ linkage, zgodnie z zegar-rtc.cpp.
 */
void pobierz_czas_rtc(
    czas_rtc* czas
);

/* Nie zwraca kalendarzowego fallbacku: false oznacza brak prawdziwej
 * probki CMOS (i brak wczesniej zapamietanej poprawnej probki). */
bool pobierz_czas_rtc_bezpiecznie(
    czas_rtc* czas
);

/*
 * Formatuje tylko godzine:
 *
 *   HH:MM:SS
 *
 * Wymagany bufor:
 *
 *   co najmniej RTC_ROZMIAR_BUFORA_HHMMSS bajtow.
 *
 * Jezeli czas == nullptr albo struktura zawiera niepoprawne wartosci,
 * zapisuje:
 *
 *   --:--:--
 *
 * bufor == nullptr:
 *   funkcja nic nie robi.
 *
 * Funkcja zawsze dopisuje '\0', jezeli bufor jest poprawnym wskaznikiem.
 */
void formatuj_czas_do_stringa(
    const czas_rtc* czas,
    char* bufor
);

#ifdef __cplusplus

/* =========================================================================
 * 5. LEKKI HELPER WALIDACYJNY
 * ========================================================================= */

/*
 * Sprawdza podstawowe pola czasu bez weryfikacji liczby dni w konkretnym
 * miesiacu. Pelna walidacja kalendarza pozostaje w zegar-rtc.cpp.
 */
inline constexpr bool rtc_podstawowy_czas_poprawny(
    const czas_rtc& czas
) noexcept {
    return
        czas.sekundy <= RTC_SEKUNDA_MAX &&
        czas.minuty <= RTC_MINUTA_MAX &&
        czas.godziny <= RTC_GODZINA_MAX &&
        czas.miesiac >= RTC_MIESIAC_MIN &&
        czas.miesiac <= RTC_MIESIAC_MAX &&
        czas.dzien >= 1U &&
        czas.dzien <= 31U;
}

#endif /* __cplusplus */
