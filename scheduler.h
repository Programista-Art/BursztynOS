/*
 * Bursztyn OS - publiczny interfejs schedulera
 *
 * Naglowek definiuje:
 *  - limity i stany procesow,
 *  - dokladny format ramy ISR RejestryStanowe,
 *  - globalny stan planisty,
 *  - API inicjalizacji i przelaczania kontekstu,
 *  - API oczekiwania na zdarzenia myszy,
 *  - pomocnicza kontrole single-instance.
 *
 * WAZNE:
 * RejestryStanowe jest ABI pomiedzy:
 *
 *   przerwania.S <-> idt.cpp <-> scheduler.cpp <-> loader.cpp
 *
 * Zmiana kolejnosci lub typu ktoregokolwiek pola wymaga jednoczesnej
 * aktualizacji kodu asemblerowego i wszystkich modulow korzystajacych
 * z tej ramy.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pzb.h"

/* =========================================================================
 * 1. LIMIT PROCESOW
 * ========================================================================= */

/*
 * Jedynym zrodlem prawdy dla rozmiaru tablicy procesow jest teraz pzb.h.
 * Usuwa to ryzyko rozjazdu:
 *
 *   scheduler.h: 16
 *   pzb.h:       inna wartosc
 *
 * Scheduler uzywa PID typu int, dlatego alias ma rowniez typ int.
 * Zapobiega to ostrzezeniom/bledom -Wsign-compare w petlach planisty.
 */
#ifdef __cplusplus

inline constexpr int MAKS_PROCESOW =
    static_cast<int>(
        PZB_MAKS_PROCESOW
    );

static_assert(
    MAKS_PROCESOW == 16,
    "Obecne ABI schedulera Bursztyn OS zaklada 16 slotow procesow"
);

#endif

/* =========================================================================
 * 2. STANY PROCESU
 * ========================================================================= */

/*
 * Wartosci sa zachowane dla zgodnosci z obecnym loaderem i schedulerem.
 *
 * PROCES_PUSTY:
 *   slot nie zawiera aktywnego procesu.
 *
 * PROCES_GOTOWY:
 *   proces moze zostac wybrany przez round-robin.
 *
 * PROCES_ZABLOKOWANY:
 *   stan ogolny/rezerwacyjny. Loader wykorzystuje go podczas budowania
 *   nowego procesu przed atomowym opublikowaniem PROCES_GOTOWY.
 *
 * PROCES_ZABLOKOWANY_MYSZ:
 *   proces oczekuje na nowe zdarzenie myszy lub timeout schedulera.
 */
#define PROCES_PUSTY             0
#define PROCES_GOTOWY            1
#define PROCES_ZABLOKOWANY       2
#define PROCES_ZABLOKOWANY_MYSZ  3

#define PROCES_STAN_MIN PROCES_PUSTY
#define PROCES_STAN_MAX PROCES_ZABLOKOWANY_MYSZ

#ifdef __cplusplus

static_assert(
    PROCES_PUSTY == 0 &&
    PROCES_GOTOWY == 1 &&
    PROCES_ZABLOKOWANY == 2 &&
    PROCES_ZABLOKOWANY_MYSZ == 3,
    "Zmiana numerow stanow wymaga aktualizacji loadera i schedulera"
);

inline constexpr bool scheduler_stan_procesu_poprawny(
    int stan
) noexcept {
    return
        stan >= PROCES_STAN_MIN &&
        stan <= PROCES_STAN_MAX;
}

inline constexpr bool scheduler_pid_poprawny(
    int pid
) noexcept {
    return
        pid >= 0 &&
        pid <
            static_cast<int>(
                MAKS_PROCESOW
            );
}

inline constexpr bool scheduler_pid_uzytkownika(
    int pid
) noexcept {
    return
        pid > 0 &&
        pid <
            static_cast<int>(
                MAKS_PROCESOW
            );
}

#endif /* __cplusplus */

/* =========================================================================
 * 3. RAMA PRZERWANIA / KONTEKSTU CPU
 * ========================================================================= */

