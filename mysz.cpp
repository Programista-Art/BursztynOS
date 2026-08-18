/*
 * Bursztyn OS - sterownik myszy PS/2
 *
 * Obsluguje standardowa mysz PS/2 w trybie 3-bajtowych pakietow:
 *
 *   bajt 0:
 *     bit 0 - lewy przycisk
 *     bit 1 - prawy przycisk
 *     bit 2 - srodkowy przycisk
 *     bit 3 - zawsze 1 w poprawnym pierwszym bajcie pakietu
 *     bit 4 - znak X
 *     bit 5 - znak Y
 *     bit 6 - overflow X
 *     bit 7 - overflow Y
 *
 *   bajt 1 - przesuniecie X
 *   bajt 2 - przesuniecie Y
 *
 * EOI Local APIC NIE jest wysylane w tym pliku.
 * Odpowiada za nie wspolny dispatcher IDT.
 */

#include <stdint.h>
#include <stdbool.h>
#include "mysz_input.h"

/* =========================================================================
 * POLACZENIE Z PODSYSTEMEM GRAFIKI
 * ========================================================================= */

/* =========================================================================
 * STALE KONTROLERA PS/2
 * ========================================================================= */

namespace {

constexpr uint16_t PORT_PS2_DANE   = 0x60;
constexpr uint16_t PORT_PS2_STATUS = 0x64;
constexpr uint16_t PORT_PS2_CMD    = 0x64;

constexpr uint8_t STATUS_OUTPUT_FULL = 1U << 0;
constexpr uint8_t STATUS_INPUT_FULL  = 1U << 1;
constexpr uint8_t STATUS_AUX_DATA    = 1U << 5;

constexpr uint8_t KONTROLER_WLACZ_PORT_MYSZY = 0xA8;
constexpr uint8_t KONTROLER_CZYTAJ_KONFIG    = 0x20;
constexpr uint8_t KONTROLER_ZAPISZ_KONFIG    = 0x60;
constexpr uint8_t KONTROLER_DO_MYSZY          = 0xD4;

constexpr uint8_t KONFIG_IRQ_MYSZY            = 1U << 1;
constexpr uint8_t KONFIG_ZABLOKUJ_ZEGAR_MYSZY = 1U << 5;

constexpr uint8_t MYSZ_USTAW_DOMYSLNE         = 0xF6;
constexpr uint8_t MYSZ_WLACZ_RAPORTOWANIE     = 0xF4;

constexpr uint8_t MYSZ_ACK                     = 0xFA;
constexpr uint8_t MYSZ_RESEND                  = 0xFE;

constexpr uint8_t PAKIET_BIT_ZAWSZE_1          = 1U << 3;
constexpr uint8_t PAKIET_X_OVERFLOW            = 1U << 6;
constexpr uint8_t PAKIET_Y_OVERFLOW            = 1U << 7;

constexpr uint32_t LIMIT_OCZEKIWANIA           = 100000U;
constexpr uint32_t LIMIT_PROB_RESEND            = 3U;

constexpr uint8_t ROZMIAR_PAKIETU               = 3U;

/* =========================================================================
 * STAN STEROWNIKA
 * ========================================================================= */

uint8_t pakiet[ROZMIAR_PAKIETU] = {};
uint8_t indeks_pakietu = 0;

bool mysz_zainicjalizowana = false;

/*
 * Liczniki diagnostyczne. Nie sa obecnie eksportowane przez BWS,
 * ale pomagaja przy debugowaniu w GDB/serialu.
 */
uint64_t licznik_blednych_pakietow = 0;
uint64_t licznik_przepelnien_ruchu = 0;

/* =========================================================================
 * NISKIE I/O
 * ========================================================================= */

static inline void wyjscie_port_bajt(
    uint16_t port,
    uint8_t wartosc
) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(wartosc), "Nd"(port)
        : "memory"
    );
}

