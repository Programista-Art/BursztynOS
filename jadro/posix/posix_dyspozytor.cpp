#include "posix_dyspozytor.h"

/*
 * Bursztyn OS - dyspozytor bramki SysV (implementacja przenosna).
 *
 * Plik nie zawiera zadnej zaleznosci od jadra: kompiluje sie zarowno do jadra
 * (z -mno-sse, bez libc), jak i do testu hosta pod ASan/UBSan. Dzieki temu
 * mapowanie rejestrow, konwencja errno i walidacja numeru sa testowane bez
 * QEMU, a jadro dostaje dokladnie ten sam kod.
 *
 * Uklad rejestrow (Linux x86-64):
 *
 *   RAX = numer, RDI = arg1, RSI = arg2, RDX = arg3,
 *   R10 = arg4, R8  = arg5, R9  = arg6
 */

namespace {

/*
 * Sprawdzenie, czy usluga zwrocila poprawna wartosc.
 *
 * Kontrakt jest asymetryczny celowo: sukces to 0..INT64_MAX, blad to
 * -1..-4095. Wartosci mniejsze niz -4095 sa bledem programisty (np.
 * niezanegowane ENOSYS jako 38 zostaloby uznane za sukces, a -100000 za
 * blad spoza zakresu), wiec je wykrywamy zamiast propagowac.
 */
bool wynik_poprawny(int64_t wartosc) {
    return wartosc >= 0 || (wartosc <= -1 && wartosc >= -4095);
}

int64_t normalizuj(int64_t wartosc) {
    if (wynik_poprawny(wartosc)) return wartosc;
    /* Bledna wartosc z uslugi traktujemy jak brak implementacji. */
    return posix_blad(POSIX_ENOSYS);
}

void zapisz_diagnostyke(PosixDiagnostyka* d, uint64_t numer, int64_t wynik) {
    if (!d) return;
    ++d->licznik_wywolan;
    const bool enosys = (wynik == posix_blad(POSIX_ENOSYS));
    if (enosys) ++d->licznik_enosys;
    else if (wynik < 0) ++d->licznik_blad;
    d->ostatnie.numer = numer;
    d->ostatnie.wynik = wynik;
    ++d->ostatnie.licznik;

    /*
     * Telemetria per numer: dokladnie to, czego brakuje przy porcie libc.
     * "Ktore wywolanie zwraca -ENOSYS i ile razy" zastepuje zgadywanie
     * listy brakujacych funkcji. Numer spoza zakresu jest pomijany, a nie
     * liczony pod indeksem 0 (0 to read - mylenie ich falszowaloby raport).
     */
    if (numer < POSIX_TELEMETRIA_MAKS) {
        ++d->liczba_wywolan_per_numer[numer];
        if (enosys) ++d->liczba_enosys_per_numer[numer];
    }
}

/*
 * Deskryptory i kody sa 32-bitowe. Rzutowanie przez uint32_t sprawia, ze
 * wartosc -1 przeslana jako 0xFFFFFFFF w argumencie daje -1, a nie
 * 4294967295.
 */
inline int32_t jako_i32(uint64_t v) {
    return static_cast<int32_t>(static_cast<uint32_t>(v & 0xFFFFFFFFULL));
}

}  // namespace

