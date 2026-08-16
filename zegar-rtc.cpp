/*
 * Bursztyn OS - sterownik RTC / CMOS
 *
 * Odczytuje zegar czasu rzeczywistego zgodny z MC146818/PC CMOS przez:
 *
 *   0x70 - indeks rejestru CMOS + bit maskowania NMI,
 *   0x71 - dane.
 *
 * Publiczne API pozostaje zgodne z obecnym zegar-rtc.h:
 *
 *   void pobierz_czas_rtc(czas_rtc* czas);
 *   void formatuj_czas_do_stringa(const czas_rtc* czas, char* bufor);
 *
 * Bezpieczenstwo i poprawnosc:
 *
 *  - dostep do pary portow 0x70/0x71 jest serializowany,
 *  - lokalne IRQ sa wylaczane na czas transakcji CMOS, aby inny handler
 *    na tym samym CPU nie zmienil indeksu portu 0x70,
 *  - sterownik nie wylacza NMI podczas zwyklego odczytu,
 *  - UIP (Update In Progress) ma timeout - uszkodzony RTC nie zawiesi jadra,
 *  - wykonywane sa dwie identyczne probki, aby nie zlozyc czasu z dwoch
 *    roznych sekund,
 *  - obslugiwany jest tryb BCD i binarny,
 *  - obslugiwany jest tryb 12 h i 24 h,
 *  - dane sa walidowane przed publikacja,
 *  - przy chwilowym bledzie zwracana jest ostatnia poprawna probka,
 *  - strefa dla Polski korzysta z CET/CEST zamiast stalego +2.
 *
 * ZALOZENIE DOMYSLNE:
 * CMOS przechowuje UTC. Tak pracuje typowa konfiguracja Bursztyn OS/QEMU.
 *
 * Na komputerze, na ktorym firmware/inny system przechowuje w RTC juz czas
 * lokalny, zbuduj ten plik z:
 *
 *   -DBURSZTYN_RTC_CMOS_JEST_UTC=0
 *
 * Wtedy sterownik nie zastosuje przesuniecia CET/CEST.
 *
 * Rok:
 * Bez wskazanego przez ACPI rejestru stulecia nie wolno zakladac, ze CMOS
 * 0x32 rzeczywiscie jest century register. Domyslnie interpretujemy wiec
 * dwu-cyfrowy rok jako 2000..2099.
 *
 * Jezeli platforma pozniej poda poprawny rejestr stulecia, mozna zbudowac:
 *
 *   -DBURSZTYN_RTC_REJESTR_STULECIA=0x32
 */

#include "zegar-rtc.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * 1. KONFIGURACJA
 * ========================================================================= */

#ifndef BURSZTYN_RTC_CMOS_JEST_UTC
#define BURSZTYN_RTC_CMOS_JEST_UTC 1
#endif

/*
 * 0 = brak rejestru stulecia.
 *
 * Nie czytamy 0x32 bez wiedzy z ACPI/FADT, poniewaz na czesci platform
 * ten adres moze miec inne znaczenie.
 */
#ifndef BURSZTYN_RTC_REJESTR_STULECIA
#define BURSZTYN_RTC_REJESTR_STULECIA 0
#endif