/*
 * Dokladne odwzorowanie stosu utworzonego przez wspolne_isr_wejscie
 * w przerwania.S.
 *
 * Po zapisaniu rejestrow RSP wskazuje na pole r15.
 *
 * Offsety:
 *
 *   0x00  r15
 *   0x08  r14
 *   0x10  r13
 *   0x18  r12
 *   0x20  r11
 *   0x28  r10
 *   0x30  r9
 *   0x38  r8
 *
 *   0x40  rbp
 *   0x48  rsi
 *   0x50  rdi
 *   0x58  rdx
 *   0x60  rcx
 *   0x68  rbx
 *   0x70  rax
 *
 *   0x78  wektor_przerwania
 *   0x80  kod_bledu
 *
 *   0x88  RIP / adres_powrotu
 *   0x90  CS
 *   0x98  RFLAGS
 *
 *   0xA0  stary RSP
 *   0xA8  stary SS
 *
 * Razem 0xB0 = 176 bajtow.
 *
 * Dla przejsc Ring 3 -> Ring 0 CPU zapisuje stary_rsp/stary_ss.
 * Loader tworzy te pola rowniez dla pierwszego sztucznego kontekstu
 * procesu Ring 3.
 *
 * Przy niektorych przerwaniach pozostajacych w tym samym CPL procesor
 * normalnie nie doklada RSP/SS. Kod korzystajacy z tych dwoch ostatnich
 * pol musi wiec wiedziec, z jakiego rodzaju ramy korzysta.
 */
struct RejestryStanowe {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t wektor_przerwania;
    uint64_t kod_bledu;

    uint64_t adres_powrotu;
    uint64_t rejestr_cs;
    uint64_t rflags;

    uint64_t stary_rsp;
    uint64_t stary_ss;
};

/* =========================================================================
 * 4. KONTROLA ABI RejestryStanowe
 * ========================================================================= */

#ifdef __cplusplus

static_assert(
    sizeof(uint64_t) == 8,
    "Scheduler wymaga 64-bitowego uint64_t"
);

static_assert(
    alignof(RejestryStanowe) == 8,
    "RejestryStanowe musi miec naturalne wyrownanie 8 bajtow"
);

static_assert(
    sizeof(RejestryStanowe) == 0xB0,
    "Rama ISR nie zgadza sie z przerwania.S"
);

static_assert(
    sizeof(RejestryStanowe) ==
        22U * sizeof(uint64_t),
    "RejestryStanowe musi zawierac dokladnie 22 pola uint64_t"
);

static_assert(
    offsetof(RejestryStanowe, r15) == 0x00,
    "Nieprawidlowy offset R15"
);

static_assert(
    offsetof(RejestryStanowe, r14) == 0x08,
    "Nieprawidlowy offset R14"
);

static_assert(
    offsetof(RejestryStanowe, r13) == 0x10,
    "Nieprawidlowy offset R13"
);

static_assert(
    offsetof(RejestryStanowe, r12) == 0x18,
    "Nieprawidlowy offset R12"
);

static_assert(
    offsetof(RejestryStanowe, r11) == 0x20,
    "Nieprawidlowy offset R11"
);

static_assert(
    offsetof(RejestryStanowe, r10) == 0x28,
    "Nieprawidlowy offset R10"
);

static_assert(
    offsetof(RejestryStanowe, r9) == 0x30,
    "Nieprawidlowy offset R9"
);

static_assert(
    offsetof(RejestryStanowe, r8) == 0x38,
    "Nieprawidlowy offset R8"
);

static_assert(
    offsetof(RejestryStanowe, rbp) == 0x40,
    "Nieprawidlowy offset RBP"
);

static_assert(
    offsetof(RejestryStanowe, rsi) == 0x48,
    "Nieprawidlowy offset RSI"
);

static_assert(
    offsetof(RejestryStanowe, rdi) == 0x50,
    "Nieprawidlowy offset RDI"
);

static_assert(
    offsetof(RejestryStanowe, rdx) == 0x58,
    "Nieprawidlowy offset RDX"
);

static_assert(
    offsetof(RejestryStanowe, rcx) == 0x60,
    "Nieprawidlowy offset RCX"
);

static_assert(
    offsetof(RejestryStanowe, rbx) == 0x68,
    "Nieprawidlowy offset RBX"
);

static_assert(
    offsetof(RejestryStanowe, rax) == 0x70,
    "Nieprawidlowy offset RAX"
);

static_assert(
    offsetof(
        RejestryStanowe,
        wektor_przerwania
    ) == 0x78,
    "Nieprawidlowy offset numeru wektora"
);

static_assert(
    offsetof(
        RejestryStanowe,
        kod_bledu
    ) == 0x80,
    "Nieprawidlowy offset kodu bledu"
);

static_assert(
    offsetof(
        RejestryStanowe,
        adres_powrotu
    ) == 0x88,
    "Nieprawidlowy offset RIP"
);

static_assert(
    offsetof(
        RejestryStanowe,
        rejestr_cs
    ) == 0x90,
    "Nieprawidlowy offset CS"
);