const char* posix_nazwa_wywolania(uint64_t numer) {
    switch (numer) {
        case POSIX_SYS_read: return "read";
        case POSIX_SYS_write: return "write";
        case POSIX_SYS_open: return "open";
        case POSIX_SYS_close: return "close";
        case POSIX_SYS_pipe: return "pipe";
        case POSIX_SYS_dup: return "dup";
        case POSIX_SYS_dup2: return "dup2";
        case POSIX_SYS_clone: return "clone";
        case POSIX_SYS_fork: return "fork";
        case POSIX_SYS_vfork: return "vfork";
        case POSIX_SYS_execve: return "execve";
        case POSIX_SYS_wait4: return "wait4";
        case POSIX_SYS_kill: return "kill";
        case POSIX_SYS_fstat: return "fstat";
        case POSIX_SYS_lseek: return "lseek";
        case POSIX_SYS_mmap: return "mmap";
        case POSIX_SYS_mprotect: return "mprotect";
        case POSIX_SYS_munmap: return "munmap";
        case POSIX_SYS_brk: return "brk";
        case POSIX_SYS_rt_sigaction: return "rt_sigaction";
        case POSIX_SYS_rt_sigprocmask: return "rt_sigprocmask";
        case POSIX_SYS_ioctl: return "ioctl";
        case POSIX_SYS_writev: return "writev";
        case POSIX_SYS_getpid: return "getpid";
        case POSIX_SYS_exit: return "exit";
        case POSIX_SYS_uname: return "uname";
        case POSIX_SYS_fcntl: return "fcntl";
        case POSIX_SYS_gettimeofday: return "gettimeofday";
        case POSIX_SYS_getuid: return "getuid";
        case POSIX_SYS_getgid: return "getgid";
        case POSIX_SYS_arch_prctl: return "arch_prctl";
        case POSIX_SYS_gettid: return "gettid";
        case POSIX_SYS_futex: return "futex";
        case POSIX_SYS_clock_gettime: return "clock_gettime";
        case POSIX_SYS_exit_group: return "exit_group";
        case POSIX_SYS_membarrier: return "membarrier";
        case POSIX_SYS_getrandom: return "getrandom";
        case POSIX_SYS_rseq: return "rseq";
        case POSIX_SYS_set_tid_address: return "set_tid_address";
        case POSIX_SYS_set_robust_list: return "set_robust_list";
        case POSIX_SYS_prlimit64: return "prlimit64";
        case POSIX_SYS_sched_getaffinity: return "sched_getaffinity";
        case POSIX_SYS_getrlimit: return "getrlimit";
        case POSIX_SYS_prctl: return "prctl";
        case POSIX_SYS_sysinfo: return "sysinfo";
        case POSIX_SYS_madvise: return "madvise";
        case POSIX_SYS_poll: return "poll";
        case POSIX_SYS_openat: return "openat";
        case POSIX_SYS_newfstatat: return "newfstatat";
        case POSIX_SYS_readlink: return "readlink";
        case POSIX_SYS_access: return "access";
        case POSIX_SYS_clock_getres: return "clock_getres";
        case POSIX_SYS_getdents64: return "getdents64";
        case POSIX_SYS_dup3: return "dup3";
        case POSIX_SYS_pipe2: return "pipe2";
        case POSIX_SYS_socket: return "socket";
        case POSIX_SYS_connect: return "connect";
        case POSIX_SYS_accept: return "accept";
        case POSIX_SYS_sendto: return "sendto";
        case POSIX_SYS_recvfrom: return "recvfrom";
        case POSIX_SYS_shutdown: return "shutdown";
        case POSIX_SYS_bind: return "bind";
        case POSIX_SYS_listen: return "listen";
        case POSIX_SYS_getsockname: return "getsockname";
        case POSIX_SYS_getpeername: return "getpeername";
        case POSIX_SYS_socketpair: return "socketpair";
        case POSIX_SYS_setsockopt: return "setsockopt";
        case POSIX_SYS_getsockopt: return "getsockopt";
        default: return "nieznane";
    }
}