static inline uint8_t wejscie_port_bajt(
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

/*
 * Krotkie opoznienie I/O przez port 0x80.
 * Na prawdziwym sprzecie ogranicza zbyt szybkie sekwencje dostepow
 * do starszego kontrolera 8042.
 */
static inline void opoznienie_io() {
    asm volatile(
        "outb %%al, $0x80"
        :
        : "a"(0)
        : "memory"
    );
}

/* =========================================================================
 * OCZEKIWANIE NA KONTROLER
 * ========================================================================= */

bool czekaj_na_mozliwosc_zapisu() {
    for (uint32_t i = 0; i < LIMIT_OCZEKIWANIA; ++i) {
        const uint8_t status =
            wejscie_port_bajt(PORT_PS2_STATUS);

        if ((status & STATUS_INPUT_FULL) == 0) {
            return true;
        }

        asm volatile("pause");
    }

    return false;
}

bool czekaj_na_dane(
    bool wymagaj_danych_myszy,
    uint8_t* bajt_wyj
) {
    if (!bajt_wyj) return false;

    for (uint32_t i = 0; i < LIMIT_OCZEKIWANIA; ++i) {
        const uint8_t status =
            wejscie_port_bajt(PORT_PS2_STATUS);

        if ((status & STATUS_OUTPUT_FULL) == 0) {
            asm volatile("pause");
            continue;
        }

        /*
         * Jesli czekamy na odpowiedz myszy, nie wolno pomylic
         * scancode'u klawiatury z ACK myszy.
         *
         * Podczas inicjalizacji jadra przerwania sa wylaczone, dlatego
         * obcy bajt musimy usunac z bufora kontrolera, aby nie blokowal
         * odpowiedzi urzadzenia.
         */
        const bool dane_myszy =
            (status & STATUS_AUX_DATA) != 0;

        const uint8_t bajt =
            wejscie_port_bajt(PORT_PS2_DANE);

        if (wymagaj_danych_myszy && !dane_myszy) {
            continue;
        }

        *bajt_wyj = bajt;
        return true;
    }

    return false;
}

/*
 * Usuwa stare bajty oczekujace w buforze wyjsciowym kontrolera.
 * Funkcja jest uzywana tylko podczas inicjalizacji przed wlaczeniem IRQ.
 */
void oproznij_bufor_kontrolera() {
    for (uint32_t i = 0; i < 64U; ++i) {
        const uint8_t status =
            wejscie_port_bajt(PORT_PS2_STATUS);

        if ((status & STATUS_OUTPUT_FULL) == 0) {
            break;
        }

        (void)wejscie_port_bajt(PORT_PS2_DANE);
        opoznienie_io();
    }
}

/* =========================================================================
 * KOMENDY KONTROLERA 8042
 * ========================================================================= */

bool wyslij_komende_kontrolera(
    uint8_t komenda
) {
    if (!czekaj_na_mozliwosc_zapisu()) {
        return false;
    }

    wyjscie_port_bajt(
        PORT_PS2_CMD,
        komenda
    );

    opoznienie_io();
    return true;
}

bool zapisz_dane_kontrolera(
    uint8_t dane
) {
    if (!czekaj_na_mozliwosc_zapisu()) {
        return false;
    }

    wyjscie_port_bajt(
        PORT_PS2_DANE,
        dane
    );

    opoznienie_io();
    return true;
}

bool odczytaj_konfiguracje_kontrolera(
    uint8_t* konfiguracja
) {
    if (!konfiguracja) return false;

    if (!wyslij_komende_kontrolera(
            KONTROLER_CZYTAJ_KONFIG)) {
        return false;
    }

    /*
     * Odpowiedz 0x20 pochodzi od kontrolera, nie od myszy,
     * dlatego nie wymagamy bitu AUX.
     */
    return czekaj_na_dane(
        false,
        konfiguracja
    );
}

bool zapisz_konfiguracje_kontrolera(
    uint8_t konfiguracja
) {
    if (!wyslij_komende_kontrolera(
            KONTROLER_ZAPISZ_KONFIG)) {
        return false;
    }

    return zapisz_dane_kontrolera(
        konfiguracja
    );
}

/* =========================================================================
 * KOMENDY DO MYSZY
 * ========================================================================= */

bool wyslij_bajt_do_myszy_bez_ack(
    uint8_t bajt
) {
    if (!wyslij_komende_kontrolera(
            KONTROLER_DO_MYSZY)) {
        return false;
    }

    return zapisz_dane_kontrolera(
        bajt
    );
}

bool wyslij_komende_myszy(
    uint8_t komenda
) {
    /*
     * PS/2 moze odpowiedziec 0xFE (RESEND). Ponawiamy wtedy komende,
     * ale z ograniczona liczba prob.
     */
    for (uint32_t proba = 0;
         proba < LIMIT_PROB_RESEND;
         ++proba) {

        if (!wyslij_bajt_do_myszy_bez_ack(
                komenda)) {
            return false;
        }

        uint8_t odpowiedz = 0;

        if (!czekaj_na_dane(
                true,
                &odpowiedz)) {
            return false;
        }

        if (odpowiedz == MYSZ_ACK) {
            return true;
        }

        if (odpowiedz != MYSZ_RESEND) {
            return false;
        }
    }

    return false;
}

/* =========================================================================
 * OBRABIANIE PAKIETU MYSZY
 * ========================================================================= */

void zresetuj_synchronizacje_pakietu() {
    indeks_pakietu = 0;
}

void przetworz_pakiet() {
    const uint8_t flagi =
        pakiet[0];

    /*
     * Bit 3 pierwszego bajtu standardowego pakietu PS/2 zawsze wynosi 1.
     */
    if ((flagi & PAKIET_BIT_ZAWSZE_1) == 0) {
        ++licznik_blednych_pakietow;
        return;
    }

    /*
     * Przy overflow wartosc X/Y jest niewiarygodna. Zamiast generowac
     * ogromny skok kursora odrzucamy caly ruch z tego pakietu.
     * Stan przyciskow rowniez jest pomijany, aby packet byl atomowy.
     */
    if ((flagi &
         (PAKIET_X_OVERFLOW | PAKIET_Y_OVERFLOW)) != 0) {
        ++licznik_przepelnien_ruchu;
        return;
    }

    /*
     * Drugi i trzeci bajt sa 8-bitowymi wartosciami ze znakiem
     * w kodzie uzupelnien do dwoch.
     */
    const int dx =
        static_cast<int>(
            static_cast<int8_t>(pakiet[1]));

    const int dy =
        static_cast<int>(
            static_cast<int8_t>(pakiet[2]));

    const uint8_t przyciski =
        static_cast<uint8_t>(
            flagi & MYSZ_MASKA_PRZYCISKOW);

    zaktualizuj_mysze(
        dx,
        dy,
        przyciski
    );
}

void przyjmij_bajt_pakietu(
    uint8_t bajt
) {
    if (indeks_pakietu == 0) {
        /*
         * Szukamy prawidlowego pierwszego bajtu. Po utracie jednego
         * bajtu transmisji pozwala to odzyskac synchronizacje.
         */
        if ((bajt & PAKIET_BIT_ZAWSZE_1) == 0) {
            ++licznik_blednych_pakietow;
            return;
        }

        pakiet[0] = bajt;
        indeks_pakietu = 1;
        return;
    }

    if (indeks_pakietu >= ROZMIAR_PAKIETU) {
        ++licznik_blednych_pakietow;
        zresetuj_synchronizacje_pakietu();
        return;
    }

    pakiet[indeks_pakietu++] = bajt;

    if (indeks_pakietu == ROZMIAR_PAKIETU) {
        przetworz_pakiet();
        zresetuj_synchronizacje_pakietu();
    }
}

} // namespace

