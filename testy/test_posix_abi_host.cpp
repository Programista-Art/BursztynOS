/*
 * Host gate bramki SysV (punkt 2.12 planu).
 *
 * Sprawdza trzy rzeczy, ktorych nie da sie sprawdzic w QEMU tanio:
 *
 *   1. mapowanie rejestrow: numer z RAX, szesc argumentow z
 *      RDI/RSI/RDX/R10/R8/R9 trafia do uslugi w tej samej kolejnosci,
 *   2. konwencja errno: blad to -errno z zakresu -1..-4095, nieznany numer
 *      to -ENOSYS (nigdy 0), brak uslugi tez -ENOSYS, a bledna wartosc
 *      z uslugi jest normalizowana zamiast propagowana,
 *   3. ze bramka nie gubi/nie podmienia argumentow przy wartosciach
 *      skrajnych (UINT64_MAX, -1 jako int32, duze offsety).
 *
 * Test jest host-only: nie dotyka QEMU ani jadra, wiec jest szybki i
 * uruchamiany w kazdym gate'cie.
 */
#include "jadro/posix/posix_dyspozytor.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool warunek, const char* opis) {
    ++g_checks;
    if (!warunek) {
        std::printf("FAIL: %s\n", opis);
        ++g_failures;
    }
}

/* ---- Rejestracja argumentow ------------------------------------------ */

struct Zapisane {
    /* write */
    int fd;
    const void* dane;
    uint64_t dlugosc;
    /* exit */
    int kod_wyjscia;
    int wywolan_exit;
    /* mmap - 6 argumentow */
    uint64_t mmap_adres, mmap_dlugosc, mmap_ochrona, mmap_flagi;
    int64_t mmap_fd;
    uint64_t mmap_offset;
    /* futex */
    uint64_t futex_adres, futex_op, futex_wartosc, futex_timeout, futex_adres2, futex_wartosc3;
    /* arch_prctl */
    int arch_kod;
    uint64_t arch_adres;
    /* getrandom */
    uint64_t random_bufor, random_dlugosc;
    /* clock_gettime */
    int clock_id;
    uint64_t clock_bufor;
    /* uname / brk / munmap / mprotect */
    uint64_t uname_bufor, brk_wartosc, munmap_adres, munmap_dlugosc;
    uint64_t mprotect_adres, mprotect_dlugosc, mprotect_ochrona;
    uint64_t gettimeofday_tv, gettimeofday_tz;
};

Zapisane g_z{};

int64_t usl_write(void*, int fd, const void* dane, uint64_t dlugosc) {
    g_z.fd = fd;
    g_z.dane = dane;
    g_z.dlugosc = dlugosc;
    return static_cast<int64_t>(dlugosc);
}

void usl_exit(void*, int kod) {
    g_z.kod_wyjscia = kod;
    ++g_z.wywolan_exit;
}

int64_t usl_getpid(void*) { return 7; }
int64_t usl_gettid(void*) { return 9; }

int64_t usl_uname(void*, uint64_t bufor) {
    g_z.uname_bufor = bufor;
    return 0;
}

int64_t usl_clock(void*, int id, uint64_t bufor) {
    g_z.clock_id = id;
    g_z.clock_bufor = bufor;
    return 0;
}

int64_t usl_gettimeofday(void*, uint64_t tv, uint64_t tz) {
    g_z.gettimeofday_tv = tv;
    g_z.gettimeofday_tz = tz;
    return 0;
}

int64_t usl_arch_prctl(void*, int kod, uint64_t adres) {
    g_z.arch_kod = kod;
    g_z.arch_adres = adres;
    return 0;
}

int64_t usl_getrandom(void*, uint64_t bufor, uint64_t dlugosc) {
    g_z.random_bufor = bufor;
    g_z.random_dlugosc = dlugosc;
    return static_cast<int64_t>(dlugosc);
}

int64_t usl_brk(void*, uint64_t wartosc) {
    g_z.brk_wartosc = wartosc;
    return static_cast<int64_t>(wartosc);
}

int64_t usl_mmap(void*, uint64_t adres, uint64_t dlugosc, uint64_t ochrona,
                 uint64_t flagi, int64_t fd, uint64_t offset) {
    g_z.mmap_adres = adres;
    g_z.mmap_dlugosc = dlugosc;
    g_z.mmap_ochrona = ochrona;
    g_z.mmap_flagi = flagi;
    g_z.mmap_fd = fd;
    g_z.mmap_offset = offset;
    return 0x40000000;
}

