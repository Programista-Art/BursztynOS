#pragma once
#include <stdint.h>

/*
 * Bursztyn OS - brama ABI SysV/Linux (`int 0x80`).
 *
 * Dlaczego druga bramka, a nie zmiana BWS:
 *
 *   BWS uzywa wlasnej konwencji (numer w R8, argumenty R9/R10/R12/R13) i jest
 *   zamrozone - 23 aplikacje .bur i cala warstwa GUI zaleza od tego ukladu.
 *   Kazdy kompilator C/C++ (gcc, clang, musl, Chromium) generuje jednak
 *   wylacznie ABI SysV: argumenty w RDI/RSI/RDX/R10/R8/R9. Bez drugiej bramki
 *   nie da sie uruchomic zadnego obcego binarium bez patrzenia kompilatora.
 *
 * Konwencja tej bramki (zgodna z Linux x86-64 i z ABI SysV):
 *
 *   RAX = numer wywolania (tak jak Linux; dzieki temu zostaje 6 rejestrow
 *         argumentow, czego wymaga mmap z 6 argumentami)
 *   RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5, R9 = arg6
 *   RAX = wynik; blad to -errno w zakresie -1..-4095
 *
 * Roznica wobec BWS, ktora upraszcza sprawe: `int 0x80` nie niszczy
 * RCX/R11 (to robi tylko SYSCALL), wiec caly kontekst uzytkownika jest
 * odtwarzalny bez dodatkowych sztuczek. Druga roznica: bramka jest
 * przerwaniem, wiec wchodzi przez istniejaca rame RejestryStanowe i ten sam
 * dispatcher, ktory obsluguje wyjatki CPU.
 *
 * Wektor i uklad rejestrow sa celowo identyczne z Linux x86-64, wiec
 * pozniejsza migracja na bramke `syscall` (IA32_LSTAR) nie wymaga zmiany
 * dyspozytora - zmienia sie tylko wejscie asemblerowe.
 *
 * Numer 0x80 jest wektorem programowym: nie ma go w rejestrze ISR LAPIC,
 * wiec nie wymaga EOI. Musi byc wylaczony z puli wektorow MSI.
 */

constexpr uint8_t POSIX_WEKTOR = 0x80U;

/* P=1, DPL=3, typ=0xE - bramka przerwania dostepna z Ring 3. */
constexpr uint8_t POSIX_FLAGA_BRAMKI = 0xEEU;

/* =========================================================================
 * ERRNOS
 * ========================================================================= */

/*
 * Wartosci zgodne z Linux x86-64. Nie przenumerowujemy ich: kazdy naglowek
 * <errno.h> w obcym libc zaklada te konkretne liczby.
 */
enum PosixErrno : int64_t {
    POSIX_EPERM   = 1,
    POSIX_ENOENT  = 2,
    POSIX_ESRCH   = 3,
    POSIX_EINTR   = 4,
    POSIX_EIO     = 5,
    POSIX_ENXIO   = 6,
    POSIX_E2BIG   = 7,
    POSIX_ENOEXEC = 8,
    POSIX_EBADF   = 9,
    POSIX_ECHILD  = 10,
    POSIX_EAGAIN  = 11,
    POSIX_ENOMEM  = 12,
    POSIX_EACCES  = 13,
    POSIX_EFAULT  = 14,
    POSIX_EBUSY   = 16,
    POSIX_EEXIST  = 17,
    POSIX_EXDEV   = 18,
    POSIX_ENODEV  = 19,
    POSIX_ENOTDIR = 20,
    POSIX_EISDIR  = 21,
    POSIX_EINVAL  = 22,
    POSIX_ENFILE  = 23,
    POSIX_EMFILE  = 24,
    POSIX_ENOSPC  = 28,
    POSIX_EPIPE   = 32,
    POSIX_ERANGE  = 34,
    POSIX_ENOSYS  = 38,
    POSIX_ENOTEMPTY = 39,
    POSIX_ELOOP   = 40,
    POSIX_EOVERFLOW = 75,
    POSIX_ENOTSUP = 95
};

/* Konwencja jadra Linuksa: kazdy blad to -errno z przedzialu -1..-4095. */
constexpr bool posix_czy_blad(int64_t wynik) {
    return wynik <= -1 && wynik >= -4095;
}