namespace {

constexpr uint16_t PORT_CMOS_INDEKS =
    0x70U;

constexpr uint16_t PORT_CMOS_DANE =
    0x71U;

constexpr uint8_t RTC_REJ_SEKUNDY =
    0x00U;

constexpr uint8_t RTC_REJ_MINUTY =
    0x02U;

constexpr uint8_t RTC_REJ_GODZINY =
    0x04U;

constexpr uint8_t RTC_REJ_DZIEN =
    0x07U;

constexpr uint8_t RTC_REJ_MIESIAC =
    0x08U;

constexpr uint8_t RTC_REJ_ROK =
    0x09U;

constexpr uint8_t RTC_REJ_A =
    0x0AU;

constexpr uint8_t RTC_REJ_B =
    0x0BU;

constexpr uint8_t RTC_REJ_D =
    0x0DU;

constexpr uint8_t RTC_A_UIP =
    0x80U;

constexpr uint8_t RTC_B_24H =
    0x02U;

constexpr uint8_t RTC_B_BINARY =
    0x04U;

/*
 * Bit 7 rejestru D to VRT (Valid RAM and Time).
 */
constexpr uint8_t RTC_D_VRT =
    0x80U;

/*
 * Bit 7 wartosci wysylanej na port 0x70:
 *
 *   0 -> NMI wlaczone
 *   1 -> NMI zamaskowane
 *
 * Nie maskujemy NMI podczas odczytu RTC. To wazne zwlaszcza po dodaniu
 * osobnych stosow IST dla NMI/#MC.
 */
constexpr uint8_t CMOS_NMI_WLACZONE =
    0x00U;

/*
 * Oczekiwanie na UIP ma limit. Wartosc jest liczba prob odczytu portu,
 * nie jednostka czasu.
 *
 * Awaria/nieobecnosc RTC nie moze zamrozic calego systemu.
 */
constexpr uint32_t MAX_PROB_UIP =
    10000U;

/*
 * Przy granicy sekundy pierwsza i druga probka moga sie roznic.
 * Kilka powtorzen jest wystarczajace, a ograniczenie zapobiega petli
 * nieskonczonej na uszkodzonym sprzecie.
 */
constexpr uint32_t MAX_PROB_STABILNEGO_ODCZYTU =
    8U;

/* =========================================================================
 * 2. BLOKADA CMOS
 * ========================================================================= */

/*
 * Port 0x70 jest wspolnym rejestrem indeksu. Dwa rownolegle odczyty:
 *
 *   CPU/A: wybierz sekundy
 *   CPU/B: wybierz godziny
 *   CPU/A: przeczytaj 0x71
 *
 * zwrocilyby godziny zamiast sekund.
 *
 * Spinlock przygotowuje kod rowniez pod przyszly SMP. Na obecnym jednym CPU
 * wylaczenie IRQ dodatkowo zapobiega reentrancji przez handler.
 */
uint32_t blokada_cmos = 0;

struct StanPrzerwan {
    uint64_t rflags;
};

StanPrzerwan zapisz_i_wylacz_przerwania() {
    StanPrzerwan stan{};

    asm volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(stan.rflags)
        :
        : "memory", "cc"
    );

    return stan;
}

void przywroc_przerwania(
    StanPrzerwan stan
) {
    if ((stan.rflags &
         (UINT64_C(1) << 9)) != 0) {

        asm volatile(
            "sti"
            :
            :
            : "memory"
        );
    }
}

void zablokuj_cmos() {
    while (__atomic_exchange_n(
               &blokada_cmos,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {

        while (__atomic_load_n(
                   &blokada_cmos,
                   __ATOMIC_RELAXED) != 0U) {

            asm volatile(
                "pause"
                :
                :
                : "memory"
            );
        }
    }
}

void odblokuj_cmos() {
    __atomic_store_n(
        &blokada_cmos,
        0U,
        __ATOMIC_RELEASE
    );
}

class BlokadaCMOS {
public:
    BlokadaCMOS()
        : stan_irq_(
              zapisz_i_wylacz_przerwania()
          ) {

        zablokuj_cmos();
    }

    ~BlokadaCMOS() {
        odblokuj_cmos();

        przywroc_przerwania(
            stan_irq_
        );
    }

    BlokadaCMOS(
        const BlokadaCMOS&
    ) = delete;

    BlokadaCMOS& operator=(
        const BlokadaCMOS&
    ) = delete;

private:
    StanPrzerwan stan_irq_;
};

/* =========================================================================
 * 3. PORT I/O
 * ========================================================================= */

void outb_rtc(
    uint16_t port,
    uint8_t wartosc
) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(wartosc),
          "Nd"(port)
        : "memory"
    );
}

uint8_t inb_rtc(
    uint16_t port
) {
    uint8_t wartosc = 0;

    asm volatile(
        "inb %1, %0"
        : "=a"(wartosc)
        : "Nd"(port)
        : "memory"
    );

    return wartosc;
}