int64_t usl_munmap(void*, uint64_t adres, uint64_t dlugosc) {
    g_z.munmap_adres = adres;
    g_z.munmap_dlugosc = dlugosc;
    return 0;
}

int64_t usl_mprotect(void*, uint64_t adres, uint64_t dlugosc, uint64_t ochrona) {
    g_z.mprotect_adres = adres;
    g_z.mprotect_dlugosc = dlugosc;
    g_z.mprotect_ochrona = ochrona;
    return 0;
}

int64_t usl_futex(void*, uint64_t adres, uint64_t op, uint64_t wartosc,
                  uint64_t timeout, uint64_t adres2, uint64_t wartosc3) {
    g_z.futex_adres = adres;
    g_z.futex_op = op;
    g_z.futex_wartosc = wartosc;
    g_z.futex_timeout = timeout;
    g_z.futex_adres2 = adres2;
    g_z.futex_wartosc3 = wartosc3;
    return 0;
}

PosixUslugi pelne_uslugi() {
    PosixUslugi u{};
    u.write = usl_write;
    u.exit_proces = usl_exit;
    u.getpid = usl_getpid;
    u.gettid = usl_gettid;
    u.uname = usl_uname;
    u.clock_gettime = usl_clock;
    u.gettimeofday = usl_gettimeofday;
    u.arch_prctl = usl_arch_prctl;
    u.getrandom = usl_getrandom;
    u.brk = usl_brk;
    u.mmap = usl_mmap;
    u.munmap = usl_munmap;
    u.mprotect = usl_mprotect;
    u.futex = usl_futex;
    u.kontekst = nullptr;
    return u;
}

PosixRama rama(uint64_t numer, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0,
               uint64_t a4 = 0, uint64_t a5 = 0, uint64_t a6 = 0) {
    PosixRama r{};
    r.numer = numer;
    r.a1 = a1; r.a2 = a2; r.a3 = a3; r.a4 = a4; r.a5 = a5; r.a6 = a6;
    r.rip = 0x00401000;
    r.rsp = 0x7FFF0000;
    r.cs = 0x23;
    return r;
}

/* ---- 1. Mapowanie rejestrow ------------------------------------------- */