constexpr int64_t posix_blad(int64_t errno_kod) {
    return -errno_kod;
}

/* =========================================================================
 * NUMERY WYWOLAN
 * ========================================================================= */

/*
 * Podzbior, ktory ma realna implementacje dzisiaj. Wszystko inne zwraca
 * -ENOSYS - swiadomie, zeby libc mogl zrobic fallback zamiast dostac
 * przypadkowe 0 (0 wyglada jak sukces i psuje logike wywolujacego).
 *
 * Numery sa linuxowe, bo musl/crt1 zakladaja wlasnie te.
 */
enum PosixNumer : uint64_t {
    POSIX_SYS_read          = 0,
    POSIX_SYS_write         = 1,
    POSIX_SYS_open          = 2,
    POSIX_SYS_close         = 3,
    POSIX_SYS_fstat         = 5,
    POSIX_SYS_lseek         = 8,
    POSIX_SYS_mmap          = 9,
    POSIX_SYS_mprotect      = 10,
    POSIX_SYS_munmap        = 11,
    POSIX_SYS_brk           = 12,
    POSIX_SYS_rt_sigaction  = 13,
    POSIX_SYS_rt_sigprocmask= 14,
    POSIX_SYS_ioctl         = 16,
    POSIX_SYS_writev        = 20,
    POSIX_SYS_getpid        = 39,
    POSIX_SYS_exit          = 60,
    POSIX_SYS_uname         = 63,
    POSIX_SYS_fcntl         = 72,
    POSIX_SYS_gettimeofday  = 96,
    POSIX_SYS_getuid        = 102,
    POSIX_SYS_getgid        = 104,
    POSIX_SYS_arch_prctl    = 158,
    POSIX_SYS_gettid        = 186,
    POSIX_SYS_futex         = 202,
    POSIX_SYS_clock_gettime = 228,
    POSIX_SYS_exit_group    = 231,
    POSIX_SYS_membarrier    = 324,
    POSIX_SYS_getrandom     = 318,
    POSIX_SYS_rseq          = 334
};

/* =========================================================================
 * STRUKTURY ABI PRZEKAZYWANE DO RING 3
 * ========================================================================= */

/* struct timespec z x86-64 (time_t i long to 64 bity). */
struct PosixTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct PosixTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

enum PosixClock : int32_t {
    POSIX_CLOCK_REALTIME  = 0,
    POSIX_CLOCK_MONOTONIC = 1
};

enum PosixArchPrctl : int32_t {
    POSIX_ARCH_SET_GS = 0x1001,
    POSIX_ARCH_SET_FS = 0x1002,
    POSIX_ARCH_GET_FS = 0x1003,
    POSIX_ARCH_GET_GS = 0x1004
};

/* Wersja jadra raportowana przez uname(2). Pola sa stale, bez NUL. */
struct PosixUtsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};
static_assert(sizeof(PosixUtsname) == 390, "ABI uname x86-64");

/* =========================================================================
 * RAMA WYWOLANIA
 * ========================================================================= */

/*
 * Bramka SysV jest wywolywana z ramy ISR (RejestryStanowe), wiec argumenty
 * sa odczytywane z zapisanych rejestrow, a wynik trafia do zapisanego RAX
 * przed IRETQ. Ten struct jest wspolny dla host-testu i jadra.
 *
 * Uklad jest dokladnie linuxowy: numer w RAX, szesc argumentow w kolejnosci
 * SysV (RDI, RSI, RDX, RCX jest omijany przez ABI, R8, R9 - u nas R10 jest
 * czwartym argumentem, bo tak dziala ABI SysV dla wywolan systemowych).
 */
struct PosixRama {
    uint64_t numer;  /* RAX */
    uint64_t a1;     /* RDI */
    uint64_t a2;     /* RSI */
    uint64_t a3;     /* RDX */
    uint64_t a4;     /* R10 */
    uint64_t a5;     /* R8 */
    uint64_t a6;     /* R9 */
    uint64_t rip;    /* adres powrotu Ring 3 (diagnostyka) */
    uint64_t rsp;    /* stos Ring 3 (diagnostyka) */
    uint64_t cs;
};

static_assert(sizeof(PosixRama) == 80, "ABI ramy bramki SysV");