uint8_t pobierz_rejestr_rtc(
    uint8_t rejestr
) {
    /*
     * Rejestry CMOS maja 7-bitowy indeks.
     * Bit 7 pozostawiamy 0, czyli NMI pozostaje wlaczone.
     */
    outb_rtc(
        PORT_CMOS_INDEKS,
        static_cast<uint8_t>(
            CMOS_NMI_WLACZONE |
            (rejestr & 0x7FU)
        )
    );

    return
        inb_rtc(
            PORT_CMOS_DANE
        );
}

bool aktualizacja_w_toku() {
    return
        (pobierz_rejestr_rtc(
             RTC_REJ_A) &
         RTC_A_UIP) != 0;
}

bool czekaj_na_koniec_aktualizacji() {
    for (uint32_t proba = 0;
         proba <
            MAX_PROB_UIP;
         ++proba) {

        if (!aktualizacja_w_toku()) {
            return true;
        }

        asm volatile(
            "pause"
        );
    }

    return false;
}

/* =========================================================================
 * 4. SUROWA PROBKA CMOS
 * ========================================================================= */

struct ProbkaRTC {
    uint8_t sekundy;
    uint8_t minuty;
    uint8_t godziny;

    uint8_t dzien;
    uint8_t miesiac;
    uint8_t rok;

    uint8_t stulecie;

    uint8_t rejestr_b;
    uint8_t rejestr_d;
};

ProbkaRTC pobierz_probke_surowa() {
    ProbkaRTC p{};

    p.sekundy =
        pobierz_rejestr_rtc(
            RTC_REJ_SEKUNDY
        );

    p.minuty =
        pobierz_rejestr_rtc(
            RTC_REJ_MINUTY
        );

    p.godziny =
        pobierz_rejestr_rtc(
            RTC_REJ_GODZINY
        );

    p.dzien =
        pobierz_rejestr_rtc(
            RTC_REJ_DZIEN
        );

    p.miesiac =
        pobierz_rejestr_rtc(
            RTC_REJ_MIESIAC
        );

    p.rok =
        pobierz_rejestr_rtc(
            RTC_REJ_ROK
        );

#if BURSZTYN_RTC_REJESTR_STULECIA != 0
    p.stulecie =
        pobierz_rejestr_rtc(
            static_cast<uint8_t>(
                BURSZTYN_RTC_REJESTR_STULECIA
            )
        );
#else
    p.stulecie = 0;
#endif

    p.rejestr_b =
        pobierz_rejestr_rtc(
            RTC_REJ_B
        );

    p.rejestr_d =
        pobierz_rejestr_rtc(
            RTC_REJ_D
        );

    return p;
}

bool probki_rowne(
    const ProbkaRTC& a,
    const ProbkaRTC& b
) {
    return
        a.sekundy == b.sekundy &&
        a.minuty == b.minuty &&
        a.godziny == b.godziny &&
        a.dzien == b.dzien &&
        a.miesiac == b.miesiac &&
        a.rok == b.rok &&
        a.stulecie == b.stulecie &&
        a.rejestr_b == b.rejestr_b &&
        a.rejestr_d == b.rejestr_d;
}

bool pobierz_stabilna_probke(
    ProbkaRTC* wynik
) {
    if (!wynik) {
        return false;
    }

    for (uint32_t proba = 0;
         proba <
            MAX_PROB_STABILNEGO_ODCZYTU;
         ++proba) {

        if (!czekaj_na_koniec_aktualizacji()) {
            return false;
        }

        const ProbkaRTC pierwsza =
            pobierz_probke_surowa();

        /*
         * Jezeli aktualizacja ruszyla podczas pierwszego odczytu, probka
         * mogla zawierac dane z dwoch kolejnych sekund.
         */
        if (aktualizacja_w_toku()) {
            continue;
        }

        if (!czekaj_na_koniec_aktualizacji()) {
            return false;
        }

        const ProbkaRTC druga =
            pobierz_probke_surowa();

        if (aktualizacja_w_toku()) {
            continue;
        }

        if (probki_rowne(
                pierwsza,
                druga)) {

            *wynik =
                druga;

            return true;
        }
    }

    return false;
}