void test_mapowanie() {
    const PosixUslugi u = pelne_uslugi();
    PosixDiagnostyka d{};
    static const char tekst[] = "abc";

    /* write(fd, buf, len) -> RDI/RSI/RDX */
    g_z = Zapisane{};
    int64_t w = posix_obsluz(u, rama(POSIX_SYS_write, 2, reinterpret_cast<uint64_t>(tekst), 3), &d);
    check(w == 3, "write zwraca dlugosc");
    check(g_z.fd == 2, "write: fd z RDI");
    check(g_z.dane == tekst, "write: bufor z RSI");
    check(g_z.dlugosc == 3, "write: dlugosc z RDX");

    /* getpid/gettid nie przyjmuja argumentow */
    check(posix_obsluz(u, rama(POSIX_SYS_getpid), &d) == 7, "getpid");
    check(posix_obsluz(u, rama(POSIX_SYS_gettid), &d) == 9, "gettid");

    /* uname(buf) -> RDI */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_uname, 0x1234), &d) == 0, "uname");
    check(g_z.uname_bufor == 0x1234, "uname: bufor z RDI");

    /* clock_gettime(clockid, ts) -> RDI/RSI */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_clock_gettime, 1, 0x5678), &d) == 0, "clock_gettime");
    check(g_z.clock_id == 1, "clock_gettime: clock_id z RDI");
    check(g_z.clock_bufor == 0x5678, "clock_gettime: ts z RSI");

    /* gettimeofday(tv, tz) -> RDI/RSI */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_gettimeofday, 0xAAA, 0xBBB), &d) == 0, "gettimeofday");
    check(g_z.gettimeofday_tv == 0xAAA, "gettimeofday: tv z RDI");
    check(g_z.gettimeofday_tz == 0xBBB, "gettimeofday: tz z RSI");

    /* arch_prctl(kod, adres) -> RDI/RSI */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_arch_prctl, POSIX_ARCH_SET_FS, 0xC000), &d) == 0, "arch_prctl");
    check(g_z.arch_kod == POSIX_ARCH_SET_FS, "arch_prctl: kod z RDI");
    check(g_z.arch_adres == 0xC000, "arch_prctl: adres z RSI");

    /* getrandom(buf, len) -> RDI/RSI */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_getrandom, 0x2000, 32), &d) == 32, "getrandom");
    check(g_z.random_bufor == 0x2000 && g_z.random_dlugosc == 32, "getrandom: argumenty");

    /* brk(nowy) -> RDI */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_brk, 0x800000), &d) == 0x800000, "brk");
    check(g_z.brk_wartosc == 0x800000, "brk: wartosc z RDI");

    /* mprotect(addr, len, prot) -> RDI/RSI/RDX */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_mprotect, 0x1000, 0x2000, 3), &d) == 0, "mprotect");
    check(g_z.mprotect_adres == 0x1000 && g_z.mprotect_dlugosc == 0x2000 &&
          g_z.mprotect_ochrona == 3, "mprotect: trzy argumenty");

    /* munmap(addr, len) -> RDI/RSI */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_munmap, 0x3000, 0x4000), &d) == 0, "munmap");
    check(g_z.munmap_adres == 0x3000 && g_z.munmap_dlugosc == 0x4000, "munmap: argumenty");

    /* futex - szesc argumentow, wiec jedyne wywolanie testujace R9 */
    g_z = Zapisane{};
    check(posix_obsluz(u, rama(POSIX_SYS_futex, 0x5000, 0, 1, 0x6000, 0x7000, 0x8000), &d) == 0,
          "futex");
    check(g_z.futex_adres == 0x5000, "futex: uaddr z RDI");
    check(g_z.futex_op == 0, "futex: op z RSI");
    check(g_z.futex_wartosc == 1, "futex: val z RDX");
    check(g_z.futex_timeout == 0x6000, "futex: timeout z R10");
    check(g_z.futex_adres2 == 0x7000, "futex: uaddr2 z R8");
    check(g_z.futex_wartosc3 == 0x8000, "futex: val3 z R9 (szosty rejestr)");

    /* mmap - szesc argumentow; numer w RAX, wiec wszystkie sa dostepne */
    g_z = Zapisane{};
    const int64_t mmap_wynik = posix_obsluz(
        u, rama(POSIX_SYS_mmap, 0x100000, 0x4000, 3, 0x22, uint64_t(-1), 0x9000), &d);
    check(mmap_wynik == 0x40000000, "mmap zwraca adres");
    check(g_z.mmap_adres == 0x100000, "mmap: addr z RDI");
    check(g_z.mmap_dlugosc == 0x4000, "mmap: len z RSI");
    check(g_z.mmap_ochrona == 3, "mmap: prot z RDX");
    check(g_z.mmap_flagi == 0x22, "mmap: flags z R10");
    check(g_z.mmap_fd == -1, "mmap: fd z R8 jako int64 (-1 zachowane)");
    check(g_z.mmap_offset == 0x9000, "mmap: offset z R9");

    /* exit(kod) i exit_group(kod) */
    g_z = Zapisane{};
    posix_obsluz(u, rama(POSIX_SYS_exit, 42), &d);
    check(g_z.wywolan_exit == 1 && g_z.kod_wyjscia == 42, "exit: kod z RDI");
    g_z = Zapisane{};
    posix_obsluz(u, rama(POSIX_SYS_exit_group, 43), &d);
    check(g_z.wywolan_exit == 1 && g_z.kod_wyjscia == 43, "exit_group: kod z RDI");
}

/* ---- 2. Konwencja errno ----------------------------------------------- */

int64_t usl_blad_ok(void*) { return posix_blad(POSIX_EACCES); }
int64_t usl_blad_zly(void*) { return -5000; }     /* spoza zakresu -1..-4095 */
int64_t usl_blad_dodatni(void*) { return 38; }    /* niezanegowane ENOSYS */
int64_t usl_pusty(void*) { return 0; }