/* =========================================================================
 * INICJALIZACJA MYSZY PS/2
 * ========================================================================= */

extern "C" void InicjalizujMyszPS2() {
    mysz_zainicjalizowana = false;
    zresetuj_synchronizacje_pakietu();

    oproznij_bufor_kontrolera();

    /*
     * Wlacz drugi port kontrolera 8042.
     */
    if (!wyslij_komende_kontrolera(
            KONTROLER_WLACZ_PORT_MYSZY)) {
        return;
    }

    uint8_t konfiguracja = 0;

    if (!odczytaj_konfiguracje_kontrolera(
            &konfiguracja)) {
        return;
    }

    /*
     * Wlacz IRQ12 i upewnij sie, ze zegar drugiego portu nie jest
     * zablokowany.
     *
     * Pozostale bity konfiguracji zachowujemy.
     */
    konfiguracja |=
        KONFIG_IRQ_MYSZY;

    konfiguracja &=
        static_cast<uint8_t>(
            ~KONFIG_ZABLOKUJ_ZEGAR_MYSZY);

    if (!zapisz_konfiguracje_kontrolera(
            konfiguracja)) {
        return;
    }

    /*
     * 0xF6 przywraca standardowe parametry:
     *   - 100 samples/s,
     *   - rozdzielczosc 4 counts/mm,
     *   - scaling 1:1.
     */
    if (!wyslij_komende_myszy(
            MYSZ_USTAW_DOMYSLNE)) {
        return;
    }

    /*
     * 0xF4 uruchamia stream mode.
     */
    if (!wyslij_komende_myszy(
            MYSZ_WLACZ_RAPORTOWANIE)) {
        return;
    }

    mysz_zainicjalizowana = true;
}

/* =========================================================================
 * IRQ12
 * ========================================================================= */

extern "C" void obsluga_przerwania_myszy() {
    const uint8_t status =
        wejscie_port_bajt(
            PORT_PS2_STATUS);

    /*
     * IRQ12 powinno wskazywac bajt myszy. Nie odczytujemy portu 0x60,
     * jesli bufor jest pusty albo zawiera bajt klawiatury.
     *
     * Jest to wazne, bo klawiatura i mysz dziela ten sam port danych.
     */
    if ((status & STATUS_OUTPUT_FULL) == 0) {
        return;
    }

    if ((status & STATUS_AUX_DATA) == 0) {
        return;
    }

    const uint8_t bajt =
        wejscie_port_bajt(
            PORT_PS2_DANE);

    if (!mysz_zainicjalizowana) {
        return;
    }

    przyjmij_bajt_pakietu(
        bajt);

    /*
     * Brak EOI tutaj.
     *
     * WspolnaObslugaPrzerwan() w idt.cpp wykonuje EOI centralnie
     * po zakonczeniu handlera. Zapobiega to podwojnemu EOI oraz
     * zachowuje jedna polityke zakonczenia wszystkich IRQ APIC.
     */
}
