/*
 * Bursztyn OS - uslugi bramki SysV (strona jadra).
 *
 * Petla zaufania jest tu taka sama jak w BWS:
 *
 *   Ring 3 -> int 0x80 -> rama ISR -> tu -> dyspozytor -> usluga -> RAX.
 *
 * Ten plik jest jedynym miejscem, gdzie bramka SysV dotyka podsystemow
 * jadra. Dyspozytor pozostaje przenosny i testowalny na hoscie.
 */
#include "jadro/posix/posix_jadro.h"
#include "jadro/posix/posix_abi.h"

/* Terminal jadra i dziennik szeregowy. */
extern "C" void wypisz_na_ekranie(const char* tekst);
extern "C" void SerialLog(const char* tekst);

#include "jadro/bws/scheduler.h"
#include "jadro/bws/pzb.h"
#include "jadro/mm/pamiec.h"
#include "jadro/mm/tls.h"
#include "jadro/mm/mmap_jadro.h"
#include "sterowniki/czas/hpet.h"
#include "sterowniki/czas/zegar-rtc.h"
#include "entropia.h"

#include <stdint.h>
#include <stddef.h>

/*
 * Wspolne helpery bezpieczenstwa (jadro/bws/bezpieczenstwo.cpp). Kopiuja
 * przez okno SMAP i waliduja zakres strona po stronie.
 */
extern bool skopiuj_z_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool skopiuj_do_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void*, size_t);

extern int grafika_pobierz_szerokosc();
extern int grafika_pobierz_wysokosc();

namespace {

constexpr const char POSIX_SYSNAME[] = "BursztynOS";
constexpr const char POSIX_NODENAME[] = "bursztyn";
constexpr const char POSIX_RELEASE[] = "0.1.0-posix-seed";
constexpr const char POSIX_VERSION[] = "#1 Bursztyn OS SysV seed";
constexpr const char POSIX_MACHINE[] = "x86_64";
constexpr const char POSIX_DOMAIN[] = "localdomain";

PosixDiagnostyka g_diag = {};

/* Kopiuje stala dlugosc z wyrownaniem do NUL, jak robi uname(2). */
void kopiuj_pole(char* cel, size_t pojemnosc, const char* zrodlo, size_t dlugosc) {
    size_t i = 0;
    for (; i < dlugosc && i + 1 < pojemnosc; ++i) cel[i] = zrodlo[i];
    for (; i < pojemnosc - 1; ++i) cel[i] = '\0';
    cel[pojemnosc - 1] = '\0';
}

/* =========================================================================
 * USLUGI
 * ========================================================================= */

int64_t usl_write(void*, int fd, const void* dane, uint64_t dlugosc) {
    /*
     * Na tym etapie obslugiwane sa wylacznie fd 1 i 2 (stdout/stderr).
     * Kazdy inny deskryptor to EBADF - nie zgadujemy, czym mialby byc.
     */
    if (fd != 1 && fd != 2) return posix_blad(POSIX_EBADF);
    if (dlugosc == 0) return 0;
    if (!dane) return posix_blad(POSIX_EFAULT);
    if (dlugosc > 64U * 1024U) return posix_blad(POSIX_EINVAL);
    if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(dane, (size_t)dlugosc))
        return posix_blad(POSIX_EFAULT);

    /*
     * Tekst jest wypisywany przez ten sam terminal jadra co BWS 1, wiec
     * wyjscie programu SysV trafia dokladnie tam, gdzie wyjscie .bur.
     */
    char bufor[256];
    const uint8_t* zrodlo = static_cast<const uint8_t*>(dane);
    uint64_t zapisane = 0;
    while (zapisane < dlugosc) {
        const uint64_t porcja = (dlugosc - zapisane) < (sizeof(bufor) - 1)
                                    ? (dlugosc - zapisane)
                                    : (sizeof(bufor) - 1);
        if (!skopiuj_z_przestrzeni_uzytkownika(bufor, zrodlo + zapisane, (size_t)porcja))
            return posix_blad(POSIX_EFAULT);
        bufor[porcja] = '\0';
        SerialLog(bufor);
        wypisz_na_ekranie(bufor);
        zapisane += porcja;
    }
    return static_cast<int64_t>(zapisane);
}

