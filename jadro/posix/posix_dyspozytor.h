#pragma once
#include "posix_abi.h"

/*
 * Bursztyn OS - dyspozytor bramki SysV.
 *
 * Ten naglowek jest wspolny dla jadra i testu hosta. Dyspozytor NIE wola
 * zadnego podsystemu bezposrednio: dostaje tablice uslug (callbackow), wiec
 * test hosta podmienia je stubami i sprawdza wylacznie to, co jest jego
 * zadaniem - mapowanie rejestrow na argumenty, konwencje errno i walidacje.
 *
 * Zasady:
 *
 *   - wynik 0..4095 to sukces (liczba bajtow, PID, 0),
 *   - kazdy blad to -errno z przedzialu -1..-4095,
 *   - nieznany numer to -ENOSYS (nigdy 0 - 0 wyglada jak sukces),
 *   - usluga, ktorej nie ma (nullptr), tez zwraca -ENOSYS,
 *   - dyspozytor nie dotyka pamieci Ring 3 samodzielnie; dostaje gotowe
 *     kopie argumentow tam, gdzie liczy sie wskaznik (uslugi robia kopie).
 */

/* =========================================================================
 * USLUGI
 * ========================================================================= */

/*
 * Kazda usluga dostaje juz zdekodowane argumenty i kontekst wywolujacego.
 * Umowa: zwracaja 0 lub wartosc dodatnia przy sukcesie, -errno przy bledzie.
 * Dyspozytor nie poprawia wynikow - bledne errno to blad uslugi.
 */
struct PosixUslugi {
    /* Zapis do standardowego wyjscia procesu (fd 1/2). */
    int64_t (*write)(void* kontekst, int fd, const void* dane, uint64_t dlugosc);
    /* Zakonczenie procesu. Nie wraca. */
    void (*exit_proces)(void* kontekst, int kod);
    int64_t (*getpid)(void* kontekst);
    int64_t (*gettid)(void* kontekst);
    /* Wypelnia PosixUtsname w pamieci Ring 3; usluga robi copy_to_user. */
    int64_t (*uname)(void* kontekst, uint64_t bufor);
    int64_t (*clock_gettime)(void* kontekst, int clock_id, uint64_t bufor_ts);
    int64_t (*gettimeofday)(void* kontekst, uint64_t bufor_tv, uint64_t bufor_tz);
    int64_t (*arch_prctl)(void* kontekst, int kod, uint64_t adres);
    int64_t (*getrandom)(void* kontekst, uint64_t bufor, uint64_t dlugosc);
    /* Program break; zwraca nowy break albo -ENOMEM. */
    int64_t (*brk)(void* kontekst, uint64_t nowy_break);
    int64_t (*mmap)(void* kontekst, uint64_t adres, uint64_t dlugosc,
                    uint64_t ochrona, uint64_t flagi, int64_t fd, uint64_t offset);
    int64_t (*munmap)(void* kontekst, uint64_t adres, uint64_t dlugosc);
    int64_t (*mprotect)(void* kontekst, uint64_t adres, uint64_t dlugosc,
                        uint64_t ochrona);
    int64_t (*futex)(void* kontekst, uint64_t adres, uint64_t operacja,
                     uint64_t wartosc, uint64_t timeout, uint64_t adres2,
                     uint64_t wartosc3);
    void* kontekst;
};
/* =========================================================================
 * DYSPOZYTOR
 * ========================================================================= */

/*
 * Rekord diagnostyczny: ostatnie wywolanie kazdego procesu. Zawsze
 * nadpisywany, bez historii - nie rosnie i nie wymaga alokacji.
 */
struct PosixOstatnieWywolanie {
    uint64_t numer;
    int64_t wynik;
    uint64_t licznik;
};

struct PosixDiagnostyka {
    PosixOstatnieWywolanie ostatnie;
    uint64_t licznik_wywolan;
    uint64_t licznik_enosys;
    uint64_t licznik_blad;
};

/*
 * Glowna funkcja. Zwraca wartosc do wpisania w RAX procesu.
 *
 * `rama` jest modyfikowana tylko w polu `rax`. Argumenty sa kopiowane
 * lokalnie, wiec wywolujacy moze trzymac je na stosie IRQ.
 */
int64_t posix_obsluz(const PosixUslugi& uslugi,
                     const PosixRama& rama,
                     PosixDiagnostyka* diagnostyka);

/* Nazwa wywolania do logow (albo "nieznane"). */
const char* posix_nazwa_wywolania(uint64_t numer);