/* =========================================================================
 * 5. BCD / 12H / 24H
 * ========================================================================= */

bool poprawny_bcd(
    uint8_t wartosc
) {
    return
        (wartosc & 0x0FU) <= 9U &&
        ((wartosc >> 4) &
         0x0FU) <= 9U;
}

uint8_t bcd_na_bin(
    uint8_t wartosc
) {
    return
        static_cast<uint8_t>(
            (wartosc &
             0x0FU) +
            ((wartosc >> 4) &
             0x0FU) *
                10U
        );
}

bool skonwertuj_probke(
    const ProbkaRTC& surowa,
    czas_rtc* czas
) {
    if (!czas) {
        return false;
    }

    /*
     * VRT=0 oznacza, ze bateria/RTC nie gwarantuje poprawnego czasu.
     */
    if ((surowa.rejestr_d &
         RTC_D_VRT) == 0) {

        return false;
    }

    const bool tryb_binarny =
        (surowa.rejestr_b &
         RTC_B_BINARY) != 0;

    const bool tryb_24h =
        (surowa.rejestr_b &
         RTC_B_24H) != 0;

    /*
     * Bit 7 godzin jest znacznikiem PM tylko w trybie 12-godzinnym.
     */
    const bool pm =
        !tryb_24h &&
        (surowa.godziny &
         0x80U) != 0;

    uint8_t sekundy =
        surowa.sekundy;

    uint8_t minuty =
        surowa.minuty;

    uint8_t godziny =
        static_cast<uint8_t>(
            surowa.godziny &
            0x7FU
        );

    uint8_t dzien =
        surowa.dzien;

    uint8_t miesiac =
        surowa.miesiac;

    uint8_t rok2 =
        surowa.rok;

#if BURSZTYN_RTC_REJESTR_STULECIA != 0
    uint8_t stulecie =
        surowa.stulecie;
#endif

    if (!tryb_binarny) {
        if (!poprawny_bcd(
                sekundy) ||
            !poprawny_bcd(
                minuty) ||
            !poprawny_bcd(
                godziny) ||
            !poprawny_bcd(
                dzien) ||
            !poprawny_bcd(
                miesiac) ||
            !poprawny_bcd(
                rok2)) {

            return false;
        }

#if BURSZTYN_RTC_REJESTR_STULECIA != 0
        if (!poprawny_bcd(
                stulecie)) {

            return false;
        }
#endif

        sekundy =
            bcd_na_bin(
                sekundy
            );

        minuty =
            bcd_na_bin(
                minuty
            );

        godziny =
            bcd_na_bin(
                godziny
            );

        dzien =
            bcd_na_bin(
                dzien
            );

        miesiac =
            bcd_na_bin(
                miesiac
            );

        rok2 =
            bcd_na_bin(
                rok2
            );

#if BURSZTYN_RTC_REJESTR_STULECIA != 0
        stulecie =
            bcd_na_bin(
                stulecie
            );
#endif
    }

    /*
     * Konwersja 12 h -> 24 h.
     *
     * 12:xx AM -> 00:xx
     * 12:xx PM -> 12:xx
     *  1:xx PM -> 13:xx
     */
    if (!tryb_24h) {
        if (godziny < 1U ||
            godziny > 12U) {

            return false;
        }

        if (pm) {
            if (godziny != 12U) {
                godziny =
                    static_cast<uint8_t>(
                        godziny + 12U
                    );
            }
        } else if (godziny == 12U) {
            godziny = 0;
        }
    }

    uint16_t rok = 0;

#if BURSZTYN_RTC_REJESTR_STULECIA != 0
    if (stulecie < 19U ||
        stulecie > 99U) {

        return false;
    }

    rok =
        static_cast<uint16_t>(
            static_cast<uint16_t>(
                stulecie) *
                100U +
            rok2
        );
#else
    /*
     * Obecny format bez century register obejmuje 2000..2099.
     */
    rok =
        static_cast<uint16_t>(
            2000U +
            rok2
        );
#endif

    czas->sekundy =
        sekundy;

    czas->minuty =
        minuty;

    czas->godziny =
        godziny;

    czas->dzien =
        dzien;

    czas->miesiac =
        miesiac;

    czas->rok =
        rok;

    return true;
}