void test_errno() {
    PosixUslugi u{};
    PosixDiagnostyka d{};

    /* Nieznany numer -> -ENOSYS, nigdy 0. */
    const int64_t nieznany = posix_obsluz(u, rama(9999), &d);
    check(nieznany == posix_blad(POSIX_ENOSYS), "nieznany numer zwraca -ENOSYS");
    check(nieznany != 0, "nieznany numer nie zwraca 0");
    check(posix_czy_blad(nieznany), "nieznany numer jest bledem wg konwencji");

    /* Wszystkie zadeklarowane numery bez uslug -> -ENOSYS. */
    const uint64_t numery[] = {
        POSIX_SYS_write, POSIX_SYS_getpid, POSIX_SYS_gettid, POSIX_SYS_uname,
        POSIX_SYS_clock_gettime, POSIX_SYS_gettimeofday, POSIX_SYS_arch_prctl,
        POSIX_SYS_getrandom, POSIX_SYS_brk, POSIX_SYS_mmap, POSIX_SYS_munmap,
        POSIX_SYS_mprotect, POSIX_SYS_futex,
    };
    for (uint64_t n : numery) {
        const int64_t w = posix_obsluz(u, rama(n), &d);
        check(w == posix_blad(POSIX_ENOSYS), "brak uslugi -> -ENOSYS");
    }

    /* Blad z uslugi jest propagowany bez zmian, gdy miesci sie w zakresie. */
    u.getpid = usl_blad_ok;
    check(posix_obsluz(u, rama(POSIX_SYS_getpid), &d) == posix_blad(POSIX_EACCES),
          "poprawne -errno jest propagowane");

    /* Bledna wartosc z uslugi nie moze udawac sukcesu ani wyjsc poza zakres. */
    u.getpid = usl_blad_zly;
    check(posix_obsluz(u, rama(POSIX_SYS_getpid), &d) == posix_blad(POSIX_ENOSYS),
          "blad poza zakresem normalizowany do -ENOSYS");
    u.getpid = usl_blad_dodatni;
    check(posix_obsluz(u, rama(POSIX_SYS_getpid), &d) == 38,
          "dodatnia wartosc z uslugi jest sukcesem (kontrakt uslugi)");

    /* Wszystkie errnos w zakresie -1..-4095. */
    const int64_t errnos[] = {POSIX_EPERM, POSIX_ENOENT, POSIX_EIO, POSIX_EAGAIN,
                              POSIX_EFAULT, POSIX_EINVAL, POSIX_ENOSYS,
                              POSIX_ENOTSUP, POSIX_EOVERFLOW};
    for (int64_t e : errnos) {
        check(posix_czy_blad(posix_blad(e)), "errno w zakresie konwencji");
        check(!posix_czy_blad(-(e + 5000)), "errno poza zakresem nie jest bledem");
    }
    check(!posix_czy_blad(0), "0 nie jest bledem");
    check(!posix_czy_blad(5), "5 nie jest bledem");
    check(posix_czy_blad(-1) && posix_czy_blad(-4095), "granice zakresu");
    check(!posix_czy_blad(-4096), "-4096 poza zakresem");

    /* Diagnostyka liczy kategorie. */
    PosixDiagnostyka d2{};
    PosixUslugi puste{};
    posix_obsluz(puste, rama(POSIX_SYS_write), &d2);      /* ENOSYS */
    posix_obsluz(puste, rama(12345), &d2);                 /* ENOSYS */
    PosixUslugi u2{};
    u2.getpid = usl_blad_ok;
    posix_obsluz(u2, rama(POSIX_SYS_getpid), &d2);          /* blad */
    u2.getpid = usl_pusty;
    posix_obsluz(u2, rama(POSIX_SYS_getpid), &d2);          /* sukces */
    check(d2.licznik_wywolan == 4, "diagnostyka: licznik wywolan");
    check(d2.licznik_enosys == 2, "diagnostyka: licznik ENOSYS");
    check(d2.licznik_blad == 1, "diagnostyka: licznik bledow");
    check(d2.ostatnie.numer == POSIX_SYS_getpid && d2.ostatnie.wynik == 0,
          "diagnostyka: ostatnie wywolanie");
}

/* ---- 3. Wartosci skrajne ---------------------------------------------- */

