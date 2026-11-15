#pragma once
#include <stdint.h>
#include <stddef.h>

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
    POSIX_ENOTSOCK = 88,
    POSIX_ENOPROTOOPT = 92,
    POSIX_EPROTONOSUPPORT = 93,
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
    POSIX_ESPIPE  = 29,
    POSIX_EPIPE   = 32,
    POSIX_ERANGE  = 34,
    POSIX_ENOSYS  = 38,
    POSIX_ENOTEMPTY = 39,
    POSIX_ELOOP   = 40,
    POSIX_EOVERFLOW = 75,
    POSIX_ENOTSUP = 95,
    POSIX_EAFNOSUPPORT = 97,
    POSIX_EADDRINUSE = 98,
    POSIX_ENOBUFS = 105,
    POSIX_EISCONN = 106,
    POSIX_ENOTCONN = 107,
    POSIX_ESHUTDOWN = 108,
    POSIX_ETIMEDOUT = 110,
    POSIX_ECONNREFUSED = 111,
    POSIX_EALREADY = 114,
    POSIX_ENETUNREACH = 101
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
    POSIX_SYS_pipe          = 22,
    POSIX_SYS_dup           = 32,
    POSIX_SYS_dup2          = 33,
    POSIX_SYS_clone         = 56,
    POSIX_SYS_fork          = 57,
    POSIX_SYS_vfork         = 58,
    POSIX_SYS_execve        = 59,
    POSIX_SYS_wait4         = 61,
    POSIX_SYS_kill          = 62,
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
    POSIX_SYS_dup3          = 292,
    POSIX_SYS_pipe2         = 293,
    POSIX_SYS_clock_gettime = 228,
    POSIX_SYS_exit_group    = 231,
    POSIX_SYS_membarrier    = 324,
    POSIX_SYS_getrandom     = 318,
    POSIX_SYS_rseq          = 334,

    /*
     * Wywolania, ktorych crt1 musl/glibc domaga sie PRZED main().
     *
     * Bez nich program konczy sie kodem bledu jeszcze w starcie: musl
     * wywoluje set_tid_address i set_robust_list bezwarunkowo, a jego
     * alokator sprawdza prlimit64(RLIMIT_AS). Numery sa linuxowe.
     */
    POSIX_SYS_set_tid_address = 218,
    POSIX_SYS_set_robust_list = 273,
    POSIX_SYS_prlimit64       = 302,
    POSIX_SYS_sched_getaffinity = 204,
    POSIX_SYS_getrlimit       = 97,
    POSIX_SYS_prctl           = 157,
    POSIX_SYS_sysinfo         = 99,
    POSIX_SYS_madvise         = 28,
    POSIX_SYS_poll            = 7,
    POSIX_SYS_openat          = 257,
    POSIX_SYS_newfstatat      = 262,
    POSIX_SYS_readlink        = 89,
    POSIX_SYS_access          = 21,
    POSIX_SYS_clock_getres    = 229,
    POSIX_SYS_getdents64      = 217,
    POSIX_SYS_socket          = 41,
    POSIX_SYS_connect         = 42,
    POSIX_SYS_accept          = 43,
    POSIX_SYS_sendto          = 44,
    POSIX_SYS_recvfrom        = 45,
    POSIX_SYS_shutdown        = 48,
    POSIX_SYS_bind            = 49,
    POSIX_SYS_listen          = 50,
    POSIX_SYS_getsockname     = 51,
    POSIX_SYS_getpeername     = 52,
    POSIX_SYS_socketpair      = 53,
    POSIX_SYS_setsockopt      = 54,
    POSIX_SYS_getsockopt      = 55
};

/* =========================================================================
 * ABI GNIAZD IPv4 (Linux x86-64)
 * ========================================================================= */

constexpr int32_t POSIX_AF_INET = 2;
constexpr int32_t POSIX_SOCK_STREAM = 1;
constexpr int32_t POSIX_IPPROTO_TCP = 6;
constexpr int32_t POSIX_SOL_SOCKET = 1;
constexpr int32_t POSIX_SO_TYPE = 3;
constexpr int32_t POSIX_SO_ERROR = 4;
constexpr int32_t POSIX_SHUT_RD = 0;
constexpr int32_t POSIX_SHUT_WR = 1;
constexpr int32_t POSIX_SHUT_RDWR = 2;
constexpr uint64_t POSIX_MSG_NOSIGNAL = 0x4000ULL;

/*
 * sockaddr_in jest kopiowany pole po polu. Adres IP pozostaje tablica bajtow
 * w kolejnosci sieciowej, zeby kernel nie musial dereferencjonowac struktury
 * Ring 3 ani polegac na hostowym ulozeniu uint32_t.
 */
struct PosixSockaddrIn {
    uint16_t sin_family;
    uint16_t sin_port;
    uint8_t sin_addr[4];
    uint8_t sin_zero[8];
};
static_assert(sizeof(PosixSockaddrIn) == 16, "ABI sockaddr_in x86-64");

struct PosixPollfd {
    int32_t fd;
    int16_t events;
    int16_t revents;
};
static_assert(sizeof(PosixPollfd) == 8, "ABI pollfd x86-64");