/* =========================================================================
 * 6. WALIDACJA KALENDARZA
 * ========================================================================= */

bool rok_przestepny(
    uint16_t rok
) {
    if ((rok % 400U) == 0U) {
        return true;
    }

    if ((rok % 100U) == 0U) {
        return false;
    }

    return
        (rok % 4U) == 0U;
}

uint8_t dni_w_miesiacu(
    uint16_t rok,
    uint8_t miesiac
) {
    switch (miesiac) {
        case 1:
            return 31;

        case 2:
            return
                rok_przestepny(
                    rok)
                    ? 29
                    : 28;

        case 3:
            return 31;

        case 4:
            return 30;

        case 5:
            return 31;

        case 6:
            return 30;

        case 7:
            return 31;

        case 8:
            return 31;

        case 9:
            return 30;

        case 10:
            return 31;

        case 11:
            return 30;

        case 12:
            return 31;

        default:
            return 0;
    }
}

bool czas_poprawny(
    const czas_rtc& czas
) {
    if (czas.sekundy > 59U ||
        czas.minuty > 59U ||
        czas.godziny > 23U) {

        return false;
    }

    if (czas.rok < 1900U) {
        return false;
    }

    if (czas.miesiac < 1U ||
        czas.miesiac > 12U) {

        return false;
    }

    const uint8_t maks_dzien =
        dni_w_miesiacu(
            static_cast<uint16_t>(
                czas.rok),
            static_cast<uint8_t>(
                czas.miesiac)
        );

    if (czas.dzien < 1U ||
        czas.dzien >
            maks_dzien) {

        return false;
    }

    return true;
}

/* =========================================================================
 * 7. KALENDARZ - DZIEN TYGODNIA
 * ========================================================================= */

/*
 * Algorytm Sakamoto:
 *
 * wynik:
 *   0 = niedziela
 *   1 = poniedzialek
 *   ...
 *   6 = sobota
 */
uint8_t dzien_tygodnia(
    uint16_t rok,
    uint8_t miesiac,
    uint8_t dzien
) {
    static constexpr uint8_t tabela[12] = {
        0, 3, 2, 5, 0, 3,
        5, 1, 4, 6, 2, 4
    };

    uint32_t y =
        rok;

    if (miesiac < 3U) {
        --y;
    }

    const uint32_t wynik =
        y +
        y / 4U -
        y / 100U +
        y / 400U +
        tabela[miesiac - 1U] +
        dzien;

    return
        static_cast<uint8_t>(
            wynik % 7U
        );
}

uint8_t ostatnia_niedziela(
    uint16_t rok,
    uint8_t miesiac
) {
    const uint8_t ostatni =
        dni_w_miesiacu(
            rok,
            miesiac
        );

    const uint8_t dzien_tyg =
        dzien_tygodnia(
            rok,
            miesiac,
            ostatni
        );

    /*
     * Jezeli ostatni dzien miesiaca:
     *   niedziela -> odejmij 0
     *   poniedzialek -> odejmij 1
     *   ...
     */
    return
        static_cast<uint8_t>(
            ostatni -
            dzien_tyg
        );
}

/* =========================================================================
 * 8. POLSKA STREFA CZASOWA CET / CEST
 * ========================================================================= */

/*
 * Zasada UE uzywana obecnie przez Polske:
 *
 *   CEST zaczyna sie:
 *       ostatnia niedziela marca, 01:00 UTC
 *
 *   CEST konczy sie:
 *       ostatnia niedziela pazdziernika, 01:00 UTC
 *
 * Zwracamy przesuniecie lokalne od UTC w minutach.
 *
 * Ta logika zaklada wspolczesne zasady i jest przeznaczona dla obecnego
 * zakresu lat BSP/RTC, nie jako historyczna baza stref czasowych.
 */