void usl_exit(void*, int kod) {
    (void)kod;
    zakoncz_aktualny_proces();
}

int64_t usl_getpid(void*) {
    return aktualny_pid > 0 ? static_cast<int64_t>(aktualny_pid) : 1;
}

int64_t usl_gettid(void*) {
    /* Jednowątkowy model: tid == pid. */
    return aktualny_pid > 0 ? static_cast<int64_t>(aktualny_pid) : 1;
}

int64_t usl_uname(void*, uint64_t bufor) {
    if (!bufor) return posix_blad(POSIX_EFAULT);
    PosixUtsname u{};
    kopiuj_pole(u.sysname, sizeof(u.sysname), POSIX_SYSNAME, sizeof(POSIX_SYSNAME) - 1);
    kopiuj_pole(u.nodename, sizeof(u.nodename), POSIX_NODENAME, sizeof(POSIX_NODENAME) - 1);
    kopiuj_pole(u.release, sizeof(u.release), POSIX_RELEASE, sizeof(POSIX_RELEASE) - 1);
    kopiuj_pole(u.version, sizeof(u.version), POSIX_VERSION, sizeof(POSIX_VERSION) - 1);
    kopiuj_pole(u.machine, sizeof(u.machine), POSIX_MACHINE, sizeof(POSIX_MACHINE) - 1);
    kopiuj_pole(u.domainname, sizeof(u.domainname), POSIX_DOMAIN, sizeof(POSIX_DOMAIN) - 1);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor), sizeof(u)))
        return posix_blad(POSIX_EFAULT);
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor), &u, sizeof(u)))
        return posix_blad(POSIX_EFAULT);
    return 0;
}

int64_t usl_clock_gettime(void*, int clock_id, uint64_t bufor_ts) {
    if (!bufor_ts) return posix_blad(POSIX_EFAULT);
    if (clock_id != POSIX_CLOCK_REALTIME && clock_id != POSIX_CLOCK_MONOTONIC)
        return posix_blad(POSIX_EINVAL);

    PosixTimespec ts{};
    if (clock_id == POSIX_CLOCK_MONOTONIC && hpet_dostepny()) {
        const uint64_t ns = czas_monotoniczny_ns();
        ts.tv_sec = static_cast<int64_t>(ns / 1000000000ULL);
        ts.tv_nsec = static_cast<int64_t>(ns % 1000000000ULL);
    } else {
        czas_rtc rtc{};
        pobierz_czas_rtc(&rtc);
        /* RTC daje tylko sekundy - dokladnie tyle, ile ma sprzet. */
        ts.tv_sec = static_cast<int64_t>(rtc.sekundy) +
                    static_cast<int64_t>(rtc.minuty) * 60 +
                    static_cast<int64_t>(rtc.godziny) * 3600;
        ts.tv_nsec = 0;
    }
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor_ts), sizeof(ts)))
        return posix_blad(POSIX_EFAULT);
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor_ts), &ts, sizeof(ts)))
        return posix_blad(POSIX_EFAULT);
    return 0;
}

int64_t usl_gettimeofday(void*, uint64_t bufor_tv, uint64_t bufor_tz) {
    /* Strefa czasowa nie jest wspierana: bufor_tz musi byc NULL. */
    if (bufor_tz) return posix_blad(POSIX_EINVAL);
    if (!bufor_tv) return posix_blad(POSIX_EFAULT);
    PosixTimeval tv{};
    if (hpet_dostepny()) {
        const uint64_t ns = czas_monotoniczny_ns();
        tv.tv_sec = static_cast<int64_t>(ns / 1000000000ULL);
        tv.tv_usec = static_cast<int64_t>((ns % 1000000000ULL) / 1000ULL);
    }
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor_tv), sizeof(tv)))
        return posix_blad(POSIX_EFAULT);
    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(bufor_tv), &tv, sizeof(tv)))
        return posix_blad(POSIX_EFAULT);
    return 0;
}

