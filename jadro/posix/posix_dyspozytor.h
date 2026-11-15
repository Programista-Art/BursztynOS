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
    /* Zapis do standardowego wyjscia procesu (fd 1/2) albo do pliku. */
    int64_t (*write)(void* kontekst, int fd, const void* dane, uint64_t dlugosc);
    /* Odczyt z pliku albo terminala (terminal -> -ENOSYS). */
    int64_t (*read)(void* kontekst, int fd, uint64_t bufor, uint64_t dlugosc);
    /* Otwarcie sciezki Ring 3; zwraca fd albo -errno. */
    int64_t (*open)(void* kontekst, uint64_t sciezka, uint64_t flagi, uint64_t tryb);
    int64_t (*openat)(void* kontekst, int dirfd, uint64_t sciezka,
                      uint64_t flagi, uint64_t tryb);
    int64_t (*close_fd)(void* kontekst, int fd);
    int64_t (*lseek_fd)(void* kontekst, int fd, int64_t przesuniecie, int skad);
    int64_t (*fstat_fd)(void* kontekst, int fd, uint64_t bufor_stat);
    int64_t (*stat_sciezka)(void* kontekst, uint64_t sciezka, uint64_t bufor_stat);
    int64_t (*newfstatat_sciezka)(void* kontekst, int dirfd, uint64_t sciezka,
                                   uint64_t bufor_stat, uint32_t flagi);
    int64_t (*readlink_sciezka)(void* kontekst, uint64_t sciezka,
                                uint64_t bufor, uint64_t dlugosc);
    int64_t (*getdents64_fd)(void* kontekst, int fd, uint64_t bufor,
                             uint64_t dlugosc);
    int64_t (*poll_fds)(void* kontekst, uint64_t fds, uint64_t nfds,
                        int timeout_ms);
    /*
     * Wywolania startowe libc. Semantyka jest celowo minimalna i jawna:
     * zwracaja sukces tam, gdzie jadro potrafi dac prawdziwa odpowiedz,
     * i -ENOSYS tam, gdzie odpowiedz bylaby zmyslona.
     */
    int64_t (*set_tid_address)(void* kontekst, uint64_t wskaznik_tid);
    int64_t (*set_robust_list)(void* kontekst, uint64_t naglowek, uint64_t dlugosc);
    int64_t (*prlimit64)(void* kontekst, int pid, int zasob, uint64_t nowy,
                         uint64_t stary);
    int64_t (*sched_getaffinity)(void* kontekst, int pid, uint64_t rozmiar,
                                 uint64_t maska);
    int64_t (*getrlimit_zasob)(void* kontekst, int zasob, uint64_t limit);
    int64_t (*sysinfo_ogolny)(void* kontekst, uint64_t bufor);
    int64_t (*access_sciezka)(void* kontekst, uint64_t sciezka, int tryb);
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
    /* IPv4/TCP: uslugi waliduja wskazniki Ring 3 po stronie jadra. */
    int64_t (*socket)(void* kontekst, int domena, int typ, int protokol);
    int64_t (*connect)(void* kontekst, int fd, uint64_t adres, uint32_t dlugosc);
    int64_t (*sendto)(void* kontekst, int fd, uint64_t bufor, uint64_t dlugosc,
                      uint32_t flagi, uint64_t adres, uint32_t dlugosc_adresu);
    int64_t (*recvfrom)(void* kontekst, int fd, uint64_t bufor, uint64_t dlugosc,
                        uint32_t flagi, uint64_t adres, uint64_t dlugosc_adresu);
    int64_t (*bind)(void* kontekst, int fd, uint64_t adres, uint32_t dlugosc);
    int64_t (*listen)(void* kontekst, int fd, int backlog);
    int64_t (*accept)(void* kontekst, int fd, uint64_t adres,
                      uint64_t dlugosc_adresu);
    int64_t (*shutdown)(void* kontekst, int fd, int jak);
    int64_t (*getsockopt)(void* kontekst, int fd, int poziom, int opcja,
                          uint64_t wartosc, uint64_t dlugosc);
    int64_t (*setsockopt)(void* kontekst, int fd, int poziom, int opcja,
                          uint64_t wartosc, uint64_t dlugosc);
    /*
     * Interfejsy procesu, deskryptorow i sygnalow sa jawnie wystawione jako
     * opcjonalne uslugi. Obecny model nie dostarcza jeszcze ich semantyki:
     * nullptr oznacza -ENOSYS, a nie czesciowy sukces.
     */
    int64_t (*pipe)(void* kontekst, uint64_t bufor_fd);
    int64_t (*dup)(void* kontekst, int stary_fd);
    int64_t (*dup2)(void* kontekst, int stary_fd, int nowy_fd);
    int64_t (*fcntl)(void* kontekst, int fd, int polecenie, uint64_t argument);
    int64_t (*clone)(void* kontekst, uint64_t flagi, uint64_t stos,
                     uint64_t parent_tid, uint64_t child_tid, uint64_t tls);
    int64_t (*fork)(void* kontekst);
    int64_t (*execve)(void* kontekst, uint64_t sciezka, uint64_t argv,
                      uint64_t envp);
    int64_t (*wait4)(void* kontekst, int pid, uint64_t status,
                     uint64_t opcje, uint64_t rusage);
    int64_t (*kill)(void* kontekst, int pid, int sygnal);
    int64_t (*rt_sigaction)(void* kontekst, int sygnal, uint64_t akcja,
                            uint64_t stara_akcja, uint64_t rozmiar_maski);
    int64_t (*rt_sigprocmask)(void* kontekst, int how, uint64_t set,
                              uint64_t oldset, uint64_t rozmiar_maski);
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

/*
 * Gorna granica numeru wywolania objętego telemetria.
 *
 * Numery linuxowe siegaja ~450, wiec 512 pokrywa wszystkie realne
 * przypadki, a tablica ma 2 * 512 * 4 B = 4 KiB - miesci sie bezproblemowo
 * w BSS jadra i nie wymaga alokacji na sciezce syscallu.
 */
inline constexpr uint32_t POSIX_TELEMETRIA_MAKS = 512U;

struct PosixDiagnostyka {
    PosixOstatnieWywolanie ostatnie;
    uint64_t licznik_wywolan;
    uint64_t licznik_enosys;
    uint64_t licznik_blad;
    /*
     * Telemetria per numer: ktore wywolania sa wolane i ktore nie maja
     * implementacji. Bez tego "musl nie startuje" sprowadza sie do
     * zgadywania. Rozmiar jest staly (bez alokacji), a indeks jest
     * numerem wywolania ograniczonym do POSIX_TELEMETRIA_MAKS.
     */
    uint32_t liczba_wywolan_per_numer[POSIX_TELEMETRIA_MAKS];
    uint32_t liczba_enosys_per_numer[POSIX_TELEMETRIA_MAKS];
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