void test_skrajne() {
    const PosixUslugi u = pelne_uslugi();
    PosixDiagnostyka d{};

    /* Deskryptor -1 przeslany jako 0xFFFFFFFF musi byc -1, nie 4294967295. */
    g_z = Zapisane{};
    posix_obsluz(u, rama(POSIX_SYS_write, 0xFFFFFFFFULL, 0, 1), &d);
    check(g_z.fd == -1, "fd -1 zachowane przez rzutowanie");

    /* Duzy offset mmap nie jest obcinany. */
    g_z = Zapisane{};
    const uint64_t duzy_offset = 0x7FFFFFFFF000ULL;
    posix_obsluz(u, rama(POSIX_SYS_mmap, 0, 4096, 3, 0x22, 0, duzy_offset), &d);
    check(g_z.mmap_offset == duzy_offset, "mmap: duzy offset bez obciecia");

    /* UINT64_MAX jako dlugosc dochodzi nietkniete do uslugi. */
    g_z = Zapisane{};
    posix_obsluz(u, rama(POSIX_SYS_write, 1, 0x1000, UINT64_MAX), &d);
    check(g_z.dlugosc == UINT64_MAX, "dlugosc UINT64_MAX bez obciecia");

    /* Numer wywolania UINT64_MAX nie jest bledem wewnetrznym, tylko ENOSYS. */
    check(posix_obsluz(u, rama(UINT64_MAX), &d) == posix_blad(POSIX_ENOSYS),
          "numer UINT64_MAX -> -ENOSYS");

    /* Diagnostyka z nullptr nie crashuje. */
    check(posix_obsluz(u, rama(POSIX_SYS_getpid), nullptr) == 7,
          "nullptr diagnostyka jest dozwolona");
}

/* ---- 4. Nazwy wywolan ------------------------------------------------- */

void test_nazwy() {
    check(std::strcmp(posix_nazwa_wywolania(POSIX_SYS_mmap), "mmap") == 0, "nazwa mmap");
    check(std::strcmp(posix_nazwa_wywolania(POSIX_SYS_futex), "futex") == 0, "nazwa futex");
    check(std::strcmp(posix_nazwa_wywolania(424242), "nieznane") == 0, "nazwa nieznana");
    /* Kazdy zadeklarowany numer ma nazwe inna niz "nieznane". */
    const uint64_t numery[] = {
        POSIX_SYS_read, POSIX_SYS_write, POSIX_SYS_open, POSIX_SYS_close,
        POSIX_SYS_fstat, POSIX_SYS_lseek, POSIX_SYS_mmap, POSIX_SYS_mprotect,
        POSIX_SYS_munmap, POSIX_SYS_brk, POSIX_SYS_rt_sigaction,
        POSIX_SYS_rt_sigprocmask, POSIX_SYS_ioctl, POSIX_SYS_writev,
        POSIX_SYS_getpid, POSIX_SYS_exit, POSIX_SYS_uname, POSIX_SYS_fcntl,
        POSIX_SYS_gettimeofday, POSIX_SYS_getuid, POSIX_SYS_getgid,
        POSIX_SYS_arch_prctl, POSIX_SYS_gettid, POSIX_SYS_futex,
        POSIX_SYS_clock_gettime, POSIX_SYS_exit_group, POSIX_SYS_membarrier,
        POSIX_SYS_getrandom, POSIX_SYS_rseq,
    };
    for (uint64_t n : numery) {
        check(std::strcmp(posix_nazwa_wywolania(n), "nieznane") != 0,
              "kazdy numer ma nazwe");
    }
}

/* ---- 5. Stale ABI ----------------------------------------------------- */

void test_abi() {
    check(POSIX_WEKTOR == 0x80, "wektor bramki to 0x80");
    check((POSIX_FLAGA_BRAMKI & 0x60) == 0x60, "DPL=3 w bramce");
    check((POSIX_FLAGA_BRAMKI & 0x0F) == 0x0E, "typ = 64-bit interrupt gate");
    check((POSIX_FLAGA_BRAMKI & 0x80) != 0, "bramka obecna");
    check(sizeof(PosixUtsname) == 390, "uname ABI");
    check(sizeof(PosixRama) == 80, "rama ABI");
    check(sizeof(PosixTimespec) == 16, "timespec ABI");
    check(sizeof(PosixTimeval) == 16, "timeval ABI");
}

}  // namespace

int main() {
    test_mapowanie();
    test_errno();
    test_skrajne();
    test_nazwy();
    test_abi();

    if (g_failures != 0) {
        std::printf("[POSIX-ABI-HOST] FAIL %d/%d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("[POSIX-ABI-HOST] PASS %d asercji\n", g_checks);
    return 0;
}