int64_t usl_arch_prctl(void*, int kod, uint64_t adres) {
    /*
     * %fs jest przelaczany przy zmianie kontekstu (jadro/mm/tls.cpp), wiec
     * SET_FS/GET_FS maja realne znaczenie: to one ustanawiaja TLS, na ktorym
     * stoi errno i pthreads kazdej libc.
     *
     * %gs jest uzywany przez jadro jako wskaznik danych per-CPU (bramka BWS
     * czyta z niego szczyt stosu jadra), wiec ARCH_SET_GS z Ring 3 jest
     * ODMOWIONY. Inaczej aplikacja podmienilaby sobie stos jadra przy
     * nastepnym SYSCALL.
     */
    switch (kod) {
        case POSIX_ARCH_SET_FS: {
            if (!tls_ustaw_baze(aktualny_pid, adres)) return posix_blad(POSIX_EPERM);
            /*
             * Ustawienie dziala natychmiast, nie przy nastepnym przelaczeniu:
             * proces wlasnie teraz chce uzywac %fs (np. po arch_prctl w crt1).
             */
            tls_przelacz(aktualny_pid);
            return 0;
        }
        case POSIX_ARCH_GET_FS: {
            const uint64_t baza = tls_pobierz_baze(aktualny_pid);
            if (!adres) return posix_blad(POSIX_EFAULT);
            if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(adres), sizeof(baza)))
                return posix_blad(POSIX_EFAULT);
            if (!skopiuj_do_przestrzeni_uzytkownika(
                    reinterpret_cast<void*>(adres), &baza, sizeof(baza)))
                return posix_blad(POSIX_EFAULT);
            return 0;
        }
        case POSIX_ARCH_SET_GS:
        case POSIX_ARCH_GET_GS:
            /* %gs nalezy do jadra (dane per-CPU) - niedostepne z Ring 3. */
            return posix_blad(POSIX_EPERM);
        default:
            return posix_blad(POSIX_EINVAL);
    }
}

int64_t usl_getrandom(void*, uint64_t bufor, uint64_t dlugosc) {
    if (dlugosc == 0) return 0;
    if (!bufor) return posix_blad(POSIX_EFAULT);
    /* Limit: jedno wywolanie nie moze zablokowac jadra na dlugo. */
    if (dlugosc > 4096U) return posix_blad(POSIX_EINVAL);
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(bufor), (size_t)dlugosc))
        return posix_blad(POSIX_EFAULT);

    uint8_t tymczasowy[256];
    uint64_t zapisane = 0;
    while (zapisane < dlugosc) {
        const uint64_t porcja = (dlugosc - zapisane) < sizeof(tymczasowy)
                                    ? (dlugosc - zapisane)
                                    : sizeof(tymczasowy);
        if (!entropia_pobierz_bajty(tymczasowy, (size_t)porcja))
            return posix_blad(POSIX_EIO);
        if (!skopiuj_do_przestrzeni_uzytkownika(
                reinterpret_cast<void*>(bufor + zapisane), tymczasowy, (size_t)porcja))
            return posix_blad(POSIX_EFAULT);
        zapisane += porcja;
    }
    return static_cast<int64_t>(zapisane);
}

int64_t usl_brk(void*, uint64_t nowy_break) {
    /*
     * brk jest potrzebny kazdej libc, ale jego poprawna implementacja wymaga
     * demand pagingu (punkt 2.18). Do tego czasu: zapytanie z wartoscia 0
     * zwraca biezaca granice sterty, a proba jej przesuniecia -ENOMEM.
     * Aplikacja dostaje spojna semantyke zamiast cichego sukcesu bez pamieci.
     */
    if (nowy_break == 0) {
        return aktualny_pid > 0
                   ? static_cast<int64_t>(tablica_procesow[aktualny_pid].granica_sterty)
                   : 0;
    }
    return posix_blad(POSIX_ENOMEM);
}