int32_t polska_strefa_minuty_utc(
    const czas_rtc& utc
) {
    if (utc.miesiac < 3U ||
        utc.miesiac > 10U) {

        return 60;
    }

    if (utc.miesiac > 3U &&
        utc.miesiac < 10U) {

        return 120;
    }

    if (utc.miesiac == 3U) {
        const uint8_t zmiana =
            ostatnia_niedziela(
                static_cast<uint16_t>(
                    utc.rok),
                3
            );

        if (utc.dzien > zmiana) {
            return 120;
        }

        if (utc.dzien < zmiana) {
            return 60;
        }

        /*
         * W dniu zmiany:
         * 00:xx UTC -> CET
         * 01:xx UTC -> CEST
         */
        return
            utc.godziny >= 1U
                ? 120
                : 60;
    }

    /*
     * Pazdziernik.
     */
    const uint8_t zmiana =
        ostatnia_niedziela(
            static_cast<uint16_t>(
                utc.rok),
            10
        );

    if (utc.dzien < zmiana) {
        return 120;
    }

    if (utc.dzien > zmiana) {
        return 60;
    }

    /*
     * W dniu zmiany:
     * 00:xx UTC -> jeszcze CEST
     * 01:xx UTC -> juz CET
     */
    return
        utc.godziny < 1U
            ? 120
            : 60;
}

/* =========================================================================
 * 9. PRZESUWANIE DATY/CZASU
 * ========================================================================= */

void nastepny_dzien(
    czas_rtc* czas
) {
    if (!czas) {
        return;
    }

    const uint8_t maks =
        dni_w_miesiacu(
            static_cast<uint16_t>(
                czas->rok),
            static_cast<uint8_t>(
                czas->miesiac)
        );

    if (czas->dzien <
        maks) {

        ++czas->dzien;
        return;
    }

    czas->dzien = 1;

    if (czas->miesiac < 12U) {
        ++czas->miesiac;
        return;
    }

    czas->miesiac = 1;
    ++czas->rok;
}

void poprzedni_dzien(
    czas_rtc* czas
) {
    if (!czas) {
        return;
    }

    if (czas->dzien > 1U) {
        --czas->dzien;
        return;
    }

    if (czas->miesiac > 1U) {
        --czas->miesiac;
    } else {
        czas->miesiac = 12;

        if (czas->rok > 0U) {
            --czas->rok;
        }
    }

    czas->dzien =
        dni_w_miesiacu(
            static_cast<uint16_t>(
                czas->rok),
            static_cast<uint8_t>(
                czas->miesiac)
        );
}

bool przesun_czas_o_minuty(
    czas_rtc* czas,
    int32_t przesuniecie
) {
    if (!czas ||
        !czas_poprawny(
            *czas)) {

        return false;
    }

    int32_t minuty_dnia =
        static_cast<int32_t>(
            czas->godziny) *
            60 +
        static_cast<int32_t>(
            czas->minuty);

    minuty_dnia +=
        przesuniecie;

    while (minuty_dnia >=
           24 * 60) {

        minuty_dnia -=
            24 * 60;

        nastepny_dzien(
            czas
        );
    }

    while (minuty_dnia < 0) {
        minuty_dnia +=
            24 * 60;

        poprzedni_dzien(
            czas
        );
    }

    czas->godziny =
        static_cast<uint8_t>(
            minuty_dnia / 60
        );

    czas->minuty =
        static_cast<uint8_t>(
            minuty_dnia % 60
        );

    return
        czas_poprawny(
            *czas
        );
}

/* =========================================================================
 * 10. CACHE OSTATNIEGO POPRAWNEGO CZASU
 * ========================================================================= */

czas_rtc ostatni_poprawny_czas{};
bool mamy_ostatni_poprawny_czas = false;

void ustaw_czas_awaryjny(
    czas_rtc* czas
) {
    if (!czas) {
        return;
    }

    czas->sekundy = 0;
    czas->minuty = 0;
    czas->godziny = 0;

    czas->dzien = 1;
    czas->miesiac = 1;
    czas->rok = 2000;
}