static_assert(
    offsetof(
        RejestryStanowe,
        rflags
    ) == 0x98,
    "Nieprawidlowy offset RFLAGS"
);

static_assert(
    offsetof(
        RejestryStanowe,
        stary_rsp
    ) == 0xA0,
    "Nieprawidlowy offset starego RSP"
);

static_assert(
    offsetof(
        RejestryStanowe,
        stary_ss
    ) == 0xA8,
    "Nieprawidlowy offset starego SS"
);

#endif /* __cplusplus */

/* =========================================================================
 * 5. GLOBALNY STAN PLANISTY
 * ========================================================================= */

/*
 * -1:
 *   scheduler nie zostal jeszcze poprawnie zainicjalizowany.
 *
 * 0:
 *   kontekst jadra/idle.
 *
 * 1..MAKS_PROCESOW-1:
 *   procesy Ring 3.
 */
extern int aktualny_pid;

/*
 * Planista moze zostac zainicjalizowany przed faktycznym wlaczeniem
 * round-robin. Kernel ustawia te flage dopiero po przygotowaniu IDT,
 * APIC, TSS i pierwszych procesow.
 */
extern bool wielozadaniowosc_aktywna;

/* =========================================================================
 * 6. INICJALIZACJA
 * ========================================================================= */

/*
 * Rejestruje PID0 jako kontekst jadra.
 *
 * kernel_rsp0:
 *   16-bajtowo wyrownany szczyt stosu jadra dla TSS.rsp0/SYSCALL.
 *
 * cr3:
 *   fizyczny adres PML4 jadra. Dolne bity flag sa maskowane przez
 *   implementacje schedulera.
 *
 * Funkcja zachowuje C++ linkage, zgodnie z scheduler.cpp.
 */
void InicjalizujPlaniste(
    uint64_t kernel_rsp0,
    uint64_t cr3
);

/* =========================================================================
 * 7. PRZELACZANIE KONTEKSTU
 * ========================================================================= */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wywolywane z dispatchera przerwania timera.
 *
 * stary_rsp:
 *   wskaznik RejestryStanowe aktualnego procesu.
 *
 * wynik:
 *   wskaznik RejestryStanowe procesu, z ktorego przerwania.S wykona
 *   POP GPR + IRETQ.
 *
 * Funkcja nie moze zwrocic 0 przy poprawnym dzialaniu.
 */
uint64_t PrzelaczKontekst(
    uint64_t stary_rsp
);

/*
 * Oznacza aktualny proces Ring 3 jako zakonczony.
 *
 * Funkcja logicznie nie wraca do programu wywolujacego. Czeka na timer,
 * ktory bezpiecznie przeniesie CPU na inny kernel stack; zwalnianie starego
 * stosu/PML4 jest wykonywane pozniej przez scheduler.
 */
void zakoncz_aktualny_proces();

/* =========================================================================
 * 8. ZDARZENIA MYSZY
 * ========================================================================= */

/*
 * Wywolywane przez podsystem myszy/grafiki po pojawieniu sie nowego
 * zdarzenia. Budzi procesy czekajace w PROCES_ZABLOKOWANY_MYSZ.
 *
 * Funkcja jest bezpieczna do wywolania z IRQ12; nie wykonuje STI.
 */
void WybudzProcesyOczekujaceNaMysz();

/* =========================================================================
 * 9. SINGLE-INSTANCE
 * ========================================================================= */

/*
 * Sprawdza, czy niepusty slot procesu posiada dokladnie te sama sciezke.
 *
 * sciezka musi byc zaufanym, zakonczonym NUL-em stringiem kernela o
 * dlugosci mieszczacej sie w PZB_DLUGOSC_SCIEZKI_PROCESU.
 *
 * Wskaznik Ring 3 nalezy najpierw skopiowac przez copy_from_user.
 */
bool czy_proces_uruchomiony(
    const char* sciezka
);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* =========================================================================
 * 10. BLOKOWANIE NA ZDARZENIE MYSZY
 * ========================================================================= */

/*
 * Zachowuje C++ linkage, zgodnie z scheduler.cpp.
 *
 * Jezeli od poprzedniego sprawdzenia nie pojawilo sie nowe zdarzenie,
 * funkcja oznacza aktualny proces jako PROCES_ZABLOKOWANY_MYSZ.
 *
 * WAZNE:
 * Funkcja NIE wykonuje aktywnego oczekiwania ani HLT w syscallu.
 * Po powrocie do Ring 3 najblizszy timer zapisze standardowa rame IRQ
 * i scheduler bedzie mogl bezpiecznie przejsc do innego procesu.
 */
void ZablokujAktualnyProcesNaMyszy();