/*
 * Podzbior operacji futex, ktory rozumie obecna implementacja.
 *
 * FUTEX_WAIT nie moze jeszcze uspic procesu: proces = pojedynczy kontekst,
 * a wejscie przez int 0x80 nie ma bezpiecznej sciezki do przelaczenia ramy
 * syscallu. Implementacja jadra wykonuje tylko ograniczona obserwacje
 * wartosci i zwraca -ENOSYS, jezeli nie nastapi zmiana. FUTEX_WAKE zwraca
 * liczbe rzeczywiscie obudzonych oczekujacych; przy obecnym braku kolejki
 * oczekujacych jest to poprawne 0.
 */
enum PosixFutexOperacja : uint32_t {
    POSIX_FUTEX_WAIT = 0,
    POSIX_FUTEX_WAKE = 1,
    POSIX_FUTEX_PRIVATE_FLAG = 128,
    POSIX_FUTEX_CLOCK_REALTIME = 256,
    POSIX_FUTEX_CMD_MASK = 127
};

constexpr uint32_t posix_futex_polecenie(uint64_t operacja) {
    return static_cast<uint32_t>(operacja) & POSIX_FUTEX_CMD_MASK;
}

constexpr bool posix_futex_ma_nieznane_flagi(uint64_t operacja) {
    return operacja > UINT32_MAX ||
           (static_cast<uint32_t>(operacja) &
            ~(POSIX_FUTEX_CMD_MASK | POSIX_FUTEX_PRIVATE_FLAG)) != 0;
}

constexpr int64_t posix_futex_sprawdz_wartosc(uint32_t obserwowana,
                                              uint32_t oczekiwana) {
    return obserwowana == oczekiwana ? 0 : posix_blad(POSIX_EAGAIN);
}

constexpr int64_t posix_futex_wynik_po_spinie(bool wartosc_zmieniona) {
    return wartosc_zmieniona ? 0 : posix_blad(POSIX_ENOSYS);
}

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
 * STRUKTURY STARTOWE LIBC
 * ========================================================================= */

/* struct rlimit z x86-64: dwa 64-bitowe pola. */
struct PosixRlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};
static_assert(sizeof(PosixRlimit) == 16, "ABI rlimit x86-64");

/*
 * struct sysinfo z x86-64.
 *
 * Uklad jest dokladnie linuxowy: ostatnie pole `_f` ma rozmiar
 * 20 - 2*sizeof(long) - sizeof(int) = 0 w x86-64, wiec struktura konczy sie
 * na mem_unit i jest wyrownana do 8 bajtow (112 B).
 */
struct PosixSysinfo {
    int64_t  uptime;        /*   0 */
    uint64_t loads[3];      /*   8, 16, 24 */
    uint64_t totalram;      /*  32 */
    uint64_t freeram;       /*  40 */
    uint64_t sharedram;     /*  48 */
    uint64_t bufferram;     /*  56 */
    uint64_t totalswap;     /*  64 */
    uint64_t freeswap;      /*  72 */
    uint16_t procs;         /*  80 */
    uint16_t pad;           /*  82 */
    uint32_t _pad_align;    /*  84: wyrownanie do 8 przed totalhigh */
    uint64_t totalhigh;     /*  88 */
    uint64_t freehigh;      /*  96 */
    uint32_t mem_unit;      /* 104 */
    uint32_t _pad_end;      /* 108: wyrownanie calej struktury do 112 */
};
static_assert(sizeof(PosixSysinfo) == 112, "ABI sysinfo x86-64");
static_assert(offsetof(PosixSysinfo, totalhigh) == 88, "sysinfo: totalhigh@88");
static_assert(offsetof(PosixSysinfo, mem_unit) == 104, "sysinfo: mem_unit@104");

/*
 * Numery zasobow (RLIMIT_*) i stale polityki.
 *
 * Wartosc RLIM_INFINITY jest czescia ABI: musl porownuje z nia wprost.
 */
enum PosixZasob : int32_t {
    POSIX_RLIMIT_CPU        = 0,
    POSIX_RLIMIT_FSIZE      = 1,
    POSIX_RLIMIT_DATA       = 2,
    POSIX_RLIMIT_STACK      = 3,
    POSIX_RLIMIT_CORE       = 4,
    POSIX_RLIMIT_RSS        = 5,
    POSIX_RLIMIT_NPROC      = 6,
    POSIX_RLIMIT_NOFILE     = 7,
    POSIX_RLIMIT_MEMLOCK    = 8,
    POSIX_RLIMIT_AS         = 9
};

constexpr uint64_t POSIX_RLIM_INFINITY = ~0ULL;

/* Tryby access(2). */
enum PosixAccess : int32_t {
    POSIX_R_OK = 4,
    POSIX_W_OK = 2,
    POSIX_X_OK = 1,
    POSIX_F_OK = 0
};

/* Ograniczony podzbior openat/newfstatat obslugiwany przez VFS. */
constexpr int32_t POSIX_AT_FDCWD = -100;
constexpr uint32_t POSIX_AT_SYMLINK_NOFOLLOW = 0x100U;
constexpr uint32_t POSIX_AT_EMPTY_PATH = 0x1000U;

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