int64_t posix_obsluz(const PosixUslugi& uslugi,
                     const PosixRama& rama,
                     PosixDiagnostyka* diagnostyka) {
    const uint64_t numer = rama.numer;

    /*
     * Bramka jest dostepna z Ring 3, ale moze zostac wywolana takze z Ring 0
     * (bledny skok w kodzie jadra). W obu przypadkach obslugujemy ja
     * identycznie; ochrona przed wywolaniem z Ring 0 jest w bramce
     * asemblerowej, ktora sprawdza CS ramy.
     */
    int64_t wynik;

    switch (numer) {
        case POSIX_SYS_write:
            wynik = uslugi.write
                        ? uslugi.write(uslugi.kontekst, jako_i32(rama.a1),
                                       reinterpret_cast<const void*>(rama.a2),
                                       rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_read:
            wynik = uslugi.read
                        ? uslugi.read(uslugi.kontekst, jako_i32(rama.a1),
                                      rama.a2, rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_open:
            /*
             * open(2) w ABI Linux x86-64: sciezka w RDI, flagi w RSI,
             * tryb w RDX. Trzeci argument jest uzywany tylko z O_CREAT,
             * ale przekazujemy go zawsze - obcinanie go do zera przed
             * decyzja o utworzeniu pliku byloby zgadywaniem zamiaru.
             */
            wynik = uslugi.open
                        ? uslugi.open(uslugi.kontekst, rama.a1, rama.a2, rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_openat:
            wynik = uslugi.openat
                        ? uslugi.openat(uslugi.kontekst, jako_i32(rama.a1),
                                        rama.a2, rama.a3, rama.a4)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_pipe:
            wynik = uslugi.pipe
                        ? uslugi.pipe(uslugi.kontekst, rama.a1)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_dup:
            wynik = uslugi.dup
                        ? uslugi.dup(uslugi.kontekst, jako_i32(rama.a1))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_dup2:
            wynik = uslugi.dup2
                        ? uslugi.dup2(uslugi.kontekst, jako_i32(rama.a1),
                                      jako_i32(rama.a2))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_fcntl:
            wynik = uslugi.fcntl
                        ? uslugi.fcntl(uslugi.kontekst, jako_i32(rama.a1),
                                       jako_i32(rama.a2), rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_clone:
            wynik = uslugi.clone
                        ? uslugi.clone(uslugi.kontekst, rama.a1, rama.a2,
                                       rama.a3, rama.a4, rama.a5)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_fork:
        case POSIX_SYS_vfork:
            wynik = uslugi.fork
                        ? uslugi.fork(uslugi.kontekst)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_execve:
            wynik = uslugi.execve
                        ? uslugi.execve(uslugi.kontekst, rama.a1, rama.a2,
                                        rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_wait4:
            wynik = uslugi.wait4
                        ? uslugi.wait4(uslugi.kontekst, jako_i32(rama.a1),
                                       rama.a2, rama.a3, rama.a4)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_kill:
            wynik = uslugi.kill
                        ? uslugi.kill(uslugi.kontekst, jako_i32(rama.a1),
                                      jako_i32(rama.a2))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_rt_sigaction:
            wynik = uslugi.rt_sigaction
                        ? uslugi.rt_sigaction(uslugi.kontekst,
                                              jako_i32(rama.a1), rama.a2,
                                              rama.a3, rama.a4)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_rt_sigprocmask:
            wynik = uslugi.rt_sigprocmask
                        ? uslugi.rt_sigprocmask(uslugi.kontekst,
                                                jako_i32(rama.a1), rama.a2,
                                                rama.a3, rama.a4)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_close:
            wynik = uslugi.close_fd
                        ? uslugi.close_fd(uslugi.kontekst, jako_i32(rama.a1))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_lseek:
            wynik = uslugi.lseek_fd
                        ? uslugi.lseek_fd(uslugi.kontekst, jako_i32(rama.a1),
                                          static_cast<int64_t>(rama.a2),
                                          jako_i32(rama.a3))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_fstat:
            wynik = uslugi.fstat_fd
                        ? uslugi.fstat_fd(uslugi.kontekst, jako_i32(rama.a1), rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_newfstatat:
            wynik = uslugi.newfstatat_sciezka
                        ? uslugi.newfstatat_sciezka(
                              uslugi.kontekst, jako_i32(rama.a1), rama.a2,
                              rama.a3, static_cast<uint32_t>(rama.a4))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_readlink:
            wynik = uslugi.readlink_sciezka
                        ? uslugi.readlink_sciezka(
                              uslugi.kontekst, rama.a1, rama.a2, rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_getdents64:
            wynik = uslugi.getdents64_fd
                        ? uslugi.getdents64_fd(
                              uslugi.kontekst, jako_i32(rama.a1),
                              rama.a2, rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_poll:
            wynik = uslugi.poll_fds
                        ? uslugi.poll_fds(
                              uslugi.kontekst, rama.a1, rama.a2,
                              jako_i32(rama.a3))
                        : posix_blad(POSIX_ENOSYS);
            break;

        /*
         * Wywolania startowe libc.
         *
         * kazde ma tu wlasna galeź, a nie wpada w default -ENOSYS, bo
         * musl rozroznia "nie ma wywolania" od "wywolanie dziala, ale
         * zasob jest nielimitowany". Roznica ma znaczenie: -ENOSYS
         * przerywa start, a poprawna odpowiedz pozwala isc dalej.
         */
        case POSIX_SYS_set_tid_address:
            wynik = uslugi.set_tid_address
                        ? uslugi.set_tid_address(uslugi.kontekst, rama.a1)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_set_robust_list:
            wynik = uslugi.set_robust_list
                        ? uslugi.set_robust_list(uslugi.kontekst, rama.a1, rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_prlimit64:
            wynik = uslugi.prlimit64
                        ? uslugi.prlimit64(uslugi.kontekst, jako_i32(rama.a1),
                                           jako_i32(rama.a2), rama.a3, rama.a4)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_sched_getaffinity:
            wynik = uslugi.sched_getaffinity
                        ? uslugi.sched_getaffinity(uslugi.kontekst, jako_i32(rama.a1),
                                                   rama.a2, rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_getrlimit:
            wynik = uslugi.getrlimit_zasob
                        ? uslugi.getrlimit_zasob(uslugi.kontekst, jako_i32(rama.a1),
                                                 rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_sysinfo:
            wynik = uslugi.sysinfo_ogolny
                        ? uslugi.sysinfo_ogolny(uslugi.kontekst, rama.a1)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_access:
            wynik = uslugi.access_sciezka
                        ? uslugi.access_sciezka(uslugi.kontekst, rama.a1,
                                                jako_i32(rama.a2))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_exit:
            if (uslugi.exit_proces) uslugi.exit_proces(uslugi.kontekst, jako_i32(rama.a1));
            /* Nieosiagalne przy poprawnej usludze; bezpieczny fallback. */
            wynik = 0;
            break;

        case POSIX_SYS_getpid:
            wynik = uslugi.getpid ? uslugi.getpid(uslugi.kontekst)
                                  : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_gettid:
            wynik = uslugi.gettid ? uslugi.gettid(uslugi.kontekst)
                                  : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_uname:
            wynik = uslugi.uname ? uslugi.uname(uslugi.kontekst, rama.a1)
                                 : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_clock_gettime:
            wynik = uslugi.clock_gettime
                        ? uslugi.clock_gettime(uslugi.kontekst, jako_i32(rama.a1), rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_gettimeofday:
            wynik = uslugi.gettimeofday
                        ? uslugi.gettimeofday(uslugi.kontekst, rama.a1, rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_arch_prctl:
            wynik = uslugi.arch_prctl
                        ? uslugi.arch_prctl(uslugi.kontekst, jako_i32(rama.a1), rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_getrandom:
            wynik = uslugi.getrandom
                        ? uslugi.getrandom(uslugi.kontekst, rama.a1, rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_brk:
            wynik = uslugi.brk ? uslugi.brk(uslugi.kontekst, rama.a1)
                               : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_mmap:
            wynik = uslugi.mmap
                        ? uslugi.mmap(uslugi.kontekst, rama.a1, rama.a2, rama.a3,
                                      rama.a4, static_cast<int64_t>(rama.a5), rama.a6)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_munmap:
            wynik = uslugi.munmap
                        ? uslugi.munmap(uslugi.kontekst, rama.a1, rama.a2)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_mprotect:
            wynik = uslugi.mprotect
                        ? uslugi.mprotect(uslugi.kontekst, rama.a1, rama.a2, rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_socket:
            wynik = uslugi.socket
                        ? uslugi.socket(uslugi.kontekst, jako_i32(rama.a1),
                                        jako_i32(rama.a2), jako_i32(rama.a3))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_connect:
            wynik = uslugi.connect
                        ? uslugi.connect(uslugi.kontekst, jako_i32(rama.a1),
                                         rama.a2, static_cast<uint32_t>(rama.a3))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_sendto:
            wynik = uslugi.sendto
                        ? uslugi.sendto(uslugi.kontekst, jako_i32(rama.a1),
                                        rama.a2, rama.a3,
                                        static_cast<uint32_t>(rama.a4),
                                        rama.a5, static_cast<uint32_t>(rama.a6))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_recvfrom:
            wynik = uslugi.recvfrom
                        ? uslugi.recvfrom(uslugi.kontekst, jako_i32(rama.a1),
                                          rama.a2, rama.a3,
                                          static_cast<uint32_t>(rama.a4),
                                          rama.a5, rama.a6)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_bind:
            wynik = uslugi.bind
                        ? uslugi.bind(uslugi.kontekst, jako_i32(rama.a1),
                                      rama.a2, static_cast<uint32_t>(rama.a3))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_listen:
            wynik = uslugi.listen
                        ? uslugi.listen(uslugi.kontekst, jako_i32(rama.a1),
                                        jako_i32(rama.a2))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_accept:
            wynik = uslugi.accept
                        ? uslugi.accept(uslugi.kontekst, jako_i32(rama.a1),
                                        rama.a2, rama.a3)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_shutdown:
            wynik = uslugi.shutdown
                        ? uslugi.shutdown(uslugi.kontekst, jako_i32(rama.a1),
                                          jako_i32(rama.a2))
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_getsockopt:
            wynik = uslugi.getsockopt
                        ? uslugi.getsockopt(uslugi.kontekst, jako_i32(rama.a1),
                                            jako_i32(rama.a2), jako_i32(rama.a3),
                                            rama.a4, rama.a5)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_setsockopt:
            wynik = uslugi.setsockopt
                        ? uslugi.setsockopt(uslugi.kontekst, jako_i32(rama.a1),
                                            jako_i32(rama.a2), jako_i32(rama.a3),
                                            rama.a4, rama.a5)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_futex:
            wynik = uslugi.futex
                        ? uslugi.futex(uslugi.kontekst, rama.a1, rama.a2, rama.a3,
                                       rama.a4, rama.a5, rama.a6)
                        : posix_blad(POSIX_ENOSYS);
            break;

        case POSIX_SYS_exit_group:
            /*
             * exit_group konczy caly proces (wszystkie watki). W obecnym
             * modelu proces = jeden kontekst, wiec semantyka jest ta sama
             * co exit.
             */
            if (uslugi.exit_proces) uslugi.exit_proces(uslugi.kontekst, jako_i32(rama.a1));
            wynik = 0;
            break;

        default:
            /*
             * Nieznany numer: -ENOSYS. Nigdy 0, bo 0 wyglada jak sukces
             * i psuje logike wywolujacego (np. read() zwracajacy 0 to EOF).
             */
            wynik = posix_blad(POSIX_ENOSYS);
            break;
    }

    wynik = normalizuj(wynik);
    zapisz_diagnostyke(diagnostyka, numer, wynik);
    return wynik;
}
