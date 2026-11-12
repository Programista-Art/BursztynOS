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
    if (wynik == posix_blad(POSIX_ENOSYS)) ++d->licznik_enosys;
    else if (wynik < 0) ++d->licznik_blad;
    d->ostatnie.numer = numer;
    d->ostatnie.wynik = wynik;
    ++d->ostatnie.licznik;
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