int64_t usl_mmap(void*, uint64_t adres, uint64_t dlugosc, uint64_t ochrona,
                 uint64_t flagi, int64_t fd, uint64_t offset) {
    int64_t blad = 0;
    const uint64_t wynik = mmap_jadro_mapuj(aktualny_pid, adres, dlugosc, ochrona,
                                            flagi, fd, offset, &blad);
    if (wynik != 0) return static_cast<int64_t>(wynik);
    /* mmap_jadro_mapuj zwraca blad juz jako -errno. */
    return blad ? blad : posix_blad(POSIX_ENOMEM);
}

int64_t usl_munmap(void*, uint64_t adres, uint64_t dlugosc) {
    if (adres == 0 || dlugosc == 0) return posix_blad(POSIX_EINVAL);
    return mmap_jadro_odmapuj(aktualny_pid, adres, dlugosc);
}

int64_t usl_mprotect(void*, uint64_t adres, uint64_t dlugosc, uint64_t ochrona) {
    if (adres == 0 || dlugosc == 0) return posix_blad(POSIX_EINVAL);
    return mmap_jadro_ochrona(aktualny_pid, adres, dlugosc, ochrona);
}

int64_t usl_futex(void*, uint64_t adres, uint64_t op, uint64_t wartosc,
                  uint64_t timeout, uint64_t adres2, uint64_t wartosc3) {
    (void)op; (void)wartosc; (void)timeout; (void)adres2; (void)wartosc3;
    if (adres == 0) return posix_blad(POSIX_EINVAL);
    /*
     * FUTEX_WAIT bez schedulera watkow zablokowalby caly system: nie ma
     * jeszcze watkow (punkt 2.17). -ENOSYS jest poprawna odpowiedzia i
     * pthread_mutex fallbackuje na spinlock z yield.
     */
    return posix_blad(POSIX_ENOSYS);
}

PosixUslugi zbuduj_uslugi() {
    PosixUslugi u{};
    u.write = usl_write;
    u.exit_proces = usl_exit;
    u.getpid = usl_getpid;
    u.gettid = usl_gettid;
    u.uname = usl_uname;
    u.clock_gettime = usl_clock_gettime;
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

}  // namespace

bool posix_bramka_dostepna(const proces_t& proces) {
    /* Kazdy proces Ring 3 moze wejsc bramka; piaskownica ma zawezony zestaw. */
    return proces.pid > 0 &&
           proces.pid < static_cast<uint64_t>(MAKS_PROCESOW) &&
           proces.poziom_zaufania <= PZB_PIASKOWNICA;
}

uint64_t posix_obsluz_bramke(RejestryStanowe* rama) {
    if (!rama) return static_cast<uint64_t>(-POSIX_EFAULT);

    PosixRama r{};
    /*
     * Uklad RejestryStanowe jest udokumentowanym ABI miedzy przerwania.S,
     * idt.cpp i schedulerem. Odczytujemy dokladnie te pola, ktore przenosi
     * ABI SysV; RCX i R11 nie sa potrzebne, bo `int 0x80` ich nie niszczy.
     */
    r.numer = rama->rax;
    r.a1 = rama->rdi;
    r.a2 = rama->rsi;
    r.a3 = rama->rdx;
    r.a4 = rama->r10;
    r.a5 = rama->r8;
    r.a6 = rama->r9;
    r.rip = rama->adres_powrotu;
    r.rsp = rama->stary_rsp;
    r.cs = rama->rejestr_cs;

    const PosixUslugi uslugi = zbuduj_uslugi();
    const int64_t wynik = posix_obsluz(uslugi, r, &g_diag);
    return static_cast<uint64_t>(wynik);
}

const PosixDiagnostyka& posix_diagnostyka() {
    return g_diag;
}

void posix_zwolnij_proces(int pid) {
    /*
     * Kazda tablica per-PID musi byc zerowana przy recyklingu, inaczej nowy
     * proces odziedziczylby cudze adresy mmap albo blok TLS.
     */
    mmap_zwolnij_proces(pid);
    tls_zwolnij_proces(pid);
}