/* =========================================================================
 * 11. ODCZYT WEWNETRZNY
 * ========================================================================= */

bool pobierz_czas_rtc_wewnetrznie(
    czas_rtc* wynik
) {
    if (!wynik) {
        return false;
    }

    ProbkaRTC probka{};

    if (!pobierz_stabilna_probke(
            &probka)) {

        return false;
    }

    czas_rtc odczyt{};

    if (!skonwertuj_probke(
            probka,
            &odczyt)) {

        return false;
    }

    if (!czas_poprawny(
            odczyt)) {

        return false;
    }

#if BURSZTYN_RTC_CMOS_JEST_UTC
    const int32_t przesuniecie =
        polska_strefa_minuty_utc(
            odczyt
        );

    if (!przesun_czas_o_minuty(
            &odczyt,
            przesuniecie)) {

        return false;
    }
#endif

    if (!czas_poprawny(
            odczyt)) {

        return false;
    }

    *wynik =
        odczyt;

    return true;
}

} // namespace

/* =========================================================================
 * 12. PUBLICZNE API
 * ========================================================================= */

void pobierz_czas_rtc(
    czas_rtc* czas
) {
    if (!czas) {
        return;
    }

    /*
     * Obejmuje caly handshake portow CMOS oraz dostep do cache.
     */
    BlokadaCMOS blokada;

    czas_rtc nowy{};

    if (pobierz_czas_rtc_wewnetrznie(
            &nowy)) {

        ostatni_poprawny_czas =
            nowy;

        mamy_ostatni_poprawny_czas =
            true;

        *czas =
            nowy;

        return;
    }

    /*
     * Chwilowa granica aktualizacji, uszkodzony VRT albo timeout nie
     * powinny wprowadzac losowych cyfr na pulpit ani do BWS.
     */
    if (mamy_ostatni_poprawny_czas) {
        *czas =
            ostatni_poprawny_czas;

        return;
    }

    /*
     * API jest historycznie void, wiec nie ma kanalu na blad.
     * Zamiast pozostawic niezainicjalizowana strukture zwracamy jawny,
     * poprawny kalendarzowo fallback.
     *
     * Docelowo warto dodac:
     *
     *   bool pobierz_czas_rtc_bezpiecznie(czas_rtc*);
     */
    ustaw_czas_awaryjny(
        czas
    );
}

/* =========================================================================
 * 13. FORMATOWANIE HH:MM:SS
 * ========================================================================= */

void formatuj_czas_do_stringa(
    const czas_rtc* czas,
    char* bufor
) {
    if (!bufor) {
        return;
    }

    if (!czas ||
        !czas_poprawny(
            *czas)) {

        /*
         * Nadal gwarantujemy prawidlowy string 9-bajtowy.
         */
        bufor[0] = '-';
        bufor[1] = '-';
        bufor[2] = ':';
        bufor[3] = '-';
        bufor[4] = '-';
        bufor[5] = ':';
        bufor[6] = '-';
        bufor[7] = '-';
        bufor[8] = '\0';

        return;
    }

    const uint8_t godziny =
        static_cast<uint8_t>(
            czas->godziny
        );

    const uint8_t minuty =
        static_cast<uint8_t>(
            czas->minuty
        );

    const uint8_t sekundy =
        static_cast<uint8_t>(
            czas->sekundy
        );

    bufor[0] =
        static_cast<char>(
            '0' +
            godziny / 10U
        );

    bufor[1] =
        static_cast<char>(
            '0' +
            godziny % 10U
        );

    bufor[2] = ':';

    bufor[3] =
        static_cast<char>(
            '0' +
            minuty / 10U
        );

    bufor[4] =
        static_cast<char>(
            '0' +
            minuty % 10U
        );

    bufor[5] = ':';

    bufor[6] =
        static_cast<char>(
            '0' +
            sekundy / 10U
        );

    bufor[7] =
        static_cast<char>(
            '0' +
            sekundy % 10U
        );

    bufor[8] = '\0';
}
